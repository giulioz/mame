// license:BSD-3-Clause
// copyright-holders:giulioz

#include "emu.h"
#include "roland_xp.h"

#include <algorithm>
#include <array>
#include <cmath>


DEFINE_DEVICE_TYPE(ROLAND_XP, roland_xp_device, "roland_xp", "Roland XP PCM+DSP")

// Cumulative-delta form of SCCore's four direct sample coefficients.  For a
// row A/B/C below, the equivalent direct coefficients are
// { 4096-A, A-B, B-C, C }.  Keeping this basis lets the DPCM stream be
// interpolated without materialising four absolute accumulator samples.
static constexpr int32_t interp_lut[3][128] = {
    {3385, 3401, 3417, 3432, 3448, 3463, 3478, 3492, 3506, 3521, 3534, 3548, 3562, 3575, 3588, 3601,
     3614, 3626, 3638, 3650, 3662, 3673, 3685, 3696, 3707, 3718, 3728, 3739, 3749, 3759, 3768, 3778,
     3787, 3796, 3805, 3814, 3823, 3831, 3839, 3847, 3855, 3863, 3870, 3878, 3885, 3892, 3899, 3905,
     3912, 3918, 3924, 3930, 3936, 3942, 3948, 3953, 3958, 3963, 3968, 3973, 3978, 3983, 3987, 3991,
     3995, 4000, 4004, 4007, 4011, 4015, 4018, 4022, 4025, 4028, 4031, 4034, 4037, 4040, 4042, 4045,
     4047, 4050, 4052, 4054, 4057, 4059, 4061, 4063, 4064, 4066, 4068, 4070, 4071, 4073, 4074, 4076,
     4077, 4078, 4079, 4081, 4082, 4083, 4084, 4085, 4086, 4086, 4087, 4088, 4089, 4089, 4090, 4091,
     4091, 4092, 4092, 4093, 4093, 4094, 4094, 4094, 4094, 4095, 4095, 4095, 4095, 4095, 4095, 4095},

    { 710,  726,  742,  758,  775,  792,  809,  826,  844,  861,  879,  897,  915,  933,  952,  971,
      990, 1009, 1028, 1047, 1067, 1087, 1106, 1126, 1147, 1167, 1188, 1208, 1229, 1250, 1271, 1292,
     1314, 1335, 1357, 1379, 1400, 1423, 1445, 1467, 1489, 1512, 1534, 1557, 1580, 1602, 1625, 1648,
     1671, 1695, 1718, 1741, 1764, 1788, 1811, 1835, 1858, 1882, 1906, 1929, 1953, 1977, 2000, 2024,
     2048, 2069, 2095, 2119, 2143, 2166, 2190, 2214, 2237, 2261, 2284, 2308, 2331, 2355, 2378, 2401,
     2425, 2448, 2471, 2494, 2517, 2539, 2562, 2585, 2607, 2630, 2652, 2674, 2696, 2718, 2740, 2762,
     2783, 2805, 2826, 2847, 2868, 2889, 2910, 2931, 2951, 2971, 2991, 3011, 3031, 3051, 3070, 3089,
     3108, 3127, 3146, 3164, 3182, 3200, 3218, 3236, 3253, 3271, 3288, 3304, 3321, 3338, 3354, 3370},

    {  0,   0,   0,   1,   1,   1,   2,   2,   3,   3,   3,   4,   4,   5,   5,   6,
       6,   7,   8,   8,   9,  10,  10,  11,  12,  13,  14,  15,  16,  17,  18,  19,
      20,  22,  23,  24,  26,  27,  29,  30,  32,  34,  36,  38,  40,  42,  44,  46,
      49,  51,  53,  56,  59,  62,  65,  68,  71,  74,  77,  81,  84,  88,  92,  96,
     100, 104, 109, 113, 118, 122, 127, 132, 137, 143, 148, 154, 160, 165, 171, 178,
     184, 191, 197, 204, 211, 219, 226, 234, 241, 249, 257, 266, 274, 283, 292, 301,
     310, 319, 329, 339, 349, 359, 369, 380, 391, 402, 413, 424, 436, 448, 460, 472,
     484, 497, 510, 523, 536, 549, 563, 577, 591, 605, 619, 634, 648, 663, 679, 694},
};

// Pitch/cutoff exponential LUT.  Entries 0-255 are exactly
// floor(2^17 * 2^(index/257)); entry 256 is the forced octave endpoint 2^18.
static const int32_t s_log_table[257] = {
	131072, 131425, 131780, 132136, 132493, 132851, 133210, 133570,
	133930, 134292, 134655, 135018, 135383, 135749, 136115, 136483,
	136851, 137221, 137592, 137963, 138336, 138709, 139084, 139460,
	139836, 140214, 140593, 140972, 141353, 141735, 142118, 142501,
	142886, 143272, 143659, 144047, 144436, 144826, 145217, 145610,
	146003, 146397, 146793, 147189, 147587, 147985, 148385, 148786,
	149187, 149590, 149994, 150399, 150806, 151213, 151621, 152031,
	152441, 152853, 153266, 153680, 154095, 154511, 154928, 155347,
	155766, 156187, 156609, 157032, 157456, 157881, 158307, 158735,
	159164, 159593, 160024, 160457, 160890, 161325, 161760, 162197,
	162635, 163074, 163515, 163956, 164399, 164843, 165288, 165735,
	166182, 166631, 167081, 167532, 167985, 168439, 168894, 169350,
	169807, 170266, 170725, 171187, 171649, 172112, 172577, 173043,
	173511, 173979, 174449, 174920, 175393, 175866, 176341, 176818,
	177295, 177774, 178254, 178736, 179218, 179702, 180188, 180674,
	181162, 181651, 182142, 182634, 183127, 183622, 184118, 184615,
	185113, 185613, 186115, 186617, 187121, 187627, 188133, 188642,
	189151, 189662, 190174, 190688, 191203, 191719, 192237, 192756,
	193277, 193799, 194322, 194847, 195373, 195901, 196430, 196960,
	197492, 198026, 198560, 199097, 199634, 200173, 200714, 201256,
	201800, 202345, 202891, 203439, 203989, 204539, 205092, 205646,
	206201, 206758, 207316, 207876, 208438, 209001, 209565, 210131,
	210699, 211268, 211838, 212410, 212984, 213559, 214136, 214714,
	215294, 215876, 216459, 217043, 217629, 218217, 218807, 219397,
	219990, 220584, 221180, 221777, 222376, 222977, 223579, 224183,
	224788, 225395, 226004, 226614, 227226, 227840, 228455, 229072,
	229691, 230311, 230933, 231557, 232182, 232810, 233438, 234069,
	234701, 235335, 235970, 236608, 237247, 237887, 238530, 239174,
	239820, 240468, 241117, 241768, 242421, 243076, 243732, 244391,
	245051, 245713, 246376, 247042, 247709, 248378, 249048, 249721,
	250396, 251072, 251750, 252430, 253111, 253795, 254481, 255168,
	255857, 256548, 257241, 257936, 258632, 259331, 260031, 260733,
	262144,
};


roland_xp_device::roland_xp_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, ROLAND_XP, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, device_rom_interface(mconfig, *this)
	, m_int_callback(*this)
	, m_rate(0)
	, m_stream(nullptr)
	, m_control_phase(0)
{
}


//-------------------------------------------------
//  pitch_to_increment - convert log pitch value
//  to linear phase increment (32-bit, 16.16 format)
//
//  From CInterpP::Run_PitchLiner:
//    bits 0-5: interpolation fraction (6 bits)
//    bits 6-13: table index (8 bits)
//    bits 14+: octave shift (inverted, 4 bits)
//-------------------------------------------------

