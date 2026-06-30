// license:BSD-3-Clause
// copyright-holders:Valley Bell
#ifndef MAME_SOUND_ROLAND_LP_H
#define MAME_SOUND_ROLAND_LP_H

#pragma once

#include "dirom.h"
#include "wavwrite.h"

class mb87419_mb87420_device : public device_t, public device_sound_interface, public device_rom_interface<22>
{
public:
	static constexpr unsigned NUM_CHANNELS = 32;

	mb87419_mb87420_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	auto int_callback() { return m_int_callback.bind(); }

	uint8_t read(offs_t offset);
	void write(offs_t offset, uint8_t data, offs_t pc = 0);

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// device_sound_interface implementation
	virtual void sound_stream_update(sound_stream &stream) override;

	// device_rom_interface implementation
	virtual void rom_bank_pre_change() override;

	static int16_t decode_sample(int8_t data);

private:
	struct pcm_channel
	{
		pcm_channel() { }

		int32_t volume_cur = 0; // actually 26 bits
		uint8_t bank_loopmode = 0;
		uint16_t step = 0;      // 2.14 fixed point (0x4000 equals 32000 Hz)
		uint8_t volume_incr = 0;
		uint8_t volume_dest = 0;
		uint32_t addr = 0;      // current address (18.14 fixed point)
		uint16_t end = 0;       // end offset (high word)
		uint16_t loop = 0;      // loop offset (high word)

		bool enable = false;
		int8_t play_dir = 0;    // playing direction, -1 [backwards] / 0 [stopped] / +1 [forwards]
		int32_t loop_reference = 0;
		bool loop_reference_valid = false;
		bool dlm_checked = false;
		bool dlm = false;
		uint8_t dlm_reference = 0;
		uint8_t dlm_loop_sum = 0;
		int16_t dlm_bias = 0;
		bool dlm_alt_cycle_phase = false;
		bool irq = false;
		int tempReference = 0;
	};

	devcb_write_line m_int_callback;

	uint32_t m_clock;                   // clock
	uint32_t m_rate;                    // sample rate (usually 32000 Hz)
	sound_stream* m_stream;             // stream handle
	pcm_channel m_chns[NUM_CHANNELS];   // channel memory
	uint8_t m_sel_chn;                  // selected channel

	uint8_t m_int_channel;
	uint8_t m_irq_queue[NUM_CHANNELS];
	uint8_t m_irq_queue_read;
	uint8_t m_irq_queue_write;
	uint8_t m_irq_queue_count;
	bool m_irq_current_valid;
	uint16_t m_readback;
	u8 m_sound_io_buffer[0x100];
	util::wav_file_ptr m_debug_wav;
	std::vector<s16> m_debug_buffer;

	void signal_envelope_complete(uint8_t channel);
};

DECLARE_DEVICE_TYPE(MB87419_MB87420, mb87419_mb87420_device)

#endif // MAME_SOUND_ROLAND_LP_H
