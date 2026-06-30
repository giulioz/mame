#!/usr/bin/env python3
"""Summarize a retriggered JV-1080 RFX parameter sweep."""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path

from analyze_dsp_bytecode import (
    Instruction,
    RFX_NAMES,
    coefficient_value,
    decode_eram_address,
    read_templates,
)


DELTA_RE = re.compile(r"^([0-9a-f]+):([0-9a-f]+)>([0-9a-f]+)$")


def parse_delta(text: str) -> dict[int, tuple[int, int]]:
    result = {}
    for item in filter(None, text.split(";")):
        match = DELTA_RE.match(item)
        if not match:
            raise ValueError(f"bad delta {item!r}")
        slot, old, new = (int(value, 16) for value in match.groups())
        result[slot] = (old, new)
    return result


def cram_text(changes: dict[int, tuple[int, int]]) -> str:
    return "; ".join(
        f"{slot:03x}:{old:04x}({coefficient_value(old):+.5f})>"
        f"{new:04x}({coefficient_value(new):+.5f})"
        for slot, (old, new) in sorted(changes.items())
    ) or "-"


def eram_pair_starts(write_text: str) -> set[int]:
    """Find the two adjacent PRAM writes emitted by the ERAM-offset helper."""
    slots = []
    for item in filter(None, write_text.split(";")):
        address = int(item.split(":", 1)[0], 16)
        if 0x04003400 <= address < 0x04003880 and not (address & 3):
            slots.append((address - 0x04003400) // 4)
    starts = set()
    index = 0
    while index + 1 < len(slots):
        if slots[index + 1] == slots[index] + 1:
            starts.add(slots[index])
            index += 2
        else:
            index += 1
    return starts


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("sweep", type=Path)
    parser.add_argument(
        "--rom",
        type=Path,
        default=Path("jv1080/roland_r00678167.ic20"),
        help="external firmware image used to seed unchanged program words",
    )
    parser.add_argument("--effect", type=int, action="append", default=[])
    args = parser.parse_args()

    templates = read_templates(args.rom)
    current = [[item.raw for item in program] for program, _, _ in templates]
    selected = set(args.effect or range(len(RFX_NAMES)))

    print("effect\tname\tpage\tfield\tdirection\tpram\teram-address\tcram\tfresh-note")
    with args.sweep.open(newline="") as source:
        for row in csv.DictReader(source):
            effect = int(row["effect"])
            if effect not in selected:
                continue
            pram = parse_delta(row["pram_delta"])
            cram = parse_delta(row["cram_delta"])
            words = current[effect]

            # The template is updated to its factory parameter values before
            # the first sweep row.  Each delta's old side is authoritative for
            # every word touched by that row.
            for slot, (old, _) in pram.items():
                words[slot] = old

            starts = eram_pair_starts(row["xp_parameter_writes"])
            eram_before = {
                slot: decode_eram_address(Instruction(words[slot], 0), Instruction(words[slot + 1], 0))
                for slot in starts
            }

            for slot, (_, new) in pram.items():
                words[slot] = new
            eram_after = {
                slot: decode_eram_address(Instruction(words[slot], 0), Instruction(words[slot + 1], 0))
                for slot in starts
            }

            if not pram and not cram:
                continue
            pram_desc = "; ".join(
                f"{slot:03x}:{old:08x}>{new:08x}" for slot, (old, new) in sorted(pram.items())
            ) or "-"
            eram_desc = "; ".join(
                f"{slot:03x}:0x{eram_before[slot]:04x}>0x{eram_after[slot]:04x}"
                for slot in sorted(starts)
                if eram_before[slot] != eram_after[slot]
            ) or "-"
            print(
                f"{effect:02d}\t{RFX_NAMES[effect]}\t{row['page']}\t{row['field']}\t"
                f"{row['direction']}\t{pram_desc}\t{eram_desc}\t{cram_text(cram)}\t"
                f"{row['fresh_note']}"
            )


if __name__ == "__main__":
    main()
