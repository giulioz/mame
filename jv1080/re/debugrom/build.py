#!/usr/bin/env python3
"""Build a JV-1080 debug ROM: assemble debug_patch.s, splice it into the ic20
external program ROM free space, and apply the sysex hook.

  python3 build.py [orig_ic20] [out_ic20]

Default in == ../../roland_r00678167.ic20 (the image MAME loads), out == ic20_debug.bin.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sh1asm

HERE = os.path.dirname(os.path.abspath(__file__))
ORIG = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "roland_r00678167.ic20")
OUT  = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "ic20_debug.bin")

PAYLOAD_VADDR = 0x0a059d48      # free 0xFF block (33 KiB), file offset 0x59d48
PAYLOAD_FOFF  = PAYLOAD_VADDR & 0xfffff
HOOK_FOFF     = 0x28a84         # midi_task literal pool word (sysex completion dispatcher)
HOOK_OLD      = 0x0a000200      # real "message complete" dispatcher

def main():
    rom = bytearray(open(ORIG, "rb").read())
    if len(rom) != 0x100000:
        raise SystemExit("ic20 must be exactly 1 MiB, got 0x%x" % len(rom))

    # assemble payload
    A = sh1asm.Assembler(base=PAYLOAD_VADDR)
    payload = A.assemble(open(os.path.join(HERE, "debug_patch.s")).read())
    shim = A.labels["dbg_shim"]
    if shim != PAYLOAD_VADDR:
        raise SystemExit("dbg_shim must be at payload base 0x%x, got 0x%x" % (PAYLOAD_VADDR, shim))

    # verify the hook word is what we expect before touching it
    cur = int.from_bytes(rom[HOOK_FOFF:HOOK_FOFF+4], "big")
    if cur != HOOK_OLD:
        raise SystemExit("hook word at file 0x%x is 0x%08x, expected 0x%08x — wrong image?"
                         % (HOOK_FOFF, cur, HOOK_OLD))

    # verify the payload region is free (all 0xFF) and fits
    region = rom[PAYLOAD_FOFF:PAYLOAD_FOFF+len(payload)]
    if any(b != 0xff for b in region):
        raise SystemExit("payload region 0x%x..0x%x is not free (0xFF)"
                         % (PAYLOAD_FOFF, PAYLOAD_FOFF+len(payload)))

    # splice payload + apply hook
    rom[PAYLOAD_FOFF:PAYLOAD_FOFF+len(payload)] = payload
    rom[HOOK_FOFF:HOOK_FOFF+4] = shim.to_bytes(4, "big")

    open(OUT, "wb").write(rom)

    # Split into the two physical 8-bit EPROM images, matching the existing
    # jv1080/dumper/{LOW,HIGH}.bin convention: the program ROM is a 16-bit bus of
    # two byte-wide EPROMs, LOW = even bytes, HIGH = odd bytes, each 512 KiB of
    # data doubled to fill a 1 MiB device (top address line don't-care).
    low = rom[0::2]
    high = rom[1::2]
    low_img = low * 2
    high_img = high * 2
    out_low = os.path.join(os.path.dirname(OUT), "ic20_debug_LOW.bin")
    out_high = os.path.join(os.path.dirname(OUT), "ic20_debug_HIGH.bin")
    open(out_low, "wb").write(low_img)
    open(out_high, "wb").write(high_img)
    # self-check: de-interleaving the two halves must reproduce the image
    recon = bytearray(len(rom))
    recon[0::2] = low_img[:len(rom)//2]
    recon[1::2] = high_img[:len(rom)//2]
    assert bytes(recon) == bytes(rom), "EPROM split failed round-trip"

    # report
    print("debug ROM written: %s" % OUT)
    print("  payload: %d bytes at vaddr 0x%08x (file 0x%05x), free room 0x8158" % (len(payload), PAYLOAD_VADDR, PAYLOAD_FOFF))
    print("  hook   : file 0x%05x  0x%08x -> 0x%08x (dbg_shim)" % (HOOK_FOFF, HOOK_OLD, shim))
    print("  symbols:")
    for name in ("dbg_shim", "rdn", "en", "emit4", "tx_send", "tx_hdr", "tx_eot",
                 "send_ping", "handle_setflag", "handle_poke", "handle_peek", "handle_peekdsp"):
        if name in A.labels:
            print("    %-16s 0x%08x" % (name, A.labels[name]))
    print("  first 48 payload bytes: %s" % payload[:48].hex())
    print("  EPROM images (1 MiB each, 512 KiB doubled):")
    print("    %s  (even bytes / D15-8)" % out_low)
    print("    %s  (odd bytes  / D7-0)" % out_high)

if __name__ == "__main__":
    main()
