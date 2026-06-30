# Feasibility & Design Memo: MIDI-Sysex Debug ROM Mod for the JV-1080 XP Chip

**To:** Project maintainer
**Re:** Adding a sysex peek/poke/dump debug facility by modding the external program ROM (ic20)
**Basis:** Firmware-hook feasibility analysis + prior-art/alternatives analysis + your existing `prom_dumper` and Arduino bus-probe work

---

## 1. Verdict

**Do it — but scope it tightly, and only for the questions the Arduino probe physically cannot reach.**

The three tools you have are **complementary, not competing**. Cheapest-first, the right order to attack the open questions is: (1) capture the **factory EFX self-test** bus trace on stock hardware (zero build cost, exact DSP-execution oracle); (2) use the **Arduino probe** for static register-map and SCCore-path validation (zero firmware risk); (3) reserve the **debug-ROM mod** for the behavioral/timing questions the other two cannot observe. The mod is **not redundant**: it is the only one of the three that can see real-time, on-chip behavior driven by the real SH7034.

### What the debug-ROM mod can UNIQUELY do (vs. Arduino probe + factory test)

| Capability | ROM mod | Arduino probe | Factory test |
|---|---|---|---|
| Exact CPU register-write **sequences & bus timing** (real SH7034 driving CS4) | ✅ only tool | ❌ crude 9-NOP hand-timed isolated transactions | ✅ but only one canned program |
| Observe **XP IRQ7 back to the CPU** (reasons 4/7/8) | ✅ only tool — INT goes to the SH7034, which runs your code | ❌ INT pin not wired to Arduino | ⚠️ indirectly, fixed program only |
| Capture **DSP commit-boundary / completion timing** in-situ | ✅ only tool | ❌ no continuous sample clock, can't run DSP | ⚠️ one fixed 256-slot program |
| Drive **note-on / voice-start through the real firmware path** while leaving the engine otherwise quiescent | ✅ only tool | ❌ can't invoke firmware | ❌ |
| Real continuous **32 kHz sample clock** running while you observe | ✅ (runs on the live chip) | ❌ | ✅ |
| **Arbitrary** address peek/poke of the 0x4000 reg space | ✅ | ✅ | ❌ fixed walking pattern only |

### Be honest: where the Arduino probe (or factory test) is the BETTER tool

- **Static register-map characterization** — the open/passive banks (0x3380, 0x3880, 0x3908–0x390f, 0x3914, 0x391c, 0x3924–0x3932, 0x3930/0x3932, init-only and zero-reference banks): the Arduino probe nails these **with zero firmware risk and zero flashing**. Use it.
- **The SCCore-derived "easy" validations** — random sequence at `0x320c`, IRAM3 direct-current-write cancellation, wave-ROM aperture/latch protocol: probe-first. (Caveat: the probe's wave-ROM read protocol is self-labeled *speculative* — cross-check before trusting `dump.bin`/`dump2.bin`.)
- **A free, exact DSP-execution oracle** — the **factory EFX self-test** deterministically maps seeds `000000/555555/aaaaaa/ffffff` → expected low bytes `00/55/aa/ff` on real silicon, **with no mod at all** (just the documented key chord on a stock unit with disposable NVRAM). This is the single highest-leverage thing to capture *first*, because the emulator's test currently **passes falsely** (all-zero outputs accepted by the firmware's nonzero-only compare).

**Bottom line:** Build the ROM mod, but as the *third* tool, aimed squarely at the IRQ7-reasons / DSP-commit-timing / real-cadence questions — which are exactly the blockers keeping the chip at `MACHINE_NO_SOUND`. Don't use it for what the probe does cheaper and safer.

---

## 2. Design of the Sysex Debug Protocol

**Use a PRIVATE universal-sysex opcode, NOT Roland DT1/RQ1.** The high-level Roland model-ID (0x6A) DT1/RQ1 command parser was **not located** in ic20 (the `0x6A` immediates cluster in effects/`rfx_worker` code, not a parse table; the buffered message is handed to an untraced task via RTOS event `0x0a000200`) — *confidence: medium / OPEN*. Extending the standard map would require RE-ing that parser. A private opcode spliced at the low-level system-message handler **sidesteps the model-ID parser entirely**. Downside, stated plainly: standard Roland editor software will not recognize it — fine for a debug tool.

