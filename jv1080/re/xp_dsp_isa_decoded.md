# Roland XP DSP — instruction set (current model)

*Assembled from three evidence streams: (1) static alignment of the `scgsMaster.txt` XP bytecode
against its native transpile `SystemEffects_process` in `SCCore00.dylib`; (2) live JV-1080 hardware
experiments via the MIDI debug ROM (`jv1080/re/debugrom/xp_manual_eq.py` — front-panel sweeps,
hand-built programs, single-instruction probes); (3) the MAME emulator write-footprint. Every claim
tagged **CONFIRMED** / **STRONG** / **[H]** hypothesis. Ledger of record: `XP_FACTS.md` (C14–C23).*

*Rewritten 2026-07-02 around the store-control / addr14 / column model. Earlier field splits
("op nibble [15:12]", "sm[11:9]/sel[8:0]") were projections of this layout; the evidence that
established them (§10) remains valid — only the boundaries moved.*

---

## 1. The instruction word

Each program slot = one **32-bit PRAM word** + one paired **16-bit CRAM coefficient** (same index;
PRAM slot k at host `0x3400+4k`, CRAM at `0x2C00+2k`). Programs free-run every sample at **32 kHz**;
slot position carries ordering only (programs are relocatable — a word-identical system program ran
at slot 61 instead of 104. CONFIRMED).

```
 31       25 24 23        16 15  14 13          6 5        0
+-----------+--+------------+------+-------------+----------+
|   ext     |wb|     hi     |  st  |    word     |  column  |
+-----------+--+------------+------+-------------+----------+
 \______________________________/   \______________________/
   parallel ERAM/delay channel        addr14 = word<<6|col
   (dormant in delay-free programs)   the DSP-internal address
```

| bits | field | meaning | confidence |
|---|---|---|---|
| **[15:14]** | **st — store control** | `3` = **store accumulator to mem[addr14]** (hardware-proven). `0/1/2` = no-store / ? / ? — verbs open (§3) | st=3 **CONFIRMED**; others open |
| **[13:6]** | **word** | memory word address. For addr-tops 0/1, stores land at IRAM entry = `word` (contiguous 0–255 over IRAM1/2/3/tgt) | **CONFIRMED** (store probes) |
| **[5:0]** | **column** | ALU operand/mode select (§5); plausibly also the TDM bus slot [H] | **CONFIRMED** (truth table) |
| **[23:16]** | **hi** | parallel ERAM address byte; bit23 = tap-pair marker; +3 counter on the reverb allpass chain | **CONFIRMED** (§7) |
| **[24]** | **wb** | in an ERAM pair second word = **addr[8]** (hardware-swept); "writeback" elsewhere [H] | pair role **CONFIRMED** |
| **[31:25]** | **ext** | ERAM/DRAM control strobes [H]: bit26 (`ext=2`) set on data-carrying tap pairs, bit27 (`ext=4`) on the EQ's L out send — likely start-DRAM-read/write style controls | [H] |
| CRAM[slot] | coefficient | per-**column** decode (§6) | **CONFIRMED** |

Historical note: the old "op nibble" `[15:12]` = `st[1:0] ‖ addr14[13:12]` — two store-control bits
glued to the top two address bits, which is why it always behaved as opcode *and* region selector
simultaneously. Legacy notation `op/sm/sel` converts as `addr14 = op[1:0]<<12 | sm<<9 | sel`,
`st = op>>2` (the builder still accepts it).

---

## 2. Execution model

- **Free-running**: the DSP executes all 288 PRAM slots every sample, continuously; poke and it
  runs — no kick, `0x3916` stays 7 (XP_FACTS C13). A lone `acc += const` column therefore
  *integrates across passes* (0x123/pass saturates in ~0.9 s) — reset acc with a load column
  (0x1F) per pass when probing. **CONFIRMED**
