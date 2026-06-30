// license:BSD-3-Clause
// copyright-holders:AJR
/****************************************************************************

    NEC 78K/III series 16/8-bit single-chip microcontrollers

    Instruction execution implements the original uPD78310A/uPD78312A
    78K/III instruction set.

****************************************************************************/

#include "emu.h"
#include "upd78k3.h"
#include "upd78k3d.h"

#include <bit>

namespace {

constexpr u16 PSW_CY  = 0x0001;
constexpr u16 PSW_SUB = 0x0002;
constexpr u16 PSW_PV  = 0x0004;
constexpr u16 PSW_AC  = 0x0010;
constexpr u16 PSW_RSS = 0x0020;
constexpr u16 PSW_Z   = 0x0040;
constexpr u16 PSW_S   = 0x0080;
constexpr u16 PSW_IE  = 0x0200;
constexpr u16 PSW_RBS = 0x7000;

constexpr int saddr_states(u8 address, int ram_states, int sfr_states)
{
	return address < 0x20 ? sfr_states : ram_states;
}

}

// device type definitions
DEFINE_DEVICE_TYPE(UPD78310, upd78310_device, "upd78310", "NEC uPD78310")
DEFINE_DEVICE_TYPE(UPD78312, upd78312_device, "upd78312", "NEC uPD78312")

//**************************************************************************
//  78K/III CORE
//**************************************************************************

//-------------------------------------------------
//  upd78k3_device - constructor
//-------------------------------------------------

upd78k3_device::upd78k3_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock, address_map_constructor mem_map, address_map_constructor sfr_map)
	: cpu_device(mconfig, type, tag, owner, clock)
	, m_program_config("program", ENDIANNESS_LITTLE, 8, 16, 0, mem_map)
	, m_iram_config("iram", ENDIANNESS_LITTLE, 16, 8, 0, address_map_constructor(FUNC(upd78k3_device::iram_map), this))
	, m_sfr_config("sfr", ENDIANNESS_LITTLE, 16, 8, 0, sfr_map)
	, m_iram(*this, "iram")
	, m_pc(0)
	, m_ppc(0)
	, m_psw(0)
	, m_sp(0)
	, m_ccw(0)
	, m_irq_state(0)
	, m_icount(0)
{
}


//-------------------------------------------------
//  iram_map - type-universal IRAM map
//-------------------------------------------------

void upd78k3_device::iram_map(address_map &map)
{
	map(0x00, 0xff).ram().share("iram");
}


//-------------------------------------------------
//  iram_byte_r - read one byte from IRAM
//-------------------------------------------------

u8 upd78k3_device::iram_byte_r(offs_t offset)
{
	if (BIT(offset, 0))
		return (m_iram[offset >> 1] & 0xff00) >> 8;
	else
		return m_iram[offset >> 1] & 0x00ff;
}


//-------------------------------------------------
//  iram_byte_w - write one byte to IRAM
//-------------------------------------------------

void upd78k3_device::iram_byte_w(offs_t offset, u8 data)
{
	if (BIT(offset, 0))
		m_iram[offset >> 1] = (m_iram[offset >> 1] & 0x00ff) | u16(data) << 8;
	else
		m_iram[offset >> 1] = (m_iram[offset >> 1] & 0xff00) | data;
}


//-------------------------------------------------
//  memory_space_config - return a vector of
//  address space configurations for this device
//-------------------------------------------------

device_memory_interface::space_config_vector upd78k3_device::memory_space_config() const
{
	return space_config_vector {
		std::make_pair(AS_PROGRAM, &m_program_config),
		std::make_pair(AS_DATA, &m_iram_config),
		std::make_pair(AS_IO, &m_sfr_config)
	};
}


//-------------------------------------------------
//  register_base - determine current base of
//  register file in IRAM
//-------------------------------------------------

inline u8 upd78k3_device::register_base() const noexcept
{
	return 0x80 | (~m_psw & 0x7000) >> 8;
}


//-------------------------------------------------
//  instruction and operand access helpers
//-------------------------------------------------

u8 upd78k3_device::fetch()
{
	return m_program_cache.read_byte(m_pc++);
}

u16 upd78k3_device::fetch_word()
{
	u16 const result = m_program_cache.read_word(m_pc);
	m_pc += 2;
	return result;
}

u8 upd78k3_device::read_byte(u16 address)
{
	return address >= 0xff00 ? read_sfr(address) : m_program_space.read_byte(address);
}

void upd78k3_device::write_byte(u16 address, u8 data)
{
	if (address >= 0xff00)
		write_sfr(address, data);
	else
		m_program_space.write_byte(address, data);
}

u16 upd78k3_device::read_word(u16 address)
{
	return read_byte(address) | u16(read_byte(address + 1)) << 8;
}

void upd78k3_device::write_word(u16 address, u16 data)
{
	write_byte(address, data);
	write_byte(address + 1, data >> 8);
}

u8 upd78k3_device::read_sfr(u8 address)
{
	if (address == 0x4e)
		return m_ccw;
	if (address == 0xfe)
		return m_psw;
	if (address == 0xff)
		return m_psw >> 8;
	return m_sfr_space.read_byte(address);
}

void upd78k3_device::write_sfr(u8 address, u8 data)
{
	if (address == 0x4e)
		m_ccw = data;
	else if (address == 0xfe)
		m_psw = (m_psw & 0xff00) | data;
	else if (address == 0xff)
		m_psw = (m_psw & 0x00ff) | u16(data & 0x72) << 8;
	else
		m_sfr_space.write_byte(address, data);
}

u16 upd78k3_device::read_sfrp(u8 address)
{
	address &= 0xfe;
	if (address == 0xfc)
		return m_sp;
	if (address == 0xfe)
		return m_psw;
	return read_sfr(address) | u16(read_sfr(address + 1)) << 8;
}

void upd78k3_device::write_sfrp(u8 address, u16 data)
{
	address &= 0xfe;
	if (address == 0xfc)
		m_sp = data;
	else if (address == 0xfe)
		m_psw = data & 0x72ff;
	else
	{
		write_sfr(address, data);
		write_sfr(address + 1, data >> 8);
	}
}

u8 upd78k3_device::read_saddr(u8 address)
{
	return address < 0x20 ? read_sfr(address) : read_byte(0xfe00 | address);
}

void upd78k3_device::write_saddr(u8 address, u8 data)
{
	if (address < 0x20)
		write_sfr(address, data);
	else
		write_byte(0xfe00 | address, data);
}

u16 upd78k3_device::read_saddrp(u8 address)
{
	address &= 0xfe;
	return address < 0x20 ? read_sfrp(address) : read_word(0xfe00 | address);
}

void upd78k3_device::write_saddrp(u8 address, u16 data)
{
	address &= 0xfe;
	if (address < 0x20)
		write_sfrp(address, data);
	else
		write_word(0xfe00 | address, data);
}

u8 upd78k3_device::get_r(unsigned index) const
{
	u16 const word = m_iram[(register_base() >> 1) | ((index & 15) >> 1)];
	return BIT(index, 0) ? word >> 8 : word;
}

void upd78k3_device::set_r(unsigned index, u8 data)
{
	u16 &word = m_iram[(register_base() >> 1) | ((index & 15) >> 1)];
	if (BIT(index, 0))
		word = (word & 0x00ff) | u16(data) << 8;
	else
		word = (word & 0xff00) | data;
}

u16 upd78k3_device::get_rp(unsigned index) const
{
	return m_iram[(register_base() >> 1) | (index & 7)];
}

void upd78k3_device::set_rp(unsigned index, u16 data)
{
	m_iram[(register_base() >> 1) | (index & 7)] = data;
}

void upd78k3_device::set_logic_flags(u8 result)
{
	m_psw &= ~(PSW_S | PSW_Z | PSW_PV | PSW_SUB);
	if (BIT(result, 7))
		m_psw |= PSW_S;
	if (result == 0)
		m_psw |= PSW_Z;
	if (!(std::popcount(result) & 1))
		m_psw |= PSW_PV;
}

