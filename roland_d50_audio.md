# Roland D-50 audio and effect hardware

These notes describe the host interfaces between the uPD78312, LA32
(MB87136/LA32), reverb IC (MB87126-006), chorus IC (MB87137-001), and IC28
gate array.  They combine the D-50 2.20 firmware, service schematics, live MAME
traces, factory PN-D50-00 data, and the independent high-level implementation
in `hack_d50/d50lib`.

The evidence labels used below are:

- **confirmed**: directly established by firmware or schematic
- **strong**: repeatable differential trace plus matching firmware code
- **provisional**: a useful implementation hypothesis that still needs an
  audio capture or logic trace

## Physical signal path

The service schematic establishes this digital chain:

```text
LA32 (IC31) -> MB87126-006 REVERB (IC9) -> MB87137-001 CHORUS (IC8)
             -> 8-bit DC0..DC7 bus       -> DAC and stereo sample/hold
```

IC9 has six uPD41416C-12 16K x 4 DRAMs wired as a 16K x 24-bit delay
memory (48 KiB total).  The service schematic labels the complete word bus
`DR0-DR23`, the multiplexed row/column address bus `DA0-DA7`, and the usual
`RAS`, `CAS`, and `WE` controls.  This independently confirms a 24-bit sample
datapath and a fourteen-bit D-50 delay address.
The schematic shows IC9 driving an eight-line `DC0-DC7` sample bus into IC8;
there is no matching eight-bit return bus.  IC8 has one HM6264 8K x 8 SRAM used
by the chorus/output stage, then drives its own `DC0-DC7` bus into the PCM54
DAC and generates the upper/lower sample-and-hold timing.  Consequently a
direct LA32-to-speaker route is a dry diagnostic bypass, not the D-50 audio
path.

IC28 is not only a panel gate array.  Its SCK, serial, ERCL, BUSY, LOAD, and
reset lines connect to the effect section.  Firmware uses IC28 as a checked,
two-lane parameter transfer engine in addition to its panel duties.

## Editable parameter layout

The live edit buffers in work RAM are:

| Address | Block |
|---|---|
| `C400-C43F` | Upper partial 1 |
| `C440-C47F` | Upper partial 2 |
| `C480-C4BF` | Upper common |
| `C4C0-C4FF` | Lower partial 1 |
| `C500-C53F` | Lower partial 2 |
| `C540-C57F` | Lower common |
| `C580-...` | Patch |

The relevant bytes agree with the MIDI implementation and factory SysEx:

| Live address | Packed patch offset | Meaning |
|---|---:|---|
| `C4A5-C4A9` / `C565-C569` | `0A5-0A9` / `165-169` | LF frequency/gain, HF frequency/Q/gain |
| `C4AA-C4AD` / `C56A-C56D` | `0AA-0AD` / `16A-16D` | Chorus type, rate, depth, balance |
| `C4AE-C4AF` / `C56E-C56F` | `0AE-0AF` / `16E-16F` | Partial-enable mask and partial balance |
| `C592` | `192` | Key mode |
| `C59D` | `19D` | Output mode |
| `C59E` | `19E` | Reverb type |
| `C59F` | `19F` | Reverb balance |
| `C5A0` | `1A0` | Total volume |
| `C5A1` | `1A1` | Tone balance |

Firmware routine `6655` compares these bytes with shadows in `CD74-CD9C` and
calls narrowly scoped compilers only when a parameter group changes.  This is
why changing one UI value is a useful register-mapping experiment.

## Tone NVRAM and bulk SysEx

The PN-D50-00 bulk file is 136 valid Roland DT1 frames containing one
contiguous `0x8780`-byte logical image beginning at MIDI address `02 00 00`
(`0x8000`):

- `0x0000-0x6fff`: 64 patches of `0x1c0` bytes
- `0x7000-0x877f`: 16 reverb programs of `0x178` nibble bytes

Firmware mapper `57FA` converts that logical image into the 32 KiB HM62256
tone RAM as follows:

| Logical bulk offset | Physical tone-RAM offset | Contents |
|---|---|---|
| `0000-3EFF` | `0040-3F3F` | first 36 patch records |
| `3F00-6FFF` | `4000-70FF` | remaining 28 patch records |
| `7000-877F` | `7100-7CBF` | reverb data, two low-nibble-first MIDI bytes packed into each byte |

Offsets `0000-003F`, `3F40-3FFF`, and `7CC0-7FFF` are outside the bulk patch
payload.  Directly copying the `.syx` bytes into NVRAM is therefore incorrect:
the split at logical `3F00` and the 2:1 reverb packing are both required.

