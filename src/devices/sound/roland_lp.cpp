// license:BSD-3-Clause
// copyright-holders:Valley Bell, giulioz

/***************************************************************************

    Roland LP PCM engine:
    - Fujitsu MB87419 (LP-1, address/envelope generator)
    - Fujitsu MB87420 (LP-2, DPCM decoder, interpolator, volume multiplier)
    Driver derived from silicon reverse engineering (thanks InfoSecDJ).

    LP-1 maintains two 136 x 16-bit SRAMs, of which 128 words in each bank
    hold four words per voice. LP-2 has 32 x 12-bit DPCM predictors and a
    384 x 12-bit interpolation ROM, three coefficients for each of 128 phases.

    Each voice's eight little-endian parameter words:
      0: current volume bits 15:0.
      1: bits 9:0 = current volume bits 25:16; bits 13:10 = ROM bank;
         bit 14 = initial reverse direction, bit 15 = alternate/reflected loop.
      2: playback step, unsigned 2.14 fixed point (4000 = one byte/frame).
      3: low byte = envelope rate, high byte = envelope target.
      4/5: current 18.14 sample address (updates during playback).
      6: end address, in units of four bytes.
      7: loop anchor address, in units of four bytes.

    Host writes:
      00..0f: shared staging bytes. Writing an even address commits the whole
      corresponding parameter word to the selected voice. Firmware writes
      the high byte first; writing a high byte alone does not change SRAM.
      11/13/15/17: eight voice-enable bits each. A rising enable clears that
      voice's predictor, starts the outward leg and arms its envelope.
      10/12/.../1e: capture a selected voice's parameter word for readback.
      1f: voice select, low five bits.
    Reads mirror modulo four:
      0: completed-envelope voice number, acknowledging one queued IRQ.
      1: wave ROM data from the staged bank/address, with the firmware's
         two-byte lookahead: ((staged_address >> 6) + 2) & 3ffff.
      2/3: captured parameter word low/high byte.

    A decoded sample byte is a signed mantissa/exponent delta. Playback
    fetches up to five successive deltas per frame, wrapping the signed
    12-bit predictor once per crossed byte. Interpolation adds three weighted
    deltas to the old predictor, then applies the updated envelope volume.
    The interpolation table is a decoded mask-ROM transcription, not a
    substitute linear interpolation formula.

    The loop anchor is consumed on the initial pass; subsequent passes start
    at anchor + initial direction. Reflected playback reverses at the end
    and at that following byte, preserving the predictor across both legs.
    All loop modes use the same decoder and predictor; there is no synthetic
    predictor reset or separate decoder for short loops.

    Envelope direction follows current volume versus target. The level/rate
    curves are generated with shifts and mantissas. Rate 00 holds the
    accumulator without completion; rate ff loads the target immediately
    without IRQ; an ordinary segment queues an IRQ on completion,
    including an armed zero-distance segment. Each committed envelope word
    arms a new segment, including repeated rate/target words (MV-30 release
    stages); an odd/high-byte staging write alone does not arm it. Reading IRQ
    status promotes one waiting voice, pulsing the line low/high if another
    IRQ remains.

    One synchronous stream tick represents clock()/1024 input clocks (32 kHz
    at 32.768 MHz). Synchronous updates deliver envelope IRQs at sample times,
    and host accesses update the stream before changing parameter state.
    This is a sample/register model, not a simulation of LP-1/LP-2 bus cycles.
    SRAM arbitration, pad timing, internal chorus/tremolo voice handling and
    the output serial format remain unmodeled. Outputs 0..31 expose individual
    voice values, scaled by 1/131072 without clipping to preserve the board's
    bus taps. D-70 and MV-30 route these through TVF/TVA and then RCC, which
    performs the programmed mixing, panning, chorus and reverb. Board drivers
    select the RCC output slots; serializers and analog DAC stages are omitted.

***************************************************************************/

#include "emu.h"
#include "roland_lp.h"

#include <algorithm>

DEFINE_DEVICE_TYPE(MB87419_MB87420, mb87419_mb87420_device, "mb87419_mb87420", "Roland LP (Fujitsu MB87419 and MB87420)")

mb87419_mb87420_device::mb87419_mb87420_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, MB87419_MB87420, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, device_rom_interface(mconfig, *this)
	, m_int_callback(*this)
{
}