u16 upd78k3_device::alu8(unsigned operation, u8 lhs, u8 rhs)
{
	if (operation >= 4 && operation < 7)
	{
		u8 const result = operation == 4 ? lhs & rhs : operation == 5 ? lhs ^ rhs : lhs | rhs;
		set_logic_flags(result);
		return result;
	}

	bool const subtract = operation == 2 || operation == 3 || operation == 7;
	unsigned const carry = (operation == 1 || operation == 3) && BIT(m_psw, 0);
	u16 const wide = subtract ? u16(lhs) - u16(rhs) - carry : u16(lhs) + u16(rhs) + carry;
	u8 const result = wide;
	m_psw &= ~(PSW_S | PSW_Z | PSW_AC | PSW_PV | PSW_SUB | PSW_CY);
	if (BIT(result, 7))
		m_psw |= PSW_S;
	if (result == 0)
		m_psw |= PSW_Z;
	if (subtract)
	{
		m_psw |= PSW_SUB;
		if ((lhs & 0x0f) < ((rhs & 0x0f) + carry))
			m_psw |= PSW_AC;
		if (u16(lhs) < u16(rhs) + carry)
			m_psw |= PSW_CY;
		if (BIT((lhs ^ rhs) & (lhs ^ result), 7))
			m_psw |= PSW_PV;
	}
	else
	{
		if ((lhs & 0x0f) + (rhs & 0x0f) + carry > 0x0f)
			m_psw |= PSW_AC;
		if (wide > 0xff)
			m_psw |= PSW_CY;
		if (BIT(~(lhs ^ rhs) & (lhs ^ result), 7))
			m_psw |= PSW_PV;
	}
	return result;
}

u32 upd78k3_device::alu16(unsigned operation, u16 lhs, u16 rhs)
{
	bool const subtract = operation == 2 || operation == 3;
	u32 const wide = subtract ? u32(lhs) - u32(rhs) : u32(lhs) + u32(rhs);
	u16 const result = wide;
	m_psw &= ~(PSW_S | PSW_Z | PSW_AC | PSW_PV | PSW_SUB | PSW_CY);
	if (BIT(result, 15))
		m_psw |= PSW_S;
	if (result == 0)
		m_psw |= PSW_Z;
	if (subtract)
	{
		m_psw |= PSW_SUB;
		if ((lhs & 0x0f) < (rhs & 0x0f))
			m_psw |= PSW_AC;
		if (lhs < rhs)
			m_psw |= PSW_CY;
		if (BIT((lhs ^ rhs) & (lhs ^ result), 15))
			m_psw |= PSW_PV;
	}
	else
	{
		if ((lhs & 0x0f) + (rhs & 0x0f) > 0x0f)
			m_psw |= PSW_AC;
		if (wide > 0xffff)
			m_psw |= PSW_CY;
		if (BIT(~(lhs ^ rhs) & (lhs ^ result), 15))
			m_psw |= PSW_PV;
	}
	return result;
}

void upd78k3_device::push_word(u16 data)
{
	write_byte(--m_sp, data >> 8);
	write_byte(--m_sp, data);
}

u16 upd78k3_device::pop_word()
{
	u16 const result = read_byte(m_sp) | u16(read_byte(m_sp + 1)) << 8;
	m_sp += 2;
	return result;
}

void upd78k3_device::illegal(u8 op1, int op2)
{
	if (op2 < 0)
		logerror("%s: illegal opcode %02X at %04X\n", machine().describe_context(), op1, m_ppc);
	else
		logerror("%s: illegal opcode %02X %02X at %04X\n", machine().describe_context(), op1, op2, m_ppc);
	m_icount -= 3;
}


//-------------------------------------------------
//  prefixed opcode groups
//-------------------------------------------------

void upd78k3_device::execute_01(u8 op2)
{
	if (op2 >= 0x0d && op2 < 0x10)
	{
		u8 const address = fetch();
		u16 const rhs = fetch_word();
		u16 const lhs = read_sfrp(address);
		u16 const result = alu16(op2 & 3, lhs, rhs);
		if ((op2 & 3) != 3)
			write_sfrp(address, result);
		m_icount -= 10;
	}
	else if (op2 == 0x1b)
	{
		u8 const address = fetch();
		u16 const data = read_sfrp(address);
		write_sfrp(address, get_rp(ax_index()));
		set_rp(ax_index(), data);
		m_icount -= 9;
	}
	else if (op2 >= 0x1d && op2 < 0x20)
	{
		u8 const address = fetch();
		u16 const lhs = get_rp(ax_index());
		u16 const result = alu16(op2 & 3, lhs, read_sfrp(address));
		if ((op2 & 3) != 3)
			set_rp(ax_index(), result);
		m_icount -= 8;
	}
	else if (op2 == 0x21)
	{
		u8 const address = fetch();
		u8 const data = read_sfr(address);
		write_sfr(address, get_r(a_index()));
		set_r(a_index(), data);
		m_icount -= 8;
	}
	else if ((op2 & 0xf8) == 0x68)
	{
		u8 const address = fetch();
		u8 const immediate = fetch();
		u8 const result = alu8(op2 & 7, read_sfr(address), immediate);
		if ((op2 & 7) != 7)
			write_sfr(address, result);
		m_icount -= 10;
	}
	else if ((op2 & 0xf8) == 0x98)
	{
		u8 const address = fetch();
		u8 const result = alu8(op2 & 7, get_r(a_index()), read_sfr(address));
		if ((op2 & 7) != 7)
			set_r(a_index(), result);
		m_icount -= 7;
	}
	else
		illegal(0x01, op2);
}

void upd78k3_device::execute_02(u8 op1, u8 op2)
{
	bool const accumulator = BIT(op1, 0);
	auto read_bit = [&]() -> bool
	{
		if (accumulator)
			return BIT(get_r(BIT(op2, 3) ? a_index() : x_index()), op2 & 7);
		return BIT(m_psw, op2 & 15);
	};
	auto write_bit = [&](bool state)
	{
		if (accumulator)
		{
			unsigned const index = BIT(op2, 3) ? a_index() : x_index();
			u8 data = get_r(index);
			if (state) data |= 1U << (op2 & 7); else data &= ~(1U << (op2 & 7));
			set_r(index, data);
		}
		else if (state)
			m_psw |= 1U << (op2 & 15);
		else
			m_psw &= ~(1U << (op2 & 15));
	};

	if ((op2 & 0xf0) == 0x10)
		write_bit(BIT(m_psw, 0));
	else if (op2 < 0x70)
	{
		bool bit = read_bit();
		if (BIT(op2, 4)) bit = !bit;
		bool const carry = BIT(m_psw, 0);
		switch ((op2 >> 5) & 3)
		{
		case 0: if (bit) m_psw |= PSW_CY; else m_psw &= ~PSW_CY; break;
		case 1: if (!(carry && bit)) m_psw &= ~PSW_CY; break;
		case 2: if (carry || bit) m_psw |= PSW_CY; else m_psw &= ~PSW_CY; break;
		case 3: if (carry != bit) m_psw |= PSW_CY; else m_psw &= ~PSW_CY; break;
		}
	}
	else if (op2 < 0xa0)
	{
		if (op2 < 0x80)
			write_bit(!read_bit());
		else
			write_bit(!BIT(op2, 4));
	}
	else if (op2 < 0xe0)
	{
		s8 const displacement = fetch();
		bool const bit = read_bit();
		bool const branch = BIT(op2, 4) ? bit : !bit;
		if (branch)
		{
			m_pc += displacement;
			if (BIT(op2, 6))
				write_bit(!BIT(op2, 4));
		}
		m_icount -= branch ? (BIT(op2, 6) ? (accumulator ? 11 : 12) : 10) : 7;
		return;
	}
	else
	{
		illegal(op1, op2);
		return;
	}
	if ((op2 & 0xf0) == 0x10)
		m_icount -= accumulator ? 8 : 9;
	else if (op2 < 0x70)
		m_icount -= 6;
	else
		m_icount -= accumulator ? 7 : 8;
}

