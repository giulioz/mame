# MB87126 reconstructed execution model

This is a constrained execution model for the MB87126-006, not a claimed
decode of its 35-bit mask ROM.  It combines the D-50 schematic, the DEP-5
wire captures, the D-50 firmware's parameter compilers, the visible die
blocks, and the independently recovered `d50lib` signal graph.  It is intended
to be precise enough to implement and test a cycle interpreter while keeping
the remaining unknown control-ROM bits explicit.

## Facts fixed by independent evidence

### Clock and external datapath

The D-50 service schematic shows:

- a 32.768 MHz crystal at the LA32;
- the LA32's `CK16M` output passing through a 74HC00 gate to IC9 `MSCK`;
- a 24-bit IC9 delay-memory word bus, `DR0-DR23`;
- an eight-bit multiplexed DRAM address bus, `DA0-DA7`;
- six uPD41416C-12 16K x 4 DRAMs, forming 16K x 24 bits;
- IC9 `RAS`, `CAS`, and `WE` outputs;
- an eight-bit `DC0-DC7` stream from IC9 to the MB87137 chorus chip.

The external delay memory is therefore 16384 signed 24-bit words, not an
8-bit companded memory.  This agrees directly with the 24-bit input side of
the multiplier visible on the die.  The D-50 forces parameter offset bits
15:14 low because only fourteen address bits are useful on this board.

At 32 kHz there are 512 `MSCK` periods per sample.  An especially plausible
timing model is a two-phase internal microcycle, giving 256 microcycles per
sample.  In that model the 192 ROM rows occupy slots `00-BF`, while `C0-FF`
are idle, refresh, fixed I/O, or hardwired service phases.  The die's eight-bit
counter and the 192-row ROM fit this model well.  It remains an inference:
the alternative is 192 variable-length operations scheduled inside the same
512-clock frame.  The interpreter should initially expose both the 16.384 MHz
master phase and the abstract slot counter rather than baking in one answer.

### Program and parameter stores

The established stores are:

```text
control ROM:       192 x 35 bits, slots 00-BF
parameter word:           30 bits
    DC:             16-bit relative DRAM offset
    DD[11:0]:       signed 12-bit multiplier
    DD[13:12]:      multiplier range
observed DEP-5 use: slots 00-9F
physical RAM depth: unresolved (160, 192, or 256)
```

The program and parameter memories share a slot address.  A control word
chooses buses, accumulator operations, DRAM reads/writes and state writeback;
the parameter word supplies one address operand and one multiplier operand.
The same slot can use both lanes concurrently for unrelated graph nodes.

The D-50 has another important alignment: its indirect IC9 banks `80-BF`
are exactly the upper 64 program-ROM addresses.  Their values are 12-bit
operands with instruction-dependent meanings: rates, depths, coefficients,
and delay positions.  Together, IC28's dual-lane `00-7F` image and the indirect
`80-BF` image cover all 192 ROM slots.  This strongly suggests that the -006
variant exposes an upper microprogram operand plane through its parallel host
port.  It is not yet proven that these words occupy the same physical 30-bit
array, so the emulator should retain a distinct upper-operand file until a
D-50 pin capture or die trace resolves the write path.

## Arithmetic model

Let `x` be a signed 24-bit datapath value and `m` the sign-extended 12-bit DD
mantissa.  The four observed range codes represent exponents
`{-4, 0, +2, +4}`.  The high-level value is exactly:

```text
gain = m / 2048 * 2^exponent
```

The corresponding integer multiplier path is:

```text
p36  = signed24(x) * signed12(m)
term = p36 >> {15, 11, 9, 7}[range]
```

before the still-unknown rounding rule.  The maximum range adds four bits of
headroom to a 24-bit sample, which explains the visible 28-bit adder/accumulator
particularly well.  A useful first implementation is therefore:

```text
24 x 12 signed multiplier -> 36-bit product
range-controlled alignment -> signed 28-bit term
28-bit add/sub/load accumulator -> 24-bit rounded or saturated writeback
```

The following details must remain selectable until measurements settle them:

- arithmetic shift truncation versus round-to-nearest;
- 28-bit wrap versus saturation;
- the exact 28-to-24 writeback slice;
- whether DRAM and output writeback saturate independently;
- whether the low-level sample encoding is two's complement or offset binary
  at each external interface.  The internal arithmetic is almost certainly
  two's complement.

## Address generator

The 16-bit adder visible by the DRAM interface naturally computes a moving
base plus a parameter offset:

```text
effective = (delay_base - DC) & installed_memory_mask
D-50 mask = 0x3fff
```

The sign is chosen here to match the recovered initialization
`tap = 0x4000 - DC`.  Incrementing the base once per sample is equivalent to
decrementing all 39 software tap pointers once per sample, which is what
`d50lib` does.  Hardware therefore needs one circular base counter rather than
39 counters.  Reversing both the base direction and the offset sign produces
the same graph and remains possible until an address-bus trace is available.

`DA0-DA7` carries row and column halves of this address under `RAS` and `CAS`.
The DRAM pipeline must consequently distinguish address issue, data capture,
and writeback phases even when the symbolic graph presents a single tap read.

## Why each slot is a parallel microinstruction

The recovered selector maps rule out a scalar interpretation where DC and DD
must describe the same operation.  The opening slots illustrate it:

| Slot | DC/address lane | DD/multiplier lane |
|---:|---|---|
| `01` | read chain-2 tap 1 | output-volume operation |
| `02` | read chain-1 tap 11 | `c2 * input` for chain 2 |
| `03` | read chain-1 tap 3 | `c6 * tap1` |
| `04` | read wet-right tap 20 | `c0 * input` for chain 1 |
| `05` | write chain-2 tap 0 | `c1 * tap11` |
| `06` | read chain-1 tap 5 | `c7 * chain2` |
| `07` | read wet-left tap 15 | `c8 * tap3` |
| `08` | no reverb-variable address | `c10 * tap5` |
| `09` | write chain-1 tap 2 | `c5 * chain2` |
| `0A` | read chain-1 tap 7 | `c9 * chain1` |
| `0B` | read wet-right tap 19 | `c24 * tap20` |
| `0C` | write chain-1 tap 4 | `c19 * tap15` |
| `0D` | no reverb-variable address | `c11 * chain1` |
| `0E` | read chain-1 tap 9 | `c12 * tap7` |
| `0F` | no reverb-variable address | `c23 * tap19` |
| `10` | write chain-1 tap 6 | tone/output matrix operation |

For example, the address lane issues the tap-11 read at slot `02`, while its
coefficient is consumed at slot `05`.  At slot `05` the address lane is already
issuing a chain-2 write.  Other read-to-MAC separations vary because the ROM
interleaves chain, wet-mix, EQ, and output work.  This is the expected shape of
a scheduled DSP pipeline, not an inconsistency in the selector recovery.

The complete DC and DD semantic maps are in
`roland_d50_mb87126.md`.  They should be consumed as independent constraints
on the address and MAC lanes of each control-ROM row.

## Abstract per-microcycle execution

A first cycle core can execute this sequence without knowing the physical ROM
bit positions:

```text
1. Fetch control[slot], parameter[slot], and upper_operand[slot if 80-BF].
2. Address lane:
     select base/offset source;
     form the 16-bit effective address;
     drive a DRAM row/column read, write, or refresh phase.
3. Input lane:
     latch the returning 24-bit DRAM word, an LA32 input word,
     an accumulator/register value, zero, or an immediate constant.
4. Multiply lane:
     signed 24 x 12 multiply using DD[11:0];
     align the product using DD[13:12].
5. ALU lane:
     load/add/subtract the aligned product, the DRAM latch, or a state latch
     into a 28-bit accumulator.
6. Writeback lane:
     update an accumulator/state latch, queue a 24-bit DRAM write,
     or serialize/route a result toward DC0-DC7.
7. Advance the slot/phase counters.  At sample boundary, advance the circular
   delay base and latch the next audio inputs.
```

The actual implementation needs pipeline latches between these conceptual
stages.  A conservative state set is:

```text
master_phase, slot, delay_base, pending_dram_address, pending_dram_mode
dram_read_latch[24], multiplier_x[24], product[36], aligned_term[28]
accumulator[28], one or more 28-bit temporary/state latches
input latches, output serializer, sync/load state
```

