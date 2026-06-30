// license:BSD-3-Clause
// copyright-holders:giulioz, ValleyBell
/****************************************************************************

    Driver for Roland D-70 synthesizer.
    Derived by the CM32P driver by ValleyBell

****************************************************************************/

#include "emu.h"

#include "bus/generic/carts.h"
#include "bus/generic/slot.h"
#include "bus/midi/midiinport.h"
#include "bus/midi/midioutport.h"
#include "cpu/mcs96/i8x9x.h"
#include "cpu/mcs96/i8xc196.h"
#include "machine/nvram.h"
#include "machine/timer.h"
#include "sound/roland_lp.h"
#include "sound/roland_rcc.h"
#include "video/t6963c.h"
#include "wavwrite.h"

#include "emupal.h"
#include "screen.h"
#include "softlist_dev.h"
#include "speaker.h"

#include "multibyte.h"

#include <cmath>

#include "roland_d70.lh"


namespace {

// unscramble address: ROM dump offset -> proper (descrambled) offset
template <typename T> constexpr auto UNSCRAMBLE_ADDR_INT(T offset) {
	return bitswap<19>(offset, 18, 17, 15, 14, 16, 12, 11, 7, 9, 13, 10, 8, 3, 2, 1, 6, 4, 5, 0);
}
// scramble address: proper offset -> ROM dump offset
template <typename T> constexpr auto SCRAMBLE_ADDR_INT(T offset) {
	return bitswap<19>(offset, 18, 17, 14, 16, 15, 9, 13, 12, 8, 10, 7, 11, 3, 1, 2, 6, 5, 4, 0);
}

constexpr u8 UNSCRAMBLE_DATA(u8 data) { return bitswap<8>(data, 1, 2, 7, 3, 5, 0, 4, 6); }

// Bitmasks for the display board interface via PORT1
static constexpr u8 CONT_MASK = 0b10000000;
static constexpr u8 RW_MASK   = 0b01000000;
static constexpr u8 AD_MASK   = 0b00100000;
static constexpr u8 SCK_MASK  = 0b00010000;
static constexpr u8 SI_MASK   = 0b00001000;
static constexpr u8 SO_MASK   = 0b00000100;
static constexpr u8 ACK_MASK  = 0b00000010;
static constexpr u8 ENCO_MASK = 0b00000001;

static INPUT_PORTS_START(d70)
	PORT_START("KEY0")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Performance") PORT_CODE(KEYCODE_K)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Patch") PORT_CODE(KEYCODE_L)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Tone") PORT_CODE(KEYCODE_COLON)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("A/B") PORT_CODE(KEYCODE_Q)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Int/Card") PORT_CODE(KEYCODE_W)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Command") PORT_CODE(KEYCODE_E)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Write") PORT_CODE(KEYCODE_R)
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Enter") PORT_CODE(KEYCODE_T)

	PORT_START("KEY1")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Bank 1")
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Bank 2")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Bank 3")
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Bank 4")
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Bank 5")
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Bank 6")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Bank 7")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Bank 8")

	PORT_START("KEY2")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Number 1") PORT_CODE(KEYCODE_1)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Number 2") PORT_CODE(KEYCODE_2)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Number 3") PORT_CODE(KEYCODE_3)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Number 4") PORT_CODE(KEYCODE_4)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Number 5") PORT_CODE(KEYCODE_5)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Number 6") PORT_CODE(KEYCODE_6)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Number 7") PORT_CODE(KEYCODE_7)
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Number 8") PORT_CODE(KEYCODE_8)

	PORT_START("KEY3")
	// NOTE: Signals for DEC and INC buttons seem to be incorrectly described
	//       (swapped) on the schematics available in the service manual.
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Dec/Del") PORT_CODE(KEYCODE_H)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Inc/Ins") PORT_CODE(KEYCODE_J)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Down") PORT_CODE(KEYCODE_DOWN)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Left") PORT_CODE(KEYCODE_LEFT)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Right") PORT_CODE(KEYCODE_RIGHT)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Up") PORT_CODE(KEYCODE_UP)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Midi Out") PORT_CODE(KEYCODE_N)
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Tone Display") PORT_CODE(KEYCODE_B)

	PORT_START("KEY4")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Exit") PORT_CODE(KEYCODE_SLASH)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("F5") PORT_CODE(KEYCODE_G)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("F4") PORT_CODE(KEYCODE_F)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("F3") PORT_CODE(KEYCODE_D)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("F2") PORT_CODE(KEYCODE_S)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("F1") PORT_CODE(KEYCODE_A)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("User") PORT_CODE(KEYCODE_STOP)
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Part") PORT_CODE(KEYCODE_COMMA)

	PORT_START("KEY5")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Edit") PORT_CODE(KEYCODE_X)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Portamento")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Resonance")
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Pan")
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Tuning") PORT_CODE(KEYCODE_V)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Attack")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Release")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("PCM Card") PORT_CODE(KEYCODE_C)

	PORT_START("KEY6")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Play") PORT_CODE(KEYCODE_Z)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Solo")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Cutoff")
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Level")
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Upper 4")
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Upper 3")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Lower 2")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Lower 1")

	PORT_START("KEY7")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Effect/Ctrl") PORT_CODE(KEYCODE_M)

	PORT_START("PROTECT_SW")
	PORT_DIPNAME(0x01, 0x01, "Memory Protect Switch")
	PORT_DIPSETTING(   0x00, DEF_STR(On))
	PORT_DIPSETTING(   0x01, DEF_STR(Off))

	PORT_START("SLIDER0")
	PORT_ADJUSTER(100, "MODU")

	PORT_START("SLIDER1")
	PORT_ADJUSTER(100, "AFTER")

	PORT_START("SLIDER2")
	PORT_ADJUSTER(100, "C2")

	PORT_START("SLIDER3")
	PORT_ADJUSTER(100, "C1")

	PORT_START("SLIDER4")
	PORT_ADJUSTER(100, "LOWER (1)")

	PORT_START("SLIDER5")
	PORT_ADJUSTER(100, "LOWER (2)")

	PORT_START("SLIDER6")
	PORT_ADJUSTER(100, "UPPER (3)")

	PORT_START("SLIDER7")
	PORT_ADJUSTER(100, "UPPER (4)")