int32_t roland_xp_device::pitch_to_increment(int32_t pitch_val)
{
	if (pitch_val <= 0)
		return 0;

	const unsigned tbl_idx = (pitch_val >> 6) & 0xff;
	const int frac = pitch_val & 0x3f;
	const int32_t v0 = s_log_table[tbl_idx];
	const int32_t v1 = s_log_table[tbl_idx + 1];

	// Linear interpolation with 6-bit fraction, then divide by 64
	int32_t interp = int32_t((int64_t(frac) * v1 + int64_t(64 - frac) * v0));
	// Rounding: add (1 << 5) before shift, matching reference's signed rounding
	interp = (interp + ((interp >> 31) >> 26)) >> 6;

	// Octave shift: ~(pitch_val >> 14) & 0xf
	const int shift = (~(pitch_val >> 14)) & 0xf;
	return interp >> shift;
}

uint16_t roland_xp_device::decode_interp_flags(uint32_t ctrl, bool force_log)
{
	static constexpr uint16_t flag_type[4] = { 2, 0, 4, 4 };
	static constexpr uint16_t flag_interval[4] = { 0, 8, 16, 24 };

	const uint16_t mode = force_log ? 2 : flag_type[(ctrl >> 14) & 3];
	const uint16_t interval = flag_interval[(ctrl >> 12) & 3];
	return mode | interval;
}

uint32_t roland_xp_device::interp_mask(uint16_t flags)
{
	switch ((flags >> 3) & 3)
	{
	case 0: return 0x00;
	case 1: return 0x07;
	case 2: return 0x1f;
	default: return 0x7f;
	}
}

int16_t roland_xp_device::interp_q14_output(const interp_state &s)
{
	return (int16_t)(s.current >> 3);
}

int16_t roland_xp_device::limit_tvf_frequency(int16_t f_q14, const interp_state &q)
{
	// CInterpQ::Run clamps F through a 1024-entry stability table indexed by
	// q.current >> 8.  The table closely follows the Chamberlin SVF stability
	// boundary sqrt(q*q + 4) - q.  Generate the fixed-point equivalent once.
	static const std::array<int16_t, 1024> limiter = []
	{
		std::array<int16_t, 1024> result{};
		for (unsigned i = 0; i < result.size(); i++)
		{
			const double q_value = double(i + 1) / 512.0;
			const double limit = std::sqrt(q_value * q_value + 4.0) - q_value;
			result[i] = int16_t(std::min(32767.0, std::floor(limit * 16384.0)));
		}
		return result;
	}();

	const unsigned index = std::min<unsigned>(std::max<int32_t>(q.current, 0) >> 8, 1023);
	return std::min(f_q14, limiter[index]);
}

int roland_xp_device::decode_filter_type(const pcm_voice &v)
{
	static constexpr uint8_t type_map[4] = { 0, 2, 1, 3 };
	if (v.filter_type_select & 0xf000)
		return v.tvf_q_interp_ctrl & 3;
	return type_map[(v.filter_type_select >> 10) & 3];
}

void roland_xp_device::interp_start_linear(interp_state &s)
{
	const int32_t diff = s.target - s.current;
	int32_t step = (int32_t)(((int64_t)s.rate * diff) >> 13);
	if (diff > 0 && step == 0)
		step = 1;
	s.aux = step;
	s.flags &= ~1;
	if (s.current != s.target)
		s.flags |= 1;
}

void roland_xp_device::interp_start_log(interp_state &s)
{
	s.aux = s.current << 10;
	s.flags &= ~1;
	if (s.current != s.target)
		s.flags |= 1;
}

void roland_xp_device::interp_start_trunk(interp_state &s)
{
	s.aux = s.current / 2;
	s.target = 0;
	s.flags &= ~1;
	if (s.current != 0)
		s.flags |= 1;
}

void roland_xp_device::interp_set_newdist_linear(interp_state &s)
{
	const int32_t diff = s.target - s.current;
	int32_t step = (int32_t)(((int64_t)s.rate * diff) >> 13);
	if (diff > 0 && step == 0)
		step = 1;
	s.aux = step;
	s.flags &= ~1;
	if (s.current != s.target)
		s.flags |= 1;
}

bool roland_xp_device::interp_tick_due(interp_state &s)
{
	s.counter++;
	return (s.counter & interp_mask(s.flags)) == 0;
}

void roland_xp_device::interp_update_linear(interp_state &s)
{
	int32_t next = int32_t(uint32_t(s.current) + uint32_t(s.aux));
	// SCCore deliberately treats a zero step like a descending step.  A tiny
	// descending segment can therefore remain active until it is retargeted.
	const bool overshoot = (s.aux < 1) ? (next < s.target) : (next > s.target);
	if (overshoot)
		next = s.target;
	s.current = next;
	if (s.current == s.target)
	{
		s.flags &= ~1;
		s.aux = 0;
	}
}

void roland_xp_device::interp_update_log(interp_state &s)
{
	// The intermediate error is narrowed to 16 bits before the multiply.  The
	// two unsigned comparisons in SCCore enforce a *minimum* magnitude of 1024
	// (one exposed integer unit), rather than limiting the maximum movement.
	const int32_t error = int32_t(uint32_t(s.target) * 0x400U - uint32_t(s.aux));
	const int32_t delta = int32_t(s.rate) * int16_t(error >> 13);
	uint32_t adjusted = 0x400;
	if (0x3ffU < uint32_t(delta))
		adjusted = uint32_t(delta);
	uint32_t bounded = 0xfffffc00U;
	if (adjusted < 0xfffffc01U)
		bounded = adjusted;
	s.aux = int32_t(uint32_t(s.aux) + bounded);
	s.current = s.aux >> 10;
	if (s.current == s.target)
		s.flags &= ~1;
}

void roland_xp_device::interp_update_trunk(interp_state &s)
{
	if (s.aux < s.current)
	{
		s.target = int32_t(uint32_t(s.target) - uint32_t(int32_t(s.rate)));
		s.current = int32_t(uint32_t(s.current) + uint32_t(s.target));
		// Run_Trunk returns here even when current has just reached zero; the
		// active flag is only cleared by the other branch below.
		return;
	}

	s.target = int32_t(uint32_t(s.target) + uint32_t(int32_t(s.rate)));
	if (s.target < 1)
	{
		s.current = int32_t(uint32_t(s.current) + uint32_t(s.target));
		if (s.current != 0)
			return;
	}
	else
	{
		s.current = 0;
	}
	s.flags &= ~1;
}

void roland_xp_device::interp_update_pitch(interp_state &s)
{
	if (s.flags & 1 && interp_tick_due(s))
		interp_update_linear(s);
	s.output_i = pitch_to_increment(s.current);
}

void roland_xp_device::interp_update_f(interp_state &s)
{
	if (s.flags & 1 && interp_tick_due(s))
		interp_update_linear(s);
}

void roland_xp_device::interp_update_q(interp_state &s)
{
	if (s.flags & 1 && interp_tick_due(s))
		interp_update_log(s);
}

void roland_xp_device::interp_update_a(interp_state &s)
{
	if (s.flags & 1 && interp_tick_due(s))
	{
		switch (s.flags & 6)
		{
		case 0: interp_update_linear(s); break;
		case 2: interp_update_log(s); break;
		case 4: interp_update_trunk(s); break;
		}
	}
}

void roland_xp_device::interp_update_am(interp_state &s)
{
	// CInterpAM is unusual: Run advances its interval counter even after its
	// active bit clears.  This matters when a later target is installed.
	if (interp_tick_due(s))
		interp_update_log(s);
}

void roland_xp_device::reload_pitch_interp(pcm_voice &v)
{
	v.pitch_interp.flags = decode_interp_flags(v.pitch_interp_ctrl);
	v.pitch_interp.rate = v.pitch_interp_ctrl & 0x0fff;
	v.pitch_interp.counter = 0;
	v.pitch_interp.current = v.pitch_start_val;
	v.pitch_interp.target = v.pitch_destination_val;
	interp_start_linear(v.pitch_interp);
	v.pitch_interp.output_i = pitch_to_increment(v.pitch_interp.current);
}

