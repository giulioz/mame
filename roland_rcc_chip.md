# Roland RCC / Effect Custom IC

These are reverse-engineering notes for the effect processor called "RCC" in
the U-220 and D-70 MAME drivers.  The U-220 service manual identifies its
physical part as **TC23SC140AF-007**; whether the D-70 uses the identical mask
revision is not yet established. Confirmed behavior is separated from working
theory so this document can guide a real execution engine without turning old
guesses into an interface contract.

## Hardware data path

The service schematic establishes this path:

```
MB87419 address/control -> MB87420 PCM/interpolation
                              |
                              | wide parallel digital sample/control bus
                              v
                    TC23SC140AF-007 effect IC
                              |
                    two M5M4464-10 DRAMs
                         (64K x 8 total)
                              |
                       PCM56 mono DAC
                              |
                    74HC4051 8-way mux
                              |
             six populated sample/hold/filter paths
                              |
      MIX L/R, DIRECT OUT 1 L/R, DIRECT OUT 2 L/R
```

The LP output is therefore neither mono nor a precomputed L/R pair.  The
MB87420 passes time-multiplexed voice samples to the effect IC, where the
firmware-programmed coefficients perform level, pan, output assignment and
effect sends.  The RCC serializes its output channels through one DAC and the
analogue multiplexer.  The 4051 has eight positions, but the U-220 schematic
populates six output paths.

MAME's shared `roland_rcc_device` preserves the 32 LP/TVF slots as separate
sound-stream inputs. It owns the host-visible program and state memories,
decodes the known dry L/R coefficient instructions, and provides a provisional
stereo fractional-delay chorus from the identified chorus state. That is a
useful functional substitute, not an emulation of the TC23SC140AF-007 execution
engine.

## CPU interface

On the U-220, the P8098 selects gate-array bank `0x40`; the effect IC then
occupies CPU addresses `0x0800-0x080d`. The D-70 exposes the same register
protocol in its external I/O window. Both firmwares treat offsets `0x00-0x02`
as one 24-bit payload latch.

| Offset | Direction | Observed operation |
|--------|-----------|--------------------|
| `00` | R/W | Payload byte 0 |
| `01` | R/W | Payload byte 1 |
| `02` | R/W | Payload byte 2 |
| `04` | W | Commit payload to one of 32 runtime state/modulation words; written value is the index |
| `06` | W | Commit payload to one of 256 program descriptors; written value is the index |
| `0a` | W | Select a program descriptor and copy it to payload bytes `00-02` for reading |
| `0c` | W | Reset/control latch; boot writes `00` |
| `0d` | W | Operating-mode latch; boot writes `14` while loading and `54` to run |

No firmware readback command for the `0x04` memory has been found.  Boot clears
all 32 indices twice.  This may indicate two internal banks or a pipeline with
two state copies; treating it as a simple single-bank RAM is sufficient only
for tracing host behavior.

### Boot sequence (`0xbb10`)

1. Write `00` to control offset `0c` and `14` to mode offset `0d`.
2. Copy 256 three-byte words from CPU ROM address `0x1a000` into command `06`
   indices `00-ff`.
3. Clear command `04` indices `00-1f`, twice.
4. Initialize state words `08`, `0e`, and `02`.
5. Write `54` to mode offset `0d`.
6. Read, modify, and write selected program words before normal audio begins.

This is a complete 256-step uploaded descriptor program.  It is not merely a
small set of mixer coefficients.

## Program descriptor format

For payload bytes `b0 b1 b2`, firmware masking and all captured programs agree
on this provisional decode:

| Field | Decode | Status |
|-------|--------|--------|
| External/state RAM selector | `(b0 << 2) | (b1 >> 6)` | Strong; `b0` is observed in range 0-3 |
| Operation | `(b1 & 0x3f) >> 1` | Strong field boundary, operation meanings unknown |
| Shift flag | `b1 & 1` | Strong field boundary, arithmetic effect not yet proved |
| Coefficient | signed `b2` | Confirmed by dry pan/level tests and effect sweeps |

The temporary dry mixer interprets coefficient `0x40` as unity for the known
dry-output operations.  The shift flag probably changes the coefficient or
accumulator scale, so `coefficient / 64` must not be generalized to every
operation yet.

The "external RAM selector" name is descriptive, not definitive.  Firmware
routines `0xc23c` and `0xc285` replace that four-bit field in groups of four
adjacent descriptors while preserving the operation, shift, and coefficient.
This is consistent with selecting delay/state taps.

## Firmware upload paths

Static analysis of firmware 1.02 finds three normal external entry points plus
one post-boot effect setup path.  All program/state helpers are in the contiguous
`0xbb10-0xc4bc` region.

