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
//   0x1100-0x2100  ramp destinations, starting values and controls
//   0x2C00-0x38FF  DSP program and configuration
//   0x3900-0x39FF  global configuration and readback registers
//   0x3A00-0x3A7F  mixer send 0 (16-bit x 64 voices)
//   0x3A80-0x3AFF  mixer send 1
//   0x3B00-0x3B7F  mixer send 2
//   0x3B80-0x3BFF  mixer send 3
//   0x3C00-0x3FFF  host wave-ROM read aperture (1 KiB window)
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

	// Debug: side-effect-free read of live DSP/config/mixer/voice state.  Unlike read(),
	// this does NOT arm the readback latch, advance the RNG, or update the stream.  It
	// returns the byte actually held in the device's arrays (incl. ramp-evolved IRAM3).
	// Used by the driver's periodic DSP dump.
	u8 dbg_peek(offs_t offset) const;

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
	static constexpr unsigned NUM_MIXER_SENDS = 4;
	static constexpr unsigned NUM_IRAM3_BREAKPOINTS = 64;
	static constexpr unsigned IRQ_QUEUE_SIZE = 64;
	static constexpr unsigned DSP_PROGRAM_SIZE = 0x3900 - 0x2C00;   // 0x0D00 bytes
	static constexpr unsigned GLOBAL_CONFIG_SIZE = 0x100;
	static constexpr unsigned DRAM_SIZE = 0x40000;                   // 2Mbit = 256KB
	static constexpr unsigned REG_ARRAY_SIZE = 0x4000 / 4;          // full register space as 32-bit words

	struct interp_state
	{
		uint16_t flags = 0;
		int16_t rate = 0;
		uint32_t counter = 0;
		int32_t current = 0;
		int32_t target = 0;
		int32_t aux = 0;
		int32_t output_i = 0;
	};

	struct pcm_voice
	{
		uint32_t wave_ctrl = 0;       // area 0x0000
		uint32_t sample_start = 0;    // area 0x0100
		uint32_t sample_loop = 0;     // area 0x0200
		uint32_t sample_end = 0;      // area 0x0300
		
		uint16_t mixer_send[NUM_MIXER_SENDS] = {};

		int32_t filter_type_select = 0;

		uint32_t pitch_destination_val = 0; // area 0x1200
		uint32_t pitch_start_val = 0;       // area 0x1b00
		uint32_t pitch_interp_ctrl = 0;
		
		uint32_t amp_destination_val = 0; // area 0x1500, max:0x1ffc0?
		uint32_t amp_start_val = 0;       // area 0x1e00
		uint32_t amp_interp_ctrl = 0;
		
		uint32_t ampmod_destination_val = 0; // area 0x1400, max:0x20000?
		uint32_t ampmod_start_val = 0;       // area 0x1d00
		uint32_t ampmod_interp_ctrl = 0;
		
		uint32_t tvf_f_destination_val = 0; // area 0x1300, max(open):0x3c000 min:0x14000
		uint32_t tvf_f_start_val = 0;       // area 0x1c00
		uint32_t tvf_f_interp_ctrl = 0;
		
		uint32_t tvf_q_destination_val = 0; // area 0x1100, max(no resonance):0x80000
		uint32_t tvf_q_start_val = 0;       // area 0x2100, shifted right two on start
		uint32_t tvf_q_interp_ctrl = 0;

		uint32_t current_addr = 0;    // runtime decode/read position
		int32_t dpcm_val = 0;
		uint16_t subphase = 0;
		bool alt_loop_dir = false;
		bool playing = false;
		bool loop_irq_pending = false;

		int32_t tvf_bp;
		int32_t tvf_lp;

		interp_state pitch_interp;
		interp_state amp_interp;
		interp_state ampmod_interp;
		interp_state tvf_q_interp;
		interp_state tvf_f_interp;
	};

	// Pitch/cutoff conversion: log value -> linear increment
	static int32_t pitch_to_increment(int32_t pitch_val);
	static uint16_t decode_interp_flags(uint32_t ctrl, bool force_log = false);
	static uint32_t interp_mask(uint16_t flags);
	static int16_t interp_q14_output(const interp_state &s);
	static int16_t limit_tvf_frequency(int16_t f_q14, const interp_state &q);
	static int decode_filter_type(const pcm_voice &v);
	static void interp_start_linear(interp_state &s);
	static void interp_start_log(interp_state &s);
	static void interp_start_trunk(interp_state &s);
	static void interp_set_newdist_linear(interp_state &s);
	static bool interp_tick_due(interp_state &s);
	static void interp_update_linear(interp_state &s);
	static void interp_update_log(interp_state &s);
	static void interp_update_trunk(interp_state &s);
	static void interp_update_pitch(interp_state &s);
	static void interp_update_f(interp_state &s);
	static void interp_update_q(interp_state &s);
	static void interp_update_a(interp_state &s);
	static void interp_update_am(interp_state &s);
	void reload_pitch_interp(pcm_voice &v);
	void reload_amp_interp(pcm_voice &v);
	void reload_ampmod_interp(pcm_voice &v);
	void reload_tvf_q_interp(pcm_voice &v);
	void reload_tvf_f_interp(pcm_voice &v);
	void reload_all_interps(pcm_voice &v);
	void retarget_pitch_interp(pcm_voice &v);
	void retarget_amp_interp(pcm_voice &v);
	void retarget_ampmod_interp(pcm_voice &v);
	void retarget_tvf_q_interp(pcm_voice &v);
	void retarget_tvf_f_interp(pcm_voice &v);

	void reset_voice_runtime(pcm_voice &v);
	void update_irq_line();
	void raise_irq(uint8_t reason, uint8_t source, uint16_t data = 0);
	void acknowledge_irq();
	uint16_t current_irq_status() const;
	uint16_t current_irq_data() const;
	uint8_t read_command_status();
	uint16_t next_random();
	uint32_t dsp_read_u32(offs_t offset) const;
	uint16_t dsp_read_u16(offs_t offset) const;
	void dsp_write_u32(offs_t offset, uint32_t data);
	void update_iram3_breakpoints();
	int32_t do_voice(pcm_voice &v, bool control_tick_2, bool control_tick_8, bool &voice_event);
	int32_t decode_sample(uint32_t sample_addr, uint32_t wave_ctrl);

	devcb_write_line m_int_callback;

	uint32_t m_rate;
	sound_stream *m_stream;
	uint8_t m_control_phase;
	pcm_voice m_voices[NUM_VOICES];

	// Full register array (for ramp current/target lookups across banks)
	uint32_t m_reg[REG_ARRAY_SIZE];

	// DSP program/config area (0x2C00-0x38FF)
	uint8_t m_dsp_program[DSP_PROGRAM_SIZE];
	uint8_t m_dsp_read_latch[4];
	uint8_t m_dsp_read_latch_mask;
	uint8_t m_command_busy_reads;
	uint16_t m_random_seed1;
	uint16_t m_random_seed2;
	bool m_iram3_breakpoint_active[NUM_IRAM3_BREAKPOINTS];

	// IRQ7 is level-triggered.  Status contains source in bits 13-8 and reason
	// in bits 3-0; reading the low byte of 0x391a acknowledges the head event.
	uint16_t m_irq_status[IRQ_QUEUE_SIZE];
	uint16_t m_irq_data[IRQ_QUEUE_SIZE];
	uint8_t m_irq_head;
	uint8_t m_irq_count;
	bool m_irq_line;
	uint16_t m_irq_control;

	// Global config area (0x3900-0x39FF)
	uint8_t m_global_config[GLOBAL_CONFIG_SIZE];

	// Effects DRAM (2Mbit)
	std::unique_ptr<uint8_t[]> m_dram;
};

DECLARE_DEVICE_TYPE(ROLAND_XP, roland_xp_device)

#endif // MAME_SOUND_ROLAND_XP_H