void upd78k3_device::execute_05(u8 op2)
{
	// The original 7831x instruction set has ADJ4 at opcode 04 and does
	// not implement the later MULSW or ADJBA/ADJBS encodings.
	if (op2 >= 0xfe || (op2 & 0xf0) == 0x30)
	{
		illegal(0x05, op2);
		return;
	}
	unsigned const index = ((op2 & 6) >> 1) | ((op2 & 1) << 2);
	if ((op2 & 0x88) == 0x08)
	{
		if (BIT(op2, 6))
		{
			u16 const destination = BIT(op2, 5) ? read_word(get_rp(index)) : get_rp(index);
			if (BIT(op2, 4))
				push_word(m_pc);
			m_pc = destination;
			m_icount -= BIT(op2, 4) ? (BIT(op2, 5) ? 11 : 8) : (BIT(op2, 5) ? 8 : 5);
		}
		else if (BIT(op2, 5))
		{
			if (BIT(op2, 4))
			{
				s32 const product = s16(get_rp(ax_index())) * s16(get_rp(index));
				set_rp(ax_index(), product >> 16);
				set_rp(index, product);
			}
			else
			{
				u32 const product = u32(get_rp(ax_index())) * get_rp(index);
				set_rp(ax_index(), product >> 16);
				set_rp(index, product);
			}
			m_icount -= 27;
		}
		else if (BIT(op2, 4))
		{
			u8 const divisor = get_r(op2 & 7);
			if (divisor)
			{
				u16 const dividend = get_rp(ax_index());
				set_rp(ax_index(), dividend / divisor);
				set_r(op2 & 7, dividend % divisor);
			}
			m_icount -= 26;
		}
		else
		{
			set_rp(ax_index(), u16(get_r(a_index())) * get_r(op2 & 7));
			m_icount -= 18;
		}
	}
	else if ((op2 & 0xc8) == 0x88)
	{
		if (BIT(op2, 5))
		{
			m_psw = (m_psw & ~(PSW_RBS | PSW_RSS)) | u16(op2 & 7) << 12;
			if (BIT(op2, 4)) m_psw |= PSW_RSS;
			m_icount -= 3;
		}
		else
		{
			u16 const address = get_rp(index);
			u8 const memory = read_byte(address);
			u8 const a = get_r(a_index());
			if (BIT(op2, 4))
			{
				set_r(a_index(), (a & 0xf0) | (memory >> 4));
				write_byte(address, (memory << 4) | (a & 0x0f));
			}
			else
			{
				set_r(a_index(), (a & 0xf0) | (memory & 0x0f));
				write_byte(address, (a << 4) | (memory >> 4));
			}
			m_icount -= 7;
		}
	}
	else if ((op2 & 0xfe) == 0xc8)
	{
		if (BIT(op2, 0)) --m_sp; else ++m_sp;
		m_icount -= 5;
	}
	else if ((op2 & 0xf8) == 0xd8)
	{
		unsigned const bank = op2 & 7;
		u16 const old_psw = m_psw;
		u16 const old_pc = m_pc;
		m_psw = (m_psw & ~PSW_RBS) | u16(bank) << 12;
		u16 const destination = get_rp(2);
		set_rp(2, old_pc);
		set_rp(3, old_psw);
		m_psw &= ~(PSW_RSS | PSW_IE);
		m_pc = destination;
		m_icount -= 12;
	}
	else if ((op2 & 0xf8) == 0xe8)
	{
		u16 const divisor = get_rp(index);
		if (divisor)
		{
			u32 const dividend = u32(get_rp(ax_index())) << 16 | get_rp(6);
			set_rp(ax_index(), dividend / divisor >> 16);
			set_rp(6, dividend / divisor);
			set_rp(index, dividend % divisor);
		}
		m_icount -= 50;
	}
	else
		illegal(0x05, op2);
}

void upd78k3_device::execute_06(u8 op2)
{
	if ((op2 & 0x0b) == 0x01 || (op2 & 0x70) >= 0x50 || (!BIT(op2, 3) && (op2 & (BIT(op2, 7) ? 0x06 : 0x02)) != 0))
	{
		illegal(0x06, op2);
		return;
	}
	static constexpr unsigned rp_index[5] = { 6, 0, 7, 5, 4 };
	u8 const displacement = fetch();
	u16 const address = get_rp(rp_index[(op2 >> 4) & 7]) + displacement;
	if (BIT(op2, 3))
	{
		u8 const lhs = BIT(op2, 7) ? read_byte(address) : get_r(a_index());
		u8 const rhs = BIT(op2, 7) ? get_r(a_index()) : read_byte(address);
		u8 const result = alu8(op2 & 7, lhs, rhs);
		if ((op2 & 7) != 7)
		{
			if (BIT(op2, 7)) write_byte(address, result); else set_r(a_index(), result);
		}
	}
	else if (BIT(op2, 2))
	{
		u8 const data = read_byte(address);
		write_byte(address, get_r(a_index()));
		set_r(a_index(), data);
	}
	else if (BIT(op2, 7))
		write_byte(address, get_r(a_index()));
	else
		set_r(a_index(), read_byte(address));
	m_icount -= BIT(op2, 3) ? (BIT(op2, 7) ? 8 : 7) : (BIT(op2, 2) ? 8 : 6);
}

void upd78k3_device::execute_07(u8 op2)
{
	if (op2 >= 0xf8 && op2 <= 0xfd)
	{
		s8 const displacement = fetch();
		bool const sv = BIT(m_psw, 7) != BIT(m_psw, 2);
		bool branch = false;
		switch (op2 & 7)
		{
		case 0: branch = sv; break;
		case 1: branch = !sv; break;
		case 2: branch = BIT(m_psw, 6) || sv; break;
		case 3: branch = !BIT(m_psw, 6) && !sv; break;
		case 4: branch = BIT(m_psw, 0) || BIT(m_psw, 6); break;
		case 5: branch = !BIT(m_psw, 0) && !BIT(m_psw, 6); break;
		}
		if (branch) m_pc += displacement;
		m_icount -= branch ? 9 : 5;
	}
	else if ((op2 & 0xce) == 0xc8 && BIT(op2, 5))
	{
		u8 const address = fetch();
		u16 data = read_saddrp(address);
		if (BIT(op2, 0)) --data; else ++data;
		write_saddrp(address, data);
		m_icount -= saddr_states(address, 6, 8);
	}
	else
		illegal(0x07, op2);
}

void upd78k3_device::execute_08(u8 op2)
{
	u8 const address = fetch();
	auto read_target = [&]() { return BIT(op2, 3) ? read_sfr(address) : read_saddr(address); };
	auto write_target = [&](u8 data) { if (BIT(op2, 3)) write_sfr(address, data); else write_saddr(address, data); };
	auto read_bit = [&]() { return BIT(read_target(), op2 & 7); };
	auto write_bit = [&](bool state)
	{
		u8 data = read_target();
		if (state) data |= 1U << (op2 & 7); else data &= ~(1U << (op2 & 7));
		write_target(data);
	};

	if ((op2 & 0xf0) == 0x10)
		write_bit(BIT(m_psw, 0));
	else if (op2 < 0x70)
	{
		bool bit = read_bit();
		if (BIT(op2, 4)) bit = !bit;
		bool const carry = BIT(m_psw, 0);
		switch ((op2 >> 5) & 3)
		{
		case 0: if (bit) m_psw |= PSW_CY; else m_psw &= ~PSW_CY; break;
		case 1: if (!(carry && bit)) m_psw &= ~PSW_CY; break;
		case 2: if (carry || bit) m_psw |= PSW_CY; else m_psw &= ~PSW_CY; break;
		case 3: if (carry != bit) m_psw |= PSW_CY; else m_psw &= ~PSW_CY; break;
		}
	}
	else if (op2 < (BIT(op2, 3) ? 0xa0 : 0x80))
	{
		if (op2 < 0x80) write_bit(!read_bit()); else write_bit(!BIT(op2, 4));
	}
	else if (op2 < 0xe0)
	{
		s8 const displacement = fetch();
		bool const bit = read_bit();
		bool const branch = BIT(op2, 4) ? bit : !bit;
		if (branch)
		{
			m_pc += displacement;
			if (BIT(op2, 6)) write_bit(!BIT(op2, 4));
		}
		bool const explicit_sfr = BIT(op2, 3);
		bool const short_sfr = !explicit_sfr && address < 0x20;
		if (BIT(op2, 6))
			m_icount -= branch ? (explicit_sfr || short_sfr ? 14 : 12) : (explicit_sfr || short_sfr ? 8 : 7);
		else
			m_icount -= branch ? (explicit_sfr ? 11 : short_sfr ? 10 : 9) : (explicit_sfr ? 8 : short_sfr ? 7 : 6);
		return;
	}
	else
	{
		illegal(0x08, op2);
		return;
	}
	bool const explicit_sfr = BIT(op2, 3);
	bool const short_sfr = !explicit_sfr && address < 0x20;
	if ((op2 & 0xf0) == 0x10)
		m_icount -= explicit_sfr ? 8 : short_sfr ? 8 : 7;
	else if (op2 < 0x70)
		m_icount -= explicit_sfr ? 7 : short_sfr ? 7 : 6;
	else
		m_icount -= explicit_sfr ? 8 : short_sfr ? 7 : 5;
}

