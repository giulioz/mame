// license:GPL-2.0
// copyright-holders:giulioz

/***************************************************************************

    Fujitsu MB87424, Roland TVF/TVA gate array (D-70, JD-800, JD-990, S-770 and more).
    Driver derived from silicon reverse engineering (thanks InfoSecDJ).

    Thirty-two contexts are processed as adjacent A/B pairs. Four structures
    select single, serial, ring-before-filter or ring-after-filter routing.
    Each filter selects LP, HP, BP or silence. One shared 18 x 14 multiplier
    feeds a saturating 24-bit accumulator. Filter states retain six fractional
    bits. The multiplier discards these on its 18-bit signal input, and its
    product is arithmetic-shifted by eight before accumulation. Subtraction
    truncates the product first, preserving the measured damping dead zone.

    RAM-A: four 24-bit words per voice:
      00: band integrator
      04: high-pass companion
      08: control
        b0: add the B path around ring multiplication
        b2:1: LP/HP/BP/silent
        b4:3: structure
        b20:8: Q12 damping (1000=unity)
      0c: low integrator
    RAM-B: six 20-bit words per voice:
      20/24: current cutoff/gain
      28/2c: signed glide steps
      30/34: targets, bit 19 selects snap, bits 18:0 hold the destination

    Global controls: 10/12 are the high/low voice masks, 14 enables target/ramp
    processing when bits 1 and 2 and at least one of bits 3/4 are set, 18 is the
    five-bit glide-rate code.

***************************************************************************/

#include "emu.h"
#include "roland_tvf.h"

#include <cassert>
#include <cstdlib>
#include <cstring>

DEFINE_DEVICE_TYPE(MB87424, mb87424_device, "mb87424", "Roland TVF (Fujitsu MB87424)")

mb87424_device::mb87424_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, MB87424, tag, owner, clock), device_sound_interface(mconfig, *this)
{
}

void mb87424_device::device_start()
{
	m_stream = stream_alloc(32, 32, clock() / 512, STREAM_SYNCHRONOUS);
	save_item(NAME(m_inputs));
	save_item(NAME(m_outputs));
	save_item(NAME(m_global));
	save_item(NAME(m_active_mask));
	save_item(NAME(m_ramp_enable));
	save_item(NAME(m_glide_rate));
	save_item(NAME(m_prev_active_mask));
	save_item(NAME(m_glide_tick_phase));
	save_item(NAME(m_selected_voice));
	save_item(NAME(m_port_reg));
	save_item(NAME(m_port_voice));
	save_item(NAME(m_port_latch));
	save_item(STRUCT_MEMBER(m_voices, svf_state_band_00));
	save_item(STRUCT_MEMBER(m_voices, svf_state_high_04));
	save_item(STRUCT_MEMBER(m_voices, filter_control_08));
	save_item(STRUCT_MEMBER(m_voices, svf_state_low_0c));
	save_item(STRUCT_MEMBER(m_voices, cutoff_current_20));
	save_item(STRUCT_MEMBER(m_voices, gain_current_24));
	save_item(STRUCT_MEMBER(m_voices, cutoff_step_28));
	save_item(STRUCT_MEMBER(m_voices, gain_step_2c));
	save_item(STRUCT_MEMBER(m_voices, cutoff_target_30));
	save_item(STRUCT_MEMBER(m_voices, gain_target_34));
	save_item(STRUCT_MEMBER(m_voices, cutoff_gliding));
	save_item(STRUCT_MEMBER(m_voices, gain_gliding));
}

void mb87424_device::device_reset()
{
	m_stream->update();
	reset_state();
	m_inputs.fill(0);
	m_outputs.fill(0);
}

void mb87424_device::device_clock_changed()
{
	if (m_stream)
		m_stream->set_sample_rate(clock() / 512);
}

u8 mb87424_device::read(offs_t offset)
{
	if (!machine().side_effects_disabled())
		m_stream->update();
	return register_r(offset);
}

