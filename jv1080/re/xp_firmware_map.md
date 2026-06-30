# Roland JV-1080 XP firmware map

This is a living reverse-engineering map of the JV-1080's SH7034 firmware and
Roland XP PCM/DSP interface.  `confirmed` means the conclusion is supported by
both firmware code and an emulator bus trace; `static` means it follows from
firmware code but still needs a behavioral sweep; `hypothesis` is explicitly
not suitable as an implementation contract yet.

## Images and address map

| Image | CPU range | SHA-256 |
|---|---:|---|
| `roland_r00677323_6437034c12f.ic15` | `0x00000000-0x0000ffff` | `24c8b1f29bbc91b4ce94f95ccda7b21fe27164ee8d4c23cbe190d0204fe14633` |
| `roland_r00678167.ic20` | `0x02000000-0x020fffff`, mirrored at `0x0a000000` | `6101e1fd4c662419d0ee06dc160f72e60ced84d7294481a104be64c993254f56` |

The reset PC is `0x000007f8`, initial SP is `0x0ffffff0`, and the firmware uses
the A27-set ROM and RAM mirrors (`0x0a...` and `0x09...`) heavily.  The XP is at
`0x04000000-0x04003fff`, mirrored at `0x0c000000`.

The external-ROM header defines ten RTOS tasks with 12-byte descriptors:

| Task | Entry | Current role |
|---:|---:|---|
| 0 | `0x0a02882e` | MIDI/input task |
| 1 | `0x0a028ff0` | panel task |
| 2 | `0x00001ce0` | internal synth/event task |
| 3 | `0x0a020e20` | display task |
| 4 | `0x0a01015c` | sequencer/demo task |
| 5 | `0x0a00a0c8` | XP/DSP task |
| 6 | `0x0a01a5d4` | effects/control task, role still being split |
| 7 | `0x0a021264` | I/O task, role still being split |
| 8 | `0x00009fa8` | voice-engine service task (misnamed `idle_task` initially) |
| 9 | `0x0a001200` | fallback/idle task |

The reproducible Ghidra import currently recovers about 970 functions, 964
defined strings, and about 24,000 labels/data symbols.  `NameJV1080.py` applies
180 evidence-backed function names, including all 40 RFX parameter builders,
the SCI0/MIDI path, the complete factory XP self-test, and the factory-test
dispatchers.  The
exports deliberately remain generated artifacts; `ImportJV1080.py`,
`NameJV1080.py`, and `ExportJV1080.py` recreate them.

## Factory diagnostics and SH7034 ADC

The hidden factory mode is now executable without firmware patches.  ROM Play
dispatcher `0x0a027eb0` recognizes SHIFT+ENTER followed by live GA row 3 equal
to `0x09` (UP+DOWN) while VALUE action `0x4f` is asserted.  The screen at
`0x0a029f78` dispatches Memory, LCD, Switch, Card, MIDI, Sound/EFX, LED, and
Initialize tests to the eight handlers named in `NameJV1080.py`.

This path exposed two non-XP hardware omissions.  GA registers `0x00-0x03` are
live active-high matrix rows, independent of the IRQ5 key FIFO.  GA registers
`0x10-0x17` are an active-high LED bitmap indexed by the same six-bit key code.
VALUE is active-low bit 0 of direct port `0x3b`; key and encoder IRQ payloads
remain stable until reads of `0x3e` and `0x3d`, respectively.

The SH7034 firmware's `sh_adc_scan_group` at `0x0a019fd8` proves the ADC mapping:
with `GBR=0x05fffe00`, it starts conversion at `GBR+0xf8`, sleeps two ticks,
then reads ADDRA-D at `GBR+0xe0`.  ADCSR/ADCR therefore belong at
`0x05fffef8/0x05fffef9`; the former `0xe8/0xe9` mapping left all results zero.
AN1 is the internal backup battery and must fall in firmware's healthy range
`0x1ff-0x2cc`; AN0 is the optional card battery.  The destructive S-RAM test
also proves a 64 KiB window at `0x02300000-0x0230ffff`.

