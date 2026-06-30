// license:BSD-3-Clause
// copyright-holders:Giulio Zausa
#ifndef MAME_SOUND_ROLAND_D50FX_H
#define MAME_SOUND_ROLAND_D50FX_H

#pragma once


DECLARE_DEVICE_TYPE(ROLAND_D50_EFFECTS, roland_d50_effects_device)


// Preliminary model of the D-50's MB87126/MB87137 effect and output path.
//
// It consumes the firmware-facing IC8, IC9 and IC28 register writes.  The
// active key/output/reverb mode is also supplied provisionally while IC28's
// final output-matrix arithmetic is being recovered.
class roland_d50_effects_device : public device_t, public device_sound_interface
{
public:
	roland_d50_effects_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);

	void ic8_write(offs_t address, u8 data);
	void ic9_write(u8 address, u16 data);
	void ic28_write(u8 selector, u16 dc, u16 dd);
	void patch_mixer_write(u8 key_mode, u8 output_mode, u8 reverb_type, u8 reverb_balance,
		u8 total_volume, u8 tone_balance, u8 tone0_partial_mask, u8 tone0_partial_balance,
		u8 tone1_partial_mask, u8 tone1_partial_balance,
		u8 tone0_chorus_balance, u8 tone1_chorus_balance);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void sound_stream_update(sound_stream &stream) override;

private:
	static constexpr unsigned CHORUS_TONES = 2;
	static constexpr unsigned CHORUS_OPERATORS = 2;
	static constexpr unsigned CHORUS_DELAYS = 2;
	static constexpr unsigned CHORUS_LFOS = 4;
	static constexpr unsigned CHORUS_SIZE = 0x583;
	// Six uPD41416C-12 devices form the D-50's 16K x 24-bit delay memory.
	// d50lib allocates a 32K software ring for the family-wide abstraction,
	// but the -006 hardware exposes only fourteen address bits.
	static constexpr unsigned REVERB_SIZE = 16384;
	static constexpr unsigned REVERB_MASK = REVERB_SIZE - 1;

	void update_chorus_parameters(u8 address);
	void update_eq_parameters(u8 selector);
	void update_mixer_parameters();
	float chorus_lfo(unsigned tone, unsigned op, unsigned lfo);
	float chorus_delay(unsigned tone, unsigned op, unsigned delay, float modulation, float input);
	void chorus_operator(unsigned tone, unsigned op, float input, float (&output)[2]);
	void chorus_half(unsigned tone, float input, float (&output)[2]);
	void process_chorus(float &left, float &right);
	void process_eq(float &left, float &right);
	void process_reverb(float &left, float &right);
	void process_reverb_legacy(float &left, float &right);
	void process_reverb_mb87126(float &left, float &right);
	void process_final_eq(float &left, float &right);
	float mb87126_multiplier(u16 data) const;
	static s32 mb87126_clip24(s64 value);
	static s32 mb87126_clip28(s64 value);
	static s32 mb87126_multiply(s32 value, u16 coefficient);
	static s32 mb87126_accumulate(s32 accumulator, s32 value, u16 coefficient);
	static s32 mb87126_add(s32 accumulator, s32 value);
	static s32 mb87126_from_float(float value);
	static float mb87126_to_float(s32 value);

	sound_stream *m_stream = nullptr;
	optional_ioport m_reverb_model;

	u8 m_ic8_registers[8]{};
	u16 m_ic9_registers[256]{};
	u16 m_ic28_words[2][128]{};
	bool m_ic28_seen[2][128]{};

	float m_chorus_buffer[CHORUS_TONES][CHORUS_OPERATORS][CHORUS_DELAYS][CHORUS_SIZE]{};
	s16 m_chorus_read_position[CHORUS_TONES][CHORUS_OPERATORS][CHORUS_DELAYS]{};
	s16 m_chorus_write_position[CHORUS_TONES][CHORUS_OPERATORS][CHORUS_DELAYS]{};
	float m_chorus_lfo_phase[CHORUS_TONES][CHORUS_OPERATORS][CHORUS_LFOS]{};
	float m_chorus_lfo_step[CHORUS_TONES][CHORUS_OPERATORS][CHORUS_LFOS]{};
	float m_chorus_pan_depth[CHORUS_TONES][CHORUS_OPERATORS][2]{};
	float m_chorus_ramp_depth[CHORUS_TONES][CHORUS_OPERATORS][2]{};
	float m_chorus_stage_mix[CHORUS_TONES][8]{};
	float m_chorus_feedback_mix[CHORUS_TONES][2]{};
	float m_chorus_global[CHORUS_TONES][4]{};
	float m_chorus_feedback[CHORUS_TONES]{};
	float m_chorus_input_gain[2]{};
	float m_partial_mix[2][2]{};
	float m_chorus_dry_mix[2][2]{};
	float m_chorus_return_gain[2]{};

	// MB87126 family evidence establishes a 30-bit parameter word and a fixed
	// 35-bit x 192 program ROM.  DEP-5 initializes selectors 00-9f, but whether
	// the physical RAM has 160, 192, or 256 words remains unresolved.  The word
	// is a 16-bit delay-memory operand plus a signed 12-bit multiplier and two
	// MAC-range bits.  IC28 exposes the D-50's first 128 slots as 14-bit DC
	// (upper offset bits fixed low) and 14-bit DD lanes.  These are the high-level
	// states of the two fixed-ROM EQ programs; coefficients and output matrices
	// are recovered directly from DD.
	float m_eq_low_coeff[2][4]{};
	float m_eq_high_coeff[2][5]{};
	float m_eq_low_mix_target[2][2]{};
	float m_eq_high_mix_target[2][2]{};
	float m_eq_low_mix_current[2][2]{};
	float m_eq_high_mix_current[2][2]{};
	float m_eq_low_state[2][3]{};
	float m_eq_high_state[2][4]{};
	bool m_eq_low_valid[2]{};
	bool m_eq_high_valid[2]{};
	float m_final_eq_state[2][2]{};

	float m_output_gain = 1.0F;
	float m_output_reference[4]{};
	bool m_output_reference_valid = false;
	float m_reverb_direct = 0.8F;
	float m_reverb_wet = 0.0F;
	bool m_patch_mixer_valid = false;
	float m_reverb_delay[REVERB_SIZE]{};
	s32 m_reverb_tap[39]{};
	float m_reverb_coeff[55]{};
	float m_reverb_state[6]{};
	u16 m_reverb_overload_count = 0;
	bool m_reverb_faulted = false;
	bool m_reverb_dc_seen[39]{};
	bool m_reverb_dd_seen[55]{};
	bool m_reverb_ready = false;

	// Reconstructed fixed-point backend.  This is deliberately independent of
	// the legacy floating-point graph so both implementations can run in lockstep
	// and be selected at runtime for direct A/B testing.
	s32 m_mb87126_delay[REVERB_SIZE]{};
	u16 m_mb87126_offset[39]{};
	u16 m_mb87126_coefficient[55]{};
	s32 m_mb87126_state[6]{};
	u16 m_mb87126_delay_base = 0;
};

#endif // MAME_SOUND_ROLAND_D50FX_H
