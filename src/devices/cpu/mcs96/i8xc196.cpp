// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, giulioz
/***************************************************************************

    i8xc196.h

    MCS96, c196 branch, the enhanced 16 bits bus version

***************************************************************************/

#include "emu.h"
#include "i8xc196.h"
#include "i8xc196d.h"

i8xc196_device::i8xc196_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock) :
	mcs96_device(mconfig, type, tag, owner, clock, 16, address_map_constructor(FUNC(i8xc196_device::internal_regs), this)),
	m_ach_cb(*this, 0),
	m_hso_cb(*this),
	m_serial_tx_cb(*this),
	m_pwm_cb(*this),
	m_in_p0_cb(*this, 0),
	m_out_p1_cb(*this), m_in_p1_cb(*this, 0xff),
	m_out_p2_cb(*this), m_in_p2_cb(*this, 0xc2),
	timer1_base(0), ad_busy_set(0), ad_done(0), timer1_expire(0), timer1_base_value(0), timer2_value(0), timer2_capture(0),
	hsi_mode(0), hsi_status(0), hso_command(0), ad_command(0), hso_active(0), hsi_count(0), hso_time(0), ad_result(0), ad_pending_result(0),
	pwm_control(0), pwm_latch(0), pwm_counter(0), pwm_next(0), pwm_state(false),
	port1(0), port2(0),
	ios0(0), ios1(0), ios2(0), ioc0(0), ioc1(0), ioc2(0), ppw(0),
	extint(false), extint1(false), nmi(false), t2clk(false), t2rst(false), t2updn(false), t2cap(false), timer2_resetting(false),
	sbuf(0), sp_con(0), sp_stat(0), serial_rx_full(false), serial_send_buf(0), serial_pending_buf(0), serial_tx_bit(0), serial_tx_stop_bit(0),
	serial_tx_active(false), serial_tx_pending(false), serial_send_timer(0), baud_reg(0), brh(false), serial_external_count(0),
	pending_irq_1(0), mask_irq_1(0), irq_requested_1(false), wsr(0), wsr_control(0),
	watchdog_key(0), watchdog_enabled(false), watchdog_base(0), watchdog_expire(0), powerdown_start(0), idle(false), powerdown(false)
{
	for (auto &hso : hso_info)
	{
		hso.command = 0;
		hso.time = 0;
		hso.deadline = 0;
	}
	hso_cam_hold.command = 0;
	hso_cam_hold.time = 0;
	hso_cam_hold.deadline = 0;
	std::fill(std::begin(hsi_transition_count), std::end(hsi_transition_count), 0);
	for (auto &entry : hsi_fifo)
		entry = { 0, 0 };
}

std::unique_ptr<util::disasm_interface> i8xc196_device::create_disassembler()
{
	return std::make_unique<i8xc196_disassembler>();
}

uint32_t i8xc196_device::execute_min_cycles() const noexcept
{
	return 4;
}

uint32_t i8xc196_device::execute_max_cycles() const noexcept
{
	return (6 + 14 * 0x10000) * 2;
}

