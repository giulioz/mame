// license:BSD-3-Clause
// copyright-holders:
/***************************************************************************

    Roland XV-3080 / XV-5080 — 128-voice PCM synthesizer modules.

    Shared mainboard (from board + firmware reverse engineering):
    - CPU: Hitachi SH7042A (SH-2), on-chip 256 KB mask ROM + 4 KB RAM,
      on-chip peripherals at 0xFFFF8xxx (SCI0 = MIDI, MTU, WDT, DMAC, ADC, ...).
      The "A" die has the dual mid-speed A/D at 0xFFFF8410; the firmware reads it
      as a backup-battery monitor and refuses to boot on a dead (0x000) reading.
    - Internal SH7042A mask ROM at 0x00000000 (256 KB) — boot + ITRON kernel.
    - CS decode:
        CS0 XCS0    -> voice/effect chips:
                         XV-3080: two XP chips (XP0 @ 0x200000, XP1 @ 0x280000)
                         XV-5080: "XV" voice chips (not yet modelled)
        CS1 XGACS1  -> I/O gate array (panel, LCD, MIDI-IN receive IRQ)
        CS2 XSRAMCS -> battery SRAM (0x00800000)
        CS3 XFMCS   -> flash PROM = application ROM (0x00D00000, 2 MB)
      plus a DRAM on the SH7042 DRAM controller (0x01000000).

    The gate array (CS1) is modelled as a MULTIPLEXED INTERRUPT CONTROLLER: it
    drives IRQ0 (GAINT), and its register 0x40 low nibble is the pending-source
    index the internal-ROM dispatcher (0x000086BC) uses to index the handler
    table.  Sources: 0 = MIDI-RX, 9 = periodic RTOS tick, 1/7/8 = RTOS events.

    Booting the ITRON kernel additionally needed, in the CPU device: the WDT
    interval-timer interrupt (ITI, vector 152) which the kernel uses as its
    preemptive-dispatch software interrupt (without it, timer-woken tasks never
    preempt the spinning main thread and boot deadlocks); and the DMAC transfer
    engine, which streams the framebuffer to the LCD.

    Status:
    - XV-3080: BOOTS to the main PERFORM screen on its HD44780 character LCD
      (DMA-fed to gate-array reg 0x38 = command / 0x39 = data, as on the JV-1080).
    - XV-5080: same board with a SED1335 graphic LCD.

    Not yet done: sound (voice/effect DSP output), front-panel input, NVRAM.

***************************************************************************/

#include "emu.h"

#include "bus/midi/midiinport.h"
#include "bus/midi/midioutport.h"
#include "cpu/sh/sh7042.h"
#include "sound/roland_xp.h"
#include "video/hd44780.h"
#include "video/sed1330.h"

#include "emupal.h"
#include "screen.h"
#include "speaker.h"


namespace {

class xv_state : public driver_device
{
public:
	xv_state(const machine_config &mconfig, device_type type, const char *tag) :
		driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_xp(*this, "xp%u", 0U),
		m_hd44780(*this, "hd44780"),
		m_sed1335(*this, "sed1335")
	{ }

