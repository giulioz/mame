// license:BSD-3-Clause
// copyright-holders:
/***************************************************************************

    sh_dtc.cpp

    SH7040-series Data Transfer Controller (DTC)

    Functional (non-cycle-accurate) model: when an enabled interrupt source
    fires, the whole transfer for that activation is performed synchronously,
    then the CPU interrupt is either suppressed (mid-sequence) or raised
    (transfer complete, or DISEL=1).  Transfer information is read from and
    written back to memory each activation, exactly as the hardware does.

***************************************************************************/

#include "emu.h"
#include "sh_dtc.h"

#include "sh7042.h"
#include "sh_intc.h"

// Verbosity: 0 = silent, 1 = transfers, 2 = every access
static constexpr int V = 0;

DEFINE_DEVICE_TYPE(SH_DTC, sh_dtc_device, "sh_dtc", "SH7040 data transfer controller")

// Interrupt source -> (DTE register/bit, DTC vector-table address).  From the
// SH7040 hardware manual table 8.2; the vector numbers are the SH-2 internal
// interrupt vectors used by the sh7042 peripherals (MTU chN base+source, etc.).
const sh_dtc_device::dtc_source sh_dtc_device::c_sources[] = {
	{ 120, 0, 7, 0x0400 }, // MTU4 TGI4A  DTEA7
	{ 121, 0, 6, 0x0402 }, // MTU4 TGI4B  DTEA6
	{ 122, 0, 5, 0x0404 }, // MTU4 TGI4C  DTEA5
	{ 123, 0, 4, 0x0406 }, // MTU4 TGI4D  DTEA4
	{ 124, 0, 3, 0x0408 }, // MTU4 TCI4V  DTEA3
	{ 112, 0, 2, 0x040a }, // MTU3 TGI3A  DTEA2
	{ 113, 0, 1, 0x040c }, // MTU3 TGI3B  DTEA1
	{ 114, 0, 0, 0x040e }, // MTU3 TGI3C  DTEA0
	{ 115, 1, 7, 0x0410 }, // MTU3 TGI3D  DTEB7
	{ 104, 1, 6, 0x0412 }, // MTU2 TGI2A  DTEB6
	{ 105, 1, 5, 0x0414 }, // MTU2 TGI2B  DTEB5
	{  96, 1, 4, 0x0416 }, // MTU1 TGI1A  DTEB4
	{  97, 1, 3, 0x0418 }, // MTU1 TGI1B  DTEB3
	{  88, 1, 2, 0x041a }, // MTU0 TGI0A  DTEB2
	{  89, 1, 1, 0x041c }, // MTU0 TGI0B  DTEB1
	{  90, 1, 0, 0x041e }, // MTU0 TGI0C  DTEB0
	{  91, 2, 7, 0x0420 }, // MTU0 TGI0D  DTEC7
	{ 136, 2, 6, 0x0422 }, // A/D  ADI0   DTEC6
	{  64, 2, 5, 0x0424 }, // IRQ0        DTEC5
	{  65, 2, 4, 0x0426 }, // IRQ1        DTEC4
	{  66, 2, 3, 0x0428 }, // IRQ2        DTEC3
	{  67, 2, 2, 0x042a }, // IRQ3        DTEC2
	{  68, 2, 1, 0x042c }, // IRQ4        DTEC1
	{  69, 2, 0, 0x042e }, // IRQ5        DTEC0
	{  70, 3, 7, 0x0430 }, // IRQ6        DTED7
	{  71, 3, 6, 0x0432 }, // IRQ7        DTED6
	{ 144, 3, 5, 0x0434 }, // CMT  CMI0   DTED5
	{ 148, 3, 4, 0x0436 }, // CMT  CMI1   DTED4
	{ 129, 3, 3, 0x0438 }, // SCI0 RXI0   DTED3
	{ 130, 3, 2, 0x043a }, // SCI0 TXI0   DTED2
	{ 133, 3, 1, 0x043c }, // SCI1 RXI1   DTED1
	{ 134, 3, 0, 0x043e }, // SCI1 TXI1   DTED0
	{   0, 0, 0, 0x0000 }  // terminator
};

