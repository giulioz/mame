#!/usr/bin/env python3
"""Inventory the XP DSP PRAM/CRAM templates embedded in JV-1080 firmware."""

from __future__ import annotations

import argparse
import hashlib
import struct
from collections import defaultdict
from pathlib import Path


ROM_BASE = 0x0A000000
TABLE_ADDRESS = 0x0A044EBC
UPDATER_TABLE_ADDRESS = 0x0A059B30
PRAM_WORDS = 104
CRAM_WORDS = 104

RFX_NAMES = (
    "Stereo EQ",
    "Overdrive",
    "Distortion",
    "Phaser",
    "Spectrum",
    "Enhancer",
    "Auto Wah",
    "Rotary",
    "Compressor",
    "Limiter",
    "Hexa Chorus",
    "Tremolo Chorus",
    "Space-D",
    "Stereo Chorus",
    "Stereo Flanger",
    "Step Flanger",
    "Stereo Delay",
    "Modulation Delay",
    "Triple Tap Delay",
    "Quadruple Tap Delay",
    "Time Control Delay",
    "2-Voice Pitch Shifter",
    "Feedback Pitch Shifter",
    "Reverb",
    "Gate Reverb",
    "Overdrive -> Chorus",
    "Overdrive -> Flanger",
    "Overdrive -> Delay",
    "Distortion -> Chorus",
    "Distortion -> Flanger",
    "Distortion -> Delay",
    "Enhancer -> Chorus",
    "Enhancer -> Flanger",
    "Enhancer -> Delay",
    "Chorus -> Delay",
    "Flanger -> Delay",
    "Chorus -> Flanger",
    "Chorus / Delay",
    "Flanger / Delay",
    "Chorus / Flanger",
)


def rom_offset(address: int, size: int, image_size: int) -> int:
    offset = address - ROM_BASE
    if offset < 0 or offset + size > image_size:
        raise ValueError(f"address 0x{address:08x} is outside the external ROM")
    return offset


def short_hash(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()[:16]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "rom",
        nargs="?",
        type=Path,
        default=Path("jv1080/roland_r00678167.ic20"),
        help="1 MiB external firmware image",
    )
    args = parser.parse_args()
    image = args.rom.read_bytes()
    table = rom_offset(TABLE_ADDRESS, 8, len(image))
    updater_table = rom_offset(UPDATER_TABLE_ADDRESS, 4, len(image))

    templates = []
    for index in range(256):
        pram_address, cram_address = struct.unpack_from(">II", image, table + index * 8)
        try:
            pram_offset = rom_offset(pram_address, PRAM_WORDS * 4, len(image))
            cram_offset = rom_offset(cram_address, CRAM_WORDS * 2, len(image))
        except ValueError:
            break
        pram = image[pram_offset : pram_offset + PRAM_WORDS * 4]
        cram = image[cram_offset : cram_offset + CRAM_WORDS * 2]
        updater = struct.unpack_from(">I", image, updater_table + index * 4)[0]
        templates.append((index, pram_address, cram_address, updater, pram, cram))

    if not templates:
        raise SystemExit("no valid template pointers found")

    groups: dict[tuple[str, str], list[int]] = defaultdict(list)
    for index, _, _, _, pram, cram in templates:
        groups[(short_hash(pram), short_hash(cram))].append(index)

    print(
        "index\trole\tparameter_updater\tpram_address\tcram_address\tpram_sha256_16\t"
        "cram_sha256_16\tpram_nonzero\tcram_nonzero\tidentical_indices"
    )
    for index, pram_address, cram_address, updater, pram, cram in templates:
        pram_words = struct.unpack(f">{PRAM_WORDS}I", pram)
        cram_words = struct.unpack(f">{CRAM_WORDS}H", cram)
        hashes = (short_hash(pram), short_hash(cram))
        role = RFX_NAMES[index] if index < len(RFX_NAMES) else "padding/no-op"
        peers = ",".join(str(peer) for peer in groups[hashes])
        print(
            f"{index}\t{role}\t0x{updater:08x}\t"
            f"0x{pram_address:08x}\t0x{cram_address:08x}\t"
            f"{hashes[0]}\t{hashes[1]}\t"
            f"{sum(word != 0 for word in pram_words)}\t"
            f"{sum(word != 0 for word in cram_words)}\t{peers}"
        )

    print(
        f"# selectors={len(templates)} unique_images={len(groups)} "
        f"rfx_names={len(RFX_NAMES)}"
    )


if __name__ == "__main__":
    main()
