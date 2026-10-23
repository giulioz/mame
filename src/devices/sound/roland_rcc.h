// license:GPL-2.0
// copyright-holders:giulioz
#ifndef MAME_SOUND_ROLAND_RCC_H
#define MAME_SOUND_ROLAND_RCC_H

#pragma once

#include <array>

// Roland RCC reverb/chorus gate array (Toshiba TC23SC140AF).
class tc23sc140_device : public device_t, public device_sound_interface
{
public:
	tc23sc140_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	// Stream inputs 0..31 use a scale of 1/131072. Select source bits 1..17
	// and the supplied sign bit, optionally delaying capture by four voice slots.
	// Outputs 0..7 are program-slot samples, scaled by 1/32768; the board routes them.
	void set_input_bus(u8 sign_bit, bool four_slot_delay = false)
	{
		m_input_sign_bit = sign_bit;
		m_four_slot_delay = four_slot_delay;
	}
	// External DRAM wiring; the chip itself produces a 16-bit word address.
	void set_dram_mask(u16 mask) { m_dram_mask = mask; }

	// Logical byte-register interface, as used by the U-220/D-70 CPU adapters.
	u8 read(offs_t offset);
	void write(offs_t offset, u8 data);

	// Admitted H:L bus closures. 'alternate' is the external bus-mode strap.
	u16 read_bus(offs_t offset);
	void write_bus(offs_t offset, u16 data, bool alternate = false);

	// Parallel 18-bit inputs in ROM capture order. The board supplies samples
	// before their capture aperture. D2 selects signed versus offset-binary input.
	void input_w(offs_t slot, s32 sample);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_clock_changed() override;
	virtual const tiny_rom_entry *device_rom_region() const override ATTR_COLD;
	virtual void sound_stream_update(sound_stream &stream) override;

private:
	// Chip state and arithmetic.

	void reset_state();
	uint8_t register_r(uint16_t offset) const { return register_bus_r(offset); }
	uint16_t register_bus_r(uint16_t offset) const;
	void register_w(uint16_t offset, uint8_t data);
	void register_bus_w(uint16_t offset, uint16_t data, bool alternate = false);
	void clock_half_step();
	void set_program_words(const uint32_t *program) { m_program_words = program; }
	void capture_input(unsigned index, int32_t sample) { m_inputs[index & 31] = sample; }
	int32_t output(unsigned index) const { return (m_registers[0x0d] & 0x40) ? m_state.output_words[index & 7] : 0; }

	void load_readback(uint32_t word);
	void capture_host_register(uint16_t offset, uint8_t data);
	void commit_host_write(bool ram_a, uint8_t address);
	void clock_half_edge();
	void arithmetic_edge(unsigned pc);
	uint32_t host_payload() const;
	struct instruction
	{
		uint32_t word = 0;
		constexpr unsigned bit(const unsigned index) const { return (word >> index) & 1u; }
		constexpr unsigned coefficient_source() const { return word & 7u; }
		constexpr unsigned multiplier_source() const { return ((bit(3) ^ 1u) << 1) | (bit(4) ^ 1u); }
		constexpr unsigned a_source(const bool result_sign) const
		{
			return ((unsigned(!result_sign && !bit(7)) | (bit(7) & bit(6))) << 1) | (bit(7) & bit(5));
		}
		constexpr unsigned bank_read() const { return ((bit(8) ^ 1u) << 1) | (bit(9) ^ 1u); }
		constexpr unsigned bank_write() const { return ((bit(12) ^ 1u) << 1) | (bit(11) ^ 1u); }
		constexpr unsigned flags() const { return (word >> 16) & 7u; }
	};

	static constexpr uint32_t WORD24_MASK = 0xffffffu;
	static constexpr uint32_t WORD29_MASK = 0x1fffffffu;

	static uint32_t saturate_result(uint32_t result29);
	static uint32_t format_product(uint32_t product32, bool shift_four, bool invert);
	static unsigned carry(uint32_t formatted32, bool invert, bool mode);
	static uint32_t add(uint32_t a24, uint32_t formatted32, bool invert, bool mode);
	static uint8_t coefficient(unsigned selector, uint32_t bank24, uint32_t ram_a24, uint8_t parameter);

