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
	m_in_p0_cb(*this, 0),
	m_out_p1_cb(*this), m_in_p1_cb(*this, 0xff),
	m_out_p2_cb(*this), m_in_p2_cb(*this, 0xc2),
	base_timer2(0), ad_done(0), timer1_expire(0), timer2_expire(0), hsi_mode(0), hsi_status(0), hso_command(0), ad_command(0), hso_active(0), hso_time(0), ad_result(0), pwm_control(0),
	port1(0), port2(0),
	ios0(0), ios1(0), ios2(0), ioc0(0), ioc1(0), extint(false), extint1(false),
	sbuf(0), sp_con(0), sp_stat(0), serial_send_buf(0), serial_send_timer(0), baud_reg(0), brh(false), pending_irq_1(0), mask_irq_1(0), irq_requested_1(false), wsr(0)
{
	for (auto &hso : hso_info)
	{
		hso.command = 0;
		hso.time = 0;
	}
	hso_cam_hold.command = 0;
	hso_cam_hold.time = 0;
}

std::unique_ptr<util::disasm_interface> i8xc196_device::create_disassembler()
{
	return std::make_unique<i8xc196_disassembler>();
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
	state_add(I8XC196_PWM_CONTROL,   "PWM_CONTROL", pwm_control);
	state_add(I8XC196_SBUF_RX,       "SBUF_RX",     sbuf);
	state_add(I8XC196_SBUF_TX,       "SBUF_TX",     serial_send_buf);
	state_add(I8XC196_SP_CON,        "SP_CON",      sp_con).mask(0x1f);
	state_add(I8XC196_SP_STAT,       "SP_STAT",     sp_stat).mask(0xe0);
	state_add(I8XC196_BAUD_RATE,     "BAUD_RATE",   baud_reg);
	state_add<u8>(I8XC196_PORT1,     "PORT1",     [this]() -> u8 { return port1; }, [this](u8 data) { port1_w(data); });
	state_add<u8>(I8XC196_PORT2,     "PORT2",       [this]() -> u8 { return port2; }, [this](u8 data) { port2_w(data); });
	state_add(I8XC196_IOC0,          "IOC0",        ioc0).mask(0xfd);
	state_add(I8XC196_IOC1,          "IOC1",        ioc1);
	state_add(I8XC196_IOS1,          "IOS1",        ios1);
	state_add(I8XC196_IOS2,          "IOS2",        ios2);
	state_add(I8XC196_WSR,           "WSR",         wsr);
	state_add(I8XC196_INT_PENDING_1, "INT_PENDING_1",      pending_irq_1);
	state_add(I8XC196_INT_MASK_1,    "INT_MASK_1",         mask_irq_1);

	save_item(STRUCT_MEMBER(hso_info, command));
	save_item(STRUCT_MEMBER(hso_info, time));
	save_item(NAME(hso_cam_hold.command));
	save_item(NAME(hso_cam_hold.time));

	save_item(NAME(base_timer2));
	save_item(NAME(ad_done));
	save_item(NAME(timer1_expire));
	save_item(NAME(timer2_expire));
	save_item(NAME(hsi_mode));
	save_item(NAME(hsi_status));
	save_item(NAME(hso_command));
	save_item(NAME(ad_command));
	save_item(NAME(hso_active));
	save_item(NAME(hso_time));
	save_item(NAME(ad_result));
	save_item(NAME(port1));
	save_item(NAME(port2));
	save_item(NAME(pwm_control));
	save_item(NAME(ios0));
	save_item(NAME(ios1));
	save_item(NAME(ios2));
	save_item(NAME(ioc0));
	save_item(NAME(ioc1));
	save_item(NAME(extint));
	save_item(NAME(extint1));
	save_item(NAME(sbuf));
	save_item(NAME(sp_con));
	save_item(NAME(sp_stat));
	save_item(NAME(serial_send_buf));
	save_item(NAME(serial_send_timer));
	save_item(NAME(baud_reg));
	save_item(NAME(brh));
	save_item(NAME(pending_irq_1));
	save_item(NAME(mask_irq_1));
	save_item(NAME(wsr));
	save_item(NAME(irq_requested_1));
}

