// license:GPL-2.0
// copyright-holders:giulioz

/***************************************************************************

    Roland RCC (Toshiba TC23SC140AF), used in the CM-32P, U-220, D-70 and more.
    Driver derived from silicon reverse engineering (thanks InfoSecDJ).

    The mask ROM drives concurrent pipeline stages, rather than an atomic
    multiply/accumulate instruction. There are 256 normal steps per sample,
    four half-clock phases per step (512 CKI periods). All arithmetic D inputs
    use old register values. Separate low-phase captures update the feedback
    banks, input latch, external-memory banks and delay-address state.

    Memories and arithmetic:
      RAM-A: 32 x 24-bit scratchpad, shared with the host CPU.
      RAM-B: 256 x 18-bit parameters, shared with the host CPU.
      ROM:   288 x 29-bit controls (256 normal + four 8-step service pages).
      Four 24-bit feedback banks, signed 24 x 8-bit multiplication, a 32-bit
      product formatter and 29-bit addition, saturating to signed 24 bits.
      External delay RAM has a 16-bit word address and three byte lanes. The
      model finishes a whole 24-bit transaction at its high-byte completion
      phase, retaining the address/data captured when the transaction began.

    ROM file format: 288 little-endian 32-bit words, bits 28:0 = geometric
    U[28:0] contacts (left-to-right in the traced die), bits 31:29 unused.
    The contents were read optically from a die photograph. Every cell was
    classified from the diffusion outline around its gate and checked
    against the transcription; there is no electrical readout.
    The normal counter address is split across different register cuts:
      arithmetic controls: ((P - 2) & f8) | ((P - 1) & 07)
      following captures:  ((P - 1) & f8) | (P & 07)
    P is the logical 00..ff step, not the physical ROM address.

    ROM bit definitions (active-low controls explicitly indicated):
       0-2  Coefficient selector: 0=bank[23:16], 1=bank fraction,
            3=RAM-A[23:16], 4=RAM-A fraction, 5=RAM-B[7:0]; 2/6/7=0.
            Fraction = {word[23], word[11:5]}, preserving the sign bit.
       3-4  Inverted multiplier selector {~U3,~U4}: 0=bank, 1=external,
            2=RAM-A, 3=18-bit input << 6.
       5-6  A source {U6,U5} when U7=1: 0=bank, 1=saturated result,
            2=RAM-A, 3=external. With U7=0 select RAM-A for a nonnegative
            previous 29-bit result, bank for a negative result.
         7  A source sign-dependent mode, as above.
       8-9  Inverted feedback read bank {~U8,~U9}.
        10  Inverted external read-bank selector.
     11-12  Inverted feedback write bank {~U12,~U11}.
        13  External read-bank capture destination.
        14  External read-bank capture inhibit (active low enable).
        15  RAM-A read-latch inhibit (active low enable).
        16  Invert formatted product conditionally on product sign.
        17  Invert formatted product unconditionally (XOR with U16 & sign).
        18  Carry mode: 1 increments negative formatted values, even exact
            multiples of eight; 0 rounds using the discarded three bits.
        19  18-bit voice-input capture inhibit (active low enable).
        20  Serial shift/load select; 0 loads a parallel output on odd P.
        21  Carry-in to the 16-bit delay-address adder.
        22  Delay offset source: 0=four RAM-B nibbles, 1=variable offset.
        23  Variable offset capture inhibit (active low enable); capture
            saturated feedback[23:12], sign-extended to 16 bits.
        24  RAM-A host-grant inhibit (active low enable).
        25  External output-word capture inhibit (active low enable).
        26  RAM-A DSP write inhibit (active low enable).
        27  External memory direction: 0=write, 1=read.
        28  Programmable serial output timing source (not modeled).

    Every RAM-B bit is retained for host readback:
       0-7  Signed multiplier coefficient, selected only by ROM selector 5.
         8  Product arithmetic shift right by four when selector 5 is used;
            other coefficient sources always shift right by four.
      9-13  RAM-A address, substituted by the host address during a grant.
     14-17  Delay offset nibble. Four sequential words form one 16-bit tap,
            first word most significant. The nibble latches are pipelined;
            host writes do not retroactively change a captured parameter.

    Host byte writes (offsets mirror modulo 16):
      0/1/2 stage payload[23:16]/[15:8]/[7:0]; 4/6 request RAM-A/B writes;
      8/A request RAM-A/B readback; C/D capture configuration.
    Payload and readback have distinct latches. A new request shares the old
    payload/address latches; there is no transaction FIFO. RAM-A grants follow
    U24. RAM-B requests cross at group half-edge 4 and hold a grant over 7..11;
    writes complete at 11. These phases are preserved by the word core.
    read_bus/write_bus additionally expose the recovered H:L lane mux. In
    alternate mode (strap or D0), offsets 0 and C capture two bytes at once.
    ALE, bus strobe qualification and electrical pad enables are outside this API.

    Configuration C[2:0]/C[5:3] select serial clock taps (not modeled).
    D0 selects the short service program and alternate host bus mode; short
    execution is not implemented, so the normal pipeline freezes in this mode
    and host RAM transfers are modeled as atomic setups.
    D1 selects ROM bits on the high host read lane. D2=1 accepts two's complement
    input; D2=0 flips input bit 17. D3 selects an outward pin mux, D4 presets the
    framing latch, D5 selects delayed serial data (not modeled). D6=0 clears
    the serializer: parallel outputs are muted without stopping DSP state.
    C[7:6] and D7 are retained but have no modeled consumer.

***************************************************************************/

