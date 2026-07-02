#!/usr/bin/env python3
"""XP load/store probe — free-running DSP, IRAM1/2 observable.

Corrected methodology:
  * The DSP FREE-RUNS its PRAM continuously (like CSP/ESP). We do NOT touch
    0x3916 (leave it at 7 = run) and we do NOT play notes / change patches.
  * Observable = IRAM1/IRAM2 (0x3000/0x3100). NEVER IRAM3 (0x3200) — it is the
    ramp-engine "magic" bank and auto-overwrites.
  * Tracer: put 0x0042 in the CRAM slot the LOAD reads; fuzz the two PRAM words
    (load, store) until 0x42 (or a shifted form) shows up in IRAM1/2.

Two instructions, exactly the probe design:
    PRAM[0] = <load candidate>    with CRAM[0] = 0x0042
    PRAM[s] = <store candidate>   with CRAM[s] = 0x0000   (s swept over pipeline gap)
Slot-parallel accelerator: fix a bank of store candidates in odd slots (each
preceded by the load in the even slot for a fresh accumulator), sweep the load.

Run:  python3 xp_probe.py            # slot-parallel sweep
      python3 xp_probe.py simple     # literal 2-instruction double loop (slower, exact)
"""
import sys, time
from jvdebug import JV, poke, ping
jv = JV(); XP = 0x0c000000
assert jv.xfer(ping()), "no PING"
def PK(a, w, d): jv.send(poke(XP + a, w, d)); time.sleep(0.011)
def RD(off):     return jv.read_dsp(off, 4)

TRACER = 0x42
def tracer_set(v):
    s = set()
    for k in range(0, 19):
        s.add((v << k) & 0xffffff)
        if v >> k: s.add(v >> k)
    s.discard(0)
    return s
TSET = tracer_set(TRACER)

IR1 = [0x3000 + i * 4 for i in range(64)]
IR2 = [0x3100 + i * 4 for i in range(64)]

def zero_dsp(all=False):
    # PRAM, CRAM, IRAM1, IRAM2 — leave IRAM3 (magic) and 0x3916 (run) alone
    for i in range(256): PK(0x3400 + i * 4, 4, 0)
    for i in range(256): PK(0x2c00 + i * 2, 2, 0)
    for i in range(64):  PK(0x3000 + i * 4, 4, 0)
    for i in range(64):  PK(0x3100 + i * 4, 4, 0)
    if all:
        for i in range(64):  PK(0x3200 + i * 4, 4, 0)
        for i in range(64):  PK(0x3300 + i * 4, 4, 0)

def scan_hits():
    out = []
    for o in IR1 + IR2:
        v = RD(o)
        if v in TSET:
            out.append((hex(o), hex(v)))
    return out

# ---- candidate encodings (structured guesses across the ~28-bit word) ----
def load_candidates():
    c = []
    for op in range(16):                       # low-16 MAC-style (uses CRAM[slot])
        for sel in (0, 1, 2, 4):
            c.append((op << 12) | sel)
    for hib in range(16):                      # high parallel-channel patterns
        c.append(hib << 24)
        c.append((1 << 24) | (hib << 21))      # factory-ish "read" flag + index
    return list(dict.fromkeys(c))

def store_candidates():
    c = []
    for hib in range(16):                      # parallel-channel "write" (j<<21)-ish
        for idx in range(16):
            c.append((hib << 24) | (idx << 20))
    for op in range(16):
        for sel in (0, 1, 2, 4, 8):
            c.append((op << 12) | sel)
    return list(dict.fromkeys(c))

def simple():
    """Literal double loop: PRAM[0]=load, PRAM[s]=store for s in 1..4 (pipeline gap)."""
    print("=== SIMPLE 2-instruction probe (free-running, IRAM1/2) ===", flush=True)
    zero_dsp()
    PK(0x2c00, 2, TRACER)          # CRAM[0] = 0x42 (load)
    PK(0x2c02, 2, 0)               # CRAM[1] = 0     (store)
    L = load_candidates(); S = store_candidates()
    print(f"loads={len(L)} stores={len(S)} pairs={len(L)*len(S)}", flush=True)
    for li, lc in enumerate(L):
        PK(0x3400, 4, lc)
        for sc in S:
            for s in (1, 2, 3, 4):     # cover pipeline delay
                PK(0x3400 + s * 4, 4, sc)
            time.sleep(0.05)
            h = scan_hits()
            for s in (1, 2, 3, 4): PK(0x3400 + s * 4, 4, 0)
            if h:
                print(f"HIT  load={hex(lc)} store={hex(sc)} -> {h}", flush=True)
        if li % 4 == 0: print(f"  ...load {li}/{len(L)} {hex(lc)}", flush=True)
    print("DONE", flush=True)

def parallel():
    """Slot-parallel: odd slots = fixed store candidates (fresh acc from even-slot
    load), sweep the load in even slots. One scan tests many stores at once."""
    print("=== SLOT-PARALLEL probe (free-running, IRAM1/2) ===", flush=True)
    S = store_candidates(); L = load_candidates()
    N = min(len(S), 100)                        # store candidates -> odd slots 1,3,..
    zero_dsp()
    for k in range(N):        PK(0x3400 + (2*k+1)*4, 4, S[k])     # stores (fixed)
    for k in range(N + 1):    PK(0x2c00 + (2*k)*2, 2, TRACER)     # CRAM tracer on load slots
    print(f"stores(parallel)={N} loads={len(L)}", flush=True)
    for li, lc in enumerate(L):
        for k in range(N + 1): PK(0x3400 + (2*k)*4, 4, lc)        # load in every even slot
        time.sleep(0.08)
        h = scan_hits()
        if h:
            print(f"HIT  load={hex(lc)} -> {h}  (binary-search stores to ID the encoding)", flush=True)
        if li % 4 == 0: print(f"  ...load {li}/{len(L)} {hex(lc)}", flush=True)
    print("DONE", flush=True)

if __name__ == "__main__":
    (simple if len(sys.argv) > 1 and sys.argv[1] == "simple" else parallel)()