void mb87424_device::write(offs_t offset, u8 data)
{
	m_stream->update();
	register_w(offset, data);
}

s32 mb87424_device::voice_r(offs_t voice)
{
	if (!machine().side_effects_disabled())
		m_stream->update();
	return m_outputs[voice & 31];
}

void mb87424_device::sound_stream_update(sound_stream &stream)
{
	for (int sample = 0; sample < stream.samples(); ++sample)
	{
		for (unsigned voice = 0; voice < 32; ++voice)
		{
			m_inputs[voice] = stream.get(voice, sample) * (1 << 17);
			if (m_input_sign_bit)
			{
				const u32 bus = m_inputs[voice];
				const u32 word = ((bus >> 1) & 0x1ffff) | (((bus >> m_input_sign_bit) & 1) << 17);
				m_inputs[voice] = s32(word) - (BIT(word, 17) ? 0x40000 : 0);
			}
		}
		process_frame<32>(m_inputs.data(), m_outputs.data());
		for (unsigned voice = 0; voice < 32; ++voice)
			stream.put_int(voice, sample, m_outputs[voice], 1 << 17);
	}
}

// Chip state and arithmetic.

namespace
{

constexpr int mul_unity = 0x4000;

inline int64_t mul18x14(int64_t signal_18, int64_t coeff_14)
{
	return (signal_18 * coeff_14) >> 8;
}

inline int32_t sign_extend24(uint32_t raw)
{
	return int32_t(raw ^ 0x800000) - 0x800000;
}

inline int32_t sat24(int64_t value)
{
	constexpr int64_t SAMPLE_MAX = (1 << 23) - 1; // +8388607
	constexpr int64_t SAMPLE_MIN = -(1 << 23);    // -8388608
	if (value > SAMPLE_MAX)
		return SAMPLE_MAX;
	if (value < SAMPLE_MIN)
		return SAMPLE_MIN;
	return int32_t(value);
}

// The subtract form truncates the product toward -inf FIRST, then subtracts
// -- that dead zone below one die LSB is silicon-measured (damping residual
// floors of 4096/damp).
inline int32_t mac(int64_t acc, int64_t sig_18, int64_t coeff_14)
{
	return sat24(acc + mul18x14(sig_18, coeff_14));
}
inline int32_t mac_sub(int64_t acc, int64_t sig_18, int64_t coeff_14)
{
	return sat24(acc - mul18x14(sig_18, coeff_14));
}

// 24 bit RAM A words: reg + 0 = val >> 8, reg + 2 = (val & 0xff) << 8.
void write_reg24_word0(uint32_t *dest, uint16_t value)
{
	*dest = (*dest & 0x0000ff) | (uint32_t(value) << 8);
}
void write_reg24_word2(uint32_t *dest, uint16_t value)
{
	*dest = (*dest & 0xffff00) | (value >> 8);
}
// The SVF state slots hold signed samples; they are kept sign-extended from
// bit 23 so the ramp can use them directly.
void write_reg24_word0(int32_t *dest, uint16_t value)
{
	uint32_t raw = uint32_t(*dest) & 0xffffff;
	write_reg24_word0(&raw, value);
	*dest = sign_extend24(raw);
}
void write_reg24_word2(int32_t *dest, uint16_t value)
{
	uint32_t raw = uint32_t(*dest) & 0xffffff;
	write_reg24_word2(&raw, value);
	*dest = sign_extend24(raw);
}

// 20 bit RAM B words: reg + 0 = val >> 4, reg + 2 = (val & 0xf) << 12.
void write_reg20_word0(uint32_t *dest, uint16_t value)
{
	*dest = (*dest & 0x0000f) | (uint32_t(value) << 4);
}
void write_reg20_word2(uint32_t *dest, uint16_t value)
{
	*dest = (*dest & 0xffff0) | ((value >> 12) & 0xf);
}

} // namespace