void i8xc196_device::device_start()
{
	mcs96_device::device_start();
	cycles_scaling = 2;

	state_add(I8XC196_HSI_MODE,    "HSI_MODE",    hsi_mode);
	state_add<u8>(I8XC196_HSI_STATUS, "HSI_STATUS",
					[this]() -> u8 { return hsi_status; },
					[this](u8 data) { hsi_status = (data & 0x55) | (hsi_status & 0xaa); });
	state_add(I8XC196_HSO_TIME,      "HSO_TIME",    hso_time);
	state_add(I8XC196_HSO_COMMAND,   "HSO_COMMAND", hso_command);
	state_add(I8XC196_AD_COMMAND,    "AD_COMMAND",  ad_command).mask(0xf);
	state_add(I8XC196_AD_RESULT,     "AD_RESULT",   ad_result);
	state_add(I8XC196_PWM_CONTROL,   "PWM_CONTROL", pwm_latch);
	state_add(I8XC196_SBUF_RX,       "SBUF_RX",     sbuf);
	state_add(I8XC196_SBUF_TX,       "SBUF_TX",     serial_send_buf);
	state_add(I8XC196_SP_CON,        "SP_CON",      sp_con).mask(0x1f);
	state_add(I8XC196_SP_STAT,       "SP_STAT",     sp_stat).mask(0xfc);
	state_add(I8XC196_BAUD_RATE,     "BAUD_RATE",   baud_reg);
	state_add<u8>(I8XC196_PORT1,     "PORT1",     [this]() -> u8 { return port1; }, [this](u8 data) { port1_w(data); });
	state_add<u8>(I8XC196_PORT2,     "PORT2",       [this]() -> u8 { return port2; }, [this](u8 data) { port2_w(data); });
	state_add(I8XC196_IOC0,          "IOC0",        ioc0).mask(0xfd);
	state_add(I8XC196_IOC1,          "IOC1",        ioc1);
	state_add(I8XC196_IOC2,          "IOC2",        ioc2).mask(0x7f);
	state_add(I8XC196_IOS0,          "IOS0",        ios0);
	state_add(I8XC196_IOS1,          "IOS1",        ios1);
	state_add(I8XC196_IOS2,          "IOS2",        ios2);
	state_add<u8>(I8XC196_WSR,       "WSR",         [this]() -> u8 { return wsr_r(); }, [this](u8 data) { wsr_w(data); });
	state_add(I8XC196_INT_PENDING_1, "INT_PENDING_1",      pending_irq_1);
	state_add(I8XC196_INT_MASK_1,    "INT_MASK_1",         mask_irq_1);

	save_item(STRUCT_MEMBER(hso_info, command));
	save_item(STRUCT_MEMBER(hso_info, time));
	save_item(STRUCT_MEMBER(hso_info, deadline));
	save_item(NAME(hso_cam_hold.command));
	save_item(NAME(hso_cam_hold.time));
	save_item(NAME(hso_cam_hold.deadline));
	save_item(STRUCT_MEMBER(hsi_fifo, time));
	save_item(STRUCT_MEMBER(hsi_fifo, events));

	save_item(NAME(timer1_base));
	save_item(NAME(timer1_base_value));
	save_item(NAME(timer2_value));
	save_item(NAME(timer2_capture));
	save_item(NAME(ad_busy_set));
	save_item(NAME(ad_done));
	save_item(NAME(timer1_expire));
	save_item(NAME(hsi_mode));
	save_item(NAME(hsi_status));
	save_item(NAME(hsi_count));
	save_item(NAME(hsi_transition_count));
	save_item(NAME(hso_command));
	save_item(NAME(ad_command));
	save_item(NAME(hso_active));
	save_item(NAME(hso_time));
	save_item(NAME(ad_result));
	save_item(NAME(ad_pending_result));
	save_item(NAME(port1));
	save_item(NAME(port2));
	save_item(NAME(pwm_control));
	save_item(NAME(pwm_latch));
	save_item(NAME(pwm_counter));
	save_item(NAME(pwm_next));
	save_item(NAME(pwm_state));
	save_item(NAME(ios0));
	save_item(NAME(ios1));
	save_item(NAME(ios2));
	save_item(NAME(ioc0));
	save_item(NAME(ioc1));
	save_item(NAME(ioc2));
	save_item(NAME(ppw));
	save_item(NAME(extint));
	save_item(NAME(extint1));
	save_item(NAME(nmi));
	save_item(NAME(t2clk));
	save_item(NAME(t2rst));
	save_item(NAME(t2updn));
	save_item(NAME(t2cap));
	save_item(NAME(timer2_resetting));
	save_item(NAME(sbuf));
	save_item(NAME(sp_con));
	save_item(NAME(sp_stat));
	save_item(NAME(serial_rx_full));
	save_item(NAME(serial_send_buf));
	save_item(NAME(serial_pending_buf));
	save_item(NAME(serial_tx_bit));
	save_item(NAME(serial_tx_stop_bit));
	save_item(NAME(serial_tx_active));
	save_item(NAME(serial_tx_pending));
	save_item(NAME(serial_send_timer));
	save_item(NAME(baud_reg));
	save_item(NAME(brh));
	save_item(NAME(serial_external_count));
	save_item(NAME(pending_irq_1));
	save_item(NAME(mask_irq_1));
	save_item(NAME(wsr));
	save_item(NAME(wsr_control));
	save_item(NAME(irq_requested_1));
	save_item(NAME(watchdog_key));
	save_item(NAME(watchdog_enabled));
	save_item(NAME(watchdog_base));
	save_item(NAME(watchdog_expire));
	save_item(NAME(powerdown_start));
	save_item(NAME(idle));
	save_item(NAME(powerdown));
}

void i8xc196_device::device_reset()
{
	mcs96_device::device_reset();
	memset(register_file.target(), 0xff, register_file.bytes());
	clear_hso_cam();
	hso_command = 0;
	hso_time = 0;
	timer1_base = total_cycles();
	timer1_base_value = 0;
	timer2_value = timer2_capture = 0;
	timer1_expire = timer_time_until(1, total_cycles(), 0);
	port1 = 0xff;
	port2 = 0xc1; // P2.5 is cleared
	ios0 = ios1 = ios2 = 0x00;
	ioc0 = ioc2 = 0x00;
	ioc1 = 0x21;
	hsi_mode = 0xff;
	hsi_status &= 0xaa;
	hsi_count = 0;
	std::fill(std::begin(hsi_transition_count), std::end(hsi_transition_count), 0);
	ad_command = 0;
	ad_result = ad_pending_result = 0x7ff0;
	ad_busy_set = ad_done = 0;
	pwm_control = pwm_latch = pwm_counter = 0x00;
	pwm_state = false;
	pwm_next = m_pwm_cb.isunset() ? 0 : total_cycles() + cycles_scaling;
	sp_con = 0x0b;
	sp_stat = 0x08;
	sbuf = 0;
	serial_rx_full = false;
	serial_tx_active = serial_tx_pending = false;
	serial_send_timer = 0;
	serial_external_count = 0;
	brh = false;
	m_out_p1_cb(0xff);
	m_out_p2_cb(0xc1);
	m_hso_cb(0);
	m_serial_tx_cb(1);
	m_pwm_cb(0);
	extint = extint1 = nmi = false;
	t2clk = t2rst = t2updn = t2cap = false;
	timer2_resetting = false;
	pending_irq_1 = 0;
	mask_irq_1 = 0;
	wsr = 0;
	wsr_control = 0;
	irq_requested_1 = false;
	watchdog_key = 0;
	watchdog_enabled = false;
	watchdog_base = total_cycles();
	watchdog_expire = 0;
	powerdown_start = 0;
	idle = powerdown = false;
}

