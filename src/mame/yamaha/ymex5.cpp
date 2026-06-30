// license:BSD-3-Clause
// copyright-holders:Giulio Zausa
/***************************************************************************

    Yamaha EX5R music synthesizer

    Main board:
      IC1   Fujitsu MB91103 (FR20), 20 MHz
      IC7   Fujitsu MB91103 (FR20), 20 MHz
      IC44  Yamaha SWP30B (master), 33.8688 MHz
      IC45  Yamaha SWP30B (slave),  33.8688 MHz

    The two 27C800 pairs are arranged as two 16-bit halves of a 32-bit
    FR20 program bus.  IC10 (sub-CPU flash) and the four wave ROMs have not
    been dumped.  The mask-ROM firmware nevertheless contains a complete
    reset vector and boots independently of IC10.

***************************************************************************/

#include "emu.h"

#include "cpu/fr/fr.h"
#include "machine/intelfsh.h"
#include "sound/swp30.h"
#include "video/sed1330.h"

#include "emupal.h"
#include "emuopts.h"
#include "screen.h"
#include "speaker.h"

#include "ex5r.lh"

namespace {

class ex5_state : public driver_device
{
public:
	ex5_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_subcpu(*this, "subcpu")
		, m_swp30m(*this, "swp30m")
		, m_swp30s(*this, "swp30s")
		, m_subflash(*this, "subflash")
		, m_lcdc(*this, "lcdc")
		, m_knobs(*this, "KNOB%u", 1U)
		, m_main_to_sub(0)
		, m_sub_to_main(0)
		, m_main_wait(false)
		, m_sub_wait(false)
	{
	}

	void ex5r(machine_config &config);
	DECLARE_INPUT_CHANGED_MEMBER(encoder_changed);
	DECLARE_INPUT_CHANGED_MEMBER(panel_logical_changed);

private:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	required_device<mb91103_device> m_maincpu;
	required_device<mb91103_device> m_subcpu;
	required_device<swp30_device> m_swp30m;
	required_device<swp30_device> m_swp30s;
	required_device<fujitsu_29f800b_16bit_device> m_subflash;
	required_device<sed1330_device> m_lcdc;
	required_ioport_array<6> m_knobs;
	u32 m_main_to_sub;
	u32 m_sub_to_main;
	bool m_main_wait;
	bool m_sub_wait;
	std::array<u32, 0x4000> m_wave_glue;
	std::array<u8, 16> m_panel_command;
	u8 m_panel_command_count = 0;
	u8 m_panel_control = 0xff;
	u8 m_subflash_mode = 0;

