#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders:Giulio Zausa
"""Inspect the AN/VL SWP30 MEG programs embedded in the EX5 TG ROM."""

from __future__ import annotations

import argparse
import hashlib
from collections import Counter
from pathlib import Path


UNKNOWN_BITS = (38, 35, 34, 32, 3, 2, 1, 0)
BLOBS = (
    ("AN/common first half", 0x28EE54, 192),
    ("AN second half", 0x2964F0, 192),
    ("VL program A", 0x3C8D68, 247),
    ("VL program B", 0x3C97D4, 254),
)


def reconstruct(romdir: Path) -> bytes:
    high = (romdir / "ex5r_tg_h.bin").read_bytes()
    low = (romdir / "ex5r_tg_l.bin").read_bytes()
    if len(high) != len(low) or len(high) % 2:
        raise ValueError("TG ROM halves must have equal, even sizes")
    image = bytearray(len(high) * 2)
    for source in range(0, len(high), 2):
        dest = source * 2
        image[dest : dest + 2] = high[source : source + 2][::-1]
        image[dest + 2 : dest + 4] = low[source : source + 2][::-1]
    return bytes(image)


def opcode_blob(image: bytes, address: int, count: int) -> list[int]:
    # CPU addresses 0x200000-0x3fffff select the 2 MiB mask ROM image.
    offset = address & 0x1FFFFF
    stored_count = int.from_bytes(image[offset : offset + 2], "big")
    if stored_count != count:
        raise ValueError(f"blob at {address:08x} has count {stored_count}, expected {count}")
    offset += 2
    end = offset + count * 8
    if end > len(image):
        raise ValueError(f"blob at {address:08x} extends past the TG ROM")
    return [int.from_bytes(image[pos : pos + 8], "big") for pos in range(offset, end, 8)]


def inspect(name: str, address: int, opcodes: list[int], verbose: bool) -> None:
    counts = {bit: sum((opcode >> bit) & 1 for opcode in opcodes) for bit in UNKNOWN_BITS}
    patterns = Counter(sum(((opcode >> bit) & 1) << bit for bit in UNKNOWN_BITS) for opcode in opcodes)
    print(f"{name}: address={address:08x} instructions={len(opcodes)}")
    print("  " + " ".join(f"bit{bit}={counts[bit]}" for bit in UNKNOWN_BITS))
    print("  unknown-patterns " + " ".join(f"{pattern:010x}:{count}" for pattern, count in patterns.most_common()))
    if verbose:
        mask = sum(1 << bit for bit in UNKNOWN_BITS)
        for pc, opcode in enumerate(opcodes):
            unknown = opcode & mask
            if unknown:
                print(f"    {pc:03x} opcode={opcode:016x} unknown={unknown:010x}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--romdir", type=Path, default=Path("ex5r"), help="directory containing the two TG ROM halves")
    parser.add_argument("--verbose", action="store_true", help="list every instruction using an unknown bit")
    args = parser.parse_args()

    image = reconstruct(args.romdir)
    digest = hashlib.sha1(image).hexdigest()
    print(f"TG image bytes={len(image)} sha1={digest}")
    if digest != "849f3e9748ea53fdada51a304217c0072ce28edd":
        print("warning: reconstructed image differs from the analyzed EX5R TG ROM")
    for name, address, count in BLOBS:
        inspect(name, address, opcode_blob(image, address, count), args.verbose)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
