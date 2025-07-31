// license:BSD-3-Clause
// copyright-holders:AJR
/****************************************************************************

    Skeleton driver for Boss SX-700 signal processor.

****************************************************************************/

#include "emu.h"
#include "cpu/h8/h83002.h"
#include "video/hd44780.h"
#include "emupal.h"
#include "screen.h"


namespace {

class boss_sx700_state : public driver_device
{
public:
	boss_sx700_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, lcd(*this, "lcd")
		, m_sw0(*this, "SW0")
		, m_sw1(*this, "SW1")
	{
	}

	void sx700(machine_config &config);
	void gx700(machine_config &config);

	DECLARE_INPUT_CHANGED_MEMBER(button);

private:
	void sx700_map(address_map &map) ATTR_COLD;
	void gx700_map(address_map &map) ATTR_COLD;

	required_device<h83002_device> m_maincpu;
	required_device<hd44780_device> lcd;
	required_ioport m_sw0, m_sw1;

	void lcd_palette(palette_device &palette) const;
	uint32_t screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

	u8 p4_value = 0;
	u8 p6_value = 0;
	u8 p4_r();
  void p4_w(u8 data);
	u8 p6_r();
  void p6_w(u8 data);

	u8 p7_r();
	u8 p8_r();
  void p8_w(u8 data);
	u8 p9_r();
  void p9_w(u8 data);
	u8 pa_r();
  void pa_w(u8 data);
	u8 pb_r();
  void pb_w(u8 data);

	u16 adc0_r(); // swcol0
	u16 adc1_r(); // swcol1
	u16 adc2_r(); // swcol2
	u16 adc3_r(); // swcol3
	u16 adc4_r(); // expression
	u16 adc5_r(); // battery
	u16 adc6_r(); // enc sw
	u16 adc7_r(); // enc xb

	u8 dsp_r(offs_t offs);
  void dsp_w(offs_t offs, u8 data);
};

u8 boss_sx700_state::p4_r()
{
	return p4_value;
}

void boss_sx700_state::p4_w(u8 data)
{
	p4_value = data;
	logerror("write LCD p4=%02x %c\n", data, data);
}

u8 boss_sx700_state::p6_r()
{
	return p6_value;
}

void boss_sx700_state::p6_w(u8 data)
{
	// bit 0: LCD enable
	// bit 1: LCD read/write
	// bit 2: LCD register select
	// bits 3-7: unused
	p6_value = data;
	logerror("write LCD p6=%02x\n", data);
	
	if (BIT(data, 0)) // LCD enable
	{
		if (BIT(data, 1)) // read
		{
			lcd->rs_w(BIT(data, 2));
			lcd->rw_w(1);
			lcd->e_w(1);
			lcd->e_w(0);
		}
		else // write
		{
			lcd->rs_w(BIT(data, 2));
			lcd->rw_w(0);
			lcd->e_w(1);
			lcd->e_w(0);
		}
	}
	else // LCD disable
	{
		lcd->rs_w(0);
		lcd->rw_w(0);
		lcd->e_w(0);
	}
}

u8 boss_sx700_state::p7_r()
{
	if (!machine().side_effects_disabled()) logerror("read p7\n");
	return 0x00;
}

u8 boss_sx700_state::p8_r()
{
	if (!machine().side_effects_disabled()) logerror("read p8\n");
	return 0x00;
}
void boss_sx700_state::p8_w(u8 data)
{
	logerror("write p8=%02x\n", data);
}

u8 boss_sx700_state::p9_r()
{
	if (!machine().side_effects_disabled()) logerror("read p9\n");
	return 0x00;
}
void boss_sx700_state::p9_w(u8 data)
{
	logerror("write p9=%02x\n", data);
}

u8 boss_sx700_state::pa_r()
{
	if (!machine().side_effects_disabled()) logerror("read pa\n");
	return 0x00;
}
void boss_sx700_state::pa_w(u8 data)
{
	logerror("write pa=%02x\n", data);
}

u8 boss_sx700_state::pb_r()
{
	if (!machine().side_effects_disabled()) logerror("read pb\n");
	return 0x00;
}
void boss_sx700_state::pb_w(u8 data)
{
	logerror("write pb=%02x\n", data);
}

u16 boss_sx700_state::adc0_r() {
	if (!machine().side_effects_disabled()) logerror("read adc0\n");
	return 0x0000; // swcol0
}
u16 boss_sx700_state::adc1_r() {
	if (!machine().side_effects_disabled()) logerror("read adc1\n");
	return 0x0000; // swcol1
}
u16 boss_sx700_state::adc2_r() {
	if (!machine().side_effects_disabled()) logerror("read adc2\n");
	return 0x0000; // swcol2
}
u16 boss_sx700_state::adc3_r() {
	if (!machine().side_effects_disabled()) logerror("read adc3\n");
	return 0x0000; // swcol3
}
u16 boss_sx700_state::adc4_r() {
	// if (!machine().side_effects_disabled()) logerror("read adc4\n");
	return 0xffff; // expression
}
u16 boss_sx700_state::adc5_r() {
	// if (!machine().side_effects_disabled()) logerror("read adc5\n");
	return 0xffff; // battery
}
u16 boss_sx700_state::adc6_r() {
	if (!machine().side_effects_disabled()) logerror("read adc6\n");
	return 0x0000; // enc sw
}
u16 boss_sx700_state::adc7_r() {
	if (!machine().side_effects_disabled()) logerror("read adc7\n");
	return 0x0000; // enc xb
}

u8 boss_sx700_state::dsp_r(offs_t offs)
{
	if (!machine().side_effects_disabled()) logerror("DSP read %04x\n", offs);
	return 0x00;
}

void boss_sx700_state::dsp_w(offs_t offs, u8 data)
{
	logerror("DSP write %04x=%02x\n", offs, data);
}


uint32_t boss_sx700_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	u16 sy=0;
	u8 const *const data = lcd->render();
	bitmap.fill(0);

