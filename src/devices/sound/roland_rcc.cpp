// license:BSD-3-Clause
// copyright-holders:giulioz

#include "emu.h"
#include "roland_rcc.h"


DEFINE_DEVICE_TYPE(ROLAND_RCC, roland_rcc_device, "roland_rcc", "Roland RCC host interface and mixer")

roland_rcc_device::roland_rcc_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock) :
	device_t(mconfig, ROLAND_RCC, tag, owner, clock),
	device_sound_interface(mconfig, *this),
	m_stream(nullptr),
	m_program_voice_offset(0),
	m_chorus_position(0),
	m_chorus_phase(0.0)
{
}

void roland_rcc_device::device_start()
{
	m_stream = stream_alloc(NUM_CHANNELS, 2, clock(), STREAM_SYNCHRONOUS);

	save_item(NAME(m_io));
	save_item(NAME(m_program));
	save_item(NAME(m_state));
	save_item(NAME(m_gain));
	save_item(NAME(m_program_voice_offset));
	save_item(NAME(m_chorus_buffer));
	save_item(NAME(m_chorus_position));
	save_item(NAME(m_chorus_phase));
}

void roland_rcc_device::device_reset()
{
	std::fill(std::begin(m_io), std::end(m_io), 0);
	std::fill_n(&m_program[0][0], 0x100 * 3, 0);
	std::fill_n(&m_state[0][0], 0x20 * 3, 0);
	std::fill_n(&m_gain[0][0], NUM_CHANNELS * 2, 0.0F);
	std::fill_n(&m_chorus_buffer[0][0], 2 * CHORUS_BUFFER_SAMPLES, 0.0F);
	m_chorus_position = 0;
	m_chorus_phase = 0.0;
}

u8 roland_rcc_device::read(offs_t offset)
{
	return m_io[offset & 0x0f];
}

void roland_rcc_device::write(offs_t offset, u8 data)
{
	offset &= 0x0f;
	m_io[offset] = data;

	switch (offset)
	{
	case 0x04:
		if (m_stream)
			m_stream->update();
		std::copy_n(&m_io[0], 3, &m_state[data & 0x1f][0]);
		break;

	case 0x06:
		if (m_stream)
			m_stream->update();
		std::copy_n(&m_io[0], 3, &m_program[data][0]);
		update_dry_gain(data);
		break;

	case 0x0a:
		std::copy_n(&m_program[data][0], 3, &m_io[0]);
		break;

	case 0x0c: // reset/control latch
	case 0x0d: // operating mode latch
	default:
		break;
	}
}

float roland_rcc_device::chorus_read(unsigned side, double delay_samples) const
{
	double const read_position = double(m_chorus_position) - delay_samples;
	int const integral = int(std::floor(read_position));
	float const fraction = float(read_position - integral);
	unsigned const first = unsigned(integral) & (CHORUS_BUFFER_SAMPLES - 1);
	unsigned const second = (first + 1) & (CHORUS_BUFFER_SAMPLES - 1);
	return m_chorus_buffer[side][first]
		+ (m_chorus_buffer[side][second] - m_chorus_buffer[side][first]) * fraction;
}