### Splice point (high confidence)

`midi_task` (ext `0x0a02882e`) reads bytes via `midi_receive_queue_byte` (`0x0a0288bc`) and dispatches on an **8-entry jump table at `0x0a059cd0`**, indexed by `(status − 0x80) >> 4`. **Index 7 = all `0xF0–0xFF` status = the sysex handler at `0x0a028948`.** That handler accumulates `0xF0..0xF7` into a 256-byte RAM buffer at **`0x0901e954`** (base ptr `0x0a028998`, max-len word `0x0100` at `0x0a028a7a`) then signals RTOS event `0x0a000200`.

**Hook:** repoint the single 32-bit table entry 7 (file offset **`0x59cec`**, currently `0x0a028948`) to a custom dispatcher in free ROM. The dispatcher inspects the just-buffered message at `0x0901e954` for our private opcode; on no-match it `JMP`s to `0x0a028948` so normal MIDI is untouched. No instruction-length juggling — it's a pointer repoint.

### Wire format (private opcode)

```
F0 7D <op> <args...> F7        ; 0x7D = non-commercial/educational manufacturer ID
```

| op | name | args | action |
|----|------|------|--------|
| 0x00 | ENGINE_FREEZE | `<flag>` | set/clear debug freeze byte (see §below) |
| 0x10 | POKE16 | `<reg_hi><reg_lo> <d_hi><d_lo>` (7-bit nibblized) | `MOV.W Rd,@(0x0c000000+reg)` |
| 0x11 | POKE8  | `<reg><data>` | `MOV.B Rd,@(0x0c000000+reg)` |
| 0x20 | PEEK16 | `<reg_hi><reg_lo>` | reply with `MOV.W @(0x0c000000+reg)` |
| 0x21 | PEEK_DSP | `<reg>` | latch protocol: trigger `0x0c00320c`, read `0x0c003912:0x0c003910` (mimics `xp_read_busy_random` `0x00009c00`) |
| 0x30 | DUMP | `<start_hi><start_lo> <count_hi><count_lo>` | stream reg range out as sysex (chunked) |
| 0x40 | NOTE_ON_THRU | `<chan><note><vel>` | drive the real firmware note path once, engine otherwise frozen |
| 0x50 | RUN_N | `<n_hi><n_lo>` | (optional) release freeze for N samples then re-freeze |

Nibblize all data bytes (`hi = byte>>4`, `lo = byte&0x0f`) so no payload byte can be ≥0x80 and trip the status decoder. Reserve a **debug-state byte + sysex TX/RX scratch** in work DRAM near the existing MIDI buffers (e.g. adjacent to `0x0901e954`), or in battery NVRAM `0x02380000` if you want persistence.

### (i) Suspending the voice/TG engine — what the firmware hook actually allows

**Critical constraint (high confidence):** the periodic voice/TG update pass and *every* per-voice XP writer live in the **internal mask ROM ic15** (`0x0000xxxx`), which you **cannot reflash**. `xp_voice_service_task` (`0x00009fa8`) waits on RTOS event bit 4, then calls `synth_update_parts` (`0xa148`) and `xp_update_all_voices` (`0xa268`) — all internal. You **cannot stub the periodic pass from ic20.**

**What you CAN do (high confidence):** note→voice rendering is decoupled. The external `midi_note_on` (`0x0a010728`) and the sequencer channel-voice handlers build a synth event and hand it to the internal task via RTOS event/RAM queue — the external ROM **never directly calls** the internal commit/start functions (binary search for 32-bit literals to `xp_commit_note_voices`/`xp_start_voice`/`xp_commit_one_voice`/`xp_update_all_voices` in ic20 → **zero hits**). So:

> **Patch `midi_note_on` (`0x0a010728`) and the sequencer note-on dispatch (reached via channel-voice table `0x0a0104a6`) to early-return when the debug-freeze byte is set.** This blocks all *new* voice allocation, so no new XP voice-start writes are issued.