INPUT_PORTS_END


// The D-70 TVF and the later JD/XP family share a 32-context state-variable
// filter/VCA architecture.  The register formats below are supported by D-70
// firmware traces; structure/ring-modulation routing remains latched only.
class d70_tvf_device : public device_t, public device_sound_interface
{
public:
	d70_tvf_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);

	u8 read(offs_t offset) const;
	void write(offs_t offset, u8 data);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void sound_stream_update(sound_stream &stream) override;

private:
	static constexpr unsigned CONTEXTS = 32;
	static bool per_context(offs_t offset);
	u16 context_word(unsigned context, unsigned offset) const;
	void commit_word(unsigned context, unsigned offset);

	sound_stream *m_stream;
	u8 m_global[0x80];
	u8 m_context[CONTEXTS][0x40];
	u8 m_dest;
	u8 m_source;
	double m_low[CONTEXTS];
	double m_band[CONTEXTS];
	double m_cutoff[CONTEXTS];
	double m_cutoff_target[CONTEXTS];
	double m_gain[CONTEXTS];
	double m_gain_target[CONTEXTS];
	double m_damping[CONTEXTS];
	util::wav_file_ptr m_debug_wav;
	std::vector<s16> m_debug_buffer;
};

DEFINE_DEVICE_TYPE_PRIVATE(D70_TVF, d70_tvf_device, d70_tvf_device, "d70_tvf", "Roland D-70 TVF")

d70_tvf_device::d70_tvf_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock) :
	device_t(mconfig, D70_TVF, tag, owner, clock),
	device_sound_interface(mconfig, *this),
	m_stream(nullptr),
	m_dest(0),
	m_source(0)
{
}

bool d70_tvf_device::per_context(offs_t offset)
{
	return offset <= 0x0f
		|| (offset >= 0x20 && offset <= 0x27)
		|| (offset >= 0x30 && offset <= 0x35);
}

u16 d70_tvf_device::context_word(unsigned context, unsigned offset) const
{
	return get_u16le(&m_context[context & 0x1f][offset]);
}