void i8xc196_device::commit_hso_cam()
{
	for(int i=0; i<8; i++)
		if(!BIT(hso_active, i)) {
			hso_active |= 1 << i;
			if(hso_active == 0xff)
				ios0 |= 0x40;
			hso_info[i].command = hso_command;
			hso_info[i].time = hso_time;
			hso_info[i].deadline = BIT(hso_command, 6) ? 0 : timer_time_until(1, total_cycles(), hso_time);
			internal_update(total_cycles());
			return;
		}
	ios0 |= 0xc0;
	hso_cam_hold.command = hso_command;
	hso_cam_hold.time = hso_time;
	hso_cam_hold.deadline = 0;
}

void i8xc196_device::ad_start(u64 current_time)
{
	ad_pending_result = ad_command & 7;
	if (m_ach_cb[ad_command & 7].isunset())
		logerror("Analog input on ACH%d not configured\n", ad_command & 7);
	else
		ad_pending_result |= (m_ach_cb[ad_command & 7]() & 0x3ff) << 6;
	ad_busy_set = current_time + 8 * cycles_scaling;
	ad_done = current_time + (BIT(ioc2, 4) ? 91 : 158) * cycles_scaling;
	internal_update(current_time);
}

u64 i8xc196_device::serial_bit_period() const
{
	if (!BIT(baud_reg, 15))
		return 0;
	return u64((baud_reg & 0x7fff) + 1) * ((sp_con & 3) ? 16 : 2);
}

void i8xc196_device::serial_send(u8 data)
{
	sp_stat &= ~0x08;
	if (serial_tx_active)
	{
		serial_pending_buf = data;
		serial_tx_pending = true;
		return;
	}

	serial_send_buf = data;
	serial_tx_active = true;
	serial_tx_bit = 0;
	serial_tx_stop_bit = (sp_con & 3) >= 2 ? 10 : ((sp_con & 3) ? 9 : 8);
	m_serial_tx_cb((sp_con & 3) ? 0 : BIT(data, 0));
	const u64 period = serial_bit_period();
	serial_send_timer = period ? total_cycles() + period : 0;
	serial_external_count = 0;
}

void i8xc196_device::serial_tick(u64 current_time)
{
	const u8 mode = sp_con & 3;
	const u64 period = serial_bit_period();
	serial_tx_bit++;
	if (serial_tx_bit < serial_tx_stop_bit)
	{
		if (!mode)
			m_serial_tx_cb(BIT(serial_send_buf, serial_tx_bit));
		else if (serial_tx_bit <= 8 && !(mode == 1 && BIT(sp_con, 2) && serial_tx_bit == 8))
			m_serial_tx_cb(BIT(serial_send_buf, serial_tx_bit - 1));
		else
		{
			const unsigned data_mask = mode == 1 ? 0x7f : 0xff;
			const bool parity_enabled = BIT(sp_con, 2) && mode != 2;
			m_serial_tx_cb(parity_enabled ? (std::popcount(unsigned(serial_send_buf) & data_mask) & 1) : BIT(sp_con, 4));
		}
	}
	else if (serial_tx_bit == serial_tx_stop_bit)
	{
		m_serial_tx_cb(1);
		sp_con &= ~0x10;
		sp_stat |= 0x20;
		pending_irq |= IRQ_SERIAL;
		pending_irq_1 |= IRQ_TI;
		check_irq();
	}
	else
	{
		if (serial_tx_pending)
		{
			serial_send_buf = serial_pending_buf;
			serial_tx_pending = false;
			serial_tx_bit = 0;
			serial_tx_stop_bit = mode >= 2 ? 10 : (mode ? 9 : 8);
			m_serial_tx_cb(mode ? 0 : BIT(serial_send_buf, 0));
		}
		else
		{
			serial_tx_active = false;
			sp_stat |= 0x08;
			serial_send_timer = 0;
			return;
		}
	}
	serial_send_timer = period ? current_time + period : 0;
}