sh_dtc_device::sh_dtc_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock) :
	device_t(mconfig, SH_DTC, tag, owner, clock),
	m_cpu(*this, finder_base::DUMMY_TAG),
	m_intc(*this, finder_base::DUMMY_TAG)
{
}

void sh_dtc_device::device_start()
{
	m_program = &m_cpu->space(AS_PROGRAM);

	save_item(NAME(m_dter));
	save_item(NAME(m_dtcsr));
	save_item(NAME(m_dtbr));
}

void sh_dtc_device::device_reset()
{
	std::fill(std::begin(m_dter), std::end(m_dter), 0);
	m_dtcsr = 0;
	m_dtbr = 0;
}

const sh_dtc_device::dtc_source *sh_dtc_device::find_source(int vector) const
{
	for(const dtc_source *s = c_sources; s->vector_addr; s++)
		if(s->vector == vector)
			return s;
	return nullptr;
}

u8 sh_dtc_device::dter_r(offs_t offset)
{
	return m_dter[offset];
}

void sh_dtc_device::dter_w(offs_t offset, u8 data)
{
	m_dter[offset] = data;
	if(V>=2) logerror("dte%c_w %02x\n", 'a'+offset, data);
}

u16 sh_dtc_device::dtcsr_r()
{
	return m_dtcsr;
}

void sh_dtc_device::dtcsr_w(offs_t, u16 data, u16 mem_mask)
{
	u16 newval = m_dtcsr;
	COMBINE_DATA(&newval);

	// NMIF/AE can only be cleared (write 0 after read 1), never set by a write.
	u16 keep = m_dtcsr & (DTCSR_NMIF | DTCSR_AE);
	if(!(newval & DTCSR_NMIF)) keep &= ~DTCSR_NMIF;
	if(!(newval & DTCSR_AE))   keep &= ~DTCSR_AE;

	bool sw_start = (newval & DTCSR_SWDTE) && !(m_dtcsr & DTCSR_SWDTE);
	m_dtcsr = keep | (newval & (DTCSR_SWDTE | 0x00ff));

	if(sw_start && !(m_dtcsr & (DTCSR_NMIF | DTCSR_AE))) {
		// Software activation: vector address = H'0400 + DTVEC[7:0].
		u16 vector_addr = 0x0400 + (m_dtcsr & 0x00fe);
		if(V>=1) logerror("software activation, vector %04x\n", vector_addr);
		transfer(vector_addr);
		m_dtcsr &= ~DTCSR_SWDTE; // auto-clears (software-end interrupt not modelled)
	}
}

u16 sh_dtc_device::dtbr_r()
{
	return m_dtbr;
}

void sh_dtc_device::dtbr_w(offs_t, u16 data, u16 mem_mask)
{
	COMBINE_DATA(&m_dtbr);
	if(V>=2) logerror("dtbr_w %04x\n", m_dtbr);
}

int sh_dtc_device::address_delta(u32 mode_bits, u32 unit) const
{
	// mode_bits: bit1 = increment/decrement enable, bit0 = direction.
	if(!(mode_bits & 2))
		return 0;                       // address fixed
	return (mode_bits & 1) ? -int(unit) : int(unit);
}

bool sh_dtc_device::trigger(int vector)
{
	const dtc_source *s = find_source(vector);
	if(!s)
		return false;                   // not a DTC-capable source
	if(!BIT(m_dter[s->dte_reg], s->dte_bit))
		return false;                   // DTC not enabled for this source
	if(m_dtcsr & (DTCSR_NMIF | DTCSR_AE))
		return false;                   // DTC halted; let the CPU take the interrupt

	bool raise = transfer(s->vector_addr);

	if(raise) {
		// Completion (or DISEL=1): clear the enable bit and let the CPU take the
		// interrupt normally (the caller raises it because we return false).
		m_dter[s->dte_reg] &= ~(1 << s->dte_bit);
		return false;
	}
	// Transfer done but sequence not complete: the DTC consumed the interrupt.
	return true;
}

