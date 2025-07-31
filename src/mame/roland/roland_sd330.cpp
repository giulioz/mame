// license:BSD-3-Clause
// copyright-holders:giulioz

#include "cpu/nec/v55.h"
#include "emu.h"
#include "emupal.h"
#include "machine/ram.h"
#include "machine/timer.h"
#include "screen.h"
#include "softlist_dev.h"
#include "video/hd44780.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

[[maybe_unused]] static int open_serial(const char *path) {
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

namespace {

class roland_sd330_state : public driver_device {
public:
  roland_sd330_state(const machine_config &mconfig, device_type type, const char *tag)
      : driver_device(mconfig, type, tag), cpu(*this, "maincpu"),
        lcd(*this, "lcd"),
        m_sw0(*this, "SW0"),
        m_sw1(*this, "SW1"),
        m_test_timer(*this, "test_timer") {}

  void sd330(machine_config &config);

  DECLARE_INPUT_CHANGED_MEMBER(button);

protected:
  virtual void machine_start() override;
  virtual void machine_reset() override;

private:
  required_device<v55_device> cpu;
  required_device<hd44780_device> lcd;
  required_ioport m_sw0, m_sw1;

  u8 p0_r();
  void p0_w(u8 data);
  uint8_t btn_sel = 0;

  u8 unk2_r(offs_t ofs);
  void unk2_w(offs_t ofs, u8 data);

  u8 ga_r(offs_t ofs);
  void ga_w(offs_t ofs, u8 data);
  
  u8 csp_r(offs_t ofs);
  void csp_w(offs_t ofs, u8 data);
  uint32_t csp0_pram[1024] = {0};
  uint32_t csp1_pram[1024] = {0};
  uint16_t csp0_cram[1024] = {0};
  uint16_t csp1_cram[1024] = {0};
  uint32_t csp0_cfg[16] = {0};
  uint32_t csp1_cfg[16] = {0};
  uint8_t csp0_regs[4] = {0};
  uint8_t csp1_regs[4] = {0};
  
  void lcd_palette(palette_device &palette) const;

  HD44780_PIXEL_UPDATE(lcd_pixel_update);

  void sd330_map(address_map &map);

  TIMER_DEVICE_CALLBACK_MEMBER(test_timer_cb);
  required_device<timer_device> m_test_timer;

  [[maybe_unused]] int serial_fd = -1;
};

HD44780_PIXEL_UPDATE(roland_sd330_state::lcd_pixel_update)
{
  if (x < 5 && y < 8 && line < 2 && pos < 21)
		bitmap.pix(line * 8 + y, pos * 6 + x) = state;
}

void roland_sd330_state::machine_start() {
  save_item(NAME(csp0_pram));
  save_item(NAME(csp1_pram));
  save_item(NAME(csp0_cram));
  save_item(NAME(csp1_cram));
  save_item(NAME(csp0_cfg));
  save_item(NAME(csp1_cfg));
  save_item(NAME(csp0_regs));
  save_item(NAME(csp1_regs));

  // serial_fd = open_serial("/dev/cu.usbmodem1101");
}

void roland_sd330_state::machine_reset() {
}

void roland_sd330_state::lcd_palette(palette_device &palette) const {
  palette.set_pen_color(0, rgb_t(0, 0, 0));
	palette.set_pen_color(1, rgb_t(0, 255, 0));
}

// TEST MODE
bool tmA = false;
bool tmB = false;
// bool tmA = true;
// bool tmB = true;
u8 roland_sd330_state::p0_r() {
  // if (!machine().side_effects_disabled())
  //   logerror("SD330: p0_r sel=%02x\n", btn_sel);

  if (btn_sel == 0x7f) {
    if (tmA) {
      tmA = false;
      return (~0x24) & 0xff;
    }

    return m_sw0->read();
  } else if (btn_sel == 0xbf) {
    if (tmB) {
      tmB = false;
      return (~0x20) & 0xff;
    }

    return m_sw1->read();
  }

  return 0xff;
}

void roland_sd330_state::p0_w(u8 data) {
  // logerror("SD330: p0_w %02x\n", data);

  btn_sel = data;
}

u8 roland_sd330_state::ga_r(offs_t ofs) {
  if (ofs == 0 || ofs == 1) {
    return lcd->read(ofs);
  }

  if (!machine().side_effects_disabled()) {
    // logerror("SD330: ga_r %06x\n", ofs);
    // printf("SD330: ga_r %06x\n", ofs);
  }
  return 0xff;
}

void roland_sd330_state::ga_w(offs_t ofs, u8 data) {
  if (ofs == 0 || ofs == 1)
    lcd->write(ofs, data);
  else {
    // logerror("SD330: ga_w %06x=%02x\n", ofs, data);
    // printf("SD330: ga_w %06x=%02x\n", ofs, data);
  }
}

u8 roland_sd330_state::csp_r(offs_t ofs) {
  // if (!machine().side_effects_disabled()) {
  //   uint8_t buf[4] = {0};
  //   buf[0] = 'r';
  //   buf[1] = (ofs >> 8) & 0xff;
  //   buf[2] = ofs & 0xff;
  //   buf[3] = 0x00;
  //   write_bytes(serial_fd, buf, 4);
  //   uint8_t r = read_byte(serial_fd);

  //   printf("SD330: csp_r %06x=%02x\n", ofs, r);
  //   return r;
  // }

  if (ofs < 0x4) {
    return csp0_regs[ofs & 0x3];
  } else {
    return csp1_regs[ofs & 0x3];
  }
}

void roland_sd330_state::csp_w(offs_t ofs, u8 data) {
  // printf("SD330: csp_w %06x=%02x\n", ofs, data);

  // uint8_t buf[4] = {0};
  // buf[0] = 'w';
  // buf[1] = (ofs >> 8) & 0xff;
  // buf[2] = ofs & 0xff;
  // buf[3] = data;
  // write_bytes(serial_fd, buf, 4);
  // read_byte(serial_fd);
  // return;

  if (ofs < 0x0800) {
    // CSP0 CRAM
    uint32_t cram_ofs = (ofs - 0x0000) / 2;
    printf("SD330: csp0 cram w (%06x) %06x=%02x\n", ofs, cram_ofs, data);
    if (ofs & 1) {
      csp0_cram[cram_ofs] = (csp0_cram[cram_ofs] & 0x00ff) | (data << 8);
    } else {
      csp0_cram[cram_ofs] = (csp0_cram[cram_ofs] & 0xff00) | data;
    }
  }
  
  else if (ofs < 0x1000) {
    // CSP0 CONFIG
    uint32_t config_ofs = (ofs - 0x800) / 4;
    if ((ofs & 3) == 0x00) {
      csp0_cfg[config_ofs] = (csp0_cfg[config_ofs] & 0x00ffffff) | (data << 24);
    } else if ((ofs & 3) == 0x01) {
      csp0_cfg[config_ofs] = (csp0_cfg[config_ofs] & 0xff00ffff) | (data << 16);
    } else if ((ofs & 3) == 0x02) {
      csp0_cfg[config_ofs] = (csp0_cfg[config_ofs] & 0xffff00ff) | (data << 8);
    } else {
      csp0_cfg[config_ofs] = (csp0_cfg[config_ofs] & 0xffffff00) | data;
    }
    printf("SD330: csp0 cfg w %06x=%02x\n", ofs, data);
  }

  else if (ofs < 0x2000) {
    // CSP0 PRAM
    uint32_t pram_ofs = (ofs - 0x1000) / 4;
    printf("SD330: csp0 pram w (%06x) %06x=%02x\n", ofs, pram_ofs, data);
    if ((ofs & 3) == 0x00) {
      csp0_pram[pram_ofs] = (csp0_pram[pram_ofs] & 0x00ffffff) | (data << 24);
    } else if ((ofs & 3) == 0x01) {
      csp0_pram[pram_ofs] = (csp0_pram[pram_ofs] & 0xff00ffff) | (data << 16);
    } else if ((ofs & 3) == 0x02) {
      csp0_pram[pram_ofs] = (csp0_pram[pram_ofs] & 0xffff00ff) | (data << 8);
    } else {
      csp0_pram[pram_ofs] = (csp0_pram[pram_ofs] & 0xffffff00) | data;
    }
  }

  else if (ofs < 0x2800) {
    // CSP0 read CRAM
    uint32_t cram_ofs = (ofs - 0x2000) / 2;
    uint16_t cram_val = csp0_cram[cram_ofs];
    printf("SD330: csp0 cram r (%06x) %06x=%04x\n", ofs, cram_ofs, cram_val);
    csp0_regs[0] = cram_val & 0xff;
    csp0_regs[1] = (cram_val >> 8) & 0xff;
    csp0_regs[2] = 0x00;
    csp0_regs[3] = 0x00;
  }

  else if (ofs < 0x3000) {
    // CSP0 read CONFIG
    uint32_t cfg_ofs = ofs - 0x2800;
    // printf("SD330: csp0 config r (%06x) %06x=%04x\n", ofs, cfg_ofs, data);
    printf("SD330: csp0 config r (%06x) %06x\n", ofs, cfg_ofs);
    // csp0_regs[2] = csp0_cfg[cfg_ofs];
    csp0_regs[0] = 0x00;
    csp0_regs[1] = 0x00;
    csp0_regs[2] = 0x00;
    csp0_regs[3] = 0x01;
  }

  else if (ofs < 0x4000) {
    // CSP0 read PRAM
    uint32_t pram_ofs = (ofs - 0x3000) / 4;
    uint32_t pram_val = csp0_pram[pram_ofs];
    printf("SD330: csp0 pram r (%06x) %06x=%08x\n", ofs, pram_ofs, pram_val);
    csp0_regs[2] = (pram_val >> 8) & 0xff;
    csp0_regs[1] = (pram_val >> 16) & 0xff;
    csp0_regs[0] = (pram_val >> 24) & 0xff;
    csp0_regs[3] = 0x00;
  }

  //
  //
  //

  if (ofs >= 0x8000) {
    ofs -= 0x8000;
  } else {
    return;
  }

  //
  //
  //

  if (ofs < 0x0800) {
    // CSP1 CRAM
    uint32_t cram_ofs = (ofs - 0x0000) / 2;
    if (ofs & 1) {
      csp1_cram[cram_ofs] = (csp1_cram[cram_ofs] & 0x00ff) | (data << 8);
    } else {
      csp1_cram[cram_ofs] = (csp1_cram[cram_ofs] & 0xff00) | data;
    }
  }
  
  else if (ofs < 0x1000) {
    // CSP1 CONFIG
    uint32_t config_ofs = (ofs - 0x800) / 4;
    if ((ofs & 3) == 0x00) {
      csp1_cfg[config_ofs] = (csp1_cfg[config_ofs] & 0x00ffffff) | (data << 24);
    } else if ((ofs & 3) == 0x01) {
      csp1_cfg[config_ofs] = (csp1_cfg[config_ofs] & 0xff00ffff) | (data << 16);
    } else if ((ofs & 3) == 0x02) {
      csp1_cfg[config_ofs] = (csp1_cfg[config_ofs] & 0xffff00ff) | (data << 8);
    } else {
      csp1_cfg[config_ofs] = (csp1_cfg[config_ofs] & 0xffffff00) | data;
    }
    printf("SD330: csp1 cfg w %06x=%02x\n", ofs, data);
  }

  else if (ofs < 0x2000) {
    // CSP1 PRAM
    uint32_t pram_ofs = (ofs - 0x1000) / 4;
    if ((ofs & 3) == 0x00) {
      csp1_pram[pram_ofs] = (csp1_pram[pram_ofs] & 0x00ffffff) | (data << 24);
    } else if ((ofs & 3) == 0x01) {
      csp1_pram[pram_ofs] = (csp1_pram[pram_ofs] & 0xff00ffff) | (data << 16);
    } else if ((ofs & 3) == 0x02) {
      csp1_pram[pram_ofs] = (csp1_pram[pram_ofs] & 0xffff00ff) | (data << 8);
    } else {
      csp1_pram[pram_ofs] = (csp1_pram[pram_ofs] & 0xffffff00) | data;
    }
  }

  else if (ofs < 0x2800) {
    // CSP1 read CRAM
    uint32_t cram_ofs = (ofs - 0x2000) / 2;
    uint16_t cram_val = csp1_cram[cram_ofs];
    // printf("SD330: csp1 cram r (%06x) %06x=%04x\n", ofs, cram_ofs, cram_val);
    csp1_regs[0] = cram_val & 0xff;
    csp1_regs[1] = (cram_val >> 8) & 0xff;
    csp1_regs[2] = 0x00;
    csp1_regs[3] = 0x00;
  }

  else if (ofs < 0x3000) {
    // CSP1 read CONFIG
    uint32_t cfg_ofs = ofs - 0x2800;
    // csp1_regs[2] = csp1_cfg[cfg_ofs];
    printf("SD330: csp1 config r (%06x) %06x\n", ofs, cfg_ofs);
    csp1_regs[0] = 0x00;
    csp1_regs[1] = 0x00;
    csp1_regs[2] = 0x00;
    csp1_regs[3] = 0x01;
  }

  else if (ofs < 0x4000) {
    // CSP1 read PRAM
    uint32_t pram_ofs = (ofs - 0x3000) / 4;
    uint32_t pram_val = csp1_pram[pram_ofs];
    // printf("SD330: csp1 pram r (%06x) %06x=%08x\n", ofs, pram_ofs, pram_val);
    csp1_regs[2] = (pram_val >> 8) & 0xff;
    csp1_regs[1] = (pram_val >> 16) & 0xff;
    csp1_regs[0] = (pram_val >> 24) & 0xff;
    csp1_regs[3] = 0x00;
  }
}

u8 roland_sd330_state::unk2_r(offs_t ofs) {
  if (!machine().side_effects_disabled()) {
    printf("SD330: unk2_r %06x\n", ofs);
  }
  return 0x00;
}

void roland_sd330_state::unk2_w(offs_t ofs, u8 data) {
  printf("SD330: unk2_w %06x=%02x\n", ofs, data);
}

void roland_sd330_state::sd330_map(address_map &map) {
  map(0x00000, 0x1ffff).ram();
  map(0x20000, 0x2ffff).rw(FUNC(roland_sd330_state::unk2_r), FUNC(roland_sd330_state::unk2_w));
  map(0x60000, 0x6ffff).rw(FUNC(roland_sd330_state::csp_r), FUNC(roland_sd330_state::csp_w));
  map(0x70000, 0x7ffff).rw(FUNC(roland_sd330_state::ga_r), FUNC(roland_sd330_state::ga_w));
  map(0x80000, 0xfffff).rom().region("progrom", 0);
}

void roland_sd330_state::sd330(machine_config &config) {
  V55(config, cpu, 16_MHz_XTAL); // NEC uPD70320GJ-8
	cpu->set_addrmap(AS_PROGRAM, &roland_sd330_state::sd330_map);
  cpu->p0_in_cb().set(FUNC(roland_sd330_state::p0_r));
  cpu->p0_out_cb().set(FUNC(roland_sd330_state::p0_w));

  screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
  screen.set_refresh_hz(50);
  screen.set_screen_update("lcd", FUNC(hd44780_device::screen_update));
  screen.set_palette("palette");
  screen.set_size(6*21, 8*2);
  screen.set_visarea_full();
  PALETTE(config, "palette", FUNC(roland_sd330_state::lcd_palette), 2);
  HD44780(config, lcd, 270'000);
  lcd->set_lcd_size(2, 21);
  lcd->set_pixel_update_cb(FUNC(roland_sd330_state::lcd_pixel_update));

  TIMER(config, m_test_timer).configure_periodic(FUNC(roland_sd330_state::test_timer_cb), attotime::from_hz(400));
}

TIMER_DEVICE_CALLBACK_MEMBER(roland_sd330_state::test_timer_cb) {
  // cpu->execute_set_input(INT_CM21, ASSERT_LINE);
  cpu->execute_set_input(INT_CM31, ASSERT_LINE);
}

// int state = 1;
INPUT_CHANGED_MEMBER(roland_sd330_state::button) {
  if (m_sw1->read() & 0x100) {
    // if (state == 0) {
    //   cpu->execute_set_input(INT_CM31, CLEAR_LINE);
    //   state = 1;
    // } else {
    //   cpu->execute_set_input(INT_CM31, ASSERT_LINE);
    //   state = 0;
    // }

    FILE *fcsp = fopen("csp.txt", "w");
    for (int i = 0x0000; i < 0x1000/4; i++) {
        // fprintf(fcsp, "%04x: %02x %02x %02x %04x\n", i, (csp0_pram[i] >> 8) & 0xff, (csp0_pram[i] >> 16) & 0xff, (csp0_pram[i] >> 24) & 0xff, csp0_cram[i]);
        
        uint8_t b0 = (csp0_pram[i] >> 8) & 0xff;
        uint8_t b1 = (csp0_pram[i] >> 16) & 0xff;
        uint8_t b2 = (csp0_pram[i] >> 24) & 0xff;

        uint8_t dram_ctrl = b0 >> 2;
        uint8_t mac_shift = b0 & 3;
        uint8_t opcode = b1 >> 4;
        uint8_t store_dst = (b1 >> 1) & 0x7;
        uint16_t ram_offs = b2 | ((b1 & 1) << 8);
        uint16_t param = csp0_cram[i];
        fprintf(fcsp, "%04x: %02x %02x %02x   dram_ctrl:%02x  mac_shift:%x opcode:%x store:%x ram_offs:%03x param:%04x\n",
            (i - 0x1000) / 4, b0, b1, b2,
            dram_ctrl, mac_shift, opcode, store_dst, ram_offs, param);
    }
    for (int i = 0x0000; i < 0x1000/4; i++) {
        // fprintf(fcsp, "%04x: %02x %02x %02x %04x\n", i, (csp1_pram[i] >> 8) & 0xff, (csp1_pram[i] >> 16) & 0xff, (csp1_pram[i] >> 24) & 0xff, csp1_cram[i]);
        
        uint8_t b0 = (csp1_pram[i] >> 8) & 0xff;
        uint8_t b1 = (csp1_pram[i] >> 16) & 0xff;
        uint8_t b2 = (csp1_pram[i] >> 24) & 0xff;

        uint8_t dram_ctrl = b0 >> 2;
        uint8_t mac_shift = b0 & 3;
        uint8_t opcode = b1 >> 4;
        uint8_t store_dst = (b1 >> 1) & 0x7;
        uint16_t ram_offs = b2 | ((b1 & 1) << 8);
        uint16_t param = csp1_cram[i];
        fprintf(fcsp, "%04x: %02x %02x %02x   dram_ctrl:%02x  mac_shift:%x opcode:%x store:%x ram_offs:%03x param:%04x\n",
            (i - 0x1000) / 4, b0, b1, b2,
            dram_ctrl, mac_shift, opcode, store_dst, ram_offs, param);
    }
    fclose(fcsp);
  }
}


static INPUT_PORTS_START(sd330)
  PORT_START("SW0")
  PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("PROGRAM N") PORT_CODE(KEYCODE_Q)
  PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("FUNK1") PORT_CODE(KEYCODE_W)
  PORT_BIT(0x8, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("UP") PORT_CODE(KEYCODE_E)
  PORT_BIT(0x4, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("EDIT") PORT_CODE(KEYCODE_R)
  PORT_BIT(0x2, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("SYSTEM") PORT_CODE(KEYCODE_T)
  PORT_BIT(0x1, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("EXIT") PORT_CODE(KEYCODE_Y)
  
  PORT_START("SW1")
  PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("FUNK2") PORT_CODE(KEYCODE_A)
  PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("FUNK3") PORT_CODE(KEYCODE_S)
  PORT_BIT(0x8, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("DOWN") PORT_CODE(KEYCODE_D)
  PORT_BIT(0x4, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("PAGE") PORT_CODE(KEYCODE_F)
  PORT_BIT(0x2, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("MEMORY") PORT_CODE(KEYCODE_G)
  PORT_BIT(0x1, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("BYPASS") PORT_CODE(KEYCODE_H)

  PORT_BIT(0x100, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("DEBUG") PORT_CODE(KEYCODE_P) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(roland_sd330_state::button), 0)
INPUT_PORTS_END


ROM_START(sde330)
	ROM_REGION(0x80000, "progrom", 0)
	ROM_LOAD("sde330_1.0.3.bin", 0x00000, 0x80000, CRC(73e70eae) SHA1(ec0d76d325f27eddd7d8ac5530221137dbde6db1))
ROM_END

ROM_START(srv330)
	ROM_REGION(0x80000, "progrom", 0)
	ROM_LOAD("srv330.bin", 0x00000, 0x80000, CRC(73e70eae) SHA1(ec0d76d325f27eddd7d8ac5530221137dbde6db1))
ROM_END

} // anonymous namespace

SYST(1993, sde330, 0, 0, sd330, sd330, roland_sd330_state, empty_init, "Roland", "SDE-330", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
SYST(1993, srv330, 0, 0, sd330, sd330, roland_sd330_state, empty_init, "Roland", "SRV-330", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
