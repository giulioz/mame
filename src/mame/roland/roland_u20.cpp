// license:BSD-3-Clause
// copyright-holders:AJR
/****************************************************************************

    Skeleton driver for Roland U-20 & related synthesizers.

****************************************************************************/


// SRAM
// 0400-04ff  buffer? FF
// 2000-27ff  multisamples?
// 700a-7039  LCD out buffer
// 7100-73ff  LCD another buffer?
// 709a-709b  Buttons state inverted
// 709c-709d  Buttons state raw
// 7f80-7fff  stack (fixed area 1xxx)

// INTs
// SoftTimer: handles front panel buttons + some magic?
// HSI0 (IOGA): handles lcd fifo
// SerPort: handles MIDI
// ExtInt: handle LP int

// ROM
// 1A000: RCC initial RAM data


#include "emu.h"
#include "bus/midi/midiinport.h"
#include "cpu/mcs96/i8x9x.h"
#include "machine/ram.h"
#include "machine/timer.h"
#include "sound/roland_lp.h"
#include "sound/roland_rcc.h"
#include "video/hd44780.h"
#include "emupal.h"
#include "screen.h"
#include "speaker.h"
#include "multibyte.h"

#include <cstdlib>
#include <queue>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

static int open_serial(const char *path) {
    int fd = open(path, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        perror("open serial");
        return -1;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        close(fd);
        return -1;
    }

    // configure raw mode
    cfmakeraw(&tty);

    // set baud rate (match your arduino sketch)
    // cfsetspeed(&tty, B115200);
    cfsetspeed(&tty, 921600);

    tty.c_cflag |= (CLOCAL | CREAD);  // ignore modem control lines, enable receiver
    tty.c_cc[VMIN]  = 1;              // wait for at least 1 byte
    tty.c_cc[VTIME] = 5;              // timeout: 0.5s

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }

    return fd;
}

[[maybe_unused]] static uint8_t read_byte(int fd) {
  uint8_t b;
  int n = read(fd, &b, 1);
  if (n != 1) {
      perror("read");
      return 0xFF; // or throw
  }
  return b;
}

[[maybe_unused]] static void write_bytes(int fd, const uint8_t *data, size_t len) {
  size_t total = 0;
  while (total < len) {
      ssize_t n = write(fd, data + total, len - total);
      if (n < 0) {
          perror("write");
          break;
      }
      total += n;
  }
}

static int serial_fd;

[[maybe_unused]] static uint8_t bus_r(int ofs) {
	uint8_t buf[4] = {0};
	buf[0] = 'r';
	buf[1] = (ofs >> 8) & 0xff;
	buf[2] = ofs & 0xff;
	buf[3] = 0x00;
	write_bytes(serial_fd, buf, 4);
	uint8_t res = read_byte(serial_fd);
	printf("bus read %04x=%02x\n", ofs, res);
	return res;
}

[[maybe_unused]] static uint8_t bus_w(int ofs, uint8_t data) {
	uint8_t buf[4] = {0};
	buf[0] = 'w';
	buf[1] = (ofs >> 8) & 0xff;
	buf[2] = ofs & 0xff;
	buf[3] = data;
	write_bytes(serial_fd, buf, 4);
	uint8_t res = read_byte(serial_fd);
	printf("bus write %04x=%02x\n", ofs, data);
	return res;
}