	u32 main_cpubus_r();
	void main_cpubus_w(offs_t offset, u32 data, u32 mem_mask = ~0U);
	u32 sub_cpubus_r();
	void sub_cpubus_w(offs_t offset, u32 data, u32 mem_mask = ~0U);
	u16 subflash_hle_r(offs_t offset);
	void subflash_hle_w(offs_t offset, u16 data);
	u8 main_portf_r();
	u8 sub_portf_r();
	u8 main_portg_r();
	u8 sub_portg_r();
	u8 panel_status_r();
	u8 panel_data_r();
	void panel_data_w(u8 data);
	u8 panel_control_r();
	void panel_control_w(u8 data);
	u32 wave_glue_r(offs_t offset);
	void wave_glue_w(offs_t offset, u32 data, u32 mem_mask = ~0U);
	void main_map(address_map &map) ATTR_COLD;
	void sub_map(address_map &map) ATTR_COLD;
	void wave_map(address_map &map) ATTR_COLD;
	void lcdc_map(address_map &map) ATTR_COLD;
	void palette_init(palette_device &palette);
};

void ex5_state::machine_start()
{
	save_item(NAME(m_main_to_sub));
	save_item(NAME(m_sub_to_main));
	save_item(NAME(m_main_wait));
	save_item(NAME(m_sub_wait));
	save_item(NAME(m_wave_glue));
	save_item(NAME(m_panel_command));
	save_item(NAME(m_panel_command_count));
	save_item(NAME(m_panel_control));
	save_item(NAME(m_subflash_mode));
}

void ex5_state::machine_reset()
{
	m_main_to_sub = 0;
	m_sub_to_main = 0;
	m_main_wait = false;
	m_sub_wait = false;
	std::fill(m_wave_glue.begin(), m_wave_glue.end(), 0);
	m_panel_command_count = 0;
	m_panel_control = 0xff;
	m_subflash_mode = 0;
}

u8 ex5_state::panel_status_r()
{
	// PKST[7:4] value 8 tells the main CPU that the panel can accept a byte.
	return 0x80;
}

u8 ex5_state::panel_data_r()
{
	return 0xff;
}

void ex5_state::panel_data_w(u8 data)
{
	if (m_panel_command_count < m_panel_command.size())
		m_panel_command[m_panel_command_count++] = data;
}

u8 ex5_state::panel_control_r()
{
	// Bit 7 reports a panel-interface fault; keep it clear for the HLE scanner.
	return 0x00;
}

void ex5_state::panel_control_w(u8 data)
{
	// PKSC is a control/status register, not a packet terminator.  Dn/En writes
	// bracket the main CPU's panel transactions.
	m_panel_control = data;
	m_panel_command_count = 0;
}

u32 ex5_state::main_cpubus_r()
{
	m_sub_wait = false;
	return m_sub_to_main;
}

void ex5_state::main_cpubus_w(offs_t, u32 data, u32 mem_mask)
{
	COMBINE_DATA(&m_main_to_sub);
	m_main_wait = true;
	m_subcpu->external_interrupt(0);
}

u32 ex5_state::sub_cpubus_r()
{
	m_main_wait = false;
	return m_main_to_sub;
}

void ex5_state::sub_cpubus_w(offs_t, u32 data, u32 mem_mask)
{
	COMBINE_DATA(&m_sub_to_main);
	m_sub_wait = true;
	m_maincpu->external_interrupt(4);
}

u16 ex5_state::subflash_hle_r(offs_t offset)
{
	if (m_subflash_mode == 7)
	{
		if (offset == 0)
			return 0x0004; // Fujitsu
		if (offset == 1)
			return 0x2258; // MBM29F800B
	}

	const u8 *const base = m_subflash->base();
	return (u16(base[offset * 2]) << 8) | base[offset * 2 + 1];
}

void ex5_state::subflash_hle_w(offs_t offset, u16 data)
{
	// IC10 is 16 bits wide on the FR20's 32-bit external bus.  MAME's generic
	// flash device cannot currently observe the FR20's halfword transactions,
	// so decode the small AMD command set used by the TG ROM at this bus layer.
	if (m_subflash_mode != 3 && data == 0x00f0)
	{
		m_subflash_mode = 0;
		return;
	}

	switch (m_subflash_mode)
	{
	case 0:
		m_subflash_mode = (offset == 0x5555 && data == 0x00aa) ? 1 : 0;
		break;
	case 1:
		m_subflash_mode = (offset == 0x2aaa && data == 0x0055) ? 2 : 0;
		break;
	case 2:
		if (offset != 0x5555)
			m_subflash_mode = 0;
		else if (data == 0x00a0)
			m_subflash_mode = 3;
		else if (data == 0x0080)
			m_subflash_mode = 4;
		else if (data == 0x0090)
			m_subflash_mode = 7;
		else
			m_subflash_mode = 0;
		break;
	case 3:
	{
		u8 *const base = m_subflash->base();
		base[offset * 2] &= data >> 8;
		base[offset * 2 + 1] &= data;
		m_subflash_mode = 0;
		break;
	}
	case 4:
		m_subflash_mode = (offset == 0x5555 && data == 0x00aa) ? 5 : 0;
		break;
	case 5:
		m_subflash_mode = (offset == 0x2aaa && data == 0x0055) ? 6 : 0;
		break;
	case 6:
		if (data == 0x0030)
		{
			const u32 byte_address = u32(offset) * 2;
			u32 base_address;
			u32 size;
			if (byte_address < 0x4000) { base_address = 0; size = 0x4000; }
			else if (byte_address < 0x6000) { base_address = 0x4000; size = 0x2000; }
			else if (byte_address < 0x8000) { base_address = 0x6000; size = 0x2000; }
			else if (byte_address < 0x10000) { base_address = 0x8000; size = 0x8000; }
			else { base_address = byte_address & ~0xffffU; size = 0x10000; }
			std::fill_n(m_subflash->base() + base_address, size, 0xff);
		}
		else if (offset == 0x5555 && data == 0x0010)
			std::fill_n(m_subflash->base(), 0x100000, 0xff);
		m_subflash_mode = 0;
		break;
	case 7:
		m_subflash_mode = (offset == 0x5555 && data == 0x00aa) ? 1 : 7;
		break;
	}
}

u8 ex5_state::main_portf_r()
{
	return 0xfd | (m_main_wait ? 0x02 : 0x00);
}

u8 ex5_state::sub_portf_r()
{
	return 0xfd | (m_sub_wait ? 0x02 : 0x00);
}

u8 ex5_state::main_portg_r()
{
	// The bus is 16 bits wide; PG0 carries the framing/data bit 16.
	return 0xfe | BIT(m_sub_to_main, 16);
}

u8 ex5_state::sub_portg_r()
{
	return 0xfe | BIT(m_main_to_sub, 16);
}

u32 ex5_state::wave_glue_r(offs_t offset)
{
	return m_wave_glue[offset];
}

void ex5_state::wave_glue_w(offs_t offset, u32 data, u32 mem_mask)
{
	u32 &value = m_wave_glue[offset];
	const u32 old = value;
	COMBINE_DATA(&value);
	if (machine().options().verbose() && offset < 2 && old != value)
		logerror("%s: wave glue %06x %08x -> %08x (mask %08x)\n", machine().describe_context(), 0x800000 + offset * 4, old, value, mem_mask);
}

void ex5_state::main_map(address_map &map)
{
	// CS0: two 8-Mbit x16 program ROMs.  The low window is required for
	// the fixed reset vector; firmware subsequently relocates TBR to 2ffc00.
	map(0x000800, 0x1fffff).rom().region("maincpu", 0x000800);
	map(0x200000, 0x3fffff).rom().region("maincpu", 0x000000);

	// CS4: main-to-sub and sub-to-main latches.  The physical interface has
	// independent directions; writes signal sub-CPU INT0.
	map(0x580000, 0x580003).rw(FUNC(ex5_state::main_cpubus_r), FUNC(ex5_state::main_cpubus_w));

	// CS5: IC5, 4-Mbit x16 DRAM.  The firmware stack starts at 700b60
	// and its BSS/data areas occupy 703400-75ae23.
	map(0x700000, 0x77ffff).ram();

	// CS2: external working buffer used by the ADC/DMA and control-panel
	// processing code.  Firmware uses offsets through at least 7f58.
	map(0x0c0000, 0x0cffff).ram();

	// CS3: HD63B01Y panel scanner (PKS).  It scans all front-panel
	// switches and LEDs and exchanges framed bytes with the main CPU.
	map(0x0e0000, 0x0e0000).r(FUNC(ex5_state::panel_status_r));
	map(0x0e0001, 0x0e0001).rw(FUNC(ex5_state::panel_data_r), FUNC(ex5_state::panel_data_w));
	map(0x0e0002, 0x0e0002).rw(FUNC(ex5_state::panel_control_r), FUNC(ex5_state::panel_control_w));

	// CS3: PLS board, SED1335-compatible LCD controller.  The firmware
	// initializes the command/status and data ports at 0e8000/0e8001.
	map(0x0e8000, 0x0e8000).rw(m_lcdc, FUNC(sed1330_device::status_r), FUNC(sed1330_device::data_w));
	map(0x0e8001, 0x0e8001).rw(m_lcdc, FUNC(sed1330_device::data_r), FUNC(sed1330_device::command_w));
}

void ex5_state::sub_map(address_map &map)
{
	// IC8/IC9 TG firmware ROMs use the same reset-vector mirror as main.
	map(0x000800, 0x1fffff).rom().region("subcpu", 0x000800);
	map(0x200000, 0x3fffff).rom().region("subcpu", 0x000000);

	// CS1: other side of the two communication latches.  Writes signal main INT4.
	map(0x480000, 0x480003).rw(FUNC(ex5_state::sub_cpubus_r), FUNC(ex5_state::sub_cpubus_w));

	// CS2: the two SWP30B control buses.  Firmware accesses the chips at
	// 500000 and 580000, including registers through offset 109e.
	map(0x500000, 0x501fff).m(m_swp30m, FUNC(swp30_device::map));
	map(0x580000, 0x581fff).m(m_swp30s, FUNC(swp30_device::map));

	// CS3: IC10, a Fujitsu MBM29F800B-90 8-Mbit bottom-boot flash.
	map(0x600000, 0x6fffff).lrw16(
		NAME([this](offs_t offset) { return subflash_hle_r(offset * 2); }),
		NAME([this](offs_t offset, u16 data) { subflash_hle_w(offset * 2, data); })).umask32(0xffff0000);
	map(0x600000, 0x6fffff).lrw16(
		NAME([this](offs_t offset) { return subflash_hle_r(offset * 2 + 1); }),
		NAME([this](offs_t offset, u16 data) { subflash_hle_w(offset * 2 + 1, data); })).umask32(0x0000ffff);

	// CS4: wave/SWP glue aperture.  The first eight bytes are configuration
	// latches used together with SWP control 1a.  Keep the entire 64 KiB
	// aperture stateful because the firmware also probes it as wave memory.
	map(0x800000, 0x80ffff).rw(FUNC(ex5_state::wave_glue_r), FUNC(ex5_state::wave_glue_w));

	// CS5: IC11, 4-Mbit x16 DRAM; reset stack is at a00c40.
	map(0xa00000, 0xa7ffff).ram();
}

void ex5_state::wave_map(address_map &map)
{
	// IC51-IC54 are four 32-Mbit ROMs on a 32-bit bus (16 MiB total).
	// IC55/IC56 form the 1 MiB on-board wave DRAM.  SWP addresses here
	// are dword addresses because AS_DATA has a -2 address shift.
	map(0x000000, 0x3fffff).rom().region("wave", 0);
	map(0x1000000, 0x103ffff).ram().share("wave_ram");
}

void ex5_state::lcdc_map(address_map &map)
{
	map(0x0000, 0x7fff).ram(); // IC3, 256-Kbit display SRAM
	map(0xf000, 0xf5bf).rom().region("lcdc:gfx1", 0); // internal character generator
}

void ex5_state::palette_init(palette_device &palette)
{
	palette.set_pen_color(0, rgb_t(0x25, 0x34, 0x25));
	palette.set_pen_color(1, rgb_t(0xb8, 0xc8, 0x8c));
}

INPUT_CHANGED_MEMBER(ex5_state::encoder_changed)
{
	const s8 delta = s8(newval - oldval);
	for (unsigned step = 0; step < std::abs(int(delta)); step++)
	{
		if (delta > 0)
		{
			// One clockwise quadrature cycle.  The firmware selects x2
			// mode and divides UDCR0 by two, yielding one menu step.
			m_maincpu->udc_input(0, 0, 1);
			m_maincpu->udc_input(0, 1, 1);
			m_maincpu->udc_input(0, 0, 0);
			m_maincpu->udc_input(0, 1, 0);
		}
		else
		{
			m_maincpu->udc_input(0, 1, 1);
			m_maincpu->udc_input(0, 0, 1);
			m_maincpu->udc_input(0, 1, 0);
			m_maincpu->udc_input(0, 0, 0);
		}
	}
}

INPUT_CHANGED_MEMBER(ex5_state::panel_logical_changed)
{
	// The HD63B01Y panel scanner ROM has not been dumped, so its translation
	// from the physical switch matrix to Yamaha's logical key numbers cannot
	// be emulated yet.  Feed the scanner's translated state into the firmware-
	// owned 80-bit switch bitmap; the ROM then performs its normal debounce,
	// edge detection and event generation (including in the service tests).
	const unsigned index = param;
	const offs_t address = 0x758224 + 2 * (index / 16);
	const u16 mask = u16(1U << (index % 16));
	address_space &space = m_maincpu->space(AS_PROGRAM);
	u16 value = space.read_word(address);
	value = newval ? (value | mask) : (value & ~mask);
	space.write_word(address, value);

	if (machine().options().verbose())
		logerror("%s: panel logical switch %u %s\n", machine().describe_context(), index, newval ? "pressed" : "released");
}

static INPUT_PORTS_START(ex5r)
	PORT_START("PANEL0")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("SC2")          PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 57)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("SC1")          PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 56)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("VOICE")        PORT_CODE(KEYCODE_V) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 58)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("PERFORM")      PORT_CODE(KEYCODE_P) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 59)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("SONG")         PORT_CODE(KEYCODE_S) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 60)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("PATTERN")      PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 61)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("SAMPLE")       PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 62)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("UTILITY")      PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 63)

	PORT_START("PANEL1")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("DISK")         PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 64)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("EDIT/COMPARE") PORT_CODE(KEYCODE_E) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 65)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("JOB")          PORT_CODE(KEYCODE_J) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 66)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("STORE")        PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 67)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("ARPEGGIO")     PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 68)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("KNOB MODE")    PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 69)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("TOP")          PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 72)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("REW")          PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 73)

	PORT_START("PANEL2")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("FWD")          PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 74)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("KEY MAP")      PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 70)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("BYPASS")       PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 71)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("REC")          PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 75)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("STOP")         PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 76)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("PLAY")         PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 77)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("SHIFT")        PORT_CODE(KEYCODE_LSHIFT) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 45)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("F1")           PORT_CODE(KEYCODE_F1) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 0)

	PORT_START("PANEL3")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("F2")           PORT_CODE(KEYCODE_F2) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 1)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("F3")           PORT_CODE(KEYCODE_F3) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 2)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("F4")           PORT_CODE(KEYCODE_F4) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 3)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("F5")           PORT_CODE(KEYCODE_F5) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 4)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("F6")           PORT_CODE(KEYCODE_F6) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 5)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("F7")           PORT_CODE(KEYCODE_F7) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 6)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("F8")           PORT_CODE(KEYCODE_F8) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 7)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("EXIT")         PORT_CODE(KEYCODE_ESC) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 44)

	PORT_START("PANEL4")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("CANCEL")       PORT_CODE(KEYCODE_BACKSPACE) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 55)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("CURSOR/DATA")  PORT_CODE(KEYCODE_TAB) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 54)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("DEC/NO")       PORT_CODE(KEYCODE_MINUS) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 48)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Cursor Up")    PORT_CODE(KEYCODE_UP) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 53)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("INC/YES")      PORT_CODE(KEYCODE_EQUALS) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 49)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Cursor Left")  PORT_CODE(KEYCODE_LEFT) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 51)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Cursor Down")  PORT_CODE(KEYCODE_DOWN) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 52)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Cursor Right") PORT_CODE(KEYCODE_RIGHT) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 50)

	PORT_START("PANEL5")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("7")            PORT_CODE(KEYCODE_7) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 39)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("8")            PORT_CODE(KEYCODE_8) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 40)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("9")            PORT_CODE(KEYCODE_9) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 41)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("4")            PORT_CODE(KEYCODE_4) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 36)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("5")            PORT_CODE(KEYCODE_5) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 37)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("6")            PORT_CODE(KEYCODE_6) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 38)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("1")            PORT_CODE(KEYCODE_1) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 33)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("2")            PORT_CODE(KEYCODE_2) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 34)

	PORT_START("PANEL6")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("3")            PORT_CODE(KEYCODE_3) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 35)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("0")            PORT_CODE(KEYCODE_0) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 32)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Keypad -")     PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 42)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("ENTER")        PORT_CODE(KEYCODE_ENTER) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::panel_logical_changed), 43)
	PORT_BIT(0xf0, IP_ACTIVE_HIGH, IPT_UNUSED)

	PORT_START("KNOB1")
	PORT_BIT(0x3ff, 0x200, IPT_PADDLE) PORT_NAME("Controller Knob 1") PORT_MINMAX(0, 0x3ff) PORT_SENSITIVITY(50) PORT_KEYDELTA(8)
	PORT_START("KNOB2")
	PORT_BIT(0x3ff, 0x200, IPT_PADDLE) PORT_NAME("Controller Knob 2") PORT_MINMAX(0, 0x3ff) PORT_SENSITIVITY(50) PORT_KEYDELTA(8)
	PORT_START("KNOB3")
	PORT_BIT(0x3ff, 0x200, IPT_PADDLE) PORT_NAME("Controller Knob 3") PORT_MINMAX(0, 0x3ff) PORT_SENSITIVITY(50) PORT_KEYDELTA(8)
	PORT_START("KNOB4")
	PORT_BIT(0x3ff, 0x200, IPT_PADDLE) PORT_NAME("Controller Knob 4") PORT_MINMAX(0, 0x3ff) PORT_SENSITIVITY(50) PORT_KEYDELTA(8)
	PORT_START("KNOB5")
	PORT_BIT(0x3ff, 0x200, IPT_PADDLE) PORT_NAME("Controller Knob 5") PORT_MINMAX(0, 0x3ff) PORT_SENSITIVITY(50) PORT_KEYDELTA(8)
	PORT_START("KNOB6")
	PORT_BIT(0x3ff, 0x200, IPT_PADDLE) PORT_NAME("Controller Knob 6") PORT_MINMAX(0, 0x3ff) PORT_SENSITIVITY(50) PORT_KEYDELTA(8)

	PORT_START("encoder")
	PORT_BIT(0xff, 0x00, IPT_POSITIONAL) PORT_NAME("Data Dial") PORT_POSITIONS(256) PORT_WRAPS
		PORT_SENSITIVITY(25) PORT_KEYDELTA(1)
		PORT_CODE_DEC(KEYCODE_OPENBRACE) PORT_CODE_INC(KEYCODE_CLOSEBRACE) PORT_FULL_TURN_COUNT(24)
		PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ex5_state::encoder_changed), 0)