void i8xc196_device::internal_regs(address_map &map)
{
	map(0x00, 0x01).lr16([] () -> u16 { return 0; }, "r0").nopw();
	map(0x02, 0x03).r(FUNC(i8xc196_device::ad_result_r)); // 8-bit access
	map(0x02, 0x02).w(FUNC(i8xc196_device::ad_command_w));
	map(0x03, 0x03).w(FUNC(i8xc196_device::hsi_mode_w));
	map(0x04, 0x05).rw(FUNC(i8xc196_device::hsi_time_r), FUNC(i8xc196_device::hso_time_w)); // 16-bit access
	map(0x06, 0x06).rw(FUNC(i8xc196_device::hsi_status_r), FUNC(i8xc196_device::hso_command_w));
	map(0x07, 0x07).rw(FUNC(i8xc196_device::sbuf_r), FUNC(i8xc196_device::sbuf_w));
	map(0x08, 0x08).rw(FUNC(i8xc196_device::int_mask_r), FUNC(i8xc196_device::int_mask_w));
	map(0x09, 0x09).rw(FUNC(i8xc196_device::int_pending_r), FUNC(i8xc196_device::int_pending_w));
	map(0x0a, 0x0b).r(FUNC(i8xc196_device::timer1_r)); // 16-bit access
	map(0x0c, 0x0d).rw(FUNC(i8xc196_device::timer2_r), FUNC(i8xc196_device::timer2_w)); // 16-bit access
	map(0x0a, 0x0a).w(FUNC(i8xc196_device::watchdog_w));
	map(0x0b, 0x0b).w(FUNC(i8xc196_device::ioc2_w));
	map(0x0e, 0x0e).rw(FUNC(i8xc196_device::port0_r), FUNC(i8xc196_device::baud_rate_w));
	map(0x0f, 0x0f).rw(FUNC(i8xc196_device::port1_r), FUNC(i8xc196_device::port1_w));
	map(0x10, 0x10).rw(FUNC(i8xc196_device::port2_r), FUNC(i8xc196_device::port2_w));
	map(0x11, 0x11).rw(FUNC(i8xc196_device::sp_stat_r), FUNC(i8xc196_device::sp_con_w));
	map(0x12, 0x12).rw(FUNC(i8xc196_device::int_pending_1_r), FUNC(i8xc196_device::int_pending_1_w));
	map(0x13, 0x13).rw(FUNC(i8xc196_device::int_mask_1_r), FUNC(i8xc196_device::int_mask_1_w));
	map(0x14, 0x14).rw(FUNC(i8xc196_device::wsr_r), FUNC(i8xc196_device::wsr_w));
	map(0x15, 0x15).rw(FUNC(i8xc196_device::ios0_r), FUNC(i8xc196_device::ioc0_w));
	map(0x16, 0x16).rw(FUNC(i8xc196_device::ios1_r), FUNC(i8xc196_device::ioc1_w));
	map(0x17, 0x17).rw(FUNC(i8xc196_device::ios2_r), FUNC(i8xc196_device::pwm_control_w));
	map(0x18, 0xff).ram().share("register_file");
}

void i8xc196_device::ad_command_w(u8 data)
{
	if (wsr == 15)
	{
		ad_result = (ad_result & 0xff00) | data;
		return;
	}
	if (wsr != 0)
		return;
	ad_command = data & 0xf;
	if (ad_command & 8)
		ad_start(total_cycles());
}

u8 i8xc196_device::ad_result_r(offs_t offset)
{
	if (wsr == 15)
		return offset ? hsi_mode : ad_command;
	if (wsr != 0)
		return 0xff;
	return ad_result >> (offset ? 8 : 0);
}

void i8xc196_device::hsi_mode_w(u8 data)
{
	if (wsr == 15)
	{
		ad_result = (ad_result & 0x00ff) | (u16(data) << 8);
		return;
	}
	if (wsr != 0)
		return;
	hsi_mode = data;
}

void i8xc196_device::hso_time_w(offs_t offset, u16 data, u16 mem_mask)
{
	if (wsr == 14)
	{
		if (ACCESSING_BITS_0_7)
			ppw = data;
		return;
	}
	if (wsr == 15)
	{
		u16 time = hsi_count ? hsi_fifo[0].time : 0;
		time = (time & ~mem_mask) | (data & mem_mask);
		if (!hsi_count)
		{
			hsi_count = 1;
			hsi_fifo[0].events = 0;
		}
		hsi_fifo[0].time = time;
		update_hsi_status();
		return;
	}
	if (wsr != 0)
		return;
	hso_time = (hso_time & ~mem_mask) | (data & mem_mask);
	commit_hso_cam();
}

u16 i8xc196_device::hsi_time_r()
{
	if (wsr == 15)
		return hso_time;
	if (wsr != 0 || !hsi_count)
		return 0xffff;
	const u16 result = hsi_fifo[0].time;
	if (!machine().side_effects_disabled())
	{
		const bool refill = hsi_count > 1;
		for (int i = 1; i < hsi_count; i++)
			hsi_fifo[i - 1] = hsi_fifo[i];
		hsi_count--;
		update_hsi_status();
		if (refill && !BIT(ioc1, 7))
		{
			pending_irq |= IRQ_HSI;
			check_irq();
		}
	}
	return result;
}

void i8xc196_device::hso_command_w(u8 data)
{
	if (wsr == 15)
	{
		if (!hsi_count)
		{
			hsi_count = 1;
			hsi_fifo[0] = { 0, u8(data & 0x55) };
		}
		else
			hsi_fifo[0].events = data & 0x55;
		update_hsi_status();
		return;
	}
	if (wsr != 0)
		return;
	hso_command = data;
}

u8 i8xc196_device::hsi_status_r()
{
	if (wsr == 15)
		return hso_command;
	if (wsr != 0)
		return 0xff;
	return hsi_status;
}

void i8xc196_device::sbuf_w(u8 data)
{
	if (wsr == 15)
	{
		sbuf = data;
		return;
	}
	if (wsr != 0)
		return;
	serial_send(data);
}

u8 i8xc196_device::sbuf_r()
{
	if (wsr == 15)
		return serial_send_buf;
	if (wsr != 0)
		return 0xff;
	if (!machine().side_effects_disabled())
		serial_rx_full = false;
	return sbuf;
}

void i8xc196_device::watchdog_w(u8 data)
{
	if (wsr == 15)
	{
		timer1_w_byte(0, data);
		return;
	}
	if (wsr != 0)
		return;
	if (watchdog_key == 0 && data == 0x1e)
	{
		watchdog_key = 1;
		return;
	}
	if (watchdog_key == 1 && data == 0xe1)
	{
		watchdog_enabled = true;
		watchdog_base = total_cycles();
		watchdog_expire = watchdog_base + u64(cycles_scaling) * 0x10000;
		watchdog_key = 0;
		internal_update(total_cycles());
		return;
	}
	watchdog_key = 0;
}

