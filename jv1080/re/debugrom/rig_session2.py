#!/usr/bin/env python3
"""Rig session 2: E3 full column sweep (2 cram flavors), E5 wrap test, E4 cram-steered stores.
Assumes session 1 left the DSP zeroed except exec config. No full reset here.
"""
import sys, time, json
from xp_manual_eq import XP

xp = XP()
LOG = open("rig_session2.log", "w")
def log(*a):
    s = " ".join(str(x) for x in a)
    print(s, flush=True); LOG.write(s + "\n"); LOG.flush()

fails = [0]
def rd_entry(e):
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
        log("!! 2 consecutive read failures - ABORT")
        sys.exit(1)
    return None

def poke_slot(i, pram, cram=None):
    xp.poke(0x3400 + i*4, 4, pram)
    if cram is not None:
        xp.poke(0x2c00 + i*2, 2, cram)

def clear_slots(n=8):
    for i in range(n):
        poke_slot(i, 0, 0)

def lo16(st, b13, b12, k, col):
    return (st << 14) | (b13 << 13) | (b12 << 12) | ((k & 0x3f) << 6) | (col & 0x3f)

def s24(v): return v - 0x1000000 if v & 0x800000 else v

# ---------------- E3: full column sweep ----------------
# [slot0: load 0x321][slot2: TEST col X, cram C][slot4: store st3 k=5]
# capture iram[5]. Two flavors: C=0x0123 (int 291 / C14 +0.0355) and C=0x2000 (C14 -1.0).
log("=== E3: column sweep, acc=0x321 ===")
KNOWN = {0x00,0x01,0x02,0x03,0x04,0x05,0x07,0x08,0x0F,0x18,0x1F,0x2F,0x3F}
poke_slot(0, 0x001F, 0x0321)
poke_slot(4, lo16(3, 0, 0, 5, 0), 0x0000)
for label, cram in (("cram=0123", 0x0123), ("cram=2000(-1.0)", 0x2000)):
    log(f"-- flavor {label} --")
    xp.poke(0x2c00 + 2*2, 2, cram)
    for col in range(0x40):
        poke_slot(2, lo16(0, 0, 0, 0, col))     # st0, k=0, column under test
        time.sleep(0.12)
        v = rd_entry(5)
        mark = "" if col in KNOWN else "  <-- new"
        log(f"  col 0x{col:02x}: {v:06x} ({s24(v):+9d}){mark}")
clear_slots(6)

# ---------------- E5: wrap test on the saw path ----------------
# Replicate the JV saw block shape with a big increment; watch IRAM3[7] (entry 135) over time.
log("=== E5: wrap vs saturate on the bit13=1 store path (saw shape, incr 0x1000) ===")
for e in (135, 199): pass
poke_slot(0, 0x73F0, 0x0009)     # st1 b13=1 b12=1 sel-path read (JV 71F0-shape at sm=1 variant)
poke_slot(1, 0xF19F, 0x1000)     # st3 col 0x1F 'phase += cram' (big step)
poke_slot(2, 0x51F0, 0x1001)     # the 'unsat?' companion
poke_slot(3, 0xF1F0, 0x0000)     # st3 store phase
time.sleep(0.2)
seq = []
for i in range(14):
    v = rd_entry(135)
    seq.append(v)
    time.sleep(0.15)
log("  IRAM3[7] over time:", " ".join(f"{v:06x}" if v is not None else "??" for v in seq))
d = [s24((seq[i+1]-seq[i]) & 0xffffff) for i in range(len(seq)-1) if seq[i] is not None and seq[i+1] is not None]
log("  deltas:", d)
log("  verdict:", "WRAPS (monotone mod 2^24)" if any(x < 0 for x in d) and max(seq) > 0x700000 else
    ("saturates/pins" if len(set(seq[-3:])) == 1 else "inconclusive - inspect"))
# now nop the 'unsat?' line and re-watch
poke_slot(2, 0, 0)
time.sleep(0.2)
seq2 = [rd_entry(135) or 0]
for i in range(8):
    time.sleep(0.15); seq2.append(rd_entry(135) or 0)
log("  after nopping 51F0:", " ".join(f"{v:06x}" for v in seq2))
clear_slots(6)

# ---------------- E4: cram-steered store destinations (bit13=1) ----------------
log("=== E4: bit13=1 store destination vs CRAM value (k=15, acc=0x00333) ===")
poke_slot(0, 0x001F, 0x0333)
probe = list(range(32, 64)) + list(range(128, 160)) + [192+15, 192+14, 192+16]
for cram in (0x0000, 0x0001, 0x0010, 0x0080, 0x0100, 0x1000, 0x4000, 0x8000, 0x8001):
    # clear probe zone
    for e in probe:
        if e < 64:    xp.poke(0x3000 + e*4, 4, 0)
        elif e < 192: xp.poke((0x3100 if e < 128 else 0x3200) + (e - (64 if e < 128 else 128))*4, 4, 0)
        else:         xp.poke(0x3300 + (e-192)*2, 2, 0)
    poke_slot(2, lo16(3, 1, 1, 15, 0), cram)
    time.sleep(0.25)
    hits = [(e, rd_entry(e)) for e in probe]
    hits = [(e, v) for e, v in hits if v]
    log(f"  cram={cram:04x}: " + (" ".join(f"[{e}]={v:06x}" for e, v in hits) or "no hits in probe zone"))
    poke_slot(2, 0, 0)

clear_slots(6)
log("=== session 2 done ===")
json.dump({"done": 2}, open("rig_session2.done.json", "w"))
