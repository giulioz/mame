// license:BSD-3-Clause
// copyright-holders:AJR
/***************************************************************************

    Fujitsu FR series

    The execution core implements the original 16-bit FR20/FR30 instruction
    set.  Later FR family extensions are intentionally left to derived cores.

***************************************************************************/

#include "emu.h"
#include "fr.h"
#include "frdasm.h"

// device type definition
DEFINE_DEVICE_TYPE(MB91F155A, mb91f155a_device, "mb91f155a", "Fujitsu MB91F155A")
DEFINE_DEVICE_TYPE(MB91103, mb91103_device, "mb91103", "Fujitsu MB91103")

fr_cpu_device::fr_cpu_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock, int addrbits, address_map_constructor map)
	: cpu_device(mconfig, type, tag, owner, clock)
	, m_space_config("program", ENDIANNESS_BIG, 32, addrbits, 0, map)
	, m_regs{0}
	, m_pc(0)
	, m_ppc(0)
	, m_ps(0)
	, m_tbr(0)
	, m_rp(0)
	, m_md(0)
	, m_resource{0}
	, m_delay_target(0)
	, m_delay_pending(false)
	, m_reset_pending(true)
	, m_exception_taken(false)
	, m_trace_inhibit(false)
	, m_eit_stack{0}
	, m_eit_depth(0)
	, m_irq_pending(0)
	, m_irq_level{0}
	, m_nmi_pending(false)
	, m_icount(0)
{
}

mb91103_device::mb91103_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: fr_cpu_device(mconfig, MB91103, tag, owner, clock, 24, address_map_constructor(FUNC(mb91103_device::internal_map), this))
	, m_port_read(*this, 0xff)
	, m_port_write(*this)
	, m_adc_read(*this, 0x200)
	, m_uart_write(*this)
	, m_adc_timer(nullptr)
	, m_reload_timer{ nullptr, nullptr }
	, m_free_timer(nullptr)
	, m_udc_timer{ nullptr, nullptr }
	, m_utimer{ nullptr, nullptr }
	, m_uart_timer{ nullptr, nullptr }
	, m_adc_channel(0)
	, m_adc_end(0)
	, m_adc_mode(0)
	, m_adc_active(false)
	, m_free_count(0)
	, m_free_time(attotime::zero)
	, m_udc_count{ 0, 0 }
	, m_udc_input{ 0, 0 }
	, m_icu_input(0)
	, m_utimer_phase{ false, false }
	, m_io{0}
{
}

mb91f155a_device::mb91f155a_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: fr_cpu_device(mconfig, MB91F155A, tag, owner, clock, 24, address_map_constructor(FUNC(mb91f155a_device::internal_map), this))
{
}

void mb91f155a_device::internal_map(address_map &map)
{
	// TODO: I/O registers
	map(0x001000, 0x008fff).ram();
}

void mb91103_device::internal_map(address_map &map)
{
	map(0x000000, 0x0007ff).rw(FUNC(mb91103_device::io_r), FUNC(mb91103_device::io_w));
}

std::unique_ptr<util::disasm_interface> fr_cpu_device::create_disassembler()
{
	return std::make_unique<fr_disassembler>();
}

device_memory_interface::space_config_vector fr_cpu_device::memory_space_config() const
{
	return space_config_vector {
		std::make_pair(AS_PROGRAM, &m_space_config),
	};
}

void fr_cpu_device::device_start()
{
	space(AS_PROGRAM).cache(m_cache);
	space(AS_PROGRAM).specific(m_space);

	set_icountptr(m_icount);

	state_add(FR_PC, "PC", m_pc).mask(0xfffffffe);
	state_add(STATE_GENPC, "GENPC", m_pc).mask(0xfffffffe).noshow();
	state_add(STATE_GENPCBASE, "CURPC", m_pc).mask(0xfffffffe).noshow();
	state_add(FR_PS, "PS", m_ps).mask(0x001f073f).formatstr("%08X");
	state_add(STATE_GENFLAGS, "CURFLAGS", m_ps).mask(0x001f073f).noshow().formatstr("%19s");
	state_add<u8>(FR_CCR, "CCR",
		[this]() { return u8(m_ps & 0x0000003f); },
		[this](u8 value) { m_ps = (m_ps & 0x001f0700) | value; }).mask(0x3f).noshow();
	state_add<u8>(FR_ILM, "ILM",
		[this]() { return u8((m_ps & 0x001f0000) >> 16); },
		[this](u8 value) { m_ps = (m_ps & 0x0000073f) | u32(value) << 16; }).mask(0x1f).noshow();
	state_add(FR_TBR, "TBR", m_tbr);
	state_add(FR_RP, "RP", m_rp);
	state_add(FR_SSP, "SSP", m_regs[15]);
	state_add(FR_USP, "USP", m_regs[16]);
	state_add(FR_MD, "MD", m_md);
	state_add<u32>(FR_MDH, "MDH",
		[this]() { return u32(m_md >> 32); },
		[this](u32 value) { m_md = (m_md & 0x00000000ffffffffULL) | u64(value) <<  32; }).noshow();
	state_add<u32>(FR_MDL, "MDL",
		[this]() { return u32(m_md & 0x00000000ffffffffULL); },
		[this](u32 value) { m_md = (m_md & 0xffffffff00000000ULL) | value; }).noshow();
	for (int i = 0; i < 15; i++)
		state_add(FR_R0 + i, string_format("R%d", i).c_str(), m_regs[i]);
	state_add<u32>(FR_R15, "R15",
		[this]() { return m_regs[BIT(m_ps, 5) ? 16 : 15]; },
		[this](u32 value) { m_regs[BIT(m_ps, 5) ? 16 : 15] = value; });

	save_item(NAME(m_regs));
	save_item(NAME(m_pc));
	save_item(NAME(m_ppc));
	save_item(NAME(m_ps));
	save_item(NAME(m_tbr));
	save_item(NAME(m_rp));
	save_item(NAME(m_md));
	save_item(NAME(m_resource));
	save_item(NAME(m_delay_target));
	save_item(NAME(m_delay_pending));
	save_item(NAME(m_reset_pending));
	save_item(NAME(m_exception_taken));
	save_item(NAME(m_trace_inhibit));
	save_item(NAME(m_eit_stack));
	save_item(NAME(m_eit_depth));
	save_item(NAME(m_irq_pending));
	save_item(NAME(m_irq_level));
	save_item(NAME(m_nmi_pending));
}

void fr_cpu_device::device_reset()
{
	// Only TBR, SSP, ILM and certain CCR and SCR bits are reset here; PC will be reloaded subsequently
	m_ps = (m_ps & 0x0000060f) | 0x000f0000;
	m_tbr = 0x000ffc00;
	m_regs[15] = 0x00000000;
	m_delay_pending = false;
	m_reset_pending = true;
	m_exception_taken = false;
	m_trace_inhibit = false;
	m_eit_depth = 0;
	m_nmi_pending = false;
	m_irq_pending = 0;
}

void fr_cpu_device::execute_run()
{
	if (m_reset_pending)
	{
		m_pc = read_dword(0x000ffffc);
		m_reset_pending = false;
	}

	do
	{
		if (!m_delay_pending && check_interrupts())
			continue;

		const bool in_delay_slot = m_delay_pending;
		const u32 delayed_target = m_delay_target;
		m_delay_pending = false;
		m_exception_taken = false;
		m_trace_inhibit = false;
		m_ppc = m_pc;

		debugger_instruction_hook(m_pc);
		const u16 opcode = m_cache.read_word(m_pc);
		m_pc += 2;
		m_icount--;
		execute_one(opcode, in_delay_slot);

		if (in_delay_slot && !m_exception_taken)
		{
			m_pc = delayed_target;
			if (BIT(m_ps, 8) && !m_trace_inhibit && !trace_suppressed())
				take_exception(12, m_pc, false, 4, true);
		}
		else if (!m_exception_taken && BIT(m_ps, 8) && !m_delay_pending && !m_trace_inhibit && !trace_suppressed())
			take_exception(12, m_pc, false, 4, true);
	} while (m_icount > 0);
}

void fr_cpu_device::execute_set_input(int inputnum, int state)
{
	if (inputnum == INPUT_LINE_NMI)
	{
		if (state != CLEAR_LINE)
			m_nmi_pending = true;
	}
	else if (unsigned(inputnum) < 48)
	{
		set_irq_pending(inputnum, state != CLEAR_LINE);
	}
}

u32 fr_cpu_device::get_reg(unsigned reg) const noexcept
{
	return reg == 15 ? m_regs[BIT(m_ps, 5) ? 16 : 15] : m_regs[reg & 15];
}

void fr_cpu_device::set_reg(unsigned reg, u32 value) noexcept
{
	if (reg == 15)
		m_regs[BIT(m_ps, 5) ? 16 : 15] = value;
	else
		m_regs[reg & 15] = value;
}

u32 fr_cpu_device::get_dr(unsigned reg) const noexcept
{
	switch (reg)
	{
	case 0: return m_tbr;
	case 1: return m_rp;
	case 2: return m_regs[15];
	case 3: return m_regs[16];
	case 4: return u32(m_md >> 32);
	case 5: return u32(m_md);
	default: return 0;
	}
}

void fr_cpu_device::set_dr(unsigned reg, u32 value) noexcept
{
	switch (reg)
	{
	case 0: m_tbr = value; break;
	case 1: m_rp = value; break;
	case 2: m_regs[15] = value; break;
	case 3: m_regs[16] = value; break;
	case 4: m_md = (m_md & 0x00000000ffffffffU) | u64(value) << 32; break;
	case 5: m_md = (m_md & 0xffffffff00000000U) | value; break;
	}
}

