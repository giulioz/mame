# XP DSP — confirmed-facts ledger (single source of truth)

**Purpose:** stop the "confirm X, then reason from not-X" failure loop. This file is the
*only* authoritative statement of what we know. Everything else (XP_HARDWARE_DEBUG.md,
xp_dsp_isa.md, chat) is narrative and may drift — when in doubt, THIS wins.

**Tags:** `[C]` proven by cited evidence · `[E]` expected / strong prior, NOT proven on the XP ·
`[H]` hypothesis / guess · `[R]` retracted (was believed, now disproven).

**Working rules (follow every time):**
1. Before proposing an experiment or a conclusion, list the assumptions it rests on and tag
   each `[C]/[E]/[H]`. **If any assumption contradicts a `[C]`, stop and rethink.**
2. After a result, write one line: what it proves, and move the affected ledger entries.
3. Never write an `[E]` or `[H]` as if it were `[C]`, in code comments or prose.
4. When something fails, FIRST ask "did I violate a `[C]` or a known rule?" before inventing a
   new mechanism. (Most of our dead ends were self-inflicted rule violations.)

---

## [C] Confirmed (with evidence)

- **C1. Host access works.** MIDI-sysex PING/POKE/PEEK reach the XP at `0x0c00xxxx`. *(hardware bring-up)*
- **C2. DSP memory is read-only via a latch, not direct.** Reading a DSP address returns `0` on
  the bus but latches that word into host readback reg `0x3910`(lo16)/`0x3912`(hi16); you then
  read that register. CRAM(<0x3000)=16-bit; IRAM/PRAM(≥0x3000)=32-bit. *(peek(0x3400)=0 vs latch
  read=0x74c0; after peek(0x2c02), raw 0x3910=0x3d24)*
- **C3. DSP control registers are write-only** (read back `0` on silicon even though firmware
  writes them): `0x3908, 0x3914, 0x3916, 0x3924`. *(direct peeks returned 0)*
- **C4. No store-to-host needed.** Every IRAM/PRAM/CRAM word is host-readable via C2, so — unlike
  CSP/ESP — the DSP program needs no "send to CPU" instruction. *(follows from C2 working for all regions)*
- **C5. The SH firmware writes the DSP area only at boot.** All writes to CRAM/IRAM/PRAM/config
  land in t≈0.4–1.3 s; **zero** writes after t≈2 s while idle. *(MAME `xp_w` instrumentation, 20 s trace)*
- **C6. Notes and Program Change trigger a firmware re-upload** of the DSP program (rewrites
  PRAM/CRAM slots 0–103). So: **no note-on, no program change during experiments.** *(PRAM differs
  per patch; changes across PC)*
- **C7. `0x3916` write values (values confirmed; run/stop meaning is inference).** Boot writes
  `…0, 7, 0, 7` (ends at 7). The factory test-program executor writes `0, 4, wait, 0`.
  *(emulator trace; disasm of `factory_xp_execute_test_program` 0x0a008cea)*
- **C8. Config the firmware leaves set:** `0x3908=1c19 0x390a=1818 0x390c=1818 0x390e=0808
  0x3914=403f 0x3924=d200 0x3926=16d2 0x3928=0100 0x392a=0100 0x392c=0300 0x392e=0300`,
  reset bitmaps `0x3900-06=ffff`. *(emulator trace)*