	void xv3080(machine_config &config);
	void xv5080(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;

private:
	required_device<sh7042_device> m_maincpu;
	optional_device_array<roland_xp_device, 2> m_xp;
	optional_device<hd44780_device> m_hd44780;
	optional_device<sed1330_device> m_sed1335;

	u16 m_pe = 0;
	emu_timer *m_ga_irq_timer = nullptr;

	// Gate array multiplexed interrupt controller (IRQ0 = GAINT).
	u8 m_ga_regs[0x1000] = {};
	u16 m_ga_pending = 0;
	void ga_set_pending(unsigned source);
	void ga_update_irq();
	TIMER_CALLBACK_MEMBER(ga_irq_tick);

	template <unsigned N> u8 xp_r(offs_t offset) { return m_xp[N]->read(offset); }
	template <unsigned N> void xp_w(offs_t offset, u8 data) { m_xp[N]->write(offset, data); }
	u8 ga_r(offs_t offset);
	void ga_w(offs_t offset, u8 data);
	u16 cs2_r(offs_t offset, u16 mem_mask) { return 0; }
	void cs2_w(offs_t offset, u16 data, u16 mem_mask) { }
	u16 pe_r() { return m_pe | 0xc000; }
	void pe_w(u16 data) { m_pe = data; }
	void lcd_palette(palette_device &palette) const;

	void xv_base(machine_config &config);
	void map_common(address_map &map) ATTR_COLD;
	void map_3080(address_map &map) ATTR_COLD;
	void map_5080(address_map &map) ATTR_COLD;
	void sed1335_vram(address_map &map) ATTR_COLD;
};

void xv_state::machine_start()
{
	save_item(NAME(m_pe));
	save_item(NAME(m_ga_pending));
	// Gate-array periodic timer tick -> GA interrupt source 9 (the RTOS clock).
	// vec9 (kernel 0x8C12) no-ops until the RTOS arms a software timer, so it is
	// safe to run this from the start. Rate provisional (~1 kHz).
	m_ga_irq_timer = timer_alloc(FUNC(xv_state::ga_irq_tick), this);
	m_ga_irq_timer->adjust(attotime::from_hz(1000), 0, attotime::from_hz(1000));
}

void xv_state::ga_update_irq()
{
	// IRQ0 (GAINT) is level: high while any source is pending.
	m_maincpu->set_input_line(0, m_ga_pending ? ASSERT_LINE : CLEAR_LINE);
}

void xv_state::ga_set_pending(unsigned source)
{
	m_ga_pending |= (1u << source);
	ga_update_irq();
}

TIMER_CALLBACK_MEMBER(xv_state::ga_irq_tick)
{
	ga_set_pending(9); // source 9 = periodic RTOS timer tick
}

// Gate array (CS1): panel/LCD I/O and the multiplexed interrupt controller.
u8 xv_state::ga_r(offs_t offset)
{
	// reg 0x40: pending-interrupt-source register, read-to-acknowledge. The
	// dispatcher reads (low nibble) to pick the handler; return the highest
	// pending source and clear it so IRQ0 deasserts when none remain.
	if (offset == 0x40)
	{
		u8 source = 0;
		for (int i = 15; i >= 0; i--)
			if (BIT(m_ga_pending, i)) { source = i; break; }
		if (!machine().side_effects_disabled())
		{
			m_ga_pending &= ~(1u << source);
			ga_update_irq();
		}
		return source;
	}

	return m_ga_regs[offset];
}

void xv_state::ga_w(offs_t offset, u8 data)
{
	// reg 0x40: the firmware raises software interrupts by ORing source bits in
	// (e.g. 0xE1B1CA: OR.B #2 -> source 1). Treat any set bit as a pending
	// request so those service ISRs actually run.
	if (offset == 0x40)
	{
		for (int i = 0; i < 16; i++)
			if (BIT(data, i))
				ga_set_pending(i);
		return;
	}

	// XV-3080 LCD (HD44780-compatible), DMA-fed to reg 0x38 (command) / 0x39
	// (data), exactly as on the JV-1080.  Each byte is one bus cycle latched by E.
	if (m_hd44780 && (offset == 0x38 || offset == 0x39))
	{
		m_hd44780->db_w(data);
		m_hd44780->rs_w(offset == 0x39); // 0x38 = command (RS=0), 0x39 = data (RS=1)
		m_hd44780->rw_w(0);
		m_hd44780->e_w(1);
		m_hd44780->e_w(0);
		return;
	}

	m_ga_regs[offset] = data;
}

void xv_state::lcd_palette(palette_device &palette) const
{
	palette.set_pen_color(0, rgb_t(2, 8, 30));     // background
	palette.set_pen_color(1, rgb_t(0, 190, 255));  // lit pixel
}


void xv_state::map_common(address_map &map)
{
	// Internal SH7042A mask ROM (boot + kernel); reset vector at 0.
	map(0x00000000, 0x0003ffff).rom().region("kernel", 0);

	// CS1: I/O gate array (panel/LCD/MIDI-IN IRQ; control/status @ 0x006Cxxxx).
	// Rest of CS1 backed as RAM; the GA control window is a handler.
	map(0x00400000, 0x007fffff).ram();
	map(0x006c0000, 0x006c0fff).rw(FUNC(xv_state::ga_r), FUNC(xv_state::ga_w));

	// CS2: battery SRAM. Zero-returning handler for bring-up.
	map(0x00800000, 0x00bfffff).rw(FUNC(xv_state::cs2_r), FUNC(xv_state::cs2_w));

	// CS3: flash PROM (application ROM), 2 MB at 0x00D00000.
	map(0x00d00000, 0x00efffff).rom().region("progrom", 0);

	// DRAM work RAM (XV-3080 stack top 0x01200000; XV-5080 uses more, top
	// 0x01400000).  Map 4 MB to cover both.
	map(0x01000000, 0x013fffff).ram();
}

void xv_state::map_3080(address_map &map)
{
	map_common(map);
	// CS0: two XP chips, selected by A19, each mirrored across its 512 KB half.
	map(0x00200000, 0x00203fff).rw(FUNC(xv_state::xp_r<0>), FUNC(xv_state::xp_w<0>)).mirror(0x07c000);
	map(0x00280000, 0x00283fff).rw(FUNC(xv_state::xp_r<1>), FUNC(xv_state::xp_w<1>)).mirror(0x07c000);
}

void xv_state::map_5080(address_map &map)
{
	map_common(map);
	// CS0: XV voice chips (not yet modelled) — backed as RAM so read-backs work.
	map(0x00200000, 0x002fffff).ram();

	// SED1335 graphic LCD, memory-mapped on two adjacent addresses within CS1
	// (the firmware DMAs the framebuffer to them).  Even = data / status,
	// odd = command / data-read, as on other SED133x boards (e.g. ympsr2000).
	map(0x005c0000, 0x005c0000).rw(m_sed1335, FUNC(sed1330_device::status_r), FUNC(sed1330_device::data_w));
	map(0x005c0001, 0x005c0001).rw(m_sed1335, FUNC(sed1330_device::data_r), FUNC(sed1330_device::command_w));
}

void xv_state::sed1335_vram(address_map &map)
{
	map(0x0000, 0x7fff).ram(); // 32 KB display RAM
}


static INPUT_PORTS_START(xv)
INPUT_PORTS_END


void xv_state::xv_base(machine_config &config)
{
	// SH7042A: the "A"-die variant with the dual mid-speed A/D at 0xFFFF8410/8411.
	SH7042A(config, m_maincpu, 28'000'000); // TODO: exact XTAL/PLL not yet confirmed
	m_maincpu->read_porte().set(FUNC(xv_state::pe_r));
	m_maincpu->write_porte().set(FUNC(xv_state::pe_w));

	// On-chip A/D inputs: one channel monitors the backup battery, and the
	// firmware refuses to proceed with a dead (0x000) reading.  Feed a healthy
	// mid-scale voltage on every channel for now.
	m_maincpu->read_adc<0>().set_constant(0x266);
	m_maincpu->read_adc<1>().set_constant(0x266);
	m_maincpu->read_adc<2>().set_constant(0x266);
	m_maincpu->read_adc<3>().set_constant(0x266);
	m_maincpu->read_adc<4>().set_constant(0x266);
	m_maincpu->read_adc<5>().set_constant(0x266);
	m_maincpu->read_adc<6>().set_constant(0x266);
	m_maincpu->read_adc<7>().set_constant(0x266);

	SPEAKER(config, "speaker", 2).front();

	// MIDI is on SCI0.
	auto &mdin(MIDI_PORT(config, "mdin"));
	midiin_slot(mdin);
	mdin.rxd_handler().set(m_maincpu, FUNC(sh7042_device::sci_rx_w<0>));

	auto &mdout(MIDI_PORT(config, "mdout"));
	midiout_slot(mdout);
	m_maincpu->write_sci_tx<0>().set(mdout, FUNC(midi_port_device::write_txd));
}

void xv_state::xv3080(machine_config &config)
{
	xv_base(config);
	m_maincpu->set_addrmap(AS_PROGRAM, &xv_state::map_3080);

	// LCD: HD44780-compatible character display, 2 lines x 40 columns, DMA-fed to
	// gate-array regs 0x38 (command) / 0x39 (data).
	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
	screen.set_refresh_hz(60);
	screen.set_size(40 * 6, 2 * 9);
	screen.set_visarea_full();
	screen.set_screen_update("hd44780", FUNC(hd44780_device::screen_update));
	screen.set_palette("palette");
	PALETTE(config, "palette", FUNC(xv_state::lcd_palette), 2);
	HD44780(config, m_hd44780, 270'000);
	m_hd44780->set_lcd_size(2, 40);

	// Two XP effect/voice chips on CS0, sharing the 32 MB wave ROM.
	// Per schematics: IRQ0 = gate-array INT, IRQ1 = XP0 INT, IRQ2 = XP1 INT.
	for (auto &xp : m_xp)
	{
		ROLAND_XP(config, xp, 24.576_MHz_XTAL);
		xp->set_device_rom_tag("waverom");
		xp->add_route(0, "speaker", 1.0, 0);
		xp->add_route(1, "speaker", 1.0, 1);
	}
	m_xp[0]->int_callback().set_inputline(m_maincpu, 1); // XP0INT -> IRQ1
	m_xp[1]->int_callback().set_inputline(m_maincpu, 2); // XP1INT -> IRQ2
}

void xv_state::xv5080(machine_config &config)
{
	xv_base(config);
	m_maincpu->set_addrmap(AS_PROGRAM, &xv_state::map_5080);

	// SED1335 graphic LCD at 0x005C0000/0x005C0001 (DMA-fed by the firmware).
	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
	screen.set_refresh_hz(60);
	screen.set_size(320, 80);
	screen.set_visarea_full();
	screen.set_screen_update("sed1335", FUNC(sed1330_device::screen_update));
	screen.set_palette("palette");
	PALETTE(config, "palette", FUNC(xv_state::lcd_palette), 2);
	SED1330(config, m_sed1335, 8'000'000);
	m_sed1335->set_screen("screen");
	m_sed1335->set_addrmap(0, &xv_state::sed1335_vram);
}


ROM_START(xv3080)
	ROM_REGION32_BE(0x40000, "kernel", 0) // SH7042A on-chip mask ROM, dumped via the debug-ROM boot hook
	ROM_LOAD("xv3080_internal_maskrom.bin", 0x00000, 0x40000, CRC(b9f76b27) SHA1(52ff93d702cad686d091eeaa507604645b39e91c))

	ROM_REGION32_BE(0x200000, "progrom", 0) // external application ROM v1.11, extracted from the MIDI update
	ROM_LOAD("xv3080_1.11.bin", 0x000000, 0x200000, CRC(4d5b2473) SHA1(3c94673a837eca602664ab62aaf36140f677039d))

	ROM_REGION(0x2000000, "waverom", 0) // 32 MB internal wave ROM (shared by both XP chips); header "XV3080ROM_Ver001"
	ROM_LOAD("xv3080_waverom.bin", 0x000000, 0x2000000, CRC(34e32c1a) SHA1(258f124ae67e4da4a0ae332c4bac79bb96acaf29))
ROM_END

ROM_START(xv5080)
	ROM_REGION32_BE(0x40000, "kernel", 0) // SH7042A on-chip mask ROM
	ROM_LOAD("xv5080_internal_maskrom_256k.bin", 0x00000, 0x40000, CRC(f749a5bd) SHA1(3d2a9ca8cd10130e71353f82f440e46d44a3daf0))

	ROM_REGION32_BE(0x200000, "progrom", 0) // external application ROM v1.30
	ROM_LOAD("xv5080_1.30.bin", 0x000000, 0x200000, CRC(598f5c14) SHA1(14fdb7464c718a074530439235f92199a25e2ee3))
ROM_END

} // anonymous namespace


SYST(2000, xv3080, 0, 0, xv3080, xv, xv_state, empty_init, "Roland", "XV-3080", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
SYST(2000, xv5080, 0, 0, xv5080, xv, xv_state, empty_init, "Roland", "XV-5080", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
