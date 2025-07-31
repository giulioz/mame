// license:BSD-3-Clause
// copyright-holders:Bryan McPhail, Alex W. Jackson, giulioz
/****************************************************************************

    NEC V55 special function registers and internal data area access

****************************************************************************/

#include "emu.h"
#include "v55.h"
#include "v55priv.ipp"

void v55_device::ida_sfr_map(address_map &map)
{
	map(0x000, 0x000).rw(FUNC(v55_device::adcr0_r),FUNC(v55_device::adcr0_w));
	map(0x006, 0x006).rw(FUNC(v55_device::adcr3_r),FUNC(v55_device::adcr3_w));
	map(0x020, 0x020).rw(FUNC(v55_device::adm_r),FUNC(v55_device::adm_w));
	map(0x0c4, 0x0c4).r(FUNC(v55_device::ispr_r));
	map(0x0c5, 0x0c5).rw(FUNC(v55_device::imc_r), FUNC(v55_device::imc_w));
	map(0x0c9, 0x0e5).rw(FUNC(v55_device::ic_r),FUNC(v55_device::ic_w));
	map(0x100, 0x100).rw(FUNC(v55_device::p0_r),FUNC(v55_device::p0_w));
	map(0x102, 0x102).rw(FUNC(v55_device::p2_r),FUNC(v55_device::p2_w));
	map(0x104, 0x104).rw(FUNC(v55_device::p4_r),FUNC(v55_device::p4_w));
	map(0x110, 0x110).rw(FUNC(v55_device::pm0_r),FUNC(v55_device::pm0_w));
	map(0x112, 0x112).rw(FUNC(v55_device::pm2_r),FUNC(v55_device::pm2_w));
	map(0x114, 0x114).rw(FUNC(v55_device::pm4_r),FUNC(v55_device::pm4_w));
	map(0x118, 0x118).rw(FUNC(v55_device::pm8_r),FUNC(v55_device::pm8_w));
	map(0x122, 0x122).rw(FUNC(v55_device::pmc2_r),FUNC(v55_device::pmc2_w));
	map(0x124, 0x124).rw(FUNC(v55_device::pmc4_r),FUNC(v55_device::pmc4_w));
	map(0x128, 0x128).rw(FUNC(v55_device::pmc8_r),FUNC(v55_device::pmc8_w));
	map(0x130, 0x130).rw(FUNC(v55_device::tmc0_r),FUNC(v55_device::tmc0_w));
	map(0x134, 0x134).rw(FUNC(v55_device::intm0_r),FUNC(v55_device::intm0_w));
	map(0x146, 0x146).rw(FUNC(v55_device::tm3_r),FUNC(v55_device::tm3_w));
	map(0x14c, 0x14c).rw(FUNC(v55_device::cm00_r),FUNC(v55_device::cm00_w));
	map(0x152, 0x152).rw(FUNC(v55_device::cm10_r),FUNC(v55_device::cm10_w));
	map(0x15a, 0x15a).rw(FUNC(v55_device::cm21_r),FUNC(v55_device::cm21_w));
	map(0x166, 0x166).rw(FUNC(v55_device::cm31_r),FUNC(v55_device::cm31_w));
	map(0x16c, 0x16c).rw(FUNC(v55_device::pwm_r),FUNC(v55_device::pwm_w));
	map(0x170, 0x170).rw(FUNC(v55_device::txbrg0_r),FUNC(v55_device::txbrg0_w));
	map(0x171, 0x171).rw(FUNC(v55_device::rxbrg0_r),FUNC(v55_device::rxbrg0_w));
	map(0x172, 0x172).rw(FUNC(v55_device::prs0_r),FUNC(v55_device::prs0_w));
	map(0x173, 0x173).rw(FUNC(v55_device::uartm0_r),FUNC(v55_device::uartm0_w));
	map(0x178, 0x178).rw(FUNC(v55_device::txbrg1_r),FUNC(v55_device::txbrg1_w));
	map(0x179, 0x179).rw(FUNC(v55_device::rxbrg1_r),FUNC(v55_device::rxbrg1_w));
	map(0x17a, 0x17a).rw(FUNC(v55_device::prs1_r),FUNC(v55_device::prs1_w));
	map(0x17d, 0x17d).rw(FUNC(v55_device::txb1_r),FUNC(v55_device::txb1_w));
	map(0x17f, 0x17f).rw(FUNC(v55_device::asp_r),FUNC(v55_device::asp_w));

	map(0x180, 0x181).rw(FUNC(v55_device::tc0l_r),FUNC(v55_device::tc0l_w));
	map(0x182, 0x183).rw(FUNC(v55_device::tc0h_r),FUNC(v55_device::tc0h_w));
	map(0x188, 0x189).rw(FUNC(v55_device::udc0l_r),FUNC(v55_device::udc0l_w));
	map(0x18a, 0x18b).rw(FUNC(v55_device::udc0h_r),FUNC(v55_device::udc0h_w));
	map(0x18c, 0x18d).rw(FUNC(v55_device::dcm0l_r),FUNC(v55_device::dcm0l_w));
	map(0x18e, 0x18f).rw(FUNC(v55_device::dcm0h_r),FUNC(v55_device::dcm0h_w));
	map(0x190, 0x191).rw(FUNC(v55_device::mar0l_r),FUNC(v55_device::mar0l_w));
	map(0x192, 0x193).rw(FUNC(v55_device::mar0h_r),FUNC(v55_device::mar0h_w));
	map(0x194, 0x195).rw(FUNC(v55_device::dptc0l_r),FUNC(v55_device::dptc0l_w));
	map(0x196, 0x197).rw(FUNC(v55_device::dptc0h_r),FUNC(v55_device::dptc0h_w));
	map(0x19c, 0x19c).rw(FUNC(v55_device::dmam0_r),FUNC(v55_device::dmam0_w));
	map(0x19d, 0x19d).rw(FUNC(v55_device::dmac0_r),FUNC(v55_device::dmac0_w));
	map(0x19e, 0x19e).rw(FUNC(v55_device::dmas_r),FUNC(v55_device::dmas_w));

	map(0x1e8, 0x1e8).rw(FUNC(v55_device::pwc0_r),FUNC(v55_device::pwc0_w));
	map(0x1ea, 0x1ea).rw(FUNC(v55_device::mbc_r),FUNC(v55_device::mbc_w));
	map(0x1ec, 0x1ec).rw(FUNC(v55_device::rfm_r),FUNC(v55_device::rfm_w));
	map(0x1ee, 0x1ee).rw(FUNC(v55_device::stbc_r),FUNC(v55_device::stbc_w));
}

