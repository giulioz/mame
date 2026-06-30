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


def write_probe_midi(path: Path, note: int) -> None:
    events = b"".join(
        (
            event(0, bytes((0x90, note, 110))),
            event(1536, bytes((0x80, note, 0))),
            event(0, b"\xff\x2f\x00"),
        )
    )
    header = b"MThd" + (6).to_bytes(4, "big") + b"\x00\x00\x00\x01\x00\x60"
    path.write_bytes(header + b"MTrk" + len(events).to_bytes(4, "big") + events)


def window_rms(path: Path, start: float, end: float) -> float:
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

    return rms


def extract_channel(source_path: Path, destination_path: Path, channel: int,
                    start: float = 9.9, end: float = 16.5) -> None:
    with wave.open(str(source_path), "rb") as source:
        rate = source.getframerate()
        channels = source.getnchannels()
        source.setpos(int(start * rate))
        interleaved = array.array("h", source.readframes(int((end - start) * rate)))
    if sys.byteorder != "little":
        interleaved.byteswap()
    samples = array.array("h", interleaved[channel::channels])
    if sys.byteorder != "little":
        samples.byteswap()
    with wave.open(str(destination_path), "wb") as destination:
        destination.setnchannels(1)
        destination.setsampwidth(2)
        destination.setframerate(rate)
        destination.writeframes(samples.tobytes())


def channel_stats(path: Path, channel: int, start: float, end: float,
                  frequencies: tuple[float, ...]) -> tuple[float, float, list[float]]:
    with wave.open(str(path), "rb") as source:
        rate = source.getframerate()
        channels = source.getnchannels()
        source.setpos(int(start * rate))
        interleaved = array.array("h", source.readframes(int((end - start) * rate)))
    if sys.byteorder != "little":
        interleaved.byteswap()
    samples = interleaved[channel::channels]
    dc = sum(samples) / len(samples) / 32768.0
    peak = max(abs(sample) for sample in samples) / 32768.0

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
    return dc, peak, amplitudes


def main() -> int:
    repository = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description="Run the D-70 Schizoid pitch regression")
    parser.add_argument("--mame", type=Path, default=repository / "mame")
    parser.add_argument("--rompath", type=Path, default=repository)
    parser.add_argument("--nvram-directory", type=Path, default=repository / "nvram")
    parser.add_argument("--note", type=int, default=36)
    parser.add_argument("--output-directory", type=Path,
                        help="keep the final, raw LP, and trace artifacts here")
    args = parser.parse_args()

    source_nvram = args.nvram_directory.resolve() / "d70"
    if not source_nvram.is_dir():
        parser.error(f"factory D-70 NVRAM not found at {source_nvram}")

    lua = repository / "scripts/tests/d70_demo_trace.lua"
    with tempfile.TemporaryDirectory(prefix="mame-d70-schizoid-") as temporary:
        root = Path(temporary)
        midi = root / "schizoid.mid"
        wav = root / "schizoid.wav"
        lp_wav = root / "schizoid-lp.wav"
        tvf_wav = root / "schizoid-tvf.wav"
        write_probe_midi(midi, args.note)
        test_nvram = root / "nvram"
        shutil.copytree(source_nvram, test_nvram / "d70")

        environment = os.environ.copy()
        environment["D70_TRACE_SCHIZOID"] = "1"
        environment["D70_TRACE_VOICE_STATE"] = "1"
        environment["ROLAND_LP_WAV"] = str(lp_wav)
        environment["D70_TVF_WAV"] = str(tvf_wav)
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
        rms = window_rms(wav, 11.0, 16.0) if wav.is_file() else 0.0
        dlm1 = channel_stats(lp_wav, 1, 11.0, 16.0, (32.563, 65.126)) if lp_wav.is_file() else (0.0, 1.0, [0.0, 1.0])
        dlm2 = channel_stats(lp_wav, 2, 10.05, 10.4, (16.6, 33.2)) if lp_wav.is_file() else (1.0, 1.0, [0.0, 1.0])
        piano = channel_stats(lp_wav, 3, 10.2, 13.0, (65.406,)) if lp_wav.is_file() else (0.0, 1.0, [0.0])
        if args.output_directory:
            args.output_directory.mkdir(parents=True, exist_ok=True)
            for source in (wav, lp_wav, tvf_wav):
                if source.is_file():
                    shutil.copy2(source, args.output_directory / source.name)
            for source, suffix in ((lp_wav, "LP"), (tvf_wav, "TVF")):
                if source.is_file():
                    extract_channel(source, args.output_directory / f"DLM_1_note_{args.note}_{suffix}.wav", 1)
                    extract_channel(source, args.output_directory / f"DLM_2_note_{args.note}_{suffix}.wav", 2)
                    extract_channel(source, args.output_directory / f"A_Piano_f1_note_{args.note}_{suffix}.wav", 3)
            (args.output_directory / "trace.log").write_text(result.stdout)

    print(result.stdout, end="")
    print(f"D70SCHIZOID AUDIO rms={rms:.6f} "
          f"dlm1_c1={dlm1[2][0]:.6f} dlm1_octave={dlm1[2][1]:.6f} "
          f"dlm2_c0={dlm2[2][0]:.6f} dlm2_octave={dlm2[2][1]:.6f} "
          f"dlm2_dc={dlm2[0]:+.6f} dlm2_peak={dlm2[1]:.6f} "
          f"piano_c2={piano[2][0]:.6f}")
    match = re.search(r"^D70SCHIZOID PASS=(\d)", result.stdout, re.MULTILINE)
    expected_sources = (
        re.search(r"^D70DEMO SOURCE v=01 .*name='DLM 1 +'.*descriptor=a5f70f4300400040474e$", result.stdout, re.MULTILINE),
        re.search(r"^D70DEMO SOURCE v=02 .*name='DLM 2 +'.*descriptor=958e8cab2ea82e405b40$", result.stdout, re.MULTILINE),
        re.search(r"^D70DEMO SOURCE v=03 .*name='A\.Piano f1'.*descriptor=f5bf00e77fbc3c403248$", result.stdout, re.MULTILINE),
    )
    if (result.returncode != 0 or not match or match.group(1) != "1"
            or not all(expected_sources) or rms < 0.005
            or dlm1[2][0] < 0.01 or dlm1[2][0] <= 1.5 * dlm1[2][1]
            or dlm2[2][0] < 0.005 or dlm2[2][0] <= 1.5 * dlm2[2][1]
            or abs(dlm2[0]) > 0.02 or dlm2[1] >= 0.9
            or piano[2][0] < 0.0003):
        print("D-70 Schizoid pitch regression failed", file=sys.stderr)
        return result.returncode or 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