u16 i8xc196_device::timer1_r()
{
	if (wsr == 15)
	{
		const u64 elapsed = (total_cycles() - watchdog_base) / cycles_scaling;
		return u8(elapsed >> 8) | (u16(ioc2 | 0x80) << 8);
	}
	return wsr == 0 ? timer_value(1, total_cycles()) : 0xffff;
}

u16 i8xc196_device::timer2_r()
{
	if (wsr == 15)
		return timer2_capture;
	return wsr == 0 ? timer2_value : 0xffff;
}

void i8xc196_device::timer2_w(offs_t offset, u16 data, u16 mem_mask)
{
	if (wsr == 15)
		timer2_capture = (timer2_capture & ~mem_mask) | (data & mem_mask);
	else if (wsr == 0)
	{
		timer2_value = (timer2_value & ~mem_mask) | (data & mem_mask);
		trigger_timer2_cam(total_cycles());
	}
}

void i8xc196_device::baud_rate_w(u8 data)
{
	if (wsr != 0)
		return;
	if (brh)
		baud_reg = (baud_reg & 0x00ff) | u16(data) << 8;
	else
		baud_reg = (baud_reg & 0xff00) | data;
	if (!machine().side_effects_disabled())
		brh = !brh;
}

u8 i8xc196_device::port0_r()
{
	return wsr == 0 ? m_in_p0_cb() : 0xff;
}

void i8xc196_device::port1_w(u8 data)
{
	if (wsr != 0)
		return;
	port1 = data;
	m_out_p1_cb(data);
}

u8 i8xc196_device::port1_r()
{
	return wsr == 0 ? m_in_p1_cb() & port1 : 0xff;
}

void i8xc196_device::port2_w(u8 data)
{
	if (wsr != 0)
		return;
	data &= 0xe1;
	port2 = data;
	m_out_p2_cb(data);
}

u8 i8xc196_device::port2_r()
{
	if (wsr != 0)
		return 0xff;
	const u8 input = m_in_p2_cb();
	return (port2 & 0x21) | (input & 0x1e) | (input & port2 & 0xc0);
}

void i8xc196_device::sp_con_w(u8 data)
{
	if (wsr == 15)
	{
		sp_stat |= data & 0xfc;
		return;
	}
	if (wsr != 0)
		return;
	if ((sp_con ^ data) & 3)
	{
		serial_tx_active = serial_tx_pending = false;
		serial_send_timer = 0;
		sp_stat |= 0x08;
		m_serial_tx_cb(1);
	}
	sp_con = data & 0x1f;
}

u8 i8xc196_device::sp_stat_r()
{
	if (wsr == 15)
		return sp_con;
	if (wsr != 0)
		return 0xff;
	u8 res = sp_stat;
	if (!machine().side_effects_disabled())
		sp_stat &= 0x88;
	return res;
}

void i8xc196_device::ioc0_w(u8 data)
{
	if (wsr == 15)
	{
		const u8 changed = (ios0 ^ data) & 0x3f;
		set_hso(changed & ~data, false);
		set_hso(changed & data, true);
		return;
	}
	if (wsr != 0)
		return;
	ioc0 = data & 0xfd;
	if (BIT(data, 1))
		timer2_reset(total_cycles());
}

void i8xc196_device::ioc1_w(u8 data)
{
	if (wsr == 15)
	{
		ios1 |= data & 0x3f;
		return;
	}
	if (wsr != 0)
		return;
	ioc1 = data;
}

void i8xc196_device::ioc2_w(u8 data)
{
	if (wsr == 15)
	{
		timer1_w_byte(1, data);
		return;
	}
	if (wsr != 0)
		return;
	if (BIT(data, 7))
		clear_hso_cam();
	ioc2 = data & 0x7f;
}

u8 i8xc196_device::ios0_r()
{
	if (wsr == 15)
		return ioc0 | 0x02;
	return wsr == 0 ? ios0 : 0xff;
}

u8 i8xc196_device::ios1_r()
{
	if (wsr == 15)
		return ioc1;
	if (wsr != 0)
		return 0xff;
	u8 res = ios1;
	if (!machine().side_effects_disabled())
		ios1 = ios1 & 0xc0;
	return res;
}

u8 i8xc196_device::ios2_r()
{
	if (wsr == 15)
		return pwm_latch;
	if (wsr != 0)
		return 0xff;
	const u8 result = ios2;
	if (!machine().side_effects_disabled())
		ios2 = 0;
	return result;
}

u8 i8xc196_device::int_pending_1_r()
{
	return pending_irq_1;
}

void i8xc196_device::int_pending_1_w(u8 data)
{
	pending_irq_1 = data;
	check_irq();
}

u8 i8xc196_device::int_mask_1_r()
{
	return mask_irq_1;
}

void i8xc196_device::int_mask_1_w(u8 data)
{
	mask_irq_1 = data;
	check_irq();
}

u8 i8xc196_device::wsr_r()
{
	return wsr | wsr_control;
}

void i8xc196_device::wsr_w(u8 data)
{
	const u8 window = data & 0x0f;
	if (window == 0 || window == 14 || window == 15)
	{
		wsr = window;
		wsr_control = data & 0xf0;
	}
}

void i8xc196_device::pwm_control_w(u8 data)
{
	if (wsr == 15)
	{
		ios2 |= data;
		return;
	}
	if (wsr == 0)
		pwm_latch = data;
}

