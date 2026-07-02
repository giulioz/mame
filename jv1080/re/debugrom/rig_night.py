#!/usr/bin/env python3
"""Night session (GENTLE PACING): accumulator count + pipeline + sentinel store redo.
Rules: pace 18ms/poke, 60ms between reads, 0.6s settle after programming, 2s idle between
phases, ping checkpoint per phase (fail -> 25s cooldown -> retry; 2 strikes -> STOP ALL).
Fresh boot state assumed -> one full reset first."""
import sys, time
from xp_manual_eq import XP
from jvdebug import ping as PING

xp = XP(pace=0.018)
def log(s): print(s, flush=True)

strikes = [0]
def checkpoint(tag):
    time.sleep(0.5)
    if xp.jv.xfer(PING(), timeout=3.0):
        log(f"[ping {tag}: ok]"); strikes[0] = 0; return True
    log(f"[ping {tag}: no reply - 25s cooldown]")
    time.sleep(25)
    if xp.jv.xfer(PING(), timeout=4.0):
        log(f"[ping {tag}: recovered]"); strikes[0] = 0; return True
    strikes[0] += 1
    log(f"[ping {tag}: STRIKE {strikes[0]}]")
    if strikes[0] >= 2:
        log("!! two strikes - STOPPING ALL HARDWARE for the night")
        sys.exit(1)
    return False

fails = [0]
def rd_entry(e):
    time.sleep(0.06)
    if e < 64:    off, w = 0x3000 + e*4, 4
    elif e < 128: off, w = 0x3100 + (e-64)*4, 4
    elif e < 192: off, w = 0x3200 + (e-128)*4, 4
    else:         off, w = 0x3300 + (e-192)*2, 2
    try:
        v = xp.jv.read_dsp(off, w)
        if v is not None:
            fails[0] = 0
            return v & 0xffffff
    except Exception:
        pass
    fails[0] += 1
    if fails[0] >= 2:
        log("!! 2 consecutive read fails - cooldown 20s, no retry of the reads")
        time.sleep(20)
        fails[0] = 0
    return None

def poke_slot(i, pram, cram=None):
    xp.poke(0x3400 + i*4, 4, pram)
    if cram is not None:
        xp.poke(0x2c00 + i*2, 2, cram)

def clear_slots(n=10):
    for i in range(n): poke_slot(i, 0, 0)

def wr_entry(e, v):
    if e < 64:    xp.poke(0x3000 + e*4, 4, v)
    elif e < 128: xp.poke(0x3100 + (e-64)*4, 4, v)
    elif e < 192: xp.poke(0x3200 + (e-128)*4, 4, v)
    else:         xp.poke(0x3300 + (e-192)*2, 2, v)

def lo16(st, b13, b12, k, col):
    return (st << 14) | (b13 << 13) | (b12 << 12) | ((k & 0x3f) << 6) | (col & 0x3f)

def show(d):
    return " ".join(f"[{e}]={'??' if v is None else f'{v:06x}'}" for e, v in d.items())

log("=== NIGHT: full reset (fresh boot state) ===")
xp.reset()
time.sleep(1.0)
checkpoint("post-reset")

# ---------- N1: accumulator count -- b12-select test ----------
log("=== N1: dual-acc? loads/stores with b12 0 vs 1 ===")
clear_slots(10)
for e in (5, 9, 69, 73): wr_entry(e, 0)
poke_slot(0, lo16(0, 0, 0, 0, 0x1F), 0x0111)   # load-A? (b12=0) 0x111
poke_slot(2, lo16(0, 0, 1, 0, 0x1F), 0x0222)   # load-B? (b12=1) 0x222
poke_slot(4, lo16(3, 0, 0, 5, 0), 0x0000)      # store b12=0 -> [5]/[69]
poke_slot(6, lo16(3, 0, 1, 9, 0), 0x0000)      # store b12=1 -> [9]/[73]
time.sleep(0.6)
r = {e: rd_entry(e) for e in (5, 69, 9, 73)}
log(f"  N1: {show(r)}")
log( "      single acc -> [5]=[9]=000222 ; b12-selected accs -> [5]=000111,[9]=000222")
checkpoint("N1")
time.sleep(2)

