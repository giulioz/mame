// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, giulioz
/***************************************************************************

    i8xc196.h

    MCS96, c196 branch, the enhanced 16 bits bus version

***************************************************************************/

#ifndef MAME_CPU_MCS96_I8XC196_H
#define MAME_CPU_MCS96_I8XC196_H

#include "mcs96.h"

class i8xc196_device : public mcs96_device {
public:
	enum {
		EXTINT_LINE = 0, // P0.7
		EXTINT1_LINE,    // P2.2
		NMI_LINE,
		HSI0_LINE,
		HSI1_LINE,
		HSI2_LINE,
		HSI3_LINE,
		T2CLK_LINE,      // P2.3
		T2RST_LINE,      // P2.4
		T2UPDN_LINE,     // P2.6
		T2CAP_LINE       // P2.7
	};

	enum {
		I8XC196_HSI_MODE = MCS96_LAST_REG + 1,
		I8XC196_HSI_STATUS,
		I8XC196_HSO_TIME,
		I8XC196_HSO_COMMAND,
		I8XC196_AD_COMMAND,
		I8XC196_AD_RESULT,
		I8XC196_PORT1,
		I8XC196_PORT2,
		I8XC196_PWM_CONTROL,
		I8XC196_SBUF_RX,
		I8XC196_SBUF_TX,
		I8XC196_SP_CON,
		I8XC196_SP_STAT,
		I8XC196_BAUD_RATE,
		I8XC196_IOC0,
		I8XC196_IOC1,
		I8XC196_IOC2,
		I8XC196_IOS0,
		I8XC196_IOS1,
		I8XC196_IOS2,
		I8XC196_WSR,
		I8XC196_INT_PENDING_1,
		I8XC196_INT_MASK_1
	};

	auto ach0_cb() { return m_ach_cb[0].bind(); }
	auto ach1_cb() { return m_ach_cb[1].bind(); }
	auto ach2_cb() { return m_ach_cb[2].bind(); }
	auto ach3_cb() { return m_ach_cb[3].bind(); }
	auto ach4_cb() { return m_ach_cb[4].bind(); }
	auto ach5_cb() { return m_ach_cb[5].bind(); }
	auto ach6_cb() { return m_ach_cb[6].bind(); }
	auto ach7_cb() { return m_ach_cb[7].bind(); }
	auto hso_cb() { return m_hso_cb.bind(); }
	auto serial_tx_cb() { return m_serial_tx_cb.bind(); }
	auto pwm_cb() { return m_pwm_cb.bind(); }

	auto in_p0_cb() { return m_in_p0_cb.bind(); }
	auto out_p1_cb() { return m_out_p1_cb.bind(); }
	auto in_p1_cb() { return m_in_p1_cb.bind(); }
	auto out_p2_cb() { return m_out_p2_cb.bind(); }
	auto in_p2_cb() { return m_in_p2_cb.bind(); }

	void serial_w(u16 val);

protected:
	i8xc196_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock);

	virtual void device_start() override;
	virtual void device_reset() override;
	virtual uint32_t execute_min_cycles() const noexcept override;
	virtual uint32_t execute_max_cycles() const noexcept override;

	// virtual uint32_t execute_input_lines() const noexcept override { return 5; }
	virtual void execute_set_input(int linenum, int state) override;

	virtual std::unique_ptr<util::disasm_interface> create_disassembler() override;

	virtual void do_exec_full() override;
	virtual void do_exec_partial() override;
	virtual void internal_update(u64 current_time) override;
	virtual void check_irq() override;
	virtual bool is_196() const override { return true; }

	void internal_regs(address_map &map);
	void ad_command_w(u8 data);
	u8 ad_result_r(offs_t offset);
	void hsi_mode_w(u8 data);
	void hso_time_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 hsi_time_r();
	void hso_command_w(u8 data);
	u8 hsi_status_r();
	void sbuf_w(u8 data);
	u8 sbuf_r();
	void watchdog_w(u8 data);
	void ioc2_w(u8 data);
	u16 timer1_r();
	u16 timer2_r();
	void timer2_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	void baud_rate_w(u8 data);
	u8 port0_r();
	void port1_w(u8 data);
	u8 port1_r();
	void port2_w(u8 data);
	u8 port2_r();
	void sp_con_w(u8 data);
	u8 sp_stat_r();
	void ioc0_w(u8 data);
	u8 ios0_r();
	void ios0_w(u8 data);
	void ioc1_w(u8 data);
	u8 ios1_r();
	void pwm_control_w(u8 data);
	u8 int_pending_1_r();
	void int_pending_1_w(u8 data);
	u8 int_mask_1_r();
	void int_mask_1_w(u8 data);
	u8 wsr_r();
	void wsr_w(u8 data);
	u8 ios2_r();