| Entry/call site | Purpose |
|-----------------|---------|
| `0x2609 -> 0xbb10` | RCC reset and complete initial upload |
| `0x2749 -> 0xbae4` | Post-boot effect setup: marks effect blocks dirty, patches routing/address fields, and modifies descriptors `31` and `3a` |
| `0xa361`, `0xa4c2 -> 0xbc4b` | Per-voice output assignment, pan, and level updates |
| `0xaf05 -> 0xbe58` | Periodic chorus/reverb update state machine |

Important internal routines are:

| Address | Operation |
|---------|-----------|
| `0xbdc5` | Write staged 24-bit word using command `04` |
| `0xbddf` | Write staged 24-bit word using command `06` |
| `0xbdf9` | Select with command `0a`, then read payload bytes |
| `0xbe13` | Read/modify/write a descriptor's operation/address field |
| `0xbe20` | Read/modify/write a descriptor's coefficient byte |
| `0xbf96` | Reload a reverb/delay algorithm block from CPU ROM tables |
| `0xc04d` | Rebuild chorus routing and parameters |
| `0xc23c`, `0xc285` | Replace RAM-selector fields in four adjacent descriptors |
| `0xc2d1` | Rebuild reverb/delay state and tap parameters |

The routing path `0xbc4b` uses firmware tables to locate dry/send descriptors
for the selected LP voice. Sound Test (1), Sound Test (2), and pan sweeps gave
the dry L/R coefficient locations now listed in
`roland_rcc_device::update_dry_gain`. U-220 voice 28 has not yielded a normal
dry pair and remains intentionally muted by the temporary mixer.

## D-70 integration and slot phase

The D-70 uses the same payload, program commit, state commit, descriptor
readback, control, and mode registers. Its LP contexts are offset by four
entries in the circular RCC program layout:

```text
LP/TVF context N -> RCC dry-program voice (N + 4) & 31
```

This is observed rather than inferred from output level. A single MIDI note
allocates D-70 TVF contexts 1 and 2, while its dynamic dry coefficients are
written to descriptors `0x2a/0x2b` and `0x34/0x35`; those are dry pairs 5 and 6
in the independently recovered U-220 table. With a zero offset the ROM demo
happened to remain audible, but an ordinary MIDI note was silent. Applying the
four-slot phase produces the expected unequal L/R note levels and stereo ROM
demo output.

A 12-note, four-partial allocation trace fills all 30 advertised voices and
shows that firmware uses contexts `1-23` and `25-31`, reserving contexts `0`
and `24`. Those reserved contexts align with RCC program voices `4` and `28`;
program voice 28 is the independently observed slot without an ordinary dry
pair. The phase must therefore wrap: contexts `28-31` map to program voices
`0-3`. Treating the offset as a non-wrapping addition silently dropped four
valid voices and removed late-allocated layers from dense ROM-demo passages.

The current D-70 audio path is therefore:

```text
MB87420 LP slots -> 32 TVF contexts -> RCC dry + provisional chorus -> stereo DAC stand-in
```

The model retains all uploaded D-70 RCC program and state words. It still
bypasses reverb, long delay, Structures, direct-output routing, and the external
effect DRAM; chorus is the functional approximation described below.

## Provisional chorus

Controlled U-220 sweeps and D-70 factory-performance traces identify both
stereo chorus lanes:

| Lane | Rate | Centre delay | Depth |
|------|------|--------------|-------|
| Left | state `09` | state `0a` | state `0b` |
| Right | state `0f` | state `10` | state `11` |

Program coefficients `01` and `03` control the two wet output levels. The
functional model uses a provisional 2-30 ms fractional delay, the observed
`e0-ff` rate coordinate, opposite-polarity stereo LFOs, and the measured depth
ranges
(`0b`: `000003-0002fd`, `11`: `000001-0000ff`). Exact physical delay/rate
units, effect sends, feedback, output-mode routing, arithmetic and interpolation
remain unproved.

Factory performance `55 Schizoid` is an important audible discriminator. Its
LP steps are correctly tuned in the low register, and its RCC state selects
nonzero delay/depth at a high chorus rate. The bundled offline wave-ROM
decompression is useful for identifying sample content, but its leaky decoder
is not hardware evidence and is not used by the LP core.

## Click-free update state machine

`0xbe58` is not an immediate parameter copier.  It has three phases:

1. Repeatedly slew effect-path coefficients toward silence.
2. At silence, replace the algorithm/tap fields and runtime state.
3. Slew the coefficients back to their requested values, then clear the dirty
   flags.

This explains why one type change can produce 400-780 command-`06` writes even
though far fewer descriptor indices are involved.  The repeated writes are an
intentional fade around the reconfiguration and should eventually be preserved
by the emulator; applying only the final state risks audible discontinuities.