#include "emu.h"
#include "roland_rcc.h"

DEFINE_DEVICE_TYPE(TC23SC140, tc23sc140_device, "tc23sc140", "Roland RCC (Toshiba TC23SC140AF)")

ROM_START(tc23sc140)
ROM_REGION32_LE(0x480, "program", 0)
ROM_LOAD("tc23sc140.bin", 0x000, 0x480, CRC(fe7bd65a) SHA1(3da92a3e618f5923ef3af5aa0939890604fcd9a3))
ROM_END

const tiny_rom_entry *tc23sc140_device::device_rom_region() const
{
	return ROM_NAME(tc23sc140);
}

tc23sc140_device::tc23sc140_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, TC23SC140, tag, owner, clock), device_sound_interface(mconfig, *this), m_program(*this, "program")
{
}

void tc23sc140_device::device_start()
{
	set_program_words(&m_program[0]);
	m_input_clock = clock();
	m_stream = stream_alloc(32, 8, m_input_clock / 512);
	m_sample_timer = timer_alloc(FUNC(tc23sc140_device::sample_tick), this);

	save_item(NAME(m_serial_delay));
	save_item(NAME(m_clock_origin));
	save_item(NAME(m_half_edges));
	save_item(NAME(m_input_clock));
	save_item(NAME(m_frame_half_edge));
	save_item(NAME(m_voice_index));
	save_item(NAME(m_inputs));
	save_item(NAME(m_dram));
	save_item(NAME(m_dram_mask));
	save_item(NAME(m_registers));
	save_item(NAME(m_read_capture));
	save_item(NAME(m_rom_capture));
	save_item(NAME(m_ram_a));
	save_item(NAME(m_ram_b));
	save_item(NAME(m_requests.a_write));
	save_item(NAME(m_requests.a_read));
	save_item(NAME(m_requests.b_write));
	save_item(NAME(m_requests.b_read));
	save_item(NAME(m_requests.a_write_cross));
	save_item(NAME(m_requests.a_read_cross));
	save_item(NAME(m_requests.b_write_cross));
	save_item(NAME(m_requests.b_read_cross));
	save_item(NAME(m_requests.a_write_grant));
	save_item(NAME(m_requests.a_read_grant));
	save_item(NAME(m_requests.b_write_grant));
	save_item(NAME(m_requests.b_read_grant));
	save_item(NAME(m_requests.address));
	save_item(NAME(m_requests.a_address));
	save_item(NAME(m_state.parameter));
	save_item(NAME(m_state.ram_b_read));
	save_item(NAME(m_state.ram_b_address));
	save_item(NAME(m_state.tap_nibbles));
	save_item(NAME(m_state.parameter_primed));
	save_item(NAME(m_state.ram_a_read));
	save_item(NAME(m_state.voice_capture));
	save_item(NAME(m_state.external_read));
	save_item(NAME(m_state.external_output));
	save_item(NAME(m_state.assembled_read));
	save_item(NAME(m_state.variable_offset));
	save_item(NAME(m_state.offset));
	save_item(NAME(m_state.address_sum));
	save_item(NAME(m_state.counter));
	save_item(NAME(m_state.transaction_address));
	save_item(NAME(m_state.transaction_word));
	save_item(NAME(m_state.transaction_pending));
	save_item(NAME(m_state.transaction_write));
	save_item(NAME(m_state.output_words));
	save_item(NAME(m_state.dsp.a));
	save_item(NAME(m_state.dsp.formatted));
	save_item(NAME(m_state.dsp.invert));
	save_item(NAME(m_state.dsp.carry_mode));
	save_item(NAME(m_state.dsp.result));
	save_item(NAME(m_state.dsp.multiplier_operand));
	save_item(NAME(m_state.dsp.multiplier_coefficient));
	save_item(NAME(m_state.dsp.shift_four));
	save_item(NAME(m_state.dsp.external_flags));
	save_item(NAME(m_state.dsp.banks));
}

