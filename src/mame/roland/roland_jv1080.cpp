// license:BSD-3-Clause
// copyright-holders:giulioz
/****************************************************************************

    Skeleton driver for Roland JV-1080.

****************************************************************************/

#include "emu.h"
#include "machine/nvram.h"
#include "cpu/sh/sh7032.h"


namespace {

class roland_jv1080_state : public driver_device
{
public:
	roland_jv1080_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
	{
	}

	void jv1080(machine_config &config);

private:
	void jv1080_mem_map(address_map &map);

	required_device<sh7032_device> m_maincpu;
};


void roland_jv1080_state::jv1080_mem_map(address_map &map)
{
	map(0x01000000, 0x0101ffff).ram().mirror(0x8000000); // DRAM
	map(0x02000000, 0x020fffff).rom().region("progrom", 0).mirror(0x08000000);

	// 0x0000000-0x0ffffff 0x8000000-0x8ffffff CS0: (internal rom)
	// 0x1000000-0x1ffffff 0x9000000-0x9ffffff CS1: (DRAM)
	// 0x2000000-0x2ffffff 0xa000000-0xaffffff CS2: GA CS2
	// 0x4000000-0x4ffffff 0xc000000-0xcffffff CS4: GA CS4
	// 0x6000000-0x6ffffff 0xe000000-0xeffffff CS6: GA CS6
	// 0xf000000-0xfffffff: (internal ram)

	// GA   MASK: 0x3f + A19-20-21
	// SRAM MASK: 0xFFFF
	// CARD MASK: 0xFF
	// XP   MASK: 0x3FFF
	
	// CS2 EC0: PROM (0x00)
	// CS2 EC2: RAM CARD GA (0x28)
	// CS2 EC3: SRAM (0x38)
	// CS4 EC4: XP (0x20)

	// LCD: 0bx00xxxxxxxxxxxxx11000x?
	// LCD: CS4 0b111


  // writes
  // 0x000009b0-0x000009e4
  // 0x04380000-0x0438003f  GA?
  // 0x07fff000-0x07ffffff
}

static INPUT_PORTS_START(jv1080)
INPUT_PORTS_END

void roland_jv1080_state::jv1080(machine_config &config)
{
	SH7032(config, m_maincpu, 20_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &roland_jv1080_state::jv1080_mem_map);
}

ROM_START(jv1080)
	ROM_REGION(0x10000, "maincpu", 0)
	ROM_LOAD("roland_r00677323_6437034c12f.ic15", 0x0000, 0x10000, CRC(609c8b8d) SHA1(075b4d60d14afa3ff3c825b8da5c8687aca84560))

	ROM_REGION32_BE(0x100000, "progrom", 0)
	ROM_LOAD("roland_r00678167.ic20", 0x00000, 0x100000, CRC(0e26e075) SHA1(77f09efecf267def69b4e8f851cf124798347c03))

	// ROM_REGION(0x400000, "waverom", 0)
	// ROM_LOAD("roland-a_r00459923.ic29", 0x000000, 0x200000, CRC(1348c0dc) SHA1(37e28498351fb502f6d43398d288a026c02b446d))
	// ROM_LOAD("roland-b_r00459934.ic28", 0x000000, 0x200000, CRC(1348c0dc) SHA1(37e28498351fb502f6d43398d288a026c02b446d))
	// ROM_LOAD("roland-c_r00459945.ic27", 0x000000, 0x200000, CRC(1348c0dc) SHA1(37e28498351fb502f6d43398d288a026c02b446d))
	// ROM_LOAD("roland-d_r00459956.ic26", 0x000000, 0x200000, CRC(1348c0dc) SHA1(37e28498351fb502f6d43398d288a026c02b446d))
ROM_END

} // anonymous namespace


SYST(1994, jv1080, 0, 0, jv1080, jv1080, roland_jv1080_state, empty_init, "Roland", "JV-1080 Super JV 64 Voice Synthesizer Module", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
