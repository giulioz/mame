# JV-1080 firmware and XP reverse engineering

This directory holds reproducible analysis tooling and evidence for the JV-1080
SH7034 firmware.  Conclusions belong in `xp_firmware_map.md`; generated Ghidra
projects and emulator traces stay outside the repository.

## Headless Ghidra import

```sh
GHIDRA="$HOME/personal/old pc/giuliozausa/Downloads/ghidra_11.4_PUBLIC"
rm -rf /tmp/jv1080-ghidra
"$GHIDRA/support/analyzeHeadless" /tmp/jv1080-ghidra JV1080 \
  -import jv1080/roland_r00677323_6437034c12f.ic15 \
  -loader BinaryLoader -loader-baseAddr 0 -processor 'SuperH:BE:32:SH-1' \
  -scriptPath jv1080/re/ghidra \
  -preScript ImportJV1080.py "$PWD/jv1080/roland_r00678167.ic20"

rm -rf /tmp/jv1080-ghidra-export
"$GHIDRA/support/analyzeHeadless" /tmp/jv1080-ghidra JV1080 \
  -process roland_r00677323_6437034c12f.ic15 -noanalysis \
  -scriptPath jv1080/re/ghidra -postScript NameJV1080.py \
  -postScript ExportJV1080.py /tmp/jv1080-ghidra-export
```

## XP access traces

```sh
JV1080_SCENARIO=demo JV1080_XP_TRACE=/tmp/jv1080_xp_demo.csv \
  ./jv1080sh jv1080 -rompath . -video none -sound none -nothrottle \
  -skip_gameinfo -seconds_to_run 30 -autoboot_delay 0.000001 \
  -autoboot_script jv1080/re/lua/xp_trace.lua
```

Valid scenarios are `boot`, `modulation`, and `demo`.

Set `JV1080_TRACE_REGS=1` to append R0-R14 to timeline records and
`JV1080_TRACE_AFTER=12` to reserve the timeline for post-boot accesses.
Set `JV1080_TRACE_IRQ_ONLY=1`, `JV1080_TRACE_VOICE_STATUS=1`, or
`JV1080_TRACE_DSP_READS=1` for compact IRQ, voice-command, or DSP-trigger/
readback captures respectively.
Summarize a capture with:

```sh
python3 jv1080/re/analyze_trace.py /tmp/jv1080_xp_demo.csv --after 12 \
  --functions /tmp/jv1080-ghidra-export/functions.tsv
```

Probe the IRAM3 current/target/rate banks and their normal DSP readback path
with an otherwise unused slot:

```sh
./jv1080sh jv1080 -rompath . -video none -sound none -nothrottle \
  -skip_gameinfo -seconds_to_run 15 -autoboot_delay 0.000001 \
  -autoboot_script jv1080/re/lua/iram3_breakpoint_probe.lua
```

The script checks both ramp directions, target clamping, and that a trigger
read returns the evolving 32-bit current through `0x3912:0x3910`.

Use `--bank 0x2000` to isolate one 256-byte XP bank.

After a boot capture, verify host wave-ROM transactions against the raw ROMs:

```sh
python3 jv1080/re/verify_wave_reads.py /tmp/jv1080_xp_boot.csv
```

Inventory the firmware's XP DSP program/coefficient selector table with:

```sh
python3 jv1080/re/extract_dsp_templates.py > /tmp/jv1080_dsp_templates.tsv
```

Decode the confirmed XP instruction fields and normalized CRAM values with:

```sh
python3 jv1080/re/analyze_dsp_bytecode.py --effect "Stereo EQ" --listing
python3 jv1080/re/analyze_dsp_bytecode.py --factory-test
python3 jv1080/re/analyze_dsp_bytecode.py > /tmp/jv1080_dsp_static_summary.tsv
```

The complete front-panel RFX sweep edits twelve field positions for each of
the 40 algorithms.  It releases and retriggers a Preview note after every edit,
and exits MAME when the CSV is complete:

```sh
JV1080_RFX_SWEEP=/tmp/jv1080_rfx_parameter_sweep.csv \
  ./jv1080sh jv1080 -rompath . -video none -sound none -nothrottle \
  -skip_gameinfo -seconds_to_run 2000 -autoboot_delay 0.000001 \
  -autoboot_script jv1080/re/lua/rfx_parameter_sweep.lua

python3 jv1080/re/analyze_rfx_sweep.py \
  /tmp/jv1080_rfx_parameter_sweep.csv > /tmp/jv1080_rfx_parameter_summary.tsv
```

Set `JV1080_RFX_FIRST`, `JV1080_RFX_COUNT`, and `JV1080_RFX_SNAP` to limit a
run or save one LCD screenshot per parameter page.  See `xp_dsp_isa.md` for the
current decoder, CSP/ESP comparison, and algorithm findings.

The factory-patch topology sweep is:

```sh
JV1080_STRUCTURE_SWEEP=/tmp/jv1080_structure_sweep.csv \
  ./jv1080sh jv1080 -rompath . -video none -sound none -nothrottle \
  -skip_gameinfo -seconds_to_run 70 -autoboot_delay 0.000001 \
  -autoboot_script jv1080/re/lua/structure_sweep.lua
```

The exact Structure/Booster edit sweep (PR-A:030 with a paired tone held
constant) is:

```sh
JV1080_STRUCTURE_EXACT=/tmp/jv1080_structure_exact.csv \
  ./jv1080sh jv1080 -rompath . -video none -sound none -nothrottle \
  -skip_gameinfo -seconds_to_run 45 -autoboot_delay 0.000001 \
  -autoboot_script jv1080/re/lua/structure_exact_sweep.lua
```

See `xp_firmware_map.md` for the firmware map and `xp_dsp_isa.md` for the DSP
encoding/algorithm investigation.

## Real-silicon XP host interface (debug ROM)

A MIDI-sysex debug ROM gives direct read/write access to the XP over MIDI, and
the first silicon measurements confirmed the DSP host interface — see
`XP_HARDWARE_DEBUG.md §6` (and `debugrom/README.md`):

- DSP memory is **not** directly readable; a PEEK returns 0 but latches the
  value into the host readback register `0x3910` (low 16) / `0x3912` (high 16).
  The DSP **control** registers (`0x3908/0x3914/0x3916/0x3924`) are write-only.
- `0x3916` is the DSP **run/stop** control (`7` = run, `0` = stop).
- The SH firmware writes the DSP area **only at boot** (~0.4–1.3 s) then goes
  idle, so at idle the DSP program is stable and host-overwritable — but any
  experiment must use **no note-on and no Program Change** (both re-upload it).

`debugrom/xp_lab.py` is the `XPDSP` host harness that follows this methodology
(clear → upload → monitor via the readback register); `sh1dis.py` is an SH-1
disassembler with literal-pool resolution.

The hidden manufacturing diagnostics, their firmware dispatchers, and the
reproducible `factory_test_probe.lua` sequences are documented in
`factory_test_mode.md`.

`DecompileJV1080.py` exports bounded call neighborhoods; when exact SH
instructions are needed to audit decompiler output, use
`ExportDisassembly.py output hex-address...` as another post-script.