void roland_xp_device::reload_amp_interp(pcm_voice &v)
{
	v.amp_interp.flags = decode_interp_flags(v.amp_interp_ctrl);
	v.amp_interp.rate = v.amp_interp_ctrl & 0x0fff;
	v.amp_interp.counter = 0;
	v.amp_interp.current = v.amp_start_val;
	v.amp_interp.target = v.amp_destination_val;

	switch (v.amp_interp.flags & 6)
	{
	case 0: interp_start_linear(v.amp_interp); break;
	case 2: interp_start_log(v.amp_interp); break;
	case 4: interp_start_trunk(v.amp_interp); break;
	}
}

void roland_xp_device::reload_ampmod_interp(pcm_voice &v)
{
	v.ampmod_interp.flags = decode_interp_flags(v.ampmod_interp_ctrl, true);
	v.ampmod_interp.rate = v.ampmod_interp_ctrl & 0x0fff;
	v.ampmod_interp.counter = 0;
	v.ampmod_interp.current = v.ampmod_start_val;
	v.ampmod_interp.target = v.ampmod_destination_val;
	interp_start_log(v.ampmod_interp);
}

void roland_xp_device::reload_tvf_q_interp(pcm_voice &v)
{
	v.tvf_q_interp.flags = decode_interp_flags(v.tvf_q_interp_ctrl, true);
	v.tvf_q_interp.rate = v.tvf_q_interp_ctrl & 0x0fff;
	v.tvf_q_interp.counter = 0;
	v.tvf_q_interp.current = v.tvf_q_start_val >> 2;
	v.tvf_q_interp.target = v.tvf_q_destination_val;
	interp_start_log(v.tvf_q_interp);
}

void roland_xp_device::reload_tvf_f_interp(pcm_voice &v)
{
	v.tvf_f_interp.flags = decode_interp_flags(v.tvf_f_interp_ctrl);
	v.tvf_f_interp.rate = v.tvf_f_interp_ctrl & 0x0fff;
	v.tvf_f_interp.counter = 0;
	v.tvf_f_interp.current = pitch_to_increment(v.tvf_f_start_val);
	v.tvf_f_interp.target = pitch_to_increment(v.tvf_f_destination_val);
	interp_start_linear(v.tvf_f_interp);
}

void roland_xp_device::reload_all_interps(pcm_voice &v)
{
	reload_pitch_interp(v);
	reload_amp_interp(v);
	reload_ampmod_interp(v);
	reload_tvf_q_interp(v);
	reload_tvf_f_interp(v);
}

void roland_xp_device::retarget_pitch_interp(pcm_voice &v)
{
	v.pitch_interp.flags = decode_interp_flags(v.pitch_interp_ctrl);
	v.pitch_interp.rate = v.pitch_interp_ctrl & 0x0fff;
	v.pitch_interp.target = v.pitch_destination_val;
	interp_set_newdist_linear(v.pitch_interp);
	v.pitch_interp.output_i = pitch_to_increment(v.pitch_interp.current);
}

void roland_xp_device::retarget_amp_interp(pcm_voice &v)
{
	v.amp_interp.flags = decode_interp_flags(v.amp_interp_ctrl);
	v.amp_interp.rate = v.amp_interp_ctrl & 0x0fff;
	v.amp_interp.target = v.amp_destination_val;
	v.amp_interp.flags &= ~1;
	if (v.amp_interp.current != v.amp_interp.target)
		v.amp_interp.flags |= 1;
	if ((v.amp_interp.flags & 6) == 0)
		interp_set_newdist_linear(v.amp_interp);
}

void roland_xp_device::retarget_ampmod_interp(pcm_voice &v)
{
	v.ampmod_interp.flags = decode_interp_flags(v.ampmod_interp_ctrl, true);
	v.ampmod_interp.rate = v.ampmod_interp_ctrl & 0x0fff;
	v.ampmod_interp.target = v.ampmod_destination_val;
	v.ampmod_interp.flags &= ~1;
	if (v.ampmod_interp.current != v.ampmod_interp.target)
		v.ampmod_interp.flags |= 1;
}

void roland_xp_device::retarget_tvf_q_interp(pcm_voice &v)
{
	v.tvf_q_interp.flags = decode_interp_flags(v.tvf_q_interp_ctrl, true);
	v.tvf_q_interp.rate = v.tvf_q_interp_ctrl & 0x0fff;
	v.tvf_q_interp.target = v.tvf_q_destination_val;
	v.tvf_q_interp.flags &= ~1;
	if (v.tvf_q_interp.current != v.tvf_q_interp.target)
		v.tvf_q_interp.flags |= 1;
}

void roland_xp_device::retarget_tvf_f_interp(pcm_voice &v)
{
	v.tvf_f_interp.flags = decode_interp_flags(v.tvf_f_interp_ctrl);
	v.tvf_f_interp.rate = v.tvf_f_interp_ctrl & 0x0fff;
	v.tvf_f_interp.target = pitch_to_increment(v.tvf_f_destination_val);
	interp_set_newdist_linear(v.tvf_f_interp);
}

void roland_xp_device::reset_voice_runtime(pcm_voice &v)
{
	v.current_addr = v.sample_start & 0xfffff;
	v.dpcm_val = 0;
	v.subphase = 0;
	v.alt_loop_dir = false;
	v.playing = false;
	v.loop_irq_pending = false;
	v.tvf_bp = 0;
	v.tvf_lp = 0;
	v.pitch_interp = {};
	v.amp_interp = {};
	v.ampmod_interp = {};
	v.tvf_q_interp = {};
	v.tvf_f_interp = {};
}

void roland_xp_device::update_irq_line()
{
	const bool line = m_irq_count != 0;
	m_irq_line = line;
	m_int_callback(line ? ASSERT_LINE : CLEAR_LINE);
}

void roland_xp_device::raise_irq(uint8_t reason, uint8_t source, uint16_t data)
{
	if (m_irq_count == IRQ_QUEUE_SIZE)
	{
		logerror("Roland XP: IRQ queue overflow (reason %u source %u)\n", reason, source);
		return;
	}

	const unsigned tail = (m_irq_head + m_irq_count) % IRQ_QUEUE_SIZE;
	m_irq_status[tail] = (uint16_t(source & 0x3f) << 8) | (reason & 0x0f);
	m_irq_data[tail] = data;
	m_irq_count++;
	update_irq_line();
}

void roland_xp_device::acknowledge_irq()
{
	if (!m_irq_count)
		return;

	const uint16_t status = m_irq_status[m_irq_head];
	if ((status & 0x0f) == 5)
		m_voices[(status >> 8) & 0x3f].loop_irq_pending = false;

	m_irq_head = (m_irq_head + 1) % IRQ_QUEUE_SIZE;
	m_irq_count--;
	update_irq_line();
}

uint16_t roland_xp_device::current_irq_status() const
{
	return m_irq_count ? m_irq_status[m_irq_head] : 0;
}

uint16_t roland_xp_device::current_irq_data() const
{
	return m_irq_count ? m_irq_data[m_irq_head] : 0;
}

uint8_t roland_xp_device::read_command_status()
{
	// Voice-start code polls the low nibble until it becomes zero.  A command
	// write is observed busy once; repeated writes of the same command must not
	// restart this countdown because the firmware retries the write while busy.
	if (m_command_busy_reads)
	{
		m_command_busy_reads--;
		return 1;
	}
	return 0;
}

uint16_t roland_xp_device::next_random()
{
	// Exact XP generator used by Roland's SCCore reference implementation.
	// The physical JV firmware enables its random source by writing one to
	// IRAM3 slot 3 (host address 0x320c), then trigger-reads that slot whenever
	// it needs a new value.
	uint16_t next1 = (m_random_seed1 >> 1) | (m_random_seed1 & 0x8000);
	if (BIT(m_random_seed1, 5))
		next1 ^= 0x8000;
	m_random_seed1 = next1;

	uint16_t next2 = m_random_seed2 << 1;
	if (BIT(m_random_seed2, 2))
		next2 = (next2 | BIT(m_random_seed2, 13)) ^ 1;
	else
		next2 |= BIT(m_random_seed2, 9);
	m_random_seed2 = next2;
	return m_random_seed1 ^ m_random_seed2;
}