//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void mb87419_mb87420_device::device_start()
{
	m_stream = stream_alloc(0, VOICE_COUNT, clock() / 1024, STREAM_SYNCHRONOUS);

	save_item(NAME(m_voices));
	save_item(NAME(m_lp1_ram_a));
	save_item(NAME(m_lp1_ram_b));
	save_item(NAME(m_lp2_ram));
	save_item(NAME(m_registers));
	save_item(NAME(m_irq_queue));
	save_item(NAME(m_selected));
	save_item(NAME(m_irq_read));
	save_item(NAME(m_irq_write));
	save_item(NAME(m_irq_count));
	save_item(NAME(m_irq_voice));
	save_item(NAME(m_irq_current_valid));
	save_item(NAME(m_readback));
	for (unsigned voice = 0; voice < VOICE_COUNT; ++voice)
	{
		save_item(NAME(m_voice_runtime[voice].enabled), voice);
		save_item(NAME(m_voice_runtime[voice].play_direction), voice);
		save_item(NAME(m_voice_runtime[voice].envelope_active), voice);
	}
	machine().save().register_postload(save_prepost_delegate(FUNC(mb87419_mb87420_device::update_irq), this));
}

//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void mb87419_mb87420_device::device_reset()
{
	m_stream->update();
	reset_state();
	m_voices.fill(0);
}

void mb87419_mb87420_device::device_clock_changed()
{
	if (m_stream)
		m_stream->set_sample_rate(clock() / 1024);
}

//-------------------------------------------------
//  rom_bank_pre_change - refresh the stream if the
//  ROM banking changes
//-------------------------------------------------

void mb87419_mb87420_device::rom_bank_pre_change()
{
	if (m_stream)
		m_stream->update();
}

void mb87419_mb87420_device::update_irq()
{
	m_int_callback(interrupt_active());
}

u8 mb87419_mb87420_device::read(offs_t offset)
{
	const bool side_effects = !machine().side_effects_disabled();
	if (side_effects)
		m_stream->update();
	return register_r(offset, side_effects);
}

void mb87419_mb87420_device::write(offs_t offset, u8 data)
{
	m_stream->update();
	register_w(offset, data);
}

s32 mb87419_mb87420_device::voice_r(offs_t voice)
{
	if (!machine().side_effects_disabled())
		m_stream->update();
	return m_voices[voice & 31];
}

void mb87419_mb87420_device::sound_stream_update(sound_stream &stream)
{
	for (int sample = 0; sample < stream.samples(); ++sample)
	{
		m_voices = render_sample();
		for (unsigned voice = 0; voice < VOICE_COUNT; ++voice)
			stream.put_int(voice, sample, m_voices[voice], 1 << 17);
	}
}

// Chip state and arithmetic.

