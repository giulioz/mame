// license:BSD-3-Clause
// copyright-holders:
/***************************************************************************

    Roland XV-3080 (and siblings) — 128-voice PCM synthesizer module.

    Hardware (from board + firmware reverse engineering):
    - CPU: Hitachi SH7042 (SH-2), on-chip 256 KB mask ROM + 4 KB RAM,
      on-chip peripherals at 0xFFFF8xxx (SCI0 = MIDI, MTU, BSC, WDT, ...).
    - Internal mask ROM at 0x00000000 (256 KB) — boot + MIDI kernel; the
      reset vector lives here (PC 0x000086F0, SP 0xFFFFFFFC).
    - CS decode (board schematics):
        CS0 XCS0    -> [XCS0,A19] decoder Y0/Y1 = TWO XP effect/voice chips
                       (XP0 @ 0x00200000, XP1 @ 0x00280000)
        CS1 XGACS1  -> I/O gate array (panel, LCD, MIDI-IN receive IRQ)
        CS2 XSRAMCS -> 512 KB battery SRAM (0x00800000, mirrors)
        CS3 XFMCS   -> flash PROM = application ROM (0x00D00000, 2 MB)
      plus a VG2618165CJ DRAM on the SH7042 DRAM controller (0x01000000).
    - MIDI IN/OUT on SCI0; MIDI-IN is additionally wired to the gate array
      to raise the receive IRQ.

    Status: BOOTS to the LCD.  The firmware comes all the way up to its main
    PERFORM screen, rendered on the emulated HD44780 character display.

    Getting there required, in order: the SH7042A part (dual mid-speed A/D at
    0xFFFF8410, gating boot on a healthy battery reading); the gate array modelled
    as a multiplexed interrupt controller (IRQ0=GAINT, reg 0x40 = pending source);
    the SH7042 WDT interval-timer interrupt (ITI, vector 152) -- the firmware's
    ITRON kernel uses it as its preemptive-dispatch software interrupt, without
    which timer-woken tasks never preempt the spinning main thread and boot
    deadlocks; and the SH7042 DMAC transfer engine, which streams the framebuffer
    to gate-array reg 0x38 (LCD command) / 0x39 (LCD data), exactly as on the
    JV-1080.  A full SH7042 DTC was also modelled (the firmware disables it here).

    Not yet done: sound (the XP voice/effect DSP output path), the front-panel
    button/encoder inputs, and NVRAM.

    The gate array (CS1) is modelled as a MULTIPLEXED INTERRUPT CONTROLLER, which
    is what it is: it drives IRQ0 (GAINT), and its register 0x40 low nibble is the
    pending-source index that the internal-ROM dispatcher (0x000086BC) uses to
    index the handler table at 0x00D0CC0C.  Decoded sources: 0 = MIDI-RX (handler
    reads GA[0x43]), 9 = periodic RTOS timer tick (kernel handler 0x8C12, a
    decrement-and-fire countdown that drives the scheduler), 1/7/8 = other RTOS
    events.  Here a periodic timer raises source 9 and reg 0x40 is read-to-ack;
    that alone gets the RTOS scheduling.

    The CPU is the SH7042A die variant: its on-chip peripherals at 0xFFFF84xx are
    the dual mid-speed A/D converter (ADCSR0 @0xFFFF8410, ADCSR1 @0xFFFF8411), which
    the firmware's panel-ADC init writes -- the plain SH7042 instead has a single
    high-speed ADC at 0xFFFF83E0, so 0xFFFF84xx read as unmapped.  Using SH7042A
    maps the ADC and the boot advances (the spin moves from crt0's pre-RTOS wait
    into the RTOS idle path).

    Remaining blocker: 3 of ~11 counted boot-init operations (DRAM semaphore
    0x01003218: byte0=started, byte1=done; crt0 waits at 0x00E1B01E for byte0<=
    byte1) never complete -- it stalls at byte0=11, byte1=8.  Forcing that wait
    past shows the RTOS otherwise comes fully up (reaches the crt0 idle loop
    0x00E1B00C), so the init-wait is the only gate; but even then no display is
    drawn, so the LCD path is a separate missing piece.

    The stall is a task blocked in the kernel wait-event primitive 0x00007E86,
    waiting on event bit 0 of its task-control block, which nothing ever posts.
    Interrupts that DO fire in this state: the gate-array timer (IRQ0 source 9),
    MTU channel 1 TGI1A (vector 96, ~800 Hz) and CMT0 CMI0 (vector 144, ~78 Hz).
    The event the blocked task needs must come from a peripheral that does NOT
    fire here (other MTU/ADC/SCI/XP) or from a producer task that is itself
    blocked -- pinning that down is the next step.  Not the cause: the SH7042 DTC
    (firmware writes 0 to all DTC enables, so it is off; a full DTC is nonetheless
    modelled in the CPU device now), the ADC battery gate (hooked healthy here),
    the DMAC (only DMAOR master-enable is written, no channel), and XP handshake
    (the XPs are written ~60k times but read only ~20, i.e. not polled).

    Both XP chips + the 32 MB wave ROM are on CS0 (XP0INT->IRQ1, XP1INT->IRQ2).
    Exact XTAL ~33 MHz (from SCI baud).  LCD (task #25) is fed by DMA per the
    schematics (DACK0/DREQ0); the JV-1080 (this GA's predecessor) drives an
    HD44780 via GA registers 0x38/0x39 -- the model to follow once init completes.

***************************************************************************/