namespace {

// unscramble address: ROM dump offset -> proper (descrambled) offset
#define UNSCRAMBLE_ADDR_INT(_offset) \
	bitswap<19>(_offset,18,17,15,14,16,12,11, 7, 9,13,10, 8, 3, 2, 1, 6, 4, 5, 0)
// scramble address: proper offset -> ROM dump offset
#define SCRAMBLE_ADDR_INT(_offset) \
	bitswap<19>(_offset,18,17,14,16,15, 9,13,12, 8,10, 7,11, 3, 1, 2, 6, 5, 4, 0)

// PCM cards use a different address line scrambling
#define UNSCRAMBLE_ADDR_EXT(_offset) \
	bitswap<19>(_offset,18,17, 8, 9,16,11,12, 7,14,10,13,15, 3, 2, 1, 6, 4, 5, 0)

#define UNSCRAMBLE_DATA(_data) \
	bitswap<8>(_data,1,2,7,3,5,0,4,6)

class roland_u20_state : public driver_device
{
public:
	roland_u20_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_pcm(*this, "pcm")
		, m_rcc(*this, "rcc")
		, lcd(*this, "lcd")
		, midi_timer(*this, "midi_timer")
		, lp_ram(*this, "lp_ram")
		, sw0(*this, "SW0")
		, sw1(*this, "SW1")
		, sw2(*this, "SW2")
	{
	}

	void u20(machine_config &config);
	void u220(machine_config &config);

	void init_u20();

	DECLARE_INPUT_CHANGED_MEMBER(button);

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

private:
	void lcd_palette(palette_device &palette) const;
  HD44780_PIXEL_UPDATE(lcd_pixel_update);

	void midi_w(u16 data);
	void midi_in_w(int state);

	u8 ga_bank_r(offs_t offset);
	void ga_bank_w(offs_t offset, u8 data);
	u8 bank_r_800(offs_t offset);
	void bank_w_800(offs_t offset, u8 data);
	u8 fixed_r_000(offs_t offset);
	void fixed_w_000(offs_t offset, u8 data);
	u8 fixed_r_1000(offs_t offset);
	void fixed_w_1000(offs_t offset, u8 data);

	u8 dsp_io_r(offs_t offset);
	void dsp_io_w(offs_t offset, u8 data);

	TIMER_DEVICE_CALLBACK_MEMBER(midi_timer_cb);
	TIMER_DEVICE_CALLBACK_MEMBER(test_timer_cb);

	void u220_map(address_map &map) ATTR_COLD;

	void descramble_rom_internal(u8* dst, const u8* src);
	void descramble_rom_external(u8* dst, const u8* src);

	u8 midi;
	u8 m_midi_rx = 0;
	u8 m_midi_pos = 0;
	u8 bank_00 = 0;
	u8 bank_01 = 0;
	u8 sram[0x8000];

	std::queue<uint8_t> midi_queue;