The firmware's 22-byte system block is at physical tone-RAM offset `7F00`.
PN-D50-00 does not contain it.  An all-zero block passes the ROM's range
validation but clears byte 9 bit 0, which disables both panel and MIDI patch
selection.  ROM 2.22's defaults are:

```text
3A 00 00 01 01 01 01 01 01 01 01 00 01 00 00 00
00 0C 00 00 00 00
```

MAME now repairs only an all-zero block and uses erased (`FF`) SRAM for a new
NVRAM image, allowing the firmware to install these defaults itself.

## LA32 host interface (`E000-E5FF`)

The D-50 maps six LA32 files into six 0x100-byte CPU pages.  Only the low 0x40
bytes of a page select the 32 16-bit slots; aliases in the page repeat the same
file.  An even write latches the low byte globally and the following odd write
commits the word.

| CPU page | LA32 file | Confirmed use in the current core |
|---|---:|---|
| `E000` | 0 | TVA ramp destination/rate |
| `E100` | 1 | Wave/PCM base and waveform parameters |
| `E200` | 2 | TVF ramp destination/rate |
| `E300` | 3 | Pitch increment |
| `E400` | 4 | Wave/mode/ring/output/pan control |
| `E500` | 5 | Internal feedback and pipeline state |

File 4 currently decodes as:

| Bits | Meaning |
|---|---|
| 7 | Waveform family |
| 6 | Synthesis mode |
| 5 | Ring modulation |
| 4:3 | Output group 0-3 |
| 2:0 | Pan |

LA32 exposes eight streams.  The silicon model names groups 0-3 as its first
pan side and 4-7 as the second, but they are not final D-50 speaker channels.
On factory I-11, the four partial paths appear independently on buses 0, 2, 4,
and 6; summing 0-3 as left and 4-7 as right therefore turns partial/tone
balance into a false 20+ dB stereo imbalance.  All eight streams feed the
IC8/IC9 mixer.  Until that matrix is fully decoded, the provisional effects
device combines all eight feeds before forming stereo chorus/reverb.

The D-50 board fixes LA32 global control to `1C1=60`.  Bit 6 selects its packed
14-bit PCM ROM format; leaving it clear interprets most D-50 PCM words as a
constant exponent and makes PCM partials effectively silent.  Bits 5:4 select
inactive-state grouping mode 2: slots 8-15 inherit key-on/inactive state from
0-7, while slots 24-31 inherit it from 16-23.  This is the D-50's two
partial-pair layout (0/8 and 16/24 for the first allocated note).

Using only `40` left secondary oscillators free-running while idle.  During
I-76 setup, one-shot wave 43 was first written with file-4 control `18D0`; its
stale phase was already beyond the 4096-sample boundary, so it terminated
before the firmware's immediate `98D0` acknowledgement.  The surviving
Spectrum wave 71 consequently sounded much too prominent.  The same missing
phase reset made the Spectrum pairs in I-16 (wave 69) and I-88 (waves 67/68)
start at unrelated ROM positions.  With `60`, paired phases reset together,
I-76 wave 43 runs for its intended attack, and measured phase increments for
all affected waves agree with d50lib to the LA32's 1/256-sample quantization.

The PCM ROM also has address line A18 inverted relative to the LA32 address.
This is visible without an audio heuristic: factory I-11 requests PCM waves
12 and 68, which the firmware compiles to file-1 bases `54` and `B1`.  The
independently recovered D-50 PCM table places those waves at byte offsets
`014000` and `071000`; the uncorrected core reads `054000` and mirrored
`031000`.  XORing LA32 ROM addresses with `040000` resolves both mappings.
The correction is a D-50 device configuration so MT-32-family users retain
their direct ROM addressing.

Ramp completion and PCM-boundary events produce the LA32 interrupt and status
byte read by the MCU.  Routing only groups 2/3 and 6/7 directly to speakers
loses valid voices and bypasses output mode, EQ, chorus, and reverb.

### PCM sample directory (`BF00-BFFF`)

The last 256 bytes of every checked v2.x program ROM are two parallel
128-entry PCM tables.  The MCU sees them at `BF00-BFFF` after selecting the
last program-ROM bank:

| CPU address | ROM-file offset | Meaning |
|---|---:|---|
| `BF00-BF7F` | `FF00-FF7F` | size exponent and loop flag |
| `BF80-BFFF` | `FF80-FFFF` | LA32 ROM start-page code |

The PCM waveform number is partial byte 7.  Internal-ROM compiler `045E`
indexes both tables.  It writes the size entry into LA32 file 4 bits 15:12
(and forces file 4 bit 11), duplicates the pointer entry into both bytes of
file 1, and then the normal note compiler adds mode/output/pan bits.

For a size byte `s`:

```text
n              = s & 7
loop           = (s & 8) != 0
pages          = 1 << n
samples        = 0x800 << n       (2048 * pages)
encoded bytes  = 0x1000 << n      (4096 * pages)
```

Each ROM sample occupies two packed-log bytes.  Size codes `00-05` are
non-looping lengths of 1, 2, 4, 8, 16, or 32 pages.  Codes `08-0D` are the
same lengths with wrapping enabled.  User waveforms 0-99 use only these
values.

The pointer byte is not a byte offset by itself.  The LA32 aligns it to the
power-of-two size and inserts the low page bits from the phase counter.  With
`p = BF80[wave]`, sample index `k`, and `n` as above:

```text
page_mask   = (0x7f << n) & 0x7f
page        = (p & page_mask) | ((k >> 11) & (~page_mask & 0x7f))
la_address  = ((p & 0x80) << 12) | (page << 12) | ((k & 0x7ff) << 1)
d50_address = (la_address ^ 0x40000) & 0x7ffff
```

`p.7` is LA32 A19.  The D-50 has a 512 KiB ROM, so A19 is physically absent
and aliases after the final mask.  A18 is inverted on the board.  For a
non-looping sample, LA32 raises its boundary event when the phase leaves the
`2^n`-page window.  For a looping sample, the boundary event is suppressed
and the inserted low page bits naturally wrap within that window.

A non-looping boundary is also a terminal oscillator state, not merely an
interrupt condition recomputed from the current phase.  The firmware sets the
control loop bit while acknowledging some completed one-shots; that suppresses
repeated boundary notifications but must not restart the wrapped ROM data.
MAME therefore latches each of the two PCM reads silent at its boundary until
the partial becomes inactive and is retriggered.  Missing this latch made the
one-shot attacks in factory I-72 Syn Marimba repeat indefinitely underneath a
slow TVA segment.

In LA32 dual-wave mode the two reads have independent size/loop nibbles in
file 4 (`15:12` and `11:8`).  The second read must therefore test its own bit
11 loop flag; inheriting bit 15 from the first descriptor can terminate a
looping second wave or repeat a second-wave one-shot.  Interpolation mode uses
one descriptor for both adjacent reads and is unaffected.

This decoding was checked against d50lib's independently recovered
`m_pcmInfo`: start address, byte length, and loop flag match exactly for all
100 user-selectable D-50 PCM waveforms.  Examples:

| Wave | `BF00` | `BF80` | Result |
|---:|---:|---:|---|
| 12 | `01` | `54` | `0x14000`, 4096 samples, non-looping |
| 68 | `08` | `B1` | `0x71000`, 2048 samples, looping |
| 75 | `0B` | `B8` | `0x78000`, 16384 samples, looping |
| 94 | `0C` | `42` | aligned to `0x00000`, 32768 samples, looping |

Wave 94 demonstrates why the size mask is essential: pointer `42` is not
used literally; a 16-page (`n=4`) sample clears its low four page bits before
the D-50 A18 transform.

Factory I-21 compiles PCM waves 57, 71, 67, and 94 into slots 0, 8, 16, and
24.  Wave 94 appears as file-4 `C8D0` with file-1 base `4242`; it reaches LA32
bus 6 and traverses the entire 32768-sample region.  Its programmed TVA has a
deep held-note dip: during a C4 test its peak relative to companion wave 67
fell to 0.14 in the second second and rose to 0.37 in the third.  The recovered
d50lib engine shows the same 0.15 dip and later rise, so this should not be
mistaken for a missing or prematurely terminated PCM partial.

## IC9 parallel interface (`F000-F007`)

IC9 has an eight-byte CPU window.  Routine `B033` gives the transaction exactly:

1. Poll `F007.7` until BUSY clears.
2. Write the register number to `F007`.
3. Poll BUSY again.
4. Write the 16-bit value high byte to `F000`.
5. Poll BUSY again.
6. Write the low byte to `F001`.

The firmware times out after 256 polls.  Returning open bus or leaving BUSY
set makes every write fail and was the source of the earlier register-write
errors.  The minimum useful emulation therefore needs an address latch, a
16-bit register file, and BUSY clear when idle.

Firmware keeps the pending values as 16-bit pairs in `DE00-DEAE`; bit 15 is a
CPU-side dirty/sent flag.  `AF43` uploads these sparse ranges:

| CPU staging offsets | IC9 registers | Count |
|---|---|---:|
| `DE00-DE7F` | `80-BF` | 64 |
| `DE80-DE9F` | `D0-DF` | 16 |
| `DEA0-DEA3` | `C0-C1` | 2 |
| `DEA4-DEA7` | `C4-C5` | 2 |

### Chorus-dependent IC9 register groups

