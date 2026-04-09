// license:BSD-3-Clause
// copyright-holders:giulioz
/****************************************************************************

    Skeleton driver for Roland JV-1080.

    Hardware:
    - CPU: Hitachi SH7034 (HD6477034) @ 20MHz
    - I/O GA: Custom gate array (HG62E11B24FS)
      Generates multiplexed IRQ5 with status byte in register 0x3C:
        Index 0: Button key event (key code in reg 0x3E, bit 7 = press)
        Index 1: Encoder rotation (signed delta in reg 0x3D)
        Index 8: Button/ADC scan complete
        Index 9: Timer tick (~1ms, drives cooperative scheduler)
      Register 0x3A: PORT input (active-low: bit1=PREVIEW, bit2=INC, bit3=DEC, bit4=ENC_SW)
      Register 0x3B: Matrix scan state (active-low)
      Registers 0x38/0x39: LCD command/data (HD44780 via DMA ch0)
    - LCD: Optrex DMC-2079 (40x2 chars, HD44780-compatible)
    - XP: Roland custom sound generator (14-bit address bus)
    - DRAM: 1 Mbit (128 KB) on SH7034 DRAM interface
    - SRAM: 512 Kbit (64 KB) battery-backed
    - Program ROM: 8 Mbit (1 MB)
    - Wave ROM: 4x 16 Mbit (4x 2 MB), scrambled

****************************************************************************/

#include "emu.h"
#include "machine/nvram.h"
#include "cpu/sh/sh7032.h"
#include "video/hd44780.h"

#include "sound/roland_xp.h"

#include "emupal.h"
#include "screen.h"
#include "speaker.h"

#include "jv1080.lh"


namespace {

class roland_jv1080_state : public driver_device
{
public:
	roland_jv1080_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_lcdc(*this, "lcdc")
		, m_xp(*this, "xp")
		, m_buttons(*this, "BUTTONS%u", 1U)
	{
	}

	void jv1080(machine_config &config);

	void init_jv1080() ATTR_COLD;

private:
	void jv1080_mem_map(address_map &map);

	void ga_w(offs_t offset, uint8_t data);
	uint8_t ga_r(offs_t offset);

	void xp_w(offs_t offset, uint8_t data);
	uint8_t xp_r(offs_t offset);
	TIMER_CALLBACK_MEMBER(ga_tick_cb);
	TIMER_CALLBACK_MEMBER(ga_button_scan_cb);
	TIMER_CALLBACK_MEMBER(xp_dump_cb);

	void ga_deliver_next_irq();
	void ga_queue_irq(uint8_t index);
	void ga_send_key_event(uint8_t keycode);
	void ga_send_encoder_delta(int8_t delta);

	virtual void machine_start() override ATTR_COLD;

	void lcd_palette(palette_device &palette) const ATTR_COLD;
	void descramble_waverom(u8 *dst, const u8 *src, offs_t size) ATTR_COLD;

	required_device<sh7032_device> m_maincpu;
	required_device<hd44780_device> m_lcdc;
	required_device<roland_xp_device> m_xp;
	required_ioport_array<5> m_buttons;

	uint8_t m_xp_regs[0x4000];  // debug shadow of XP register writes

	uint8_t m_ga_regs[64];
	emu_timer *m_ga_tick_timer = nullptr;
	emu_timer *m_ga_button_scan_timer = nullptr;
	emu_timer *m_xp_dump_timer = nullptr;

	static constexpr int GA_IRQ_QUEUE_SIZE = 64;
	struct ga_irq_entry { uint8_t index; uint8_t reg_3d; uint8_t reg_3e; };
	ga_irq_entry m_ga_irq_queue[GA_IRQ_QUEUE_SIZE];
	uint8_t m_ga_irq_queue_head = 0;
	uint8_t m_ga_irq_queue_tail = 0;
	bool m_ga_irq_pending = false;
	bool m_ga_tick_pending = false;
	bool m_ga_scan_pending = false;

