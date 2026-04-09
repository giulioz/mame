// license:BSD-3-Clause
// copyright-holders:giulioz

#include "emu.h"
#include "roland_xp.h"

#include <cmath>
#include <algorithm>


DEFINE_DEVICE_TYPE(ROLAND_XP, roland_xp_device, "roland_xp", "Roland XP PCM+DSP")

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

// Pitch/cutoff exponential LUT: 257 entries, implements 2^(index/256)
// Entry 0 = 131072 (2^17), entry 256 = 262144 (2^18)
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

//-------------------------------------------------
//  device_start
//-------------------------------------------------

void roland_xp_device::device_start()
{
	m_rate = clock() / 768; // 24.576 MHz / 768 = 32 kHz

	m_stream = stream_alloc(0, 2, m_rate);

	m_dram = std::make_unique<uint8_t[]>(DRAM_SIZE);

	// save state
	for (int i = 0; i < NUM_VOICES; i++)
	{
		save_item(NAME(m_voices[i].wave_ctrl), i);
		save_item(NAME(m_voices[i].sample_start), i);
		save_item(NAME(m_voices[i].sample_loop), i);
		save_item(NAME(m_voices[i].sample_end), i);
		save_item(NAME(m_voices[i].mixer_send), i);
		save_item(NAME(m_voices[i].pitch_current_val), i);
		save_item(NAME(m_voices[i].pitch_target_val), i);
		save_item(NAME(m_voices[i].pitch_interp_ctrl), i);
		save_item(NAME(m_voices[i].dpcm_val), i);
		save_item(NAME(m_voices[i].subphase), i);
	}
	save_item(NAME(m_reg));
	save_item(NAME(m_dsp_program));
	save_item(NAME(m_global_config));
	save_pointer(NAME(m_dram), DRAM_SIZE);

	logerror("Roland XP: Clock %u, Rate %u\n", clock(), m_rate);
}

//-------------------------------------------------
//  device_reset
//-------------------------------------------------

void roland_xp_device::device_reset()
{
	m_int_callback(CLEAR_LINE);

	for (auto &v : m_voices)
	{
		v.wave_ctrl = 0;
		v.sample_start = 0;
		v.sample_loop = 0;
		v.sample_end = 0;
		v.pitch_current_val = 0;
		v.pitch_target_val = 0;
		v.pitch_interp_ctrl = 0;
		v.dpcm_val = 0;
		v.subphase = 0;
		for (auto &s : v.mixer_send)
			s = 0;
	}

	std::fill_n(m_reg, REG_ARRAY_SIZE, 0);
	std::fill_n(m_dsp_program, DSP_PROGRAM_SIZE, 0);
	std::fill_n(m_global_config, GLOBAL_CONFIG_SIZE, 0);
	std::fill_n(m_dram.get(), DRAM_SIZE, 0);
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
		return m_dsp_program[offset - 0x2c00];
	}

	if (offset >= 0x3900 && offset < 0x3a00)
	{
		return m_global_config[offset - 0x3900];
	}

	if (offset >= 0x3a00 && offset < 0x3c80)
	{
		const unsigned rel = offset - 0x3a00;
		const unsigned send = rel / 0x80;
		const unsigned voice = (rel % 0x80) >> 1;
		const unsigned byte_idx = rel & 0x01;
		if (send < NUM_MIXER_SENDS && voice < NUM_VOICES)
			return (m_voices[voice].mixer_send[send] >> ((1 - byte_idx) * 8)) & 0xff;
		return 0;
	}

	return 0;
}

//-------------------------------------------------
//  write - register write
//-------------------------------------------------