# ---------- N2: slot-parity accs? ----------
log("=== N2: dual-acc? adjacent loads (even/odd slots), both b12=0 ===")
clear_slots(10)
for e in (5, 9, 69, 73): wr_entry(e, 0)
poke_slot(0, lo16(0, 0, 0, 0, 0x1F), 0x0111)   # load 0x111 (even slot)
poke_slot(1, lo16(0, 0, 0, 0, 0x1F), 0x0222)   # load 0x222 (odd slot)
poke_slot(4, lo16(3, 0, 0, 5, 0), 0x0000)      # store at even slot
poke_slot(5, lo16(3, 0, 0, 9, 0), 0x0000)      # store at odd slot
time.sleep(0.6)
r = {e: rd_entry(e) for e in (5, 69, 9, 73)}
log(f"  N2: {show(r)}")
log( "      single acc -> both 000222 ; slot-parity accs -> [5]=000111,[9]=000222")
checkpoint("N2")
time.sleep(2)

# ---------- N3: pipeline latency map ----------
log("=== N3: pipeline: loads at 0(0x111) and 4(0x222); stores at 1,2,3,5,6,8 ===")
clear_slots(10)
kmap = {1: 11, 2: 12, 3: 13, 5: 14, 6: 15, 8: 16}
for k in kmap.values(): wr_entry(k, 0); wr_entry(k+64, 0)
poke_slot(0, lo16(0, 0, 0, 0, 0x1F), 0x0111)
poke_slot(4, lo16(0, 0, 0, 0, 0x1F), 0x0222)
for s, k in kmap.items():
    poke_slot(s, lo16(3, 0, 0, k, 0), 0x0000)
time.sleep(0.6)
r = {f"slot{s}(k{k})": rd_entry(k) for s, k in kmap.items()}
log("  N3: " + " ".join(f"{n}={'??' if v is None else f'{v:06x}'}" for n, v in r.items()))
log( "      store@s sees acc after slot (s - latency); 0x111 between loads, 0x222 after slot4+lat")
checkpoint("N3")
time.sleep(2)

# ---------- N4: sentinel store redo (slot-3 anomaly) ----------
log("=== N4: sentinel redo of slot-3 store + adjacent stores ===")
clear_slots(10)
for e in (5, 6, 69, 70): wr_entry(e, 0x5A5A5A)
poke_slot(0, lo16(0, 0, 0, 0, 0x1F), 0x0123)
poke_slot(3, lo16(3, 0, 0, 6, 0), 0x0000)      # lone store at slot 3
time.sleep(0.6)
r = {e: rd_entry(e) for e in (6, 70)}
log(f"  N4a lone store@slot3: {show(r)}  (sentinel 5a5a5a = no write; 000123 = wrote)")
for e in (5, 6, 69, 70): wr_entry(e, 0x5A5A5A)
poke_slot(2, lo16(3, 0, 0, 5, 0), 0x0000)      # adjacent stores at 2 AND 3
time.sleep(0.6)
r = {e: rd_entry(e) for e in (5, 6, 69, 70)}
log(f"  N4b adjacent 2+3:     {show(r)}")
checkpoint("N4")
time.sleep(2)

# ---------- N5: the E2 anomaly recheck (col 0x1F with k!=0) ----------
log("=== N5: col 0x1F k-sensitivity (test cram=0x100; capture k=9) ===")
for kk in (0, 5):
    clear_slots(10)
    for e in (9, 73): wr_entry(e, 0x5A5A5A)
    poke_slot(0, lo16(0, 0, 0, 0, 0x1F), 0x0555)
    poke_slot(2, lo16(0, 0, 0, kk, 0x1F), 0x0100)
    poke_slot(4, lo16(3, 0, 0, 9, 0), 0x0000)
    time.sleep(0.6)
    v = rd_entry(9)
    log(f"  N5 k={kk}: capture={'??' if v is None else f'{v:06x}'}   (expected 000100 if pure const load)")
    checkpoint(f"N5k{kk}")
    time.sleep(1.5)

clear_slots(10)
log("=== night session phase 1 done ===")
