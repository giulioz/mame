#!/usr/bin/env python3
"""Decode and materialize Kawai K4 K007-FP DSP upload images.

The K4 code ROM stores a directory, two-plane DSP programs, sparse program
patches, parameter destination lists, and coefficient matrices in bank 3.
This utility decodes those structures and can emit the exact big-endian words
that the main CPU writes to the K007-FP.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
import struct
import sys


EFFECTS = (
    ("Reverb 1", ("Pre-Delay", "Time", "Tone")),
    ("Reverb 2", ("Pre-Delay", "Time", "Tone")),
    ("Reverb 3", ("Pre-Delay", "Time", "Tone")),
    ("Reverb 4", ("Pre-Delay", "Time", "Tone")),
    ("Gate Reverb", ("Pre-Delay", "Time", "Tone")),
    ("Reverse Gate", ("Pre-Delay", "Time", "Tone")),
    ("Normal Delay", ("Feedback", "Tone", "Delay")),
    ("Stereo Panpot Delay", ("Feedback", "L/R Delay", "Delay")),
    ("Chorus", ("Width", "Feedback", "Rate")),
    ("Overdrive + Flanger", ("Drive", "Flanger Type", "Balance")),
    ("Overdrive + Normal Delay", ("Drive", "Delay Time", "Balance")),
    ("Overdrive + Reverb", ("Drive", "Reverb Type", "Balance")),
    ("Normal Delay + Normal Delay", ("Delay 1", "Delay 2", "Balance")),
    ("Normal Delay + Stereo Panpot Delay", ("Delay 1", "Delay 2", "Balance")),
    ("Chorus + Normal Delay", ("Chorus", "Delay", "Balance")),
    ("Chorus + Stereo Panpot Delay", ("Chorus", "Delay", "Balance")),
)


@dataclass(frozen=True)
class Destination:
    plane: int
    address: int


@dataclass(frozen=True)
class SparsePatch:
    plane: int
    address: int
    word: int


@dataclass(frozen=True)
class Algorithm:
    number: int
    name: str
    parameter_names: tuple[str, str, str]
    configuration: int
    base_pointer: int
    sparse_pointer: int
    word_count: int


class K007Image:
    """Reader for the K007-FP structures in a 64 KiB K4 code ROM."""

    def __init__(self, data: bytes) -> None:
        if len(data) != 0x10000:
            raise ValueError(f"expected a 64 KiB K4 code ROM, got {len(data):#x} bytes")
        self.data = data

    @staticmethod
    def file_offset(bank3_address: int) -> int:
        if not 0x8000 <= bank3_address <= 0xbfff:
            raise ValueError(f"bank-3 pointer outside 8000-bfff: {bank3_address:04x}")
        # Bank 3 maps the CPU's 8000-bfff window to physical c000-ffff.
        return bank3_address + 0x4000

    def byte(self, address: int) -> int:
        return self.data[self.file_offset(address)]

    def little_word(self, address: int) -> int:
        offset = self.file_offset(address)
        return struct.unpack_from("<H", self.data, offset)[0]

    def big_word(self, address: int) -> int:
        offset = self.file_offset(address)
        return struct.unpack_from(">H", self.data, offset)[0]

    def algorithm(self, number: int) -> Algorithm:
        if not 0 <= number < len(EFFECTS):
            raise ValueError("algorithm must be in the range 0..15")
        configuration = self.byte(0x8000 + number)
        record = 0x8011 + number * 4
        base_pointer = self.little_word(record)
        sparse_pointer = self.little_word(record + 2)
        # The firmware seeds its inclusive address counter with 0x7f when
        # configuration bit 2 is set and 0xff otherwise.
        word_count = 128 if configuration & 0x04 else 256
        name, parameter_names = EFFECTS[number]
        return Algorithm(
            number,
            name,
            parameter_names,
            configuration,
            base_pointer,
            sparse_pointer,
            word_count,
        )

    def base_planes(self, algorithm: Algorithm) -> list[list[int]]:
        result: list[list[int]] = []
        for plane in range(2):
            start = algorithm.base_pointer + plane * algorithm.word_count * 2
            result.append(
                [self.big_word(start + index * 2) for index in range(algorithm.word_count)]
            )
        return result

    def sparse_patches(self, algorithm: Algorithm) -> list[SparsePatch]:
        result = []
        pointer = algorithm.sparse_pointer
        while self.byte(pointer) != 0xff:
            plane = self.byte(pointer) & 1
            address = self.byte(pointer + 1)
            word = self.big_word(pointer + 2)
            if address >= algorithm.word_count:
                raise ValueError(
                    f"algorithm {algorithm.number} patch address {address:#x} "
                    f"exceeds its {algorithm.word_count}-word plane"
                )
            result.append(SparsePatch(plane, address, word))
            pointer += 4
        return result

    def parameter_destinations(self, algorithm: Algorithm, parameter: int) -> list[Destination]:
        if not 0 <= parameter < 3:
            raise ValueError("parameter must be in the range 0..2")
        pointer = self.little_word(0x8055 + algorithm.number * 6 + parameter * 2)
        count = self.byte(pointer)
        result = [
            Destination(self.byte(pointer + 1 + index * 2) & 1, self.byte(pointer + 2 + index * 2))
            for index in range(count)
        ]
        terminator = self.byte(pointer + 1 + count * 2)
        if terminator != 0xff:
            raise ValueError(
                f"algorithm {algorithm.number} parameter {parameter + 1}: "
                f"destination list has bad terminator {terminator:#x}"
            )
        return result

    def parameter_words(
        self, algorithm: Algorithm, parameter: int, target_count: int, value: int
    ) -> list[int]:
        matrix = self.little_word(0x80bb + algorithm.number * 6 + parameter * 2)
        row = matrix + value * target_count * 2
        return [self.big_word(row + index * 2) for index in range(target_count)]

    def materialize(
        self, algorithm: Algorithm, values: tuple[int | None, int | None, int | None]
    ) -> tuple[list[list[int]], list[dict[str, object]]]:
        planes = self.base_planes(algorithm)
        operations: list[dict[str, object]] = []

        for patch in self.sparse_patches(algorithm):
            planes[patch.plane][patch.address] = patch.word
            operations.append(
                {
                    "kind": "sparse_patch",
                    "plane": patch.plane,
                    "address": patch.address,
                    "word": patch.word,
                }
            )

        for parameter, value in enumerate(values):
            if value is None:
                continue
            maximum = 31 if parameter == 2 else 7
            if not 0 <= value <= maximum:
                raise ValueError(
                    f"parameter {parameter + 1} must be in the range 0..{maximum}"
                )
            destinations = self.parameter_destinations(algorithm, parameter)
            words = self.parameter_words(algorithm, parameter, len(destinations), value)
            for destination, word in zip(destinations, words):
                if destination.address >= algorithm.word_count:
                    raise ValueError(
                        f"parameter destination {destination.address:#x} exceeds "
                        f"the {algorithm.word_count}-word plane"
                    )
                planes[destination.plane][destination.address] = word
                operations.append(
                    {
                        "kind": "parameter",
                        "parameter": parameter + 1,
                        "parameter_name": algorithm.parameter_names[parameter],
                        "value": value,
                        "plane": destination.plane,
                        "address": destination.address,
                        "word": word,
                    }
                )
        return planes, operations


def print_directory(image: K007Image) -> None:
    print("alg  cfg  words  base  patch  targets       name")
    for number in range(16):
        algorithm = image.algorithm(number)
        counts = [len(image.parameter_destinations(algorithm, index)) for index in range(3)]
        print(
            f"{number:2d}   {algorithm.configuration:02x}   {algorithm.word_count:3d}   "
            f"{algorithm.base_pointer:04x}  {algorithm.sparse_pointer:04x}   "
            f"{counts[0]:2d}/{counts[1]:2d}/{counts[2]:2d}       {algorithm.name}"
        )


def print_algorithm(image: K007Image, algorithm: Algorithm) -> None:
    print(f"Algorithm {algorithm.number}: {algorithm.name}")
    print(
        f"configuration={algorithm.configuration:#04x}, words/plane={algorithm.word_count}, "
        f"base={algorithm.base_pointer:04x}, sparse={algorithm.sparse_pointer:04x}"
    )
    patches = image.sparse_patches(algorithm)
    print(f"Sparse patches ({len(patches)}):")
    for patch in patches:
        print(f"  plane {patch.plane} [{patch.address:02x}] = {patch.word:04x}")
    for parameter, name in enumerate(algorithm.parameter_names):
        destinations = image.parameter_destinations(algorithm, parameter)
        rendered = ", ".join(
            f"p{destination.plane}:{destination.address:02x}" for destination in destinations
        )
        print(f"Parameter {parameter + 1} ({name}, {len(destinations)} targets): {rendered}")


def write_outputs(
    output_directory: Path,
    algorithm: Algorithm,
    planes: list[list[int]],
    values: tuple[int | None, int | None, int | None],
    operations: list[dict[str, object]],
) -> None:
    output_directory.mkdir(parents=True, exist_ok=True)
    stem = f"k007_algorithm_{algorithm.number:02d}"
    for plane_number, words in enumerate(planes):
        payload = b"".join(struct.pack(">H", word) for word in words)
        (output_directory / f"{stem}_plane{plane_number}.bin").write_bytes(payload)

    manifest = {
        "algorithm": algorithm.number,
        "name": algorithm.name,
        "configuration": algorithm.configuration,
        "word_count_per_plane": algorithm.word_count,
        "base_pointer": algorithm.base_pointer,
        "sparse_pointer": algorithm.sparse_pointer,
        "parameter_values": {
            name: value for name, value in zip(algorithm.parameter_names, values)
        },
        "word_overlays": operations,
        "word_byte_order": "big-endian (F008 high, F009 low)",
    }
    (output_directory / f"{stem}.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path, help="64 KiB K4 code ROM (for example k4_v14.u8)")
    parser.add_argument("--algorithm", "-a", type=int, choices=range(16))
    parser.add_argument("--param1", type=int)
    parser.add_argument("--param2", type=int)
    parser.add_argument("--param3", type=int)
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="write materialized plane binaries and a JSON manifest",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        image = K007Image(args.rom.read_bytes())
        values = (args.param1, args.param2, args.param3)
        if args.algorithm is None:
            if args.output_dir is not None or any(value is not None for value in values):
                raise ValueError("--algorithm is required with parameters or --output-dir")
            print_directory(image)
            return 0

        algorithm = image.algorithm(args.algorithm)
        print_algorithm(image, algorithm)
        planes, operations = image.materialize(algorithm, values)
        if args.output_dir is not None:
            write_outputs(args.output_dir, algorithm, planes, values, operations)
            print(f"Wrote materialized upload image to {args.output_dir}")
        return 0
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
