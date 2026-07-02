#!/usr/bin/env python3
"""MORNING SCRIPT (run after power-cycling the JV if needed).
Completes the night's unfinished experiments with maximum link care:
  M1: column sweep 0x00-0x3F (the big remaining table; 150ms read spacing, reconnect ladder)
  M2: N1-combination retest (does a b12=1 LOAD disable a coexisting b12=1 STORE?)
  M3: cram-steered store map (b13=1 b12=1 destinations vs CRAM value)
Recovery ladder on failure: 20s idle -> ping; fresh JV() reopen -> ping; 60s idle -> reopen;
only then stop. READS are spaced 150ms; a batch pause every 8 ops.
"""
import sys, time
import xp_manual_eq as M
from jvdebug import ping as PING

def connect():
    return M.XP(pace=0.02)

xp = connect()
def log(s): print(s, flush=True)

def reconnect():
    global xp
    for wait in (20, 60):
        log(f"  [reconnect: {wait}s idle then fresh ports]")
        time.sleep(wait)
        try:
            xp = connect()
            log("  [reconnected]")
            return True
        except Exception as e:
            log(f"  [reconnect failed: {e}]")
    return False

fails = [0]
def rd_entry(e):
    time.sleep(0.15)
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
        fails[0] = 0
        if not reconnect():
            log("!! link dead - STOP"); sys.exit(1)
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

def s24(v): return v - 0x1000000 if v & 0x800000 else v

log("=== MORNING: full reset ===")
xp.reset()
time.sleep(1.0)

# ---------- M1: column sweep ----------
log("=== M1: column sweep (acc=0x321, cram=0x0123, capture k=5) ===")
clear_slots(10)
wr_entry(5, 0); wr_entry(69, 0)
poke_slot(0, lo16(0, 0, 0, 0, 0x1F), 0x0321)
poke_slot(4, lo16(3, 0, 0, 5, 0), 0x0000)
xp.poke(0x2c00 + 2*2, 2, 0x0123)
for col in range(0x40):
    poke_slot(2, lo16(0, 0, 0, 0, col))
    time.sleep(0.3)
    v = rd_entry(5)
    log(f"  col 0x{col:02x}: {'??' if v is None else f'{v:06x} ({s24(v):+9d})'}")
    if col % 8 == 7:
        time.sleep(1.0)
clear_slots(10)
time.sleep(2)

# ---------- M2: N1-combination retest ----------
log("=== M2: b12=1 load + b12=1 store coexistence (the N1 anomaly) ===")
for desc, l2 in (("with b12=1 load", lo16(0, 0, 1, 0, 0x1F)),
                 ("with b12=0 load", lo16(0, 0, 0, 0, 0x1F))):
    clear_slots(10)
    for e in (5, 9, 69, 73): wr_entry(e, 0x5A5A5A)
    poke_slot(0, lo16(0, 0, 0, 0, 0x1F), 0x0111)
    poke_slot(2, l2, 0x0222)
    poke_slot(4, lo16(3, 0, 0, 5, 0), 0x0000)
    poke_slot(6, lo16(3, 0, 1, 9, 0), 0x0000)
    time.sleep(0.6)
    r = {e: rd_entry(e) for e in (5, 69, 9, 73)}
    log(f"  {desc}: " + " ".join(f"[{e}]={'??' if v is None else f'{v:06x}'}" for e, v in r.items()))
clear_slots(10)
time.sleep(2)

# ---------- M3: cram-steered store destinations ----------
log("=== M3: b13=1 b12=1 store dest vs CRAM (k=15, acc=0x333) ===")
probe = list(range(128, 160)) + list(range(32, 48)) + [192+14, 192+15, 192+16]
poke_slot(0, lo16(0, 0, 0, 0, 0x1F), 0x0333)
for cram in (0x0000, 0x0001, 0x0010, 0x0080, 0x0100, 0x1000, 0x4000, 0x8000):
    for e in probe: wr_entry(e, 0)
    poke_slot(2, lo16(3, 1, 1, 15, 0), cram)
    time.sleep(0.5)
    hits = [(e, rd_entry(e)) for e in probe]
    hits = [(e, v) for e, v in hits if v]
    log(f"  cram={cram:04x}: " + (" ".join(f"[{e}]={v:06x}" for e, v in hits) or "no hits"))
    poke_slot(2, 0, 0)
    time.sleep(1.0)
clear_slots(10)
log("=== morning session done ===")
