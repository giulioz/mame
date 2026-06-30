#!/usr/bin/env python3
"""Decode the confirmed fields of the JV-1080 XP DSP templates.

The XP ALU mnemonics are intentionally not asserted yet.  The low halfword is
decoded with the field layout shared by Roland's CSP: opcode nibble, store
mode, and a 9-bit memory/special-register selector.  ERAM pair decoding comes
from the JV firmware's xp_dsp_write_eram_offset routine.
"""

from __future__ import annotations

import argparse
import struct
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path

from extract_dsp_templates import (
    CRAM_WORDS,
    PRAM_WORDS,
    ROM_BASE,
    RFX_NAMES,
    TABLE_ADDRESS,
    rom_offset,
)


CSP_ANALOG_MNEMONICS = (
    "MAC.A", "MAC.B", "MUL.A", "MUL.B",
    "ABSMUL.A", "CMP.A", "NEGMUL.A", "NEGMAC.A",
    "ABS8MUL.A", "ABS8MAC.A", "DMAC.A", "DMAC.B",
    "VMUL.A", "VMUL.B", "VMAC.A", "VMAC.B",
)

FACTORY_TEST_PRAM_ADDRESS = 0x0A03EFDC
FACTORY_TEST_CRAM_ADDRESS = 0x0A03F3DC
FACTORY_TEST_SLOTS = 256

STORE_MODES = (
    "none",
    "synth-bus",
    "special.A",
    "special.A.sat",
    "iram.A",
    "iram.B",
    "iram.A.sat",
    "iram.B.sat",
)


def signed(value: int, bits: int) -> int:
    sign = 1 << (bits - 1)
    return (value & (sign - 1)) - (value & sign)


def coefficient_value(raw: int) -> float:
    """Decode SCCore's preserved XP CRAM fixed-point representation."""
    shifts = (0, 1, 2, 4)
    mantissa = signed(raw, 14)
    return (mantissa << shifts[(raw >> 14) & 3]) / 8192.0


@dataclass(frozen=True)
class Instruction:
    raw: int
    coefficient: int

    @property
    def opcode(self) -> int:
        return (self.raw >> 12) & 0x0F

    @property
    def store_mode(self) -> int:
        return (self.raw >> 9) & 0x07

    @property
    def memory(self) -> int:
        return (self.raw & 0xFF) | (((self.raw >> 8) & 1) << 8)

    @property
    def scaler(self) -> int:
        return (self.raw >> 24) & 3

    @property
    def high_control(self) -> int:
        return (self.raw >> 24) & 0xFF

    @property
    def parallel_memory(self) -> int:
        """Unresolved upper-half memory selector exposed by the factory test."""
        return (self.raw >> 21) & 0x7F

    @property
    def parallel_control(self) -> int:
        return (self.raw >> 16) & 0x1F

    @property
    def eram_address_high(self) -> int:
        return (self.raw >> 16) & 0x7F

    @property
    def starts_eram_pair(self) -> bool:
        # Bit 23 survives the firmware's address-high rewrite and marks every
        # dynamically confirmed ERAM-address first word.  It is only a static
        # candidate marker because a second-word low-address bit can overlap.
        return bool(self.raw & 0x00800000)

    @property
    def is_empty(self) -> bool:
        return self.raw == 0 and self.coefficient == 0


def decode_eram_address(first: Instruction, second: Instruction) -> int:
    return (first.eram_address_high << 9) | ((second.raw >> 16) & 0x1FF)


def read_templates(path: Path) -> list[tuple[list[Instruction], int, int]]:
    image = path.read_bytes()
    table = rom_offset(TABLE_ADDRESS, len(RFX_NAMES) * 8, len(image))
    result = []
    for index in range(len(RFX_NAMES)):
        pram_address, cram_address = struct.unpack_from(">II", image, table + index * 8)
        po = rom_offset(pram_address, PRAM_WORDS * 4, len(image))
        co = rom_offset(cram_address, CRAM_WORDS * 2, len(image))
        pram = struct.unpack_from(f">{PRAM_WORDS}I", image, po)
        cram = struct.unpack_from(f">{CRAM_WORDS}H", image, co)
        result.append(([Instruction(p, c) for p, c in zip(pram, cram)], pram_address, cram_address))
    return result


def select_effects(values: list[str]) -> list[int]:
    if not values:
        return list(range(len(RFX_NAMES)))
    selected = []
    for value in values:
        try:
            index = int(value, 0)
        except ValueError:
            matches = [i for i, name in enumerate(RFX_NAMES) if value.casefold() in name.casefold()]
            if len(matches) != 1:
                raise SystemExit(f"effect selector {value!r} matched {len(matches)} algorithms")
            index = matches[0]
        if not 0 <= index < len(RFX_NAMES):
            raise SystemExit(f"effect index {index} is outside 0..{len(RFX_NAMES) - 1}")
        selected.append(index)
    return selected


