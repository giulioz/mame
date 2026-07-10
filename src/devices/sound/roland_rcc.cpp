// license:BSD-3-Clause
// copyright-holders:giulioz

// Roland RCC (TC23SC140AF) -- see roland_rcc.h for the model overview.
//
// Provenance tags used in comments below:
//   [V]  verified against the solved die netlist (1441/1441 equivalence
//        checks) or measured on real U-220 hardware
//   [S]  structural (wiring verified; boolean of a plausible cell)
//   [H]  hypothesis / modeling choice (rcc repo, EMULATOR_STATUS.md)

#include "emu.h"
#include "roland_rcc.h"


DEFINE_DEVICE_TYPE(ROLAND_RCC, roland_rcc_device, "roland_rcc", "Roland RCC host interface, mixer and effects DSP")

// 288 x 29-bit program mask ROM (die transcription; words 256..287 are the
// service page, unused at runtime) [V die]
static constexpr u32 PROGRAM_ROM[288] = {
	0x0ad43cd1, 0x0ad41cc4, 0x088cfec0, 0x0bd41cc4, 0x0b853cc0, 0x0a863cc0, 0x09845cc5, 0x08d41cc5,
	0x0ad43cc1, 0x0ad714c1, 0x09845ec5, 0x0bdc1cc5, 0x0b843cc1, 0x0a873cc1, 0x09845cc5, 0x0adc1cc5,
	0x08d414c4, 0x089f3cc1, 0x0ad45ec5, 0x09cc1cd5, 0x0b441cc0, 0x0b863cc0, 0x0a877cc0, 0x088c1cc4,
	0x0b843cc0, 0x09d43cd0, 0x09de5ec0, 0x0ad41cc4, 0x088f38c0, 0x0a8614d0, 0x09845dc0, 0x0bcc1cc4,
	0x0ace1cd1, 0x0a863cc0, 0x09845ec4, 0x0bcc1cc4, 0x0b843cc0, 0x088e3cc0, 0x0ad45cc5, 0x09841cd5,
	0x0a873cc1, 0x0b801ec1, 0x0b867cc1, 0x09843cc1, 0x0acc1cc5, 0x089c1cc5, 0x0a863cd1, 0x0ad41cc1,
	0x0ad43cd2, 0x0a9c1cc5, 0x05d07ec1, 0x0b541cc5, 0x0b873cc0, 0x0a843cc8, 0x0a84dcc4, 0x088c1cc6,
	0x09841cc6, 0x0adc1cc2, 0x0ace3ec2, 0x0ad73cd8, 0x0ad61cc0, 0x0b841cc4, 0x09803dc0, 0x0bd43cc2,
	0x0a843cc3, 0x0a443cc2, 0x089c1ed6, 0x0b441cc0, 0x0b873cc0, 0x0a843cc8, 0x0a845cc5, 0x0a841cc7,
	0x0a843cd1, 0x0adc34c3, 0x0acc36c3, 0x0bdf34c1, 0x0a841cc5, 0x0afc3cc1, 0x0900dcc5, 0x0b061cc5,
	0x08941cd6, 0x089f1cc9, 0x0a001ed5, 0x09841cd3, 0x0b441cc2, 0x0b843cc2, 0x0a873cca, 0x0a841cc6,
	0x0a841cd4, 0x0ad41cc2, 0x0a843ec2, 0x0acc1cc4, 0x089c3cd0, 0x0b041cc0, 0x0980fdc0, 0x0b443cc0,
	0x08d41cc1, 0x089f1cc8, 0x0a005ed4, 0x0b841cc2, 0x0b873cc2, 0x0a843cc2, 0x08941cc7, 0x08941cd5,
	0x0bd43cc1, 0x0ad43cc1, 0x09d0d6d5, 0x0a441cc1, 0x088e1cc5, 0x0a863cc1, 0x0a847cd1, 0x08441cc1,
	0x0adc1cc6, 0x0b841cc5, 0x08847ec9, 0x0a4c1cc7, 0x089c1cd6, 0x0b441cca, 0x0b843cc2, 0x09873cc2,
	0x0a841cc6, 0x0ad41cc6, 0x089f3eca, 0x0a041cc6, 0x0b843cc2, 0x0b843cca, 0x0a043dc2, 0x0a841cc6,
	0x0b841cc5, 0x0a043cc2, 0x0a8436c2, 0x0a803cd0, 0x0bdf14c0, 0x089c3cc0, 0x0afc7cc1, 0x0a841cc5,
	0x0a843cc1, 0x09803cc1, 0x0a00fec1, 0x0bd41cc5, 0x088e3cc1, 0x0a041cc5, 0x0b84dcc5, 0x0a843cc9,
	0x08101cd6, 0x0a841cc5, 0x0b807ec1, 0x08803cc3, 0x0a041cc6, 0x0b841cc6, 0x0a873cda, 0x089c1cc2,
	0x0a841cc6, 0x0ad41cc2, 0x088c3eca, 0x0ad414c6, 0x0b843cc2, 0x0b043cca, 0x0a843dc2, 0x0a841cc2,
	0x08843c01, 0x08843cc2, 0x0a842ec2, 0x0b843c80, 0x0a871cb4, 0x0a841c80, 0x0b84dc85, 0x0a843c01,
	0x0d983cc3, 0x05801c15, 0x0bd45e01, 0x0b843c01, 0x0a843cc9, 0x0a841cc5, 0x0d9cdcd5, 0x081414c3,
	0x0adc04c0, 0x0a443cd3, 0x0bd41ecb, 0x0b863cc3, 0x0a873cc2, 0x0a841cc6, 0x0a841cc6, 0x0a843cd0,
	0x088c3c00, 0x0a843cc0, 0x085416c4, 0x0ad43cc0, 0x0a873ca0, 0x0a841c94, 0x0b841d80, 0x0b843c00,
	0x0acc1cc5, 0x0ad01c04, 0x0b845e04, 0x0b863cc0, 0x0d9c3cc0, 0x081c14c0, 0x0dd8dcc5, 0x0a443cc1,
	0x08941cc5, 0x0ad53cd1, 0x085c16d1, 0x0a961cc1, 0x0b841cc5, 0x0b873cc1, 0x0a843cc1, 0x08841cc5,
	0x0a843cc2, 0x0acc3cc1, 0x0adc3ec1, 0x0acc1cd5, 0x0a441cd0, 0x0a841cc8, 0x0b841cc4, 0x0b843cc2,
	0x0a843cc0, 0x0a841cc6, 0x08941ec6, 0x0a543cc0, 0x0a841cc0, 0x0a801cd4, 0x0b841dc0, 0x0b843cc0,
	0x0a443c81, 0x0a841cc4, 0x0a841ec4, 0x0a043cc0, 0x0a842cc0, 0x0b843cc0, 0x0ac73ca1, 0x08941c85,
	0x0d9c3c81, 0x0a842481, 0x089c1e85, 0x088e3401, 0x0a843401, 0x0a841c05, 0x0b865c25, 0x0b843c81,
	0x0a0414c4, 0x0a041485, 0x0d98de95, 0x0a441c11, 0x0b841c00, 0x0b873c00, 0x0a847c00, 0x0d8c1cc4,
	0x08941cc4, 0x0ad43cd0, 0x0d88dec0, 0x0ac61cc4, 0x0a843cd0, 0x0b861cc0, 0x0b843dc0, 0x0a843cc0,
	0x0a973ccb, 0x0a973ccb, 0x0a971ccf, 0x0a973ccb, 0x0a9734cb, 0x0a971ccf, 0x0a971ccf, 0x0a973ccb,
	0x0b8f1ed0, 0x0b8f1ed0, 0x0b8bded1, 0x0b8a1ed3, 0x0b0b1ed3, 0x0b8b1ed2, 0x0b8b1fda, 0x0b8f1ed0,
	0x0acc36c1, 0x0a8436c1, 0x0a845ed5, 0x0d9f16c1, 0x0a5c3ed1, 0x0b8416d1, 0x0b86ded1, 0x1d911ec1,
	0x0b843601, 0x0a843e11, 0x0bdf1601, 0x0b8c3e91, 0x0bfc1e91, 0x0b9c1e81, 0x0b9c1ea5, 0x0a843601,
};