	uint64_t m_button_prev_state = 0;
};


void roland_jv1080_state::jv1080_mem_map(address_map &map)
{
	// Area 1: DRAM (1 Mbit = 128 KB)
	map(0x01000000, 0x0101ffff).ram().mirror(0x08000000);

	// Area 2: CS2 — address decoding by GA using A19-A21
	map(0x02000000, 0x020fffff).rom().region("progrom", 0).mirror(0x08000000);
	map(0x02280000, 0x022fffff).ram().mirror(0x08000000); // CS2 ECS2 (card related)
	map(0x02380000, 0x0238ffff).ram().share("nvram").mirror(0x08000000);

	// Area 4: CS4 — XP PCM+DSP (14-bit register address space)
	map(0x04000000, 0x04003fff).rw(FUNC(roland_jv1080_state::xp_r), FUNC(roland_jv1080_state::xp_w)).mirror(0x08000000);
	map(0x04380000, 0x0438003f).rw(FUNC(roland_jv1080_state::ga_r), FUNC(roland_jv1080_state::ga_w)).mirror(0x08000000);
}

void roland_jv1080_state::machine_start()
{
	std::fill(std::begin(m_xp_regs), std::end(m_xp_regs), 0);
	std::fill(std::begin(m_ga_regs), std::end(m_ga_regs), 0);
	std::memset(m_ga_irq_queue, 0, sizeof(m_ga_irq_queue));
	m_ga_irq_queue_head = 0;
	m_ga_irq_queue_tail = 0;
	m_ga_irq_pending = false;
	m_ga_tick_pending = false;
	m_ga_scan_pending = false;
	m_button_prev_state = 0;

	save_item(NAME(m_xp_regs));
	save_item(NAME(m_ga_regs));
	save_item(NAME(m_ga_irq_queue_head));
	save_item(NAME(m_ga_irq_queue_tail));
	save_item(NAME(m_ga_irq_pending));
	save_item(NAME(m_button_prev_state));

	m_ga_tick_timer = timer_alloc(FUNC(roland_jv1080_state::ga_tick_cb), this);
	m_ga_tick_timer->adjust(attotime::from_msec(1), 0, attotime::from_msec(1));

	m_ga_button_scan_timer = timer_alloc(FUNC(roland_jv1080_state::ga_button_scan_cb), this);
	m_ga_button_scan_timer->adjust(attotime::from_seconds(3), 0, attotime::from_msec(10));

	m_xp_dump_timer = timer_alloc(FUNC(roland_jv1080_state::xp_dump_cb), this);
	m_xp_dump_timer->adjust(attotime::from_msec(10), 0, attotime::from_msec(10));
}

// --- XP register access (pass-through to XP device, shadow for debugging) ---

uint8_t roland_jv1080_state::xp_r(offs_t offset)
{
	return m_xp->read(offset);
}

void roland_jv1080_state::xp_w(offs_t offset, uint8_t data)
{
	m_xp_regs[offset] = data;
	m_xp->write(offset, data);
}

// --- GA IRQ delivery ---

void roland_jv1080_state::ga_deliver_next_irq()
{
	// Priority: key/encoder events > scan complete > timer tick
	if (m_ga_irq_queue_head != m_ga_irq_queue_tail)
	{
		ga_irq_entry &next = m_ga_irq_queue[m_ga_irq_queue_head];
		m_ga_regs[0x3c] = next.index;
		m_ga_regs[0x3d] = next.reg_3d;
		m_ga_regs[0x3e] = next.reg_3e;
		m_ga_irq_queue_head = (m_ga_irq_queue_head + 1) % GA_IRQ_QUEUE_SIZE;
		m_ga_irq_pending = true;
		m_maincpu->set_input_line(5, ASSERT_LINE);
	}
	else if (m_ga_scan_pending)
	{
		m_ga_scan_pending = false;
		m_ga_regs[0x3c] = 0x08;
		m_ga_irq_pending = true;
		m_maincpu->set_input_line(5, ASSERT_LINE);
	}
	else if (m_ga_tick_pending)
	{
		m_ga_tick_pending = false;
		m_ga_regs[0x3c] = 0x09;
		m_ga_irq_pending = true;
		m_maincpu->set_input_line(5, ASSERT_LINE);
	}
}

void roland_jv1080_state::ga_queue_irq(uint8_t index)
{
	if (index == 0x09)
		m_ga_tick_pending = true;
	else if (index == 0x08)
		m_ga_scan_pending = true;
	else
	{
		uint8_t next_tail = (m_ga_irq_queue_tail + 1) % GA_IRQ_QUEUE_SIZE;
		if (next_tail == m_ga_irq_queue_head)
			return;
		m_ga_irq_queue[m_ga_irq_queue_tail] = { index, m_ga_regs[0x3d], m_ga_regs[0x3e] };
		m_ga_irq_queue_tail = next_tail;
	}

	if (!m_ga_irq_pending)
		ga_deliver_next_irq();
}

void roland_jv1080_state::ga_send_key_event(uint8_t keycode)
{
	m_ga_regs[0x3e] = keycode;
	ga_queue_irq(0x00);
}

void roland_jv1080_state::ga_send_encoder_delta(int8_t delta)
{
	m_ga_regs[0x3d] = (uint8_t)delta;
	ga_queue_irq(0x01);
}

// --- GA register access ---

uint8_t roland_jv1080_state::ga_r(offs_t offset)
{
	uint8_t data = m_ga_regs[offset];

	if (!machine().side_effects_disabled())
	{
		switch (offset)
		{
		case 0x3a:
		{
			// PORT register — active-low button inputs
			data = 0xfe; // idle: bits 1-7 high, bit 0 low (LED output)
			uint8_t buttons1 = m_buttons[0]->read();
			uint8_t buttons4 = m_buttons[3]->read();
			uint8_t buttons5 = m_buttons[4]->read();
			if (buttons1 & 0x01) data &= ~0x02; // PREVIEW
			if (buttons4 & 0x20) data &= ~0x04; // INC
			if (buttons4 & 0x10) data &= ~0x08; // DEC
			if (buttons5 & 0x04) data &= ~0x10; // Encoder push
			break;
		}
		case 0x3b:
			data = 0xff; // matrix scan state: all released
			break;
		case 0x3c:
			// Acknowledge current IRQ, deliver next
			m_ga_irq_pending = false;
			m_maincpu->set_input_line(5, CLEAR_LINE);
			ga_deliver_next_irq();
			break;
		}
	}

	return data;
}

void roland_jv1080_state::ga_w(offs_t offset, uint8_t data)
{
	m_ga_regs[offset] = data;

	switch (offset)
	{
	case 0x38:
		// LCD command register (RS=0), accessed via DMA dest+0
		m_lcdc->db_w(data);
		m_lcdc->rs_w(0);
		m_lcdc->rw_w(0);
		m_lcdc->e_w(1);
		m_lcdc->e_w(0);
		break;
	case 0x39:
		// LCD data register (RS=1), accessed via DMA dest+1
		m_lcdc->db_w(data);
		m_lcdc->rs_w(1);
		m_lcdc->rw_w(0);
		m_lcdc->e_w(1);
		m_lcdc->e_w(0);
		break;
	}
}

// --- Timers ---

TIMER_CALLBACK_MEMBER(roland_jv1080_state::ga_tick_cb)
{
	ga_queue_irq(0x09);
}

TIMER_CALLBACK_MEMBER(roland_jv1080_state::ga_button_scan_cb)
{
	// Button matrix key codes (GA scans SS0-SS3 × D0-D7):
	// Bit 7 (0x80) = press, bit 7 clear = release
	// 0xFF = PORT button (handled via GA reg 0x3A, not key events)
	static const uint8_t keymap[] = {
		// BUTTONS1: PREVIEW(PORT), PALETTE, PARAMETER, 1-8/9-16, 1/9, 2/10, 3/11, 4/12
		0xFF, 0x10, 0x08, 0x00, 0x01, 0x09, 0x11, 0x02,
		// BUTTONS2: 5/13, 6/14, 7/15, 8/16, PERFORM, PATCH, RHYTHM, SYSTEM
		0x0A, 0x12, 0x03, 0x0B, 0x06, 0x0E, 0x16, 0x13,
		// BUTTONS3: UTILITY, USER/CARD, PRESET, EXP, SOUND_A, SOUND_B, SOUND_C, SOUND_D
		0x04, 0x07, 0x0F, 0x17, 0x14, 0x05, 0x0D, 0x15,
		// BUTTONS4: EFX, SHIFT, EXIT, ENTER, DEC(PORT), INC(PORT), CUR_U, CUR_D
		0x0C, 0x1D, 0x1E, 0x1F, 0xFF, 0xFF, 0x1B, 0x18,
		// BUTTONS5: CUR_L, CUR_R, ENC_SW(PORT)
		0x19, 0x1C, 0xFF,
	};

	uint64_t current_state = 0;
	for (int i = 0; i < 5; i++)
		current_state |= (uint64_t)m_buttons[i]->read() << (i * 8);

	uint64_t changed = current_state ^ m_button_prev_state;
	m_button_prev_state = current_state;

	for (int i = 0; i < 35; i++)
	{
		if (BIT(changed, i))
		{
			uint8_t keycode = keymap[i];
			if (keycode == 0xFF)
				continue;
			if (BIT(current_state, i))
				ga_send_key_event(keycode | 0x80);  // press
			else
				ga_send_key_event(keycode);          // release
		}
	}

	// Encoder
	if (BIT(changed, 35) && BIT(current_state, 35))
		ga_send_encoder_delta(1);
	if (BIT(changed, 36) && BIT(current_state, 36))
		ga_send_encoder_delta(-1);

	// Periodic ADC scan complete
	static int scan_div = 0;
	if (++scan_div >= 5)
	{
		scan_div = 0;
		ga_queue_irq(0x08);
	}
}

// --- XP DSP program dump ---

TIMER_CALLBACK_MEMBER(roland_jv1080_state::xp_dump_cb)
{
	FILE *f = fopen("xp_dsp_dump.hex", "w");
	if (!f)
		return;

	// PRAM + CRAM
	fprintf(f, "Program 0 (0x3400/0x2C00)\n");
	for (offs_t addr = 0x3400; addr < 0x3600; addr += 4)
	{
		int cram_addr = ((addr - 0x3400) / 4) * 2 + 0x2C00;
		int full_op = (m_xp_regs[addr] << 24) | (m_xp_regs[addr + 1] << 16) | (m_xp_regs[addr + 2] << 8) | m_xp_regs[addr + 3];
		int eram_op = full_op >> 16;
		int alu_op = (full_op >> 12) & 0xf;
		int mem_slot = (full_op >> 6) & 0x3f;
		int unk_op = full_op & 0x3f;
		fprintf(f, "%04X: %02X%02X%02X%02X %02X%02X    eram:%04X alu:%X mem:%02X unk:%02X\n", addr, m_xp_regs[addr], m_xp_regs[addr + 1], m_xp_regs[addr + 2], m_xp_regs[addr + 3], m_xp_regs[cram_addr], m_xp_regs[cram_addr + 1], eram_op, alu_op, mem_slot, unk_op);
	}
	fprintf(f, "\n\n");
	
	fprintf(f, "Program 1 (0x3600/0x2D00)\n");
	for (offs_t addr = 0x3600; addr < 0x3800; addr += 4)
	{
		int cram_addr = ((addr - 0x3600) / 4) * 2 + 0x2D00;
		int full_op = (m_xp_regs[addr] << 24) | (m_xp_regs[addr + 1] << 16) | (m_xp_regs[addr + 2] << 8) | m_xp_regs[addr + 3];
		int eram_op = full_op >> 16;
		int alu_op = (full_op >> 12) & 0xf;
		int mem_slot = (full_op >> 6) & 0x3f;
		int unk_op = full_op & 0x3f;
		fprintf(f, "%04X: %02X%02X%02X%02X %02X%02X    eram:%04X alu:%X mem:%02X unk:%02X\n", addr, m_xp_regs[addr], m_xp_regs[addr + 1], m_xp_regs[addr + 2], m_xp_regs[addr + 3], m_xp_regs[cram_addr], m_xp_regs[cram_addr + 1], eram_op, alu_op, mem_slot, unk_op);
	}
	fprintf(f, "\n\n");
	
	// CRAM area: 0x2C00-0x2FFF (offset 0x000-0x3FF in dsp_program) — 2 hex bytes per line
	for (offs_t addr = 0x2C00; addr < 0x3000; addr += 2)
	{
		fprintf(f, "%04X: %02X%02X\n", addr, m_xp_regs[addr], m_xp_regs[addr + 1]);
	}

	// Rest: 0x3000-0x38FF (offset 0x400-0xCFF in dsp_program) — 4 hex bytes per line
	for (offs_t addr = 0x3000; addr < 0x3900; addr += 4)
	{
		fprintf(f, "%04X: %02X%02X%02X%02X\n", addr, m_xp_regs[addr], m_xp_regs[addr + 1], m_xp_regs[addr + 2], m_xp_regs[addr + 3]);
	}

	fclose(f);
}

// --- LCD palette ---

void roland_jv1080_state::lcd_palette(palette_device &palette) const
{
	// Optrex DMC-2079 LCD colors
	palette.set_pen_color(0, rgb_t(62, 112, 47));   // pixel off
	palette.set_pen_color(1, rgb_t(250, 254, 0));    // pixel on
}

// --- Wave ROM descrambling ---

void roland_jv1080_state::descramble_waverom(u8 *dst, const u8 *src, offs_t size)
{
	for (offs_t i = 0; i < size; i++)
	{
		// Address descrambling (21-bit)
		const offs_t dst_addr = bitswap<21>(i, 20,19,18,15,11,17,14,8,6,9,16,10,5,12,7,13,1,3,2,4,0);
		// Data descrambling
		dst[dst_addr] = bitswap<8>(src[i], 1,3,6,7,5,4,0,2);
	}
}

void roland_jv1080_state::init_jv1080()
{
	u8 *waverom = memregion("xp")->base();
	const u32 rom_size = 0x200000; // 2MB per ROM
	std::vector<u8> temp(rom_size);

	for (int i = 0; i < 4; i++)
	{
		std::copy_n(&waverom[i * rom_size], rom_size, temp.data());
		descramble_waverom(&waverom[i * rom_size], temp.data(), rom_size);
	}

	// Dump first bytes of each chip half to understand ROM layout
	FILE *f = fopen("rom_dump.txt", "w");
	if (f)
	{
		for (int chip = 0; chip < 4; chip++)
		{
			u32 base = chip * rom_size;
			fprintf(f, "=== Chip %d, lower half (0x%06X) ===\n", chip, base);
			for (int row = 0; row < 8; row++)
			{
				fprintf(f, "%06X:", base + row * 16);
				for (int col = 0; col < 16; col++)
					fprintf(f, " %02X", waverom[base + row * 16 + col]);
				fprintf(f, "\n");
			}
			u32 upper = base + 0x100000;
			fprintf(f, "=== Chip %d, upper half (0x%06X) ===\n", chip, upper);
			for (int row = 0; row < 8; row++)
			{
				fprintf(f, "%06X:", upper + row * 16);
				for (int col = 0; col < 16; col++)
					fprintf(f, " %02X", waverom[upper + row * 16 + col]);
				fprintf(f, "\n");
			}
		}
		fclose(f);
	}
}

// --- Input ports ---

static INPUT_PORTS_START(jv1080)
	PORT_START("BUTTONS1")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("PREVIEW")     PORT_CODE(KEYCODE_P)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("PALETTE")     PORT_CODE(KEYCODE_L)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("PARAMETER")   PORT_CODE(KEYCODE_R)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("1-8/9-16")    PORT_CODE(KEYCODE_TAB)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("1/9")         PORT_CODE(KEYCODE_1)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("2/10")        PORT_CODE(KEYCODE_2)
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("3/11")        PORT_CODE(KEYCODE_3)
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("4/12")        PORT_CODE(KEYCODE_4)

