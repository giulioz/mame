// license:BSD-3-Clause
// copyright-holders:AJR
/***************************************************************************

    Skeleton driver for Kawai K4 synthesizer.

***************************************************************************/

#include "emu.h"
#include "cpu/upd78k/upd78k3.h"
#include "machine/nvram.h"
#include "emupal.h"
#include "screen.h"
#include "video/hd44780.h"


namespace {

class kawai_k4_state : public driver_device
{
public:
	kawai_k4_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_mpu(*this, "mpu")
		, m_lcdc(*this, "lcdc")
	{
	}

	void k4(machine_config &config);

private:
	virtual void machine_start() override ATTR_COLD;

	u8 peripheral_r(offs_t offset);
	void peripheral_w(offs_t offset, u8 data);
	void port0_w(u8 data);

	void mem_map(address_map &map) ATTR_COLD;

	required_device<upd78310_device> m_mpu;
	required_device<hd44780_device> m_lcdc;
	std::array<u8, 0x1800> m_peripheral{};
};


void kawai_k4_state::machine_start()
{
	save_item(NAME(m_peripheral));
}

u8 kawai_k4_state::peripheral_r(offs_t offset)
{
	// The DCO/DCA command ports expose busy flags rather than their last writes.
	if ((offset == 0x0003) || (offset == 0x0403))
		return m_peripheral[offset] & 0x7f;
	if ((offset == 0x0004) || (offset == 0x0005) || (offset == 0x0404) || (offset == 0x0405))
		return m_peripheral[offset] & 0xfe;

	// Bit 0 of the DSP status register indicates that it is ready for a command.
	return m_peripheral[offset] | ((offset == 0x100f) ? 0x01 : 0x00);
}

void kawai_k4_state::peripheral_w(offs_t offset, u8 data)
{
	m_peripheral[offset] = data;
}

void kawai_k4_state::port0_w(u8 data)
{
	// P00-P03 select the panel switch row; P04-P06 control the LCD.
	m_lcdc->rs_w(BIT(data, 4));
	m_lcdc->rw_w(BIT(data, 5));
	m_lcdc->e_w(BIT(data, 6));
}


void kawai_k4_state::mem_map(address_map &map)
{
	map(0x0000, 0xbfff).rom().region("coderom", 0);
	map(0xc000, 0xdfff).ram().share("nvram");
	// TODO: replace these register latches with the K002-FP DCO/DCA, K003-FP DCF,
	// K004-FP DSP, K100-FP pan pot and card buffer devices.
	map(0xe000, 0xf7ff).rw(FUNC(kawai_k4_state::peripheral_r), FUNC(kawai_k4_state::peripheral_w));
}


static INPUT_PORTS_START(k4)
INPUT_PORTS_END

void kawai_k4_state::k4(machine_config &config)
{
	UPD78310(config, m_mpu, 12_MHz_XTAL); // µPD78310G-36
	m_mpu->set_addrmap(AS_PROGRAM, &kawai_k4_state::mem_map);
	m_mpu->port_out_cb<0>().set(FUNC(kawai_k4_state::port0_w));
	m_mpu->port_in_cb<1>().set(m_lcdc, FUNC(hd44780_device::db_r));
	m_mpu->port_out_cb<1>().set(m_lcdc, FUNC(hd44780_device::db_w));

	NVRAM(config, "nvram", nvram_device::DEFAULT_ALL_0); // HM62256LP-12 + battery

	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
	screen.set_refresh_hz(60);
	screen.set_screen_update(m_lcdc, FUNC(hd44780_device::screen_update));
	screen.set_size(6 * 16, 8 * 2);
	screen.set_visarea_full();
	screen.set_palette("palette");

	PALETTE(config, "palette", palette_device::MONOCHROME_INVERTED);

	HD44780(config, m_lcdc, 270'000); // clock not measured, datasheet typical clock used
	m_lcdc->set_lcd_size(2, 16);
}

ROM_START(k4)
	ROM_REGION(0x10000, "coderom", 0)
	ROM_SYSTEM_BIOS(0, "v1.4", "Version 1.4")
	ROMX_LOAD("k4_v14.u8",    0x00000, 0x10000, CRC(bf463780) SHA1(acb391fc8d074420a0dcdf0cbea523f6e7601e66), ROM_BIOS(0))
	ROM_SYSTEM_BIOS(1, "v1.3", "Version 1.3")
	ROMX_LOAD("p206b.u8",     0x00000, 0x10000, CRC(4108dec4) SHA1(8666583bc2f7a6792586325d8b98ffb684e51259), ROM_BIOS(1)) // 27C512
	ROM_SYSTEM_BIOS(2, "v1.0", "Version 1.0")
	ROMX_LOAD("p206_e5dp.u8", 0x00000, 0x10000, CRC(fdf47a8e) SHA1(2a074da55746cc51f1e06d3f88afc296750f3ea6), ROM_BIOS(2)) // 27C512

	ROM_REGION(0x180000, "waverom", 0)
	ROM_LOAD("d941_p202-m8dw.u30", 0x000000, 0x80000, CRC(7fca0b2c) SHA1(e5adddd3f9399ff57b0d9619181b9fecb4341798)) // TC534000P
	ROM_LOAD("d942_p203-m8dw.u29", 0x080000, 0x80000, CRC(263212a8) SHA1(8b175cbb6949cd47af06cc6faa180e26b6b18163)) // TC534000P
	ROM_LOAD("d943_p204-m8dw.u31", 0x100000, 0x80000, CRC(3e29064d) SHA1(e911d9bd23953613fefc96ba55c84c2dc69f58c1)) // TC534000P
ROM_END

ROM_START(k4r)
	ROM_REGION(0x10000, "coderom", 0)
	ROM_SYSTEM_BIOS(0, "v1.4", "Version 1.4")
	ROMX_LOAD("p207c.u7",    0x00000, 0x10000, CRC(17bdaaf6) SHA1(a1220fd479e154be679596ab18a1e76ae722a39d), ROM_BIOS(0))
	ROM_SYSTEM_BIOS(1, "v1.3", "Version 1.3")
	ROMX_LOAD("k4r_v1.3.u7", 0x00000, 0x10000, CRC(1b36d5ce) SHA1(afe25260e3ada1646d6c04cebb4c6dec41f742c0), ROM_BIOS(1))
	ROM_SYSTEM_BIOS(2, "v1.2", "Version 1.2")
	ROMX_LOAD("k4r_v1.2.u7", 0x00000, 0x10000, CRC(8b4953bc) SHA1(7bdb0d15bfe396fe6c2499c75137d71aef6c2ca8), ROM_BIOS(2))

	ROM_REGION(0x180000, "waverom", 0)
	ROM_LOAD("d941_p202-m8dw.u40", 0x000000, 0x80000, CRC(7fca0b2c) SHA1(e5adddd3f9399ff57b0d9619181b9fecb4341798)) // TC534000P
	ROM_LOAD("d942_p203-m8dw.u39", 0x080000, 0x80000, CRC(263212a8) SHA1(8b175cbb6949cd47af06cc6faa180e26b6b18163)) // TC534000P
	ROM_LOAD("d943_p204-m8dw.u38", 0x100000, 0x80000, CRC(3e29064d) SHA1(e911d9bd23953613fefc96ba55c84c2dc69f58c1)) // TC534000P
ROM_END

} // anonymous namespace


SYST(1989, k4,  0,  0, k4, k4, kawai_k4_state, empty_init, "Kawai Musical Instrument Manufacturing", "K4 16-bit Digital Synthesizer",         MACHINE_NO_SOUND | MACHINE_NOT_WORKING)
SYST(1989, k4r, k4, 0, k4, k4, kawai_k4_state, empty_init, "Kawai Musical Instrument Manufacturing", "K4r 16-bit Digital Synthesizer Module", MACHINE_NO_SOUND | MACHINE_NOT_WORKING)
