#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders:giulioz

import argparse
import array
import math
import re
import shutil
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

from run_d70_pitch_regression import event


def write_probe_midi(path: Path) -> None:
    notes = range(48, 55)
    events = b"".join(event(0, bytes((0x90, note, 100))) for note in notes)
    events += event(192, bytes((0x80, 48, 0)))
    events += b"".join(event(0, bytes((0x80, note, 0))) for note in range(49, 54))
    events += event(576, bytes((0x80, 54, 0)))
    events += event(0, b"\xff\x2f\x00")
    header = b"MThd" + (6).to_bytes(4, "big") + b"\x00\x00\x00\x01\x00\x60"
    path.write_bytes(header + b"MTrk" + len(events).to_bytes(4, "big") + events)


def window_stats(path: Path, start: float, end: float) -> tuple[float, float, float]:
    with wave.open(str(path), "rb") as source:
        if source.getsampwidth() != 2:
            raise ValueError("expected a 16-bit MAME WAV")
        rate = source.getframerate()
        channels = source.getnchannels()
        source.setpos(int(start * rate))
        samples = array.array("h", source.readframes(int((end - start) * rate)))
    if sys.byteorder != "little":
        samples.byteswap()
    rms = math.sqrt(sum(sample * sample for sample in samples) / len(samples)) / 32768.0
    dc = sum(samples) / len(samples) / 32768.0
    peak = max(abs(sample) for sample in samples) / 32768.0
    return rms, dc, peak


def main() -> int:
    repository = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description="Run the D-70 ChorusBell loop/RCC regression")
    parser.add_argument("--mame", type=Path, default=repository / "mame")
    parser.add_argument("--rompath", type=Path, default=repository)
    parser.add_argument("--nvram-directory", type=Path, default=repository / "nvram")
    args = parser.parse_args()

    source_nvram = args.nvram_directory.resolve() / "d70"
    if not source_nvram.is_dir():
        parser.error(f"factory D-70 NVRAM not found at {source_nvram}")

    lua = repository / "scripts/tests/d70_demo_trace.lua"
    with tempfile.TemporaryDirectory(prefix="mame-d70-chorusbell-") as temporary:
        root = Path(temporary)
        midi = root / "chorusbell.mid"
        wav = root / "chorusbell.wav"
        write_probe_midi(midi)
        test_nvram = root / "nvram"
        shutil.copytree(source_nvram, test_nvram / "d70")

        command = (
            str(args.mame.resolve()), "d70",
            "-rompath", str(args.rompath.resolve()),
            "-nvram_directory", str(test_nvram),
            "-cfg_directory", str(root / "cfg"),
            "-video", "none", "-nothrottle", "-skip_gameinfo",
            "-seconds_to_run", "17", "-wavwrite", str(wav),
            "-midiin", str(midi), "-autoboot_script", str(lua),
        )
        result = subprocess.run(
            command, cwd=repository, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, timeout=60, check=False,
        )
        rms, dc, peak = window_stats(wav, 11.2, 13.5) if wav.is_file() else (0.0, 0.0, 0.0)

    print(result.stdout, end="")
    print(f"D70CHORUSBELL AUDIO rms={rms:.6f} dc={dc:+.6f} peak={peak:.6f}")
    match = re.search(r"^D70CHORUSBELL PASS=(\d)", result.stdout, re.MULTILINE)
    if (result.returncode != 0 or not match or match.group(1) != "1"
            or rms < 0.012 or abs(dc) > 0.002 or peak >= 0.98):
        print("D-70 ChorusBell loop/RCC regression failed", file=sys.stderr)
        return result.returncode or 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
