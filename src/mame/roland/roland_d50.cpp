// license:BSD-3-Clause
// copyright-holders:AJR
/****************************************************************************

    Skeleton driver for Roland D-50/D-550.

****************************************************************************/

#include "emu.h"
#include "emuopts.h"
#include "bus/midi/midiinport.h"
#include "cpu/upd78k/upd78k3.h"
#include "machine/bankdev.h"
#include "mb63h149.h"
#include "machine/nvram.h"
#include "sound/roland_d50fx.h"
#include "sound/roland_la32.h"
#include "speaker.h"
#include "emupal.h"
#include "screen.h"
#include "video/hd44780.h"


namespace {

class roland_d50_state : public driver_device
{
public:
	roland_d50_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_eram(*this, "eram")
		, m_lcd(*this, "lcd")
		, m_keyscan(*this, "keyscan")
		, m_la32(*this, "la32")
		, m_effects(*this, "effects")
		, m_toneram(*this, "toneram")
		, m_panel_rows(*this, "ROW%u", 0U)
	{
	}

	void d50(machine_config &config);
	void d550(machine_config &config);

private:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	void d50_mem_map(address_map &map) ATTR_COLD;
	void d550_mem_map(address_map &map) ATTR_COLD;
	void eram_map(address_map &map) ATTR_COLD;

	void lcd_palette(palette_device &palette) const;
	HD44780_PIXEL_UPDATE(pixel_update);

	uint8_t lcd_ctrl_r();
	void lcd_ctrl_w(uint8_t data);
	void lcd_data_w(uint8_t data);
	uint8_t gate_array_status_r();
	void gate_array_end_w(uint8_t data);
	void gate_array_w(offs_t offset, uint8_t data);
	uint8_t gate_array_input_r();
	void gate_array_input_w(uint8_t data);
	void port0_w(uint8_t data);
	void port1_w(uint8_t data);
	uint8_t port2_r();
	void update_eram_bank();
	void la32_irq_w(int state);
	void midi_rx_w(int state);

	void la32_w(offs_t address, uint8_t data);
	uint8_t la32_r(offs_t address);
	void chorus_w(offs_t address, uint8_t data);
	uint8_t chorus_r(offs_t address);
	void reverb_w(offs_t address, uint8_t data);
	uint8_t reverb_r(offs_t address);

