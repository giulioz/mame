#!/usr/bin/env python3
"""Summarize xp_trace.lua output and reconstruct byte-addressed XP writes."""

import argparse
import collections
import csv
import pathlib


def load(path):
    events = []
    with open(path, newline="", encoding="utf-8") as source:
        for row in csv.reader(source):
            if not row or row[0].startswith("#"):
                if row and row[0].startswith("# aggregate"):
                    break
                continue
            if len(row) < 7:
                continue
            timestamp, kind, pc, pr, address, data, mask = row[:7]
            events.append((float(timestamp), kind, int(pc, 16), int(pr, 16),
                           int(address, 16), int(data, 16), int(mask, 16)))
    return events


def bytes_for(event):
    timestamp, kind, pc, pr, address, data, mask = event
    for lane in range(4):
        shift = (3 - lane) * 8
        if (mask >> shift) & 0xff:
            yield timestamp, kind, pc, pr, address + lane, (data >> shift) & 0xff


def load_functions(path):
    if not path:
        return []
    functions = []
    with open(path, newline="", encoding="utf-8") as source:
        for row in csv.DictReader(source, delimiter="\t"):
            entry = int(row["entry"], 16)
            functions.append((entry, entry + int(row["size"]), row["name"]))
    return sorted(functions)


def function_name(functions, pc):
    # Memory taps report the PC after the memory instruction on this SH core.
    # Try both the reported address and the preceding 16-bit instruction.
    for candidate in (pc, pc - 2):
        for start, end, name in functions:
            if start <= candidate < end:
                return name
    return "?"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("trace")
    parser.add_argument("--after", type=float, default=0.0)
    parser.add_argument("--top", type=int, default=40)
    parser.add_argument("--bank", type=lambda value: int(value, 0),
                        help="only include byte accesses in this 0x100-byte bank")
    parser.add_argument("--functions", type=pathlib.Path,
                        help="functions.tsv exported by ExportJV1080.py")
    args = parser.parse_args()

    events = [event for event in load(args.trace) if event[0] >= args.after]
    byte_events = [item for event in events for item in bytes_for(event)]
    if args.bank is not None:
        byte_events = [item for item in byte_events if (item[4] & 0xff00) == args.bank]
    functions = load_functions(args.functions)
    by_pc = collections.Counter((kind, pc, pr) for _, kind, pc, pr, _, _ in byte_events)
    by_bank = collections.Counter((kind, address & 0xff00) for _, kind, _, _, address, _ in byte_events)
    by_address = collections.Counter((kind, address) for _, kind, _, _, address, _ in byte_events)

    print(f"events={len(events)} byte_accesses={len(byte_events)} after={args.after:g}s")
    print("\nXP banks:")
    for (kind, bank), count in sorted(by_bank.items()):
        print(f"  {kind} {bank:04x}: {count}")
    print("\nTop firmware access sites:")
    for (kind, pc, pr), count in by_pc.most_common(args.top):
        name = function_name(functions, pc)
        print(f"  {kind} pc={pc:08x} pr={pr:08x} function={name}: {count}")
    print("\nTop XP byte addresses:")
    for (kind, address), count in by_address.most_common(args.top):
        print(f"  {kind} {address:08x}: {count}")


if __name__ == "__main__":
    main()
