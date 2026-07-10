// license:BSD-3-Clause
// copyright-holders:giulioz
#ifndef MAME_SOUND_ROLAND_RCC_DSP_H
#define MAME_SOUND_ROLAND_RCC_DSP_H

#pragma once

#include <cstdint>

// Roland RCC effects-DSP interpreter (pure, MAME-independent so it can be diffed
// bit-exact against the die-RE reference model, rcc/emu/rcc_ref.cpp).
//
// This is a port of that reference model: the 288 x 29-bit mask-ROM program was
// transcribed from die photos of the RCC (R15239126) and the machine below
// implements the netlist-verified behavior as plain arithmetic.  Provenance tags
// carried over from the reference:
//   [V]  verified against the solved netlist (1441/1441 equivalence checks)
//   [S]  structural (wiring verified; function of a plausible cell)
//   [H]  hypothesis / harness modeling choice (rcc/EMULATOR_STATUS.md)
//
// One program pass = one audio sample frame = 256 steps (8 blocks x 32 steps;
// words 256..287 are the service page, unused at runtime).  Datapath: 24-bit
// saturating MAC, RAM-A 32x24 working/delay memory, RAM-B 256x18 host parameter
// file, external DRAM delay memory addressed by param-base + frame counter.
//
// Coefficient semantics are hardware-anchored: the U-220 dry-gain calibration
// fixes the positive range at byte/64 (0x40 = unity), and the firmware's boot
// parameter image implies sign-magnitude for the negative half (see
// decode_coef).  The remaining known approximations, all tagged [H] below:
// the per-voice input-bus multiplexing (owned by the PCM-side chip; modeled
// as one summed effect input), the DRAM tap micro-order/feedback gain, and
// the block-floating-point exponent scaling of large accumulator values.