	required_device<upd78312_device> m_maincpu;
	required_device<address_map_bank_device> m_eram;
	required_device<hd44780_device> m_lcd;
	optional_device<mb63h149_device> m_keyscan;
	required_device<la32_device> m_la32;
	required_device<roland_d50_effects_device> m_effects;
	required_shared_ptr<u8> m_toneram;
	optional_ioport_array<7> m_panel_rows;
	u8 m_gate_array_input = 0xff;
	u8 m_gate_array_registers[0x40]{};
	u16 m_gate_array_words[2][0x80]{};
	u8 m_port0 = 0xff;
	u8 m_port1 = 0xff;
	bool m_la32_irq = false;
	bool m_midi_idle = true;
	u8 m_midi_count = 0;
	u8 m_midi_byte = 0;
	u8 m_la32_data_l = 0;
	u16 m_la32_registers[6][32]{};
	u8 m_chorus_registers[8]{};
	u8 m_reverb_address = 0;
	u8 m_reverb_data_h = 0;
	u16 m_reverb_registers[256]{};
};

void roland_d50_state::machine_start()
{
	save_item(NAME(m_port0));
	save_item(NAME(m_port1));
	save_item(NAME(m_la32_irq));
	save_item(NAME(m_midi_idle));
	save_item(NAME(m_midi_count));
	save_item(NAME(m_midi_byte));
	save_item(NAME(m_gate_array_input));
	save_item(NAME(m_gate_array_registers));
	save_item(NAME(m_gate_array_words));
	save_item(NAME(m_la32_data_l));
	save_item(NAME(m_la32_registers));
	save_item(NAME(m_chorus_registers));
	save_item(NAME(m_reverb_address));
	save_item(NAME(m_reverb_data_h));
	save_item(NAME(m_reverb_registers));
}

void roland_d50_state::machine_reset()
{
	// The D-50 board straps LA32 global control for packed 14-bit PCM and
	// two pairs of partial slots (0/8 and 16/24).  The pairing propagates
	// inactive/key-on state so both oscillators restart at phase zero.
	// These pins are not exposed in the D-50 MCU address map.
	m_la32->write(0x1c1, 0x60);

	// A real battery failure leaves indeterminate SRAM.  MAME previously
	// initialized it to zero, which happens to pass the firmware's range
	// validation while disabling panel/MIDI patch selection (system byte 9,
	// bit 0).  The 22-byte system block is at physical tone-RAM offset 7f00.
	// Repair only an artificial all-zero block, using the firmware's own
	// v2.x defaults; never replace an existing user setup.
	static constexpr u8 system_defaults[22] = {
		0x3a, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
		0x00, 0x0c, 0x00, 0x00, 0x00, 0x00
	};
	u8 *const system = &m_toneram[0x7f00];
	if (std::all_of(system, system + std::size(system_defaults), [](u8 value) { return value == 0; }))
		std::copy(std::begin(system_defaults), std::end(system_defaults), system);

	m_port0 = 0xff;
	m_port1 = 0xff;
	m_la32_irq = false;
	m_midi_idle = true;
	m_midi_count = 0;
	m_midi_byte = 0;
	m_gate_array_input = 0xff;
	std::fill(std::begin(m_gate_array_registers), std::end(m_gate_array_registers), 0);
	std::fill(&m_gate_array_words[0][0], &m_gate_array_words[0][0] + 2 * 0x80, 0);
	m_la32_data_l = 0;
	std::fill(&m_la32_registers[0][0], &m_la32_registers[0][0] + 6 * 32, 0);
	std::fill(std::begin(m_chorus_registers), std::end(m_chorus_registers), 0);
	m_reverb_address = 0;
	m_reverb_data_h = 0;
	std::fill(std::begin(m_reverb_registers), std::end(m_reverb_registers), 0);
	update_eram_bank();
}

void roland_d50_state::lcd_palette(palette_device &palette) const
{
	palette.set_pen_color(0, rgb_t(0, 0, 0));
	palette.set_pen_color(1, rgb_t(0, 255, 0));
}

HD44780_PIXEL_UPDATE(roland_d50_state::pixel_update)
{
	if (x < 5 && y < 8 && line < 2 && pos < 40)
		bitmap.pix(line * 8 + y, pos * 6 + x) = state;
}

void roland_d50_state::lcd_ctrl_w(uint8_t data)
{
	m_lcd->control_w(data);
}

uint8_t roland_d50_state::lcd_ctrl_r()
{
	return m_lcd->control_r();
}

void roland_d50_state::lcd_data_w(uint8_t data)
{
	m_lcd->data_w(data);
}

uint8_t roland_d50_state::gate_array_status_r()
{
	m_maincpu->set_input_line(upd78312_device::INT1_LINE, CLEAR_LINE);
	return 0x07; // panel phases and LCD transfer complete
}

void roland_d50_state::gate_array_end_w(uint8_t)
{
	m_maincpu->set_input_line(upd78312_device::INT1_LINE, ASSERT_LINE);
}

void roland_d50_state::gate_array_w(offs_t offset, uint8_t data)
{
	offset &= 0x3f;
	m_gate_array_registers[offset] = data;

	// F800 selects one of the 128 D-50-visible IC28 words.  F801/F802 and
	// F803/F804 supply two 14-bit values; F804.6 is the transfer marker and
	// F804.7 is parity.  At the MB87126 this becomes a 30-bit parameter word:
	// a 16-bit relative delay operand (the D-50 forces its top two bits low),
	// a signed 12-bit multiplier, and a two-bit MAC range.  Firmware builds
	// the values in DC00-DDFF, using bit 15 of each word as a CPU-side dirty
	// flag.  IC28 then serializes them to the downstream effect hardware.
	if (offset == 4)
	{
		u8 const selector = m_gate_array_registers[0] & 0x7f;
		u16 const word0 = (u16(m_gate_array_registers[2] & 0x3f) << 8) | m_gate_array_registers[1];
		u16 const word1 = (u16(data & 0x3f) << 8) | m_gate_array_registers[3];
		if (machine().options().verbose() &&
			(m_gate_array_words[0][selector] != word0 || m_gate_array_words[1][selector] != word1))
		{
			logerror("D50 IC28 pc=%04X selector=%02X dc=%04X dd=%04X parity=%u\n",
				m_maincpu->pc(), selector, word0, word1, BIT(data, 7));
		}
		m_gate_array_words[0][selector] = word0;
		m_gate_array_words[1][selector] = word1;
		m_effects->ic28_write(selector, word0, word1);
		address_space &program = m_maincpu->space(AS_PROGRAM);
		m_effects->patch_mixer_write(program.read_byte(0xc592), program.read_byte(0xc59d),
			program.read_byte(0xc59e), program.read_byte(0xc59f), program.read_byte(0xc5a0),
			program.read_byte(0xc5a1), program.read_byte(0xc4ae), program.read_byte(0xc4af),
			program.read_byte(0xc56e), program.read_byte(0xc56f),
			program.read_byte(0xc4ad), program.read_byte(0xc56d));
	}
	m_maincpu->set_input_line(upd78312_device::INT1_LINE, ASSERT_LINE);
}

uint8_t roland_d50_state::gate_array_input_r()
{
	// IC28 scans an 8x8 matrix by writing a rotating zero in bits 7 to 0.
	// Reads return the active-low column inputs, not the row-select latch.
	// Bit 7 is the unused eighth row on the D-50 panel.
	for (unsigned row = 0; row < 7; row++)
		if (!BIT(m_gate_array_input, row) && m_panel_rows[row])
			return ~m_panel_rows[row]->read();
	return 0xff;
}

void roland_d50_state::gate_array_input_w(uint8_t data)
{
	m_gate_array_input = data;
}

void roland_d50_state::port0_w(uint8_t data)
{
	m_port0 = data;
	update_eram_bank();
}

void roland_d50_state::port1_w(uint8_t data)
{
	if (machine().options().verbose() && m_port1 != data)
		logerror("D50 P1 pc=%04X value=%02X changed=%02X\n", m_maincpu->pc(), data, m_port1 ^ data);
	m_port1 = data;
}

uint8_t roland_d50_state::port2_r()
{
	// P2.2 is IC28 BUSY (idle low here); P2.1 is the active-low LA32 IRQ.
	return m_la32_irq ? 0xf9 : 0xfb;
}

void roland_d50_state::update_eram_bank()
{
	// P0.7 and P0.5 select the external device or program-ROM quarter;
	// P0.6 is the high address bit for the 32 KiB RAM devices.
	u8 const select = (BIT(m_port0, 5) << 1) | BIT(m_port0, 7);
	m_eram->set_bank((select << 1) | BIT(m_port0, 6));
}

void roland_d50_state::la32_irq_w(int state)
{
	m_la32_irq = bool(state);
	m_maincpu->set_input_line(upd78312_device::INT0_LINE, state ? ASSERT_LINE : CLEAR_LINE);
}

void roland_d50_state::midi_rx_w(int state)
{
	if (!state && m_midi_idle)
	{
		m_midi_idle = false;
		m_midi_count = 0;
		m_midi_byte = 0;
	}
	else if (!m_midi_idle)
	{
		if (m_midi_count < 8)
		{
			m_midi_byte |= bool(state) << m_midi_count;
			m_midi_count++;
		}
		else
		{
			if (state)
			{
				machine().scheduler().synchronize();
				m_maincpu->serial_rx(m_midi_byte);
			}
			m_midi_idle = true;
		}
	}
}

void roland_d50_state::la32_w(offs_t address, uint8_t data)
{
	// The D-50 presents each of the LA32's six 0x40-byte register files in
	// a separate 0x100-byte page.
	m_la32->write(((address >> 8) * 0x40) | (address & 0x3f), data);

	// Keep a shadow solely to make -verbose register traces useful.  The
	// LA32 latches the low byte globally and commits a word on an odd write.
	if (!BIT(address, 0))
		m_la32_data_l = data;
	else if ((address >> 8) < 6 && (address & 0x3f) < 0x40)
	{
		u16 const value = (u16(data) << 8) | m_la32_data_l;
		u8 const file = address >> 8;
		u8 const slot = (address >> 1) & 0x1f;
		if (machine().options().verbose() && m_la32_registers[file][slot] != value)
			logerror("D50 LA32 at=%s pc=%04X file=%u slot=%02u value=%04X\n",
				machine().time().as_string(), m_maincpu->pc(), file, slot, value);
		m_la32_registers[file][slot] = value;
	}
}

uint8_t roland_d50_state::la32_r(offs_t address)
{
	return m_la32->read(((address >> 8) * 0x40) | (address & 0x3f));
}

void roland_d50_state::chorus_w(offs_t address, uint8_t data)
{
	address &= 7;
	if (machine().options().verbose() && m_chorus_registers[address] != data)
		logerror("D50 CHORUS pc=%04X reg=%u value=%02X\n", m_maincpu->pc(), unsigned(address), data);
	m_chorus_registers[address] = data;
	m_effects->ic8_write(address, data);
}

uint8_t roland_d50_state::chorus_r(offs_t address)
{
	return m_chorus_registers[address & 7];
}

void roland_d50_state::reverb_w(offs_t address, uint8_t data)
{
	switch (address & 7)
	{
	case 0:
		m_reverb_data_h = data;
		break;

	case 1:
	{
		u16 const value = (u16(m_reverb_data_h) << 8) | data;
		if (machine().options().verbose() && m_reverb_registers[m_reverb_address] != value)
			logerror("D50 IC9 at=%s pc=%04X reg=%02X value=%04X\n",
				machine().time().as_string(), m_maincpu->pc(), m_reverb_address, value);
		m_reverb_registers[m_reverb_address] = value;
		m_effects->ic9_write(m_reverb_address, value);
		break;
	}

	case 7:
		m_reverb_address = data;
		break;
	}
}

uint8_t roland_d50_state::reverb_r(offs_t address)
{
	switch (address & 7)
	{
	case 0: return m_reverb_registers[m_reverb_address] >> 8;
	case 1: return m_reverb_registers[m_reverb_address];
	case 7: return 0x00; // IC9 BUSY is bit 7, active high
	default: return 0xff;
	}
}

void roland_d50_state::d50_mem_map(address_map &map)
{
	// Internal ROM is enabled at 0000–1FFF (+5V pullup on EA pin)
	map(0x0000, 0x1fff).rom().region("maincpu", 0);
	map(0x2000, 0x7fff).rom().region("progrom", 0x2000);
	map(0x8000, 0xbfff).m(m_eram, FUNC(address_map_bank_device::amap8));
	map(0xc000, 0xdfff).ram();
	map(0xe000, 0xe5ff).rw(FUNC(roland_d50_state::la32_r), FUNC(roland_d50_state::la32_w));
	map(0xe700, 0xe707).rw(FUNC(roland_d50_state::chorus_r), FUNC(roland_d50_state::chorus_w));
	map(0xf000, 0xf007).rw(FUNC(roland_d50_state::reverb_r), FUNC(roland_d50_state::reverb_w));
	map(0xf400, 0xf7ff).rw("keyscan", FUNC(mb63h149_device::read), FUNC(mb63h149_device::write));
	map(0xf800, 0xf83f).w(FUNC(roland_d50_state::gate_array_w));
	map(0xf840, 0xf840).rw(FUNC(roland_d50_state::lcd_ctrl_r), FUNC(roland_d50_state::lcd_ctrl_w));
	map(0xf841, 0xf87e).w(FUNC(roland_d50_state::lcd_data_w));
	map(0xf87f, 0xf87f).w(FUNC(roland_d50_state::gate_array_end_w));
	map(0xf880, 0xf9be).ram(); // IC28 panel input/register area (preliminary)
	map(0xf93f, 0xf93f).rw(FUNC(roland_d50_state::gate_array_input_r), FUNC(roland_d50_state::gate_array_input_w));
	map(0xf9bf, 0xf9bf).r(FUNC(roland_d50_state::gate_array_status_r));
	map(0xf9c0, 0xfbff).noprw();
}

void roland_d50_state::d550_mem_map(address_map &map)
{
	// Internal ROM is enabled at 0000–1FFF (+5V pullup on EA pin)
	map(0x0000, 0x1fff).rom().region("maincpu", 0);
	map(0x2000, 0x7fff).rom().region("progrom", 0x2000);
	map(0x8000, 0xbfff).m(m_eram, FUNC(address_map_bank_device::amap8));
	map(0xc000, 0xdfff).ram();
	map(0xe000, 0xe5ff).rw(FUNC(roland_d50_state::la32_r), FUNC(roland_d50_state::la32_w));
	map(0xe700, 0xe707).rw(FUNC(roland_d50_state::chorus_r), FUNC(roland_d50_state::chorus_w));
	map(0xf000, 0xf007).rw(FUNC(roland_d50_state::reverb_r), FUNC(roland_d50_state::reverb_w));
	map(0xf800, 0xf83f).w(FUNC(roland_d50_state::gate_array_w));
	map(0xf840, 0xf840).rw(FUNC(roland_d50_state::lcd_ctrl_r), FUNC(roland_d50_state::lcd_ctrl_w));
	map(0xf841, 0xf87e).w(FUNC(roland_d50_state::lcd_data_w));
	map(0xf87f, 0xf87f).w(FUNC(roland_d50_state::gate_array_end_w));
	map(0xf880, 0xf9be).ram(); // IC28 panel input/register area (preliminary)
	map(0xf93f, 0xf93f).rw(FUNC(roland_d50_state::gate_array_input_r), FUNC(roland_d50_state::gate_array_input_w));
	map(0xf9bf, 0xf9bf).r(FUNC(roland_d50_state::gate_array_status_r));
	map(0xf9c0, 0xfbff).noprw();
}

void roland_d50_state::eram_map(address_map &map)
{
	map(0x00000, 0x07fff).ram().share("toneram");
	//map(0x08000, 0x0ffff).rw(FUNC(roland_d50_state::memcard_r), FUNC(roland_d50_state::memcard_w));
	map(0x10000, 0x13fff).mirror(0x4000).rom().region("progrom", 0x8000);
	map(0x18000, 0x1bfff).mirror(0x4000).rom().region("progrom", 0xc000);
}


static INPUT_PORTS_START(d50)
	PORT_START("ROW6")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("TUNE/FUNC")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("MIDI")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_9) PORT_NAME("INTERNAL")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_O) PORT_NAME("CARD")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("LOCAL")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("VALUE")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("LOWER")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("UPPER")

	PORT_START("ROW5")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_LSHIFT) PORT_CODE(KEYCODE_RSHIFT) PORT_NAME("SHIFT")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_1_PAD) PORT_NAME("1/ABC")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_4_PAD) PORT_NAME("4/JKL")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_7_PAD) PORT_NAME("7/STU")
	PORT_BIT(0xf0, IP_ACTIVE_HIGH, IPT_UNUSED)

	PORT_START("ROW4")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_0_PAD) PORT_NAME("0/SPACE")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_2_PAD) PORT_NAME("2/DEF")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_5_PAD) PORT_NAME("5/MNO")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_8_PAD) PORT_NAME("8/VWX")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_ENTER) PORT_NAME("ENTER")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_3_PAD) PORT_NAME("3/GHI")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_6_PAD) PORT_NAME("6/PQR")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_9_PAD) PORT_NAME("9/YZ-")

	PORT_START("ROW3")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("DECREMENT")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("INCREMENT")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("WRITE")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("DATA TRANS")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("EXIT")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("UNDO")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("COPY")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("COMPARE")

	PORT_START("ROW2")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("TUNE/DETUNE")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("KEY MODE")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("SPLIT POINT")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("TONE BALANCE")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("L-TONE EDIT")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("U-TONE EDIT")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("PATCH EDIT")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)

	PORT_START("ROW1")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_Q) PORT_NAME("Patch number 1")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_W) PORT_NAME("Patch number 2")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_E) PORT_NAME("Patch number 3")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_R) PORT_NAME("Patch number 4")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_T) PORT_NAME("Patch number 5")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_Y) PORT_NAME("Patch number 6")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_U) PORT_NAME("Patch number 7")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_I) PORT_NAME("Patch number 8")

	PORT_START("ROW0")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_1) PORT_NAME("Patch Bank 1")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_2) PORT_NAME("Patch Bank 2")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_3) PORT_NAME("Patch Bank 3")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_4) PORT_NAME("Patch Bank 4")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_5) PORT_NAME("Patch Bank 5")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_6) PORT_NAME("Patch Bank 6")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_7) PORT_NAME("Patch Bank 7")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_8) PORT_NAME("Patch Bank 8")

	PORT_START("REVERB_MODEL")
	PORT_CONFNAME(0x01, 0x01, "Reverb implementation")
	PORT_CONFSETTING(0x00, "Legacy floating-point graph")
	PORT_CONFSETTING(0x01, "MB87126 fixed-point reconstruction")
