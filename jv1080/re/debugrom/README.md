# JV-1080 debug ROM — MIDI-sysex peek/poke for the XP chip

A patch to the JV-1080 external program ROM (`roland_r00678167.ic20`) that adds a
private MIDI-sysex facility to read and write arbitrary memory — primarily the XP
PCM/DSP chip register space at CS4 (`0x04000000`, A27-mirror `0x0c000000`).

**Status: built and validated end-to-end in MAME.** The modified ROM boots to the
normal `PATCH PLAY` screen and every command round-trips correctly (see Validation).

## Files

| File | Purpose |
|---|---|
| `sh1asm.py` | Minimal SH-1 (big-endian) assembler. `python3 sh1asm.py selftest` checks 52 encodings against real firmware bytes. |
| `debug_patch.s` | The payload: sysex shim + command handlers, assembled at vaddr `0x0a059d48`. |
| `build.py` | Assembles `debug_patch.s`, splices it into ic20 free space, applies the hook → `ic20_debug.bin`. |
| `ic20_debug.bin` | The built debug ROM (1 MiB). |
| `jvdebug.py` | Host client (`python-rtmidi`): `JV().ping/peek/poke/read_dsp`, the low-level protocol. |
| `xp_lab.py` | XP DSP host harness (`XPDSP`): `clear_dsp` / `upload` / `verify` / `monitor_iram` / `monitor_readback` / `rd_reg` / `wr`. Implements the no-notes / no-Program-Change methodology below. |

## How the hook works (one 4-byte change)

`midi_task` (`0x0a02882e`) dispatches each MIDI status byte through an 8-entry
table at `0x0a059cd0`; entry 7 is the sysex handler `0x0a028948`, which accumulates
a full `F0..F7` message into a 4-page ring buffer at `0x0901e954` and then calls the
"message-complete dispatcher" `0x0a000200` via a literal at **file `0x28a84`**, with
the message page pointer at `@(8,r15)`.

`build.py` replaces that single literal (`0x0a000200` → `dbg_shim` at `0x0a059d48`).
Every completed sysex now enters `dbg_shim`, which checks the manufacturer id at
`page[3]`: if it is `0x7D` (ours) it handles the command; otherwise it tail-jumps to
the real `0x0a000200`, so normal Roland sysex is untouched. The 552-byte payload
lives in the 33 KiB `0xFF` free block at file `0x59d48`.

## Wire protocol

```
F0 7D <op> <nibblized payload...> F7
```
Every data byte is sent as two bytes — `(b>>4)` then `(b&0x0F)` — so no payload byte
is ever ≥ `0x80`. Multi-byte fields are big-endian. Use addresses `0x0c00xxxx` to hit
the XP chip on CS4.

| op | name | payload | reply |
|----|------|---------|-------|
| `00` | PING | — | `F0 7D 00 00 01 F7` |
| `01` | SETFLAG | `<flag:1B=2 nib>` | — (writes debug-freeze byte at `0x0901ff00`) |
| `10` | POKE | `<addr:4B><width:1B><data:4B>` | — (`MOV.b/w/l` data→addr; width 1/2/4 selects bytes written; data is always 8 nibbles) |
| `20` | PEEK | `<addr:4B><width:1B>` | `F0 7D 20 <value:4B nibblized> F7` |
| `21` | PEEKDSP | `<xpoff:2B>` | `F0 7D 21 <hi:2B><lo:2B> F7` — latch read: trigger-reads `[0x0c000000+off]`, returns `0x3912`/`0x3910` |

`PEEKDSP` is required for XP DSP memory (`0x2c00–0x38ff`): a direct `PEEK` there
returns the bus trigger value, not the stored word. Use `PEEKDSP` for CRAM/IRAM/PRAM.

Scratch RAM used by the payload (work DRAM, validated free in MAME): debug-freeze
byte `0x0901ff00`, TX reply buffer `0x0901ff10`.

## Build

```sh
cd jv1080/re/debugrom
python3 sh1asm.py selftest      # optional: verify the assembler
python3 build.py                # -> ic20_debug.bin  (+ symbol map)
```

`build.py` refuses to run unless the hook word at file `0x28a84` is the expected
`0x0a000200` and the payload region is still free, so it fails safe on the wrong image.

## Test in MAME before burning hardware

The MAME driver already loads ic20 and shadows XP writes, so validate there first.
Point an isolated romset at the debug ROM (CRC mismatch is only a warning):

