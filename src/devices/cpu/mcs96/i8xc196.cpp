// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, giulioz

/***************************************************************************

    Intel 80C196KB CMOS MCS-96.

    Extends the shared 8x9x interpreter with two-clock state times, the KB
    ALU/memory timing tables, windowed SFRs, auxiliary PSW stack operations,
    block moves and long compares. The low interrupt bank uses 2000..200e;
    the additional bank uses 2030..203e. Timer overflow and buffered UART
    receive/transmit feed the additional bank as well as the legacy sources.

    Peripheral coverage is partial: locked HSO entries,
    timer-2 external/up-down clocking, PWM timing, watchdog/programming pulses
    and peripheral clock freezing during power-down are not implemented.
    BMOVI currently completes atomically like BMOV; interruptible transfers
    and external READY wait states still need bus-cycle modeling.

***************************************************************************/

#include "emu.h"
#include "i8xc196.h"
#include "i8xc196d.h"

namespace
{
enum class timing_mode
{
	Direct,
	Immediate,
	Indirect,
	IndirectAutoIncrement,
	IndexedShort,
	IndexedLong
};
// Timing shared by the ordinary ADD/SUB/ADDC/SUBC/CMP/AND/OR/XOR families,
// keyed on the addressing mode and the operand's data space.
constexpr unsigned common_alu(const u8 operand_count, const bool byte, const timing_mode mode, const bool ext)
{
	const bool three = operand_count == 3;
	if (mode == timing_mode::Direct)
		return three ? 5u : 4u;
	if (mode == timing_mode::Immediate)
		return three ? (byte ? 5u : 6u) : (byte ? 4u : 5u);
	const unsigned mode_offset =
		(mode == timing_mode::IndirectAutoIncrement || mode == timing_mode::IndexedLong) ? 1u : 0u;
	const unsigned internal = (three ? 7u : 6u) + mode_offset;
	if (!ext)
		return internal;
	// External operands add three states for three-operand forms, two for two.
	return internal + (three ? 3u : 2u);
}

constexpr bool auto_increment(timing_mode m)
{
	return m == timing_mode::IndirectAutoIncrement;
}
constexpr bool long_indexed(timing_mode m)
{
	return m == timing_mode::IndexedLong;
}
constexpr bool memory_mode(timing_mode m)
{
	return m >= timing_mode::Indirect;
}

// MUL / MULU (unsigned: !prefixed) with a byte source.
constexpr unsigned multiply_byte(bool prefixed, bool three, timing_mode m, bool ext)
{
	const bool increment = auto_increment(m) || long_indexed(m);
	const bool memory = memory_mode(m);
	if (!memory)
		return prefixed ? 12 : 10;
	unsigned t = 0;
	if (m == timing_mode::Indirect)
		t = prefixed ? 14 : 12;
	else if (m == timing_mode::IndirectAutoIncrement)
		t = prefixed ? (three ? 13 : 15) : (three ? 12 : 13);
	else if (m == timing_mode::IndexedShort)
		t = prefixed ? 15 : 12;
	else
		t = prefixed ? 16 : 14;
	if (ext)
	{
		if (prefixed)
			t += auto_increment(m) && three ? 2 : 3;
		else if (m == timing_mode::Indirect)
			t += 3;
		else if (m == timing_mode::IndirectAutoIncrement)
			t += three ? 4 : 2;
		else
			t += increment ? 3 : 4;
	}
	return t;
}

// MUL / MULU with a word source.
constexpr unsigned multiply_word(bool prefixed, bool, timing_mode m, bool ext)
{
	const bool memory = memory_mode(m);
	if (!memory)
		return (prefixed ? 16u : 14u) + (m == timing_mode::Immediate ? 1u : 0u);
	unsigned t = prefixed ? 18 : 16;
	if (m == timing_mode::IndirectAutoIncrement || m == timing_mode::IndexedShort)
		++t;
	else if (m == timing_mode::IndexedLong)
		t += 2;
	if (ext)
		t += !prefixed && m == timing_mode::IndirectAutoIncrement ? 2 : 3;
	return t;
}

// DIV / DIVU with a byte divisor.
constexpr unsigned divide_byte(bool prefixed, timing_mode m, bool ext)
{
	const bool increment = auto_increment(m) || long_indexed(m);
	const bool memory = memory_mode(m);
	unsigned t = prefixed ? 18 : 16;
	if (memory)
		t += 2 + (increment ? 1 : 0) + ((m == timing_mode::IndexedShort || m == timing_mode::IndexedLong) ? 1 : 0) +
			 (ext ? 3 : 0);
	return t;
}

// DIV / DIVU with a word divisor.
constexpr unsigned divide_word(bool prefixed, timing_mode m, bool ext)
{
	const bool increment = auto_increment(m) || long_indexed(m);
	const bool memory = memory_mode(m);
	unsigned t = prefixed ? 26 : 24;
	if (m == timing_mode::Immediate)
		t += 1;
	else if (memory)
		t += 2 + (increment ? 1 : 0) + ((m == timing_mode::IndexedShort || m == timing_mode::IndexedLong) ? 1 : 0) +
			 (ext ? 3 : 0);
	return t;
}

// LD / LDB / LDBZE / LDBSE.
constexpr unsigned load(bool byte_load, timing_mode m, bool ext)
{
	switch (m)
	{
	case timing_mode::Direct:
		return 4;
	case timing_mode::Immediate:
		return byte_load ? 4 : 5;
	case timing_mode::Indirect:
		return ext ? 8 : 5;
	case timing_mode::IndirectAutoIncrement:
		return ext ? 8 : 6;
	case timing_mode::IndexedShort:
		return ext ? 9 : 6;
	case timing_mode::IndexedLong:
		return ext ? 10 : 7;
	}
	return 0;
}

// ST / STB to a memory operand.
constexpr unsigned store(timing_mode m, bool ext)
{
	switch (m)
	{
	case timing_mode::Indirect:
		return ext ? 8 : 5;
	case timing_mode::IndirectAutoIncrement:
		return ext ? 9 : 6;
	case timing_mode::IndexedShort:
		return ext ? 9 : 6;
	default:
		return ext ? 10 : 7;
	}
}

} // anonymous namespace