	struct sources
	{
		uint32_t ram_a_read = 0;          // Output of the separate 24-bit RAM-A read latches.
		uint32_t external_read = 0;       // Selected external-memory 24-bit bank.
		uint32_t voice_capture = 0;       // Output of the separate 18-bit input latches.
		uint8_t parameter = 0;            // RAM-B bits 7:0, after their parameter registers.
		bool parameter_alignment = false; // RAM-B bit 8, after rect2601.
		uint8_t external_flag_pins = 0;   // Raw external input bits 2:0 at the sampling edge.
		bool use_rom_flags = true;        // path4156, derived separately from mode/strap inputs.
	};

	struct datapath
	{
		uint32_t a = 0;
		// B[28:0] and the three tail bits have corresponding D values and clocks.
		// This packed view preserves all 32 captured bits, including B's sign.
		uint32_t formatted = 0;
		bool invert = false;
		bool carry_mode = false;
		uint32_t result = 0;
		uint32_t multiplier_operand = 0;
		uint8_t multiplier_coefficient = 0;
		bool shift_four = false;
		uint8_t external_flags = 0;
		std::array<uint32_t, 4> banks{};

		uint32_t feedback() const { return saturate_result(result); }
		void rising_edge(instruction op, const sources &inputs, uint32_t product);
		void latch_feedback_bank(instruction op);
	};
	struct requests
	{
		bool a_write = false, a_read = false, b_write = false, b_read = false;
		bool a_write_cross = false, a_read_cross = false, b_write_cross = false, b_read_cross = false;
		bool a_write_grant = false, a_read_grant = false, b_write_grant = false, b_read_grant = false;
		uint8_t address = 0, a_address = 0;
	};
	requests m_requests{};
	std::array<int32_t, 32> m_inputs{};
	unsigned m_frame_half_edge = 0, m_voice_index = 0;

	struct pipeline
	{
		datapath dsp{};
		uint32_t parameter = 0, ram_b_read = 0;
		uint8_t ram_b_address = 1;
		std::array<uint8_t, 3> tap_nibbles{}; // High, middle, low staged tap nibbles.
		bool parameter_primed = false;
		uint32_t ram_a_read = 0;
		uint32_t voice_capture = 0;
		std::array<uint32_t, 2> external_read{};
		uint32_t external_output = 0;
		uint32_t assembled_read = 0;
		uint16_t variable_offset = 0;
		uint16_t offset = 0;
		uint16_t address_sum = 0;
		uint16_t counter = 0xbbbb;
		uint16_t transaction_address = 0;
		uint32_t transaction_word = 0;
		bool transaction_pending = false;
		bool transaction_write = false;
		std::array<int32_t, 8> output_words{};
	};
	pipeline m_state{};

	const uint32_t *m_program_words = nullptr;
	std::array<uint32_t, 65536> m_dram{};
	uint16_t m_dram_mask = 0xffff;
	std::array<uint8_t, 16> m_registers{};
	uint32_t m_read_capture = 0;
	uint32_t m_rom_capture = 0;
	std::array<uint32_t, 32> m_ram_a{};
	std::array<uint32_t, 256> m_ram_b{};

	void advance_to(attotime time);
	void synchronize();
	TIMER_CALLBACK_MEMBER(sample_tick);

	required_region_ptr<u32> m_program;

	sound_stream *m_stream = nullptr;
	emu_timer *m_sample_timer = nullptr;
	attotime m_clock_origin;
	u64 m_half_edges = 0;
	u32 m_input_clock = 0;
	u8 m_input_sign_bit = 0;
	bool m_four_slot_delay = false;
	std::array<s32, 4> m_serial_delay{};
};

DECLARE_DEVICE_TYPE(TC23SC140, tc23sc140_device)

#endif // MAME_SOUND_ROLAND_RCC_H