## Controlled SysEx sweep

`u220/generate_rcc_sweep.py` writes individual Patch Common parameters with
Roland DT1 messages.  Table 10 of the MIDI implementation assigns one 7-bit
data byte to each parameter.  Nibble pairs are used by character/bulk formats,
not by these individual writes.

The sweep covers all effect types and exercises ordinary controls under an
algorithm where the manual says they are active.  The following locations are
repeatable across runs:

| Parameter and context | Command `06` program words | Command `04` state words |
|-----------------------|----------------------------|--------------------------|
| Reverb type, all 8 types | Broad reverb block: `00-28`, `2c-33`, `38-39`, `3c-5b`, `5d`, `5f-70`, `73-9f`; many repeated fade writes | None |
| Reverb time, Room 1 | `70` coefficient | None |
| Reverb time, Cross Delay | Tap groups `08-0b`, `1c-1f`, `68-6b` | None |
| Reverb level | Coefficients `2e`, `38` | None |
| Reverb/delay feedback | Coefficient `70` | None |
| Chorus type, all 5 types | `01`, `03`, `11`, `92`, `b0-b3`, `c1`, `cb`, `d3`; repeated fade writes | `08-0b`, `0e-11` |
| Chorus output mode | `1d`, `1f`, `6e`, `71`, `c9`, `cc`, `d1`, `d6` | None |
| Chorus level, Chorus 1 | None | `0a`, `10` |
| Chorus delay, Chorus 1 | `01`, `03` (`c1`, `cb`, `d3` are also rewritten) | None |
| Chorus rate, Chorus 1 | None | `09`, `0f` |
| Chorus depth, Chorus 1 | None | `0b`, `11` |
| Chorus/short-delay feedback | `d3` (`01`, `03`, `c1`, `cb` are also rewritten) | None |
| Short-delay time | `01`, `03`, `d3` | None |

Representative coefficient/state curves also reveal the fixed-point scales:

| UI values | Captured values |
|-----------|-----------------|
| Reverb level `0,8,16,24,31` | coefficients `00,0f,22,42,7f` at both `2e` and `38` |
| Reverb feedback `0,8,16,24,31` | coefficients `00,0f,22,42,7f` at `70` |
| Chorus level `0,8,16,24,31` | state `200000,308000,410000,518000,600000` at `0a` and `10` |
| Chorus rate `0,8,16,24,31` | state `e00000,e80000,f00000,f80000,ff0000` at `09` and `0f` |
| Chorus depth `0,8,16,24,31` | state word `11`: `000001,00001f,000046,000085,0000ff`; word `0b` is approximately three times it |

Chorus feedback crosses a signed discontinuity between UI encodings `31` and
`32`: descriptor `d3` changes from coefficient `fd` to `00`.  That is strong
evidence that its coefficient is signed and that the UI's `-31..+31` range is
stored with an offset/bias.

## Mask-ROM theory

The chip is known to contain mask ROM, but the host still uploads all 256
descriptors.  The most likely split is:

- mask ROM implements the fixed per-step execution engine and meanings of the
  5-bit operation field;
- uploaded descriptors select an operation, RAM/state source, shift, and
  coefficient for each step in a 256-step sample-frame program;
- command-`04` words provide slowly changing modulation and delay state;
- external DRAM stores audio delay history;
- CPU ROM tables define the actual chorus/reverb topologies by patching the
  uploaded descriptor program.

Thus dumping a physical chip's mask ROM would reveal operator semantics, not
necessarily a self-contained Room/Hall/Delay program.  This remains a theory
until an impulse test connects individual operation codes to arithmetic and
memory behavior.

## Current shared device and remaining work

The former per-driver RAM shims have been replaced by the shared sound device.
It owns the 256 three-byte descriptors, 32 three-byte state words, payload and
control/mode latches, descriptor readback, decoded dry gains, and save-state
data. U-220 uses program phase zero; D-70 configures phase four.

Remaining work for a real RCC execution engine is:

1. Determine whether command `04` is double-buffered and why initialization
   clears it twice.
2. Execute the uploaded 256-step program once per 32 kHz frame in hardware
   slot order rather than decoding dry coefficients plus the provisional
   host-level chorus.
3. Characterize each used operation code with sparse descriptor programs and
   impulse/constant inputs; derive accumulator width, rounding, saturation,
   shift behavior, and delay-RAM addressing.
4. Model the 64K x 8 external delay DRAM and the six-channel DAC/multiplexer
   schedule.
5. Compare impulse responses and Effect Test output against real hardware
   before removing the temporary dry mixer.

The current evidence implements the host-visible shell and retains all firmware
uploads losslessly. It is not yet enough to execute arbitrary RCC programs
without inventing operation semantics.