```sh
mkdir -p /tmp/jv1080dbg/jv1080 && cd /tmp/jv1080dbg/jv1080
ln -sf <mame>/jv1080/roland_r00677323_6437034c12f.ic15 .
ln -sf <mame>/jv1080/jv1080_waverom[1-4].bin .
ln -sf <mame>/jv1080/hd44780_a00.bin .
cp <mame>/jv1080/re/debugrom/ic20_debug.bin roland_r00678167.ic20

cd <mame>
./jv1080sh jv1080 -rompath /tmp/jv1080dbg -sound none -skip_gameinfo \
  -seconds_to_run 16 -autoboot_delay 1e-6 -autoboot_script boot_test.lua
```

The lua harnesses under `/tmp` during development invoke `dbg_shim` directly from
the idle loop (parking the CPU, building a synthetic message page, hijacking `PC`)
— a clean way to exercise the handlers without MIDI-serial injection.

## Validation (MAME, emulated JV-1080)

- Boot: modified ROM reaches the normal `PATCH USER:001 / PLAY` screen — hook benign.
- `PING` → `F0 7D 00 00 01 F7`.
- `POKE` 8/16/32-bit to RAM, read back: exact.
- `PEEK` 32-bit: byte-exact reply (`0xCAFEF00D` → `f0 7d 20 0c 0a 0f 0e 0f 00 00 0d f7`).
- `PEEKDSP` on IRAM1[0]: poked `0x00ABCDEF`, latch readback returned `0x00ABCDEF`
  through the real XP device emulation.
- **Persistence:** poked distinct values into CRAM `0x2c00`/`0x2d00`, PRAM `0x3400`/`0x3600`,
  IRAM1/2/3, global `0x3908`, mixer-send `0x3a00`, idled 2 s with no panel input, peeked
  back — **all nine SURVIVED**. The effect workers (RFX/reverb/chorus → `xp_dsp_upload_program0`)
  are blocked on their RTOS event masks and only run on a patch change, effect edit, demo,
  or the boot bursts (~0.4–1.3 s), so the DSP/effects space is stable for probing at idle.

## Validation status before flashing hardware

| Check | Result |
|---|---|
| Modified ROM boots | ✅ normal PATCH PLAY screen |
| Hook literal at file `0x28a84` = `dbg_shim` | ✅ verified in image |
| Firmware sysex-completion calls that literal with page ptr at `@(8,r15)` | ✅ confirmed in disassembly (`0x0289d8`/`0x028a28`) |
| `dbg_shim` + all handlers (PING/POKE/PEEK/PEEKDSP) | ✅ byte-exact (direct-call) |
| Poke/peek vs the real XP device model | ✅ incl. PEEKDSP latch round-trip |
| DSP-space pokes persist at idle | ✅ all regions |
| Scratch RAM `0x0901ff00`/`0x0901ff10` untouched by firmware (idle + playing) | ✅ zero accesses |
| **Live inbound: real RX ring → `midi_task` → sysex handler → hook → `dbg_shim` → POKE** | ✅ POKE landed (`TGT=DEADBEEF`) |
| **Live outbound: `dbg_shim` → reply → `midi_transmit_bytes` → TX ring** | ✅ TX ring = `f0 7d 00 00 01 f7` |

The live MIDI path was validated by injecting bytes into the firmware's real receive
ring (`0x09000030`, write index `0x090005e0`) and waking `midi_task` exactly as the SCI
RX ISR does (`rtos_signal_event(0x0ffff024, 2)`), then letting the unmodified firmware
buffer, complete, and dispatch the message through the hooked literal. The only step not
exercised is the literal UART bit-shifting between TX and RX pins — standard MAME serial,
proven by the factory MIDI loopback test, and identical on hardware. Burning is still
reversible (keep `prom_orig.bin`); a wrong hook would only make debug commands silent,
since non-`0x7D` sysex is forwarded to `0x0a000200` and the ROM boots normally.

## Probing notes (so pokes are not trampled)

- Probe in steady PATCH PLAY **after ~1.5 s** (past the boot DSP uploads). Don't touch the
  panel, change patch, edit effects, or enter the demo — any of those re-runs the effect
  workers and overwrites CRAM/PRAM slots `0x3400–0x359f` / `0x2c00–0x2ccf`.
- Per-voice registers (`0x0000–0x21ff`) and mixer sends are rewritten only for *active*
  voices, so they hold while no note is playing — confirmed in the persistence test.
- IRAM3 current values (`0x3200`): a direct write stops that slot's hardware ramp so the
  poke holds; writing an IRAM3 *target* (`0x3300`) restarts the ramp.