namespace roland_rcc_dsp {

// 288 x 29-bit program mask ROM (die transcription, rcc/rcc_rom.hex) [SIL]
inline constexpr uint32_t PROGRAM_ROM[288] = {
	0x0ad43cd1, 0x0ad41cc4, 0x088cfec0, 0x0bd41cc4, 0x0b853cc0, 0x0a863cc0, 0x09845cc5, 0x08d41cc5,
	0x0ad43cc1, 0x0ad714c1, 0x09845ec5, 0x0bdc1cc5, 0x0b843cc1, 0x0a873cc1, 0x09845cc5, 0x0adc1cc5,
	0x08d414c4, 0x089f3cc1, 0x0ad45ec5, 0x09cc1cd5, 0x0b441cc0, 0x0b863cc0, 0x0a877cc0, 0x088c1cc4,
	0x0b843cc0, 0x09d43cd0, 0x09de5ec0, 0x0ad41cc4, 0x088f38c0, 0x0a8614d0, 0x09845dc0, 0x0bcc1cc4,
	0x0ace1cd1, 0x0a863cc0, 0x09845ec4, 0x0bcc1cc4, 0x0b843cc0, 0x088e3cc0, 0x0ad45cc5, 0x09841cd5,
	0x0a873cc1, 0x0b801ec1, 0x0b867cc1, 0x09843cc1, 0x0acc1cc5, 0x089c1cc5, 0x0a863cd1, 0x0ad41cc1,
	0x0ad43cd2, 0x0a9c1cc5, 0x05d07ec1, 0x0b541cc5, 0x0b873cc0, 0x0a843cc8, 0x0a84dcc4, 0x088c1cc6,
	0x09841cc6, 0x0adc1cc2, 0x0ace3ec2, 0x0ad73cd8, 0x0ad61cc0, 0x0b841cc4, 0x09803dc0, 0x0bd43cc2,
	0x0a843cc3, 0x0a443cc2, 0x089c1ed6, 0x0b441cc0, 0x0b873cc0, 0x0a843cc8, 0x0a845cc5, 0x0a841cc7,
	0x0a843cd1, 0x0adc34c3, 0x0acc36c3, 0x0bdf34c1, 0x0a841cc5, 0x0afc3cc1, 0x0900dcc5, 0x0b061cc5,
	0x08941cd6, 0x089f1cc9, 0x0a001ed5, 0x09841cd3, 0x0b441cc2, 0x0b843cc2, 0x0a873cca, 0x0a841cc6,
	0x0a841cd4, 0x0ad41cc2, 0x0a843ec2, 0x0acc1cc4, 0x089c3cd0, 0x0b041cc0, 0x0980fdc0, 0x0b443cc0,
	0x08d41cc1, 0x089f1cc8, 0x0a005ed4, 0x0b841cc2, 0x0b873cc2, 0x0a843cc2, 0x08941cc7, 0x08941cd5,
	0x0bd43cc1, 0x0ad43cc1, 0x09d0d6d5, 0x0a441cc1, 0x088e1cc5, 0x0a863cc1, 0x0a847cd1, 0x08441cc1,
	0x0adc1cc6, 0x0b841cc5, 0x08847ec9, 0x0a4c1cc7, 0x089c1cd6, 0x0b441cca, 0x0b843cc2, 0x09873cc2,
	0x0a841cc6, 0x0ad41cc6, 0x089f3eca, 0x0a041cc6, 0x0b843cc2, 0x0b843cca, 0x0a043dc2, 0x0a841cc6,
	0x0b841cc5, 0x0a043cc2, 0x0a8436c2, 0x0a803cd0, 0x0bdf14c0, 0x089c3cc0, 0x0afc7cc1, 0x0a841cc5,
	0x0a843cc1, 0x09803cc1, 0x0a00fec1, 0x0bd41cc5, 0x088e3cc1, 0x0a041cc5, 0x0b84dcc5, 0x0a843cc9,
	0x08101cd6, 0x0a841cc5, 0x0b807ec1, 0x08803cc3, 0x0a041cc6, 0x0b841cc6, 0x0a873cda, 0x089c1cc2,
	0x0a841cc6, 0x0ad41cc2, 0x088c3eca, 0x0ad414c6, 0x0b843cc2, 0x0b043cca, 0x0a843dc2, 0x0a841cc2,
	0x08843c01, 0x08843cc2, 0x0a842ec2, 0x0b843c80, 0x0a871cb4, 0x0a841c80, 0x0b84dc85, 0x0a843c01,
	0x0d983cc3, 0x05801c15, 0x0bd45e01, 0x0b843c01, 0x0a843cc9, 0x0a841cc5, 0x0d9cdcd5, 0x081414c3,
	0x0adc04c0, 0x0a443cd3, 0x0bd41ecb, 0x0b863cc3, 0x0a873cc2, 0x0a841cc6, 0x0a841cc6, 0x0a843cd0,
	0x088c3c00, 0x0a843cc0, 0x085416c4, 0x0ad43cc0, 0x0a873ca0, 0x0a841c94, 0x0b841d80, 0x0b843c00,
	0x0acc1cc5, 0x0ad01c04, 0x0b845e04, 0x0b863cc0, 0x0d9c3cc0, 0x081c14c0, 0x0dd8dcc5, 0x0a443cc1,
	0x08941cc5, 0x0ad53cd1, 0x085c16d1, 0x0a961cc1, 0x0b841cc5, 0x0b873cc1, 0x0a843cc1, 0x08841cc5,
	0x0a843cc2, 0x0acc3cc1, 0x0adc3ec1, 0x0acc1cd5, 0x0a441cd0, 0x0a841cc8, 0x0b841cc4, 0x0b843cc2,
	0x0a843cc0, 0x0a841cc6, 0x08941ec6, 0x0a543cc0, 0x0a841cc0, 0x0a801cd4, 0x0b841dc0, 0x0b843cc0,
	0x0a443c81, 0x0a841cc4, 0x0a841ec4, 0x0a043cc0, 0x0a842cc0, 0x0b843cc0, 0x0ac73ca1, 0x08941c85,
	0x0d9c3c81, 0x0a842481, 0x089c1e85, 0x088e3401, 0x0a843401, 0x0a841c05, 0x0b865c25, 0x0b843c81,
	0x0a0414c4, 0x0a041485, 0x0d98de95, 0x0a441c11, 0x0b841c00, 0x0b873c00, 0x0a847c00, 0x0d8c1cc4,
	0x08941cc4, 0x0ad43cd0, 0x0d88dec0, 0x0ac61cc4, 0x0a843cd0, 0x0b861cc0, 0x0b843dc0, 0x0a843cc0,
	0x0a973ccb, 0x0a973ccb, 0x0a971ccf, 0x0a973ccb, 0x0a9734cb, 0x0a971ccf, 0x0a971ccf, 0x0a973ccb,
	0x0b8f1ed0, 0x0b8f1ed0, 0x0b8bded1, 0x0b8a1ed3, 0x0b0b1ed3, 0x0b8b1ed2, 0x0b8b1fda, 0x0b8f1ed0,
	0x0acc36c1, 0x0a8436c1, 0x0a845ed5, 0x0d9f16c1, 0x0a5c3ed1, 0x0b8416d1, 0x0b86ded1, 0x1d911ec1,
	0x0b843601, 0x0a843e11, 0x0bdf1601, 0x0b8c3e91, 0x0bfc1e91, 0x0b9c1e81, 0x0b9c1ea5, 0x0a843601,
};

// sign-extend an n-bit two's-complement value
static inline int32_t sext(uint32_t v, int n)
{
	uint32_t const m = 1u << (n - 1);
	return int32_t((v ^ m) - m);
}

// ---------------------------------------------------------------------------
// Instruction fields (verified band -> field map, rcc/RCC_DSP_GUIDE.md par.6) [V]
// ---------------------------------------------------------------------------
struct instr
{
	int  ram_a_addr_bit; // band 0  : one bit of the RAM-A serial address stream
	bool host_gate;      // band 4  : opband4 - host access window gate
	bool add0_cin;       // band 7  : delay-address increment (+1)
	int  opcode;         // bands 26-28 : datapath mode (dominant 010 = MAC)
	int  mulsrc;         // ~band25,~band24 : multiplier sample source select
	int  routing;        // bands 19,20 : output routing mux select
	int  addasel;        // bands 21..23 : accumulator A-operand select
	int  macneg;         // product negate control
	uint32_t word;
};

// The control-field decodes below are the EXACT verified netlist booleans
// (rcc/RCC_DSP_GUIDE.md par.4/par.6, equivalence-checked).  `ovf` is the
// accumulator overflow flag (add1_sum[28]) carried between steps.
static inline instr decode(uint32_t w, bool ovf)
{
	instr in{};
	in.word           = w;
	in.ram_a_addr_bit = (w >> 0) & 1;
	in.host_gate      = (w >> 4) & 1;                      // opband4 [V]
	in.add0_cin       = (w >> 7) & 1;                      // r_romq[7] [V]
	in.opcode         = ((w >> 26) & 1) | (((w >> 27) & 1) << 1) | (((w >> 28) & 1) << 2);
	// op_mulsrc1 = ~band25, op_mulsrc0 = ~band24 [V]
	in.mulsrc         = (int(!((w >> 25) & 1)) << 1) | int(!((w >> 24) & 1));
	// Product negate: the earlier band-11 guess is REFUTED by a hardware
	// constraint -- band 11 differs within one dry L/R slot pair, which would
	// put that voice's outputs in opposite phase on the real unit (the U-220
	// dry calibration shows coherent stereo).  Signal polarity is carried by
	// the sign-magnitude coefficient instead (see decode_coef; the D-70's
	// live image uses negative taps for its network).  No band negate.
	in.macneg         = 0;
	in.routing        = ((w >> 19) & 1) | (((w >> 20) & 1) << 1); // g373/g2947 [V]
	// op_addasel0 = band21 & band23 [V]; op_addasel1 = (~ovf & ~band21) |
	// (band21 & band22) [V, overflow-steered]
	int const a0 = ((w >> 21) & 1) & ((w >> 23) & 1);
	int const a1 = (int(!ovf) & int(!((w >> 21) & 1))) | (((w >> 21) & 1) & ((w >> 22) & 1));
	in.addasel        = (a1 << 1) | a0;
	return in;
}

// ---------------------------------------------------------------------------
// RAM-B parameter word layout (verified symbolically) [V]
//   [17:14] delay base   [13:8] bank/gain-range (bit8 = macalign)   [7:0] coef
// ---------------------------------------------------------------------------
struct param
{
	int delay_base;
	int bank_gain;
	int coef_code;
	bool macalign;
};

static inline param decode_param(uint32_t p)
{
	param q;
	q.delay_base = (p >> 14) & 0xf;
	q.bank_gain  = (p >> 8) & 0x3f;
	q.coef_code  = p & 0xff;
	q.macalign   = (p >> 8) & 1;
	return q;
}

// ---------------------------------------------------------------------------
// The machine.  One run_frame() = one audio sample frame.
// ---------------------------------------------------------------------------
struct engine
{
	// memories (RAM-A/RAM-B host-loadable; DRAM behavioral [H])
	int32_t  ram_a[32] = {};    // 32 x 24 working memory / delay line     [V]
	uint32_t ram_b[256] = {};   // 256 x 18 host parameter file            [V]
	int32_t  dram[1 << 16] = {}; // external delay memory (behavioral)     [H]

