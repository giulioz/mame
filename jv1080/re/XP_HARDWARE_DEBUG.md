# JV-1080 XP chip — hardware debug ROM and first silicon findings

A MIDI-sysex debug ROM (see [`debugrom/`](debugrom/)) was flashed into a real JV-1080,
giving direct read/write access to the XP PCM/DSP chip over MIDI. This documents the
hardware bring-up and the first measurements taken from real silicon — including a
byte-exact validation of the emulated DSP upload and a real-silicon readback-latch
quirk the emulator does not model.

## 1. Setup

- **ROM**: `debugrom/ic20_debug.bin` (the patched external program ROM), split by
  `build.py` into `ic20_debug_LOW.bin` / `ic20_debug_HIGH.bin` and flashed into **two
  27C801** EPROMs (even/odd byte lanes, 512 KiB doubled to 1 MiB — see
  [`debugrom/README.md`](debugrom/README.md)). The unit boots normally to PATCH PLAY.
- **Link**: JV-1080 MIDI IN/OUT ↔ Arturia **MiniFuse 2** ↔ host.
- **Host client**: [`debugrom/jvdebug.py`](debugrom/jvdebug.py) (`python-rtmidi`).
  Protocol `F0 7D <op> <nibblized payload> F7`; address `0x0c00xxxx` reaches the XP on CS4.

## 2. Bring-up results (real hardware)

| Test | Result |
|---|---|
| PING (`F0 7D 00 F7`) | ✅ `F0 7D 00 00 01 F7` — hook + reply path live on silicon |
| PEEK ROM `0x0a000010` | ✅ `0x0a02882e` (correct `midi_task` pointer) |
| POKE + PEEK scratch RAM | ✅ `0xCAFEF00D` round-trip |
| PEEK_DSP CRAM (16-bit) | ✅ real coefficients |
| read_dsp PRAM/IRAM (32-bit) | ✅ real DSP instruction words (see §3 for the latch caveat) |

## 3. Finding: the 32-bit DSP readback latch needs settling time

A DSP-RAM read on the XP is a two-step "latch" transaction: a trigger read of the DSP
address loads the host-visible readback registers `0x3910` (low) / `0x3912` (high), which
are then read back. The original `PEEK_DSP` (op `0x21`) does both steps in one handler,
back-to-back. On real silicon this works for **CRAM** (`0x2c00-0x2dff`, 16-bit) but
returns stale data for **PRAM/IRAM** (`≥0x3000`, 32-bit).

**Decisive test** (not an assumption): load the latch with a CRAM read, then immediately
issue an atomic PRAM read.

```
CRAM 0x2c04 = 0x0009  (correct)
PRAM 0x3408 = 0x000009   <-- returns the PRECEDING CRAM value, not the correct 0x0030
```

The atomic PRAM read deterministically returns the **previous latch contents** (across all
trials; never the correct value over 12 repeats). So the handler reads `0x3910/0x3912`
*before* the 32-bit trigger has loaded them. CRAM's 16-bit latch settles fast enough; the
32-bit PRAM/IRAM latch does not. This latency is **not modeled in MAME** (its latch loads
instantly), which is why op-`0x21` passed in emulation but not on hardware.

**Workarounds (no reflash):**
- Read the latch as separate MIDI messages so the natural gap lets it settle —
  `jvdebug.JV.read_dsp(off)` does exactly this and reads PRAM/IRAM correctly.
- Do **not** interleave 32-bit (PRAM/IRAM) and 16-bit (CRAM) reads; a slow 32-bit latch
  load contaminates the next fast read. Read each region in its own pass. (This was
  observed directly: an interleaved capture produced spurious CRAM "mismatches" that
  vanished entirely when PRAM and CRAM were captured in separate passes.)

**Fix for the next ROM revision:** insert a few NOPs (or a short dummy-read loop) between
the trigger and the readback in `handle_peekdsp`. Worth batching with other changes
(e.g. a `DUMP` opcode) so it costs only one reflash.

## 4. Validation: silicon DSP == firmware upload == emulator

The firmware uploads the effect program identically in MAME and on hardware (same code,
same patch — default `USER:001 Symphonique`). MAME's driver dumps the uploaded image to
`xp_dsp_dump.hex` (via `xp_dump_cb`). Capturing the same region from silicon with
`read_dsp` and diffing, in clean separate passes:

```
PRAM (slots 0-63): 64/64 match firmware upload
CRAM (slots 0-63): 64/64 match firmware upload
```

Byte-exact. This simultaneously confirms three things: the firmware's DSP upload is
faithful on hardware, the emulator's capture of it is correct, and our silicon readback
path is correct. Notably `CRAM[0x2c02] = 0x3d24` on silicon matches the value MAME's XP
model independently produced, so the emulator's CRAM fixed-point handling is silicon-accurate.

### Sample of the captured real DSP program (PRAM, ~27-bit instruction words)

```
slot  0  0x3400 = 0x000074c0      slot  8  0x3420 = 0x00005280
slot  1  0x3404 = 0x00007521      slot  9  0x3424 = 0x00000021
slot  2  0x3408 = 0x00000030      slot 10  0x3428 = 0x00007db0
slot  3  0x340c = 0x0000f500      slot 11  0x342c = 0x04007430   <- bit26 (ext) set
slot  4  0x3410 = 0x00007580      slot 12  0x3430 = 0x00007465
slot  5  0x3414 = 0x000075e1      slot 13  0x3434 = 0x0000f415
slot  6  0x3418 = 0x00000030      slot 14  0x3438 = 0x04000023
slot  7  0x341c = 0x0000f5c0      slot 15  0x343c = 0x040074a3
```

CRAM coefficients, slots 0-15:
`0000 3d24 0009 0000 0000 0000 0009 0000 0000 e000 0005 02d1 2190 508b 1f87 6ff8`

## 5. What this unlocks

The XP DSP is now directly observable on real silicon — the instrument the
[`XP_OPEN_QUESTIONS.md`](XP_OPEN_QUESTIONS.md) catalog was missing. High-value next steps:

1. **Capture the full DSP state** (all 288 PRAM/CRAM slots + IRAM1/2/3) and diff against
   the firmware upload and the SCCore reference — directly attacks the DSP-ISA unknowns
   (opcode semantics, CRAM fixed-point format, the JV-vs-SC-88Pro microcode gap).
2. **Drive the XP directly**: with the engine idle (no notes), poke voices/registers and
   measure the chip's response, comparing against the emulator and SCCore.
3. **Resolve the hardware-only questions** (IRQ7 reasons 4/7/8, real readback timing,
   loop-boundary behavior) now that the CPU-side path is observable.

## 6. DSP ISA reverse-engineering — method, the confirmed host interface, and the first probe

We attack the XP ISA the same way the LSP/CSP/ESP were done — drive the DSP host interface
directly, upload small test programs, and read back IRAM / the host readback register — except
here the "host" is the SH firmware's debug ROM reached over MIDI, not an Arduino on a desoldered
bus. The facts below are **confirmed**; the instruction *encodings* remain **provisional**.

### 6.1 Confirmed: the firmware only touches the DSP at boot, then goes quiet

Instrumenting MAME's `xp_w` and tracing the real firmware: every SH write to CRAM / IRAM1/2/3 /
PRAM / config happens in **t ≈ 0.4–1.3 s (boot)** and then **stops completely** — zero writes to
the DSP area after t≈2 s while idle. Per-region last-write times: PRAM 1.15 s, CRAM 1.28 s,
IRAM1/2 0.42 s, config 0.72 s.

**Consequence — the two rules for every experiment:**
1. **No note-on and no Program Change.** Both make the firmware *re-upload* the DSP program
   (the effect workers rewrite PRAM/CRAM slots 0–103). Every earlier "my poke reverted / the
   zeroed program still ran" result was self-inflicted by playing notes / changing patches.
2. Boot, wait ~2 s for idle, **then** the DSP area is stable and ours to overwrite.

### 6.2 Confirmed: the host readback mechanism (CSP-style register, but no store-to-host needed)