DEFINE_DEVICE_TYPE(I80C196KB, i80c196kb_device, "i80c196kb", "Intel 80C196KB")

i80c196kb_device::i80c196kb_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: i8xc196_device(mconfig, I80C196KB, tag, owner, clock)
{
}

i8xc196_device::i8xc196_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock)
	: i8x9x_device(mconfig, type, tag, owner, clock, 16, address_map_constructor(FUNC(i8xc196_device::internal_regs), this))
{
}

void i8xc196_device::device_start()
{
	i8x9x_device::device_start();
	cycles_scaling = 2;
	save_item(NAME(m_window));
	save_item(NAME(m_nmi_state));
	save_item(NAME(m_extint1_state));
	save_item(NAME(m_mask_high));
	save_item(NAME(m_ioc2));
	save_item(NAME(m_ios2));
	save_item(NAME(m_program_pulse));
}

void i8xc196_device::device_reset()
{
	i8x9x_device::device_reset();
	m_window = m_mask_high = m_ioc2 = m_ios2 = m_program_pulse = 0;
	m_nmi_state = m_extint1_state = false;
	for (unsigned address = 0x18; address < 0x100; address += 2)
		reg_w16(address, 0xffff);
	ioc1 = 0x21;
	hsi_mode = 0xff;
	sp_con = 0x0b;
	sp_stat = 8;
	ad_result = 0x7ff0;
}

std::unique_ptr<util::disasm_interface> i8xc196_device::create_disassembler()
{
	return std::make_unique<i8xc196_disassembler>();
}

void i8xc196_device::internal_regs(address_map &map)
{
	map(0x00, 0x01).lr16([]() -> u16 { return 0; }, "r0").nopw();
	map(0x02, 0x17).rw(FUNC(i8xc196_device::sfr_r), FUNC(i8xc196_device::sfr_w));
	map(0x18, 0xff).ram().share("register_file");
}

void i8xc196_device::auxiliary_psw_w(u16 value)
{
	m_mask_high = value & 0x7f;
	const u8 window = (value >> 8) & 15;
	if (!window || window == 14 || window == 15)
		m_window = value >> 8;
	check_irq();
}

