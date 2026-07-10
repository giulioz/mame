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

// Field map v2.1 + anchor-searched dry machine (rcc repo,
// db/RCC_FIELDMAP_V2.md).  RAM-B word, per step (all [V hw+die]):
//   [17:14] DRAM address nibble (serial, 4 bits/step -- delay engine, TODO)
//   [13:9]  RAM-A address a5: the step's memory operand / store target
//   [8]     product alignment select ("shifter": 1 -> >>6, hw-calibrated)
//   [7:0]   sign-magnitude coefficient (0x40 = unity at shift=1)
//
// Per-step datapath (unique survivor of the 3.6M-config anchor search
// against the D-70/U-220 test-mode captures) [V hw]:
//   sample (b25,b24): 0 | RAM-A[a5] | audio-in | acc-bank tap
//   A-op   (b21..23): codes 001/010 -> RAM-A[a5] (read-modify-write),
//                     011 -> audio-in (capture), 000/111 -> 0 [H: 2 codes]
//   res    = sat24(A + (b13 ? (sample * coef) >> align : 0))
//   pipeline: accw = res from TWO steps back (multiplier/adder registers)
//   every step: bank[(b17,b16)] <= accw;  b2: RAM-A[a5] <= accw
//   b3 strobes capture accw onto the DAC buses: mix L at step 0x61,
//   mix R at step 0x35 [V hw]; direct-out strobe map still unknown [H]

roland_rcc_device::roland_rcc_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock) :
	device_t(mconfig, ROLAND_RCC, tag, owner, clock),
	device_sound_interface(mconfig, *this),
	m_stream(nullptr),
	m_frame(0),
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
	save_item(NAME(m_bank));
	save_item(NAME(m_hist));
	save_item(NAME(m_dram));
	save_item(NAME(m_frame));
	save_item(NAME(m_mix_l));
	save_item(NAME(m_mix_r));
	save_item(NAME(m_program_voice_offset));
}