The chorus compiler uses an address table at ROM `B6C9-B70C`.  Differential
boots and UI edits establish four 16-register lanes plus four mode registers:

| Tone | Stage/operator 0 | Stage/operator 1 | Mode/routing |
|---|---|---|---|
| Lower | `80-8F` | `A0-AF` | `D4`, `D8`, `DC`, `DE` |
| Upper | `90-9F` | `B0-BF` | `D6`, `DA`, `DD`, `DF` |

The operator layout and units are now recovered.  For an operator bank at
base `B`, the first eight registers are:

| Offset | Meaning | Hardware-to-DSP conversion |
|---:|---|---|
| `+0`, `+1` | left/right amplitude-LFO rates | triangle phase step = word × `0.001575` per sample |
| `+2`, `+3` | two delay-modulation LFO rates | same conversion |
| `+4`, `+5` | left/right amplitude modulation | word ÷ `2048` |
| `+6`, `+7` | two delay excursions | word × `0.17` samples |
| `+E`, `+F` | left/right base delay | `(0x1000 - word) mod 1411` samples |

The phase accumulator spans 512 units at 32 kHz, so one rate-register unit is
`0.0984375 Hz`.  Each operator uses four triangle LFOs and two independent
1411-sample linearly interpolated delays.  The two delay read offsets move in
opposite directions; the delayed outputs also receive independent amplitude
modulation.

The remaining registers are signed Q12 coefficients (12-bit two's complement,
divided by 2048):

| Function | Lower | Upper |
|---|---|---|
| Operator 0 low/high output matrix | `88`, `89`, `8B` | `9A`, `9B`, `99` |
| Operator 0 -> operator 1 links | `8C`, `8D` | `9C`, `9D` |
| Operator 1 low/high output matrix | `A8`, `A9`, `AB` | `BA`, `BB`, `B9` |
| Operator 1 -> feedback carry | `AC`, `AD` | `BC`, `BD` |
| Half input gain | `D4` | `D6` |
| Half feedback gain | `D8` | `DA` |
| Operator 0 drive | `DC` | `DD` |
| Direct pre-mix into operator 1 | `DE` | `DF` |

This explains values such as `0800`: it is `-1.0`, not an unsigned gain of
one.  The firmware's deliberately permuted upper addresses (`9A/9B/99` and
`BA/BB/B9`) come directly from its scatter table and must not be sorted into
numeric order.

For example, upper chorus balance 50 produces `99=0C03`, `9A=03FC`,
`B9=0B07`, `BA=04F8`.  Upper balance 100 produces `0AAF/0550` and
`095F/06A0`.  Lower uses the complementary orientation.

The type compiler (`B70D` upper, `B7F3` lower) selects one of eight 0x20-byte
hardware presets at ROM `A542`, applies rate, depth, balance and output-mode
transforms, then writes 34 sparse IC9 registers per tone.  d50lib's 0x38-byte
row is the exact word-expanded form of this preset: the first 24 source bytes
become the two 15-register operator banks and bytes 24-27 become the four
`D4-DF` half-level gains.

Tracing note assignments through all 64 factory patches also resolves the
LA32-to-IC8 split: lower-tone audio is normally on LA32 buses 2+4 and upper
tone audio on buses 0+6.  The rare reversed assignment is intentional firmware
routing for a voice structure, not stereo panning.  No factory patch used the
odd buses.

Per sample, each half therefore computes a feedback-fed operator 0, mixes its
two outputs into operator 1, updates one feedback carry from operator 1, and
emits low/high matrix outputs.  Stereo recombination is diagonal: lower-low +
upper-high feeds left, while lower-high + upper-low feeds right.  This matches
the independently recovered d50lib graph and the measured patch-1 channel
bias.

## IC28 two-lane transfer (`F800-F804`)

The firmware-facing packet format is confirmed by routine `AE7A`:

| Address | Packet field |
|---|---|
| `F800` | Selector 1-127 |
| `F801` | DC lane bits 7:0 |
| `F802` | DC lane bits 13:8 |
| `F803` | DD lane bits 7:0 |
| `F804.5:0` | DD lane bits 13:8 |
| `F804.6` | Transfer/framing marker, always set for a normal packet |
| `F804.7` | Packet parity computed by `AF2A` |

The two CPU shadows are `DC00-DCFF` and `DD00-DDFF`, each holding 128 16-bit
words.  Bit 15 is not transferred; zero means dirty and one means already sent.
Writing zero to `DC00` starts a scan.  The transfer routine skips selectors for
which both words are clean, sends a complete two-word packet when either is
dirty, then marks both clean.