void upd78k3_device::execute_09(u8 op2)
{
	if (op2 == 0x42 || op2 == 0x44)
	{
		u8 const negative = fetch();
		u8 const positive = fetch();
		if (u8(~negative) == positive)
			write_sfr(op2, positive);
		m_icount -= 6;
	}
	else if ((op2 & 0xe8) == 0x80)
	{
		unsigned const index = ((op2 & 6) >> 1) | ((op2 & 1) << 2);
		u16 const address = fetch_word();
		if (BIT(op2, 4)) write_word(address, get_rp(index)); else set_rp(index, read_word(address));
		m_icount -= BIT(op2, 4) ? 4 : 10;
	}
	else if ((op2 & 0xfe) == 0xf0)
	{
		u16 const address = fetch_word();
		if (BIT(op2, 0)) write_byte(address, get_r(a_index())); else set_r(a_index(), read_byte(address));
		m_icount -= BIT(op2, 0) ? 4 : 5;
	}
	else
		illegal(0x09, op2);
}

void upd78k3_device::execute_0a(u8 op2)
{
	if ((op2 & 0x0b) == 0x01 || BIT(op2, 6) || (!BIT(op2, 3) && (op2 & (BIT(op2, 7) ? 0x06 : 0x02)) != 0))
	{
		illegal(0x0a, op2);
		return;
	}
	u16 const displacement = fetch_word();
	u16 address;
	if (BIT(op2, 4))
		address = displacement + get_r(BIT(op2, 5) ? b_index() : a_index());
	else
		address = displacement + get_rp(BIT(op2, 5) ? 7 : 6);

	if (BIT(op2, 3))
	{
		u8 const lhs = BIT(op2, 7) ? read_byte(address) : get_r(a_index());
		u8 const rhs = BIT(op2, 7) ? get_r(a_index()) : read_byte(address);
		u8 const result = alu8(op2 & 7, lhs, rhs);
		if ((op2 & 7) != 7)
		{
			if (BIT(op2, 7)) write_byte(address, result); else set_r(a_index(), result);
		}
	}
	else if (BIT(op2, 2))
	{
		u8 const data = read_byte(address);
		write_byte(address, get_r(a_index()));
		set_r(a_index(), data);
	}
	else if (BIT(op2, 7))
		write_byte(address, get_r(a_index()));
	else
		set_r(a_index(), read_byte(address));
	m_icount -= BIT(op2, 3) ? (BIT(op2, 7) ? 8 : 7) : (BIT(op2, 2) ? 8 : 6);
}

void upd78k3_device::execute_15(u8 op2)
{
	if ((op2 & 0xc8) != 0 || (op2 & 0x06) == 0x02)
	{
		illegal(0x15, op2);
		return;
	}
	bool const decrement = BIT(op2, 4);
	bool const block = BIT(op2, 5);
	bool const compare = BIT(op2, 2);
	unsigned iterations = 0;
	do
	{
		u16 const de = get_rp(6);
		u16 const hl = get_rp(7);
		if (compare)
			alu8(7, read_byte(de), block ? read_byte(hl) : get_r(a_index()));
		else if (block)
		{
			u8 const lhs = read_byte(de);
			u8 const rhs = read_byte(hl);
			if (BIT(op2, 0))
			{
				write_byte(de, rhs);
				write_byte(hl, lhs);
			}
			else
				write_byte(de, rhs);
		}
		else if (BIT(op2, 0))
		{
			u8 const data = read_byte(de);
			write_byte(de, get_r(a_index()));
			set_r(a_index(), data);
		}
		else
			write_byte(de, get_r(a_index()));

		set_rp(6, de + (decrement ? -1 : 1));
		if (block) set_rp(7, hl + (decrement ? -1 : 1));
		u8 const count = get_r(c_index()) - 1;
		set_r(c_index(), count);
		++iterations;
		if (!count)
			break;
		if (compare)
		{
			bool repeat;
			switch (op2 & 3)
			{
			case 0: repeat = BIT(m_psw, 6); break;
			case 1: repeat = !BIT(m_psw, 6); break;
			case 2: repeat = !BIT(m_psw, 0); break;
			default: repeat = BIT(m_psw, 0); break;
			}
			if (!repeat) break;
		}
	} while (true);
	unsigned const per_iteration = block ? (compare || !BIT(op2, 0) ? 10 : 15) : (compare || !BIT(op2, 0) ? 7 : 12);
	m_icount -= 2 + per_iteration * iterations;
}

void upd78k3_device::execute_16(u8 op1, u8 op2)
{
	if ((op2 & 0x0b) == 0x01 || (BIT(op1, 0) && (op2 & 0x60) == 0x60) || (!BIT(op2, 3) && (op2 & (BIT(op2, 7) ? 0x06 : 0x02)) != 0))
	{
		illegal(op1, op2);
		return;
	}
	u16 address;
	int post_adjust = 0;
	if (BIT(op1, 0) && (op2 & 0x60) == 0x40)
		address = get_rp(4) + get_rp(BIT(op2, 4) ? 7 : 6);
	else if ((op2 & 0x60) == 0x60)
		address = get_rp(BIT(op2, 4) ? 5 : 4);
	else
	{
		unsigned const base = BIT(op2, 4) ? 7 : 6;
		address = get_rp(base);
		if (!BIT(op2, 6))
		{
			if (BIT(op1, 0))
				address += get_r(BIT(op2, 5) ? b_index() : a_index());
			else
				post_adjust = BIT(op2, 5) ? -1 : 1;
		}
	}

	if (BIT(op2, 3))
	{
		u8 const lhs = BIT(op2, 7) ? read_byte(address) : get_r(a_index());
		u8 const rhs = BIT(op2, 7) ? get_r(a_index()) : read_byte(address);
		u8 const result = alu8(op2 & 7, lhs, rhs);
		if ((op2 & 7) != 7)
		{
			if (BIT(op2, 7)) write_byte(address, result); else set_r(a_index(), result);
		}
	}
	else if (BIT(op2, 2))
	{
		u8 const data = read_byte(address);
		write_byte(address, get_r(a_index()));
		set_r(a_index(), data);
	}
	else if (BIT(op2, 7))
		write_byte(address, get_r(a_index()));
	else
		set_r(a_index(), read_byte(address));

	if (post_adjust)
		set_rp(BIT(op2, 4) ? 7 : 6, address + post_adjust);
	m_icount -= BIT(op2, 3) ? (BIT(op2, 7) ? 7 : 6) : (BIT(op2, 2) ? 7 : 5);
}


//-------------------------------------------------
//  execute_one - decode and execute one instruction
//-------------------------------------------------