void mb87424_device::reset_state()
{
	memset(m_global, 0, sizeof(m_global));
	memset(m_voices, 0, sizeof(m_voices));
	m_active_mask = 0;
	m_ramp_enable = 0;
	m_glide_rate = 0;
	m_selected_voice = 0;
	m_prev_active_mask = 0;
	m_glide_tick_phase = 0;
	m_port_reg = 0;
	m_port_voice = 0;
	m_port_latch = 0;
}

bool mb87424_device::ramp_enabled() const
{
	return (m_ramp_enable & 0x02) && (m_ramp_enable & 0x04) && (m_ramp_enable & 0x18);
}

int32_t mb87424_device::glide_step(int32_t dist20, uint8_t rate)
{
	if ((rate & 0x1f) == 0)
		return dist20 | 1;
	if ((rate & 0x1f) == 0x1f)
		return int32_t(int64_t(dist20) >> 10) | 1;
	static const int MANT[4] = {16, 13, 11, 9};
	const int shift = (rate >> 2) & 7;
	const int64_t step = (int64_t(dist20) * MANT[rate & 3]) >> (6 + shift);
	return int32_t(step) | 1;
}

void mb87424_device::arm_glide(int32_t cur20, int32_t tgt20, uint8_t rate, int32_t *step20, bool *gliding)
{
	const int32_t dist = tgt20 - cur20;
	if (dist == 0)
	{
		*gliding = false;
		return;
	}
	*step20 = glide_step(dist, rate);
	*gliding = true;
}

void mb87424_device::register_w(uint16_t offset, uint8_t value)
{
	offset &= 0x7f;
	m_global[offset] = value;
	if (offset & 1)
	{
		const uint16_t word = m_little_endian_bus ? uint16_t(m_global[offset - 1]) | (uint16_t(value) << 8) : (uint16_t(m_global[offset - 1]) << 8) | uint16_t(value);
		write16(uint16_t(offset - 1), word);
	}
}

