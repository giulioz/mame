#!/usr/bin/env python3
"""Rig session 1: E0 sanity+pipeline, E1 bit12 A/B, E2 st-verb A/B.
Safety: single-attempt reads, abort session on 2 consecutive failures (XP_FACTS C11);
no notes, no PC, 0x3916 untouched. Full xp.reset() once at start (user instruction);
accumulators persist between runs -> every program begins with a col-0x1F load.
"""
import sys, time, json
from xp_manual_eq import XP

xp = XP()                      # UM-ONE; PINGs in constructor
LOG = open("rig_session1.log", "w")
def log(*a):
    s = " ".join(str(x) for x in a)
    print(s, flush=True); LOG.write(s + "\n"); LOG.flush()

fails = [0]
def rd_entry(e):
    """Read contiguous IRAM entry 0-255 (single attempt, abort on 2 consecutive fails)."""
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
        log("!! 2 consecutive read failures - ABORT (power-cycle may be needed)")
        sys.exit(1)
    return None

def poke_slot(i, pram, cram=None):
    xp.poke(0x3400 + i*4, 4, pram)
    if cram is not None:
        xp.poke(0x2c00 + i*2, 2, cram)

def clear_slots(n=8):
    for i in range(n):
        poke_slot(i, 0, 0)

def clear_entries(es):
    for e in es:
        if e < 64:    xp.poke(0x3000 + e*4, 4, 0)
        elif e < 128: xp.poke(0x3100 + (e-64)*4, 4, 0)
        elif e < 192: xp.poke(0x3200 + (e-128)*4, 4, 0)
        else:         xp.poke(0x3300 + (e-192)*2, 2, 0)

def lo16(st, b13, b12, k, col):
    return (st << 14) | (b13 << 13) | (b12 << 12) | ((k & 0x3f) << 6) | (col & 0x3f)

# ---------------- E0: full reset + sanity + pipeline ----------------
log("=== E0: reset + sanity + store pipeline ===")
xp.reset()          # zero everything + exec config (the one full reset)
time.sleep(0.3)
z = [rd_entry(e) for e in (0, 5, 64, 69, 128)]
log("post-reset entries 0/5/64/69/128:", [f"{v:06x}" if v is not None else "??" for v in z])

# program: [load 0x123] then stores at slots 2..5 to k=5,6,7,8 (dual expected)
poke_slot(0, 0x001F, 0x0123)                     # st0 col 0x1F load const
for s, k in ((2, 5), (3, 6), (4, 7), (5, 8)):
    poke_slot(s, lo16(3, 0, 0, k, 0), 0x0000)    # st3 b13=0 store
time.sleep(0.3)
for k in (5, 6, 7, 8):
    a, b = rd_entry(k), rd_entry(k + 64)
    log(f"  store@k={k}: iram[{k}]={a:06x} iram[{k+64}]={b:06x}  (want 000123/000123)")

# ---------------- E1: bit12 A/B ----------------
log("=== E1: bit12 A/B (b13=1, cram=0, k=15: old-opE vs old-opF) ===")
clear_slots(8); clear_entries([5, 6, 7, 8, 69, 70, 71, 72])
for name, b12 in (("E (b12=0)", 0), ("F (b12=1)", 1)):
    clear_entries([15, 47, 79, 111, 143, 15+192])
    poke_slot(0, 0x001F, 0x0321)                 # load 0x321
    poke_slot(2, lo16(3, 1, b12, 15, 0), 0x0000) # st3 b13=1 k=15 cram=0
    time.sleep(0.3)
    got = {e: rd_entry(e) for e in (15, 47, 79, 111, 143, 15+192)}
    log(f"  {name}: " + " ".join(f"[{e}]={v:06x}" for e, v in got.items() if v))
    nz = [e for e, v in got.items() if v]
    log(f"  {name}: nonzero at {nz}  (bit12-dead prediction: only [47])")
    clear_slots(4)

# ---------------- E2: st-verb A/B (identical addr14+cram under st=0/1/2/3) ----------------
log("=== E2: st verbs (k=5, col=0x1F 'load const' 0x0100; capture acc at k=9) ===")
# For each st, run: [load 0x555] [TEST: st=X b13=0 b12=0 k=5 col=0x1F cram=0x0100] [store st3 k=9]
# Then read: acc-capture iram[9]/[73]; side-writes at 5/69/37/101; and a full scan for st1/st2.
for st in (0, 1, 2, 3):
    clear_slots(6)
    clear_entries([5, 9, 37, 69, 73, 101, 133, 137, 197, 201])
    poke_slot(0, 0x001F, 0x0555)                 # load 0x555
    poke_slot(2, lo16(st, 0, 0, 5, 0x1F), 0x0100)  # TEST instruction
    poke_slot(4, lo16(3, 0, 0, 9, 0), 0x0000)    # capture acc -> iram[9]/[73]
    time.sleep(0.3)
    acc = rd_entry(9)
    side = {e: rd_entry(e) for e in (5, 69, 37, 101, 133, 197)}
    log(f"  st={st}: acc-after-test={acc:06x} (0x555=keep, 0x100=loaded)  side-writes: " +
        (" ".join(f"[{e}]={v:06x}" for e, v in side.items() if v) or "none"))

# full scan for the exotic ones: st=1 and st=2 test instruction alone
for st in (1, 2):
    clear_slots(6)
    poke_slot(0, 0x001F, 0x0444)                 # load 0x444
    poke_slot(2, lo16(st, 0, 0, 5, 0x1F), 0x0100)
    time.sleep(0.3)
    log(f"  full-scan with only st={st} test present (looking for ANY write):")
    nz = []
    for e in range(256):
        v = rd_entry(e)
        if v: nz.append((e, v))
    log(f"    nonzero entries: " + (" ".join(f"[{e}]={v:06x}" for e, v in nz) or "none"))

clear_slots(8)
log("=== session 1 done ===")
json.dump({"done": 1}, open("rig_session1.done.json", "w"))