	required_device<i8x9x_device> m_maincpu;
	required_device<mb87419_mb87420_device> m_pcm;
	required_device<roland_rcc_device> m_rcc;
	required_device<hd44780_device> lcd;
	required_device<timer_device> midi_timer;
	required_device<ram_device> lp_ram;
	required_ioport sw0;
	required_ioport sw1;
	required_ioport sw2;
};

void roland_u20_state::lcd_palette(palette_device &palette) const {
  palette.set_pen_color(0, rgb_t(0, 0, 0));
	palette.set_pen_color(1, rgb_t(0, 255, 0));
}

HD44780_PIXEL_UPDATE(roland_u20_state::lcd_pixel_update) {
  if (x < 5 && y < 8 && line < 2 && pos < 24)
		bitmap.pix(line * 8 + y, pos * 6 + x) = state;
}

void roland_u20_state::machine_start()
{
	// u8 *rom = memregion("progrom")->base();

	// rom[0xa3b5] = 0xfd;
	// rom[0xa3b6] = 0xfd;

	// rom[0xa254] = 0xfd;
	// rom[0xa255] = 0xfd;

	// rom[0x283f] = 0xfd;
	// rom[0x2840] = 0xfd;
	// rom[0x2841] = 0xfd;

	save_item(NAME(bank_00));
	save_item(NAME(bank_01));
	save_item(NAME(sram));
	save_item(NAME(m_midi_rx));
	save_item(NAME(m_midi_pos));

	serial_fd = open_serial("/dev/cu.usbmodem1101");

	FILE *ramf = fopen("u20.ram", "rb");
	if (ramf) {
		fread(sram, 1, sizeof(sram), ramf);
		fclose(ramf);
	}
}

void roland_u20_state::machine_reset()
{
	m_midi_rx = 0;
	m_midi_pos = 0;
}

void roland_u20_state::midi_w(u16 data)
{
	logerror("midi_out %02x\n", data);
	midi = data;
}

void roland_u20_state::midi_in_w(int state)
{
	// The MIDI slot supplies 31.25 kbaud 8-N-1.  Convert it to bytes for the
	// current MCS-96 serial shim, then let midi_timer_cb pace delivery so SBUF
	// cannot be overwritten while the firmware services its serial interrupt.
	if (m_midi_pos == 0)
	{
		if (!state)
		{
			m_midi_rx = 0;
			m_midi_pos = 1;
		}
	}
	else if (m_midi_pos <= 8)
	{
		m_midi_rx |= state << (m_midi_pos - 1);
		m_midi_pos++;
	}
	else
	{
		if (state)
			midi_queue.push(m_midi_rx);
		m_midi_rx = 0;
		m_midi_pos = 0;
	}
}

TIMER_DEVICE_CALLBACK_MEMBER(roland_u20_state::midi_timer_cb)
{
	// const static u8 midi_data[3] = { 0x9a, 0x40, 0x7f };
	// midi = midi_data[midi_pos++];
	// logerror("midi_in %02x\n", midi);
	// m_maincpu->serial_w(midi);
	// if(midi_pos < sizeof(midi_data))
	// 	midi_timer->adjust(attotime::from_hz(1250));

	if (!midi_queue.empty()) {
		midi = midi_queue.front();
		midi_queue.pop();
		m_maincpu->serial_w(midi);
	}
}

TIMER_DEVICE_CALLBACK_MEMBER(roland_u20_state::test_timer_cb)
{
	m_maincpu->set_input_line(i8x9x_device::HSI0_LINE, ASSERT_LINE); // lcd

	// m_maincpu->set_input_line(i8x9x_device::EXTINT_LINE, ASSERT_LINE); // lp
}

u8 roland_u20_state::dsp_io_r(offs_t offset)
{
	return m_rcc->read(offset);
}

void roland_u20_state::dsp_io_w(offs_t offset, u8 data)
{
	m_rcc->write(offset, data);
}

u8 roland_u20_state::ga_bank_r(offs_t offset)
{
	if (offset == 0)
		return bank_00;
	else if (offset == 1)
		return bank_01;

	printf("GA read %02x\n", offset);
	return 0;
}
void roland_u20_state::ga_bank_w(offs_t offset, u8 data)
{
	if (offset == 0) {
		bank_00 = data;
		// if (bank_00 == 0x40 || bank_00 == 0x60) bus_w(0x1000, data);
		// printf("%04x: banksel 00: %02x\n", m_maincpu->pc(), data);
	}
	else if (offset == 1) {
		bank_01 = data;
		// printf("banksel 01: %02x\n", data);
	}
	else {
		printf("GA write %02x: %02x %c\n", offset, data, data);
	}
}

u8 roland_u20_state::bank_r_800(offs_t offset)
{
	if (bank_00 == 0x30) {
		// buttons
		// if (!machine().side_effects_disabled()) printf("buttons read %04x\n", offset);
		if (offset == 0x0d)
			return sw0->read() | 0b11000000;
		else if (offset == 0x0e)
			return sw1->read() | 0b11000000;
		else
			return 0xff;
	}

	else if (bank_00 == 0x40) {
		return dsp_io_r(offset);
		// return 0xff;

		// if (machine().side_effects_disabled()) return 0xff;
		// return bus_r(0x800+offset);
	}

	else if (bank_00 == 0x60) {
		return m_pcm->read(offset);
		// return 0xff;

		// if (machine().side_effects_disabled()) return 0xff;
		// return bus_r(0x800+offset);
	}

	else if (bank_00 == 0x10) {
		// m_maincpu->set_input_line(i8x9x_device::HSI0_LINE, CLEAR_LINE);
		// printf("lcd_r: %04x\n", offset);
		// return lcd_ctrl_r();
		// if (!machine().side_effects_disabled()) printf("lcd read %02x %04x\n", bank_00, offset);

		return 0x00;
	}

	else if (bank_00 >= 0x80) {
		offs_t dest = 0x10000 + ((bank_00 - 0x80) << 11) + offset;
		if (dest >= 0x20000) {
			if (!machine().side_effects_disabled()) printf("bank_r_800: out of range read %02x offset %04x -> %05x\n", bank_00, offset, dest);
			return 0;
		}

		return memregion("progrom")->base()[dest];
	}

	else if (bank_00 < 0x10) {
		return sram[offset + (bank_00 << 11)];
	}

	else {
		// if (!machine().side_effects_disabled()) printf("bank_r_800: bank %02x offset %04x\n", bank_00, offset);
	}

	return 0xff;
}
void roland_u20_state::bank_w_800(offs_t offset, u8 data)
{
	if (bank_00 == 0x40) {
		dsp_io_w(offset, data);
		// bus_w(0x800+offset, data);
	}

	else if (bank_00 == 0x60) {
		m_pcm->write(offset, data, m_maincpu->pc());
		// bus_w(0x800+offset, data);
	}

	else if (bank_00 == 0x10) {
		// printf("lcd_w: %04x = %02x %c\n", offset, data, data);
		if (offset == 1) {
			lcd->data_w(data);
		} else if (offset == 0) {
			lcd->control_w(data);
		}

		m_maincpu->set_input_line(i8x9x_device::HSI0_LINE, CLEAR_LINE);
	}

	else if (bank_00 < 0x10) {
		FILE *ramf = fopen("u20.ram", "wb");
		if (ramf) {
			fwrite(sram, 1, sizeof(sram), ramf);
			fclose(ramf);
		}
		sram[offset + (bank_00 << 11)] = data;
	}

	else {
		// printf("bank_w_800: bank %02x offset %04x data %02x %c\n", bank_00, offset, data, data);
	}
}

u8 roland_u20_state::fixed_r_000(offs_t offset)
{
	if (!machine().side_effects_disabled()) printf("fixed_r_000: offset %04x\n", offset);
	return 0xff;
}

void roland_u20_state::fixed_w_000(offs_t offset, u8 data)
{
	printf("fixed_w_000: offset %04x data %02x %c\n", offset, data, data);
}

u8 roland_u20_state::fixed_r_1000(offs_t offset)
{
	return sram[offset + 0x7000 + 2];
}

void roland_u20_state::fixed_w_1000(offs_t offset, u8 data)
{
	sram[offset + 0x7000 + 2] = data;
}


void roland_u20_state::u220_map(address_map &map)
{
	// From hw:
	// 0x0000-0x07ff fixed ??
	// 0x0800-0x0fff banked
	// 0x1000-0x1fff ram fixed 0x7000-0x7fff
	// 0x2000-0xffff rom fixed 0x2000-0xffff

	// bank 00-0f: ram (ofs-0x800) + (bank * 0x800)
	// bank 80-9f: eprom (ofs-0x800) + ((bank-0x80) * 0x800) + 0x10000

	// bank 10: lcd (?)
	// bank 30: front panel buttons (0d/0e)
	// bank 40: dsp
	// bank 60: lp
	// bank 70: keyscan (?)

	// bank 30?
	// bank 70?

	map(0x0000, 0x07ff).rw(FUNC(roland_u20_state::fixed_r_000), FUNC(roland_u20_state::fixed_w_000));
	map(0x0800, 0x0fff).rw(FUNC(roland_u20_state::bank_r_800), FUNC(roland_u20_state::bank_w_800));
	map(0x1000, 0x1001).rw(FUNC(roland_u20_state::ga_bank_r), FUNC(roland_u20_state::ga_bank_w));
	map(0x1002, 0x1fff).rw(FUNC(roland_u20_state::fixed_r_1000), FUNC(roland_u20_state::fixed_w_1000));
	map(0x2000, 0xffff).rom().region("progrom", 0x2000);
}

void roland_u20_state::u20(machine_config &config)
{
	P8098(config, m_maincpu, 12_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &roland_u20_state::u220_map);
	m_maincpu->serial_tx_cb().set(FUNC(roland_u20_state::midi_w));

	//R15239124(config, "keyscan", 12_MHz_XTAL);

	SPEAKER(config, "speaker", 2).front();

	MB87419_MB87420(config, m_pcm, 32.768_MHz_XTAL);
	m_pcm->int_callback().set_inputline(m_maincpu, i8x9x_device::EXTINT_LINE);
	ROLAND_RCC(config, m_rcc, 32'000);
	// Preserve the LP time slots through the device boundary.  The temporary
	// RCC decodes only the firmware's dry L/R coefficients; effects and the
	// physical direct-output assignments are intentionally bypassed.
	for (unsigned output = 0; output < mb87419_mb87420_device::NUM_CHANNELS; output++)
		m_pcm->add_route(output, "rcc", 1.0, output);
	m_rcc->add_route(0, "speaker", 1.0, 0);
	m_rcc->add_route(1, "speaker", 1.0, 1);

	RAM(config, lp_ram).set_default_size("16K");

	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
  screen.set_refresh_hz(50);
  screen.set_screen_update("lcd", FUNC(hd44780_device::screen_update));
  screen.set_palette("palette");
  screen.set_size(6*24, 8*2);
  screen.set_visarea_full();
  PALETTE(config, "palette", FUNC(roland_u20_state::lcd_palette), 2);
  HD44780(config, lcd, 270'000);
  lcd->set_lcd_size(2, 24);
  lcd->set_pixel_update_cb(FUNC(roland_u20_state::lcd_pixel_update));

	// Leave a little idle time between debug-injected MIDI bytes.  The
	// current serial input API supplies complete bytes rather than bits,
	// so minimum-spacing packets can otherwise overwrite SBUF.
	TIMER(config, midi_timer).configure_periodic(FUNC(roland_u20_state::midi_timer_cb), attotime::from_hz(1000));

	midi_port_device &mdin(MIDI_PORT(config, "mdin", midiin_slot, "midiin"));
	mdin.rxd_handler().set(FUNC(roland_u20_state::midi_in_w));

	TIMER(config, "test_timer").configure_periodic(FUNC(roland_u20_state::test_timer_cb), attotime::from_hz(10000));
}

void roland_u20_state::u220(machine_config &config)
{
	u20(config);

	//config.device_remove("keyscan");
}

void roland_u20_state::init_u20()
{
	// Roland did a fair amount of scrambling on the address and data lines.
	// Only the first 0x80 bytes of the ROMs are readable text in a raw dump.
	// The CM-32P actually checks some of these header bytes, but it uses post-scrambling variants of offsets/values.
	u8 *src = reinterpret_cast<u8 *>(memregion("pcmorg")->base());
	u8 *dst = reinterpret_cast<u8 *>(memregion("pcm")->base());
	// descramble internal ROMs
	descramble_rom_internal(&dst[0x000000], &src[0x000000]); // INT008 0.08
	descramble_rom_internal(&dst[0x080000], &src[0x080000]); // u20-1-371.00
	descramble_rom_internal(&dst[0x100000], &src[0x100000]); // "00000000 00000301 00FF01FF 00FEFEFD"
	descramble_rom_internal(&dst[0x180000], &src[0x180000]); // U20-2-270.27
	descramble_rom_internal(&dst[0x200000], &src[0x200000]); // "03010402 04030504 050506"
	// 0x280000 card A
	descramble_rom_internal(&dst[0x300000], &src[0x300000]); // "01FF01FC FFFE00FC"
	// 0x380000 card B
}

void roland_u20_state::descramble_rom_internal(u8* dst, const u8* src)
{
	for (offs_t srcpos = 0x00; srcpos < 0x80000; srcpos++)
	{
		offs_t dstpos = UNSCRAMBLE_ADDR_INT(srcpos);
		dst[dstpos] = UNSCRAMBLE_DATA(src[srcpos]);
	}
}

void roland_u20_state::descramble_rom_external(u8* dst, const u8* src)
{
	for (offs_t srcpos = 0x00; srcpos < 0x80000; srcpos++)
	{
		offs_t dstpos = UNSCRAMBLE_ADDR_EXT(srcpos);
		dst[dstpos] = UNSCRAMBLE_DATA(src[srcpos]);
	}
}

INPUT_CHANGED_MEMBER(roland_u20_state::button) {
  // DEBUG
  if (sw2->read() & 0x1) {
		// Pan-discrimination capture (RCC_PAN_SWEEP): each press re-strikes the
		// debug note at a stepped CC10 pan value so the firmware writes fresh,
		// UNEQUAL L/R voice gains into the RCC RAM-B parameter file.  With
		// RCC_DUMP_RAMB logging, the per-pan bytes reveal which slot carries the
		// L gain, which the R gain, and the pan law -- the discriminating anchor
		// the center-pan sound-test states cannot provide.
		if (std::getenv("RCC_PAN_SWEEP")) {
			static int pan_step = 0;
			int pan = pan_step * 8;
			if (pan > 127) pan = 127;
			midi_queue.push(0x80); midi_queue.push(0x3c); midi_queue.push(0x00); // note off
			midi_queue.push(0xB0); midi_queue.push(0x0A); midi_queue.push(u8(pan)); // CC10 pan
			midi_queue.push(0x90); midi_queue.push(0x3c); midi_queue.push(0x70); // note on
			printf("PAN SWEEP step %d pan %d\n", pan_step, pan);
			pan_step++;
		} else {
			printf("NOTE ON\n");
			midi_queue.push(0x90);
			midi_queue.push(0x3c);
			midi_queue.push(0x70);
		}
	} else if (!std::getenv("RCC_PAN_SWEEP")) {
		printf("NOTE OFF\n");
		midi_queue.push(0x80);
		midi_queue.push(0x3c);
		midi_queue.push(0x00);
	}
}

static INPUT_PORTS_START(u20)
	PORT_START("SW0")
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("SW1")
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("SW2")
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)
INPUT_PORTS_END

static INPUT_PORTS_START(u220)
	PORT_START("SW1")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Edit / Chorus")       PORT_CODE(KEYCODE_Q)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Data / Reverb")      PORT_CODE(KEYCODE_W)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Exit")               PORT_CODE(KEYCODE_E)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Enter")              PORT_CODE(KEYCODE_R)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Part/Inst Left")     PORT_CODE(KEYCODE_T)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Part/Inst Right")    PORT_CODE(KEYCODE_Y)

	PORT_START("SW0")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Mark")           PORT_CODE(KEYCODE_A)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Jump")           PORT_CODE(KEYCODE_S)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Cursor Left")    PORT_CODE(KEYCODE_D)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Cursor Right")   PORT_CODE(KEYCODE_F)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Value Down")      PORT_CODE(KEYCODE_G)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Value Up")        PORT_CODE(KEYCODE_H)

	PORT_START("SW2")
  PORT_BIT(0x1, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("DEBUG") PORT_CODE(KEYCODE_P) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(roland_u20_state::button), 0)