void i8xc196_device::device_reset()
{
	mcs96_device::device_reset();
	memset(register_file.target(), 0xff, register_file.bytes());
	hso_active = 0;
	hso_command = 0;
	hso_time = 0;
	timer2_reset(total_cycles());
	port1 = 0xff;
	port2 = 0xc1; // P2.5 is cleared
	ios0 = ios1 = 0x00;
	ioc0 &= 0xaa;
	ioc1 = (ioc1 & 0xae) | 0x01;
	ad_result = 0;
	ad_done = 0;
	pwm_control = 0x00;
	sp_con &= 0x17;
	sp_stat &= 0x80;
	serial_send_timer = 0;
	brh = false;
	m_out_p1_cb(0xff);
	m_out_p2_cb(0xc1);
	m_hso_cb(0);
	extint = false;
	extint1 = false;
	pending_irq_1 = 0;
	mask_irq_1 = 0;
	wsr = 0;
	irq_requested_1 = false;
	timer1_expire = total_cycles() + (u64(cycles_scaling) << 3) * 0x10000;
	timer2_expire = total_cycles() + (u64(cycles_scaling) << 3) * 0x10000;
}

void i8xc196_device::commit_hso_cam()
{
	for(int i=0; i<8; i++)
		if(!BIT(hso_active, i)) {
			//logerror("hso cam %02x %04x in slot %d (%04x)\n", hso_command, hso_time, i, PPC);
			hso_active |= 1 << i;
			if(hso_active == 0xff)
				ios0 |= 0x40;
			hso_info[i].command = hso_command;
			hso_info[i].time = hso_time;
			internal_update(total_cycles());
			return;
		}
	ios0 |= 0xc0;
	hso_cam_hold.command = hso_command;
	hso_cam_hold.time = hso_time;
}

void i8xc196_device::ad_start(u64 current_time)
{
	ad_result = 8 | (ad_command & 7);
	if (m_ach_cb[ad_command & 7].isunset())
		logerror("Analog input on ACH%d not configured\n", ad_command & 7);
	else
		ad_result |= m_ach_cb[ad_command & 7]() << 6;
	ad_done = current_time + 88;
	internal_update(current_time);
}

void i8xc196_device::serial_send(u8 data)
{
	serial_send_buf = data;
	serial_send_timer = total_cycles() + 9600;
}

void i8xc196_device::serial_send_done()
{
	serial_send_timer = 0;
	m_serial_tx_cb(serial_send_buf);
	pending_irq |= IRQ_SERIAL;
	pending_irq_1 |= IRQ_TI;
	sp_stat |= 0x20;
	check_irq();
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
	ad_command = data & 0xf;
	if (ad_command & 8)
		ad_start(total_cycles());
}

u8 i8xc196_device::ad_result_r(offs_t offset)
{
	return ad_result >> (offset ? 8 : 0);
}

void i8xc196_device::hsi_mode_w(u8 data)
{
	hsi_mode = data;
}

void i8xc196_device::hso_time_w(u16 data)
{
	// Also PPW if WSR=14
	hso_time = data;
	commit_hso_cam();
}

u16 i8xc196_device::hsi_time_r()
{
	return 0x0000;
}

void i8xc196_device::hso_command_w(u8 data)
{
	hso_command = data;
}

u8 i8xc196_device::hsi_status_r()
{
	return hsi_status;
}

void i8xc196_device::sbuf_w(u8 data)
{
	logerror("sbuf %02x (%04x)\n", data, PPC);
	serial_send(data);
}

u8 i8xc196_device::sbuf_r()
{
	if (!machine().side_effects_disabled())
		logerror("read sbuf %02x (%04x)\n", sbuf, PPC);
	return sbuf;
}

void i8xc196_device::watchdog_w(u8 data)
{
}

u16 i8xc196_device::timer1_r()
{
	u16 data = timer_value(1, total_cycles());
	return data;
}

u16 i8xc196_device::timer2_r()
{
	// Also T2 capture if WSR=15
	u16 data = timer_value(2, total_cycles());
	return data;
}

void i8xc196_device::timer2_w(u16 data)
{
}

void i8xc196_device::baud_rate_w(u8 data)
{
	if (brh)
		baud_reg = (baud_reg & 0x00ff) | u16(data) << 8;
	else
		baud_reg = (baud_reg & 0xff00) | data;
	if (!machine().side_effects_disabled())
		brh = !brh;
}

u8 i8xc196_device::port0_r()
{
	return m_in_p0_cb();
}

