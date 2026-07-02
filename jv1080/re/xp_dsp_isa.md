# Roland XP effects DSP: instruction and algorithm map

This is a working specification assembled from the JV-1080 firmware, complete
40-algorithm template inventory, retriggered front-panel sweeps, SCCore's
preserved XP interface, and Gearmulator's Roland CSP/ESP interpreters.  Field
encodings are marked separately from opcode semantics: knowing where an opcode
lives does not yet prove what that opcode does on XP silicon.

> **Status note.** The XP **host interface** is now confirmed on real silicon via the
> MIDI debug ROM (`XP_HARDWARE_DEBUG.md §6`): DSP memory is not directly readable — a
> PEEK returns 0 but latches the value into the readback register `0x3910` (low 16) /
> `0x3912` (high 16); the DSP control registers are write-only; `0x3916` is the DSP
> run/stop control (`7` = run, `0` = stop); the SH firmware writes the DSP area only at
> boot (~0.4–1.3 s) then goes idle; and IRAM3 (`0x3200`) is "ramp-magic" (auto-updated by
> the breakpoint-ramp engine — use IRAM1/2 as the clean observable).
>
> **The instruction ISA is now largely decoded** — see the companion spec
> [`xp_dsp_isa_decoded.md`](xp_dsp_isa_decoded.md) (rewritten 2026-07-02), built from the SCCore
> `SystemEffects_process` ↔ `scgsMaster.txt` alignment plus JV hardware probes. The field split
> below is SUPERSEDED: the real layout is **`[15:14]` store-control / `[13:0]` address
> (word `[13:6]` | column `[5:0]`)**, with the hi bytes as the parallel ERAM channel; the old
> "op nibble" was store-control‖addr-top and "[11:9] store mode" turned out to be address bits
> (XP_FACTS C22–C24). The tables below are kept as the historical JV-side evidence record.

## Architecture established so far

The XP has 288 paired slots.  Each slot consists of one 32-bit PRAM word and
one 16-bit CRAM word.  The JV uses slots 0-103 for the selected Insert EFX,
104-255 for the fixed system-effects/mixer program, and leaves 256-287 as
reserve.  The host-visible internal state is three 64-word IRAM banks at
`0x3000`, `0x3100`, and `0x3200`; external 2-Mbit DRAM supplies the long delay
memory.

### IRAM3 breakpoint interpolation

IRAM3 has a paired target bank that the earlier `routing/configuration`
description obscured:

| Address | Width | Function |
|---:|---:|---|
| `0x3200 + 4*i` | 32 | IRAM3 current value, host-visible Q22 |
| `0x3300 + 2*i` | 16 | 9-bit breakpoint target |
| `0x3928 + 2*(i >> 4)` | 16 | interpolation rate for each 16-slot group |

SCCore's `_TGXpDsp_writeIramBp` converts a target with `value << 6`, and
`TGXpLspDsp_process` approaches it every two samples using `rate / 65536`.
Its native replacement only consumes slots 48-63, where its fixed rate
`0x0300` agrees with the JV's group-3 register.  The JV itself uses targets in
other groups, proving that the silicon facility is not restricted to those 16
slots.  Mapping SCCore's Q15 ramp to the host's Q22 IRAM word gives
`target << 13`; target `0x1ff` therefore approaches `0x003fe000`, just below
unity `0x00400000`.

The emulator treats direct IRAM3 writes as current-state seeds and target-bank
writes as ramp triggers.  Host trigger reads of IRAM3 return the evolving
32-bit current through `0x3912:0x3910`, except slot 3 while its stored word is
exactly `1`, which enables the hardware random source.

The strongest current PRAM field split is:

| Bits | Working name | Evidence |
|---|---|---|
| 31:21 | parallel memory/control channel | factory execution image exposes selector codes and a three-slot source-to-destination pipeline; exact subfields remain open |
| 22:16 | ERAM control/address fragment | directly rewritten by the JV ERAM-offset helper; overlaps the parallel channel by operation type |
| 15:12 | ALU opcode | exact nibble boundary shared with CSP; only 0, 5, 7, B, D, and F occur in active JV RFX slots |
| 11:9 | store mode | CSP-family layout: none, synth bus, special A, special A saturated, IRAM A/B, IRAM A/B saturated |
| 8 | memory selector bit 8 | combines with bits 7:0 |
| 7:0 | memory selector bits 7:0 | gives a 9-bit IRAM/special-register selector |