INPUT_PORTS_END

void ex5_state::ex5r(machine_config &config)
{
	MB91103(config, m_maincpu, 20_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &ex5_state::main_map);
	m_maincpu->read_port<10>().set(FUNC(ex5_state::main_portf_r));
	m_maincpu->read_port<11>().set(FUNC(ex5_state::main_portg_r));
	m_maincpu->read_adc<0>().set_ioport(m_knobs[0]);
	m_maincpu->read_adc<1>().set_ioport(m_knobs[1]);
	m_maincpu->read_adc<2>().set_ioport(m_knobs[2]);
	m_maincpu->read_adc<3>().set_ioport(m_knobs[3]);
	m_maincpu->read_adc<4>().set_ioport(m_knobs[4]);
	m_maincpu->read_adc<5>().set_ioport(m_knobs[5]);
	// AN6 monitors the backed-up SRAM battery.  An unbound ADC input reads as
	// zero and makes healthy firmware report "Change internal battery!".
	m_maincpu->read_adc<6>().set_constant(0x3ff);
	// The common EX5/EX7/EX5R firmware identifies the model from AN7.
	// Values above 0x300 select the rack-mount EX5R configuration.
	m_maincpu->read_adc<7>().set_constant(0x3ff);

	MB91103(config, m_subcpu, 20_MHz_XTAL);
	m_subcpu->set_addrmap(AS_PROGRAM, &ex5_state::sub_map);
	m_subcpu->read_port<10>().set(FUNC(ex5_state::sub_portf_r));
	m_subcpu->read_port<11>().set(FUNC(ex5_state::sub_portg_r));
	FUJITSU_29F800B_16BIT(config, m_subflash);

	SWP30(config, m_swp30m, 33.8688_MHz_XTAL);
	m_swp30m->set_addrmap(AS_DATA, &ex5_state::wave_map);

	SWP30(config, m_swp30s, 33.8688_MHz_XTAL);
	m_swp30s->set_addrmap(AS_DATA, &ex5_state::wave_map);
	for (unsigned channel = 0; channel < 16; channel++)
	{
		m_swp30m->add_route(4 + channel, m_swp30s, 1.0, channel);
		m_swp30s->add_route(4 + channel, m_swp30m, 1.0, channel);
	}

	SPEAKER(config, "mainout", 2).front();
	SPEAKER(config, "individualout", 2).front();
	m_swp30m->add_route(0, "mainout", 0.5, 0);
	m_swp30m->add_route(1, "mainout", 0.5, 1);
	m_swp30m->add_route(2, "individualout", 0.5, 0);
	m_swp30m->add_route(3, "individualout", 0.5, 1);

	PALETTE(config, "palette", FUNC(ex5_state::palette_init), 2);
	auto &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
	screen.set_refresh_hz(60);
	screen.set_size(240, 64);
	screen.set_visarea_full();
	screen.set_screen_update(m_lcdc, FUNC(sed1330_device::screen_update));
	screen.set_palette("palette");

	SED1330(config, m_lcdc, 8_MHz_XTAL); // IC4 is an SED1335-compatible controller
	m_lcdc->set_screen("screen");
	m_lcdc->set_addrmap(0, &ex5_state::lcdc_map);

	config.set_default_layout(layout_ex5r);
}

