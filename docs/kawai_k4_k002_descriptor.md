# Kawai K4 K002-FP waveform descriptors

This documents the PCM waveform directory used by the K4 and K4r firmware.
The extractor produces an identical 268-row directory for all six dumped ROMs:
K4 versions 1.0, 1.3, and 1.4 and K4r versions 1.2, 1.3, and 1.4.

## Directory

The 96 DC waves (patch wave indices 0-95) use a separate K002 cyclic-wave
setup path.  PCM wave indices 96-255 use a 160-entry pointer table at code-ROM
address `0xbbda`.  Each pointer addresses this variable-length record:

```text
u8 flags
u8 root_or_upper_key[]
u8 terminator = 0xff
u8 descriptor[count][6]
```

There is one descriptor for every key byte before the terminator.  The
firmware chooses the first key at or above the played key, making the byte both
the sample's root pitch and the upper edge used by the zone-selection search.

Across all 160 PCM waves, the table contains 268 key-zone descriptors: 133 use
the paired 16-bit ROMs and 135 use the 8-bit ROM.

## Six-byte descriptor

```text
offset  size  meaning
0       2     little-endian start word, address in units of 16 samples
2       2     little-endian loop address bits A0-A15
4       2     little-endian end address bits A0-A15
```

Every physical wave ROM has a 19-bit, 512 KiB address space.  Decode the start
address as:

```text
start = (start_word << 4) & 0x7ffff
```

The loop and end words omit A16-A18.  For a forward waveform, inherit those
bits from the preceding address and add `0x10000` if the result would move
backwards:

```text
loop = (start & ~0xffff) | loop_word
if loop < start: loop += 0x10000

end = (loop & ~0xffff) | end_word
if end < loop: end += 0x10000
```

For a reverse waveform, perform the inverse operation, subtracting `0x10000`
when the result would move forwards.  These rules produce valid ordered
addresses in `0x00000-0x7ffff` for all 268 descriptors, including every
forward and reverse wave.

For example, PCM wave index 96 (displayed as wave 97, Kick) has descriptor
`00 41 f0 1f ff 1f` and flags `0x48`:

```text
start = 0x41000
loop  = 0x41ff0
end   = 0x41fff
```

The addresses are inclusive.  Its final 16 samples form the terminal loop.
This is also how most named one-shot waves are represented: no separate
one-shot bit has been identified; the K002 repeats a short terminal pad
instead.

## Flags and physical ROMs

Only bits 1, 3, 4, and 6 occur in the PCM directory:

```text
bit 1  suspected special loop mode in five long percussion variants; exact behavior unknown
bit 3  select the 8-bit PCM ROM rather than the paired 16-bit ROMs
bit 4  reverse address direction
bit 6  root-key/pitch-domain extension used by the firmware pitch calculation
```

The service-manual designators and the current MAME wave region correspond as
follows:

```text
P202 / K4 U30 / K4r U40  8-bit PCM             region +0x000000
P203 / K4 U29 / K4r U39  16-bit sample MSB     region +0x080000
P204 / K4 U31 / K4r U38  16-bit sample LSB     region +0x100000
```

For an 8-bit descriptor, its decoded address is the byte offset in P202.  For
a 16-bit descriptor, the same decoded sample index addresses P203 and P204 in
parallel.  The 16-bit sample word is assembled from P203 as the high byte and
P204 as the low byte.

## Extractor

Run the extractor against a K4 code ROM to list every PCM key zone and its
logical and MAME-region addresses:

```sh
python3 scripts/k4_k002_descriptors.py k4_v14.u8 > k4-k002.csv
```

Use `--wave 139` to select a single zero-based patch wave index.  The CSV also
reports the one-based wave number printed in the K4 documentation.

The firmware's separate three-byte DC-wave table begins at `0xb9ba`.  It does
not contain the six-byte start/loop/end format, so those 96 cyclic waves should
not be passed through this decoder.

## WAV export

Once P202, P203, and P204 have been dumped, export all 268 PCM key zones with:

```sh
python3 scripts/k4_dump_samples.py \
    k4_v14.u8 p202.bin p203.bin p204.bin output/k4-samples
```

The exporter emits signed, mono, 16-bit PCM at 32 kHz.  P202's signed 8-bit
samples are promoted to 16-bit, while P203 and P204 are combined as MSB and
LSB.  Reverse descriptors are written in playback order.  Every WAV contains
an inclusive RIFF `smpl` loop and its descriptor, format, direction, root key,
and loop positions are recorded in `manifest.csv`.
