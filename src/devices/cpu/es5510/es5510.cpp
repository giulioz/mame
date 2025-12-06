// license:BSD-3-Clause
// copyright-holders:Christian Brunschen
/***************************************************************************************
 *
 *   es5510.cpp - Ensoniq ES5510 (ESP) emulation
 *   by Christian Brunschen
 *
 *   TODO:
 *      gunlock and clones: Glitch sound after game over once (MT #07861)
 *      DRAM Size isn't verified, differs per machines?
 *
 ***************************************************************************************/

#include "emu.h"
#include "es5510.h"
#include "es5510d.h"

#include "cpu/m68000/m68000.h"

#include "corestr.h"

#include <cstdarg>
#include <cstdio>
#include <algorithm>

DEFINE_DEVICE_TYPE(ES5510, es5510_device, "es5510", "Ensoniq ES5510")

// Initialize ESP to mostly zeroed, configured for 64k samples of delay line memory, running (not halted)
es5510_device::es5510_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: cpu_device(mconfig, ES5510, tag, owner, clock)
{
}


uint8_t es5510_device::host_r(address_space &space, offs_t _addr)
{
	if (_addr == 0x12) return 0; // Host control. Must return 0 unless you're busy and you want the uC to wait.
	if (_addr == 0x16) return pc; // PC. This should just be zero, I think?!?
	if (_addr == 0x0b) return 0;
	if (_addr == 0x0e) return 0xff;
	if (_addr < 3) return (host_gpr >> (16 - _addr * 8)) & 255;
	if (_addr < 9) {_addr -= 3; return (host_instr >> (40 - _addr * 8)) & 255;}
	if (_addr < 12) { _addr -= 9; return (host_dil >> (16 - _addr * 8)) & 255;}
	if (_addr < 15) { _addr -= 12; return (host_dol >> (16 - _addr * 8)) & 255;}
	if (_addr < 18) { _addr -= 15; return (host_dadr >> (16 - _addr * 8)) & 255;}
	if (_addr == 0x14) return hostregs[0x14] | 0x3f;
	//		if (_addr >= 0x20)
	//		printf("ESP%d:: Reading from %02x [%02x]\n", which + 1, _addr, hostregs[_addr]);
	return hostregs[_addr];
}

void es5510_device::host_w(offs_t _addr, uint8_t _val)
{
	if (_addr < 3)
	{
		host_gpr &= ~(255 << (16 - _addr * 8));
		host_gpr |= ((uint32_t)_val) << (16 - _addr * 8);
	}
	else if (_addr < 9)
	{
		_addr -= 3;
		host_instr &= ~(255ull << (40 - _addr * 8));
		host_instr |= ((uint64_t)_val) << (40 - _addr * 8);
	}
	else if (_addr < 12)
	{
		_addr -= 9;
		host_dil &= ~(255 << (16 - _addr * 8));
		host_dil |= ((uint32_t)_val) << (16 - _addr * 8);
	}
	else if (_addr < 15)
	{
		_addr -= 12;
		host_dol &= ~(255 << (16 - _addr * 8));
		host_dol |= ((uint32_t)_val) << (16 - _addr * 8);
	}
	else if (_addr < 18)
	{
		_addr -= 15;
		host_dadr &= ~(255 << (16 - _addr * 8));
		host_dadr |= ((uint32_t)_val) << (16 - _addr * 8);
		if (_addr == 0)
		{
			if (hostregs[0x14] & 0x40)
			{
				if (hostregs[0x14] & 0x80)
					host_dil = (int)ram[host_dadr >> 8] << 8;
				else
					ram[host_dadr >> 8] = host_dol >> 8;
			}
			else
				printf("IO Access from DSP!\n");
		}
	}
	else if (_addr == 0xe0 || _addr == 0xa0 || _addr == 0xc0)
	{
		if (_addr & 0x40)
		{
			//				printf("ESP%d:: Write value 0x%012llx to INSTR address %02x\n", which + 1, instr, _val);
			if (_val < 0xa0) instructions[_val] = host_instr;
		}
		if (_addr & 0x20)
		{
			host_gpr = (host_gpr & 0x800000) ? host_gpr | 0xff000000 : host_gpr & 0x7fffff;
//				printf("ESP%d:: Write value 0x%06x to GPR address %s\n", which + 1, host_gpr, regName(_val));
			writeReg(_val, host_gpr);
		}
	}
	else if (_addr == 0x80)
	{
		host_gpr = gprs[_val];
		if (_val < 0xa0) host_instr = instructions[_val];
		//			printf("ESP%d:: Read request for %02x [0x%06x 0x%012llx]\n", which + 1, _val, gpr, instr);
	}
	else
	{
		hostregs[_addr] = _val;
//			printf("ESP%d:: Write to %02x, value = %02x\n", which + 1, _addr, _val);
	}
}