void fr_cpu_device::set_ps(u32 value) noexcept
{
	u32 result = value & PS_MASK;
	if (BIT(m_ps, 20) && !BIT(result, 20))
		result += 0x00100000;
	m_ps = result;
}

u8 fr_cpu_device::read_byte(u32 address) { return m_space.read_byte(address); }
u16 fr_cpu_device::read_word(u32 address) { return m_space.read_word(address); }
u32 fr_cpu_device::read_dword(u32 address) { return m_space.read_dword(address); }
void fr_cpu_device::write_byte(u32 address, u8 data) { m_space.write_byte(address, data); }
void fr_cpu_device::write_word(u32 address, u16 data) { m_space.write_word(address, data); }
void fr_cpu_device::write_dword(u32 address, u32 data) { m_space.write_dword(address, data); }

void fr_cpu_device::set_nz(u32 value)
{
	m_ps = (m_ps & ~(PS_N | PS_Z)) | (BIT(value, 31) ? PS_N : 0) | (!value ? PS_Z : 0);
}

void fr_cpu_device::set_nz(u32 value, unsigned bits)
{
	const u32 mask = bits == 32 ? 0xffffffffU : (u32(1) << bits) - 1;
	value &= mask;
	m_ps = (m_ps & ~(PS_N | PS_Z)) | (BIT(value, bits - 1) ? PS_N : 0) | (!value ? PS_Z : 0);
}

u32 fr_cpu_device::add_flags(u32 left, u32 right, bool carry)
{
	const u64 wide = u64(left) + right + (carry ? 1 : 0);
	const u32 result = u32(wide);
	const bool overflow = BIT(~(left ^ right) & (left ^ result), 31);
	m_ps = (m_ps & ~(PS_C | PS_V)) | (BIT(wide, 32) ? PS_C : 0) | (overflow ? PS_V : 0);
	set_nz(result);
	return result;
}

u32 fr_cpu_device::sub_flags(u32 left, u32 right, bool borrow)
{
	const u64 subtrahend = u64(right) + (borrow ? 1 : 0);
	const u32 result = left - u32(subtrahend);
	const bool overflow = BIT((left ^ right) & (left ^ result), 31);
	m_ps = (m_ps & ~(PS_C | PS_V)) | (u64(left) < subtrahend ? PS_C : 0) | (overflow ? PS_V : 0);
	set_nz(result);
	return result;
}

bool fr_cpu_device::condition(unsigned condition) const noexcept
{
	const bool c = bool(m_ps & PS_C), v = bool(m_ps & PS_V), z = bool(m_ps & PS_Z), n = bool(m_ps & PS_N);
	switch (condition & 15)
	{
	case 0: return true;
	case 1: return false;
	case 2: return z;
	case 3: return !z;
	case 4: return c;
	case 5: return !c;
	case 6: return n;
	case 7: return !n;
	case 8: return v;
	case 9: return !v;
	case 10: return v != n;
	case 11: return v == n;
	case 12: return (v != n) || z;
	case 13: return (v == n) && !z;
	case 14: return c || z;
	case 15: return !c && !z;
	}
	return false;
}

void fr_cpu_device::schedule_delayed_branch(u32 target, bool taken) noexcept
{
	m_delay_target = taken ? target : m_pc + 2;
	m_delay_pending = true;
}

void fr_cpu_device::take_exception(unsigned vector, u32 return_pc, bool clear_i, int level, bool trace_handler)
{
	const u32 old_ps = m_ps;
	m_regs[15] -= 4;
	write_dword(m_regs[15], old_ps);
	m_regs[15] -= 4;
	write_dword(m_regs[15], return_pc);
	if (clear_i)
		m_ps &= ~PS_I;
	if (level >= 0)
		m_ps = (m_ps & ~PS_ILM) | (u32(level) & 0x1f) << 16;
	m_ps &= ~PS_S;
	m_pc = read_dword(m_tbr + 0x3fc - 4 * vector);
	if (m_eit_depth < std::size(m_eit_stack))
		m_eit_stack[m_eit_depth++] = trace_handler ? 1 : 0;
	m_delay_pending = false;
	m_exception_taken = true;
	m_icount -= 5;
}

void fr_cpu_device::set_irq_level(unsigned source, u8 level)
{
	if (source < 48)
		m_irq_level[source] = level & 0x1f;
}

void fr_cpu_device::set_irq_pending(unsigned source, bool pending)
{
	if (source < 48)
	{
		if (pending)
			m_irq_pending |= u64(1) << source;
		else
			m_irq_pending &= ~(u64(1) << source);
	}
}

bool fr_cpu_device::check_interrupts()
{
	if (m_ps & PS_T)
		return false;

	const unsigned mask = BIT(m_ps, 16, 5);
	unsigned best = 48;
	unsigned best_level = 32;
	if ((m_ps & PS_I) && m_irq_pending)
	{
		for (unsigned source = 0; source < 48; source++)
			if (BIT(m_irq_pending, source) && m_irq_level[source] < mask && m_irq_level[source] < best_level)
			{
				best = source;
				best_level = m_irq_level[source];
			}
	}

	// NMI has fixed level 15 and interrupt number 15.  It wins ties with
	// user interrupts, whose interrupt numbers begin at 16.
	if (m_nmi_pending && 15 < mask && best_level >= 15)
	{
		m_nmi_pending = false;
		take_exception(15, m_pc, false, 15);
		return true;
	}
	if (best != 48)
	{
		take_exception(16 + best, m_pc, false, best_level);
		return true;
	}
	return false;
}

bool fr_cpu_device::trace_suppressed() const noexcept
{
	for (unsigned depth = 0; depth < m_eit_depth; depth++)
		if (m_eit_stack[depth])
			return true;
	return false;
}

void fr_cpu_device::undefined_instruction(bool in_delay_slot)
{
	if (!in_delay_slot)
		take_exception(14, m_ppc, false);
}