void i8xc196_device::do_exec_partial()
{
}

void i8xc196_device::serial_w(u16 val)
{
	if (!BIT(sp_con, 3))
		return;
	// RI is a latched status flag and remains set until SP_STAT is read.  It
	// does not indicate whether SBUF has been consumed.  The firmware is free
	// to read SBUF without reading SP_STAT, so keep the buffer occupancy as a
	// separate state for receive-overrun detection.
	if (serial_rx_full)
		sp_stat |= 0x04;
	sbuf = val;
	serial_rx_full = true;
	const u8 mode = sp_con & 3;
	if (mode >= 2 && !(mode == 3 && BIT(sp_con, 2)))
	{
		if (BIT(val, 8))
			sp_stat |= 0x80;
		else
			sp_stat &= ~0x80;
	}
	else if (BIT(sp_con, 2))
		sp_stat &= ~0x80; // byte injection has no parity-error indication
	if (mode == 2 && !BIT(val, 8))
		return;
	sp_stat |= 0x40;
	pending_irq |= IRQ_SERIAL;
	pending_irq_1 |= IRQ_RI;
	check_irq();
}

static constexpr unsigned TIMER_SCALING = 3;

u16 i8xc196_device::timer_value(int timer, u64 current_time) const
{
	if (timer == 2)
		return timer2_value;
	return timer1_base_value + ((current_time - timer1_base) / (cycles_scaling << TIMER_SCALING));
}

u64 i8xc196_device::timer_time_until(int timer, u64 current_time, u16 target) const
{
	if (timer == 2)
		return 0;
	const u64 period = cycles_scaling << TIMER_SCALING;
	const u64 elapsed = current_time - timer1_base;
	u32 delta = u16(target - timer_value(1, current_time));
	if (!delta)
		delta = 0x10000;
	return current_time - (elapsed % period) + u64(delta) * period;
}

void i8xc196_device::timer2_reset(u64 current_time)
{
	timer2_value = 0;
	if (!timer2_resetting)
	{
		timer2_resetting = true;
		trigger_timer2_cam(current_time);
		timer2_resetting = false;
	}
}

void i8xc196_device::timer2_clock(u64 current_time)
{
	const u16 old = timer2_value;
	if (BIT(ioc2, 1) && t2updn)
		timer2_value--;
	else
		timer2_value++;

	const bool overflow = BIT(ioc2, 5)
		? ((old == 0x7fff && timer2_value == 0x8000) || (old == 0x8000 && timer2_value == 0x7fff))
		: ((old == 0xffff && timer2_value == 0x0000) || (old == 0x0000 && timer2_value == 0xffff));
	if (overflow)
	{
		ios1 |= 0x10;
		pending_irq_1 |= IRQ_T2OVF;
		if (BIT(ioc1, 3))
			pending_irq |= IRQ_TIMER;
		check_irq();
	}
	trigger_timer2_cam(current_time);
	internal_update(current_time);
}

void i8xc196_device::timer2_capture_event()
{
	timer2_capture = timer2_value;
	pending_irq_1 |= IRQ_T2CAP;
	check_irq();
}

void i8xc196_device::update_hsi_status()
{
	hsi_status = (hsi_status & 0xaa) | (hsi_count ? hsi_fifo[0].events : 0);
	if (hsi_count)
		ios1 |= 0x80;
	else
		ios1 &= ~0x80;
	if (hsi_count >= 7)
		ios1 |= 0x40;
	else
		ios1 &= ~0x40;
}

void i8xc196_device::hsi_push(int pin, u64 current_time)
{
	const u16 time = timer_value(1, current_time);
	const u8 event = 1 << (pin * 2);
	if (hsi_count && hsi_fifo[hsi_count - 1].time == time)
	{
		hsi_fifo[hsi_count - 1].events |= event;
		update_hsi_status();
		return;
	}
	if (hsi_count == 8)
		return;

	const u8 old_count = hsi_count;
	hsi_fifo[hsi_count++] = { time, event };
	update_hsi_status();
	if (!old_count && !BIT(ioc1, 7))
		pending_irq |= IRQ_HSI;
	if (old_count < 5 && hsi_count >= 5)
		pending_irq_1 |= IRQ_HSI4;
	if (old_count < 7 && hsi_count >= 7)
	{
		pending_irq_1 |= IRQ_FIFOFULL;
		if (BIT(ioc1, 7))
			pending_irq |= IRQ_HSI;
	}
	check_irq();
}

void i8xc196_device::set_hsi_state(int pin, bool state)
{
	const bool old = BIT(hsi_status, pin * 2 + 1);
	if (old == state)
		return;
	if (state)
		hsi_status |= 2 << (pin * 2);
	else
		hsi_status &= ~(2 << (pin * 2));
	if (powerdown)
		return;

	if (pin == 0 && state)
	{
		pending_irq |= IRQ_HSI0;
		if (BIT(ioc0, 3) && BIT(ioc0, 5))
			timer2_reset(total_cycles());
		check_irq();
	}
	if (pin == 1 && BIT(ioc0, 7))
		timer2_clock(total_cycles());

	if (!BIT(ioc0, pin * 2))
		return;
	const u8 mode = (hsi_mode >> (pin * 2)) & 3;
	bool capture = (mode == 1 && state) || (mode == 2 && !state) || mode == 3;
	if (mode == 0 && state)
	{
		hsi_transition_count[pin] = (hsi_transition_count[pin] + 1) & 7;
		capture = !hsi_transition_count[pin];
	}
	if (capture)
		hsi_push(pin, total_cycles());
}