uint8_t v55_device::adcr0_r() { return 0xff; }
void v55_device::adcr0_w(uint8_t data) {}
uint8_t v55_device::adcr3_r() { return 0xff; }
void v55_device::adcr3_w(uint8_t data) {}
uint8_t v55_device::cm21_r() { return 0x00; }
void v55_device::cm21_w(uint8_t data) {}
uint8_t v55_device::txbrg0_r() { return 0x00; }
void v55_device::txbrg0_w(uint8_t data) {}
uint8_t v55_device::rxbrg0_r() { return 0x00; }
void v55_device::rxbrg0_w(uint8_t data) {}
uint8_t v55_device::prs0_r() { return 0x00; }
void v55_device::prs0_w(uint8_t data) {}
uint8_t v55_device::uartm0_r() { return 0x00; }
void v55_device::uartm0_w(uint8_t data) {}
uint8_t v55_device::txbrg1_r() { return 0x00; }
void v55_device::txbrg1_w(uint8_t data) {}
uint8_t v55_device::rxbrg1_r() { return 0x00; }
void v55_device::rxbrg1_w(uint8_t data) {}
uint8_t v55_device::prs1_r() { return 0x00; }
void v55_device::prs1_w(uint8_t data) {}
uint8_t v55_device::txb1_r() { return 0x00; }
void v55_device::txb1_w(uint8_t data) {}
uint8_t v55_device::asp_r() { return 0x00; }
void v55_device::asp_w(uint8_t data) {}

uint8_t v55_device::adm_r() {
	// if (!machine().side_effects_disabled())
	// 	printf("adm_r\n");
	return 0x00;
}

void v55_device::adm_w(uint8_t data) {
	// printf("adm_w: %02x\n", data);
}

uint8_t v55_device::ispr_r() {
	if (!machine().side_effects_disabled())
		printf("ispr_r\n");
	return 0x00;
}

uint8_t v55_device::imc_r() {
	if (!machine().side_effects_disabled())
		printf("imc_r\n");
	return m_imc;
}