u8 i8xc196_device::sfr_r(offs_t offset)
{
	offset += 2;
	if (offset == 8)
		return int_mask_r();
	if (offset == 0x14)
		return m_window;
	if ((m_window & 15) == 14)
		return offset == 4 ? m_program_pulse : 0xff;
	if ((m_window & 15) == 15)
	{
		switch (offset)
		{
		case 2:
			return ad_command;
		case 3:
			return hsi_mode;
		case 4:
			return hso_time;
		case 5:
			return hso_time >> 8;
		case 6:
			return hso_command;
		case 7:
			return serial_send_buf;
		case 0x0b:
			return m_ioc2 | 0x80;
		case 0x11:
			return sp_con;
		case 0x15:
			return ioc0 | 2;
		case 0x16:
			return ioc1;
		case 0x17:
			return pwm_control;
		default:
			return 0xff;
		}
	}
	switch (offset)
	{
	case 2:
	case 3:
		return ad_result_r(offset - 2);
	case 4:
		return hsi_time_byte_r(0);
	case 5:
		return hsi_time_byte_r(1);
	case 6:
		return hsi_status_r();
	case 7:
		return sbuf_r();
	case 9:
		return int_pending_r();
	case 0x0a:
		return timer1_r();
	case 0x0b:
		return timer1_r() >> 8;
	case 0x0c:
		return timer2_r();
	case 0x0d:
		return timer2_r() >> 8;
	case 0x0e:
		return port0_r();
	case 0x0f:
		return port1_r();
	case 0x10:
		return port2_r();
	case 0x11:
		return sp_stat_r();
	case 0x12:
		return pending_irq >> 8;
	case 0x13:
		return m_mask_high;
	case 0x15:
		return ios0_r();
	case 0x16:
		return ios1_r();
	case 0x17:
	{
		const u8 result = m_ios2;
		if (!machine().side_effects_disabled())
			m_ios2 = 0;
		return result;
	}
	default:
		return 0xff;
	}
}

void i8xc196_device::sfr_w(offs_t offset, u8 data)
{
	offset += 2;
	if (offset == 8)
	{
		int_mask_w(data);
		return;
	}
	if (offset == 0x14)
	{
		const u8 window = data & 15;
		if (!window || window == 14 || window == 15)
			m_window = data;
		return;
	}
	if ((m_window & 15) == 14)
	{
		if (offset == 4)
			m_program_pulse = data;
		return;
	}
	if ((m_window & 15) == 15)
	{
		switch (offset)
		{
		case 2:
			ad_result = (ad_result & 0xff00) | data;
			break;
		case 3:
			ad_result = (ad_result & 0x00ff) | (u16(data) << 8);
			break;
		case 4:
			hsi_holding_time_w(data, 0x00ff);
			break;
		case 5:
			hsi_holding_time_w(u16(data) << 8, 0xff00);
			break;
		case 6:
			hsi_holding_status_w(data);
			break;
		case 0x0a:
		case 0x0b:
		{
			u16 timer = timer_value(1, total_cycles());
			timer = offset == 0x0a ? (timer & 0xff00) | data : (timer & 0x00ff) | (u16(data) << 8);
			base_timer1 = total_cycles() - u64(timer) * timer_divisor();
			timer1_overflow = timer_time_until(1, total_cycles(), 0);
			internal_update(total_cycles());
			break;
		}
		case 0x11:
			sp_stat |= data & 0xfc;
			break;
		case 0x15:
			ios0_w(data);
			break;
		case 0x16:
			ios1 |= data & 0x3f;
			break;
		case 0x17:
			m_ios2 |= data;
			break;
		default:
			break;
		}
		return;
	}
	switch (offset)
	{
	case 2:
		ad_command_w(data);
		break;
	case 3:
		hsi_mode_w(data);
		break;
	case 4:
		hso_time = (hso_time & 0xff00) | data;
		break;
	case 5:
		hso_time_w((hso_time & 0x00ff) | (u16(data) << 8));
		break;
	case 6:
		hso_command_w(data);
		break;
	case 7:
		sbuf_w(data);
		break;
	case 9:
		int_pending_w(data);
		break;
	case 0x0a:
		watchdog_w(data);
		break;
	case 0x0b:
		if (data & 0x80)
		{
			hso_active = 0;
			ios0 &= 0x3f;
		}
		m_ioc2 = data & 0x7f;
		break;
	case 0x0c:
	case 0x0d:
	{
		u16 timer = timer_value(2, total_cycles());
		timer = offset == 0x0c ? (timer & 0xff00) | data : (timer & 0x00ff) | (u16(data) << 8);
		base_timer2 = total_cycles() - u64(timer) * timer_divisor();
		timer2_overflow = timer_time_until(2, total_cycles(), timer < 0x8000 ? 0x8000 : 0);
		internal_update(total_cycles());
		break;
	}
	case 0x0e:
		baud_rate_w(data);
		break;
	case 0x0f:
		port1_w(data);
		break;
	case 0x10:
		port2_w(data);
		break;
	case 0x11:
		sp_con_w(data);
		break;
	case 0x12:
		pending_irq = (pending_irq & 0x00ff) | (u16(data) << 8);
		check_irq();
		break;
	case 0x13:
		m_mask_high = data & 0x7f;
		check_irq();
		break;
	case 0x15:
		ioc0_w(data);
		break;
	case 0x16:
		ioc1_w(data);
		break;
	case 0x17:
		pwm_control_w(data);
		break;
	default:
		break;
	}
}

