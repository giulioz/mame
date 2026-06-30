// license:BSD-3-Clause
// copyright-holders:AJR

#ifndef MAME_CPU_FR_FR_H
#define MAME_CPU_FR_FR_H

#pragma once

class fr_cpu_device : public cpu_device
{
public:
	enum {
		FR_PC, FR_PS, FR_CCR, FR_ILM,
		FR_TBR, FR_RP, FR_SSP, FR_USP,
		FR_MD, FR_MDH, FR_MDL,
		FR_R0, FR_R1, FR_R2, FR_R3,
		FR_R4, FR_R5, FR_R6, FR_R7,
		FR_R8, FR_R9, FR_R10, FR_R11,
		FR_R12, FR_R13, FR_R14, FR_R15
	};

protected:
	// construction/destruction
	fr_cpu_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock, int addrbits, address_map_constructor map);

	// device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// device_execute_interface overrides
	virtual u32 execute_min_cycles() const noexcept override { return 1; }
	virtual u32 execute_max_cycles() const noexcept override { return 36; }
	virtual bool execute_input_edge_triggered(int inputnum) const noexcept override { return inputnum == INPUT_LINE_NMI; }
	virtual void execute_run() override;
	virtual void execute_set_input(int inputnum, int state) override;

	// device_disasm_interface overrides
	virtual std::unique_ptr<util::disasm_interface> create_disassembler() override;

	// device_memory_interface overrides
	virtual space_config_vector memory_space_config() const override;

	// device_state_interface overrides
	void state_string_export(const device_state_entry &entry, std::string &str) const override;

	void set_irq_level(unsigned source, u8 level);
	void set_irq_pending(unsigned source, bool pending);

private:
	enum : u32
	{
		PS_C    = 0x00000001,
		PS_V    = 0x00000002,
		PS_Z    = 0x00000004,
		PS_N    = 0x00000008,
		PS_I    = 0x00000010,
		PS_S    = 0x00000020,
		PS_T    = 0x00000100,
		PS_D0   = 0x00000200,
		PS_D1   = 0x00000400,
		PS_ILM  = 0x001f0000,
		PS_MASK = 0x001f073f
	};

	u32 get_reg(unsigned reg) const noexcept;
	void set_reg(unsigned reg, u32 value) noexcept;
	u32 get_dr(unsigned reg) const noexcept;
	void set_dr(unsigned reg, u32 value) noexcept;
	void set_ps(u32 value) noexcept;

	u8 read_byte(u32 address);
	u16 read_word(u32 address);
	u32 read_dword(u32 address);
	void write_byte(u32 address, u8 data);
	void write_word(u32 address, u16 data);
	void write_dword(u32 address, u32 data);

	void set_nz(u32 value);
	void set_nz(u32 value, unsigned bits);
	u32 add_flags(u32 left, u32 right, bool carry);
	u32 sub_flags(u32 left, u32 right, bool borrow);
	bool condition(unsigned condition) const noexcept;
	void schedule_delayed_branch(u32 target, bool taken = true) noexcept;
	void take_exception(unsigned vector, u32 return_pc, bool clear_i, int level = -1, bool trace_handler = false);
	bool check_interrupts();
	bool trace_suppressed() const noexcept;
	void execute_one(u16 opcode, bool in_delay_slot);
	void undefined_instruction(bool in_delay_slot);

	// address space
	address_space_config m_space_config;
	memory_access<24, 2, 0, ENDIANNESS_BIG>::cache m_cache;
	memory_access<24, 2, 0, ENDIANNESS_BIG>::specific m_space;

	// internal state
	u32 m_regs[17]; // includes both SSP and USP
	u32 m_pc;
	u32 m_ppc;
	u32 m_ps;
	u32 m_tbr;
	u32 m_rp;
	u64 m_md;
	u32 m_resource[16];
	u32 m_delay_target;
	bool m_delay_pending;
	bool m_reset_pending;
	bool m_exception_taken;
	bool m_trace_inhibit;
	u8 m_eit_stack[64];
	u8 m_eit_depth;
	u64 m_irq_pending;
	u8 m_irq_level[48];
	bool m_nmi_pending;
	s32 m_icount;
};

class mb91103_device : public fr_cpu_device
{
public:
	mb91103_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);

	template <unsigned Port> auto read_port() { return m_port_read[Port].bind(); }
	template <unsigned Port> auto write_port() { return m_port_write[Port].bind(); }
	template <unsigned Channel> auto read_adc() { return m_adc_read[Channel].bind(); }
	template <unsigned Channel> auto write_uart() { return m_uart_write[Channel].bind(); }
	void external_interrupt(unsigned line);
	void uart_receive(unsigned channel, u8 data);
	void input_capture(unsigned channel, int state);
	void udc_input(unsigned channel, unsigned input, int state);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	void internal_map(address_map &map) ATTR_COLD;
	u32 io_r(offs_t offset, u32 mem_mask = ~0U);
	void io_w(offs_t offset, u32 data, u32 mem_mask = ~0U);
	u8 io_byte_r(u16 address);
	void io_byte_w(u16 address, u8 data);
	TIMER_CALLBACK_MEMBER(adc_complete);
	TIMER_CALLBACK_MEMBER(reload_timer_underflow);
	TIMER_CALLBACK_MEMBER(free_timer_event);
	TIMER_CALLBACK_MEMBER(udc_underflow);
	TIMER_CALLBACK_MEMBER(utimer_underflow);
	TIMER_CALLBACK_MEMBER(uart_transmit_complete);
	void adc_start(u16 control);
	void reload_timer_start(unsigned channel);
	u16 reload_timer_count(unsigned channel) const;
	void free_timer_sync();
	void free_timer_schedule();
	void free_timer_update_irq(unsigned channel);
	unsigned free_timer_divider() const;
	void udc_sync(unsigned channel);
	void udc_start(unsigned channel);
	void udc_step(unsigned channel, int direction);
	void udc_update_irq(unsigned channel);
	bool udc_gate_open(unsigned channel) const;
	void utimer_start(unsigned channel);
	u16 utimer_count(unsigned channel) const;
	void uart_update_irq(unsigned channel);
	u32 uart_frame_clocks(unsigned channel) const;
	int port_index(u16 address) const noexcept;
	u16 port_ddr_address(unsigned port) const noexcept;

	devcb_read8::array<14> m_port_read;
	devcb_write8::array<14> m_port_write;
	devcb_read16::array<8> m_adc_read;
	devcb_write8::array<2> m_uart_write;
	emu_timer *m_adc_timer;
	emu_timer *m_reload_timer[2];
	emu_timer *m_free_timer;
	emu_timer *m_udc_timer[2];
	emu_timer *m_utimer[2];
	emu_timer *m_uart_timer[2];
	u8 m_adc_channel;
	u8 m_adc_end;
	u8 m_adc_mode;
	bool m_adc_active;
	u16 m_free_count;
	attotime m_free_time;
	u16 m_udc_count[2];
	u8 m_udc_input[2];
	u8 m_icu_input;
	bool m_utimer_phase[2];
	u8 m_io[0x800];
};

class mb91f155a_device : public fr_cpu_device
{
public:
	// device type constructor
	mb91f155a_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);

private:
	void internal_map(address_map &map) ATTR_COLD;
};

// device type declaration
DECLARE_DEVICE_TYPE(MB91F155A, mb91f155a_device)
DECLARE_DEVICE_TYPE(MB91103, mb91103_device)

#endif // MAME_CPU_FR_FR_H
