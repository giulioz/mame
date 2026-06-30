// license:BSD-3-Clause
// copyright-holders:AJR
/**********************************************************************

    NEC 78K/III series 16/8-bit single-chip microcontrollers

**********************************************************************/

#ifndef MAME_CPU_UPD78K_UPD78K3_H
#define MAME_CPU_UPD78K_UPD78K3_H

#pragma once

//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// ======================> upd78k3_device

class upd78k3_device : public cpu_device
{
public:
	enum {
		UPD78K3_PC,
		UPD78K3_PSW, UPD78K3_PSWL, UPD78K3_PSWH, UPD78K3_RBS, UPD78K3_SP,
		UPD78K3_RP0, UPD78K3_RP1, UPD78K3_RP2, UPD78K3_RP3,
		UPD78K3_RP4, UPD78K3_RP5, UPD78K3_RP6, UPD78K3_RP7,
		UPD78K3_AX, UPD78K3_BC,
		UPD78K3_VP, UPD78K3_UP, UPD78K3_DE, UPD78K3_HL,
		UPD78K3_R0, UPD78K3_R1, UPD78K3_R2, UPD78K3_R3,
		UPD78K3_R4, UPD78K3_R5, UPD78K3_R6, UPD78K3_R7,
		UPD78K3_R8, UPD78K3_R9, UPD78K3_R10, UPD78K3_R11,
		UPD78K3_R12, UPD78K3_R13, UPD78K3_R14, UPD78K3_R15,
		UPD78K3_X, UPD78K3_A, UPD78K3_C, UPD78K3_B,
		UPD78K3_VPL, UPD78K3_VPH, UPD78K3_UPL, UPD78K3_UPH,
		UPD78K3_E, UPD78K3_D, UPD78K3_L, UPD78K3_H
	};
	enum interrupt_lines : int {
		NMI_LINE,
		INT0_LINE,
		INT1_LINE,
		INT2_LINE
	};

	// TODO: callbacks and configuration thereof

protected:
	upd78k3_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock, address_map_constructor mem_map, address_map_constructor sfr_map);

	// device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// device_execute_interface overrides
	virtual void execute_run() override;
	virtual void execute_set_input(int inputnum, int state) override;
	virtual bool execute_input_edge_triggered(int inputnum) const noexcept override { return inputnum == NMI_LINE; }
	virtual u64 execute_clocks_to_cycles(u64 clocks) const noexcept override { return (clocks + 2 - 1) / 2; }
	virtual u64 execute_cycles_to_clocks(u64 cycles) const noexcept override { return (cycles * 2); }

	// device_memory_interface overrides
	virtual space_config_vector memory_space_config() const override;

	// device_state_interface overrides
	virtual void state_string_export(const device_state_entry &entry, std::string &str) const override;

	virtual void state_add_psw();
	virtual void execute_peripherals(int cycles) { }
	virtual int pending_internal_interrupt() const { return -1; }
	virtual void acknowledge_internal_interrupt(int vector) { }

private:
	// internal memory map
	void iram_map(address_map &map) ATTR_COLD;

	// internal helpers
	inline u8 register_base() const noexcept;
	u8 fetch();
	u16 fetch_word();
	u8 read_byte(u16 address);
	void write_byte(u16 address, u8 data);
	u16 read_word(u16 address);
	void write_word(u16 address, u16 data);
	u8 read_sfr(u8 address);
	void write_sfr(u8 address, u8 data);
	u16 read_sfrp(u8 address);
	void write_sfrp(u8 address, u16 data);
	u8 read_saddr(u8 address);
	void write_saddr(u8 address, u8 data);
	u16 read_saddrp(u8 address);
	void write_saddrp(u8 address, u16 data);
	u8 get_r(unsigned index) const;
	void set_r(unsigned index, u8 data);
	u16 get_rp(unsigned index) const;
	void set_rp(unsigned index, u16 data);
	unsigned ax_index() const noexcept { return BIT(m_psw, 5) ? 2 : 0; }
	unsigned bc_index() const noexcept { return BIT(m_psw, 5) ? 3 : 1; }
	unsigned x_index() const noexcept { return BIT(m_psw, 5) ? 4 : 0; }
	unsigned a_index() const noexcept { return BIT(m_psw, 5) ? 5 : 1; }
	unsigned c_index() const noexcept { return BIT(m_psw, 5) ? 6 : 2; }
	unsigned b_index() const noexcept { return BIT(m_psw, 5) ? 7 : 3; }
	u16 alu8(unsigned operation, u8 lhs, u8 rhs);
	u32 alu16(unsigned operation, u16 lhs, u16 rhs);
	void set_logic_flags(u8 result);
	void push_word(u16 data);
	u16 pop_word();
	void execute_one();
	void execute_01(u8 op2);
	void execute_02(u8 op1, u8 op2);
	void execute_05(u8 op2);
	void execute_06(u8 op2);
	void execute_07(u8 op2);
	void execute_08(u8 op2);
	void execute_09(u8 op2);
	void execute_0a(u8 op2);
	void execute_15(u8 op2);
	void execute_16(u8 op1, u8 op2);
	bool take_interrupt();
	void illegal(u8 op1, int op2 = -1);