namespace
{
// The MB87420 contains one 384 x 12-bit mask ROM: three interpolation
// coefficients for each of the 128 fractional phases. It decodes to the
// same coefficients as the GP chip. Thanks to InfoSecDJ and NukeYKT.
constexpr std::array<std::array<uint16_t, 128>, 3> interpolation_rom = {{
	{{
		3385, 3401, 3417, 3432, 3448, 3463, 3478, 3492, 3506, 3521, 3535, 3548, 3562, 3575, 3588, 3601,
		3614, 3626, 3638, 3650, 3662, 3673, 3685, 3696, 3707, 3718, 3728, 3739, 3749, 3759, 3768, 3778,
		3787, 3796, 3805, 3814, 3823, 3831, 3839, 3847, 3855, 3863, 3870, 3878, 3885, 3892, 3899, 3905,
		3912, 3918, 3924, 3930, 3936, 3942, 3948, 3953, 3958, 3963, 3968, 3973, 3978, 3983, 3987, 3991,
		3995, 4000, 4004, 4007, 4011, 4015, 4018, 4022, 4025, 4028, 4031, 4034, 4037, 4040, 4042, 4045,
		4047, 4050, 4052, 4054, 4057, 4059, 4061, 4063, 4064, 4066, 4068, 4070, 4071, 4073, 4074, 4076,
		4077, 4078, 4079, 4081, 4082, 4083, 4084, 4085, 4086, 4086, 4087, 4088, 4089, 4089, 4090, 4091,
		4091, 4092, 4092, 4093, 4093, 4094, 4094, 4094, 4094, 4095, 4095, 4095, 4095, 4095, 4095, 4095,
	}},
	{{
		710,  726,  742,  758,  775,  792,  809,  826,  844,  861,  879,  897,  915,  933,  952,  971,
		990,  1009, 1028, 1047, 1067, 1087, 1106, 1126, 1147, 1167, 1188, 1208, 1229, 1250, 1271, 1292,
		1314, 1335, 1357, 1379, 1400, 1423, 1445, 1467, 1489, 1512, 1534, 1557, 1580, 1602, 1625, 1648,
		1671, 1695, 1718, 1741, 1764, 1788, 1811, 1835, 1858, 1882, 1906, 1929, 1953, 1977, 2000, 2024,
		2048, 2071, 2095, 2119, 2143, 2166, 2190, 2214, 2237, 2261, 2284, 2308, 2331, 2355, 2378, 2401,
		2425, 2448, 2471, 2494, 2517, 2539, 2562, 2585, 2607, 2630, 2652, 2674, 2696, 2718, 2740, 2762,
		2783, 2805, 2826, 2847, 2868, 2889, 2910, 2931, 2951, 2971, 2991, 3011, 3031, 3051, 3070, 3089,
		3108, 3127, 3146, 3164, 3182, 3200, 3218, 3236, 3253, 3271, 3288, 3304, 3321, 3338, 3354, 3370,
	}},
	{{
		0,   0,   0,   1,   1,   1,   2,   2,   3,   3,   3,   4,   4,   5,   5,   6,   6,   7,   8,   8,   9,   10,
		10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  22,  23,  24,  26,  27,  29,  30,  32,  34,  36,  38,
		40,  42,  44,  46,  49,  51,  53,  56,  59,  62,  65,  68,  71,  74,  77,  81,  84,  88,  92,  96,  100, 104,
		109, 113, 118, 122, 127, 132, 137, 143, 148, 154, 160, 165, 171, 178, 184, 191, 197, 204, 211, 219, 226, 234,
		241, 249, 257, 266, 274, 283, 292, 301, 310, 319, 329, 339, 349, 359, 369, 380, 391, 402, 413, 424, 436, 448,
		460, 472, 484, 497, 510, 523, 536, 549, 563, 577, 591, 605, 619, 634, 648, 663, 679, 694,
	}},
}};

uint16_t read16(const std::array<uint8_t, 0x100> &data, const size_t offset)
{
	return uint16_t(data[offset] | (data[offset + 1] << 8));
}

int32_t wrap12(const int32_t value)
{
	const uint32_t bits = static_cast<uint32_t>(value) & 0xfff;
	return (bits & 0x800) != 0 ? static_cast<int32_t>(bits) - 0x1000 : static_cast<int32_t>(bits);
}

} // anonymous namespace

uint16_t mb87419_mb87420_device::parameter_word(const unsigned voice, const unsigned word) const
{
	if (voice >= VOICE_COUNT || word >= 8)
		return 0;
	return word < 4 ? m_lp1_ram_a[voice * 4 + word] : m_lp1_ram_b[voice * 4 + (word - 4)];
}

void mb87419_mb87420_device::set_parameter_word(const unsigned voice, const unsigned word, const uint16_t value)
{
	if (voice >= VOICE_COUNT || word >= 8)
		return;
	if (word < 4)
		m_lp1_ram_a[voice * 4 + word] = value;
	else
		m_lp1_ram_b[voice * 4 + (word - 4)] = value;
}

int32_t mb87419_mb87420_device::current_volume(const unsigned voice) const
{
	return int32_t(parameter_word(voice, 0)) | (int32_t(parameter_word(voice, 1) & 0x03ff) << 16);
}

void mb87419_mb87420_device::set_current_volume(const unsigned voice, const int32_t value)
{
	set_parameter_word(voice, 0, static_cast<uint16_t>(value));
	set_parameter_word(
		voice, 1,
		static_cast<uint16_t>((parameter_word(voice, 1) & 0xfc00) | ((static_cast<uint32_t>(value) >> 16) & 0x03ff)));
}

uint32_t mb87419_mb87420_device::current_address(const unsigned voice) const
{
	return uint32_t(parameter_word(voice, 4)) | (uint32_t(parameter_word(voice, 5)) << 16);
}

void mb87419_mb87420_device::set_current_address(const unsigned voice, const uint32_t value)
{
	set_parameter_word(voice, 4, static_cast<uint16_t>(value));
	set_parameter_word(voice, 5, static_cast<uint16_t>(value >> 16));
}