// These program locations are the dry L/R coefficient instructions found by
// cycling every U-220 voice in Sound Test (1). Products can connect their
// first synth voice at a later RCC program slot. The phase is circular: the
// D-70 offsets LP contexts by four, so contexts 28-31 use program voices 0-3.
// Its two reserved LP contexts align with non-voice program slots 4 and 28. [V hw]
static constexpr u8 GAIN_PROGRAM[roland_rcc_device::NUM_CHANNELS][2] = {
	{ 0x04, 0x05 }, { 0x0c, 0x0d }, { 0x15, 0x16 }, { 0x20, 0x21 },
	{ 0x24, 0x25 }, { 0x2a, 0x2b }, { 0x34, 0x35 }, { 0x3e, 0x40 },
	{ 0x44, 0x45 }, { 0x4c, 0x50 }, { 0x55, 0x56 }, { 0x5e, 0x60 },
	{ 0x64, 0x65 }, { 0x71, 0x72 }, { 0x76, 0x77 }, { 0x7d, 0x7e },
	{ 0x85, 0x89 }, { 0x8c, 0x8f }, { 0x93, 0x96 }, { 0x9d, 0x9e },
	{ 0xa4, 0xa7 }, { 0xab, 0xac }, { 0xb3, 0xb4 }, { 0xbf, 0xc0 },
	{ 0xc3, 0xc4 }, { 0xcd, 0xce }, { 0xd7, 0xd8 }, { 0xdf, 0xe0 },
	{ 0xff, 0xff }, { 0xef, 0xf0 }, { 0xf5, 0xf6 }, { 0xfe, 0xff }
};