The two lanes are the writable parameter half of a fixed-program DSP slot, not
two unrelated register files.  Evidence from the MB87126-family DEP-5, the
D-50 tables, and d50lib agrees on this format:

- the selector is a parameter-RAM/microcycle address;
- DC is the low 14 bits of a 16-bit delay-memory relative offset;
- DD bits 11:0 are a signed 12-bit MAC multiplier;
- DD bits 13:12 select MAC ranges `1/16`, `1`, `4`, or `16` (effective shifts
  `-4`, `0`, `+2`, `+4`).

The physical MB87126 parameter word is therefore 30 bits: 16 offset bits,
12 signed coefficient bits, and two shift bits.  The D-50 forces the upper two
offset bits to zero because its delay memory needs only 14 address bits.  A
physical 40-bit serial frame is 8 address bits + 30 parameter bits + two
framing/parity bits.  A direct decode of the real DEP-5 power-on capture shows
every active-low selector `00-9F` being written.  This proves a 160-location
used window, not a physically 160-word array: the eight-bit selector still
allows a 192- or 256-word implementation with unused upper locations.  Die-row
counting or a hardware alias test is required to settle physical depth.

Thus `DD = mantissa | range << 12` evaluates as
`sign_extend_12(mantissa) / 2048 * {1/16,1,4,16}[range]`.  The chip's fixed
internal ROM supplies the operation, accumulator routing, and stores.  This
explains why one selector can receive both a DC address and a DD multiplier,
and why EQ, mixing, and reverb occupy disjoint sparse slots in the same space.

That skip rule is significant when installing a new effect image.  Selector 1
is also a normal output-gain DD slot, so a packet at selector 1 alone is *not* a
program reset.  A changed selector-1 DC operand identifies a new reverb delay
topology.  MAME then clears only the 55 reverb-owned DD slots (not EQ or mixer
slots), clears the old delay/state, and mutes the network until all 39 DC
offsets have arrived.  The old implementation cleared all 128 DD slots on
every selector-1 write, erasing EQ and routing during ordinary output updates.

`P0.3` is the reverb-data latch/control output.  `P2.2` is IC28 BUSY.  Status at
`F9BF` supplies completion bits 0 and 1 and clears the gate-array interrupt.
The current MAME implementation still returns an immediately-complete status;
it retains the decoded 14-bit words for tracing and feeds the preliminary
effects device.

The schematic proves that IC28's effect-control pins connect into the IC9/IC8
section.  Which physical serial pin consumes DC versus DD is still provisional;
the firmware-visible packet format does not depend on that naming.

The related DEP-5 connects its MCU directly to an MB87126-002 and supplies the
missing physical framing evidence.  Its receiver captures SXD on rising SCK,
least-significant bit first, in five consecutive bytes.  The decoded 40-bit
frame is:

| Wire byte | Meaning |
|---:|---|
| 0 | active-low parameter slot (`slot = ~byte0`) |
| 1-2 | little-endian delay-memory address/offset |
| 3 | signed multiplier bits 7:0 |
| 4.3:0 | signed multiplier bits 11:8 |
| 4.5:4 | MAC shift/range |
| 4.6 | load/framing marker |
| 4.7 | parity |

BUSY pulses after the 40th bit.  ERCL clears/resets the interface and INCK/LOAD
latches initialization; the DEP-5 initialization sends three zero frames,
then `FF F9 00 00 00`, pulses INCK, and changes the chip sync clock from about
2.5 MHz to the 32 kHz audio clock.  The D-50's `F800-F804` image is the same
five-byte field layout, but IC28 sits between the CPU and IC9.  A physical
D-50 SCK/SXD capture is still needed to prove whether IC28 performs the same
byte-0 inversion and exactly which clock edge reaches IC9.

## Reverb program format

This is the clearest fully recovered part of the effect interface.  The full
slot ownership and reverb graph mapping is in
[`roland_d50_mb87126.md`](roland_d50_mb87126.md).
The reconstructed integer datapath, arithmetic assumptions, and the
legacy/fixed-point comparison switch are documented in
[`roland_d50_mb87126_execution.md`](roland_d50_mb87126_execution.md).

Routine `64B4` chooses the program and `64C5` expands it into the IC28 shadows.
Each hardware program is exactly `0xBC` (188) bytes of slot parameters:

- 39 16-bit delay/tap address operands -> selected DC words
- 55 packed shift/multiplier operands -> selected DD words

The 39 DC selectors, derived from the ROM scatter table at `6456`, are:

```text
01 02 03 04 05 06 07 09 0A 0B 0C 0E 10 15 17 1B
26 28 36 37 38 39 3A 3B 3D 41 42 44 48 4A 4B 4E
51 52 54 59 60 77 78
```