void roland_xp_device::write(offs_t offset, u8 data)
{
	if (offset < 0x2c00)
	{
		m_stream->update();

		const unsigned bank = offset & 0xff00;
		const unsigned voice_idx = (offset & 0xff) >> 2;
		const unsigned byte_idx = offset & 0x03;
		const unsigned shift = (3 - byte_idx) * 8;
		const uint32_t mask = ~(uint32_t(0xff) << shift);

		if (voice_idx >= NUM_VOICES)
			return;

		const unsigned reg_idx = offset >> 2;
		m_reg[reg_idx] = (m_reg[reg_idx] & mask) | (uint32_t(data) << shift);
		const uint32_t reg_val = m_reg[reg_idx];

		pcm_voice &v = m_voices[voice_idx];
		switch (bank)
		{
			case 0x0000: v.wave_ctrl = reg_val; break;
			case 0x0100: v.sample_start = reg_val; break;
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
			case 0x1100: v.tvf_q_current_val = reg_val; break;
			case 0x1200: v.pitch_current_val = reg_val; break;
			case 0x1300: v.tvf_f_current_val = reg_val; break;
			case 0x1400: v.ampmod_current_val = reg_val; break;
			case 0x1500: v.amp_current_val = reg_val; break;
			case 0x1600: v.tvf_q_interp_ctrl = reg_val; break;
			case 0x1700: v.pitch_interp_ctrl = reg_val; break;
			case 0x1800: v.tvf_f_interp_ctrl = reg_val; break;
			case 0x1900: v.ampmod_interp_ctrl = reg_val; break;
			case 0x1a00: v.amp_interp_ctrl = reg_val; break;
			case 0x1b00: v.pitch_target_val = reg_val; break;
			case 0x1c00: v.tvf_f_target_val = reg_val; break;
			case 0x1d00: v.ampmod_target_val = reg_val; break;
			case 0x1e00: v.amp_target_val = reg_val; break;
			case 0x1f00: break;
			
			case 0x2000: v.filter_type_select = reg_val; break;
			case 0x2100: v.tvf_q_target_val = reg_val; break;
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
		return;
	}

	if (offset >= 0x3900 && offset < 0x3a00)
	{
		m_global_config[offset - 0x3900] = data;
		printf("XP: config write %04x = %02x\n", offset, data);
		return;
	}

	if (offset >= 0x3a00 && offset < 0x3c80)
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

int32_t roland_xp_device::decode_sample(uint32_t sample_addr, uint32_t wave_ctrl)
{
	uint32_t rom_high = (wave_ctrl & 0b1111) << 20;
	uint32_t exp_both = read_byte((sample_addr >> 5) | rom_high);
	int32_t mantissa = int8_t(read_byte(sample_addr | rom_high));
	uint32_t exp = ((sample_addr >> 4) & 1) == 1 ? ((exp_both >> 4) & 0x0f) : (exp_both & 0x0f);
	const uint32_t shift = (10 - exp) & 0x0f;
	return (mantissa << 11) >> shift;
}

int32_t roland_xp_device::do_voice(pcm_voice &v)
{
	// increment phase
	uint32_t old_subphase = v.subphase;
	uint32_t subphase_full = old_subphase + pitch_to_increment(v.pitch_current_val);
	uint32_t subphase_overflow = subphase_full >> 16;
	int interp_ratio = (old_subphase >> 9) & 127;
	v.subphase = subphase_full & 0xffff;

	// dpcm
	int32_t reference = v.dpcm_val;
	int32_t temp_samples[4] = { 0 };
	uint32_t start_addr = v.sample_start;
	for (uint32_t i = 0; i < 4; i++)
	{
		temp_samples[i] = decode_sample(start_addr + i, v.wave_ctrl);

		if (i < subphase_overflow)
		{
			reference += temp_samples[i];
			v.sample_start++;

			if (v.sample_start == v.sample_end)
				v.sample_start = v.sample_loop;
		}
	}

	// interpolation
	int64_t interp_sum = v.dpcm_val; // s[n-1]
	interp_sum += (temp_samples[0] * interp_lut[0][interp_ratio]) >> 12;
	interp_sum += (temp_samples[1] * interp_lut[1][interp_ratio]) >> 12;
	interp_sum += (temp_samples[2] * interp_lut[2][interp_ratio]) >> 12;
	v.dpcm_val = reference;

	// int32_t tvf_f = v.tvf_f_current_val;
	// int32_t tvf_q = v.tvf_q_current_val >> 2;

    // v.tvf_lp = (int64_t)(v.tvf_bp * tvf_f) + v.tvf_lp;
    // int32_t hp = (int64_t)interp_sum - (v.tvf_bp * tvf_q) - v.tvf_lp;
    // v.tvf_bp = (int64_t)(hp * tvf_f) + v.tvf_bp;

    // switch ((v.filter_type_select >> 10) & 0x03) {
    // case 0: interp_sum = v.tvf_lp; break;
    // case 1: interp_sum = hp; break;
    // case 2: interp_sum = v.tvf_bp; break;
    // case 3: interp_sum = (int64_t)v.tvf_lp - hp; break;
    // default: interp_sum = v.tvf_lp; break;
    // }

	interp_sum *= v.amp_current_val;
	interp_sum >>= 16;

	return interp_sum;
}

void roland_xp_device::sound_stream_update(sound_stream &stream)
{
	for (int smpl = 0; smpl < stream.samples(); smpl++)
	{
		int64_t mix = 0;

		for (unsigned v_idx = 0; v_idx < NUM_VOICES; v_idx++)
		{
			pcm_voice &v = m_voices[v_idx];

			// skip inactive voices for now
			if (v.sample_start == v.sample_end || v.amp_current_val == 0)
				continue;
			
			mix += do_voice(v);
		}

		stream.add_int(0, smpl, mix, 1<<18);
		stream.add_int(1, smpl, mix, 1<<18);
	}
}