ROM_START(ex5r)
	ROM_REGION32_BE(0x200000, "maincpu", 0)
	ROM_LOAD32_WORD_SWAP("ex5r_main_h.bin", 0x000000, 0x100000, CRC(f47d2470) SHA1(d3c66a79932ff4df3d88ade724f710a5cc0e13c8))
	ROM_LOAD32_WORD_SWAP("ex5r_main_l.bin", 0x000002, 0x100000, CRC(d0bfbb58) SHA1(ea85bbaedde0490fc482bb6c6856b93036ce17d0))

	ROM_REGION32_BE(0x200000, "subcpu", 0)
	ROM_LOAD32_WORD_SWAP("ex5r_tg_h.bin", 0x000000, 0x100000, CRC(c6df84cd) SHA1(499549968d836996faf5acb06fb48003cd8f1d30))
	ROM_LOAD32_WORD_SWAP("ex5r_tg_l.bin", 0x000002, 0x100000, CRC(bb31da91) SHA1(ec17656e8b9ab08b129d8773b3cb3496f6fb6f21))

	ROM_REGION(0x100000, "subflash", ROMREGION_ERASEFF) // IC10, 8-Mbit x16 flash, undumped
	ROM_REGION32_LE(0x1000000, "wave", ROMREGION_ERASE00) // IC51-IC54, four 32-Mbit ROMs, undumped
ROM_END

} // anonymous namespace

SYST(1998, ex5r, 0, 0, ex5r, ex5r, ex5_state, empty_init, "Yamaha", "EX5R", MACHINE_NOT_WORKING | MACHINE_IMPERFECT_GRAPHICS | MACHINE_IMPERFECT_SOUND | MACHINE_SUPPORTS_SAVE)
