// license:BSD-3-Clause
// copyright-holders:giulioz
/***************************************************************************

    Roland MV-30 Studio M music production system (1990).

    IC19: Intel 80C196KB, 12 MHz
    IC15: Hitachi HG62E33B08F CPU gate array
    IC6..9: 512 KiB DRAM (four 256K x 4 devices)
    IC35: 16 KiB boot EPROM; the operating system is loaded from floppy
    IC1: NEC uPD72068GF-3B9, 3.5-inch 2DD drive
    IC43: Toshiba TC23SC060AF-501 panel gate array
    IC31/29/30/28: MB87419/MB87420/MB87424/TC23SC140 sound section

    The CPU gate array provides four 16 KiB instruction windows and four
    independent data windows, with bank bases in units of 1 KiB. Registers 0100..010f select the windows; the
    remaining registers control interrupts and floppy DMA. Registers 0115,
    0118/011a/011c and 011d hold the IRQ mask, DMA count/address and control;
    0110/0112 bus-mode bits are retained but not decoded. Code and stack
    can therefore occupy the same CPU address in different physical banks.

    The MV-30 boot ROM selects bank 0474 for its peripheral page:
      +0000 RCC, +0800 panel index/data, +1000 FDC status/FIFO,
      +1800 LP, +2000 tape sync, +2800 LCD data/status, +3000 TVF.
    These offsets come from the boot firmware. The sound chips and waveform ROMs are shared with D-70.
    LP voices 0..31 pass through TVF/TVA to RCC without a software dry mix.
    The RCC mask program mixes voices and generates effects; firmware uses
    output slots 6/7 for the main stereo pair, monitored as PHONES by default.

    The panel switch rows use SB0 for mode keys and SB2 for cursor keys.
    LED registers 08..0f and 10..17 expose the two eight-bit row banks.
    HSO0/1 select and enable the metronome oscillator; HSO3 gates it to
    PHONES. P1.3 mutes the line outputs. The analog waveform/level and
    mute settling are approximated; the nominal pitches are 1320/1110 Hz.

    IC14 latches the value encoder event/direction on P0.6/P0.5 until HSO2
    clears it. LP completion reaches P0.7/EXTINT directly (vector 200e),
    while the CPU gate drives P2.2/EXTINT1 (vector 203a). The footswitch
    reaches HSI0; HSI2/HSI1 receive active-low PCM card sense. The card
    sockets share the LP waveform address/data wiring.

    TODO: panel scan/debounce timing, tape sync, and the
    complete CPU-gate interrupt/mode logic. Audio bus taps and the four-slot
    TVF/RCC delay are assumed to match the D-70; the DIRECT stereo pairs
    and analog DAC stages are not modeled. DRAM refresh and external READY
    wait states are not modeled. The LCD module is represented by a T6963C
    controller.

    Sources: Roland MV-30 Service Notes, first edition, December 1990,
    pp. 4, 8, 12 and 15..18; boot ROM and system disk disassembly.
    Schematics: https://www.synthxl.com/roland-mv-30/

***************************************************************************/

#include "emu.h"

#include "bus/generic/carts.h"
#include "bus/generic/slot.h"
#include "bus/midi/midiinport.h"
#include "bus/midi/midioutport.h"
#include "cpu/mcs96/i8xc196.h"
#include "formats/roland_dsk.h"
#include "imagedev/floppy.h"
#include "machine/input_merger.h"
#include "machine/upd765.h"
#include "sound/beep.h"
#include "sound/flt_vol.h"
#include "sound/roland_lp.h"
#include "sound/roland_rcc.h"
#include "sound/roland_tvf.h"
#include "video/t6963c.h"

#include "emupal.h"
#include "screen.h"
#include "speaker.h"
#include "softlist_dev.h"

#include "roland_mv30.lh"

#include "logmacro.h"

namespace
{

class roland_mv30_state : public driver_device
{
public:
	roland_mv30_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_cpu(*this, "maincpu")
		, m_pcm(*this, "pcm")
		, m_tvf(*this, "tvf")
		, m_rcc(*this, "rcc")
		, m_metronome(*this, "metronome")
		, m_metronome_phone(*this, "metronome_phone")
		, m_mix_volume(*this, "mix_volume%u", 0U)
		, m_lcd(*this, "lcd")
		, m_fdc(*this, "fdc")
		, m_tc(*this, "fdc_tc")
		, m_floppy(*this, "fdc:0")
		, m_bootrom(*this, "bootrom")
		, m_ram(*this, "ram", 0x80000, ENDIANNESS_LITTLE)
		, m_keys(*this, "KEY%u", 0U)
		, m_faders(*this, "FADER%u", 0U)
		, m_dial(*this, "DIAL")
		, m_foot(*this, "FOOT")
		, m_metronome_level(*this, "METLEVEL")
		, m_value_keys(*this, "VALUE")
		, m_volume(*this, "VOLUME")
		, m_phones(*this, "speaker")
		, m_mix_out(*this, "mix")
		, m_cards(*this, "pcmcard%u", 0U)
		, m_leds(*this, "led%u_%u", 0U, 0U)
	{
	}

	void mv30(machine_config &config) ATTR_COLD;
	void init_mv30() ATTR_COLD;
	std::pair<std::error_condition, std::string> card_load(unsigned slot, device_image_interface &image);
	void card_unload(unsigned slot);
	DEVICE_IMAGE_LOAD_MEMBER(card0_load) { return card_load(0, image); }
	DEVICE_IMAGE_LOAD_MEMBER(card1_load) { return card_load(1, image); }
	DEVICE_IMAGE_UNLOAD_MEMBER(card0_unload) { card_unload(0); }
	DEVICE_IMAGE_UNLOAD_MEMBER(card1_unload) { card_unload(1); }
	INPUT_CHANGED_MEMBER(metronome_level_changed) { update_audio(); }
	INPUT_CHANGED_MEMBER(volume_changed) { update_audio(); }

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

private:
	void program_map(address_map &map) ATTR_COLD;
	void data_map(address_map &map) ATTR_COLD;
	void lcd_map(address_map &map) ATTR_COLD;
	void palette(palette_device &palette) const ATTR_COLD;
	static void floppy_formats(format_registration &fr);
	static void floppies(device_slot_interface &device);

