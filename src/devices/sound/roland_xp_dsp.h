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

namespace roland_xp_dsp {

struct regs
{
	int64_t  acc = 0;       // wide accumulator (24-bit saturation applied on store)
	int32_t  hold = 0;      // 24-bit signed operand latch
	uint16_t coefreg = 0;   // raw C14 coefficient register (consumed by col 0x30)
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
	// The HOLD-consuming columns take their operand from the memory read when the
	// slot is a read (st1/st2), otherwise from the persistent HOLD latch.  This is
	// why col04/14/24/34 mean "acc = mem" under a read yet "acc = HOLD" acc-only,
	// and why the biquad's st1 col23/col25 MAC the freshly-read filter state.
	// (col 0x30 is the exception: it uses HOLD *and* mem as separate operands.)
	const int32_t opnd = has_mem ? mem : r.hold;

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
	case 0x23: val = r.acc + c14_mul(opnd, cram); cls = ACC; break;      // acc += operand*C (MAC)
	case 0x30:                                                           // acc += HOLD + mem*2*COEFREG
	{
		static const int shift[4] = { 0, 1, 2, 4 };
		const int64_t m = sext14(r.coefreg);
		const int e = shift[(r.coefreg >> 14) & 3];
		const int32_t term = has_mem ? sat24((((int64_t)mem * m) << e) >> 12) : 0;
		val = r.acc + r.hold + term; cls = ACC; break;
	}

	// multiply family: writes AND re-arms (clears the col 0x25 lock)
	case 0x13: val = r.acc + c14_mul(sat24(r.acc), cram); cls = MUL; break;      // acc*(1+C)
	case 0x15: case 0x19: val = c14_mul(sat24(r.acc), cram); cls = MUL; break;   // acc*C
	case 0x18: val = (int64_t)c14_mul(sat24(r.acc), cram) - r.acc; cls = MUL; break; // acc*C - acc

	// replace family (overwrites acc; suppressed while locked)
	case 0x04: case 0x0a: case 0x14: case 0x1a:
	case 0x24: case 0x2a: case 0x34: case 0x3a:
		val = opnd; cls = REPL; break;                                  // acc = operand (mem or HOLD)
	case 0x06: case 0x16: case 0x26: case 0x36:
		val = -r.acc; cls = REPL; break;                                // acc = -acc
	case 0x07: case 0x17: case 0x27: case 0x37:
		val = (int64_t)opnd - r.acc; cls = REPL; break;                 // acc = operand - acc
	case 0x28: val = (int64_t)c14_mul(opnd, cram) - r.acc; cls = REPL; break; // acc = -acc + operand*C
	case 0x29: val = (int64_t)opnd + c14_mul(opnd, cram); cls = REPL; break;  // acc = operand*(1+C)
	case 0x05: val = (int64_t)int_lane(cram) << 8; cls = REPL; break;   // acc = C<<8
	case 0x09: val = (int64_t)opnd + ((int64_t)int_lane(cram) << 8); cls = REPL; break; // operand + C<<8
	case 0x0e: val = 0; cls = REPL; break;                              // acc = 0
	case 0x1f: val = (int64_t)opnd + int_lane(cram); cls = REPL; break; // acc = operand + int C
	case 0x3f: val = (int64_t)int_lane(cram) - r.acc; cls = REPL; break;// acc = C - acc
	case 0x25: val = c14_mul(opnd, cram); cls = REPL; break;            // acc = operand*C (also sets lock)

	// wide-multiply tap family (reverb/chorus taps) - deferred to phase 2, keep acc
	case 0x31: case 0x33: case 0x35: case 0x38: case 0x39:
		cls = NONE; break;

	default: cls = NONE; break;
	}

	switch (cls)
	{
	case ACC:  r.acc = val; break;                        // accumulate always writes
	case MUL:  if (!r.acc_lock) r.acc = val; r.acc_lock = false; break; // writes if unlocked; always re-arms
	case REPL: if (!r.acc_lock) r.acc = val; break;       // suppressed while locked
	case NONE: default: break;
	}
	if (col == 0x25)
		r.acc_lock = true;                                // col 0x25 sets the lock
}

} // namespace roland_xp_dsp

#endif // MAME_SOUND_ROLAND_XP_DSP_H