void d70_tvf_device::device_start()
{
	m_stream = stream_alloc(CONTEXTS, CONTEXTS, 32'000, STREAM_SYNCHRONOUS);
	if (char const *const path = osd_getenv("D70_TVF_WAV"); path && *path)
	{
		m_debug_wav = util::wav_open(path, 32'000, CONTEXTS);
		if (!m_debug_wav)
			logerror("D-70 TVF: unable to open debug WAV %s\n", path);
	}

	save_item(NAME(m_global));
	save_item(NAME(m_context));
	save_item(NAME(m_dest));
	save_item(NAME(m_source));
	save_item(NAME(m_low));
	save_item(NAME(m_band));
	save_item(NAME(m_cutoff));
	save_item(NAME(m_cutoff_target));
	save_item(NAME(m_gain));
	save_item(NAME(m_gain_target));
	save_item(NAME(m_damping));
}

void d70_tvf_device::device_reset()
{
	std::fill(std::begin(m_global), std::end(m_global), 0);
	std::fill_n(&m_context[0][0], CONTEXTS * 0x40, 0);
	std::fill(std::begin(m_low), std::end(m_low), 0.0);
	std::fill(std::begin(m_band), std::end(m_band), 0.0);
	std::fill(std::begin(m_cutoff), std::end(m_cutoff), 0.0);
	std::fill(std::begin(m_cutoff_target), std::end(m_cutoff_target), 0.0);
	std::fill(std::begin(m_gain), std::end(m_gain), 0.0);
	std::fill(std::begin(m_gain_target), std::end(m_gain_target), 0.0);
	std::fill(std::begin(m_damping), std::end(m_damping), 1.0);
	m_dest = 0;
	m_source = 0;
}

u8 d70_tvf_device::read(offs_t offset) const
{
	offset &= 0x7f;
	if (offset >= 0x04 && offset <= 0x07)
		return m_context[m_source][offset];
	if (per_context(offset))
		return m_context[m_dest][offset];
	return m_global[offset];
}

void d70_tvf_device::commit_word(unsigned context, unsigned offset)
{
	u16 const word = context_word(context, offset);
	if (offset == 0x08)
	{
		// Reset writes 0x1000 here, establishing a Q12 coefficient.  Treating
		// it as Q14 makes the nominal zero-resonance setting highly resonant.
		m_damping[context] = double(word & 0x1fff) / 4096.0;
	}
	else if (offset == 0x30)
	{
		m_cutoff_target[context] = std::clamp(double(word & 0x7fff) / 16384.0, 0.0, 1.999);
		if (BIT(word, 15))
		{
			m_cutoff[context] = m_cutoff_target[context];
			m_low[context] = 0.0;
			m_band[context] = 0.0;
		}
	}
	else if (offset == 0x34)
	{
		m_gain_target[context] = double(word & 0x7fff) / 32768.0;
		if (BIT(word, 15))
			m_gain[context] = m_gain_target[context];
	}
}

void d70_tvf_device::write(offs_t offset, u8 data)
{
	offset &= 0x7f;

	// D-70 words are written low byte first.  Bring audio up to the high-byte
	// commit time so a half-written cutoff or gain never reaches the filter.
	if ((offset == 0x09 || offset == 0x31 || offset == 0x35) && m_stream)
		m_stream->update();

	m_global[offset] = data;
	if (offset == 0x40 || offset == 0x41)
	{
		m_dest = get_u16le(&m_global[0x40]) & 0x1f;
		return;
	}
	if (offset == 0x64 || offset == 0x65)
	{
		m_source = get_u16le(&m_global[0x64]) & 0x1f;
		return;
	}

	if (!per_context(offset))
		return;
	m_context[m_dest][offset] = data;
	if (offset == 0x09 || offset == 0x31 || offset == 0x35)
		commit_word(m_dest, offset - 1);
}

void d70_tvf_device::sound_stream_update(sound_stream &stream)
{
	if (m_debug_wav)
		m_debug_buffer.assign(stream.samples() * CONTEXTS, 0);

	for (int sample = 0; sample < stream.samples(); sample++)
	{
		for (unsigned context = 0; context < CONTEXTS; context++)
		{
			m_cutoff[context] += (m_cutoff_target[context] - m_cutoff[context]) * (1.0 / 64.0);
			m_gain[context] += (m_gain_target[context] - m_gain[context]) * (1.0 / 128.0);

			double const input = stream.get(context, sample);
			double const damping = m_damping[context];
			double const stability_limit = std::sqrt(damping * damping + 4.0) - damping;
			double const frequency = std::min(m_cutoff[context], stability_limit);

			// Roland's later XP/GP TVF performs LP, HP, then BP in this order.
			// The JD-990 research model uses the same topology and Q14 cutoff coefficient.
			m_low[context] += m_band[context] * frequency;
			double const high = input - damping * m_band[context] - m_low[context];
			m_band[context] += high * frequency;
			m_low[context] = std::clamp(m_low[context], -16.0, 16.0);
			m_band[context] = std::clamp(m_band[context], -16.0, 16.0);

			double const output = m_low[context] * m_gain[context];
			stream.put(context, sample, output);
			if (m_debug_wav)
				m_debug_buffer[sample * CONTEXTS + context] = std::clamp<int>(
					std::lround(output * 32768.0), -32768, 32767);
		}
	}
	if (m_debug_wav)
		util::wav_add_data_16(*m_debug_wav, m_debug_buffer.data(), m_debug_buffer.size());
}


class roland_d70_state : public driver_device
{
public:
	roland_d70_state(const machine_config &mconfig, device_type type, const char *tag) :
		driver_device(mconfig, type, tag),
		m_bank_view(*this, "bank"),
		m_ram1(*this, "ram1", 16 * 1024, ENDIANNESS_LITTLE),
		m_ram2(*this, "ram2", 32 * 1024, ENDIANNESS_LITTLE),
		m_cardram(*this, "cardram", 32 * 1024, ENDIANNESS_LITTLE),
		m_rom_bank(*this, "rom_bank"),
		m_ram_bank(*this, "ram_bank"),
		m_card_bank(*this, "card_bank"),
		m_pcm_rom(*this, "pcm"),
		m_cpu(*this, "maincpu"),
		m_pcm(*this, "pcm"),
		m_tvf(*this, "tvf"),
		m_rcc(*this, "rcc"),
		m_lcd(*this, "lcd"),
		m_keys(*this, "KEY%u", 0),
		m_sliders(*this, "SLIDER%u", 0),
		m_protect_sw(*this, "PROTECT_SW"),
		m_selected_slider(0),
		m_sw_scan_index(0),
		m_sw_scan_bank(0),
		m_lp_eint(false),
		m_midi_rx(0),
		m_midi_pos(0)
	{
	}

	void d70(machine_config &config) ATTR_COLD;
	void init_d70() ATTR_COLD;

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

private:
	void lcd_map(address_map &map) ATTR_COLD;
	void lcd_palette(palette_device &palette) const ATTR_COLD;

	void bank_w(u8 data);
	u8 ksga_io_r(offs_t offset);
	void ksga_io_w(offs_t offset, u8 data);
	u8 port0_r();
	u8 port1_r();
	u8 port2_r();
	void port1_w(u8 data);
	void port2_w(u8 data);
	u8 dsp_io_r(offs_t offset);
	void dsp_io_w(offs_t offset, u8 data);
	u8 tvf_io_r(offs_t offset);
	void tvf_io_w(offs_t offset, u8 data);
	u8 snd_io_r(offs_t offset);
	void snd_io_w(offs_t offset, u8 data);

	u16 ach0_r();
	u16 ach1_r();
	u16 ach2_r();
	u16 ach3_r();
	u16 ach4_r();

	TIMER_DEVICE_CALLBACK_MEMBER(test_timer_cb);

	void midi_in_w(int state);
	void lp_eint_w(int state);

	void d70_map(address_map &map) ATTR_COLD;

	void descramble_rom_internal(u8 *dst, const u8 *src) ATTR_COLD;
	void descramble_rom_external(u8 *dst, const u8 *src) ATTR_COLD;

	memory_view m_bank_view;
	memory_share_creator<u16> m_ram1;
	memory_share_creator<u16> m_ram2;
	memory_share_creator<u16> m_cardram;
	required_memory_bank m_rom_bank;
	required_memory_bank m_ram_bank;
	required_memory_bank m_card_bank;
	required_region_ptr<u8> m_pcm_rom;
	required_device<i8xc196_device> m_cpu;
	required_device<mb87419_mb87420_device> m_pcm;
	required_device<d70_tvf_device> m_tvf;
	required_device<roland_rcc_device> m_rcc;
	required_device<t6963c_device> m_lcd;
	required_ioport_array<8> m_keys;
	required_ioport_array<8> m_sliders;
	required_ioport m_protect_sw;

	u8 m_sound_io_buffer[0x100];
	u8 m_selected_slider;
	int m_sw_scan_index;
	int m_sw_scan_bank;
	u8 m_sw_scan_prev = 0;
	bool m_sw_scan_write = true;
	bool m_sw_scan_ad = true;
	bool m_lp_eint;
	u8 m_midi_rx;
	int m_midi_pos;
};

void roland_d70_state::machine_start() {
	m_bank_view.select(0);
	m_rom_bank->configure_entries(0, 8, memregion("maincpu")->base(), 0x4000);
	m_ram_bank->configure_entries(0, 2, &m_ram2[0], 0x4000);
	m_card_bank->configure_entries(0, 2, &m_cardram[0], 0x4000);

	save_item(NAME(m_sound_io_buffer));
	save_item(NAME(m_selected_slider));
	save_item(NAME(m_sw_scan_index));
	save_item(NAME(m_sw_scan_bank));
	save_item(NAME(m_sw_scan_prev));
	save_item(NAME(m_sw_scan_write));
	save_item(NAME(m_sw_scan_ad));
	save_item(NAME(m_lp_eint));
	save_item(NAME(m_midi_rx));
	save_item(NAME(m_midi_pos));
}


void roland_d70_state::machine_reset() {
	m_bank_view.select(0);
	m_rom_bank->set_entry(0);
	m_ram_bank->set_entry(0);
	m_card_bank->set_entry(0);
	m_sw_scan_index = 0;
	m_sw_scan_bank = 0;
	m_sw_scan_prev = 0;
	m_sw_scan_write = true;
	m_sw_scan_ad = true;
	m_lp_eint = false;
	m_selected_slider = 0;
	m_midi_rx = 0;
	m_midi_pos = 0;
}

void roland_d70_state::bank_w(u8 data) {
	// Only address-decoder inputs BS0/BS1 (bits 4-5) select the window target;
	// bits 6-7 are unrelated control outputs and must not become view indices.
	m_bank_view.select((data >> 4) & 0x03);
	m_rom_bank->set_entry(data & 0x07);
	m_ram_bank->set_entry(data & 0x01);
	m_card_bank->set_entry(data & 0x01);
}

u8 roland_d70_state::ksga_io_r(offs_t offset) {
	return 0;
}

void roland_d70_state::ksga_io_w(offs_t offset, u8 data) {
}

void roland_d70_state::lcd_map(address_map &map) {
	map(0x0000, 0x7fff).ram();
}

void roland_d70_state::midi_in_w(int state) {
	// The MIDI image device presents a 31.25 kbaud 8-N-1 bitstream.  Position
	// zero waits for the falling start bit, positions 1-8 collect data LSB
	// first, and position 9 validates the stop bit.  The previous decoder only
	// delivered a byte on the next start bit and consequently lost the first
	// data bit of every byte after the first one.
	if (!m_midi_pos) {
		if (!state) {
			m_midi_rx = 0;
			m_midi_pos = 1;
		}
	} else if (m_midi_pos <= 8) {
		m_midi_rx |= bool(state) << (m_midi_pos - 1);
		m_midi_pos++;
	} else {
		if (state) {
			m_cpu->serial_w(m_midi_rx);
		}
		m_midi_pos = 0;
	}
}

u8 roland_d70_state::port0_r() {
	// LP EINT is wired to the CPU's digital P0.7 input.  The separate XINT
	// signal on the schematic is the legacy EXTINT pin used by the effects
	// path; treating LP EINT as EXTINT leaves its completion queue unserviced.
	// EINT is active-low at the CPU pin; devcb line assertion is logical-high.
	// SENS0/SENS1 have pull-ups on the main board.  EINT is also pulled high
	// and the LP asserts it low when an envelope segment completes.
	return 0x60 | (m_lp_eint ? 0x00 : 0x80);
}

void roland_d70_state::lp_eint_w(int state) {
	m_lp_eint = bool(state);
}

u8 roland_d70_state::roland_d70_state::port1_r() {
	u8 result = 0xff;

	if (!m_sw_scan_write && m_sw_scan_ad) {
		if (m_sw_scan_bank == 0xff) {
			return 0x00; // encoder?	
		}

		if (m_sw_scan_bank < 0 || m_sw_scan_bank >= 8 || m_sw_scan_index < 0 || m_sw_scan_index >= 8) {
			return 0xff;
		}

		u8 buttonState = m_sw_scan_bank != -1 ? BIT(m_keys[m_sw_scan_bank]->read(), m_sw_scan_index) : 1;
		if (buttonState) {
			result |= SO_MASK;
		} else {
			result &= ~(SO_MASK);
		}

	}

	return result;
}

void roland_d70_state::roland_d70_state::port1_w(u8 data) {
	if ((m_sw_scan_prev & SCK_MASK) && !(data & SCK_MASK)) {
		if (!m_sw_scan_write && !m_sw_scan_ad) {
			m_sw_scan_index = -1;
  		m_sw_scan_bank = -1;
		} else if (!m_sw_scan_write && m_sw_scan_ad) {
			m_sw_scan_index += 1;
		}
  }

  if (!(m_sw_scan_prev & RW_MASK) && (data & RW_MASK)) {
    m_sw_scan_write = false;
		m_sw_scan_index = 0;
		m_sw_scan_bank = 0xfe;
  }
  if ((m_sw_scan_prev & RW_MASK) && !(data & RW_MASK)) {
    m_sw_scan_write = true;
  }

  if (!(m_sw_scan_prev & AD_MASK) && (data & AD_MASK)) {
    m_sw_scan_ad = true;
  }
  if ((m_sw_scan_prev & AD_MASK) && !(data & AD_MASK)) {
    m_sw_scan_ad = false;
  }
  
  if (!(m_sw_scan_prev & CONT_MASK) && (data & CONT_MASK)) {
		m_sw_scan_index = -1;
		m_sw_scan_bank += 1;
  }

  m_sw_scan_prev = data;
}

u8 roland_d70_state::port2_r() {
	u8 value = m_selected_slider << 5;
	if (m_protect_sw->read())
		value |= (1 << 4);
	return value;
}

void roland_d70_state::port2_w(u8 data) {
	m_selected_slider = data >> 5;
}

u8 roland_d70_state::dsp_io_r(offs_t offset) {
	return m_rcc->read(offset);
}

void roland_d70_state::dsp_io_w(offs_t offset, u8 data) {
	m_rcc->write(offset, data);
}

u8 roland_d70_state::tvf_io_r(offs_t offset) {
	return m_tvf->read(offset);
}

void roland_d70_state::tvf_io_w(offs_t offset, u8 data) {
	m_tvf->write(offset, data);
}

u8 roland_d70_state::snd_io_r(offs_t offset) {
	// printf("lp read %x\n", offset);
	// lots of offset modification magic to achieve the following:
	//  - offsets 00..1F are "sound chip read"
	//  - offsets 20..3F are a readback of what was written to registers 00..1F
	//  - This behaviour is reversed for offset 01/21, which is used for reading
	//  the PCM sample tables.
	// All this is just for making debugging easier, as it allows one to check the
	// register state using the Memory Viewer.
	if (offset == 0x01 || offset == 0x21)
		offset ^= 0x20; // remove when PCM data readback via sound chip is confirmed to work
	if (offset < 0x20)
		return m_pcm->read(offset);
	if (offset < 0x40)
		offset -= 0x20;

	if (offset == 0x01) {
		// code for reading from the PCM sample table is at 0xb027
		// The code at 0xb0ac writes to 1411/1F (??), then 1403/02 (bank), then
		// 1409/08/0b/0a (address). It waits a few cycles and at 0xb0f7 it reads the
		// resulting data from 1401.
		offs_t bank = m_sound_io_buffer[0x03];
		offs_t addr = get_u24le(&m_sound_io_buffer[0x09]);
		addr = ((addr >> 6) + 2) & 0x3ffff;
		addr |= (bank << 16);
		// write actual ROM address to 1440..1443 for debugging
		put_u32be(&m_sound_io_buffer[40], addr);
		return m_pcm_rom[addr];
	}
	return m_sound_io_buffer[offset];
}

void roland_d70_state::snd_io_w(offs_t offset, u8 data) {
	// printf("lp write %02x %02x\n", offset, data);
	// register map
	// ------------
	// Note: 16-bit words are Little Endian, the firmware writes the odd byte is
	// first
	//  00/01 - ??
	//  02/03 - ROM bank (only bits 11-13 are used, bit 11 = PCM card, bits 12-13
	//  select between IC18/19/20) 04/05 - frequency (2.14 fixed point, 0x4000 =
	//  32000 Hz) 06/07 - volume 08/09 - sample start address, fraction (2.14
	//  fixed point, i.e. 1 byte = 0x4000) 0A/0B - sample start address (high
	//  word, i.e. address bits 2..17) 0C/0D - sample end address (high word)
	//  0E/0F - sample loop address (high word)
	//  11/13/15/17 - voice enable mask (11 = least significant 8 bits, 17 = most
	//  significant 8 bits) 1A - ?? 1F - voice select
	if (offset < 0x20) {
		m_pcm->write(offset, data);
	}
	m_sound_io_buffer[offset] = data;
}

u16 roland_d70_state::ach0_r() {
	return u16(m_sliders[m_selected_slider & 7]->read()) * 0x3ff / 0xff;
}

u16 roland_d70_state::ach1_r() { return 512; } // TODO: EXT PEDAL
u16 roland_d70_state::ach2_r() { return 512; } // TODO: BENDER
u16 roland_d70_state::ach3_r() { return 0x280; } // 3.1 V internal CR2032 on the 5 V ADC reference
u16 roland_d70_state::ach4_r() { return 512; } // TODO: RAM CARD (VBB)

TIMER_DEVICE_CALLBACK_MEMBER(roland_d70_state::test_timer_cb) {
}

void roland_d70_state::lcd_palette(palette_device &palette) const {
	palette.set_pen_color(0, rgb_t(0x9f, 0xb4, 0x86));
	palette.set_pen_color(1, rgb_t(0x5e, 0x5f, 0x71));
}

void roland_d70_state::d70_map(address_map &map) {
	map(0x0100, 0x0100).w(FUNC(roland_d70_state::bank_w));
	map(0x0400, 0x07ff).rw(FUNC(roland_d70_state::ksga_io_r), FUNC(roland_d70_state::ksga_io_w));
	map(0x0800, 0x0802).rw(m_lcd, FUNC(t6963c_device::read), FUNC(t6963c_device::write)).umask16(0x00ff);
	map(0x0900, 0x09ff).rw(FUNC(roland_d70_state::snd_io_r), FUNC(roland_d70_state::snd_io_w));
	map(0x0a00, 0x0aff).rw(FUNC(roland_d70_state::dsp_io_r), FUNC(roland_d70_state::dsp_io_w));
	map(0x0c00, 0x0cff).rw(FUNC(roland_d70_state::tvf_io_r), FUNC(roland_d70_state::tvf_io_w));
	map(0x1000, 0x7fff).rom().region("maincpu", 0x1000);
	map(0x8000, 0xbfff).view(m_bank_view);
	m_bank_view[0](0x8000, 0xbfff).bankr(m_rom_bank);
	m_bank_view[1](0x8000, 0xbfff).unmaprw();
	m_bank_view[2](0x8000, 0xbfff).bankrw(m_ram_bank);
	m_bank_view[3](0x8000, 0xbfff).bankrw(m_card_bank);
	map(0xc000, 0xffff).ram().share("ram1");
}

void roland_d70_state::d70(machine_config &config) {
	i8xc196_device &maincpu(C80C196KB(config, m_cpu, 12_MHz_XTAL));
	maincpu.set_addrmap(AS_PROGRAM, &roland_d70_state::d70_map);
	maincpu.serial_tx_cb().set("mdout", FUNC(midi_port_device::write_txd));
	maincpu.in_p0_cb().set(FUNC(roland_d70_state::port0_r));
	maincpu.in_p1_cb().set(FUNC(roland_d70_state::port1_r));
	maincpu.out_p1_cb().set(FUNC(roland_d70_state::port1_w));
	maincpu.in_p2_cb().set(FUNC(roland_d70_state::port2_r));
	maincpu.out_p2_cb().set(FUNC(roland_d70_state::port2_w));
	maincpu.ach0_cb().set(FUNC(roland_d70_state::ach0_r));
	maincpu.ach1_cb().set(FUNC(roland_d70_state::ach1_r));
	maincpu.ach2_cb().set(FUNC(roland_d70_state::ach2_r));
	maincpu.ach3_cb().set(FUNC(roland_d70_state::ach3_r));
	maincpu.ach4_cb().set(FUNC(roland_d70_state::ach4_r));

	// IC5/IC6 are the battery-backed working SRAM.  The address decoder exposes
	// it as a fixed 16 KiB window and two banked 16 KiB windows.  The separate
	// IC26/IC27 DRAM belongs to the effects chip, not this CPU address space.
	NVRAM(config, "ram1", nvram_device::DEFAULT_ALL_0);
	NVRAM(config, "ram2", nvram_device::DEFAULT_ALL_0);

	SPEAKER(config, "speaker", 2).front();

	MB87419_MB87420(config, m_pcm, 32.768_MHz_XTAL);
	m_pcm->int_callback().set(FUNC(roland_d70_state::lp_eint_w));
	D70_TVF(config, m_tvf, 0);
	ROLAND_RCC(config, m_rcc, 32'000);
	m_rcc->set_program_voice_offset(4);
	// DA is a 32-slot TDM stream, one slot per LP voice/context.  TVF performs
	// the per-context resonant low-pass and VCA, then RCC applies the decoded
	// firmware dry L/R coefficients. Effects remain incomplete. The 4x output
	// makeup stands in for the unknown RCC/DAC fixed-point gain.
	for (unsigned voice = 0; voice < mb87419_mb87420_device::NUM_CHANNELS; voice++) {
		m_pcm->add_route(voice, "tvf", 1.0, voice);
		m_tvf->add_route(voice, "rcc", 1.0, voice);
	}
	m_rcc->add_route(0, "speaker", 4.0, 0);
	m_rcc->add_route(1, "speaker", 4.0, 1);

	T6963C(config, m_lcd);
	m_lcd->set_addrmap(0, &roland_d70_state::lcd_map);

	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
	screen.set_refresh_hz(60);
	screen.set_size(240, 64);
	screen.set_visarea_full();
	screen.set_screen_update("lcd", FUNC(t6963c_device::screen_update));
	screen.set_palette("palette");

	PALETTE(config, "palette", FUNC(roland_d70_state::lcd_palette), 2);

	TIMER(config, "test_timer").configure_periodic(FUNC(roland_d70_state::test_timer_cb), attotime::from_hz(1));

	midi_port_device &mdin(MIDI_PORT(config, "mdin", midiin_slot, "midiin"));
	mdin.rxd_handler().set(FUNC(roland_d70_state::midi_in_w));
	mdin.rxd_handler().append("mdthru", FUNC(midi_port_device::write_txd));

	MIDI_PORT(config, "mdout", midiout_slot, "midiout");
	MIDI_PORT(config, "mdthru", midiout_slot, "midiout");

	config.set_default_layout(layout_roland_d70);
}

void roland_d70_state::init_d70() {
	// Roland did a fair amount of scrambling on the address and data lines.
	// Only the first 0x80 bytes of the ROMs are readable text in a raw dump.
	// The D-70 actually checks some of these header bytes, but it uses
	// post-scrambling variants of offsets/values.
	u8 *src = reinterpret_cast<u8 *>(memregion("pcmorg")->base());
	u8 *dst = reinterpret_cast<u8 *>(memregion("pcm")->base());
	// descramble internal ROMs
	descramble_rom_internal(&dst[0x000000], &src[0x000000]);
	descramble_rom_internal(&dst[0x080000], &src[0x080000]);
	descramble_rom_internal(&dst[0x100000], &src[0x100000]);
	descramble_rom_internal(&dst[0x180000], &src[0x180000]);
	descramble_rom_internal(&dst[0x200000], &src[0x200000]);
	descramble_rom_internal(&dst[0x300000], &src[0x300000]);
}

void roland_d70_state::descramble_rom_internal(u8 *dst, const u8 *src) {
	for (offs_t srcpos = 0x00; srcpos < 0x80000; srcpos++) {
		const offs_t dstpos = UNSCRAMBLE_ADDR_INT(srcpos);
		dst[dstpos] = UNSCRAMBLE_DATA(src[srcpos]);
	}
}

ROM_START(d70)
	ROM_REGION(0x20000, "maincpu", 0)
	ROM_DEFAULT_BIOS("v119")
	// ROM_DEFAULT_BIOS("v110")
	ROM_SYSTEM_BIOS( 0, "v119", "Version 1.19 - March 9, 1993" )
	ROM_SYSTEM_BIOS( 1, "v116", "Version 1.16 - January 28, 1991" )
	ROM_SYSTEM_BIOS( 2, "v114", "Version 1.14 - September 20, 1990" )
	ROM_SYSTEM_BIOS( 3, "v112", "Version 1.12" )
	ROM_SYSTEM_BIOS( 4, "v110", "Version 1.10 - April 19, 1990" )
	ROM_SYSTEM_BIOS( 5, "v100", "Version 1.00 - March 10, 1990" )

	ROMX_LOAD("roland_d70_v1.19_a_even.ic4", 0, 0x10000, CRC(95fcf250) SHA1(174962eb42f56aaf936aeca4db77228bf19ec97a), ROM_BIOS(0) | ROM_SKIP(1) )
	ROMX_LOAD("roland_d70_v1.19_b_odd.ic9",  1, 0x10000, CRC(a14f0ce1) SHA1(9ef2d62b1be5c38b9fa0072a5889b8ab0e47623e), ROM_BIOS(0) | ROM_SKIP(1) )

	ROMX_LOAD("roland_d70_v1.16_a_even.ic4", 0, 0x10000, CRC(761f6eac) SHA1(18bd2c8390d67f1000fae652f51a03322bb088cd), ROM_BIOS(1) | ROM_SKIP(1) )
	ROMX_LOAD("roland_d70_v1.16_b_odd.ic9",  1, 0x10000, CRC(103a1f07) SHA1(530408a8ae8d74786cb8179892b7e3a24db93000), ROM_BIOS(1) | ROM_SKIP(1) )

	ROMX_LOAD("roland_d70_v1.14_a_even.ic4", 0, 0x10000, CRC(651e7cd5) SHA1(73ad2d897f51449111064e92b515a4118fee4dad), ROM_BIOS(2) | ROM_SKIP(1) )
	ROMX_LOAD("roland_d70_v1.14_b_odd.ic9",  1, 0x10000, CRC(b3533278) SHA1(8c2019c20b60d4fbb5f5956bce672a84f515215b), ROM_BIOS(2) | ROM_SKIP(1) )

	ROMX_LOAD("roland_d70_v1.12_a_even.ic4", 0, 0x10000, CRC(86032879) SHA1(07e7545c2dae93311f7b5d77b65c548e56391748), ROM_BIOS(3) | ROM_SKIP(1) )
	ROMX_LOAD("roland_d70_v1.12_b_odd.ic9",  1, 0x10000, CRC(910cf30e) SHA1(15cb5ecb956205dc9d6254822178139a228790d8), ROM_BIOS(3) | ROM_SKIP(1) )

	ROMX_LOAD("roland_d70_v1.10_a_even.ic4", 0, 0x10000, CRC(5f7374c5) SHA1(15d9249c35c3db6a7d1df6a4f4e42f0ce99f11a5), ROM_BIOS(4) | ROM_SKIP(1) )
	ROMX_LOAD("roland_d70_v1.10_b_odd.ic9",  1, 0x10000, CRC(f024220e) SHA1(cde358c1ec205f446114f478b43269b01b8be048), ROM_BIOS(4) | ROM_SKIP(1) )

	// Seen at https://www.youtube.com/watch?v=9zGcHzpz7zo
	ROMX_LOAD("roland_d70_v1.00_a_even.ic4", 0, 0x10000, NO_DUMP, ROM_BIOS(5) | ROM_SKIP(1) )
	ROMX_LOAD("roland_d70_v1.00_b_odd.ic9",  1, 0x10000, NO_DUMP, ROM_BIOS(5) | ROM_SKIP(1) )


	ROM_REGION(0x600000, "pcmorg", 0) // ROMs before descrambling
	ROM_LOAD("roland_d70_waverom-a.bin", 0x000000, 0x80000, CRC(8e53b2a3) SHA1(4872530870d5079776e80e477febe425dc0ec1df))
	ROM_LOAD("roland_d70_waverom-e.bin", 0x080000, 0x80000, CRC(d46cc7a4) SHA1(d378ac89a5963e37f7c157b3c8e71892c334fd7b))
	ROM_LOAD("roland_d70_waverom-b.bin", 0x100000, 0x80000, CRC(c8220761) SHA1(49e55fa672020f95fd9c858ceaae94d6db93df7d))
	ROM_LOAD("roland_d70_waverom-f.bin", 0x180000, 0x80000, CRC(d4b01f5e) SHA1(acd867d68e49e5f59f1006ed14a7ca197b6dc4af))
	ROM_LOAD("roland_d70_waverom-c.bin", 0x200000, 0x80000, CRC(733c4054) SHA1(9b6b59ab74e5bf838702abb087c408aaa85b7b1f))
	ROM_LOAD("roland_d70_waverom-d.bin", 0x300000, 0x80000, CRC(b6c662d2) SHA1(3fcbcfd0d8d0fa419c710304c12482e2f79a907f))
	ROM_REGION(0x600000, "pcm", ROMREGION_ERASEFF) // ROMs after descrambling

	ROM_REGION(0x400, "lcd:cgrom", 0)
	ROM_LOAD("t6963c_0101.bin", 0x000, 0x400, CRC(547d118b) SHA1(0dd3e3acd3d47e6ece644c98c390fc86587373e9))
	// This t6963c_0101 internal CG ROM is similar to lm24014w_0101.bin which may be
	// used as a replacement
ROM_END

} // anonymous namespace

SYST(1991, d70, 0, 0, d70, d70, roland_d70_state, init_d70, "Roland", "D-70 Super LA Synthesizer", MACHINE_NOT_WORKING | MACHINE_IMPERFECT_SOUND)