The 55 DD selectors, from the table at `647D`, are:

```text
02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 12 13
16 19 1D 2D 2F 38 3A 3B 3C 3D 3E 3F 40 42 43 44
45 46 47 48 49 4A 4B 4C 4D 4E 4F 52 53 54 55 56
57 58 5A 5B 5E 7A 7D
```

Reverb types 0-15 use factory programs beginning at EPROM `9668`, with a
stride of `0xBC`.  Types 16-31 select the editable/card reverb bank after the
firmware changes the external-memory bank.

The MIDI implementation exposes each program as 376 bytes with values limited
to `0-F`.  Those are nibbles: pairs combine into the 188 binary program bytes.
This explains the manual's warning that the 376 bytes are mutually related.

This format is independently validated by d50lib: its reverb decoder produces
exactly 39 ring-buffer tap offsets and 55 coefficients.  d50lib feeds these
through a 32768-entry family-wide software array; the D-50 hardware itself
wraps the reconstructed graph through its 16384-word external delay memory.
delay network with diffusion, damped feedback, and a stereo tap matrix.

Reverb type changes are asynchronous.  State machine `650F` first fades/reroutes
the output, loads the new 188-byte program, and then restores the requested
balance.  A UI change from reverb 2 to 3 therefore produces hundreds of IC28
packets over several update passes but no immediate `F000-F007` transaction.
This behavior was previously mistaken for a missing peripheral transfer.

## EQ, output mode, and mixing

These controls are compiled mainly into IC28 DD words rather than the four
static IC8 host latches:

| Compiler | Input |
|---|---|
| `B0F9`, `B259` | Upper EQ sections |
| `B1AB`, `B2F8` | Lower EQ sections |
| `B397` | Tone/output balance matrix |
| `B4DD`, `B4EF` | Output/key mode routing |
| `B64C` | Reverb balance and mode-dependent return level |
| `65E3` | Total volume / output gain |

Booting isolated parameter variants and comparing the retained IC28 state gives
these DD selector groups:

| Parameter | DD selectors changed |
|---|---|
| Upper LF frequency | `14 15 17 18 1A 1B 1C 1E` |
| Upper LF gain | `22 23` |
| Upper HF frequency or Q | `24 25 26 27 28 29 2A 2B 2C 2E` |
| Upper HF gain | `32 33` |
| Lower LF frequency | `59 5C 5D 62 63 64 65 66` |
| Lower LF gain | `68 69` |
| Lower HF frequency or Q | `67 6A 6B 6C 6D 6E 6F 72 73 78` |
| Lower HF gain | `7B 7C` |
| Reverb balance | return level `34`, mirrored by `75`; routing companions `36 76` |
| Total volume | `01 39 41 79` |
| Tone balance | `10 30 50 70` |
| Chorus balance | upper `1F 5F`, lower `20 60`, plus IC9 chorus coefficients |
| Output mode | core matrix `1F 20 21 5F 60 61`, plus mode-dependent `34 35 74 75 7E 7F` |

An exhaustive temporary-patch sweep establishes that selector `34` is a
monotonic but nonlinear encoding of reverb balance 0-100.  It is zero for
balances 0-2, crosses the range tag at balance 40 (`2204`), is `2270` at Metal
Harp's balance 48, and reaches `2532` at 100.  Selector `36` carries the
mode-dependent direct/decay companion: it remains `254D` through balance 50,
then falls to zero at 100.  Treating `34` as a generic signed coefficient and
clamping it at unity consequently made every balance from about 40 upward
nearly identical.

Sweeping key modes 0-4 against output modes 0-3 also reproduces d50lib's mixer
branches.  In the commonly used dual/split output mode 0, the recovered direct
gain is `0.4` through balance 50 and then falls linearly to zero; the previous
provisional `0.8` direct gain was responsible for Griitttar overdrive and
masked Metal Harp's return.  Until the complete IC28 arithmetic that generates
this matrix is decoded, MAME supplies the active bytes at `C592/C59D-C59F` to
the effects device alongside the authoritative IC28 stream.

Frequency and Q intentionally share a coefficient group: either control causes
the complete high section to be regenerated.  Comparing all 16 low-frequency
and all 22 x 12 high-frequency/Q firmware rows against d50lib recovers the
exact fixed-ROM recombination.  In firmware table order `w[]`, after decoding
each packed DD multiplier:

```text
low: c0=w3+w2/64, c1=w1+w0/64, c2=w4+w5/64, c3=w6+w7/64

high companion f(w) = w/64 when the packed word has range 0, otherwise w
high: c0=w4+f(w3), c1=w2+f(w1), c2=w0+f(w5),
      c3=w6+f(w7), c4=w8+f(w9)
```

