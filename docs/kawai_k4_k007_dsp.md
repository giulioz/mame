# Kawai K4 K007-FP effects DSP

The K007-FP in the keyboard K4 is a programmable effects DSP.  The main CPU
uploads one of 16 two-plane programs, patches algorithm-specific words into
it, then rewrites selected program words for the three effect parameters and
the submix send levels.  The parameters are therefore not three conventional
hardware registers: they are inputs to a firmware-side microcode compiler.

This analysis covers all CPU-visible ports and the complete upload data
format.  It does not yet decode the K007 instruction set or identify the
arithmetic meaning of every 16-bit word.

## CPU-visible register map

The ports occupy `0xf000-0xf00f` in the keyboard K4.  K4r firmware does not
access this range.

```text
address  access  firmware-visible function
f000     W       commit latch word 0 to program plane 0 at written address
f001     W       commit latch word 0 to program plane 1 at written address
f002     W       commit both zeroed data latches to auxiliary slot 0-15
f003-07  --      never accessed
f008     W       data latch word 0, high byte
f009     W       data latch word 0, low byte
f00a     W       data latch word 1, high byte
f00b     W       data latch word 1, low byte
f00c-0d  --      never accessed
f00e     W       reset; firmware writes 00
f00f     R/W     ready/status and DSP mode control
```

The byte order is confirmed by the send-level writer: it puts `ff` in `f008`
and a table value in `f009`, producing a sign-extended `ffxx` program word.
Microcode words in the code ROM use the same big-endian order.

`f002` is used only during reset.  The firmware clears both 16-bit latches and
commits that zero value to addresses 0 through 15.  This establishes a
16-entry auxiliary RAM or register bank, but the firmware gives no further
evidence for its internal purpose.  Word 1 (`f00a/f00b`) is otherwise never
written with nonzero data.

### Status and mode control at `f00f`

```text
bit  read/write behavior seen by firmware
0    read: 1 means ready for the next commit
     write: program/load mode enable
1    run enable, set only after the complete upload
2    short program mode: 128 words/plane instead of 256
7    compound/two-effect mode
```

The firmware never assigns a meaning to bits 3-6.  Its four mode bytes are
`00`, `04`, `80`, and `84`; during upload it writes `mode | 01`, and to start
the DSP it writes `mode` followed by `mode | 02` after a short delay.

## Upload sequence

The complete sequence at firmware routines `7bf9-7fc9` is:

1. Wait for `f00f.0`, write `00` to `f00e`, write `01` to `f00f`, clear
   `f008-f00b`, and commit zero to all 16 `f002` slots.
2. Select code-ROM bank 3 and read the effect directory.
3. Write `mode | 01` to `f00f`.
4. Upload the base program, alternating plane 0 and plane 1 at each 8-bit
   address.  Every word write polls `f00f.0` before its commit.
5. Apply the terminated sparse-patch list.
6. Compile effect parameters 1 and 2 into program words.  Compile parameter 3
   here only for a non-compound effect.
7. Compile the eight submix send values into plane-1 words.
8. Write `mode`, delay, then write `mode | 02` to run.

For a 256-word program the base image is laid out as 512 bytes for plane 0
followed by 512 bytes for plane 1.  A 128-word program uses two 256-byte
planes.  Although upload writes are interleaved, the ROM storage is not.

## Bank-3 effect directory

Firmware bank 3 maps virtual `8000-bfff` to physical code-ROM offsets
`c000-ffff`.

```text
8000 + effect              u8 mode byte
8011 + effect * 4 + 0      little-endian u16 base-program pointer
8011 + effect * 4 + 2      little-endian u16 sparse-patch pointer
8055 + effect * 6 + p * 2  little-endian u16 destination-list pointer
80bb + effect * 6 + p * 2  little-endian u16 coefficient-matrix pointer
```

Here `effect` is 0-15 and `p` is 0-2.  Directory pointers are little-endian
CPU addresses; the 16-bit DSP words at those pointers are big-endian.