void fr_cpu_device::execute_one(u16 opcode, bool in_delay_slot)
{
	const unsigned op = opcode >> 8;
	const unsigned rj = BIT(opcode, 4, 4);
	const unsigned ri = BIT(opcode, 0, 4);
	const u8 imm8 = u8(opcode);
	auto const logical_memory = [this, rj, ri](unsigned bits, unsigned operation)
	{
		const u32 address = get_reg(ri);
		u32 value = bits == 32 ? read_dword(address) : bits == 16 ? read_word(address) : read_byte(address);
		if (operation == 0)
			value &= get_reg(rj);
		else if (operation == 1)
			value |= get_reg(rj);
		else
			value ^= get_reg(rj);
		if (bits == 32)
			write_dword(address, value);
		else if (bits == 16)
			write_word(address, value);
		else
			write_byte(address, value);
		set_nz(value, bits);
		m_icount -= 2;
	};
	auto const shift = [this, ri](u32 count, unsigned operation)
	{
		count &= 31;
		const u32 value = get_reg(ri);
		u32 result = value;
		m_ps &= ~PS_C;
		if (count)
		{
			if (operation == 0)
			{
				if (BIT(value, 32 - count)) m_ps |= PS_C;
				result = value << count;
			}
			else
			{
				if (BIT(value, count - 1)) m_ps |= PS_C;
				result = operation == 1 ? value >> count : u32(s32(value) >> count);
			}
		}
		set_reg(ri, result);
		set_nz(result);
	};

	switch (op)
	{
	case 0x00: set_reg(ri, read_dword(get_reg(13) + get_reg(rj))); break;
	case 0x01: set_reg(ri, read_word(get_reg(13) + get_reg(rj))); break;
	case 0x02: set_reg(ri, read_byte(get_reg(13) + get_reg(rj))); break;
	case 0x03: set_reg(ri, read_dword(get_reg(15) + (rj << 2))); break;
	case 0x04: set_reg(ri, read_dword(get_reg(rj))); break;
	case 0x05: set_reg(ri, read_word(get_reg(rj))); break;
	case 0x06: set_reg(ri, read_byte(get_reg(rj))); break;

	case 0x07:
		switch (rj)
		{
		case 0:
		{
			const u32 address = get_reg(15);
			const u32 value = read_dword(address);
			if (ri != 15)
				set_reg(15, address + 4);
			set_reg(ri, value);
			break;
		}
		case 1: set_ps(get_reg(ri)); break;
		case 8:
			if (ri < 6)
			{
				const u32 value = read_dword(get_reg(15));
				set_reg(15, get_reg(15) + 4);
				set_dr(ri, value);
			}
			else undefined_instruction(in_delay_slot);
			break;
		case 9:
			if (ri == 0)
			{
				const u32 value = read_dword(get_reg(15));
				set_reg(15, get_reg(15) + 4);
				set_ps(value);
				m_icount -= 2;
			}
			else undefined_instruction(in_delay_slot);
			break;
		default: undefined_instruction(in_delay_slot); break;
		}
		break;

	case 0x08: set_reg(13, read_dword(u32(imm8) << 2)); break;
	case 0x09: set_reg(13, s32(s16(read_word(u32(imm8) << 1)))); break;
	case 0x0a: set_reg(13, s32(s8(read_byte(imm8)))); break;
	case 0x0b:
		set_reg(15, get_reg(15) - 4);
		write_dword(get_reg(15), read_dword(u32(imm8) << 2));
		m_icount--;
		break;
	case 0x0c: write_dword(get_reg(13), read_dword(u32(imm8) << 2)); set_reg(13, get_reg(13) + 4); m_icount--; break;
	case 0x0d: write_word(get_reg(13), read_word(u32(imm8) << 1)); set_reg(13, get_reg(13) + 2); m_icount--; break;
	case 0x0e: write_byte(get_reg(13), read_byte(imm8)); set_reg(13, get_reg(13) + 1); m_icount--; break;
	case 0x0f:
	{
		const u32 sp = get_reg(15) - 4;
		write_dword(sp, get_reg(14));
		set_reg(14, sp);
		set_reg(15, sp - (u32(imm8) << 2));
		m_icount--;
		break;
	}

	case 0x10: write_dword(get_reg(13) + get_reg(rj), get_reg(ri)); break;
	case 0x11: write_word(get_reg(13) + get_reg(rj), get_reg(ri)); break;
	case 0x12: write_byte(get_reg(13) + get_reg(rj), get_reg(ri)); break;
	case 0x13: write_dword(get_reg(15) + (rj << 2), get_reg(ri)); break;
	case 0x14: write_dword(get_reg(rj), get_reg(ri)); break;
	case 0x15: write_word(get_reg(rj), get_reg(ri)); break;
	case 0x16: write_byte(get_reg(rj), get_reg(ri)); break;

	case 0x17:
		switch (rj)
		{
		case 0:
		{
			const u32 value = get_reg(ri);
			set_reg(15, get_reg(15) - 4);
			write_dword(get_reg(15), value);
			break;
		}
		case 1: set_reg(ri, m_ps); break;
		case 8:
			if (ri < 6)
			{
				const u32 value = get_dr(ri);
				set_reg(15, get_reg(15) - 4);
				write_dword(get_reg(15), value);
			}
			else undefined_instruction(in_delay_slot);
			break;
		case 9:
			if (ri == 0)
			{
				const u32 value = m_ps;
				set_reg(15, get_reg(15) - 4);
				write_dword(get_reg(15), value);
			}
			else undefined_instruction(in_delay_slot);
			break;
		default: undefined_instruction(in_delay_slot); break;
		}
		break;

	case 0x18: write_dword(u32(imm8) << 2, get_reg(13)); break;
	case 0x19: write_word(u32(imm8) << 1, get_reg(13)); break;
	case 0x1a: write_byte(imm8, get_reg(13)); break;
	case 0x1b: write_dword(u32(imm8) << 2, read_dword(get_reg(15))); set_reg(15, get_reg(15) + 4); m_icount--; break;
	case 0x1c: write_dword(u32(imm8) << 2, read_dword(get_reg(13))); set_reg(13, get_reg(13) + 4); m_icount--; break;
	case 0x1d: write_word(u32(imm8) << 1, read_word(get_reg(13))); set_reg(13, get_reg(13) + 2); m_icount--; break;
	case 0x1e: write_byte(imm8, read_byte(get_reg(13))); set_reg(13, get_reg(13) + 1); m_icount--; break;
	case 0x1f: take_exception(imm8, m_pc, true); break;

	case 0x80: write_byte(get_reg(ri), read_byte(get_reg(ri)) & (0xf0 | rj)); m_icount -= 2; break;
	case 0x81: write_byte(get_reg(ri), read_byte(get_reg(ri)) & (0x0f | (rj << 4))); m_icount -= 2; break;
	case 0x82: set_reg(ri, get_reg(ri) & get_reg(rj)); set_nz(get_reg(ri)); break;
	case 0x83: set_ps((m_ps & ~0x3fU) | ((m_ps & 0x3fU) & imm8)); break;
	case 0x84: logical_memory(32, 0); break;
	case 0x85: logical_memory(16, 0); break;
	case 0x86: logical_memory(8, 0); break;
	case 0x87:
	{
		u8 level = imm8 & 0x1f;
		if (BIT(m_ps, 20) && level < 16)
			level += 16;
		m_ps = (m_ps & ~PS_ILM) | u32(level) << 16;
		break;
	}
	case 0x88: m_ps = (m_ps & ~(PS_N | PS_Z)) | (!(read_byte(get_reg(ri)) & rj) ? PS_Z : 0); m_icount -= 2; break;
	case 0x89:
	{
		const u8 value = read_byte(get_reg(ri)) & (rj << 4);
		m_ps = (m_ps & ~(PS_N | PS_Z)) | (BIT(value, 7) ? PS_N : 0) | (!value ? PS_Z : 0);
		m_icount -= 2;
		break;
	}
	case 0x8a:
	{
		const u32 value = get_reg(ri);
		set_reg(ri, read_byte(get_reg(rj)));
		write_byte(get_reg(rj), value);
		m_icount--;
		break;
	}
	case 0x8b: set_reg(ri, get_reg(rj)); break;
	case 0x8c:
		for (unsigned bit = 0; bit < 8; bit++)
			if (BIT(imm8, bit))
			{
				set_reg(bit, read_dword(get_reg(15)));
				set_reg(15, get_reg(15) + 4);
				m_icount--;
			}
		break;
	case 0x8d:
		for (unsigned bit = 0; bit < 8; bit++)
			if (BIT(imm8, bit))
			{
				const u32 value = read_dword(get_reg(15));
				if (bit != 7)
					set_reg(15, get_reg(15) + 4);
				set_reg(8 + bit, value);
				m_icount--;
			}
		break;
	case 0x8e:
		for (unsigned bit = 0; bit < 8; bit++)
			if (BIT(imm8, bit))
			{
				set_reg(15, get_reg(15) - 4);
				write_dword(get_reg(15), get_reg(7 - bit));
				m_icount--;
			}
		break;
	case 0x8f:
		for (unsigned bit = 0; bit < 8; bit++)
			if (BIT(imm8, bit))
			{
				const u32 value = get_reg(15 - bit);
				set_reg(15, get_reg(15) - 4);
				write_dword(get_reg(15), value);
				m_icount--;
			}
		break;

	case 0x90: write_byte(get_reg(ri), read_byte(get_reg(ri)) | rj); m_icount -= 2; break;
	case 0x91: write_byte(get_reg(ri), read_byte(get_reg(ri)) | (rj << 4)); m_icount -= 2; break;
	case 0x92: set_reg(ri, get_reg(ri) | get_reg(rj)); set_nz(get_reg(ri)); break;
	case 0x93: set_ps((m_ps & ~0x3fU) | ((m_ps & 0x3fU) | imm8)); break;
	case 0x94: logical_memory(32, 1); break;
	case 0x95: logical_memory(16, 1); break;
	case 0x96: logical_memory(8, 1); break;
	case 0x97:
		switch (rj)
		{
		case 0: m_pc = get_reg(ri); m_icount--; break;
		case 1: m_rp = m_pc; m_pc = get_reg(ri); m_icount--; break;
		case 2:
			if (ri == 0) { m_pc = m_rp; m_icount--; } else undefined_instruction(in_delay_slot);
			break;
		case 3:
			if (ri == 0)
			{
				const bool user_stack = bool(m_ps & PS_S);
				u32 &sp = m_regs[user_stack ? 16 : 15];
				const u32 new_pc = read_dword(sp);
				const u32 new_ps = read_dword(sp + 4);
				sp += 8;
				set_ps(new_ps);
				m_pc = new_pc;
				if (m_eit_depth)
					m_eit_depth--;
				m_trace_inhibit = true;
				m_icount -= 3;
			}
			else undefined_instruction(in_delay_slot);
			break;
		case 4:
			m_ps = (m_ps & ~PS_D0) | (BIT(u32(m_md), 31) ? PS_D0 : 0);
			m_ps = (m_ps & ~PS_D1) | ((bool(m_ps & PS_D0) != bool(BIT(get_reg(ri), 31))) ? PS_D1 : 0);
			set_dr(4, (m_ps & PS_D0) ? 0xffffffffU : 0);
			break;
		case 5:
			m_ps &= ~(PS_D0 | PS_D1);
			set_dr(4, 0);
			break;
		case 6:
		{
			u32 mdh = get_dr(4);
			u32 mdl = get_dr(5);
			mdh = (mdh << 1) | BIT(mdl, 31);
			mdl <<= 1;
			const u32 divisor = get_reg(ri);
			u32 result;
			bool carry;
			if (m_ps & PS_D1)
			{
				const u64 wide = u64(mdh) + divisor;
				result = u32(wide);
				carry = BIT(wide, 32);
			}
			else
			{
				result = mdh - divisor;
				carry = mdh < divisor;
			}
			m_ps = (m_ps & ~PS_C) | (carry ? PS_C : 0);
			if (!(bool(m_ps & PS_D0) ^ bool(m_ps & PS_D1) ^ carry))
			{
				mdh = result;
				mdl |= 1;
			}
			set_dr(4, mdh);
			set_dr(5, mdl);
			m_ps = (m_ps & ~PS_Z) | (!mdh ? PS_Z : 0);
			break;
		}
		case 7:
		{
			const u32 mdh = get_dr(4);
			const u32 divisor = get_reg(ri);
			const u32 result = (m_ps & PS_D1) ? mdh + divisor : mdh - divisor;
			const bool carry = (m_ps & PS_D1) ? u64(mdh) + divisor > 0xffffffffU : mdh < divisor;
			m_ps = (m_ps & ~(PS_C | PS_Z)) | (carry ? PS_C : 0) | (!result ? PS_Z : 0);
			if (!result) set_dr(4, 0);
			break;
		}
		case 8: set_reg(ri, s32(s8(get_reg(ri)))); break;
		case 9: set_reg(ri, u8(get_reg(ri))); break;
		case 10: set_reg(ri, s32(s16(get_reg(ri)))); break;
		case 11: set_reg(ri, u16(get_reg(ri))); break;
		default: undefined_instruction(in_delay_slot); break;
		}
		break;
	case 0x98: write_byte(get_reg(ri), read_byte(get_reg(ri)) ^ rj); m_icount -= 2; break;
	case 0x99: write_byte(get_reg(ri), read_byte(get_reg(ri)) ^ (rj << 4)); m_icount -= 2; break;
	case 0x9a: set_reg(ri, get_reg(ri) ^ get_reg(rj)); set_nz(get_reg(ri)); break;
	case 0x9b:
		set_reg(ri, (u32(rj) << 16) | read_word(m_pc));
		m_pc += 2;
		m_icount--;
		break;
	case 0x9c: logical_memory(32, 2); break;
	case 0x9d: logical_memory(16, 2); break;
	case 0x9e: logical_memory(8, 2); break;
	case 0x9f:
		switch (rj)
		{
		case 0: schedule_delayed_branch(get_reg(ri)); break;
		case 1: m_rp = m_pc + 2; schedule_delayed_branch(get_reg(ri)); break;
		case 2:
			if (ri == 0) schedule_delayed_branch(m_rp); else undefined_instruction(in_delay_slot);
			break;
		case 3:
			if (ri == 0)
			{
				if (!(m_ps & PS_T))
					take_exception(9, m_pc, false, 4, true);
			}
			else undefined_instruction(in_delay_slot);
			break;
		case 6:
			if (ri == 0 && (m_ps & PS_Z)) set_dr(5, get_dr(5) + 1); else if (ri != 0) undefined_instruction(in_delay_slot);
			break;
		case 7:
			if (ri == 0 && (m_ps & PS_D1)) set_dr(5, -get_dr(5)); else if (ri != 0) undefined_instruction(in_delay_slot);
			break;
		case 8:
			// The six-byte LDI:32 instruction may place its immediate at an
			// address that is only half-word aligned.  Address-space dword
			// helpers align the address, so fetch the two encoded halves.
			set_reg(ri, (u32(read_word(m_pc)) << 16) | read_word(m_pc + 2));
			m_pc += 4;
			m_icount -= 2;
			break;
		case 9:
			if (ri == 0)
			{
				const u32 fp = get_reg(14);
				set_reg(15, fp + 4);
				set_reg(14, read_dword(fp));
			}
			else undefined_instruction(in_delay_slot);
			break;
		case 10:
			if (ri != 0) undefined_instruction(in_delay_slot);
			break;
		case 12: case 13: case 14: case 15:
			m_pc += 2;
			take_exception(7, m_pc, false);
			break;
		default: undefined_instruction(in_delay_slot); break;
		}
		break;

	case 0xa0: set_reg(ri, get_reg(ri) + rj); break;
	case 0xa1: set_reg(ri, get_reg(ri) + u32(s32(rj) - 16)); break;
	case 0xa2: set_reg(ri, get_reg(ri) + get_reg(rj)); break;
	case 0xa3: set_reg(15, get_reg(15) + u32(s32(s8(imm8)) * 4)); break;
	case 0xa4: set_reg(ri, add_flags(get_reg(ri), rj, false)); break;
	case 0xa5: set_reg(ri, add_flags(get_reg(ri), u32(s32(rj) - 16), false)); break;
	case 0xa6: set_reg(ri, add_flags(get_reg(ri), get_reg(rj), false)); break;
	case 0xa7: set_reg(ri, add_flags(get_reg(ri), get_reg(rj), bool(m_ps & PS_C))); break;
	case 0xa8: sub_flags(get_reg(ri), rj, false); break;
	case 0xa9: sub_flags(get_reg(ri), u32(s32(rj) - 16), false); break;
	case 0xaa: sub_flags(get_reg(ri), get_reg(rj), false); break;
	case 0xab:
	{
		const u64 result = u64(get_reg(ri)) * get_reg(rj);
		m_md = result;
		m_ps = (m_ps & ~(PS_N | PS_Z | PS_V)) | (BIT(result, 63) ? PS_N : 0) | (!u32(result) ? PS_Z : 0) | (u32(result >> 32) ? PS_V : 0);
		m_icount -= 4;
		break;
	}
	case 0xac: set_reg(ri, sub_flags(get_reg(ri), get_reg(rj), false)); break;
	case 0xad: set_reg(ri, sub_flags(get_reg(ri), get_reg(rj), bool(m_ps & PS_C))); break;
	case 0xae: set_reg(ri, get_reg(ri) - get_reg(rj)); break;
	case 0xaf:
	{
		const s64 result = s64(s32(get_reg(ri))) * s32(get_reg(rj));
		m_md = u64(result);
		m_ps = (m_ps & ~(PS_N | PS_Z | PS_V)) | (BIT(u32(result), 31) ? PS_N : 0) | (!result ? PS_Z : 0) |
			((result > 0x7fffffffLL || result < -0x80000000LL) ? PS_V : 0);
		m_icount -= 4;
		break;
	}

	case 0xb0: shift(rj, 1); break;
	case 0xb1: shift(rj + 16, 1); break;
	case 0xb2: shift(get_reg(rj), 1); break;
	case 0xb3:
		if (rj < 6) set_dr(rj, get_reg(ri)); else undefined_instruction(in_delay_slot);
		break;
	case 0xb4: shift(rj, 0); break;
	case 0xb5: shift(rj + 16, 0); break;
	case 0xb6: shift(get_reg(rj), 0); break;
	case 0xb7:
		if (rj < 6) set_reg(ri, get_dr(rj)); else undefined_instruction(in_delay_slot);
		break;
	case 0xb8: shift(rj, 2); break;
	case 0xb9: shift(rj + 16, 2); break;
	case 0xba: shift(get_reg(rj), 2); break;
	case 0xbb:
	{
		const u32 result = u16(get_reg(ri)) * u32(u16(get_reg(rj)));
		set_dr(5, result);
		set_nz(result);
		m_icount -= 2;
		break;
	}
	case 0xbc: m_resource[rj] = read_dword(get_reg(ri)); set_reg(ri, get_reg(ri) + 4); break;
	case 0xbd: write_dword(get_reg(ri), m_resource[rj]); set_reg(ri, get_reg(ri) + 4); break;
	case 0xbe: undefined_instruction(in_delay_slot); break;
	case 0xbf:
	{
		const s32 result = s16(get_reg(ri)) * s32(s16(get_reg(rj)));
		set_dr(5, result);
		set_nz(result);
		m_icount -= 2;
		break;
	}

	case 0xc0: case 0xc1: case 0xc2: case 0xc3: case 0xc4: case 0xc5: case 0xc6: case 0xc7:
	case 0xc8: case 0xc9: case 0xca: case 0xcb: case 0xcc: case 0xcd: case 0xce: case 0xcf:
		set_reg(ri, BIT(opcode, 4, 8));
		break;

	case 0xd0: case 0xd1: case 0xd2: case 0xd3: case 0xd4: case 0xd5: case 0xd6: case 0xd7:
	case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xdd: case 0xde: case 0xdf:
	{
		const s32 displacement = ((s32(opcode & 0x07ff) ^ 0x400) - 0x400) * 2;
		const u32 target = m_pc + displacement;
		if (BIT(opcode, 11))
		{
			m_rp = m_pc + 2;
			schedule_delayed_branch(target);
		}
		else
		{
			m_rp = m_pc;
			m_pc = target;
			m_icount--;
		}
		break;
	}

	case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe4: case 0xe5: case 0xe6: case 0xe7:
	case 0xe8: case 0xe9: case 0xea: case 0xeb: case 0xec: case 0xed: case 0xee: case 0xef:
		if (condition(op & 15))
		{
			m_pc += s32(s8(imm8)) * 2;
			m_icount--;
		}
		break;

	case 0xf0: case 0xf1: case 0xf2: case 0xf3: case 0xf4: case 0xf5: case 0xf6: case 0xf7:
	case 0xf8: case 0xf9: case 0xfa: case 0xfb: case 0xfc: case 0xfd: case 0xfe: case 0xff:
		schedule_delayed_branch(m_pc + s32(s8(imm8)) * 2, condition(op & 15));
		break;

	default:
		if (op >= 0x20 && op <= 0x7f)
		{
			const u32 address = get_reg(14) + s32(s8(BIT(opcode, 4, 8))) * (op < 0x40 ? 4 : op < 0x60 ? 2 : 1);
			switch (op >> 4)
			{
			case 2: set_reg(ri, read_dword(address)); break;
			case 3: write_dword(address, get_reg(ri)); break;
			case 4: set_reg(ri, read_word(address)); break;
			case 5: write_word(address, get_reg(ri)); break;
			case 6: set_reg(ri, read_byte(address)); break;
			case 7: write_byte(address, get_reg(ri)); break;
			}
		}
		else
			undefined_instruction(in_delay_slot);
		break;
	}
}

