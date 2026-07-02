# Roland XP — host register / memory map

The XP (MBCS30109B / MB87B105PF / RHR-2342) is a 64-voice PCM sample player with an
integrated effect DSP. The CPU sees it through a **0x4000-byte aperture** at host base
**`0x04000000`**, mirrored at **`0x0c000000`** (A27 mirror; the debug ROM uses the mirror).
All offsets below are relative to that base.

**Confidence legend**
- **[S]** — confirmed on real silicon via the MIDI debug ROM (peek/poke), per `XP_FACTS.md`.
- **[F]** — confirmed the firmware *writes* this region (empirical write-footprint, `xp_dump_cb`),
  even where the debug ROM never poked it directly.
- **[M]** — emulator device model, reverse-engineered from SH firmware disassembly
  (`src/devices/sound/roland_xp.cpp`); reproduces correct behavior but not individually silicon-verified.
- **[H]** — hypothesis / not fully pinned.

Word endianness: DSP/config/mixer words are **big-endian** in this space (a 16-bit CRAM value
`0x5000` is byte `0x50` then `0x00`), matching how the firmware reads them back through the
`0x3912:0x3910` latch.

---

## Top-level regions

| range | size | region | conf |
|---|---|---|---|
| `0x0000–0x2BFF` | 0x2C00 | **Per-voice PCM engine** — 44 banks of 64×32-bit voice registers | [F]/[M] |
| `0x2C00–0x2E3F` | 0x0240 | **CRAM** — 288 × 16-bit coefficients | [S] |
| `0x2E40–0x2FFF` | — | reserve (never written) | [F] |
| `0x3000–0x30FF` | 0x0100 | **IRAM1** — 64 × 32-bit working/filter state | [S] |
| `0x3100–0x31FF` | 0x0100 | **IRAM2** — 64 × 32-bit working/filter state | [S] |
| `0x3200–0x32FF` | 0x0100 | **IRAM3 current** — 64 × 32-bit ramp bank (Q22) | [S] |
| `0x3300–0x337F` | 0x0080 | **IRAM3 targets** — 64 × 16-bit ramp breakpoints | [S] |
| `0x3380–0x33FF` | — | reserve | [F] |
| `0x3400–0x387F` | 0x0480 | **PRAM** — 288 × 32-bit program words | [S] |
| `0x3880–0x38FF` | — | reserve (never written) | [F] |
| `0x3900–0x39FF` | 0x0100 | **Global config / control / readback** | [S]/[M] |
| `0x3A00–0x3BFF` | 0x0200 | **Mixer sends** — 4 banks × 64 × 16-bit | [F]/[M] |
| `0x3C00–0x3FFF` | 0x0400 | **Wave-ROM read aperture** (1 KiB window, read-only) | [M] |

CRAM and PRAM occupy 288 slots each, paired 1:1 (`CRAM[k]` at `0x2C00+2k` is the coefficient
for `PRAM[k]` at `0x3400+4k`). The write-footprint confirms CRAM ends exactly at `0x2E3F` and
PRAM exactly at `0x387F` — nothing spills into the reserve gaps.

---

## Per-voice PCM engine — `0x0000–0x2BFF` [F]/[M]

44 banks, each `0x100` bytes = **64 voices × one 32-bit word** (`bank = offset & 0xFF00`,
`voice = (offset & 0xFF) >> 2`, big-endian word). Banks the emulator actually decodes:

| bank | reg | conf |
|---|---|---|
| `0x0000` | **wave control** — rom/bank/loop-mode; **bit15 = start** (write restarts the PCM reader) | [M] |
| `0x0100` | **sample start** address (20-bit) | [M] |
| `0x0200` | **sample loop** point | [M] |
| `0x0300` | **sample end** | [M] |
| `0x1000` | voice init constant (`0x08` on start) | [M] |
| `0x1100` | TVF-Q (resonance) **destination** | [M] |
| `0x1200` | pitch **destination** | [M] |
| `0x1300` | TVF-F (cutoff) **destination** | [M] |
| `0x1400` | amp-mod **destination** | [M] |
| `0x1500` | amp (TVA) **destination** | [M] |
| `0x1600` | TVF-Q interp control | [M] |
| `0x1700` | pitch interp control | [M] |
| `0x1800` | TVF-F interp control | [M] |
| `0x1900` | amp-mod interp control | [M] |
| `0x1A00` | amp interp control | [M] |
| `0x1B00` | pitch **start** value | [M] |
| `0x1C00` | TVF-F **start** value | [M] |
| `0x1D00` | amp-mod **start** value | [M] |
| `0x1E00` | amp **start** value | [M] |
| `0x2000` | filter-type select | [M] |
| `0x2100` | TVF-Q **start** value (shifted right 2 on start) | [M] |
| `0x2300` | amp-modulation level | [H] |
| `0x2700` | amp-modulation base | [H] |
| `0x0C00 / 0x0E00 / 0x2800 / 0x2900` | "voice state A/B/C/D" (named, not modeled) | [H] |

Banks `0x0400–0x0F00`, `0x1F00`, `0x2200`, `0x2400–0x2600`, `0x2A00–0x2B00` are present in the
dispatch but unused/unknown. **Footprint note:** at idle the firmware writes the ramp pages
`0x1100–0x1EFF` and `0x2000–0x21FF`; the wave/sample pages `0x0000–0x03FF` are written **only on
note-on**. These are the sample-playback engine, distinct from the effect DSP.

---

## CRAM / IRAM / PRAM — `0x2C00–0x387F` [S]

The effect-DSP program memory (device member `m_dsp_program`). Fully readable on silicon via the
`0x3912:0x3910` latch.

| range | element | count | name | notes |
|---|---|---|---|---|
| `0x2C00` | 16-bit | 288 | **CRAM** | coefficient per PRAM slot. Format: `coef = sext14(raw[13:0]) << [0,1,2,4][raw[15:14]] / 8192` (`0x5000`=+1.0, `0xE000`=−16.0). [S, C14] |
| `0x3000` | 32-bit | 64 | **IRAM1** | working memory / L filter state. Emulator never runs the DSP → stays 0. [S] |
| `0x3100` | 32-bit | 64 | **IRAM2** | working memory / R filter state. [S] |
| `0x3200` | 32-bit | 64 | **IRAM3 current** | ramp "magic" bank (Q22). A **direct write seeds the value and stops** its ramp. Slot 3 = `1` arms the hardware RNG at `0x320C`. [S, C9] |
| `0x3300` | 16-bit | 64 | **IRAM3 targets** | 9-bit ramp breakpoints; **writing a target starts** the ramp (`current → target<<13`). [S, C9] |
| `0x3400` | 32-bit | 288 | **PRAM** | instruction words. Decode: `st=[15:14]` store-control, `addr=[13:0]` (word `[13:6]` | column `[5:0]`), `hi=[23:16]` ERAM channel, `wb=[24]`, `ext=[31:25]` DRAM strobes [H]. See `xp_dsp_isa_decoded.md`. [S, C16-C24] |

PRAM slot usage: **0–103 = insert-EFX (RFX)**, **104–255 = fixed system effect/mixer program**,
**256–287 = reserve**. IRAM3 ramp rate is set per 16-voice group at `0x3928+` (see below).

---

## Global config / control / readback — `0x3900–0x39FF` [S]/[M]

Device member `m_global_config`. Registers are byte-addressable; the firmware accesses them as
16-bit. Several are **write-only on silicon** (read back 0) and observable only via the emulator.