INPUT_PORTS_END

static INPUT_PORTS_START(d550)
	PORT_START("ROW4")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_G) PORT_NAME("EXIT")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_H) PORT_NAME("EDIT")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_J) PORT_NAME("WRITE")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_B) PORT_CODE(KEYCODE_ENTER) PORT_NAME("ENTER")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_N) PORT_CODE(KEYCODE_LSHIFT) PORT_CODE(KEYCODE_RSHIFT) PORT_NAME("SHIFT")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_M) PORT_NAME("CHASE")
	PORT_BIT(0xc0, IP_ACTIVE_HIGH, IPT_UNUSED)

	PORT_START("ROW3")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_A) PORT_NAME("TUNE")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_S) PORT_NAME("MIDI")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_D) PORT_NAME("DATA TRANS")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_Z) PORT_NAME("COPY")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_X) PORT_NAME("UNDO")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_C) PORT_NAME("COMPARE")
	PORT_BIT(0xc0, IP_ACTIVE_HIGH, IPT_UNUSED)

	PORT_START("ROW2")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_9) PORT_CODE(KEYCODE_F) PORT_NAME("INT")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_O) PORT_CODE(KEYCODE_V) PORT_NAME("CARD")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_L) PORT_NAME(">>")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_K) PORT_NAME("<<")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_RIGHT) PORT_NAME(">")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_LEFT) PORT_NAME("<")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_UP) PORT_NAME("VALUE UP")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_DOWN) PORT_NAME("VALUE DOWN")

	PORT_START("ROW1")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_1) PORT_NAME("Patch Bank 1")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_2) PORT_NAME("Patch Bank 2")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_3) PORT_NAME("Patch Bank 3")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_4) PORT_NAME("Patch Bank 4")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_5) PORT_NAME("Patch Bank 5")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_6) PORT_NAME("Patch Bank 6")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_7) PORT_NAME("Patch Bank 7")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_8) PORT_NAME("Patch Bank 8")

	PORT_START("ROW0")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_Q) PORT_NAME("Patch number 1")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_W) PORT_NAME("Patch number 2")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_E) PORT_NAME("Patch number 3")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_R) PORT_NAME("Patch number 4")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_T) PORT_NAME("Patch number 5")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_Y) PORT_NAME("Patch number 6")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_U) PORT_NAME("Patch number 7")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_I) PORT_NAME("Patch number 8")

	PORT_START("REVERB_MODEL")
	PORT_CONFNAME(0x01, 0x01, "Reverb implementation")
	PORT_CONFSETTING(0x00, "Legacy floating-point graph")
	PORT_CONFSETTING(0x01, "MB87126 fixed-point reconstruction")