roland_rcc_device::roland_rcc_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock) :
	device_t(mconfig, ROLAND_RCC, tag, owner, clock),
	device_sound_interface(mconfig, *this),
	m_stream(nullptr),
	m_frame(0),
	m_wet_l(0),
	m_wet_r(0),
	m_program_voice_offset(0)
{
}

void roland_rcc_device::device_start()
{
	m_stream = stream_alloc(NUM_CHANNELS, NUM_OUTPUTS, clock(), STREAM_SYNCHRONOUS);

	save_item(NAME(m_io));
	save_item(NAME(m_program));
	save_item(NAME(m_state));
	save_item(NAME(m_ram_a));
	save_item(NAME(m_ram_b));
	save_item(NAME(m_dram));
	save_item(NAME(m_frame));
	save_item(NAME(m_wet_l));
	save_item(NAME(m_wet_r));
	save_item(NAME(m_effect_dc));
	save_item(NAME(m_chorus_l));
	save_item(NAME(m_chorus_r));
	save_item(NAME(m_lfo_phase));
	save_item(NAME(m_gain));
	save_item(NAME(m_program_voice_offset));
}

void roland_rcc_device::device_reset()
{
	std::fill(std::begin(m_io), std::end(m_io), 0);
	std::fill_n(&m_program[0][0], 0x100 * 3, 0);
	std::fill_n(&m_state[0][0], 0x20 * 3, 0);
	std::fill(std::begin(m_ram_a), std::end(m_ram_a), 0);
	std::fill(std::begin(m_ram_b), std::end(m_ram_b), 0);
	std::fill(std::begin(m_dram), std::end(m_dram), 0);
	m_frame = 0;
	m_wet_l = 0;
	m_wet_r = 0;
	m_effect_dc = 0.0F;
	std::fill_n(&m_gain[0][0], NUM_CHANNELS * 2, 0.0F);
}

//-------------------------------------------------------------------------
//  host interface
//-------------------------------------------------------------------------

u8 roland_rcc_device::read(offs_t offset)
{
	return m_io[offset & 0x0f];
}

