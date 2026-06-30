# Yamaha EX5R ROM analysis

This note records findings from static disassembly of the two reconstructed
2 MiB, big-endian FR20 images and from instrumented MAME boot runs.  Addresses
are CPU addresses unless explicitly described as file offsets.

Primary hardware references are the supplied MB91103 data sheet and service
manual.  Counter bit semantics and quadrature timing were cross-checked against
Infineon/Cypress application note
[AN205315](https://www.infineon.com/assets/row/public/documents/30/42/infineon-an205315-fr-cy91460-up-down-counter-applicationnotes-en.pdf),
which documents the same Fujitsu UDC register fields used by the MB91103.

## Images and reset

| Image | SHA-1 of reconstructed 32-bit image | Reset vector |
|---|---|---|
| Main CPU | `33d16006da5fb4337a848b6023a52075e68263c0` | `002f0288` |
| Sub/TG CPU | `849f3e9748ea53fdada51a304217c0072ce28edd` | `0020027e` |

The reset table is read through the MB91103's fixed `000ffc00-000fffff`
window.  Both programs subsequently use the table at `002ffc00-002fffff`.
The main image contains useful handlers including the external interrupt 4
receiver at `002c9bea`; the sub image has its external interrupt 0 receiver at
`0023cb72`.

The four original byte-lane ROM files are interleaved by `ROM_LOAD32_WORD_SWAP`
in the driver.  Treating either physical ROM as a linear program produces
plausible-looking but incorrect code.

## Confirmed memory maps

### Main CPU

| Range | Chip-select/use | Evidence |
|---|---|---|
| `000800-1fffff`, `200000-3fffff` | Main mask ROM and reset mirror | Reset vectors and continuous code xrefs |
| `0c0000-0cffff` | Panel/ADC working buffer | DMA destination and panel processing through at least `0c7f58` |
| `0e8000-0e8001` | SED1335 LCD command/data ports | LCD initialization and all screen drawing |
| `580000-580003` | Main/sub communication latch | Send routines `2d200a-2d2352`, receive ISR `2c9bea` |
| `700000-77ffff` | Main work RAM | Reset stack near `700b60`, data/BSS extending above `75ae00` |

### Sub/TG CPU

| Range | Chip-select/use | Evidence |
|---|---|---|
| `000800-1fffff`, `200000-3fffff` | TG mask ROM and reset mirror | Reset vectors and continuous code xrefs |
| `480000-480003` | Main/sub communication latch | ISR/queue `23cb72-23ce56`, send routines `23ce90`, `23ceb2` |
| `500000-501fff` | Master SWP30B registers | Large register access families in the TG program |
| `580000-581fff` | Slave SWP30B registers | Same access families at the second base |
| `600000-6fffff` | IC10 MBM29F800B flash | Schematic, ID/program/erase routines and mask-ROM initializer |
| `800000-80ffff` | Wave/SWP glue window | TG accesses; individual registers are not decoded yet |
| `a00000-a7ffff` | Sub work RAM | Reset stack near `a00c40`, queues and TG state |

The SWP sample map is separately `000000-3fffff` wave ROM and
`1000000-103ffff` wave RAM.  These are SWP dword addresses: the schematic's
four 32-Mbit IC51-IC54 devices provide 16 MiB of ROM, while the two 4-Mbit
IC55/IC56 devices provide 1 MiB of 32-bit wave DRAM.  The missing wave ROM
prevents AWM2 sample playback, but is not needed for CPU/LCD boot.  A blank
IC10 does not prevent startup: the TG mask ROM recognizes the erased device and
creates 478,419 non-erased bytes of initial data, which MAME persists as NVRAM.
It includes the `EX5/7 FLSe` signature and performance/system records beginning
with `Init Perform` at flash byte offset `90000`.

## EX5, EX7 and EX5R selection

This is a hardware strap, not a different operating-system image.  Function
`002cd110` converts ADC channel 7 and stores the model at RAM `00734d78`:

| AN7 result | Stored model | Product |
|---|---:|---|
| `000-100` | 0 | EX5 |
| `101-300` | 1 | EX7 |
| `301-3ff` | 2 | EX5R |

The test UI independently confirms the mapping at `002c6c7e`: model 0 prints
`EX5`, model 1 prints `EX7`, and model 2 prints `EX5R TEST`.  The EX5R driver
therefore returns `3ff` on AN7.

## Main/sub communication

The interface is two independent directional latches.  A single shared latch
is incorrect because replies can overwrite an unconsumed command.

| Direction | Data address | Request | Pending/framing pins |
|---|---|---|---|
| Main to sub | Main `580000`, sub `480000` | Sub INT0 | PF1 pending; PG0 is data/framing bit 16 |
| Sub to main | Sub `480000`, main `580000` | Main INT4 | PF1 pending; PG0 is data/framing bit 16 |

Each transfer is a 17-bit unit: the low 16 bits come from the external latch
and PG0 supplies bit 16.  The receive ISRs acknowledge the external interrupt,
sample PG0 and the latch, and enqueue the combined value in a 256-entry ring.
The sub ring is at `00a082f8`, with indices at `00a086f8/00a086fc`.

The sub receiver at `0023cc2a` recognizes a framed header with
`word & 1f000 == 19000`.  Its parser beginning at `0023cf88` dispatches on the
low nibble of a packet field at offset `77`; cases 0-8 select different TG
parameter transformations.  The boot path sends four-byte messages through
the main routines around `002d209a` and waits for response event `1c`.  Main
queue producer `0030031a` signals REALOS flag 5; consumer `003002b6` waits on
that flag.  These details explain why both CPUs and both interrupt directions
must run for the splash screen to advance.

Packet command names beyond the proven boot response are not assigned here:
the parser is heavily table/offset driven, and naming them before correlating
TG register effects would be guesswork.

## Test Mode

The main ROM contains a complete service UI.  Its strings start near
`00270b28`; menu construction is at `002c2d28`, and callback dispatch is at
`002c2e8c`.

The model-specific callback tables are:

| Product | Table | Entries |
|---|---:|---:|
| EX5R | `00271644` | 26 |
| EX7 | `002716ac` | 36 |
| EX5 | `0027173c` | 36 |

The shorter EX5R table omits keyboard-only controls.  The complete string list
contains RAM, battery, wave ROM, LCD, switches/LEDs, rotary encoder, keyboard,
six knobs, pitch/modulation/breath/aftertouch controllers, pedal inputs, MIDI
A/B, audio outputs and A/D loopback, floppy, SIMMs, DIO, SCSI, mLAN, factory
set, and exit.  This makes Test Mode a useful integration target, but many tests
need front-panel matrix, MIDI/audio routing, or missing ROM data rather than
additional FR20 instructions.

The service manual clarifies the dump dependencies: test 1 checks main SRAM,
TG flash IC10 and wave DRAM, while test 3 checks wave ROM IC51-IC54.  The TG
firmware initializes a blank IC10 through standard AMD ID, sector-erase and
word-program sequences, so the flash portion can operate without an original
dump.  The wave-ROM test still cannot pass without IC51-IC54.

The service manual gives the physical entry sequence: boot normally, hold
`VOICE`, then press and hold `BANK H`, then press `PART 8`.  The ordinary event
loop reaches Test Mode at `002b2110` after receiving a corresponding `feXX`
system event from the panel/TG side.  Once in Test Mode, event codes `24`, `25`,
`26`, `28`, `2c`, `32`, and `36` drive its function-key/menu actions.  The
manual also specifies 24 clicks right followed by 24 clicks left for the rotary
encoder test, matching the firmware's x2 count followed by signed divide-by-2.

The genuine EX5R Test Mode was also run in emulation by entering its ROM entry
point at `002c2b68` after normal initialization.  The main menu displays the
main and TG versions, F1 enters AUTO, and the manual `05: SW & LED` test accepts
the emulated sequence `VOICE`, `PERFORM`, `SONG`.  The wave-ROM test correctly
reports all four missing IC51-IC54 devices as NG.  This validates both the test
UI and the logical button path; reproducing the power-on chord still requires
the undumped panel MCU's special `feXX` event.

## MB91103 peripherals exercised by this ROM

An instrumented 90-second boot observed 187 distinct internal-I/O byte
addresses.  The main CPU actively uses GPIO, both UARTs and U-TIMERs, both
reload timers, ADC, input/output timer, both up/down counters, external
interrupts, interrupt controller, delayed interrupt, bit search, external bus
controller and DMA channel 6.  The sub CPU's boot-time internal-I/O footprint
is small because most of its traffic is external SWP and latch I/O.

The main ADC sweep is an especially strong DMA regression:

```
DMACC6 = 0160600c
DMACT6 = 0006
DMAAR6 = 000c0000 + 12 * buffer_index
DMACS6 = 80002904
ADCS   = a205
```

Firmware polls DMACS6 bit 31 until it clears, then requires status value 8 in
bits 27-24 before consuming the six halfwords.  Merely copying samples and
clearing bit 31 is insufficient.

The two up/down-counter configurations are also unambiguous:

- Channel 0: `CCR0=0800`, `CSR0=80`, phase-difference x2 mode for the data
  dial.  Firmware treats UDCR0 as signed, waits for magnitude 2, divides by two,
  then clears the count.
- Channel 1: `CCR1=1010`, `CSR1=a0`, CLKP/8 countdown mode with reload and
  underflow interrupt.

## Rack panel and controls

The EX5R now has a clickable rack-panel layout containing all 52 switches
present on the rack front panel, the six controller knobs and the data dial.
The panel MCU itself is still undumped, so the driver provides an HLE endpoint
for the three PKS bus registers at `0e0000-0e0002`.

The main ROM's interrupt handler at `3b4936` establishes the raw PKS framing:
it acknowledges each two-byte notification with `08` and expects a final `80`
byte.  Brute-force tests showed that a syntactically valid interrupt packet is
not enough to update the UI; initialization and raw-to-logical translation
performed by the HD63B01Y firmware are also required.

The post-translation interface is fully identified.  The panel task maintains
five 16-bit switch words at `758224`, compares them with the consumed state at
`758218`, and translates changed bits through the 80-byte table at `274d4c`.
The EX5R-specific 52-key event sequence is at `270f98`, with its corresponding
name-pointer table copied to `757d88`.  The input callbacks therefore update
the firmware-owned logical bitmap at the boundary where the missing scanner
MCU would have delivered translated keys.  Firmware debounce, edge detection
and event dispatch remain active.  Tests confirm that all normal play modes,
Utility, Disk and Voice Edit are navigable; cursor keys and the data dial edit
parameters; F1 enters AUTO in Test Mode; and the switch test advances from
VOICE to PERFORM to SONG.  ADC channel 6 now reports a healthy backup battery,
eliminating the spurious startup warning.  Raw scanner protocol and panel LED
commands remain pending an MCU dump.

The six knob ports are connected to main-CPU ADC channels 0-5, matching the
firmware's six-result ADC/DMA sweep.  The data dial drives up/down counter 0 in
the quadrature sequence expected by the ROM.  Its keyboard shortcuts use `[`
and `]`, leaving the cursor buttons on the arrow keys.

## Audio architecture and synthesis engines

The two SWP30Bs are not independent stereo tone generators.  The schematic
connects all 16 MELO serial streams from each chip to the corresponding 16
MELI streams of the other chip.  Only master IC44 has connected DAC pins:
DAC0/DAC1 feed main L/R and DAC2/DAC3 feed individual outputs 1/2.  All four
slave DAC pins are marked NC.  The driver now models that topology rather than
mixing the two chips' DAC0/DAC1 outputs directly.

Each SWP30 contains the 64-channel AWM2 sample engine, mixer and 384-step MEG
DSP.  The sub CPU configures both chips.  AWM2 performs ordinary sample
playback; AN, VL and FDSP are implemented by firmware-loaded MEG programs and
associated register/constant tables, not by FR20 sample-by-sample software.

The important program ports are control `21` (address) and `22-25` (four
16-bit words, MSW first).  The firmware loaders are:

| Function | Address | Use |
|---|---:|---|
| VL program loader | `224c00` | Loads a complete slave MEG graph |
| AN/common half loader | `301892` | Loads up to 192 instructions at step 0 |
| AN second-half loader | `30fcc4` | Loads up to 192 instructions at step `c0` |
| Modular effects loader | `25ea22` | Loads 40/48/96/152-step blocks into either SWP |

Voice type values in the TG data are 0 AWM, 1 VL, 2 FDSP, 3 AN poly,
4 AN layer, 5 AN+FDSP and 6 drums.  Control register `43` tracks the modeled
engine transition:

| Value | Observed path |
|---:|---|
| `0000` | AWM/off or transition |
| `0003` | VL |
| `0100` | AN transition/teardown |
| `0101` | AN |
| `0001` | AN+FDSP |

These values were verified dynamically by entering the real ROM loader
routines after normal boot.  The trace also proved that controls `53-5c`
change with the selected engine: normal boot initializes them to `0056`, VL
clears all ten, and the AN path rewrites a subset.  Their arithmetic function
is still unknown, so MAME stores all currently unmapped SWP registers as saved
16-bit latches and can trace accesses with `-verbose`; it does not pretend the
write-only state has a proven DSP effect.

### MEG instruction gaps

Normal EX5R boot leaves the slave program cleared and installs the effects
graph on the master.  The final master graph uses the still-unidentified MEG
bits 32, 1 and 0.  The physical-modeling programs exercise more of them:

| Program | b38 | b35 | b34 | b32 | b3 | b2 | b1 | b0 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| AN/common half (`28ee54`, 192) | 0 | 19 | 4 | 11 | 0 | 55 | 27 | 50 |
| AN second half (`2964f0`, 192) | 0 | 19 | 4 | 11 | 55 | 27 | 28 | 50 |
| VL A (`3c8d68`, 247) | 0 | 8 | 13 | 2 | 2 | 78 | 107 | 109 |
| VL B (`3c97d4`, 254) | 0 | 8 | 11 | 2 | 6 | 67 | 96 | 119 |

The two AN halves are structurally paired.  Where the common half uses low
operation values 4, 5, 6 and 7, the second half uses A, B, C and D.  This is
strong evidence that instruction bits 3-0 form a special operation selector,
not four independent flags.  Neither the supplied Fujitsu documentation nor
the Yamaha schematic defines those operations, and the SWP30 core currently
ignores them.  Implementing guessed arithmetic here would make AN/VL output
plausible but incorrect; hardware captures or a Yamaha MEG description are
still required to make these engine programs bit-accurate.

The reproducible tools are:

```sh
python3 scripts/tests/ex5_meg_rom.py --romdir ex5r
./mame ex5r -rompath . -seconds_to_run 20 -video none -sound none \
    -autoboot_script scripts/tests/ex5_panel_mapping.lua
rm -rf /tmp/ex5-nvram
./mame ex5r -rompath . -nvram_directory /tmp/ex5-nvram -video none -sound none \
    -autoboot_script scripts/tests/ex5_flash_init.lua
./mame ex5r -rompath . -seconds_to_run 12 -video none -sound none -nothrottle \
    -autoboot_script scripts/tests/ex5_swp_trace.lua
./mame ex5r -rompath . -video none -sound none -nothrottle -debug -debugger none \
    -debugscript scripts/tests/ex5_swp_engines.cmd \
    -autoboot_script scripts/tests/ex5_swp_trace.lua
./mame ex5r -rompath . -seconds_to_run 2 -video none -sound none -nothrottle \
    -autoboot_script scripts/tests/ex5_swp_latch_test.lua
```

The latch test covers channel slot `0b` and controls `43`/`5c`.  The boot trace
reconstructs every downloaded MEG instruction and reports use of the unknown
bits after each program burst.

## Remaining hardware unknowns

- Decode the electrical effect of the first eight `800000-80ffff` wave/SWP
  glue bytes.  Their size-dependent values and associated SWP control `1a`
  writes are known and the complete aperture is stateful, but the discrete
  decoder/transceiver behavior is not yet proven.
- Determine MEG low-operation selectors 0-15 and instruction bits 32, 34, 35
  and 38 from SWP30 hardware captures or authoritative Yamaha documentation.
- Determine the arithmetic role of SWP controls `43-49` and `53-5c`; readback
  and engine-transition traces are implemented, not their unknown DSP effect.
- Acquire the four IC51-IC54 wave ROM dumps.  Until then AWM2 addresses read
  zero and no meaningful sampled audio can be produced.
- Dump an original IC10 to compare its shipped/user contents with the complete
  mask-ROM-generated initial image; it is no longer required for UI startup.
- Correlate the known `VOICE` + `BANK H` + `PART 8` chord with the panel MCU's
  special `feXX` event producer, or recover the EX5R-specific entry chord.
- Name the higher-level main/sub commands by tracing each parser case to SWP
  register writes, rather than inferring names from packet values alone.
- Decode the PKS output commands and LED matrix so the rack-panel lamps follow
  firmware state; MIDI ports and audio loopback also remain to be wired for
  their service tests.
