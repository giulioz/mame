// license:BSD-3-Clause
// copyright-holders:giulioz
#ifndef MAME_SOUND_ROLAND_RCC_H
#define MAME_SOUND_ROLAND_RCC_H

#pragma once


// Roland RCC (TC23SC140AF) host interface, voice mixer and effects DSP.
//
// One class models the whole chip: the 288 x 29-bit mask-ROM program, the
// RAM-A working memory, the RAM-B host parameter file, the external DRAM
// delay memory, the host interface, and the per-frame program execution.
// The chip was fully reverse-engineered from die photos (netlist 100%
// equivalence-checked; see the rcc repo: RCC_DSP_GUIDE.md and
// db/RCC_FIELDMAP_V2.md), combined with hardware captures from a real
// U-220 / D-70 (test-mode routing, delay-time ladders, dry calibration).
//
// The mixer runs by EXECUTING the mask-ROM program against the firmware's
// RAM-B parameters: per step a MAC (A-operand + sample x coefficient),
// a four-register accumulator file, read-modify-write sums in RAM-A, and
// DAC strobes that capture the mix buses.  Panning/routing therefore come
// out of the same parameter words the real firmware writes (a5 targets,
// sign-magnitude coefficients).
//
// Inputs: the PCM chip's voices as 32 sound-stream channels (the real chip
// receives them time-multiplexed, one voice per 8-step program window).
// Outputs: four stereo pairs:
//   0/1 = mix L/R (executed-program mix bus -- the MIX OUT jacks)
//   2/3 = mirror of 0/1 (direct-out strobe map not yet identified)
//   4/5, 6/7 = silent until the DRAM delay engine (chorus/reverb) is
//              brought up from the program's b9/b14 transaction marks

class roland_rcc_device : public device_t, public device_sound_interface
{
public:
	static constexpr unsigned NUM_CHANNELS = 32;
	static constexpr unsigned NUM_OUTPUTS = 8;

	roland_rcc_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);

	roland_rcc_device &set_program_voice_offset(unsigned offset) { m_program_voice_offset = offset; return *this; }

	u8 read(offs_t offset);
	void write(offs_t offset, u8 data);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void sound_stream_update(sound_stream &stream) override;

private:
	// one pass of the 256-step mask-ROM program = one sample frame
	void run_program(s32 const *voices);
	static s32 decode_coef(u32 param);

	sound_stream *m_stream;

	// host interface registers and shadows
	u8 m_io[0x10];
	u8 m_program[0x100][3];
	u8 m_state[0x20][3];

	// chip memories and datapath state
	s32 m_ram_a[32];          // 32 x 24 working memory (sums, host cells)
	u32 m_ram_b[256];         // 256 x 18 parameter file (one word per step)
	s32 m_bank[4];            // four-register accumulator file
	s32 m_hist[2];            // result pipeline (accw = res two steps back)
	s8 m_dram[1 << 16];       // two 4464 DRAMs: 64K x 8 delay ring [V board]
	u32 m_frame;              // frame counter (delay-engine ring position)

	// DAC strobe captures
	s32 m_mix_l, m_mix_r;

	u8 m_program_voice_offset;

};

DECLARE_DEVICE_TYPE(ROLAND_RCC, roland_rcc_device)

#endif // MAME_SOUND_ROLAND_RCC_H