void i8xc196_device::port1_w(u8 data)
{
	port1 = data;
	m_out_p1_cb(data);
}

u8 i8xc196_device::port1_r()
{
	return m_in_p1_cb() & port1;
}

void i8xc196_device::port2_w(u8 data)
{
	data &= 0xe1;
	port2 = data;
	m_out_p2_cb(data);
}

u8 i8xc196_device::port2_r()
{
	// P2.0 and P2.5 are for output only (but can be read back despite what Intel claims?)
	return (m_in_p2_cb() | 0x25) & (port2 | (extint ? 0x1e : 0x1a));
}

void i8xc196_device::sp_con_w(u8 data)
{
	sp_con = data & 0x1f;
}

u8 i8xc196_device::sp_stat_r()
{
	u8 res = sp_stat;
	if (!machine().side_effects_disabled())
		sp_stat &= 0x80;
	return res;
}

void i8xc196_device::ioc0_w(u8 data)
{
	ioc0 = data & 0xfd;
	if (BIT(data, 1))
		timer2_reset(total_cycles());
}

void i8xc196_device::ioc1_w(u8 data)
{
	ioc1 = data;
}

void i8xc196_device::ioc2_w(u8 data)
{
}

u8 i8xc196_device::ios0_r()
{
	return ios0;
}

u8 i8xc196_device::ios1_r()
{
	u8 res = ios1;
	if (!machine().side_effects_disabled())
		ios1 = ios1 & 0xc0;
	return res;
}

u8 i8xc196_device::ios2_r()
{
	// FIXME
	return ios2;
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
	// FIXME
	return wsr;
}

void i8xc196_device::wsr_w(u8 data)
{
	// FIXME
	wsr = data;
}

void i8xc196_device::pwm_control_w(u8 data)
{
	pwm_control = data;
}

void i8xc196_device::do_exec_partial()
{
}

void i8xc196_device::serial_w(u8 val)
{
	sbuf = val;
	sp_stat |= 0x40;
	pending_irq |= IRQ_SERIAL;
	pending_irq_1 |= IRQ_RI;
	check_irq();
	// logerror("serial_w %02x (%04x)\n", val, PPC);
	// logerror("check_irq %02x %02x\n", irq_requested, irq_requested_1);
}

#define TIMER_SCALING 3
u16 i8xc196_device::timer_value(int timer, u64 current_time) const
{
	if(timer == 2)
		current_time -= base_timer2;
	return current_time / (cycles_scaling << TIMER_SCALING);
}

u64 i8xc196_device::timer_time_until(int timer, u64 current_time, u16 timer_value) const
{
	u64 timer_base = timer == 2 ? base_timer2 : 0;
	u64 delta = (current_time - timer_base) / (cycles_scaling << TIMER_SCALING);
	u32 tdelta = u16(timer_value - delta);
	if(!tdelta)
		tdelta = 0x10000;
	return timer_base + ((delta + tdelta) * (cycles_scaling << TIMER_SCALING));
}

void i8xc196_device::timer2_reset(u64 current_time)
{
	base_timer2 = current_time;
	timer2_expire = base_timer2 + (cycles_scaling << TIMER_SCALING) * 0x10000;
}

void i8xc196_device::set_hsi_state(int pin, bool state)
{
	if(pin == 0 && !BIT(hsi_status, 1) && state) {
		pending_irq |= IRQ_HSI0;
		check_irq();
		if((ioc0 & 0x28) == 0x28)
			timer2_reset(total_cycles());
	}

	if(state)
		hsi_status |= 2 << (pin * 2);
	else
		hsi_status &= ~(2 << (pin * 2));
}

void i8xc196_device::trigger_cam(int id, u64 current_time)
{
	hso_cam_entry &cam = hso_info[id];
	if(hso_active == 0xff && !BIT(ios0, 7))
		ios0 &= 0xbf;
	hso_active &= ~(1 << id);
	switch(cam.command & 0x0f) {
	case 0x0: case 0x1: case 0x2: case 0x3: case 0x4: case 0x5:
		set_hso(1 << (cam.command & 7), BIT(cam.command, 5));
		break;

	case 0x6:
		set_hso(0x03, BIT(cam.command, 5));
		break;

	case 0x7:
		set_hso(0x0c, BIT(cam.command, 5));
		break;

	case 0x8: case 0x9: case 0xa: case 0xb:
		ios1 |= 1 << (cam.command & 3);
		break;

	case 0xe:
		timer2_reset(current_time);
		break;

	case 0xf:
		ad_start(current_time);
		break;

	default:
		logerror("HSO action %x undefined\n", cam.command & 0x0f);
		break;
	}

	if(BIT(cam.command, 4))
	{
		pending_irq |= BIT(cam.command, 3) ? IRQ_SOFT : IRQ_HSO;
		check_irq();
	}
}