	for (u8 y = 0; y < 2; y++)
	{
		for (u8 ra = 0; ra < 9; ra++)
		{
			u16 *p = &bitmap.pix(sy++);

			for (u16 x = 0; x < 16; x++)
			{
				u8 gfx = 0;
				if (ra < 8)
					gfx = data[x*16 + y*640 + ra];

				*p++ = BIT(gfx, 4);
				*p++ = BIT(gfx, 3);
				*p++ = BIT(gfx, 2);
				*p++ = BIT(gfx, 1);
				*p++ = BIT(gfx, 0);
				*p++ = 0;
			}
		}
	}
	return 0;
}

void boss_sx700_state::lcd_palette(palette_device &palette) const
{
  palette.set_pen_color(0, rgb_t(0, 0, 0));
	palette.set_pen_color(1, rgb_t(0, 255, 0));
}


void boss_sx700_state::sx700_map(address_map &map)
{
	map(0x000000, 0x07ffff).mirror(0x080000).rom().region("program", 0);
	map(0x200000, 0x20ffff).ram();
	map(0x400000, 0x5fffff).rw(FUNC(boss_sx700_state::dsp_r), FUNC(boss_sx700_state::dsp_w));
}

void boss_sx700_state::gx700_map(address_map &map)
{
	map(0x000000, 0x03ffff).mirror(0x40000).rom().region("program", 0);
	map(0x200000, 0x20ffff).ram();
	map(0x400000, 0x5fffff).rw(FUNC(boss_sx700_state::dsp_r), FUNC(boss_sx700_state::dsp_w));
}