	uint32_t frame = 0;         // frame counter (drives the DRAM tap walk) [V]
	int32_t  out_l = 0;         // L output tap, latched at step 80        [V]
	int32_t  out_r = 0;         // R output tap, latched at step 137       [V]

	// Product alignment shifts, selected by op_macalign (param bit 8).
	// The dry-gain slots all carry bit 8 SET (parameter banks 0x01/0x41/...),
	// and the U-220 hardware calibration fixes their gain at byte/64, so
	// shift_align = 6 [V by HW cal].  The bit-8-clear slots in the firmware
	// boot image hold small mix constants (+16/+8) consistent with a
	// 16-equals-unity range, so shift_plain = 4 [H].
	int shift_plain = 4;
	int shift_align = 6;

	// Coefficient decode: SIGN-MAGNITUDE.  The U-220 dry-gain calibration
	// (roland_rcc.cpp) shows byte 0x40 = unity with shift_plain = 6, i.e.
	// gain = byte/64 over the positive range [V by HW cal].  The negative
	// half is sign-magnitude rather than two's-complement [H]: the firmware's
	// boot parameter image (progrom 0x1a000) fills the effect-network slots
	// with 0x80 = "-0" (muted) -- as two's-complement those would be -2.0
	// feedback taps, which cannot be a silent boot state.  The positive-only
	// hardware calibration cannot distinguish the two encodings.
	//
	// (The coefficient DFF *storage* is a companded/complemented scramble of the
	// byte - resolvable from the netlist under the hold-safe convention - but
	// the on-die NAND-tree reconstructs the multiplier input back to the byte.
	// The remaining refinement is the block-floating-point exponent scaling for
	// large accumulator magnitudes; not yet modeled. [S])
	static int32_t decode_coef(param const &q)
	{
		int32_t const mag = q.coef_code & 0x7f;
		return (q.coef_code & 0x80) ? -mag : mag;
	}

