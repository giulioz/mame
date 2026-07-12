// license:BSD-3-Clause
// copyright-holders:
/***************************************************************************

    sh_dtc.h

    SH7040-series Data Transfer Controller (DTC)

    An interrupt-triggered micro-DMA.  Transfer information (mode, source,
    destination, counts) lives in memory; each activating interrupt source has
    a 2-byte entry in the DTC vector table whose value forms the low 16 bits of
    the register-information start address (DTBR supplies the high 16 bits).
    When an enabled source fires, the DTC performs the transfer instead of the
    CPU taking the interrupt, and only raises the CPU interrupt on completion.

***************************************************************************/

#ifndef MAME_CPU_SH_SH_DTC_H
#define MAME_CPU_SH_SH_DTC_H

#pragma once

class sh7042_device;
class sh_intc_device;

class sh_dtc_device : public device_t {
public:
	sh_dtc_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	template<typename T, typename U> sh_dtc_device(const machine_config &mconfig, const char *tag, device_t *owner,
			T &&cpu, U &&intc) :
		sh_dtc_device(mconfig, tag, owner)
	{
		m_cpu.set_tag(std::forward<T>(cpu));
		m_intc.set_tag(std::forward<U>(intc));
	}

	// Called by the interrupt controller when an interrupt source fires.
	// Returns true if the DTC serviced it and the CPU interrupt must be
	// suppressed; false if the source is not DTC-driven or the DTC transfer
	// completed (in which case the CPU interrupt is raised normally).
	bool trigger(int vector);

	u8 dter_r(offs_t offset);
	void dter_w(offs_t offset, u8 data);
	u16 dtcsr_r();
	void dtcsr_w(offs_t, u16 data, u16 mem_mask);
	u16 dtbr_r();
	void dtbr_w(offs_t, u16 data, u16 mem_mask);

protected:
	required_device<sh7042_device> m_cpu;
	required_device<sh_intc_device> m_intc;

	address_space *m_program = nullptr;

	u8 m_dter[5];   // DTEA..DTEE
	u16 m_dtcsr;    // control/status (NMIF/AE/SWDTE + software vector)
	u16 m_dtbr;     // register-information base (upper 16 bits)

	// DTCSR bits
	enum {
		DTCSR_NMIF  = 0x0400,
		DTCSR_AE    = 0x0200,
		DTCSR_SWDTE = 0x0100
	};

	// DTMR (mode) bits, in the 16-bit word read from memory
	enum {
		DTMR_SM    = 0xc000, // source address mode
		DTMR_DM    = 0x3000, // destination address mode
		DTMR_MD    = 0x0c00, // transfer mode (normal/repeat/block)
		DTMR_SZ    = 0x0300, // transfer size
		DTMR_DTS   = 0x0080, // repeat/block area select
		DTMR_CHNE  = 0x0040, // chain enable
		DTMR_DISEL = 0x0020, // interrupt select
		DTMR_NMIM  = 0x0010  // NMI mode
	};

	struct dtc_source { u8 vector; u8 dte_reg; u8 dte_bit; u16 vector_addr; };
	static const dtc_source c_sources[];

	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	const dtc_source *find_source(int vector) const;
	int address_delta(u32 mode_bits, u32 unit) const;
	bool transfer(u16 vector_addr);
};

DECLARE_DEVICE_TYPE(SH_DTC, sh_dtc_device)

#endif // MAME_CPU_SH_SH_DTC_H
