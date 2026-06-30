# JV-1080 factory test mode

This documents the manufacturing/service test in firmware 1.02 and the
emulator probes used to exercise it.  All addresses use the firmware's A27-set
mirrors.  Tests that initialize or check memory must be run with disposable
NVRAM.

## Entry and dispatcher

1. Boot normally and wait for the ROM Play screen.
2. Press `SHIFT+ENTER`, then release them.
3. Hold both cursor `UP+DOWN` and press the VALUE encoder switch.

The ordinary ROM Play dispatcher at `0x0a027eb0` handles VALUE action `0x4f`.
It reads the live GA matrix and enters the factory screen when GA row 3 is
`0x09` (UP key code `0x1b`, DOWN key code `0x18`).  This is separate from the
queued IRQ5 key events.  The factory main dispatcher is `0x0a029f78`.

The first screen reports CPU ROM 1.01 and external firmware 1.02.  Its eight
test selectors are:

| Front-panel button | Test | Event dispatcher |
|---|---|---:|
| Tone Switch 1 (`1/9`) | Memory | `0x0a02a274` |
| Tone Switch 2 (`2/10`) | LCD | `0x0a02a4d8` |
| Tone Switch 3 (`3/11`) | Panel switches | `0x0a02a720` |
| Tone Switch 4 (`4/12`) | Card/expansion | `0x0a02a90c` |
| Tone Select 1 (`5/13`) | MIDI | `0x0a02acb8` |
| Tone Select 2 (`6/14`) | Sound, output, EFX | `0x0a02adcc` |
| Tone Select 3 (`7/15`) | LEDs | `0x0a02b28c` |
| Tone Select 4 (`8/16`) | Initialize | `0x0a02b494` |

`4/12` and `8/16` are easy to confuse.  The former enters Card Test; the latter
performs Initialize and returns to normal Performance Play.  There is no
evidence that `4/12` is a service-mode exit command.

## Emulator findings and fixes

The entry chord exposed three independent GA problems:

- GA registers `0x00-0x03` are active-high live matrix rows.  IRQ key codes are
  `row * 8 + column`, but the service chord deliberately reads row 3 directly.
- The VALUE switch is active-low bit 0 of GA register `0x3b`.  The firmware
  constructs its direct-button word as `(GA[0x3a] & 0x0f) | (GA[0x3b] << 4)`.
- The IRQ5 handler reads status `0x3c` before payload `0x3d/0x3e`.  Key and
  encoder IRQ payloads must remain stable and be acknowledged by the payload
  read, not by the earlier status read.  Simultaneous key events also require
  their payload to be stored in each queue entry.

The Memory test then found two non-panel omissions:

- SH7032/SH7034 ADCSR and ADCR are at `0x05fffef8/0x05fffef9`.  The firmware
  sets `GBR=0x05fffe00`, writes `GBR+0xf8`, waits two ticks, and reads ADDRA-D
  from `GBR+0xe0`.  Mapping control at `0xe8/0xe9` leaves every result at zero.
  Firmware threshold `0x1ff-0x2cc` is the healthy internal-battery range; AN1
  is the internal battery and AN0 is the optional card battery.
- The S-RAM destructive read/write test covers `0x02300000-0x0230ffff`.  This
  64 KiB work-SRAM window was absent from the driver.

GA registers `0x10-0x17` are an active-high 64-bit LED bitmap using the same
row/column number as the matrix key code.  The factory walk table is at
`0x0a0582a4`; its 24 ordinary values exactly match the front-panel buttons that
have LEDs.  The driver now exports those bits and the layout displays them.

## Confirmed test behavior

- **Memory:** With a healthy AN1 value and the S-RAM window mapped, VALUE runs
  all destructive checks, initializes factory data, and returns to the main
  service screen with `Completed.`
- **LCD:** Five VALUE presses show blank, all-pixel, and character-pattern
  screens, then return to the main service screen.