void v55_device::imc_w(uint8_t data) {
	printf("imc_w: %02x\n", data);
	m_imc = data;

	if (data == 0x00)
		m_imc_base = 0x0000;
	else if (data == 0xb0)
		m_imc_base = 0x0300;
	else
	{
		printf("imc_w: unknown IMC value %02x\n", data);
		m_imc_base = 0x0000;
	}
}

uint8_t v55_device::ic_r(offs_t pos) {
	// if (!machine().side_effects_disabled())
	// 	printf("ic_r %02x\n", pos);
	return m_ic[pos + 9];
}

void v55_device::ic_w(offs_t pos, uint8_t data) {
	// printf("ic_w: %02x=%02x\n", pos, data);
	m_ic[pos + 9] = data;
}

uint8_t v55_device::p0_r() {
	return m_p0_in();
}

void v55_device::p0_w(uint8_t data) {
	m_p0_out(data);
}

uint8_t v55_device::p2_r() {
	if (!machine().side_effects_disabled())
		printf("p2_r\n");
	return 0x00;
}

void v55_device::p2_w(uint8_t data) {
	printf("p2_w: %02x\n", data);
}

uint8_t v55_device::p4_r() {
	// if (!machine().side_effects_disabled())
	// 	printf("p4_r\n");
	return 0x00;
}

void v55_device::p4_w(uint8_t data) {
	// printf("p4_w: %02x\n", data);
}

uint8_t v55_device::pm0_r() {
	if (!machine().side_effects_disabled())
		printf("pm0_r\n");
	return 0x00;
}

void v55_device::pm0_w(uint8_t data) {
	printf("pm0_w: %02x\n", data);
}

uint8_t v55_device::pm2_r() {
	if (!machine().side_effects_disabled())
		printf("pm2_r\n");
	return 0x00;
}

void v55_device::pm2_w(uint8_t data) {
	printf("pm2_w: %02x\n", data);
}

uint8_t v55_device::pm4_r() {
	if (!machine().side_effects_disabled())
		printf("pm4_r\n");
	return 0x00;
}

void v55_device::pm4_w(uint8_t data) {
	printf("pm4_w: %02x\n", data);
}

uint8_t v55_device::pm8_r() {
	if (!machine().side_effects_disabled())
		printf("pm8_r\n");
	return 0x00;
}

void v55_device::pm8_w(uint8_t data) {
	printf("pm8_w: %02x\n", data);
}

uint8_t v55_device::pmc2_r() {
	if (!machine().side_effects_disabled())
		printf("pmc2_r\n");
	return 0x00;
}

void v55_device::pmc2_w(uint8_t data) {
	printf("pmc2_w: %02x\n", data);
}

uint8_t v55_device::pmc4_r() {
	if (!machine().side_effects_disabled())
		printf("pmc4_r\n");
	return 0x00;
}

void v55_device::pmc4_w(uint8_t data) {
	printf("pmc4_w: %02x\n", data);
}

uint8_t v55_device::pmc8_r() {
	if (!machine().side_effects_disabled())
		printf("pmc8_r\n");
	return 0x00;
}

void v55_device::pmc8_w(uint8_t data) {
	printf("pmc8_w: %02x\n", data);
}

uint8_t v55_device::tmc0_r() {
	if (!machine().side_effects_disabled())
		printf("tmc0_r\n");
	return m_TMC0;
}

void v55_device::tmc0_w(uint8_t data) {
	printf("tmc0_w: %02x\n", data);
	m_TMC0 = data;
}

uint8_t v55_device::intm0_r() {
	if (!machine().side_effects_disabled())
		printf("intm0_r\n");
	return m_intm0;
}

void v55_device::intm0_w(uint8_t data) {
	printf("intm0_w: %02x\n", data);
	m_intm0 = data;
}

uint8_t v55_device::tm3_r() {
	if (!machine().side_effects_disabled())
		printf("tm3_r\n");
	return 0x00;
}

void v55_device::tm3_w(uint8_t data) {
	printf("tm3_w: %02x\n", data);
}

uint8_t v55_device::cm00_r() {
	if (!machine().side_effects_disabled())
		printf("cm00_r\n");
	return 0x00;
}

void v55_device::cm00_w(uint8_t data) {
	printf("cm00_w: %02x\n", data);
}

uint8_t v55_device::cm10_r() {
	if (!machine().side_effects_disabled())
		printf("cm10_r\n");
	return 0x00;
}

