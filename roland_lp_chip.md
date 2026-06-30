# Roland LP PCM Chip: MB87419 / MB87420

## Overview

The MB87419/MB87420 is a two-chip PCM playback system used in Roland synthesizers (e.g. D-70, U-20, CM-32P, R-8). It provides **32 independent PCM voices** reading from an external ROM, with hardware envelope generation and sample interpolation. In MAME it is modeled as a single device (`mb87419_mb87420_device`).

The chip pair is internally pipelined across a fixed number of processing cycles per voice per output sample.

---

## Two-Chip Architecture

| Chip | Role |
|------|------|
| **MB87419** (Chip 1) | Address generator: 18-bit adder, RAM A holding per-voice parameters (bank, loop mode, frequency, envelope, phase address, end/loop addresses) |
| **MB87420** (Chip 2) | Signal processor: 28→22-bit multiplier, 16-bit adder, ROM-based LUTs for interpolation, accumulator RAM |

---

## Clocking

- Input clock is divided by 2 internally.
- Sample rate = `(clock / 2) / 512` → typically **32 000 Hz** at a 32.768 MHz input.

---

## Voice / Channel State

There are 32 voices. Each voice holds:

| Field | Width | Meaning |
|-------|-------|---------|
| `addr` | 32-bit (18.14 fixed-point) | Current playback position. Integer part [31:14] = ROM word address; fractional part [13:0] = sub-sample phase. |
| `step` | 16-bit (2.14 fixed-point) | Pitch step added to `addr` each output sample. `0x4000` = 32 000 Hz (unity pitch). |
| `bank_loopmode` | 8-bit | Bits [5:2]: ROM bank (4 bits, selects upper address bits [21:18]). Bit 7: alternate/ping-pong loop flag. Bit 6: reverse playback flag. |
| `end` | 16-bit | Sample end address, high word (address bits [17:2]). |
| `loop` | 16-bit | Sample loop address, high word (address bits [17:2]). |
| `volume_cur` | 26-bit | Current envelope level (internal linear scale). |
| `volume_dest` | 8-bit | Envelope destination level (index into `env_limit_table`). |
| `volume_incr` | 8-bit | Envelope rate. Bit 7: 0 = attack (rising), 1 = decay (falling). Lower 7 bits index `env_incr_table`. |
| `play_dir` | int8 | Current loop direction state (used by ping-pong logic). |
| `enable` | bool | Voice active flag. |
| `irq` | bool | Interrupt pending (envelope still moving). |
| `tempReference` | 32-bit | DPCM accumulator. |

---

## Register Map

All registers are 8-bit wide. The host CPU selects the target voice with register `0x1F` before accessing per-voice registers.
The parameter RAM is word-oriented: firmware writes the odd (high) byte first,
and the following even (low) byte write commits the complete 16-bit word.
Emulating each byte as immediately live causes torn pitch and address values.

### Per-voice registers (offsets 0x00–0x0F, apply to selected voice)

| Offset | Direction | Description |
|--------|-----------|-------------|
| 0x00 | W | Envelope volume byte 0 (bits [7:0]) |
| 0x01 | W | Envelope volume byte 1 (bits [15:8]) |
| 0x02 | W | Envelope volume byte 2 (bits [23:16]) |
| 0x03 | W | Envelope volume bits [25:24] (bits [1:0]) + ROM bank & loop mode (bits [7:2]) |
| 0x04 | W | Pitch step LSB |
| 0x05 | W | Pitch step MSB |
| 0x06 | W | Envelope speed (`volume_incr`) |
| 0x07 | W | Envelope destination (`volume_dest`) |
| 0x08 | W | Current address byte 0 (fractional, bits [7:0]) |
| 0x09 | W | Current address byte 1 (fractional, bits [15:8]) |
| 0x0A | W | Current address byte 2 (integer, bits [23:16]) |
| 0x0B | W | Current address byte 3 (integer, bits [31:24]) |
| 0x0C | W | Sample end address LSB (bits [7:0] of high word) |
| 0x0D | W | Sample end address MSB (bits [15:8] of high word) |
| 0x0E | W | Sample loop address LSB |
| 0x0F | W | Sample loop address MSB |

### Global control registers (offsets 0x10–0x1F)

| Offset | Direction | Description |
|--------|-----------|-------------|
| 0x10 | W | Latch voice volume LSB into readback (argument = voice number) |
| 0x11 | W | Voice enable mask for voices 0–7 (1 bit per voice) |
| 0x12 | W | Latch voice volume MSB + bank/loop into readback |
| 0x13 | W | Voice enable mask for voices 8–15 |
| 0x14 | W | Latch voice frequency into readback |
| 0x15 | W | Voice enable mask for voices 16–23 |
| 0x16 | W | Latch voice envelope speed/target into readback |
| 0x17 | W | Voice enable mask for voices 24–31 |
| 0x18 | W | Latch voice current address (low word) into readback |
| 0x1A | W | Latch voice current address (high word) into readback |
| 0x1C | W | Latch voice sample end into readback |
| 0x1E | W | Latch voice sample loop into readback |
| 0x19/1B/1D | W | Unknown config registers |
| 0x1F | W | **Voice select** (sets which voice 0x00–0x0F writes target) |

Writing to 0x11/0x13/0x15/0x17 also resets `play_dir` and `irq` on voices that transition from disabled to enabled.

### Read ports (offsets 0x00–0x03, read from chip)

| Offset | Description |
|--------|-------------|
| 0x00 | Interrupt channel number (which voice last triggered IRQ) |
| 0x01 | ROM byte at the address computed from the latched IO buffer (used by CPU to read sample table) |
| 0x02 | Readback register LSB |
| 0x03 | Readback register MSB |