void upd78k3_device::execute_one()
{
	u8 const op = fetch();
	switch (op & 0xf8)
	{
	case 0x00:
		switch (op)
		{
		case 0x00: m_icount -= 3; break;
		case 0x01: execute_01(fetch()); break;
		case 0x02: case 0x03: execute_02(op, fetch()); break;
		case 0x04:
		{
			u8 a = get_r(a_index());
			bool carry = BIT(m_psw, 0);
			bool const subtract = BIT(m_psw, 1);
			if (!subtract)
			{
				u8 adjust = ((a & 0x0f) > 9 || BIT(m_psw, 4)) ? 0x06 : 0;
				if (a > 0x99 || carry) { adjust |= 0x60; carry = true; }
				a += adjust;
			}
			else
				a -= (BIT(m_psw, 4) ? 0x06 : 0) | (carry ? 0x60 : 0);
			set_r(a_index(), a);
			set_logic_flags(a);
			if (subtract) m_psw |= PSW_SUB;
			if (carry) m_psw |= PSW_CY; else m_psw &= ~PSW_CY;
			m_icount -= 3;
			break;
		}
		case 0x05: execute_05(fetch()); break;
		case 0x06: execute_06(fetch()); break;
		case 0x07: execute_07(fetch()); break;
		}
		break;

	case 0x08:
		switch (op)
		{
		case 0x08: execute_08(fetch()); break;
		case 0x09: execute_09(fetch()); break;
		case 0x0a: execute_0a(fetch()); break;
		default:
		{
			u8 const address = fetch();
			u16 const immediate = fetch_word();
			if (BIT(op, 2))
			{
				u16 const lhs = read_saddrp(address);
				u16 const result = (op & 3) ? alu16(op & 3, lhs, immediate) : immediate;
				if ((op & 3) != 3) write_saddrp(address, result);
			}
			else
				write_sfrp(address, immediate);
			if (op == 0x0b)
				m_icount -= 4;
			else if (op == 0x0c)
				m_icount -= saddr_states(address, 3, 4);
			else if (op == 0x0f)
				m_icount -= saddr_states(address, 4, 5);
			else
				m_icount -= saddr_states(address, 5, 7);
			break;
		}
		}
		break;

	case 0x10:
		if (!BIT(op, 2))
		{
			u8 const address = fetch();
			if (BIT(op, 0))
			{
				if (BIT(op, 1)) write_sfrp(address, get_rp(ax_index())); else set_rp(ax_index(), read_sfrp(address));
			}
			else if (BIT(op, 1))
				write_sfr(address, get_r(a_index()));
			else
				set_r(a_index(), read_sfr(address));
			m_icount -= 4;
		}
		else if (BIT(op, 1))
			execute_16(op, fetch());
		else if (BIT(op, 0))
			execute_15(fetch());
		else
		{
			s8 const displacement = fetch();
			m_pc += displacement;
			m_icount -= 7;
		}
		break;

	case 0x18:
	{
		u8 const address = fetch();
		if (BIT(op, 2))
		{
			u16 const rhs = read_saddrp(address);
			u16 const result = (op & 3) ? alu16(op & 3, get_rp(ax_index()), rhs) : rhs;
			if ((op & 3) != 3) set_rp(ax_index(), result);
		}
		else if (BIT(op, 1))
		{
			if (BIT(op, 0))
			{
				u16 const data = read_saddrp(address);
				write_saddrp(address, get_rp(ax_index()));
				set_rp(ax_index(), data);
			}
			else
				write_saddrp(address, get_rp(ax_index()));
		}
		else
		{
			u16 const indirect = read_saddrp(address);
			if (BIT(op, 0)) write_byte(indirect, get_r(a_index())); else set_r(a_index(), read_byte(indirect));
		}
		if (op < 0x1a)
			m_icount -= BIT(op, 0) ? saddr_states(address, 4, 5) : saddr_states(address, 5, 6);
		else if (op == 0x1a || op == 0x1c)
			m_icount -= saddr_states(address, 3, 4);
		else if (op == 0x1b)
			m_icount -= saddr_states(address, 4, 6);
		else
			m_icount -= saddr_states(address, 4, 5);
		break;
	}

	case 0x20:
		if (BIT(op, 2))
		{
			u8 const post = fetch();
			if (BIT(op, 1))
			{
				u8 const old_carry = m_psw & PSW_CY;
				u8 const data = read_saddr(post);
				u8 const result = alu8(BIT(op, 0) ? 2 : 0, data, 1);
				write_saddr(post, result);
				m_psw = (m_psw & ~PSW_CY) | old_carry;
				m_icount -= saddr_states(post, 4, 6);
			}
			else if (!BIT(post, 3))
			{
				unsigned const dst = post >> 4;
				unsigned const src = post & 7;
				if (BIT(op, 0))
				{
					u8 const data = get_r(dst);
					set_r(dst, get_r(src));
					set_r(src, data);
				}
				else set_r(dst, get_r(src));
				m_icount -= BIT(op, 0) ? 4 : 3;
			}
			else if (!BIT(post, 4))
			{
				unsigned const dst = post >> 5;
				unsigned const src = ((post & 6) >> 1) | ((post & 1) << 2);
				if (BIT(op, 0))
				{
					u16 const data = get_rp(dst);
					set_rp(dst, get_rp(src));
					set_rp(src, data);
				}
				else set_rp(dst, get_rp(src));
				m_icount -= BIT(op, 0) ? 5 : 3;
			}
			else illegal(op, post);
		}
		else
		{
			u8 const address = fetch();
			if (op == 0x23)
				set_r(a_index(), read_byte(read_saddrp(address)));
			else if (op == 0x22)
				write_saddr(address, get_r(a_index()));
			else if (op == 0x21)
			{
				u8 const data = read_saddr(address);
				write_saddr(address, get_r(a_index()));
				set_r(a_index(), data);
			}
			else
				set_r(a_index(), read_saddr(address));
			if (op == 0x23)
				m_icount -= saddr_states(address, 5, 6);
			else if (op == 0x21)
				m_icount -= saddr_states(address, 4, 6);
			else
				m_icount -= saddr_states(address, 3, 4);
		}
		break;

	case 0x28:
		switch (op)
		{
		case 0x28:
		{
			u16 const destination = fetch_word();
			push_word(m_pc);
			m_pc = destination;
			m_icount -= 8;
			break;
		}
		case 0x29:
		{
			u16 const replacement = fetch_word();
			u16 const destination = get_rp(2);
			u16 const psw = get_rp(3);
			set_rp(2, replacement);
			m_psw = psw & 0x72ff;
			m_ccw &= ~0x01;
			m_pc = destination;
			m_icount -= 6;
			break;
		}
		case 0x2a:
		{
			u8 const source = fetch();
			u8 const destination = fetch();
			u16 const data = read_saddrp(destination);
			write_saddrp(destination, read_saddrp(source));
			write_saddrp(source, data);
			m_icount -= (source < 0x20 || destination < 0x20) ? 12 : 8;
			break;
		}
		case 0x2b:
		{
			u8 const address = fetch();
			write_sfr(address, fetch());
			m_icount -= 4;
			break;
		}
		case 0x2c: m_pc = fetch_word(); m_icount -= 4; break;
		default:
		{
			u16 const immediate = fetch_word();
			u16 const result = alu16(op & 3, get_rp(ax_index()), immediate);
			if ((op & 3) != 3) set_rp(ax_index(), result);
			m_icount -= 4;
			break;
		}
		}
		break;

	case 0x30:
		if (BIT(op, 2))
		{
			u8 post = fetch();
			bool const user = BIT(op, 1);
			bool const push = BIT(op, 0);
			u16 pointer = user ? get_rp(5) : m_sp;
			auto push_to = [&](u16 data) { write_byte(--pointer, data >> 8); write_byte(--pointer, data); };
			auto pop_from = [&]() { u16 const data = read_byte(pointer) | u16(read_byte(pointer + 1)) << 8; pointer += 2; return data; };
			if (push)
			{
				for (int n = 7; n >= 0; --n) if (BIT(post, n)) push_to(user && n == 5 ? m_psw : get_rp(n));
			}
			else
			{
				for (int n = 0; n < 8; ++n) if (BIT(post, n))
				{
					u16 const data = pop_from();
					if (user && n == 5) m_psw = data & 0x72ff; else set_rp(n, data);
				}
			}
			if (user) set_rp(5, pointer); else m_sp = pointer;
			m_icount -= 4 + (push ? 4 : 5) * std::popcount(post);
		}
		else if (BIT(op, 1))
		{
			s8 const displacement = fetch();
			unsigned const index = BIT(op, 0) ? b_index() : c_index();
			u8 const result = get_r(index) - 1;
			set_r(index, result);
			if (result) m_pc += displacement;
			m_icount -= result ? 8 : 5;
		}
		else
		{
			u8 const post = fetch();
			unsigned const count = (post >> 3) & 7;
			bool const word = post >= 0xc0;
			unsigned const index = word ? ((post & 6) >> 1) | ((post & 1) << 2) : post & 7;
			u16 data = word ? get_rp(index) : get_r(index);
			unsigned const width = word ? 16 : 8;
			unsigned const kind = post >> 6;
			for (unsigned n = 0; n < count; ++n)
			{
				bool const outgoing = BIT(data, BIT(op, 0) ? width - 1 : 0);
				if (BIT(op, 0))
					data = (data << 1) | (kind < 2 ? (kind ? outgoing : BIT(m_psw, 0)) : 0);
				else
					data = (data >> 1) | (kind < 2 ? (kind ? outgoing : BIT(m_psw, 0)) << (width - 1) : 0);
				if (outgoing) m_psw |= PSW_CY; else m_psw &= ~PSW_CY;
			}
			if (word)
			{
				set_rp(index, data);
				if (count)
				{
					m_psw &= ~(PSW_S | PSW_Z | PSW_PV | PSW_SUB);
					if (BIT(data, 15)) m_psw |= PSW_S;
					if (!data) m_psw |= PSW_Z;
					if (!(std::popcount(u8(data)) & 1)) m_psw |= PSW_PV;
				}
			}
			else
			{
				set_r(index, data);
				if (count) set_logic_flags(data);
			}
			m_icount -= 4 + 3 * count;
		}
		break;

	case 0x38:
		if (BIT(op, 2))
		{
			u8 const source = fetch();
			u8 const destination = fetch();
			u16 const lhs = read_saddrp(destination);
			u16 const result = (op & 3) ? alu16(op & 3, lhs, read_saddrp(source)) : read_saddrp(source);
			if ((op & 3) != 3) write_saddrp(destination, result);
			bool const sfr = source < 0x20 || destination < 0x20;
			if (op == 0x3c)
				m_icount -= sfr ? 6 : 4;
			else if (op == 0x3d)
				m_icount -= sfr ? 9 : 6;
			else if (op == 0x3e)
				m_icount -= sfr ? 9 : 6;
			else
				m_icount -= sfr ? 7 : 5;
		}
		else if (BIT(op, 1))
		{
			u8 const address = fetch();
			if (BIT(op, 0))
			{
				s8 const displacement = fetch();
				u8 const result = read_saddr(address) - 1;
				write_saddr(address, result);
				if (result) m_pc += displacement;
				m_icount -= result ? saddr_states(address, 9, 11) : saddr_states(address, 6, 8);
			}
			else
			{
				write_saddr(address, fetch());
				m_icount -= saddr_states(address, 3, 4);
			}
		}
		else
		{
			u8 const source = fetch();
			u8 const destination = fetch();
			u8 const data = read_saddr(destination);
			write_saddr(destination, read_saddr(source));
			if (BIT(op, 0)) write_saddr(source, data);
			if (BIT(op, 0))
				m_icount -= (source < 0x20 || destination < 0x20) ? 12 : 8;
			else
				m_icount -= (source < 0x20 || destination < 0x20) ? 6 : 4;
		}
		break;

	case 0x40:
		if (BIT(op, 2)) { set_rp(op & 7, get_rp(op & 7) + 1); m_icount -= 3; }
		else if (op == 0x43) { m_psw ^= PSW_RSS; m_icount -= 3; }
		else if (op == 0x42) { m_psw ^= PSW_CY; m_icount -= 3; }
		else { if (BIT(op, 0)) m_psw |= PSW_CY; else m_psw &= ~PSW_CY; m_icount -= 3; }
		break;

	case 0x48:
		if (BIT(op, 2)) { set_rp(op & 7, get_rp(op & 7) - 1); m_icount -= 3; }
		else if (BIT(op, 1)) { if (BIT(op, 0)) m_psw |= PSW_IE; else m_psw &= ~PSW_IE; m_icount -= 3; }
		else if (BIT(op, 0)) { push_word(m_psw); m_icount -= 5; }
		else { m_psw = pop_word() & 0x72ff; m_icount -= 6; }
		break;

	case 0x50: case 0x58:
		if ((op & 6) == 6)
		{
			if (op == 0x56) { m_pc = pop_word(); m_icount -= 8; }
			else if (op == 0x57) { m_pc = pop_word(); m_psw = pop_word() & 0x72ff; m_ccw &= ~0x01; m_icount -= 14; }
			else if (op == 0x5e)
			{
				push_word(m_psw);
				push_word(m_pc);
				m_psw &= ~PSW_IE;
				u16 const table = BIT(m_ccw, 1) ? 0x8000 : 0;
				m_pc = read_word(table | 0x003e);
				m_icount -= 20;
			}
			else illegal(op);
		}
		else
		{
			unsigned const index = BIT(op, 0) ? 7 : 6;
			u16 const address = get_rp(index);
			if (BIT(op, 3)) write_byte(address, get_r(a_index())); else set_r(a_index(), read_byte(address));
			if (!BIT(op, 2)) set_rp(index, address + (BIT(op, 1) ? -1 : 1));
			m_icount -= 5;
		}
		break;

	case 0x60:
		set_rp(((op & 6) >> 1) | ((op & 1) << 2), fetch_word());
		m_icount -= 3;
		break;

	case 0x68:
	{
		u8 const address = fetch();
		u8 const result = alu8(op & 7, read_saddr(address), fetch());
		if ((op & 7) != 7) write_saddr(address, result);
		m_icount -= saddr_states(address, 5, 7);
		break;
	}

	case 0x70:
	{
		u8 const address = fetch();
		s8 const displacement = fetch();
		bool const branch = BIT(read_saddr(address), op & 7);
		if (branch) m_pc += displacement;
		m_icount -= branch ? saddr_states(address, 9, 10) : saddr_states(address, 6, 7);
		break;
	}

	case 0x78:
	{
		u8 const source = fetch();
		u8 const destination = fetch();
		u8 const result = alu8(op & 7, read_saddr(destination), read_saddr(source));
		if ((op & 7) != 7) write_saddr(destination, result);
		m_icount -= (source < 0x20 || destination < 0x20) ? 9 : 6;
		break;
	}

	case 0x80:
	{
		s8 const displacement = fetch();
		bool branch = false;
		switch (op & 7)
		{
		case 0: branch = !BIT(m_psw, 6); break;
		case 1: branch = BIT(m_psw, 6); break;
		case 2: branch = !BIT(m_psw, 0); break;
		case 3: branch = BIT(m_psw, 0); break;
		case 4: branch = !BIT(m_psw, 2); break;
		case 5: branch = BIT(m_psw, 2); break;
		case 6: branch = BIT(m_psw, 7); break;
		case 7: branch = !BIT(m_psw, 7); break;
		}
		if (branch) m_pc += displacement;
		m_icount -= branch ? 7 : 3;
		break;
	}

	case 0x88:
	{
		u8 const post = fetch();
		if (!BIT(post, 3))
		{
			unsigned const dst = post >> 4;
			unsigned const src = post & 7;
			u8 const result = alu8(op & 7, get_r(dst), get_r(src));
			if ((op & 7) != 7) set_r(dst, result);
			m_icount -= 3;
		}
		else if (!BIT(post, 4) && (op == 0x88 || op == 0x8a || op == 0x8f))
		{
			unsigned const dst = post >> 5;
			unsigned const src = ((post & 6) >> 1) | ((post & 1) << 2);
			u16 const result = alu16(op == 0x88 ? 1 : op == 0x8a ? 2 : 3, get_rp(dst), get_rp(src));
			if (op != 0x8f) set_rp(dst, result);
			m_icount -= 4;
		}
		else illegal(op, post);
		break;
	}

	case 0x90:
	{
		u16 const destination = 0x0800 | u16(op & 7) << 8 | fetch();
		push_word(m_pc);
		m_pc = destination;
		m_icount -= 8;
		break;
	}

	case 0x98:
	{
		u8 const address = fetch();
		u8 const result = alu8(op & 7, get_r(a_index()), read_saddr(address));
		if ((op & 7) != 7) set_r(a_index(), result);
		m_icount -= saddr_states(address, 3, 4);
		break;
	}

	case 0xa0: case 0xb0:
	{
		u8 const address = fetch();
		u8 data = read_saddr(address);
		if (BIT(op, 4)) data |= 1U << (op & 7); else data &= ~(1U << (op & 7));
		write_saddr(address, data);
		m_icount -= saddr_states(address, 5, 7);
		break;
	}

	case 0xa8:
	{
		u8 const result = alu8(op & 7, get_r(a_index()), fetch());
		if ((op & 7) != 7) set_r(a_index(), result);
		m_icount -= 3;
		break;
	}

	case 0xb8: set_r(op & 7, fetch()); m_icount -= 3; break;
	case 0xc0: case 0xc8:
	{
		unsigned const index = op & 7;
		u8 const old_carry = m_psw & PSW_CY;
		set_r(index, alu8(BIT(op, 3) ? 2 : 0, get_r(index), 1));
		m_psw = (m_psw & ~PSW_CY) | old_carry;
		m_icount -= 3;
		break;
	}
	case 0xd0: set_r(a_index(), get_r(op & 7)); m_icount -= 3; break;
	case 0xd8:
	{
		u8 const data = get_r(a_index());
		set_r(a_index(), get_r(op & 7));
		set_r(op & 7, data);
		m_icount -= 4;
		break;
	}

	case 0xe0: case 0xe8: case 0xf0: case 0xf8:
	{
		u16 const table = (BIT(m_ccw, 1) ? 0x8000 : 0) | ((op & 0x3f) << 1);
		push_word(m_pc);
		m_pc = read_word(table);
		m_icount -= 13;
		break;
	}
	}
}


