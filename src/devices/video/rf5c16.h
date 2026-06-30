// license:BSD-3-Clause
// copyright-holders:Giulio Zausa
/***************************************************************************

  Ricoh RF5C16A/RP5C16 CRT Controller

 ***************************************************************************/

#ifndef MAME_VIDEO_RF5C16_H
#define MAME_VIDEO_RF5C16_H

#pragma once


class rf5c16_device : public device_t, public device_memory_interface, public device_video_interface {
public:
	static constexpr unsigned TOTAL_WIDTH = 320;
	static constexpr unsigned TOTAL_HEIGHT = 200;
	
	// construction/destruction
	rf5c16_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	uint8_t reg_r(uint8_t offset);
	void reg_w(uint8_t offset, uint8_t data);

	uint32_t screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

protected:
	// device-level overrides
	virtual void device_start() override;

	// device_config_memory_interface overrides
	virtual space_config_vector memory_space_config() const override;

private:
	// address space configurations
	const address_space_config m_space_config;

	void rf5c16(address_map &map);

	uint32_t col_to_rgb(uint8_t color);
	bool is_bit_set(uint16_t num, int bit);

	uint16_t transferReg = 0;
	uint16_t transferAddr = 0;
	uint8_t transferMode = 0;
	uint8_t flags = 0xFF;
	bool upperSet = false;

	// 0: character 80x25
	// 1: graphics 640x200
	// 2: character 40x25 + character 40x25
	// 3: character 40x25 + graphics 320x200
	uint8_t displayMode = 0;
	bool foregroundOn = false;
	bool backgroundOn = false;
	bool rOn = false;   // ?? cursor?
	bool mOrS = false;  // ??
	uint8_t ah = false; // ?? 2 byte attribute?
	uint16_t cursorCoordH = 0;
	uint16_t cursorCoordV = 0;
	uint16_t charGenBase = 0;
	uint16_t foregroundBase = 0;
	uint16_t backgroundBase = 0;
	uint8_t fg3rdColor = 0;
	uint8_t fg2rdColor = 0;
	uint8_t bg3rdColor = 0;
	uint8_t bg2rdColor = 0;
	uint8_t bdColorPeripheral = 0;
	uint8_t bdColorCenter = 0;
	uint8_t dotScrollV = 0;
	uint8_t dotScrollH = 0;

	bitmap_ind16 m_bitmap;
};


// device type definition
DECLARE_DEVICE_TYPE(RF5C16, rf5c16_device)

#endif // MAME_VIDEO_RF5C16_H
