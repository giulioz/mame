#!/usr/bin/env python3
"""Verify XP host-window reads against the four raw, scrambled wave ROMs."""

import argparse
import csv
import pathlib


ADDRESS_BITS = [20, 19, 18, 15, 11, 17, 14, 8, 6, 9, 16,
                10, 5, 12, 7, 13, 1, 3, 2, 4, 0]
DATA_BITS = [1, 3, 6, 7, 5, 4, 0, 2]


def bitswap(value, bits):
    result = 0
    for index, source in enumerate(bits):
        result |= ((value >> source) & 1) << (len(bits) - 1 - index)
    return result


def source_address(destination):
    result = 0
    for index, source in enumerate(ADDRESS_BITS):
        result |= ((destination >> (20 - index)) & 1) << source
    return result


def bytes_for(row):
    address = int(row[4], 16) & 0x3fff
    data = int(row[5], 16)
    mask = int(row[6], 16)
    for lane in range(4):
        shift = (3 - lane) * 8
        if (mask >> shift) & 0xff:
            yield address + lane, (data >> shift) & 0xff


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("trace")
    parser.add_argument("rom", nargs="*", type=pathlib.Path,
                        default=[pathlib.Path(f"jv1080/jv1080_waverom{i}.bin")
                                 for i in range(1, 5)])
    args = parser.parse_args()

    if len(args.rom) != 4:
        parser.error("exactly four 2 MiB wave ROMs are required")
    raw = b"".join(path.read_bytes() for path in args.rom)
    if len(raw) != 0x800000:
        parser.error("the four wave ROMs must total 8 MiB")

    config = bytearray(0x100)
    pending = []
    checked = 0
    mismatches = []
    with open(args.trace, newline="", encoding="utf-8") as source:
        for row in csv.reader(source):
            if not row or row[0].startswith("# aggregate"):
                break
            if row[0].startswith("#") or len(row) < 7:
                continue
            for address, value in bytes_for(row):
                if row[1] == "W" and 0x3900 <= address < 0x3a00:
                    config[address - 0x3900] = value
                elif row[1] == "R" and 0x3c00 <= address < 0x4000:
                    bank = int.from_bytes(config[0x22:0x24], "big") & 0x7f
                    page = int.from_bytes(config[0x20:0x22], "big") & 0x3ff
                    pending.append((float(row[0]), (bank << 20) |
                                    (page << 10) | (address & 0x3ff)))
                elif row[1] == "R" and address == 0x3911 and pending:
                    timestamp, wave_address = pending.pop(0)
                    if wave_address >= len(raw):
                        continue
                    chip = wave_address >> 21
                    within = wave_address & 0x1fffff
                    raw_address = (chip << 21) | source_address(within)
                    expected = bitswap(raw[raw_address], DATA_BITS)
                    checked += 1
                    if value != expected:
                        mismatches.append((timestamp, wave_address, value, expected))

    print(f"checked={checked} mismatches={len(mismatches)} pending={len(pending)}")
    for timestamp, address, actual, expected in mismatches[:20]:
        print(f"{timestamp:.9f} address={address:07x} "
              f"actual={actual:02x} expected={expected:02x}")
    raise SystemExit(bool(mismatches))


if __name__ == "__main__":
    main()