uint32_t roland_xp_device::dsp_read_u32(offs_t offset) const
{
	const unsigned base = offset - 0x2c00;
	return (uint32_t(m_dsp_program[base + 0]) << 24) |
		(uint32_t(m_dsp_program[base + 1]) << 16) |
		(uint32_t(m_dsp_program[base + 2]) << 8) |
		uint32_t(m_dsp_program[base + 3]);
}

uint16_t roland_xp_device::dsp_read_u16(offs_t offset) const
{
	const unsigned base = offset - 0x2c00;
	return (uint16_t(m_dsp_program[base + 0]) << 8) |
		uint16_t(m_dsp_program[base + 1]);
}

void roland_xp_device::dsp_write_u32(offs_t offset, uint32_t data)
{
	const unsigned base = offset - 0x2c00;
	m_dsp_program[base + 0] = data >> 24;
	m_dsp_program[base + 1] = data >> 16;
	m_dsp_program[base + 2] = data >> 8;
	m_dsp_program[base + 3] = data;
}

void roland_xp_device::update_iram3_breakpoints()
{
	// IRAM3 has a second, 16-bit host bank containing 64 breakpoint targets.
	// The four time constants at 0x3928-0x392e control groups of 16 slots.
	// SCCore models the same exponential approach at one update per two output
	// samples.  Its target is value << 6 in Q15; the XP's host-visible IRAM3
	// representation is Q22, giving the equivalent value << 13 here.
	for (unsigned index = 0; index < NUM_IRAM3_BREAKPOINTS; index++)
	{
		if (!m_iram3_breakpoint_active[index])
			continue;

		const unsigned group = index >> 4;
		const uint16_t rate = (uint16_t(m_global_config[0x28 + group * 2]) << 8) |
			m_global_config[0x29 + group * 2];
		if (!rate)
			continue;

		const int32_t current = int32_t(dsp_read_u32(0x3200 + index * 4));
		const int32_t target = int32_t(dsp_read_u16(0x3300 + index * 2) & 0x01ff) << 13;
		const int64_t delta = int64_t(target) - current;
		if (!delta)
		{
			m_iram3_breakpoint_active[index] = false;
			continue;
		}

		// Round away from zero as SCCore's signed multiply/logical-shift sequence
		// does, while retaining the host IRAM word's additional Q22 precision.
		// Clamping also makes out-of-range diagnostic rates benign.
		const uint64_t distance = delta < 0 ? uint64_t(-delta) : uint64_t(delta);
		const uint64_t magnitude = std::min<uint64_t>(distance,
			(distance * rate + 0xffffU) >> 16);
		const int64_t movement = delta < 0 ? -int64_t(magnitude) : int64_t(magnitude);
		const int32_t next = int32_t(int64_t(current) + movement);
		dsp_write_u32(0x3200 + index * 4, uint32_t(next));
		if (next == target)
			m_iram3_breakpoint_active[index] = false;
	}
}

//-------------------------------------------------
//  device_start
//-------------------------------------------------

void roland_xp_device::device_start()
{
	m_rate = clock() / 768; // 24.576 MHz / 768 = 32 kHz

	m_stream = stream_alloc(0, 2, m_rate);

	m_dram = std::make_unique<uint8_t[]>(DRAM_SIZE);

	save_item(NAME(m_control_phase));
	save_item(NAME(m_reg));
	save_item(NAME(m_dsp_program));
	save_item(NAME(m_dsp_read_latch));
	save_item(NAME(m_dsp_read_latch_mask));
	save_item(NAME(m_command_busy_reads));
	save_item(NAME(m_random_seed1));
	save_item(NAME(m_random_seed2));
	save_item(NAME(m_iram3_breakpoint_active));
	save_item(NAME(m_irq_status));
	save_item(NAME(m_irq_data));
	save_item(NAME(m_irq_head));
	save_item(NAME(m_irq_count));
	save_item(NAME(m_irq_line));
	save_item(NAME(m_irq_control));
	save_item(NAME(m_global_config));
	save_item(NAME(m_dsp.acc));
	save_item(NAME(m_dsp.hold));
	save_item(NAME(m_dsp.coefreg));
	save_item(NAME(m_dsp.acc_lock));
	save_item(NAME(m_dsp.eram_addr));
	save_pointer(NAME(m_dram), DRAM_SIZE);
	
	save_pointer(&m_reg[0x0000/4], "wave_ctrl", 64);
	save_pointer(&m_reg[0x0100/4], "sample_start", 64);
	save_pointer(&m_reg[0x0200/4], "sample_loop", 64);
	save_pointer(&m_reg[0x0300/4], "sample_end", 64);
	save_pointer(&m_reg[0x1100/4], "tvf_q_destination_val", 64);
	save_pointer(&m_reg[0x1200/4], "pitch_destination_val", 64);
	save_pointer(&m_reg[0x1300/4], "tvf_f_destination_val", 64);
	save_pointer(&m_reg[0x1400/4], "ampmod_destination_val", 64);
	save_pointer(&m_reg[0x1500/4], "amp_destination_val", 64);
	save_pointer(&m_reg[0x1600/4], "tvf_q_interp_ctrl", 64);
	save_pointer(&m_reg[0x1700/4], "pitch_interp_ctrl", 64);
	save_pointer(&m_reg[0x1800/4], "tvf_f_interp_ctrl", 64);
	save_pointer(&m_reg[0x1900/4], "ampmod_interp_ctrl", 64);
	save_pointer(&m_reg[0x1a00/4], "amp_interp_ctrl", 64);
	save_pointer(&m_reg[0x1b00/4], "pitch_start_val", 64);
	save_pointer(&m_reg[0x1c00/4], "tvf_f_start_val", 64);
	save_pointer(&m_reg[0x1d00/4], "ampmod_start_val", 64);
	save_pointer(&m_reg[0x1e00/4], "amp_start_val", 64);
	save_pointer(&m_reg[0x2000/4], "filter_type_select", 64);
	save_pointer(&m_reg[0x2100/4], "tvf_q_start_val", 64);

	for (int i = 0; i < NUM_VOICES; i++)
	{
		save_item(NAME(m_voices[i].wave_ctrl), i);
		save_item(NAME(m_voices[i].sample_start), i);
		save_item(NAME(m_voices[i].sample_loop), i);
		save_item(NAME(m_voices[i].sample_end), i);
		save_item(NAME(m_voices[i].mixer_send), i);
		save_item(NAME(m_voices[i].filter_type_select), i);
		save_item(NAME(m_voices[i].pitch_destination_val), i);
		save_item(NAME(m_voices[i].pitch_start_val), i);
		save_item(NAME(m_voices[i].pitch_interp_ctrl), i);
		save_item(NAME(m_voices[i].amp_destination_val), i);
		save_item(NAME(m_voices[i].amp_start_val), i);
		save_item(NAME(m_voices[i].amp_interp_ctrl), i);
		save_item(NAME(m_voices[i].ampmod_destination_val), i);
		save_item(NAME(m_voices[i].ampmod_start_val), i);
		save_item(NAME(m_voices[i].ampmod_interp_ctrl), i);
		save_item(NAME(m_voices[i].tvf_f_destination_val), i);
		save_item(NAME(m_voices[i].tvf_f_start_val), i);
		save_item(NAME(m_voices[i].tvf_f_interp_ctrl), i);
		save_item(NAME(m_voices[i].tvf_q_destination_val), i);
		save_item(NAME(m_voices[i].tvf_q_start_val), i);
		save_item(NAME(m_voices[i].tvf_q_interp_ctrl), i);
		save_item(NAME(m_voices[i].current_addr), i);
		save_item(NAME(m_voices[i].dpcm_val), i);
		save_item(NAME(m_voices[i].subphase), i);
		save_item(NAME(m_voices[i].alt_loop_dir), i);
		save_item(NAME(m_voices[i].playing), i);
		save_item(NAME(m_voices[i].loop_irq_pending), i);
		save_item(NAME(m_voices[i].tvf_bp), i);
		save_item(NAME(m_voices[i].tvf_lp), i);

		save_item(NAME(m_voices[i].pitch_interp.flags), i);
		save_item(NAME(m_voices[i].pitch_interp.rate), i);
		save_item(NAME(m_voices[i].pitch_interp.counter), i);
		save_item(NAME(m_voices[i].pitch_interp.current), i);
		save_item(NAME(m_voices[i].pitch_interp.target), i);
		save_item(NAME(m_voices[i].pitch_interp.aux), i);
		save_item(NAME(m_voices[i].pitch_interp.output_i), i);

		save_item(NAME(m_voices[i].amp_interp.flags), i);
		save_item(NAME(m_voices[i].amp_interp.rate), i);
		save_item(NAME(m_voices[i].amp_interp.counter), i);
		save_item(NAME(m_voices[i].amp_interp.current), i);
		save_item(NAME(m_voices[i].amp_interp.target), i);
		save_item(NAME(m_voices[i].amp_interp.aux), i);
		save_item(NAME(m_voices[i].amp_interp.output_i), i);

		save_item(NAME(m_voices[i].ampmod_interp.flags), i);
		save_item(NAME(m_voices[i].ampmod_interp.rate), i);
		save_item(NAME(m_voices[i].ampmod_interp.counter), i);
		save_item(NAME(m_voices[i].ampmod_interp.current), i);
		save_item(NAME(m_voices[i].ampmod_interp.target), i);
		save_item(NAME(m_voices[i].ampmod_interp.aux), i);
		save_item(NAME(m_voices[i].ampmod_interp.output_i), i);

		save_item(NAME(m_voices[i].tvf_q_interp.flags), i);
		save_item(NAME(m_voices[i].tvf_q_interp.rate), i);
		save_item(NAME(m_voices[i].tvf_q_interp.counter), i);
		save_item(NAME(m_voices[i].tvf_q_interp.current), i);
		save_item(NAME(m_voices[i].tvf_q_interp.target), i);
		save_item(NAME(m_voices[i].tvf_q_interp.aux), i);
		save_item(NAME(m_voices[i].tvf_q_interp.output_i), i);

		save_item(NAME(m_voices[i].tvf_f_interp.flags), i);
		save_item(NAME(m_voices[i].tvf_f_interp.rate), i);
		save_item(NAME(m_voices[i].tvf_f_interp.counter), i);
		save_item(NAME(m_voices[i].tvf_f_interp.current), i);
		save_item(NAME(m_voices[i].tvf_f_interp.target), i);
		save_item(NAME(m_voices[i].tvf_f_interp.aux), i);
		save_item(NAME(m_voices[i].tvf_f_interp.output_i), i);
	}

	machine().save().register_postload(save_prepost_delegate(FUNC(roland_xp_device::update_irq_line), this));

	logerror("Roland XP: Clock %u, Rate %u\n", clock(), m_rate);
}