The gain pairs are the wet/filter and dry coefficients.  Both pairs smooth
toward new targets by `1/80` per sample.  MAME now runs d50lib's recovered
four-coefficient low recurrence followed by its five-coefficient high
recurrence separately for the lower and upper tones.  It also runs the fixed
post-effects stereo state-variable output EQ selected by d50lib's default
indices `{4,0,45,65}`.

EQ is therefore not absent merely because `E700-E707` and `F000-F007` do not
change.  Its coefficients travel through the IC28 packet engine.  The compiler
uses lookup tables and fixed-point conversions, so a behavioral emulation
should either reproduce these generated words or take the already decoded
high-level filter parameters; treating EQ as five direct chip registers would
be incorrect.

The complete key/output matrix refines this further.  For key modes 1-3,
output mode 0 is the retained baseline; mode 1 changes
`1F 20 21 5F 60 61`, mode 2 additionally changes
`34 35 75 7E 7F`, and mode 3 instead changes
`34 74 75 7E 7F`.  Modes 1-3 use the IC9 chorus matrix in a different routing
orientation from mode 0.  Key modes 0 and 4 alias/recompile a complete tone
and EQ side, so their large traces are not pure output-mode deltas.

## IC8 host interface (`E700-E707`)

The MCU decodes eight byte registers, but current firmware observations only
show writes to 0-3.  Boot initializes:

```text
E702=00  E701=E1  E700=55  E703=60
```

Normal chorus, EQ, reverb, and output-mode edits did not directly alter these
four bytes.  They are therefore global mode/timing/routing latches, not a
complete user-facing chorus register set.  Dynamic programs reach the effect
chain through IC9 and IC28.  Type/rate/depth edits change only IC9's indirect
`80-BF`/`D4-DF` program; chorus balance additionally changes two IC28 DD words
per tone.  This agrees with the schematic's one-way IC9 `DC0-DC7` sample bus
into IC8: IC9 is an intermediary for chorus sample/control delivery even
though IC8 owns the chorus SRAM.  Exact fixed-latch bit meanings and readback
behavior are still open.

Routine `AA00` can rewrite registers 0 and 1 after a key-mode/voice-structure
change.  Its four-entry table at `AB18` contains these pairs:

```text
55 E1   75 F1   75 F1   55 C1
```

The selected index is derived from the patch key mode and upper/lower tone
structure, strengthening the interpretation of `E700/E701` as global source
and routing configuration rather than chorus rate/depth controls.

## d50lib correspondence and cautions

The high-level implementation under
`/Users/giuliozausa/personal/programming/hack_d50/d50lib` is valuable as a DSP
oracle:

- its chain is per-tone EQ -> two-stage stereo chorus -> reverb -> final EQ
- chorus has eight presets, two halves, two operators per half, four triangle
  LFOs and two interpolated delays per operator
- reverb has the now hardware-confirmed 39 offsets and 55 coefficients in a
  recovered feedback/diffusion network over the D-50's 16384-sample memory
- its patch wrapper reproduces output mode, tone balance, total volume, and
  reverb dry/wet routing interactions

It is not a literal hardware-register implementation.  Its floating-point
coefficients, expanded chorus rows, and object layout should guide the audio
algorithm while the D-50 firmware and traces remain authoritative for bus
timing and register semantics.

## Remaining implementation order

1. Finish IC28 timing: BUSY, status bits and parity checking.
2. Decode IC8's fixed host latches; the high-level final chorus/output matrix
   is now implemented, but its individual IC28 selector mapping remains open.
3. Replace the provisional final EQ shelves with the recovered coefficient
   topology.
4. Continue using d50lib as a reference oracle while comparing UI and SysEx
   sweeps against firmware-generated fixed-point programs.

## Preliminary MAME effect model

`roland_d50_effects_device` now replaces the diagnostic direct-to-speaker
route and accepts all eight LA32 buses.  Its implementation has deliberately
different confidence levels:

- **strong**: LA32 buses 2+4 feed the lower chorus half and buses 0+6 feed the
  upper half; IC8, IC9, and both IC28 lanes are delivered to the audio device
  at the time of the firmware write.
- **strong**: chorus implements both serial operators in both tone halves,
  including all 16 triangle LFOs, eight 1411-sample fractional delays, signed
  Q12 matrices, feedback carry, firmware address permutations, quantization,
  and diagonal stereo recombination.  IC9's physical return order is the
  opposite of the software half-array order, so final left is `lower[1] +
  upper[0]` and final right is `lower[0] + upper[1]`; this makes I-48 and I-76
  match the independent reference stereo ratios.  All eight types and
  rate/depth/balance extremes have been exercised through live temporary-patch
  SysEx.
