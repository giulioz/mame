// license:BSD-3-Clause
// copyright-holders:giulioz

/*
  Skeleton driver for the Yamaha SPX90.

  ACIA is emulated at a high level for simplicity.

  CPU->DSP protocol:
  - 0x00 0x40 ADDR DATA: MOD set param
  - 0x00 0x80 0x00 DATA: DSP halt? (0,2)
  - 0x00 0x80 0x01 0x00: DSP init?
  - 0x00 0x80 0x03 0x05: DSP init?
  - 0x00 0x80 0x04 DATA: DSP addr high? (0,1)
  - 0x00 0x80 0x05 DATA: DSP addr low? (0,1,2,3,4,6,7)
  - 0x00 0x80 0x06 DATA: DSP LSB?
  - 0x00 0x80 0x07 DATA: DSP MSB?

  DSP areas:
  00 00: Program 0? (8 bit, 64)
  00 01: Program 1? (8 bit, 64)
  00 02: Program 2? (8 bit, 64)
  00 03: Program 3? (8 bit, 64)
  00 04: Program 4? (8 bit, 64)
  00 06: ADRS data (16 bit, 64)
  00 07: COEF data (16 bit, 64)


  DSP instr:
  00-02: RAM D(mul) address W?
     03: RAM D(mul) write?
  04-07: RAM D(mul) address R?

  08-10: RAM C(alu) address W?
     11: RAM C(alu) write?
  12-14: RAM C(alu) address R?
     15: RAM D(mul) high addr?

  16-23: Multiplier control?
    22: clear accumulator?

     24: DRAM addr control?
  25-26: DRAM addr control + multiplier?
  27-31: ALU control?

  32-33: ALU control?
  34-36: DRAM addr control?
  37-39: DRAM control?


  pgm[4]:
    00
    02
    20
    22
    26
    2e
    60
    62


  MOD params:
  0x00: ??
  0x10: Mod Speed LSB
  0x20: Mod Speed MSB
  0x30: ??
  0x40: ??
  0x50: Decay level
  0x60: Mod delay
  0x70: ??
  0x80: ??
  0x90: ??
  0xA0: ??
  0xB0: ??
  0xC0: Attack time
  0xD0: Mod depth
  0xE0: Release/Decay time
  0xF0: ??
 */

#include "emu.h"
#include "emupal.h"
#include "screen.h"
#include "disound.h"
#include "cpu/m6800/m6801.h"
#include "machine/clock.h"
#include "video/hd44780.h"


namespace {

class yamaha_spx90_state : public driver_device
{
public:
	yamaha_spx90_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_cpu(*this, "cpu")
    , m_lcd(*this, "lcd")
    , m_keys(*this, "KEY%u", 0)
	{
	}

	void spx90(machine_config &config);

  DECLARE_INPUT_CHANGED_MEMBER(button);

protected:
	virtual void machine_start() override;
	virtual void machine_reset() override;

private:
	void yamaha_spx90_map_a(address_map &map);
	required_device<hd6303r_cpu_device> m_cpu;
  required_device<hd44780_device> m_lcd;
  required_ioport_array<2> m_keys;

	HD44780_PIXEL_UPDATE(pixel_update);
	void lcd_palette(palette_device &palette) const;

  u8 panel_r(offs_t offset);
  void panel_w(offs_t offset, u8 data);

  u8 acia_r(offs_t offset);
  void acia_w(offs_t offset, u8 data);

  u8 m_cmd_buffer[8] = {0};
  u8 m_cmd_buffer_pos = 0;
  