	u16 gate_r(offs_t offset, u16 mem_mask = ~0);
	void gate_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u8 memory_r(bool instruction, u16 address);
	void memory_w(bool instruction, u16 address, u8 data);
	u16 memory_word_r(bool instruction, u16 address, u16 mem_mask);
	void memory_word_w(bool instruction, u16 address, u16 data, u16 mem_mask);
	u8 peripheral_r(u16 offset);
	void peripheral_w(u16 offset, u8 data);
	u8 lp_r(u8 offset);
	void lp_w(u8 offset, u8 data);
	u8 panel_r(u8 offset);
	void panel_w(u8 offset, u8 data);
	void update_panel_outputs();
	u8 port0_r();
	void hso_w(offs_t offset, u8 data, u8 mem_mask);
	void update_audio();
	u8 port1_r() { return 0xff; }
	void port1_w(u8 data);
	u8 port2_r();
	void port2_w(u8 data);
	u16 slider_r() { return m_faders[m_fader_select]->read() * 0x3ff / 100; }
	void lp_irq_w(int state);
	void fdc_irq_w(int state);
	void fdc_drq_w(int state);
	void update_irq();
	u8 irq_sources() const;
	void update_ready();
	void floppy_changed(floppy_image_device *) { update_ready(); }
	TIMER_CALLBACK_MEMBER(dma_terminal_count);
	TIMER_CALLBACK_MEMBER(panel_tick);

	required_device<i80c196kb_device> m_cpu;
	required_device<mb87419_mb87420_device> m_pcm;
	required_device<mb87424_device> m_tvf;
	required_device<tc23sc140_device> m_rcc;
	required_device<beep_device> m_metronome;
	required_device<filter_volume_device> m_metronome_phone;
	required_device_array<filter_volume_device, 2> m_mix_volume;
	required_device<t6963c_device> m_lcd;
	required_device<upd72068_device> m_fdc;
	required_device<input_merger_device> m_tc;
	required_device<floppy_connector> m_floppy;
	required_region_ptr<u8> m_bootrom;
	memory_share_creator<u8> m_ram;
	required_ioport_array<8> m_keys;
	required_ioport_array<8> m_faders;
	required_ioport m_dial;
	required_ioport m_foot;
	required_ioport m_metronome_level;
	required_ioport m_value_keys;
	required_ioport m_volume;
	required_device<speaker_device> m_phones;
	required_device<speaker_device> m_mix_out;
	required_device_array<generic_slot_device, 2> m_cards;
	output_finder<8, 16> m_leds;
	std::array<bool, 2> m_card_loaded { };

