#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders:giulioz

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def variable_length(value: int) -> bytes:
    result = [value & 0x7f]
    value >>= 7
    while value:
        result.append(0x80 | (value & 0x7f))
        value >>= 7
    return bytes(reversed(result))


def event(delta: int, message: bytes) -> bytes:
    return variable_length(delta) + message


def write_probe_midi(path: Path) -> None:
    events = b"".join(
        (
            event(0, bytes((0x90, 60, 100))),
            event(96, bytes((0xE0, 0x7F, 0x7F))),
            event(96, bytes((0xE0, 0x00, 0x00))),
            event(96, bytes((0xE0, 0x00, 0x40))),
            event(96, bytes((0xB0, 0x01, 0x7F))),
            event(192, bytes((0xB0, 0x01, 0x00))),
            event(96, bytes((0x80, 60, 0))),
            event(0, b"\xff\x2f\x00"),
        )
    )
    header = b"MThd" + (6).to_bytes(4, "big") + b"\x00\x00\x00\x01\x00\x60"
    path.write_bytes(header + b"MTrk" + len(events).to_bytes(4, "big") + events)


def main() -> int:
    repository = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description="Run the D-70 firmware/LP pitch regression")
    parser.add_argument("--mame", type=Path, default=repository / "mame")
    parser.add_argument("--rompath", type=Path, default=repository)
    parser.add_argument("--nvram-directory", type=Path, default=repository / "nvram")
    args = parser.parse_args()

    source_nvram = args.nvram_directory.resolve() / "d70"
    if not source_nvram.is_dir():
        parser.error(f"factory D-70 NVRAM not found at {source_nvram}")

    lua = repository / "scripts/tests/d70_pitch_regression.lua"
    with tempfile.TemporaryDirectory(prefix="mame-d70-pitch-") as temporary:
        root = Path(temporary)
        midi = root / "pitch_probe.mid"
        write_probe_midi(midi)
        test_nvram = root / "nvram"
        shutil.copytree(source_nvram, test_nvram / "d70")

        command = (
            str(args.mame.resolve()),
            "d70",
            "-rompath", str(args.rompath.resolve()),
            "-nvram_directory", str(test_nvram),
            "-cfg_directory", str(root / "cfg"),
            "-video", "none",
            "-nothrottle",
            "-skip_gameinfo",
            "-seconds_to_run", "19",
            "-midiin", str(midi),
            "-autoboot_script", str(lua),
        )
        result = subprocess.run(
            command,
            cwd=repository,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=60,
            check=False,
        )

    print(result.stdout, end="")
    match = re.search(r"^D70PITCH PASS=(\d)", result.stdout, re.MULTILINE)
    if result.returncode != 0:
        print(f"D-70 process exited with status {result.returncode}", file=sys.stderr)
        return result.returncode or 1
    if not match or match.group(1) != "1":
        print("D-70 pitch regression failed", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
