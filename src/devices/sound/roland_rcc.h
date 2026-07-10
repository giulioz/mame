// license:BSD-3-Clause
// copyright-holders:giulioz
#ifndef MAME_SOUND_ROLAND_RCC_H
#define MAME_SOUND_ROLAND_RCC_H

#pragma once

#include "roland_rcc_dsp.h"


class roland_rcc_device : public device_t, public device_sound_interface
{
public:
	static constexpr unsigned NUM_CHANNELS = 32;

	roland_rcc_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);

	roland_rcc_device &set_program_voice_offset(unsigned offset) { m_program_voice_offset = offset; return *this; }

	// Enable/disable the die-RE effect-network (wet) output on top of the
	// calibrated dry mixer + chorus.  The network always runs on the
	// host-written RAM-A/RAM-B state; this only selects audibility.
	roland_rcc_device &set_dsp_output(bool output) { m_dsp_output = output; return *this; }

	u8 read(offs_t offset);
	void write(offs_t offset, u8 data);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void sound_stream_update(sound_stream &stream) override;

private:
	static constexpr unsigned CHORUS_BUFFER_SAMPLES = 2048;

	void update_dry_gain(u8 index);
	float chorus_read(unsigned side, double delay_samples) const;

	sound_stream *m_stream;
	u8 m_io[0x10];
	u8 m_program[0x100][3];
	u8 m_state[0x20][3];
	float m_gain[NUM_CHANNELS][2];
	u8 m_program_voice_offset;
	float m_chorus_buffer[2][CHORUS_BUFFER_SAMPLES];
	u32 m_chorus_position;
	double m_chorus_phase;

	// Die-RE program interpreter: runs the real 256-step mask-ROM program on
	// the firmware-written parameter file; its delay-network read taps are the
	// wet (reverb/delay) return -- zero until the firmware programs the
	// network.
	roland_rcc_dsp::engine m_dsp;
	bool m_dsp_output;
};

DECLARE_DEVICE_TYPE(ROLAND_RCC, roland_rcc_device)

#endif // MAME_SOUND_ROLAND_RCC_H