void i8xc196_device::clear_hso_cam()
{
	hso_active = 0;
	ios0 &= 0x3f;
	for (auto &entry : hso_info)
		entry.deadline = 0;
	hso_cam_hold.deadline = 0;
}

void i8xc196_device::trigger_timer2_cam(u64 current_time)
{
	const bool already_matching = timer2_resetting;
	timer2_resetting = true;
	u8 matches = 0;
	for (int i = 0; i < 8; i++)
		if (BIT(hso_active, i) && BIT(hso_info[i].command, 6) && hso_info[i].time == timer2_value)
			matches |= 1 << i;
	for (int i = 0; i < 8; i++)
		if (BIT(matches, i))
			trigger_cam(i, current_time);
	if (!already_matching)
		timer2_resetting = false;
}

void i8xc196_device::trigger_cam(int id, u64 current_time)
{
	hso_cam_entry &cam = hso_info[id];
	const u8 command = cam.command;
	const bool locked = BIT(command, 7) && BIT(ioc2, 6);
	if (hso_active == 0xff && !BIT(ios0, 7) && !locked)
		ios0 &= 0xbf;
	if (!locked)
		hso_active &= ~(1 << id);
	switch(command & 0x0f) {
	case 0x0: case 0x1: case 0x2: case 0x3: case 0x4: case 0x5:
		ios2 |= 1 << (command & 7);
		set_hso(1 << (command & 7), BIT(command, 5));
		break;

	case 0x6:
		ios2 |= 0x03;
		set_hso(0x03, BIT(command, 5));
		break;

	case 0x7:
		ios2 |= 0x0c;
		set_hso(0x0c, BIT(command, 5));
		break;

	case 0x8: case 0x9: case 0xa: case 0xb:
		ios1 |= 1 << (command & 3);
		break;

	case 0xe:
		ios2 |= 0x40;
		timer2_reset(current_time);
		break;

	case 0xf:
		ios2 |= 0x80;
		ad_start(current_time);
		break;

	default:
		logerror("HSO action %x undefined\n", command & 0x0f);
		break;
	}

	if (BIT(command, 4))
	{
		pending_irq |= BIT(command, 3) ? IRQ_SOFT : IRQ_HSO;
		check_irq();
	}
	if (locked && !BIT(command, 6))
		cam.deadline = timer_time_until(1, current_time, cam.time);
}

void i8xc196_device::set_hso(u8 mask, bool state)
{
	if (!mask)
		return;
	if(state)
		ios0 |= mask;
	else
		ios0 &= ~mask;
	m_hso_cb(0, ios0 & 0x3f, mask);
}

void i8xc196_device::timer1_w_byte(int offset, u8 data)
{
	u16 value = timer_value(1, total_cycles());
	if (offset)
		value = (value & 0x00ff) | (u16(data) << 8);
	else
		value = (value & 0xff00) | data;
	timer1_base = total_cycles();
	timer1_base_value = value;
	timer1_expire = timer_time_until(1, timer1_base, 0);
	for (int i = 0; i < 8; i++)
		if (BIT(hso_active, i) && !BIT(hso_info[i].command, 6))
			hso_info[i].deadline = timer_time_until(1, timer1_base, hso_info[i].time);
	internal_update(total_cycles());
}

void i8xc196_device::pwm_update(u64 current_time)
{
	if (!pwm_next || current_time < pwm_next)
		return;
	const u64 tick = u64(cycles_scaling) << BIT(ioc2, 2);
	do
	{
		pwm_counter++;
		if (!pwm_counter)
		{
			pwm_control = pwm_latch;
			pwm_state = pwm_control != 0;
			m_pwm_cb(pwm_state);
		}
		else if (pwm_counter == pwm_control && pwm_state)
		{
			pwm_state = false;
			m_pwm_cb(0);
		}
		pwm_next += tick;
	}
	while (current_time >= pwm_next);
}