The low-half layout is a strong family-level identification, not yet proof that
every XP special address and ALU mnemonic is identical to CSP.  In particular,
blindly applying CSP's `VMUL/VMAC` coefficient-selector rules does not work:
XP opcode-F slots routinely carry full fixed-point values such as `0x5000`.
The analyzer therefore prints CSP names as analogies only.

The factory execution image proves that the upper half is active even when the
low store field is zero.  Selector codes 8, 9, 10, and 11 occur at slots 1,
11, 21, and 31; codes 4, 5, 6, and 7 follow exactly three slots later.  Those
are the four seeded inputs and four result destinations, matching the
three-cycle accumulator-store delay in CSP and ESP.  The decoder now prints
`bits 27:21` and `bits 20:16` separately as `p-mem/p-ctl`; assigning read,
write, bank, and saturation bits remains open.

Across all 40 images the opcode population is restricted to six nibbles:

| Opcode | Slots | CSP analogue, not yet XP name |
|---:|---:|---|
| 0 | 1517 | MAC.A |
| 5 | 166 | CMP.A |
| 7 | 1052 | NEGMAC.A |
| B | 79 | DMAC.B |
| D | 152 | VMUL.B |
| F | 539 | VMAC.B |

This restricted set and the repeated three-instruction coefficient groups are
consistent with a dual-accumulator, pipelined MAC DSP.  The exact accumulator,
clear/accumulate, multiplier-source, saturation, and write-delay truth table is
still an execution hypothesis.

## CRAM number format

SCCore retains the conversion used by its native replacement for the XP system
effects.  A CRAM word has a signed 14-bit mantissa and a two-bit exponent:

```text
mantissa = sign_extend(raw[13:0], 14)
shift    = [0, 1, 2, 4][raw[15:14]]
value    = (mantissa << shift) / 8192
```

Useful anchors are `0x0000 = 0`, `0x1000 = +0.5`, `0x1fff` just below +1,
`0x2000 = -1`, and `0x5000 = +1`.  Dynamic EQ, shelf, damping, feedback,
balance, and output-level changes all produce numerically coherent values with
this decoder.

## ERAM address encoding

`xp_dsp_write_eram_offset` at `0x0a00225c` proves that one 16-bit ERAM address
is split across two adjacent PRAM words:

```text
P[n]   = (P[n]   & 0x0f80ffff) | ((address & 0xfe00) << 7)
P[n+1] = (P[n+1] & 0x0e00ffff) | (address << 16)

address[15:9] -> P[n][22:16]
address[8:0]  -> P[n+1][24:16]
```

The masks preserve transaction/control bits in both words.  The operation type,
circular-buffer base, commit latency, and variable-offset interpretation have
not yet been assigned.

Stereo Delay provides a direct dynamic check.  Its 500 ms factory taps are
encoded at pairs 7/8 and 34/35.  Decrementing the displayed delay changes
`00cf7480,00800021` to `00ce7480,00000021`, i.e. decoded address
`0x9e80 -> 0x9c00`, without touching the low instruction halfword.  The right
tap similarly changes `0xee80 -> 0xec00`.  Modulation Delay uses the same
address operation at pairs 7/8 and 18/19.  Triple/Quadruple Tap Delay and both
reverb algorithms expose further address pairs; changing Gate Reverb mode
rewrites a large table of ERAM taps rather than replacing its ALU skeleton.

## Host readback latch

DSP-memory reads are trigger reads, not ordinary memory reads.  The firmware:

1. reads a PRAM address and discards the returned CPU value;
2. reads the high half from XP `0x3912`;
3. reads the low half from XP `0x3910`;
4. modifies the field and writes the PRAM word back.

CRAM uses the low readback word.  This was exposed by the first parameter sweep:
the emulator did not populate the latch, so every delay edit combined its new
address with stale status/wave data and produced a characteristic bogus low
half `0x0022`.  The XP device now latches CRAM as 16 bits and IRAM/PRAM as 32
bits, presenting the low half at `0x3910` and high half at `0x3912`.  A repeated
Stereo Delay sweep preserves `0x7480/0x0021` exactly, validating the fix.

This also explains why `0x3912` cannot be modeled as a single passive status
register: it is multiplexed with DSP readback.  A read of IRAM3 slot 3 at XP
`0x320c` triggers the hardware random generator and places its 16-bit result in
the high word.  A direct read after a voice-start command instead exposes the
busy nibble.  The emulator implements both paths using the exact two-seed
generator recovered from SCCore.

## Manufacturing EFX test program