- **Accumulator: SINGLE (not CSP's two), 24-bit signed, saturating** at `0x7FFFFF`/`0x800000`.
  Rig-proven (2026-07-03): a second load overwrites the acc regardless of bit12; no even/odd-slot
  split; **a store at slot s captures the result of slot s-1** (no visible pipeline latency).
  **CONFIRMED**
- Datapath = MAC against the running accumulator; the column selects operands/mode (§5); `st`
  controls the memory write (§3). Sample rate 32 kHz (measured via tap addressing, §7).

---

## 3. The store-control field `st[15:14]`

Population over the 256 nonzero scgsMaster slots (old-nibble members in parentheses):

| st | slots | members (old nibbles) | addr-tops used | verb |
|---|---|---|---|---|
| `0` | 36 | op0 (1,2,3 never seen) | only `0` | compute, no store? **open** |
| `1` | 159 | op4/5/7 (6 never) | 0,1,3 | compute/MAC family? **open** |
| `2` | 22 | opB (8,9,A never) | only 3 | compute (DMAC candidate)? **open** |
| `3` | 39 | opC/D/E/F | **all four** | **STORE acc → mem[addr14]** — hardware-proven |

- **The 9 never-seen old-nibble values are exactly the unused (st, addr-top) combinations** —
  resolves the "opcode holes" mystery (a 16-opcode ISA with 9 holes was always awkward). CONFIRMED
  as population fact.
- **st=3 store ruleset (single-instruction probes, 2026-07-02 — 9/11 rows bit-exact):**

  | bit13 | cram | behavior | fit |
  |---|---|---|---|
  | 0 | — | **dual write** `IRAM1[k]` and `IRAM2[k]`, `k = addr[11:6]` | 5/5 (`w0x00→{0,64}`, `w0x02→{2,66}`, `w0x20→{32,96}`, `w0x40→{0,64}`, `w0x7F→{63,127}`) |
  | 1 | =0 | **single write at entry `k+32`** | 3/3 (`w0x80→32`, `w0xC0→32`, `w0x8F→47`) |
  | 1 | ≠0 | **coefficient-steered** into the IRAM3 region (`F000→128`, `F1F0→134`, `F3F0→136` — target depends on the cram value; the ERAM/write-head path) | formula open |

  **Rig updates (2026-07-03):** IRAM words **{6, 70} are hardware-driven** (an input-sample port:
  with no program at all the chip zeroes them each pass — sentinel test) — avoid k=6 as a store
  observable. A LONE `b13=0 b12=1` store dual-writes `{k,k+64}` exactly like b12=0 (C≡D confirmed
  for lone stores). **M2 (controlled A/B, 2026-07-03): a `b12=1` LOAD in the program DISABLES a
  coexisting `b12=1` STORE** (with b12=1 load: b12=1 store writes nothing, b12=0 store fine;
  with b12=0 load: BOTH stores dual-write) — bit12 on compute ops engages a shared port/resource
  that hijacks the b12=1 store path [H].

  **bit12 (RESOLVED 2026-07-03 by controlled A/B, supersedes the earlier "no observable effect"):**
  on the **b13=1** side bit12 selects the destination bank: `b12=0 → IRAM1[32+k]` (k=15→[47]),
  `b12=1 → IRAM3[k]` (k=15→[143], k=0→[128]; values read mid-decay — the IRAM3 ramp engine fights
  the store, zero the targets when observing). On the **b13=0** side C≡D (dual `{k,k+64}`) for
  lone stores — no bit12 effect seen there. Store-destination decode:
  `{b13,b12}`: `00/01` → dual IRAM1[k]+IRAM2[k] · `10` → IRAM1[32+k] · `11` → IRAM3[k]
  (+ cram≠0 offsets the IRAM3 index — formula open). The store index is **6 bits (`k=addr[11:6]`)**.
- Remaining decisive experiments (§11): st-verb A/B (old op0/op4/op8 at identical addr14+cram);
  the b12=1-load + b12=1-store combination anomaly (`rig_morning.py` M2).

---

## 4. addr14 memory map

`addr14 = word<<6 | column`; 14 bits = 256 words × 64 columns.

| addr14 range | words | contents | confidence |
|---|---|---|---|
| `0x0000-0x0FFF` | 0x00–0x3F | registers/scratch + column ops (st0 lives here; word 0x00 columns = the ALU constant/register file) | **STRONG** |
| `0x1000-0x1FFF` | 0x40–0x7F | **buses**: word `0x4A`/`0x4B` = EFX input L/R (all 64 columns); `0x54`/`0x57` chorus sends [H]; `0x55`/`0x56` = **EFX out L/R**; reverb feeds at old-D489 (word 0x52 col 9 — col 9 = mixer bus 9/efxB [H]) | in/out **CONFIRMED** (working passthrough) |
| `0x2000-0x2FFF` | 0x80–0xBF | store page top-2 (old opE): probe wrote entry 128 = IRAM3 bank; cram-dependent [H] | open |
| `0x3000-0x3FFF` | 0xC0–0xFF | **datapath state** (the EQ/chorus/reverb working words). EQ: L biquad state words `0xD0-0xD7`, R = **+8** = `0xD8-0xDF` (the L/R mirror bit = word bit 3 here = old bit9). Chorus one-pole state at words `0xCB/0xCC` | **CONFIRMED** (EQ mirror quantified) |

The store space (st=3) reaches **IRAM contiguous** (entries 0–63 = IRAM1 `0x3000`, 64–127 = IRAM2,
128–191 = IRAM3, 192–255 = targets `0x3300`) via the bit13 ruleset in §3: bit13=0 dual-writes
`{k, k+64}` (both L/R banks), bit13=1 single-writes `k+32` (cram=0) or a coefficient-steered
IRAM3-region target (cram≠0). Only 6 index bits (`addr[11:6]`) are proven; bit12 unobserved.

Cross-consistency [H]: the `word|column` split mirrors the mixer-send word format
(`level[15:6] | bus[5:0]`) — the 64 columns are plausibly the 64 TDM bus/voice slots.

---

## 5. The column ALU — COMPLETE 64-column hardware table (2026-07-03)

Method: `[col 0x1F: load acc A=0x321]` → `[test column, word=0, cram C=0x0123]` → `[st3 store]`.
**Caveat:** word=0 ⇒ every `mem` operand reads 0 — "keep" ≡ `acc += mem`, "zero" ≡ `acc = mem`,
"negate" ≡ `acc = mem − acc`, "load C" ≡ `acc = mem + C`. The mem≠0 re-sweep (preload a word,
sweep with word=k) separates these — queued.

| result class | columns | operation (mem=0 reading) |
|---|---|---|
| keep (+A) | 0x00-03, 0x0B-0D, 0x10-12, 0x1B/1C/1E, 0x20-23, 0x2B/2C, 0x30-33, 0x3B, 0x3E | `acc += mem` (the mem-read/MAC family; col 0x30 = the program workhorse) |
| zero | 0x04/05, 0x09/0A, 0x14, 0x1A, 0x24/25, 0x29/2A, 0x34/35, 0x39/3A, 0x3C | `acc = mem` (load-mem) |
| negate (−A) | 0x06/07/08, 0x16/17, 0x26/27/28, 0x36/37/38 | `acc = mem − acc` |
| **load const** (+C) | **0x0E**, 0x1F, **0x2E, 0x2F** | `acc = mem + const` |
| acc+const | 0x0F | `acc += const` (+mem) |
| **leak MAC** | **0x13** | **`acc += acc·coef`** = `A + trunc(A·coef)` (+829 = 801+28) — the one-pole op |
| **pure multiply** | **0x15, 0x19** | **`acc = trunc(acc·coef)`** (+28) (+mem) |
| MAC−acc | 0x18 | `acc = acc·coef − acc` (−773) |
| rsub const | 0x3F | `acc = const − acc` (−510) |
| open constants | 0x1D (+289 = C−2), 0x2D (+803 = A+2), 0x3D (+514 = 0x202) | ±(C>>7)-ish — need a 2nd cram value |

- **Periodic structure ≈ 0x10**: the 0x1X block holds the coefficient-using variants of the 0x0X
  block; 0x2X/0x3X largely mirror 0x0X/0x1X with the extra loads (0x2E/0x2F) and 0x3D/0x3F.
- **col 0x11 (the α/β column)** reads "keep" at mem=0 → consistent with `acc += mem·coef`
  (MAC) — exactly its one-pole role in programs.
- **col 0x2F correction:** in this controlled run 0x2F = plain load-const (+C). The earlier
  `−C·0x3FF` reading (user session) did not reproduce — likely stale-state contamination.
- Accumulator: SINGLE, 24-bit saturating; store captures slot s−1 (§2).

## 6. CRAM coefficient formats — decode is PER-COLUMN

| context | decode | evidence |
|---|---|---|
| **multiply columns** (MAC paths) | **C14**: `value = sext14(raw[13:0]) << [0,1,2,4][raw[15:14]] / 8192` — anchors `0x5000=+1.0`, `0x2000=−1.0`, `0x1000=+0.5`, `0xE000=−16.0`, `0x1FF0=+0.9980` (α), `0x3FE0=−0.0039` (β) | **CONFIRMED byte-for-byte** from `TGXpDsp_setCRAM` @0x51e03 (shiftLUT @0x19ebf70={0,1,2,4}, scale @0x99a688=1/8192) + JV gain sweeps crossing the exponent boundary smoothly |
| **const/address columns** (0x1F etc.) | **`sat24( sext15(raw[14:0]) << (raw[15] ? 13 : 0) )`** — 6/6 hardware points (`0x3456→+0x3456`, `0x4321→−0x3CDF`, `0xF123→0x800000` sat) | **CONFIRMED**; refutes the SCCore-doc `sext14` int decode *for this column* |

1:1 pairing `CRAM[k] ↔ PRAM[k]` and verbatim program load (no re-encoding) — CONFIRMED
(`MasterXpdspLoadProgram` @0x4f9f1 copies raw words to `0x3400/0x2C00`).

---

## 7. The parallel ERAM / delay channel (bits [31:16])

The upper half is an independent channel carrying **external delay-RAM (ERAM) addresses** past the
ALU stream. Dormant (all zero) in delay-free programs (EQ: `hi=0` on 72/72 slots).

### 7.1 Two-word tap-address pair — CONFIRMED on hardware (Triple Tap Delay sweep)

```
addr[15:9] -> word1[22:16]   (bit23 = 1: pair marker)
addr[8:0]  -> word2[24:16]   (bit24 of word2 = addr[8] -- an ADDRESS bit, not wb)
```

Center-tap sweep, all points satisfying **`addr = 0x6000 + ms·32`** (unit = 1 sample @32 kHz;
this RFX delay's region base = 0x6000):

| ms | pair | addr | | ms | pair | addr |
|---|---|---|---|---|---|---|
| 200 | `00BCB4C0 05007021` | 0x7900 | | 300 | `00C2B4C0 05807021` | 0x8580 |
| 205 | `00BCB4C0 05A07021` | 0x79A0 | | 395 | `00C8B4C0 05607021` | 0x9160 |
| 210 | `00BDB4C0 04407021` | 0x7A40 | | 550 | `00D2B4C0 04C07021` | 0xA4C0 |
| 215 | `00BDB4C0 04E07021` | 0x7AE0 | | 1000 | `00EEB4C0 05007021` | 0xDD00 |

- The 205→210 ms step rolls lo9 `0x1A0→0x040` with hi-byte `0xBC→0xBD` — the carry across the
  9-bit boundary proves the exact split.
- The channel is **independent of the low-half instruction**: the LEFT tap's pair
  (`00BC0000 01000000`) rides on zero low-halves (pure address carrier); the center tap rides on
  live st2/st1 instructions.
- **ext strobes [H]**: word2 bit26 (`ext=2`) = 1 on the data-carrying pair, 0 on the carrier pair;
  bit27 (`ext=4`) on the EQ L out send — plausibly **start-DRAM-read / start-DRAM-write controls**
  for the external 2-Mbit delay RAM. Untested; see §11.
- The JV firmware helper `xp_dsp_write_eram_offset` @0x0a00225c rewrites exactly these fields
  (masks `0x0F80FFFF`/`0x0E00FFFF`) on live delay-time edits.
- **bit23 is NOT a reliable static pair-finder** (59/256 scgsMaster slots have it set, 19 in
  overlapping chains) — locate pairs dynamically (sweeps or the helper's call sites).
- Reverb allpass chains: the `hi` byte walks as a **+3 counter** in lockstep with the low address
  (+0x40/step) — an ERAM address stream for successive allpass taps.

### 7.2 ERAM organization (from the SCCore transpile — STRONG)
64K-word circular buffer; per 32-sample block the global pointer decrements 32; region bases
Chorus `+0x0000`, Reverb/pre-delay `+0x1000`, Delay `+0x8000`. Chorus taps use fractional
addressing with linear interpolation (`frac = (0x1000 − (tapAddr & 0xFFF))/4096`).

---

## 8. Worked examples (new notation; `line_s(st=, word=, col=)`)

### 8.1 The minimal working RFX (hardware-verified passthrough)
```python
.line_s(st=1, word=0x4a, col=0x00)                  # read EFX input bus L
.line_s(st=0, word=0x00, col=0x21, cram=0xe000)     # input conditioning (const col, -16 gain idiom)
.line_s(st=1, word=0xf6, col=0x30)                  # accumulate via the 0x30 register column
.line_s(st=1, word=0xd0, col=0x30, ext=2)           # into L out accumulator ("nop -> no left out")
...
.line_s(st=3, word=0x55, col=0x00, ext=4)           # STORE -> EFX out L
.line_s(st=3, word=0x56, col=0x00)                  # STORE -> EFX out R
```

### 8.2 One EQ biquad band (ROM Stereo EQ, LOW shelf, L; R = every word +8)
```python
.line_s(st=1, word=0xd0, col=0x30, ext=2, cram=0x02d1)  # MAC into acc          (was 04007430)
.line_s(st=1, word=0xd1, col=0x25, cram=0x2190)         # state MAC             (was 7465)
.line_s(st=3, word=0xd0, col=0x15, cram=0x508b)         # STORE delay state     (was F415)
.line_s(st=0, word=0x00, col=0x23, ext=2, cram=0x1f87)  # register accumulate   (was 04000023)
```
The old "opF = ERAM" reading of `F415` becomes an **st3 store to datapath word 0xD0** — the biquad
`z⁻¹` update. 0 opcode mismatches across all 36 L/R pairs; R chan = words `0xD8–0xDF`.

### 8.3 The chorus input one-pole (scgsMaster slots 92–99)
| slot | old word | st | word | col | cram | role |
|---|---|---|---|---|---|---|
| 93 | `000072D1` | 1 | 0xCB | 0x11 | `1FF0` (α=+0.998) | `acc = in·α` |
| 95 | `08000011` | 0 | 0x00 | 0x11 | `3FE0` (β=−0.0039) | `state += acc·β` (ext=4) |
| 96 | `00007319` | 1 | 0xCC | 0x19 | `5000` (+1.0) | `acc += state` |
| 97 | `0000F2E5` | 3 | 0xCB | 0x25 | 0 | STORE (tap read/write path) |
| 99 | `0088F321` | 3 | 0xCC | 0x21 | 0 | STORE, hi=0x88 tag |

α and β share **column 0x11** at different words — the same operand column across the datapath.
This one-pole (α=`0x1FF0`, β=`0x3FE0`) appears exactly 3× in the system program
(chorus/delay/reverb input stages) — the anchor that locked the SCCore alignment.

### 8.4 A delay tap (Triple Tap center, 200 ms)
```python
.eram_pair(0x6000 + 200*32,
           l1=dict(st=2, word=0xd3, col=0x00),          # rides on the st2 carrier op
           l2=dict(st=1, word=0xc0, col=0x21, ext=2))   # ext=2 = DRAM strobe [H]
```

### 8.5 The chorus LFO saw-phase generator (system program slots 110–113)

Four instructions maintain the chorus LFO phase — the cleanest demonstration yet of the
free-running model and of **in-program state living in IRAM3**:

| line | st | b13 | k | col | ROM cram | role |
|---|---|---|---|---|---|---|
| `71F0` | 1 | 1 | (sel>>6 = 7) | 0x30 | `0009` | read phase; also writes **targets[7]** (iram 199) — st1 = store-and-read port [H] |
| `F19F` | 3 | 1 | 6/7 | 0x1F | **`0432` = the front-panel "chorus rate" sweep hit** | **phase += CRAM per pass** — hardware-verified: cram `0x0080` → observed +0x80/sample increments |
| `51F0` | 1 | 0 | 7 | 0x30 | `1001` | scaling / wrap control? [open — "unsat" candidate] |
| `F1F0` | 3 | 1 | 7 | 0x30 | `2801` | write phase → **IRAM3[7]** (iram 135, observed) |

- **Two independent experiments agree**: the DT1 sweep tagged `F19F`'s CRAM as *Chorus Rate*, and
  the direct probe shows that CRAM is the per-sample phase increment. Rate parameter = phase step.
- **The saw requires WRAPAROUND** — the probed accumulator path *saturates* (sticks at
  `0x7FFFFF`), so a monotone-increment phase must wrap somewhere in this path: either the
  bit13-store side is unsaturated, or the `51F0`/`0x1001` line enables wrap. Targeted test: §11.
- The observed write indices (IRAM3[7], targets[7]) track `sel>>6 = 7` with the sm bits dropped —
  supporting the [H] that the bit13=1 side indexes from `sel[8:6]` only.
- Writing **targets[7]** from the program means the DSP drives its own IRAM3 ramp targets —
  IRAM3 is genuinely shared between the host ramp engine and running programs.

---

## 8.6 scgsMaster re-decoded under the new encoding — coherence audit (2026-07-02)

Re-decoding all 256 nonzero scgsMaster slots as `st/b13/b12/k/col` and re-checking against
`SystemEffects_process`. **Verdict: the new encoding makes the bytecode strictly MORE coherent;
several old puzzles dissolve, nothing contradicts.**

**(a) The α/β puzzle dissolves — column = operand role, word = state home.** All six input
one-pole coefficient slots across the three stages use **the same column 0x11**:

| slot | stage | coef | st | b13 | k | col |
|---|---|---|---|---|---|---|
| 93 | chorus α | `1FF0` | 1 | 1 | 11 | **0x11** |
| 95 | chorus β | `3FE0` | 0 | 0 | 0 | **0x11** |
| 144 | delay α | `1FF0` | 1 | 1 | 4 | **0x11** |
| 146 | delay β | `3FE0` | 1 | 1 | 62 | **0x11** |
| 169 | reverb α | `1FF0` | 1 | 1 | 17 | **0x11** |
| 171 | reverb β | `3FE0` | 1 | 1 | 53 | **0x11** |

Under the old model, slot 95 being "op0" while its five siblings were "op7" needed the awkward
"destination region" argument. Now: **same operand column, different state word** — exactly what
an operand-select architecture predicts.

**(b) Shared column vocabulary across machines.** scgsMaster's top columns
(`0x30`×85, `0x23`×77, `0x25`, `0x15`, `0x11`, `0x21`, `0x19`, `0x14`…) are the SAME set the JV
EQ and system program use — columns are architectural (the chip's operand file), not
program-specific.

**(c) The bit13 store split maps onto the transpile's write classes.** Of the 39 st3 stores:
- **8 dual-IRAM stores (b13=0)** at k ∈ {1,3,5,7,9,10,11,12} — the per-sample *internal state*
  writes (the transpile's persistent mix/state variables).
- **31 b13=1 stores** — the *external/ERAM-side* writes: the chorus delay-line write pair
  (slots 97/99, the `hi=0x88`-tagged write the transpile performs into `fEramBuf`), the delay and
  reverb line writes (slots 126–155 region), and the saw-phase updates. Internal-state vs
  delay-memory writes land on opposite sides of bit13, matching the physical IRAM/DRAM split.

**(d) The saw-phase generator exists in scgsMaster too.** Exactly one st3+col`0x1F` slot in the
whole program: **slot 115** (`F3DF`, k=15, cram `0x8400`), adjacent to a st3 k=13 col`0x30` store
at slot 105 with cram **`0x2801`** — the same companion constant as the JV generator (`F1F0` cram
`0x2801`), in the same program region as the byte-identical JV↔scgs cross-kernel (JV 117–121 =
scgs 109–113). Same generator shape, different rate constant. (The transpile-side LFO phase
advance has not been located in the disassembly yet — [open].)

---

## 9. System-program map (hardware nop-kill tests + sweeps)

All observed on the real JV in a **relocated** copy of the system program (C21):

| word/old word | role | evidence |
|---|---|---|
| st1 `0x4A`/`0x4B` (5280/52C0) | EFX input bus L/R | passthrough works [C] |
| st1 word 0x56 col 0x30 (55B0) | dry-path feed | nop → dry gone [C] |
| st3 `0x55`/`0x56` (D540/D580) | EFX out L/R | passthrough works [C] |
| st3 word 0x52 col 9, st3 word 0x46 col 0x30 (D489/D1B0) | reverb input feeds | nop → no reverb [C] |
| st3 `0x54`/`0x57` (D500/D5C0) | chorus-path sends | [H] |
| st3 word 0x4A col 9 (D289) | main out R? | [H] |
| st3 word 0x53 col 5 hi78 wb1 (D4C5) | reverb out L? | [H] |
| old 4021/40E5 cram `1FFF` (st1 word 0x00 col 0x21/0x25) | **EFX OUT assign L/R** | nop L → reverb goes mono [C] |
| old C001/C0F0 (st3 word 0x00 col 1 / word 0x03 col 0x30) | reverb input state L/R? | [H] |
| coef roles | chorus rate `F19F`=0x0432, depth `0014/0011`=0x0183, delay `C0EF/002F`=0x8001 (int decode), EFX-OUT→mix/reverb `F011`=0x1FFF, →chorus `5423` | front-panel sweeps [C] |

Parameter→slot maps for the EQ bands: `effect_param_map.md` (band = 3–8 CRAM slots, R = L+36).

---

## 10. How the skeleton was established (SCCore alignment — kept for the record)

- `SCCore00.dylib` contains the XP system-effects bytecode (`scgsMaster.txt`, PRAM @0x8ee3e0,
  CRAM @0x8ee860) AND its hand-transpiled native implementation `SystemEffects_process` @0x2ca14
  (chorus @0x2cadd, delay @0x2ccc0, reverb @0x2ce4e, `process_EQ` @0x2d088 = additive DF-I biquad).
  Aligning the two established: the MAC execution model, the C14 coefficient format (byte-for-byte
  from `setCRAM` @0x51e03), the α/β one-pole peg (exactly 3 instances), program split at slot 128,
  and refuted the rival "op=[23:16]" decode (that byte is an address channel: 37 distinct values,
  +3 counter behavior). The JV Stereo EQ cross-validated everything (perfect L/R mirror).
- `lspproc_*`/`Lsp*` in SCCore is the **LSP** — a different chip; family reference only.
- The IRAM3 ramp engine (host `0x3200` current / `0x3300` targets, `cur += (tgt−cur)·rate/65536`,
  Q22 = target<<13) is binary-confirmed from `TGXpDsp_writeIramBp`/`TGXpLspDsp_process` — it is a
  host-side breakpoint smoother, not part of the instruction stream.

---

## 11. Confidence summary & next experiments

### Confirmed skeleton
| claim | status |
|---|---|
| word split `st[15:14] | word[13:6] | col[5:0]`; st=3 = store acc; entry=word (tops 0/1) | **CONFIRMED** (hw probes) |
| column ALU truth table (keep/zero/negate/add/load/rsub/MAC-leak), acc 24-bit saturating | **CONFIRMED** |
| per-column CRAM decode: C14 (multiply) + sext15/<<13 (const) | **CONFIRMED** |
| ERAM two-word pair encoding; tap unit = 1 sample @32 kHz; base 0x6000 (Triple Tap) | **CONFIRMED** |
| bus words 0x4A/0x4B in, 0x55/0x56 out; programs relocatable; EQ L/R = word+8 | **CONFIRMED** |
| 9 never-seen old-op values = unused (st, addr-top) combos | **CONFIRMED** (population) |
| ERAM = 64K circular, ptr −32/block, region bases | **STRONG** (transpile) |

### Open
1. **bit12's role** — no observable effect in any experiment so far ("stray MSB" caveat). A/B with
   an observable: `E` vs `F` at k≠0 with cram=0 (predict identical `k+32` if dead); also compare
   dual-side `C`-vs-`D` at k with distinct IRAM1/IRAM2 preloads.
2. **st verbs 0/1/2** — st1 looks like a store-and-read (RMW) port from the saw generator; A/B:
   same addr14+cram under old op0 vs op4 vs op8 (op8 = st2 top0, never seen — fresh data
   guaranteed).
3. **Wraparound vs saturation** — the ALU path saturates but the saw phase wraps: nop/modify the
   `51F0` (cram `0x1001`) line and watch whether the phase pins at 0x7FFFFF ("unsat" hypothesis).
4. **Full column table** — 13/64 mapped; sweep `[5:0]` 0x00–0x3F behind a load prefix; repeat at
   another word and on the L (0xD0+) vs R (0xD8+) datapath words. Col `0x2F` identity.
5. **bit13=1 store addressing** — cram=0 `k+32` rule vs cram≠0 coefficient-steering: sweep cram
   over a bit13=1 store and map destination vs value (connect to the SCCore int-tap decode and
   the delay write-head).
6. **`ext` bits = DRAM read/write strobes?** — toggle bit26/bit27 on a working tap/out and observe.
7. **The `abyte=0x88` tag** (21 scgsMaster slots, 6 old-ops) and the second write marker.
8. Static ERAM pair-finder; sub-slot micro-expansion vs the SSE transpile.

*Tools: `xp_manual_eq.py` (st/word/col builder, show() disassembler, eram_pair, cram codecs),
`dump_dsp/restore_dsp/dsp_snapshot`, MAME `xp_dump_cb` per-second dump + write-footprint.
Bytecode refs: `scgsMaster.txt` via `scratchpad/scc.py`; JV live dump `dsp_dump_steady.txt`.*