void i8xc196_device::internal_update(u64 current_time)
{
	if (powerdown)
	{
		recompute_bcount(0);
		return;
	}
	const u64 timer_period = (u64(cycles_scaling) << TIMER_SCALING) * 0x10000;
	if (timer1_expire && current_time >= timer1_expire)
	{
		do
			timer1_expire += timer_period;
		while (current_time >= timer1_expire);
		ios1 |= 0x20;
		if (BIT(ioc1, 2))
		{
			pending_irq |= IRQ_TIMER;
			check_irq();
		}
	}

	for (int i = 0; i < 8; i++)
		if (BIT(hso_active, i) && !BIT(hso_info[i].command, 6) && current_time >= hso_info[i].deadline)
				trigger_cam(i, current_time);

	if (ad_busy_set && current_time >= ad_busy_set)
	{
		ad_busy_set = 0;
		ad_result |= 8;
	}

	if (ad_done && current_time >= ad_done)
	{
		ad_busy_set = 0;
		ad_done = 0;
		ad_result = ad_pending_result & ~8;
		pending_irq |= IRQ_AD;
		check_irq();
	}

	if (serial_send_timer && current_time >= serial_send_timer)
		serial_tick(current_time);
	pwm_update(current_time);

	if (watchdog_expire && current_time >= watchdog_expire)
	{
		device_reset_from_watchdog();
		return;
	}

	u64 event_time = 0;
	for (int i = 0; i < 8; i++)
	{
		if (!BIT(hso_active, i) && BIT(ios0, 7))
		{
			hso_info[i] = hso_cam_hold;
			hso_info[i].deadline = BIT(hso_info[i].command, 6) ? 0 : timer_time_until(1, current_time, hso_info[i].time);
			hso_active |= 1 << i;
			ios0 &= 0x7f;
			if (hso_active == 0xff)
				ios0 |= 0x40;
		}
		if (BIT(hso_active, i) && !BIT(hso_info[i].command, 6))
		{
			const u64 deadline = hso_info[i].deadline;
			if (!event_time || deadline < event_time)
				event_time = deadline;
		}
	}

	if (ad_busy_set && (!event_time || ad_busy_set < event_time))
		event_time = ad_busy_set;
	if (ad_done && (!event_time || ad_done < event_time))
		event_time = ad_done;
	if (serial_send_timer && (!event_time || serial_send_timer < event_time))
		event_time = serial_send_timer;
	if (pwm_next && (!event_time || pwm_next < event_time))
		event_time = pwm_next;
	if (watchdog_expire && (!event_time || watchdog_expire < event_time))
		event_time = watchdog_expire;
	if (timer1_expire && (!event_time || timer1_expire < event_time))
		event_time = timer1_expire;

	recompute_bcount(event_time);
}

void i8xc196_device::device_reset_from_watchdog()
{
	device_reset();
}

void i8xc196_device::enter_powerdown()
{
	idle = powerdown = true;
	powerdown_start = total_cycles();
	ad_busy_set = ad_done = 0;
	ad_result &= ~8;
	bcount = 0;
}

void i8xc196_device::leave_powerdown()
{
	if (!powerdown)
		return;

	const u64 delta = total_cycles() - powerdown_start;
	timer1_base += delta;
	if (timer1_expire)
		timer1_expire += delta;
	for (auto &entry : hso_info)
		if (entry.deadline)
			entry.deadline += delta;
	if (serial_send_timer)
		serial_send_timer += delta;
	if (pwm_next)
		pwm_next += delta;
	watchdog_base += delta;
	if (watchdog_expire)
		watchdog_expire += delta;
	powerdown = idle = false;
	internal_update(total_cycles());
}

void i8xc196_device::unimplemented_opcode()
{
	TMP = reg_r16(0x18) - 2;
	reg_w16(0x18, TMP);
	any_w16(TMP, PC);
	PC = any_r16(0x2012);
	next_noirq(TMP < 0x100 ? 16 : 18);
}

void i8xc196_device::check_irq()
{
	irq_requested = (PSW & pending_irq) && (PSW & F_I);
	irq_requested_1 = (pending_irq_1 & IRQ_NMI) || ((mask_irq_1 & pending_irq_1) && (PSW & F_I));
}

void i8xc196_device::execute_set_input(int linenum, int state)
{
	switch(linenum) {
	case EXTINT_LINE:
		if(!extint && state && BIT(ioc1, 1)) {
			leave_powerdown();
			pending_irq |= IRQ_EXTINT;
			check_irq();
		}
		extint = state;
		break;
	case EXTINT1_LINE:
		if(!extint1 && state) {
			if (powerdown && BIT(ioc1, 1))
			{
				extint1 = state;
				break;
			}
			leave_powerdown();
			pending_irq_1 |= IRQ_EXTINT1;
			if (!BIT(ioc1, 1))
				pending_irq |= IRQ_EXTINT;
			check_irq();
		}
		extint1 = state;
		break;
	case NMI_LINE:
		if (!powerdown && !nmi && state)
		{
			pending_irq_1 |= IRQ_NMI;
			check_irq();
		}
		nmi = state;
		break;

	case HSI0_LINE:
		set_hsi_state(0, state);
		break;

	case HSI1_LINE:
		set_hsi_state(1, state);
		break;

	case HSI2_LINE:
		set_hsi_state(2, state);
		break;

	case HSI3_LINE:
		set_hsi_state(3, state);
		break;

	case T2CLK_LINE:
		if (t2clk != bool(state))
		{
			t2clk = state;
			if (powerdown)
				break;
			if (!BIT(ioc0, 7))
				timer2_clock(total_cycles());
			if (state && !BIT(baud_reg, 15) && ++serial_external_count >= u32((baud_reg & 0x7fff) + 1) * ((sp_con & 3) ? 8 : 1))
			{
				serial_external_count = 0;
				if (serial_tx_active)
					serial_tick(total_cycles());
			}
		}
		break;

	case T2RST_LINE:
		if (!powerdown && !t2rst && state && BIT(ioc0, 3) && !BIT(ioc0, 5))
			timer2_reset(total_cycles());
		t2rst = state;
		break;

	case T2UPDN_LINE:
		t2updn = state;
		break;

	case T2CAP_LINE:
		if (!powerdown && !t2cap && state)
			timer2_capture_event();
		t2cap = state;
		break;
	}
}

c80c196kb_device::c80c196kb_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock) :
	i8xc196_device(mconfig, C80C196KB, tag, owner, clock)
{
}

DEFINE_DEVICE_TYPE(C80C196KB, c80c196kb_device, "c80c196kb", "Intel C80C196KB")

#include "cpu/mcs96/i8xc196.hxx"