int32_t mb87419_mb87420_device::predictor(const unsigned voice) const
{
	if (voice >= VOICE_COUNT)
		return 0;
	const uint16_t value = m_lp2_ram[voice] & 0x0fff;
	return (value & 0x0800) != 0 ? int32_t(value) - 0x1000 : int32_t(value);
}

void mb87419_mb87420_device::set_predictor(const unsigned voice, const int32_t value)
{
	if (voice < VOICE_COUNT)
		m_lp2_ram[voice] = static_cast<uint16_t>(value) & 0x0fff;
}

void mb87419_mb87420_device::reset_state()
{
	m_lp1_ram_a.fill(0);
	m_lp1_ram_b.fill(0);
	m_lp2_ram.fill(0);
	m_voice_runtime = {};
	m_registers.fill(0);
	m_irq_queue.fill(0);
	m_selected = m_irq_read = m_irq_write = m_irq_count = m_irq_voice = 0;
	m_irq_current_valid = false;
	m_readback = 0;
	set_interrupt_line(false);
}

uint8_t mb87419_mb87420_device::register_r(const uint8_t offset, const bool side_effects)
{
	switch (offset & 3)
	{
	case 0:
	{
		const uint8_t result = m_irq_voice;
		if (side_effects && m_irq_current_valid)
		{
			if (m_irq_count)
			{
				m_irq_voice = m_irq_queue[m_irq_read];
				m_irq_read = static_cast<uint8_t>((m_irq_read + 1) % VOICE_COUNT);
				--m_irq_count;
				set_interrupt_line(false);
				set_interrupt_line(true);
			}
			else
			{
				m_irq_current_valid = false;
				set_interrupt_line(false);
			}
		}
		return result;
	}
	case 1:
	{
		const uint32_t bank = m_registers[3] & 0x3c;
		uint32_t address =
			uint32_t(m_registers[9]) | (uint32_t(m_registers[10]) << 8) | (uint32_t(m_registers[11]) << 16);
		address = (((address >> 6) + 2) & 0x3ffff) | (bank << 16);
		return read_rom(address);
	}
	case 2:
		return static_cast<uint8_t>(m_readback);
	case 3:
		return static_cast<uint8_t>(m_readback >> 8);
	default:
		return 0xff;
	}
}

void mb87419_mb87420_device::register_w(const uint8_t offset, const uint8_t data)
{
	m_registers[offset] = data;
	if (offset < 0x10)
	{
		voice_runtime &runtime = m_voice_runtime[m_selected];
		switch (offset)
		{
		case 0x00:
			set_parameter_word(m_selected, 0, read16(m_registers, 0));
			break;
		case 0x02:
			set_parameter_word(m_selected, 1, read16(m_registers, 2));
			break;
		case 0x04:
			set_parameter_word(m_selected, 2, read16(m_registers, 4));
			break;
		case 0x06:
		{
			const uint16_t value = read16(m_registers, 6);
			set_parameter_word(m_selected, 3, value);
			// A commit starts another segment even when rate/target repeat.
			// MV-30 release stages can have identical words; suppressing the
			// second commit strands the envelope at its completed target.
			// Rate 00 holds without requesting another completion.
			runtime.envelope_active = runtime.enabled && (value & 0xff) != 0;
		}
		break;
		case 0x08:
			set_parameter_word(m_selected, 4, read16(m_registers, 8));
			break;
		case 0x0a:
			set_parameter_word(m_selected, 5, read16(m_registers, 10));
			break;
		case 0x0c:
			set_parameter_word(m_selected, 6, read16(m_registers, 12));
			break;
		case 0x0e:
			set_parameter_word(m_selected, 7, read16(m_registers, 14));
			break;
		default:
			break;
		}
		return;
	}

	switch (offset)
	{
	case 0x11:
	case 0x13:
	case 0x15:
	case 0x17:
	{
		const uint8_t first = static_cast<uint8_t>(((offset >> 1) & 3) * 8);
		for (uint8_t i = 0; i < 8; ++i)
		{
			voice_runtime &runtime = m_voice_runtime[first + i];
			const bool enabled = (data & (1 << i)) != 0;
			if (enabled && !runtime.enabled)
			{
				runtime.play_direction = 0;
				runtime.envelope_active = true;
				set_predictor(first + i, 0);
			}
			runtime.enabled = enabled;
		}
	}
	break;

	case 0x10:
		m_readback = parameter_word(data & 0x1f, 0);
		break;
	case 0x12:
		m_readback = parameter_word(data & 0x1f, 1);
		break;
	case 0x14:
		m_readback = parameter_word(data & 0x1f, 2);
		break;
	case 0x16:
		m_readback = parameter_word(data & 0x1f, 3);
		break;
	case 0x18:
		m_readback = parameter_word(data & 0x1f, 4);
		break;
	case 0x1a:
		m_readback = parameter_word(data & 0x1f, 5);
		break;
	case 0x1c:
		m_readback = parameter_word(data & 0x1f, 6);
		break;
	case 0x1e:
		m_readback = parameter_word(data & 0x1f, 7);
		break;
	case 0x1f:
		m_selected = data & 0x1f;
		break;
	default:
		break;
	}
}