**Honest limitation (OPEN/RISK, high confidence):** this is *"suppress new voice allocation,"* **not** *"halt the engine."* The internal 100 Hz service pass and `xp_irq_dispatch` (IRQ7, internal `0x1158`) keep running — you cannot stop them from ic20. The pass is woken by the GA status-8 event (driver `ga_control_tick`, 100 Hz). **Mitigation:** issue all-notes-off + set freeze, so with no active voices the internal pass writes only **idle/zero state** over the register file, leaving it quiescent enough to poke. For a guaranteed-static file, do POKE/PEEK between passes or accept that the pass will reassert idle values on banks it owns.

### (ii) Reading XP registers and the DSP readback latch

XP access is **plain memory-mapped MOV** against the A27-set mirror `0x0c00xxxx` (= driver CS4 `0x04000000`). **No helper function to hook** — each routine hardcodes its bank base.

- **Normal-reg peek:** `MOV.W @(0x0c000000+reg)`.
- **DSP / IRAM3 / busy-random readback:** use the latch protocol — trigger via `0x0c00320c`, then read `0x0c003912` (status/high) and `0x0c003910` (data). Copy the idiom directly from `xp_read_busy_random` (`0x00009c00`, pool: `0x9c14=0x0c00320c`, `0x9c18=0x0c003912`).
- **Poke:** `MOV.W/MOV.B Rdata,@(0x0c000000+reg)`.

**Lift the proven idiom from `factory_xp_test_cram` (`0x0a0080f8`)** — it already writes a walking pattern into XP CRAM at `0x0c002c00` (`MOV.W R3,@R7`) and reads back via the latch at `0x0c003910` (pool: `0x008184=0x0c003910` readback, `0x008188=0x0c002c00` target, `0x008180=0x5555ffff` pattern). Its siblings (`pram/iram1/iram2/iram3`, and `factory_xp_execute_test_program` `0x0a008cea`) cover the other banks. These are arbitrary-address-incapable (fixed walking pattern only), so **lift the MOV idiom into the new handler** rather than driving the factory UI.

### (iii) Streaming a register-space dump over MIDI

