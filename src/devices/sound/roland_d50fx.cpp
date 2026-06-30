// license:BSD-3-Clause
// copyright-holders:Giulio Zausa

#include "emu.h"
#include "roland_d50fx.h"

#include <algorithm>
#include <cmath>


DEFINE_DEVICE_TYPE(ROLAND_D50_EFFECTS, roland_d50_effects_device, "roland_d50fx", "Roland D-50 Effects")


namespace {

// ROM routine 64C5 scatters the 39 delay offsets and 55 coefficients in
// this selector order.  The destination permutations come independently
// from d50lib's recovered CEmuReverb::CalcOfstCoef object layout.
constexpr u8 s_reverb_dc_selector[39] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x09,
	0x0a, 0x0b, 0x0c, 0x0e, 0x10, 0x15, 0x17, 0x1b,
	0x26, 0x28, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b,
	0x3d, 0x41, 0x42, 0x44, 0x48, 0x4a, 0x4b, 0x4e,
	0x51, 0x52, 0x54, 0x59, 0x60, 0x77, 0x78
};

constexpr u8 s_reverb_dc_destination[39] = {
	1, 11, 3, 20, 0, 5, 15, 2, 7, 19, 4, 9, 6, 8, 14, 10,
	18, 13, 17, 12, 34, 28, 25, 31, 23, 26, 29, 32, 21, 24,
	35, 38, 27, 22, 30, 33, 36, 37, 16
};

constexpr u8 s_reverb_dd_selector[55] = {
	0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
	0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x12, 0x13,
	0x16, 0x19, 0x1d, 0x2d, 0x2f, 0x38, 0x3a, 0x3b,
	0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x42, 0x43, 0x44,
	0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c,
	0x4d, 0x4e, 0x4f, 0x52, 0x53, 0x54, 0x55, 0x56,
	0x57, 0x58, 0x5a, 0x5b, 0x5e, 0x7a, 0x7d
};

constexpr u8 s_reverb_dd_destination[55] = {
	2, 6, 0, 1, 7, 8, 10, 5, 9, 24, 19, 11, 12, 23, 13, 14,
	15, 4, 18, 22, 17, 21, 16, 33, 29, 27, 31, 3, 26, 37,
	38, 41, 40, 39, 28, 42, 43, 44, 30, 32, 46, 47, 45, 48,
	50, 49, 34, 36, 51, 25, 52, 53, 54, 35, 20
};

constexpr u8 s_total_volume_selector[4] = { 0x01, 0x39, 0x41, 0x79 };

// The firmware tables at B06D-B098 contain byte offsets into the 16-bit DD
// shadow.  Dividing those offsets by two gives the MB87126 parameter slots.
// Tone 0 is lower/left and tone 1 is upper/right.  Adjacent MAC operations
// hold high and low fragments of each effective IIR coefficient.  Exhaustive
// comparison of the firmware tables with d50lib proves the /64 recombination
// used below.
constexpr u8 s_eq_low_coeff_selector[2][8] = {
	{ 0x59, 0x5c, 0x5d, 0x62, 0x63, 0x64, 0x65, 0x66 },
	{ 0x18, 0x17, 0x15, 0x14, 0x1a, 0x1b, 0x1c, 0x1e }
};
constexpr u8 s_eq_low_mix_selector[2][2] = {
	{ 0x69, 0x68 },
	{ 0x22, 0x23 }
};
constexpr u8 s_eq_high_coeff_selector[2][10] = {
	{ 0x67, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x72, 0x73, 0x78 },
	{ 0x28, 0x25, 0x24, 0x27, 0x26, 0x29, 0x2a, 0x2b, 0x2c, 0x2e }
};
constexpr u8 s_eq_high_mix_selector[2][2] = {
	{ 0x7b, 0x7c },
	{ 0x32, 0x33 }
};

inline s16 signed_12(u16 value)
{
	return s16(value << 4) >> 4;
}

inline float finite_clip(float value, float limit = 8.0F)
{
	return std::isfinite(value) ? std::clamp(value, -limit, limit) : 0.0F;
}

} // anonymous namespace


roland_d50_effects_device::roland_d50_effects_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, ROLAND_D50_EFFECTS, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, m_reverb_model(*this, ":REVERB_MODEL")
{
}

