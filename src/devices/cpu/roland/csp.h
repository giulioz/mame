// license:BSD-3-Clause
// copyright-holders:giulioz
/***************************************************************************
 *
 *   Roland/Boss TC6088AF (CSP) emulation
 *   by Giulio Zausa
 *
 ***************************************************************************/

#ifndef MAME_CPU_ROLAND_CSP_H
#define MAME_CPU_ROLAND_CSP_H

#include "emu.h"

class csp_device : public cpu_device {
public:
	csp_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock, bool most_significant_first = false);

	uint8_t host_r(address_space &space, offs_t offset);
	void host_w(offs_t offset, uint8_t data);

	// public for ease of debugging for now

	uint32_t program_ram[1024] = {0};
  uint16_t coef_ram[1024] = {0};
  uint32_t cfg_regs[16] = {0};
  uint8_t host_read[4] = {0};
	
	protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual space_config_vector memory_space_config() const override;
	virtual uint64_t execute_clocks_to_cycles(uint64_t clocks) const noexcept override;
	virtual uint64_t execute_cycles_to_clocks(uint64_t cycles) const noexcept override;
	virtual uint32_t execute_min_cycles() const noexcept override;
	virtual uint32_t execute_max_cycles() const noexcept override;
	virtual void execute_run() override;
	virtual void execute_set_input(int linenum, int state) override;
	virtual std::unique_ptr<util::disasm_interface> create_disassembler() override;
	
	private:
	int icount;
	uint32_t pc;

	// configurable endinness pin
	bool most_significant_first;
};

DECLARE_DEVICE_TYPE(CSP, csp_device)

#endif // MAME_CPU_ROLAND_CSP_H