The factory `EFX Test OK` result is useful, but it is not proof that the DSP
executes yet.  The complete traced sequence is:

1. walking-pattern tests over all 288 CRAM words and 288 PRAM words;
2. equivalent tests over the 64 words in each of IRAM1, IRAM2, and IRAM3;
3. upload a dedicated 256-slot program from ROM `0x0a03efdc`, with CRAM at
   `0x0a03f3dc`;
4. seed IRAM3 slots 0-3 with `000000`, `555555`, `aaaaaa`, and `ffffff`;
5. write `4` to DSP access control `0x3916`, wait `0x208` firmware ticks,
   stop it, and trigger-read IRAM3 slots 4-7;
6. expect low result bytes `00`, `55`, `aa`, and `ff`.

The current emulator genuinely passes the five memory/readback tests.  It does
not execute PRAM, so all four result slots remain zero.  The ROM comparison has
an explicit `if (result != 0)` guard before checking either nibble; consequently
all-zero results produce status zero and the factory UI reports OK.  This is a
firmware-test loophole, not a reason to add a test-specific emulator shortcut.
This trace also corrected the slot-3 random source: it is active only when the
stored IRAM3 word equals `1`; other values participate in normal RAM readback.

The uploaded image is nevertheless an unusually good execution oracle.  It
contains four repeated 41-slot regions, uses only opcodes 0, 7, and B,
repeatedly combines the known `0x5000` unity coefficient with tiny
coefficients, and maps the four known input words to four known outputs.
Decode it reproducibly with:

```sh
python3 jv1080/re/analyze_dsp_bytecode.py --factory-test
```

`JV1080_FACTORY_XP_TRACE=1` on the factory probe captures the complete bus
transaction sequence.  A future interpreter must make slots 4-7 match the
four expected bytes before `EFX Test OK` can be treated as an execution test.

## Simple algorithm landmarks

### Stereo EQ

Stereo EQ has no ERAM transactions.  It consists of mirrored left/right filter
graphs.  The firmware's parameter builder gives exact coefficient ownership:

| Function | Left CRAM | Right CRAM |
|---|---|---|
| low shelf triplet | 0C, 0D, 0E | 30, 31, 32 |
| high shelf triplet | 0F, 10, 11 | 33, 34, 35 |
| parametric band 1 state/coefs | 01, 12-19 | 25, 36-3D |
| parametric band 2 state/coefs | 05, 16-21 | 29, 1E, 3E-45 |

Frequency or gain edits rewrite three-coefficient groups symmetrically.  The
parametric frequency/Q edits rewrite five or more coefficients per channel,
with `0x5000` appearing as the unity numerator/state coefficient.  Combined
with the firmware helper tables and mirrored state graph, this is strong
evidence for cascaded second-order IIR/biquad sections.  Assigning each slot to
`b0/b1/b2/a1/a2` still requires executing the instruction pipeline.

### Stereo Chorus / Flanger

Stereo Chorus and Stereo Flanger share one 95-active-slot template but use
different parameter builders.  Static ERAM pairs at slots 17 and 35 decode to
`0x6000` and `0xb000`, a separation of `0x5000`.  The surrounding program has
two near-identical channel blocks, a rate/depth/phase section containing the
only opcode-B operations in the image, and mirrored output-filter triplets.

The retriggered Chorus sweep shows:

- pre-delay changes mirrored CRAM slots 2C and 3A;
- rate changes slot 25;
- depth/phase change the interpolation coefficient around slot 34;
- filter type/cutoff rewrite symmetric three-coefficient groups at 0D-0F and
  1F-21;
- low/high gain rewrite output biquad triplets at 48-4D and 56-5B.

This matches the expected structure: filtered input, ERAM write, two
phase-related variable reads with interpolation, feedback, then stereo output
filtering.  The exact LFO accumulator and ERAM-read opcodes remain to be named.

### Delay and reverb family

Stereo and Modulation Delay prove fixed-tap address pairs plus symmetric
feedback/damping/output filters.  The CRAM sweep isolates phase inversion as
`0x5000 -> 0x2000` (+1 to -1), HF damping as complementary pairs, and the low/
high output filters as mirrored triplets.  Triple and Quadruple Tap Delay write
additional address pairs, while their per-tap levels are near-unity CRAM gains.

Reverb changes pre-delay through the ERAM pair beginning at slot 10 and decay
through mirrored coefficient pairs.  Gate Reverb mode is especially useful:
one mode step rewrites more than twenty ERAM addresses while leaving each
word's low instruction half intact.  That table is a diffusion/tap topology,
not executable code replacement.