void i8xc196_device::execute_set_input(int linenum, int state)
{
	if (linenum == INPUT_LINE_NMI)
	{
		if (state && !m_nmi_state)
		{
			pending_irq |= 0x8000;
			check_irq();
		}
		m_nmi_state = state;
	}
	else if (linenum == EXTINT1_LINE)
	{
		if (state && !m_extint1_state)
		{
			pending_irq |= 0x2000;
			if (!BIT(ioc1, 1))
				pending_irq |= IRQ_EXTINT;
			check_irq();
		}
		m_extint1_state = state;
		if (state && suspended(SUSPEND_REASON_HALT))
			resume(SUSPEND_REASON_HALT);
	}
	else if (linenum == EXTINT_LINE)
	{
		if (!extint && state && BIT(ioc1, 1))
		{
			pending_irq |= IRQ_EXTINT;
			check_irq();
		}
		extint = state;
		if (state && suspended(SUSPEND_REASON_HALT))
			resume(SUSPEND_REASON_HALT);
	}
	else
		i8x9x_device::execute_set_input(linenum, state);
}

// State times from the 80C196KB instruction summary. The NMOS handlers
// retain their original timing; only this device applies the CMOS tables.
int i8xc196_device::instruction_cycles(int cycles)
{
	const u8 op = inst_state;
	if ((op >= 1 && op <= 7) || (op >= 0x11 && op <= 0x17))
		return 3;
	if (op == 8 || op == 9 || op == 0x0a || op == 0x0c || op == 0x0d || op == 0x0e || op == 0x18 || op == 0x19 ||
		op == 0x1a)
		return cycles - 1;
	if (op == 0x0f)
		return cycles - 3;
	if (op == 0xc8 || op == 0xf2)
		return TMP < 0x100 ? 6 : 8;
	if (op == 0xc9)
		return TMP < 0x100 ? 7 : 9;
	if (op == 0xcc)
		return u16(reg_r16(0x18) - 2) < 0x100 ? 8 : 11;
	if (op == 0xf3)
		return TMP < 0x100 ? 7 : 10;
	if (op == 0xe1)
		return cycles - 1;
	if (op == 0xf7)
		return TMP < 0x100 ? 16 : 18;
	if (op == 0 || op >= 0xf8)
		return op == 0 ? 3 : op == 0xff ? 15 : 2;
	if ((op >= 0x20 && op <= 0x27) || op == 0xe3 || op == 0xe7)
		return 7;
	if ((op >= 0x28 && op <= 0x2f) || op == 0xef)
		return TMP < 0x100 ? 11 : 13;
	if (op == 0xf0)
		return u16(reg_r16(0x18) - 2) < 0x100 ? 11 : 14;
	if (op >= 0x40 && op <= 0xc7)
	{
		const unsigned mode_bits = op & 3;
		const timing_mode mode = mode_bits < 2 ? timing_mode(mode_bits)
								 : mode_bits == 2
									 ? ((OPI & 1) ? timing_mode::IndirectAutoIncrement : timing_mode::Indirect)
									 : (m_long_index ? timing_mode::IndexedLong : timing_mode::IndexedShort);
		const bool prefixed = inst_state & 0x100;
		const bool byte = op & 0x10;
		const bool external = mode_bits >= 2 && OP1 >= 0x100;
		if (op < 0x80 && (op & 0x0c) == 0x0c)
			return byte ? multiply_byte(prefixed, op < 0x60, mode, external)
						: multiply_word(prefixed, op < 0x60, mode, external);
		if (op >= 0x80 && op < 0xa0 && (op & 0x0c) == 0x0c)
			return byte ? divide_byte(prefixed, mode, external) : divide_word(prefixed, mode, external);
		if ((op & 0xec) == 0xa0 || (op & 0xec) == 0xac)
			return load(byte || ((op & 0xc) == 0xc), mode, external);
		if (op >= 0xc0)
			return mode_bits < 2 ? cycles : store(mode, external);
		return common_alu(op < 0x60 ? 3 : 2, byte, mode, external);
	}
	return cycles;
}

void i8xc196_device::do_exec_partial() {}

#include "cpu/mcs96/i8xc196.hxx"