//-------------------------------------------------
//  state_add_psw - overridable method for PSW
//  state registration
//-------------------------------------------------

void upd78k3_device::state_add_psw()
{
	state_add(UPD78K3_PSW, "PSW", m_psw).mask(0xf0fd);
	state_add(STATE_GENFLAGS, "FLAGS", m_psw).mask(0xf0fd).formatstr("%12s").noshow();
	state_add<u8>(UPD78K3_PSWL, "PSWL",
		[this]() { return m_psw & 0x00ff; },
		[this](u8 data) { m_psw = (m_psw & 0xff00) | data; }
	).mask(0xfd).noshow();
	state_add<u8>(UPD78K3_PSWH, "PSWH",
		[this]() { return (m_psw & 0xff00) >> 8; },
		[this](u8 data) { m_psw = (m_psw & 0x00ff) | u16(data) << 8; }
	).mask(0xf0).noshow();
}


//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void upd78k3_device::device_start()
{
	// get address spaces and access caches
	space(AS_PROGRAM).cache(m_program_cache);
	space(AS_PROGRAM).specific(m_program_space);
	space(AS_DATA).cache(m_iram_cache);
	space(AS_IO).specific(m_sfr_space);

	set_icountptr(m_icount);

	// debug state
	state_add(UPD78K3_PC, "PC", m_pc);
	state_add(STATE_GENPC, "GENPC", m_pc).noshow();
	state_add(STATE_GENPCBASE, "GENPCBASE", m_ppc).noshow();
	state_add_psw();
	state_add<u8>(UPD78K3_RBS, "RBS",
		[this]() { return (m_psw & 7000) >> 12; },
		[this](u8 data) { m_psw = (m_psw & 0x8fff) | u16(data) << 12; }
	).mask(7).noshow();
	state_add(UPD78K3_SP, "SP", m_sp);
	for (int n = 0; n < 4; n++)
		state_add<u16>(UPD78K3_RP0 + n, string_format("RP%d", n).c_str(),
			[this, n]() { return m_iram[register_base() >> 1 | n]; },
			[this, n](u16 data) { m_iram[register_base() >> 1 | n] = data; }
		).formatstr("%9s");
	for (int n = 0; n < 2; n++)
		state_add<u16>(UPD78K3_AX + n, std::array<const char *, 2>{{"AX", "BC"}}[n],
			[this, n]() { return m_iram[register_base() >> 1 | (m_psw & 0x0020) >> 4 | n]; },
			[this, n](u16 data) { m_iram[register_base() >> 1 | (m_psw & 0x0020) >> 4 | n] = data; }
		).noshow();
	for (int n = 0; n < 4; n++)
	{
		state_add<u16>(UPD78K3_VP + n, std::array<const char *, 4>{{"VP", "UP", "DE", "HL"}}[n],
			[this, n]() { return m_iram[register_base() >> 1 | 0x04 | n]; },
			[this, n](u16 data) { m_iram[register_base() >> 1 | 0x04 | n] = data; }
		);
		state_add<u16>(UPD78K3_RP4 + n, string_format("RP%d", 4 + n).c_str(),
			[this, n]() { return m_iram[register_base() >> 1 | 0x04 | n]; },
			[this, n](u16 data) { m_iram[register_base() >> 1 | 0x04 | n] = data; }
		).noshow();
	}
	for (int n = 0; n < 16; n++)
		state_add<u8>(UPD78K3_R0 + n, string_format("R%d", n).c_str(),
			[this, n]() { return iram_byte_r(register_base() | n); },
			[this, n](u8 data) { iram_byte_w(register_base() | n, data); }
		).noshow();
	for (int n = 0; n < 4; n++)
		state_add<u8>(UPD78K3_X + n, std::array<const char *, 4>{{"X", "A", "C", "B"}}[n],
			[this, n]() { return iram_byte_r(register_base() | (m_psw & 0x0020) >> 3 | n); },
			[this, n](u8 data) { iram_byte_w(register_base() | (m_psw & 0x0020) >> 3 | n, data); }
		).noshow();
	for (int n = 0; n < 8; n++)
		state_add<u8>(UPD78K3_VPL + n, std::array<const char *, 8>{{"VPL", "VPH", "UPL", "UPH", "E", "D", "L", "H"}}[n],
			[this, n]() { return iram_byte_r(register_base() | 0x08 | n); },
			[this, n](u8 data) { iram_byte_w(register_base() | 0x08 | n, data); }
		).noshow();

	// save state
	save_item(NAME(m_pc));
	save_item(NAME(m_ppc));
	save_item(NAME(m_sp));
	save_item(NAME(m_psw));
	save_item(NAME(m_ccw));
	save_item(NAME(m_irq_state));
}


