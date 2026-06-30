// license:BSD-3-Clause
// copyright-holders:giulioz
/****************************************************************************

    Driver for Roland S-760 digital sampler.

****************************************************************************/

#include "bus/midi/midiinport.h"
#include "bus/midi/midioutport.h"
#include "cpu/mcs96/i8x9x.h"
#include "cpu/mcs96/i8xc196.h"
#include "emu.h"
#include "emupal.h"
#include "machine/ram.h"
#include "machine/timer.h"
#include "screen.h"
#include "softlist_dev.h"
#include "speaker.h"
#include "video/sed1330.h"
#include "machine/eepromser.h"
#include "imagedev/floppy.h"
#include "machine/upd765.h"
#include "video/rf5c16.h"
#include "bus/nscsi/devices.h"
#include "machine/mb87030.h"
#include "formats/roland_dsk.h"
#include <queue>

namespace {

static INPUT_PORTS_START(s760)
  PORT_START("SC0")
  PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Option P1")
  PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Option P2")
  PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Preview Btn") PORT_CODE(KEYCODE_K)
  PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Encoder Btn") PORT_CODE(KEYCODE_L)
  PORT_BIT(0x8, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Right") PORT_CODE(KEYCODE_D)
  PORT_BIT(0x4, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("S2") PORT_CODE(KEYCODE_E)
  PORT_BIT(0x2, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("S1") PORT_CODE(KEYCODE_Q)
  PORT_BIT(0x1, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Command") PORT_CODE(KEYCODE_X)

  PORT_START("SC1")
  PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Option P1")
  PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Option P2")
  PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER)
  PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER)
  PORT_BIT(0x8, IP_ACTIVE_LOW, IPT_OTHER)
  PORT_BIT(0x4, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Up") PORT_CODE(KEYCODE_W)
  PORT_BIT(0x2, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Exit") PORT_CODE(KEYCODE_C)
  PORT_BIT(0x1, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Mode") PORT_CODE(KEYCODE_Z)

  PORT_START("SC2")
  PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Option P1")
  PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Option P2")
  PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER)
  PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER)
  PORT_BIT(0x8, IP_ACTIVE_LOW, IPT_OTHER)
  PORT_BIT(0x4, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Left") PORT_CODE(KEYCODE_A)
  PORT_BIT(0x2, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("F3") PORT_CODE(KEYCODE_3)
  PORT_BIT(0x1, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("F1") PORT_CODE(KEYCODE_1)

  PORT_START("SC3")
  PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Option P1")
  PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Option P2")
  PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER)
  PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER)
  PORT_BIT(0x8, IP_ACTIVE_LOW, IPT_OTHER)
  PORT_BIT(0x4, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Down") PORT_CODE(KEYCODE_S)
  PORT_BIT(0x2, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Shift") PORT_CODE(KEYCODE_G)
  PORT_BIT(0x1, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("F2") PORT_CODE(KEYCODE_2)
INPUT_PORTS_END

static void floppy_formats(format_registration &fr) {
	fr.add_mfm_containers();
	fr.add(FLOPPY_ROLAND_SDISK_FORMAT);
}

class roland_s760_state : public driver_device {
public:
  roland_s760_state(const machine_config &mconfig, device_type type,
                   const char *tag)
      : driver_device(mconfig, type, tag),
        m_cpu(*this, "maincpu"),
        m_ram(*this, "ram"),
        m_lcd(*this, "lcd"),
        m_vdp(*this, "vdp"),
        m_eeprom(*this, "eeprom"),
        m_fdc(*this, "fdc"),
		    m_floppy(*this, "fdc:0"),
        m_scsi(*this, "scsi:7:spc"),
        m_keys(*this, "SC%u", 0) {}

  void s760(machine_config &config);

protected:
  virtual void machine_start() override;
  virtual void machine_reset() override;

private:
  required_device<i8xc196_device> m_cpu;
  required_device<ram_device> m_ram;
  required_device<sed1330_device> m_lcd;
  required_device<rf5c16_device> m_vdp;
  required_device<eeprom_serial_93cxx_device> m_eeprom;
  required_device<upd72067_device> m_fdc;
	required_device<floppy_connector> m_floppy;
	required_device<mb89352_device> m_scsi;
  required_ioport_array<4> m_keys;

  u8 port1_r();
  void port1_w(u8 data);
  u8 port2_r();
  void port2_w(u8 data);

  void lcd_map(address_map &map);
  void lcd_palette(palette_device &palette) const;

  uint16_t controller_r(offs_t offset);
  void controller_w(offs_t offset, uint16_t data);
  uint8_t cont_mem_read8(bool inst, offs_t offset);
  void cont_mem_write8(bool inst, offs_t offset, u8 value);
  uint16_t cont_mem_read16(bool inst, offs_t offset);
  void cont_mem_write16(bool inst, offs_t offset, u16 value);
  uint8_t io_read8(offs_t offset);
  void io_write8(offs_t offset, u8 value);
  void s760_map_inst(address_map &map);
  void s760_map_data(address_map &map);

  // uint16_t bank_config[16] = {0x780, 0x780, 0x780, 0x780, 0x780, 0x780, 0x780, 0x780};
  uint16_t bank_config[16] = {0x780, 0x790, 0x780, 0x780, 0x780, 0x780, 0x780, 0x780};
  uint8_t spPos = 0;
  
  uint8_t samplerStatus = 0xFF;

  static void floppies(device_slot_interface &device);
};

void roland_s760_state::machine_start() {
  // for (size_t i = 0; i < 0xFC000; i++) {
  //   m_ram->write(i, memregion("ospreload")->base()[i + 0x4800]);
  // }
  // for (size_t i = 0; i < 0x80; i++) {
  //   m_eeprom->internal_write(i, memregion("eeprompreload")->base()[i]);
  // }
  for (size_t i = 0; i < 0x1FFF; i++) {
    m_lcd->space().write_byte(i, memregion("lcdpreload")->base()[i]);
  }

  u8 *rom = memregion("bootrom")->base();
	rom[0x2080] = 0xE7;
	rom[0x2081] = 0xCC;
	rom[0x2082] = 0x26;

  save_item(NAME(bank_config));
}

void roland_s760_state::machine_reset() {}

uint16_t roland_s760_state::controller_r(offs_t offset) {
  if (offset <= 0x0e) {
    return bank_config[offset];
  } else {
    logerror("controller_r %04x\n", offset);
  }
  return 0xFF;
}
void roland_s760_state::controller_w(offs_t offset, uint16_t data) {
  if (offset <= 0x0e) {
    bank_config[offset] = data;
  } else {
    logerror("controller_w %04x %04x\n", offset, data);
  }
}

u8 roland_s760_state::roland_s760_state::port1_r() {
  // logerror("p1r %02x\n", sw_scan_current_out);
  return 0xFF;
}
void roland_s760_state::roland_s760_state::port1_w(u8 data) {
  // logerror("p1w %02x\n", data);
  m_eeprom->cs_write(data & 0b1);
  m_eeprom->clk_write((data & 0b10) >> 1);
  m_eeprom->di_write((data & 0b100) >> 2);
}
u8 roland_s760_state::roland_s760_state::port2_r() {
  // logerror("p1r %02x\n", sw_scan_current_out);
  return m_eeprom->do_read() << 7;
}
void roland_s760_state::roland_s760_state::port2_w(u8 data) {
  // logerror("p1w %02x\n", data);
}

void roland_s760_state::lcd_map(address_map &map) {
  map.global_mask(0x1fff);
	map(0x0000, 0x1fff).ram();
}
void roland_s760_state::lcd_palette(palette_device &palette) const {
  palette.set_pen_color(0, rgb_t(138, 146, 148));
  palette.set_pen_color(1, rgb_t(69, 62, 66));
}

uint8_t roland_s760_state::cont_mem_read8(bool inst, offs_t offset) {
  offset += 0x0120;
  uint16_t bank_n = bank_config[(offset >> 0xe) + (inst ? 0 : 4)];
  offset -= (offset >> 0xe) * 0x4000;

  // logerror("read8 %s %04x %02x\n", inst ? "inst" : "data", offset, bank_n);
  
  if (bank_n <= 0x3f0) {
    return m_ram->read(offset + (bank_n * 0x400));
  }
  else if (bank_n == 0x400) {
    // logerror("ioread8 %04x\n", offset);
    return io_read8(offset);
  }
  else if (bank_n >= 0x780) {
    return memregion("bootrom")->base()[offset + ((bank_n - 0x780) * 0x400)];
  }
  
  return 0xFF;
}
void roland_s760_state::cont_mem_write8(bool inst, offs_t offset, u8 value) {
  offset += 0x0120;
  uint16_t bank_n = bank_config[(offset >> 0xe) + (inst ? 0 : 4)];
  offset -= (offset >> 0xe) * 0x4000;

  if (bank_n <= 0x3f0) {
    // logerror("write8 %s %04x %02x %04n\n", inst ? "inst" : "data", offset, bank_n, value);
    m_ram->write(offset + (bank_n * 0x400), value);
  }
  else if (bank_n == 0x400) {
    // logerror("iowrite8 %04x\n", offset);
    io_write8(offset, value);
  }
}

uint16_t roland_s760_state::cont_mem_read16(bool inst, offs_t offset) {
  offset *= 2;
  offset += 0x0120;
  uint16_t bank_n = bank_config[(offset >> 0xe) + (inst ? 0 : 4)];
  offset -= (offset >> 0xe) * 0x4000;
  
  // logerror("read16 %s %04x %02x\n", inst ? "inst" : "data", offset, bank_n);

  if (bank_n <= 0x3f0) {
    return m_ram->read(offset + (bank_n * 0x400))
      | m_ram->read(offset + (bank_n * 0x400) + 1) << 8;
  }
  else if (bank_n == 0x400) {
    logerror("ioread16 %04x\n", offset);
    return io_read8(offset);
  }
  else if (bank_n >= 0x780) {
    return memregion("bootrom")->base()[offset + ((bank_n - 0x780) * 0x400)]
      | memregion("bootrom")->base()[offset + 1 + ((bank_n - 0x780) * 0x400)]  << 8;
  }
  
  return 0xFFFF;
}
void roland_s760_state::cont_mem_write16(bool inst, offs_t offset, u16 value) {
  offset *= 2;
  offset += 0x0120;
  uint16_t bank_n = bank_config[(offset >> 0xe) + (inst ? 0 : 4)];
  offset -= (offset >> 0xe) * 0x4000;

  if (bank_n <= 0x3f0) {
    // logerror("write16 %s %04x %02x %04x\n", inst ? "inst" : "data", offset, bank_n, value);
    m_ram->write(offset + (bank_n * 0x400), value & 0xFF);
    m_ram->write(offset + (bank_n * 0x400) + 1, value >> 8);
  }
  else if (bank_n == 0x400) {
    logerror("iowrite16 %04x\n", offset);
    io_write8(offset, value);
  }
}

uint8_t roland_s760_state::io_read8(offs_t offset) {
  // Sampler
  if (offset < 0x1000) {
    // logerror("sampler r %04x\n", offset);
    if (offset == 0x00) return samplerStatus;
    return 0xFF;
  }
  // Filter
  if (offset < 0x1C00) {
    logerror("filter r %04x\n", offset - 0x1000);
    return 0xFF;
  }
  // MEQ
  if (offset < 0x2000) {
    logerror("meq r %04x\n", offset - 0x1C00);
    return 0xFF;
  }

  // FDC
  if (offset == 0x2000) {
    auto value = m_fdc->msr_r();
    logerror("fdc msr_r %02x\n", value);
    return value;
  }
  if (offset == 0x2002) {
    auto value = m_fdc->fifo_r();
    logerror("fdc fifo_r %02x\n", value);
    return value;
  }

  // SCSI
  if (offset >= 0x2400 && offset <= 0x2420) {
    offs_t localOffset = (offset - 0x2400) / 2;
    if (localOffset == 0x00) return m_scsi->bdid_r();
    if (localOffset == 0x01) return m_scsi->sctl_r();
    if (localOffset == 0x02) return m_scsi->scmd_r();
    if (localOffset == 0x04) return m_scsi->ints_r();
    if (localOffset == 0x05) return m_scsi->psns_r();
    if (localOffset == 0x06) return m_scsi->ssts_r();
    if (localOffset == 0x07) return m_scsi->serr_r();
    if (localOffset == 0x08) return m_scsi->pctl_r();
    if (localOffset == 0x09) return m_scsi->mbc_r();
    if (localOffset == 0x0a) return m_scsi->dreg_r();
    if (localOffset == 0x0b) return m_scsi->temp_r();
    if (localOffset == 0x0c) return m_scsi->tch_r();
    if (localOffset == 0x0d) return m_scsi->tcm_r();
    if (localOffset == 0x0e) return m_scsi->tcl_r();
  }

  // VDP
  if (offset >= 0x2800 && offset <= 0x2820) {
    // logerror("vdp r %04x\n", m_cpu->m_pr8(m_cpu->PC));
    return m_vdp->reg_r((offset - 0x2800) / 2);
  }

  // I/O Controller
  if (offset >= 0x3000 && offset <= 0x3020) {
    if (offset == 0x3000) { // PA
      logerror("ioc read PA\n");
      return 0xFF;
    } else if (offset == 0x3002) { // PB
      logerror("ioc read PB\n");
      return 0b00000000;
    } else if (offset == 0x3004) { // Mouse
      logerror("ioc read Mouse\n");
      return 0xFF;
    } else if (offset == 0x3006) { // Unused?
      logerror("ioc read Unused_06\n");
      return 0xFF;
    } else if (offset == 0x3008) { // Unused?
      logerror("ioc read Unused_08\n");
      return 0xFF;
    } else if (offset == 0x300A) { // SP
      uint8_t prevSpos = spPos;
      spPos = (spPos + 1) % 4;
      return m_keys[prevSpos]->read() & 0b11111111;
    } else if (offset == 0x300C) { // Unk
      logerror("ioc read Unk_0C\n");
      return 0xFF;
    } else if (offset == 0x300E) { // Unk
      logerror("ioc read Unk_0E\n");
      return 0xFF;
    } else if (offset == 0x3010) { // LEDS
      logerror("ioc read Leds\n");
      return 0xFF;
    } else if (offset == 0x3012) { // Unk probably encoder
      return 0x00;
    } else {
      logerror("ioc read Unmapped\n");
      return 0xFF;
    }
  }

  // LCD
  if (offset == 0x3800) return m_lcd->status_r();
  if (offset == 0x3802) return m_lcd->data_r();

  return 0xFF;
}

void roland_s760_state::io_write8(offs_t offset, u8 value) {
  // Sampler
  if (offset < 0x1000) {
    logerror("sampler w %04x %02x\n", offset, value);
    if (offset == 0x000e) samplerStatus = ~value;
    return;
  }
  // Filter
  if (offset < 0x1C00) {
    logerror("filter w %04x %02x\n", offset - 0x1000, value);
    return;
  }
  // MEQ
  if (offset < 0x2000) {
    logerror("meq w %04x %02x\n", offset - 0x1C00, value);
    return;
  }

  // FDC
  if (offset == 0x2000) {
    logerror("fdc auxcmd_w %02x\n", value);
    m_fdc->auxcmd_w(value);
  }
  if (offset == 0x2002) {
    logerror("fdc fifo_w %02x\n", value);
    m_fdc->fifo_w(value);
  }

  // SCSI
  if (offset >= 0x2400 && offset <= 0x2420) {
    offs_t localOffset = (offset - 0x2400) / 2;
    if (localOffset == 0x00) m_scsi->bdid_w(value);
    if (localOffset == 0x01) m_scsi->sctl_w(value);
    if (localOffset == 0x02) m_scsi->scmd_w(value);
    if (localOffset == 0x04) m_scsi->ints_w(value);
    if (localOffset == 0x05) m_scsi->sdgc_w(value);
    if (localOffset == 0x08) m_scsi->pctl_w(value);
    if (localOffset == 0x0a) m_scsi->dreg_w(value);
    if (localOffset == 0x0b) m_scsi->temp_w(value);
    if (localOffset == 0x0c) m_scsi->tch_w(value);
    if (localOffset == 0x0d) m_scsi->tcm_w(value);
    if (localOffset == 0x0e) m_scsi->tcl_w(value);
  }

  // VDP
  if (offset >= 0x2800 && offset <= 0x2820) {
    // logerror("vdp w %04x\n", m_cpu->m_pr8(m_cpu->PC));
    m_vdp->reg_w((offset - 0x2800) / 2, value);
  }

  // I/O Controller
  if (offset >= 0x3000 && offset <= 0x3020) {
    if (offset == 0x3000) { // PA
      logerror("ioc write PA %02x\n", value);

      // Reset
      m_fdc->reset_w(value & 0b00000001);
      m_scsi->reset_w(value & 0b00000001);

      // TC
      m_fdc->tc_line_w(value & 0b00000010);
    } else if (offset == 0x3002) { // PB
      logerror("ioc write PB %02x\n", value);
    } else if (offset == 0x3004) { // Mouse
      logerror("ioc write Mouse %02x\n", value);
    } else if (offset == 0x3006) { // Unused?
      logerror("ioc write Unused_06 %02x\n", value);
    } else if (offset == 0x3008) { // Unused?
      logerror("ioc write Unused_08 %02x\n", value);
    } else if (offset == 0x300A) { // SP
      spPos = value;
    } else if (offset == 0x300C) { // Unk
      logerror("ioc write Unk_0C %02x\n", value);
    } else if (offset == 0x300E) { // Unk
      logerror("ioc write Unk_0E %02x\n", value);
    } else if (offset == 0x3010) { // LEDS
      logerror("ioc write Leds %02x\n", value);
    } else if (offset == 0x3012) { // Unk
      logerror("ioc write Unk_12 %02x\n", value);
    } else {
      logerror("ioc write Unmapped %04x %0x\n", offset, value);
    }
  }

  // LCD
  if (offset == 0x3800) m_lcd->data_w(value);
  if (offset == 0x3802) m_lcd->command_w(value);
}

void roland_s760_state::s760_map_inst(address_map &map) {
  map(0x0100, 0x011f).rw(FUNC(roland_s760_state::controller_r), FUNC(roland_s760_state::controller_w));
  map(0x0120, 0xffff).lr16(NAME([this](offs_t offset) { return cont_mem_read16(true, offset); }));
  map(0x0120, 0xffff).lw16(NAME([this](offs_t offset, u16 data) { return cont_mem_write16(true, offset, data); }));
  map(0x0120, 0xffff).lr8(NAME([this](offs_t offset) { return cont_mem_read8(true, offset); }));
  map(0x0120, 0xffff).lw8(NAME([this](offs_t offset, u8 data) { return cont_mem_write8(true, offset, data); }));
}
void roland_s760_state::s760_map_data(address_map &map) {
  map(0x0100, 0x011f).rw(FUNC(roland_s760_state::controller_r), FUNC(roland_s760_state::controller_w));
  map(0x0120, 0xffff).lr16(NAME([this](offs_t offset) { return cont_mem_read16(false, offset); }));
  map(0x0120, 0xffff).lw16(NAME([this](offs_t offset, u16 data) { return cont_mem_write16(false, offset, data); }));
  map(0x0120, 0xffff).lr8(NAME([this](offs_t offset) { return cont_mem_read8(false, offset); }));
  map(0x0120, 0xffff).lw8(NAME([this](offs_t offset, u8 data) { return cont_mem_write8(false, offset, data); }));
}

void roland_s760_state::s760(machine_config &config) {
  i8xc196_device &maincpu(C80C196KB(config, m_cpu, 16_MHz_XTAL));
  maincpu.set_addrmap(AS_PROGRAM, &roland_s760_state::s760_map_inst);
  maincpu.set_addrmap(AS_IO, &roland_s760_state::s760_map_data);
  maincpu.in_p1_cb().set(FUNC(roland_s760_state::port1_r));
  maincpu.out_p1_cb().set(FUNC(roland_s760_state::port1_w));
  maincpu.in_p2_cb().set(FUNC(roland_s760_state::port2_r));
  maincpu.out_p2_cb().set(FUNC(roland_s760_state::port2_w));
  // maincpu->in_ior_cb<1>().set(m_fdc, FUNC(upd72067_device::dma_r));
	// maincpu->out_iow_cb<1>().set(m_fdc, FUNC(upd72067_device::dma_w));
  // m_maincpu->in_ior_cb<0>().set("scsi:7:spc", FUNC(mb89352_device::dma_r));
	// m_maincpu->out_iow_cb<0>().set("scsi:7:spc", FUNC(mb89352_device::dma_w));

  RAM(config, m_ram).set_default_size("1M");
  EEPROM_93C46_16BIT(config, "eeprom"); /* Actually AK93C45F */

  SED1330(config, m_lcd, 8000000);
	m_lcd->set_screen("screen_lcd");
	m_lcd->set_addrmap(0, &roland_s760_state::lcd_map);

  screen_device &screen_lcd(SCREEN(config, "screen_lcd", SCREEN_TYPE_LCD));
  screen_lcd.set_refresh_hz(60);
  screen_lcd.set_size(160, 64);
  screen_lcd.set_visarea_full();
  screen_lcd.set_screen_update("lcd", FUNC(sed1330_device::screen_update));
  screen_lcd.set_palette("palette_lcd");

  PALETTE(config, "palette_lcd", FUNC(roland_s760_state::lcd_palette), 2);

  RF5C16(config, m_vdp, 14.318181_MHz_XTAL);
	m_vdp->set_screen("screen_vdp");
  
	screen_device &screen_vdp(SCREEN(config, "screen_vdp", SCREEN_TYPE_RASTER));
	screen_vdp.set_video_attributes(VIDEO_UPDATE_BEFORE_VBLANK);
	screen_vdp.set_screen_update(m_vdp, FUNC(rf5c16_device::screen_update));
	screen_vdp.set_size(rf5c16_device::TOTAL_WIDTH, rf5c16_device::TOTAL_HEIGHT);
	screen_vdp.set_visarea(0, rf5c16_device::TOTAL_WIDTH - 1, 0, rf5c16_device::TOTAL_HEIGHT - 1);
	screen_vdp.set_refresh_hz(60);
	screen_vdp.set_vblank_time(ATTOSECONDS_IN_USEC(2500)); /* not accurate */
	screen_vdp.set_palette("palette_vdp");

  PALETTE(config, "palette_vdp", palette_device::RGB_3BIT);

  UPD72067(config, m_fdc, 16_MHz_XTAL);
  // m_fdc->intrq_wr_callback().set_inputline(maincpu, i8xc196_device::EXTINT_LINE);
  m_fdc->intrq_wr_callback().set([this](int state) {
    logerror("FDC INT\n");
  });
	m_fdc->drq_wr_callback().set([this](int state) {
    logerror("FDC DRQ\n");
  });

	FLOPPY_CONNECTOR(config, m_floppy, roland_s760_state::floppies, "35hd", &floppy_formats);

  NSCSI_BUS(config, "scsi");
	NSCSI_CONNECTOR(config, "scsi:0", default_scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:1", default_scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:2", default_scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:3", default_scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:4", default_scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:5", default_scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:6", default_scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:7").option_set("spc", MB89352).machine_config(
		[](device_t *device) {
			mb89352_device &spc = downcast<mb89352_device &>(*device);

			spc.set_clock(16_MHz_XTAL / 2);
			// spc.out_irq_callback().set(":intp3", FUNC(input_merger_device::in_w<1>));
			// spc.out_dreq_callback().set(m_maincpu, FUNC(v53a_device::dreq_w<0>));
		});
}

void roland_s760_state::floppies(device_slot_interface &device) {
	device.option_add("35hd", FLOPPY_35_HD);
}

ROM_START(s760)
ROM_REGION(0x8000, "bootrom", 0)
ROM_SYSTEM_BIOS(0, "v111", "Version 1.11")
ROMX_LOAD("Roland_S-760_v1.11.BIN", 0x0000, 0x8000, CRC(026a8e85) SHA1(531b6a57367db663b8531078973f1afa094c268a), ROM_BIOS(0))

ROM_REGION(0x168000, "ospreload", 0)
ROM_LOAD("S760224.OUT", 0x0000, 0x168000, CRC(b14b0257) SHA1(e5ab4abc96654965f23d1930b1e93c9211784873))
// ROM_LOAD("testDisk.OUT", 0x0000, 0x168000, CRC(b14b0257) SHA1(e5ab4abc96654965f23d1930b1e93c9211784873))

ROM_REGION(0x80, "eeprompreload", 0)
ROM_LOAD("eeprom.bin", 0x0000, 0x80, CRC(b14b0257) SHA1(e5ab4abc96654965f23d1930b1e93c9211784873))
// 0x08: screen config

ROM_REGION(0x1FFF, "lcdpreload", 0)
ROM_LOAD("lcd.bin", 0x0000, 0x1FFF, CRC(b14b0257) SHA1(e5ab4abc96654965f23d1930b1e93c9211784873))
ROM_END

} // anonymous namespace

SYST(1991, s760, 0, 0, s760, s760, roland_s760_state, empty_init, "Roland", "S-760 Digital Sampler", MACHINE_NOT_WORKING | MACHINE_IMPERFECT_SOUND)