void tc23sc140_device::device_reset()
{
	m_stream->update();
	reset_state();
	m_serial_delay.fill(0);
	m_clock_origin = machine().time();
	m_half_edges = 0;
	m_sample_timer->adjust(attotime::from_hz(m_input_clock / 512), 0, attotime::from_hz(m_input_clock / 512));
}

void tc23sc140_device::device_clock_changed()
{
	if (m_stream)
	{
		synchronize();
		m_clock_origin = machine().time();
		m_half_edges = 0;
		m_input_clock = clock();
		m_stream->set_sample_rate(m_input_clock / 512);
		m_sample_timer->adjust(attotime::from_hz(m_input_clock / 512), 0, attotime::from_hz(m_input_clock / 512));
	}
}

void tc23sc140_device::advance_to(attotime time)
{
	// Lazy clocking preserves every register cut while allowing host accesses
	// between arithmetic steps. The periodic timer also clocks unrouted devices.
	if (time <= m_clock_origin)
		return;
	const u64 target = (time - m_clock_origin).as_ticks(m_input_clock * 2);
	while (m_half_edges < target)
	{
		clock_half_step();
		++m_half_edges;
	}
}

void tc23sc140_device::synchronize()
{
	m_stream->update();
	advance_to(machine().time());
}

TIMER_CALLBACK_MEMBER(tc23sc140_device::sample_tick)
{
	synchronize();
}

u8 tc23sc140_device::read(offs_t offset)
{
	if (!machine().side_effects_disabled())
		synchronize();
	return register_r(offset);
}

void tc23sc140_device::write(offs_t offset, u8 data)
{
	synchronize();
	register_w(offset, data);
}

u16 tc23sc140_device::read_bus(offs_t offset)
{
	if (!machine().side_effects_disabled())
		synchronize();
	return register_bus_r(offset);
}

void tc23sc140_device::write_bus(offs_t offset, u16 data, bool alternate)
{
	synchronize();
	register_bus_w(offset, data, alternate);
}

void tc23sc140_device::input_w(offs_t slot, s32 sample)
{
	synchronize();
	capture_input(slot, sample);
}

void tc23sc140_device::sound_stream_update(sound_stream &stream)
{
	for (int sample = 0; sample < stream.samples(); ++sample)
	{
		// MAME includes the current sample in update(); its timestamp is the
		// beginning of that sample interval. Never clock ahead of a host access.
		advance_to(stream.sample_to_time(stream.start_index() + sample));
		for (unsigned slot = 0; slot < 8; ++slot)
			stream.put_int(slot, sample, output(slot), 32768);
		if (m_input_sign_bit)
		{
			// Prepare the next frame after completing the previous one's edges.
			// This keeps host writes between DSP steps at their actual timestamps.
			for (unsigned slot = 0; slot < 32; ++slot)
			{
				const s32 input = stream.get(slot, sample) * (1 << 17);
				const u32 bus = input;
				const u32 word = ((bus >> 1) & 0x1ffff) | (((bus >> m_input_sign_bit) & 1) << 17);
				s32 value = s32(word) - (BIT(word, 17) ? 0x40000 : 0);
				if (m_four_slot_delay)
					value = std::exchange(m_serial_delay[slot & 3], value);
				capture_input(slot, value);
			}
		}
	}
}