- **C9. IRAM3 (`0x3200`) ramps only when a target is set.** The ramp engine moves `0x3200+4i`
  toward target `0x3300+2i` at rate `0x3928+`. A **direct** write to `0x3200+4i` seeds the current
  value and cancels its ramp; with `0x3300` zeroed it holds still. *(firmware map: "direct writes
  seed a current value and cancel its old ramp".)* So IRAM3 IS usable as an observable **iff you
  zero the `0x3300` targets first** — which is exactly what the factory EFX test does. Otherwise
  prefer IRAM1/IRAM2 (`0x3000`/`0x3100`).
  - **Encoding CONFIRMED from the SCCore binary** (`TGXpDsp_writeIramBp` @0x51ffd,
    `TGXpLspDsp_process` @0x5202d disassembled): target = **9-bit** value; host `0x3300` holds it raw
    (emulator masks `& 0x1ff`), host `0x3200` current is **Q22 = value<<13** at rest (target `0x1ff`
    → `0x003fe000`, just below unity `0x00400000` — matches the live dump). SCCore's internal copy is
    **Q15 = value<<6** (`writeIramBp` literally `shl esi,6`; writes/reads the same table @0x1A759D0).
    Step per update = **`current += (target-current) * rate / 65536`** (one-pole exponential approach;
    SCCore `imul …,0x300; shr 16; add`). Rate is per-16-slot group from `0x3928/2A/2C/2E`
    (`0100/0100/0300/0300`); SCCore hardcodes `0x300` for its one group. Our `update_iram3_breakpoints`
    (`roland_xp.cpp`) implements exactly this in Q22. (`xp_dsp_isa.md` IRAM3 section is verified correct.)
- **C10. Latch reads are reliable at idle, contaminated during active audio** (a note makes even
  PRAM reads return audio-like garbage). Keep the engine idle when reading. *(observed)*
- **C11. `0x39xx` control regs HANG the firmware when read via `read_dsp`** — the ≥0x3000 width-4
  trigger spans IRQ reg `0x3918`; read those with direct PEEK width 2 instead (power-cycle to
  recover from the hang). *(observed hang)*
  - **`0x3300` is NOT hazardous (isolation-tested).** A single read and four-in-a-row all succeeded,
    returning drifting IRAM3 ramp values (`0xffffd7→…d8→…e1→…ee`), and the JV pinged fine throughout.
    An earlier claim that `0x3300` hangs the firmware was WRONG (retracted). Run 1's `None` there was a
    transient (or cumulative) timeout, not `0x3300`'s fault.
  - **The full-DSP-dump hang cause IS isolated: a RETRY STORM.** `dump_dsp.py` with single-attempt
    reads (no retry) + abort-on-2-consecutive-failures completed the same ~770-read dump cleanly in
    23 s. So the earlier hangs were the `read_dsp` retry loop hammering the firmware after a transient
    fail — NOT `0x3300`, NOT read volume. Rule: **never retry a failed latch read; abort instead.**
  - **Safe to latch-read:** PRAM `0x3400`, CRAM `0x2c00`, IRAM1/2/3 `0x3000/0x3100/0x3200`, and
    `0x3300` (though it's just drifting ramp state, not useful to snapshot).
- **C12. A real running program (Stereo EQ) reads/writes IRAM1 and IRAM2.** Steady-state dump (boot
  default patch) = Stereo EQ (PRAM slots 0–103 match the ROM template `0x0a04080c` 104/104; live CRAM
  differs at slots 1,12–18 = computed coefficients). **IRAM1 and IRAM2 hold nonzero values at the
  *same* slots (0,3,16,17,19–22) differing only slightly per channel** — i.e. the L/R filter state of
  the stereo EQ. So the DSP does use IRAM1/2 as working memory, via the EQ's op+selector addressing
  (NOT the factory parallel channel, which targets IRAM3). `[H]` lead: the EQ's L/R address bit was
  **bit 9** (from the L/R XOR analysis), so bit 9 likely selects IRAM1 vs IRAM2. Reference dump:
  `jv1080/re/dsp_dump_steady.txt`. Tool: `debugrom/dump_dsp.py`.

- **C13. Poked programs EXECUTE — free-running, no kick needed (PROVEN).** `restore_dsp.py`:
  zeroed the whole DSP (IRAM1/2 verified = 0), poked back the captured Stereo EQ (PRAM/CRAM/IRAM3
  only), left `0x3916` at 7, no notes / no PC. After 0.6 s **IRAM1/2 re-developed 16 nonzero slots
  on their own** (`ffff43…`, same pattern as the live dump). Only the DSP program writes IRAM1/2,
  and the firmware is idle (C5) → the poked EQ ran and rebuilt its filter state from zero. This
  confirms the DSP free-runs PRAM continuously; **just poke and it runs — do not touch `0x3916`.**
  (Supersedes former E1.) **Also AUDIO-confirmed:** after restore, played notes produce correct
  Stereo EQ sound; and the JV emits garbage noise *during* the poke-by-poke upload (it's running the
  partially-written program live) — independent proof of continuous free-running. Tools:
  `debugrom/{dump_dsp,restore_dsp}.py`, `dsp_snapshot.py`.

- **C14. CRAM coefficient format CONFIRMED (14-bit mantissa + 2-bit exponent).**
  `value = sign_extend(raw[13:0]) << shift / 8192`, where `shift = [0,1,2,4][raw[15:14]]`
  (exp field bits 15:14 → ×1/×2/×4/×16). Proof: sweeping the Stereo-EQ Low-Gain param produces a
  coefficient that steps *smoothly* across the exponent boundary (`0x1fa0` exp0 = 0.988 → `0x5030`
  exp1 = 1.012) crossing 1.0 at gain-center — only consistent with this decode. Also confirmed:
  a band's L/R coefficient **values** are identical (R slot = L slot + 36). So a MAC (`op0`:
  `acc += mem × CRAM[slot]`) multiplies by a coefficient on this scale (`0x5000` = +1.0). Method +
  full parameter→slot map: `jv1080/re/effect_param_map.md`.

- **C15. SCCore has TWO transpiles — `lspproc_*` is the LSP (useless for XP), but
  `SystemEffects_process` IS the XP and DID decode the ISA (REFINED).** Earlier this entry claimed
  "SCCore is not a direct XP decode." That is right for the `lspproc_*`/`Lsp*` path (`lspproc_eq8`,
  `LspLSFWrite`, `LspReadCB`, …) — those are the **LSP**, a different chip in the same family; a
  structural reference only (`jv1080/re/sccore_dsp_reference.md`). BUT `SCCore00.dylib` *also* contains
  `SystemEffects_process` @0x2ca14, a native transpile of the **XP** system effects
  (chorus/delay/reverb/EQ), paired with the XP bytecode `hack_scva/.../scgsMaster.txt`. Aligning
  **those two** decoded the XP instruction ISA (see C16/C17 and `jv1080/re/xp_dsp_isa_decoded.md`).
  Still true: SCCore does not *run* the XP bytecode at runtime (`setPRAM/setCRAM` are setters,
  `TGXpLspDsp_process` is the IRAM3 ramp engine) — it runs the native transpile instead — but the
  transpile+bytecode pair is a legitimate Rosetta stone, cross-validated against the JV's own Stereo EQ.

- **C16. Instruction field layout — op = PRAM bits [15:12] (decode B), decode A refuted.** The 32-bit
  PRAM word is `[31:25]=ext / [24]=wb / [23:16]=hi-addr byte / [15:12]=op·region / [11:9]=store-sel? /
  *(field boundaries SUPERSEDED by C23/C24: [15:14]=store-control, [13:0]=addr; kept as history)* /
  [8:0]=low-select`. `[15:12]` takes exactly 7 values `{0,4,5,7,B,C,F}` (histogram
  `{0:36,4:4,5:73,7:82,B:22,C:8,F:31}` over all 256 nonzero scgsMaster slots). The rival "op=[23:16]"
  is REFUTED: `[23:16]` spans 37 byte values (op7 alone spans 23), and in the reverb allpass it walks
  as a **+3 counter in lockstep with lo-addr +0x40** (slots 184/187/190/193 → abyte 0x8a/8d/90/93) —
  i.e. it is an *address channel*, not an opcode. The opcode nibble = top nibble of the destination
  region: the same transpile op `state+=acc·β` lands at op0/0x011 (chorus slot 95) vs op7 (delay/reverb
  slots 146/171). `[24]=wb` (store accumulator) on 34 slots. This matches the low-half layout
  `xp_dsp_isa.md` already had; my earlier scout's lean toward decode A was wrong. Reproduced this
  session via `scc.pram_words()`. Full spec: `jv1080/re/xp_dsp_isa_decoded.md`.

- **C17. Execution model = accumulator MAC; input one-pole is the alignment peg.** Each slot is
  `acc = acc·CRAM[slot] + mem[addr]`, or `mem[addr] = acc·CRAM[slot]` when wb=1. The shared input
  one-pole `acc=in·α; state+=acc·β; acc+=state` appears **exactly 3 times** (chorus/delay/reverb) with
  **α=`0x1ff0`=+0.998047** (slots 93/144/169) and **β=`0x3fe0`=−0.003906** (slots 95/146/171) — EQ is a
  separate biquad with none. `process_EQ` @0x2d088 is an additive Direct-Form-I biquad (`mulss`/`addss`
  only, **no subtraction** → the JV "NEGMAC" op7 sign is a per-slot sign, not proven here). CRAM format
  (C14) is now confirmed **byte-for-byte** from `TGXpDsp_setCRAM` @0x51e03 (decode @0x51e57–0x51e91,
  shiftLUT @0x19ebf70={0,1,2,4}, scale @0x99a688=1/8192). Region map: op0=regs/scratch, op5=buses+tap
  stream, op7=MAC datapath, opB=coeff-carrier, opC=IRAM, opF=ERAM (64K circular; bases Cho+0/Rev+0x1000/
  Del+0x8000), opD=output writer (JV-only). Full spec + confidence tags: `jv1080/re/xp_dsp_isa_decoded.md`.

- **C18. The "mixer coefficients" are 4 per-voice SEND banks at `0x3A00-0x3BFF`; the full firmware
  write-footprint is now enumerated.** Each send word = `level[15:6] | bus[5:0]` (10-bit level, 6-bit
  destination bus); 4 banks × 64 voices: send0 `0x3A00`, send1 `0x3A80`, send2 `0x3B00`, send3 `0x3B80`.
  At idle the boot patch routes every voice send0→bus6, send1→bus7, send2→bus8, send3→bus9 at level 0
  (voices muted, no notes). Effect **return/dry-wet/master** levels are NOT separate registers — they
  are CRAM system-region coefficients (slots 104-255) consumed by the DSP MAC ops. **Firmware write
  footprint** (emulator `xp_dump_cb`, idle, 4 s): `0x1100-0x1EFF` (voice ramp dest/start pages),
  `0x2000-0x21FF` (filter topology / tvf_q start), `0x2C00-0x2E3F` (CRAM = 288 slots exactly),
  `0x3000-0x32FF` (IRAM1/2/3 seed), `0x332C-0x333F` (a few IRAM3 targets), `0x3400-0x387F` (PRAM = 288
  slots exactly), `0x3900-0x392F` (config, sparse), `0x3A00-0x3BFF` (mixer sends). It writes **nothing**
  in the reserve gaps (`0x2E40-0x2FFF`, `0x3880-0x38FF`, `0x3930-0x39FF`) or the wave aperture
  (`0x3C00-0x3FFF`); voice control/sample pages `0x0000-0x03FF` are written only on note-on. Tool:
  the driver's per-second `xp_dump_cb` → `xp_dsp_dump.txt` (reads live device state via
  `roland_xp::dbg_peek`, so IRAM3 shows ramp-evolved values). IRAM3 current confirms ramps: target
  `0x1FF` → current `0x003FE000` (= `0x1FF<<13`), slot2=`0x00400000` unity, slot3=`0x1` RNG-enable.

