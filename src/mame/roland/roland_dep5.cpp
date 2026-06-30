// license:BSD-3-Clause
// copyright-holders:giulioz
/*************************************************************************************************

    Roland DEP-5 driver

    Skeleton driver by Giulio Zausa


	Chips to emulate:
	- i8097: MCS-96 CPU
	- MB654119: Custom chorus chip gate array (die shot: https://siliconpr0n.org/archive/doku.php?id=furrtek:roland:r15229844)
	- MB673106: I/O Gate Array (die shot: https://siliconpr0n.org/archive/doku.php?id=furrtek:roland:rdd673106u)
	- MB87126-002: Reverb gate array (die shot: https://siliconpr0n.org/archive/doku.php?id=furrtek:roland:r15229841)
		inner ROM: 35b x 192
		inner RAM: 30b x 256? (160?)
		RAM and ROM addresses are in common

	Port0:
	0: analog FEEDBACK
	1: analog RATE
	2: analog DEPTH
	3: analog ALG SEL
	4: analog PRE DELAY
	5: analog REV TIME
	6: analog HF DAMP
	7: in KEY IN 1

	Port1:
	0: out ~MUTE
	1: in  MONO/~STEREO
	2: out FIP dp
	3: out FIP h
	4: out FIP i
	5: out FIP j
	6: out timing B (SCK)
	7: out timing C (SXD)

	Port2:
	0: (midi txd)
	1: (midi rxd)
	2: in  KEY IN 2
	3: in  EFFECT ON/OFF pedal
	4: in  PRESET SHIFT pedal
	5: out reverb 22 (INCK)
	6: in  reverb 24 (BUSY)
	7: out reverb 23 (ERCL)

	I/O Gate Array:
	0x8XXX: F7
	0x9XXX: F8
	0xAXXX: F9
	0xBXXX: F10
	0xCXXX: RAM
	0xDXXX: Chorus 31 (rate)
	0xEXXX: Chorus 30 (depth)
	0xFXXX: Chorus 32 (delay)

	0xFFE0: timer lsb?
	0xFFE2: timer msb?
	0xFFE4: timer? either 0 or 1
	0xFFE6: timer? either 0 or 1
	0xFFE8: F7-F12? (write 0x00, 0x06, 0x50, 0x79) bit 6??
	0xFFEA: F0,1,2,3,4,5,6,13?  (write scan from 0x00 to 0x40)

	F0: FIP 7G
	F1: FIP 6G
	F2: FIP 5G
	F3: FIP 4G
	F4: FIP 3G
	F5: FIP 2G
	F6: FIP 1G
	F7: FIP g
	F8: FIP f
	F9: FIP e
	F10: FIP d
	F11: FIP c
	F12: FIP b
	F13: FIP a


	# Reverb-CPU communication

	Commands sent to reverb chip:
	- 5B 80 00 B0 85: 								Set predelay to 0 (value in the middle?)
	- 83 2C 47 67 64: 								Set chorus feedback (last 2 is value)
	- 97 93 FF CA 15 4F FF FF D5 F5: 	Set EQ low

	Params protocol:
	- SCK and SXD work similarly to an SPI interface (LSB first and inverted?)
	- After 40 bits (5 bytes) are transfered, BUSY pulses high for a few us

	Init protocol:
	- 80 00 00 00 00
	- 80 00 00 00 00
	- 80 00 00 00 00
	- ff f9 00 00 00
	- INCK pulses low
	- SYNC changes from 2.5MHz to 32kHz

  Maybe RAM content:
  0: eram offs LSB
  1: eram offs MSB (6 bit)
  2: coef LSB
  3: coef MSB (4 bit)


*************************************************************************************************/

#include "emu.h"
#include "bus/midi/midiinport.h"
#include "cpu/mcs96/i8x9x.h"
#include "machine/ram.h"
#include "machine/timer.h"
#include "dep5.lh"
#include "emupal.h"
#include "screen.h"

// U: WRITE
// E: MIDI
// R: VALUE? (04 059)
// T: CHORUS EQ? OUT LEVEL?

