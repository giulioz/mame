// license:BSD-3-Clause
// copyright-holders:giulioz
#ifndef MAME_SOUND_ROLAND_XP_DSP_H
#define MAME_SOUND_ROLAND_XP_DSP_H

#pragma once

#include <cstdint>

// Roland XP effect-DSP column ALU (pure, MAME-independent so it can be unit tested).
//
// The XP effect DSP is a free-running fixed-point MAC engine: every sample it runs a
// microprogram of up to 288 slots, top to bottom.  Each slot is a 28-bit PRAM word plus
// its paired 16-bit CRAM immediate.  The word carries:
//     st     [15:14]  memory-access mode (handled by the caller; see roland_xp.cpp)
//     word   [13:6]   memory word address
//     column [5:0]    the ALU operation selected here
//
// Machine state is three persistent registers plus a lock latch (hardware-proven,
// jv1080/re/XP_ARCH_REGISTERS.md, sessions rig_ground4-7 / rig_master / rig_chorus):
//   * acc     - one wide accumulator, saturates to signed 24-bit only when stored
//   * HOLD    - 24-bit operand latch, loaded by the fetch columns, consumed by the MACs
//   * COEFREG - coefficient register loaded by col 0x11/0x23/0x25, consumed by col 0x30
//   * acc_lock- set by col 0x25 (multiply-replace).  While locked, the replace and
//               multiply columns leave the accumulator holding the col 0x25 product;
//               only the accumulate columns (0x02/0x0f/0x2f/0x23/0x30) still add to it.
//               The multiply columns 0x13/0x15/0x19 re-arm (clear) the lock.
//
// CRAM decodes per-column: the multiply/MAC columns read Roland's "C14" float
// (value = sext14(m) << [0,1,2,4][exp] / 8192); the constant columns read a
// sign-extended integer lane (sext15(raw) << (raw15 ? 13 : 0)).
//
// OPEN (why roland_xp.cpp gates the DSP off the DAC for now): replaying the real
// boot program reveals the Stereo-EQ biquad state (IRAM3 D0-D3) diverges ~4x per
// sample, and col 0x30 reading COEFREG (hardware-proven, ground4-E/E3) then
// amplifies the railed state into the output.  The cross-slot col25-lock / COEFREG
// carry that the isolated rig probes never exercised is not yet fully pinned, so
// the EQ is not bit-correct.  The per-column ALU below is golden-validated; the
// remaining work is the biquad structure / register carry, not this table.