With these corrections, Memory, LCD, all 35 panel switches, the output walk,
LED walk, MIDI loopback/connect-disconnect sequence, and initialization paths
complete.  MIDI is SCI0 at 31.25 kbaud; the test sends `A0 A1 A2` and its
dedicated poly-pressure callback sets the loopback flag.  The emulator exposes
normal MIDI ports plus a virtual factory cable.

The Sound test executes descriptor entries 10 and 11 at `0x0a057dcc`, checks
the two result bits returned by the hidden XP test routine, and displays
`EFX Test OK`.  A full XP trace changes the interpretation: CRAM, PRAM, and all
three IRAM walking-pattern/readback tests genuinely pass, but the final
256-slot DSP execution test accepts all-zero result slots because the firmware
only compares nonzero results.  DSP execution is still unemulated; the exact
program, seeds, and expected `00/55/aa/ff` outputs are documented in
`xp_dsp_isa.md`.  Absent cards correctly report as absent.  Full key order,
screenshots, and reproduction commands are in `factory_test_mode.md`.

## XP interrupt path

This is now confirmed: the XP is connected to SH7034 **IRQ7**, not IRQ5.
IRQ5 is the gate-array dispatcher used for the panel, LCD/DMA coordination, and
the 1 ms RTOS tick.  Vector 71 points to `xp_irq_dispatch` at `0x00001158`, which
reads only XP registers:

```text
XP 0x3918 -> reason in bits 0-3, source/voice field in bits 8-13
XP 0x391a -> accompanying data/readback
reason -> handler table at 0x00001190
```

Observed static dispatch meanings:

| Reason | Handler | Firmware action |
|---:|---:|---|
| 0-3, 6 | `0x000013cc` | no-op return |
| 4 | `0x0000121c` | acknowledge/no-op path |
| 5 | `0x000012fa` | update source/voice state and completion bookkeeping |
| 7 | `0x000011b4` | write `0x0130` to `0x3918`, signal an RTOS event with mask `0x10` |
| 8 | `0x000011de` | call the per-source handler at `0x00003b9c` with the decoded source field |

The XP device now queues reason-5 address-generator events, asserts the SH7034
IRQ7 input, exposes the source/reason at `0x3918`, and acknowledges the head
event when the firmware reads the low byte of `0x391a`.  A modulation preview
and a demo-song capture both execute the firmware handler; the latter handled
4,267 events from all 64 sources without an unacknowledged event or overflow.
SCCore confirms that reason 5 is raised each time a looping address generator
reaches its loop point, and at the terminal point of a one-shot voice.

## Voice-engine call graph

The central 100 Hz service path is:

```text
voice service task 0x00009fa8
  -> xp_update_all_voices 0x0000a268        (64 voices)
      -> xp_update_voice_pitch 0x00008a0e
      -> xp_update_voice_tva   0x000014e6
      -> xp_update_voice_tvf   0x00006be8
      -> LFO/modulation and mixer helpers

note allocation
  -> 0x0000f14c
      -> clear bit in XP reset bitmap       (0x3900-0x3907)
      -> xp_start_voice 0x00009764
      -> restore/set bit in reset bitmap
```

The task waits on RTOS event bit 4.  IRQ5 gate-array status 8 signals that event,
while status 9 supplies the independent 1 ms RTOS/demo-sequencer tick.  The
driver originally synthesized status 8 by dividing a 10 ms panel scan by five,
so the all-voice pitch, TVA, TVF, and status/random passes ran every 50 ms.
Instruction breakpoints disprove the earlier CPU-throughput theory: one active
pass takes about 76,800 SH cycles (3.84 ms at 20 MHz), then waits for the next
status-8 interrupt.  Status 8 is now generated by a separate exact 10 ms timer,
without changing the SH clock or the status-9 demo clock.