INPUT_PORTS_END


void roland_d50_state::d50(machine_config &config)
{
	UPD78312(config, m_maincpu, 12_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &roland_d50_state::d50_mem_map);
	m_maincpu->port_out_cb<0>().set(FUNC(roland_d50_state::port0_w));
	m_maincpu->port_out_cb<1>().set(FUNC(roland_d50_state::port1_w));
	m_maincpu->port_in_cb<2>().set(FUNC(roland_d50_state::port2_r));
	m_maincpu->serial_tx_cb().set("mdout", FUNC(midi_port_device::write_txd));

	ADDRESS_MAP_BANK(config, m_eram);
	m_eram->set_addrmap(0, &roland_d50_state::eram_map);
	m_eram->set_data_width(8);
	m_eram->set_endianness(ENDIANNESS_LITTLE);
	m_eram->set_addr_width(17);
	m_eram->set_stride(0x4000);

	NVRAM(config, "toneram", nvram_device::DEFAULT_ALL_1); // HM62256LP-12 + battery

	MB63H149(config, m_keyscan, 32.768_MHz_XTAL / 2); // on Dyna Scan Board
	// m_keyscan.int_callback().set_inputline(m_maincpu, upd78312_device::INT2_LINE);

	SPEAKER(config, "lspeaker").front_left();
	SPEAKER(config, "rspeaker").front_right();

	LA32(config, m_la32, 16.384_MHz_XTAL);
	m_la32->set_rom_address_xor(0x40000); // PCM ROM A18 is inverted on the D-50 board
	m_la32->int_callback().set(FUNC(roland_d50_state::la32_irq_w));
	ROLAND_D50_EFFECTS(config, m_effects, 0);
	for (unsigned output = 0; output < 8; output++)
		m_la32->add_route(output, "effects", 3.0, output);
	m_effects->add_route(0, "lspeaker", 1.0);
	m_effects->add_route(1, "rspeaker", 1.0);

	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
	screen.set_refresh_hz(50);
	screen.set_screen_update(m_lcd, FUNC(hd44780_device::screen_update));
	screen.set_size(40*6, 16);
	screen.set_visarea_full();
	screen.set_palette("palette");

	PALETTE(config, "palette", FUNC(roland_d50_state::lcd_palette), 2);

	// LCD unit is LM402802 (D-50) or LM402551 (D-550)
	HD44780(config, m_lcd, 270'000);
	m_lcd->set_lcd_size(2, 40);
	m_lcd->set_pixel_update_cb(FUNC(roland_d50_state::pixel_update));

	midi_port_device &mdin(MIDI_PORT(config, "mdin", midiin_slot, "midiin"));
	mdin.rxd_handler().set(FUNC(roland_d50_state::midi_rx_w));
	MIDI_PORT(config, "mdout", midiout_slot, "midiout");
}

void roland_d50_state::d550(machine_config &config)
{
	d50(config);
	m_maincpu->set_addrmap(AS_PROGRAM, &roland_d50_state::d550_mem_map);

	config.device_remove("keyscan");
}

// the internal date format for the external program roms is as such:
// DDMY where DD is the day within the month, M is a month where
// January == A,February == B, etc, and Y is the last digit of the year
// (the first 3 digits are '198'); note that the date was not updated for
// some minor revisions.

// the portion after the data is in an unknown format, but is presumably
// version information for certain subprograms on the rom itself.

ROM_START(d50) // Newer PCB with silkscreen "Roland || D-50, D-550 || MAIN BOARD" and a checkbox for which board it is intended for "[O]D-50 | ASSY 76180090 || [ ]D-550 | ASSY 79379050" and trace layer ID "22925445 05" and a QFP LA32 "R15229896 || LA32 || 8816Q26" at IC31; the board supports either the split or the combined PCM roms.
	ROM_REGION(0x10000, "progrom", 0)
	ROM_SYSTEM_BIOS(0, "v2.22", "ROM Version 2.22") // "        D-50    Ver 2.22                Thanks to Eric & Adrian.  14J7 D.420/7.9"
	ROMX_LOAD("d-50_222.ic22", 0x00000, 0x10000, CRC(e92c69f9) SHA1(fd783abc7fbd4abe2dedfe89d59e396b5e687a27), ROM_BIOS(0))
	ROM_SYSTEM_BIOS(1, "v2.21", "ROM Version 2.21") // "        D-50    Ver 2.21                Thanks to Eric & Adrian.  14J7 D.420/7.6"
	ROMX_LOAD("roland_d-50__r15179866-01__lh531452.v2.21.27c512.ic22", 0x0000, 0x10000, CRC(3e72bdf0) SHA1(6e7f8cd3b3ce385350ca2eda6aee73f5bbc433ef), ROM_BIOS(1)) // "Roland D-50 // R15179866-01 // LH531452" Mask ROM (23c512 equivalent)
	ROM_SYSTEM_BIOS(2, "v2.20cfw", "ROM Version 2.20 (CFW, patched)") // "        D-50    Ver 2.21                Thanks to Ricard W.       14J7 D.420/7.5"
	ROMX_LOAD("roland_d50_220rw_cfw_patched.ic22", 0x00000, 0x10000, CRC(8901640e) SHA1(abbc0026c64c55a6cbaca5e3e5dcb1a7536e91b3), ROM_BIOS(2))
	ROM_SYSTEM_BIOS(3, "v2.20", "ROM Version 2.20") // "        D-50    Ver 2.20                Thanks to Eric & Adrian.  14J7 D.420/7.5"
	ROMX_LOAD("roland_d-50__r15179866__lh531306__8803_d.v2.20.27c512.ic22", 0x0000, 0x10000, CRC(5db7c340) SHA1(ada0e65bf04ec24188c01b03ad4e0087d88439a8), ROM_BIOS(3)) // "Roland D-50 // R15179866 // LH531306 // 8803 D" Mask ROM (23c512 equivalent)
	ROM_SYSTEM_BIOS(4, "v2.10", "ROM Version 2.10") // "        D-50    Ver 2.10                Thanks to Eric & Adrian.  16G7 D.400/7.4"
	ROMX_LOAD("d50__v.2.10.27c512.ic22", 0x00000, 0x10000, CRC(d1387c54) SHA1(904cf9daf296a66d3c4969266541983081e19471), ROM_BIOS(4))
	// missing 2.00

	ROM_REGION(0x2000, "maincpu", 0)
	ROM_LOAD("d78312g-022_15179266.ic25", 0x0000, 0x2000, CRC(9564903f) SHA1(f68ed97a06764ee000fe6e9d7b39017165f0efc4)) // 8-digit Roland part number not printed on IC

	ROM_REGION(0x80000, "la32", 0)
	ROM_LOAD("roland__r15179858_8801ebi__tc534000p-7477.ic30", 0x00000, 0x80000, CRC(e2aed2d9) SHA1(e9f5b38b9b5fce04beb4cf871401e821a42edacb)) // A+B "Roland || R15179858 8801EBI || TC534000P-7477" 512KiB Mask ROM @ ic30
	// ic29 is empty on boards with tc534000-sized Mask ROMs
ROM_END

ROM_START(d50o) // Older PCB with silkscreen "Roland || D-50 MAIN BOARD || ASSY 76180090" and trace layer ID "22925445 00" and a PGA LA32 "R15229851 || MB87136A || 8706 E02" at IC31, though the oldest service manual supposedly claims it could have an "MB87136" with no A as well. This board only supports the split PCM roms unless hand-modified. This board has a factory greenwire fix for a missing trace from the IC8 Chorus ASIC MB87137-001 pin 59 to the IC1 DRAM D41416 pin 17.
	ROM_REGION(0x10000, "progrom", 0)
	ROM_SYSTEM_BIOS(0, "v1.10cfw", "ROM Version 1.10 maybe user patched") // "        D-50    Ver 1.10                Thanks to Eric & Adrian.  30G7 C.410/7.4"
	ROMX_LOAD("roland__1-11__2-1-96_greg.v1.10.ic22", 0x00000, 0x10000, CRC(7fc199c5) SHA1(3fc889d6c44f8b40de6619a3427d0c89730dd9b3), ROM_BIOS(0)) // this particular dump came from a handwritten label eprom chip labeled "Roland || 1-11 2/1/96 Greg" so it could be original code, but it was a hand-made update chip by some operator in the past. It is possible this code has a user-bugfix applied to it, though, given the 1-11 in the label.
	// missing 1.0.7
	ROM_SYSTEM_BIOS(1, "v1.06", "ROM Version 1.06") // "Thanks to Eric & Adrian.  01E7 C.36a/5.4        D-50    Ver 1.06                "
	ROMX_LOAD("d50-v1.06.ic22", 0x00000, 0x10000, CRC(ccba4e46) SHA1(ce56321226dbbf7dbfac2ad344da447ad6448ee8), ROM_BIOS(1))
	// missing 1.0.5
	ROM_SYSTEM_BIOS(2, "v1.04", "ROM Version 1.04") // "Thanks to Eric & Adrian.  08D7 C.35r/5.3        D-50    Ver 1.04                "
	ROMX_LOAD("d-50__1.0.4.mbm27c512.ic22", 0x00000, 0x10000, CRC(d871451e) SHA1(e692f3553ce6d61633c7551e98ad86f5c40e6449), ROM_BIOS(2)) // "D-50 // 1.0.4" on an MBM27c512 @ ic22 (original roland sticker? not sure, no pic)

	ROM_REGION(0x2000, "maincpu", 0)
	ROM_LOAD("d78312g-017_15179261.ic25", 0x0000, 0x2000, NO_DUMP) // not compatible with newer firmware
	ROM_COPY("progrom", 0x0000, 0x0000, 0x2000)

	ROM_REGION(0x80000, "la32", 0)
	ROM_LOAD("roland_a__r15179835_8710eai__tc532000p-7469.ic30", 0x00000, 0x40000, CRC(1461c0fb) SHA1(55257e70bcf003439b76f96b7ae7e45a3cf24276)) // A "Roland A || R15179835 8710EAI || TC532000P-7469" 256KiB Mask ROM @ ic30; this is identical to the first half of the 512KiB ROM; verified from original ic; also seen with 8736 datecode
	ROM_LOAD("roland_b__r15179836_8710eai__tc532000p-7470.ic29", 0x40000, 0x40000, CRC(e50599bf) SHA1(487c62f2ef7baccf2421059a940fa707e27aefb2)) // B "Roland B || R15179836 8710EAI || TC532000P-7470" 256KiB Mask ROM @ ic29; this is identical to the second half of the 512KiB ROM; verified from original ic; also seen with 8736 datecode
ROM_END

ROM_START(d550) // Newer PCB with silkscreen "Roland || D-50, D-550 || MAIN BOARD" and a checkbox for which board it is intended for "[ ]D-50 | ASSY 76180090 || [O]D-550 | ASSY 79379050" and trace layer ID "22925445 05" and a QFP LA32 "R15229896 || LA32 || 8816Q26" at IC31, This board supports either the split or the combined PCM roms.
	ROM_REGION(0x10000, "progrom", 0)
	ROM_SYSTEM_BIOS(0, "v1.02", "ROM Version 1.02") // "        D-550   Ver 1.02                Thanks to Eric & Adrian.  20G7 D.000/1.0"
	ROMX_LOAD("roland_d-550_v1.02_eprom_firmware.ic22", 0x00000, 0x10000, CRC(11b54d24) SHA1(d11bdb50c49edababec73a936346a6f918dd0949), ROM_BIOS(0))
	ROM_SYSTEM_BIOS(1, "v1.01", "ROM Version 1.01") // "        D-550   Ver 1.01                Thanks to Eric & Adrian.  20G7 D.000/0.9"
	ROMX_LOAD("roland_d-550_ver_1.01.ic22", 0x00000, 0x10000, CRC(c267019f) SHA1(314616d14b91e8c8733aee5dd64736fda8ff6904), ROM_BIOS(1))
	ROM_SYSTEM_BIOS(2, "v1.0.0", "ROM Version 1.0.0") // "        D-550   Ver 1.00                Thanks to Eric & Adrian.  20G7 D.000/0.5"
	ROMX_LOAD("d-550__1.0.0.mbm27c512.ic22", 0x00000, 0x10000, CRC(f8ec84c3) SHA1(00b2b74009bdc0747e11bf57d4dc6c39424c7669), ROM_BIOS(2))

	ROM_REGION(0x2000, "maincpu", 0)
	ROM_LOAD("d78312g-022_15179266.ic25", 0x0000, 0x2000, NO_DUMP)
	ROM_COPY("progrom", 0x0000, 0x0000, 0x2000)

	ROM_REGION(0x80000, "la32", 0)
	ROM_LOAD("roland__r15179858_8801ebi__tc534000p-7477.ic30", 0x00000, 0x80000, CRC(e2aed2d9) SHA1(e9f5b38b9b5fce04beb4cf871401e821a42edacb)) // A+B "Roland || R15179858 8801EBI || TC534000P-7477" 512KiB Mask ROM @ ic30
	// ic29 is empty on boards with tc534000-sized Mask ROMs
ROM_END

} // anonymous namespace


SYST(1987, d50,  0,   0, d50,  d50, roland_d50_state, empty_init, "Roland", "D-50 Linear Synthesizer (Ver. 2.xx)", MACHINE_NOT_WORKING)
SYST(1987, d50o, d50, 0, d50,  d50, roland_d50_state, empty_init, "Roland", "D-50 Linear Synthesizer (Ver. 1.xx)", MACHINE_NOT_WORKING)
SYST(1987, d550, d50, 0, d550, d550, roland_d50_state, empty_init, "Roland", "D-550 Linear Synthesizer", MACHINE_NOT_WORKING)
