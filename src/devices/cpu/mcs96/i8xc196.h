// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, giulioz
#ifndef MAME_CPU_MCS96_I8XC196_H
#define MAME_CPU_MCS96_I8XC196_H

#pragma once

#include "i8x9x.h"

// CMOS MCS-96: two oscillator periods per state, enhanced instructions and
// windowed SFRs. The common timers/ADC/serial block is shared with the NMOS family.
class i8xc196_device : public i8x9x_device
{
public:
	enum { EXTINT1_LINE = 5 }; // P2.2, in addition to the inherited HSI lines

protected:
	i8xc196_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock);
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual unsigned ad_conversion_states() const override { return BIT(m_ioc2, 4) ? 91 : 158; }
	virtual bool is_196() const noexcept override { return true; }
	virtual bool is_cmos() const noexcept override { return true; }
	virtual int instruction_cycles(int cycles) override;
	virtual u16 interrupt_pending() const override { return (pending_irq & 0x8000) | mcs96_device::interrupt_pending(); }
	virtual u16 interrupt_mask() const override { return (PSW & 0xff) | (u16(m_mask_high) << 8); }
	virtual u16 interrupt_vector(int level) const override { return level < 8 ? 0x2000 + level * 2 : 0x2030 + (level - 8) * 2; }
	virtual u16 auxiliary_psw() const override { return m_mask_high | (u16(m_window) << 8); }
	virtual void auxiliary_psw_w(u16 value) override;
	virtual void execute_set_input(int linenum, int state) override;
	virtual std::unique_ptr<util::disasm_interface> create_disassembler() override;
	virtual void do_exec_full() override;
	virtual void do_exec_partial() override;
	void internal_regs(address_map &map) ATTR_COLD;
	u8 sfr_r(offs_t offset);
	void sfr_w(offs_t offset, u8 data);

#define O(o) void o ## _196_full(); void o ## _196_partial()
	O(bmov_direct_2w); O(bmovi_direct_2w); O(cmpl_direct_2w); O(djnzw_wrrel8); O(idlpd_none);
	O(pop_indexed_1w); O(pop_indirect_1w); O(popa_none); O(pusha_none);
#undef O

	bool m_nmi_state = false, m_extint1_state = false;
	u8 m_window = 0, m_mask_high = 0, m_ioc2 = 0, m_ios2 = 0, m_program_pulse = 0;
};

class i80c196kb_device : public i8xc196_device
{
public:
	i80c196kb_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);
};

DECLARE_DEVICE_TYPE(I80C196KB, i80c196kb_device)

#endif // MAME_CPU_MCS96_I8XC196_H
