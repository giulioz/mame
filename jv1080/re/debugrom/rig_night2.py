#!/usr/bin/env python3
"""Night phase 2 (gentle): P2a where does the b13=0,b12=1 store land (full scan);
P2b is entry 6 hardware-driven (sentinel, no program); P2c column sweep completion."""
import sys, time
from xp_manual_eq import XP
from jvdebug import ping as PING

xp = XP(pace=0.018)
def log(s): print(s, flush=True)

strikes = [0]
def checkpoint(tag):
    time.sleep(0.5)
    if xp.jv.xfer(PING(), timeout=3.0):
        log(f"[ping {tag}: ok]"); return True
    log(f"[ping {tag}: no reply - 25s cooldown]")
    time.sleep(25)
    if xp.jv.xfer(PING(), timeout=4.0):
        log(f"[ping {tag}: recovered]"); return True
    strikes[0] += 1
    if strikes[0] >= 2:
        log("!! two strikes - STOP"); sys.exit(1)
    return False

fails = [0]
def rd_entry(e, pause=0.05):
    time.sleep(pause)
    if e < 64:    off, w = 0x3000 + e*4, 4
    elif e < 128: off, w = 0x3100 + (e-64)*4, 4
    elif e < 192: off, w = 0x3200 + (e-128)*4, 4
    else:         off, w = 0x3300 + (e-192)*2, 2
    try:
        v = xp.jv.read_dsp(off, w)
        if v is not None:
            fails[0] = 0; return v & 0xffffff
    except Exception:
        pass
    fails[0] += 1
    if fails[0] >= 2:
        log("!! 2 consecutive read fails - 20s cooldown"); time.sleep(20); fails[0] = 0
    return None

def poke_slot(i, pram, cram=None):
    xp.poke(0x3400 + i*4, 4, pram)
    if cram is not None: xp.poke(0x2c00 + i*2, 2, cram)

def clear_slots(n=10):
    for i in range(n): poke_slot(i, 0, 0)

def wr_entry(e, v):
    if e < 64:    xp.poke(0x3000 + e*4, 4, v)
    elif e < 128: xp.poke(0x3100 + (e-64)*4, 4, v)
    elif e < 192: xp.poke(0x3200 + (e-128)*4, 4, v)
    else:         xp.poke(0x3300 + (e-192)*2, 2, v)

def lo16(st, b13, b12, k, col):
    return (st << 14) | (b13 << 13) | (b12 << 12) | ((k & 0x3f) << 6) | (col & 0x3f)

# ---------- P2b FIRST (cheapest): is entry 6 hardware-driven? ----------
log("=== P2b: sentinel WITHOUT any program (is entry 6 hw-driven?) ===")
clear_slots(10)
for e in (5, 6, 69, 70): wr_entry(e, 0x5A5A5A)
time.sleep(0.8)
r = {e: rd_entry(e) for e in (5, 6, 69, 70)}
log("  no-program sentinels: " + " ".join(f"[{e}]={'??' if v is None else f'{v:06x}'}" for e, v in r.items()))
log("  (5A5A5A everywhere = nothing hw-driven; [6]/[70]=0 = hw-driven port)")
checkpoint("P2b")
time.sleep(2)

# ---------- P2a: full scan for the b13=0,b12=1 store ----------
log("=== P2a: lone store st3 b13=0 b12=1 k=9 (0xD240), acc=0x333 -> full scan ===")
poke_slot(0, lo16(0, 0, 0, 0, 0x1F), 0x0333)
poke_slot(2, lo16(3, 0, 1, 9, 0), 0x0000)
time.sleep(0.8)
nz = []
for e in range(256):
    v = rd_entry(e, pause=0.03)
    if v: nz.append((e, v))
log("  nonzero entries: " + (" ".join(f"[{e}]={v:06x}" for e, v in nz) or "NONE"))
clear_slots(4)
checkpoint("P2a")
time.sleep(2)

# ---------- P2c: column sweep completion (acc=0x321, cram=0x0123) ----------
log("=== P2c: column sweep 0x00-0x3F (skip known), capture k=5 ===")
KNOWN = {0x00,0x01,0x02,0x03,0x04,0x05,0x07,0x08,0x0F,0x18,0x1F,0x2F,0x3F}
clear_slots(10)
wr_entry(5, 0); wr_entry(69, 0)
poke_slot(0, lo16(0, 0, 0, 0, 0x1F), 0x0321)
poke_slot(4, lo16(3, 0, 0, 5, 0), 0x0000)
xp.poke(0x2c00 + 2*2, 2, 0x0123)     # CRAM[2] for the test slot
results = {}
for col in range(0x40):
    if col in KNOWN and col not in (0x0F, 0x1F, 0x3F):   # re-run 3 knowns as controls
        continue
    poke_slot(2, lo16(0, 0, 0, 0, col))
    time.sleep(0.25)
    v = rd_entry(5)
    results[col] = v
    log(f"  col 0x{col:02x}: {'??' if v is None else f'{v:06x}'}")
    if col % 8 == 7:
        if not xp.jv.xfer(PING(), timeout=3.0):
            log("  [mid-sweep ping failed - 20s cooldown]"); time.sleep(20)
clear_slots(10)
checkpoint("P2c")
log("=== phase 2 done ===")