void roland_rcc_device::write(offs_t offset, u8 data)
{
	offset &= 0x0f;
	m_io[offset] = data;

	switch (offset)
	{
	case 0x04:
		if (m_stream)
			m_stream->update();
		std::copy_n(&m_io[0], 3, &m_state[data & 0x1f][0]);
		// RAM-A host load: 24-bit signed data word into the working memory.
		// The program's per-step writeback will overwrite it; whether the
		// real chip protects host cells during the host_gate window is open. [H]
		m_ram_a[data & 0x1f] = util::sext(
				(u32(m_io[0]) << 16) | (u32(m_io[1]) << 8) | u32(m_io[2]), 24);
		if (char const *dump = std::getenv("RCC_DUMP_RAMA"); dump)
		{
			if (FILE *f = std::fopen(dump, "a"); f)
			{
				std::fprintf(f, "%02x %02x%02x%02x\n", data & 0x1f, m_io[0], m_io[1], m_io[2]);
				std::fclose(f);
			}
		}
		break;

	case 0x06:
		if (m_stream)
			m_stream->update();
		std::copy_n(&m_io[0], 3, &m_program[data][0]);
		// RAM-B parameter load: the 18-bit word rides the low bits of the
		// 24-bit host data word (coef byte = io[2], verified by the dry-gain
		// calibration; delay base in io[0] bits 1:0 + io[1]).
		m_ram_b[data] = ((u32(m_io[0]) << 16) | (u32(m_io[1]) << 8) | u32(m_io[2])) & 0x3ffff;
		if (char const *dump = std::getenv("RCC_DUMP_RAMB"); dump)
		{
			if (FILE *f = std::fopen(dump, "a"); f)
			{
				std::fprintf(f, "%02x %02x%02x%02x\n", data, m_io[0], m_io[1], m_io[2]);
				std::fclose(f);
			}
		}
		update_dry_gain(data);
		break;

	case 0x0a:
		std::copy_n(&m_program[data][0], 3, &m_io[0]);
		break;

	case 0x0c: // reset/control latch
	case 0x0d: // operating mode latch
	default:
		break;
	}
}

//-------------------------------------------------------------------------
//  program execution -- one pass = one 32kHz sample frame
//-------------------------------------------------------------------------

// RAM-A address: the program's band-0 bitstream runs through a 7-tap shift
// register into the row decoders; the address for a step is the last 5
// band-0 bits.  Cross-validated against the gate-level netlist simulator
// (exact across the steady state of every block). [V]
int roland_rcc_device::rama_addr(int step)
{
	int a = 0;
	for (int k = 0; k < 5; ++k)
		a = (a << 1) | (PROGRAM_ROM[(step - 1 - k + 256) & 255] & 1);
	return a & 31;
}

// Coefficient: SIGN-MAGNITUDE byte.  The U-220 dry-gain calibration fixes
// the positive range at byte/64 (0x40 = unity) [V hw]; the firmware's boot
// parameter image fills the muted effect-network slots with 0x80 = "-0",
// which only makes sense as sign-magnitude (as two's-complement those would
// be -2.0 feedback taps at silent boot). [H sign half]
s32 roland_rcc_device::decode_coef(u32 param)
{
	s32 const mag = param & 0x7f;
	return (param & 0x80) ? -mag : mag;
}

