// license:GPL-2.0
// copyright-holders:giulioz
#ifndef MAME_SOUND_ROLAND_TVF_H
#define MAME_SOUND_ROLAND_TVF_H

#pragma once

#include <array>

class mb87424_device : public device_t, public device_sound_interface
{
public:
	mb87424_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);
	// Stream slots 0..31 preserve voice order from LP to RCC, scaled by 1/131072.
	// Select source bits 1..17 and the supplied sign bit for the 18-bit input bus.
	void set_input_bus(u8 sign_bit) { m_input_sign_bit = sign_bit; }
	u8 read(offs_t offset);
	void write(offs_t offset, u8 data);
	s32 voice_r(offs_t voice);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_clock_changed() override;
	virtual void sound_stream_update(sound_stream &stream) override;

private:
	// Chip state and arithmetic.

	static constexpr size_t VOICE_SLOTS = 32;

	void reset_state();

	void register_w(uint16_t offset, uint8_t value);
	uint8_t register_r(uint16_t offset) const;

	template <size_t VoiceCount> void process_frame(const int32_t *in_voices, int32_t *out_voices);

	struct voice_state
	{
		// RAM A (24 bit words, 4 per voice)
		int32_t svf_state_band_00;  // SVF BP integrator, signed
		int32_t svf_state_high_04;  // SVF HP companion state, signed
		uint32_t filter_control_08; // [20:8] damping Q12, [7:0] flags byte
		int32_t svf_state_low_0c;   // SVF LP integrator, signed

		// RAM B (20 bit words, 6 per voice)
		uint32_t cutoff_current_20;
		uint32_t gain_current_24;
		int32_t cutoff_step_28; // signed glide distance per tick
		int32_t gain_step_2c;
		uint32_t cutoff_target_30; // [19] snap flag, [18:4] Q14 destination
		uint32_t gain_target_34;   // [19] snap flag, [18:4] Q14 destination

		bool cutoff_gliding;
		bool gain_gliding;
	};

	enum filter_mode : uint32_t
	{
		MODE_LP = 0,
		MODE_HP = 1,
		MODE_BP = 2
	};
	enum structure : uint32_t
	{
		STRUCT_SINGLE = 0,    // A -> TVF A -> TVA A
		STRUCT_SERIAL = 1,    // A + B -> TVF A -> TVF B -> TVA B
		STRUCT_RING_PRE = 2,  // A -> TVA A -> RING(B) -> TVF A -> TVF B -> TVA B
		STRUCT_RING_POST = 3, // A -> TVF A -> TVA A -> RING(B -> TVF B) -> TVA B
	};

	static constexpr uint32_t TARGET_SNAP = 1u << 19;
	static constexpr uint32_t TARGET_VALUE = 0x7ffff;

	void write16(uint16_t offset, uint16_t value);
	uint32_t read_port24() const;

	bool ramp_enabled() const;
	static int32_t glide_step(int32_t dist20, uint8_t rate);
	static void arm_glide(int32_t cur20, int32_t tgt20, uint8_t rate, int32_t *step20, bool *gliding);
	int32_t filter_sample(voice_state &voice, int32_t input);

	uint8_t m_global[0x80];
	voice_state m_voices[VOICE_SLOTS];

	uint32_t m_active_mask; // 0x10 (hi) / 0x12 (lo) voice bitmap
	uint16_t m_ramp_enable; // 0x14
	uint8_t m_glide_rate;   // 0x18, low byte only (high byte + bits[7:5] inert)

	// Previous frame's active mask: a bit's rising edge delivers the voice's
	// pending target words to the ramp (SET snaps, CLEAR glides).
	uint32_t m_prev_active_mask;
	// Frame parity: glide steps apply every 2nd frame (fs/2).
	unsigned m_glide_tick_phase;
	uint8_t m_selected_voice;
	// Read-port source: any word write to a selector offset 0x40..0x7e re-points
	// the port at (per-voice register, context) -- last write wins.
	uint8_t m_port_reg;
	uint8_t m_port_voice;
	uint32_t m_port_latch;
	bool m_little_endian_bus = true;

	sound_stream *m_stream = nullptr;
	std::array<s32, 32> m_inputs{}, m_outputs{};
	u8 m_input_sign_bit = 0;
};

DECLARE_DEVICE_TYPE(MB87424, mb87424_device)

#endif // MAME_SOUND_ROLAND_TVF_H