## All-algorithm dynamic coverage

The following is the union of slots changed by the corrected 480-edit sweep.
Numbers are hexadecimal slot indices.  `P` is PRAM and `C` is CRAM.  A dash in
`P` is meaningful: the effect is parameterized entirely through coefficients
and shared state for the tested one-step edits.  Some short algorithms reach a
common EFX output-routing page before the third probed page, so system-region
slots such as `DE` or `F6` are routing observations rather than RFX bytecode.

| # | Algorithm | P slots changed | C slots changed |
|---:|---|---|---|
| 00 | Stereo EQ | - | 01, 0C-12, 14, 17, 19, 1F, 25, 30-36, 38, 3B, 3D, 43 |
| 01 | Overdrive | F6, F8-F9, FB | 36-47, 53, 59-5B, 5F, DE, F6, F9 |
| 02 | Distortion | - | 36-47, 52-53, 59-5B |
| 03 | Phaser | - | 07, 0E, 61, 63-64 |
| 04 | Spectrum | - | 09, 0C, 12, 15, 18, 1E, 23, 25-26, 2A-2B, 2D-2E, 32, 3B, 3D-3E, 42-43, 45-46, 4A-4B, 4D-4E, 52, 5B, 5D-5E, 62 |
| 05 | Enhancer | - | 28-2D, 45-4A |
| 06 | Auto Wah | - | 0F, 29, 2C, 4C-4D, 4F-51, 58 |
| 07 | Rotary | - | 13, 1E, 45-46, 4C-4D |
| 08 | Compressor | - | 0D, 1D, 41, 45-4A, 4C |
| 09 | Limiter | - | 0D, 28-29, 2E-2F, 34-35, 41, 45-4A, 4C |
| 10 | Hexa Chorus | - | 0B, 10, 18, 1B, 1D, 25, 28, 2A, 32, 35, 37, 3F, 42, 44, 4C, 4F, 51, 59, 5C-5D, 5F-62 |
| 11 | Tremolo Chorus | - | 0C, 11, 1D, 44, 47-49, 4C-4D |
| 12 | Space-D | - | 15, 1A, 22, 26, 35-3A, 44-49 |
| 13 | Stereo Chorus | - | 0D-0F, 1F-21, 25, 2C, 34, 3A, 48-4D, 56-5B |
| 14 | Stereo Flanger | - | 0D-0F, 1F-21, 25, 2C, 34, 3A, 48-4D, 56-5B |
| 15 | Step Flanger | - | 17, 19, 30, 38, 41, 4D-52, 59-5E |
| 16 | Stereo Delay | 07-08, 22-23 | 08-09, 0B, 19-1E, 23-24, 26, 34-39 |
| 17 | Modulation Delay | 07-08, 12-13 | 08-09, 13-14, 1E, 21, 2C, 3B-40, 49-4E |
| 18 | Triple Tap Delay | 0A, 0D-0E | 0E-0F, 17, 1B, 20-25, 2F-34 |
| 19 | Quadruple Tap Delay | 09-0C, 0F-10 | 0A-0B, 15-16, 1B |
| 20 | Time Control Delay | - | 0A-0B, 24, 27-2C, 31, 34-39 |
| 21 | 2-Voice Pitch Shifter | - | 08, 0C, 0E, 1A, 1C, 2E, 32, 34, 40, 42, 58 |
| 22 | Feedback Pitch Shifter | - | 0F, 13, 15, 21, 23, 40-45, 4C-51 |
| 23 | Reverb | 0B | 09-0A, 1F-20, 30-31, 51-56, 5B-60 |
| 24 | Gate Reverb | 0A, 22, 24, 26, 28, 2A-2C, 2E, 30-32, 34-36, 3A, 3C, 3E, 40-42, 44, 46, 48-4A, 4C-4E | 21, 23, 25, 27, 29, 2B, 2D, 2F, 31, 33, 35, 3A-3B, 3D, 3F, 41, 43, 45, 47, 49, 4B, 4D, 52-57, 5C-61 |
| 25 | Overdrive -> Chorus | - | 18, 3A, 3F, 41, 4D, 4F |
| 26 | Overdrive -> Flanger | - | 18, 31, 3A, 41, 4F |
| 27 | Overdrive -> Delay | 31 | 18, 31-32, 34 |
| 28 | Distortion -> Chorus | - | 18, 3A, 3F, 41, 4D, 4F |
| 29 | Distortion -> Flanger | - | 18, 31, 3A, 41, 4F |
| 30 | Distortion -> Delay | 31 | 18, 31-32, 34 |
| 31 | Enhancer -> Chorus | - | 1F, 32, 3E, 43, 45, 51, 53 |
| 32 | Enhancer -> Flanger | - | 1F, 32, 36, 3E, 45, 53 |
| 33 | Enhancer -> Delay | 40 | 25, 39-41, 43 |
| 34 | Chorus -> Delay | 39 | 0F, 16, 24, 39-3A, 3C |
| 35 | Flanger -> Delay | 39 | 07, 0F, 16, 24, 39-3A |
| 36 | Chorus -> Flanger | - | 0C, 11, 1D, 2F, 36, 3D, 4B |
| 37 | Chorus / Delay | 30 | 0B, 12, 20, 30-31, 33 |
| 38 | Flanger / Delay | 30 | 02, 0B, 12, 20, 30-31 |
| 39 | Chorus / Flanger | - | 09, 0E, 1A, 2B, 31, 38, 46 |