void v55_device::cm10_w(uint8_t data) {
	printf("cm10_w: %02x\n", data);
}

uint8_t v55_device::cm31_r() {
	if (!machine().side_effects_disabled())
		printf("cm31_r\n");
	return 0x00;
}

void v55_device::cm31_w(uint8_t data) {
	printf("cm31_w: %02x\n", data);
}

uint8_t v55_device::pwm_r() {
	if (!machine().side_effects_disabled())
		printf("pwm_r\n");
	return 0x00;
}

void v55_device::pwm_w(uint8_t data) {
	printf("pwm_w: %02x\n", data);
}


uint16_t v55_device::tc0l_r() {
	// if (!machine().side_effects_disabled())
	// 	printf("tc0l_r\n");
	return tc0 & 0xffff;
}
void v55_device::tc0l_w(uint16_t data) {
	// printf("tc0l_w: %04x\n", data);
	tc0 = (tc0 & 0xffff0000) | (data & 0xffff);
}
uint16_t v55_device::udc0l_r() {
	// if (!machine().side_effects_disabled())
	// 	printf("udc0l_r\n");
	return udc0 & 0xffff;
}
void v55_device::udc0l_w(uint16_t data) {
	// printf("udc0l_w: %04x\n", data);
	udc0 = (udc0 & 0xffff0000) | (data & 0xffff);
}
uint16_t v55_device::dcm0l_r() {
	// if (!machine().side_effects_disabled())
	// 	printf("dcm0l_r\n");
	return dcm0 & 0xffff;
}
void v55_device::dcm0l_w(uint16_t data) {
	// printf("dcm0l_w: %04x\n", data);
	dcm0 = (dcm0 & 0xffff0000) | (data & 0xffff);
}
uint16_t v55_device::mar0l_r() {
	// if (!machine().side_effects_disabled())
	// 	printf("mar0l_r\n");
	return mar0 & 0xffff;
}
void v55_device::mar0l_w(uint16_t data) {
	// printf("mar0l_w: %04x\n", data);
	mar0 = (mar0 & 0xffff0000) | (data & 0xffff);
}
uint16_t v55_device::dptc0l_r() {
	// if (!machine().side_effects_disabled())
	// 	printf("dptc0l_r\n");
	return dptc0 & 0xffff;
}
void v55_device::dptc0l_w(uint16_t data) {
	// printf("dptc0l_w: %04x\n", data);
	dptc0 = (dptc0 & 0xffff0000) | (data & 0xffff);
}
uint16_t v55_device::tc0h_r() {
	// if (!machine().side_effects_disabled())
	// 	printf("tc0h_r\n");
	return (tc0 >> 16) & 0xffff;
}
void v55_device::tc0h_w(uint16_t data) {
	// printf("tc0h_w: %04x\n", data);
	tc0 = (tc0 & 0x0000ffff) | ((data & 0xffff) << 16);
}
uint16_t v55_device::udc0h_r() {
	// if (!machine().side_effects_disabled())
	// 	printf("udc0h_r\n");
	return (udc0 >> 16) & 0xffff;
}
void v55_device::udc0h_w(uint16_t data) {
	// printf("udc0h_w: %04x\n", data);
	udc0 = (udc0 & 0x0000ffff) | ((data & 0xffff) << 16);
}
uint16_t v55_device::dcm0h_r() {
	// if (!machine().side_effects_disabled())
	// 	printf("dcm0h_r\n");
	return (dcm0 >> 16) & 0xffff;
}
void v55_device::dcm0h_w(uint16_t data) {
	// printf("dcm0h_w: %04x\n", data);
	dcm0 = (dcm0 & 0x0000ffff) | ((data & 0xffff) << 16);
}
uint16_t v55_device::mar0h_r() {
	// if (!machine().side_effects_disabled())
	// 	printf("mar0h_r\n");
	return (mar0 >> 16) & 0xffff;
}
void v55_device::mar0h_w(uint16_t data) {
	// printf("mar0h_w: %04x\n", data);
	mar0 = (mar0 & 0x0000ffff) | ((data & 0xffff) << 16);
}
uint16_t v55_device::dptc0h_r() {
	// if (!machine().side_effects_disabled())
	// 	printf("dptc0h_r\n");
	return (dptc0 >> 16) & 0xffff;
}
void v55_device::dptc0h_w(uint16_t data) {
	// printf("dptc0h_w: %04x\n", data);
	dptc0 = (dptc0 & 0x0000ffff) | ((data & 0xffff) << 16);
}

