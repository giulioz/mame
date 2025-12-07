// license:BSD-3-Clause
// copyright-holders:Christian Brunschen
/**********************************************************************************************
 *
 *   es5510.h - Ensoniq ES5510 (ESP) driver
 *   by Christian Brunschen
 *
 **********************************************************************************************/

#ifndef MAME_CPU_ES5510_ES5510_H
#define MAME_CPU_ES5510_ES5510_H

#pragma once

class es5510_device : public cpu_device {
public:
	// TODO : Not verified, Most of games are using 128KB DRAM.
	static constexpr uint32_t DRAM_SIZE = (1<<20);
	static constexpr uint32_t DRAM_MASK = (DRAM_SIZE-1);

	es5510_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	uint8_t host_r(address_space &space, offs_t offset);
	void host_w(offs_t offset, uint8_t data);

	int16_t ser_r(int offset);
	void ser_w(int offset, int16_t data);

	enum line_t {
		ES5510_HALT = 0
	};

	enum state_t {
		STATE_RUNNING = 0,
		STATE_HALTED = 1
	};

	struct alu_op_t {
		int operands;
		const char * const opcode;
	};

	enum op_src_dst_t {
		SRC_DST_REG =   1 << 0,
		SRC_DST_DELAY = 1 << 1,
		SRC_DST_BOTH =  (1 << 0) | (1 << 1)
	};

	struct op_select_t {
		const op_src_dst_t alu_src;
		const op_src_dst_t alu_dst;
		const op_src_dst_t mac_src;
		const op_src_dst_t mac_dst;
	};

	enum ram_control_access_t {
		RAM_CONTROL_DELAY = 0,
		RAM_CONTROL_TABLE_A,
		RAM_CONTROL_TABLE_B,
		RAM_CONTROL_IO
	};

	enum ram_cycle_t {
		RAM_CYCLE_READ = 0,
		RAM_CYCLE_WRITE = 1,
		RAM_CYCLE_DUMP_FIFO = 2
	};

	struct ram_control_t {
		ram_cycle_t cycle;
		ram_control_access_t access;
		const char * const description;
	};

	static const alu_op_t ALU_OPS[16];
	static const op_select_t OPERAND_SELECT[16];
	static const ram_control_t RAM_CONTROL[8];

	struct alu_t {
		uint8_t aReg;
		uint8_t bReg;
		op_src_dst_t src;
		op_src_dst_t dst;
		uint8_t op;
		int32_t aValue;
		int32_t bValue;
		int32_t result;
		bool update_ccr;
		bool write_result;
	};

	struct mulacc_t {
		uint8_t cReg;
		uint8_t dReg;
		op_src_dst_t src;
		op_src_dst_t dst;
		bool accumulate;
		int32_t cValue;
		int32_t dValue;
		int64_t product;
		int64_t result;
		bool write_result;
	};

	struct ram_t {
		int32_t address;     // up to 20 bits, left-justified within the right 24 bits of the 32-bit word
		bool io;           // I/O space, rather than delay line memory
		ram_cycle_t cycle; // cycle type
	};

	// direct access to the 'HALT' pin - not just through the
	void set_HALT(bool halt) { halt_asserted = halt; }
	bool get_HALT() { return halt_asserted; }

	void run_once();
	void transpile();

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual space_config_vector memory_space_config() const override;
	virtual uint64_t execute_clocks_to_cycles(uint64_t clocks) const noexcept override;
	virtual uint64_t execute_cycles_to_clocks(uint64_t cycles) const noexcept override;
	virtual uint32_t execute_min_cycles() const noexcept override;
	virtual uint32_t execute_max_cycles() const noexcept override;
	virtual void execute_run() override;
	virtual void execute_set_input(int linenum, int state) override;
	virtual std::unique_ptr<util::disasm_interface> create_disassembler() override;

private:
	int icount;
	bool halt_asserted;

	void handle_instr(int opaddr);

	inline int32_t se24(int32_t x) {return (x & 0x800000) ? (x | 0xff000000) : (x & 0x7fffff);}
	inline int32_t se24(int64_t x) {return se24((int32_t)x);}
	void writeReg(uint8_t which, int32_t val)
	{
		if (which == 0xf4) {memsiz = host_gpr & 0xffffff;return;}
		else if (which == 0xf2)
		{
			mulacc &= ~0xffffff;
			mulacc |= (val & 0xffffff);
		}
		else if (which == 0xf3)
		{
			mulacc &= 0xffffff;
			mulacc |= (int64_t)(val) << 24;
		}
		if (which < 0xfc) gprs[which] = val;
	}
	void ccrf(int32_t &ccr, int32_t flag, bool set)
	{
		ccr &= ~flag;
		if (set) ccr |= flag;
	}
	void ccrf_lt(int32_t &ccr) {ccrf(ccr, ccr_lt, ((ccr & ccr_v) ? 1 : 0) != ((ccr & ccr_n) ? 1 : 0));}
	const char *regName(uint8_t which, bool read = true)
	{
		const char *names[22] = {"SER0R", "SER0L", "SER1R", "SER1L", "SER2R", "SER2L", "SER3R", "SER3L", "MACL", "MACH",
			"DIL", "DLENGTH", "ABASE", "BBASE", "DBASE", "SIGREG", "CCR", "CMR", "MINUS 1", "MIN (-1)", "MAX (1)", "Zero"};
		if (!read && which == 244) return "MEMSIZ";
		if (which >= 234) return names[which - 234];
		static char buf[32]; snprintf(buf, 32, "GPR%d", which);
		return buf;
	}
	void fifoout(int32_t x)
	{
		dol[1] = dol[0];
		dol[0] = x >> 8;
		if (dolfill < 2) dolfill++;
	}
	// Host interface
	uint32_t host_gpr {0}, host_dil {0}, host_dol {0}, host_dadr {0};
	uint64_t host_instr {};
	uint8_t hostregs[0x100] {};
	
	// Core
	enum ccr_flags
	{
		ccr_not = (1 << 18),
		ccr_z = (1 << 19),
		ccr_lt = (1 << 20),
		ccr_v = (1 << 21),
		ccr_c = (1 << 22),
		ccr_n = (1 << 23),
	};
	uint64_t instructions[0xa0] {};
	int32_t gprs[0x100] {}, alures {}, memsiz {}, ccr {};
	int64_t mulacc;
	int16_t ram[65536] {}, dol[2] {}, dolfill {0};
	uint8_t pc, writealuresulttogpr {};
	struct ram_action {uint32_t addr; bool read; bool flush; bool io;};
	ram_action ramact;
};

DECLARE_DEVICE_TYPE(ES5510, es5510_device)

#endif // MAME_CPU_ES5510_ES5510_H