void roland_rcc_device::run_program(s32 effect_in)
{
	m_wet_l = 0;
	m_wet_r = 0;
	m_head_base = -1;

	s32 acc = 0;                                     // 24-bit saturating accumulator
	for (int step = 0; step < 256; ++step)
	{
		u32 const op = PROGRAM_ROM[step];
		u32 const param = m_ram_b[step];             // parameter fetched by the PC [V]

		// ---- multiplier sample source (bands 24/25, inverted) [S] ----
		// RAM-A cells 09-0b/0f-11 are the chorus control lanes the host
		// keeps loaded with control constants (identified by U-220 sweeps);
		// their products modulate the delay rather than entering the audio
		// sum, and the chorus block models that separately -- so they read
		// as silence in the audio MAC here.  [H split]
		bool const audio_in = BIT(op, 25) && !BIT(op, 24);
		int const ra = rama_addr(step);
		bool const control_lane = (ra >= 0x09 && ra <= 0x0b) || (ra >= 0x0f && ra <= 0x11);
		s32 const sample = audio_in ? effect_in : (control_lane ? 0 : m_ram_a[ra]);

		// ---- multiply-accumulate ----
		// All three runtime opcodes (bands 26-28: 010 x246, 100 x2, 110 x8)
		// are MACs; the opcode selects the coefficient scaling mode (the
		// block-floating-point exponent, not yet modeled). [V structure]
		// A-operand select (bands 21-23, overflow-steered): 1 = load,
		// otherwise accumulate. [V]
		// Product alignment: byte/64 uniformly (the only hardware-calibrated
		// scale).  A shift-4 reading of the bit-8-clear slots was tried and
		// makes the program's cell-0 recirculation a unity loop -> saturation
		// lock; at /64 the same loop decays at 0.25. [V hw]
		{
			s32 const coef = decode_coef(param);
			s64 prod = (s64(sample) * coef) >> 6;
			int const a0 = BIT(op, 21) & BIT(op, 23);
			int const a1 = (int(acc >= 0) & int(!BIT(op, 21))) | (BIT(op, 21) & BIT(op, 22));
			if (((a1 << 1) | a0) == 1)
				acc = s32(prod);                     // load (start of a sum)
			else
				acc += s32(prod);                    // accumulate
		}

		// ---- 24-bit signed saturation [V] ----
		acc = std::clamp<s32>(acc, -0x800000, 0x7fffff);

		// ---- RAM-A writeback, gated by ~band0 [V] ----
		if (!BIT(op, 0))
			m_ram_a[rama_addr(step)] = acc;

		// ---- DRAM delay network: addr = param base + frame counter [V],
		// 4-step cadence [V].  The parameter's top 10 bits are a delay base
		// in 64-sample (2ms) units on one 64K ring; param bit 8 selects the
		// slot role: 0 = write head (stores acc scaled by the coefficient =
		// send level), 1 = read tap (mixes the delayed content through the
		// coefficient).  Role split extracted from the D-70's live parameter
		// image; muted slots (coef +-0) are inert. [H field widths/roles]
		if (!BIT(op, 4) && (step & 3) == 2)
		{
			s32 const coef = decode_coef(param);
			if (coef != 0)
			{
				int const daddr = ((int(param >> 8) << 6) + int(m_frame)) & 0xffff;
				if (!(param & 0x100))                // write head
				{
					m_dram[daddr] = s32((s64(acc) * coef) >> 6);
				}
				else                                 // read tap
				{
					// stability guard: cap the tap gain just below unity so
					// residual loops under this approximate role/base model
					// always decay (-0.14dB on a 1.0 tap) [H]
					s32 const tc = std::clamp<s32>(coef, -63, 63);
					s32 const c = s32((s64(m_dram[daddr]) * tc) >> 6);
					acc = std::clamp<s32>(acc + c, -0x800000, 0x7fffff);
					if (step < 128)                  // L half / R half [H routing]
						m_wet_l += c;
					else
						m_wet_r += c;
				}
			}
		}
	}
	// ---- chorus: host-lane-modulated read taps of the SAME delay ring ----
	// The firmware writes the chorus controls into the RAM-A lanes identified
	// by real-unit sweeps (09/0f = rate, 0a/10 = centre delay, 0b/11 = depth).
	// On the chip these modulate delay-tap addressing; here we read the ring
	// content the write heads just stored, at a host-controlled modulated
	// distance behind the head, with linear interpolation.  Same parameters,
	// same 8-bit delay memory, same tap mechanism -- only the modulation
	// arithmetic is behavioral. [H mod math; V lanes/laws by U-220 sweeps]
	m_chorus_l = 0;
	m_chorus_r = 0;
	if (m_head_base >= 0)
	{
		auto state24 = [this](unsigned i) {
			return (u32(m_state[i][0]) << 16) | (u32(m_state[i][1]) << 8) | u32(m_state[i][2]);
		};
		unsigned const rate_value = std::clamp<int>(int(m_state[0x09][0]) - 0xe0, 0, 31);
		double const rate_hz = 0.05 * std::pow(200.0, double(rate_value) / 31.0);
		unsigned const delay_value = std::clamp<int>(int(m_state[0x0a][0]) - 0x20, 0, 64);
		double const centre = (2.0 + double(delay_value) * (28.0 / 64.0)) * 32.0;   // samples @32kHz
		double const depth_l = std::min(1.0, double(state24(0x0b)) / 0x2fd) * 5.0 * 32.0;
		double const depth_r = std::min(1.0, double(state24(0x11)) / 0x0ff) * 5.0 * 32.0;
		m_lfo_phase += 2.0 * 3.14159265358979323846 * rate_hz / 32000.0;
		if (m_lfo_phase >= 2.0 * 3.14159265358979323846)
			m_lfo_phase -= 2.0 * 3.14159265358979323846;
		double const sn = std::sin(m_lfo_phase);
		auto tap = [this](double back) {
			double const pos = double(int(m_frame)) - back;
			int const i0 = int(std::floor(pos));
			double const fr = pos - std::floor(pos);
			s32 const a = s32(m_dram[(((m_head_base << 6) + i0) & 0xffff)]) << 15;
			s32 const b = s32(m_dram[(((m_head_base << 6) + i0 + 1) & 0xffff)]) << 15;
			return s32(a + (b - a) * fr);
		};
		if (rate_value || delay_value)
		{
			m_chorus_l = tap(centre + sn * depth_l);
			m_chorus_r = tap(centre - sn * depth_r);
		}
	}
	++m_frame;
}