INPUT_PORTS_END

ROM_START(u20)
	ROM_REGION(0x20000, "progrom", 0)
	ROM_SYSTEM_BIOS(0, "v303", "Version 3.03")
	ROMX_LOAD("u-20-v303.bin", 0x00000, 0x20000, CRC(28ce7fca) SHA1(d4186a7034a0646e1c76a3a7f5c3cf5f165ace80), ROM_BIOS(0))
	ROM_SYSTEM_BIOS(1, "v103", "Version 1.03")
	ROMX_LOAD("u-20-v103.bin", 0x00000, 0x20000, CRC(eb94054f) SHA1(1127e21ba94cc629eb00355c0be6ace6ca7759d6), ROM_BIOS(1)) // M5M27C100P

	ROM_REGION(0x400000, "pcmorg", ROMREGION_ERASE00)
	ROM_LOAD("roland-a_r15179892f_mb834000a-20_226-aa.ic27", 0x000000, 0x080000, NO_DUMP)
	ROM_LOAD("roland-b_r15179893f_mb834000a-20_227-aa.ic28", 0x080000, 0x080000, NO_DUMP)
	ROM_LOAD("roland-c_r15179894f_mb834000a-20_228-aa.ic29", 0x100000, 0x080000, NO_DUMP)
	ROM_LOAD("roland-d_r15179895f_mb834000a-20_229-aa.ic30", 0x180000, 0x080000, NO_DUMP)
	ROM_LOAD("roland-e_r15179947_mb834000a-20_3a1-aa.ic31",  0x200000, 0x080000, NO_DUMP)
	ROM_LOAD("roland-f_r15179948_mb834000a-20_3a2-aa.ic32",  0x280000, 0x080000, NO_DUMP)
	ROM_REGION(0x400000, "pcm", ROMREGION_ERASEFF) // ROMs after descrambling