`DUMP` (op 0x30) walks `[start, start+count)` reading `MOV.W @(0x0c000000+reg)`, nibblizes, and emits chunked `F0 7D 30 ... F7` frames (keep each frame ≤ a couple hundred bytes to respect the firmware's 256-byte buffering and avoid MIDI-out backpressure). For the full space, `count=0x4000`.

**TX path (medium confidence — VERIFY BEFORE BUILDING):** `midi_transmit_bytes` (`0x0000bc04`) is **internal** (callable but internal). The external sequencer/MIDI code already drives SCI0 TX; the driver turns completed SCI0 bytes into the 31.25 kbaud stream (`midi_tx_byte`, `roland_jv1080.cpp:197–207`; `isr_sci0_transmit` `0x00000f02`). **The exact external TX entry was not pinned down.** Pin it down by mimicking how the sequencer/note path queues TX before relying on the reply path.

### (iv) Single-step / run-N-samples (optional)

You cannot truly single-step the internal engine from ic20. The practical approximation is `RUN_N` (op 0x50): clear the freeze byte, let the GA status-8 100 Hz pass run for N ticks (count GA ticks in your stub), then re-assert freeze and `DUMP`. This gives a coarse "advance the engine M ms, then snapshot" capability — enough to watch a voice's pitch/TVF/TVA trajectory evolve, but **bounded by the 100 Hz cadence, not per-sample.** For true per-sample observation you depend on IRQ7 events, not single-stepping.

---

## 3. Implementation Plan

### Patch points in ic20 (all high confidence)

1. **Sysex splice:** file offset `0x59cec` (jump-table entry 7) `0x0a028948` → `<custom_dispatcher>`.
2. **Engine freeze:** early-return guards in `midi_note_on` (`0x0a010728`) and the sequencer note-on dispatch (via channel-voice table `0x0a0104a6`), gated on the debug-freeze byte.
3. **New code payload:** the custom dispatcher, poke/peek/dump/reply routines, and RX/TX scratch.

### Free ROM space (high confidence)

Run-length scan of ic20 found ~175 KB of `0xFF` padding (runs ≥64B):
- **`0x0a059d48`, len `0x8158` (33 KB)** — *immediately after the MIDI dispatch table.* **Co-locate the dispatcher here** (cache/locality, and it's right where `midi_task` already operates).
- **`0x0a0e28d5`, len `0x1d709` (≈120 KB)** — upper-quarter unused; use for larger buffers/tables if needed.
- `0x0a000882`, len `0x97e` (2.4 KB) — small overflow.

### Trampoline strategy

SH-1 sites read their target via a 4-byte PC-relative pointer literal, so each hook is a **single 32-bit-literal repoint** — no instruction-length juggling:
- Sysex: repoint `0x59cec`.
- For `midi_note_on`/sequencer early-returns, prefer repointing the literal the caller uses to reach the handler (so the original prologue is preserved) — the stub checks the freeze byte and either RTS-es early or `JMP`s to the original entry. Confirm each site's literal against the live image before cutting.

### Build / flash (reuse `prom_dumper` toolchain — proven)

`prom_dumper.bin` already demonstrates the full pipeline: a 1 MB modified program ROM whose **RTOS task-descriptor header is byte-identical to stock `prom_orig.bin`**, with appended SH2 code, hand-assembled SH2 snippets (`dumper_bin*.bin`), and a captured DWF logic trace. Reuse it directly:
1. Cross-assemble the new SH2 handler/stub blocks; place at `0x0a059d48` (+ `0x0a0e28d5` for buffers).
2. Apply the literal repoints (`0x59cec`, note-on guards) to a copy of `prom_orig.bin`.
3. **Keep the RTOS header byte-identical** to stock (the proven boot invariant).
4. **Recompute any ROM self-checksum/CRC** — the factory Memory/Initialize tests and possible boot checksum over ic20 were **not audited**; if one exists, the mod will fail self-test until the checksum is fixed. *(Risk — audit this before flashing.)*
5. Flash with the existing `prom_dumper` socket/burn flow. **Preserve `prom_orig.bin`** for instant rollback.

### Test in MAME FIRST (before burning hardware)

The emulator already loads ic20 and shadows all XP writes in `m_xp_regs` (`roland_jv1080.cpp:102,229`), giving a ground-truth check. Workflow:
1. Build the modded ic20, point the MAME driver at it.
2. Feed the private sysex over the driver's MIDI IN.
3. Confirm `xp_w` sees your pokes on CS4; confirm `PEEK`/`DUMP` replies leave via `midi_tx_byte` (`roland_jv1080.cpp:197`).
4. Use the existing Lua trace harness (`jv1080/re/lua/xp_trace.lua`, `factory_test_probe.lua`) to verify the freeze byte actually suppresses new voice writes.
5. Only after MAME passes end-to-end, burn hardware.

---

## 4. Risks & Concrete Experiment List

### Risks

- **You cannot truly halt the engine from ic20.** Internal 100 Hz pass + IRQ7 service keep running; the mod only *suppresses new allocation*. For a static register file, combine all-notes-off + freeze and accept that the internal pass reasserts idle/zero state on banks it owns. *(high)*
- **TX reply path unconfirmed.** External MIDI-out entry not pinned to a single function; `midi_transmit_bytes` is internal. **Verify the reply path before relying on it.** *(medium)*
- **ROM self-checksum not audited.** Recompute any CRC over ic20 or the unit may fail self-test/boot. *(medium)*
- **DT1/RQ1 parser not located** — only matters if you later want standard-Roland-map compatibility; the private opcode avoids it. *(medium/OPEN)*
- **Static-RE provenance.** All addresses are from a static Ghidra/unidasm pass + your notes; **re-confirm each cited literal/address against the live image before cutting the patch.**
- **Partially destructive ops.** Factory test runs walking NVRAM/SRAM tests; flashing can brick. Use **disposable NVRAM** and **keep `prom_orig.bin`**.

### Concrete experiments the modded ROM uniquely enables (priority order)

These target the current critical-path blockers (DSP execution model + IRQ7 sources), which the Arduino probe and factory test **cannot** reach:

1. **IRQ7 reason sourcing (4/7/8) — highest priority.** Install an IRQ7 hook path observable from your stub (the SH7034 receives INT; your code runs there). Program a known voice/DSP image, kick the engine for one note, and **log every IRQ7 with its reason field + timestamp (sample count)**. This is the *only* way to prove reasons 4/7/8 — reason 7 is explicitly forbidden from being synthesized in the emulator until a real DSP commit boundary is observed. (Emulator today only ever raises reason 5.)
2. **DSP commit-boundary timing.** Upload a known PRAM/CRAM program, seed IRAM3, write the trigger (`0x3916`), and **stream back `0x3910`/`0x3912`/IRQ status over time** — capture *when* results commit relative to the sample clock and the 100 Hz cadence. Pins accumulator-store/pipeline timing the factory test cannot (fixed program only).
3. **Real factory-EFX outputs in-situ.** Drive the factory execution program via your poke primitives with seeds `000000/555555/aaaaaa/ffffff` and `DUMP` the result slots — capture the true `00/55/aa/ff` low bytes the emulator currently fakes as zeros. Cross-check against the stock-unit factory-test trace.
4. **Busy-handshake true duration.** Issue a real voice command via the firmware path, then **poll the busy status at sample resolution** to measure the true completion time (emulator hardcodes `m_command_busy_reads = 1`).
5. **100 Hz cadence phase/jitter.** With the engine frozen, time the GA status-8 wakeups against the sample clock from your stub to confirm exact phase and jitter (emulator reconstructed this but flagged phase/jitter as needing a physical IRQ trace).
6. **Real CPU write sequence/timing capture.** Let one note play through the real firmware (op 0x40 NOTE_ON_THRU) with logging on, and capture the **exact ordered CS4 write sequence + inter-write timing** the SH7034 emits — ground-truth for the voice-start register choreography.

For the *static* open items (random `0x320c`, IRAM3 direct-write cancellation, register-map gaps `0x3380/0x3880/0x3908/0x3914/0x391c/0x3924–0x3932`, wave-ROM aperture), **use the Arduino probe** — cheaper and zero-risk. Reserve the ROM mod for items 1–6 above.


---

## Addendum — VERIFIED against the ic20 image (supersedes the §2(i) "cannot freeze the periodic pass" limitation)

A direct scan of `roland_r00678167.ic20` confirms the hook points **and overturns the memo's claim that the 100 Hz voice pass cannot be stopped from ic20**:

**The RTOS task-descriptor table lives in the ic20 header.** 12-byte descriptors start at file offset `0x10`:
- Task 0 entry `0x0a02882e` is the 32-bit literal at file `0x10`.
- Task 8 (voice-engine service) entry `0x00009fa8` is the 32-bit literal at file `0x70` (= `0x10 + 8*0x0C`).

Therefore the periodic pitch/TVA/TVF rewrite **can be frozen with an ic20-only patch**: repoint the task-8 entry at file `0x70` from `0x00009fa8` to a custom body in free ROM that waits on the same RTOS event (copy task 8's own wait prologue) and calls the internal subroutine `xp_update_all_voices` (`0x0000a268`) **only when the debug-freeze byte is clear**. This is a true freeze of the periodic pass, not just "suppress new allocation."

**Recommended freeze = two ic20-only levers combined:**
1. Repoint task-8 descriptor entry (file `0x70`) → custom gated body. Freezes the periodic per-voice register rewrites.
2. Gate the note-on event source (see caveat below). Stops new voice allocation.

**Confirmed hook facts:**
- Sysex jump-table entry 7 at file `0x59cec` = `0x0a028948`, referenced **exactly once** (the table itself) → clean single-pointer repoint. The other 7 entries are `0x0a028ac8/8b38/8bc0/8d4a/8c30/8c8a/8cd2`.
- 33 KB `0xFF` free run at file `0x59d48` (`0x8158` bytes) — confirmed.
- Sysex-buffer base literal `0x0901e954` present at file `0x28998` — confirmed.

**Caveat (verify before cutting):** `midi_note_on` at `0x0a010728` is **not** referenced as a 32-bit literal anywhere in ic20 (0 hits). Either the address is slightly off or note-on is reached via the channel-voice dispatch table at `0x0a0104a6` rather than a pointer literal. Pin the real note-on gate site in the decompile before patching lever 2. (`xp_update_all_voices 0x0000a268` also shows 0 hits in ic20, as expected — it is internal-ROM code, not referenced by the external image.)
