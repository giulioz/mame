# D-50 MB87126 parameter RAM and chorus link

This note separates what is established by the D-50 firmware, DEP-5 wire
captures and firmware, the D-50 schematic, and the independently recovered
`d50lib` signal graph from what still requires an optical dump of the
MB87126-006 internal ROM.

The corresponding datapath and per-microcycle reconstruction is documented in
[`roland_d50_mb87126_execution.md`](roland_d50_mb87126_execution.md).

## Physical organization

The confirmed data-word format and observed address windows are:

```text
internal program ROM: 35 bits x 192 slots (00-BF)
parameter RAM word:    30 bits
observed DEP-5 window: 160 slots (00-9F)
physical RAM depth:    unresolved (160, 192, or 256 remain possible)

parameter word:
    bits  0-15  external delay-memory relative offset
    bits 16-27  signed 12-bit multiplier
    bits 28-29  multiplier range/shift
```

The parameter and program stores use a common slot/microcycle address.  The
ROM word supplies the operation, accumulator and I/O routing, and memory
read/write behavior.  The writable word supplies only the relative delay
operand and the multiplier/shift operand.  Consequently a parameter sweep can
identify which high-level compiler owns a word, but cannot by itself reveal a
slot's opcode or store destination.

The related DEP-5 sends 40-bit frames.  They divide exactly into an 8-bit slot
address, a 30-bit parameter word, and two framing/parity bits.  Direct decoding
of `dep5/poweron.dwf3logic` at its recorded 20 MHz sample rate confirms rising-
edge, LSB-first transfers.  Ordinary wire-byte-0 values cover `60-FF`; after
the active-low address inversion these cover every selector `00-9F`.  There
are 560 normal 40-clock transactions, plus exceptional initialization
sequences.

This proves that 160 locations are addressed and initialized.  It does **not**
prove that only 160 cells exist on silicon.  The wire address is eight bits,
so a 256-word physical decoder remains possible; a 192-word RAM matching the
program-ROM depth is also possible.  Locations `A0-FF` have not been exercised
by the available captures.  Settling physical depth requires counting the RAM
array on the die or an electrical alias/behavior test on real hardware.

The D-50's IC28 host window is narrower than the physical word:

- firmware uploads selectors `01-7F`;
- `F801/F802.5:0` expose only offset bits 0-13;
- `F803/F804.5:0` expose the 12-bit coefficient and two shift bits;
- `F804.6` is framing/load and `F804.7` is parity.

The missing offset bits 14-15 are zero on the D-50 because its reverb delay
memory only needs the lower 14 address bits.  Thus the CPU-visible payload is
14+14 bits even though the MB87126 parameter word is physically 16+14 bits.

## What the firmware sweep establishes

`scripts/tests/d50_mb87126_sweep.lua` changes one live edit byte, waits for the
firmware compiler and asynchronous uploaders, and diffs:

- both CPU shadows at `DC00-DCFF` and `DD00-DDFF`;
- actual `F800-F804` transfers;
- indirect IC9 writes at `F000/F001/F007`;
- IC8 host latches at `E700-E707`.

Run a representative sweep with:

```sh
./mame d50 -rompath . \
  -autoboot_script scripts/tests/d50_mb87126_sweep.lua \
  -nothrottle -video none -sound none -seconds_to_run 90 -skip_gameinfo
```

Set `D50_MB87126_SWEEP=full` for every legal EQ row, all 20 key/output
combinations, all 16 ROM reverbs, and complete 0-100 mixer curves.  Set
`D50_MB87126_SWEEP=routing` for only the key/output matrix.  Set
`D50_MB87126_DUMP=1` to print all 127 retained D-50-visible words.

All 127 DC words contain operands in the retained boot image.  Reverb-type
changes alter exactly 39 of them; the other 88 are fixed topology/state
offsets for the selected internal ROM program.  We know their values, but the
internal ROM is needed to name their read/write operations safely.

Of the 127 DD words, 121 have a user-facing compiler owner.  The ownership
sets are disjoint:

| Function | Count | Selectors |
|---|---:|---|
| Reverb graph | 55 | See the semantic table below |
| Upper and lower EQ | 44 | `14 15 17 18 1A 1B 1C 1E 22 23 24 25 26 27 28 29 2A 2B 2C 2E 32 33 59 5C 5D 62 63 64 65 66 67 68 69 6A 6B 6C 6D 6E 6F 72 73 78 7B 7C` |
| Volume, balance, chorus/output matrix | 22 | `01 10 1F 20 21 30 34 35 36 39 41 50 5F 60 61 70 74 75 76 79 7E 7F` |
| Static in all current sweeps | 6 | `11 31 37 51 71 77` |