private:
	enum {
		IRQ_TIMER  = 0x01,
		IRQ_AD     = 0x02,
		IRQ_HSI    = 0x04,
		IRQ_HSO    = 0x08,
		IRQ_HSI0   = 0x10,
		IRQ_SOFT   = 0x20,
		IRQ_SERIAL = 0x40,
		IRQ_EXTINT = 0x80,

		IRQ_TI       = 0x01,
		IRQ_RI       = 0x02,
		IRQ_HSI4     = 0x04,
		IRQ_T2CAP    = 0x08,
		IRQ_T2OVF    = 0x10,
		IRQ_EXTINT1  = 0x20,
		IRQ_FIFOFULL = 0x40,
		IRQ_NMI      = 0x80
	};

	struct hso_cam_entry {
		u8 command;
		u16 time;
		u64 deadline;
	};

	struct hsi_fifo_entry {
		u16 time;
		u8 events;
	};

	devcb_read16::array<8> m_ach_cb;
	devcb_write8 m_hso_cb;
	devcb_write_line m_serial_tx_cb;
	devcb_write_line m_pwm_cb;

	devcb_read8 m_in_p0_cb;
	devcb_write8 m_out_p1_cb;
	devcb_read8 m_in_p1_cb;
	devcb_write8 m_out_p2_cb;
	devcb_read8 m_in_p2_cb;
	//devcb_write16 m_out_p3_p4_cb;
	//devcb_read16 m_in_p3_p4_cb;

	hso_cam_entry hso_info[8];
	hso_cam_entry hso_cam_hold;
	hsi_fifo_entry hsi_fifo[8];

	u64 timer1_base, ad_busy_set, ad_done, timer1_expire;
	u16 timer1_base_value, timer2_value, timer2_capture;
	u8 hsi_mode, hsi_status, hso_command, ad_command, hso_active;
	u8 hsi_count, hsi_transition_count[4];
	u16 hso_time, ad_result, ad_pending_result;
	u8 pwm_control, pwm_latch, pwm_counter;
	u64 pwm_next;
	bool pwm_state;
	u8 port1, port2;
	u8 ios0, ios1, ios2, ioc0, ioc1, ioc2, ppw;
	bool extint, extint1, nmi, t2clk, t2rst, t2updn, t2cap, timer2_resetting;
	u8 sbuf, sp_con, sp_stat;
	bool serial_rx_full;
	u8 serial_send_buf, serial_pending_buf;
	u8 serial_tx_bit, serial_tx_stop_bit;
	bool serial_tx_active, serial_tx_pending;
	u64 serial_send_timer;
	u16 baud_reg;
	bool brh;
	u32 serial_external_count;
	uint8_t pending_irq_1;
	uint8_t mask_irq_1;
	bool irq_requested_1;
	uint8_t wsr, wsr_control;
	u8 watchdog_key;
	bool watchdog_enabled;
	u64 watchdog_base, watchdog_expire, powerdown_start;
	bool idle, powerdown;

	u16 timer_value(int timer, u64 current_time) const;
	u64 timer_time_until(int timer, u64 current_time, u16 target) const;
	void timer2_reset(u64 current_time);
	void timer2_clock(u64 current_time);
	void timer2_capture_event();
	void set_hsi_state(int pin, bool state);
	void hsi_push(int pin, u64 current_time);
	void update_hsi_status();
	void commit_hso_cam();
	void trigger_cam(int id, u64 current_time);
	void trigger_timer2_cam(u64 current_time);
	void clear_hso_cam();
	void set_hso(u8 mask, bool state);
	void ad_start(u64 current_time);
	void serial_send(u8 data);
	void serial_tick(u64 current_time);
	u64 serial_bit_period() const;
	void pwm_update(u64 current_time);
	void timer1_w_byte(int offset, u8 data);
	void device_reset_from_watchdog();
	void unimplemented_opcode();
	void enter_powerdown();
	void leave_powerdown();

#define O(o) void o ## _196_full(); void o ## _196_partial()

	O(bmov_direct_2w);
	O(cmpl_direct_2w);
	O(djnzw_wrrel8);
	O(idlpd_immed_1b);
	O(pop_indexed_1w);
	O(pop_indirect_1w);
	O(popa_none);
	O(pusha_none);
	O(fetch);

#undef O
};

class c80c196kb_device : public i8xc196_device {
public:
	c80c196kb_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);
};

DECLARE_DEVICE_TYPE(C80C196KB, c80c196kb_device)

#endif // MAME_CPU_MCS96_I8XC196_H