//-------------------------------------------------
//  device_reset
//-------------------------------------------------

void roland_xp_device::device_reset()
{
	m_irq_line = false;
	m_int_callback(CLEAR_LINE);
	m_control_phase = 0;

	for (auto &v : m_voices)
		v = pcm_voice{};

	std::fill_n(m_reg, REG_ARRAY_SIZE, 0);
	std::fill_n(m_dsp_program, DSP_PROGRAM_SIZE, 0);
	std::fill_n(m_dsp_read_latch, 4, 0);
	m_dsp_read_latch_mask = 0;
	m_command_busy_reads = 0;
	m_random_seed1 = 0xefa6;
	m_random_seed2 = 0x9c23;
	std::fill_n(m_iram3_breakpoint_active, NUM_IRAM3_BREAKPOINTS, false);
	std::fill_n(m_irq_status, IRQ_QUEUE_SIZE, 0);
	std::fill_n(m_irq_data, IRQ_QUEUE_SIZE, 0);
	m_irq_head = 0;
	m_irq_count = 0;
	m_irq_control = 0;
	std::fill_n(m_global_config, GLOBAL_CONFIG_SIZE, 0);
	// A one in the reset bitmap means idle/done.  Firmware clears a bit to
	// request a reset, and the chip sets it again after processing it.
	std::fill_n(m_global_config, 8, 0xff);
	std::fill_n(m_dram.get(), DRAM_SIZE, 0);
	m_dsp = roland_xp_dsp::regs{};
}

//-------------------------------------------------
//  rom_bank_pre_change
//-------------------------------------------------

void roland_xp_device::rom_bank_pre_change()
{
	m_stream->update();
}


//-------------------------------------------------
//  read - register read
//-------------------------------------------------

u8 roland_xp_device::read(offs_t offset)
{
	// printf("XP: read %04x\n", offset);

	if (offset < 0x2c00)
	{
		const unsigned reg_idx = offset >> 2;
		const unsigned byte_idx = offset & 0x03;
		const unsigned shift = (3 - byte_idx) * 8;
		if (reg_idx < REG_ARRAY_SIZE)
			return (m_reg[reg_idx] >> shift) & 0xff;
		return 0;
	}

	if (offset >= 0x2c00 && offset < 0x3900)
	{
		// DSP memory reads are trigger accesses.  The host discards the value
		// returned by this address and reads it back through 0x3912:0x3910.
		// CRAM is 16-bit; IRAM and PRAM are 32-bit.  In particular,
		// xp_dsp_write_eram_offset performs a PRAM trigger read before a
		// read/modify/write of an ERAM-address instruction pair.
		if (offset >= 0x3000 && offset < 0x3300)
			m_stream->update();
		if (offset < 0x3000)
		{
			const offs_t aligned = (offset - 0x2c00) & ~offs_t(1);
			m_dsp_read_latch[0] = m_dsp_program[aligned + 0]; // 0x3910
			m_dsp_read_latch[1] = m_dsp_program[aligned + 1]; // 0x3911
			m_dsp_read_latch_mask = 0x03;
		}
		else
		{
			const offs_t aligned = (offset - 0x2c00) & ~offs_t(3);
			if (aligned == 0x320c - 0x2c00 && dsp_read_u32(0x320c) == 1)
			{
				// Writing exactly one enables IRAM3 slot 3 as the hardware-random
				// trigger.  Other values remain ordinary RAM; the factory test
				// overwrites this slot with walking patterns and reads them back.
				// A 32-bit
				// SH read invokes this byte handler four times, so advance only on
				// the first byte and preserve the latch for the remaining bytes.
				if (offset == 0x320c)
				{
					const uint16_t random = next_random();
					m_dsp_read_latch[0] = 0;
					m_dsp_read_latch[1] = 0;
					m_dsp_read_latch[2] = random >> 8;
					m_dsp_read_latch[3] = random;
					m_dsp_read_latch_mask = 0x0f;
				}
			}
			else
			{
				m_dsp_read_latch[0] = m_dsp_program[aligned + 2]; // low half at 0x3910
				m_dsp_read_latch[1] = m_dsp_program[aligned + 3];
				m_dsp_read_latch[2] = m_dsp_program[aligned + 0]; // high half at 0x3912
				m_dsp_read_latch[3] = m_dsp_program[aligned + 1];
				m_dsp_read_latch_mask = 0x0f;
			}
		}
		return m_dsp_program[offset - 0x2c00];
	}

	if (offset >= 0x3900 && offset < 0x3a00)
	{
		const unsigned reg = offset - 0x3900;
		if (reg >= 0x10 && reg <= 0x13 && BIT(m_dsp_read_latch_mask, reg - 0x10))
		{
			const unsigned index = reg - 0x10;
			m_dsp_read_latch_mask &= ~(1U << index);
			return m_dsp_read_latch[index];
		}
		if (reg == 0x12)
			return 0;
		if (reg == 0x13)
			return read_command_status();
		if (reg == 0x18 || reg == 0x19)
		{
			const uint16_t status = current_irq_status();
			return status >> ((0x19 - reg) * 8);
		}
		if (reg == 0x1a || reg == 0x1b)
		{
			const uint16_t data = current_irq_data();
			const uint8_t result = data >> ((0x1b - reg) * 8);
			if (reg == 0x1b)
				acknowledge_irq();
			return result;
		}
		return m_global_config[reg];
	}

	if (offset >= 0x3a00 && offset < 0x3c00)
	{
		const unsigned rel = offset - 0x3a00;
		const unsigned send = rel / 0x80;
		const unsigned voice = (rel % 0x80) >> 1;
		const unsigned byte_idx = rel & 0x01;
		if (send < NUM_MIXER_SENDS && voice < NUM_VOICES)
			return (m_voices[voice].mixer_send[send] >> ((1 - byte_idx) * 8)) & 0xff;
		return 0;
	}

	if (offset >= 0x3c00 && offset < 0x4000)
	{
		// A read through this 1 KiB window performs a host-side wave-ROM
		// access.  The firmware discards the window value and reads the byte
		// from the low half of the 16-bit latch at 0x3910.  Registers 0x3922
		// and 0x3920 supply address bits 26:20 and 19:10 respectively.
		const uint16_t bank = (uint16_t(m_global_config[0x22]) << 8) |
			m_global_config[0x23];
		const uint16_t page = (uint16_t(m_global_config[0x20]) << 8) |
			m_global_config[0x21];
		const uint32_t address = (uint32_t(bank & 0x007f) << 20) |
			(uint32_t(page & 0x03ff) << 10) | (offset & 0x03ff);
		m_dsp_read_latch_mask = 0;
		m_global_config[0x10] = 0;
		m_global_config[0x11] = read_byte(address);
		return 0;
	}

	return 0;
}