//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void upd78k3_device::device_reset()
{
	m_psw = 0x0000;
	m_ccw = 0x00;
	m_irq_state = 0x00;
	m_pc = m_program_cache.read_word(0);
	m_ppc = m_pc;
}


//-------------------------------------------------
//  execute_run -
//-------------------------------------------------

void upd78k3_device::execute_run()
{
	while (m_icount > 0)
	{
		if (take_interrupt())
			continue;
		m_ppc = m_pc;
		debugger_instruction_hook(m_pc);
		if (m_pc == 0x2411 || m_pc == 0x5b07 || m_pc == 0x393d || m_pc == 0x39ec)
		{
			static int firmware_trace_count;
			if (firmware_trace_count++ < 40)
				logerror("firmware reached %04X\n", m_pc);
		}
		int const previous_icount = m_icount;
		execute_one();
		execute_peripherals(previous_icount - m_icount);
	}
}

void upd78k3_device::execute_set_input(int inputnum, int state)
{
	if (inputnum >= NMI_LINE && inputnum <= INT2_LINE)
	{
		if (state == CLEAR_LINE)
			m_irq_state &= ~(1U << inputnum);
		else
			m_irq_state |= 1U << inputnum;
	}
}

bool upd78k3_device::take_interrupt()
{
	int line = -1;
	int vector = -1;
	if (BIT(m_irq_state, NMI_LINE))
		line = NMI_LINE;
	else if ((m_psw & PSW_IE) && (m_irq_state & 0x0e))
		line = std::countr_zero(unsigned(m_irq_state & 0x0e));
	else if (m_psw & PSW_IE)
		vector = pending_internal_interrupt();
	if (line < 0 && vector < 0)
		return false;

	if (line >= 0)
	{
		if (line == NMI_LINE)
			m_irq_state &= ~(1U << NMI_LINE);
		standard_irq_callback(line, m_pc);
		vector = 2 + line * 2;
	}
	else
	{
		static int internal_irq_trace_count;
		if (internal_irq_trace_count++ < 20)
			logerror("internal interrupt %02X at %04X\n", vector, m_pc);
		acknowledge_internal_interrupt(vector);
	}
	push_word(m_psw);
	push_word(m_pc);
	m_psw &= ~PSW_IE;
	u16 const table = BIT(m_ccw, 1) ? 0x8000 : 0;
	m_pc = read_word(table | vector);
	m_icount -= 12;
	return true;
}


//-------------------------------------------------
//  state_string_export -
//-------------------------------------------------

void upd78k3_device::state_string_export(const device_state_entry &entry, std::string &str) const
{
	switch (entry.index())
	{
	case STATE_GENFLAGS:
		str = string_format("RB%d:%c%c%c%c%c%c%c%c",
				(m_psw & 0x7000) >> 12,
				BIT(m_psw, 15) ? 'U' : '.',
				BIT(m_psw, 7) ? 'S' : '.',
				BIT(m_psw, 6) ? 'Z' : '.',
				BIT(m_psw, 5) ? 'R' : '.',
				BIT(m_psw, 4) ? 'A' : '.',
				BIT(m_psw, 3) ? 'I' : '.',
				BIT(m_psw, 2) ? 'V' : '.',
				BIT(m_psw, 0) ? 'C' : '.');
		break;

	case UPD78K3_RP0:
		str = string_format("%04X %s", m_iram[register_base() >> 1], BIT(m_psw, 5) ? "    " : "(AX)");
		break;

	case UPD78K3_RP1:
		str = string_format("%04X %s", m_iram[register_base() >> 1 | 1], BIT(m_psw, 5) ? "    " : "(BC)");
		break;

	case UPD78K3_RP2:
		str = string_format("%04X %s", m_iram[register_base() >> 1 | 2], BIT(m_psw, 5) ? "(AX)" : "    ");
		break;

	case UPD78K3_RP3:
		str = string_format("%04X %s", m_iram[register_base() >> 1 | 3], BIT(m_psw, 5) ? "(BC)" : "    ");
		break;
	}
}


