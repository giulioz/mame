// license:BSD-3-Clause
// copyright-holders:giulioz

/*

  DSP chip access
  read addr*0x400 +6
  read addr*0x400 +7
  0a/09/08 (rom)                              addr=0  (load pgm/clear?)
  0a/09/08 (rom), 01/00 (param)               addr=1  (??)
  0a/09/08 (rom), 02 (param max 3)            addr=2  (??)
  0a/09/08 (rom), 03 (param)                  addr=4  (??)
  0a/09/08 (rom), 05/04 (param)               addr=8  (set param)
  0a/09/08 (rom), 03 (param), 05/04 (param)   addr=c  (??)

  0a/09/08: data?
  05/04/03/02/01/00: data?
  06: config?
  addr: amount to write?


  Test mode
  bp cfdea
  c102 = 0x20

 */

#include "cpu/nec/v25.h"
#include "emu.h"
#include "emupal.h"
#include "machine/ram.h"
#include "machine/timer.h"
#include "screen.h"
#include "softlist_dev.h"
#include "video/hd44780.h"
#include "machine/nvram.h"

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
    cfsetspeed(&tty, B115200);

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


namespace {

class roland_rsp550_state : public driver_device {
public:
  roland_rsp550_state(const machine_config &mconfig, device_type type, const char *tag)
      : driver_device(mconfig, type, tag), cpu(*this, "maincpu"),
        lcd(*this, "lcd"),
        m_sw0(*this, "SW0"),
        m_sw1(*this, "SW1") {}

  void rsp550(machine_config &config);

  DECLARE_INPUT_CHANGED_MEMBER(button);

protected:
  virtual void machine_start() override;
  virtual void machine_reset() override;

private:
  required_device<v25_device> cpu;
  required_device<hd44780_device> lcd;
  required_ioport m_sw0, m_sw1;

  u8 p2_r();
  void p2_w(u8 data);
  u8 pt_r();
  u8 current_sw = 0;

  u8 ga_r(offs_t ofs);
  void ga_w(offs_t ofs, u8 data);

  u8 dsp_r(offs_t ofs);
  void dsp_w(offs_t ofs, u8 data);
  u8 dsp_intf_buffer[16] = {0};
  u8 dsp_instr[0x400*3*2] = {0};
  u8 dsp_data[0x400*4*2] = {0};
  u8 dsp_ramctl[0x400*4*2] = {0};
  u8 dsp_unk2[0x400*2] = {0};

  int serial_fd = -1;

  void lcd_palette(palette_device &palette) const;

	uint32_t screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