void mb91103_device::device_start()
{
	fr_cpu_device::device_start();
	m_adc_timer = timer_alloc(FUNC(mb91103_device::adc_complete), this);
	m_free_timer = timer_alloc(FUNC(mb91103_device::free_timer_event), this);
	for (unsigned channel = 0; channel < 2; channel++)
	{
		m_reload_timer[channel] = timer_alloc(FUNC(mb91103_device::reload_timer_underflow), this);
		m_udc_timer[channel] = timer_alloc(FUNC(mb91103_device::udc_underflow), this);
		m_utimer[channel] = timer_alloc(FUNC(mb91103_device::utimer_underflow), this);
		m_uart_timer[channel] = timer_alloc(FUNC(mb91103_device::uart_transmit_complete), this);
	}
	save_item(NAME(m_io));
	save_item(NAME(m_adc_channel));
	save_item(NAME(m_adc_end));
	save_item(NAME(m_adc_mode));
	save_item(NAME(m_adc_active));
	save_item(NAME(m_free_count));
	save_item(NAME(m_free_time));
	save_item(NAME(m_udc_count));
	save_item(NAME(m_udc_input));
	save_item(NAME(m_icu_input));
	save_item(NAME(m_utimer_phase));
}

void mb91103_device::device_reset()
{
	fr_cpu_device::device_reset();
	std::fill(std::begin(m_io), std::end(m_io), 0);
	m_adc_timer->adjust(attotime::never);
	m_free_timer->adjust(attotime::never);
	for (emu_timer *timer : m_reload_timer)
		timer->adjust(attotime::never);
	for (emu_timer *timer : m_udc_timer)
		timer->adjust(attotime::never);
	for (emu_timer *timer : m_utimer)
		timer->adjust(attotime::never);
	for (emu_timer *timer : m_uart_timer)
		timer->adjust(attotime::never);
	m_adc_channel = 0;
	m_adc_end = 0;
	m_adc_mode = 0;
	m_adc_active = false;
	m_free_count = 0;
	m_free_time = machine().time();
	std::fill(std::begin(m_udc_count), std::end(m_udc_count), 0);
	std::fill(std::begin(m_udc_input), std::end(m_udc_input), 0);
	m_icu_input = 0;
	std::fill(std::begin(m_utimer_phase), std::end(m_utimer_phase), false);

	// Serial interfaces and U-TIMERs
	m_io[0x01a] = 0x02; // SMCS high byte
	m_io[0x01c] = 0x08; // SSR0: transmit data register empty
	m_io[0x01e] = 0x04; // SCR0: receive error clear inactive
	m_io[0x020] = 0x08; // SSR1: transmit data register empty
	m_io[0x022] = 0x04; // SCR1: receive error clear inactive
	m_io[0x07b] = 0x01; // UTIMC0: clear bit reads as one
	m_io[0x07f] = 0x01; // UTIMC1: clear bit reads as one
	free_timer_schedule();

	// Up/down counters
	m_io[0x089] = 0x08;
	m_io[0x091] = 0x08;

	// Interrupt controller
	std::fill(&m_io[0x400], &m_io[0x430], 0x1f);
	for (unsigned source = 0; source < 48; source++)
		set_irq_level(source, 0x1f);
	m_io[0x431] = 0x1f;

	// Reset/clock controller
	m_io[0x480] = 0x80;
	m_io[0x481] = 0x1c;
	m_io[0x484] = 0xcd;

	// External bus controller reset values
	m_io[0x60d] = 0x01;
	m_io[0x611] = 0x02;
	m_io[0x615] = 0x03;
	m_io[0x619] = 0x04;
	m_io[0x61d] = 0x05;
	m_io[0x620] = 0x07;
	m_io[0x628] = 0x4c;
	m_io[0x629] = 0x7f;
	m_io[0x62b] = 0xff;
}