```text
no  mode  words  base  sparse  coefficient matrices       effect
 0   00    256   9b24   81b6   84b4 95a4 8674             Reverb 1
 1   00    256   9b24   81b8   87f4 9704 8674             Reverb 2
 2   00    256   9b24   8220   8a14 9864 8674             Reverb 3
 3   00    256   9b24   8284   8c34 99c4 8674             Reverb 4
 4   00    256   9f24   81b6   8e64 8e74 8ee4             Gate Reverb
 5   00    256   9f24   82e8   8e64 8f64 8ee4             Reverse Gate
 6   04    128   a324   81b6   8fd4 8fe4 9044             Normal Delay
 7   04    128   a524   81b6   9084 90a4 90b4             Stereo Panpot Delay
 8   04    128   a724   81b6   9134 9164 9184             Chorus
 9   80    256   a924   81b6   9204 92c4 92e4             Overdrive + Flanger
10   84    128   ad24   81b6   93a4 9464 92e4             Overdrive + Normal Delay
11   80    256   af24   81b6   9204 9474 92e4             Overdrive + Reverb
12   84    128   b324   81b6   94c4 94d4 92e4             Normal Delay + Normal Delay
13   84    128   b524   81b6   94e4 94f4 92e4             Normal Delay + Stereo Panpot Delay
14   84    128   b724   81b6   9514 9574 92e4             Chorus + Normal Delay
15   84    128   b924   81b6   9514 9584 92e4             Chorus + Stereo Panpot Delay
```

Reverbs 1-4 share one base image and become distinct through sparse patches
and different parameter matrices.  Gate and Reverse Gate likewise share a
base image.  The remaining effects have dedicated bases.

### Sparse-patch format

```text
repeat:
    u8 plane             bit 0 selects plane 0 or 1
    u8 address
    u8 word_high
    u8 word_low
until plane == ff
```

Only Reverb 2, Reverb 3, Reverb 4, and Reverse Gate have nonempty lists.  They
contain 25, 24, 24, and 11 writes respectively.  The decoder utility prints
every patched address and word.

## Effect parameter compiler

The effect patch holds:

```text
D429  effect type, 0-15
D42a  parameter 1, 0-7
D42b  parameter 2, 0-7
D42c  parameter 3, 0-31
D433  submix 0 pan
D434  submix 0 send 1
D435  submix 0 send 2
      then seven more pan/send1/send2 records at a stride of 3
```

A parameter destination list has this format:

```text
u8 destination_count
repeat destination_count times:
    u8 plane
    u8 address
u8 ff
```

For a list of `N` destinations and user value `V`, the firmware reads `N`
big-endian words starting at:

```text
coefficient_matrix[effect][parameter] + V * N * 2
```

It then writes one word to each listed plane/address pair.  These matrices
are nonlinear and parameter-specific.  They can change instruction fields,
coefficients, delay offsets, or multiple mutually dependent words in one
operation; treating the raw user value as a DSP register would be incorrect.

The complete destination map follows.  `0:26` means plane 0, address `26`.

```text
effect  parameter 1 destinations
0-3     0:26 0:28 0:2a 0:2c 0:30 0:32
4-5     0:38
6       1:2d
7       1:15 1:1f
8       1:27 1:29 1:2b
9       1:43 1:49 1:4f 1:55 1:5b 1:5f 1:65 1:6f 1:71 1:73 1:75 1:79
10      1:3d 1:41 1:45 1:49 1:4d 1:51 1:59 1:5b 1:5d 1:5f 1:61 1:63
11      same as effect 9
12-13   0:28
14-15   1:27 1:2d 0:2d 1:39 1:3b 1:3d

effect  parameter 2 destinations
0-3     1:45 1:47 1:49 1:4d 1:4f 1:51 1:5d 1:5f 1:61 1:6d 1:6f
        1:71 1:93 1:95 1:97 1:a3 1:a5 1:a7 1:d5 1:d7 1:d9 1:db
4-5     1:47 1:49 1:4b 1:4d 1:4f 1:51 1:53
6       1:13 1:1b 1:1d 1:1f 1:21 1:23
7       0:1a
8       1:15 0:19
9       1:ad 1:b3
10      0:72
11      1:cf 1:d1 1:d3 1:d7 1:d9
12      0:30
13      0:30 0:7a
14      0:72
15      0:72 0:7a

effect  parameter 3 destinations
0-3     1:df 1:e7 1:e9 1:eb 1:ed 1:ef
4-5     1:cd 1:cf
6       0:2e
7       0:16 0:20
8       1:1b 0:1b
9,11    1:a7 1:f5 1:f7
10      1:6f 1:75 1:77
12-13   1:2b 1:75 1:77
14-15   1:71 1:75 1:77
```