//**************************************************************************
//  78K/III SUBSERIES DEVICES
//**************************************************************************

//-------------------------------------------------
//  upd78312_device - constructor
//-------------------------------------------------

upd78312_device::upd78312_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: upd78312_device(mconfig, UPD78312, tag, owner, clock, address_map_constructor(FUNC(upd78312_device::mem_map), this))
{
}

upd78312_device::upd78312_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock, address_map_constructor map)
	: upd78k3_device(mconfig, type, tag, owner, clock, map,
						address_map_constructor(FUNC(upd78312_device::sfr_map), this))
	, m_port_in_cb(*this, 0xff)
	, m_port_out_cb(*this)
	, m_port_latch{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
	, m_port_mode{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
	, m_timer0_control(0)
	, m_timer0_interrupt(0x47)
	, m_timer0_countdown(0)
	, m_timer0_pending(false)
{
}


//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void upd78312_device::device_start()
{
	upd78k3_device::device_start();

	save_item(NAME(m_port_latch));
	save_item(NAME(m_port_mode));
	save_item(NAME(m_timer0_control));
	save_item(NAME(m_timer0_interrupt));
	save_item(NAME(m_timer0_countdown));
	save_item(NAME(m_timer0_pending));
}


//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void upd78312_device::device_reset()
{
	upd78k3_device::device_reset();
	for (unsigned port = 0; port < 6; port++)
	{
		m_port_latch[port] = 0xff;
		m_port_mode[port] = 0xff;
		update_port_output(port);
	}
	m_timer0_control = 0;
	m_timer0_interrupt = 0x47;
	m_timer0_countdown = 0;
	m_timer0_pending = false;
}


//-------------------------------------------------
//  upd78310_device - constructor
//-------------------------------------------------

upd78310_device::upd78310_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: upd78312_device(mconfig, UPD78310, tag, owner, clock, address_map_constructor())
{
}


//-------------------------------------------------
//  create_disassembler -
//-------------------------------------------------

std::unique_ptr<util::disasm_interface> upd78312_device::create_disassembler()
{
	return std::make_unique<upd78312_disassembler>();
}


//-------------------------------------------------
//  mem_map - type-specific internal memory map
//  (excluding IRAM and SFRs)
//-------------------------------------------------

void upd78312_device::mem_map(address_map &map)
{
	map(0x0000, 0x1fff).rom().region(DEVICE_SELF, 0); // 8K mask ROM
	map(0xfe00, 0xfeff).rw(FUNC(upd78312_device::iram_byte_r), FUNC(upd78312_device::iram_byte_w));
}


//-------------------------------------------------
//  sfr_map - type-specific SFR map
//-------------------------------------------------

void upd78312_device::sfr_map(address_map &map)
{
	map(0x00, 0x05).rw(FUNC(upd78312_device::port_r), FUNC(upd78312_device::port_w));
	map(0x20, 0x25).rw(FUNC(upd78312_device::port_mode_r), FUNC(upd78312_device::port_mode_w));
	map(0x80, 0x80).rw(FUNC(upd78312_device::timer0_control_r), FUNC(upd78312_device::timer0_control_w));
	map(0xce, 0xce).rw(FUNC(upd78312_device::timer0_interrupt_r), FUNC(upd78312_device::timer0_interrupt_w));
}


//-------------------------------------------------
//  port access
//-------------------------------------------------

u8 upd78312_device::port_r(offs_t offset)
{
	u8 const inputs = m_port_in_cb[offset]();
	return (inputs & m_port_mode[offset]) | (m_port_latch[offset] & ~m_port_mode[offset]);
}

void upd78312_device::port_w(offs_t offset, u8 data)
{
	m_port_latch[offset] = data;
	update_port_output(offset);
}

u8 upd78312_device::port_mode_r(offs_t offset)
{
	return m_port_mode[offset];
}

void upd78312_device::port_mode_w(offs_t offset, u8 data)
{
	m_port_mode[offset] = data;
	update_port_output(offset);
}

void upd78312_device::update_port_output(unsigned port)
{
	// Inputs are high impedance; report them high to board-level callbacks.
	m_port_out_cb[port](m_port_latch[port] | m_port_mode[port]);
}


//-------------------------------------------------
//  timer 0 (preliminary interval mode)
//-------------------------------------------------

u8 upd78312_device::timer0_control_r()
{
	return m_timer0_control;
}

void upd78312_device::timer0_control_w(u8 data)
{
	m_timer0_control = data;
	if (BIT(data, 7) && m_timer0_countdown <= 0)
		m_timer0_countdown = 6'000;
}

u8 upd78312_device::timer0_interrupt_r()
{
	return m_timer0_interrupt | (m_timer0_pending ? 0x80 : 0x00);
}

void upd78312_device::timer0_interrupt_w(u8 data)
{
	m_timer0_interrupt = data & 0x47;
	if (!BIT(data, 7))
		m_timer0_pending = false;
}

void upd78312_device::execute_peripherals(int cycles)
{
	if (!BIT(m_timer0_control, 7))
		return;

	m_timer0_countdown -= cycles;
	if (m_timer0_countdown <= 0)
	{
		// The exact clock selector and compare register are not yet modelled.
		// The D-50 programs timer 0 as an approximately 1 kHz interval source.
		m_timer0_countdown += 6'000;
		m_timer0_pending = true;
	}
}

int upd78312_device::pending_internal_interrupt() const
{
	return m_timer0_pending && !BIT(m_timer0_interrupt, 6) ? 0x0e : -1;
}

void upd78312_device::acknowledge_internal_interrupt(int vector)
{
	if (vector == 0x0e)
	{
		static int timer0_log_count;
		if (timer0_log_count++ < 20)
			logerror("timer 0 interrupt\n");
		m_timer0_pending = false;
	}
}


//-------------------------------------------------
//  state_add_psw - overridable method for PSW
//  state registration
//-------------------------------------------------

void upd78312_device::state_add_psw()
{
	state_add(UPD78K3_PSW, "PSW", m_psw).mask(0x72ff);
	state_add(STATE_GENFLAGS, "FLAGS", m_psw).mask(0x72ff).formatstr("%13s").noshow();
	state_add<u8>(UPD78K3_PSWL, "PSWL",
		[this]() { return m_psw & 0x00ff; },
		[this](u8 data) { m_psw = (m_psw & 0xff00) | data; }
	).noshow();
	state_add<u8>(UPD78K3_PSWH, "PSWH",
		[this]() { return (m_psw & 0xff00) >> 8; },
		[this](u8 data) { m_psw = (m_psw & 0x00ff) | u16(data) << 8; }
	).mask(0x72).noshow();
}


//-------------------------------------------------
//  state_string_export -
//-------------------------------------------------

void upd78312_device::state_string_export(const device_state_entry &entry, std::string &str) const
{
	switch (entry.index())
	{
	case STATE_GENFLAGS:
		str = string_format("RB%d:%c%c%c%c%c%c%c%c%c",
				(m_psw & 0x7000) >> 12,
				BIT(m_psw, 9) ? 'I' : '.',
				BIT(m_psw, 7) ? 'S' : '.',
				BIT(m_psw, 6) ? 'Z' : '.',
				BIT(m_psw, 5) ? 'R' : '.',
				BIT(m_psw, 4) ? 'A' : '.',
				BIT(m_psw, 3) ? 'I' : '.',
				BIT(m_psw, 2) ? 'V' : '.',
				BIT(m_psw, 1) ? '-' : '+',
				BIT(m_psw, 0) ? 'C' : '.');
		break;

	default:
		upd78k3_device::state_string_export(entry, str);
		break;
	}
}