// Chip state and arithmetic.

uint32_t tc23sc140_device::saturate_result(const uint32_t result29)
{
	const uint32_t word = result29 & WORD29_MASK;
	const int64_t signed_word = int64_t(word) - ((word & 0x10000000u) ? 0x20000000ll : 0ll);
	if (signed_word > 0x7fffff)
		return 0x7fffffu;
	if (signed_word < -0x800000)
		return 0x800000u;
	return word & WORD24_MASK;
}

uint32_t tc23sc140_device::format_product(const uint32_t product32, const bool shift_four, const bool invert)
{
	const uint32_t aligned =
		shift_four ? ((product32 >> 4) | ((product32 & 0x80000000u) ? 0xf0000000u : 0u)) : product32;
	return aligned ^ (invert ? 0xffffffffu : 0u);
}

unsigned tc23sc140_device::carry(const uint32_t formatted32, const bool invert, const bool mode)
{
	const unsigned sign = formatted32 >> 31;
	if (mode)
		return sign;
	// Derived from h8_0 / h9_2 and their surrounding gates. This also
	// preserves the asymmetric tie cases after one's-complement inversion.
	const unsigned threshold = 4u + sign - unsigned(invert);
	return unsigned((formatted32 & 7u) >= threshold);
}

uint32_t tc23sc140_device::add(const uint32_t a24, const uint32_t formatted32, const bool invert, const bool mode)
{
	const uint32_t a = (a24 & WORD24_MASK) | ((a24 & 0x800000u) ? 0x1f000000u : 0u);
	return (a + (formatted32 >> 3) + carry(formatted32, invert, mode)) & WORD29_MASK;
}

uint8_t tc23sc140_device::coefficient(const unsigned selector, const uint32_t bank24, const uint32_t ram_a24, const uint8_t parameter)
{
	const auto fraction = [](const uint32_t word) { return uint8_t(((word >> 5) & 0x7fu) | ((word >> 16) & 0x80u)); };
	switch (selector)
	{
	case 0:
		return uint8_t(bank24 >> 16);
	case 1:
		return fraction(bank24);
	case 3:
		return uint8_t(ram_a24 >> 16);
	case 4:
		return fraction(ram_a24);
	case 5:
		return parameter;
	default:
		return 0;
	}
}

void tc23sc140_device::datapath::rising_edge(const instruction op, const sources &inputs, const uint32_t product)
{
	const uint32_t bank = banks[op.bank_read()] & WORD24_MASK;
	const std::array<uint32_t, 4> a_sources = {bank, feedback(), inputs.ram_a_read, inputs.external_read};
	const std::array<uint32_t, 4> m_sources = {bank, inputs.external_read, inputs.ram_a_read, (inputs.voice_capture & 0x3ffffu) << 6};
	const unsigned flags = inputs.use_rom_flags ? op.flags() : external_flags;
	const bool next_invert = bool(((product >> 31) & (flags & 1u)) ^ ((flags >> 1) & 1u));
	const uint32_t next_a = a_sources[op.a_source(bool(result & 0x10000000u))] & WORD24_MASK;
	const uint32_t next_result = add(a, formatted, invert, carry_mode);
	const uint32_t next_formatted = format_product(product, shift_four, next_invert);

	// Compute every D value above from old state before committing Q values.
	result = next_result;
	a = next_a;
	formatted = next_formatted;
	invert = next_invert;
	carry_mode = bool(flags & 4u);
	multiplier_operand = m_sources[op.multiplier_source()] & WORD24_MASK;
	multiplier_coefficient = coefficient(op.coefficient_source(), bank, inputs.ram_a_read, inputs.parameter);
	shift_four = op.coefficient_source() != 5 || inputs.parameter_alignment;
	external_flags = inputs.external_flag_pins & 7u;
}

void tc23sc140_device::datapath::latch_feedback_bank(const instruction op)
{
	banks[op.bank_write()] = feedback();
}

void tc23sc140_device::reset_state()
{
	m_registers = {};
	m_read_capture = 0;
	m_rom_capture = 0;
	m_ram_a = {};
	m_ram_b = {};
	m_state = {};
	m_dram = {};
	m_requests = {};
	m_inputs = {};
	m_frame_half_edge = m_voice_index = 0;
}