int mb91103_device::port_index(u16 address) const noexcept
{
	static constexpr std::array<u16, 14> pdr = { 0x003, 0x002, 0x001, 0x005, 0x00b, 0x00a, 0x009, 0x008, 0x011, 0x012, 0x013, 0x014, 0x015, 0x016 };
	for (unsigned index = 0; index < pdr.size(); index++)
		if (pdr[index] == address)
			return index;
	return -1;
}

u16 mb91103_device::port_ddr_address(unsigned port) const noexcept
{
	static constexpr std::array<u16, 14> ddr = { 0x603, 0x602, 0x601, 0x605, 0x60b, 0x60a, 0x609, 0x608, 0x0d1, 0x0d2, 0x0d3, 0x0d4, 0x0d5, 0x0d6 };
	return ddr[port];
}

u8 mb91103_device::io_byte_r(u16 address)
{
	if (address == 0x074 || address == 0x075)
	{
		free_timer_sync();
		return address == 0x074 ? m_free_count >> 8 : m_free_count;
	}
	if (address == 0x01d || address == 0x021)
	{
		const unsigned channel = (address - 0x01d) / 4;
		const u8 result = m_io[address];
		if (!machine().side_effects_disabled())
		{
			m_io[0x01c + 4 * channel] &= ~0x10; // RDRF
			uart_update_irq(channel);
		}
		return result;
	}
	for (unsigned channel = 0; channel < 2; channel++)
	{
		const u16 counter_address = 0x084 + 8 * channel;
		if (address == counter_address || address == counter_address + 1)
		{
			udc_sync(channel);
			const u8 result = address == counter_address ? m_udc_count[channel] >> 8 : m_udc_count[channel];
			udc_start(channel);
			return result;
		}
	}
	for (unsigned channel = 0; channel < 2; channel++)
	{
		const u16 timer_address = 0x078 + 4 * channel;
		if (address == timer_address || address == timer_address + 1)
		{
			const u16 count = utimer_count(channel);
			return address == timer_address ? count >> 8 : count;
		}
	}
	for (unsigned channel = 0; channel < 2; channel++)
	{
		const u16 timer_address = 0x02a + 8 * channel;
		if (address == timer_address || address == timer_address + 1)
		{
			const u16 count = reload_timer_count(channel);
			return address == timer_address ? count >> 8 : count;
		}
	}
	if (const int port = port_index(address); port >= 0)
	{
		const u8 ddr = m_io[port_ddr_address(port)];
		return (m_io[address] & ddr) | (m_port_read[port]() & ~ddr);
	}
	return m_io[address & 0x7ff];
}