void boss_sx700_state::sx700(machine_config &config)
{
	H83002(config, m_maincpu, 16_MHz_XTAL); // HD6413002F
	// TODO: operates in mode 3 with 8-bit data bus
	m_maincpu->set_addrmap(AS_PROGRAM, &boss_sx700_state::sx700_map);
	m_maincpu->read_port4().set(FUNC(boss_sx700_state::p4_r));
	m_maincpu->write_port4().set(FUNC(boss_sx700_state::p4_w));
	m_maincpu->read_port6().set(FUNC(boss_sx700_state::p6_r));
	m_maincpu->write_port6().set(FUNC(boss_sx700_state::p6_w));
	m_maincpu->read_port7().set(FUNC(boss_sx700_state::p7_r));
	m_maincpu->read_port8().set(FUNC(boss_sx700_state::p8_r));
	m_maincpu->write_port8().set(FUNC(boss_sx700_state::p8_w));
	m_maincpu->read_port9().set(FUNC(boss_sx700_state::p9_r));
	m_maincpu->write_port9().set(FUNC(boss_sx700_state::p9_w));
	m_maincpu->read_porta().set(FUNC(boss_sx700_state::pa_r));
	m_maincpu->write_porta().set(FUNC(boss_sx700_state::pa_w));
	m_maincpu->read_portb().set(FUNC(boss_sx700_state::pb_r));
	m_maincpu->write_portb().set(FUNC(boss_sx700_state::pb_w));
	m_maincpu->read_adc<0>().set(FUNC(boss_sx700_state::adc0_r));
	m_maincpu->read_adc<1>().set(FUNC(boss_sx700_state::adc1_r));
	m_maincpu->read_adc<2>().set(FUNC(boss_sx700_state::adc2_r));
	m_maincpu->read_adc<3>().set(FUNC(boss_sx700_state::adc3_r));
	m_maincpu->read_adc<4>().set(FUNC(boss_sx700_state::adc4_r));
	m_maincpu->read_adc<5>().set(FUNC(boss_sx700_state::adc5_r));
	m_maincpu->read_adc<6>().set(FUNC(boss_sx700_state::adc6_r));
	m_maincpu->read_adc<7>().set(FUNC(boss_sx700_state::adc7_r));

	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
  screen.set_refresh_hz(50);
  screen.set_screen_update(FUNC(boss_sx700_state::screen_update));
  screen.set_palette("palette");
  PALETTE(config, "palette", FUNC(boss_sx700_state::lcd_palette), 2);
  HD44780(config, lcd, 270'000);
  lcd->set_lcd_size(2, 16);

	//TC170C140AF_003(config, "dsp", 67.7376_MHz_XTAL);
}

void boss_sx700_state::gx700(machine_config &config)
{
	sx700(config);
	m_maincpu->set_addrmap(AS_PROGRAM, &boss_sx700_state::gx700_map);
}

INPUT_CHANGED_MEMBER(boss_sx700_state::button) {
  if (m_sw1->read() & 0x100) {
		logerror("DEBUG button pressed\n");
		m_maincpu->set_input_line(INPUT_LINE_IRQ0, ASSERT_LINE);
		m_maincpu->set_input_line(INPUT_LINE_IRQ1, ASSERT_LINE);
		m_maincpu->set_input_line(INPUT_LINE_IRQ2, ASSERT_LINE);
		m_maincpu->set_input_line(INPUT_LINE_IRQ3, ASSERT_LINE);
		m_maincpu->set_input_line(INPUT_LINE_IRQ4, ASSERT_LINE);
		m_maincpu->set_input_line(INPUT_LINE_IRQ5, ASSERT_LINE);
		m_maincpu->set_input_line(INPUT_LINE_IRQ6, ASSERT_LINE);
		m_maincpu->set_input_line(H8_INPUT_LINE_DREQ0, ASSERT_LINE);
		m_maincpu->set_input_line(H8_INPUT_LINE_DREQ1, ASSERT_LINE);
		m_maincpu->set_input_line(H8_INPUT_LINE_DREQ2, ASSERT_LINE);
		m_maincpu->set_input_line(H8_INPUT_LINE_DREQ3, ASSERT_LINE);
		m_maincpu->set_input_line(H8_INPUT_LINE_TEND0, ASSERT_LINE);
		m_maincpu->set_input_line(H8_INPUT_LINE_TEND1, ASSERT_LINE);
		m_maincpu->set_input_line(H8_INPUT_LINE_TEND2, ASSERT_LINE);
		m_maincpu->set_input_line(H8_INPUT_LINE_TEND3, ASSERT_LINE);
	}
}

static INPUT_PORTS_START(sx700)
  PORT_START("SW0")
  PORT_BIT(0x1, IP_ACTIVE_LOW, IPT_OTHER)
  PORT_BIT(0x2, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("BYPASS") PORT_CODE(KEYCODE_Q)
  PORT_BIT(0x4, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("SYSTEM") PORT_CODE(KEYCODE_W)
  PORT_BIT(0x8, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("WRITE") PORT_CODE(KEYCODE_E)
  PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("EXIT") PORT_CODE(KEYCODE_R)
  PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("VALUE_D") PORT_CODE(KEYCODE_T)
  PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER)
  PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("PROGRAM_D") PORT_CODE(KEYCODE_Y)
  
  PORT_START("SW1")
  PORT_BIT(0x1, IP_ACTIVE_LOW, IPT_OTHER)
  PORT_BIT(0x2, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("CONTROL") PORT_CODE(KEYCODE_A)
  PORT_BIT(0x4, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("NAME") PORT_CODE(KEYCODE_S)
  PORT_BIT(0x8, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("CONTROL ASSIGN") PORT_CODE(KEYCODE_D)
  PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("PARAM") PORT_CODE(KEYCODE_F)
  PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("VALUE_U") PORT_CODE(KEYCODE_G)
  PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER)
  PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("PROGRAM_U") PORT_CODE(KEYCODE_H)
  
  PORT_BIT(0x100, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("DEBUG") PORT_CODE(KEYCODE_P) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(boss_sx700_state::button), 0)
INPUT_PORTS_END

ROM_START(sx700)
	ROM_REGION16_BE(0x80000, "program", 0)
	ROM_LOAD("sx-700_1_0_2.ic17", 0x00000, 0x80000, CRC(6739f525) SHA1(3370f43fd586baa0bcc71891a766b45e1d42253d)) // M27C4001-10F1
ROM_END

ROM_START(gx700)
	ROM_REGION16_BE(0x40000, "program", 0)
	ROM_SYSTEM_BIOS(0, "v110", "v1.10 WATERDRAGON")
	ROMX_LOAD("gx-700_1_1_0.ic20", 0x00000, 0x40000, CRC(feb8a186) SHA1(5f039ff1aa45ed4a33ecd8fd9ad39880646999cb), ROM_BIOS(0)) // M27C2001-10F1
	ROM_SYSTEM_BIOS(1, "v109", "v1.09 WATERDRAGON")
	ROMX_LOAD("gx-700_1_0_9.ic20", 0x00000, 0x40000, CRC(e38b3eeb) SHA1(8ce0563b70d37103acafe1578706ec1c32419e34), ROM_BIOS(1)) // M27C2001-10F1
ROM_END

} // anonymous namespace


SYST(1996, sx700, 0, 0, sx700, sx700, boss_sx700_state, empty_init, "Roland", "Boss SX-700 Studio Effects Processor", MACHINE_NO_SOUND | MACHINE_NOT_WORKING)
SYST(1996, gx700, 0, 0, gx700, sx700, boss_sx700_state, empty_init, "Roland", "Boss GX-700 Guitar Effects Processor", MACHINE_NO_SOUND | MACHINE_NOT_WORKING)