//-------------------------------------------------
//  dbg_peek - side-effect-free live-state read
//-------------------------------------------------

u8 roland_xp_device::dbg_peek(offs_t offset) const
{
	if (offset < 0x2c00)
	{
		// voice / PCM engine pages (banks of 32-bit words)
		const unsigned reg_idx = offset >> 2;
		const unsigned shift = (3 - (offset & 0x03)) * 8;
		if (reg_idx < REG_ARRAY_SIZE)
			return (m_reg[reg_idx] >> shift) & 0xff;
		return 0;
	}
	if (offset < 0x3900)
		// DSP program/config area, incl. ramp-evolved IRAM3 current (0x3200-0x323f)
		return m_dsp_program[offset - 0x2c00];
	if (offset < 0x3a00)
		// global config (incl. write-only-on-silicon regs stored here)
		return m_global_config[offset - 0x3900];
	if (offset < 0x3c00)
	{
		// mixer sends: 4 banks x 64 voices x 16-bit; big-endian byte order like read()
		const unsigned rel = offset - 0x3a00;
		const unsigned send = rel / 0x80;
		const unsigned voice = (rel % 0x80) >> 1;
		const unsigned byte_idx = rel & 0x01;
		if (send < NUM_MIXER_SENDS && voice < NUM_VOICES)
			return (m_voices[voice].mixer_send[send] >> ((1 - byte_idx) * 8)) & 0xff;
	}
	return 0;
}

//-------------------------------------------------
//  write - register write
//-------------------------------------------------

void roland_xp_device::write(offs_t offset, u8 data)
{
	m_stream->update();

	if (offset < 0x2c00)
	{
		const unsigned bank = offset & 0xff00;
		const unsigned voice_idx = (offset & 0xff) >> 2;
		const unsigned byte_idx = offset & 0x03;
		const unsigned shift = (3 - byte_idx) * 8;
		const uint32_t mask = ~(uint32_t(0xff) << shift);
		const bool word_complete = byte_idx == 3;

		if (voice_idx >= NUM_VOICES)
			return;

		const unsigned reg_idx = offset >> 2;
		m_reg[reg_idx] = (m_reg[reg_idx] & mask) | (uint32_t(data) << shift);
		const uint32_t reg_val = m_reg[reg_idx];

			pcm_voice &v = m_voices[voice_idx];
			switch (bank)
			{
			case 0x0000:
				v.wave_ctrl = reg_val;
				// Any write to the voice-control word restarts the PCM reader.  The
				// firmware does partial-word writes when reusing a voice, so waiting
				// for byte 3 leaves the previous address and DPCM accumulator live.
				v.current_addr = v.sample_start & 0xfffff;
				v.dpcm_val = 0;
				v.subphase = 0;
				v.alt_loop_dir = false;
				v.tvf_bp = 0;
				v.tvf_lp = 0;
				// xp_start_voice sets bit 15 when submitting the completed command.
				// Initialization also clears this bank, so a completed zero write must
				// not accidentally activate all 64 voices.
				if (word_complete && BIT(reg_val, 15))
				{
					// The firmware retries the same command while the busy nibble is
					// nonzero.  Retrying an already accepted command must not restart
					// the busy interval or the polling loop can never complete.
					if (!v.playing)
						m_command_busy_reads = 1;
					v.playing = true;
					v.loop_irq_pending = false;
				}
				break;
			case 0x0100:
				v.sample_start = reg_val;
				// Firmware may program the start address after wave_ctrl, so keep
				// the live decoder address synchronized while the word is written.
				v.current_addr = reg_val & 0xfffff;
				break;
			case 0x0200: v.sample_loop = reg_val; break;
			case 0x0300: v.sample_end = reg_val; break;
			case 0x0400: break;
			case 0x0500: break;
			case 0x0600: break;
			case 0x0700: break;
			case 0x0800: break;
			case 0x0900: break;
			case 0x0a00: break;
			case 0x0b00: break;
			case 0x0c00: break; // Voice state A
			case 0x0d00: break;
			case 0x0e00: break; // Voice state B
			case 0x0f00: break;
			
			case 0x1000: break; // Voice init constant (set to 0x08 on start)
			case 0x1100: v.tvf_q_destination_val = reg_val; if (word_complete) retarget_tvf_q_interp(v); break;
			case 0x1200: v.pitch_destination_val = reg_val; if (word_complete) retarget_pitch_interp(v); break;
			case 0x1300: v.tvf_f_destination_val = reg_val; if (word_complete) retarget_tvf_f_interp(v); break;
			case 0x1400: v.ampmod_destination_val = reg_val; if (word_complete) retarget_ampmod_interp(v); break;
			case 0x1500: v.amp_destination_val = reg_val; if (word_complete) retarget_amp_interp(v); break;
			case 0x1600: v.tvf_q_interp_ctrl = reg_val; if (word_complete) retarget_tvf_q_interp(v); break;
			case 0x1700: v.pitch_interp_ctrl = reg_val; if (word_complete) retarget_pitch_interp(v); break;
			case 0x1800: v.tvf_f_interp_ctrl = reg_val; if (word_complete) retarget_tvf_f_interp(v); break;
			case 0x1900: v.ampmod_interp_ctrl = reg_val; if (word_complete) retarget_ampmod_interp(v); break;
			case 0x1a00: v.amp_interp_ctrl = reg_val; if (word_complete) retarget_amp_interp(v); break;
			case 0x1b00: v.pitch_start_val = reg_val; if (word_complete) reload_pitch_interp(v); break;
			case 0x1c00: v.tvf_f_start_val = reg_val; if (word_complete) reload_tvf_f_interp(v); break;
			case 0x1d00: v.ampmod_start_val = reg_val; if (word_complete) reload_ampmod_interp(v); break;
			case 0x1e00: v.amp_start_val = reg_val; if (word_complete) reload_amp_interp(v); break;
			case 0x1f00: break;
			
			case 0x2000: v.filter_type_select = reg_val; break;
			case 0x2100: v.tvf_q_start_val = reg_val; if (word_complete) reload_tvf_q_interp(v); break;
			case 0x2200: break;
			case 0x2300: break; // Amp modulation level
			case 0x2400: break;
			case 0x2500: break;
			case 0x2600: break;
			case 0x2700: break; // Amp modulation base
			case 0x2800: break; // Voice state C
			case 0x2900: break; // Voice state D
			case 0x2a00: break;
			case 0x2b00: break;
		}
		return;
	}

	if (offset >= 0x2c00 && offset < 0x3900)
	{
		m_dsp_program[offset - 0x2c00] = data;
		if (offset >= 0x3200 && offset < 0x3300)
		{
			// A direct IRAM3 write seeds the current value and stops any previous
			// ramp.  The target bank is what explicitly starts interpolation.
			m_iram3_breakpoint_active[(offset - 0x3200) >> 2] = false;
		}
		else if (offset >= 0x3300 && offset < 0x3380)
		{
			m_iram3_breakpoint_active[(offset - 0x3300) >> 1] = true;
		}
		return;
	}

	if (offset >= 0x3900 && offset < 0x3a00)
	{
		const unsigned global_offset = offset - 0x3900;
		m_global_config[global_offset] = data;
		if (global_offset < 8)
		{
			// Four big-endian 16-bit reset bitmaps cover the 64 voices.  Cleared
			// bits reset the corresponding runtime state and are acknowledged by
			// returning to one.  Without the acknowledgement, later read/modify/
			// write operations accumulate stale zeroes and reset unrelated voices.
			const unsigned word = global_offset >> 1;
			const unsigned byte_base = (global_offset & 1) ? 0 : 8;
			for (unsigned bit = 0; bit < 8; bit++)
			{
				if (!BIT(data, bit))
					reset_voice_runtime(m_voices[word * 16 + byte_base + bit]);
			}
			m_global_config[global_offset] = 0xff;
		}
		else if (global_offset == 0x18 || global_offset == 0x19)
		{
			m_irq_control = (uint16_t(m_global_config[0x18]) << 8) | m_global_config[0x19];
		}
		// printf("XP: config write %04x = %02x\n", offset, data);
		return;
	}

	if (offset >= 0x3a00 && offset < 0x3c00)
	{
		const unsigned rel = offset - 0x3a00;
		const unsigned send = rel / 0x80;
		const unsigned voice = (rel % 0x80) >> 1;
		const unsigned byte_idx = rel & 0x01;
		if (send < NUM_MIXER_SENDS && voice < NUM_VOICES)
		{
			uint16_t &val = m_voices[voice].mixer_send[send];
			if (byte_idx)
				val = (val & 0xff00) | data;
			else
				val = (val & 0x00ff) | (uint16_t(data) << 8);
		}
		return;
	}
}