- **`PEEKDSP` uses a 16-bit trigger read** (not 32-bit): the XP readback latch follows the
  last aligned sub-access, so a 32-bit read of the 16-bit CRAM region would latch the
  adjacent slot. This was a bug found via the persistence test and is fixed.

## DSP experiment methodology

The confirmed host interface and the workflow the `xp_lab.py` `XPDSP` harness follows
(full detail and the silicon measurements are in `../XP_HARDWARE_DEBUG.md §6`):

- **No notes / no Program Change.** Both make the firmware *re-upload* the DSP program
  (the effect workers rewrite PRAM/CRAM slots 0–103), which silently reverts your pokes.
  The firmware writes the DSP area **only at boot** (~0.4–1.3 s) and then stays quiet.
- **Boot → idle → clear → upload → monitor.** Wait ~2 s for idle, then
  `clear_dsp()` → set CRAM/PRAM → `upload()` → (optionally kick `0x3916` `0→7`) →
  `monitor_iram()` / `monitor_readback()`.
- **Readback via `0x3910` (low 16) / `0x3912` (high 16).** DSP memory is not directly
  readable — a PEEK returns 0 but latches the location's value into that register.
  CRAM (`<0x3000`) is 16-bit via `0x3910`; IRAM/PRAM (`≥0x3000`) is 32-bit via
  `0x3912:0x3910`.
- **DSP control registers are write-only** (`0x3908`, `0x3914`, `0x3916`, `0x3924`, …):
  they read back 0 on silicon. `0x3916` = run(`7`)/stop(`0`).
- **Use IRAM1/IRAM2 as the clean observable, not IRAM3.** IRAM3 (`0x3200`) is
  auto-updated by the breakpoint-ramp engine (targets `0x3300`, rates `0x3928`), so a
  ramp can move a value you stored there. (IRAM1/2 slots 6–9 are forced to 0 at idle =
  hardware L/R I/O taps — ignore them.)
- **NEVER read a `0x39xx` control/status register via `read_dsp()`.** Its width-4 trigger
  spans the IRQ register at `0x3918` and hangs the firmware. Read those with a direct
  PEEK of width 2 (`rd_reg`) instead.

## Burning hardware — two 8-bit EPROMs

The program ROM is a 16-bit bus of two byte-wide EPROMs (your `jv1080/dumper/{LOW,HIGH}.bin`
dumps). `build.py` emits matching debug images, de-interleaved and doubled exactly like the
originals:

| File | Contents | Replaces |
|---|---|---|
| `ic20_debug_LOW.bin`  (1 MiB) | even bytes (`image[0,2,4,…]`), 512 KiB doubled | the chip dumped as `LOW.bin` |
| `ic20_debug_HIGH.bin` (1 MiB) | odd bytes (`image[1,3,5,…]`), 512 KiB doubled | the chip dumped as `HIGH.bin` |

**Both must be reflashed** — the hook and payload straddle even and odd byte lanes, so the
patch lands in both chips. Verified: de-interleaving the two images reproduces `ic20_debug.bin`
exactly, and each is byte-identical to the corresponding original dump except (a) our patch and
(b) an 18-byte glitch at `0x10710` that was in the *original* raw dump and is absent here (these
are built from the clean image). Reuse the proven `jv1080/dumper` flow; keep `prom_orig.bin`/the
original chips for rollback. No global ROM checksum was found over ic20; normal boot does not
validate one (confirmed: the modified image boots).

## What is NOT done yet: hard voice freeze

The periodic voice-register rewrite (`xp_update_all_voices 0x0000a268`, called every
~10 ms from the task-8 loop) lives in the **internal** mask ROM and cannot be patched
in place. Notes:

- **DSP / CRAM / PRAM / IRAM / global registers (`0x2c00–0x3bff`) need no freeze** —
  the voice task never writes them; they only change on a patch change. Poke freely.
- **Per-voice registers (`0x0000–0x21ff`)** are rewritten at 100 Hz only for *active*
  voices. With the synth idle (no held notes), pokes there persist.
- A true runtime freeze of active voices requires repointing the **task-8 descriptor**
  (entry at ic20 file `0x70`, value `0x00009fa8`) to a re-created loop that gates the
  `synth_update_parts`/`xp_update_all_voices` calls behind the `SETFLAG` byte. The flag
  is already wired; the re-created task body is the remaining work. `SETFLAG` currently
  stores the byte but nothing enforces it.