void roland_rcc_device::device_reset()
{
	std::fill(std::begin(m_io), std::end(m_io), 0);
	std::fill_n(&m_program[0][0], 0x100 * 3, 0);
	std::fill_n(&m_state[0][0], 0x20 * 3, 0);
	std::fill(std::begin(m_ram_a), std::end(m_ram_a), 0);
	std::fill(std::begin(m_ram_b), std::end(m_ram_b), 0);
	std::fill(std::begin(m_bank), std::end(m_bank), 0);
	std::fill(std::begin(m_hist), std::end(m_hist), 0);
	std::fill(std::begin(m_dram), std::end(m_dram), 0);
	m_frame = 0;
	m_mix_l = 0;
	m_mix_r = 0;
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
		// RAM-A host load: 24-bit signed data word. [V]
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
		// RAM-B parameter load: 18-bit word in the low bits. [V]
		m_ram_b[data] = ((u32(m_io[0]) << 16) | (u32(m_io[1]) << 8) | u32(m_io[2])) & 0x3ffff;
		if (char const *dump = std::getenv("RCC_DUMP_RAMB"); dump)
		{
			if (FILE *f = std::fopen(dump, "a"); f)
			{
				std::fprintf(f, "%u %02x %02x%02x%02x\n", m_frame, data, m_io[0], m_io[1], m_io[2]);
				std::fclose(f);
			}
		}
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

// Coefficient: sign-magnitude byte (0x40 = unity at shift=1). [V hw]
s32 roland_rcc_device::decode_coef(u32 param)
{
	s32 const mag = param & 0x7f;
	return (param & 0x80) ? -mag : mag;
}

void roland_rcc_device::run_program(s32 const *voices)
{
	// acc-bank select maps, from the die's one-hot decode / read muxes,
	// polarity class fixed by the anchor search [V die + hw class]
	static constexpr u8 WMAP[4] = { 2, 3, 0, 1 };  // idx = ((b17<<1)|b16)^1
	static constexpr u8 RMAP[4] = { 1, 3, 0, 2 };  // idx = ((b20<<1)|b19)^1

	for (int step = 0; step < 256; ++step)
	{
		u32 const op = PROGRAM_ROM[step];
		u32 const P = m_ram_b[step];               // auto-fetched by PC [V]
		int const a5 = (P >> 9) & 0x1f;
		s32 const coef = decode_coef(P);
		int const align = BIT(P, 8) ? 6 : 4;       // shift=0 scale open [H]
		s32 const ain = voices[step >> 3];

		// multiplier sample source (b25,b24) [V]
		s32 sample;
		switch ((BIT(op, 25) << 1) | BIT(op, 24))
		{
		case 0:  sample = 0; break;
		case 1:  sample = m_ram_a[a5]; break;
		case 2:  sample = ain; break;
		default: sample = m_bank[RMAP[((BIT(op, 20) << 1) | BIT(op, 19)) ^ 1]]; break;
		}

		// A operand (b21..b23): RMW / audio capture / zero [V hw]
		s32 a;
		switch ((BIT(op, 21) << 2) | (BIT(op, 22) << 1) | BIT(op, 23))
		{
		case 1: case 2: a = m_ram_a[a5]; break;
		case 3:         a = ain; break;
		default:        a = 0; break;             // 000/111 unresolved [H]
		}

		// MAC (product gated by b13) + 24-bit saturation [V]
		s64 acc = s64(a);
		if (BIT(op, 13))
			acc += (s64(sample) * coef) >> align;
		s32 const res = s32(std::clamp<s64>(acc, -0x800000, 0x7fffff));

		// two-step result pipeline: accw = res(step-2) [V hw]
		s32 const accw = m_hist[1];

		// acc-bank file write (every step) [V die]
		m_bank[WMAP[((BIT(op, 17) << 1) | BIT(op, 16)) ^ 1]] = accw;

		// RAM-A store (b2) [V]
		if (BIT(op, 2))
			m_ram_a[a5] = accw;

		// DAC strobes (b3): mix pair identified by hardware routing [V hw];
		// direct-out strobes not yet mapped, delay engine (b9/b14, nibble
		// addresses) not yet modeled -- next bring-up stage
		if (step == 0x61)
			m_mix_l = accw;
		else if (step == 0x35)
			m_mix_r = accw;

		m_hist[1] = m_hist[0];
		m_hist[0] = res;
	}
	++m_frame;
}

//-------------------------------------------------------------------------
//  stream interface
//-------------------------------------------------------------------------

void roland_rcc_device::sound_stream_update(sound_stream &stream)
{
	for (int sample = 0; sample < stream.samples(); sample++)
	{
		// LP chip voice bus: 18-bit samples (17-bit magnitude + sign)
		// time-multiplexed one voice per 8-step window; the D-70 offsets
		// its LP contexts against the program windows. [V]
		s32 voices[NUM_CHANNELS];
		for (unsigned w = 0; w < NUM_CHANNELS; w++)
		{
			unsigned const v = (w - m_program_voice_offset) & (NUM_CHANNELS - 1);
			voices[w] = s32(std::clamp(stream.get(v, sample), -1.0F, 1.0F) * 131071.0F);
		}
		if (char const *dump = std::getenv("RCC_DUMP_VOICES"); dump && (m_frame & 63) == 0)
		{
			unsigned best = 0;
			s32 lvl = 0;
			for (unsigned w = 0; w < NUM_CHANNELS; w++)
				if (std::abs(voices[w]) > lvl) { lvl = std::abs(voices[w]); best = w; }
			if (FILE *f = std::fopen(dump, "a"); f)
			{
				std::fprintf(f, "%u w%u %d\n", m_frame, best, lvl);
				std::fclose(f);
			}
		}

		run_program(voices);

		// gain staging: one full-scale voice at the standard dry byte
		// (0x2c) lands around -3 dBFS, matching the previous device's
		// level; the full 32-voice sum clips into the clamp [H staging]
		float const mix_l = std::clamp(float(m_mix_l) / 131072.0F, -1.0F, 1.0F);
		float const mix_r = std::clamp(float(m_mix_r) / 131072.0F, -1.0F, 1.0F);

		stream.put(0, sample, mix_l);
		stream.put(1, sample, mix_r);
		stream.put(2, sample, mix_l);              // direct outs: strobe map
		stream.put(3, sample, mix_r);              // pending, mirror mix [H]
		stream.put(4, sample, 0.0F);
		stream.put(5, sample, 0.0F);
		stream.put(6, sample, 0.0F);               // effect returns: delay
		stream.put(7, sample, 0.0F);               // engine pending
	}
}