protected:
	u8 iram_byte_r(offs_t offset);
	void iram_byte_w(offs_t offset, u8 data);

private:
	// address spaces, caches & configuration
	address_space_config m_program_config;
	address_space_config m_iram_config;
	address_space_config m_sfr_config;
	required_shared_ptr<u16> m_iram;

	memory_access<16, 0, 0, ENDIANNESS_LITTLE>::cache m_program_cache;
	memory_access< 8, 1, 0, ENDIANNESS_LITTLE>::cache m_iram_cache;
	memory_access<16, 0, 0, ENDIANNESS_LITTLE>::specific m_program_space;
	memory_access< 8, 1, 0, ENDIANNESS_LITTLE>::specific m_sfr_space;

	// core registers and execution state
	u16 m_pc;
	u16 m_ppc;
protected:
	u16 m_psw;
private:
	u16 m_sp;
	u8 m_ccw;
	u8 m_irq_state;
	s32 m_icount;
};

// ======================> upd78312_device

class upd78312_device : public upd78k3_device
{
public:
	// device type constructor
	upd78312_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);

	template <unsigned N> auto port_in_cb() { return m_port_in_cb[N].bind(); }
	template <unsigned N> auto port_out_cb() { return m_port_out_cb[N].bind(); }

protected:
	upd78312_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock, address_map_constructor map);

	// device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// device_disasm_interface overrides
	virtual std::unique_ptr<util::disasm_interface> create_disassembler() override;

	// device_state_interface overrides
	virtual void state_string_export(const device_state_entry &entry, std::string &str) const override;

	// upd78k3_device overrides
	virtual void state_add_psw() override;
	virtual void execute_peripherals(int cycles) override;
	virtual int pending_internal_interrupt() const override;
	virtual void acknowledge_internal_interrupt(int vector) override;

private:
	// type-specific internal memory maps
	void mem_map(address_map &map) ATTR_COLD;
	void sfr_map(address_map &map) ATTR_COLD;
	u8 port_r(offs_t offset);
	void port_w(offs_t offset, u8 data);
	u8 port_mode_r(offs_t offset);
	void port_mode_w(offs_t offset, u8 data);
	void update_port_output(unsigned port);
	u8 timer0_control_r();
	void timer0_control_w(u8 data);
	u8 timer0_interrupt_r();
	void timer0_interrupt_w(u8 data);

	devcb_read8::array<6> m_port_in_cb;
	devcb_write8::array<6> m_port_out_cb;
	u8 m_port_latch[6];
	u8 m_port_mode[6];
	u8 m_timer0_control;
	u8 m_timer0_interrupt;
	s32 m_timer0_countdown;
	bool m_timer0_pending;
};

// ======================> upd78310_device

class upd78310_device : public upd78312_device
{
public:
	// device type constructor
	upd78310_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);
};

// device type declarations
DECLARE_DEVICE_TYPE(UPD78310, upd78310_device)
DECLARE_DEVICE_TYPE(UPD78312, upd78312_device)

#endif // MAME_CPU_UPD78K_UPD78K3_H