void mb87424_device::write16(uint16_t offset, uint16_t value)
{
	// global control block
	switch (offset)
	{
	case 0x10:
		m_active_mask = (m_active_mask & 0x0000ffff) | (uint32_t(value) << 16);
		return;
	case 0x12:
		m_active_mask = (m_active_mask & 0xffff0000) | value;
		return;
	case 0x14:
		m_ramp_enable = value;
		return;
	case 0x16:
		return;
	case 0x18:
		m_glide_rate = uint8_t(value);
		return;
	case 0x1a:
		return;
	case 0x1c:
		return;
	case 0x1e:
		return;
	default:
		break;
	}

	// voice read/select
	if (offset >= 0x40)
	{
		m_selected_voice = value & 0x1f;
		m_port_reg = offset < 0x60 ? uint8_t(offset & 0x0c) : uint8_t(0x20 | (offset & 0x1c));
		m_port_voice = uint8_t(value & 0x1f);
		m_port_latch = read_port24(); // silicon latches the value at select time
		return;
	}

	voice_state &voice = m_voices[m_selected_voice];
	const bool ramp = ramp_enabled();
	const bool active = (m_active_mask & (1u << m_selected_voice)) != 0;

	// per-voice block
	switch (offset)
	{
	case 0x00:
		write_reg24_word0(&voice.svf_state_band_00, value);
		break;
	case 0x02:
		write_reg24_word2(&voice.svf_state_band_00, value);
		break;
	case 0x04:
		write_reg24_word0(&voice.svf_state_high_04, value);
		break;
	case 0x06:
		write_reg24_word2(&voice.svf_state_high_04, value);
		break;

	case 0x08:
		write_reg24_word0(&voice.filter_control_08, value);
		break;
	case 0x0a:
		write_reg24_word2(&voice.filter_control_08, value);
		break;

	case 0x0c:
		write_reg24_word0(&voice.svf_state_low_0c, value);
		break;
	case 0x0e:
		write_reg24_word2(&voice.svf_state_low_0c, value);
		break;

	case 0x20:
		if (!ramp)
			break;
		write_reg20_word0(&voice.cutoff_current_20, value);
		voice.cutoff_gliding = false;
		break;
	case 0x22:
		write_reg20_word2(&voice.cutoff_current_20, value);
		break;
	case 0x24:
		if (!ramp)
			break;
		write_reg20_word0(&voice.gain_current_24, value);
		voice.gain_gliding = false;
		break;
	case 0x26:
		write_reg20_word2(&voice.gain_current_24, value);
		break;

	// TODO cutoff/gain integrator
	case 0x28:
	case 0x2a:
	case 0x2c:
	case 0x2e:
		break;

	case 0x30:
		write_reg20_word0(&voice.cutoff_target_30, value);
		if (ramp && active)
		{
			if (voice.cutoff_target_30 & TARGET_SNAP)
			{
				voice.cutoff_current_20 = voice.cutoff_target_30 & TARGET_VALUE;
				voice.cutoff_gliding = false;
			}
			else
			{
				arm_glide(int32_t(voice.cutoff_current_20), int32_t(voice.cutoff_target_30 & TARGET_VALUE), m_glide_rate, &voice.cutoff_step_28, &voice.cutoff_gliding);
			}
		}
		break;
	case 0x32:
		write_reg20_word2(&voice.cutoff_target_30, value);
		break;
	case 0x34:
		write_reg20_word0(&voice.gain_target_34, value);
		if (ramp && active)
		{
			if (voice.gain_target_34 & TARGET_SNAP)
			{
				voice.gain_current_24 = voice.gain_target_34 & TARGET_VALUE;
				voice.gain_gliding = false;
			}
			else
			{
				arm_glide(int32_t(voice.gain_current_24), int32_t(voice.gain_target_34 & TARGET_VALUE), m_glide_rate, &voice.gain_step_2c, &voice.gain_gliding);
			}
		}
		break;
	case 0x36:
		write_reg20_word2(&voice.gain_target_34, value);
		break;

	default:
		break;
	}
}

uint32_t mb87424_device::read_port24() const
{
	const voice_state &voice = m_voices[m_port_voice];
	switch (m_port_reg)
	{
	case 0x00:
		return uint32_t(voice.svf_state_band_00) & 0xffffff; // SVF BP integrator (die scale)
	case 0x04:
		return uint32_t(voice.svf_state_high_04) & 0xffffff; // companion (HP node)
	case 0x08:
		return voice.filter_control_08 & 0xffffff;
	case 0x0c:
		return uint32_t(voice.svf_state_low_0c) & 0xffffff; // SVF LP integrator (die scale)
	case 0x20:
		return voice.cutoff_current_20 & 0xfffff;
	case 0x24:
		return voice.gain_current_24 & 0xfffff;
	case 0x28:
		return uint32_t(voice.cutoff_step_28) & 0xfffff;
	case 0x2c:
		return uint32_t(voice.gain_step_2c) & 0xfffff;
	case 0x30:
		return voice.cutoff_target_30 & 0xfffff;
	case 0x34:
		return voice.gain_target_34 & 0xfffff;
	case 0x38:
	case 0x3c:
		return 0x0fffff;
	default:
		return 0;
	}
}

uint8_t mb87424_device::register_r(uint16_t offset) const
{
	offset &= 0x7f;
	const uint32_t v24 = m_little_endian_bus ? read_port24() : m_port_latch;
	const uint16_t lane = (offset & 2) ? uint16_t(v24 >> 16) : uint16_t(v24 & 0xffff);
	return m_little_endian_bus ? uint8_t((offset & 1) ? (lane >> 8) : lane) : uint8_t((offset & 1) ? lane : (lane >> 8));
}

enum class mux_mode : uint8_t
{
	low_pass = 0,
	high_pass = 1,
	band_pass = 2,
	Silent = 3,
};