uint16_t tc23sc140_device::register_bus_r(uint16_t offset) const
{
	offset &= 0x0f;
	uint8_t low = 0xff;
	switch (offset)
	{
	case 0:
		low = static_cast<uint8_t>(m_read_capture >> 16);
		break;
	case 1:
		low = static_cast<uint8_t>(m_read_capture >> 8);
		break;
	case 2:
		low = static_cast<uint8_t>(m_read_capture);
		break;
	case 0x0c:
		low = static_cast<uint8_t>(m_rom_capture);
		break;
	}
	uint8_t high = static_cast<uint8_t>(m_read_capture >> 8);
	if (m_registers[0x0d] & 2u)
	{
		high = 0xff;
		switch (offset)
		{
		case 0:
			high = static_cast<uint8_t>(0xe0u | ((m_rom_capture >> 24) & 0x1fu));
			break;
		case 2:
			high = static_cast<uint8_t>(m_rom_capture >> 16);
			break;
		case 0x0c:
			high = static_cast<uint8_t>(m_rom_capture >> 8);
			break;
		}
	}
	return (uint16_t(high) << 8) | low;
}

void tc23sc140_device::load_readback(const uint32_t word)
{
	// Readback has its own capture bank; it must not overwrite write staging.
	m_read_capture = word & 0xffffffu;
}

void tc23sc140_device::register_w(const uint16_t offset, const uint8_t data)
{
	capture_host_register(offset, data);
}

void tc23sc140_device::register_bus_w(const uint16_t offset, const uint16_t data, const bool alternate)
{
	// Every capture uses the mode before this closure. In particular a paired
	// C/D write may itself clear D0, but both banks were enabled by the old D0.
	const bool mode_m = (m_registers[0x0d] & 1u) || alternate;
	const uint8_t low = static_cast<uint8_t>(data);
	const uint8_t selected = mode_m ? static_cast<uint8_t>(data >> 8) : low;
	switch (offset & 0x0fu)
	{
	case 0:
		capture_host_register(0, selected);
		if (mode_m)
			capture_host_register(1, low);
		break;
	case 2:
		capture_host_register(2, selected);
		break;
	case 0x0c:
		capture_host_register(0x0c, selected);
		if (mode_m)
			capture_host_register(0x0d, low);
		break;
	case 1:
	case 4:
	case 6:
	case 8:
	case 0x0a:
	case 0x0d:
		capture_host_register(offset, low);
		break;
	}
}

void tc23sc140_device::capture_host_register(uint16_t offset, const uint8_t data)
{
	offset &= 0x0f;
	m_registers[offset] = data;
	if (offset != 4 && offset != 6 && offset != 8 && offset != 0x0a)
		return;
	m_requests.address = data;
	if (!(m_registers[0x0d] & 1u))
	{
		// Decode closure sets a pending FF. Address and payload remain shared
		// latches; there is no invented transaction FIFO inside the chip.
		const unsigned phase = (m_frame_half_edge - 1u) & 15u;
		const bool b_acknowledge_phase = phase >= 6 && phase <= 9;
		const bool a_acknowledge_phase = !(m_rom_capture & (1u << 24));
		switch (offset)
		{
		case 4:
			m_requests.a_write = !(a_acknowledge_phase && m_requests.a_write_cross);
			break;
		case 6:
			m_requests.b_write = !(b_acknowledge_phase && m_requests.b_write_cross);
			break;
		case 8:
			m_requests.a_read = !(a_acknowledge_phase && m_requests.a_read_cross);
			break;
		case 0x0a:
			m_requests.b_read = !(b_acknowledge_phase && m_requests.b_read_cross);
			break;
		}
		return;
	}
	// Short-program execution is not implemented. Retain the host-only setup convention.
	if (offset == 8)
		load_readback(m_ram_a[data & 31u]);
	else if (offset == 0x0a)
		load_readback(m_ram_b[data]);
	else
		commit_host_write(offset == 4, data);
}

uint32_t tc23sc140_device::host_payload() const
{
	return (uint32_t(m_registers[0]) << 16) | (uint32_t(m_registers[1]) << 8) | m_registers[2];
}

