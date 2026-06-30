// license:BSD-3-Clause
// copyright-holders:giulioz
/***************************************************************************
 *
 *   Roland/Boss TC6088AF (CSP) emulation
 *   by Giulio Zausa
 *
 ***************************************************************************/

#ifndef MAME_CPU_ROLAND_CSPD_H
#define MAME_CPU_ROLAND_CSPD_H

#pragma once

class csp_disassembler : public util::disasm_interface
{
public:
	csp_disassembler() = default;
	virtual ~csp_disassembler() = default;

	virtual u32 opcode_alignment() const override;
	virtual offs_t disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params) override;
};

#endif // MAME_CPU_ROLAND_CSPD_H