void i8xc196_device::set_hso(u8 mask, bool state)
{
	if(state)
		ios0 |= mask;
	else
		ios0 &= ~mask;
	m_hso_cb(0, ios0 & 0x3f, mask);
}

void i8xc196_device::internal_update(u64 current_time)
{
	u16 current_timer1 = timer_value(1, current_time);
	u16 current_timer2 = timer_value(2, current_time);

	if (current_time >= timer1_expire)
	{
		do
			timer1_expire += (cycles_scaling << TIMER_SCALING) * 0x10000;
		while (current_time >= timer1_expire);
		// if (!(ios1 & 0x20))
		{
			ios1 |= 0x20;
			if (ioc1 & 0x4)
			{
				pending_irq |= IRQ_TIMER;
				check_irq();
			}
		}
	}
	if (current_time >= timer2_expire)
	{
		do
			timer2_expire += (cycles_scaling << TIMER_SCALING) * 0x10000;
		while (current_time >= timer2_expire);
		// if (!(ios1 & 0x10))
		{
			ios1 |= 0x10;
			if (ioc1 & 0x8)
			{
				pending_irq |= IRQ_TIMER;
				check_irq();
			}
		}
	}

	for(int i=0; i<8; i++)
		if(BIT(hso_active, i)) {
			u8 cmd = hso_info[i].command;
			u16 t = hso_info[i].time;
			if(((cmd & 0x40) && t == current_timer2) ||
				(!(cmd & 0x40) && t == current_timer1)) {
				//logerror("hso cam %02x %04x in slot %d triggered\n", cmd, t, i);
				trigger_cam(i, current_time);
			}
		}

	if(ad_done && current_time >= ad_done) {
		// A/D conversion complete
		ad_done = 0;
		ad_result &= ~8;
		pending_irq |= IRQ_AD;
		check_irq();
	}

	if(serial_send_timer && current_time >= serial_send_timer)
		serial_send_done();

	u64 event_time = 0;
	for(int i=0; i<8; i++) {
		if(!BIT(hso_active, i) && BIT(ios0, 7)) {
			hso_info[i] = hso_cam_hold;
			hso_active |= 1 << i;
			ios0 &= 0x7f;
			if(hso_active == 0xff)
				ios0 |= 0x40;
			logerror("hso cam %02x %04x in slot %d from hold\n", hso_cam_hold.command, hso_cam_hold.time, i);
		}
		if(BIT(hso_active, i)) {
			u64 new_time = timer_time_until(hso_info[i].command & 0x40 ? 2 : 1, current_time, hso_info[i].time);
			if(!event_time || new_time < event_time)
				event_time = new_time;
		}
	}

	if(ad_done && (!event_time || ad_done < event_time))
		event_time = ad_done;

	if(serial_send_timer && (!event_time || serial_send_timer < event_time))
		event_time = serial_send_timer;

	if (!event_time || event_time > timer1_expire)
		event_time = timer1_expire;
	if (!event_time || event_time > timer2_expire)
		event_time = timer2_expire;

	recompute_bcount(event_time);
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
			pending_irq |= IRQ_EXTINT;
			check_irq();
		}
		extint = state;
		break;
	case EXTINT1_LINE:
		if(!extint1 && state) {
			pending_irq_1 |= IRQ_EXTINT1;
			if (!BIT(ioc1, 1))
				pending_irq |= IRQ_EXTINT;
			check_irq();
		}
		extint1 = state;
		break;
	case NMI_LINE:
		if (state)
		{
			pending_irq_1 |= IRQ_NMI;
			check_irq();
		}
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
	}
}

c80c196kb_device::c80c196kb_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock) :
	i8xc196_device(mconfig, C80C196KB, tag, owner, clock)
{
}

DEFINE_DEVICE_TYPE(C80C196KB, c80c196kb_device, "c80c196kb", "Intel C80C196KB")

#include "cpu/mcs96/i8xc196.hxx"
