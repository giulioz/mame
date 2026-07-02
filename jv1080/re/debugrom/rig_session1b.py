#!/usr/bin/env python3
"""Rig session 1b: E1-F retest (bit12), E0b slot-3 anomaly retest, E2 st-verbs.
Extra care: ping between phases, longer settle after poke bursts, single-attempt reads."""
import sys, time
from xp_manual_eq import XP
from jvdebug import ping as PING

xp = XP()
def log(s): print(s, flush=True)

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
        log("!! 2 consecutive read failures - pausing 3s and pinging once")
        time.sleep(3)
        if xp.jv.xfer(PING(), timeout=3.0):
            log("   ping OK - continuing (transient)")
            fails[0] = 0
            return None
        log("   ping FAILED - ABORT")
        sys.exit(1)
    return None

def alive(tag):
    ok = xp.jv.xfer(PING(), timeout=3.0)
    log(f"[ping {tag}: {'ok' if ok else 'NO REPLY'}]")
    return ok

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

# state after session1 abort: slots 0,2 held F-case program; clear first
log("=== cleanup from aborted run ===")
clear_slots(8)
clear_entries([15, 47, 79, 111, 143, 199, 207])
alive("post-cleanup")

# ---------------- E1-F: bit12=1 store retest ----------------
log("=== E1b: F-case (b12=1, b13=1, cram=0) k=15 and k=0 ===")
for k in (15, 0):
    clear_entries([k, k+32, k+64, k+96, 128+k])
    poke_slot(0, 0x001F, 0x0321)
    poke_slot(2, lo16(3, 1, 1, k, 0), 0x0000)
    time.sleep(0.5)
    got = {e: rd_entry(e) for e in (k, k+32, k+64, k+96, 128+k)}
    log(f"  F k={k}: " + (" ".join(f"[{e}]={v:06x}" for e, v in got.items() if v) or "nothing") +
        f"   (E-case wrote [{k+32}] only)")
    clear_slots(4)
    if not alive(f"after F k={k}"): sys.exit(1)

# ---------------- E0b: slot-3 store anomaly retest ----------------
log("=== E0b: store position check (single store at slot 3 / k=6) ===")
clear_entries([6, 70])
poke_slot(0, 0x001F, 0x0123)
poke_slot(3, lo16(3, 0, 0, 6, 0), 0x0000)
time.sleep(0.4)
log(f"  lone store@slot3 k=6: iram[6]={rd_entry(6):06x} iram[70]={rd_entry(70):06x}")
clear_slots(6)
# also: two adjacent stores at slots 2+3
clear_entries([5, 6, 69, 70])
poke_slot(0, 0x001F, 0x0123)
poke_slot(2, lo16(3, 0, 0, 5, 0), 0x0000)
poke_slot(3, lo16(3, 0, 0, 6, 0), 0x0000)
time.sleep(0.4)
log(f"  adjacent stores 2+3: [5]={rd_entry(5):06x} [6]={rd_entry(6):06x} [69]={rd_entry(69):06x} [70]={rd_entry(70):06x}")
clear_slots(6)
alive("after E0b")

# ---------------- E2: st-verb A/B ----------------
log("=== E2: st verbs (TEST at slot2: st=X b13=0 b12=0 k=5 col=0x1F cram=0x0100) ===")
for st in (0, 1, 2, 3):
    clear_slots(6)
    clear_entries([5, 9, 37, 69, 73, 101, 133, 197])
    poke_slot(0, 0x001F, 0x0555)
    poke_slot(2, lo16(st, 0, 0, 5, 0x1F), 0x0100)
    poke_slot(4, lo16(3, 0, 0, 9, 0), 0x0000)
    time.sleep(0.4)
    acc = rd_entry(9)
    side = {e: rd_entry(e) for e in (5, 69, 37, 101, 133, 197)}
    log(f"  st={st}: acc-capture[9]={'??' if acc is None else f'{acc:06x}'} " +
        "side: " + (" ".join(f"[{e}]={v:06x}" for e, v in side.items() if v) or "none"))
    if not alive(f"after st={st}"): sys.exit(1)

clear_slots(8)
log("=== session 1b done ===")