bool sh_dtc_device::transfer(u16 vector_addr)
{
	// The vector-table entry gives the low 16 bits of the register-information
	// start address; DTBR supplies the high 16 bits.
	u32 info = (u32(m_dtbr) << 16) | m_program->read_word(vector_addr);
	bool raise = false;

	for(int chain = 0; chain < 16; chain++) {
		u16 dtmr = m_program->read_word(info + 0);
		u32 md   = dtmr & DTMR_MD;                       // 0=normal, 0x400=repeat, 0x800=block
		u32 sz   = (dtmr & DTMR_SZ) >> 8;                // 0=byte, 1=word, 2=long
		u32 unit = sz == 0 ? 1 : sz == 1 ? 2 : 4;
		int sinc = address_delta((dtmr >> 14) & 3, unit);
		int dinc = address_delta((dtmr >> 12) & 3, unit);
		bool dts   = dtmr & DTMR_DTS;
		bool chne  = dtmr & DTMR_CHNE;
		bool disel = dtmr & DTMR_DISEL;
		bool done  = false;

		u32 sar = m_program->read_dword(info + 8);
		u32 dar = m_program->read_dword(info + 12);

		auto copy_unit = [&]() {
			switch(unit) {
			case 1: m_program->write_byte (dar, m_program->read_byte (sar)); break;
			case 2: m_program->write_word (dar, m_program->read_word (sar)); break;
			case 4: m_program->write_dword(dar, m_program->read_dword(sar)); break;
			}
		};

		if(md == 0x0800) {
			// Block transfer mode: one whole block (DTCRB units) per activation.
			u16 cra = m_program->read_word(info + 2);
			u16 crb = m_program->read_word(info + 6);
			u32 blocklen = crb ? crb : 0x10000;
			u32 sar0 = sar, dar0 = dar;
			for(u32 i = 0; i < blocklen; i++) {
				copy_unit();
				sar += sinc;
				dar += dinc;
			}
			// The block area (source or destination per DTS) resets each block.
			if(dts) sar = sar0; else dar = dar0;
			cra = (cra - 1) & 0xffff;
			done = (cra == 0);
			m_program->write_word(info + 2, cra);
		}
		else if(md == 0x0400) {
			// Repeat mode: 1 unit; DTCRAL counts down, reloads from DTCRAH and
			// resets the repeat-area address to DTIAR on wrap.  Never count-done.
			u16 cra = m_program->read_word(info + 2);
			u32 iar = m_program->read_dword(info + 4);
			u8  crah = (cra >> 8) & 0xff;
			u8  cral = cra & 0xff;
			copy_unit();
			sar += sinc;
			dar += dinc;
			cral = (cral - 1) & 0xff;
			if(cral == 0) {
				cral = crah;                 // reload (0 -> 256 on the next pass)
				if(dts) sar = iar; else dar = iar;
			}
			m_program->write_word(info + 2, (crah << 8) | cral);
		}
		else {
			// Normal mode: 1 unit; DTCRA counts down; done when it reaches 0.
			u16 cra = m_program->read_word(info + 2);
			cra = (cra - 1) & 0xffff;
			copy_unit();
			sar += sinc;
			dar += dinc;
			done = (cra == 0);
			m_program->write_word(info + 2, cra);
		}

		m_program->write_dword(info + 8, sar);
		m_program->write_dword(info + 12, dar);

		if(V>=1) logerror("transfer info=%08x md=%x sz=%d sar=%08x dar=%08x %s\n",
				info, md >> 10, unit, sar, dar, done ? "done" : "");

		if(done || disel)
			raise = true;

		if(!chne)
			break;
		info += 16;                          // chained transfer: next register info block
	}

	return raise;
}