  void rsp550_map(address_map &map);
};

INPUT_CHANGED_MEMBER(roland_rsp550_state::button) {
  if (m_sw1->read() & 0x100) {
    // DEBUG
    // FILE *f = fopen("rsp550_pgm.txt", "w");
    // for (int i = 0; i < 0x400 * 2; i++) {
    //   fprintf(f, "%03x: %02x %02x %02x\n", i, dsp_instr[i * 3 + 0], dsp_instr[i * 3 + 1], dsp_instr[i * 3 + 2]);
    // }
    // fclose(f);

    FILE *f = fopen("rsp550_pgm.txt", "w");
    // fprintf(f, "PRAM\n");
    // for (int i = 0x000; i < 0x200*2; i += 2) {
    //     fprintf(f, "%04x (%04x): %02x %02x\n", i, i/2*3, dsp_data[i+0], dsp_data[i+1]);
    // }
    // for (int i = 0x400*2; i < 0x400*2+0x200*2; i += 2) {
    //     fprintf(f, "%04x (%04x): %02x %02x\n", i, i/2*3, dsp_data[i+0], dsp_data[i+1]);
    // }
    // fprintf(f, "\n\nRAMCTL\n");
    // for (int i = 0x000; i < 0x200*2; i += 2) {
    //     fprintf(f, "%04x (%04x): %02x %02x\n", i, i/2*3, dsp_ramctl[i+0], dsp_ramctl[i+1]);
    // }
    // for (int i = 0x400*2; i < 0x400*2+0x200*2; i += 2) {
    //     fprintf(f, "%04x (%04x): %02x %02x\n", i, i/2*3, dsp_ramctl[i+0], dsp_ramctl[i+1]);
    // }
    // fprintf(f, "\n\nIRAM\n");
    // for (int i = 0x000; i < 0x200*3; i += 3) {
    //     fprintf(f, "%04x: %02x %02x %02x\n", i, dsp_instr[i+0], dsp_instr[i+1], dsp_instr[i+2]);
    // }
    // for (int i = 0x400*3; i < 0x400*3+0x200*3; i += 3) {
    //     fprintf(f, "%04x: %02x %02x %02x\n", i, dsp_instr[i+0], dsp_instr[i+1], dsp_instr[i+2]);
    // }
    for (int i = 0x000; i < 0x400; i++) {
        fprintf(f, "%04x: %02x%02x%02x %02x%02x %02x%02x %02x%02x\n", i,
                dsp_instr[i*3+0], dsp_instr[i*3+1], dsp_instr[i*3+2],
                dsp_data[i*2+0], dsp_data[i*2+1],
                dsp_ramctl[i*2+0], dsp_ramctl[i*2+1],
                dsp_unk2[i*2+0], dsp_unk2[i*2+1]
        );
    }
    fclose(f);
  }
}

uint32_t roland_rsp550_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
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

void roland_rsp550_state::machine_start() {
  save_item(NAME(dsp_instr));
  save_item(NAME(dsp_data));
  save_item(NAME(dsp_ramctl));
  save_item(NAME(dsp_unk2));

  serial_fd = open_serial("/dev/cu.usbmodem101");
}

void roland_rsp550_state::machine_reset() {
}

void roland_rsp550_state::lcd_palette(palette_device &palette) const {
  palette.set_pen_color(0, rgb_t(0, 0, 0));
	palette.set_pen_color(1, rgb_t(0, 255, 0));
}

u8 roland_rsp550_state::p2_r() {
  return 0x00;
}

void roland_rsp550_state::p2_w(u8 data) {
  current_sw = data & 0x3;
  // if ((data & 0x01) != 0) {
  //   current_sw = 1;
  // } else if ((data & 0x02) != 0) {
  //   current_sw = 0;
  // }
  // logerror("RSP-550: p2_w %02x=%02x\n", data, pt_r());
}

u8 roland_rsp550_state::pt_r() {
  if (current_sw == 0) {
    return m_sw0->read() | m_sw1->read();
  } else if (current_sw == 1) {
    return m_sw1->read();
  } else if (current_sw == 2) {
    return m_sw0->read();
  }

  return 0xff;
}

u8 roland_rsp550_state::ga_r(offs_t ofs) {
  if (!machine().side_effects_disabled()) {
    printf("RSP-550: ga_r %06x\n", ofs);
  }
  return 0x00;
}

void roland_rsp550_state::ga_w(offs_t ofs, u8 data) {
  if (ofs == 0x04) {
    // GA port BK
    // printf("RSP-550: ga_w %06x=%02x\n", ofs, data);
  } else if (ofs == 0x03) {
    // GA port LEDs
    // printf("RSP-550: ga_w %06x=%02x\n", ofs, data);
  } else if (ofs == 0x02) {
    // GA port LCD contrast
  } else {
    // printf("RSP-550: ga_w %06x=%02x\n", ofs, data);
  }
}

// int cnt = 0;

u8 roland_rsp550_state::dsp_r(offs_t ofs) {
  uint8_t result = 0x00;

  if (!machine().side_effects_disabled()) {
    // uint8_t buf[4] = {0};
    // buf[0] = 'r';
    // buf[1] = (ofs >> 8) & 0xff;
    // buf[2] = ofs & 0xff;
    // buf[3] = 0x00;
    // write_bytes(serial_fd, buf, 4);
    // result = read_byte(serial_fd);

    // logerror("RSP-550: dsp_r %06x\n", ofs);
    printf("RSP-550 (%05x): dsp_r %06x=%02x\n", cpu->pc(), ofs, result);
  }

  // if (!machine().side_effects_disabled() && (ofs == 0x406 || ofs == 0x006)) {
  //   // result = dsp_instr[0xc8e];
    
  //   result = 0xaa;
  //   switch (cnt)
  //   {
  //     case 0:
  //     case 1:
  //     case 2:
  //     case 6:
  //     case 7:
  //     case 8:
  //     case 12:
  //     case 13:
  //     case 14:
  //     case 15:
  //     case 16:
  //     case 20:
  //     case 21:
  //     case 22:
  //       result = 0x55;
  //       break;
  //   }

  //   // DEBUG
  //   char filename[64];
  //   snprintf(filename, sizeof(filename), "rsp550_data_cnt%d.txt", cnt);
  //   FILE *f = fopen(filename, "w");
  //   for (int i = 0; i < 0x400 * 2; i++) {
  //     fprintf(f, "%03x: %02x %02x %02x %02x %02x\n", i, dsp_data[i * 4 + 0], dsp_data[i * 4 + 1], dsp_data[i * 4 + 2], dsp_data[i * 4 + 3], dsp_unk2[i]);
  //   }
  //   fclose(f);

  //   cnt++;
  // }

  return result;
}

void roland_rsp550_state::dsp_w(offs_t ofs, u8 data) {
  // uint8_t buf[4] = {0};
  // buf[0] = 'w';
  // buf[1] = (ofs >> 8) & 0xff;
  // buf[2] = ofs & 0xff;
  // buf[3] = data;
  // write_bytes(serial_fd, buf, 4);
  // read_byte(serial_fd);

  // logerror("RSP-550: dsp_w %06x=%02x\n", ofs, data);
  printf("RSP-550 (%05x): dsp_w %06x=%02x\n", cpu->pc(), ofs, data);

  // 04-05: coef?
  // 08-09: ??
  //    0a: ??
  //  addr: dest?

  // 0x81000-0x811ff: dsp 0?
  // 0x81400-0x815ff: dsp 1?

  if (ofs < 16) {
    dsp_intf_buffer[ofs] = data;
  } else if (data == 0x0) {
    // printf("dsp_w_0 %03x = %02x %02x %02x\n", ofs & 0xfff, dsp_intf_buffer[0x8], dsp_intf_buffer[0x9], dsp_intf_buffer[0xa]);
    dsp_instr[(ofs & 0xfff) * 3 + 0] = dsp_intf_buffer[0xa];
    dsp_instr[(ofs & 0xfff) * 3 + 1] = dsp_intf_buffer[0x9];
    dsp_instr[(ofs & 0xfff) * 3 + 2] = dsp_intf_buffer[0x8];
  } else if (data == 0x1) {
    // printf("dsp_w_1 %03x = %02x %02x %02x %02x %02x\n", ofs & 0xfff, dsp_intf_buffer[0x8], dsp_intf_buffer[0x9], dsp_intf_buffer[0xa], dsp_intf_buffer[0x0], dsp_intf_buffer[0x1]);
    dsp_instr[(ofs & 0xfff) * 3 + 0] = dsp_intf_buffer[0xa];
    dsp_instr[(ofs & 0xfff) * 3 + 1] = dsp_intf_buffer[0x9];
    dsp_instr[(ofs & 0xfff) * 3 + 2] = dsp_intf_buffer[0x8];
    dsp_ramctl[(ofs & 0xfff) * 2 + 0] = dsp_intf_buffer[0x0];
    dsp_ramctl[(ofs & 0xfff) * 2 + 1] = dsp_intf_buffer[0x1];
  } else if (data == 0x2) {
    // printf("dsp_w_2 %03x = %02x %02x %02x %02x\n", ofs & 0xfff, dsp_intf_buffer[0x8], dsp_intf_buffer[0x9], dsp_intf_buffer[0xa], dsp_intf_buffer[0x2]);
    dsp_instr[(ofs & 0xfff) * 3 + 0] = dsp_intf_buffer[0xa];
    dsp_instr[(ofs & 0xfff) * 3 + 1] = dsp_intf_buffer[0x9];
    dsp_instr[(ofs & 0xfff) * 3 + 2] = dsp_intf_buffer[0x8];
    dsp_unk2[ofs & 0xfff] = dsp_intf_buffer[0x2];
  } else if (data == 0x4) {
    // printf("dsp_w_4 %03x = %02x %02x %02x %02x\n", ofs & 0xfff, dsp_intf_buffer[0x8], dsp_intf_buffer[0x9], dsp_intf_buffer[0xa], dsp_intf_buffer[0x3]);
    dsp_instr[(ofs & 0xfff) * 3 + 0] = dsp_intf_buffer[0xa];
    dsp_instr[(ofs & 0xfff) * 3 + 1] = dsp_intf_buffer[0x9];
    dsp_instr[(ofs & 0xfff) * 3 + 2] = dsp_intf_buffer[0x8];
    dsp_data[(ofs & 0xfff) * 4 + 0] = dsp_intf_buffer[0x3];
  } else if (data == 0x8) {
    // printf("dsp_w_8 %03x = %02x %02x %02x %02x %02x\n", ofs & 0xfff, dsp_intf_buffer[0x8], dsp_intf_buffer[0x9], dsp_intf_buffer[0xa], dsp_intf_buffer[0x4], dsp_intf_buffer[0x5]);
    dsp_instr[(ofs & 0xfff) * 3 + 0] = dsp_intf_buffer[0xa];
    dsp_instr[(ofs & 0xfff) * 3 + 1] = dsp_intf_buffer[0x9];
    dsp_instr[(ofs & 0xfff) * 3 + 2] = dsp_intf_buffer[0x8];
    dsp_data[(ofs & 0xfff) * 4 + 1] = dsp_intf_buffer[0x4];
    dsp_data[(ofs & 0xfff) * 4 + 2] = dsp_intf_buffer[0x5];
  } else if (data == 0xc) {
    // printf("dsp_w_c %03x = %02x %02x %02x %02x %02x %02x\n", ofs & 0xfff, dsp_intf_buffer[0x8], dsp_intf_buffer[0x9], dsp_intf_buffer[0xa], dsp_intf_buffer[0x3], dsp_intf_buffer[0x4], dsp_intf_buffer[0x5]);
    dsp_instr[(ofs & 0xfff) * 3 + 0] = dsp_intf_buffer[0xa];
    dsp_instr[(ofs & 0xfff) * 3 + 1] = dsp_intf_buffer[0x9];
    dsp_instr[(ofs & 0xfff) * 3 + 2] = dsp_intf_buffer[0x8];
    dsp_data[(ofs & 0xfff) * 4 + 0] = dsp_intf_buffer[0x3];
    dsp_data[(ofs & 0xfff) * 4 + 1] = dsp_intf_buffer[0x4];
    dsp_data[(ofs & 0xfff) * 4 + 2] = dsp_intf_buffer[0x5];
  } else {
    logerror("RSP-550: dsp_w %06x=%02x (unknown)\n", ofs, data);
  }
}

void roland_rsp550_state::rsp550_map(address_map &map) {
  map(0x00000, 0x07fff).mirror(0x8000).ram().share("nvram");
  map(0x20000, 0x20001).rw(lcd, FUNC(hd44780_device::read), FUNC(hd44780_device::write));
  map(0x60000, 0x6ffff).rw(FUNC(roland_rsp550_state::ga_r), FUNC(roland_rsp550_state::ga_w));
  map(0x80000, 0x8ffff).rw(FUNC(roland_rsp550_state::dsp_r), FUNC(roland_rsp550_state::dsp_w));
  map(0xc0000, 0xdffff).rom().region("roma", 0);
  map(0xe0000, 0xfffff).rom().region("romb", 0);
}

void roland_rsp550_state::rsp550(machine_config &config) {
  V25(config, cpu, 16_MHz_XTAL); // NEC uPD70320GJ-8
	cpu->set_addrmap(AS_PROGRAM, &roland_rsp550_state::rsp550_map);
  cpu->p2_in_cb().set(FUNC(roland_rsp550_state::p2_r));
  cpu->p2_out_cb().set(FUNC(roland_rsp550_state::p2_w));
  cpu->pt_in_cb().set(FUNC(roland_rsp550_state::pt_r));

  screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
  screen.set_refresh_hz(50);
  screen.set_screen_update(FUNC(roland_rsp550_state::screen_update));
  screen.set_palette("palette");
  PALETTE(config, "palette", FUNC(roland_rsp550_state::lcd_palette), 2);
  HD44780(config, lcd, 270'000);
  lcd->set_lcd_size(2, 16);

  NVRAM(config, "nvram", nvram_device::DEFAULT_ALL_0);
}


static INPUT_PORTS_START(rsp550)
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
  
  PORT_BIT(0x100, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("DEBUG") PORT_CODE(KEYCODE_P) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(roland_rsp550_state::button), 0)
INPUT_PORTS_END


ROM_START(rsp550)
	ROM_REGION(0x20000, "roma", 0)
	ROM_LOAD("104A.BIN", 0x00000, 0x20000, CRC(73e70eae) SHA1(ec0d76d325f27eddd7d8ac5530221137dbde6db1))
	
  ROM_REGION(0x20000, "romb", 0)
	ROM_LOAD("104B.BIN", 0x00000, 0x20000, CRC(73e70eae) SHA1(ec0d76d325f27eddd7d8ac5530221137dbde6db1))
ROM_END

} // anonymous namespace

SYST(1991, rsp550, 0, 0, rsp550, rsp550, roland_rsp550_state, empty_init, "Roland", "RSP-550", MACHINE_NOT_WORKING | MACHINE_IMPERFECT_SOUND)
