#!/usr/bin/env python3
"""Export every Kawai K4 PCM key zone as a looped mono WAV file."""

from __future__ import annotations

import argparse
import csv
import struct
from pathlib import Path

from k4_k002_descriptors import Descriptor, note_name, parse_directory


WAVE_ROM_SIZE = 0x80000


def load_rom(path: Path, label: str) -> bytes:
    data = path.read_bytes()
    if len(data) != WAVE_ROM_SIZE:
        raise ValueError(f"{label} must be exactly 512 KiB, got {len(data):#x} bytes")
    return data


def signed_byte(value: int) -> int:
    return value - 0x100 if value & 0x80 else value


def sample_at(descriptor: Descriptor, address: int, p202: bytes, p203: bytes, p204: bytes) -> int:
    if descriptor.sample_format == "pcm8":
        return signed_byte(p202[address]) << 8

    word = (p203[address] << 8) | p204[address]
    return word - 0x10000 if word & 0x8000 else word


def extract_samples(descriptor: Descriptor, p202: bytes, p203: bytes, p204: bytes) -> list[int]:
    step = -1 if descriptor.direction == "reverse" else 1
    stop = descriptor.end + step
    return [sample_at(descriptor, address, p202, p203, p204) for address in range(descriptor.start, stop, step)]


def riff_chunk(chunk_id: bytes, payload: bytes) -> bytes:
    padding = b"\x00" if len(payload) & 1 else b""
    return chunk_id + struct.pack("<I", len(payload)) + payload + padding


def wav_bytes(samples: list[int], sample_rate: int, root_key: int, loop_start: int, loop_end: int) -> bytes:
    fmt = struct.pack("<HHIIHH", 1, 1, sample_rate, sample_rate * 2, 2, 16)
    sample_data = struct.pack(f"<{len(samples)}h", *samples)

    # The K002 descriptor endpoints and the RIFF smpl loop endpoints are both
    # inclusive.  Reverse descriptors have already been put into playback
    # order, so their loop can be represented as a normal forward WAV loop.
    smpl_header = struct.pack(
        "<9I",
        0,
        0,
        round(1_000_000_000 / sample_rate),
        root_key,
        0,
        0,
        0,
        1,
        0,
    )
    smpl_loop = struct.pack("<6I", 0, 0, loop_start, loop_end, 0, 0)

    body = riff_chunk(b"fmt ", fmt) + riff_chunk(b"smpl", smpl_header + smpl_loop)
    body += riff_chunk(b"data", sample_data)
    return b"RIFF" + struct.pack("<I", len(body) + 4) + b"WAVE" + body


def safe_note_name(note: int) -> str:
    return note_name(note).replace("#", "s")


def output_name(descriptor: Descriptor) -> str:
    return (
        f"wave{descriptor.wave_index + 1:03d}_zone{descriptor.zone_index:02d}_"
        f"{safe_note_name(descriptor.root_key)}_{descriptor.sample_format}_"
        f"{descriptor.direction}.wav"
    )


def export_samples(
    descriptors: list[Descriptor],
    p202: bytes,
    p203: bytes,
    p204: bytes,
    output: Path,
    sample_rate: int,
) -> None:
    output.mkdir(parents=True, exist_ok=True)
    manifest_path = output / "manifest.csv"
    fields = (
        "file",
        "wave_index",
        "wave_number",
        "zone",
        "root_key",
        "root_note",
        "format",
        "direction",
        "loop_type",
        "flags",
        "start",
        "loop",
        "end",
        "frames",
        "wav_loop_start",
        "wav_loop_end",
    )

    with manifest_path.open("w", newline="", encoding="utf-8") as manifest:
        writer = csv.DictWriter(manifest, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        for descriptor in descriptors:
            samples = extract_samples(descriptor, p202, p203, p204)
            loop_start = abs(descriptor.loop - descriptor.start)
            loop_end = abs(descriptor.end - descriptor.start)
            filename = output_name(descriptor)
            (output / filename).write_bytes(
                wav_bytes(samples, sample_rate, descriptor.root_key, loop_start, loop_end)
            )
            writer.writerow(
                {
                    "file": filename,
                    "wave_index": descriptor.wave_index,
                    "wave_number": descriptor.wave_index + 1,
                    "zone": descriptor.zone_index,
                    "root_key": descriptor.root_key,
                    "root_note": note_name(descriptor.root_key),
                    "format": descriptor.sample_format,
                    "direction": descriptor.direction,
                    "loop_type": descriptor.loop_type,
                    "flags": f"0x{descriptor.flags:02x}",
                    "start": f"0x{descriptor.start:05x}",
                    "loop": f"0x{descriptor.loop:05x}",
                    "end": f"0x{descriptor.end:05x}",
                    "frames": len(samples),
                    "wav_loop_start": loop_start,
                    "wav_loop_end": loop_end,
                }
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("code_rom", type=Path, help="64 KiB K4/K4r firmware ROM")
    parser.add_argument("p202", type=Path, help="512 KiB 8-bit wave ROM (K4 U30 / K4r U40)")
    parser.add_argument("p203", type=Path, help="512 KiB 16-bit MSB wave ROM (K4 U29 / K4r U39)")
    parser.add_argument("p204", type=Path, help="512 KiB 16-bit LSB wave ROM (K4 U31 / K4r U38)")
    parser.add_argument("output", type=Path, help="output directory")
    parser.add_argument("--sample-rate", type=int, default=32_000)
    parser.add_argument(
        "--wave",
        type=lambda value: int(value, 0),
        help="only export one zero-based patch wave index (96..255)",
    )
    args = parser.parse_args()

    if args.sample_rate <= 0:
        parser.error("--sample-rate must be positive")

    try:
        descriptors = parse_directory(args.code_rom.read_bytes())
        if args.wave is not None:
            descriptors = [item for item in descriptors if item.wave_index == args.wave]
            if not descriptors:
                raise ValueError("--wave must select a PCM wave in the range 96..255")
        export_samples(
            descriptors,
            load_rom(args.p202, "P202"),
            load_rom(args.p203, "P203"),
            load_rom(args.p204, "P204"),
            args.output,
            args.sample_rate,
        )
    except (OSError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
