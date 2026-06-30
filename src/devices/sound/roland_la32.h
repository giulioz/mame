// license:BSD-3-Clause
// copyright-holders:nukeykt
#ifndef MAME_SOUND_ROLAND_LA32_H
#define MAME_SOUND_ROLAND_LA32_H

#pragma once

#include "dirom.h"

DECLARE_DEVICE_TYPE(LA32, la32_device)

class la32_device : public device_t, public device_sound_interface, public device_rom_interface<20>
{
public:
	la32_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);

	auto int_callback() { return m_int_callback.bind(); }
	// Board-level PCM ROM address-line transform (zero for direct wiring).
	void set_rom_address_xor(u32 mask) { m_rom_address_xor = mask & 0xfffff; }

	u8 read(offs_t offset);
	void write(offs_t offset, u8 data);

	u8 sh3() { return (m_cycle >> 4) & 1; }

protected:
	virtual void device_start() override;
	virtual void device_reset() override;

	virtual void sound_stream_update(sound_stream &stream) override;

	TIMER_CALLBACK_MEMBER(update);

private:
	emu_timer *m_update_timer;
	sound_stream *m_stream;
	devcb_write_line m_int_callback;
	u32 m_rom_address_xor;
	u32 m_cycle;
	u8 m_reg_1c0;
	u8 m_reg_1c1;
	u8 m_reg_1c2;
	u8 m_reg_1c3;
	u8 m_reg_data_l;
	u16 m_reg_file[6][32];
	u32 m_counters[3][32];
	u8 m_pcm_end[32];
	s32 m_accum[2][8];
	u8 m_inactive;
	s32 m_prev;
	u16 m_w186;
	u16 m_w187;
	u16 m_w188;
	bool m_int_state;
	u8 m_int_status;

	void update_inactive();
	u8 pcm_rom_r(u32 address) { return device_rom_interface<20>::read_byte(address ^ m_rom_address_xor); }
};

#endif // MAME_SOUND_ROLAND_LA32_H