	PORT_START("BUTTONS2")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("5/13")        PORT_CODE(KEYCODE_5)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("6/14")        PORT_CODE(KEYCODE_6)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("7/15")        PORT_CODE(KEYCODE_7)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("8/16")        PORT_CODE(KEYCODE_8)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("PERFORM")     PORT_CODE(KEYCODE_F1)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("PATCH")       PORT_CODE(KEYCODE_F2)
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("RHYTHM")      PORT_CODE(KEYCODE_F3)
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("SYSTEM")      PORT_CODE(KEYCODE_F4)

	PORT_START("BUTTONS3")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("UTILITY")     PORT_CODE(KEYCODE_F5)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("USER/CARD")   PORT_CODE(KEYCODE_U)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("PRESET")      PORT_CODE(KEYCODE_I)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("EXP")         PORT_CODE(KEYCODE_O)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("A")           PORT_CODE(KEYCODE_A)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("B")           PORT_CODE(KEYCODE_B)
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("C")           PORT_CODE(KEYCODE_C)
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("D")           PORT_CODE(KEYCODE_D)

	PORT_START("BUTTONS4")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("EFFECTS")     PORT_CODE(KEYCODE_E)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("SHIFT")       PORT_CODE(KEYCODE_LSHIFT)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("EXIT")        PORT_CODE(KEYCODE_BACKSPACE)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("ENTER")       PORT_CODE(KEYCODE_ENTER)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("DEC")         PORT_CODE(KEYCODE_MINUS)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("INC")         PORT_CODE(KEYCODE_EQUALS)
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("Cursor Up")   PORT_CODE(KEYCODE_UP)
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("Cursor Down") PORT_CODE(KEYCODE_DOWN)

