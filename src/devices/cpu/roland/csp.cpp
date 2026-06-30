// license:BSD-3-Clause
// copyright-holders:giulioz
/***************************************************************************
 *
 *   Roland/Boss TC6088AF (CSP) emulation
 *   by Giulio Zausa
 *
 ***************************************************************************/

#include "csp.h"
#include "cspd.h"
#include "emu.h"

DEFINE_DEVICE_TYPE(CSP, csp_device, "CSP", "Roland TC6088AF (CSP)")

csp_device::csp_device(const machine_config &mconfig, const char *tag,
                       device_t *owner, uint32_t clock,
                       bool most_significant_first)
    : cpu_device(mconfig, CSP, tag, owner, clock),
		  icount(0),
		  pc(0),
      most_significant_first(most_significant_first) {}

uint8_t csp_device::host_r(address_space &space, offs_t offset) {
  if (most_significant_first) {
    uint8_t offset_low = offset & 0x3;
    offset_low = 3 - offset_low; // reverse endianness
    offset = (offset & ~0x3) | offset_low;
  }

  return host_read[offset & 0x3];
}

void csp_device::host_w(offs_t offset, uint8_t data) {
  if (most_significant_first) {
    uint8_t offset_low = offset & 0x3;
    offset_low = 3 - offset_low; // reverse endianness
    offset = (offset & ~0x3) | offset_low;
  }

	if (offset >= 0x2000) {
		// printf("csp read op %08x\n", offset);
	}

	// CRAM
	if (offset < 0x0800) {
    uint32_t cram_ofs = ((offset - 0x0000) / 2) & 0x3ff;
    if (offset & 1) {
      coef_ram[cram_ofs] = (coef_ram[cram_ofs] & 0x00ff) | (data << 8);
    } else {
      coef_ram[cram_ofs] = (coef_ram[cram_ofs] & 0xff00) | data;
    }
  }

	// CONFIG
	else if (offset < 0x1000) {
    uint32_t config_ofs = ((offset - 0x800) / 4) & 0xf;

		// printf("csp config w %08x=%02x\n", offset, data);

    if ((offset & 3) == 0x00) {
      cfg_regs[config_ofs] = (cfg_regs[config_ofs] & 0x00ffffff) | (data << 24);
    } else if ((offset & 3) == 0x01) {
      cfg_regs[config_ofs] = (cfg_regs[config_ofs] & 0xff00ffff) | (data << 16);
    } else if ((offset & 3) == 0x02) {
      cfg_regs[config_ofs] = (cfg_regs[config_ofs] & 0xffff00ff) | (data << 8);
    } else {
      cfg_regs[config_ofs] = (cfg_regs[config_ofs] & 0xffffff00) | data;
    }
  }

	// PRAM
  else if (offset < 0x2000) {
    uint32_t pram_ofs = ((offset - 0x1000) / 4) & 0x3ff;
    if ((offset & 3) == 0x00) {
      program_ram[pram_ofs] = (program_ram[pram_ofs] & 0x00ffffff) | (data << 24);
    } else if ((offset & 3) == 0x01) {
      program_ram[pram_ofs] = (program_ram[pram_ofs] & 0xff00ffff) | (data << 16);
    } else if ((offset & 3) == 0x02) {
      program_ram[pram_ofs] = (program_ram[pram_ofs] & 0xffff00ff) | (data << 8);
    } else {
      program_ram[pram_ofs] = (program_ram[pram_ofs] & 0xffffff00) | data;
    }
  }

	// read CRAM
  else if (offset < 0x2800) {
    uint32_t cram_ofs = (offset - 0x2000) / 2;
    uint16_t cram_val = coef_ram[cram_ofs & 0x3ff];
    host_read[0] = cram_val & 0xff;
    host_read[1] = (cram_val >> 8) & 0xff;
    host_read[2] = 0x00;
    host_read[3] = 0x00;
  }

	// read ??
  else if (offset < 0x3000) {
    host_read[0] = 0xff;
    host_read[1] = 0xff;
    host_read[2] = 0xff;
    host_read[3] = 0xff;
  }

	// read PRAM
  else if (offset < 0x4000) {
    uint32_t pram_ofs = (offset - 0x3000) / 4;
    uint32_t pram_val = program_ram[pram_ofs & 0x3ff];
    host_read[2] = (pram_val >> 8) & 0xff;
    host_read[1] = (pram_val >> 16) & 0xff;
    host_read[0] = (pram_val >> 24) & 0xff;
    host_read[3] = 0x00;
  }
}

void csp_device::device_start() {
  set_icountptr(icount);
  state_add(STATE_GENPC, "GENPC", pc).noshow();
	state_add(STATE_GENPCBASE, "CURPC", pc).noshow();

  save_item(NAME(icount));
  save_item(NAME(program_ram));
  save_item(NAME(coef_ram));
  save_item(NAME(cfg_regs));
  save_item(NAME(host_read));
}

void csp_device::device_reset() {}

device_memory_interface::space_config_vector
csp_device::memory_space_config() const {
  return space_config_vector{};
}

uint64_t csp_device::execute_clocks_to_cycles(uint64_t clocks) const noexcept {
  return clocks / 3;
}

uint64_t csp_device::execute_cycles_to_clocks(uint64_t cycles) const noexcept {
  return cycles * 3;
}

uint32_t csp_device::execute_min_cycles() const noexcept { return 1; }

uint32_t csp_device::execute_max_cycles() const noexcept { return 1; }

void csp_device::execute_set_input(int linenum, int state) {}

void csp_device::execute_run() {
  while (icount > 0) {
  	--icount;
  }
}

std::unique_ptr<util::disasm_interface> csp_device::create_disassembler() {
  return std::make_unique<csp_disassembler>();
}