namespace roland_xp_dsp {

struct regs
{
	int64_t  acc = 0;       // wide accumulator (24-bit saturation applied on store)
	int32_t  hold = 0;      // 24-bit signed operand latch
	uint16_t coefreg = 0x5000; // raw C14 coef register, default +1.0.  The multiplier lane
	                        // applies it on EVERY mul/MAC column (0x13/0x15/0x19/0x23/0x25/…),
	                        // not just col 0x30 - hardware-proven probe_coefreg/probe_mulcoef.
	                        // Default +1.0 so it is invisible until a program loads it (col11/23/25).
	bool     acc_lock = false; // col 0x25 latch; re-armed only by col 0x13/0x15/0x19
	uint32_t eram_addr = 0; // ERAM read-address register (loaded by col 0x20)
};

// Saturate a wide value to signed 24-bit (the accumulator's store width and the
// multiplier output width).
static inline int32_t sat24(int64_t v)
{
	if (v > 0x7fffff)
		return 0x7fffff;
	if (v < -0x800000)
		return -0x800000;
	return int32_t(v);
}

// 14-bit sign-extended mantissa of a C14 coefficient.
static inline int32_t sext14(uint16_t raw)
{
	int32_t m = raw & 0x3fff;
	return (m & 0x2000) ? m - 0x4000 : m;
}

// C14 float multiply: x * (sext14(mant) << exp) / 8192, saturated to 24-bit.
// Anchors: 0x5000=+1.0, 0x2000=-1.0, 0x1000=+0.5, 0xE000=-16.0.
static inline int32_t c14_mul(int32_t x, uint16_t cram)
{
	static const int shift[4] = { 0, 1, 2, 4 };
	const int64_t m = sext14(cram);
	const int e = shift[(cram >> 14) & 3];
	return sat24((((int64_t)x * m) << e) >> 13);
}

// Multiplier-lane product: x * c14(cram) * c14(coefreg), saturated once at the output.
// Hardware-proven (probe_coefreg.py + probe_mulcoef.py, 2026-07-03): the persistent COEFREG
// multiplies on top of the column's own C14 coefficient for the entire mul/MAC lane
// (col 0x13/0x15/0x18/0x19/0x23/0x25/0x28/0x29).  With cram fixed the product scaled EXACTLY
// with COEFREG across a 5-point sweep; col15 c=1.0 gave acc*COEFREG, col25 c=1.0 gave HOLD*COEFREG.
// The original ground4/5/7 probes never saw this because COEFREG was +1.0 there.  The intermediate
// is kept wide; only the final result is clamped.  `coefreg` here is the value BEFORE this column's
// own COEFREG write (col23/25 update it as a side effect for the NEXT consumer).
static inline int32_t mac_mul(int32_t x, uint16_t cram, uint16_t coefreg)
{
	static const int shift[4] = { 0, 1, 2, 4 };
	int64_t p = (((int64_t)x * sext14(cram)) << shift[(cram >> 14) & 3]) >> 13;
	p = ((p * sext14(coefreg)) << shift[(coefreg >> 14) & 3]) >> 13;
	return sat24(p);
}

// Integer coefficient lane: sext15(raw[14:0]) << (raw[15] ? 13 : 0), saturated.
static inline int32_t int_lane(uint16_t cram)
{
	int32_t m = cram & 0x7fff;
	if (m & 0x4000)
		m -= 0x8000;
	return sat24((int64_t)m << ((cram & 0x8000) ? 13 : 0));
}

// Execute one column op against the register file.  `mem` is the memory operand the
// caller fetched for st1/st2 (0 and has_mem=false for st0/st3, where the read is
// suppressed).  The st3 store happens in the caller before this is invoked.
static inline void apply_column(regs &r, unsigned col, uint16_t cram, int32_t mem, bool has_mem)
{
	// The multiplier lane multiplies by the COEFREG value as it was BEFORE this column's
	// own COEFREG write (col11/23/25 update it below for the NEXT consumer).
	const uint16_t coefreg_prev = r.coefreg;

	// ---- HOLD / COEFREG / ERAM-address side effects (independent of the acc lock) ----
	switch (col)
	{
	case 0x00: if (has_mem) r.hold = mem; break;                         // fetch: HOLD <- mem
	case 0x01: if (has_mem) r.hold = c14_mul(mem, cram); break;          // scaled fetch (C=0 -> x0)
	case 0x03: if (has_mem) r.hold = cram ? c14_mul(mem, cram) : mem; break; // scaled fetch (C=0 -> x1)
	case 0x21: r.hold = c14_mul(r.hold, cram); break;                    // latch rescale (mem ignored)
	case 0x11: r.coefreg = cram; r.hold = 0; break;                      // COEFREG<-C, HOLD<-0
	case 0x20: r.eram_addr = uint32_t(r.acc & 0xffffff); break;          // ERAM read addr <- acc
	case 0x23: case 0x25: r.coefreg = cram; break;                       // MAC/mul also load COEFREG
	default: break;
	}

	// ---- accumulator write (proposed value + class, then lock gating) ----
	// HARDWARE-CORRECTED: the HOLD-consuming compute columns ALWAYS take their operand
	// from the persistent HOLD latch, even on an st1/st2 read slot -- the freshly-read
	// word is NOT consumed by these columns.  Only the fetch columns (0x00/0x01/0x03/0x21,
	// handled in the side-effect switch above) and col 0x30 (its own mem operand below)
	// consume the memory read; col25's own read explicitly does not even touch HOLD
	// (XP_ARCH_REGISTERS.md:17).  Proven C39-clean and ==MATCH by rig_ground4 F2/G,
	// rig_ground5 L, rig_ground7 R0.  The former `has_mem ? mem : r.hold` fed the read
	// cell into col23/col25 under st1 and diverged from silicon on 6/19 conformance
	// vectors (jv1080/re/debugrom/roland_xp_dsp_conformance.cpp).
	const int32_t opnd = r.hold;

	enum { NONE, ACC, REPL, MUL } cls = NONE;
	int64_t val = r.acc;

	switch (col)
	{
	// HOLD-only / keep: no accumulator write
	case 0x00: case 0x01: case 0x03: case 0x21: case 0x11:
	case 0x0b: case 0x10: case 0x20:
		cls = NONE; break;

	// accumulate family (acc += ...)
	case 0x02: val = r.acc + opnd; cls = ACC; break;                     // acc += operand
	case 0x0f: case 0x2f: val = r.acc + int_lane(cram); cls = ACC; break;// acc += int C
	case 0x23: val = r.acc + mac_mul(opnd, cram, coefreg_prev); cls = ACC; break; // acc += HOLD*C*COEFREG (MAC)
	case 0x30:                                                           // acc += HOLD + mem*2*COEFREG
	{
		static const int shift[4] = { 0, 1, 2, 4 };
		const int64_t m = sext14(r.coefreg);
		const int e = shift[(r.coefreg >> 14) & 3];
		const int32_t term = has_mem ? sat24((((int64_t)mem * m) << e) >> 12) : 0;
		val = r.acc + r.hold + term; cls = ACC; break;
	}

	// multiply family: writes AND re-arms (clears the col 0x25 lock).  The mul lane carries
	// the ×COEFREG factor (probe_mulcoef: col15 c=1.0 scaled exactly with COEFREG).
	case 0x13: val = r.acc + mac_mul(sat24(r.acc), cram, coefreg_prev); cls = MUL; break;      // acc + acc*C*COEFREG
	case 0x15: case 0x19: val = mac_mul(sat24(r.acc), cram, coefreg_prev); cls = MUL; break;   // acc*C*COEFREG
	case 0x18: val = (int64_t)mac_mul(sat24(r.acc), cram, coefreg_prev) - r.acc; cls = MUL; break; // acc*C*COEFREG - acc

	// replace family (overwrites acc; suppressed while locked)
	case 0x04: case 0x0a: case 0x14: case 0x1a:
	case 0x24: case 0x2a: case 0x34: case 0x3a:
		val = opnd; cls = REPL; break;                                  // acc = operand (mem or HOLD)
	case 0x06: case 0x16: case 0x26: case 0x36:
		val = -r.acc; cls = REPL; break;                                // acc = -acc
	case 0x07: case 0x17: case 0x27: case 0x37:
		val = (int64_t)opnd - r.acc; cls = REPL; break;                 // acc = operand - acc
	case 0x28: val = (int64_t)mac_mul(opnd, cram, coefreg_prev) - r.acc; cls = REPL; break; // -acc + HOLD*C*COEFREG
	case 0x29: val = (int64_t)opnd + mac_mul(opnd, cram, coefreg_prev); cls = REPL; break;  // HOLD + HOLD*C*COEFREG
	case 0x05: val = (int64_t)int_lane(cram) << 8; cls = REPL; break;   // acc = C<<8
	case 0x09: val = (int64_t)opnd + ((int64_t)int_lane(cram) << 8); cls = REPL; break; // operand + C<<8
	case 0x0e: val = 0; cls = REPL; break;                              // acc = 0
	case 0x1f: val = (int64_t)opnd + int_lane(cram); cls = REPL; break; // acc = operand + int C
	case 0x3f: val = (int64_t)int_lane(cram) - r.acc; cls = REPL; break;// acc = C - acc
	case 0x25: val = mac_mul(opnd, cram, coefreg_prev); cls = REPL; break; // acc = HOLD*C*COEFREG (also sets lock)

	// wide-multiply tap family (reverb/chorus taps) - deferred to phase 2, keep acc
	case 0x31: case 0x33: case 0x35: case 0x38: case 0x39:
		cls = NONE; break;

	default: cls = NONE; break;
	}

	// Lock model is UNRESOLVED: C40 (col23 blocked after col25) and hardware ground4-G (col23
	// DID write after col25 -> 0x330) contradict each other, and neither model fixes the EQ
	// biquad rail (that is a separate biquad-structure issue).  Keeping the original model,
	// which passes all 21 conformance vectors incl. ground4-G.  Revisit with a freeze-and-dump
	// lock rig (finding #2) once the biquad structure is understood.
	switch (cls)
	{
	case ACC:  r.acc = val; r.acc_lock = false; break;    // accumulate consumes the product, clears the lock
	case MUL:  if (!r.acc_lock) r.acc = val; r.acc_lock = false; break; // writes if unlocked; always re-arms
	case REPL: if (!r.acc_lock) r.acc = val; break;       // suppressed while locked (preserves the product)
	case NONE: default: break;
	}
	if (col == 0x25)
		r.acc_lock = true;                                // col 0x25 sets the lock
}

} // namespace roland_xp_dsp

#endif // MAME_SOUND_ROLAND_XP_DSP_H
