#!/usr/bin/env python3
"""Extract Kawai K4 K002-FP PCM descriptors from a 64 KiB code ROM."""

from __future__ import annotations

import argparse
import csv
import sys
from dataclasses import dataclass
from pathlib import Path


POINTER_TABLE = 0xBBDA
PCM_WAVE_BASE = 96
PCM_WAVE_COUNT = 160
SAMPLE_ADDRESS_MASK = 0x7FFFF


@dataclass(frozen=True)
class Descriptor:
    wave_index: int
    zone_index: int
    root_key: int
    flags: int
    raw: bytes
    start: int
    loop: int
    end: int

    @property
    def sample_format(self) -> str:
        return "pcm8" if self.flags & 0x08 else "pcm16"

    @property
    def direction(self) -> str:
        return "reverse" if self.flags & 0x10 else "forward"

    @property
    def terminal_loop(self) -> bool:
        # K4 one-shot waves conventionally repeat a 16-sample terminal pad.
        return abs(self.end - self.loop) == 15

    @property
    def loop_type(self) -> str:
        if self.flags & 0x10:
            return "reverse_terminal" if self.terminal_loop else "reverse"
        if self.flags & 0x02:
            return "forward_mode_b1"
        return "terminal_pad" if self.terminal_loop else "forward"


def note_name(note: int) -> str:
    names = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
    return f"{names[note % 12]}{note // 12 - 1}"


def extend_forward(start: int, low_word: int) -> int:
    address = (start & ~0xFFFF) | low_word
    if address < start:
        address += 0x10000
    return address


def extend_reverse(start: int, low_word: int) -> int:
    address = (start & ~0xFFFF) | low_word
    if address > start:
        address -= 0x10000
    return address


def decode_descriptor(wave: int, zone: int, root: int, flags: int, raw: bytes) -> Descriptor:
    start_word = int.from_bytes(raw[0:2], "little")
    loop_word = int.from_bytes(raw[2:4], "little")
    end_word = int.from_bytes(raw[4:6], "little")

    # The start is a Q4 address on a wrapping 19-bit bus.  K002 omits A16-A18
    # from the loop/end words, so extend them sequentially from the preceding
    # address.
    start = (start_word << 4) & SAMPLE_ADDRESS_MASK
    if flags & 0x10:
        loop = extend_reverse(start, loop_word)
        end = extend_reverse(loop, end_word)
        valid = 0 <= end <= loop <= start <= SAMPLE_ADDRESS_MASK
    else:
        loop = extend_forward(start, loop_word)
        end = extend_forward(loop, end_word)
        valid = 0 <= start <= loop <= end <= SAMPLE_ADDRESS_MASK

    if not valid:
        raise ValueError(
            f"wave {wave}, zone {zone}: invalid address order "
            f"{start:05x}/{loop:05x}/{end:05x}"
        )

    return Descriptor(wave, zone, root, flags, raw, start, loop, end)


def parse_directory(rom: bytes) -> list[Descriptor]:
    if len(rom) != 0x10000:
        raise ValueError(f"expected a 64 KiB K4 code ROM, got {len(rom):#x} bytes")

    result: list[Descriptor] = []
    for pcm_index in range(PCM_WAVE_COUNT):
        table_offset = POINTER_TABLE + pcm_index * 2
        entry = int.from_bytes(rom[table_offset : table_offset + 2], "little")
        if not 0x8000 <= entry < POINTER_TABLE:
            raise ValueError(f"wave {pcm_index + PCM_WAVE_BASE}: bad directory pointer {entry:#06x}")

        flags = rom[entry]
        cursor = entry + 1
        roots: list[int] = []
        while rom[cursor] != 0xFF:
            roots.append(rom[cursor])
            cursor += 1
        cursor += 1

        if not roots:
            raise ValueError(f"wave {pcm_index + PCM_WAVE_BASE}: empty key-zone list")

        for zone, root in enumerate(roots):
            raw = rom[cursor + zone * 6 : cursor + (zone + 1) * 6]
            result.append(
                decode_descriptor(pcm_index + PCM_WAVE_BASE, zone, root, flags, raw)
            )

    return result


def hex_address(value: int) -> str:
    return f"0x{value:05x}"


def region_offsets(descriptor: Descriptor, address: int) -> tuple[str, str, str]:
    if descriptor.sample_format == "pcm8":
        return hex_address(address), "", ""
    return "", hex_address(0x080000 + address), hex_address(0x100000 + address)


def write_csv(descriptors: list[Descriptor]) -> None:
    fields = (
        "wave_index",
        "wave_number",
        "zone",
        "root_key",
        "root_note",
        "format",
        "direction",
        "loop_type",
        "terminal_loop",
        "flags",
        "mode_bit_1",
        "pitch_domain_bit_6",
        "raw_descriptor",
        "start",
        "loop",
        "end",
        "play_length",
        "loop_length",
        "p202_8bit_start",
        "p202_8bit_loop",
        "p202_8bit_end",
        "p203_16bit_msb_start",
        "p203_16bit_msb_loop",
        "p203_16bit_msb_end",
        "p204_16bit_lsb_start",
        "p204_16bit_lsb_loop",
        "p204_16bit_lsb_end",
    )
    writer = csv.DictWriter(sys.stdout, fieldnames=fields, lineterminator="\n")
    writer.writeheader()
    for descriptor in descriptors:
        start_offsets = region_offsets(descriptor, descriptor.start)
        loop_offsets = region_offsets(descriptor, descriptor.loop)
        end_offsets = region_offsets(descriptor, descriptor.end)
        writer.writerow(
            {
                "wave_index": descriptor.wave_index,
                "wave_number": descriptor.wave_index + 1,
                "zone": descriptor.zone_index,
                "root_key": descriptor.root_key,
                "root_note": note_name(descriptor.root_key),
                "format": descriptor.sample_format,
                "direction": descriptor.direction,
                "loop_type": descriptor.loop_type,
                "terminal_loop": int(descriptor.terminal_loop),
                "flags": f"0x{descriptor.flags:02x}",
                "mode_bit_1": int(bool(descriptor.flags & 0x02)),
                "pitch_domain_bit_6": int(bool(descriptor.flags & 0x40)),
                "raw_descriptor": descriptor.raw.hex(),
                "start": hex_address(descriptor.start),
                "loop": hex_address(descriptor.loop),
                "end": hex_address(descriptor.end),
                "play_length": abs(descriptor.end - descriptor.start) + 1,
                "loop_length": abs(descriptor.end - descriptor.loop) + 1,
                "p202_8bit_start": start_offsets[0],
                "p202_8bit_loop": loop_offsets[0],
                "p202_8bit_end": end_offsets[0],
                "p203_16bit_msb_start": start_offsets[1],
                "p203_16bit_msb_loop": loop_offsets[1],
                "p203_16bit_msb_end": end_offsets[1],
                "p204_16bit_lsb_start": start_offsets[2],
                "p204_16bit_lsb_loop": loop_offsets[2],
                "p204_16bit_lsb_end": end_offsets[2],
            }
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path, help="K4 64 KiB code ROM (for example k4_v14.u8)")
    parser.add_argument(
        "--wave",
        type=lambda value: int(value, 0),
        help="only print one zero-based patch wave index (96..255)",
    )
    args = parser.parse_args()

    try:
        descriptors = parse_directory(args.rom.read_bytes())
        if args.wave is not None:
            descriptors = [item for item in descriptors if item.wave_index == args.wave]
            if not descriptors:
                raise ValueError("--wave must select a PCM wave in the range 96..255")
        write_csv(descriptors)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
