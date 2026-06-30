// license:BSD-3-Clause
// copyright-holders:giulioz
/***************************************************************************
 *
 *   Roland/Boss TC6088AF (CSP) emulation
 *   by Giulio Zausa
 *
 ***************************************************************************/

#include "emu.h"
#include "cspd.h"

u32 csp_disassembler::opcode_alignment() const
{
	return 1;
}

offs_t csp_disassembler::disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params)
{
	return 1;
}
