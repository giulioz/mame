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
// equivalence-checked; see the rcc repo: RCC_DSP_GUIDE.md), combined with
// hardware calibration measured on a real U-220.
//
// Inputs: the PCM chip's voices as 32 sound-stream channels (the real chip
// receives them time-multiplexed on a 7-bit x 3-phase input bus).
// Outputs: four stereo pairs, matching the chip's program-counter-selected
// 4-way DAC output mux:
//   0/1 = mix L/R (dry + chorus + effect return -- the MIX OUT jacks)
//   2/3 = dry L/R
//   4/5 = chorus L/R
//   6/7 = effect (reverb/delay network) return L/R

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
	void run_program(s32 effect_in);
	static int rama_addr(int step);
	static s32 decode_coef(u32 param);

	void update_dry_gain(u8 index);

	sound_stream *m_stream;

	// host interface registers and shadows
	u8 m_io[0x10];
	u8 m_program[0x100][3];
	u8 m_state[0x20][3];

	// chip memories
	s32 m_ram_a[32];          // 32 x 24 working memory / delay line
	u32 m_ram_b[256];         // 256 x 18 parameter file (one word per step)
	s8 m_dram[1 << 16];       // two 4464 DRAMs: 64K x 8-bit delay ring [V board]
	u32 m_frame;              // frame counter (walks the DRAM ring)

	// per-frame program results
	s32 m_wet_l, m_wet_r;     // delay-network read-tap returns
	float m_effect_dc = 0.0F; // effect-input DC blocker state

	// hardware-calibrated dry mixer state
	float m_gain[NUM_CHANNELS][2];
	u8 m_program_voice_offset;

};

DECLARE_DEVICE_TYPE(ROLAND_RCC, roland_rcc_device)

#endif // MAME_SOUND_ROLAND_RCC_H
