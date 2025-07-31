// license:BSD-3-Clause
// copyright-holders:giulioz
/****************************************************************************

    Skeleton driver for Roland U-110 synthesizers.

****************************************************************************/

#include "bus/midi/midiinport.h"
#include "bus/midi/midioutport.h"
#include "cpu/mcs96/i8x9x.h"
#include "emu.h"
#include "emupal.h"
#include "machine/ram.h"
#include "machine/timer.h"
#include "screen.h"
#include "softlist_dev.h"
#include "sound/roland_lp.h"
#include "speaker.h"
#include "video/hd44780.h"
#include <queue>

namespace {

// unscramble address: ROM dump offset -> proper (descrambled) offset
#define UNSCRAMBLE_ADDR_INT(_offset)                                           \
  bitswap<19>(_offset, 18, 17, 15, 14, 16, 12, 11, 7, 9, 13, 10, 8, 3, 2, 1,   \
              6, 4, 5, 0)
// scramble address: proper offset -> ROM dump offset
#define SCRAMBLE_ADDR_INT(_offset)                                             \
  bitswap<19>(_offset, 18, 17, 14, 16, 15, 9, 13, 12, 8, 10, 7, 11, 3, 1, 2,   \
              6, 5, 4, 0)

// PCM cards use a different address line scrambling
#define UNSCRAMBLE_ADDR_EXT(_offset)                                           \
  bitswap<19>(_offset, 18, 17, 8, 9, 16, 11, 12, 7, 14, 10, 13, 15, 3, 2, 1,   \
              6, 4, 5, 0)

#define UNSCRAMBLE_DATA(_data) bitswap<8>(_data, 1, 2, 7, 3, 5, 0, 4, 6)

static INPUT_PORTS_START(u110)
  PORT_START("SW")
  PORT_BIT(0x1, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("PART") PORT_CODE(KEYCODE_A)
  PORT_BIT(0x2, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("EDIT") PORT_CODE(KEYCODE_S)
  PORT_BIT(0x4, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("LEFT") PORT_CODE(KEYCODE_Q)
  PORT_BIT(0x8, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("RIGHT") PORT_CODE(KEYCODE_W)
  PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("DEC") PORT_CODE(KEYCODE_Z)
  PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("INC") PORT_CODE(KEYCODE_X)
INPUT_PORTS_END

class roland_u110_state : public driver_device {
public:
  roland_u110_state(const machine_config &mconfig, device_type type, const char *tag)
      : driver_device(mconfig, type, tag), cpu(*this, "maincpu"),
        pcm(*this, "pcm"), lcd(*this, "lcd"), midi_timer(*this, "midi_timer"),
        m_sw(*this, "SW"), m_ram(*this, "ram"), m_midi_rx(0), m_midi_pos(0) {}

  void u110(machine_config &config);

  void init_u110();

protected:
  virtual void machine_start() override;
  virtual void machine_reset() override;

private:
  required_device<i8x9x_device> cpu;
  required_device<mb87419_mb87420_device> pcm;
  required_device<hd44780_device> lcd;
  required_device<timer_device> midi_timer;
  required_ioport m_sw;
  required_device<ram_device> m_ram;

  void lcd_palette(palette_device &palette) const;

	uint32_t screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

  void midi_w(u16 data);

  void port2_w(uint8_t data);
  u8 lcd_ctrl_r();
  void lcd_ctrl_w(u8 data);
  void lcd_data_w(u8 data);
  void leds_w(u8 data);
  u8 sw_scan_r();
  u16 analog0_r();
  u8 pcmrom_r(offs_t offset);
  u16 snd_io_r(offs_t offset);
  void snd_io_w(offs_t offset, u16 data);

  TIMER_DEVICE_CALLBACK_MEMBER(midi_timer_cb);
  TIMER_DEVICE_CALLBACK_MEMBER(samples_timer_cb);

  void u110_map(address_map &map);

  void descramble_rom_internal(u8 *dst, const u8 *src);
  void descramble_rom_external(u8 *dst, const u8 *src);

  u8 midi;
  u8 sound_io_buffer[0x100];
  u8 m_midi_rx;
  int m_midi_pos;
  std::queue<u8> midi_queue;
};

// screen update function from Roland D-110
uint32_t roland_u110_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
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

void roland_u110_state::machine_start() {
  u8 *rom = memregion("progrom")->base();

  // TODO: The IC8 gate array has an "LCD INT" line that needs to be emulated.
  // Then, the hack can be removed. Note: The hack is not necessary when *not*
  // using test mode.
  // rom[0xBB2D] = 0x03; // hack to make test mode not freeze when displaying the LCD text
  rom[0xD302] = 0x00;
  rom[0xD303] = 0x00;

  // TODO: remove this hack
  // rom[0x7D80] = 0x00; // hack to exit some loop waiting for interrupt #8
  rom[0x4528] = 0x00;
  rom[0x4529] = 0x00;

  membank("bank")->configure_entry(0, m_ram->pointer());
  membank("bank")->configure_entry(1, memregion("progrom")->base() + 0xe000);
}

void roland_u110_state::machine_reset() {
}

void roland_u110_state::port2_w(uint8_t data) {
  uint8_t bank = data >> 7;
  membank("bank")->set_entry(bank);
}

void roland_u110_state::lcd_ctrl_w(u8 data) {
	lcd->control_w(data);
  // cpu->ioc1_w(0xff);
  cpu->pulse_input_line(n8097bh_device::HSI0_LINE, attotime::from_hz(36584));
}

u8 roland_u110_state::lcd_ctrl_r() {
	return lcd->control_r() >> 7;
}

void roland_u110_state::lcd_data_w(u8 data) {
	lcd->data_w(data);
  // cpu->ioc1_w(0xff);
  cpu->pulse_input_line(n8097bh_device::HSI0_LINE, attotime::from_hz(36584));
}

void roland_u110_state::leds_w(u8 data) {
	// logerror("leds_w %02x\n", data);
}

u8 roland_u110_state::sw_scan_r() {
  // Simulate also the pullup on the remaining bits
	return m_sw->read() | 0b11000000;
}

void roland_u110_state::midi_w(u16 data) {
  logerror("midi_out %02x\n", data);
  midi = data;
}

TIMER_DEVICE_CALLBACK_MEMBER(roland_u110_state::midi_timer_cb) {
  // CPU doesn't have a proper serial interface so we are forced
  // to simulate it this way for now
  if (midi_queue.empty())
    return;
  u8 midi = midi_queue.front();
  midi_queue.pop();
  logerror("midi_in %02x\n", midi);
  cpu->serial_w(midi);
}

u16 roland_u110_state::analog0_r() {
  return (1.0f - (3.0f / 5.0f)) * 0xFFFF;
}

u8 roland_u110_state::pcmrom_r(offs_t offset) {
  // logerror("pcm rom read %02x\n", offset);
  const u8 *pcm_rom = memregion("pcm")->base();
  return pcm_rom[offset];
}

u16 roland_u110_state::snd_io_r(offs_t offset) {
  // lots of offset modification magic to achieve the following:
  //  - offsets 00..1F are "sound chip read"
  //  - offsets 20..3F are a readback of what was written to registers 00..1F
  //  - This behaviour is reversed for offset 01/21, which is used for reading
  //  the PCM sample tables.
  // All this is just for making debugging easier, as it allows one to check the
  // register state using the Memory Viewer.
  if (offset == 0x01 || offset == 0x21)
    offset ^= 0x20; // remove when PCM data readback via sound chip is confirmed to work
  if (offset < 0x20)
    return pcm->read(offset);
  if (offset < 0x40)
    offset -= 0x20;

  if (offset == 0x01) {
    // code for reading from the PCM sample table is at 0xB027
    // The code at 0xB0AC writes to 1411/1F (??), then 1403/02 (bank), then
    // 1409/08/0B/0A (address). It waits a few cycles and at 0xB0F7 it reads the
    // resulting data from 1401.
    offs_t bank = sound_io_buffer[0x03];
    offs_t addr = (sound_io_buffer[0x09] << 0) | (sound_io_buffer[0x0A] << 8) |
                  (sound_io_buffer[0x0B] << 16);
    addr = ((addr >> 6) + 2) & 0x3FFFF;
    addr |= (bank << 16);
    // write actual ROM address to 1440..1443 for debugging
    sound_io_buffer[0x43] = (addr >> 0) & 0xFF;
    sound_io_buffer[0x42] = (addr >> 8) & 0xFF;
    sound_io_buffer[0x41] = (addr >> 16) & 0xFF;
    sound_io_buffer[0x40] = (addr >> 24) & 0xFF;
    return pcmrom_r(addr);
  }
  return sound_io_buffer[offset];
}

void roland_u110_state::snd_io_w(offs_t offset, u16 data) {
  // register map
  // ------------
  // Note: 16-bit words are Little Endian, the firmware writes the odd byte is
  // first
  //  00/01 - ??
  //  02/03 - ROM bank (only bits 11-13 are used, bit 11 = PCM card, bits 12-13
  //  select between IC18/19/20) 04/05 - frequency (2.14 fixed point, 0x4000 =
  //  32000 Hz) 06/07 - volume 08/09 - sample start address, fraction (2.14
  //  fixed point, i.e. 1 byte = 0x4000) 0A/0B - sample start address (high
  //  word, i.e. address bits 2..17) 0C/0D - sample end address (high word)
  //  0E/0F - sample loop address (high word)
  //  11/13/15/17 - voice enable mask (11 = least significant 8 bits, 17 = most
  //  significant 8 bits) 1A - ?? 1F - voice select
  if (offset < 0x20) {
    pcm->write(offset, data & 0xFF);
    pcm->write(offset + 1, (data >> 8) & 0xFF);
  }
  sound_io_buffer[offset] = data & 0xFF;
  sound_io_buffer[offset + 1] = (data >> 8) & 0xFF;
}

void roland_u110_state::lcd_palette(palette_device &palette) const {
  palette.set_pen_color(0, rgb_t(0, 0, 0));
	palette.set_pen_color(1, rgb_t(0, 255, 0));
}

void roland_u110_state::u110_map(address_map &map) {
  map(0x0100, 0x0fff).rom().region("progrom", 0x0100);
  map(0x1100, 0x1100).rw(FUNC(roland_u110_state::lcd_ctrl_r), FUNC(roland_u110_state::lcd_ctrl_w));
  map(0x1102, 0x1102).w(FUNC(roland_u110_state::lcd_data_w));
  map(0x1200, 0x12ff).w(FUNC(roland_u110_state::leds_w));
  map(0x1300, 0x13ff).r(FUNC(roland_u110_state::sw_scan_r));
	map(0x1400, 0x14ff).rw(FUNC(roland_u110_state::snd_io_r), FUNC(roland_u110_state::snd_io_w));
  map(0x2000, 0x20ff).rom().region("progrom", 0x2000);
  map(0x2100, 0x3fff).ram();
  map(0x4000, 0xdfff).rom().region("progrom", 0x4000);
  map(0xe000, 0xffff).bankrw("bank");
}

void roland_u110_state::u110(machine_config &config) {
  i8x9x_device &maincpu(N8097BH(config, cpu, 12_MHz_XTAL));
  maincpu.set_addrmap(AS_PROGRAM, &roland_u110_state::u110_map);
  maincpu.set_addrmap(AS_IO, &roland_u110_state::u110_map);
  maincpu.serial_tx_cb().set(FUNC(roland_u110_state::midi_w));
  maincpu.ach0_cb().set(FUNC(roland_u110_state::analog0_r));
  maincpu.out_p2_cb().set(FUNC(roland_u110_state::port2_w));

  RAM(config, m_ram).set_default_size("64K");

  SPEAKER(config, "lspeaker").front_left();
  SPEAKER(config, "rspeaker").front_right();

  MB87419_MB87420(config, pcm, 32.768_MHz_XTAL);
  pcm->int_callback().set_inputline(cpu, i8x9x_device::EXTINT_LINE);
  pcm->add_route(0, "lspeaker", 1.0);
  pcm->add_route(1, "rspeaker", 1.0);

  screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
  screen.set_refresh_hz(50);
  screen.set_screen_update(FUNC(roland_u110_state::screen_update));
  // screen.set_size(16 * 6 - 1, (16 * 6 - 1) * 3 / 4);
  // screen.set_visarea(0, 16 * 6 - 2, 0, (16 * 6 - 1) * 3 / 4 - 1);
  screen.set_palette("palette");
  PALETTE(config, "palette", FUNC(roland_u110_state::lcd_palette), 2);
  HD44780(config, lcd, 270'000);
  lcd->set_lcd_size(2, 16);

	TIMER(config, midi_timer).configure_periodic(FUNC(roland_u110_state::midi_timer_cb), attotime::from_hz(1250));

  midi_port_device &mdin(MIDI_PORT(config, "mdin", midiin_slot, "midiin"));
  mdin.rxd_handler().set([this](int state) {
    if (m_midi_pos == 0 || m_midi_pos == 9) {
      m_midi_pos += 1;
    } else if (m_midi_pos == 10) {
      midi_queue.push(m_midi_rx);
      m_midi_rx = 0;
      m_midi_pos = 0;
    } else {
      m_midi_rx |= state << (m_midi_pos - 1);
      m_midi_pos += 1;
    }
  });

  MIDI_PORT(config, "mdout", midiout_slot, "midiout");

  MIDI_PORT(config, "mdthru", midiout_slot, "midiout");
}

void roland_u110_state::init_u110()
{
	// Roland did a fair amount of scrambling on the address and data lines.
	// Only the first 0x80 bytes of the ROMs are readable text in a raw dump.
	// The U-110 actually checks some of these header bytes, but it uses post-scrambling variants of offsets/values.
	u8* src = static_cast<u8*>(memregion("pcmorg")->base());
	u8* dst = static_cast<u8*>(memregion("pcm")->base());
	// descramble internal ROMs
	descramble_rom_internal(&dst[0x000000], &src[0x000000]);
	descramble_rom_internal(&dst[0x100000], &src[0x100000]);
	descramble_rom_internal(&dst[0x200000], &src[0x200000]);
	descramble_rom_internal(&dst[0x300000], &src[0x300000]);
}

void roland_u110_state::descramble_rom_internal(u8* dst, const u8* src)
{
	for (offs_t srcpos = 0x00; srcpos < 0x80000; srcpos ++)
	{
		offs_t dstpos = UNSCRAMBLE_ADDR_INT(srcpos);
		dst[dstpos] = UNSCRAMBLE_DATA(src[srcpos]);
	}
}

void roland_u110_state::descramble_rom_external(u8* dst, const u8* src)
{
	for (offs_t srcpos = 0x00; srcpos < 0x80000; srcpos ++)
	{
		offs_t dstpos = UNSCRAMBLE_ADDR_EXT(srcpos);
		dst[dstpos] = UNSCRAMBLE_DATA(src[srcpos]);
	}
}

ROM_START(u110)
	ROM_REGION(0x10000, "progrom", 0)
	ROM_SYSTEM_BIOS(0, "v203", "Version 2.03")
	ROMX_LOAD("u110.bin", 0x00000, 0x10000, CRC(73e70eae) SHA1(ec0d76d325f27eddd7d8ac5530221137dbde6db1), ROM_BIOS(0))

	ROM_REGION( 0x400000, "pcmorg", 0 ) // ROMs before descrambling
	ROM_LOAD( "roland_t110_u110_u220_waverom0.bin", 0x000000, 0x80000, CRC(d5475560) SHA1(55d36199ea489969a729782afe4cbff5b1cede0e) )
	ROM_LOAD( "roland_t110_u110_u220_waverom1.bin", 0x100000, 0x80000, CRC(43a5b26c) SHA1(8b8067fbc6fa7b2e8694a3785f123627ea458d1f) )
	ROM_LOAD( "roland_t110_u110_u220_waverom2.bin", 0x200000, 0x80000, CRC(e79ee88a) SHA1(8dd9826d7d3e67b9a568eadeaaf7856a05f3e068) )
	ROM_LOAD( "roland_t110_u110_u220_waverom3.bin", 0x300000, 0x80000, CRC(da9fbf6d) SHA1(35b98a0fb5cf643fb7b761a74581486eee3bd8c3) )
	ROM_REGION( 0x400000, "pcm", ROMREGION_ERASEFF )    // ROMs after descrambling
ROM_END

} // anonymous namespace

SYST(1988, u110, 0, 0, u110, u110, roland_u110_state, init_u110, "Roland", "U-110 PCM Sound Module", MACHINE_NOT_WORKING | MACHINE_IMPERFECT_SOUND)