- **C19. ERAM two-word tap-address encoding CONFIRMED on hardware; unit = 1 sample @ 32 kHz.**
  Live Triple Tap Delay sweep (front-panel delay-time edits, PRAM pairs read back):
  `addr[15:9] → P[n][22:16]` (bit23=1 pair marker) and `addr[8:0] → P[n+1][24:16]` — where
  **bit24 of the second word = addr[8]** (an ADDRESS bit there, not wb; C16's wb reading applies to
  compute slots only). Smoking gun: the 205→210 ms step rolls lo9 `0x1A0→0x040` with hi-byte
  `0xBC→0xBD` (carry across the 9-bit boundary). All 10 sweep points satisfy
  **`addr = 0x6000 + ms·32`** → tap addresses are in samples at **32 kHz**; this RFX delay's region
  base is `0x6000`. The channel is genuinely parallel to the ALU: the left tap's pair
  (`00BC0000 01000000`) has ZERO low halves (pure address carrier), while the center tap's rides on
  live opB/op7 words. Second-word bit26 = `1` on the data-carrying pair, `0` on the carrier pair
  (role open `[H]`). NOTE: bit23 is NOT a reliable *static* pair-finder (fails in scgsMaster) —
  locate pairs dynamically. *(center-tap data: 200ms `00BCB4C0 05007021` → 0x7900 … 1000ms
  `00EEB4C0 05007021` → 0xDD00.)*