mb87419_mb87420_device::voices mb87419_mb87420_device::render_sample()
{
	voices result{};
	for (size_t i = 0; i < VOICE_COUNT; ++i)
	{
		voice_runtime &runtime = m_voice_runtime[i];
		if (!runtime.enabled)
			continue;

		int32_t volume = current_volume(static_cast<unsigned>(i));
		const uint16_t bank_volume = parameter_word(static_cast<unsigned>(i), 1);
		const uint8_t bank_mode = static_cast<uint8_t>(bank_volume >> 8) & 0xfc;
		const uint16_t step = parameter_word(static_cast<unsigned>(i), 2);
		const uint16_t envelope = parameter_word(static_cast<unsigned>(i), 3);
		const uint8_t envelope_rate = static_cast<uint8_t>(envelope);
		const uint8_t envelope_target = static_cast<uint8_t>(envelope >> 8);
		const uint32_t voice_address = current_address(static_cast<unsigned>(i));
		const uint16_t end = parameter_word(static_cast<unsigned>(i), 6);
		const uint16_t loop = parameter_word(static_cast<unsigned>(i), 7);

		const uint32_t high_phase = (voice_address >> 14) & 0x3ffff;
		const uint32_t old_sub_phase = voice_address & 0x3fff;
		const uint8_t interpolation_phase = static_cast<uint8_t>((old_sub_phase >> 7) & 0x7f);
		const bool alternate_loop = (bank_mode & 0x80) != 0;
		const uint32_t sub_phase = old_sub_phase + step;
		const unsigned crossed_bytes = sub_phase >> 14;
		const uint32_t rom_bank = ((bank_mode & 0x3c) >> 2) << 18;
		const int first_step = (bank_mode & 0x40) != 0 ? -1 : 1;
		const uint32_t loop_address = uint32_t(loop) << 2;
		const uint32_t end_address = uint32_t(end) << 2;
		const uint32_t repeat_address =
			loop_address == end_address ? loop_address : (loop_address + first_step) & 0x3ffff;

		auto advance_address =
			[alternate_loop, first_step, repeat_address, end_address](uint32_t &address, bool &return_leg)
		{
			const uint32_t boundary = return_leg ? repeat_address : end_address;
			const bool at_boundary = address == boundary;
			if (!alternate_loop && at_boundary)
				address = repeat_address;
			else if (!at_boundary)
				address = (address + (return_leg ? -first_step : first_step)) & 0x3ffff;
			return_leg = alternate_loop && (return_leg != at_boundary);
		};

		std::array<int32_t, 5> deltas{};
		uint32_t address = high_phase;
		bool return_leg = runtime.play_direction != 0;
		uint32_t next_address = address;
		bool next_return_leg = return_leg;
		for (unsigned fetch = 0; fetch < 5; ++fetch)
		{
			deltas[fetch] = decode_sample(static_cast<int8_t>(read_rom(rom_bank | address)));
			advance_address(address, return_leg);
			if (crossed_bytes == fetch + 1)
			{
				next_address = address;
				next_return_leg = return_leg;
			}
		}

		const int32_t initial_predictor = predictor(static_cast<unsigned>(i));
		int32_t reference = initial_predictor;
		for (unsigned crossed = 0; crossed < crossed_bytes; ++crossed)
			reference = wrap12(reference + deltas[crossed]);
		int32_t sample = initial_predictor;
		for (unsigned tap = 0; tap < 3; ++tap)
			sample += (interpolation_weight(tap, interpolation_phase) * deltas[tap]) >> 12;
		set_predictor(static_cast<unsigned>(i), reference);

		set_current_address(static_cast<unsigned>(i), (next_address << 14) | (sub_phase & 0x3fff));
		runtime.play_direction = static_cast<int8_t>(next_return_leg);

		const int32_t target = envelope_level(envelope_target);
		const int32_t increment = envelope_increment(envelope_rate);
		if (envelope_rate == 0 || envelope_rate == 0xff)
		{
			if (envelope_rate == 0xff)
				volume = target;
			runtime.envelope_active = false;
		}
		const bool increasing = volume < target;
		const bool decreasing = volume > target;
		const bool segment_active = runtime.envelope_active;
		if (increasing || decreasing)
		{
			volume += increasing ? increment : -increment;
			const bool overshot = (increasing && volume >= target) || (decreasing && volume <= target);
			if (overshot)
			{
				volume = target;
				runtime.envelope_active = false;
				signal_envelope_complete(static_cast<uint8_t>(i));
			}
			else
				runtime.envelope_active = envelope_rate != 0;
		}
		else
		{
			runtime.envelope_active = false;
			if (segment_active)
				signal_envelope_complete(static_cast<uint8_t>(i));
		}
		set_current_volume(static_cast<unsigned>(i), volume);
		result[i] = static_cast<int32_t>((int64_t(sample) * (volume >> 10)) >> 10);
	}
	return result;
}