def effect_summary(index: int, program: list[Instruction]) -> str:
    active = [item for item in program if not item.is_empty]
    opcodes = Counter(item.opcode for item in active)
    stores = Counter(item.store_mode for item in active)
    eram = [
        (slot, decode_eram_address(item, program[slot + 1]))
        for slot, item in enumerate(program[:-1])
        if item.starts_eram_pair
    ]
    ops = " ".join(f"{key:x}:{value}" for key, value in sorted(opcodes.items())) or "-"
    store_text = " ".join(f"{key}:{value}" for key, value in sorted(stores.items())) or "-"
    eram_text = " ".join(f"{slot:02x}=0x{address:04x}" for slot, address in eram) or "-"
    return (
        f"{index:02d}\t{RFX_NAMES[index]}\tactive={len(active)}\t"
        f"op[{ops}]\tstore[{store_text}]\teram-candidate[{eram_text}]"
    )


def listing(index: int, program: list[Instruction], pram_address: int, cram_address: int) -> None:
    print(f"\n# {index:02d} {RFX_NAMES[index]} PRAM=0x{pram_address:08x} CRAM=0x{cram_address:08x}")
    print("# slot raw       coef  q-value    op csp-analog  store          mem scale p-mem/p-ctl annotation")
    for slot, item in enumerate(program):
        if item.is_empty:
            continue
        annotation = ""
        if item.starts_eram_pair and slot + 1 < len(program):
            annotation = f"ERAM-pair candidate address=0x{decode_eram_address(item, program[slot + 1]):04x}"
        print(
            f"{slot:03x}  {item.raw:08x}  {item.coefficient:04x} "
            f"{coefficient_value(item.coefficient):+9.5f}  {item.opcode:x}  "
            f"{CSP_ANALOG_MNEMONICS[item.opcode]:<11} "
            f"{STORE_MODES[item.store_mode]:<14} {item.memory:03x}   "
            f"{item.scaler}   {item.parallel_memory:02x}/{item.parallel_control:02x}       {annotation}"
        )


def factory_test_listing(image: bytes) -> None:
    """Decode the standalone program uploaded by the manufacturing EFX test."""
    po = rom_offset(FACTORY_TEST_PRAM_ADDRESS, FACTORY_TEST_SLOTS * 4, len(image))
    co = rom_offset(FACTORY_TEST_CRAM_ADDRESS, FACTORY_TEST_SLOTS * 2, len(image))
    pram = struct.unpack_from(f">{FACTORY_TEST_SLOTS}I", image, po)
    cram = struct.unpack_from(f">{FACTORY_TEST_SLOTS}H", image, co)
    program = [Instruction(p, c) for p, c in zip(pram, cram)]

    print(
        "# Factory EFX execution test: 256 slots; IRAM3[0:4] are seeded with "
        "0x000000, 0x555555, 0xaaaaaa, 0xffffff."
    )
    print("# Expected low bytes in IRAM3[4:8]: 00 55 aa ff after 0x208 firmware ticks.")
    print("# slot raw       coef  q-value    op csp-analog  store          mem scale p-mem/p-ctl")
    for slot, item in enumerate(program):
        if item.is_empty:
            continue
        print(
            f"{slot:03x}  {item.raw:08x}  {item.coefficient:04x} "
            f"{coefficient_value(item.coefficient):+9.5f}  {item.opcode:x}  "
            f"{CSP_ANALOG_MNEMONICS[item.opcode]:<11} "
            f"{STORE_MODES[item.store_mode]:<14} {item.memory:03x}   "
            f"{item.scaler}   {item.parallel_memory:02x}/{item.parallel_control:02x}"
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "rom",
        nargs="?",
        type=Path,
        default=Path("jv1080/roland_r00678167.ic20"),
        help="1 MiB external firmware image",
    )
    parser.add_argument("--effect", action="append", default=[], help="index or unique name substring")
    parser.add_argument("--listing", action="store_true", help="print non-empty decoded slots")
    parser.add_argument(
        "--factory-test",
        action="store_true",
        help="decode the manufacturing EFX execution-test program instead of RFX templates",
    )
    args = parser.parse_args()

    if args.factory_test:
        factory_test_listing(args.rom.read_bytes())
        return

    templates = read_templates(args.rom)
    selected = select_effects(args.effect)

    groups: dict[tuple[tuple[int, int], ...], list[int]] = defaultdict(list)
    for index, (program, _, _) in enumerate(templates):
        groups[tuple((item.raw, item.coefficient) for item in program)].append(index)

    print("# XP fields marked as confirmed are firmware-derived; CSP mnemonic names are analogies, not yet XP semantics.")
    for index in selected:
        program, pram_address, cram_address = templates[index]
        print(effect_summary(index, program))
        peers = groups[tuple((item.raw, item.coefficient) for item in program)]
        if len(peers) > 1:
            print("# identical-template indices: " + ",".join(str(peer) for peer in peers))
        if args.listing:
            listing(index, program, pram_address, cram_address)


if __name__ == "__main__":
    main()