- **C20. Minimal RFX skeleton works: op5 bus-in and opD out are pinned (opD semantics CONFIRMED).**
  A hand-built passthrough program on the JV (`xp_manual_eq.py` PASSTHROUGH) makes audio:
  `op5 sm1 sel0x080` reads EFX input bus **L** (`sel 0x080-0x0BF`; R = `0x0C0-0x0FF`), then
  `opD sm2 sel0x140 ext4` (= word `0x0800D540`) writes EFX out **L** and `opD sm2 sel0x180`
  (`0x0000D580`) out **R** — copying bus-in L to both outputs. So opD = output send (closes the
  former open question); `0xD500`/`0xD5C0` appear to feed the chorus path `[H]`. Also observed:
  the op0 `sel0x021 cram0xE000` (−16.0) + `op7 sm6 sel0x1B0` input-conditioning pair from the EQ is
  needed in the input stage (volume/scaling, exact role `[H]`).

- **C21. Programs are relocatable; the EQ L/R mirror is pure address arithmetic; op4 pins to
  output-assign.** (a) The whole system program (ROM slots 104–255) re-uploaded **word-identical at
  slot 61** runs correctly (reverb/chorus work — all the C20 nop-kill tests were done in this
  relocated copy). PRAM position = ordering only. (b) XOR over all 36 ROM-EQ L/R pairs: 22× `0x200`
  (datapath op7/opF differ ONLY in bit9 → L=`0x4xx`/R=`0x6xx` state banks), 9× identical (shared op0),
  1× `0x40` (op5 bus-in L/R = bit6), 1× `0x080000C0` (opD out), 2-3× stray **ext bits (bit26/27) only
  on the L-channel words** (role `[H]`). Datapath L/R stride 0x200 + bus stride 0x40 ⇒ JV evidence now
  **tilts `[11:9]` toward flat address bits** (the scgsMaster selector-recurrence remains the
  counter-signal; the differential sweep in Q1-next still decides). (c) Nop-kill/annotation map:
  `55B0` = dry feed (nop → dry gone), `D489`/`D1B0` = reverb input feeds (nop → no reverb),
  `4021`/`40E5` cram `1FFF` = **EFX OUT assign L/R — op4's first semantics** (nop L → reverb goes
  mono), `C001`/`C0F0` = reverb input state L/R `[H]`, `D289` main out R `[H]`, `D4C5` reverb out L
  `[H]`. (d) Sweep-confirmed coef roles: chorus rate `F19F`=0x0432, depth `0014`/`0011`=0x0183, delay
  `C0EF`/`002F`=0x8001 (int tap-offset decode), EFX-OUT→mix/reverb `F011`=0x1FFF, →chorus `5423`.
  Full tables: `xp_dsp_isa_decoded.md` §9. Tools: `xp_manual_eq.py` (line_s/show disassembler,
  bus_in/efx_out, eram_pair, cram_encode/decode).

