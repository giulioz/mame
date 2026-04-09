// license:BSD-3-Clause
// copyright-holders:giulioz
#ifndef MAME_SOUND_ROLAND_XP_H
#define MAME_SOUND_ROLAND_XP_H

#pragma once

#include "dirom.h"

// Roland XP PCM + DSP (MBCS30109B / MB87B105PF / RHR-2342)
//
// 64-voice sample player with integrated DSP for effects.
// 24-bit wave ROM address bus with 8 chip selects, 14-bit register address space.
// Connects to 2Mbit (256KB) of external DRAM for effects processing.
// Wave ROM uses a custom floating-point DPCM format.
//
// Register map (active areas, write side):
//   0x0000-0x00FF  voice control: wave bank, rom select, loop mode (4 bytes x 64 voices)
//   0x0100-0x01FF  sample start address (4 bytes x 64 voices)
//   0x0200-0x02FF  sample loop point   (4 bytes x 64 voices)
//   0x0300-0x03FF  sample end           (4 bytes x 64 voices)
//   0x1100-0x2100  ramp parameters (current, target, control)
//   0x2C00-0x38FF  DSP program and configuration
//   0x3900-0x39FF  global configuration and readback registers
//   0x3A00-0x3A7F  mixer send 0 (16-bit x 64 voices)
//   0x3A80-0x3AFF  mixer send 1
//   0x3B00-0x3B7F  mixer send 2
//   0x3B80-0x3BFF  mixer send 3
//   0x3C00-0x3C7F  mixer send 4
//
// Each mixer send entry (16 bits): bits 6-15 = level (10 bits), bits 0-5 = output bus (6 bits)

class roland_xp_device : public device_t, public device_sound_interface, public device_rom_interface<27>
{
public:
	static constexpr feature_type unemulated_features() { return feature::SOUND; }

	roland_xp_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	auto int_callback() { return m_int_callback.bind(); }

	u8 read(offs_t offset);
	void write(offs_t offset, u8 data);

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// device_sound_interface implementation
	virtual void sound_stream_update(sound_stream &stream) override;

	// device_rom_interface implementation
	virtual void rom_bank_pre_change() override;

private:
	static constexpr unsigned NUM_VOICES = 64;
	static constexpr unsigned NUM_MIXER_SENDS = 5;
	static constexpr unsigned DSP_PROGRAM_SIZE = 0x3900 - 0x2C00;   // 0x0D00 bytes
	static constexpr unsigned GLOBAL_CONFIG_SIZE = 0x100;
	static constexpr unsigned DRAM_SIZE = 0x40000;                   // 2Mbit = 256KB
	static constexpr unsigned REG_ARRAY_SIZE = 0x4000 / 4;          // full register space as 32-bit words

	struct pcm_voice
	{
		uint32_t wave_ctrl = 0;       // area 0x0000
		uint32_t sample_start = 0;    // area 0x0100, reused as current decode/read position
		uint32_t sample_loop = 0;     // area 0x0200
		uint32_t sample_end = 0;      // area 0x0300
		
		uint16_t mixer_send[NUM_MIXER_SENDS] = {};

		int32_t filter_type_select = 0;

		uint32_t pitch_current_val = 0;
		uint32_t pitch_target_val = 0;
		uint32_t pitch_interp_ctrl = 0;
		
		uint32_t amp_current_val = 0;
		uint32_t amp_target_val = 0;
		uint32_t amp_interp_ctrl = 0;
		
		uint32_t ampmod_current_val = 0;
		uint32_t ampmod_target_val = 0;
		uint32_t ampmod_interp_ctrl = 0;
		
		uint32_t tvf_f_current_val = 0;
		uint32_t tvf_f_target_val = 0;
		uint32_t tvf_f_interp_ctrl = 0;
		
		uint32_t tvf_q_current_val = 0;
		uint32_t tvf_q_target_val = 0;
		uint32_t tvf_q_interp_ctrl = 0;

		int32_t dpcm_val = 0;
		uint16_t subphase = 0;

		int32_t tvf_bp;
    	int32_t tvf_lp;
	};

	// Pitch/cutoff conversion: log value -> linear increment
	static int32_t pitch_to_increment(int32_t pitch_val);

	int32_t do_voice(pcm_voice &v);
	int32_t decode_sample(uint32_t sample_addr, uint32_t wave_ctrl);

	devcb_write_line m_int_callback;

	uint32_t m_rate;
	sound_stream *m_stream;
	pcm_voice m_voices[NUM_VOICES];

	// Full register array (for ramp current/target lookups across banks)
	uint32_t m_reg[REG_ARRAY_SIZE];

	// DSP program/config area (0x2C00-0x38FF)
	uint8_t m_dsp_program[DSP_PROGRAM_SIZE];

	// Global config area (0x3900-0x39FF)
	uint8_t m_global_config[GLOBAL_CONFIG_SIZE];

	// Effects DRAM (2Mbit)
	std::unique_ptr<uint8_t[]> m_dram;
};

DECLARE_DEVICE_TYPE(ROLAND_XP, roland_xp_device)

#endif // MAME_SOUND_ROLAND_XP_H