//-------------------------------------------------------------------------
//  mixing
//-------------------------------------------------------------------------

void roland_rcc_device::update_dry_gain(u8 index)
{
	for (unsigned voice = 0; voice < NUM_CHANNELS; voice++)
	{
		unsigned const program_voice = (voice + m_program_voice_offset) & (NUM_CHANNELS - 1);
		if (program_voice == 28)
			continue;
		for (unsigned side = 0; side < 2; side++)
		{
			if (GAIN_PROGRAM[program_voice][side] != index)
				continue;

			// Hardware tests identify coefficient 0x40 as unity (gain =
			// byte/64); the byte is sign-magnitude. [V hw]
			float const mag = std::clamp(float(m_program[index][2] & 0x7f) / 64.0F, 0.0F, 2.0F);
			m_gain[voice][side] = (m_program[index][2] & 0x80) ? -mag : mag;
		}
	}
}

void roland_rcc_device::sound_stream_update(sound_stream &stream)
{
	for (int sample = 0; sample < stream.samples(); sample++)
	{
		// dry mix (hardware-calibrated per-voice gains) + effect-bus input
		// (the per-voice input-bus multiplexing is owned by the PCM-side
		// chip; modeled as the summed voice mix [H])
		float dry_l = 0.0F;
		float dry_r = 0.0F;
		float effect_in = 0.0F;
		for (unsigned voice = 0; voice < NUM_CHANNELS; voice++)
		{
			float const input = stream.get(voice, sample);
			dry_l += input * m_gain[voice][0];
			dry_r += input * m_gain[voice][1];
			effect_in += input;
		}

		// DC-block the effect input: the real per-voice effect sends are
		// gated coefficients (zeroed on release), but the summed-voice input
		// model would otherwise feed the network the PCM chip's held DC
		// tails.  One-pole high-pass, ~5Hz at 32kHz. [H input model]
		m_effect_dc += (effect_in - m_effect_dc) * 0.001F;
		float const effect_ac = effect_in - m_effect_dc;

		// run the mask-ROM program for this frame
		run_program(s32(std::clamp(effect_ac, -1.0F, 1.0F) * 4194303.0F));
		float const wet_l = float(m_wet_l) / 8388608.0F;
		float const wet_r = float(m_wet_r) / 8388608.0F;

		// output buses (see roland_rcc.h)
		// chorus wet levels (program slots 0x01/0x03, calibrated)
		float const cwl = float(m_program[0x01][2] & 0x7f) / 127.0F * 0.5F;
		float const cwr = float(m_program[0x03][2] & 0x7f) / 127.0F * 0.5F;
		float const cho_l = float(m_chorus_l) / 8388608.0F * cwl;
		float const cho_r = float(m_chorus_r) / 8388608.0F * cwr;
		stream.put(0, sample, std::clamp(dry_l + wet_l + cho_l, -1.0F, 1.0F));
		stream.put(1, sample, std::clamp(dry_r + wet_r + cho_r, -1.0F, 1.0F));
		stream.put(2, sample, std::clamp(dry_l, -1.0F, 1.0F));
		stream.put(3, sample, std::clamp(dry_r, -1.0F, 1.0F));
		stream.put(4, sample, std::clamp(cho_l, -1.0F, 1.0F));
		stream.put(5, sample, std::clamp(cho_r, -1.0F, 1.0F));
		stream.put(6, sample, std::clamp(wet_l, -1.0F, 1.0F));
		stream.put(7, sample, std::clamp(wet_r, -1.0F, 1.0F));
	}
}