- **C22. ALU/accumulator model cracked by single-instruction hardware probes (2026-07-02).**
  Manual programs (`xp_manual_eq.py`) poked one/two instructions + a store, reading IRAM:
  - **The low 6 bits of the 12-bit low-address are a COLUMN (operand/mode) select** for compute ops;
    `[13:6]` is the word address. Column truth (op0, cram C, acc A; all EXACT):
    `0x00-0x03` keep · `0x04/0x05` zero · `0x07/0x08` **negate** (−A) · `0x0F` **A + C** ·
    `0x18` **trunc(A·coef) − A** (MAC, addend −A) · `0x1F` **load C** · `0x3F` **C − A** ·
    `0x2F` = −C·0x3FF exactly (structure open). Prev-established `0x30`-style register columns
    and the EQ's `0x1f` unity loads all reinterpret under this split.
  - **Accumulator = 24-bit signed, SATURATING** (both `0x7FFFFF` and `0x800000` observed).
  - **Const-load decode (column 0x1F) = `sat24( sext15(raw[14:0]) << (raw[15] ? 13 : 0) )`** —
    6/6 observations fit (`0x0123→0x123`, `0x3456→+0x3456`, `0x4321→−0x3CDF`, `0xF123→sat`).
    This REFUTES the SCCore-doc int-decode (`sext14`) for this column, and coexists with C14:
    **CRAM decode is per-COLUMN** — multiply columns use C14 float, const/address columns use
    the sext15/<<13 int form.
  - **Store ops: nibbles 0xC-0xF write the accumulator to IRAM.** For op&3∈{0,1} (C/D) the flat
    model `addr14 = (op[1:0]<<12)|(sm<<9)|sel`, `IRAM entry = addr14>>6` fits EXACTLY
    (C/0x000→0, C/0x080→2, D/0x000→64) → op[1:0] looked like top address bits
    (SUPERSEDED by C24: bit12 has no observable effect; only bit13 proven). At sm=0, C and D each wrote
    BOTH iram[k] and iram[k+64] → mono write hits both L/R banks `[H]` (EQ's sm=2/3 = single-bank).
    op E/F rows are inconsistent under the flat model and appear **CRAM-dependent**
    (F/sel0/cram0→iram[32] vs F/sel0→iram[128]) → ERAM-page addressing with coefficient
    participation, still open.
  - **Free-running accumulation caveat:** a lone `+= const` column accumulates every pass
    (0x123/pass saturates in ~0.9 s @32 kHz) — explains "saturation" readings in single-instruction
    tests; always reset acc per pass (load column) before interpreting stores.
  - `[H]` The `[13:6]|[5:0]` split mirrors the mixer-send word (`level[15:6]|bus[5:0]`) —
    the 64 columns are plausibly the 64 TDM bus/voice slots.
  Full tables + methodology: `xp_dsp_isa_decoded.md` §3-§5.

- **C23. The PRAM low-half bit split is REVISED: `[15:14] class-verb | [13:0] addr14`.**
  Consequence of C22 (bit13 measured; bit12 later found unobserved — see C24): the "4-bit opcode" dissolves —
  old nibble = `class[1:0]‖addr[13:12]`, old `sm`/`sel` = address bits. Classes:
  `00` = compute-A (op0 family, 36 scgsMaster slots), `01` = compute-B/MAC (op4/5/7, 159),
  `10` = compute-C (opB, 22, DMAC candidate), `11` = **STORE acc** (opC/D/E/F, 39 —
  hardware-proven, the only class using all four addr-tops). The **9 never-seen op values
  {1,2,3,6,8,9,A} are exactly the unused (class, addr-top) combos** — resolves the opcode-holes
  mystery. **The `[11:9]` keystone is ANSWERED: word-address bits** (EQ L/R mirror bit9 = word+8;
  the scgsMaster "selector recurrence" counter-signal dissolves). op0/op7 share the same column
  vocabulary (col 0x30 dominant in both) ⇒ columns are ALU operand selects. NOTE (later refinements):
  [15:14] = STORE CONTROL, not a general opcode. st=3 = store (C24/C26), **st=1 = read/compute**
  (`st1 col 0x00` = `acc += mem`, proven C31/§3b); st=0/st=2 verbs still open — decisive A/B pending
  (`xp_dsp_isa_decoded.md` §3/§11).

- **C24. Store ruleset corrected (bit12 UNOBSERVED — "op[1:0]=address top" was overstated); the
  chorus LFO is a software saw in IRAM3.** (2026-07-02 probes, 9/11 rows bit-exact:)
  - **bit13=0**: st3 stores **dual-write `IRAM1[k]`+`IRAM2[k]`**, `k = addr[11:6]` (6 bits) —
    5/5 points (`w0x00→{0,64}`, `w0x02→{2,66}`, `w0x20→{32,96}`, `w0x40→{0,64}`, `w0x7F→{63,127}`).
  - **bit13=1, cram=0**: **single write at `k+32`** — 3/3 (`0x80→32`, `0xC0→32`, `0x8F→47`).
  - **bit13=1, cram≠0**: RETRACTED (2026-07-03, M3 gentle rerun) — the destination is **NOT
    cram-steered**; b12=1 store → **IRAM3[k]** for ALL cram (0,1,4,0x10,0x40,0x100 all → IRAM3[15]).
    The old `F1F0→134 / F3F0→136` offsets were a **ramp-engine decay artifact** (IRAM3 read
    mid-decay because the 0x3300 targets weren't zeroed first, C9). Zeroing targets → clean store.
  - **bit12 has NO observable effect in ANY experiment** (`C000≡D000`, `E000≡F000`) — the C22/C23
    claim "op[1:0] are literally the top two address bits" is DOWNGRADED: only bit13 proven;
    bit12 = address/control/unused, open (Giulio's "stray MSB" caveat).
  - **Saw-phase generator found** (system slots 110-113, `71F0/F19F/51F0/F1F0`): maintains the
    chorus LFO phase in **IRAM3[7]** (observed write iram[135]) and writes **targets[7]** (iram
    199) — the program drives its own ramp bank. **`F19F`'s CRAM = the per-sample phase increment**
    (cram `0x0080` → observed +0x80/pass), and the SAME slot was the front-panel "Chorus Rate"
    sweep hit — two independent experiments agree: Rate = phase step. The saw's monotone rise
    requires **wraparound** somewhere in this path (the probed ALU path saturates) — `51F0`
    cram `0x1001` is the "unsat/wrap" candidate `[H]`. On the bit13=1 side the index tracks
    `sel[8:6]` with sm dropped `[H]`. Full analysis: `xp_dsp_isa_decoded.md` §3, §8.5.

- **C25. scgsMaster re-decoded under the st/word/col encoding is MORE coherent vs the transpile
  (audit, 2026-07-02).** (a) All six one-pole α/β slots across chorus/delay/reverb use the SAME
  column `0x11` at different words — the old "β is op0 but α is op7" puzzle dissolves (column =
  operand role, word = state home). (b) scgsMaster's column vocabulary (`0x30,0x23,0x25,0x15,
  0x11,0x21,…`) is identical to the JV's — columns are architectural. (c) The bit13 store split
  maps onto the transpile's write classes: 8 dual-IRAM stores (b13=0, k1-12) = internal state;
  31 b13=1 stores = ERAM-side (incl. the chorus `fEramBuf` write pair 97/99 with the 0x88 tag).
  (d) scgsMaster contains the same saw-phase generator shape (slot 115 st3 col0x1F cram 0x8400 +
  companion cram 0x2801 at slot 105) as the JV's chorus LFO block. Nothing contradicts the new
  encoding. Details: `xp_dsp_isa_decoded.md` §8.6. Open: transpile-side LFO phase-advance not yet
  located in the disassembly.

- **C26. bit12 IS observable — it selects the store destination bank on the b13=1 side (rig
  session, 2026-07-02; CORRECTS C24's "no observable effect").** Controlled A/B with acc loaded
  0x321, cram=0: `st3 b13=1 b12=0 k=15` → wrote **[47] = IRAM1[47]** only (`k+32` rule);
  `st3 b13=1 b12=1 k=15` → wrote **[143] = IRAM3[15]**; `b12=1 k=0` → **[128] = IRAM3[0]**.
  So: **b13=1,b12=0 → IRAM1[32+k]; b13=1,b12=1 → IRAM3[k]** (values read back mid-decay — the
  IRAM3 ramp engine fights the store, consistent with C9; zero the target too when using IRAM3
  as a store observable). On the b13=0 side C≡D (dual `{k,k+64}`) still shows no b12 effect.
  The earlier user rows that suggested `F000→[32]` were likely mixed with E-rows (their own
  "stray MSB" caveat). Store-destination decode (FINAL, cram-independent — see C26/C31):
  `{b13,b12}`: `00/01` → dual IRAM1[k]+IRAM2[k] · `10` → IRAM1[32+k] · `11` → IRAM3[k].
- **C27. Rig session open anomalies (2026-07-02, to re-test with SENTINEL values not zeros):**
  (a) a store at PRAM slot 3 (k=6) wrote nothing while identical stores at slots 2/4/5 worked;
  in a follow-up, adjacent stores at slots 2+3 BOTH appeared dead — but the cleared-to-0 design
  cannot distinguish "no write" from "wrote acc=0"; redo with 0xAAAAAA sentinels. (b) A second
  col-0x1F load at slot 2 (k=5, cram 0x0100) produced acc-capture `0xFFE9F8` (−5640) instead of
  0x100 — pipeline latency and/or multiple interleaved accumulators (user hint: "accumulatorS
  keep state between runs"). Map with a two-value program + captures at varying slot gaps.
  (c) The MIDI link dropped twice under sustained poke/read traffic (first recovered after ~60 s
  idle; second pending) — pace sessions with idle gaps and ping checkpoints; never retry reads.

- **C28. CRAM cells are TYPED — the SCCore host-side CRAM router (0x2bff4) uses four distinct
  decoders by coefficient address (static, disassembled 2026-07-02).** For the chorus/delay
  region (host coeff addrs `0x60-0x6B`) and reverb region (`0x93-0x9F`):
  - **default** (addrs 0x62,0x64-0x66,0x68,0x69,0x96,0x97,0x9A-0x9E and all EQ addrs): C14 →
    **float coefficient** (per-SSE-lane WriteCoef @0x2d48e) — the ordinary multiply constant.
  - **dedicated-global C14 floats** (0x60,0x61,0x63 / 0x93,0x94,0x95,0x98,0x99): standard C14
    decode (`cvtsi2ss × 1/8192` tails @0x2c40e-0x2c4e2) but routed to NAMED globals
    (0x1a028xx-0x1a0293c cluster) instead of the per-lane coefficient store — parameter-style
    uses (rates/levels/mixes) consumed by the native code, not per-slot multiply constants.
  - **bit15-switched int offset** (0x67, 0x6B): `raw15 ? sext14<<13 : sext14` (@0x2c3ac) —
    tap-offset form. NOTE: differs from the DSP-side col-0x1F const decode measured on hardware
    (sext15<<13) — TWO distinct int decodes exist (host-side vs instruction-side).
  - **RAW 16-bit stored undecoded** (0x6A, 0x75; 0x76 split path): mode/pointer words
    (e.g. `mov [rip+0x1a02958], ax` — no decode at all).
  - 0x9F: hybrid int+float-scaled.
  This proves the user's conjecture: **CRAM is not uniformly coefficients** — specific cells are
  integers (delay taps), raw mode words, or offsets, matching the hardware findings (cram-steered
  store destinations, saw-phase increments). Instruction-side rule remains per-COLUMN (C22).

- **C29. SINGLE accumulator; store latency ≈ 0 (rig night session, 2026-07-02/03).**
  (a) Two col-0x1F loads in one program: the second overwrites the acc **regardless of bit12**
  (load-b12=1 updated the same acc the b12=0 store captured) → bit12 does NOT select an
  accumulator on loads. (b) No even/odd-slot accumulator split (adjacent loads at slots 0/1:
  every later store sees the second value). → **one accumulator**, unlike CSP's two.
  (c) Pipeline: stores at slots 1,2,3 after a load@0 all capture it; a store at slot 5 captures a
  load@4 → **a store at slot s sees the result of slot s-1** (no visible latency).
  (d) The "slot-3 store anomaly" (C27a) was misattributed: it is **k=6** — stores to word 6 read
  back 0 with the sentinel overwritten (store fires, value zeroed). All other k tested fine.
  `[H]` IRAM words {6,70} are hardware-driven each pass (input-sample port; silent bus = 0).
  (e) The col-0x1F k-sensitivity anomaly (C27b, 0xFFE9F8) did NOT reproduce after a clean reset —
  col 0x1F is a pure const load, k-insensitive; the reading was stale-state contamination.
  (f) RESOLVED by the P2a full scan: a LONE `st3 b13=0 b12=1` store (0xD240, k=9) **dual-writes
  {9,73} exactly like b12=0** — C≡D confirmed for lone stores. The N1 null (same store word but
  with a b12=1 LOAD also in the program) is a **combination anomaly** to retest.
  (g) **IRAM words 6/70 are HARDWARE-DRIVEN (confirmed)**: with NO program at all, sentinels at
  {5,69} survive while {6,70} are zeroed by the chip each pass — an input-sample port word
  (silent bus = 0). Explains every "k=6 store writes zero" reading.
  (h) Link-health ROOT CAUSE + FIX (2026-07-03): the wedge is the **SH firmware debug/MIDI
  reply-path**, not the XP DSP — each `read_dsp` is 3 back-to-back request/response sysex with NO
  inter-command gap, so a reply still transmitting when the next command's bytes arrive desyncs the
  SCI (overrun/reentrancy). POKEs (fire-and-forget, no reply) don't stress it; READs do (~3×/read,
  bidirectional). **Host-side mitigations added to `jvdebug.py`** (gap after each reply, settle
  between trigger/latch, hard back-off on a missing reply, atomic 1-txn CRAM path, `read_dsp`
  returns None instead of raising): validated — a full M3 sweep ran **350 transactions with zero
  degradation** (old ceiling ~100). Durable cure still wants a debug-ROM fix (clear SCI ORER/FER/PER
  + reentrancy guard in the command hook). Diagnostic to confirm the layer: audio survives a wedge.

- **C30. COMPLETE 64-column ALU table measured (morning session 2026-07-03; `rig_morning.py` M1)**
  — acc=0x321, cram=0x0123, word=0 (all mem operands = 0): keep-class (mem-add/MAC) at
  0x00-03/0x0B-0D/0x10-12/0x1B/1C/1E/0x20-23/0x2B/2C/0x30-33/0x3B/0x3E; zero-class (load-mem) at
  0x04/05/09/0A/14/1A/24/25/29/2A/34/35/39/3A/3C; negate at 0x06-08/16/17/26-28/36-38; load-const
  at 0x0E/0x1F/0x2E/0x2F; acc+const 0x0F; **leak MAC `acc+=acc·coef` at 0x13**; **pure multiply
  `acc=acc·coef` at 0x15/0x19**; MAC−acc 0x18; const−acc 0x3F; open ±small constants 0x1D/0x2D/0x3D.
  Period-0x10 structure (0x1X = coef variants of 0x0X). col 0x2F = plain load here → the earlier
  −C·0x3FF reading retracted as contamination. CAVEAT: word=0 masks mem operands — mem≠0 re-sweep
  queued. **M2:** a `b12=1` LOAD disables a coexisting `b12=1` STORE (clean A/B; with b12=0 load
  both stores dual-write) — compute-b12 engages a shared port [H]. M3 (cram-steer map) lost to the
  link dying (~100 reads/power-cycle budget pattern confirmed again; rig needs power-cycle).

- **C31. FULL read->MAC->add->store datapath WORKS on hardware; IRAM read-address map found
  (2026-07-03, `rig_hello.py`).** A hand-authored 4-op program, verified by readback across all
  three banks + a fresh value:
  ```
    slot0  st0 col 0x1F cram 0x0000     acc = 0
    slot2  st1 word=W col 0x00          acc += mem[addr14]     <- the READ
    slot4  st0 col 0x15 cram 0x1000     acc = trunc(acc*0.5)   <- MAC/multiply
    slot6  st0 col 0x0F cram 0x0111     acc += 0x111           <- add const
    slot8  st3 b13=0 k=9               IRAM1[9]=IRAM2[9]=acc   <- store
  ```
  Results (all bit-exact): IRAM1[40]=0x111100->0x088991 · IRAM2[40]=0x222200->0x111211 ·
  IRAM3[40]=0x333300->0x199A91 · fresh 0x0801E0->0x040201.
  - **READ map (st1/st2 col 0x00 = `acc += mem[addr14]`)**: `idx = addr14[11:6]` (CONFIRMED).
    Bank: `addr14[13:12]=11` → **IRAM3[idx]** (single-buffer, stable). `00/01/10` → the
    **IRAM1/IRAM2 double-buffer** (see C32) — bank is phase-dependent, NOT the clean
    `00→IRAM1/01→IRAM2` this program's phase suggested (that was refuted next run). Reads were the
    last unproven datapath half; now proven (with the double-buffer caveat).
  - Confirms col **0x00 = memory-add** (`acc += mem`, the "keep" reading with mem=0 was this),
    col **0x15 = multiply** (`acc = acc*coef`), col **0x0F = add-const**, col **0x1F = load** —
    all four working together in a real pipeline.
  - **UNIFICATION**: the "EFX input bus" word `0x4A` (C20) = the `IRAM2[10]` region — buses live in
    the IRAM1/2 working memory. (NOTE: read/store bank-selects do NOT simply mirror — and IRAM1/2 are
    a double-buffered pair, see C32; the clean per-run read map here was phase luck.)

- **C32. IRAM1+IRAM2 = ONE double-buffered (ping-pong) memory; IRAM3 = separate single-buffer;
    the `st` field = memory-access mode (orthogonal to the column ALU op). (2026-07-03,
    `rig_readverb.py`.)**
  - **Double-buffer:** reads of `addr14[13:12]=11` (IRAM3) are stable + correctly indexed
    (`word 0xC8→IRAM3[8]`, `0xE8→IRAM3[40]`, reproducible). Reads of `00/01/10` (the IRAM1/2 pair)
    return whichever physical half is current for the sample-phase: the `rig_hello` bank map
    (`00→IRAM1`) came back **inverted** next run (`00→IRAM2`), and a single dual-store's halves read
    back different (`IRAM1[11]=0x220349` vs `IRAM2[11]=0x110349`). Index (`addr14[11:6]`) is stable;
    the bank is not. ⇒ the §3 **dual store** (`b13=0` writes both IRAM1[k]+IRAM2[k]) keeps the two
    buffer halves coherent. **METHODOLOGY: capture experiment outputs into IRAM3 (stable), never
    IRAM1/2.** (The hello-world read map was phase luck.)
  - **Verb model:** `st` picks the memory mode, the **column** picks the ALU op (independent —
    `col 0x15` `acc*=0.5` gave `0x190` for every st): **st=0** = acc-only, NO memory operand
    (st0 word0x28 left acc=0x321 unchanged); **st=1** = `acc ⊕ mem` (read); **st=2** = `acc ⊕ mem`
    read, a variant of st1 (double-precision/DMAC? — its scgsMaster role is old-opB interpolation)
    `[H]`; **st=3** = writeback (store the column-op RESULT; a no-mem column is a plain store —
    whether col 0x00 makes st3 a read-modify-writeback `mem+=acc` is OPEN, acc-persistence confounds
    the single-value test). Confirmed via IRAM3 capture: compute is st-independent (col 0x15 → 0x190
    for all st), so **st gates memory, column selects the ALU op** — the CSP/ESP read/compute/store
    shape. Details: `xp_dsp_isa_decoded.md` §3b.

## [E] Expected but NOT proven on the XP

- *(none open right now)*

## Open questions

- **Q1. Instruction encoding — CONFIRMED core, residual verbs.** Field split `[15:14] st |
  [13:6] word | [5:0] col`, addr14=word<<6|col; single 24-bit saturating acc; read (st1 col0),
  MAC (col 0x15), add-const (col 0x0F), load (col 0x1F), store (st3) all hardware-proven (C22–C31).
  **Still open:** (a) st=0 and st=2 verbs (st1=read, st3=store both proven; is st0 compute-no-store,
  st2 double-precision/DMAC?); (b) bit12-on-compute (the M2 anomaly — a b12=1 LOAD disables a
  coexisting b12=1 STORE); (c) the read-map `addr14[13:12]=10` alias-to-IRAM2; (d) full 64-column
  table with mem≠0 (to split "keep" from `acc+=mem` — partly answered: col0=`+=mem`); (e) opD
  sub-addresses, op4, ext=DRAM-strobe hypothesis, the two write markers (bit24 vs abyte 0x88).
- **Q1-next.** Rig: (1) read-map scan 0x00-0xFF resolves (c); (2) st-verb A/B (identical addr14+cram
  under st=0/1/2/3) resolves (a); (3) mem≠0 column re-sweep resolves (d).
- **Q2.** *(answered — C13)* Poked programs execute (free-running); IRAM1/2 re-develop on their own.

## [R] Retracted (do not rebuild on these)

- **R1. "Latched/shadow copy of the program."** No evidence; contradicts C5; implausible SRAM cost.
- **R2. "`0x3916` 0→7 commit required to run poked PRAM."** `0x3916` is boot-only run/stop (C7).
- **R3. "Firmware co-owns the DSP / needs factory test mode to execute."** Contradicts C5.
- **R4. "Poked programs execute (early fuzz hits)."** Stale-readback artifact (C10).
- **R5. "IRAM3 is the clean observable."** It's the magic ramp bank (C9).