namespace {

static INPUT_PORTS_START(dep5)
	PORT_START("KEY0")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("MEMORYNO +") PORT_CODE(KEYCODE_Q)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("MEMORYNO -") PORT_CODE(KEYCODE_W)
	PORT_START("KEY1")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("REVSEL +") PORT_CODE(KEYCODE_E)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("REVSEL -") PORT_CODE(KEYCODE_R)
	PORT_START("KEY2")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("WRITE") PORT_CODE(KEYCODE_T)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("CHORUS EQ") PORT_CODE(KEYCODE_Y)
	PORT_START("KEY3")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("OUT LEVEL") PORT_CODE(KEYCODE_U)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("PARAM EQ") PORT_CODE(KEYCODE_I)
	PORT_START("KEY4")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("MIDI") PORT_CODE(KEYCODE_O)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("VALUE") PORT_CODE(KEYCODE_P)
	PORT_START("KEY5")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("U1") PORT_CODE(KEYCODE_A)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("U2") PORT_CODE(KEYCODE_S)
	PORT_START("KEY6")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("U3") PORT_CODE(KEYCODE_D)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("U4") PORT_CODE(KEYCODE_F)
	PORT_START("KEY7")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("U5") PORT_CODE(KEYCODE_G)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("U6") PORT_CODE(KEYCODE_H)
INPUT_PORTS_END

class dep5_state : public driver_device
{
public:
	dep5_state(const machine_config &mconfig, device_type type, const char *tag);

	void dep5(machine_config &config);

protected:
	virtual void machine_start() override;
	virtual void machine_reset() override;

private:
	required_device<i8x9x_device> m_cpu;
	output_finder<5> m_digit;
	output_finder<16> m_led;
	required_ioport_array<8> m_keys;

	u8 port0_r();
	u8 port1_r();
	void port1_w(u8 value);
	u8 port2_r();
	void port2_w(u8 value);
	
	void ga_e6_w(u8 value);
	void ga_e8_w(u8 value);
	void ga_ea_w(u8 value);

	u16 ach0_r();
	u16 ach1_r();
	u16 ach2_r();
	u16 ach3_r();
	u16 ach4_r();
	u16 ach5_r();
	u16 ach6_r();
	u16 m_analog_feedback = 0;
	u16 m_analog_rate = 0;
	u16 m_analog_depth = 0;
	u16 m_analog_algsel = 0;
	u16 m_analog_predelay = 0;
	u16 m_analog_revtime = 0;
	u16 m_analog_hfdamp = 0;

	void midi_recv(int state);
	bool m_midi_idle = true;
	int m_midi_cnt = 0;
	u8 m_midi_byte = 0;

	void dep5_map(address_map &map);

	u8 m_fip_sel = 0;
	u8 m_btn_sel = 0;