- **strong**: the reverb maps the confirmed 39 DC offset selectors and 55 DD
  coefficient selectors through the independently recovered d50lib scatter
  permutations.  Its delay/diffusion/feedback graph follows d50lib.  The two
  output accumulators now remain separate stereo returns.  Reverb balance and
  key/output mode follow the recovered high-level curves; normal mode 0 uses
  `0.4` direct gain, and quiet factory room program 1 receives a measured 1.25
  return normalization.  Conversely, program 6's unusually hot accumulator is
  normalized by `0.625`: on Slap Brass its return previously exceeded the dry
  attack by 2.4 dB, while the corrected return is 1.4 dB below it.  The LA32
  buses showed no saturation during that test.  A Metal Harp note-off A/B
  leaves about two seconds of reverb after the dry voice has become silent.
  Mode-dependent cross-routing for output modes 2/3 is still provisional.
- **strong at the behavioral level, provisional at the register level**: the
  final chorus/output wrapper now follows d50lib exactly.  It applies patch
  total volume and the clipped 4x tone-balance law before the two chorus
  halves, then uses the recovered three key/output-mode branches and the two
  per-tone chorus balances for the four dry cross-feed gains and two diagonal
  wet-return gains.  The first parameter block (`C4AD`) feeds tone 0 and the
  second (`C56D`) feeds tone 1; Whole modes 0/4 reuse `C4AD` for both stereo
  output sides because their second tone block is inactive.  Using the blocks
  in reverse order made velocity-sensitive Whole patches appear panned.  This
  replaced the former `(lower + upper) / 2` mono dry
  shortcut, which cut single-tone patches by 6 dB and made Spectrum-heavy
  patches use the wrong tone balance.  The corresponding IC28 selectors still
  need to be labeled individually.
- **strong**: the two LA32 partial lanes remain separate through the eight
  output buses and are combined with the recovered partial-balance curve before
  tone EQ.  Balance 50 is normalized to unity for both lanes; below 50 the
  coefficient slope is `0.016`, while above 50 it starts at `0.8` and rises by
  `0.004`.  The per-tone masks and balances come from live bytes `C4AE-C4AF`
  and `C56E-C56F`.  Equal-weight bus summing was especially wrong for I-76:
  its PCM tone requests balance 96, or approximately `0.08 : 1.23` after
  normalization, so the old `1 : 1` sum left the Spectrum loop about twelve
  times too prominent after the one-shot transient.
- **strong**: I-48's apparent low-velocity click is already present in the raw
  LA32 tone-1 structure, before EQ, chorus, or reverb.  Its main oscillator
  becomes audible about 2.7 ms into the attack.  The final stereo discontinuity
  is equal in both channels, and d50lib independently produces the same onset
  edge at every tested velocity.  Low velocity makes it perceptually exposed
  because the following tone body is quieter; it is not an effects feedback or
  channel-routing fault.
- **strong**: both editable tone EQs use all recovered lower/upper DD slots,
  exact packed shift/multiplier decoding, exact high/low fragment
  recombination, the recovered four- and five-coefficient recurrences, and the
  hardware gain smoothing.  The fixed post-effects output EQ is also present.
- **strong at the behavioral level, provisional at the register level**: patch
  total volume, tone balance, output mode, and chorus balance use the recovered
  wrapper directly from the live patch bytes.  The four total-level and related
  matrix words remain decoded and traced so a later fixed-ROM execution model
  can replace that high-level feed without changing audible behavior.

All inferred DSP stages are bounded against non-finite values and runaway
feedback.  The reverb additionally clamps its input to line level, watches
every delay write and feedback state for sustained overload, flushes its
history and latches itself bypassed after 32 overloaded samples, and stays
bypassed until selector 1 receives a changed DC topology operand.  Its stereo wet
returns have independent soft `0.35` (about `-9 dBFS`) ceilings, so normal tails
are audible without allowing an incomplete or corrupt program to become a
full-scale clipped oscillator.  A rapid all-64-factory-patch MIDI stress pass
did not trip the overload latch.  The final LA32-domain normalization is `1.5`;
the earlier d50lib-derived `3.2` assumed d50lib's different internal voice
scale and drove Metal Harp continuously into the safety limiter.  Output is now
fully linear through `0.9`, with an emergency soft ceiling approaching `0.98`
only for corrupt/incomplete parameter images.  Metal Harp and the dense MIDI
stress sequence both render without clipped samples.

Open work is concentrated in IC8/IC28 final routing, not the editable EQ
arithmetic or chorus core: the addressing, dirty protocols, reverb program
dimensions, EQ arithmetic, chorus registers and parameter-to-compiler call
graph are mapped.