The last six are not necessarily unused.  In the retained factory-patch image,
`11/31/51/71` are zero and `37/77` are equal fixed companions to the adjacent
reverb-routing words.  No legal EQ, chorus, key/output, reverb, volume, or
balance edit changed them.

### Non-reverb DD groups

| Parameter | DD selectors |
|---|---|
| Upper LF frequency | `14 15 17 18 1A 1B 1C 1E` |
| Upper LF gain | `22 23` |
| Upper HF frequency or Q | `24 25 26 27 28 29 2A 2B 2C 2E` |
| Upper HF gain | `32 33` |
| Lower LF frequency | `59 5C 5D 62 63 64 65 66` |
| Lower LF gain | `68 69` |
| Lower HF frequency or Q | `67 6A 6B 6C 6D 6E 6F 72 73 78` |
| Lower HF gain | `7B 7C` |
| Total volume | `01 39 41 79` |
| Tone balance | `10 30 50 70` |
| Upper chorus balance | `1F 5F`, plus IC9 chorus coefficients |
| Lower chorus balance | `20 60`, plus IC9 chorus coefficients |
| Reverb balance | wet `34/75`, direct companions `36/76` |

For key modes 1-3, output mode 1 changes the core matrix
`1F 20 21 5F 60 61`.  Output mode 2 also changes
`34 35 75 7E 7F`; output mode 3 instead changes
`34 74 75 7E 7F`.  Output mode 0 is the baseline.  Key modes 0 and 4
also alias/recompile a complete tone and EQ side, so those traces must not be
misread as a pure output-matrix update.

## Reverb DC selector semantics

These mappings combine the firmware scatter order with `d50lib`'s recovered
39-tap graph.  “Read” and “write” name the use in that graph, not an opcode
decoded from the still-undumped internal ROM.

| Selector | Graph tap | Recovered use |
|---|---:|---|
| `01` | 1 | chain-2 read |
| `02` | 11 | chain-1 initial feedback read |
| `03` | 3 | chain-1 stage-1 read |
| `04` | 20 | wet R tap 4 |
| `05` | 0 | chain-2 write |
| `06` | 5 | chain-1 stage-2 read |
| `07` | 15 | wet L tap 2 |
| `09` | 2 | chain-1 stage-1 write |
| `0A` | 7 | chain-1 stage-3 read |
| `0B` | 19 | wet R tap 3 |
| `0C` | 4 | chain-1 stage-2 write |
| `0E` | 9 | chain-1 stage-4 read |
| `10` | 6 | chain-1 stage-3 write |
| `15` | 8 | chain-1 stage-4 write |
| `17` | 14 | wet L tap 1 |
| `1B` | 10 | chain-2 mixed write |
| `26` | 18 | wet R tap 2 |
| `28` | 13 | wet R tap 0 |
| `36` | 17 | wet R tap 1 |
| `37` | 12 | wet L tap 0 |
| `38` | 34 | branch-4 input / wet R tap |
| `39` | 28 | wet R tap 5 |
| `3A` | 25 | wet L tap 5 |
| `3B` | 31 | wet L tap 6 |
| `3D` | 23 | branch-0 input / wet L tap |
| `41` | 26 | branch-1 input / wet R tap |
| `42` | 29 | branch-2 input / wet L tap |
| `44` | 32 | branch-3 input / wet R tap |
| `48` | 21 | feedback branch 0 write |
| `4A` | 24 | feedback branch 1 write |
| `4B` | 35 | branch-4 input / wet L tap |
| `4E` | 38 | branch-5 input / wet R tap |
| `51` | 27 | feedback branch 2 write |
| `52` | 22 | wet L tap 4 |
| `54` | 30 | feedback branch 3 write |
| `59` | 33 | feedback branch 4 write |
| `60` | 36 | feedback branch 5 write |
| `77` | 37 | wet R tap 6 |
| `78` | 16 | wet L tap 3 |

## Reverb DD selector semantics

