#!/usr/bin/env python3
"""
Factory EFX execution test — the firmware's own DSP self-test, used as a
KNOWN-ANSWER Rosetta stone for the XP ISA.

Source: ROM ic20, PRAM @ file 0x3efdc, CRAM @ file 0x3f3dc (256 slots, 100 nonzero).
Full decode: jv1080/re/factory_efx_test.txt

Known answer (from the analyzer / firmware comment):
    seed  IRAM3[0:4] = [0x000000, 0x555555, 0xaaaaaa, 0xffffff]
    run
    expect IRAM3[4:8] low bytes = 00 55 aa ff   (i.e. IRAM3[i] copied to IRAM3[i+4])

The program is repeating 10-slot blocks. Per block (validating the provisional encoding):
    clear acc          op7   (0x00007000)
    load  IRAM3[i]     0x01000000 | (i<<21)     e.g. 0x01000000, 0x01200000, ...
    store IRAM3[i+4]   ((i+4)<<21)              e.g. 0x00800000, 0x00a00000, ...
    + MAC ops (op0/opb) with CRAM coefficients that walk (0x0b,0x15,0x1f,0x29,...)

Because it's the firmware's own program, if it runs and IRAM3[4:7] shows the copied
pattern, that CONFIRMS execution AND the load/store encoding in one shot — much better
than blind fuzzing.

Note on IRAM3: it is the "magic" ramp bank, BUT it only ramps when a target (0x3300+2i)
is set. This test zeroes the targets and writes the current values (0x3200+4i) directly,
so IRAM3 holds still — which is exactly why the firmware can use it here.

Usage:
    from xp_manual import XP
    from factory_efx import run
    xp = XP()
    run(xp)              # free-run (leave 0x3916 at 7, per XP_FACTS E1)
    run(xp, mode=4)      # firmware's single-pass: 0x3916 0->4->wait->0
"""
import struct, time

ROM_PATH = "/Users/giuliozausa/personal/programming/mame/jv1080/roland_r00678167.ic20"
PRAM_OFF, CRAM_OFF = 0x3efdc, 0x3f3dc

SEED   = {0: 0x000000, 1: 0x555555, 2: 0xaaaaaa, 3: 0xffffff}   # IRAM3[0:4] inputs
EXPECT = {4: 0x000000, 5: 0x555555, 6: 0xaaaaaa, 7: 0xffffff}   # IRAM3[4:8] after run


def load(rom_path=ROM_PATH):
    rom = open(rom_path, "rb").read()
    pram = [struct.unpack(">I", rom[PRAM_OFF + i*4:PRAM_OFF + i*4 + 4])[0] for i in range(256)]
    cram = [struct.unpack(">H", rom[CRAM_OFF + i*2:CRAM_OFF + i*2 + 2])[0] for i in range(256)]
    return pram, cram


def run(xp, mode=None, wait=0.6, verbose=True):
    """Upload the factory test, seed inputs, drive execution, read IRAM3[4:8], check.
    mode=None -> free-run (0x3916 left at 7).  mode=4 -> firmware single-pass pulse."""
    pram, cram = load()
    if verbose: print("zero DSP + config...", flush=True)
    xp.zero_dsp(verbose=False)
    xp.set_config(run=(mode is None), verbose=False)          # free-run: 0x3916=7
    if verbose: print("upload factory program (100 slots)...", flush=True)
    for i in range(256):
        if pram[i]: xp.poke(0x3400 + i*4, 4, pram[i])
        if cram[i]: xp.poke(0x2c00 + i*2, 2, cram[i])
    # seed IRAM3 inputs; zero ramp targets so nothing drifts. (zero_dsp already zeroed IRAM1/2.)
    for i in range(8): xp.poke(0x3300 + i*2, 2, 0)
    for s, v in SEED.items(): xp.poke(0x3200 + s*4, 4, v)
    for s in EXPECT:          xp.poke(0x3200 + s*4, 4, 0)      # clear outputs first
    time.sleep(0.15)

    # Snapshot ALL of IRAM1/2/3 before and after — MEASURE which banks it touches, don't assume.
    def snap():
        return {(b, i): xp.read_iram(b, i) for b in (1, 2, 3) for i in range(64)}
    pre = snap()
    if verbose: print("pre  IRAM3[0:8]:", [hex(pre[(3, s)]) for s in range(8)], flush=True)

    if mode is None:
        time.sleep(wait)                                       # free-running; just wait
    else:
        xp.poke(0x3916, 2, 0); time.sleep(0.05)
        xp.poke(0x3916, 2, mode); time.sleep(wait)
        xp.poke(0x3916, 2, 0)

    post = snap()
    diffs = {b: [(i, pre[(b, i)], post[(b, i)]) for i in range(64) if pre[(b, i)] != post[(b, i)]]
             for b in (1, 2, 3)}
    if verbose:
        print("post IRAM3[0:8]:", [hex(post[(3, s)]) for s in range(8)], flush=True)
        for b in (1, 2, 3):
            d = diffs[b]
            print(f"  IRAM{b} changed: {len(d)} slots" + (": " + ", ".join(f"[{i}]{pv:#08x}->{nv:#08x}" for i, pv, nv in d[:8]) if d else ""), flush=True)
    ok = all((post[(3, s)] & 0xff) == (EXPECT[s] & 0xff) for s in EXPECT)
    changed_anywhere = any(diffs[b] for b in (1, 2, 3))
    if verbose:
        print(f"RESULT: {'PASS - IRAM3[4:7] copy matched, encoding CONFIRMED' if ok else ('EXECUTED (something changed) but not the expected copy - inspect diffs above' if changed_anywhere else 'FAIL - nothing changed in ANY IRAM bank -> no execution')}", flush=True)
    return {"pre": pre, "post": post, "diffs": diffs, "pass": ok, "changed": changed_anywhere}


if __name__ == "__main__":
    import sys
    sys.path.insert(0, ".")
    from xp_manual import XP
    xp = XP()
    run(xp)
