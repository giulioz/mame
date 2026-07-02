# SCCore LSP reference — NOT the XP (read the caveat)

**CORRECTION:** an earlier version of this doc claimed SCCore transpiles the JV-1080 **XP** effect DSP.
That is WRONG. SCCore does **not** execute the XP effect DSP — `_TGXpDsp_setPRAM/setCRAM` only store the
words and `TGXpLspDsp_process` is only the IRAM3 ramp engine (XP PRAM is stored-but-not-run, like MAME).
The transpiled effects below are the **LSP**, a *different chip* in the same Roland DSP family. Use them
as a **structural reference** for the XP (same family: MAC + coeff-with-shift + circular delay buffers +
shelf/peak filter builders), but the **instruction encodings differ** — this does NOT directly decode
the XP ISA.

`SCCore00.dylib` (Roland SC-88Pro software core; `/Users/giuliozausa/personal/programming/hack_scva/SCCore00.dylib`,
x86_64 slice) contains, for the **LSP** effect DSP:
1. the **raw LSP bytecode** stored as 32-bit instruction words, and
2. a **hand-transpiled native implementation** of each LSP effect (`lspproc_*`), plus
3. named **`Lsp*` primitives** that spell out the LSP memory model.

Aligning the LSP bytecode ↔ transpiled code decodes the **LSP** instruction semantics (a family reference).

## Bytecode storage / upload path
- `_LspWritePrgBody` (`0x28bb5`): copies raw 32-bit words from `COEF_DATA` into DSP slots 0x0a..0x170
  via `WriteCoef` (`0x53f76`), for `count` slots starting at a per-program `start` offset; zero-fills the rest.
- `COEF_DATA` table @ **`0x8ef110`** = the raw instruction words (same 32-bit format as JV PRAM).
- `PRG_TABLE` @ **`0x8eebf0`** = per-program `{start:u16, count:u16}` (4 bytes/entry).
- `_LspWritePrgHead`/`Foot` (`0x28b75`/`0x28c45`): program prologue/epilogue.

## Transpiled effect execution (the decoded bytecode)
Mangled `lspproc_XX(Q27, S&, S&, S0*, PS&, S1*, P3<Q15>)` — fixed-point (Q27 sample, Q15 coeffs). Examples:
`lspproc_eq8` (`0x651fc`, EQ), `lspproc_rev` (reverb), `lspproc_comp`, `lspproc_enh`, `lspproc_od` (overdrive),
`lspproc_trm`, `lspproc_pan`, `lspproc_d_f`/`d_c` (delay), `lspproc_gtm1..3`, `lspproc_cgt1/2`, `lspproc_lofi`, … (~50).

## Lsp* primitives = the memory model (names are self-documenting)
- Coefficients: `LspWriteCoefS/D` (single/double), **`LspWriteCoefSsft/Dsft`** (with exponent **shift** —
  corroborates XP_FACTS **C14**: bit15 = shift, `and 0xffff7f00`+cond`0x8000`+`or value`). `LspReadTmpCoefS/D/Sft`.
- Delay / circular buffer: **`LspReadCB`** (read circular buffer via HW-register shadow, value = `b0+b1<<4+b2<<8`),
  `LspWriteEramAdd`, `LspReadTmpEramAdd` (ERAM = external delay memory addressing).
- Filter coefficient builders: **`LspLSFWrite`** (low shelf), **`LspHSFWrite`** (high shelf), **`LspPKGWrite`**
  (peaking) — confirms the Stereo EQ = shelf + peaking bands (matches our param→slot map).
- Amp/drive: `LspSetGain`, `LspSetDrive`, `LspSetAmpType`, `LspSetTwinSw`. Mute: `LspMuteOn/Off*`.

## Effect coefficient builders + address tables (param → coeff)
- `_Equalizer` (`0x173e`): the EQ builder. Calls `LspLSFWrite(_eqLowLAdrsTbl,gain,freq)`,
  `LspHSFWrite(_eqHighLAdrsTbl,…)`, single coeffs at addrs `0xbb`/`0xf0`, etc. — analogous to the JV RFX builders.
- EQ address tables (coefficient memory addresses, 3×u16 per band; L/R offset ≈ 0x35):
  `eqLowL=[0x8d,0x8f,0x91]`, `eqLowR=[0xc2,0xc4,0xc6]`, `eqHighL=[0x95,0x97,0x99]`, `eqMid1L=[0x9d,0x9e,0xa6]`, …
- Similar tables: `enhancer*AdrsTbl`, `delay*Tbl`, `chorusServiceTbl`, etc.

## Next step to fully decode the JV instruction ISA
Align `lspproc_eq8` (execution) with the EQ's `COEF_DATA` words: each bytecode instruction's opcode
(15:12), store-mode (11:9), and selector (8:0) maps to a concrete native op (load state / MAC coeff /
read circular buffer / store), pinning the **selector → IRAM/ERAM/bank** mapping — the last open ISA piece.
Then apply to the JV Stereo EQ program (`stereo_eq.txt`), whose bands we've already attributed
(`effect_param_map.md`).