int16_t es5510_device::ser_r(int offset)
{
	return (int16_t)(gprs[0xea + offset] >> 8);
}

void es5510_device::ser_w(int offset, int16_t data)
{
	gprs[0xea + offset] = ((int32_t)data << 8);
}

void es5510_device::device_start() {
	set_icountptr(icount);
	state_add(STATE_GENPC,"GENPC", pc).noshow();
	state_add(STATE_GENPCBASE, "CURPC", pc).noshow();

	save_item(NAME(pc));
	save_item(NAME(instructions));
	save_item(NAME(gprs));
	save_item(NAME(ram));
	save_item(NAME(ccr));
}

void es5510_device::device_reset() {
	gprs[0xff] = 0;
	gprs[0xfe] = 0x007fffff;
	gprs[0xfd] = 0x00800000;
	gprs[0xfc] = 0x00ffffff;
}

device_memory_interface::space_config_vector es5510_device::memory_space_config() const
{
	return space_config_vector { };
}

uint64_t es5510_device::execute_clocks_to_cycles(uint64_t clocks) const noexcept {
	return clocks / 3;
}

uint64_t es5510_device::execute_cycles_to_clocks(uint64_t cycles) const noexcept {
	return cycles * 3;
}

uint32_t es5510_device::execute_min_cycles() const noexcept {
	return 1;
}

uint32_t es5510_device::execute_max_cycles() const noexcept {
	return 1;
}

void es5510_device::execute_set_input(int linenum, int state) {
	if (linenum == ES5510_HALT) {
		halt_asserted = (state == ASSERT_LINE);
	}
}

void es5510_device::execute_run() {
	while (icount > 0) {
		--icount;
	}
}

std::unique_ptr<util::disasm_interface> es5510_device::create_disassembler()
{
	return std::make_unique<es5510_disassembler>();
}

void es5510_device::run_once()
{
	if (halt_asserted) return;
	do {
		handle_instr(pc);
	} while (pc != 0);
}