uint8_t v55_device::dmam0_r() {
	// if (!machine().side_effects_disabled())
	// 	printf("dmam0_r\n");
	return dmam0;
}
void v55_device::dmam0_w(uint8_t data) {
	// printf("dmam0_w: %08x\n", data);
	dmam0 = data;

	if (data == 0xcd) {
		uint32_t src = dcm0 - tc0;
		uint32_t dst = mar0 - tc0;
		uint32_t srcEnd = dcm0;
		uint32_t dstEnd = mar0;
		printf("DMA start: %06x-%06x -> %06x-%06x\n", src, srcEnd, dst, dstEnd);

		while (src <= srcEnd && dst <= dstEnd) {
			// printf("DMA writing: %06x -> %06x\n", src, dst);
			uint8_t byte = read_mem_byte(src);
			write_mem_byte(dst, byte);
			src++;
			dst++;
		}
	}
}

uint8_t v55_device::dmac0_r() {
	// if (!machine().side_effects_disabled())
	// 	printf("dmac0_r\n");
	return dmac0;
}
void v55_device::dmac0_w(uint8_t data) {
	// printf("dmac0_w: %08x\n", data);
	dmac0 = data;
}

uint8_t v55_device::dmas_r() {
	// if (!machine().side_effects_disabled())
	// 	printf("dmas_r\n");
	return dmas;
}
void v55_device::dmas_w(uint8_t data) {
	// printf("dmas_w: %08x\n", data);
	dmas = data;
}


uint8_t v55_device::pwc0_r() {
	return 0x00;
}

void v55_device::pwc0_w(uint8_t data) {
	printf("pwc0_w: %02x\n", data);
}

uint8_t v55_device::mbc_r() {
	return 0x00;
}

void v55_device::mbc_w(uint8_t data) {
	printf("mbc_w: %02x\n", data);
}

uint8_t v55_device::rfm_r() {
	return 0x00;
}

void v55_device::rfm_w(uint8_t data) {
	printf("rfm_w: %02x\n", data);
}

uint8_t v55_device::stbc_r() {
	return 0x00;
}

void v55_device::stbc_w(uint8_t data) {
	printf("stbc_w: %02x\n", data);
}


uint8_t v55_device::v55_read_byte(unsigned a)
{
	if (a >= 0xffe00 && a <= 0xfffef)
		return m_data.read_byte(a & 0x1ff);
	else
		return m_program->read_byte(a);
}

uint16_t v55_device::v55_read_word(unsigned a)
{
	if (BIT(a, 0))
		return (v55_read_byte(a) | (v55_read_byte(a + 1) << 8));

	// not sure about this - manual says FFFFC-FFFFE are "reserved"
	if (a == 0xffffe)
		return (m_program->read_byte(a) | (m_data.read_byte(0x1ff) << 8));
	else if (a >= 0xffe00 && a <= 0xfffef)
		return m_data.read_word(a & 0x1ff);
	else
		return m_program->read_word(a);
}

void v55_device::v55_write_byte(unsigned a, uint8_t d)
{
	if (a >= 0xffe00 && a <= 0xfffef)
		m_data.write_byte(a & 0x1ff, d);
	else
		m_program->write_byte(a, d);
}

void v55_device::v55_write_word(unsigned a, uint16_t d)
{
	if (BIT(a, 0))
	{
		v55_write_byte(a, d);
		v55_write_byte(a + 1, d >> 8);
		return;
	}

	// not sure about this - manual says FFFFC-FFFFE are "reserved"
	if (a == 0xffffe)
	{
		m_program->write_byte(a, d);
		m_data.write_byte(0x1ff, d >> 8);
	}
	else if (a >= 0xffe00 && a <= 0xfffef)
		m_data.write_word(a & 0x1ff, d);
	else
		m_program->write_word(a, d);
}

bool v55_device::memory_translate(int spacenum, int intention, offs_t &address, address_space *&target_space)
{
	if (spacenum == AS_PROGRAM && intention != TR_FETCH && (address >= 0xffe00 && address <= 0xfffef))
	{
		address &= 0x1ff;
		target_space = &m_data.space();
	}
	else
		target_space = &space(spacenum);
	return true;
}
