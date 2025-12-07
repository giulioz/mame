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

void es5510_device::transpile()
{
	const uint8_t osunpack[16] = {0x12, 0x22, 0x32, 0x1a, 0x3a, 0x14, 0x1c, 0x16, 0x1e, 0x13, 0x23, 0x33, 0x1b, 0x3b, 0x17, 0x1f};
	
	printf("Transpiling for DSP\n");
	bool regread[256] {}, regwritten[256] {}, deadstores[256] {};
	int numinstructions = 0;
	
	while (numinstructions < 160 && ((instructions[numinstructions] >> 12) & 15) != 15) numinstructions++;
	
	for (int i = 0; i < numinstructions; i++)
	{
		const uint64_t inst = instructions[i];
		const uint8_t MulD = (inst >> 40), MulC = (inst >> 32), AluB = (inst >> 24), AluA = (inst >> 16), AluOpCode = (inst >> 12) & 15, OperandSelect = (inst >> 8) & 15;
		const uint8_t osu = osunpack[OperandSelect];
		const bool AluSrcAIsDIL = osu & 1;
		const bool AluDestToA = osu & 2;
		const bool MulSrcCIsDIL = osu & 8;
		const bool MulDestToC = osu & 16;
		const bool sop = (AluOpCode >= 8 && AluOpCode <= 14);
		
		if (sop)
		{
			if (!AluSrcAIsDIL) regread[AluB] = true;
		}
		else
		{
			regread[AluB] = true;
			if (!AluSrcAIsDIL) regread[AluA] = true;
		}
		if (AluDestToA) regwritten[AluA] = true;
		if (!MulSrcCIsDIL) regread[MulC] = true;
		regread[MulD] = true;
		if (MulDestToC) regwritten[MulC] = true;
	}
	for (int i = 0; i < 256; i++) deadstores[i] = regwritten[i] && !regread[i];
	if (regwritten[0xfb]) printf("!!!! Write to CMR !!!!\n");
	
	printf("const int dlength = 0x%06x\n", gprs[0xf5] & 0xffffff);
	printf("const int memsis = 0x%06x\n", memsiz & 0xffffff);
	printf("const int cmr = 0x%06x\n", gprs[0xfb]);
	printf("int ccr = 0x%06x\n", gprs[0xfa]);
	printf("int ramaddr, dol[2];\n");
	printf("int ccr, srcC, srcD, srcA, srcB, dil, mulres32;\n");
	printf("int64_t mulres;\n");
	const int shift = (gprs[0xf9] & (1 << 22)) ? 1 : 2;
	printf("int64_t mul(int32_t a, int32_t b) { a = (a & 0x800000) ? (a | 0xff000000) : (a & 0x7fffff); b = (b & 0x800000) ? (b | 0xff000000) : (b & 0x7fffff); int64_t t = (int64_t)a * (int64_t)b; return t << %d;}\n", shift);
	printf("int64_t mulclamp(int64_t t) {if (t > 0x7FFFFFFFFFFFll) t = 0x7FFFFFFFFFFFll; else if (t < -0x800000000000ll) t = -0x800000000000ll; return t;}\n");
	printf("int32_t se24(int32_t t) {return (int32_t)((t & 0x800000) ? (t | 0xff000000) : (t & 0x7fffff));}\n");
	printf("int32_t se24(int64_t t) {return se24((int32_t)t);}\n");
	printf("int32_t delayAddr(int32_t offset) {offset += reg248; offset &= 0xffffff; if (offset > dlength) offset = (offset - dlength) %% dlength; return ((offset & 0x%06x) >> 8) & 0xffff;}\n", memsiz);
	printf("int32_t ioAddr(int32_t offset) {return ((offset & 0x%06x) >> 8) & 0xffff;}\n", memsiz);
	printf("int32_t tableAAddr(int32_t offset) {offset += reg246; return ((offset & 0x%06x) >> 8) & 0xffff;}\n", memsiz);
	printf("int32_t tableBAddr(int32_t offset) {offset += reg247; return ((offset & 0x%06x) >> 8) & 0xffff;}\n", memsiz);
	
	
	for (int i = 0; i < 256; i++)
	{
		if (regwritten[i] || !regread[i]) continue;
		printf("const int reg%d = %d;\n", i, gprs[i]);
	}
	for (int i = 0; i < 256 - 4; i++)
	{
		if (!regwritten[i] || deadstores[i]) continue;
		printf("int reg%d = %d;\n", i, gprs[i]);
	}
	
	uint32_t flags[160] {};
	enum {
		kIsConditional = 1 << 0,
		kIsWrite = 1 << 1,
		kIsRead = 1 << 2,
		kFlush = 1 << 3,
		kWritesALUToReg = 1 << 4,
		kSrcCReadsDIL = 1 << 5,
		kSrcAReadsDIL = 1 << 6,
		kSrcBReadsDIL = 1 << 7,
		kUpdatesCCRn = 1 << 8,
		kUpdatesCCRc = 1 << 9,
		kUpdatesCCRv = 1 << 10,
		kUpdatesCCRlt = 1 << 11,
		kUpdatesCCRz = 1 << 12,
		kWritesALUToFIFO = 1 << 13,
		kWritesMULToFIFO = 1 << 14,
		kWritesMULToReg = 1 << 15,
		kReadsFromMULAcc = 1 << 16,
		kAddressIsUsed = 1 << 17,
		kALUIsUsed = 1 << 18,
		kMULIsUsed = 1 << 19,
		kCCRResultWillBeRead = 1 << 20,
	};
	std::vector<uint8_t> writtenTo[160], readsFrom[160];
	uint8_t aluWriteReg[160], mulWriteReg[160];
	
	for (int i = 0; i < numinstructions; i++)
	{
		const uint64_t inst = instructions[i];
		const uint8_t MulD = (inst >> 40), MulC = (inst >> 32), AluB = (inst >> 24), AluA = (inst >> 16), AluOpCode = (inst >> 12) & 15, OperandSelect = (inst >> 8) & 15, Skip = (inst >> 7) & 1, RamControl = (inst >> 3) & 7;
		const uint8_t osu = osunpack[OperandSelect];
		const bool AluSrcAIsDIL = osu & 1, AluDestToA = osu & 2, AluDestToFifo = osu & 4, MulSrcCIsDIL = osu & 8, MulDestToC = osu & 16, MulDestToFifo = osu & 32;
		
		flags[i] |= Skip ? kIsConditional : 0;
		flags[i] |= (!(RamControl & 1) || RamControl == 5) ? kIsRead : kIsWrite;
		flags[i] |= (RamControl == 5) ? kFlush : 0;
		flags[i] |= MulSrcCIsDIL ? kSrcCReadsDIL : 0;
		flags[i] |= (AluSrcAIsDIL && (AluOpCode < 8)) ? kSrcAReadsDIL : 0;
		flags[i] |= (AluSrcAIsDIL && AluOpCode >= 8 && AluOpCode < 15) ? kSrcBReadsDIL : 0;
		
		const int flagsperop[16] = {0, 0, 0, 0, 0, 1, 1, 1, 2, 4, 3, 3, 2, 0, 2, 3};
		const int flagtypes[5] {kUpdatesCCRn | kUpdatesCCRc | kUpdatesCCRv | kUpdatesCCRlt | kUpdatesCCRc,
			kUpdatesCCRn | kUpdatesCCRz, kUpdatesCCRn | kUpdatesCCRc, kUpdatesCCRn | kUpdatesCCRz | kUpdatesCCRv, 0};
		flags[i] |= flagtypes[flagsperop[AluOpCode]];
		
		if (AluOpCode != 4 && AluOpCode != 15)
		{
			flags[i] |= (AluDestToA && AluA < 0xfc) ? kWritesALUToReg : 0;
			flags[i] |= AluDestToFifo ? kWritesALUToFIFO : 0;
		}
		flags[i] |= MulDestToC ? kWritesMULToReg : 0;
		flags[i] |= MulDestToFifo ? kWritesMULToFIFO : 0;
		if ((AluA == 0xf2 || AluA == 0xf3) && AluOpCode < 8 && !AluSrcAIsDIL) flags[i] |= kReadsFromMULAcc;
		if ((AluB == 0xf2 || AluB == 0xf3) && AluOpCode >= 8 && AluOpCode < 15 && !AluSrcAIsDIL) flags[i] |= kReadsFromMULAcc;
		if ((MulD == 0xf2 || MulD == 0xf3)) flags[i] |= kReadsFromMULAcc;
		if ((MulC == 0xf2 || MulC == 0xf3) && !MulSrcCIsDIL) flags[i] |= kReadsFromMULAcc;
		if ((flags[i] & kWritesALUToReg) && AluA < 0xfc) writtenTo[i].push_back(AluA);
		if ((flags[i] & kWritesMULToReg) && MulC < 0xfc) writtenTo[i].push_back(MulC);
		readsFrom[i].push_back(i);
		if (!AluSrcAIsDIL && (AluOpCode < 8)) readsFrom[i].push_back(AluA);
		if (!(flags[i] & kSrcBReadsDIL)) readsFrom[i].push_back(AluB);
		if (!MulSrcCIsDIL) readsFrom[i].push_back(MulC);
		readsFrom[i].push_back(MulD);
		aluWriteReg[i] = AluA;
		mulWriteReg[i] = MulC;
	}
	// Try to clear: (why you could clear it)
	//		kWritesALUToReg (dead store)
	//		kWritesMULToReg (dead store)
	//		kWritesALUToFIFO (flushed)
	//		kWritesMULToFIFO (flushed)
	//		kAddressIsUsed (read value never read)
	//		kALUIsUsed (no writes to reg, no writes to fifo)
	//		kMULIsUsed (no writes to reg, no mul to fifo AND no subsequent mac)
	//		kCCRResultWillBeRead (next time CCR flags are checked, they've already been overwritten by a later op)
	for (int i = 0; i < numinstructions; i++)
	{
		if (!(flags[i] & kWritesALUToReg)) continue;
		uint8_t to = aluWriteReg[i];
		if (to >= 0xfc) {flags[i] &= ~kWritesALUToReg; continue;}
		if (to >= 0xea && to <= 0xfc) continue;
		bool aluWriteIsDead = false;
		for (int j = 0; j < numinstructions; j++)
		{
			int w = (i + j + 1) % numinstructions;
			if (std::find(readsFrom[w].begin(), readsFrom[w].end(), to) != readsFrom[w].end())	// someone read it!
				break;
			if (std::find(writtenTo[w].begin(), writtenTo[w].end(), to) != writtenTo[w].end())
			{
				aluWriteIsDead = true;
				break;
			}
		}
		if (aluWriteIsDead) flags[i] &= ~kWritesALUToReg;
	}
	for (int i = 0; i < numinstructions; i++)
	{
		if (!(flags[i] & kWritesMULToReg)) continue;
		uint8_t to = mulWriteReg[i];
		if (to >= 0xfc) {flags[i] &= ~kWritesMULToReg; continue;}
		if (to >= 0xea && to <= 0xfc) continue;
		bool aluWriteIsDead = false;
		for (int j = 0; j < numinstructions; j++)
		{
			int w = (i + j + 1) % numinstructions;
			if (std::find(readsFrom[w].begin(), readsFrom[w].end(), to) != readsFrom[w].end())	// someone read it!
				break;
			if (std::find(writtenTo[w].begin(), writtenTo[w].end(), to) != writtenTo[w].end())
			{
				aluWriteIsDead = true;
				break;
			}
		}
		if (aluWriteIsDead) flags[i] &= ~kWritesMULToReg;
	}
	for (int i = 0; i < numinstructions; i++) // preprocess the flushes
	{
		if (!(flags[i] & kFlush)) continue;
		
		for (int j = 1; j < numinstructions; j++)
		{
			int w = (i + numinstructions - j) % numinstructions;
			if (flags[w] & kWritesALUToFIFO)
			{
				flags[w] &= ~kWritesALUToFIFO;
				break;
			}
			if (flags[w] & kWritesMULToFIFO)
			{
				flags[w] &= ~kWritesMULToFIFO;
				break;
			}
		}
	}
	for (int i = 0; i < numinstructions; i++) // find unused reads
	{
		if (flags[i] & kIsWrite) {flags[i] |= kAddressIsUsed; continue;} // it's a write
		bool readIsUnused = false;
		for (int j = 1; j < numinstructions; j++)
		{
			int w = (i + j) % numinstructions;
			if (flags[w] & (kSrcCReadsDIL|kSrcAReadsDIL|kSrcBReadsDIL)) break; // it just got read. it's used.
			if (j > 1 && flags[w] & kIsRead) {readIsUnused = true; break;} // it gets written over
		}
		flags[i] |= (readIsUnused) ? 0 : kAddressIsUsed;
	}
	for (int i = 0; i < numinstructions; i++)
	{
		bool ccrused = false;
		for (int k = kUpdatesCCRn; k <= kUpdatesCCRz; k <<= 1)
		{
			if (!(flags[i] & k)) continue;	// we dont update this flag.
			for (int j = 1; j < numinstructions && !ccrused; j++)
			{
				int w = (i + j) % numinstructions;
				if (flags[w] & kIsConditional) {ccrused = true; break;}
				if (flags[w] & k) break; // got overwritten
			}
		}
		if (ccrused) flags[i] |= kCCRResultWillBeRead;
	}
	
	for (int i = 0; i < numinstructions; i++)
	{
		flags[i] |= kMULIsUsed;
		if (!(flags[i] & (kWritesMULToReg | kWritesMULToFIFO)))
		{
			if (!(flags[(i + 1) % numinstructions] & kReadsFromMULAcc) && !(instructions[(i + 1) % numinstructions] & 64)) flags[i] &= ~kMULIsUsed; // it's not used. dont do it.
		}
		flags[i] |= kALUIsUsed;
		if (!(flags[i] & (kWritesALUToReg | kWritesALUToFIFO | kCCRResultWillBeRead))) flags[i] &= ~kALUIsUsed; // nope.
	}
	
	auto printregwrite = [this](int which, const char *val){
		if (which == 0xf4)
		{
			printf("memsiz = %s & 0xffffff;\n", val);
			return;
		}
		else if (which == 0xf2)
		{
			printf("mulacc &= ~0xffffff;\nmulacc |= (%s & 0xffffff);\n", val);
		}
		else if (which == 0xf3)
		{
			printf("mulacc &= 0xffffff;\nmulacc |= (int64_t)(%s) << 24;\n", val);
		}
		if (which < 0xfc) printf("%s = %s;\n", regName(which), val);
	};
	
	for (int i = 0; i < numinstructions; i++)
	{
		int last = (i + numinstructions - 1) % numinstructions;
//			int last2 = (i + numinstructions - 2) % numinstructions;
		int next = (i + 1) % numinstructions;
		printf("// instr %d\n", i);
		const uint64_t inst = instructions[i];
		const uint8_t MulD = (inst >> 40), MulC = (inst >> 32), AluB = (inst >> 24), AluA = (inst >> 16), AluOpCode = (inst >> 12) & 15, OperandSelect = (inst >> 8) & 15;
		const uint8_t Skip = (inst >> 7) & 1, MAC = (inst >> 6) & 1, RamControl = (inst >> 3) & 7;
		
		// bit 0 = Alu Src is DIL, bit 1 = ALU Dest to A, bit2 = ALU dest to fifo, bit3 = Mul Src DIL, bit 4 = mul dest to C., bit 5 = mul dest to fifo
		const uint8_t osunpack[16] = {0x12, 0x22, 0x32, 0x1a, 0x3a, 0x14, 0x1c, 0x16, 0x1e, 0x13, 0x23, 0x33, 0x1b, 0x3b, 0x17, 0x1f};
		const uint8_t osu = osunpack[OperandSelect];
		const bool AluSrcAIsDIL = osu & 1;
		const bool AluDestToA = osu & 2;
		const bool MulSrcCIsDIL = osu & 8;
		char srcAName[32], srcBName[32], srcCName[32], srcDName[32];
		
		// handle ram controls
		bool writetoramaddr = true;
		if ((flags[last] & kIsRead) && (flags[last] & kAddressIsUsed)) writetoramaddr = false;
		if (flags[last] & kIsWrite) writetoramaddr = false;
		char addrdest[32]; strcpy(addrdest, writetoramaddr ? "ramaddress" : "address");
		if (flags[i] & kAddressIsUsed)
		{
			char dest[32]; strcpy(dest, regName(i));
			if (!regwritten[i]) snprintf(dest, sizeof dest, "%s /*0x%06x*/", regName(i), gprs[i]);
			if (RamControl == 0 || RamControl == 1 || RamControl == 5) printf("%s = delayAddr(%s);\n", addrdest, dest);
			else if (RamControl == 2 || RamControl == 3) printf("%s = tableAAddr(%s);\n", addrdest, dest);
			else if (RamControl == 4) printf("%s = tableBAddr(%s)\n", addrdest, dest);
			else printf("%s = ioAddr(%s)\n", addrdest, dest);
		}
		
		// Reads and writes
		if (flags[i] & kMULIsUsed)
		{
			if (MulSrcCIsDIL || MulC == 0xf4) strcpy(srcCName, "dil");
			else if (MulC == writealuresulttogpr && (flags[last] & kWritesALUToReg))
			{
				strcpy(srcCName, "srcC");
				printf("srcC = %s;\n", regName(MulC));
			}
			else strcpy(srcCName, regName(MulC));
			
			if (MulD == writealuresulttogpr && (flags[last] & kWritesALUToReg))
			{
				strcpy(srcDName, "srcD");
				printf("srcD = %s;\n", regName(MulD));
			}
			else if (MulD == 0xf4)
				strcpy(srcDName, "dil");
			else
				strcpy(srcDName, regName(MulD));
		}
		
		if (flags[last] & kWritesALUToReg)
		{
			if (flags[last] & kIsConditional) printf("if (!doskip) {\n\t");
			printregwrite(writealuresulttogpr, "se24(alures)");	// WRITE!!
			if (flags[last] & kIsConditional) printf("}\n");
		}
		
		if (flags[i] & kALUIsUsed)
		{
			if (AluSrcAIsDIL || AluA == 0xf4) snprintf(srcAName, sizeof srcAName, "dil");
			else strcpy(srcAName, regName(AluA));
			
			if ((AluSrcAIsDIL && (AluOpCode >= 8 && AluOpCode <= 14)) || AluB == 0xf4) snprintf(srcBName, sizeof srcBName, "dil");
			else strcpy(srcBName, regName(AluB));
			
			// Conditionals
			if (Skip)
			{
				if (AluOpCode != 4) printf("oldccr = ccr;\n");
				printf("doskip = ((cmr & ccr & 0xf80000) ? 1 : 0) ^ ((cmr & ccr_not) ? 1 : 0)\n");
			}
		}

		if (flags[i] & kMULIsUsed) // MUL
		{
			if (Skip) printf("if (!doskip) {\n\t");
			if (!MulSrcCIsDIL && MulC == 0xff && MulD == 0xff)
			{
				if (MAC) printf("mulres = mulclamp(mulacc);\n"); else printf("mulacc = mulres = 0;\n");
			}
			else
			{
				// multiply
				printf("mulres = mul(%s, %s)", srcCName, srcDName);
				if (MAC) printf(" + mulacc;\n"); else printf(";\n");
				printf("mulacc = mulres; mulres = mulclamp(mulres);\n");
			}
			if (flags[next] & kReadsFromMULAcc) printf("%s = se24(mulres);\n", regName(242));
			if (flags[next] & kReadsFromMULAcc) printf("%s = se24(mulres >> 24)\n", regName(243));
			if (flags[i] & kWritesMULToReg) printregwrite(MulC, "se24(mulres >> 24)");
			if (flags[i] & kWritesMULToFIFO)
			{
				if (dolfill) printf("dol[1] = dol[0];\n");
				printf("dol[0] = se24(mulres >> 24);\n");
				if (dolfill < 2) dolfill++;
			}
		}
		
		if (flags[i] & kALUIsUsed)
		{
			bool ccrneeded = flags[i] & kCCRResultWillBeRead;
			// ALU
			switch (AluOpCode)
			{
				case 0:
					if (ccrneeded)
					{
						printf("alures = %s + %s;\nccrf(ccr, ccr_v, 0);\nif (alures > 0x7fffff) 			{ ccrf(ccr, ccr_v, 1); alures = 0x7fffff; }\n", srcAName, srcBName);
						printf("else if (alures < -0x800000)	{ ccrf(ccr, ccr_v, 1); alures = -0x800000;	}\nccrf(ccr, ccr_n, alures < 0);\n");
						printf("ccrf(ccr, ccr_c,((%s & %s) | (%s & ~alures) | (%s & ~alures)) & 0x800000);\nccrf(ccr, ccr_z, !alures);\nccrf_lt(ccr);\n", srcAName, srcBName, srcAName, srcBName);
					}
					else
					{
						printf("alures = %s + %s;\nif (alures > 0x7fffff) alures = 0x7fffff; else if (alures < -0x800000) alures = -0x800000;\n", srcAName, srcBName);
					}
					break;
				case 1:
					if (ccrneeded)
					{
						printf("alures = %s - %s;\nccrf(ccr, ccr_v, 0);\nif (alures > 0x7fffff) 			{ ccrf(ccr, ccr_v, 1); alures = 0x7fffff; }\n", srcAName, srcBName);
						printf("else if (alures < -0x800000)	{ ccrf(ccr, ccr_v, 1); alures = -0x800000;	}\nccrf(ccr, ccr_n, alures < 0);\n");
						printf("ccrf(ccr, ccr_c,((~%s & %s) | (~%s & alures) | (%s & alures)) & 0x800000);\nccrf(ccr, ccr_z, !alures);\nccrf_lt(ccr);\n", srcAName, srcBName, srcAName, srcBName);
					}
					else
					{
						printf("alures = %s - %s;\nif (alures > 0x7fffff) alures = 0x7fffff; else if (alures < -0x800000) alures = -0x800000;\n", srcAName, srcBName);
					}
					break;
				case 2:
					printf("alures = %s + %s;\n", srcAName, srcBName);
					if (ccrneeded)
					{
						printf("ccrf(ccr, ccr_v, ((%s & %s & ~alures) | (~%s & ~%s & alures)) & 0x800000);\n", srcAName, srcBName, srcAName, srcBName);
						printf("ccrf(ccr, ccr_n, alures < 0);\nccrf(ccr, ccr_c,((%s & %s) | (%s & ~alures) | (%s & ~alures)) & 0x800000);\nccrf(ccr, ccr_z, !alures);\nccrf_lt(ccr);\n", srcAName, srcBName, srcAName, srcBName);
					}
					break;
				case 3:
				case 4:
					printf("alures = %s - %s;\n", srcAName, srcBName);
					if (ccrneeded)
					{
						printf("ccrf(ccr, ccr_v, ((%s & ~%s & ~alures) | (~%s & %s & alures)) & 0x800000);\nccrf(ccr, ccr_n, alures < 0);\n", srcAName, srcBName, srcAName, srcBName);
						printf("ccrf(ccr, ccr_c,((~%s & %s) | (~%s & alures) | (%s & alures)) & 0x800000);\nccrf(ccr, ccr_z, !alures);\nccrf_lt(ccr);\n", srcAName, srcBName, srcAName, srcBName);
					}
					break;
				case 5: printf("alures = %s & %s;\n", srcAName, srcBName); if (ccrneeded) printf("ccrf(ccr, ccr_n, alures & 0x800000); ccrf(ccr, ccr_z, !alures);\n"); break;
				case 6: printf("alures = %s | %s;\n", srcAName, srcBName); if (ccrneeded) printf("ccrf(ccr, ccr_n, alures & 0x800000); ccrf(ccr, ccr_z, !alures);\n"); break;
				case 7: printf("alures = %s ^ %s;\n", srcAName, srcBName); if (ccrneeded) printf("ccrf(ccr, ccr_n, alures & 0x800000); ccrf(ccr, ccr_z, !alures);\n"); break;
				case 8: printf("alures = (%s & 0x800000) ? ~%s : %s;\n", srcBName, srcBName, srcBName); if (ccrneeded) printf("ccrf(ccr, ccr_n, 0); ccrf(ccr, ccr_v, %s & 0x80000000);\n", srcBName); break;
				case 9: printf("alures = %s;\n", srcBName); break;
				case 10:
					if (ccrneeded)
					{
						printf("alures = %s << 2;\n", srcBName);
						printf("ccrf(ccr, ccr_v, 0);\nif (alures > 0x7fffff) 		{ ccrf(ccr, ccr_v, 1); alures = 0x7fffff; }");
						printf("else if (alures < -0x800000){ ccrf(ccr, ccr_v, 1); alures = -0x800000;	}\nccrf(ccr, ccr_n, alures & 0x800000); ccrf(ccr, ccr_z, !alures);\n");
					}
					else
					{
						printf("alures = %s << 2;\n", srcBName);
						printf("if (alures > 0x7fffff)alures = 0x7fffff; else if (alures < -0x800000) alures = -0x800000;\n");
					}
					break;
				case 11:
					if (ccrneeded)
					{
						printf("alures = %s << 8;\n", srcBName);
						printf("ccrf(ccr, ccr_v, 0);\nif (alures > 0x7fffff) 		{ ccrf(ccr, ccr_v, 1); alures = 0x7fffff; }\n");
						printf("else if (alures < -0x800000){ ccrf(ccr, ccr_v, 1); alures = -0x800000;	}\nccrf(ccr, ccr_n, alures & 0x800000); ccrf(ccr, ccr_z, !alures);\n");
					}
					else
					{
						printf("alures = %s << 8;\n", srcBName);
						printf("if (alures > 0x7fffff)alures = 0x7fffff; else if (alures < -0x800000) alures = -0x800000;\n");
						
					}
					break;
				case 12: printf("alures = (%s << 15) & 0x7fffff;\n", srcBName); if (ccrneeded) printf("ccrf(ccr, ccr_c, %s & 0x800000); ccrf(ccr, ccr_n, 0);\n", srcBName); break;
				case 13:
					printf("alures = 0x7fffff - %s;\n", srcBName);
					if (ccrneeded)
					{
						printf("ccrf(ccr, ccr_v, ((0x7fffff & ~%s & ~alures) | (~0x7fffff & %s & alures)) & 0x800000);\n", srcBName, srcBName);
						printf("ccrf(ccr, ccr_n, alures < 0);\nccrf(ccr, ccr_c,((~0x7fffff & %s) | (~0x7fffff & alures) | (%s & alures)) & 0x800000);\n", srcBName, srcBName);
						printf("ccrf(ccr, ccr_z, !alures);\nccrf_lt(ccr);\n");
					}
					break;
				case 14: printf("alures = (%s >> 1) | (%s & 0x800000);\n", srcBName, srcBName); if (ccrneeded) printf("ccrf(ccr, ccr_n, %s & 0x800000); ccrf(ccr, ccr_c, %s & 1);\n", srcBName, srcBName); break;
				case 15:
					printf("gprs[0xf8] = (gprs[0xf8] & 0xffffff) - memsiz - 1; \nif (gprs[0xf8] < 0) gprs[0xf8] = gprs[0xf5] & 0xffffff;\n");
					break;
			}
			writealuresulttogpr = 0xff;
			if (AluOpCode != 4 && AluOpCode != 15)
			{
				if (AluDestToA) writealuresulttogpr = AluA;
				if (flags[i] & kWritesALUToFIFO)
				{
					if (Skip) printf("if (!doskip) {\n\t");
					
					if (dolfill) printf("dol[1] = dol[0];\n");
					printf("dol[0] = se24(alures) >> 8;\n");
					if (dolfill < 2) dolfill++;
					
					if (Skip) printf("}\n");
				}
			}
			if (Skip && AluOpCode != 4) printf("ccr = oldccr;\n");
		}

		if ((flags[last] & kIsRead) && (flags[last] & kAddressIsUsed)) printf("dil = ((int32_t)ram[ramaddress & 0xffff]) << 8;\n");
		if (flags[last] & kIsWrite)
		{
			if (dolfill > 0) dolfill--;
			printf("ram[ramaddress & 0xffff] = dol[%d];\n", dolfill);
		}
		if (!writetoramaddr && (flags[i] & kAddressIsUsed)) printf("ramaddress = address;\n");
	}
}