| Selector | Graph coefficient | Recovered use |
|---|---:|---|
| `02` | 2 | chain-2 input |
| `03` | 6 | chain-2 feedback |
| `04` | 0 | chain-1 input |
| `05` | 1 | chain-1 first feedback |
| `06` | 7 | chain-2 feed-forward |
| `07` | 8 | chain-1 stage-1 feedback |
| `08` | 10 | chain-1 stage-2 feedback |
| `09` | 5 | chain-2 filtered mix |
| `0A` | 9 | chain-1 stage-1 feed-forward |
| `0B` | 24 | wet R tap 4 gain |
| `0C` | 19 | wet L tap 2 gain |
| `0D` | 11 | chain-1 stage-2 feed-forward |
| `0E` | 12 | chain-1 stage-3 feedback |
| `0F` | 23 | wet R tap 3 gain |
| `12` | 13 | chain-1 stage-3 feed-forward |
| `13` | 14 | chain-1 stage-4 feedback |
| `16` | 15 | chain-1 stage-4 feed-forward |
| `19` | 4 | chain-2 tap-A mix |
| `1D` | 18 | wet L tap 1 gain |
| `2D` | 22 | wet R tap 2 gain |
| `2F` | 17 | wet R tap 0 gain |
| `38` | 21 | wet R tap 1 gain |
| `3A` | 16 | wet L tap 0 gain |
| `3B` | 33 | wet R branch-4 tap gain |
| `3C` | 29 | wet R tap 5 gain |
| `3D` | 27 | not referenced by the recovered `d50lib` sample loop |
| `3E` | 31 | wet L shared taps 4/6 gain |
| `3F` | 3 | tap-A common injection |
| `40` | 26 | wet L tap 5 gain |
| `42` | 37 | branch-0 input |
| `43` | 38 | branch-0 state feedback |
| `44` | 41 | not referenced by the recovered `d50lib` sample loop |
| `45` | 40 | branch-1 input |
| `46` | 39 | branch-0 output |
| `47` | 28 | wet R branch-1 tap gain |
| `48` | 42 | branch-1 output |
| `49` | 43 | branch-2 input |
| `4A` | 44 | not referenced by the recovered `d50lib` sample loop |
| `4B` | 30 | wet L branch-2 tap gain |
| `4C` | 32 | wet R branch-3 tap gain |
| `4D` | 46 | branch-3 input |
| `4E` | 47 | branch-3 state feedback |
| `4F` | 45 | branch-2 output |
| `52` | 48 | branch-3 output |
| `53` | 50 | not referenced by the recovered `d50lib` sample loop |
| `54` | 49 | branch-4 input |
| `55` | 34 | wet L branch-4 tap gain |
| `56` | 36 | wet R branch-5 tap gain |
| `57` | 51 | branch-4 output |
| `58` | 25 | wet L tap 4 gain |
| `5A` | 52 | branch-5 input |
| `5B` | 53 | branch-5 state feedback |
| `5E` | 54 | branch-5 output |
| `7A` | 35 | wet R tap 6 gain |
| `7D` | 20 | wet L tap 3 gain |

The four “not referenced” coefficients are still uploaded by real firmware and
must remain in the emulated parameter image.  That label means only that the
independent high-level implementation does not consume them; the MB87126 ROM
may.

## Relationship to the chorus chip

The service schematic resolves the sample-data direction.  IC9
(MB87126-006) drives an eight-line `DC0-DC7` digital stream into IC8
(MB87137-001).  IC8 uses its own 8 KiB SRAM, performs the chorus/output work,
and drives another `DC0-DC7` bus into the PCM54 DAC.  No corresponding
eight-bit sample-return bus from IC8 to IC9 is visible.  The safe model is
therefore:

```text
LA32 streams -> MB87126 EQ/reverb/mix -> DC0-DC7 -> MB87137 chorus/output
             -> IC8 SRAM -> DC0-DC7 -> PCM54 DAC and stereo sample/hold
```

The control path is subtler.  Chorus type/rate/depth sweeps change the IC9
indirect register ranges `80-BF` and `D4-DF`, but change no retained MB87126
DC/DD word.  Chorus balance also changes two MB87126 output-matrix DD words per
tone.  None of these legal edits changes the four observed IC8 CPU latches.
This means IC9 is involved in delivering the dynamic chorus program/data to
IC8 even though IC8 owns the chorus delay SRAM; modeling `F000-F007` as a
direct IC8 register port would not match the board or firmware.

## Remaining hard limit

Sweeping can recover ownership, value curves, and the high-level graph.  It
cannot identify the fixed opcode paired with each parameter location or prove
the physical depth of the array.
Completing a literal MB87126 core requires an optical decode of the 35-bit x
192 internal ROM (or a sufficiently complete silicon netlist).  Until then,
the current graph-level implementation can be exact for the recovered reverb
and EQ recurrences while the per-slot ROM operation remains intentionally
unnamed.