#include "emu.h"

#include "bus/midi/midiinport.h"
#include "bus/midi/midioutport.h"
#include "cpu/sh/sh7042.h"
#include "machine/nvram.h"
#include "sound/roland_xp.h"
#include "video/hd44780.h"

#include "emupal.h"
#include "screen.h"
#include "speaker.h"


namespace {

class xv3080_state : public driver_device
{
public:
	xv3080_state(const machine_config &mconfig, device_type type, const char *tag) :
		driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_xp(*this, "xp%u", 0U),
		m_lcdc(*this, "lcdc")
	{ }

	void xv3080(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;

private:
	required_device<sh7042_device> m_maincpu;
	required_device_array<roland_xp_device, 2> m_xp;
	required_device<hd44780_device> m_lcdc;

	void lcd_palette(palette_device &palette) const;

	u16 m_pe = 0;
	emu_timer *m_ga_irq_timer = nullptr;

	void map(address_map &map) ATTR_COLD;
	TIMER_CALLBACK_MEMBER(ga_irq_tick);

	// Gate array: multiplexed interrupt controller on IRQ0 (GAINT).
	//   reg 0x40 low nibble = pending source index; the internal-ROM dispatcher
	//   (0x000086BC) reads it and jumps through the table at 0x00D0CC0C.
	// Decoded sources: 0=MIDI-RX (reads reg 0x43), 9=periodic RTOS timer tick
	//   (kernel handler 0x8C12, a decrement-and-fire countdown), 1/7/8=other
	//   RTOS events. IRQ0 is level: asserted while any source is pending.
	u8 m_ga_regs[0x1000] = {};
	u16 m_ga_pending = 0;
	void ga_set_pending(unsigned source);
	void ga_update_irq();

	template <unsigned N> u8 xp_r(offs_t offset) { return m_xp[N]->read(offset); }
	template <unsigned N> void xp_w(offs_t offset, u8 data) { m_xp[N]->write(offset, data); }
	u8 ga_r(offs_t offset);
	void ga_w(offs_t offset, u8 data);
	u16 cs2_r(offs_t offset, u16 mem_mask);
	void cs2_w(offs_t offset, u16 data, u16 mem_mask);
	u16 pe_r();
	void pe_w(u16 data);
};

void xv3080_state::machine_start()
{
	save_item(NAME(m_pe));
	save_item(NAME(m_ga_pending));
	// Gate-array periodic timer tick -> GA interrupt source 9 (the RTOS clock).
	// vec9 (kernel 0x8C12) no-ops until the RTOS arms a software timer, so it is
	// safe to run this from the start. Rate provisional (~1 kHz); the real reload
	// is programmed via GA regs 0x45/0x46.
	m_ga_irq_timer = timer_alloc(FUNC(xv3080_state::ga_irq_tick), this);
	m_ga_irq_timer->adjust(attotime::from_hz(1000), 0, attotime::from_hz(1000));
}

void xv3080_state::ga_update_irq()
{
	// IRQ0 (GAINT) is level: high while any source is pending.
	m_maincpu->set_input_line(0, m_ga_pending ? ASSERT_LINE : CLEAR_LINE);
}

void xv3080_state::ga_set_pending(unsigned source)
{
	m_ga_pending |= (1u << source);
	ga_update_irq();
}

TIMER_CALLBACK_MEMBER(xv3080_state::ga_irq_tick)
{
	ga_set_pending(9); // source 9 = periodic RTOS timer tick
}

// CS2 SRAM held as a zero-returning handler for now: it lets the DSP-upload
// mailbox status-poll fall through so the boot advances furthest (~0x00EB04EC).
// Real SRAM instead stalls at the earlier crt0 mailbox (0x00E1B01E) because the
// ISR that drains it (XP/GA interrupt, not yet modelled) never runs.
u16 xv3080_state::cs2_r(offs_t offset, u16 mem_mask)
{
	return 0;
}

void xv3080_state::cs2_w(offs_t offset, u16 data, u16 mem_mask)
{
}

// Gate array (CS1): panel/LCD I/O and the multiplexed interrupt controller.
u8 xv3080_state::ga_r(offs_t offset)
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

void xv3080_state::ga_w(offs_t offset, u8 data)
{
	// reg 0x40: the firmware raises software interrupts by ORing source bits in
	// (e.g. 0xE1B1CA: OR.B #2 -> source 1). Treat any set bit as a pending
	// request so those service ISRs (mailbox drains) actually run.
	if (offset == 0x40)
	{
		for (int i = 0; i < 16; i++)
			if (BIT(data, i))
				ga_set_pending(i);
		return;
	}

	// LCD (HD44780-compatible), DMA-fed to reg 0x38 (command) / 0x39 (data),
	// exactly as on the JV-1080.  Each byte is one bus cycle latched by E.
	if (offset == 0x38 || offset == 0x39)
	{
		m_lcdc->db_w(data);
		m_lcdc->rs_w(offset == 0x39); // 0x38 = command (RS=0), 0x39 = data (RS=1)
		m_lcdc->rw_w(0);
		m_lcdc->e_w(1);
		m_lcdc->e_w(0);
		return;
	}

	m_ga_regs[offset] = data;
}

void xv3080_state::lcd_palette(palette_device &palette) const
{
	palette.set_pen_color(0, rgb_t(2, 8, 30));     // background
	palette.set_pen_color(1, rgb_t(0, 190, 255));  // lit pixel
}

u16 xv3080_state::pe_r()
{
	// Port E: gate-array / LCD control + panel/status. Return last-written
	// output bits; leave input/status bits high (idle) for now.
	return m_pe | 0xc000;
}

void xv3080_state::pe_w(u16 data)
{
	m_pe = data;
}



void xv3080_state::map(address_map &map)
{
	// CS decode (from board schematics):
	//   CS0 XCS0   -> [XCS0,A19] decoder -> Y0/Y1 = the two XP effect/voice chips
	//   CS1 XGACS1 -> I/O gate array
	//   CS2 XSRAMCS-> 512 KB battery SRAM
	//   CS3 XFMCS  -> flash PROM (application ROM)
	//   plus a VG2618165CJ DRAM on the SH7042 DRAM controller.

	// Internal SH7042 mask ROM (boot + MIDI kernel); reset vector at 0.
	map(0x00000000, 0x0003ffff).rom().region("kernel", 0);

	// CS0: two XP chips, selected by A19. Each has a 0x4000-byte register space,
	// mirrored across its 512 KB half.
	map(0x00200000, 0x00203fff).rw(FUNC(xv3080_state::xp_r<0>), FUNC(xv3080_state::xp_w<0>)).mirror(0x07c000);
	map(0x00280000, 0x00283fff).rw(FUNC(xv3080_state::xp_r<1>), FUNC(xv3080_state::xp_w<1>)).mirror(0x07c000);

	// CS1: I/O gate array (panel/LCD/MIDI-IN IRQ; control/status @ 0x006Cxxxx).
	// Rest of CS1 backed as RAM for now; the GA control window is instrumented.
	map(0x00400000, 0x007fffff).ram();
	map(0x006c0000, 0x006c0fff).rw(FUNC(xv3080_state::ga_r), FUNC(xv3080_state::ga_w));

	// CS2: 512 KB battery SRAM. Zero-returning handler for bring-up (see cs2_r).
	map(0x00800000, 0x00bfffff).rw(FUNC(xv3080_state::cs2_r), FUNC(xv3080_state::cs2_w));

	// CS3: flash PROM (application ROM), 2 MB at 0x00D00000.
	map(0x00d00000, 0x00efffff).rom().region("progrom", 0);

	// DRAM (VG2618165CJ) work RAM (stack top 0x01200000).
	map(0x01000000, 0x011fffff).ram();
}


static INPUT_PORTS_START(xv3080)
INPUT_PORTS_END


void xv3080_state::xv3080(machine_config &config)
{
	// SH7042A: the "A"-die variant with the dual mid-speed A/D converter mapped at
	// 0xFFFF8410/8411 (plain SH7042 has a single high-speed ADC at 0xFFFF83E0).
	// The firmware's panel-ADC init writes 0xFFFF8410/8411, so this is the part.
	SH7042A(config, m_maincpu, 28'000'000); // TODO: exact XTAL/PLL not yet confirmed
	m_maincpu->set_addrmap(AS_PROGRAM, &xv3080_state::map);
	m_maincpu->read_porte().set(FUNC(xv3080_state::pe_r));
	m_maincpu->write_porte().set(FUNC(xv3080_state::pe_w));

	// On-chip A/D inputs: one channel monitors the backup battery, and the
	// firmware refuses to proceed with a dead (0x000) reading (as on the
	// JV-1080).  Feed a healthy mid-scale voltage on every channel for now.
	m_maincpu->read_adc<0>().set_constant(0x266);
	m_maincpu->read_adc<1>().set_constant(0x266);
	m_maincpu->read_adc<2>().set_constant(0x266);
	m_maincpu->read_adc<3>().set_constant(0x266);
	m_maincpu->read_adc<4>().set_constant(0x266);
	m_maincpu->read_adc<5>().set_constant(0x266);
	m_maincpu->read_adc<6>().set_constant(0x266);
	m_maincpu->read_adc<7>().set_constant(0x266);

	// LCD: HD44780-compatible character display, 2 lines x 40 columns, driven by
	// DMA to GA regs 0x38 (command) / 0x39 (data).
	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
	screen.set_refresh_hz(60);
	screen.set_size(40 * 6, 2 * 9);
	screen.set_visarea_full();
	screen.set_screen_update("lcdc", FUNC(hd44780_device::screen_update));
	screen.set_palette("palette");
	PALETTE(config, "palette", FUNC(xv3080_state::lcd_palette), 2);
	HD44780(config, m_lcdc, 270'000);
	m_lcdc->set_lcd_size(2, 40);

	SPEAKER(config, "speaker", 2).front();

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

	// MIDI is on SCI0.
	auto &mdin(MIDI_PORT(config, "mdin"));
	midiin_slot(mdin);
	mdin.rxd_handler().set(m_maincpu, FUNC(sh7042_device::sci_rx_w<0>));

	auto &mdout(MIDI_PORT(config, "mdout"));
	midiout_slot(mdout);
	m_maincpu->write_sci_tx<0>().set(mdout, FUNC(midi_port_device::write_txd));
}


ROM_START(xv3080)
	ROM_REGION32_BE(0x40000, "kernel", 0) // SH7042 on-chip mask ROM, dumped via the debug-ROM boot hook
	ROM_LOAD("xv3080_internal_maskrom.bin", 0x00000, 0x40000, CRC(b9f76b27) SHA1(52ff93d702cad686d091eeaa507604645b39e91c))

	ROM_REGION32_BE(0x200000, "progrom", 0) // external application ROM v1.11, extracted from the MIDI update
	ROM_LOAD("xv3080_1.11.bin", 0x000000, 0x200000, CRC(4d5b2473) SHA1(3c94673a837eca602664ab62aaf36140f677039d))

	ROM_REGION(0x2000000, "waverom", 0) // 32 MB internal wave ROM (shared by both XP chips); header "XV3080ROM_Ver001"
	ROM_LOAD("xv3080_waverom.bin", 0x000000, 0x2000000, CRC(34e32c1a) SHA1(258f124ae67e4da4a0ae332c4bac79bb96acaf29))
ROM_END

} // anonymous namespace


SYST(2000, xv3080, 0, 0, xv3080, xv3080, xv3080_state, empty_init, "Roland", "XV-3080", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