int16_t mb87419_mb87420_device::decode_sample(const int8_t data)
{
	const int sign = data < 0 ? -1 : 1;
	int value = data < 0 ? -data : data;
	const uint8_t shift = static_cast<uint8_t>(value >> 4);
	value &= 0x0f;
	return static_cast<int16_t>(sign * (shift ? (0x10 + value) << (shift - 1) : value));
}

int32_t mb87419_mb87420_device::envelope_level(const uint8_t value)
{
	if (value < 0x30)
		return 0;
	const uint32_t mantissa = 0x10 | (value & 0x0f);
	const int shift = int(value >> 4) - 7;
	const uint32_t units = shift >= 0 ? mantissa << shift : mantissa >> -shift;
	return static_cast<int32_t>(units << 13);
}

int32_t mb87419_mb87420_device::envelope_increment(const uint8_t value)
{
	if (value == 0)
		return 0;
	if (value < 0x80)
		return static_cast<int32_t>((8u | (value & 7u)) << (3u + (value >> 3)));
	const unsigned group = (value - 0x80) >> 3;
	const unsigned mantissa = 16u - (value & 7u);
	return group < 15 ? static_cast<int32_t>(mantissa << (14u - group)) : static_cast<int32_t>(8u - (value & 7u));
}

int32_t mb87419_mb87420_device::interpolation_weight(const unsigned tap, const uint8_t phase)
{
	return tap < interpolation_rom.size() ? interpolation_rom[tap][phase & 0x7f] : 0;
}

uint8_t mb87419_mb87420_device::read_rom(const uint32_t address)
{
	return read_byte(address);
}

unsigned mb87419_mb87420_device::active_voice_count() const
{
	return static_cast<unsigned>(std::count_if(m_voice_runtime.begin(), m_voice_runtime.end(), [](const voice_runtime &voice) { return voice.enabled; }));
}

mb87419_mb87420_device::voice_state mb87419_mb87420_device::get_voice_state(const unsigned voice) const
{
	if (voice >= VOICE_COUNT)
		return {};
	const uint16_t bank_volume = parameter_word(voice, 1);
	const uint16_t envelope = parameter_word(voice, 3);
	return {current_volume(voice),
			static_cast<uint8_t>((bank_volume >> 8) & 0xfc),
			parameter_word(voice, 2),
			static_cast<uint8_t>(envelope),
			static_cast<uint8_t>(envelope >> 8),
			current_address(voice),
			parameter_word(voice, 6),
			parameter_word(voice, 7),
			predictor(voice),
			m_voice_runtime[voice].enabled};
}

void mb87419_mb87420_device::signal_envelope_complete(const uint8_t voice)
{
	if (!m_irq_current_valid)
	{
		m_irq_voice = voice;
		m_irq_current_valid = true;
		set_interrupt_line(false);
		set_interrupt_line(true);
	}
	else if (m_irq_count < VOICE_COUNT)
	{
		m_irq_queue[m_irq_write] = voice;
		m_irq_write = static_cast<uint8_t>((m_irq_write + 1) % VOICE_COUNT);
		++m_irq_count;
	}
}

void mb87419_mb87420_device::set_interrupt_line(const bool level)
{
	m_int_callback(level);
}