void mb91103_device::io_byte_w(u16 address, u8 data)
{
	address &= 0x7ff;
	for (unsigned channel = 0; channel < 2; channel++)
	{
		const u16 counter_address = 0x084 + 8 * channel;
		const u16 reload_address = counter_address + 2;
		const u16 control_address = counter_address + 4;
		const u16 status_address = counter_address + 7;

		if (address == counter_address || address == counter_address + 1)
			return; // UDCR is read-only
		if (address == reload_address || address == reload_address + 1)
		{
			m_io[address] = data;
			return;
		}
		if (address == control_address || address == control_address + 1)
		{
			udc_sync(channel);
			if (address == control_address)
			{
				// CDCF is write-zero-to-clear; the remaining high-byte bits
				// select the clock, count mode and input edge.
				m_io[address] = (m_io[address] & data & 0x40) | (data & 0x3f);
			}
			else
			{
				if (BIT(data, 6)) // CTUT transfers RCR to UDCR
				{
					m_udc_count[channel] = (u16(m_io[reload_address]) << 8) | m_io[reload_address + 1];
				}
				if (!BIT(data, 3)) // UDCC is an active-low clear command
					m_udc_count[channel] = 0;
				// CTUT is a command.  UDCC returns to its inactive value.
				m_io[address] = (data & 0x37) | 0x08;
			}
			udc_update_irq(channel);
			udc_start(channel);
			return;
		}
		if (address == status_address)
		{
			udc_sync(channel);
			const u8 old = m_io[address];
			// CMPF, OVFF and UDFF are write-zero-to-clear.  UDF is read-only.
			m_io[address] = (data & 0xe0) | (old & data & 0x1c) | (old & 0x03);
			udc_update_irq(channel);
			udc_start(channel);
			return;
		}
	}
	if (address == 0x045 || address == 0x04d)
	{
		const u8 old = m_io[address];
		m_io[address] = (old & data & 0xc0) | (data & 0x3f);
		const unsigned first = address == 0x045 ? 0 : 2;
		for (unsigned pair_channel = 0; pair_channel < 2; pair_channel++)
		{
			const u8 flag = pair_channel ? 0x80 : 0x40;
			const u8 enable = pair_channel ? 0x20 : 0x10;
			set_irq_pending(30 + first + pair_channel, (m_io[address] & flag) && (m_io[address] & enable));
		}
		return;
	}
	if ((address >= 0x048 && address <= 0x04b) || (address >= 0x050 && address <= 0x053))
		return; // IPCP0-3 are read-only
	if (address == 0x074 || address == 0x075)
	{
		free_timer_sync();
		if (address == 0x074)
			m_free_count = (m_free_count & 0x00ff) | (u16(data) << 8);
		else
			m_free_count = (m_free_count & 0xff00) | data;
		m_io[0x074] = m_free_count >> 8;
		m_io[0x075] = m_free_count;
		m_free_time = machine().time();
		free_timer_schedule();
		return;
	}
	if (address == 0x077)
	{
		free_timer_sync();
		const u8 old = m_io[address];
		const bool clear = BIT(data, 2);
		// IVF is write-zero-to-clear; CLR is a command and always reads zero.
		m_io[address] = (old & data & 0x40) | (data & 0x3b);
		if (clear)
			m_free_count = 0;
		m_free_time = machine().time();
		set_irq_pending(46, BIT(m_io[address], 6) && BIT(m_io[address], 5));
		free_timer_schedule();
		return;
	}
	if (address >= 0x054 && address <= 0x06d && (address & 7) >= 4 && (address & 7) <= 5)
	{
		free_timer_sync();
		if (address & 1)
			m_io[address] = (m_io[address] & data & 0xc0) | (data & 0x33);
		else
			m_io[address] = data & 0x1f;
		const unsigned pair = (address - 0x054) / 8;
		free_timer_update_irq(2 * pair);
		free_timer_update_irq(2 * pair + 1);
		free_timer_schedule();
		return;
	}
	if ((address >= 0x058 && address <= 0x073) && (address & 7) <= 3)
	{
		free_timer_sync();
		m_io[address] = data;
		free_timer_schedule();
		return;
	}
	if (address == 0x01c || address == 0x020)
	{
		const unsigned channel = (address - 0x01c) / 4;
		m_io[address] = (m_io[address] & 0xfc) | (data & 0x03);
		uart_update_irq(channel);
		return;
	}
	if (address == 0x01d || address == 0x021)
	{
		const unsigned channel = (address - 0x01d) / 4;
		m_io[address] = data;
		m_io[0x01c + 4 * channel] &= ~0x08; // TDRE
		uart_update_irq(channel);
		if (BIT(m_io[0x01e + 4 * channel], 0)) // TXE
			m_uart_timer[channel]->adjust(clocks_to_attotime(uart_frame_clocks(channel)), channel);
		return;
	}
	if (address == 0x01e || address == 0x022)
	{
		const unsigned channel = (address - 0x01e) / 4;
		const bool was_enabled = BIT(m_io[address], 0);
		if (!BIT(data, 2))
			m_io[0x01c + 4 * channel] &= 0x1f; // clear parity/overrun/framing errors
		m_io[address] = data | 0x04; // REC always reads as one
		if (!was_enabled && BIT(m_io[address], 0) && !BIT(m_io[0x01c + 4 * channel], 3))
			m_uart_timer[channel]->adjust(clocks_to_attotime(uart_frame_clocks(channel)), channel);
		return;
	}
	if (address == 0x01f || address == 0x023)
	{
		m_io[address] = data & 0xcb;
		return;
	}
	if (address >= 0x400 && address < 0x430)
	{
		m_io[address] = data & 0x1f;
		set_irq_level(address - 0x400, data);
		return;
	}
	if (address == 0x430)
	{
		m_io[address] = data & 0x01;
		set_irq_pending(47, BIT(data, 0));
		return;
	}
	if (address == 0x431)
	{
		m_io[address] = data & 0x1f;
		return;
	}
	if (address == 0x07b || address == 0x07f)
	{
		const unsigned channel = (address - 0x07b) / 4;
		const u8 old = m_io[address];
		const bool underflow = BIT(old, 3) && BIT(data, 3);
		m_io[address] = (data & 0x96) | (underflow ? 0x08 : 0) | 0x01;

		if (!BIT(data, 0))
		{
			m_utimer[channel]->adjust(attotime::never);
			m_utimer_phase[channel] = false;
		}
		if (!BIT(m_io[address], 1))
			m_utimer[channel]->adjust(attotime::never);
		else if (!BIT(data, 0))
			m_utimer[channel]->adjust(clocks_to_attotime(1), channel);
		else if (!BIT(old, 1))
			utimer_start(channel);
		set_irq_pending(42 + channel, BIT(m_io[address], 4) && BIT(m_io[address], 3));
		return;
	}
	if (address == 0x094)
	{
		m_io[address] &= data; // writing zero clears the corresponding cause
		for (unsigned line = 0; line < 8; line++)
			if (!BIT(m_io[address], line))
				set_irq_pending(2 + line, false);
		return;
	}
	m_io[address] = data;
	if (address == 0x095)
	{
		for (unsigned line = 0; line < 8; line++)
			set_irq_pending(2 + line, BIT(m_io[0x094] & m_io[0x095], line));
	}

	if (const int port = port_index(address); port >= 0)
		m_port_write[port](data);
	else
	{
		for (unsigned port = 0; port < 14; port++)
			if (port_ddr_address(port) == address)
			{
				m_port_write[port](m_io[port == 0 ? 0x003 : port == 1 ? 0x002 : port == 2 ? 0x001 : port == 3 ? 0x005 :
					port == 4 ? 0x00b : port == 5 ? 0x00a : port == 6 ? 0x009 : port == 7 ? 0x008 : 0x011 + port - 8]);
				break;
			}
	}

}

u32 mb91103_device::io_r(offs_t offset, u32 mem_mask)
{
	const u16 address = u16(offset << 2);
	u32 result = 0;
	if (ACCESSING_BITS_24_31) result |= u32(io_byte_r(address + 0)) << 24;
	if (ACCESSING_BITS_16_23) result |= u32(io_byte_r(address + 1)) << 16;
	if (ACCESSING_BITS_8_15) result |= u32(io_byte_r(address + 2)) << 8;
	if (ACCESSING_BITS_0_7) result |= io_byte_r(address + 3);
	return result;
}

void mb91103_device::io_w(offs_t offset, u32 data, u32 mem_mask)
{
	const u16 address = u16(offset << 2);
	const u16 old_adcs = (u16(m_io[0x3a]) << 8) | m_io[0x3b];
	u16 old_tmcsr[2];
	for (unsigned channel = 0; channel < 2; channel++)
		old_tmcsr[channel] = (u16(m_io[0x02e + 8 * channel]) << 8) | m_io[0x02f + 8 * channel];
	if (ACCESSING_BITS_24_31) io_byte_w(address + 0, data >> 24);
	if (ACCESSING_BITS_16_23) io_byte_w(address + 1, data >> 16);
	if (ACCESSING_BITS_8_15) io_byte_w(address + 2, data >> 8);
	if (ACCESSING_BITS_0_7) io_byte_w(address + 3, data);

	if (address == 0x38 && (mem_mask & 0x0000ffffU))
	{
		const u16 written = (u16(m_io[0x3a]) << 8) | m_io[0x3b];
		u16 control = written & 0x2fff; // BUSY, INT and PAUS are status bits
		control &= ~0x0200; // STRT always reads as zero
		control |= old_adcs & written & 0x4000; // writing zero clears INT

		if (m_adc_active && !BIT(written, 15) && !BIT(written, 9))
		{
			m_adc_active = false;
			m_adc_timer->adjust(attotime::never);
		}
		else if (m_adc_active)
			control |= 0x8000;

		if (BIT(written, 9))
		{
			adc_start(written);
			control |= 0x8000;
		}

		m_io[0x3a] = control >> 8;
		m_io[0x3b] = control;
		if (!BIT(control, 14))
			set_irq_pending(25, false);
	}

	for (unsigned channel = 0; channel < 2; channel++)
	{
		const u16 control_address = 0x02e + 8 * channel;
		if (address <= control_address + 1 && address + 3 >= control_address)
		{
			const unsigned shift = (address + 3 - (control_address + 1)) * 8;
			const bool low_accessed = bool(mem_mask & (0xffU << shift));
			u16 control = (u16(m_io[control_address]) << 8) | m_io[control_address + 1];
			const bool trigger = low_accessed && BIT(control, 0);
			control &= 0x0ffe; // upper bits are unused and TRG always reads as zero
			const bool underflow = BIT(old_tmcsr[channel], 2) && (!low_accessed || BIT(control, 2));
			control = (control & ~0x0004) | (underflow ? 0x0004 : 0);
			m_io[control_address] = control >> 8;
			m_io[control_address + 1] = control;

			if (!BIT(control, 1))
				m_reload_timer[channel]->adjust(attotime::never);
			else if (trigger)
				reload_timer_start(channel);
			if (!BIT(control, 2))
				set_irq_pending(26 + channel, false);
		}
	}

	if ((address == 0x3f0 || address == 0x3f4 || address == 0x3f8) && mem_mask == 0xffffffffU)
	{
		const u32 value = (u32(m_io[address]) << 24) | (u32(m_io[address + 1]) << 16) | (u32(m_io[address + 2]) << 8) | m_io[address + 3];
		const u32 result = address == 0x3f0 ? std::countl_one(value) : address == 0x3f4 ? std::countl_zero(value) :
			(BIT(value, 31) ? std::countl_one(value) : std::countl_zero(value));
		m_io[0x3fc] = result >> 24;
		m_io[0x3fd] = result >> 16;
		m_io[0x3fe] = result >> 8;
		m_io[0x3ff] = result;
	}
}

void mb91103_device::external_interrupt(unsigned line)
{
	if (line < 8)
	{
		m_io[0x094] |= u8(1U << line);
		if (BIT(m_io[0x095], line))
			set_irq_pending(2 + line, true);
	}
}

void mb91103_device::uart_receive(unsigned channel, u8 data)
{
	if (channel >= 2 || !BIT(m_io[0x01e + 4 * channel], 1)) // RXE
		return;
	const u16 status_address = 0x01c + 4 * channel;
	if (BIT(m_io[status_address], 4))
		m_io[status_address] |= 0x40; // overrun
	m_io[0x01d + 4 * channel] = data;
	m_io[status_address] |= 0x10; // RDRF
	uart_update_irq(channel);
}

void mb91103_device::input_capture(unsigned channel, int state)
{
	if (channel >= 4)
		return;

	const u8 mask = u8(1U << channel);
	const bool old_state = bool(m_icu_input & mask);
	const bool new_state = state != 0;
	if (old_state == new_state)
		return;
	if (new_state)
		m_icu_input |= mask;
	else
		m_icu_input &= ~mask;

	const u16 control_address = channel < 2 ? 0x045 : 0x04d;
	const unsigned pair_channel = channel & 1;
	const unsigned edge = BIT(m_io[control_address], 2 * pair_channel, 2);
	if (!edge || (edge == 1 && !new_state) || (edge == 2 && new_state))
		return;

	static constexpr u16 capture_address[4] = { 0x048, 0x04a, 0x050, 0x052 };
	free_timer_sync();
	m_io[capture_address[channel]] = m_free_count >> 8;
	m_io[capture_address[channel] + 1] = m_free_count;
	const u8 flag = pair_channel ? 0x80 : 0x40;
	m_io[control_address] |= flag;
	set_irq_pending(30 + channel, BIT(m_io[control_address], pair_channel ? 5 : 4));
}