static inline int64_t mul_q27_q14(int32_t a_q27, int16_t b_q14)
{
	return (int64_t(a_q27) * b_q14) >> 14;
}

static inline int32_t sat_q27(int64_t v)
{
	if (v > 0x07ffffff)
		return 0x07ffffff;
	if (v < -0x08000000)
		return -0x08000000;
	return (int32_t)v;
}

static inline int32_t add_wrap_s32(int32_t a, int32_t b)
{
	return int32_t(uint32_t(a) + uint32_t(b));
}

int32_t run_svf_sample(int32_t in_q27, int16_t f_q14, int16_t q_q14,
                    int32_t &bp_q27, int32_t &lp_q27, int type)
{
	lp_q27 = sat_q27(int64_t(lp_q27) + mul_q27_q14(bp_q27, f_q14));
	int32_t hp_q27 = sat_q27(int64_t(in_q27) - mul_q27_q14(bp_q27, q_q14) - lp_q27);
	bp_q27 = sat_q27(int64_t(bp_q27) + mul_q27_q14(hp_q27, f_q14));

	switch (type) {
		case 0: return lp_q27;
		case 1: return hp_q27;
		case 2: return bp_q27;
		case 3: return sat_q27((int64_t)lp_q27 - hp_q27);
		default: return 0;
	}
}

int32_t roland_xp_device::decode_sample(uint32_t sample_addr, uint32_t wave_ctrl)
{
	// The JV-1080 exposes a four-bit wave-ROM bank here.  SCCore accepts a
	// seven-bit bank, but bits 4-6 in this device's control word are not address
	// bits; folding them into the address breaks reused voices on retrigger.
	const uint32_t rom_high = (wave_ctrl & 0x0f) << 20;
	uint32_t exp_both = read_byte((sample_addr >> 5) | rom_high);
	const int32_t mantissa = int8_t(read_byte(sample_addr | rom_high));
	const uint32_t exp = BIT(sample_addr, 4) ? BIT(exp_both, 4, 4) : BIT(exp_both, 0, 4);
	// Keep the JV-1080 fixed-point scale used by the original implementation.
	// SCCore shifts by exponent + 10 and then converts the accumulator by 2^-27;
	// applying that shift directly here over-ranges this integer TVF path.
	const uint32_t shift = (10 - exp) & 0x0f;
	return int32_t(uint32_t(mantissa) << 11) >> shift;
}

int32_t roland_xp_device::do_voice(pcm_voice &v, bool control_tick_2, bool control_tick_8, bool &voice_event)
{
	voice_event = false;
	if (control_tick_2)
		interp_update_a(v.amp_interp);
	if (control_tick_8)
	{
		interp_update_am(v.ampmod_interp);
		interp_update_f(v.tvf_f_interp);
		interp_update_q(v.tvf_q_interp);
	}

	auto advance_sample_address = [&v](uint32_t &address, bool &alt_loop_dir, bool &playing)
	{
		if (!playing)
			return false;

		const bool reverse = BIT(v.wave_ctrl, 11);
		const uint32_t loop_start = v.sample_loop & 0xfffff;
		const uint32_t loop_end = v.sample_end & 0xfffff;
		// SCCore's XP bridge clears the loop-mode bits when loop and end are
		// equal.  This is how the four host loop modes select the otherwise
		// hidden one-shot address-generator paths.
		const bool one_shot = loop_start == loop_end;
		const bool alt_loop = !one_shot && BIT(v.wave_ctrl, 12);
		const uint32_t compare = alt_loop_dir ? loop_start : loop_end;
		const bool at_boundary = ((compare ^ address) & 0xfffff) == 0;

		if (one_shot && at_boundary)
		{
			playing = false;
			return true;
		}

		if (!alt_loop && at_boundary)
			address = loop_start;

		const int do_add = (!at_boundary && alt_loop && !alt_loop_dir) || (!at_boundary && !alt_loop);
		const int do_sub = !at_boundary && alt_loop && alt_loop_dir;

		if (reverse)
			address -= do_add - do_sub;
		else
			address += do_add - do_sub;

		address &= 0xfffff;
		alt_loop_dir = alt_loop && (alt_loop_dir ^ at_boundary);
		// The physical voice event is produced whenever the address generator
		// enters the loop region.  The one-shot path above reports its terminal
		// boundary instead.  Firmware receives this as IRQ reason 5.
		return address == loop_start;
	};

	// increment phase
	uint32_t old_subphase = v.subphase;
	uint32_t subphase_full = old_subphase + v.pitch_interp.output_i;
	uint32_t subphase_overflow = subphase_full >> 16;
	int interp_ratio = (old_subphase >> 9) & 127;
	v.subphase = subphase_full & 0xffff;

	// dpcm
	int32_t reference = v.dpcm_val;
	int32_t temp_samples[4] = { 0 };
	uint32_t address = v.current_addr & 0xfffff;
	bool alt_loop_dir = v.alt_loop_dir;
	bool playing = v.playing;
	for (uint32_t i = 0; i < 4; i++)
	{
		temp_samples[i] = decode_sample(address, v.wave_ctrl);

		if (i < subphase_overflow)
			reference = add_wrap_s32(reference, temp_samples[i]);

		const bool event = advance_sample_address(address, alt_loop_dir, playing);
		if (i < subphase_overflow)
			voice_event = voice_event || event;

		if (i + 1 == subphase_overflow)
		{
			v.current_addr = address;
			v.alt_loop_dir = alt_loop_dir;
			v.playing = playing;
		}
	}

	// only for when the subphase is too big, technically not necessary
	for (uint32_t i = 4; i < subphase_overflow; i++)
	{
		reference = add_wrap_s32(reference, decode_sample(address, v.wave_ctrl));
		voice_event = advance_sample_address(address, alt_loop_dir, playing) || voice_event;
		v.current_addr = address;
		v.alt_loop_dir = alt_loop_dir;
		v.playing = playing;
	}

	// interpolation
	int64_t interp_sum = v.dpcm_val; // s[n-1]
	interp_sum += (int64_t(temp_samples[0]) * interp_lut[0][interp_ratio]) >> 12;
	interp_sum += (int64_t(temp_samples[1]) * interp_lut[1][interp_ratio]) >> 12;
	interp_sum += (int64_t(temp_samples[2]) * interp_lut[2][interp_ratio]) >> 12;
	v.dpcm_val = reference;

	const int16_t q_q14 = interp_q14_output(v.tvf_q_interp);
	const int16_t f_q14 = limit_tvf_frequency(interp_q14_output(v.tvf_f_interp), v.tvf_q_interp);
	interp_sum = run_svf_sample(sat_q27(interp_sum), f_q14, q_q14,
		v.tvf_bp, v.tvf_lp, decode_filter_type(v));

	// The ordering is intentionally TVF -> TVA.  Some XP products appear to
	// configure this path differently; do not move TVA ahead of TVF yet.
	interp_sum = (int64_t)interp_sum * interp_q14_output(v.amp_interp) >> 14;
	interp_sum = (int64_t)interp_sum * interp_q14_output(v.ampmod_interp) >> 14;

	return sat_q27(interp_sum);
}