The six feedback scalars in the recovered graph may be dedicated state
latches, accumulator spill registers, or fixed-address DRAM cells.  They must
not be assigned to a new physical RAM block merely because the high-level
software stores them as six doubles.

## Candidate 35-bit control functions

It is premature to assign bit positions, but every reconstructed row needs
control for these functions:

- address source and add/subtract selection;
- DRAM row/column/read/write/refresh sequencing;
- 24-bit multiplier-input source;
- multiplier enable or bypass;
- 28-bit ALU source and load/add/subtract/clear operation;
- accumulator or state-latch source/destination selection;
- 24-bit writeback slice and DRAM-write enable;
- input/output stream latch or serializer control;
- slot/frame counter reset, hold, or conditional service behavior.

The two parameter range bits already control product alignment, so those do
not need ROM bits.  The small eight-bit adder visible on the die is most
plausibly associated with the slot/phase counter or with `DA0-DA7` row/column
sequencing.  Treating it as an audio adder is inconsistent with the 24/28-bit
signal path.

## Reconstructed-program backend

Before the optical ROM is decoded, an emulator can use a symbolic 192-row
control table derived from the known graph:

- rows carry independent address-lane and MAC-lane micro-operations;
- the 39 DC selectors constrain all variable delay accesses;
- the 55 reverb DD selectors constrain reverb MACs;
- the 44 EQ DD selectors constrain both tone EQ recurrences;
- the 22 mixer DD selectors constrain input/output routing;
- indirect `80-BF` operands constrain chorus preparation and transport;
- unknown rows remain explicit `unknown`, not guessed no-ops.

This backend should use the same fixed-point datapath and pipeline latches as a
future literal ROM backend.  Replacing symbolic rows with decoded 35-bit words
then changes only control decoding, not arithmetic, DRAM, timing, or host I/O.

### Preliminary MAME backend

MAME now contains two independent reverb states which run in lockstep:

- `Legacy floating-point graph` is the previous recovered `d50lib`-style
  implementation and remains available as a regression oracle;
- `MB87126 fixed-point reconstruction` is the default and executes the same
  recovered graph through explicit hardware-width primitives.

The fixed-point path currently uses:

```text
audio input and DRAM words: signed 24-bit
multiplier:                 signed 24 x signed 12
product shifts:             15, 11, 9, or 7 bits by DD range
accumulator:                saturating signed 28-bit
DRAM writeback/state:       saturating signed 24-bit
delay addressing:           (base - DC) & 0x3fff
base update:                base = (base - 1) & 0x3fff per sample
negative shift rounding:    arithmetic floor
```

Saturation and arithmetic-floor rounding are testable initial hypotheses, not
optically decoded ROM facts.  Wet/direct output scaling uses Q20 integer
arithmetic; float conversion happens only at the MAME stream boundary.  The
backend does not use the legacy `tanh` safety limiter: its 24-bit writeback and
output saturation bound corrupt or high-feedback images.

Both implementations advance on every sample so the configuration switch is
a phase-aligned A/B comparison.  Select it in MAME's machine configuration as
`Reverb implementation`.  This backend is still a graph-scheduled execution
model; it does not claim that the arithmetic calls have already been assigned
to all 192 physical ROM rows.

## Validation order

1. Run the symbolic core in floating point and compare every sample and delay
   write against the existing recovered graph for an impulse and random input.
2. Switch the same schedule to 24/28-bit fixed point and sweep rounding,
   saturation, and writeback-slice hypotheses.
3. Verify all 16 D-50 reverb images, both EQs, and every output mode.
4. Compare the 16K wrap points and impulse-echo positions with hardware
   recordings or a logic capture of `DA0-DA7/RAS/CAS`.
5. Capture `MSCK`, `SYNC`, DRAM controls, and `DC0-DC7` together to determine
   whether one abstract slot is two master clocks and to locate the `C0-FF`
   service interval.
6. Decode several unmistakable ROM rows optically (clear accumulator, DRAM
   write, output latch) and solve the 35 control columns by correlation.

This produces a useful microinstruction execution model now, while preserving
a clean path to a literal MB87126 core when the mask-ROM bits become available.