void tc23sc140_device::commit_host_write(const bool ram_a, const uint8_t address)
{
	const uint16_t index = ram_a ? address & 31u : address;
	uint32_t &word = ram_a ? m_ram_a[index] : m_ram_b[index];
	word = host_payload() & (ram_a ? 0xffffffu : 0x3ffffu);
}

void tc23sc140_device::clock_half_step()
{
	// D0 selects an eight-step diagnostic orbit on silicon. Its entry/exit
	// timing is unresolved; freeze the normal datapath rather than run the wrong page.
	if (m_registers[0x0d] & 1)
		return;
	if (!m_frame_half_edge)
	{
		m_voice_index = 0;
		--m_state.counter;
		if (!m_state.parameter_primed)
		{
			// Deterministic normal-orbit priming, not a measured XSYNC reset sequence.
			m_state.parameter = m_ram_b[0];
			m_state.ram_b_read = m_ram_b[1];
			m_state.ram_b_address = 1;
			m_state.tap_nibbles[0] = uint8_t(m_ram_b[0] >> 14);
			m_state.parameter_primed = true;
		}
	}
	clock_half_edge();
	m_frame_half_edge = (m_frame_half_edge + 1) & 1023;
}

void tc23sc140_device::arithmetic_edge(const unsigned pc)
{
	auto &state = m_state;
	auto &dsp = state.dsp;
	const instruction op{m_program_words[((pc - 2u) & 0xf8u) | ((pc - 1u) & 7u)]};
	const instruction following{m_program_words[((pc - 1u) & 0xf8u) | (pc & 7u)]};
	sources inputs;
	inputs.ram_a_read = state.ram_a_read;
	inputs.voice_capture = state.voice_capture;
	inputs.external_read = state.external_read[op.bit(10) ? 0 : 1];
	inputs.parameter = static_cast<uint8_t>(state.parameter);
	inputs.parameter_alignment = bool(state.parameter & 0x100u);
	if ((pc & 1u) && !op.bit(20))
	{
		const uint32_t word = dsp.feedback() >> 8;
		state.output_words[pc >> 5] = int32_t(word) - ((word & 0x8000u) ? 0x10000 : 0);
	}
	const int64_t operand =
		int64_t(dsp.multiplier_operand) - ((dsp.multiplier_operand & 0x800000u) ? 0x1000000ll : 0ll);
	const int64_t coefficient =
		int64_t(dsp.multiplier_coefficient) - ((dsp.multiplier_coefficient & 0x80u) ? 256ll : 0ll);
	dsp.rising_edge(op, inputs, static_cast<uint32_t>(operand * coefficient));
	m_rom_capture = following.word;
	// Address/control FFs use the OLD ROM U24 and old parameter FFs.
	m_requests.a_write_grant = m_requests.a_write_cross && !op.bit(24);
	m_requests.a_read_grant = m_requests.a_read_cross && !op.bit(24);
	m_requests.a_address =
		(m_requests.a_write_grant || m_requests.a_read_grant) ? m_requests.address & 31u : (state.parameter >> 9) & 31u;
	if (!following.bit(26))
		m_ram_a[m_requests.a_address] = dsp.feedback();
}

