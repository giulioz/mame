# Fujitsu FR20/MB91103 emulation status

The base FR20/FR30 instruction set is implemented in `fr.cpp`.  The executable
regression scripts in `scripts/tests` exercise the implementation through the
MAME debugger against the EX5R machine.

## CPU coverage

- All documented original FR instruction encodings, including the three
  immediate widths, all addressing modes, resource and coprocessor encodings.
- NZVC arithmetic semantics, multiply and the complete signed/unsigned divide
  step sequences.
- Normal and delayed control transfers, delay-slot behavior and instruction
  cycle counts.
- INT, INTE, undefined-instruction and coprocessor exceptions, RETI, step trace,
  NMI, maskable interrupt priority, ILM restrictions, SSP/USP switching.
- Reset state, debugger-visible architectural registers and save states.

`scripts/tests/fr20_core.cmd` currently contains 70 semantic checks.
`scripts/tests/fr20_cycles.cmd` checks representative 1-, 2-, 3- and 5-cycle
paths.  Both suites must report only `1` results.

## MB91103 peripheral coverage

Implemented (the debugger suite covers the directly observable register and
timer paths):

- GPIO data/direction behavior and callbacks.
- Interrupt levels, external interrupt request latching and masking.
- A/D software-triggered single/continuous/stop conversion sequencing.
- Both reload timers using the three internal clocks.
- Both U-TIMER channels and their interrupt flags.
- Basic UART transmit timing/status/interrupts and injected receive bytes.
- 16-bit free-run timer, eight output compares and four edge-selectable input
  captures.
- Both 16-bit up/down counters: timer, external up/down, x2/x4 quadrature,
  reload/compare, direction/limit flags and interrupts.
- The EX5's channel-6 A/D DMA request, address/count updates and documented
  completion status handshake.
- Delayed interrupt, bit-search unit and documented reset values used by the
  firmware.

Still required before the MB91103 peripheral set can be called complete:

- Extended serial I/O.
- UART bit-level receive, synchronous/external-clock operation and physical
  SCK/SO pins.
- Reload-timer external clock, gate/trigger and output pins.
- A/D external/timer triggers and data-protection behavior.
- General DMAC transfers on channels 0, 1, 4 and 5, external DREQ/DACK/EOP,
  error paths, and non-A/D peripheral request routing.  The available MB91103
  data sheet omits the control-register bit definitions; these paths must not
  be guessed from the incompatible descriptor DMAC in the MB91101.
- Watchdog, time-base, standby and gear-clock effects.
- Cache timing/control, external bus wait/DRAM behavior and endian control.

`scripts/tests/mb91103_periph.cmd` covers the currently implemented peripheral
paths.  Raw storage for an unimplemented register is not considered peripheral
emulation.
