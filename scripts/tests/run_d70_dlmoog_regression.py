#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders:giulioz

import argparse
import array
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

from run_d70_pitch_regression import event


def write_probe_midi(path: Path) -> None:
    events = b"".join(
        (
            event(0, bytes((0x90, 36, 110))),
            event(1536, bytes((0x80, 36, 0))),
            event(0, b"\xff\x2f\x00"),
        )
    )
    header = b"MThd" + (6).to_bytes(4, "big") + b"\x00\x00\x00\x01\x00\x60"
    path.write_bytes(header + b"MTrk" + len(events).to_bytes(4, "big") + events)


def window_stats(path: Path, start: float, end: float) -> tuple[float, float, float, float, float, float]:
    with wave.open(str(path), "rb") as source:
        if source.getsampwidth() != 2:
            raise ValueError("expected a 16-bit MAME WAV")
        rate = source.getframerate()
        channels = source.getnchannels()
        source.setpos(int(start * rate))
        interleaved = array.array("h", source.readframes(int((end - start) * rate)))
    if sys.byteorder != "little":
        interleaved.byteswap()
    mono = [sum(interleaved[index:index + channels]) / channels
            for index in range(0, len(interleaved), channels)]
    rms = math.sqrt(sum(sample * sample for sample in mono) / len(mono)) / 32768.0
    dc = sum(mono) / len(mono) / 32768.0
    peak = max(abs(sample) for sample in mono) / 32768.0

    # Note 36 programs octave-related DLM voices.  The first byte at the loop
    # point is the predictor anchor, so the repeating four-byte differential
    # is -113.  Wrapping that sum in the signed 12-bit DLM lane puts the two
    # programmed steps near C0/C1 without a second phase correction.
    def amplitude(frequency: float) -> float:
        omega = 2.0 * math.pi * frequency / rate
        coefficient = 2.0 * math.cos(omega)
        previous = 0.0
        previous2 = 0.0
        for sample in mono:
            current = sample + coefficient * previous - previous2
            previous2 = previous
            previous = current
        power = previous2 * previous2 + previous * previous - coefficient * previous * previous2
        return 2.0 * math.sqrt(max(power, 0.0)) / len(mono) / 32768.0

    fundamental = max(amplitude(16.28), amplitude(32.62))
    old_flat_pitch = max(amplitude(29.5), amplitude(59.0))
    wrong_carrier = max(amplitude(295.0), amplitude(593.0))
    return rms, dc, peak, fundamental, old_flat_pitch, wrong_carrier


def channel_amplitudes(path: Path, channel: int, start: float, end: float,
                       frequencies: tuple[float, ...]) -> list[float]:
    with wave.open(str(path), "rb") as source:
        rate = source.getframerate()
        channels = source.getnchannels()
        source.setpos(int(start * rate))
        interleaved = array.array("h", source.readframes(int((end - start) * rate)))
    if sys.byteorder != "little":
        interleaved.byteswap()
    samples = interleaved[channel::channels]

    amplitudes = []
    for frequency in frequencies:
        coefficient = 2.0 * math.cos(2.0 * math.pi * frequency / rate)
        previous = 0.0
        previous2 = 0.0
        for sample in samples:
            current = sample + coefficient * previous - previous2
            previous2 = previous
            previous = current
        power = previous2 * previous2 + previous * previous - coefficient * previous * previous2
        amplitudes.append(2.0 * math.sqrt(max(power, 0.0)) / len(samples) / 32768.0)
    return amplitudes


def main() -> int:
    repository = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description="Run the D-70 DLMoogBs1 regression")
    parser.add_argument("--mame", type=Path, default=repository / "mame")
    parser.add_argument("--rompath", type=Path, default=repository)
    parser.add_argument("--nvram-directory", type=Path, default=repository / "nvram")
    args = parser.parse_args()

    source_nvram = args.nvram_directory.resolve() / "d70"
    if not source_nvram.is_dir():
        parser.error(f"factory D-70 NVRAM not found at {source_nvram}")

    lua = repository / "scripts/tests/d70_demo_trace.lua"
    with tempfile.TemporaryDirectory(prefix="mame-d70-dlmoog-") as temporary:
        root = Path(temporary)
        midi = root / "dlmoog.mid"
        wav = root / "dlmoog.wav"
        lp_wav = root / "dlmoog-lp.wav"
        write_probe_midi(midi)
        test_nvram = root / "nvram"
        shutil.copytree(source_nvram, test_nvram / "d70")

        environment = os.environ.copy()
        environment["D70_TRACE_DLMOOG"] = "1"
        environment["D70_TRACE_VOICE_STATE"] = "1"
        environment["ROLAND_LP_WAV"] = str(lp_wav)
        command = (
            str(args.mame.resolve()), "d70",
            "-rompath", str(args.rompath.resolve()),
            "-nvram_directory", str(test_nvram),
            "-cfg_directory", str(root / "cfg"),
            "-video", "none", "-nothrottle", "-skip_gameinfo",
            "-seconds_to_run", "19", "-wavwrite", str(wav),
            "-midiin", str(midi), "-autoboot_script", str(lua),
        )
        result = subprocess.run(
            command, cwd=repository, env=environment, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, timeout=60, check=False,
        )
        stats = window_stats(wav, 11.0, 16.0) if wav.is_file() else (0.0,) * 6
        expected_frequencies = (16.2860, 32.5047, 16.2725, 32.7337)
        voice_amplitudes = [
            channel_amplitudes(lp_wav, voice, 11.0, 16.0,
                               (expected_frequencies[voice - 1],
                                expected_frequencies[voice - 1] * 2.0))
            for voice in range(1, 5)
        ] if lp_wav.is_file() else [[0.0, 1.0] for _ in range(4)]

    rms, dc, peak, fundamental, old_flat_pitch, wrong_carrier = stats
    print(result.stdout, end="")
    print(f"D70DLMOOG AUDIO rms={rms:.6f} dc={dc:+.6f} peak={peak:.6f} "
          f"fundamental={fundamental:.6f} old_flat={old_flat_pitch:.6f} "
          f"wrong_carrier={wrong_carrier:.6f} "
          + " ".join(f"v{voice}={values[0]:.6f}/{values[1]:.6f}"
                     for voice, values in enumerate(voice_amplitudes, 1)))
    match = re.search(r"^D70DLMOOG PASS=(\d)", result.stdout, re.MULTILINE)
    expected_sources = (
        re.search(r"^D70DEMO SOURCE v=01 .*name='DLM BASS 1'.*$", result.stdout, re.MULTILINE),
        re.search(r"^D70DEMO SOURCE v=02 .*name='DLM BASS 1'.*$", result.stdout, re.MULTILINE),
        re.search(r"^D70DEMO SOURCE v=03 .*name='DLM BASS 2'.*$", result.stdout, re.MULTILINE),
        re.search(r"^D70DEMO SOURCE v=04 .*name='DLM BASS 2'.*$", result.stdout, re.MULTILINE),
    )
    if (result.returncode != 0 or not match or match.group(1) != "1"
            or not all(expected_sources)
            or rms < 0.05 or abs(dc) > 0.02 or peak >= 0.98
            or fundamental < 0.003 or fundamental <= old_flat_pitch
            or fundamental <= wrong_carrier
            or any(expected < 0.01 or expected <= 1.5 * octave
                   for expected, octave in voice_amplitudes)):
        print("D-70 DLMoogBs1 regression failed", file=sys.stderr)
        return result.returncode or 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
