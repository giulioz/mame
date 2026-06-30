# Kawai K4 custom audio-chip register map

This is the complete register map visible in the K4/K4r firmware for K002-FP,
K008-FP, and K004-FP.  It distinguishes confirmed bus behavior from semantic
names inferred by following patch parameters through the firmware.  Undecoded
bitfields are left explicitly unknown.

K007-FP is a fourth custom device and is the programmable effects processor
in the keyboard K4.  Its upload protocol, microcode directory, and parameter
compiler are documented separately in `kawai_k4_k007_dsp.md`.

## Address decoding

```text
0xe000-0xe3ff  K002-FP number 0, voices 0-7
0xe400-0xe7ff  K002-FP number 1, voices 8-15
0xe800-0xebff  K008-FP, 16 filters
0xec00-0xefff  K004-FP pan pot (keyboard K4 only)
```

Only the offsets listed below are accessed.  The larger windows are a result
of coarse board-level address decoding.

## K002-FP DCO/DCA

Two identical K002 chips provide 16 logical voices.  Each logical voice has
two source slots, giving 16 source slots per chip and 32 across the machine.

### CPU-visible registers

Offsets are relative to `0xe000` or `0xe400`.

```text
offset  access  firmware-visible function
+0      W       32-bit command latch byte 0
+1      W       32-bit command latch byte 1
+2      W       32-bit command latch byte 2; low byte used by live pages
+3      W/R     command latch byte 3; high byte used by live pages;
                bit 7 is read during the power-on diagnostic
+4      W/R     selector/commit; read bit 0 = busy
+5      W       diagnostic/self-test command
+6      W       source gate mask bits 0-7
+7      W       source gate mask bits 8-15
+8      W       global source mask low; firmware writes 0xff
+9      W       global source mask high; firmware writes 0xff
+a      W       global configuration; firmware writes 0xe6
```

Offsets above `+0x0a` are never accessed by any dumped K4 or K4r firmware.

At boot, the diagnostic writes zero to latches `+0..+3`, polls `+4` bit 0,
commits selectors `0xd0-0xdf`, writes the same value to `+5`, then examines
`+3` bit 7.  The exact diagnostic meaning of `+5` and `+3.7` is not known.

The gate registers are shadowed in RAM and use two adjacent bits per logical
voice, one bit per source slot.  A zero disables that source.  Firmware writes
the whole 16-bit mask after every gate change.

### Selector format

Writing offset `+4` commits the four command latches to:

```text
selector bits 7-4  internal page
selector bits 3-0  source slot 0-15
```

For logical voice `v`:

```text
chip = v >> 3
slot A = 2 * (v & 7)
slot B = slot A + 1
```

### Live source pages

Normal live updates write only CPU latches `+2/+3`; these form a little-endian
16-bit live value.  Runtime RAM interleaves the two sources: source A uses
bytes `base+0` and `base+2`, while source B uses `base+1` and `base+3`.

```text
page  runtime bytes  confirmed/inferred function
0x0   +00..+03       waveform/source control
0x1   +04..+07       pitch increment
0x2   +08..+0b       waveform-direction/mode control and peak DCA level
0x3   +0c..+0f       DCA control and sustain level
0x4   +10..+13       DCA attack coefficient
0x5   +14..+17       DCA decay coefficient
0x6   +18..+1b       DCA release coefficient
```

Known control bits include:

```text
page 0, control bit 3  select 8-bit PCM rather than paired 16-bit PCM
page 2, control bit 1  special mode from PCM descriptor flag bit 1
page 2, control bit 4  reverse sample direction
```

Other page 0/page 2 bits encode source pairing, AM/ring modulation, source
mode, and DC/PCM setup.  Their effects are confirmed by the compiler paths,
but their individual hardware names remain uncertain.

The attack, decay, and release words are nonlinear lookup-table results, not
the patch's 0-100 values.  Likewise, page 1 is the final pitch increment after
coarse/fine tuning, key tracking, fixed-key selection, bend, vibrato, and
modulation.

### PCM address pages

PCM selection writes complete four-byte commands to three additional pages:

```text
page  command bytes       function
0x8   00 00 start_lo hi   sample start word
0xa   00 00 loop_lo hi    loop address low 16 bits
0xc   00 00 end_lo hi     end address low 16 bits
```

The six descriptor bytes and their 19-bit expansion are documented in
`kawai_k4_k002_descriptor.md`.  These pages are static for a selected PCM key
zone; live pitch and DCA changes do not rewrite them.

The DC-wave path uses pages `0x8`, `0xa`, and `0xb` with commands of the form
`00 00 00 index`.  Page `0xb` is therefore a DC-wave auxiliary/mipmap page,
not a fourth PCM address.

Pages `0x7`, `0x9`, and `0xd-0xf` have no normal firmware use.  Selectors
`0xd0-0xdf` are used only by the power-on diagnostic.

## K008-FP digital filter

### CPU-visible registers

Offsets are relative to `0xe800`.

```text
offset  access  function
+0      W/R     filter word low byte / committed-word readback
+1      W/R     filter word high byte / committed-word readback
+2      W       selector and write strobe
+3      W       global enable/configuration; firmware writes 0x01
```

The write protocol is:

```text
write +0 = low byte
write +1 = high byte
write +2 = selector | 0x80
write +2 = selector
read +0/+1 until both equal the submitted word
```

Selector bits `3-0` choose filter voice 0-15.  Bits `6-4` choose page 0-7.
Bit 7 is the commit strobe and is not part of the stored page number.

### Per-filter pages

```text
page  runtime bytes  confirmed/inferred function
0x0   +34/+35        filter-envelope attack coefficient
0x1   +36/+37        active decay or release coefficient
0x2   --             unused by firmware
0x3   +38/+39        filter-envelope sustain target
0x4   +3a/+3b        resonance, LFO enable, routing, and mode control
0x5   --             unused by firmware
0x6   +3c/+3d        signed filter-envelope depth
0x7   +3e/+3f        current cutoff accumulator, clamped to 0x0000-0x1fff
```

Page 4 has the following confirmed fields:

```text
low byte bits 0-2   resonance 0-7
low byte bit 3      filter LFO enable
low byte bits 4-7   voice/source routing code
high byte bit 0     filter/source-pair selection
```

High-byte bits 2-3 and 5-7 carry source mode, active state, and routing flags;
their exact individual hardware meanings are not yet separable from firmware
alone.

Page 7 is updated while the filter envelope runs.  The firmware stores a
13-bit value, saturating below zero to `0x0000` and above range to `0x1fff`.
This is the best register for tracing the instantaneous cutoff rather than the
patch's unmodulated cutoff parameter.

Pages 0, 1, 3, 6, and 7 are written during normal voice start.  Page 4 is
written separately when routing/resonance changes.  Special initialization
paths also write page 1, clear page 3, and force page 7 to either `0x1d4c` or
`0x1fff`.

## K004-FP pan pot

K004 has one write-only CPU port at `0xec00`:

```text
bits 7-4  internal input channel 0-15
bits 3-0  pan code
```

Firmware initializes every channel with codes `0x00, 0x10, ... 0xf0`.
Normal pan values use codes 1-15:

```text
pan = (command & 0x0f) - 8
```

This gives the user range `-7..+7`; code 1 is hard left, code 8 is center, and
code 15 is hard right.  Code 0 is an initialization/reserved state rather than
a user pan position; whether it also mutes the channel is not established.

The eight submixes are routed to K004 channels:

```text
submix 0 1 2 3 4 5 6 7
channel 0 1 4 5 8 9 c d
```

Firmware additionally writes channel 2 to hard left (`0x21`) and channel
`0xa` to hard right (`0xaf`).  Channels 3, 6, 7, `0xb`, `0xe`, and `0xf` are
only touched by initialization.

K4r firmware never accesses `0xec00`, consistent with the rack unit's
different output arrangement.

## Revision cross-check

The K002 and K008 bus protocols, offsets, and selector-page sequences were
cross-checked against all six dumped code ROMs:

```text
K4   1.0, 1.3, 1.4
K4r  1.2, 1.3, 1.4
```

The K004 command sequence occurs in all three keyboard K4 revisions and in no
K4r revision.  Differences between revisions do not introduce any additional
CPU-visible register or internal selector page.
