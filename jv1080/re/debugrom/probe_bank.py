#!/usr/bin/env python3
"""
Find the IRAM1/2 store encoding by sweeping the store's high (bank) bits.

Idea: load a UNIQUE sentinel into the accumulator (via the factory-known load of
IRAM3[0], which we seed), then STORE the accumulator with a swept high-bit field,
and see which IRAM bank/slot the sentinel lands in. A unique sentinel per trial
means we never need to clear between trials.

Prereq: execution must actually work. Run factory_efx.run(xp) FIRST and confirm a
PASS — that proves the clear/load/store mechanism (in IRAM3). THEN this sweep tells
us which high-bit field redirects the store into IRAM1 or IRAM2.

Encodings under test (all [H], from factory test + Stereo EQ):
    clear acc : 0x00007000
    load acc  : 0x01000000            (= IRAM3[0] -> acc)
    store     : hi_field | (idx<<21)  (idx = 0..7 within a bank; hi_field = bank select)

Usage:  python3 probe_bank.py
"""
import time
from xp_manual import XP

# High-bit fields to try as the "bank select" for the store (bits 24 is the load/store
# direction, so we keep it 0 for a store and sweep 25/26/27 and a couple combos).
BANK_FIELDS = [
    0x00000000,   # baseline (what the factory used -> IRAM3)
    0x02000000,   # bit 25
    0x04000000,   # bit 26   (EQ region bit)
    0x06000000,   # bits 25+26
    0x08000000,   # bit 27   (EQ region bit)
    0x0a000000,   # bits 25+27
    0x0c000000,   # bits 26+27
    0x0e000000,   # bits 25+26+27
]


def sweep(xp, verbose=True):
    xp.reset()                                   # zero everything, config, 0x3916=7
    found = []
    it = 0
    for hi in BANK_FIELDS:
        for idx in range(8):
            it += 1
            sent = 0xA00000 | it                 # unique per trial (no clearing needed)
            store_word = hi | (idx << 21)
            xp.poke(0x3200, 4, sent)             # IRAM3[0] = sentinel (the load source)
            (xp.program()
                .line(0x00007000, 0)             # clear acc
                .line(0x01000000, 0)             # acc = IRAM3[0] = sentinel
                .nop().nop()                     # pipeline gap
                .at(4, pram=store_word)          # store acc -> swept address
                .upload(verbose=False))
            time.sleep(0.25)
            # where did the sentinel land? scan IRAM1, IRAM2 (and IRAM3 for reference)
            hits = [(b, i) for b in (1, 2, 3) for i in range(64) if xp.read_iram(b, i) == sent]
            hits = [(b, i) for (b, i) in hits if not (b == 3 and i == 0)]   # ignore the source
            if hits:
                found.append((store_word, hits))
                if verbose:
                    print(f"store_word={store_word:08x} (hi={hi:08x} idx={idx}) -> sentinel at {hits}", flush=True)
            elif verbose and idx == 0:
                print(f"  ...hi={hi:08x}: no landing yet", flush=True)
    print("\n=== store encodings that reached IRAM1/2 ===", flush=True)
    for sw, h in found:
        banks = sorted({b for b, i in h})
        if any(b in (1, 2) for b in banks):
            print(f"  {sw:08x} -> banks {banks}: {h}", flush=True)
    if not found:
        print("  nothing landed anywhere -> either execution isn't running (run factory_efx first)"
              " or the load/store encoding is wrong.", flush=True)
    return found


if __name__ == "__main__":
    xp = XP()
    sweep(xp)