void es5510_device::handle_instr(int opaddr)
{
	const uint64_t inst = instructions[opaddr];
	const uint8_t MulD = (inst >> 40), MulC = (inst >> 32), AluB = (inst >> 24), AluA = (inst >> 16), AluOpCode = (inst >> 12) & 15, OperandSelect = (inst >> 8) & 15;
	const uint8_t Skip = (inst >> 7) & 1, MAC = (inst >> 6) & 1, RamControl = (inst >> 3) & 7;
	
	// bit 0 = Alu Src is DIL, bit 1 = ALU Dest to A, bit2 = ALU dest to fifo, bit3 = Mul Src DIL, bit 4 = mul dest to C., bit 5 = mul dest to fifo
	const uint8_t osunpack[16] = {0x12, 0x22, 0x32, 0x1a, 0x3a, 0x14, 0x1c, 0x16, 0x1e, 0x13, 0x23, 0x33, 0x1b, 0x3b, 0x17, 0x1f};
	const uint8_t osu = osunpack[OperandSelect];
	const bool AluSrcAIsDIL = osu & 1;
	const bool AluDestToA = osu & 2;
	const bool AluDestToFifo = osu & 4;
	const bool MulSrcCIsDIL = osu & 8;
	const bool MulDestToC = osu & 16;
	const bool MulDestToFifo = osu & 32;
	
	// handle ram controls
	const uint8_t base[6] = {0xf8,0xf8,0xf6,0xf6,0xf7,0xf8};
	int32_t address = gprs[opaddr] & 0xffffff;
	if (RamControl < 6)
	{
		uint8_t b = base[RamControl];
		address = (address + (gprs[b] & 0xffffff)) & 0xffffff;
		uint32_t dlength = gprs[0xf5] & 0xffffff;
		if (b == 0xf8 && address >= dlength) address = (address - dlength) % (dlength);
	}
	ram_action rama = {(((address & ~memsiz) >> 8) & 0xffffu), !(RamControl & 1), (RamControl == 5), (RamControl > 5)}; // address, read, flush, io.
	
	// Reads and writes
	int32_t srcC = MulSrcCIsDIL ? gprs[0xf4] : gprs[MulC], srcD = gprs[MulD]; // read for MUL and CCR
	writeReg(writealuresulttogpr, alures); // Write ALU
	const int32_t srcA = AluSrcAIsDIL ? gprs[0xf4] : gprs[AluA], srcB = (AluSrcAIsDIL && (AluOpCode >= 8 && AluOpCode <= 14)) ? gprs[0xf4] : gprs[AluB]; // read ALU
	// Conditionals
	int32_t cmr = gprs[0xfb];
	bool doskip = Skip ? ((cmr & ccr & 0xf80000) ? 1 : 0) ^ ((cmr & ccr_not) ? 1 : 0) : false;	// compute conditional status.

	if (!doskip) // MUL
	{
		// multiply
		int shift = (gprs[0xf9] & (1 << 22)) ? 1 : 2;
		srcC = se24(srcC);
		srcD = se24(srcD);
		int64_t mulres = ((int64_t)srcC * (int64_t)srcD);
		mulres <<= shift;
		if (MAC) mulres += mulacc;
		mulacc = mulres;
		if (mulres > 0x7FFFFFFFFFFFll) mulres = 0x7FFFFFFFFFFFll;
		else if (mulres < -0x800000000000ll) mulres = -0x800000000000ll;
		gprs[0xf2] = se24(mulres);
		mulres >>= 24;
		gprs[0xf3] = se24(mulres);
		if (MulDestToC) writeReg(MulC, se24(mulres));
		if (MulDestToFifo) fifoout(se24(mulres));
	}
	// ALU
	int32_t oldccr = ccr;
	switch (AluOpCode)
	{
		case 0:
			alures = srcA + srcB;
			ccrf(ccr, ccr_v, 0);
			if (alures > 0x7fffff) 			{ ccrf(ccr, ccr_v, 1); alures = 0x7fffff; }
			else if (alures < -0x800000)	{ ccrf(ccr, ccr_v, 1); alures = -0x800000;	}
			ccrf(ccr, ccr_n, alures < 0);
			ccrf(ccr, ccr_c,((srcA & srcB) | (srcA & ~alures) | (srcB & ~alures)) & 0x800000); // Revisit this.
			ccrf(ccr, ccr_z, !alures);
			ccrf_lt(ccr);
			break;
		case 1:
			alures = srcA - srcB;
			ccrf(ccr, ccr_v, 0);
			if (alures > 0x7fffff) 			{ ccrf(ccr, ccr_v, 1); alures = 0x7fffff; }
			else if (alures < -0x800000)	{ ccrf(ccr, ccr_v, 1); alures = -0x800000;	}
			ccrf(ccr, ccr_n, alures < 0);
			ccrf(ccr, ccr_c,((~srcA & srcB) | (~srcA & alures) | (srcB & alures)) & 0x800000);
			ccrf(ccr, ccr_z, !alures);
			ccrf_lt(ccr);
			break;
		case 2:
			alures = srcA + srcB;
			ccrf(ccr, ccr_v, ((srcA & srcB & ~alures) | (~srcA & ~srcB & alures)) & 0x800000);
			ccrf(ccr, ccr_n, alures < 0);
			ccrf(ccr, ccr_c,((srcA & srcB) | (srcA & ~alures) | (srcB & ~alures)) & 0x800000);
			ccrf(ccr, ccr_z, !alures);
			ccrf_lt(ccr);
			break;
		case 3:
		case 4:
			alures = srcA - srcB;
			ccrf(ccr, ccr_v, ((srcA & ~srcB & ~alures) | (~srcA & srcB & alures)) & 0x800000);
			ccrf(ccr, ccr_n, alures < 0);
			ccrf(ccr, ccr_c,((~srcA & srcB) | (~srcA & alures) | (srcB & alures)) & 0x800000);
			ccrf(ccr, ccr_z, !alures);
			ccrf_lt(ccr);
			break;
		case 5: alures = srcA & srcB; ccrf(ccr, ccr_n, alures & 0x800000); ccrf(ccr, ccr_z, !alures); break;
		case 6: alures = srcA | srcB; ccrf(ccr, ccr_n, alures & 0x800000); ccrf(ccr, ccr_z, !alures); break;
		case 7: alures = srcA ^ srcB; ccrf(ccr, ccr_n, alures & 0x800000); ccrf(ccr, ccr_z, !alures); break;
		case 8: alures = (srcB & 0x800000) ? ~srcB : srcB; ccrf(ccr, ccr_n, 0); ccrf(ccr, ccr_c, srcB & 0x800000); break;
		case 9: alures = srcB; break;
		case 10:
			alures = srcB << 2;
			ccrf(ccr, ccr_v, 0);
			if (alures > 0x7fffff) 		{ ccrf(ccr, ccr_v, 1); alures = 0x7fffff; }
			else if (alures < -0x800000){ ccrf(ccr, ccr_v, 1); alures = -0x800000;	}
			ccrf(ccr, ccr_n, alures & 0x800000); ccrf(ccr, ccr_z, !alures);
			break;
		case 11:
			alures = srcB << 8;
			ccrf(ccr, ccr_v, 0);
			if (alures > 0x7fffff) 		{ ccrf(ccr, ccr_v, 1); alures = 0x7fffff; }
			else if (alures < -0x800000){ ccrf(ccr, ccr_v, 1); alures = -0x800000;	}
			ccrf(ccr, ccr_n, alures & 0x800000); ccrf(ccr, ccr_z, !alures);
			break;
		case 12: alures = (srcB << 15) & 0x7fffff; ccrf(ccr, ccr_c, srcB & 0x800000); ccrf(ccr, ccr_n, 0); break;
		case 13:
			alures = 0x7fffff - srcB;
			ccrf(ccr, ccr_v, ((0x7fffff & ~srcB & ~alures) | (~0x7fffff & srcB & alures)) & 0x800000);
			ccrf(ccr, ccr_n, alures < 0);
			ccrf(ccr, ccr_c,((~0x7fffff & srcB) | (~0x7fffff & alures) | (srcB & alures)) & 0x800000);
			ccrf(ccr, ccr_z, !alures);
			ccrf_lt(ccr);
			break;
		case 14: alures = (srcB >> 1) | (srcB & 0x800000); ccrf(ccr, ccr_n, srcB & 0x800000); ccrf(ccr, ccr_c, srcB & 1); break;
		case 15:
			gprs[0xf8] = (gprs[0xf8] & 0xffffff) - memsiz - 1; // decrement dbase.
			if (gprs[0xf8] < 0) gprs[0xf8] = gprs[0xf5] & 0xffffff;
			pc = 0;
			break;
	}
	alures = se24(alures);
	writealuresulttogpr = 0xff;
	if (AluOpCode != 4 && AluOpCode != 15 && !doskip)
	{
		if (AluDestToA) writealuresulttogpr = AluA;
		if (AluDestToFifo) fifoout(alures);
	}
	if (Skip && AluOpCode != 4) ccr = oldccr;

	if (ramact.read || ramact.flush) gprs[0xf4] = (ramact.io) ? 0 : ((int32_t)ram[ramact.addr & 0xffff]) << 8;
	if (!ramact.read) // Handle writes
	{
		if (dolfill > 0) dolfill--;
		if (!ramact.flush) ram[ramact.addr & 0xffff] = dol[dolfill];
	}

	ramact = rama;
	
	if (AluOpCode != 15) pc++;
}