	// RAM-A serial address (verified: band-0 bitstream through a 7-tap shift
	// register -> h32_0 row decoders).  Reproduced directly from the last 5
	// band-0 bits; cross-validated against the gate-level netlist simulator
	// (exact across the whole steady state, steps 5-31 of each block; the 5
	// differing steps are the frame-start shift-register fill transient). [V]
	static int rama_addr(int step)
	{
		int a = 0;
		for (int k = 0; k < 5; ++k)
			a = (a << 1) | (PROGRAM_ROM[(step - 1 - k + 256) & 255] & 1);
		return a & 31;
	}

	// Wet (effect) return: the sum of the delay-network read-tap contributions
	// of the last frame, split L/R by program half (the L output bus is
	// assembled in the first half of the program, tap at step 80; R in the
	// second half, tap at step 137).  This is the reverb/delay send the
	// firmware wires up in the parameter file.  [H routing]
	int32_t wet_l = 0;
	int32_t wet_r = 0;

	// Run one sample frame.  `effect_in` = the effect-bus input sample (the
	// summed voice mix; the exact per-voice input-bus multiplexing is owned by
	// the PCM-side chip and is modeled as a generic mixer input).
	void run_frame(int32_t effect_in)
	{
		int32_t acc = 0;                             // 24-bit saturating accumulator
		wet_l = 0;
		wet_r = 0;
		for (int step = 0; step < 256; ++step)
		{
			instr const in = decode(PROGRAM_ROM[step], acc < 0); // ovf steer from acc sign [V]
			param const q  = decode_param(ram_b[step]);          // param auto-fetched by PC [V]

			// ---- memory read (RAM-A delay line) ----
			int const ra = rama_addr(step);
			int32_t const mem = ram_a[ra];                       // delay-line tap [V]

			// ---- multiplier sample source [S] ----
			int32_t const sample = (in.mulsrc == 1) ? effect_in : mem;

			// ---- multiply-accumulate ----
			// The runtime program uses three opcodes (b26,b27,b28): value 2
			// (010) x246, value 1 (100) x2, value 3 (110) x8.  Netlist trace:
			// bands 26-28 drive the coefficient NAND-trees + macalign (not a
			// separate capture/reset op) -- i.e. the opcode selects the
			// coefficient SCALING MODE (block-floating-point exponent).  All
			// three are MACs; 1/3 differ only in coef scaling, not yet modeled,
			// so they are treated as the plain MAC for now.  Routing (bands
			// 19,20 -> g373/g2947) selects the output bus.  [V structure]
			if (in.opcode == 0b010 || in.opcode == 0b100 || in.opcode == 0b110)
			{
				int32_t const coef = decode_coef(q);
				int64_t prod = (int64_t(sample) * coef) >> (q.macalign ? shift_align : shift_plain); // gain range [S]
				if (in.macneg)
					prod = -prod;                                // subtract mode [V control]
				// A-operand select (op_addasel, verified): 0/2 = accumulate,
				// 1 = load from the engine word, 3 = accumulator sign hold [V]
				if (in.addasel == 1)
					acc = int32_t(prod);                         // load (start of a sum)
				else
					acc += int32_t(prod);                        // accumulate
			}

			// ---- 24-bit signed saturation [V] ----
			if (acc >  0x7fffff) acc =  0x7fffff;
			if (acc < -0x800000) acc = -0x800000;

			// ---- RAM-A writeback [V] ----
			// GATED: the write-port row decoder (rect1613) is enabled by
			// ~band0(now), so a write fires only on steps whose band-0 bit is
			// 0 (~144/256 steps).  The wa==prev-ra relation is about which
			// cell the address points at, not whether the write is enabled.
			if (!in.ram_a_addr_bit)
				ram_a[ra] = acc;

			// ---- DRAM delay tap: address = base(param) + frame counter [V] ----
			// The parameter word's top 10 bits are a delay base in 64-sample
			// (2ms) units; the external DRAM is one 65536-sample ring walked by
			// the frame counter, so the delay between a write head at base_w
			// and a read tap at base_r is (base_w - base_r) * 64 samples.
			// Parameter bit 8 selects the slot's ROLE: 0 = write head (store
			// the accumulator scaled by the coefficient -- the send level),
			// 1 = read tap (mix the delayed content through the coefficient,
			// no store).  This split is taken from the D-70 firmware's live
			// parameter image, where bit-8-clear slots carry only small
			// positive send levels (+0.12..+0.25) and bit-8-set slots carry
			// the tap/feedback gains (+-0.75, +1.0, +1.25); it also prevents
			// read taps from scrubbing the ring.  Muted slots (coef +-0) are
			// inert.  [H behavioral: role bit, base width and micro-order
			// inferred from firmware images + audio plausibility; the
			// die-verified part is addr = param-base + frame counter on a
			// 4-step cadence]
			if (!in.host_gate && (step & 3) == 2)                // 4-step DRAM cadence [V]
			{
				int32_t const coef = decode_coef(q);
				if (coef != 0)
				{
					int const daddr = (((int(ram_b[step]) >> 8) << 6) + int(frame)) & 0xffff;
					if (!q.macalign)                             // bit 8 clear: write head
					{
						dram[daddr] = int32_t((int64_t(acc) * coef) >> 6);
					}
					else                                         // bit 8 set: read tap
					{
						int32_t const c = int32_t((int64_t(dram[daddr]) * coef) >> 6);
						acc += c;
						if (acc >  0x7fffff) acc =  0x7fffff;
						if (acc < -0x800000) acc = -0x800000;
						if (step < 128) wet_l += c; else wet_r += c;   // effect return [H routing]
					}
				}
			}

			// ---- output taps: L at step 80, R at step 137 (blocks 2/4) [V] ----
			if (step == 80)  out_l = acc;
			if (step == 137) out_r = acc;
		}
		++frame;
	}
};

} // namespace roland_rcc_dsp

#endif // MAME_SOUND_ROLAND_RCC_DSP_H