DSP memory is **not** directly bus-readable. A PEEK of a DSP address returns **0** on the bus but
has the side effect of latching that location's value into the **host readback register
`0x3910` (low 16) / `0x3912` (high 16)**; the value is then read from there. Proven: `peek(0x3400)`
= 0 while `read_dsp(0x3400)` = `0x74c0`, and immediately after `peek(0x2c02)` the raw `0x3910`
reads `0x3d24` (CRAM[1]'s value). This is `jvdebug.read_dsp()`'s two-step transaction.
  - CRAM (`<0x3000`): 16-bit → `0x3910` only.
  - IRAM/PRAM (`≥0x3000`): 32-bit → `0x3912:0x3910`.

Unlike the CSP/ESP, the XP needs **no store-to-host DSP instruction**: the hardware exposes *every*
IRAM/PRAM/CRAM location on demand, so our test programs just store to IRAM1/2 and we read it. The
DSP **control** registers (`0x3908`, `0x3914`, `0x3916`, `0x3924`, …) are **write-only** — they
read back 0 on silicon (the emulator shadows them, so they read there — do not be fooled).

### 6.3 Confirmed: `0x3916` is the DSP run/stop

From the firmware boot trace: `0x3916` takes values `7` = RUN, `0` = STOP, and is **left at 7**.
Boot does `…0, 7 (run img1), 0 (stop), 7 (run img2, final)` (`xp_dsp_initialize 0x0a008f44`:
enable → wait ~200 ms `jsr 0x0a002082` r4=0xc8 → disable → load → enable). Only the two boot-init
functions touch it; per-patch workers never do. Config the firmware leaves set: `0x3908=0x1c19`,
`0x3914=0x403f`, `0x3924=0xd200`, `0x3928=0x100` (IRAM3 rate group 0). Whether a poked program
needs a `0x3916` `0→7` kick to (re)start, or the DSP runs PRAM continuously, is the open question
the probe (6.6) settles.

### 6.4 IRAM3 is "magic" — use IRAM1/IRAM2 as the observable

IRAM3 (`0x3200`) is auto-updated by the breakpoint-ramp engine (targets `0x3300`, rates `0x3928`);
direct writes seed a value but a ramp can move it. Do **not** use IRAM3 as a store target for
tests. IRAM1/IRAM2 (`0x3000`/`0x3100`) are plain host-writable working RAM (note: IRAM1/2 slots
6–9 are forced to 0 at idle — hardware L/R I/O taps — ignore them).

### 6.5 Provisional ISA hypotheses (unverified — the Rosetta stone)

The factory EFX test program (ROM `0x0a03efdc` PRAM / `0x0a03f3dc` CRAM) copies `IRAM3[0:3]→[4:7]`.
Decoded, it *suggests* (NOT yet reproduced on silicon):
- a parallel IRAM load/store channel in the PRAM high bits: load `IRAM3[i]` = `0x01000000|(i<<21)`,
  store `IRAM3[j]` = `(j<<21)`, store landing ~3 slots after the load;
- each block starting with `op 7` (`0x00007000+sel`) to clear the accumulator;
- a MAC ALU (field split since revised: [15:14]=store-control, [13:0]=addr word|column — XP_FACTS C23/C24), a CRAM coefficient, and a mem
  selector 8:0. CRAM fixed-point per `xp_dsp_isa.md` (`0x5000`=+1.0).

Treat all of the above as leads to test, not facts.

### 6.6 First probe experiment — find the load and the store

Zero everything, run only two instructions, and use a **known constant in CRAM** as the tracer:
put `0x0042` in the CRAM slot the *load* reads and `0` in the *store*'s slot, then fuzz the two
PRAM words until `0x42` (or a transform of it) appears somewhere in IRAM1/2. A hit simultaneously
(a) proves poked programs execute and (b) identifies a load+store encoding. Method: boot → idle →
`clear_dsp()` → set CRAM → upload the two PRAM words → (optionally kick `0x3916` 0→7) →
`monitor_iram()`. Harness: [`debugrom/xp_lab.py`](debugrom/xp_lab.py).

### 6.7 Retracted

Earlier revisions of this section claimed, in turn, "we can run poked programs" (from stale
readback reads), then a "latched shadow copy committed by a `0x3916` 0→7 edge," then "direct live
execution proven." **All three were wrong** — confounded by notes/Program-Changes re-uploading the
program, by using the magic IRAM3 bank, by unreliable fast readback, and by guessed encodings.
What survives is only the confirmed interface above; execution of poked programs is still to be
demonstrated by 6.6.

## 7. Tools

- [`debugrom/jvdebug.py`](debugrom/jvdebug.py) — host client (`JV().read_dsp/peek/poke`, `peekdsp`).
- [`debugrom/build.py`](debugrom/build.py) — rebuilds `ic20_debug.bin` + both EPROM halves.
- [`debugrom/README.md`](debugrom/README.md) — build, hook, protocol, MAME validation.
- The firmware-upload reference is regenerated by booting the driver
  (`xp_dump_cb` writes `xp_dsp_dump.hex`).