void tc23sc140_device::clock_half_edge()
{
	auto &state = m_state;
	auto &request = m_requests;
	auto &dsp = state.dsp;
	const unsigned phase = m_frame_half_edge & 15u;
	const unsigned local = phase & 3u;
	const unsigned base = (m_frame_half_edge >> 2) & 0xfcu;
	if (local == 0)
		arithmetic_edge(m_frame_half_edge >> 2);
	const instruction following{m_rom_capture};

	if (phase == 4)
	{
		request.b_write_cross = request.b_write;
		request.b_read_cross = request.b_read;
	}
	if (phase == 6)
	{
		if (request.b_write_cross)
			request.b_write = false;
		if (request.b_read_cross)
			request.b_read = false;
	}
	if (phase == 7)
	{
		// The offset FF samples before the delayed inverse clock changes RAM-B address.
		const uint16_t fixed = (uint16_t(state.tap_nibbles[0]) << 12) | (uint16_t(state.tap_nibbles[1]) << 8) | (uint16_t(state.tap_nibbles[2]) << 4) | ((m_ram_b[state.ram_b_address] >> 14) & 15u);
		state.offset = following.bit(22) ? state.variable_offset : fixed;
		request.b_write_grant = request.b_write_cross;
		request.b_read_grant = request.b_read_cross;
		state.ram_b_address = (request.b_write_grant || request.b_read_grant) ? request.address : base;
	}
	if (phase == 11)
	{
		// Word completion at the end of the grant aperture. Physical write skew
		// within this aperture and overlapping requests are not yet measured.
		if (request.b_write_grant)
			commit_host_write(false, state.ram_b_address);
		request.b_write_grant = request.b_read_grant = false;
		state.ram_b_address = static_cast<uint8_t>(base + 4);
	}
	if (phase == 1)
		state.ram_b_address = static_cast<uint8_t>(base + 2);
	if (phase == 4)
		state.ram_b_address = static_cast<uint8_t>(base + 3);
	if (phase == 14)
		state.ram_b_address = static_cast<uint8_t>(base + 5);

	// All captures sample old read-latch Q before the new read aperture opens.
	if (phase == 2 || phase == 5 || phase == 8 || phase == 12 || phase == 15)
		state.parameter = state.ram_b_read;
	const bool b_read_open = phase != 1 && phase != 4 && phase != 7 && phase != 11 && phase != 14;
	if (b_read_open)
		state.ram_b_read = m_ram_b[state.ram_b_address];
	const uint8_t nibble = static_cast<uint8_t>((m_ram_b[state.ram_b_address] >> 14) & 15u);
	if (phase == 12 || phase == 13)
		state.tap_nibbles[0] = nibble;
	if (phase == 0)
		state.tap_nibbles[1] = nibble;
	if (phase == 2 || phase == 3)
		state.tap_nibbles[2] = nibble;

	if (local == 1 || local == 2)
	{
		if (!following.bit(15))
			state.ram_a_read = m_ram_a[request.a_address];
		if (request.a_read_grant)
			load_readback(request.b_read_grant ? state.ram_b_read : m_ram_a[request.a_address]);
	}
	if (request.b_read_grant && b_read_open)
		load_readback(state.ram_b_read);
	if (local == 3 && request.a_write_grant)
		commit_host_write(true, request.a_address);

	if (phase == 10)
	{
		state.address_sum = static_cast<uint16_t>(state.offset + state.counter + following.bit(21));
		if (state.transaction_pending)
		{
			auto &word = m_dram[state.transaction_address & m_dram_mask];
			if (state.transaction_write)
				word = state.transaction_word;
			else
				state.assembled_read = word;
		}
	}
	if (phase == 13)
	{
		state.transaction_pending = true;
		state.transaction_address = state.address_sum;
		state.transaction_write = !following.bit(27);
		state.transaction_word = state.external_output;
	}
	if (local == 2)
	{
		if (!following.bit(24))
		{
			request.a_write_cross = request.a_write;
			request.a_read_cross = request.a_read;
			request.a_write = request.a_read = false;
		}
		dsp.latch_feedback_bank(following);
		if (!following.bit(14))
			state.external_read[following.bit(13)] = state.assembled_read;
		if (!following.bit(25))
			state.external_output = dsp.feedback();
		if (!following.bit(23))
		{
			const uint32_t word = dsp.feedback();
			state.variable_offset = static_cast<uint16_t>((word >> 12) | ((word & 0x800000u) ? 0xf000u : 0u));
		}
		if (!following.bit(19))
			state.voice_capture = (static_cast<uint32_t>(m_inputs[m_voice_index++ & 31u]) & 0x3ffffu) ^ ((m_registers[0x0d] & 4u) ? 0u : 0x20000u);
	}
	// Acknowledge is an asynchronous clear, including when U24 changes at
	// the arithmetic edge while an older crossing value is still held.
	if (!following.bit(24))
	{
		if (request.a_write_cross)
			request.a_write = false;
		if (request.a_read_cross)
			request.a_read = false;
	}
	if (phase >= 6 && phase <= 9)
	{
		if (request.b_write_cross)
			request.b_write = false;
		if (request.b_read_cross)
			request.b_read = false;
	}
}