---

## Sample Encoding (Roland Logarithmic PCM)

ROM data is stored in a proprietary floating-point byte format. Each byte encodes a **delta** value (used in DPCM — see below).

Decoding a signed byte `data`:

1. Extract sign: `sign = (data < 0) ? -1 : +1`; `val = abs(data)`
2. `shift = val >> 4` (upper nibble, 3 bits usable)
3. `mantissa = val & 0x0F` (lower nibble)
4. If `shift == 0`: `result = mantissa`
   Else: `result = (0x10 | mantissa) << (shift - 1)`
5. Final: `result * sign`

This gives a range of roughly −4064 to +4064 with logarithmic spacing (credit: Sarayan).

---

## Address Generation and Pitch

Each output sample, the fractional address is advanced. The integer address is
18 bits; ROM bank selection is kept separately so phase overflow cannot spill
into the bank bits:

```
sub_phase += step
sub_phase_of = sub_phase >> 14     // how many whole samples were crossed (0..7)
addr = (integer_part << 14) | (sub_phase & 0x3fff)
```

`sub_phase_of` tells the engine how many consecutive ROM bytes to fetch and accumulate as DPCM deltas this cycle.

The full 22-bit ROM address for a fetch is:

```
rom_address = bank_bits[21:18] | sample_integer_address[17:0]
```

---

## Loop and Playback Modes

Configured via two flag bits in `bank_loopmode`:

| Bit 7 (`altLoop`) | Bit 6 (`backwardsPlay`) | Mode |
|:-----------------:|:-----------------------:|------|
| 0 | 0 | Forward loop: play forward, jump to `loop` when `end` is reached |
| 0 | 1 | Reverse: play backwards |
| 1 | 0 | Ping-pong: alternate direction between `loop` and `end` |
| 1 | 1 | Ping-pong reversed (start direction backwards) |

The `play_dir` / `altLoopState` field tracks the current direction during ping-pong playback.

**Address comparison** at each step: the integer address is compared against `loop` or `end` (depending on current direction). On a match, the address wraps/bounces accordingly.

---

## DPCM Accumulator

The decoded sample bytes are **deltas**, not absolute PCM values. Each voice maintains a running accumulator `tempReference` (18-bit signed):

```
for each whole sample crossed (sub_phase_of times):
    tempReference = clamp(tempReference + decode_sample(rom_byte), -262144, +262143)
```

The final `tempReference` is the raw audio value before interpolation and volume.

---

## Interpolation

The chip performs **3-point weighted interpolation** between consecutive decoded samples using a hardware ROM LUT (`interp_lut[3][128]`).

`interp_ratio` = bits [13:7] of the fractional address (0–127), representing position between samples.

Three LUT rows provide weights for samples at positions 0, +1, and +2 relative to the current address. The result:

```
output = tempReference
       + (interp_lut[0][ratio] * samp0) >> 12
       + (interp_lut[1][ratio] * samp1) >> 12
       + (interp_lut[2][ratio] * samp2) >> 12
```

(All clamped to 18-bit signed range.)

---

## Envelope Generator

Each voice has an independent volume envelope with a single segment: move from `volume_cur` toward `volume_dest` at rate `volume_incr`.

- `volume_dest` → looked up in `env_limit_table[256]` to produce a 26-bit target level.
- `volume_incr` → looked up in `env_incr_table[256]` to produce a per-sample increment.
  Bit 7 of `volume_incr` selects direction: 0 = increasing (attack), 1 = decreasing (decay).
- Each sample, if `volume_cur` has not yet reached `volume_dest`, it is moved by the increment and clamped on overshoot.
- The envelope rate ranges from extremely slow (~128 seconds) to nearly instant (<1 ms).

### Volume Scaling

The interpolated PCM value is scaled by the current envelope level:

```
output_sample = (pcm_value * (volume_cur >> 10)) >> 12
```

---

## Interrupts

The chip asserts its interrupt line (`/INT`) while **any voice** has an envelope still moving (i.e., `volume_cur != volume_dest`). The register at read offset `0x00` reports which voice last triggered the interrupt. The host CPU polls this to react to envelope completions (e.g. to silence a voice or start the next segment).

---

## Output

The U-220 schematic shows one digital `DA` connection from the MB87420 to the
RCC effect IC. This is not an already-summed mono signal: it is a serial,
time-division stream whose voice slots remain distinct. The RCC program assigns
those slots to MIX L/R and DIRECT 1/2 L/R, applies panning/effects, and drives
the shared DAC and analogue output multiplexer.

MAME therefore exposes the LP's 32 slots as separate sound-stream outputs. The
U-220 driver feeds them through a temporary dry RCC mixer. Its per-voice L/R
coefficient locations and `0x40` unity scale were identified with Sound Tests
(1) and (2), so firmware pan and level changes already produce stereo output.
The temporary mixer does not execute the RCC program: effects are bypassed and
MIX/DIRECT output assignments are folded into the same stereo pair.

---

## Summary of Key Constants

| Constant | Value |
|----------|-------|
| Number of voices | 32 |
| Output sample rate | clock / 1024 (≈ 32 kHz) |
| Address format | 18.14 fixed-point (32-bit) |
| ROM address space | 22-bit (4 MB with bank bits) |
| Pitch step for 32 kHz | `0x4000` |
| Volume accumulator width | 26-bit |
| DPCM accumulator width | 18-bit signed |
| Interpolation LUT size | 3 × 128 entries |