  u8 m_mod_mem[0x100] = {0};
  u8 m_dsp_prog[64*5] = {0};
  u8 m_dsp_adrs[64 * 2] = {0};
  u8 m_dsp_coefs[64 * 2] = {0};
  u8 m_dsp_sel_pos = 0;
  u8 m_dsp_sel_area = 0;
  u8 m_dsp_temp_msb = 0;
};

void yamaha_spx90_state::machine_start()
{
  save_item(NAME(m_mod_mem));
  save_item(NAME(m_dsp_prog));
  save_item(NAME(m_dsp_adrs));
  save_item(NAME(m_dsp_coefs));
}

void yamaha_spx90_state::machine_reset()
{
  m_cmd_buffer_pos = 0;
  m_dsp_sel_pos = 0;
  m_dsp_sel_area = 0;
  m_dsp_temp_msb = 0;
}

void yamaha_spx90_state::lcd_palette(palette_device &palette) const
{
	palette.set_pen_color(0, rgb_t(0, 0, 0));
	palette.set_pen_color(1, rgb_t(0, 255, 0));
}

HD44780_PIXEL_UPDATE(yamaha_spx90_state::pixel_update)
{
	if (x < 5 && y < 8 && line < 2 && pos < 16)
		bitmap.pix(line * 8 + y, pos * 6 + x) = state;
}

u8 yamaha_spx90_state::panel_r(offs_t offset)
{
  if (offset == 0)
    return m_keys[0]->read();
  else if (offset == 2)
    return m_keys[1]->read();

  return 0xff;
}

void yamaha_spx90_state::panel_w(offs_t offset, u8 data)
{
  // TODO
}

u8 yamaha_spx90_state::acia_r(offs_t offset)
{
  return 0xff;
}

void yamaha_spx90_state::acia_w(offs_t offset, u8 data)
{
  if (offset == 1)
  {
    m_cmd_buffer[m_cmd_buffer_pos++] = data;

    if (m_cmd_buffer_pos == 4)
    {
      // printf("dsp cmd %02x %02x %02x %02x\n", m_cmd_buffer[0], m_cmd_buffer[1], m_cmd_buffer[2], m_cmd_buffer[3]);
      m_cmd_buffer_pos = 0;

      if (m_cmd_buffer[0] == 0x00 && m_cmd_buffer[1] == 0x40)
      {
        // MOD chip
        m_mod_mem[m_cmd_buffer[2]] = m_cmd_buffer[3];
      }
      else if (m_cmd_buffer[0] == 0x00 && m_cmd_buffer[1] == 0x80)
      {
        // DSP chip
        // printf("dsp %02x %02x\n", m_cmd_buffer[2], m_cmd_buffer[3]);

        if (m_cmd_buffer[2] == 0x00)
        {
          // halt?
          logerror("dsp 00: %02x\n", m_cmd_buffer[3]);
        }
        else if (m_cmd_buffer[2] == 0x01)
        {
          // init?
          logerror("dsp 01: %02x\n", m_cmd_buffer[3]);
        }
        else if (m_cmd_buffer[2] == 0x03)
        {
          // init?
          logerror("dsp 03: %02x\n", m_cmd_buffer[3]);
        }
        else if (m_cmd_buffer[2] == 0x04)
        {
          // addr low
          m_dsp_sel_pos = m_cmd_buffer[3];
        }
        else if (m_cmd_buffer[2] == 0x05)
        {
          // addr area (0,1,2,3,4,6,7)
          m_dsp_sel_area = m_cmd_buffer[3];
          m_dsp_sel_pos = 0;
        }
        else if (m_cmd_buffer[2] == 0x06)
        {
          printf("dsp write %02x%02x = %02x%02x\n", m_dsp_sel_pos, m_dsp_sel_area, m_dsp_temp_msb, m_cmd_buffer[3]);

          if (m_dsp_sel_area < 5) {
            m_dsp_prog[m_dsp_sel_pos * 5 + m_dsp_sel_area] = m_cmd_buffer[3];
          }
          else if (m_dsp_sel_area == 0x06) {
            m_dsp_adrs[m_dsp_sel_pos*2 + 0] = m_dsp_temp_msb;
            m_dsp_adrs[m_dsp_sel_pos*2 + 1] = m_cmd_buffer[3];
          }
          else if (m_dsp_sel_area == 0x07) {
            m_dsp_coefs[m_dsp_sel_pos*2 + 0] = m_dsp_temp_msb;
            m_dsp_coefs[m_dsp_sel_pos*2 + 1] = m_cmd_buffer[3];
          }

          m_dsp_temp_msb = 0;
          m_dsp_sel_pos++;
        }
        else if (m_cmd_buffer[2] == 0x07)
        {
          m_dsp_temp_msb = m_cmd_buffer[3];
        }
        else
        {
          logerror("dsp invalid command: %02x %02x %02x %02x\n", m_cmd_buffer[0], m_cmd_buffer[1], m_cmd_buffer[2], m_cmd_buffer[3]);
        }
      }
      else
      {
        logerror("serial invalid command: %02x %02x %02x %02x\n", m_cmd_buffer[0], m_cmd_buffer[1], m_cmd_buffer[2], m_cmd_buffer[3]);
      }
    }
  }
}

void yamaha_spx90_state::yamaha_spx90_map_a(address_map &map)
{
	map(0x1000, 0x1001).rw(FUNC(yamaha_spx90_state::acia_r), FUNC(yamaha_spx90_state::acia_w));
	map(0x2002, 0x2004).rw(FUNC(yamaha_spx90_state::panel_r), FUNC(yamaha_spx90_state::panel_w));
	map(0x4000, 0x5fff).ram();
  map(0x6000, 0x6000).rw(m_lcd, FUNC(hd44780_device::data_r), FUNC(hd44780_device::data_w));
  map(0x6001, 0x6001).rw(m_lcd, FUNC(hd44780_device::control_r), FUNC(hd44780_device::control_w));
	map(0x8000, 0xffff).rom().region("os", 0);
}

void yamaha_spx90_state::spx90(machine_config &config)
{
	HD6303R(config, m_cpu, 2_MHz_XTAL);
  m_cpu->set_addrmap(AS_PROGRAM, &yamaha_spx90_state::yamaha_spx90_map_a);

  screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
	screen.set_refresh_hz(50);
	screen.set_screen_update(m_lcd, FUNC(hd44780_device::screen_update));
	screen.set_size(16*6, 16);
	screen.set_visarea_full();
	screen.set_palette("palette");

	PALETTE(config, "palette", FUNC(yamaha_spx90_state::lcd_palette), 2);

	HD44780(config, m_lcd, 270'000);
	m_lcd->set_lcd_size(2, 16);
	m_lcd->set_pixel_update_cb(FUNC(yamaha_spx90_state::pixel_update));
}

INPUT_CHANGED_MEMBER(yamaha_spx90_state::button) {
  if (m_keys[1]->read() & 0x100) {
    // DEBUG
    FILE *f = fopen("spx90.txt", "w");
    for (int i = 0; i < 64; i++) {
      fprintf(f, "%03x: %02x %02x %02x %02x %02x    coef:%02x%02x  addr:%02x%02x\n", i,
        m_dsp_prog[i * 5 + 0], m_dsp_prog[i * 5 + 1], m_dsp_prog[i * 5 + 2], m_dsp_prog[i * 5 + 3], m_dsp_prog[i * 5 + 4],
        m_dsp_coefs[i * 2 + 0], m_dsp_coefs[i * 2 + 1],
        m_dsp_adrs[i * 2 + 0], m_dsp_adrs[i * 2 + 1]
      );
    }
    fclose(f);
  }
}

static INPUT_PORTS_START(spx90)
	PORT_START("KEY0")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("RECALL") PORT_CODE(KEYCODE_Q)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("PROGRAM DOWN") PORT_CODE(KEYCODE_W)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("PROGRAM UP") PORT_CODE(KEYCODE_E)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("STORE") PORT_CODE(KEYCODE_R)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("BALANCE") PORT_CODE(KEYCODE_T)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("PARAM DOWN") PORT_CODE(KEYCODE_Y)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("PARAM UP") PORT_CODE(KEYCODE_U)
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("PARAMETER") PORT_CODE(KEYCODE_I)
	
  PORT_START("KEY1")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("BYPASS") PORT_CODE(KEYCODE_O)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("FOOT TRIGGER") PORT_CODE(KEYCODE_P)
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("UTILITY") PORT_CODE(KEYCODE_A)

  PORT_BIT(0x100, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("DEBUG") PORT_CODE(KEYCODE_L) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(yamaha_spx90_state::button), 0)
INPUT_PORTS_END

ROM_START(spx90)
	ROM_REGION(0x8000, "os", 0)
	ROM_LOAD("SPX90 V1.2_8706.BIN", 0x0000, 0x8000, CRC(3c02bfa3) SHA1(0f67a501f0ccf3e8ec369c9b919cdef9d95f3b1f))
ROM_END

} // anonymous namespace


SYST(1986, spx90, 0, 0, spx90, spx90, yamaha_spx90_state, empty_init, "Yamaha", "SPX90 Digital multi-effect processor", MACHINE_NOT_WORKING)