	std::array<u16, 16> m_gate { };
	std::array<u8, 256> m_sound_io { };
	std::array<u8, 128> m_panel { };
	std::array<u8, 8> m_key_previous { };
	std::array<u8, 16> m_key_fifo { };
	u8 m_key_head = 0, m_key_count = 0, m_fader_select = 0;
	bool m_panel_irq = false;
	u8 m_panel_register = 0;
	bool m_dma_irq = false;
	u32 m_dma_address = 0;
	u16 m_dma_count = 0;
	u8 m_dma_control = 0;
	bool m_dma_active = false;
	bool m_fdc_irq = false, m_lp_irq = false, m_irq = false;
	u8 m_irq_acknowledged = 0;
	u8 m_dial_previous = 0;
	bool m_encoder_event = false, m_encoder_negative = false, m_encoder_clear = false;
	bool m_foot_previous = false;
	u8 m_hso_state = 0;
	bool m_output_mute = true;
	s16 m_dial_pending = 0;
	u8 m_value_previous = 0;
	u16 m_value_held = 0;
	emu_timer *m_dma_timer = nullptr;
	emu_timer *m_panel_timer = nullptr;
};

void roland_mv30_state::machine_start()
{
	m_dma_timer = timer_alloc(FUNC(roland_mv30_state::dma_terminal_count), this);
	m_panel_timer = timer_alloc(FUNC(roland_mv30_state::panel_tick), this);
	m_fdc->set_ready_line_connected(false);
	if (floppy_image_device *drive = m_floppy->get_device())
	{
		drive->setup_load_cb(floppy_image_device::load_cb(&roland_mv30_state::floppy_changed, this));
		drive->setup_unload_cb(floppy_image_device::unload_cb(&roland_mv30_state::floppy_changed, this));
	}
	save_item(NAME(m_card_loaded));
	save_item(NAME(m_gate));
	save_item(NAME(m_sound_io));
	save_item(NAME(m_panel));
	save_item(NAME(m_panel_register));
	save_item(NAME(m_dma_irq));
	save_item(NAME(m_dma_address));
	save_item(NAME(m_dma_count));
	save_item(NAME(m_dma_control));
	save_item(NAME(m_dma_active));
	save_item(NAME(m_fdc_irq));
	save_item(NAME(m_lp_irq));
	save_item(NAME(m_irq));
	save_item(NAME(m_irq_acknowledged));
	save_item(NAME(m_key_previous));
	save_item(NAME(m_key_fifo));
	save_item(NAME(m_key_head));
	save_item(NAME(m_key_count));
	save_item(NAME(m_fader_select));
	save_item(NAME(m_panel_irq));
	save_item(NAME(m_dial_previous));
	save_item(NAME(m_encoder_event));
	save_item(NAME(m_encoder_negative));
	save_item(NAME(m_encoder_clear));
	save_item(NAME(m_foot_previous));
	save_item(NAME(m_hso_state));
	save_item(NAME(m_output_mute));
	for (unsigned slot = 0; slot < 2; ++slot)
	{
		const u32 base = slot ? 0x380000 : 0x280000;
		save_pointer(memregion("pcmorg")->base() + base, "card_rom", 0x80000, slot);
		save_pointer(memregion("pcm")->base() + base, "card_bus", 0x80000, slot);
	}
	save_item(NAME(m_dial_pending));
	save_item(NAME(m_value_previous));
	save_item(NAME(m_value_held));
	machine().save().register_postload(save_prepost_delegate(FUNC(roland_mv30_state::update_irq), this));
	// Beep/filter controls and stream gains restore their own saved state.
	// Updating them here would run streams before the sound manager has
	// resynchronized their history after a load that moves time backwards.
	machine().save().register_postload(save_prepost_delegate(FUNC(roland_mv30_state::update_panel_outputs), this));
}

void roland_mv30_state::machine_reset()
{
	m_gate.fill(0);
	std::fill_n(m_gate.begin(), 8, 0x780);
	m_sound_io.fill(0);
	m_panel.fill(0);
	update_panel_outputs();
	m_panel_register = 0;
	m_dma_irq = false;
	m_dma_active = m_fdc_irq = m_lp_irq = m_irq = false;
	m_irq_acknowledged = 0;
	m_dma_address = m_dma_count = m_dma_control = 0;
	m_key_previous.fill(0);
	m_key_fifo.fill(0);
	m_key_head = m_key_count = m_fader_select = 0;
	m_panel_irq = false;
	m_encoder_event = m_encoder_negative = m_encoder_clear = false;
	m_hso_state = 0;
	m_output_mute = true;
	update_audio();
	m_foot_previous = false;
	m_cpu->set_input_line(i8x9x_device::HSI0_LINE, CLEAR_LINE); // Released pedal, through Q3.
	m_cpu->set_input_line(i8x9x_device::HSI1_LINE, !m_card_loaded[1]);
	m_cpu->set_input_line(i8x9x_device::HSI2_LINE, !m_card_loaded[0]);
	m_cpu->set_input_line(i8x9x_device::HSI3_LINE, ASSERT_LINE);
	m_dial_pending = 0;
	m_value_previous = 0;
	m_value_held = 0;
	m_panel_timer->adjust(attotime::from_hz(1000), 0, attotime::from_hz(1000));
	m_dial_previous = m_dial->read();
	m_dma_timer->adjust(attotime::never);
	m_tc->in_w<0>(0);
	m_tc->in_w<1>(0);
	update_ready();
	update_irq();
}

u16 roland_mv30_state::gate_r(offs_t offset, u16 mem_mask)
{
	// Bit 1 is the gate UART's transmit-ready indication. The MV-30 MIDI
	// connector uses the CPU UART; this second channel is not connected.
	const u16 result = m_gate[offset] | (offset == 10 ? 2 : 0);
	// The DMA service routine acknowledges completion by reading 011d.
	if (offset == 14 && ACCESSING_BITS_8_15 && !machine().side_effects_disabled())
	{
		m_dma_irq = false;
		update_irq();
	}
	return result;
}

void roland_mv30_state::gate_w(offs_t offset, u16 data, u16 mem_mask)
{
	COMBINE_DATA(&m_gate[offset]);
	LOG("gate %04x = %04x mask %04x PC %04x\n", 0x100 + offset * 2, m_gate[offset], mem_mask, m_cpu->pc());
	if (offset == 10)
		update_irq();
	if (offset == 14 && ACCESSING_BITS_8_15)
	{
		m_dma_count = m_gate[12];
		m_dma_address = m_gate[13] | (u32(m_gate[14] & 0xff) << 16);
		m_dma_control = m_gate[14] >> 8;
		m_dma_irq = false;
		update_irq();
		m_dma_active = m_dma_count != 0;
		LOG("DMA %02x address %06x count %04x\n", m_dma_control, m_dma_address, m_dma_count);
	}
}

u8 roland_mv30_state::memory_r(bool instruction, u16 address)
{
	const u16 bank = m_gate[(address >> 14) + (instruction ? 0 : 4)];
	const u32 local = address & 0x3fff;
	if (bank < 0x400)
	{
		const u32 physical = u32(bank) * 0x400 + local;
		return physical < m_ram.bytes() ? m_ram[physical] : 0xff;
	}
	if (bank >= 0x400 && bank < 0x780)
		return peripheral_r(u16((u32(bank) - 0x474) * 0x400 + local));
	if (bank >= 0x780)
		return m_bootrom[(u32(bank) * 0x400 + local) & 0x3fff]; // A14/A15 are not connected on the 27C128.
	LOG("unmapped read bank %04x address %04x PC %04x\n", bank, address, m_cpu->pc());
	return 0xff;
}

void roland_mv30_state::memory_w(bool instruction, u16 address, u8 data)
{
	const u16 bank = m_gate[(address >> 14) + (instruction ? 0 : 4)];
	const u32 local = address & 0x3fff;
	if (bank < 0x400)
	{
		const u32 physical = u32(bank) * 0x400 + local;
		if (physical < m_ram.bytes())
			m_ram[physical] = data;
	}
	else if (bank >= 0x400 && bank < 0x780)
		peripheral_w(u16((u32(bank) - 0x474) * 0x400 + local), data);
}

u16 roland_mv30_state::memory_word_r(bool instruction, u16 address, u16 mem_mask)
{
	u16 result = 0xffff;
	if (ACCESSING_BITS_0_7)
		result = (result & 0xff00) | memory_r(instruction, address);
	if (ACCESSING_BITS_8_15)
		result = (result & 0x00ff) | (u16(memory_r(instruction, address + 1)) << 8);
	// The gate contributes the interrupt source to the fetched EXTINT1
	// vector: panel=1, FDC=2, tape sync=3, external DMA completion=4.
	// Source 0 is unused. LP completion bypasses the gate through P0.7.
	if (!instruction && address == 0x203a && m_irq)
	{
		const u8 requests = irq_sources() & ~m_irq_acknowledged;
		const unsigned source = BIT(requests, 1) ? 1 : BIT(requests, 2) ? 2 : 4;
		result = (result & ~0x1c) | (source << 2);
		if (ACCESSING_BITS_0_7 && !machine().side_effects_disabled())
		{
			// Vector fetch acknowledges this source. Keep it suppressed until
			// its device drops INT; present any other waiting source as a new
			// edge, since the KB's EXTINT1 input is not level sensitive.
			m_irq_acknowledged |= 1 << source;
			m_cpu->set_input_line(i8xc196_device::EXTINT1_LINE, CLEAR_LINE);
			m_irq = false;
			update_irq();
		}
	}
	return result;
}

void roland_mv30_state::memory_word_w(bool instruction, u16 address, u16 data, u16 mem_mask)
{
	if (ACCESSING_BITS_0_7)
		memory_w(instruction, address, data);
	if (ACCESSING_BITS_8_15)
		memory_w(instruction, address + 1, data >> 8);
}

u8 roland_mv30_state::lp_r(u8 offset)
{
	if (offset == 1 || offset == 0x21)
		offset ^= 0x20;
	if (offset < 0x20)
		return m_pcm->read(offset);
	if (offset < 0x40)
		offset -= 0x20;
	if (offset == 1)
	{
		const u32 staged = m_sound_io[9] | (u32(m_sound_io[10]) << 8) | (u32(m_sound_io[11]) << 16);
		const u32 address = (((staged >> 6) + 2) & 0x3ffff) | (u32(m_sound_io[3]) << 16);
		return address < memregion("pcm")->bytes() ? memregion("pcm")->base()[address] : 0xff;
	}
	return m_sound_io[offset];
}

void roland_mv30_state::lp_w(u8 offset, u8 data)
{
	if (offset < 0x20)
		m_pcm->write(offset, data);
	m_sound_io[offset] = data;
}

u8 roland_mv30_state::peripheral_r(u16 offset)
{
	LOG("I/O read %04x PC %04x\n", offset, m_cpu->pc());
	if (offset < 0x800)
		return m_rcc->read(offset);
	if (offset == 0x801)
		return panel_r(m_panel_register);
	if (offset == 0x1000)
		return m_fdc->msr_r();
	if (offset == 0x1001)
		return m_fdc->fifo_r();
	if (offset >= 0x1800 && offset < 0x1900)
		return lp_r(offset - 0x1800);
	if (offset >= 0x2000 && offset < 0x2020)
		return 0xff; // Tape sync
	if (offset == 0x2800 || offset == 0x2802)
		return m_lcd->read((offset - 0x2800) >> 1);
	if (offset >= 0x3000 && offset < 0x3100)
		return m_tvf->read(offset - 0x3000);
	return 0xff;
}

void roland_mv30_state::peripheral_w(u16 offset, u8 data)
{
	LOG("I/O write %04x = %02x PC %04x\n", offset, data, m_cpu->pc());
	if (offset < 0x800)
		m_rcc->write(offset, data);
	else if (offset == 0x800)
		m_panel_register = data & 0x7f;
	else if (offset == 0x801)
		panel_w(m_panel_register, data);
	else if (offset == 0x1000)
		m_fdc->auxcmd_w(data);
	else if (offset == 0x1001)
		m_fdc->fifo_w(data);
	else if (offset >= 0x1800 && offset < 0x1900)
		lp_w(offset - 0x1800, data);
	else if (offset == 0x2800 || offset == 0x2802)
		m_lcd->write((offset - 0x2800) >> 1, data);
	else if (offset >= 0x3000 && offset < 0x3100)
		m_tvf->write(offset - 0x3000, data);
}

// TC23SC060 has a register index at C800 and a data port at C801.
// The boot ROM loads registers 70..73/7b and clears the LED words 08..17.
u8 roland_mv30_state::panel_r(u8 offset)
{
	if (offset < 8)
		return ~m_keys[offset]->read(); // Pressed scan inputs are active high at the host.
	if (offset == 0x78)
	{
		const u8 result = m_key_count ? 0x80 | m_key_fifo[m_key_head] : 0;
		if (m_key_count && !machine().side_effects_disabled())
		{
			// Reading an event acknowledges the panel INT. Another queued
			// event rearms it rather than holding the CPU interrupt pin high.
			m_panel_irq = false;
			update_irq();
			m_key_head = (m_key_head + 1) & 15;
			--m_key_count;
			m_panel_irq = m_key_count != 0;
			update_irq();
		}
		return result;
	}
	return m_panel[offset & 0x7f];
}

void roland_mv30_state::panel_w(u8 offset, u8 data)
{
	m_panel[offset & 0x7f] = data;
	if (offset >= 8 && offset < 0x18)
		update_panel_outputs();
	if (offset == 0x70)
		update_irq();
}

void roland_mv30_state::update_panel_outputs()
{
	// Each scan row has two LED bytes, written at 08+row and 10+row.
	// Expose the held matrix levels; LED duty/brightness timing is omitted.
	for (unsigned row = 0; row < 8; ++row)
		for (unsigned column = 0; column < 16; ++column)
			m_leds[row][column] = BIT(m_panel[8 + row + ((column & 8) ? 8 : 0)], column & 7);
}

// Register 78 reports a valid press in bit 7 and SC/SB position in bits 5..0.
// Registers 00..07 supply the held-key matrix for modifiers and repeat. The
// scan period and FIFO depth are provisional; contact debounce is not modeled.
TIMER_CALLBACK_MEMBER(roland_mv30_state::panel_tick)
{
	if (BIT(m_panel[0x70], 7))
	{
		for (unsigned row = 0; row < 8; ++row)
		{
			const u8 keys = ~m_keys[row]->read();
			const u8 pressed = keys & ~m_key_previous[row];
			m_key_previous[row] = keys;
			for (unsigned column = 0; column < 8; ++column)
				if (BIT(pressed, column) && m_key_count < m_key_fifo.size())
				{
					m_key_fifo[(m_key_head + m_key_count) & 15] = row * 8 + column;
					++m_key_count;
				}
		}
		m_panel_irq = m_key_count != 0;
		update_irq();
	}

	// IC14's two flip-flops hold one encoder event until the HSO2 clear pulse.
	// Preserve additional host detents until that event has been acknowledged.
	const u8 dial = m_dial->read();
	m_dial_pending = std::clamp<int>(m_dial_pending + s8(dial - m_dial_previous), -32768, 32767);
	m_dial_previous = dial;
	// Host VALUE -/+ keys turn the encoder one detent per press, repeating
	// at 20 Hz after 400 ms like a held dial.
	const u8 value = m_value_keys->read() & 3;
	if (value != m_value_previous)
		m_value_held = 0;
	m_value_previous = value;
	if (value == 1 || value == 2)
	{
		if (!m_value_held || m_value_held == 400)
			m_dial_pending = std::clamp<int>(m_dial_pending + (value == 2 ? 1 : -1), -32768, 32767);
		m_value_held = m_value_held == 400 ? 350 : m_value_held + 1;
	}
	if (!m_encoder_event && !m_encoder_clear && m_dial_pending)
	{
		m_encoder_event = true;
		m_encoder_negative = m_dial_pending < 0;
		m_dial_pending += m_encoder_negative ? 1 : -1;
	}
	const bool foot = BIT(m_foot->read(), 7);
	if (foot != m_foot_previous)
	{
		m_foot_previous = foot;
		m_cpu->set_input_line(i8x9x_device::HSI0_LINE, foot);
	}
}

u8 roland_mv30_state::port0_r()
{
	// ACH1..4 are grounded. ACH0 also reads digitally as the selected fader;
	// use mid-scale for its digital threshold, omitting the undefined region.
	return (slider_r() >= 0x200 ? 1 : 0) | (m_encoder_event ? 0x40 : 0) | (m_encoder_negative ? 0x20 : 0) | (m_lp_irq ? 0x80 : 0);
}

void roland_mv30_state::hso_w(offs_t offset, u8 data, u8 mem_mask)
{
	m_hso_state = (m_hso_state & ~mem_mask) | (data & mem_mask);
	if (mem_mask & 0x0b)
		update_audio();
	if (BIT(mem_mask, 2))
	{
		m_encoder_clear = !BIT(data, 2);
		if (m_encoder_clear)
			m_encoder_event = false;
	}
}

void roland_mv30_state::update_audio()
{
	// IC10c/d is a gated Schmitt oscillator, nominally 1320 Hz / 1110 Hz.
	// HSO0 switches C12 through Q6; HSO1 enables oscillation. HSO3 controls
	// the Q5 JFET metronome gate to PHONES. P1.3 controls line-output muting
	// through IC16a/IC7 and the analog board's six shunt transistors.
	// The RC waveform, oscillator tolerance and mute settling are omitted.
	m_metronome->set_clock(BIT(m_hso_state, 0) ? 1110 : 1320);
	m_metronome->set_state(BIT(m_hso_state, 1));
	m_metronome->set_output_gain(0, m_metronome_level->read() / 100.0);
	m_metronome_phone->set_gain(BIT(m_hso_state, 3) ? 1.0 : 0.0);
	for (auto &volume : m_mix_volume)
		volume->set_gain(m_output_mute ? 0.0 : 1.0);
	// The front VOLUME slider sets PHONES and MIX OUT after the mute stage.
	const float volume = m_volume->read() / 100.0f;
	for (unsigned channel = 0; channel < 2; ++channel)
	{
		m_phones->set_input_gain(channel, volume);
		m_mix_out->set_input_gain(channel, volume);
	}
}

u8 roland_mv30_state::port2_r()
{
	floppy_image_device *drive = m_floppy->get_device();
	return 0xf3 | (m_irq ? 4 : 0) | ((drive ? !drive->dskchg_r() : 1) << 3);
}

void roland_mv30_state::port1_w(u8 data)
{
	// P1.0..2 select the eight slider voltages through IC18 (BU4051).
	m_fader_select = data & 7;
	if (m_output_mute != BIT(data, 3))
	{
		m_output_mute = BIT(data, 3);
		update_audio();
	}
}

void roland_mv30_state::port2_w(u8 data)
{
	// P2.5 supplies the software terminal-count input independently of the
	// CPU gate's byte counter. Firmware asserts it to stop a partial sector.
	m_tc->in_w<0>(BIT(data, 5));
	if (BIT(data, 5))
		m_dma_active = false;
}

void roland_mv30_state::lp_irq_w(int state)
{
	m_lp_irq = state;
	// EINT connects LP-1 directly to ACH7/P0.7. With IOC1.1 set, the
	// firmware services each completed envelope through INT7 at 200e.
	m_cpu->set_input_line(i8x9x_device::EXTINT_LINE, state);
}

void roland_mv30_state::fdc_irq_w(int state)
{
	m_fdc_irq = state;
	update_irq();
}

u8 roland_mv30_state::irq_sources() const
{
	const u8 mask = m_gate[10] >> 8;
	return (((m_panel_irq && BIT(m_panel[0x70], 7)) ? 2 : 0) | (m_fdc_irq ? 4 : 0) | (m_dma_irq ? 0x10 : 0)) & mask;
}

void roland_mv30_state::update_irq()
{
	const u8 sources = irq_sources();
	m_irq_acknowledged &= sources;
	m_irq = (sources & ~m_irq_acknowledged) != 0;
	m_cpu->set_input_line(i8xc196_device::EXTINT1_LINE, m_irq ? ASSERT_LINE : CLEAR_LINE);
}

void roland_mv30_state::fdc_drq_w(int state)
{
	if (!state || !m_dma_active)
		return;
	const u32 address = m_dma_address & 0x7ffff;
	if (BIT(m_dma_control, 1))
		m_ram[address] = m_fdc->dma_r();
	else
		m_fdc->dma_w(m_ram[address]);
	++m_dma_address;
	m_gate[12] = --m_dma_count;
	if (!m_dma_count)
	{
		m_dma_active = false;
		// Defer TC until after dma_r()/dma_w() returns from the FDC's live
		// state machine, to avoid recursively entering live_sync().
		m_dma_timer->adjust(attotime::zero);
	}
}

TIMER_CALLBACK_MEMBER(roland_mv30_state::dma_terminal_count)
{
	m_tc->in_w<1>(1);
	m_tc->in_w<1>(0);
	m_dma_irq = true;
	update_irq();
}

void roland_mv30_state::update_ready()
{
	floppy_image_device *drive = m_floppy->get_device();
	m_fdc->ready_w(!(drive && drive->exists()));
}

void roland_mv30_state::program_map(address_map &map)
{
	map.unmap_value_high();
	map(0x0100, 0x011f).rw(FUNC(roland_mv30_state::gate_r), FUNC(roland_mv30_state::gate_w));
	map(0x0120, 0xffff)
		.lrw16([this](offs_t offset, u16 mem_mask) { return memory_word_r(true, 0x120 + offset * 2, mem_mask); }, "program_r",
			[this](offs_t offset, u16 data, u16 mem_mask) { memory_word_w(true, 0x120 + offset * 2, data, mem_mask); }, "program_w");
}

void roland_mv30_state::data_map(address_map &map)
{
	map.unmap_value_high();
	map(0x0100, 0x011f).rw(FUNC(roland_mv30_state::gate_r), FUNC(roland_mv30_state::gate_w));
	map(0x0120, 0xffff)
		.lrw16([this](offs_t offset, u16 mem_mask) { return memory_word_r(false, 0x120 + offset * 2, mem_mask); }, "data_r",
			[this](offs_t offset, u16 data, u16 mem_mask) { memory_word_w(false, 0x120 + offset * 2, data, mem_mask); }, "data_w");
}

void roland_mv30_state::lcd_map(address_map &map)
{
	map(0, 0x7fff).ram();
}

void roland_mv30_state::palette(palette_device &palette) const
{
	palette.set_pen_color(0, rgb_t(0xb0, 0xba, 0x9c));
	palette.set_pen_color(1, rgb_t(0x30, 0x40, 0x30));
}

void roland_mv30_state::floppy_formats(format_registration &fr)
{
	fr.add_mfm_containers();
	fr.add(FLOPPY_ROLAND_SDISK_FORMAT);
}

void roland_mv30_state::floppies(device_slot_interface &device)
{
	device.option_add("35dd", FLOPPY_35_DD);
}

void roland_mv30_state::mv30(machine_config &config)
{
	I80C196KB(config, m_cpu, 12_MHz_XTAL);
	m_cpu->set_addrmap(AS_PROGRAM, &roland_mv30_state::program_map);
	m_cpu->set_addrmap(AS_IO, &roland_mv30_state::data_map);
	m_cpu->in_p0_cb().set(FUNC(roland_mv30_state::port0_r));
	m_cpu->in_p1_cb().set(FUNC(roland_mv30_state::port1_r));
	m_cpu->out_p1_cb().set(FUNC(roland_mv30_state::port1_w));
	m_cpu->in_p2_cb().set(FUNC(roland_mv30_state::port2_r));
	m_cpu->out_p2_cb().set(FUNC(roland_mv30_state::port2_w));
	m_cpu->hso_cb().set(FUNC(roland_mv30_state::hso_w));
	m_cpu->ach0_cb().set(FUNC(roland_mv30_state::slider_r));
	m_cpu->txd_cb().set("mdout", FUNC(midi_port_device::write_txd));
	config.set_default_layout(layout_roland_mv30);

	T6963C(config, m_lcd);
	m_lcd->set_addrmap(0, &roland_mv30_state::lcd_map);
	screen_device &screen(SCREEN(config, "screen").set_lcd());
	screen.set_refresh_hz(60);
	screen.set_size(240, 64);
	screen.set_visarea_full();
	screen.set_screen_update(m_lcd, FUNC(t6963c_device::screen_update));
	screen.set_palette("palette");
	PALETTE(config, "palette", FUNC(roland_mv30_state::palette), 2);

	UPD72068(config, m_fdc, 32_MHz_XTAL);
	m_fdc->intrq_wr_callback().set(FUNC(roland_mv30_state::fdc_irq_w));
	m_fdc->drq_wr_callback().set(FUNC(roland_mv30_state::fdc_drq_w));
	INPUT_MERGER_ANY_HIGH(config, m_tc).output_handler().set(m_fdc, FUNC(upd72068_device::tc_line_w));
	FLOPPY_CONNECTOR(config, m_floppy, floppies, "35dd", &floppy_formats);

	SPEAKER(config, "speaker", 2).front();
	MB87419_MB87420(config, m_pcm, 32.768_MHz_XTAL);
	m_pcm->int_callback().set(FUNC(roland_mv30_state::lp_irq_w));
	MB87424(config, m_tvf, 16.384_MHz_XTAL);
	m_tvf->set_input_bus(21);
	TC23SC140(config, m_rcc, 16.384_MHz_XTAL);
	m_rcc->set_dram_mask(0x3fff);
	m_rcc->set_input_bus(23, true);
	for (unsigned voice = 0; voice < 32; ++voice)
	{
		m_pcm->add_route(voice, "tvf", 1.0, voice);
		m_tvf->add_route(voice, "rcc", 1.0, voice);
	}
	// MV-30 firmware uses RCC slots 6/7 for MIX L/R (D-70 uses 6/0).
	m_rcc->add_route(6, "speaker", 1.0, 0);
	m_rcc->add_route(7, "speaker", 1.0, 1);
	// The metronome is mixed into PHONES, not the MIX/DIRECT line outputs.
	// The default stereo monitor represents the headphone connection.
	BEEP(config, m_metronome, 1320);
	FILTER_VOLUME(config, m_metronome_phone);
	m_metronome->add_route(0, "metronome_phone", 0.15);
	m_metronome_phone->add_route(0, "speaker", 1.0, 0);
	m_metronome_phone->add_route(0, "speaker", 1.0, 1);
	SPEAKER(config, "mix", 2).map_on_request_only(0).map_on_request_only(1);
	for (unsigned channel = 0; channel < 2; ++channel)
	{
		FILTER_VOLUME(config, m_mix_volume[channel]);
		m_rcc->add_route(channel ? 7 : 6, m_mix_volume[channel], 1.0);
		m_mix_volume[channel]->add_route(0, "mix", 1.0, channel);
	}
	SPEAKER(config, "metronome_out").map_on_request_only();
	m_metronome->add_route(0, "metronome_out", 0.15);

	generic_cartslot_device &card0(GENERIC_CARTSLOT(config, m_cards[0], generic_plain_slot, "u110_card", "bin"));
	card0.set_device_load(FUNC(roland_mv30_state::card0_load));
	card0.set_device_unload(FUNC(roland_mv30_state::card0_unload));
	card0.set_default_option("rom");
	card0.set_fixed(true);
	generic_cartslot_device &card1(GENERIC_CARTSLOT(config, m_cards[1], generic_plain_slot, "u110_card", "bin"));
	card1.set_device_load(FUNC(roland_mv30_state::card1_load));
	card1.set_device_unload(FUNC(roland_mv30_state::card1_unload));
	card1.set_default_option("rom");
	card1.set_fixed(true);
	SOFTWARE_LIST(config, "card_list").set_original("u110_card");

	midi_port_device &input(MIDI_PORT(config, "mdin", midiin_slot, "midiin"));
	input.rxd_handler().set(m_cpu, FUNC(i80c196kb_device::rxd_w));
	input.rxd_handler().append("mdthru", FUNC(midi_port_device::write_txd));
	MIDI_PORT(config, "mdout", midiout_slot, "midiout");
	MIDI_PORT(config, "mdthru", midiout_slot, "midiout");
}

// Both card sockets carry VTA0..18 and VTD0..7, sharing the waveform-ROM
// wiring. Card-board CS1/CS2 select the two vacant 512 KiB LP banks. SENS is
// pulled low by an inserted card and reaches HSI2 (A) / HSI1 (B).
std::pair<std::error_condition, std::string> roland_mv30_state::card_load(unsigned slot, device_image_interface &image)
{
	const u32 size = m_cards[slot]->common_get_size("rom");
	if (size < 0x20000 || size > 0x80000 || (size & (size - 1)))
		return std::make_pair(image_error::INVALIDLENGTH, "PCM cards must contain 128, 256 or 512 KiB");
	if (!m_cards[slot]->get_rom_base())
		m_cards[slot]->rom_alloc(0x80000, GENERIC_ROM8_WIDTH, ENDIANNESS_LITTLE);
	m_cards[slot]->common_load_rom(m_cards[slot]->get_rom_base(), size, "rom");
	m_pcm->read(0); // Finish audio using the old ROM before changing the shared bus.
	const u32 base = slot ? 0x380000 : 0x280000;
	u8 *raw = memregion("pcmorg")->base() + base;
	u8 *decoded = memregion("pcm")->base() + base;
	for (u32 offset = 0; offset < 0x80000; ++offset)
	{
		raw[offset] = m_cards[slot]->get_rom_base()[offset & (size - 1)];
		decoded[bitswap<19>(offset, 18, 17, 15, 14, 16, 12, 11, 7, 9, 13, 10, 8, 3, 2, 1, 6, 4, 5, 0)]
			= bitswap<8>(raw[offset], 1, 2, 7, 3, 5, 0, 4, 6);
	}
	m_card_loaded[slot] = true;
	m_cpu->set_input_line(slot ? i8x9x_device::HSI1_LINE : i8x9x_device::HSI2_LINE, CLEAR_LINE);
	return std::make_pair(std::error_condition(), std::string());
}

void roland_mv30_state::card_unload(unsigned slot)
{
	m_pcm->read(0);
	const u32 base = slot ? 0x380000 : 0x280000;
	std::fill_n(memregion("pcmorg")->base() + base, 0x80000, 0xff);
	std::fill_n(memregion("pcm")->base() + base, 0x80000, 0xff);
	m_card_loaded[slot] = false;
	m_cpu->set_input_line(slot ? i8x9x_device::HSI1_LINE : i8x9x_device::HSI2_LINE, ASSERT_LINE);
}

void roland_mv30_state::init_mv30()
{
	// Identical waveform set and address/data wiring to the D-70.
	const u8 *source = memregion("pcmorg")->base();
	u8 *destination = memregion("pcm")->base();
	for (const u32 base : { 0x000000U, 0x080000U, 0x100000U, 0x180000U, 0x200000U, 0x300000U })
		for (u32 offset = 0; offset < 0x80000; ++offset)
			destination[base + bitswap<19>(offset, 18, 17, 15, 14, 16, 12, 11, 7, 9, 13, 10, 8, 3, 2, 1, 6, 4, 5, 0)]
				= bitswap<8>(source[base + offset], 1, 2, 7, 3, 5, 0, 4, 6);
}
static INPUT_PORTS_START(mv30)
	PORT_START("KEY0")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Pattern Edit") PORT_CODE(KEYCODE_P)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Up") PORT_CODE(KEYCODE_UP)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Enter") PORT_CODE(KEYCODE_ENTER)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Ctrl")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("0") PORT_CODE(KEYCODE_0)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Track 1/9")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Jump") PORT_CODE(KEYCODE_TAB)

	PORT_START("KEY1")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Track Edit") PORT_CODE(KEYCODE_T)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Left") PORT_CODE(KEYCODE_LEFT)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("3") PORT_CODE(KEYCODE_3)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Compu")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("2") PORT_CODE(KEYCODE_2)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Track 2/10")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("F1") PORT_CODE(KEYCODE_F1)

	PORT_START("KEY2")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("System") PORT_CODE(KEYCODE_S)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Right") PORT_CODE(KEYCODE_RIGHT)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("6") PORT_CODE(KEYCODE_6)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Manual")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("5") PORT_CODE(KEYCODE_5)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Track 3/11")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("F2") PORT_CODE(KEYCODE_F2)

	PORT_START("KEY3")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Disk") PORT_CODE(KEYCODE_D)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Down") PORT_CODE(KEYCODE_DOWN)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("9") PORT_CODE(KEYCODE_9)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Song Select")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("8") PORT_CODE(KEYCODE_8)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Track 4/12")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("F3") PORT_CODE(KEYCODE_F3)

	PORT_START("KEY4")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Pattern Microscope")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Pattern Real Time")
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Insert") PORT_CODE(KEYCODE_INSERT)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Record")
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Status")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("1") PORT_CODE(KEYCODE_1)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Track 5/13")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("F4") PORT_CODE(KEYCODE_F4)

	PORT_START("KEY5")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Track Microscope")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Track Real Time")
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Delete") PORT_CODE(KEYCODE_DEL)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Measure Dec")
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Locate")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("4") PORT_CODE(KEYCODE_4)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Track 6/14")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("F5") PORT_CODE(KEYCODE_F5)

	PORT_START("KEY6")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Timbre Edit")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Compu Mix")
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Start/Stop") PORT_CODE(KEYCODE_SPACE)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Mark")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("7") PORT_CODE(KEYCODE_7)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Track 7/15")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Exit") PORT_CODE(KEYCODE_ESC)

	PORT_START("KEY7")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Chain Load")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Play")
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Measure Inc")
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Tempo")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Shift") PORT_CODE(KEYCODE_LSHIFT)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Track 8/16")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)


	PORT_START("FADER0")
	PORT_ADJUSTER(100, "Track 1 Volume")

	PORT_START("FADER1")
	PORT_ADJUSTER(100, "Track 2 Volume")

	PORT_START("FADER2")
	PORT_ADJUSTER(100, "Track 3 Volume")

	PORT_START("FADER3")
	PORT_ADJUSTER(100, "Track 4 Volume")

	PORT_START("FADER4")
	PORT_ADJUSTER(100, "Track 5 Volume")

	PORT_START("FADER5")
	PORT_ADJUSTER(100, "Track 6 Volume")

	PORT_START("FADER6")
	PORT_ADJUSTER(100, "Track 7 Volume")

	PORT_START("FADER7")
	PORT_ADJUSTER(100, "Track 8 Volume")

	PORT_START("FOOT")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Footswitch") PORT_CODE(KEYCODE_LCONTROL)

	PORT_START("METLEVEL")
	PORT_ADJUSTER(100, "Metronome Level") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(roland_mv30_state::metronome_level_changed), 0)

	PORT_START("DIAL")
	PORT_BIT(0xff, 0, IPT_DIAL) PORT_NAME("Value") PORT_SENSITIVITY(25) PORT_KEYDELTA(1) PORT_CODE_DEC(KEYCODE_MINUS) PORT_CODE_INC(KEYCODE_EQUALS)

	PORT_START("VALUE")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Value -") PORT_CODE(KEYCODE_PGDN)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Value +") PORT_CODE(KEYCODE_PGUP)

	PORT_START("VOLUME")
	PORT_ADJUSTER(100, "Volume") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(roland_mv30_state::volume_changed), 0)