- **Panel switches:** This is an ordered test, not a free-form sweep.  Its
  35-entry table at `0x0a057f82` reaches count zero in emulation.  The physical
  order is PREVIEW, 1-8/9-16, PARAMETER, PALETTE, part 1-8, VALUE, DEC, INC,
  UP, LEFT, RIGHT, DOWN, modes, UTILITY, EFX, SHIFT, EXIT, ENTER, then the
  seven bank/group buttons.
- **Card:** Correctly reports missing expansion/card resources in the current
  machine configuration.  These are external-device failures, not test-mode
  control failures.
- **MIDI:** Sends `A0 A1 A2` through SCI0 and waits for the parser's poly-key-
  pressure callback.  Toggle **Factory MIDI loopback cable** (F12) on and then
  off to model the service manual's connect/disconnect fixture.  The driver
  also exposes normal MIDI IN and OUT ports at the SH7034's native 31.25 kbaud.
- **Sound/output:** VALUE starts the six-part tone source and walks MIX L/R,
  DIRECT 1 L/R, and DIRECT 2 L/R.  A second output walk advances into the EFX
  checks.
- **EFX:** The firmware calls its two hidden DSP self-test callbacks (descriptor
  entries 10 and 11 in the table at `0x0a057dcc`) and reports `EFX Test OK` in
  the current XP emulation.  The memory/readback portions really pass; the
  final execution test currently returns four zero results, which the ROM
  accidentally accepts because it compares only nonzero words.  This is an
  execution oracle, not working DSP emulation.  The next page exposes the EFX
  delay thresholds.
- **LED:** VALUE walks the hardware LED table, asks for confirmation, and
  returns to the main screen.  The lit button is now visible in the layout.
- **Initialize:** Loads factory data and returns to normal Performance Play.

## Reproduction

The reusable probe accepts a comma-separated sequence of `PORT:FIELD` names:

```sh
rm -rf /tmp/jv1080-factory-nvram /tmp/jv1080-factory-cfg
mkdir -p /tmp/jv1080-factory-nvram /tmp/jv1080-factory-cfg

JV1080_FACTORY_SELECT='BUTTONS1:1/9,BUTTONS5:VALUE (push)' \
JV1080_FACTORY_STEP_WAIT=10 \
JV1080_FACTORY_TEST_SNAP=/tmp/jv1080-factory \
./jv1080sh jv1080 -rompath . -video none -sound none -nothrottle \
  -skip_gameinfo -nvram_directory /tmp/jv1080-factory-nvram \
  -cfg_directory /tmp/jv1080-factory-cfg -seconds_to_run 55 \
  -autoboot_delay 0.000001 \
  -autoboot_script jv1080/re/lua/factory_test_probe.lua
```

The probe writes screen snapshots plus a GA trace.  Its log records the
current UI dispatcher, switch-test index, LED bitmap, ADC result table, and
SH7034 ADDR/ADCSR state after every step.

An end-to-end MIDI fixture run is:

```sh
JV1080_FACTORY_SELECT='BUTTONS2:5/13,MIDI_LOOPBACK:Factory MIDI loopback cable,MIDI_LOOPBACK:Factory MIDI loopback cable' \
JV1080_FACTORY_STEP_WAIT=3 \
JV1080_FACTORY_TEST_SNAP=/tmp/jv1080-factory-midi \
./jv1080sh jv1080 -rompath . -video none -sound none -nothrottle \
  -skip_gameinfo -nvram_directory /tmp/jv1080-factory-nvram \
  -cfg_directory /tmp/jv1080-factory-cfg -seconds_to_run 55 \
  -autoboot_delay 0.000001 \
  -autoboot_script jv1080/re/lua/factory_test_probe.lua
```

The three captured screens are `WAITING`, `CONNECT`, and then the main
`Completed.` page after the virtual cable is removed.

Set `JV1080_FACTORY_XP_TRACE=1` to also write `-xp.csv`, containing DSP-memory,
readback-latch, status, and IRQ accesses.  This is especially useful while
advancing the Sound/EFX test.