The XP-side cadence in the current implementation agrees with the independently
reversed SCCore oracle: pitch, amp-mod, TVF-F, and TVF-Q produce one value per
eight samples (4 kHz at the 32 kHz internal rate), and TVA produces one value
per two samples (16 kHz).  Control bits 13-12 then select divisors 1, 8, 32, or
128.  These clocks must not be doubled to compensate for firmware target timing.

The firmware-side LFO path is now separated.  `xp_compute_lfo_waveform` at
`0x00007ad4` advances a 16-bit phase by `rate * RAM[0x09001f61]`, then selects:

| Internal mode | Recovered behavior |
|---:|---|
| 0 | piecewise-linear triangle |
| 1 | linearly interpolated sine lookup table |
| 2 | sawtooth |
| 3 | square |
| 4 | trapezoid |
| 5 | sample-and-hold random value on phase wrap |
| 6 | interpolation between successive random endpoints |
| 7 | fresh random value |

The time-scale byte remains `1` throughout the modulation reproduction.  The
factory-preview square LFO advances its phase by 5,240 per pass.  The original
20 Hz status-8 cadence produced 1.599 Hz; intermediate 40 Hz and 80 Hz fixes
produced 3.198 Hz and 6.396 Hz and remained audibly slow.  At the corrected
100 Hz cadence, the observed phase steps map to almost exact musical rates:
`5240 -> 7.996 Hz`, `2620 -> 3.998 Hz`, and `3259 -> 4.973 Hz`.  The preview's
TVA target transitions now alternate at roughly 60/70 ms around the expected
62.5 ms half-cycle.  The firmware clamps the phase increment to `0x4000`, giving
a 25 Hz maximum at this update rate.  This isolates the speed error to the
emulated gate-array cadence rather than the SH core or XP interpolator.