The front-panel meanings are:

```text
0-5   Pre-Delay, Time, Tone
6     Feedback, Tone, Delay
7     Feedback, L/R Delay, Delay
8     Width, Feedback, Rate
9     Drive, Flanger Type, Balance
10    Drive, Delay Time, Balance
11    Drive, Reverb Type, Balance
12    Delay 1, Delay 2, Balance
13    Delay 1, Delay 2, Balance
14    Chorus, Delay, Balance
15    Chorus, Delay, Balance
```

### Compound-effect Balance

For effects 9-15, parameter 3 is not written during the first static pass.
The firmware uses a state machine to move an internal value toward the new
Balance setting and recompiles the parameter-3 destinations one step at a
time.  All compound Balance matrices point to the shared table at `92e4`,
although their destination addresses differ.

During effect changes the same state machine also writes sign-extended ramp
values to fixed plane-1 program locations, apparently to fade the active
topology and avoid discontinuities:

```text
effects 0-3           address f1
effects 4-5, 9, 11    addresses f9 and fb
effects 6-8, 10,12-15 addresses 79 and 7b
```

This behavior means a K007 emulation must allow program RAM writes while the
DSP is running; loading the program once and making it immutable would miss
Balance changes and transition ramps.

## Submix sends

Each send value 0-100 indexes the byte table at `8151`.  Zero becomes word
`0000`; a nonzero result `xx` becomes `ffxx`.  The word is committed to plane
1.  Depending on the mode, the eight submix values are written to these
address vectors:

```text
A: 01 09 03 0b 05 0d 07 0f
B: 81 89 83 8b 85 8d 87 8f
C: 01 11 05 15 09 19 0d 1d
D: 81 91 85 95 89 99 8d 9d
E: 03 13 07 17 0b 1b 0f 1f
F: 83 93 87 97 8b 9b 8f 9f
```

```text
mode 00  send 1 -> A and B; send 2 unused
mode 04  send 1 -> A;       send 2 unused
mode 80  send 1 -> C and D; send 2 -> E and F
mode 84  send 1 -> C;       send 2 -> E
```

Submix pan is not a K007 parameter.  The firmware sends it separately to the
K004-FP pan-pot chip as documented in `kawai_k4_custom_chip_registers.md`.

## Decoder and upload-image generator

`scripts/k4_k007_microcode.py` decodes the directory and can materialize the
two program planes after sparse and parameter overlays:

```sh
python3 scripts/k4_k007_microcode.py k4_v14.u8
python3 scripts/k4_k007_microcode.py k4_v14.u8 --algorithm 9
python3 scripts/k4_k007_microcode.py k4_v14.u8 --algorithm 9 \
    --param1 7 --param2 3 --param3 16 --output-dir /tmp/k007
```

The output plane files contain raw big-endian words in address order.  The
JSON manifest records every applied sparse or parameter write.

## Firmware revision check

The upload protocol, directory, base programs, sparse patches, and
destination lists match K4 firmware 1.0, 1.3, and 1.4.  Versions 1.3 and 1.4
also have identical parameter matrices.  Version 1.0 has different Time
matrix rows for Reverb 1-4; the decoder deliberately reads the selected ROM
rather than hard-coding the later curves.