	u8 m_serial_pos = 0;
	bool m_prev_sck = true;
	u8 m_serial_data = 0;
	bool m_busy = false;
	u8 m_serial_data_seq[5] = {0};
	u8 m_serial_data_seq_pos = 0;
};

dep5_state::dep5_state(const machine_config &mconfig, device_type type, const char *tag) :
	driver_device(mconfig, type, tag),
	m_cpu(*this, "maincpu"),
	m_digit(*this, "digit%u", 0U),
	m_led(*this, "led%u", 0U),
	m_keys(*this, "KEY%u", 0)
{
}


void dep5_state::machine_start()
{
	// m_digit.resolve();
	// m_led.resolve();

	m_analog_feedback = 0;
	m_analog_rate = 0;
	m_analog_depth = 0;
	m_analog_algsel = 0;
	m_analog_predelay = 0;
	m_analog_revtime = 0;
	m_analog_hfdamp = 0;
	
	save_item(NAME(m_fip_sel));
	save_item(NAME(m_btn_sel));
	save_item(NAME(m_analog_feedback));
	save_item(NAME(m_analog_rate));
	save_item(NAME(m_analog_depth));
	save_item(NAME(m_analog_algsel));
	save_item(NAME(m_analog_predelay));
	save_item(NAME(m_analog_revtime));
	save_item(NAME(m_analog_hfdamp));
}

void dep5_state::machine_reset()
{
	m_serial_pos = 0;
	m_prev_sck = true;
	m_serial_data = 0;
	m_busy = false;
}

u8 dep5_state::port0_r()
{
	bool keys_1 = m_keys[m_btn_sel]->read() & 1;
	return keys_1 << 7;
}

u8 dep5_state::port1_r()
{
	return 0;
}

void dep5_state::port1_w(u8 value)
{
	bool sck = BIT(value, 6);
	bool sxd = BIT(value, 7);
	// bool inck = BIT(value, 5);

  // printf("%04x: port1 w %02x\n", m_cpu->pc(), value);
	
	if (sck && !m_prev_sck)
	{
		// m_serial_data <<= 1;
		// m_serial_data |= sxd;
		m_serial_data >>= 1;
		m_serial_data |= (sxd << 7);
		m_serial_pos++;
		if (m_serial_pos == 8)
		{
			if (m_serial_data_seq_pos == 4)
			{
				m_serial_data_seq[m_serial_data_seq_pos] = m_serial_data;
				m_serial_data_seq_pos = 0;

        uint8_t loc = ~m_serial_data_seq[0];
        uint16_t memoffs = (m_serial_data_seq[2] << 8) | m_serial_data_seq[1];
        uint16_t coef = (m_serial_data_seq[4] << 8) | m_serial_data_seq[3];
        uint8_t upper = coef >> 12;
        coef &= 0x0fff;
				// printf("sx loc:%02x offs:%02x%02x coef:%02x%02x unk:%02x\n", m_serial_data_seq[0], m_serial_data_seq[2], m_serial_data_seq[1], m_serial_data_seq[4] & 0x0f, m_serial_data_seq[3], m_serial_data_seq[4] >> 4);
				printf("sx %02x%02x%02x%02x loc:%02x offs:%04x coef:%03x cmd:%x p:%x\n", m_serial_data_seq[1], m_serial_data_seq[2], m_serial_data_seq[3], m_serial_data_seq[4], loc, memoffs, coef, upper & 0x3, upper >> 2);
			}
			else
			{
				m_serial_data_seq[m_serial_data_seq_pos] = m_serial_data;
				m_serial_data_seq_pos++;
			}

			m_serial_pos = 0;
			m_serial_data = 0;
		}
	}
	m_prev_sck = sck;
}

u8 dep5_state::port2_r()
{
	if (!machine().side_effects_disabled())
		m_busy = !m_busy;
	
	bool keys_2 = (m_keys[m_btn_sel]->read() >> 1) & 1;
	m_cpu->set_input_line(c8095_90_device::EXTINT_LINE, keys_2 ? ASSERT_LINE : CLEAR_LINE);

	return 0b00011000 | (m_busy << 6) | (keys_2 << 2);
}

void dep5_state::port2_w(u8 value)
{
}

void dep5_state::ga_e6_w(u8 value)
{
	// printf("write e6 %02x\n", value);
}

void dep5_state::ga_e8_w(u8 value)
{
	if (m_fip_sel == 2)
		m_digit[0] = value;
	else if (m_fip_sel == 3)
		m_digit[1] = value;
	else if (m_fip_sel == 5)
		m_digit[2] = value;
	else if (m_fip_sel == 6)
		m_digit[3] = value;
	else if (m_fip_sel == 0)
		m_digit[4] = value;
	else if (m_fip_sel == 1)
	{
		// VAL EQ CRS REV NLR
		m_led[0] = (value >> 0) & 1;
		m_led[1] = (value >> 1) & 1;
		m_led[2] = (value >> 2) & 1;
		m_led[3] = (value >> 3) & 1;
		m_led[4] = (value >> 4) & 1;
		m_led[5] = (value >> 5) & 1;
		m_led[6] = (value >> 6) & 1;
		m_led[7] = (value >> 7) & 1;
	}
	else if (m_fip_sel == 4)
	{
		// R H P S
		m_led[8] = (value >> 0) & 1;
		m_led[9] = (value >> 1) & 1;
		m_led[10] = (value >> 2) & 1;
		m_led[11] = (value >> 3) & 1;
		m_led[12] = (value >> 4) & 1;
		m_led[13] = (value >> 5) & 1;
		m_led[14] = (value >> 6) & 1;
		m_led[15] = (value >> 7) & 1;
	}
}

void dep5_state::ga_ea_w(u8 value)
{
	if (value == 0x00) m_btn_sel = 7;
	if (value == 0x01) m_btn_sel = 6;
	if (value == 0x02) m_btn_sel = 5;
	if (value == 0x04) m_btn_sel = 4;
	if (value == 0x08) m_btn_sel = 3;
	if (value == 0x10) m_btn_sel = 2;
	if (value == 0x20) m_btn_sel = 1;
	if (value == 0x40) m_btn_sel = 0;
	
	u8 g_sel = value & 0x7f;
	if (g_sel == 0x01) m_fip_sel = 6;
	if (g_sel == 0x02) m_fip_sel = 5;
	if (g_sel == 0x04) m_fip_sel = 4;
	if (g_sel == 0x08) m_fip_sel = 3;
	if (g_sel == 0x10) m_fip_sel = 2;
	if (g_sel == 0x20) m_fip_sel = 1;
	if (g_sel == 0x40) m_fip_sel = 0;
}

u16 dep5_state::ach0_r() { return m_analog_feedback; }
u16 dep5_state::ach1_r() { return m_analog_rate; }
u16 dep5_state::ach2_r() { return m_analog_depth; }
u16 dep5_state::ach3_r() { return m_analog_algsel; }
u16 dep5_state::ach4_r() { return m_analog_predelay; }
u16 dep5_state::ach5_r() { return m_analog_revtime; }
u16 dep5_state::ach6_r() { return m_analog_hfdamp; }

void dep5_state::midi_recv(int state)
{
	if (!state && m_midi_idle)
	{
		m_midi_idle = 0;
		m_midi_cnt = 0;
		m_midi_byte = 0;
		return;
	}
	if (!m_midi_idle)
	{
		if (m_midi_cnt < 8)
		{
			m_midi_byte |= state << m_midi_cnt;
			m_midi_cnt++;
		}
		else
		{
			if (state)
			{
				machine().scheduler().synchronize();
				m_cpu->serial_w(m_midi_byte);
			}
			m_midi_idle = true;
		}
	}
}

void dep5_state::dep5_map(address_map &map)
{
	map(0x0000, 0x7fff).rom().region("maincpu", 0x0000);
	map(0xc000, 0xdfff).ram();
	map(0xffe6, 0xffe6).w(FUNC(dep5_state::ga_e6_w));
	map(0xffe8, 0xffe8).w(FUNC(dep5_state::ga_e8_w));
	map(0xffea, 0xffea).w(FUNC(dep5_state::ga_ea_w));
}

void dep5_state::dep5(machine_config &config)
{
	i8x9x_device &maincpu(N8097BH(config, m_cpu, 12_MHz_XTAL));
	maincpu.set_addrmap(AS_PROGRAM, &dep5_state::dep5_map);
	maincpu.in_p0_cb().set(FUNC(dep5_state::port0_r));
	maincpu.in_p1_cb().set(FUNC(dep5_state::port1_r));
	maincpu.out_p1_cb().set(FUNC(dep5_state::port1_w));
	maincpu.in_p2_cb().set(FUNC(dep5_state::port2_r));
	maincpu.out_p2_cb().set(FUNC(dep5_state::port2_w));
	maincpu.ach0_cb().set(FUNC(dep5_state::ach0_r));
	maincpu.ach1_cb().set(FUNC(dep5_state::ach1_r));
	maincpu.ach2_cb().set(FUNC(dep5_state::ach2_r));
	maincpu.ach3_cb().set(FUNC(dep5_state::ach3_r));
	maincpu.ach4_cb().set(FUNC(dep5_state::ach4_r));
	maincpu.ach5_cb().set(FUNC(dep5_state::ach5_r));
	maincpu.ach6_cb().set(FUNC(dep5_state::ach6_r));

	auto& mdin(MIDI_PORT(config, "mdin"));
	midiin_slot(mdin);
	mdin.rxd_handler().set(FUNC(dep5_state::midi_recv));

	config.set_default_layout(layout_dep5);
}

ROM_START(dep5)
	ROM_REGION(0x8000, "maincpu", 0)
	ROM_DEFAULT_BIOS("16")

	ROM_SYSTEM_BIOS(0, "16", "Firmware 1.6")
	ROMX_LOAD("dep5_combined_1.6.bin", 0, 0x8000, CRC(58cf8a92) SHA1(024c9598ef35255d0468e94c5eb0f84652295acc), ROM_BIOS(0))
ROM_END

} // anonymous namespace


CONS(1986, dep5,  0, 0, dep5, dep5, dep5_state, empty_init, "Roland", "DEP-5 Digital Effects Processor",  MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