## CSP and ESP comparison

| Property | XP | CSP | ESP |
|---|---|---|---|
| PRAM slots | 288 | 1024 (firmware may run 768) | 768 per core |
| instruction/coefficient | 32 + separate 16 bits | 24 + separate 16 bits | one 32-bit word with embedded signed 8-bit coefficient |
| internal memory | 3 x 64 host-visible words | 512-word circular IRAM | 256-word IRAM per core plus shared GRAM |
| low instruction layout | opcode/store/9-bit memory, CSP-shaped | opcode/store/9-bit memory | coefficient/shift/8-bit memory/7-bit opcode |
| ERAM address stream | two adjacent PRAM words, 7+9 address bits | start plus five 4-bit chunks | start plus four/five 5-bit chunks in bits 27:23 |
| observed commit pipeline | unknown | 10 instruction cycles | 10 instruction cycles |
| accumulator store delay | unknown, patterns suggest pipelining | 3 instructions | pipelined accumulator objects |

XP is therefore much closer to CSP at the ALU/store layer, but its 32-bit ERAM
encoding is its own design.  ESP is valuable for understanding Roland's later
transaction scheduling, coefficient scalers, interpolation operations, and
special registers, but its instruction bit layout is not a direct XP decoder.

There is also a byte-exact cross-product kernel.  JV normal-system slots
117-121 (and again at 128) equal SCCore `scgsMaster` slots 109-113/120-124 in
both the low instruction halfword and CRAM:

```text
0000000f/1000  0000b020/0000  00000030/1001
00007030/0231  00000030/0325
```

The same sequence occurs inside the JV chorus/modulated-delay interpolation
blocks.  Opcode B is consequently a strong double-precision/interpolation MAC
candidate, consistent with both CSP's DMAC operation and ESP's explicit DMAC
special case.  This is still a semantic constraint, not a final mnemonic.

## Reproducible evidence

- `analyze_dsp_bytecode.py` decodes all 40 templates, CRAM values, opcode/store
  populations, duplicate images, candidate ERAM pairs, and the standalone
  factory execution-test program.
- `lua/rfx_parameter_sweep.lua` edits every front-panel field and releases/
  retriggers a Preview note after every edit.
- `analyze_rfx_sweep.py` converts that CSV into per-field PRAM/ERAM/CRAM deltas
  with normalized coefficient values.
- `extract_dsp_templates.py` inventories the selector, updater, and duplicate
  program tables.

The full corrected sweep contains 480 edits across all 40 algorithms, and all
480 rows record `fresh-note=yes`.

## Remaining proof work

1. Build an XP interpreter with unknown opcode behavior table-driven, then use
   the EQ impulse response to solve accumulator clear/add, store destination,
   saturation, scale, and pipeline timing.
2. Use the delay images to solve ERAM start/read/write/variable modes and commit
   latency; reject any candidate that does not reproduce the observed taps.
3. Map special memory selectors by correlating fixed system-program slots with
   known input/output buses and the three host-visible IRAM banks.
4. Execute every RFX template and compare impulse/frequency responses against
   the expected EQ, chorus, delay, compressor, pitch-shift, and reverb topology.
5. Determine the physical event sources for XP IRQ7 reasons 4, 7, and 8.
   Reason 5, `0x391a` acknowledgement, voice-command busy, and `0x320c` random
   readback are implemented; reason 7 still requires a real DSP commit point.