void roland_d50_effects_device::device_start()
{
	m_stream = stream_alloc(8, 2, 32'000, STREAM_SYNCHRONOUS);

	save_item(NAME(m_ic8_registers));
	save_item(NAME(m_ic9_registers));
	save_item(NAME(m_ic28_words));
	save_item(NAME(m_ic28_seen));
	save_item(NAME(m_chorus_buffer));
	save_item(NAME(m_chorus_read_position));
	save_item(NAME(m_chorus_write_position));
	save_item(NAME(m_chorus_lfo_phase));
	save_item(NAME(m_chorus_lfo_step));
	save_item(NAME(m_chorus_pan_depth));
	save_item(NAME(m_chorus_ramp_depth));
	save_item(NAME(m_chorus_stage_mix));
	save_item(NAME(m_chorus_feedback_mix));
	save_item(NAME(m_chorus_global));
	save_item(NAME(m_chorus_feedback));
	save_item(NAME(m_chorus_input_gain));
	save_item(NAME(m_partial_mix));
	save_item(NAME(m_chorus_dry_mix));
	save_item(NAME(m_chorus_return_gain));
	save_item(NAME(m_eq_low_coeff));
	save_item(NAME(m_eq_high_coeff));
	save_item(NAME(m_eq_low_mix_target));
	save_item(NAME(m_eq_high_mix_target));
	save_item(NAME(m_eq_low_mix_current));
	save_item(NAME(m_eq_high_mix_current));
	save_item(NAME(m_eq_low_state));
	save_item(NAME(m_eq_high_state));
	save_item(NAME(m_eq_low_valid));
	save_item(NAME(m_eq_high_valid));
	save_item(NAME(m_final_eq_state));
	save_item(NAME(m_output_gain));
	save_item(NAME(m_output_reference));
	save_item(NAME(m_output_reference_valid));
	save_item(NAME(m_reverb_direct));
	save_item(NAME(m_reverb_wet));
	save_item(NAME(m_patch_mixer_valid));
	save_item(NAME(m_reverb_delay));
	save_item(NAME(m_reverb_tap));
	save_item(NAME(m_reverb_coeff));
	save_item(NAME(m_reverb_state));
	save_item(NAME(m_reverb_overload_count));
	save_item(NAME(m_reverb_faulted));
	save_item(NAME(m_reverb_dc_seen));
	save_item(NAME(m_reverb_dd_seen));
	save_item(NAME(m_reverb_ready));
	save_item(NAME(m_mb87126_delay));
	save_item(NAME(m_mb87126_offset));
	save_item(NAME(m_mb87126_coefficient));
	save_item(NAME(m_mb87126_state));
	save_item(NAME(m_mb87126_delay_base));
}

void roland_d50_effects_device::device_reset()
{
	std::fill(std::begin(m_ic8_registers), std::end(m_ic8_registers), 0);
	std::fill(std::begin(m_ic9_registers), std::end(m_ic9_registers), 0);
	std::fill(&m_ic28_words[0][0], &m_ic28_words[0][0] + 2 * 128, 0);
	std::fill(&m_ic28_seen[0][0], &m_ic28_seen[0][0] + 2 * 128, false);
	std::fill(&m_chorus_buffer[0][0][0][0], &m_chorus_buffer[0][0][0][0] + CHORUS_TONES * CHORUS_OPERATORS * CHORUS_DELAYS * CHORUS_SIZE, 0.0F);
	std::fill(&m_chorus_read_position[0][0][0], &m_chorus_read_position[0][0][0] + CHORUS_TONES * CHORUS_OPERATORS * CHORUS_DELAYS, 0);
	std::fill(&m_chorus_write_position[0][0][0], &m_chorus_write_position[0][0][0] + CHORUS_TONES * CHORUS_OPERATORS * CHORUS_DELAYS, 0);
	std::fill(&m_chorus_lfo_phase[0][0][0], &m_chorus_lfo_phase[0][0][0] + CHORUS_TONES * CHORUS_OPERATORS * CHORUS_LFOS, 0.0F);
	std::fill(&m_chorus_lfo_step[0][0][0], &m_chorus_lfo_step[0][0][0] + CHORUS_TONES * CHORUS_OPERATORS * CHORUS_LFOS, 0.0F);
	std::fill(&m_chorus_pan_depth[0][0][0], &m_chorus_pan_depth[0][0][0] + CHORUS_TONES * CHORUS_OPERATORS * 2, 0.0F);
	std::fill(&m_chorus_ramp_depth[0][0][0], &m_chorus_ramp_depth[0][0][0] + CHORUS_TONES * CHORUS_OPERATORS * 2, 0.0F);
	std::fill(&m_chorus_stage_mix[0][0], &m_chorus_stage_mix[0][0] + CHORUS_TONES * 8, 0.0F);
	std::fill(&m_chorus_feedback_mix[0][0], &m_chorus_feedback_mix[0][0] + CHORUS_TONES * 2, 0.0F);
	std::fill(&m_chorus_global[0][0], &m_chorus_global[0][0] + CHORUS_TONES * 4, 0.0F);
	std::fill(std::begin(m_chorus_feedback), std::end(m_chorus_feedback), 0.0F);
	std::fill(std::begin(m_chorus_input_gain), std::end(m_chorus_input_gain), 0.0F);
	std::fill(&m_partial_mix[0][0], &m_partial_mix[0][0] + 4, 1.0F);
	std::fill(&m_chorus_dry_mix[0][0], &m_chorus_dry_mix[0][0] + 4, 0.0F);
	std::fill(std::begin(m_chorus_return_gain), std::end(m_chorus_return_gain), 0.0F);
	std::fill(&m_eq_low_coeff[0][0], &m_eq_low_coeff[0][0] + 2 * 4, 0.0F);
	std::fill(&m_eq_high_coeff[0][0], &m_eq_high_coeff[0][0] + 2 * 5, 0.0F);
	std::fill(&m_eq_low_mix_target[0][0], &m_eq_low_mix_target[0][0] + 2 * 2, 0.0F);
	std::fill(&m_eq_high_mix_target[0][0], &m_eq_high_mix_target[0][0] + 2 * 2, 0.0F);
	std::fill(&m_eq_low_mix_current[0][0], &m_eq_low_mix_current[0][0] + 2 * 2, 0.0F);
	std::fill(&m_eq_high_mix_current[0][0], &m_eq_high_mix_current[0][0] + 2 * 2, 0.0F);
	std::fill(&m_eq_low_state[0][0], &m_eq_low_state[0][0] + 2 * 3, 0.0F);
	std::fill(&m_eq_high_state[0][0], &m_eq_high_state[0][0] + 2 * 4, 0.0F);
	std::fill(std::begin(m_eq_low_valid), std::end(m_eq_low_valid), false);
	std::fill(std::begin(m_eq_high_valid), std::end(m_eq_high_valid), false);
	std::fill(&m_final_eq_state[0][0], &m_final_eq_state[0][0] + 2 * 2, 0.0F);
	m_output_gain = 1.0F;
	std::fill(std::begin(m_output_reference), std::end(m_output_reference), 0.0F);
	m_output_reference_valid = false;
	m_reverb_direct = 0.8F;
	m_reverb_wet = 0.0F;
	m_patch_mixer_valid = false;
	std::fill(std::begin(m_reverb_delay), std::end(m_reverb_delay), 0.0F);
	std::fill(std::begin(m_reverb_tap), std::end(m_reverb_tap), 0);
	std::fill(std::begin(m_reverb_coeff), std::end(m_reverb_coeff), 0.0F);
	std::fill(std::begin(m_reverb_state), std::end(m_reverb_state), 0.0F);
	m_reverb_overload_count = 0;
	m_reverb_faulted = false;
	std::fill(std::begin(m_reverb_dc_seen), std::end(m_reverb_dc_seen), false);
	std::fill(std::begin(m_reverb_dd_seen), std::end(m_reverb_dd_seen), false);
	m_reverb_ready = false;
	std::fill(std::begin(m_mb87126_delay), std::end(m_mb87126_delay), 0);
	std::fill(std::begin(m_mb87126_offset), std::end(m_mb87126_offset), 0);
	std::fill(std::begin(m_mb87126_coefficient), std::end(m_mb87126_coefficient), 0);
	std::fill(std::begin(m_mb87126_state), std::end(m_mb87126_state), 0);
	m_mb87126_delay_base = 0;
}

void roland_d50_effects_device::ic8_write(offs_t address, u8 data)
{
	m_stream->update();
	m_ic8_registers[address & 7] = data;
}

void roland_d50_effects_device::ic9_write(u8 address, u16 data)
{
	m_stream->update();
	m_ic9_registers[address] = data;
	if ((address >= 0x80 && address <= 0xbf) || (address >= 0xd4 && address <= 0xdf))
		update_chorus_parameters(address);
}

void roland_d50_effects_device::patch_mixer_write(u8 key_mode, u8 output_mode, u8 reverb_type, u8 reverb_balance,
	u8 total_volume, u8 tone_balance, u8 tone0_partial_mask, u8 tone0_partial_balance,
	u8 tone1_partial_mask, u8 tone1_partial_balance,
	u8 tone0_chorus_balance, u8 tone1_chorus_balance)
{
	if (key_mode > 4 || output_mode > 3 || reverb_type > 31 || reverb_balance > 100
		|| total_volume > 100 || tone_balance > 100 || tone0_partial_mask > 3 || tone0_partial_balance > 100
		|| tone1_partial_mask > 3 || tone1_partial_balance > 100
		|| tone0_chorus_balance > 100 || tone1_chorus_balance > 100)
		return;

	m_stream->update();

	// This is the exact final chorus/output wrapper recovered from d50lib.
	// The firmware-generated IC9 words already contain the internal balance
	// transformation; these gains are the remaining input and output matrix.
	unsigned const mode = (key_mode == 0 || key_mode == 4) ? 0 : (output_mode == 0 ? 1 : 2);
	float const volume = float(total_volume) * 0.01F;
	if (mode == 0)
		m_chorus_input_gain[0] = m_chorus_input_gain[1] = volume;
	else
	{
		auto balance_gain = [](unsigned value)
		{
			return float(std::min(value * 4U, 255U)) * (1.0F / 255.0F);
		};
		m_chorus_input_gain[0] = balance_gain(tone_balance) * volume;
		m_chorus_input_gain[1] = balance_gain(100 - tone_balance) * volume;
	}

	// The LA32 keeps both partial lanes separate.  d50lib's recovered tone
	// mixer maps balance 50 to 0.8/0.8 and the extremes through a 0.016
	// lower slope and 0.004 upper slope.  A following 1.25 gain normalizes
	// the centre position, hence the division by 0.8 here.
	auto partial_gain = [](unsigned value)
	{
		float const coefficient = value <= 49 ? float(value) * 0.016F
			: float(value - 50) * 0.004F + 0.8F;
		return coefficient * 1.25F;
	};
	auto set_partial_balance = [this, &partial_gain](unsigned tone, unsigned mask, unsigned balance)
	{
		m_partial_mix[tone][0] = BIT(mask, 0) ? partial_gain(100 - balance) : 0.0F;
		m_partial_mix[tone][1] = BIT(mask, 1) ? partial_gain(balance) : 0.0F;
	};
	set_partial_balance(0, tone0_partial_mask, tone0_partial_balance);
	set_partial_balance(1, tone1_partial_mask, tone1_partial_balance);

	bool const single = key_mode == 0 || key_mode == 4;
	std::fill(&m_chorus_dry_mix[0][0], &m_chorus_dry_mix[0][0] + 4, 0.0F);
	std::fill(std::begin(m_chorus_return_gain), std::end(m_chorus_return_gain), 0.0F);
	auto set_balance = [this, mode](unsigned tone, unsigned value)
	{
		float const wet = (value > 49 ? float(value) * 0.4F + 60.0F : float(value) * 1.6F) * 0.01F;
		float const dry = (value > 49 ? float(100 - value) * 1.6F : float(100 - value) * 0.4F + 60.0F) * 0.01F;
		if (mode == 2)
		{
			if (tone == 1)
			{
				m_chorus_dry_mix[0][1] = dry * 0.1825F;
				m_chorus_return_gain[0] = wet * 0.09F;
			}
			else
			{
				m_chorus_dry_mix[1][0] = dry * 0.1825F;
				m_chorus_return_gain[1] = wet * 0.09F;
			}
		}
		else if (mode == 1)
		{
			if (tone == 1)
			{
				m_chorus_dry_mix[0][1] = dry * 0.6F;
				m_chorus_dry_mix[1][1] = dry * 0.6F;
				m_chorus_return_gain[0] = 0.57F;
			}
			else
			{
				m_chorus_dry_mix[0][0] = dry * 0.6F;
				m_chorus_dry_mix[1][0] = dry * 0.6F;
				m_chorus_return_gain[1] = 0.57F;
			}
		}
		else
		{
			if (tone == 1)
			{
				m_chorus_dry_mix[0][0] = dry * 0.23F;
				m_chorus_return_gain[0] = wet * 0.22F;
			}
			else
			{
				m_chorus_dry_mix[1][0] = dry * 0.23F;
				m_chorus_return_gain[1] = wet * 0.22F;
			}
		}
	};
	set_balance(0, tone0_chorus_balance);
	// Whole modes allocate only the first tone, but the recovered output
	// wrapper calls SetBalance for both stereo sides with that tone's chorus
	// block.  The second tone bytes are unused patch storage in these modes;
	// using them here makes a velocity-sensitive single tone appear panned.
	set_balance(1, single ? tone0_chorus_balance : tone1_chorus_balance);
	// Total volume is now applied by the recovered input matrix rather than
	// twice through the provisional IC28 energy normalization.
	m_output_gain = 1.0F;

	float const balance = float(reverb_balance);
	float const time = reverb_balance >= 50 ? 2.0F - 0.02F * balance : 1.0F;
	float reference_wet;
	if ((key_mode | 4) == 4)
	{
		m_reverb_direct = 0.8F * time;
		reference_wet = 0.008F * balance;
	}
	else if (output_mode == 0)
	{
		m_reverb_direct = 0.4F * time;
		reference_wet = 0.00512F * balance;
	}
	else if (output_mode == 1)
	{
		m_reverb_direct = std::min(0.8F, 2.56F * time);
		reference_wet = 0.032F * balance;
	}
	else
	{
		// Modes 2/3 select one reverb input and one output lane.  Preserve the
		// recovered gain here; their final left/right routing remains provisional.
		m_reverb_direct = std::min(0.8F, 2.88F * time);
		reference_wet = 0.0192F * balance;
	}

	// The recovered wrapper operates on d50lib's accumulator scale.  For the
	// normal dual/split output path, IC9's hardware-domain accumulator uses the
	// established 0.8 full return.  Factory room program 1 is quieter internally,
	// so give it the extra 1.25 normalization exposed by the Metal Harp trace.
	// Other paths retain the 0.8 ceiling until their routing is fully implemented.
	if ((key_mode | 4) != 4 && output_mode == 0)
	{
		float const normalization = reverb_type == 1 ? 48.0F : (reverb_type == 6 ? 96.0F : 60.0F);
		m_reverb_wet = std::min(1.0F, balance / normalization);
	}
	else
		m_reverb_wet = std::min(0.8F, reference_wet * 3.255208F);
	m_patch_mixer_valid = true;
}

float roland_d50_effects_device::mb87126_multiplier(u16 data) const
{
	// DD is not just a coefficient register: bits 13:12 select the MAC
	// shifter and bits 11:0 are its signed multiplier.  The four observed
	// ranges correspond to shifts -4, 0, +2 and +4.  This decoder is also
	// independently recovered by d50lib from the packed reverb programs.
	s16 const mantissa = s16(data << 4) >> 4;
	static constexpr float multiplier[4] = { 0.0625F, 1.0F, 4.0F, 16.0F };
	return float(mantissa) * (1.0F / 2048.0F) * multiplier[(data >> 12) & 3];
}

s32 roland_d50_effects_device::mb87126_clip24(s64 value)
{
	return s32(std::clamp<s64>(value, -(s64(1) << 23), (s64(1) << 23) - 1));
}

s32 roland_d50_effects_device::mb87126_clip28(s64 value)
{
	return s32(std::clamp<s64>(value, -(s64(1) << 27), (s64(1) << 27) - 1));
}

s32 roland_d50_effects_device::mb87126_multiply(s32 value, u16 coefficient)
{
	// The visible datapath is a signed 24 x 12 multiplier followed by the
	// range shifter.  Arithmetic right shift is written explicitly so negative
	// products have deterministic floor semantics on every host compiler.
	s16 const mantissa = s16(coefficient << 4) >> 4;
	static constexpr unsigned shift[4] = { 15, 11, 9, 7 };
	s64 const product = s64(mb87126_clip24(value)) * mantissa;
	unsigned const amount = shift[(coefficient >> 12) & 3];
	s64 const aligned = product >= 0
		? product >> amount
		: -((-product + (s64(1) << amount) - 1) >> amount);
	return mb87126_clip28(aligned);
}

s32 roland_d50_effects_device::mb87126_accumulate(s32 accumulator, s32 value, u16 coefficient)
{
	return mb87126_clip28(s64(accumulator) + mb87126_multiply(value, coefficient));
}

s32 roland_d50_effects_device::mb87126_add(s32 accumulator, s32 value)
{
	return mb87126_clip28(s64(accumulator) + value);
}

s32 roland_d50_effects_device::mb87126_from_float(float value)
{
	if (!std::isfinite(value))
		return 0;
	double const scaled = std::clamp<double>(value * double(s64(1) << 23),
		-double(s64(1) << 23), double((s64(1) << 23) - 1));
	return s32(std::llround(scaled));
}

float roland_d50_effects_device::mb87126_to_float(s32 value)
{
	return float(mb87126_clip24(value)) * (1.0F / float(s64(1) << 23));
}

void roland_d50_effects_device::ic28_write(u8 selector, u16 dc, u16 dd)
{
	selector &= 0x7f;
	dc &= 0x3fff;
	dd &= 0x3fff;
	m_stream->update();

	// Selector 1 is also an ordinary output-gain MAC slot.  The previous model
	// treated every packet at selector 1 as a new reverb image and erased all
	// DD slots, including both EQs and the mixer.  A changed DC operand at slot
	// 1, rather than a DD-only update, is the actual topology-change marker.
	bool const new_reverb_topology = selector == 0x01
		&& m_ic28_seen[0][selector]
		&& m_ic28_words[0][selector] != dc;
	if (new_reverb_topology)
	{
		// New reverb images omit zero-valued parameter words.  Clear only the
		// fixed-ROM slots belonging to the reverb program; EQ/output slots are
		// different microinstructions and persist across a type change.
		for (u8 const reverb_selector : s_reverb_dd_selector)
		{
			m_ic28_words[1][reverb_selector] = 0;
			m_ic28_seen[1][reverb_selector] = false;
		}
		std::fill(std::begin(m_reverb_coeff), std::end(m_reverb_coeff), 0.0F);
		std::fill(std::begin(m_mb87126_coefficient), std::end(m_mb87126_coefficient), 0);
		std::fill(std::begin(m_reverb_dd_seen), std::end(m_reverb_dd_seen), false);
		m_reverb_wet = 0.0F;
		std::fill(std::begin(m_reverb_delay), std::end(m_reverb_delay), 0.0F);
		std::fill(std::begin(m_reverb_state), std::end(m_reverb_state), 0.0F);
		std::fill(std::begin(m_mb87126_delay), std::end(m_mb87126_delay), 0);
		std::fill(std::begin(m_mb87126_offset), std::end(m_mb87126_offset), 0);
		std::fill(std::begin(m_mb87126_state), std::end(m_mb87126_state), 0);
		m_mb87126_delay_base = 0;
		m_reverb_overload_count = 0;
		m_reverb_faulted = false;
		std::fill(std::begin(m_reverb_tap), std::end(m_reverb_tap), 0x4000);
		std::fill(std::begin(m_reverb_dc_seen), std::end(m_reverb_dc_seen), false);
		m_reverb_ready = false;
	}

	m_ic28_words[0][selector] = dc;
	m_ic28_words[1][selector] = dd;
	m_ic28_seen[0][selector] = true;
	m_ic28_seen[1][selector] = true;

	for (unsigned index = 0; index < std::size(s_reverb_dc_selector); index++)
	{
		if (s_reverb_dc_selector[index] == selector)
		{
			m_reverb_tap[s_reverb_dc_destination[index]] = (0x4000 - dc) & REVERB_MASK;
			m_mb87126_offset[s_reverb_dc_destination[index]] = dc;
			m_reverb_dc_seen[index] = true;
			break;
		}
	}
	for (unsigned index = 0; index < std::size(s_reverb_dd_selector); index++)
	{
		if (s_reverb_dd_selector[index] == selector)
		{
			m_reverb_coeff[s_reverb_dd_destination[index]] = mb87126_multiplier(dd);
			m_mb87126_coefficient[s_reverb_dd_destination[index]] = dd;
			m_reverb_dd_seen[index] = true;
			break;
		}
	}
	// Zero-valued shadow words remain clean and are never put on IC28.  A
	// complete DC offset upload is therefore the reliable program-ready
	// marker; any absent DD coefficients correctly retain their reset zero.
	m_reverb_ready = std::all_of(std::begin(m_reverb_dc_seen), std::end(m_reverb_dc_seen), [](bool value) { return value; });

	update_eq_parameters(selector);
	update_mixer_parameters();
}

void roland_d50_effects_device::update_chorus_parameters(u8 address)
{
	// The four 0x10-register banks are two serial operators for each tone.
	// Their layout is the hardware expansion of d50lib's CEmuChoOPR
	// parameters: four LFO rates, two amplitude depths, two delay depths,
	// three output coefficients, two stage-link coefficients and two base
	// delays.  The upper-tone coefficient addresses have a deliberate
	// 9a/9b/99 and ba/bb/b9 permutation in the firmware scatter table.
	unsigned tone = 0;
	unsigned op = 0;
	u8 base = 0;
	if (address >= 0x80 && address <= 0x8f)
		base = 0x80;
	else if (address >= 0x90 && address <= 0x9f)
	{
		tone = 1;
		base = 0x90;
	}
	else if (address >= 0xa0 && address <= 0xaf)
	{
		op = 1;
		base = 0xa0;
	}
	else if (address >= 0xb0 && address <= 0xbf)
	{
		tone = 1;
		op = 1;
		base = 0xb0;
	}

	if (base)
	{
		u8 const offset = address - base;
		if (offset < 4)
		{
			static constexpr u8 lfo_order[4] = { 2, 3, 0, 1 };
			// Firmware has already applied the user rate bias.  One IC9 unit
			// becomes 0.1 in CEmuChoLFO::setParam, then 3.9375 * 0.004
			// phase units per 32 kHz sample.
			m_chorus_lfo_step[tone][op][lfo_order[offset]] = float(m_ic9_registers[address] & 0x3fff) * 0.001575F;
		}
		// The firmware scatter order places logical OPR words 6/7 (amplitude
		// ramp) at IC9 +4/+5, and logical words 4/5 (delay pan) at +6/+7.
		// For example, I-47 +6/+7 decode to d50lib's exact 32.64/35.36
		// pan depths while +4/+5 correctly leave both ramp depths at zero.
		else if (offset == 4 || offset == 5)
			m_chorus_ramp_depth[tone][op][offset - 4] = float(m_ic9_registers[address] & 0x0fff) * (1.0F / 2048.0F);
		else if (offset == 6 || offset == 7)
			m_chorus_pan_depth[tone][op][offset - 6] = float(m_ic9_registers[address] & 0x0fff) * 0.17F;
		else if (offset == 14 || offset == 15)
		{
			unsigned const delay = offset - 14;
			int position = m_chorus_write_position[tone][op][delay] + 0x1000 - int(m_ic9_registers[address]);
			position %= int(CHORUS_SIZE);
			if (position < 0)
				position += CHORUS_SIZE;
			m_chorus_read_position[tone][op][delay] = position;
		}
	}

	static constexpr u8 stage_address[CHORUS_TONES][8] = {
		{ 0x88, 0x89, 0x8b, 0x8c, 0x8d, 0xa8, 0xa9, 0xab },
		{ 0x9a, 0x9b, 0x99, 0x9c, 0x9d, 0xba, 0xbb, 0xb9 }
	};
	static constexpr u8 feedback_address[CHORUS_TONES][2] = {
		{ 0xac, 0xad }, { 0xbc, 0xbd }
	};
	static constexpr u8 global_address[CHORUS_TONES][4] = {
		{ 0xd4, 0xd8, 0xdc, 0xde },
		{ 0xd6, 0xda, 0xdd, 0xdf }
	};
	for (unsigned current_tone = 0; current_tone < CHORUS_TONES; current_tone++)
	{
		for (unsigned index = 0; index < 8; index++)
			m_chorus_stage_mix[current_tone][index] = float(signed_12(m_ic9_registers[stage_address[current_tone][index]])) * (1.0F / 2048.0F);
		for (unsigned index = 0; index < 2; index++)
			m_chorus_feedback_mix[current_tone][index] = float(signed_12(m_ic9_registers[feedback_address[current_tone][index]])) * (1.0F / 2048.0F);
		for (unsigned index = 0; index < 4; index++)
			m_chorus_global[current_tone][index] = float(signed_12(m_ic9_registers[global_address[current_tone][index]])) * (1.0F / 2048.0F);
	}

}

void roland_d50_effects_device::update_eq_parameters(u8 selector)
{
	auto slot_group_contains = [selector](auto const &group)
	{
		return std::find(std::begin(group), std::end(group), selector) != std::end(group);
	};
	auto all_seen = [this](auto const &group)
	{
		return std::all_of(std::begin(group), std::end(group), [this](u8 slot) { return m_ic28_seen[1][slot]; });
	};

	for (unsigned tone = 0; tone < 2; tone++)
	{
		if ((slot_group_contains(s_eq_low_coeff_selector[tone]) || slot_group_contains(s_eq_low_mix_selector[tone]))
			&& all_seen(s_eq_low_coeff_selector[tone]) && all_seen(s_eq_low_mix_selector[tone]))
		{
			float word[8];
			for (unsigned index = 0; index < 8; index++)
				word[index] = mb87126_multiplier(m_ic28_words[1][s_eq_low_coeff_selector[tone][index]]);
			m_eq_low_coeff[tone][0] = word[3] + word[2] * (1.0F / 64.0F);
			m_eq_low_coeff[tone][1] = word[1] + word[0] * (1.0F / 64.0F);
			m_eq_low_coeff[tone][2] = word[4] + word[5] * (1.0F / 64.0F);
			m_eq_low_coeff[tone][3] = word[6] + word[7] * (1.0F / 64.0F);
			for (unsigned index = 0; index < 2; index++)
				m_eq_low_mix_target[tone][index] = mb87126_multiplier(m_ic28_words[1][s_eq_low_mix_selector[tone][index]]);
			if (!m_eq_low_valid[tone])
			{
				std::copy(std::begin(m_eq_low_mix_target[tone]), std::end(m_eq_low_mix_target[tone]), std::begin(m_eq_low_mix_current[tone]));
				m_eq_low_valid[tone] = true;
			}
		}

		if ((slot_group_contains(s_eq_high_coeff_selector[tone]) || slot_group_contains(s_eq_high_mix_selector[tone]))
			&& all_seen(s_eq_high_coeff_selector[tone]) && all_seen(s_eq_high_mix_selector[tone]))
		{
			float word[10];
			for (unsigned index = 0; index < 10; index++)
				word[index] = mb87126_multiplier(m_ic28_words[1][s_eq_high_coeff_selector[tone][index]]);
			// Each effective coefficient is a main term plus a companion term.
			// A range-0 companion is a fine fragment and the fixed ROM aligns it
			// six more bits to the right; range-1 companions are already aligned.
			// Ignoring this distinction made I-25's upper feedback pole 1.04,
			// while shifting every companion destabilized rows using range 1.
			auto companion = [this, tone, &word](unsigned index)
			{
				u16 const packed = m_ic28_words[1][s_eq_high_coeff_selector[tone][index]];
				return word[index] * (((packed >> 12) & 3) == 0 ? (1.0F / 64.0F) : 1.0F);
			};
			m_eq_high_coeff[tone][0] = word[4] + companion(3);
			m_eq_high_coeff[tone][1] = word[2] + companion(1);
			m_eq_high_coeff[tone][2] = word[0] + companion(5);
			m_eq_high_coeff[tone][3] = word[6] + companion(7);
			m_eq_high_coeff[tone][4] = word[8] + companion(9);
			for (unsigned index = 0; index < 2; index++)
				m_eq_high_mix_target[tone][index] = mb87126_multiplier(m_ic28_words[1][s_eq_high_mix_selector[tone][index]]);
			if (!m_eq_high_valid[tone])
			{
				std::copy(std::begin(m_eq_high_mix_target[tone]), std::end(m_eq_high_mix_target[tone]), std::begin(m_eq_high_mix_current[tone]));
				m_eq_high_valid[tone] = true;
			}
		}
	}
}

void roland_d50_effects_device::update_mixer_parameters()
{
	float gain[4]{};
	bool all_gain_words = true;
	unsigned gain_index = 0;
	for (u8 const selector : s_total_volume_selector)
	{
		all_gain_words &= m_ic28_seen[1][selector];
		gain[gain_index++] = std::abs(mb87126_multiplier(m_ic28_words[1][selector]));
	}
	if (all_gain_words)
	{
		if (!m_output_reference_valid)
		{
			std::copy(std::begin(gain), std::end(gain), std::begin(m_output_reference));
			m_output_reference_valid = true;
		}
		float current_energy = 0.0F;
		float reference_energy = 0.0F;
		for (unsigned index = 0; index < 4; index++)
		{
			current_energy += gain[index] * gain[index];
			reference_energy += m_output_reference[index] * m_output_reference[index];
		}
		m_output_gain = reference_energy > 1.0e-9F ? std::clamp(std::sqrt(current_energy / reference_energy), 0.0F, 2.0F) : 1.0F;
	}

	float wet_level = 0.0F;
	bool wet_seen = false;
	// 34 is the primary reverb-return level and 75 is its output-mode
	// mirror.  36/76 belong to the accompanying routing matrix; averaging
	// them into the level made the user balance both too small and nonlinear.
	for (u8 const selector : { u8(0x34), u8(0x75) })
		if (m_ic28_seen[1][selector])
		{
			wet_level = std::max(wet_level, std::abs(mb87126_multiplier(m_ic28_words[1][selector])));
			wet_seen = true;
		}
	if (wet_seen && !m_patch_mixer_valid)
		m_reverb_wet = std::clamp(wet_level, 0.0F, 1.0F);
}

float roland_d50_effects_device::chorus_lfo(unsigned tone, unsigned op, unsigned lfo)
{
	float phase = m_chorus_lfo_phase[tone][op][lfo] + m_chorus_lfo_step[tone][op][lfo];
	if (phase >= 256.0F)
		phase -= 512.0F;
	m_chorus_lfo_phase[tone][op][lfo] = phase;
	return std::abs(phase * (1.0F / 128.0F)) - 1.0F;
}

float roland_d50_effects_device::chorus_delay(unsigned tone, unsigned op, unsigned delay, float modulation, float input)
{
	auto wrap = [](int position)
	{
		position %= int(CHORUS_SIZE);
		return position < 0 ? position + int(CHORUS_SIZE) : position;
	};

	int read = m_chorus_read_position[tone][op][delay];
	int write = m_chorus_write_position[tone][op][delay];
	m_chorus_buffer[tone][op][delay][write] = input;

	int const integer = int(modulation);
	float fraction = modulation - float(integer);
	bool const positive = fraction > 0.0F;
	if (!positive)
		fraction = -fraction;
	int const neighbor = integer + (positive ? 1 : -1);
	float const first = m_chorus_buffer[tone][op][delay][wrap(read + integer)];
	float const second = m_chorus_buffer[tone][op][delay][wrap(read + neighbor)];

	read = wrap(read - 1);
	write = wrap(write - 1);
	m_chorus_read_position[tone][op][delay] = read;
	m_chorus_write_position[tone][op][delay] = write;
	return first + fraction * (second - first);
}

void roland_d50_effects_device::chorus_operator(unsigned tone, unsigned op, float input, float (&output)[2])
{
	float const modulation = chorus_lfo(tone, op, 0) * m_chorus_pan_depth[tone][op][0]
		+ chorus_lfo(tone, op, 1) * m_chorus_pan_depth[tone][op][1];
	float const scale_left = 1.0F + chorus_lfo(tone, op, 2) * m_chorus_ramp_depth[tone][op][0];
	float const scale_right = 1.0F - chorus_lfo(tone, op, 3) * m_chorus_ramp_depth[tone][op][1];
	output[0] = chorus_delay(tone, op, 0, modulation, input) * scale_left;
	output[1] = chorus_delay(tone, op, 1, -modulation, input) * scale_right;
}

void roland_d50_effects_device::chorus_half(unsigned tone, float input, float (&output)[2])
{
	float const premix = input * m_chorus_global[tone][0] + m_chorus_feedback[tone] * m_chorus_global[tone][1];
	float stage0[2];
	chorus_operator(tone, 0, premix * m_chorus_global[tone][2], stage0);

	float const stage1_input = stage0[1] * m_chorus_stage_mix[tone][4]
		+ stage0[0] * m_chorus_stage_mix[tone][3]
		+ premix * m_chorus_global[tone][3];
	float stage1[2];
	chorus_operator(tone, 1, stage1_input, stage1);

	m_chorus_feedback[tone] = finite_clip(stage1[0] * m_chorus_feedback_mix[tone][0]
		+ stage1[1] * m_chorus_feedback_mix[tone][1]);
	output[0] = stage0[1] * m_chorus_stage_mix[tone][2]
		+ stage1[1] * m_chorus_stage_mix[tone][7];
	output[1] = stage1[0] * m_chorus_stage_mix[tone][5]
		+ stage0[1] * m_chorus_stage_mix[tone][1]
		+ stage0[0] * m_chorus_stage_mix[tone][0]
		+ stage1[1] * m_chorus_stage_mix[tone][6];
}

void roland_d50_effects_device::process_chorus(float &left, float &right)
{
	// IC8 presents the lower and upper tone sums separately.  The recovered
	// wrapper applies patch total/tone balance before the two chorus halves,
	// then combines their low/high outputs through a mode-dependent matrix.
	float const lower_input = left * m_chorus_input_gain[0];
	float const upper_input = right * m_chorus_input_gain[1];
	float lower[2];
	float upper[2];
	chorus_half(0, lower_input * (1.0F / 3.0F), lower);
	chorus_half(1, upper_input * (1.0F / 3.0F), upper);
	auto quantize = [](float value) { return float(s32(value * 40960.0F)) * (1.0F / 40960.0F); };
	float const wet_left = 3.0F * (quantize(lower[1]) + quantize(upper[0]));
	float const wet_right = 3.0F * (quantize(lower[0]) + quantize(upper[1]));
	left = finite_clip(lower_input * m_chorus_dry_mix[0][0]
		+ upper_input * m_chorus_dry_mix[0][1] + wet_left * m_chorus_return_gain[0]);
	right = finite_clip(lower_input * m_chorus_dry_mix[1][0]
		+ upper_input * m_chorus_dry_mix[1][1] + wet_right * m_chorus_return_gain[1]);
}

void roland_d50_effects_device::process_eq(float &left, float &right)
{
	// The fixed MB87126 program scales the input by 1/4, runs the low four-
	// coefficient recurrence followed by the high five-coefficient recurrence,
	// then scales each section by four.  The two output multipliers crossfade
	// at 1/80 per sample when gain changes (the value recovered by d50lib).
	float *sample[2] = { &left, &right }; // lower, upper
	for (unsigned tone = 0; tone < 2; tone++)
	{
		float value = *sample[tone];
		if (m_eq_low_valid[tone])
		{
			float const x = value * 0.25F + 1.0e-13F;
			float const s0 = m_eq_low_coeff[tone][0] * m_eq_low_state[tone][0] + x;
			float const s1 = m_eq_low_coeff[tone][1] * m_eq_low_state[tone][1] + s0;
			float const acc = m_eq_low_coeff[tone][2] * m_eq_low_state[tone][1] + s1;
			float const s2 = m_eq_low_coeff[tone][3] * m_eq_low_state[tone][2] + acc;
			m_eq_low_state[tone][0] = finite_clip(x);
			m_eq_low_state[tone][1] = finite_clip(s1);
			m_eq_low_state[tone][2] = finite_clip(s2);
			for (unsigned index = 0; index < 2; index++)
				m_eq_low_mix_current[tone][index] += 0.0125F
					* (m_eq_low_mix_target[tone][index] - m_eq_low_mix_current[tone][index]);
			value = finite_clip(4.0F * (s2 * m_eq_low_mix_current[tone][0] + x * m_eq_low_mix_current[tone][1]));
		}

		if (m_eq_high_valid[tone])
		{
			float const x = value * 0.25F + 1.0e-13F;
			float const acc = m_eq_high_coeff[tone][0] * x
				+ m_eq_high_coeff[tone][1] * m_eq_high_state[tone][0]
				+ m_eq_high_coeff[tone][2] * m_eq_high_state[tone][1]
				+ m_eq_high_coeff[tone][3] * m_eq_high_state[tone][2]
				+ m_eq_high_coeff[tone][4] * m_eq_high_state[tone][3];
			m_eq_high_state[tone][1] = m_eq_high_state[tone][0];
			m_eq_high_state[tone][0] = finite_clip(x);
			m_eq_high_state[tone][3] = m_eq_high_state[tone][2];
			m_eq_high_state[tone][2] = finite_clip(acc);
			for (unsigned index = 0; index < 2; index++)
				m_eq_high_mix_current[tone][index] += 0.0125F
					* (m_eq_high_mix_target[tone][index] - m_eq_high_mix_current[tone][index]);
			value = finite_clip(4.0F * (acc * m_eq_high_mix_current[tone][0] + x * m_eq_high_mix_current[tone][1]));
		}
		*sample[tone] = value;
	}
}

void roland_d50_effects_device::process_reverb_legacy(float &left, float &right)
{
	if (!m_reverb_ready)
		return;
	if (m_reverb_wet <= 0.0F || m_reverb_faulted)
	{
		left = finite_clip(m_reverb_direct * left);
		right = finite_clip(m_reverb_direct * right);
		return;
	}

	auto tap = [this](unsigned index) -> float { return m_reverb_delay[m_reverb_tap[index] & REVERB_MASK]; };
	bool overloaded = false;
	auto bounded = [&overloaded](float value) -> float
	{
		if (!std::isfinite(value) || std::abs(value) > 8.0F)
			overloaded = true;
		return finite_clip(value);
	};
	auto put = [this, &bounded](unsigned index, float value) { m_reverb_delay[m_reverb_tap[index] & REVERB_MASK] = bounded(value); };
	auto c = [this](unsigned index) -> float { return m_reverb_coeff[index]; };

	// Keep faults in an upstream provisional stage from being injected into
	// a high-feedback delay network at many times line level.
	float const pre = 0.5F * (std::clamp(left, -1.0F, 1.0F) + std::clamp(right, -1.0F, 1.0F)) + 1.0e-13F;
	float value = c(0) * pre + c(1) * tap(11);
	value += c(8) * tap(3); put(2, value);
	value = value * c(9) + tap(3) + c(10) * tap(5); put(4, value);
	value = value * c(11) + tap(5) + c(12) * tap(7); put(6, value);
	value = value * c(13) + tap(7) + c(14) * tap(9); put(8, value);
	float const tap_a = value * c(15) + tap(9);

	float chain2 = c(6) * tap(1) + pre * c(2);
	put(0, chain2);
	chain2 = chain2 * c(7) + tap(1);
	put(10, tap_a * c(4) + chain2 * c(5));
	float const scatter = tap_a * c(3);

	float lacc = c(16) * tap(12) + c(18) * tap(14) + c(19) * tap(15) + c(20) * tap(16);
	float racc = c(17) * tap(13) + c(21) * tap(17) + c(22) * tap(18) + c(23) * tap(19) + c(24) * tap(20);

	m_reverb_state[0] = bounded(c(37) * tap(23) + c(38) * m_reverb_state[0]);
	put(21, m_reverb_state[0] * c(39) + scatter);
	lacc += c(25) * tap(22);
	racc += c(28) * tap(26);
	m_reverb_state[1] = bounded(tap(26) * c(40));
	put(24, m_reverb_state[1] * c(42) + scatter);

	lacc += c(26) * tap(23);
	racc += c(29) * tap(28);
	m_reverb_state[2] = bounded(tap(29) * c(43));
	put(27, m_reverb_state[2] * c(45) + scatter);

	lacc += c(31) * tap(25);
	racc += c(32) * tap(32);
	m_reverb_state[3] = bounded(tap(32) * c(46) + m_reverb_state[3] * c(47));
	put(30, m_reverb_state[3] * c(48) + scatter);

	lacc += c(30) * tap(29);
	racc += c(33) * tap(34);
	m_reverb_state[4] = bounded(tap(35) * c(49));
	put(33, m_reverb_state[4] * c(51) + scatter);

	lacc += c(31) * tap(31) + c(34) * tap(35);
	racc += c(35) * tap(37) + c(36) * tap(38);
	m_reverb_state[5] = bounded(m_reverb_state[5] * c(53) + tap(38) * c(52));
	put(36, m_reverb_state[5] * c(54) + scatter);

	for (s32 &position : m_reverb_tap)
		position = (position + REVERB_MASK) & REVERB_MASK;

	// d50lib's recovered final matrix applies the two accumulators as distinct
	// stereo returns.  Selector 34/75 already encodes the user-facing return
	// level; its effective full-scale gain is 0.8 after the hardware's final
	// 3.2 scale.  The old mono 0.01 estimate made Metal Harp roughly two orders
	// of magnitude too dry and erased the audible late tail.
	float const wet_gain = 0.8F * m_reverb_wet;
	float const raw_wet_left = bounded(wet_gain * lacc);
	float const raw_wet_right = bounded(wet_gain * racc);
	overloaded |= std::max(std::abs(raw_wet_left), std::abs(raw_wet_right)) > 1.0F;
	if (overloaded)
		m_reverb_overload_count = std::min<u16>(m_reverb_overload_count + 1, 32);
	else if (m_reverb_overload_count)
		--m_reverb_overload_count;

	// Sustained internal clipping is a corrupt/incomplete program, not a
	// musical reverb tail.  Flush and latch the reverb bypassed until selector
	// 1 starts a fresh program image; repeatedly restarting the bad network
	// would merely turn a runaway into a periodic click.
	if (m_reverb_overload_count >= 32)
	{
		std::fill(std::begin(m_reverb_delay), std::end(m_reverb_delay), 0.0F);
		std::fill(std::begin(m_reverb_state), std::end(m_reverb_state), 0.0F);
		m_reverb_overload_count = 0;
		m_reverb_faulted = true;
		return;
	}

	// The overload latch prevents sustained feedback.  This separate soft
	// ceiling only catches musical transients, allowing a useful -9 dBFS wet
	// return without turning them into hard-clipped high-frequency energy.
	constexpr float wet_ceiling = 0.35F;
	float const wet_left = wet_ceiling * std::tanh(raw_wet_left / wet_ceiling);
	float const wet_right = wet_ceiling * std::tanh(raw_wet_right / wet_ceiling);
	left = finite_clip(m_reverb_direct * left + wet_left);
	right = finite_clip(m_reverb_direct * right + wet_right);
}

void roland_d50_effects_device::process_reverb_mb87126(float &left, float &right)
{
	if (!m_reverb_ready)
		return;

	s32 const input_left = mb87126_from_float(left);
	s32 const input_right = mb87126_from_float(right);
	auto gain = [](float value) -> s32
	{
		return s32(std::llround(std::clamp(value, 0.0F, 4.0F) * float(1U << 20)));
	};
	auto scale = [](s32 value, s32 coefficient) -> s32
	{
		s64 const product = s64(value) * coefficient;
		s64 const aligned = product >= 0
			? product >> 20
			: -((-product + (s64(1) << 20) - 1) >> 20);
		return mb87126_clip24(aligned);
	};

	if (m_reverb_wet <= 0.0F)
	{
		s32 const direct = gain(m_reverb_direct);
		left = mb87126_to_float(scale(input_left, direct));
		right = mb87126_to_float(scale(input_right, direct));
		return;
	}

	auto address = [this](unsigned index) -> u16
	{
		return (m_mb87126_delay_base - m_mb87126_offset[index]) & REVERB_MASK;
	};
	auto tap = [this, &address](unsigned index) -> s32
	{
		return m_mb87126_delay[address(index)];
	};
	auto put = [this, &address](unsigned index, s32 value)
	{
		m_mb87126_delay[address(index)] = mb87126_clip24(value);
	};
	auto c = [this](unsigned index) -> u16 { return m_mb87126_coefficient[index]; };

	// Each multiply/accumulate below passes through the reconstructed 24 x 12
	// multiplier, range shifter and saturating 28-bit accumulator.  DRAM stores
	// are reduced to signed 24-bit words, matching DR0-DR23 on the schematic.
	s32 const pre = mb87126_clip24((s64(input_left) + input_right) / 2);
	s32 value = mb87126_accumulate(mb87126_multiply(pre, c(0)), tap(11), c(1));
	value = mb87126_accumulate(value, tap(3), c(8));
	put(2, value);
	value = mb87126_multiply(value, c(9));
	value = mb87126_add(value, tap(3));
	value = mb87126_accumulate(value, tap(5), c(10));
	put(4, value);
	value = mb87126_multiply(value, c(11));
	value = mb87126_add(value, tap(5));
	value = mb87126_accumulate(value, tap(7), c(12));
	put(6, value);
	value = mb87126_multiply(value, c(13));
	value = mb87126_add(value, tap(7));
	value = mb87126_accumulate(value, tap(9), c(14));
	put(8, value);
	s32 const tap_a = mb87126_add(mb87126_multiply(value, c(15)), tap(9));

	s32 chain2 = mb87126_accumulate(mb87126_multiply(tap(1), c(6)), pre, c(2));
	put(0, chain2);
	chain2 = mb87126_add(mb87126_multiply(chain2, c(7)), tap(1));
	put(10, mb87126_accumulate(mb87126_multiply(tap_a, c(4)), chain2, c(5)));
	s32 const scatter = mb87126_multiply(tap_a, c(3));

	s32 lacc = mb87126_multiply(tap(12), c(16));
	lacc = mb87126_accumulate(lacc, tap(14), c(18));
	lacc = mb87126_accumulate(lacc, tap(15), c(19));
	lacc = mb87126_accumulate(lacc, tap(16), c(20));
	s32 racc = mb87126_multiply(tap(13), c(17));
	racc = mb87126_accumulate(racc, tap(17), c(21));
	racc = mb87126_accumulate(racc, tap(18), c(22));
	racc = mb87126_accumulate(racc, tap(19), c(23));
	racc = mb87126_accumulate(racc, tap(20), c(24));

	m_mb87126_state[0] = mb87126_clip24(mb87126_accumulate(
		mb87126_multiply(tap(23), c(37)), m_mb87126_state[0], c(38)));
	put(21, mb87126_add(mb87126_multiply(m_mb87126_state[0], c(39)), scatter));
	lacc = mb87126_accumulate(lacc, tap(22), c(25));
	racc = mb87126_accumulate(racc, tap(26), c(28));
	m_mb87126_state[1] = mb87126_clip24(mb87126_multiply(tap(26), c(40)));
	put(24, mb87126_add(mb87126_multiply(m_mb87126_state[1], c(42)), scatter));

	lacc = mb87126_accumulate(lacc, tap(23), c(26));
	racc = mb87126_accumulate(racc, tap(28), c(29));
	m_mb87126_state[2] = mb87126_clip24(mb87126_multiply(tap(29), c(43)));
	put(27, mb87126_add(mb87126_multiply(m_mb87126_state[2], c(45)), scatter));

	lacc = mb87126_accumulate(lacc, tap(25), c(31));
	racc = mb87126_accumulate(racc, tap(32), c(32));
	m_mb87126_state[3] = mb87126_clip24(mb87126_accumulate(
		mb87126_multiply(tap(32), c(46)), m_mb87126_state[3], c(47)));
	put(30, mb87126_add(mb87126_multiply(m_mb87126_state[3], c(48)), scatter));

	lacc = mb87126_accumulate(lacc, tap(29), c(30));
	racc = mb87126_accumulate(racc, tap(34), c(33));
	m_mb87126_state[4] = mb87126_clip24(mb87126_multiply(tap(35), c(49)));
	put(33, mb87126_add(mb87126_multiply(m_mb87126_state[4], c(51)), scatter));

	lacc = mb87126_accumulate(lacc, tap(31), c(31));
	lacc = mb87126_accumulate(lacc, tap(35), c(34));
	racc = mb87126_accumulate(racc, tap(37), c(35));
	racc = mb87126_accumulate(racc, tap(38), c(36));
	m_mb87126_state[5] = mb87126_clip24(mb87126_accumulate(
		mb87126_multiply(m_mb87126_state[5], c(53)), tap(38), c(52)));
	put(36, mb87126_add(mb87126_multiply(m_mb87126_state[5], c(54)), scatter));

	m_mb87126_delay_base = (m_mb87126_delay_base + REVERB_MASK) & REVERB_MASK;

	s32 const direct_gain = gain(m_reverb_direct);
	s32 const wet_gain = gain(0.8F * m_reverb_wet);
	s32 const output_left = mb87126_clip24(s64(scale(input_left, direct_gain)) + scale(lacc, wet_gain));
	s32 const output_right = mb87126_clip24(s64(scale(input_right, direct_gain)) + scale(racc, wet_gain));
	left = mb87126_to_float(output_left);
	right = mb87126_to_float(output_right);
}

void roland_d50_effects_device::process_reverb(float &left, float &right)
{
	// Run both backends so changing the configuration port is a phase-aligned
	// A/B comparison rather than a reset into stale delay memory.
	float legacy_left = left;
	float legacy_right = right;
	float mb87126_left = left;
	float mb87126_right = right;
	process_reverb_legacy(legacy_left, legacy_right);
	process_reverb_mb87126(mb87126_left, mb87126_right);
	if (BIT(m_reverb_model.read_safe(1), 0))
	{
		left = mb87126_left;
		right = mb87126_right;
	}
	else
	{
		left = legacy_left;
		right = legacy_right;
	}
}

void roland_d50_effects_device::process_final_eq(float &left, float &right)
{
	// Fixed post-effects output shaper recovered from d50lib.  Its constructor
	// selects table indices {4, 0, 45, 65}; these are the exact resulting
	// constants.  This block is separate from the user-editable tone EQ above.
	static constexpr float low_frequency = 0.0392696525F;
	static constexpr float feedback = 0.7943282127F;
	static constexpr float high_frequency = 0.1236922199F;
	static constexpr float output_boost = 1.0F;
	float *sample[2] = { &left, &right };
	for (unsigned channel = 0; channel < 2; channel++)
	{
		float difference = *sample[channel] - m_final_eq_state[channel][0];
		float const state0 = m_final_eq_state[channel][0] + low_frequency * difference;
		difference -= feedback * state0 + m_final_eq_state[channel][1];
		float const state1 = m_final_eq_state[channel][1] + high_frequency * difference;
		m_final_eq_state[channel][0] = finite_clip(state0);
		m_final_eq_state[channel][1] = finite_clip(state1);
		*sample[channel] = finite_clip(state1 - output_boost * difference);
	}
}

void roland_d50_effects_device::sound_stream_update(sound_stream &stream)
{
	auto const output_limit = [](float value)
	{
		// Normal patches remain completely linear.  Keep only an emergency soft
		// ceiling above -0.9 dBFS for corrupt/incomplete parameter images; the
		// slope is continuous at the knee and approaches -0.2 dBFS.
		float const magnitude = std::abs(value);
		if (magnitude <= 0.9F)
			return value;
		float const limited = 0.9F + 0.08F * std::tanh((magnitude - 0.9F) * 12.5F);
		return std::copysign(limited, value);
	};

	for (int sample = 0; sample < stream.samples(); sample++)
	{
		// The firmware's LA32 assignments across all 64 factory patches show
		// the lower tone on IC8 buses 2+4 and the upper tone on 0+6.  The D-50
		// does not normally use the odd buses; split any residual equally so
		// unusual structures remain audible rather than being discarded.
		float const residual = stream.get(1, sample) + stream.get(3, sample)
			+ stream.get(5, sample) + stream.get(7, sample);
		float left = stream.get(4, sample) * m_partial_mix[0][0]
			+ stream.get(2, sample) * m_partial_mix[0][1] + 0.5F * residual;
		float right = stream.get(0, sample) * m_partial_mix[1][0]
			+ stream.get(6, sample) * m_partial_mix[1][1] + 0.5F * residual;
		process_eq(left, right);
		process_chorus(left, right);
		process_reverb(left, right);
		process_final_eq(left, right);
		// Keep the final gain inside the device so the soft output stage sees
		// it; applying it as a speaker-route gain would hard-clip afterward.
		// The d50lib 3.2 accumulator conversion assumes its own internal voice
		// normalization.  LA32's hardware-domain stream needs 1.5 here; using
		// 3.2 forced ordinary factory attacks through the safety limiter.
		float const output_left = 1.5F * left * m_output_gain;
		float const output_right = 1.5F * right * m_output_gain;
		stream.put(0, sample, output_limit(output_left));
		stream.put(1, sample, output_limit(output_right));
	}
}