enum class mux_structure : uint8_t
{
	Single = 0,
	Serial = 1,
	ring_pre = 2,
	ring_post = 3,
};

[[noreturn]] static void invalid_mux_selector()
{
	assert(false && "TVF mux selector outside its two-bit range");
	abort();
}

static inline int32_t filter_mode_mux(mux_mode mode, int32_t svf_state_low_00, int32_t svf_state_high_04, int32_t svf_state_band_0c)
{
	switch (mode)
	{
	case mux_mode::low_pass:
		return svf_state_low_00;
	case mux_mode::high_pass:
		return svf_state_high_04;
	case mux_mode::band_pass:
		return svf_state_band_0c;
	case mux_mode::Silent:
		return 0;
	}
	invalid_mux_selector();
}

static inline int32_t pre_ring_bypass_mux(int32_t input, mux_structure struct_, int32_t bypass)
{
	switch (struct_)
	{
	case mux_structure::Single:
		return input;
	case mux_structure::Serial:
		return input;
	case mux_structure::ring_pre:
		return bypass ? input : 0;
	case mux_structure::ring_post:
		return input;
	}
	invalid_mux_selector();
}

static inline int32_t post_ring_bypass_mux(int32_t input, mux_structure struct_, int32_t bypass)
{
	switch (struct_)
	{
	case mux_structure::Single:
		return input;
	case mux_structure::Serial:
		return input;
	case mux_structure::ring_pre:
		return input;
	case mux_structure::ring_post:
		return bypass ? input : 0;
	}
	invalid_mux_selector();
}

static inline int32_t mix_mux(int32_t input, mux_structure struct_)
{
	switch (struct_)
	{
	case mux_structure::Single:
		return 0;
	case mux_structure::Serial:
		return input;
	case mux_structure::ring_pre:
		return 0;
	case mux_structure::ring_post:
		return 0;
	}
	invalid_mux_selector();
}

static inline int32_t pre_tva_amux(int32_t gain_a, mux_structure struct_)
{
	switch (struct_)
	{
	case mux_structure::Single:
		return mul_unity;
	case mux_structure::Serial:
		return mul_unity;
	case mux_structure::ring_pre:
		return gain_a;
	case mux_structure::ring_post:
		return mul_unity;
	}
	invalid_mux_selector();
}

static inline int32_t post_tva_amux(int32_t gain_a, mux_structure struct_)
{
	switch (struct_)
	{
	case mux_structure::Single:
		return gain_a;
	case mux_structure::Serial:
		return 0;
	case mux_structure::ring_pre:
		return mul_unity >> 1; // unsure
	case mux_structure::ring_post:
		return gain_a;
	}
	invalid_mux_selector();
}

static inline int64_t &tvf_amux(mux_structure struct_, int64_t &acc_a, int64_t &acc_b)
{
	switch (struct_)
	{
	case mux_structure::Single:
		return acc_a;
	case mux_structure::Serial:
		return acc_b;
	case mux_structure::ring_pre:
		return acc_b;
	case mux_structure::ring_post:
		return acc_a;
	}
	invalid_mux_selector();
}