At this point the firmware has configured `BCR=0x8000`, `WCR1=0xffff`,
`WCR2=0xffff`, and `WCR3=0x0000`.  The SH7034 hardware manual defines WCR3
`A02LW=00` as **one inserted state** for external areas 0 and 2.  The current
core's additional cycle on external-ROM reads therefore agrees with the manual;
removing all bus waits merely to accelerate modulation would contradict the
programmed BSC state.  The measured 3.84 ms voice pass also rules those waits
out as the source of the former 50 ms cadence.  See the
[SH7032/SH7034 Hardware Manual](https://www.renesas.com/en/document/mah/sh7032-sh7034-hardware-manual),
section 8.2.4.

The random source is not an SH software PRNG.  `xp_read_busy_random` first
performs a 32-bit trigger read from XP `0x320c`, then reads the low byte of the
high readback word at `0x3912`.  Boot writes `1` to `0x320c` to enable this
source.  This is distinct from a direct `0x3912` read after a voice command,
whose low nibble is the busy field.  SCCore supplies the exact generator: two
16-bit sequences seeded with `0xefa6` and `0x9c23`, XORed for each result.  The
emulator now implements this trigger and reproduces SCCore's initial words
`4f95, 4b65, 7cec, 8cca`.  Random tuning, voice initialization, random LFOs,
and the random waveform modes therefore no longer receive constant zero.
The factory IRAM3 walking-pattern test further proves that slot 3 is random
only while its stored value is exactly `1`: overwriting it with `0x555555`
must restore ordinary trigger/readback behavior.  The emulator now honors that
enable condition rather than treating every slot-3 read as random.

The live firmware voice record has a stride of `0x72` bytes.  Confirmed fields
within it include transition state at `+0x01`, TVA transition state at `+0x27`,
voice/layer state at `+0x26/+0x2c`, and TVF transition state at `+0x4d`.  Several
parallel arrays hold XP-ready 32-bit destinations and controls; firmware writes
these directly into the matching bank with `voice * 4` addressing.

`xp_start_voice` programs topology first, copies four additional 16-bit voice
state fields, installs all four mixer sends, writes the wave-control command and
polls `0x3912 & 0x0f` until ready, initializes address/loop-direction state,
marks the active-voice bitmap, then performs initial pitch, TVF, and TVA target
updates.  Voice reset and release update one of four 16-bit software bitmaps and
write the corresponding XP word three times; this triple write is literal
firmware behavior, likely a hardware synchronization requirement.

## Sequencer, MIDI, and apparent note staggering

The task at `0x0a01015c` is a ten-phase event router, not the voice renderer.
`sequencer_wait_phase_slot` waits on task event bit 1, advances a modulo-10
phase index, and selects one of three queue decoders.  Each decoder normalizes
its input to MIDI status semantics.  The literal table at `0x0a0104a6` maps
status nibbles `0x80-0xe0` to the now-named Note Off, Note On, Poly Pressure,
Control Change, Program Change, Channel Pressure, and Pitch Bend handlers.

`midi_note_on` performs channel/part filtering, key/velocity-range checks, and
builds a synth event for every matching part.  The internal synth task then
selects as many as four tones for one note.  `xp_commit_note_voices` commits
those tones through `xp_commit_one_voice` and `xp_start_voice`; consequently
the XP bank-0 start writes for one four-tone note are tightly grouped rather
than paced by the demo timer.

A voice-only demo trace confirms the distinction.  Four-tone note groups are
submitted in about `0.11-0.16 ms` (roughly `0.035-0.055 ms` between XP voice
commands).  Longer `0.7-2.1 ms` gaps occur between separate note groups, and
some musical events are separated by tens of milliseconds.  The firmware
therefore intentionally serializes distinct sequencer events, while the layers
of one note are effectively simultaneous.  This makes the reported
"staggering" measurable, but the trace does not support lengthening XP command
busy time as a fix: current busy emulation is only one status read, and the
large gaps occur before the next note reaches `xp_start_voice`.  A hardware
MIDI/audio capture of the same demo is still required before changing this
firmware-visible sequencing cadence.

## XP register map

### Per-voice banks

All entries below use a four-byte stride for 64 voices unless stated otherwise.

| Range | Meaning | Confidence/evidence |
|---:|---|---|
| `0x0000-00ff` | wave control / voice start command | confirmed; `xp_start_voice` writes then polls `0x3912 & 0x0f` |
| `0x0100-01ff` | sample start | confirmed |
| `0x0200-02ff` | sample loop | confirmed |
| `0x0300-03ff` | sample end | confirmed |
| `0x0c00-0cff` | voice state A | static |
| `0x0e00-0x0eff` | voice state B | static |
| `0x1000-10ff` | voice initialization value | confirmed at start; exact bits open |
| `0x1100-11ff` | TVF resonance/Q destination | confirmed from `xp_update_voice_tvf` |
| `0x1200-12ff` | pitch destination | confirmed from `xp_update_voice_pitch` |
| `0x1300-13ff` | TVF cutoff destination | confirmed |
| `0x1400-14ff` | amplitude-modulation destination | confirmed from TVA/modulation path |
| `0x1500-15ff` | TVA destination | confirmed |
| `0x1600-16ff` | TVF Q ramp control | confirmed |
| `0x1700-17ff` | pitch ramp control | confirmed |
| `0x1800-18ff` | TVF cutoff ramp control | confirmed |
| `0x1900-19ff` | amplitude-modulation ramp control | confirmed |
| `0x1a00-1aff` | TVA ramp control | confirmed |
| `0x1b00-1bff` | pitch starting/current value | confirmed |
| `0x1c00-1cff` | TVF cutoff starting/current value | confirmed |
| `0x1d00-1dff` | amplitude-modulation starting/current value | confirmed |
| `0x1e00-1eff` | TVA starting/current value | confirmed |
| `0x2000-20ff` | TVF type plus paired-tone topology/structure | confirmed role, bit layout partial |
| `0x2100-21ff` | TVF Q starting/current value | confirmed |
| `0x2300-23ff` | amplitude-modulation auxiliary/level | static |
| `0x2700-27ff` | amplitude-modulation auxiliary/base | static |

The destination/control/start interpretation is not based on write order alone:
the three named internal-ROM routines use exactly these triplets and transition
state fields.  A voice start programs all three values before enabling playback.

### Topology, filter order, ring modulation, and booster

`xp_start_voice` calls `0x00006d46` to program bank `0x2000`.  That helper reads
a per-voice structure array at `0x09003af4` and a 16-bit configuration array at
`0x09008778`:

- unpaired voices receive the 16-bit configuration word directly;
- for a paired structure, the odd partner receives zero and the even/owner
  voice receives the composite word;
- ordinary filter modes observed are `0x0400`, `0x0800`, and `0x0c00`;
- paired structures set `0x2000` and add topology/booster fields.

The controlled front-panel edit sweep now gives the exact encoding.  PR-A:030
was held constant with both tones active; every documented Structure value was
selected in turn and the resulting XP write was captured:

| Structure | Owner XP word (booster 0) | Documented topology |
|---:|---:|---|
| 1 | no paired word | independent `WG -> TVF -> TVA` paths |
| 2 | `0x1010` | two serial filters, TVA1 controls the input balance |
| 3 | `0x2010` | booster before the two serial filters |
| 4 | `0x3010` | first TVF, then booster, then second TVF/TVA |
| 5 | `0x4010` | ring modulation before the two-filter path |
| 6 | `0x5010` | ring path mixed with dry tone 2 |
| 7 | `0x6010` | filtered tone 1 ring-modulates tone 2 |
| 8 | `0x7010` | type 7 plus dry tone 2 before TVF2/TVA2 |
| 9 | `0x8010` | both tones filtered before ring modulation |
| 10 | `0x9010` | type 9 plus filtered dry tone 2 before TVA2 |

Thus bits 15-12 are exactly `Structure - 1` for paired types 2-10, and bit 4
marks the owner/even member of the pair.  The partner has the same high-nibble
structure value in firmware RAM without bit 4, but `xp_write_voice_topology`
writes zero to its XP slot.  This suppression means the owner XP lane is
responsible for processing both tone streams.

On Structure 3, sweeping Booster produced `0x2010`, `0x2050`, `0x2090`, and
`0x20d0`.  Bits 7-6 therefore encode 0, +6, +12, and +18 dB exactly.  The old
`decode_filter_type()` behavior only covers independent Structure 1 voices; it
does not implement any of these paired signal graphs.

The topology descriptions are cross-checked against the original Roland
owner's manual diagrams on pages 43-44:
<https://static.roland.com/assets/media/pdf/JV-1080_OM.pdf>.

### Global, DSP, mixer, and host-wave registers

| Range/register | Meaning | Confidence/evidence |
|---:|---|---|
| `0x2c00-2e3f` | 288 x 16-bit DSP coefficient RAM (CRAM) | confirmed by exact boot clear range |
| `0x3000-30ff` | 64 x 32-bit DSP internal RAM bank 1 | confirmed by boot clear |
| `0x3100-31ff` | 64 x 32-bit DSP internal RAM bank 2 | confirmed by boot clear |
| `0x3200-32ff` | 64 x 32-bit DSP internal RAM bank 3 / interpolated current values | confirmed by boot clear, SCCore, and routing helpers |
| `0x3300-337f` | 64 x 16-bit IRAM3 breakpoint targets | confirmed by firmware writes and SCCore |
| `0x3380-33ff` | unknown DSP configuration/reserve area | open |
| `0x3400-387f` | 288 x 32-bit DSP program RAM (PRAM) | confirmed by exact boot clear range |
| `0x3880-38ff` | unknown DSP configuration/reserve area | open |
| `0x3900-3907` | four big-endian 16-bit voice reset/completion bitmaps | confirmed; firmware writes each update three times |
| `0x3908-390f` | global engine configuration | confirmed existence, bit layout open |
| `0x3910` | low DSP readback word / host wave-ROM read-data latch | confirmed |
| `0x3912` | high DSP/random-trigger readback word, or direct voice-command busy status | confirmed multiplexed usage |
| `0x3914` | global configuration word (`0x403f` after boot) | static |
| `0x3916` | DSP access/update control | confirmed around every effects upload |
| `0x3918` | IRQ status/reason and source field | confirmed by IRQ7 handler |
| `0x391a` | IRQ-associated data | confirmed by IRQ7 handler |
| `0x391c` | EFX-delay comparator/status; factory test reads bit 6 | confirmed role, threshold semantics open |
| `0x3920` | host wave-ROM address high/middle field | confirmed |
| `0x3922` | host wave-ROM bank/high field | confirmed; written by metadata reader |
| `0x3924-3926` | DSP routing/address/control words | static |
| `0x3928-392e` | four IRAM3 interpolation rates, one per 16 slots | confirmed by JV trace and SCCore group 3 |
| `0x3930` | EFX-delay calibration drive (`step << 5` in factory test) | confirmed role, normal DSP meaning open |
| `0x3a00-3a7f` | mixer send 0 | confirmed |
| `0x3a80-3aff` | mixer send 1 | confirmed |
| `0x3b00-3b7f` | mixer send 2 | confirmed |
| `0x3b80-3bff` | mixer send 3 | confirmed |
| `0x3c00-3fff` | 1 KiB host wave-ROM aperture | confirmed; **not a fifth mixer send** |

`xp_wave_rom_read_byte` at `0x0000bb14` forms a 27-bit address in three pieces:
`0x3922 = address[26:20]`, `0x3920 = address[19:10]`, and the accessed aperture
byte is `0x3c00 + address[9:0]`.  The aperture read is deliberately discarded;
it triggers the transaction, after which the firmware obtains the byte from
the low half of the 16-bit latch at `0x3910`.  The masks `0x007f` and `0x03ff`
are literal firmware constants, not inferred sizes.  The current worktree now
models this latch and no longer aliases `0x3c00` to a fifth mixer-send array.

The board scanner at `0x0a00a1b0` recursively reads and classifies every wave
bank.  Its helpers recover the 16-byte board ID, build date, format, bank count,
directory addresses, and expansion name/number.  Internal ROM bank zero begins
with ID `INT100A`, date `1994-05-22`, and the expected format words.  A fresh
post-fix boot trace checked 52 internal-window transactions against a separate
inverse of the driver's address/data descrambler: all 52 matched and none
disagreed.  Later transactions probe unpopulated expansion/card banks.

Mixer entries are 16-bit.  Runtime active-voice values such as `0x58ca` and
`0x58cb` support the current split of level in bits 15-6 and destination bus in
bits 5-0.  Sends 0/1 route active dry audio to buses 10/11; sends 2/3 are the
effect sends.  Idle defaults route to buses 6/7/8/9 with zero level.

Dry sends 0/1 always retain the physical voice index.  Effect sends 2/3 can
redirect a paired/secondary voice to an owner index through a 64-byte mapping
array, so a paired structure is summed before entering the RFX graph.  Both dry
and effect sends have separate target/current arrays and firmware slew loops
(`xp_ramp_dry_sends` and `xp_ramp_effect_sends`); they are not instantaneous
register changes.  This is a concrete source of intentional output staggering
that is distinct from the missing IRQ7/completion timing.

## Effects upload paths

Three persistent workers are now separated and named from both their parameter
shapes and the product's effect model:

| Worker | Entry | RTOS mask | Identification |
|---|---:|---:|---|
| RFX/insert EFX | `0x0a0097f8` | `0x11` | selects one of 40 algorithms and copies a 12-parameter block |
| reverb | `0x0a009b44` | `0x03` | mode handling matches the eight ROOM/STAGE/HALL/DELAY/PAN-DLY types |
| chorus | `0x0a009d60` | `0x05` | builds the 128-entry modulation table before upload |

All three call `xp_dsp_upload_program0` at `0x0a009194`.  That routine replaces
exactly the first 104 DSP slots: PRAM `0x3400-0x359f` and CRAM
`0x2c00-0x2ccf`.  The fixed system portion occupies slots 104-255 (PRAM
`0x35a0-0x37ff`, CRAM `0x2cd0-0x2dff`); slots 256-287 are cleared reserve space.
Additional helpers update the `0x3200` and `0x3300` routing/state words.  A
normal boot has bulk uploads at about
0.420 s, 0.621 s, 0.721 s, 0.921 s, and 1.021-1.276 s.  Entering the demo or
changing a patch repeats the relevant reverb, chorus, and RFX uploads; during a
demo, coefficient-only `0x2dxx` updates also occur between full uploads.

IRAM3 is not an ordinary third state bank.  A 16-bit write at
`0x3300 + 2*i` installs a 9-bit breakpoint target for the 32-bit current value
at `0x3200 + 4*i`.  Slots are split into four groups of 16; their rates are the
16-bit words at `0x3928 + 2*(i >> 4)`.  The native JV boot writes group rates
`0x0100, 0x0100, 0x0300, 0x0300`.  SCCore implements slots 48-63 and uses the
matching group-3 default `0x0300`, updating once every two samples with an
exponential approach:

```text
current += sign(target - current) * ceil(abs(target - current) * rate / 65536)
```

SCCore stores the ramp in Q15 with `target = breakpoint << 6`.  The XP's
host-visible IRAM3 uses the equivalent Q22 form, so the emulation uses
`target = breakpoint << 13`; this agrees with the boot-time direct unity value
`0x00400000`.  Direct writes to `0x3200` seed a current value and cancel its
old ramp, while target writes start a new ramp.  Slot 3 (`0x320c`) becomes the
special random-generator trigger only while its stored value is exactly `1`.
The emulator now advances these ramps at the audio cadence and exposes the
moving current through the normal `0x3912:0x3910` DSP readback latch.

`xp_dsp_initialize` first clears all 288 PRAM/CRAM slots, loads one 256-slot
image, enables DSP access with `0x3916 = 7`, waits 200 RTOS ticks (about 200 ms),
disables access, then loads the second 256-slot image.  The second image is the
normal running base program.  This exact trace disproves the earlier tentative
`0x3400`/`0x3600` two-bank split: program boundaries are instruction indices,
not 0x200-byte address banks.

The selector byte at RAM `0x0901f885` indexes two parallel 46-entry tables:

- `0x0a044ebc`: PRAM and CRAM template pointer pairs;
- `0x0a059b30`: effect-specific parameter-update functions.

Indices 0-39 correspond exactly to the 40 front-panel RFX names and dispatch to
40 individually recovered builders.  Indices 40-45 all select the same nearly
empty program, the same two coefficients, and `xp_rfx_update_noop`; they are
padding/diagnostic-safe entries.  The 40 named effects reduce to 30 distinct
active images plus the common padding image (31 unique PRAM/CRAM pairs): for
example Overdrive/Distortion, Compressor/Limiter, and Stereo Chorus/Flanger
share microcode but use different parameter conversion paths.  The loader
special-cases selectors 15, 18, and 19 (Step Flanger, Triple Tap Delay, and
Quadruple Tap Delay) when selecting its runtime state callback.

The reverb and chorus workers do not select separate entries in this table.
They re-upload the currently selected RFX image for consistency and then update
their own fixed-program coefficients, IRAM breakpoint ramps, routing, delay
offsets, and modulation state.  `xp_load_reverb_settings` copies 12 common,
four output, six type, and five mode bytes; `xp_load_chorus_settings` copies 12
common and four chorus bytes.  The chorus worker also constructs a 128-step
modulation/rate table before entering its event loop.

The external-ROM region `0x0a002000-0x0a008fff` contains the effect builders,
parameter conversion tables, delay layouts, and algorithm templates.  The ROM
also contains the explicit 40-effect name table beginning with Stereo EQ,
Overdrive, Distortion, ... through Chorus/Flanger combinations.

The instruction/CRAM/ERAM encoding and retriggered parameter results are kept
in `xp_dsp_isa.md`.  A particularly important host-interface result is that a
DSP-memory read latches its data into `0x3912:0x3910`; it is not an ordinary
direct read.  The emulator now implements that readback path.  This fixed
runtime ERAM-offset updates that previously combined the new address with stale
status/wave-read data.

## Known emulator divergences exposed by the firmware

1. Reason-5 IRQ generation, IRQ7 wiring, the `0x391a` acknowledgement, voice
   command busy retry, reset-bit acknowledgement, and the `0x320c` hardware
   random trigger are implemented.  Reasons 4, 7, and 8 still lack a proven
   hardware event source; in particular reason 7 must not be synthesized until
   a real DSP commit boundary exists.
2. Most of `0x3900-0x39ff` remains passive storage beyond the confirmed reset,
   readback, busy, IRQ, wave-ROM, random, and IRAM3-rate paths.
3. DSP PRAM/CRAM uploads are stored but never executed.  IRAM3 ramps now
   advance and read back correctly, but no emulated DSP program consumes them,
   so RFX, reverb, chorus, output routing, and the real mixer graph are missing.
4. Bank `0x2000` is decoded but its paired topology, booster, ring modulation,
   and alternate TVF/TVA graphs are not executed.
5. XP interpolation rates agree with SCCore, and the firmware target-update
   task is now paced at 100 Hz by the independently modeled gate-array status-8
   event.  A physical IRQ trace would still be useful to confirm its exact phase
   and jitter.

The address-generator interrupt and metadata/readback omissions were plausible
causes of apparently staggered demo notes.  They are now testable independently
of the still-missing DSP execution and paired-tone signal graphs.

## Reproduction artifacts

- `ghidra/ImportJV1080.py`: composite SH-1 memory map, vectors, RTOS task roots.
- `ghidra/NameJV1080.py`: evidence-backed function and register names.
- `ghidra/ExportJV1080.py`: function/call/string/symbol inventories.
- `ghidra/DecompileJV1080.py`: bounded recursive call-neighborhood export.
- `lua/xp_trace.lua`: boot/modulation/demo bus traces with PC, PR, and optional
  R0-R14 snapshots.
- `lua/structure_sweep.lua`: automated 128-patch bank-`0x2000` sweep.
- `lua/structure_edit_probe.lua`: screenshots every Patch/Common edit page.
- `lua/structure_exact_sweep.lua`: controlled Structure 1-10 and Booster sweep.
- `analyze_trace.py`: byte-lane reconstruction and bank/access-site summaries.
- `verify_wave_reads.py`: checks latched host-window reads against raw wave ROMs.
- `extract_dsp_templates.py`: inventories all 46 DSP selectors, pointer pairs,
  parameter updaters, hashes, duplicate groups, and nonzero slot counts.
- `analyze_dsp_bytecode.py`: confirmed field/CRAM decoder and all-template
  opcode, store, and ERAM-pair inventory.
- `lua/rfx_parameter_sweep.lua`: all 40 algorithms and twelve field positions,
  with a released/retriggered Preview note after every edit.
- `analyze_rfx_sweep.py`: normalized PRAM/ERAM/CRAM delta report for a sweep.

## Open proof tasks

- Determine the physical sources for IRQ reasons 4, 7, and 8.  Reason 5 and its
  per-loop/one-shot timing are now implemented from SCCore evidence.
- Implement and verify the ten bank-`0x2000` signal graphs now that the exact
  structure and booster bitfields are known.
- Complete the PRAM opcode/special-register truth table now that the low-half
  field layout, CRAM fixed point, ERAM address pairs, and host readback latch
  are known; then execute the boot DSP images against captured bus inputs.
- Validate the SCCore-derived `0x320c` random sequence against physical XP
  hardware if a bus capture becomes available.
- Validate IRAM3's low-bit Q22 trajectory and direct-current-write cancellation
  on physical XP hardware.  The target/rate map, group split, and two-sample
  cadence are independently supported by native JV traffic and SCCore.
- Measure every pitch/TVF/TVA/amp-mod trajectory against native hardware.  The
  factor-of-two modulation bug is fixed at the gate-array status-8 source, but
  the XP auxiliary TVA state at `0x2300/0x2700` still needs to be executed.