INPUT_PORTS_END

ROM_START(mv30)
	ROM_REGION(0x4000, "bootrom", 0)
	ROM_LOAD("mv30.bin", 0, 0x4000, CRC(15cc9474) SHA1(dc09c839c774e8190c0dbfaa3f2fce3049be735e))

	ROM_REGION(0x600000, "pcmorg", ROMREGION_ERASEFF)
	ROM_LOAD("roland_d70_waverom-a.bin", 0x000000, 0x80000, CRC(8e53b2a3) SHA1(4872530870d5079776e80e477febe425dc0ec1df))
	ROM_LOAD("roland_d70_waverom-e.bin", 0x080000, 0x80000, CRC(d46cc7a4) SHA1(d378ac89a5963e37f7c157b3c8e71892c334fd7b))
	ROM_LOAD("roland_d70_waverom-b.bin", 0x100000, 0x80000, CRC(c8220761) SHA1(49e55fa672020f95fd9c858ceaae94d6db93df7d))
	ROM_LOAD("roland_d70_waverom-f.bin", 0x180000, 0x80000, CRC(d4b01f5e) SHA1(acd867d68e49e5f59f1006ed14a7ca197b6dc4af))
	ROM_LOAD("roland_d70_waverom-c.bin", 0x200000, 0x80000, CRC(733c4054) SHA1(9b6b59ab74e5bf838702abb087c408aaa85b7b1f))
	ROM_LOAD("roland_d70_waverom-d.bin", 0x300000, 0x80000, CRC(b6c662d2) SHA1(3fcbcfd0d8d0fa419c710304c12482e2f79a907f))
	ROM_REGION(0x600000, "pcm", ROMREGION_ERASEFF)

	ROM_REGION(0x400, "lcd:cgrom", 0)
	ROM_LOAD("t6963c_0101.bin", 0, 0x400, CRC(547d118b) SHA1(0dd3e3acd3d47e6ece644c98c390fc86587373e9))
ROM_END

} // anonymous namespace

SYST(1990, mv30, 0, 0, mv30, mv30, roland_mv30_state, init_mv30, "Roland", "MV-30 Studio M", MACHINE_IMPERFECT_SOUND)