template <size_t VoiceCount>
void mb87424_device::process_frame(const int32_t *const in_voices, int32_t *const out_voices)
{
	static_assert(VoiceCount > 0, "TVF must render at least one voice");
	static_assert(VoiceCount <= VOICE_SLOTS, "TVF render count exceeds its physical voice slots");
	static_assert((VoiceCount & 1) == 0, "TVF renders paired voice contexts");
	const uint32_t active_mask = m_active_mask;

	const bool ramp = ramp_enabled();
	const bool tick = ++m_glide_tick_phase >= unsigned(2);
	if (tick)
		m_glide_tick_phase = 0;
	const uint8_t rate = m_glide_rate;
	for (size_t index = 0; index < VoiceCount; ++index)
	{
		voice_state &voice = m_voices[index];
		const bool active = (active_mask & (1u << index)) != 0;
		const bool rose = active && !(m_prev_active_mask & (1u << index));
		if (ramp && active)
		{
			const int32_t cutoff_tgt20 = int32_t(voice.cutoff_target_30 & TARGET_VALUE);
			const int32_t gain_tgt20 = int32_t(voice.gain_target_34 & TARGET_VALUE);
			if (rose)
			{
				if (voice.cutoff_target_30 & TARGET_SNAP)
				{
					voice.cutoff_current_20 = uint32_t(cutoff_tgt20);
					voice.cutoff_gliding = false;
				}
				else
				{
					arm_glide(int32_t(voice.cutoff_current_20), cutoff_tgt20, rate, &voice.cutoff_step_28, &voice.cutoff_gliding);
				}
				if (voice.gain_target_34 & TARGET_SNAP)
				{
					voice.gain_current_24 = uint32_t(gain_tgt20);
					voice.gain_gliding = false;
				}
				else
				{
					arm_glide(int32_t(voice.gain_current_24), gain_tgt20, rate, &voice.gain_step_2c, &voice.gain_gliding);
				}
			}
			if (!voice.cutoff_gliding && int32_t(voice.cutoff_current_20) != cutoff_tgt20)
				arm_glide(int32_t(voice.cutoff_current_20), cutoff_tgt20, rate, &voice.cutoff_step_28, &voice.cutoff_gliding);
			if (!voice.gain_gliding && int32_t(voice.gain_current_24) != gain_tgt20)
				arm_glide(int32_t(voice.gain_current_24), gain_tgt20, rate, &voice.gain_step_2c, &voice.gain_gliding);
			if (tick)
			{
				if (voice.cutoff_gliding)
				{
					int32_t cur = int32_t(voice.cutoff_current_20) + voice.cutoff_step_28;
					if ((voice.cutoff_step_28 > 0 && cur >= cutoff_tgt20) ||
						(voice.cutoff_step_28 < 0 && cur <= cutoff_tgt20))
					{
						cur = cutoff_tgt20;
						voice.cutoff_gliding = false;
					}
					voice.cutoff_current_20 = uint32_t(cur) & 0xfffff;
				}
				if (voice.gain_gliding)
				{
					int32_t cur = int32_t(voice.gain_current_24) + voice.gain_step_2c;
					if ((voice.gain_step_2c > 0 && cur >= gain_tgt20) || (voice.gain_step_2c < 0 && cur <= gain_tgt20))
					{
						cur = gain_tgt20;
						voice.gain_gliding = false;
					}
					voice.gain_current_24 = uint32_t(cur) & 0xfffff;
				}
			}
		}
		out_voices[index] = 0;
	}
	m_prev_active_mask = active_mask;

	for (size_t index = 0; index < VoiceCount; index += 2)
	{
		voice_state &v_a = m_voices[index];
		voice_state &v_b = m_voices[index + 1];

		const bool active_A = ((active_mask >> index) & 1) != 0;
		const bool active_B = ((active_mask >> (index + 1)) & 1) != 0;

		const int32_t input_A = in_voices[index];
		const int32_t input_B = in_voices[index + 1];
		const int32_t gain_A = int32_t(v_a.gain_current_24 >> 4);
		const int32_t gain_B = int32_t(v_b.gain_current_24 >> 4);
		const int32_t f_A = int32_t(v_a.cutoff_current_20 >> 4);
		const int32_t f_B = int32_t(v_b.cutoff_current_20 >> 4);
		const int32_t q_A = int32_t((v_a.filter_control_08 >> 8) & 0x1fff) << 2;
		const int32_t q_B = int32_t((v_b.filter_control_08 >> 8) & 0x1fff) << 2;
		int32_t &svf_state_band_00_a = v_a.svf_state_band_00;
		int32_t &svf_state_band_00_b = v_b.svf_state_band_00;
		int32_t &svf_state_high_04_a = v_a.svf_state_high_04;
		int32_t &svf_state_high_04_b = v_b.svf_state_high_04;
		int32_t &svf_state_low_0c_a = v_a.svf_state_low_0c;
		int32_t &svf_state_low_0c_b = v_b.svf_state_low_0c;
		const auto mode_A = static_cast<mux_mode>((v_a.filter_control_08 & 0x06) >> 1);
		const auto mode_B = static_cast<mux_mode>((v_b.filter_control_08 & 0x06) >> 1);
		const auto struct_A = static_cast<mux_structure>((v_a.filter_control_08 & 0x18) >> 3);
		// const uint32_t struct_B = (v_b.filter_control_08 & 0x18) >> 3; // doesn't depend on this
		const uint32_t tvf_ring_bypass_a = (v_a.filter_control_08 & 0x01) != 0;
		// const uint32_t tvf_ring_bypass_b = (v_b.filter_control_08 & 0x01) != 0; // doesn't depend on this

		//
		// algorithm
		// ===================================

		int64_t out_A = 0, out_B = 0;

		out_A = mac(0, input_A, pre_tva_amux(gain_A, struct_A));   // 1: pre TVA A
		out_B = mac(mix_mux(out_A, struct_A), input_B, mul_unity); // 2: mix A + B
		out_B = mac(pre_ring_bypass_mux(out_B, struct_A, tvf_ring_bypass_a),
					(struct_A == mux_structure::ring_pre ? out_A >> 6 : 0), out_B >> 6); // 3: pre RING

		if (!active_A)
			out_A = 0;
		if (!active_B)
			out_B = 0;

		// 4/5/6/7: TVF A
		svf_state_low_0c_a = mac(svf_state_low_0c_a, svf_state_band_00_a >> 6, f_A);                         // 4
		svf_state_high_04_a = mac_sub(tvf_amux(struct_A, out_A, out_B), svf_state_low_0c_a >> 6, mul_unity); // 5
		svf_state_high_04_a = mac_sub(svf_state_high_04_a, svf_state_band_00_a >> 6, q_A);                   // 6
		svf_state_band_00_a = mac(svf_state_band_00_a, svf_state_high_04_a >> 6, f_A);                       // 7
		tvf_amux(struct_A, out_A, out_B) =
			filter_mode_mux(mode_A, svf_state_low_0c_a, svf_state_high_04_a, svf_state_band_00_a);

		// 8/9/a/b: TVF B
		svf_state_low_0c_b = mac(svf_state_low_0c_b, svf_state_band_00_b >> 6, f_B);       // 8
		svf_state_high_04_b = mac_sub(out_B, svf_state_low_0c_b >> 6, mul_unity);          // 9
		svf_state_high_04_b = mac_sub(svf_state_high_04_b, svf_state_band_00_b >> 6, q_B); // a
		svf_state_band_00_b = mac(svf_state_band_00_b, svf_state_high_04_b >> 6, f_B);     // b
		out_B = filter_mode_mux(mode_B, svf_state_low_0c_b, svf_state_high_04_b, svf_state_band_00_b);

		out_A = mac(0, out_A >> 6, post_tva_amux(gain_A, struct_A)); // c: post TVA A
		out_B = mac(post_ring_bypass_mux(out_B, struct_A, tvf_ring_bypass_a),
					(struct_A == mux_structure::ring_post ? out_A >> 6 : 0), out_B >> 6); // d: post RING
		out_B = mac(0, out_B >> 6, gain_B);                                               // e: TVA B

		out_voices[index] = int32_t(out_A >> 6);
		out_voices[index + 1] = int32_t(out_B >> 6);
	}
}

template void mb87424_device::process_frame<24>(const int32_t *, int32_t *);
template void mb87424_device::process_frame<32>(const int32_t *, int32_t *);