ROM_END

ROM_START(u220)
	ROM_REGION(0x20000, "progrom", 0)
	ROM_SYSTEM_BIOS(0, "v102", "Version 1.02")
	ROMX_LOAD("roland_u220_pgm_v1-02_15209245_lh530847.bin", 0x00000, 0x20000, CRC(d5492e9b) SHA1(9d72b1688a173505b5e1c5ddac01efa78ee76c7f), ROM_BIOS(0))
	ROM_SYSTEM_BIOS(1, "v101", "Version 1.01")
	ROMX_LOAD("u-220_roland_1-0-1.ic8", 0x00000, 0x20000, CRC(893afa9c) SHA1(d0b66c0ea0e3af284a1806226aed79d8da2f3dd4), ROM_BIOS(1)) // HN27C101G-20

	ROM_REGION(0x600000, "pcmorg", ROMREGION_ERASE00)
	ROM_LOAD("roland_t110_u110_u220_waverom0.bin", 0x000000, 0x80000, CRC(d5475560) SHA1(55d36199ea489969a729782afe4cbff5b1cede0e)) // INT008 0.08
	ROM_LOAD("roland_u220_waverom4_sn-u110-08.bin", 0x080000, 0x80000, CRC(104f3974) SHA1(71b6f97df6dd4e573db5743c10c77e86d67965a3)) // u20-1-371.00
	ROM_LOAD("roland_t110_u110_u220_waverom1.bin", 0x100000, 0x80000, CRC(43a5b26c) SHA1(8b8067fbc6fa7b2e8694a3785f123627ea458d1f)) // "00000000 00000301 00FF01FF 00FEFEFD"
	ROM_LOAD("roland_u220_waverom5_sn-u110-09.bin", 0x180000, 0x80000, CRC(af1c2778) SHA1(aea715a0a554c071a2e912894805a8d819802e1d)) // U20-2-270.27
	ROM_LOAD("roland_t110_u110_u220_waverom2.bin", 0x200000, 0x80000, CRC(e79ee88a) SHA1(8dd9826d7d3e67b9a568eadeaaf7856a05f3e068)) // "03010402 04030504 050506"
	ROM_LOAD("roland_t110_u110_u220_waverom3.bin", 0x300000, 0x80000, CRC(da9fbf6d) SHA1(35b98a0fb5cf643fb7b761a74581486eee3bd8c3)) // "01FF01FC FFFE00FC"
	ROM_REGION(0x600000, "pcm", ROMREGION_ERASEFF) // ROMs after descrambling
ROM_END

} // anonymous namespace


SYST(1989, u20,  0, 0, u20,  u20, roland_u20_state, init_u20, "Roland", "U-20 RS-PCM Keyboard", MACHINE_NO_SOUND | MACHINE_NOT_WORKING)
SYST(1989, u220, 0, 0, u220, u220, roland_u20_state, init_u20, "Roland", "U-220 RS-PCM Sound Module", MACHINE_NO_SOUND | MACHINE_NOT_WORKING)