	PORT_START("BUTTONS5")
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("Cursor Left")  PORT_CODE(KEYCODE_LEFT)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("Cursor Right") PORT_CODE(KEYCODE_RIGHT)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("VALUE (push)")  PORT_CODE(KEYCODE_SPACE)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("Encoder CW")   PORT_CODE(KEYCODE_CLOSEBRACE)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_OTHER ) PORT_NAME("Encoder CCW")  PORT_CODE(KEYCODE_OPENBRACE)
	PORT_BIT( 0xe0, IP_ACTIVE_HIGH, IPT_UNUSED )
INPUT_PORTS_END

// --- Machine config ---

void roland_jv1080_state::jv1080(machine_config &config)
{
	SH7032(config, m_maincpu, 20_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &roland_jv1080_state::jv1080_mem_map);

	NVRAM(config, "nvram", nvram_device::DEFAULT_ALL_0);

	// LCD: Optrex DMC-2079 (40x2 chars, HD44780-compatible)
	screen_device &screen = SCREEN(config, "screen", SCREEN_TYPE_LCD);
	screen.set_refresh_hz(60);
	screen.set_size(40 * 6, 2 * 9);
	screen.set_visarea_full();
	screen.set_screen_update("lcdc", FUNC(hd44780_device::screen_update));
	screen.set_palette("palette");

	PALETTE(config, "palette", FUNC(roland_jv1080_state::lcd_palette), 2);

	HD44780(config, m_lcdc, 270'000);
	m_lcdc->set_lcd_size(2, 40);

	// Audio
	SPEAKER(config, "lspeaker").front_left();
	SPEAKER(config, "rspeaker").front_right();

	ROLAND_XP(config, m_xp, 24.576_MHz_XTAL);
	// TODO: m_xp->int_callback().set_inputline(m_maincpu, ...);
	m_xp->add_route(0, "lspeaker", 1.0);
	m_xp->add_route(1, "rspeaker", 1.0);

	config.set_default_layout(layout_jv1080);
}

// --- ROMs ---

ROM_START(jv1080)
	ROM_REGION(0x10000, "maincpu", 0)
	ROM_LOAD("roland_r00677323_6437034c12f.ic15", 0x0000, 0x10000, CRC(609c8b8d) SHA1(075b4d60d14afa3ff3c825b8da5c8687aca84560))

	ROM_REGION32_BE(0x100000, "progrom", 0)
	ROM_LOAD("roland_r00678167.ic20", 0x00000, 0x100000, CRC(0e26e075) SHA1(77f09efecf267def69b4e8f851cf124798347c03))

	ROM_REGION(0x800000, "xp", 0)  // wave ROM, directly loaded by the XP device via device_rom_interface
	ROM_LOAD("jv1080_waverom1.bin", 0x000000, 0x200000, CRC(f965d95f) SHA1(6b9a177f3ac6560e27befbcb9e907cf369f0f28a))
	ROM_LOAD("jv1080_waverom2.bin", 0x200000, 0x200000, CRC(78c3b1d8) SHA1(0dffa4ab4b9ea86c338fe146d902810f66c36cfd))
	ROM_LOAD("jv1080_waverom3.bin", 0x400000, 0x200000, CRC(edb5f1aa) SHA1(21eff48c6efe434ffce89c3bc61fe2b6d1584d00))
	ROM_LOAD("jv1080_waverom4.bin", 0x600000, 0x200000, CRC(4a877d28) SHA1(a1732be64e9c86c559df9c715703bcabc2a83440))
ROM_END

} // anonymous namespace


SYST(1994, jv1080, 0, 0, jv1080, jv1080, roland_jv1080_state, init_jv1080, "Roland", "JV-1080 Super JV 64 Voice Synthesizer Module", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