//-------------------------------------------------
//  Effect-DSP interpreter
//
//  The XP runs a fixed 288-slot microprogram (PRAM at 0x3400, CRAM at 0x2C00)
//  every sample.  IRAM1/IRAM2 form one double-buffered 64-word working buffer
//  (host 0x3000/0x3100, kept coherent by dual stores); IRAM3 is a single-buffered
//  64-word aux bank (0x3200).  Column ALU semantics live in roland_xp_dsp.h.
//-------------------------------------------------

int32_t roland_xp_device::dsp_iram_read(unsigned word) const
{
	// word[7:6] selects the bank; word[5:0] the index.  Banks 00/01/10 all read the
	// working buffer (we read IRAM1, which the dual store keeps equal to IRAM2);
	// bank 11 reads IRAM3.
	const unsigned idx = word & 0x3f;
	const offs_t base = (word < 0xc0) ? 0x3000 : 0x3200;
	const uint32_t raw = dsp_read_u32(base + idx * 4) & 0xffffff;
	return (raw & 0x800000) ? int32_t(raw) - 0x1000000 : int32_t(raw);
}

void roland_xp_device::dsp_iram_store(unsigned word, int32_t value)
{
	const unsigned idx = word & 0x3f;
	const uint32_t u = uint32_t(value) & 0xffffff;
	if (word < 0xc0)
	{
		// Dual-write both ping-pong halves so the logical working buffer stays coherent.
		dsp_write_u32(0x3000 + idx * 4, u);
		dsp_write_u32(0x3100 + idx * 4, u);
	}
	else
	{
		dsp_write_u32(0x3200 + idx * 4, u); // IRAM3 (single buffer)
	}
}

void roland_xp_device::run_dsp_program(int64_t &dac_l, int64_t &dac_r, bool &dac_valid)
{
	using namespace roland_xp_dsp;

	regs r = m_dsp;
	for (unsigned slot = 0; slot < NUM_DSP_SLOTS; slot++)
	{
		const uint32_t pram = dsp_read_u32(0x3400 + slot * 4) & 0x0fffffff; // PRAM is 28-bit
		const uint16_t cram = dsp_read_u16(0x2c00 + slot * 2);
		const unsigned st = (pram >> 14) & 3;
		const unsigned word = (pram >> 6) & 0xff;
		const unsigned col = pram & 0x3f;

		// st: 0 = acc-only, 1 = IRAM read, 2 = ERAM receive (stubbed to 0 in phase 1
		// so chorus/reverb tails are silent), 3 = pure store then acc-only op.
		int32_t mem = 0;
		bool has_mem = false;
		if (st == 1)
		{
			mem = dsp_iram_read(word);
			has_mem = true;
		}
		else if (st == 2)
		{
			has_mem = true; // ERAM read returns 0 until the delay memory is modelled
		}

		if (st == 3)
		{
			// Store the pre-op accumulator first, then run the column op acc-only.
			const int32_t sv = sat24(r.acc);
			dsp_iram_store(word, sv);
			if (word == 0x55) { dac_l = sv; dac_valid = true; }       // EFX out L
			else if (word == 0x56) { dac_r = sv; dac_valid = true; }  // EFX out R
		}

		apply_column(r, col, cram, mem, has_mem);
	}
	m_dsp = r;
}

void roland_xp_device::sound_stream_update(sound_stream &stream)
{
	for (int smpl = 0; smpl < stream.samples(); smpl++)
	{
		std::array<int64_t, 64> buses{};
		int64_t dry_left = 0;
		int64_t dry_right = 0;
		const bool control_tick_2 = (m_control_phase & 1) == 0;
		const bool control_tick_8 = m_control_phase == 0;
		if (control_tick_2)
			update_iram3_breakpoints();

		for (unsigned v_idx = 0; v_idx < NUM_VOICES; v_idx++)
		{
			pcm_voice &v = m_voices[v_idx];
			if (!v.playing)
				continue;

			// Pitch is the SCCore voice-engine active criterion and continues to
			// run while the remaining interpolators are gated.
			if (control_tick_8)
				interp_update_pitch(v.pitch_interp);
			if (v.pitch_interp.output_i == 0)
				continue;
			
			bool voice_event;
			const int32_t voice = do_voice(v, control_tick_2, control_tick_8, voice_event);
			if (voice_event && !v.loop_irq_pending)
			{
				v.loop_irq_pending = true;
				raise_irq(5, v_idx);
			}
			for (unsigned send = 0; send < NUM_MIXER_SENDS; send++)
			{
				const uint16_t send_word = v.mixer_send[send];
				const unsigned level = send_word >> 6;
				const unsigned bus = send_word & 0x3f;
				const int64_t contribution = (int64_t(voice) * level) >> 10;
				buses[bus] += contribution;
				if (send == 0)
					dry_left += contribution;
				else if (send == 1)
					dry_right += contribution;
			}
		}

		// Present the voice-send buses to the effect DSP as its working-buffer input
		// words (bus b -> word 0x40+b -> IRAM index b).  Shifting the Q27 voice sum
		// right by 4 aligns the DSP's 24-bit store saturation with sat_q27, so a
		// unity effect reproduces the legacy dry level.  Only the low 16 buses feed
		// the input region (0x40-0x4F); higher indices are DSP scratch/state.
		static constexpr int BUS_SHIFT = 4;
		for (unsigned b = 0; b < 16; b++)
			dsp_iram_store(0x40 + b, roland_xp_dsp::sat24(buses[b] >> BUS_SHIFT));

		int64_t dac_l = 0, dac_r = 0;
		bool dac_valid = false;
		run_dsp_program(dac_l, dac_r, dac_valid);

		if (dac_valid)
		{
			// The effect program drives EFX-out L/R (stores to words 0x55/0x56).
			stream.add_int(0, smpl, int32_t(dac_l), 1 << 17);
			stream.add_int(1, smpl, int32_t(dac_r), 1 << 17);
		}
		else
		{
			// No effect program loaded yet: fall back to the direct dry mix so audio
			// keeps working through boot and before the DSP is programmed.
			stream.add_int(0, smpl, sat_q27(dry_left), 1 << 21);
			stream.add_int(1, smpl, sat_q27(dry_right), 1 << 21);
		}
		m_control_phase = (m_control_phase + 1) & 7;
	}
}