| addr | reg | conf |
|---|---|---|
| `0x3900 0x3902 0x3904 0x3906` | **voice reset bitmaps** (4×16-bit, one bit per voice; a cleared bit resets that voice's runtime, then reads back `0xFFFF`) | [S] |
| `0x3908 0x390A 0x390C 0x390E` | global engine config (boot values `1C19 1818 1818 0808`) — **write-only** | [S, C3/C8] |
| `0x3910 / 0x3911` | **DSP readback latch, low 16** (holds the last trigger-read word / low half) | [S, C2] |
| `0x3912 / 0x3913` | readback **high 16** (`0x3912` direct-reads 0; `0x3913` = voice-command status) | [S, C2] |
| `0x3914` | config word (`403F`) — **write-only** | [S, C3/C8] |
| `0x3916` | **DSP run/stop** (`7`=run/free-run, `0`=stop, `4`=factory single-pass) — **write-only** | [S, C7] |
| `0x3918 / 0x3919` | **IRQ7** status/control (source bits 13:8, reason bits 3:0) | [M] |
| `0x391A / 0x391B` | **IRQ data** (reading `0x391B` acknowledges the head event) | [M] |
| `0x391C` | EFX-delay comparator (factory-test path only) | [H] |
| `0x3920 / 0x3921` | wave-ROM read address, **page** (addr bits 19:10) | [M] |
| `0x3922 / 0x3923` | wave-ROM read address, **bank** (addr bits 26:20) | [M] |
| `0x3924 0x3926` | DSP routing/control (`D200 16D2`) — **write-only**; bit layout open | [S, C3/C8] / [H] |
| `0x3928 0x392A 0x392C 0x392E` | **IRAM3 ramp rates**, one per 16-voice group (`0100 0100 0300 0300`) | [S, C9] |
| `0x3930` | EFX-delay cal drive (factory-test path only) | [H] |

Rest of `0x3932–0x39FF`: unused. `0x3940/0x3980/0x39C0` as mirrors of `0x3900` is **[H]**, untested.

---

## Mixer sends ("mixer coefficients") — `0x3A00–0x3BFF` [F]/[M]

Four send banks, each **64 voices × 16-bit**, decoded into `m_voices[].mixer_send`. Each word is
**`level[15:6] | bus[5:0]`** (10-bit level, 6-bit destination bus).

| addr | bank | idle routing (boot patch) |
|---|---|---|
| `0x3A00` | send 0 (dry L) | every voice → bus 6, level 0 |
| `0x3A80` | send 1 (dry R) | every voice → bus 7, level 0 |
| `0x3B00` | send 2 (effect A) | every voice → bus 8, level 0 |
| `0x3B80` | send 3 (effect B) | every voice → bus 9, level 0 |

These are the per-voice level+routing coefficients. Effect **return / dry-wet / master** levels are
NOT separate registers — they are **CRAM system-region coefficients** (slots 104–255) consumed by
the DSP MAC ops. (`XP_FACTS.md` C18.)

---

## Wave-ROM read aperture — `0x3C00–0x3FFF` [M]

A 1 KiB read-only window. A read triggers a host-side wave-ROM fetch at the address formed from
`0x3922` (bank, bits 26:20), `0x3920` (page, bits 19:10) and the low 10 bits of the offset; the
byte is returned via the low half of the `0x3910` latch. The firmware never writes here.

---

## External memories (not in the host aperture)

| memory | size | notes | conf |
|---|---|---|---|
| **Wave ROM** | 8 MB (4 × 2 MB in the JV-1080) | 24-bit address bus, 8 chip selects (`device_rom_interface<27>`), custom floating-point DPCM; descrambled at load | [M] |
| **Effects DRAM (ERAM)** | 256 KB (2 Mbit) | circular delay buffer for chorus/delay/reverb; addressed *internally* by DSP `opF` ERAM ops (region bases Chorus+0x0000 / Reverb+0x1000 / Delay+0x8000). Not host-addressable. | [S ISA] / [M] |

---

## Cross-references
- Instruction word decode & DSP semantics: `jv1080/re/xp_dsp_isa_decoded.md`, `xp_dsp_isa.md`.
- Silicon-confirmed facts (readback latch, run/stop, config, CRAM format, ramp): `jv1080/re/XP_FACTS.md` C2–C18.
- Live per-second state dump: driver `xp_dump_cb` → `xp_dsp_dump.txt` (reads `roland_xp::dbg_peek`).
- Device model: `src/devices/sound/roland_xp.h` (map comment) and `roland_xp.cpp` (`read`/`write` dispatch).