void mb91103_device::udc_input(unsigned channel, unsigned input, int state)
{
	if (channel >= 2 || input >= 3)
		return;

	const u8 mask = u8(1U << input);
	const bool old_state = bool(m_udc_input[channel] & mask);
	const bool new_state = state != 0;
	if (old_state == new_state)
		return;
	if (new_state)
		m_udc_input[channel] |= mask;
	else
		m_udc_input[channel] &= ~mask;

	const u16 control_address = 0x088 + 8 * channel;
	const u16 status_address = 0x08b + 8 * channel;
	if (!BIT(m_io[status_address], 7))
		return;

	if (input == 2)
	{
		if (BIT(m_io[control_address + 1], 2))
		{
			udc_sync(channel);
			udc_start(channel);
		}
		else
		{
			const unsigned edge = BIT(m_io[control_address + 1], 0, 2);
			if ((edge == 1 && !new_state) || (edge == 2 && new_state))
				m_udc_count[channel] = 0;
		}
		return;
	}
	if (!udc_gate_open(channel))
		return;

	const unsigned mode = BIT(m_io[control_address], 2, 2);
	if (mode == 1)
	{
		const unsigned edge = BIT(m_io[control_address], 0, 2);
		if (edge == 3 || (edge == 1 && !new_state) || (edge == 2 && new_state))
			udc_step(channel, input == 0 ? 1 : -1);
	}
	else if (mode == 2 && input == 1)
	{
		// In x2 quadrature mode both BIN edges count.  Matching A/B
		// levels count up; differing levels count down.
		udc_step(channel, BIT(m_udc_input[channel], 0) == new_state ? 1 : -1);
	}
	else if (mode == 3)
	{
		const bool other = BIT(m_udc_input[channel], input ^ 1);
		const bool up = input == 1 ? (other == new_state) : (new_state != other);
		udc_step(channel, up ? 1 : -1);
	}
}

void mb91103_device::adc_start(u16 control)
{
	const u8 mode = BIT(control, 6, 2);
	if (!m_adc_active)
	{
		m_adc_channel = BIT(control, 3, 3);
		m_adc_end = BIT(control, 0, 3);
		m_adc_mode = mode;
		m_adc_active = true;
	}
	else if (m_adc_mode == 0) // restartable single conversion
		m_adc_channel = BIT(control, 3, 3);
	else
		return;

	// The MB91103 specifies a minimum conversion time of 5.6 us at
	// 25 MHz, or 140 peripheral clocks.
	m_adc_timer->adjust(clocks_to_attotime(140));
}

TIMER_CALLBACK_MEMBER(mb91103_device::adc_complete)
{
	const u16 result = m_adc_read[m_adc_channel]() & 0x03ff;
	m_io[0x38] = result >> 8;
	m_io[0x39] = result;

	// Channel 6 can use the A/D completion request to collect a channel
	// sweep.  DMAAR6 is the moving memory address and DMACT6 is the
	// remaining 16-bit transfer count.  The EX5 uses this mode to sample
	// its control inputs without taking an interrupt for every channel.
	const bool adc_dma = BIT(m_io[0x230], 7);
	if (adc_dma)
	{
		u32 address = (u32(m_io[0x258]) << 24) | (u32(m_io[0x259]) << 16) | (u32(m_io[0x25a]) << 8) | m_io[0x25b];
		u16 count = (u16(m_io[0x26c]) << 8) | m_io[0x26d];
		if (count)
		{
			space(AS_PROGRAM).write_word(address, result);
			address += 2;
			count--;
			m_io[0x258] = address >> 24;
			m_io[0x259] = address >> 16;
			m_io[0x25a] = address >> 8;
			m_io[0x25b] = address;
			m_io[0x26c] = count >> 8;
			m_io[0x26d] = count;
		}
		if (!count)
		{
			// DE clears at the end of transfer and DSS reports normal
			// completion as 8.  The EX5 polls both fields before consuming
			// the six converted samples.
			m_io[0x230] = (m_io[0x230] & 0x70) | 0x08;
		}
		m_io[0x3a] &= ~0x40; // DMA acknowledgement clears the A/D request
		set_irq_pending(25, false);
	}
	else
	{
		m_io[0x3a] |= 0x40; // INT
		if (BIT(m_io[0x3a], 5)) // INTE
			set_irq_pending(25, true);
	}

	const bool more_channels = m_adc_channel < m_adc_end;
	if (more_channels)
		m_adc_channel++;
	else if (m_adc_mode >= 2)
		m_adc_channel = BIT(m_io[0x3b], 3, 3);

	if (m_adc_mode == 2 || (m_adc_mode < 2 && more_channels))
		m_adc_timer->adjust(clocks_to_attotime(140));
	else if (m_adc_mode < 2)
	{
		m_adc_active = false;
		m_io[0x3a] &= ~0x80; // BUSY
	}
	// Stop mode deliberately leaves BUSY set and waits for the next trigger.
}

void mb91103_device::reload_timer_start(unsigned channel)
{
	const u16 reload_address = 0x028 + 8 * channel;
	const u16 control_address = 0x02e + 8 * channel;
	const u16 reload = (u16(m_io[reload_address]) << 8) | m_io[reload_address + 1];
	const u16 control = (u16(m_io[control_address]) << 8) | m_io[control_address + 1];
	if (BIT(control, 10, 2) == 3) // external event clock
	{
		m_reload_timer[channel]->adjust(attotime::never);
		return;
	}
	const unsigned divider = 2U << (2 * BIT(control, 10, 2));
	m_io[0x02a + 8 * channel] = reload >> 8;
	m_io[0x02b + 8 * channel] = reload;
	m_reload_timer[channel]->adjust(clocks_to_attotime((u64(reload) + 1) * divider), channel);
}

u16 mb91103_device::reload_timer_count(unsigned channel) const
{
	if (m_reload_timer[channel]->expire().is_never())
		return (u16(m_io[0x02a + 8 * channel]) << 8) | m_io[0x02b + 8 * channel];
	const u16 control = (u16(m_io[0x02e + 8 * channel]) << 8) | m_io[0x02f + 8 * channel];
	const unsigned divider = 2U << (2 * BIT(control, 10, 2));
	return std::min<u64>(0xffff, m_reload_timer[channel]->remaining().as_ticks(clock()) / divider);
}

TIMER_CALLBACK_MEMBER(mb91103_device::reload_timer_underflow)
{
	const unsigned channel = param;
	const u16 control_address = 0x02e + 8 * channel;
	u16 control = (u16(m_io[control_address]) << 8) | m_io[control_address + 1];
	control |= 0x0004; // UF
	m_io[control_address] = control >> 8;
	m_io[control_address + 1] = control;
	m_io[0x02a + 8 * channel] = 0xff;
	m_io[0x02b + 8 * channel] = 0xff;
	if (BIT(control, 3))
		set_irq_pending(26 + channel, true);
	if (BIT(control, 4) && BIT(control, 1))
		reload_timer_start(channel);
}

unsigned mb91103_device::free_timer_divider() const
{
	static constexpr unsigned divider[4] = { 4, 16, 32, 64 };
	return divider[BIT(m_io[0x077], 0, 2)];
}

void mb91103_device::free_timer_sync()
{
	const attotime now = machine().time();
	if (!BIT(m_io[0x077], 4)) // STOP
	{
		const u32 frequency = clock() / free_timer_divider();
		const u64 ticks = (now - m_free_time).as_ticks(frequency);
		if (ticks)
		{
			m_free_count += ticks;
			m_free_time += attotime::from_ticks(ticks, frequency);
		}
	}
	else
		m_free_time = now;

	m_io[0x074] = m_free_count >> 8;
	m_io[0x075] = m_free_count;
}

void mb91103_device::free_timer_update_irq(unsigned channel)
{
	const u8 control = m_io[0x055 + 8 * (channel / 2)];
	const unsigned pair_channel = channel & 1;
	const u8 flag = pair_channel ? 0x80 : 0x40;
	const u8 enable = pair_channel ? 0x20 : 0x10;
	set_irq_pending(34 + channel, (control & flag) && (control & enable));
}

void mb91103_device::free_timer_schedule()
{
	m_free_timer->adjust(attotime::never);
	if (BIT(m_io[0x077], 4)) // STOP
		return;

	u32 ticks = 0x10000U - m_free_count;
	u16 events = 0x100; // free-run timer overflow
	for (unsigned channel = 0; channel < 8; channel++)
	{
		const u8 control = m_io[0x055 + 8 * (channel / 2)];
		if (!BIT(control, channel & 1)) // CST0/CST1
			continue;

		const u16 compare_address = 0x058 + 8 * (channel / 2) + 2 * (channel & 1);
		const u16 compare = (u16(m_io[compare_address]) << 8) | m_io[compare_address + 1];
		u32 delta = u16(compare - m_free_count);
		if (!delta)
			delta = 0x10000;
		if (delta < ticks)
		{
			ticks = delta;
			events = u16(1U << channel);
		}
		else if (delta == ticks)
			events |= u16(1U << channel);
	}

	m_free_timer->adjust(attotime::from_ticks(ticks, clock() / free_timer_divider()), events);
}