void roland_rcc_device::update_dry_gain(u8 index)
{
	// These program locations are the dry L/R coefficient instructions found
	// by cycling every U-220 voice in Sound Test (1). Products can connect their
	// first synth voice at a later RCC program slot. The phase is circular: the
	// D-70 offsets LP contexts by four, so contexts 28-31 use program voices 0-3.
	// Its two reserved LP contexts align with non-voice program slots 4 and 28.
	static constexpr u8 gain_program[NUM_CHANNELS][2] = {
		{ 0x04, 0x05 }, { 0x0c, 0x0d }, { 0x15, 0x16 }, { 0x20, 0x21 },
		{ 0x24, 0x25 }, { 0x2a, 0x2b }, { 0x34, 0x35 }, { 0x3e, 0x40 },
		{ 0x44, 0x45 }, { 0x4c, 0x50 }, { 0x55, 0x56 }, { 0x5e, 0x60 },
		{ 0x64, 0x65 }, { 0x71, 0x72 }, { 0x76, 0x77 }, { 0x7d, 0x7e },
		{ 0x85, 0x89 }, { 0x8c, 0x8f }, { 0x93, 0x96 }, { 0x9d, 0x9e },
		{ 0xa4, 0xa7 }, { 0xab, 0xac }, { 0xb3, 0xb4 }, { 0xbf, 0xc0 },
		{ 0xc3, 0xc4 }, { 0xcd, 0xce }, { 0xd7, 0xd8 }, { 0xdf, 0xe0 },
		{ 0xff, 0xff }, { 0xef, 0xf0 }, { 0xf5, 0xf6 }, { 0xfe, 0xff }
	};

	for (unsigned voice = 0; voice < NUM_CHANNELS; voice++)
	{
		unsigned const program_voice = (voice + m_program_voice_offset) & (NUM_CHANNELS - 1);
		if (program_voice == 28)
			continue;
		for (unsigned side = 0; side < 2; side++)
		{
			if (gain_program[program_voice][side] != index)
				continue;

			// Hardware tests identify coefficient 0x40 with the shifter set as
			// unity. Dry coefficients are non-negative; signed feedback/effect
			// coefficients are intentionally not applied by this mixer.
			m_gain[voice][side] = std::clamp(float(s8(m_program[index][2])) / 64.0F, 0.0F, 2.0F);
		}
	}
}

void roland_rcc_device::sound_stream_update(sound_stream &stream)
{
	// RAM-A locations 09/0f, 0a/10 and 0b/11 are the two chorus rate,
	// centre-delay and depth lanes.  The exact RCC micro-operations and effect
	// sends remain unknown, but these host values are independently identified
	// by controlled U-220 sweeps.  Run a bounded stereo fractional delay here
	// so patches which rely on the chorus comb response do not collapse to the
	// dry mixer.  This remains a functional approximation, not execution of the
	// uploaded 256-word RCC program.
	auto state24 = [this](unsigned index)
	{
		return (u32(m_state[index][0]) << 16)
			| (u32(m_state[index][1]) << 8)
			| u32(m_state[index][2]);
	};
	unsigned const rate_value = std::clamp<int>(int(m_state[0x09][0]) - 0xe0, 0, 31);
	double const rate_hz = 0.05 * std::pow(200.0, double(rate_value) / 31.0);
	unsigned const delay_value = std::clamp<int>(int(m_state[0x0a][0]) - 0x20, 0, 64);
	double const centre_delay = (2.0 + double(delay_value) * (28.0 / 64.0))
		* clock() / 1000.0;
	double const left_depth = std::min(1.0, double(state24(0x0b)) / 0x2fd)
		* 5.0 * clock() / 1000.0;
	double const right_depth = std::min(1.0, double(state24(0x11)) / 0x0ff)
		* 5.0 * clock() / 1000.0;
	float const left_wet = std::clamp(float(m_program[0x01][2]) / 127.0F, 0.0F, 1.0F)
		* 0.5F;
	float const right_wet = std::clamp(float(m_program[0x03][2]) / 127.0F, 0.0F, 1.0F)
		* 0.5F;
	double constexpr pi = 3.14159265358979323846;
	double const phase_step = 2.0 * pi * rate_hz / clock();

	for (int sample = 0; sample < stream.samples(); sample++)
	{
		float left = 0.0F;
		float right = 0.0F;
		for (unsigned voice = 0; voice < NUM_CHANNELS; voice++)
		{
			float const input = stream.get(voice, sample);
			left += input * m_gain[voice][0];
			right += input * m_gain[voice][1];
		}

		m_chorus_buffer[0][m_chorus_position] = left;
		m_chorus_buffer[1][m_chorus_position] = right;
		float const chorus_left = chorus_read(0,
			centre_delay + std::sin(m_chorus_phase) * left_depth);
		float const chorus_right = chorus_read(1,
			centre_delay - std::sin(m_chorus_phase) * right_depth);
		m_chorus_position = (m_chorus_position + 1) & (CHORUS_BUFFER_SAMPLES - 1);
		m_chorus_phase += phase_step;
		if (m_chorus_phase >= 2.0 * pi)
			m_chorus_phase -= 2.0 * pi;

		stream.put(0, sample, left + chorus_left * left_wet);
		stream.put(1, sample, right + chorus_right * right_wet);
	}
}