TIMER_CALLBACK_MEMBER(mb91103_device::free_timer_event)
{
	const u16 events = param;
	free_timer_sync();

	// Snap to the programmed event count to avoid sub-tick rounding at the
	// scheduler boundary.
	for (unsigned channel = 0; channel < 8; channel++)
		if (BIT(events, channel))
		{
			const u16 compare_address = 0x058 + 8 * (channel / 2) + 2 * (channel & 1);
			m_free_count = (u16(m_io[compare_address]) << 8) | m_io[compare_address + 1];
			break;
		}
	if (events & 0x100)
		m_free_count = 0;

	for (unsigned channel = 0; channel < 8; channel++)
		if (BIT(events, channel))
		{
			const u16 control_address = 0x055 + 8 * (channel / 2);
			m_io[control_address] |= (channel & 1) ? 0x80 : 0x40; // ICP1/ICP0
			free_timer_update_irq(channel);
		}

	if ((events & 0x100) || (BIT(events, 0) && BIT(m_io[0x077], 3)))
	{
		m_free_count = 0;
		m_io[0x077] |= 0x40; // IVF
		set_irq_pending(46, BIT(m_io[0x077], 5));
	}

	m_io[0x074] = m_free_count >> 8;
	m_io[0x075] = m_free_count;
	m_free_time = machine().time();
	free_timer_schedule();
}

bool mb91103_device::udc_gate_open(unsigned channel) const
{
	const u16 control_address = 0x088 + 8 * channel;
	if (!BIT(m_io[control_address + 1], 2)) // ZIN is a clear input, not a gate
		return true;

	const unsigned gate = BIT(m_io[control_address + 1], 0, 2);
	const bool zin = BIT(m_udc_input[channel], 2);
	return gate == 0 || (gate == 1 && !zin) || (gate == 2 && zin);
}

void mb91103_device::udc_sync(unsigned channel)
{
	if (m_udc_timer[channel]->expire().is_never())
		return;

	const u16 control_address = 0x088 + 8 * channel;
	const unsigned divider = BIT(m_io[control_address], 4) ? 8 : 2;
	const u64 ticks = m_udc_timer[channel]->remaining().as_ticks(clock()) / divider;
	m_udc_count[channel] = ticks ? std::min<u64>(0xffff, ticks - 1) : 0;
	m_udc_timer[channel]->adjust(attotime::never);
}

void mb91103_device::udc_start(unsigned channel)
{
	m_udc_timer[channel]->adjust(attotime::never);
	const u16 control_address = 0x088 + 8 * channel;
	const u16 status_address = 0x08b + 8 * channel;
	if (!BIT(m_io[status_address], 7) || BIT(m_io[control_address], 2, 2) != 0 || !udc_gate_open(channel))
		return;

	const unsigned divider = BIT(m_io[control_address], 4) ? 8 : 2;
	m_udc_timer[channel]->adjust(clocks_to_attotime((u64(m_udc_count[channel]) + 1) * divider), channel);
}

void mb91103_device::udc_update_irq(unsigned channel)
{
	const u16 control_address = 0x088 + 8 * channel;
	const u16 status_address = 0x08b + 8 * channel;
	const u8 status = m_io[status_address];
	const bool compare = BIT(status, 4) && BIT(status, 6);
	const bool limit = (status & 0x0c) && BIT(status, 5);
	const bool direction = BIT(m_io[control_address], 6) && BIT(m_io[control_address], 5);
	set_irq_pending(28 + channel, compare || limit || direction);
}

void mb91103_device::udc_step(unsigned channel, int direction)
{
	const u16 control_address = 0x088 + 8 * channel;
	const u16 reload_address = 0x086 + 8 * channel;
	const u16 status_address = 0x08b + 8 * channel;
	const u8 old_direction = m_io[status_address] & 0x03;
	const u8 new_direction = direction > 0 ? 0x02 : 0x01;
	if (old_direction && old_direction != new_direction)
		m_io[control_address] |= 0x40; // CDCF
	m_io[status_address] = (m_io[status_address] & ~0x03) | new_direction;

	if (direction > 0)
	{
		if (m_udc_count[channel] == 0xffff)
		{
			m_udc_count[channel] = 0;
			m_io[status_address] |= 0x08; // OVFF
		}
		else
			m_udc_count[channel]++;
	}
	else if (!m_udc_count[channel])
	{
		m_io[status_address] |= 0x04; // UDFF
		m_udc_count[channel] = BIT(m_io[control_address + 1], 4)
			? (u16(m_io[reload_address]) << 8) | m_io[reload_address + 1]
			: 0xffff;
	}
	else
		m_udc_count[channel]--;

	const u16 compare = (u16(m_io[reload_address]) << 8) | m_io[reload_address + 1];
	if (m_udc_count[channel] == compare)
	{
		m_io[status_address] |= 0x10; // CMPF
		if (BIT(m_io[control_address + 1], 5))
			m_udc_count[channel] = 0;
	}
	udc_update_irq(channel);
}

TIMER_CALLBACK_MEMBER(mb91103_device::udc_underflow)
{
	const unsigned channel = param;
	const u16 control_address = 0x088 + 8 * channel;
	const u16 reload_address = 0x086 + 8 * channel;
	const u16 status_address = 0x08b + 8 * channel;
	m_io[status_address] = (m_io[status_address] & ~0x03) | 0x05; // count down, UDFF
	m_udc_count[channel] = BIT(m_io[control_address + 1], 4)
		? (u16(m_io[reload_address]) << 8) | m_io[reload_address + 1]
		: 0xffff;
	udc_update_irq(channel);
	udc_start(channel);
}

void mb91103_device::utimer_start(unsigned channel)
{
	const u16 reload_address = 0x078 + 4 * channel;
	const u16 reload = (u16(m_io[reload_address]) << 8) | m_io[reload_address + 1];
	const bool odd_cycle = BIT(m_io[0x07b + 4 * channel], 7) && m_utimer_phase[channel];
	m_utimer[channel]->adjust(clocks_to_attotime(u32(reload) + 1 + (odd_cycle ? 1 : 0)), channel);
}

u16 mb91103_device::utimer_count(unsigned channel) const
{
	if (m_utimer[channel]->expire().is_never())
		return 0;
	const u64 ticks = m_utimer[channel]->remaining().as_ticks(clock());
	return ticks ? std::min<u64>(0xffff, ticks - 1) : 0;
}

TIMER_CALLBACK_MEMBER(mb91103_device::utimer_underflow)
{
	const unsigned channel = param;
	const u16 control_address = 0x07b + 4 * channel;
	m_io[control_address] |= 0x08;
	if (BIT(m_io[control_address], 4))
		set_irq_pending(42 + channel, true);
	m_utimer_phase[channel] = !m_utimer_phase[channel];
	if (BIT(m_io[control_address], 1))
		utimer_start(channel);
}

void mb91103_device::uart_update_irq(unsigned channel)
{
	const u8 status = m_io[0x01c + 4 * channel];
	set_irq_pending(11 + channel, BIT(status, 1) && BIT(status, 4));
	set_irq_pending(14 + channel, BIT(status, 0) && BIT(status, 3));
}

u32 mb91103_device::uart_frame_clocks(unsigned channel) const
{
	const u8 smr = m_io[0x01f + 4 * channel];
	const u8 scr = m_io[0x01e + 4 * channel];
	const unsigned mode = BIT(smr, 6, 2);
	const bool synchronous = mode == 2;
	unsigned bits = synchronous ? 8 : (BIT(scr, 4) ? 8 : 7) + 2 + (BIT(scr, 7) ? 1 : 0) + (BIT(scr, 5) ? 1 : 0);
	if (BIT(smr, 3)) // external serial clock; retain deterministic progress if it is not wired
		return bits;

	const u16 reload_address = 0x078 + 4 * channel;
	const u16 reload = (u16(m_io[reload_address]) << 8) | m_io[reload_address + 1];
	const u32 serial_clock_period = 2 * (u32(reload) + 1) + BIT(m_io[0x07b + 4 * channel], 7);
	return std::max<u32>(1, bits * serial_clock_period * (synchronous ? 1 : 16));
}

TIMER_CALLBACK_MEMBER(mb91103_device::uart_transmit_complete)
{
	const unsigned channel = param;
	m_uart_write[channel](m_io[0x01d + 4 * channel]);
	m_io[0x01c + 4 * channel] |= 0x08; // TDRE
	uart_update_irq(channel);
}

void fr_cpu_device::state_string_export(const device_state_entry &entry, std::string &str) const
{
	switch (entry.index())
	{
	case STATE_GENFLAGS:
		str = string_format("<%d%d%d%d%d> %c%c%c--%c%c%c%c%c%c",
				BIT(m_ps, 20),
				BIT(m_ps, 19),
				BIT(m_ps, 18),
				BIT(m_ps, 17),
				BIT(m_ps, 16),
				BIT(m_ps, 10) ? 'D' : '.',
				BIT(m_ps, 9) ? 'd' : '.',
				BIT(m_ps, 8) ? 'T' : '.',
				BIT(m_ps, 5) ? 'S' : '.',
				BIT(m_ps, 4) ? 'I' : '.',
				BIT(m_ps, 3) ? 'N' : '.',
				BIT(m_ps, 2) ? 'Z' : '.',
				BIT(m_ps, 1) ? 'V' : '.',
				BIT(m_ps, 0) ? 'C' : '.');
		break;
	}
}
