// license:BSD-3-Clause
// copyright-holders:Giulio Zausa
/*
    Ricoh RF5C16A/RP5C16 CRT Controller

    Most stuff still TODO, so far made to make the Roland S760 work
*/

#include "emu.h"
#include "rf5c16.h"

#include "screen.h"

// #define VERBOSE 1
#include "logmacro.h"


// devices
DEFINE_DEVICE_TYPE(RF5C16, rf5c16_device, "RF5C16", "Ricoh RF5C16A/RP5C16 CRT Controller")


// default address map
void rf5c16_device::rf5c16(address_map &map) {
	if (!has_configured_map(0))
		map(0x0000, 0x1ffff).ram();
}

device_memory_interface::space_config_vector rf5c16_device::memory_space_config() const {
	return space_config_vector {
		std::make_pair(0, &m_space_config)
	};
}

//-------------------------------------------------
//  rf5c16_device - constructor
//-------------------------------------------------

rf5c16_device::rf5c16_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, RF5C16, tag, owner, clock),
		device_memory_interface(mconfig, *this),
		device_video_interface(mconfig, *this),
		m_space_config("videoram", ENDIANNESS_LITTLE, 16, 17, 0, address_map_constructor(FUNC(rf5c16_device::rf5c16), this)) {
}


//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void rf5c16_device::device_start() {
	// register for state saving
	save_item(NAME(transferReg));
	save_item(NAME(transferAddr));
	save_item(NAME(transferMode));
	save_item(NAME(flags));
	save_item(NAME(upperSet));
	save_item(NAME(displayMode));
	save_item(NAME(foregroundOn));
	save_item(NAME(backgroundOn));
	save_item(NAME(rOn));
	save_item(NAME(mOrS));
	save_item(NAME(ah));
	save_item(NAME(cursorCoordH));
	save_item(NAME(cursorCoordV));
	save_item(NAME(charGenBase));
	save_item(NAME(foregroundBase));
	save_item(NAME(backgroundBase));
	save_item(NAME(fg3rdColor));
	save_item(NAME(fg2rdColor));
	save_item(NAME(bg3rdColor));
	save_item(NAME(bg2rdColor));
	save_item(NAME(bdColorPeripheral));
	save_item(NAME(bdColorCenter));
	save_item(NAME(dotScrollV));
	save_item(NAME(dotScrollH));

	screen().register_screen_bitmap(m_bitmap);
}


uint32_t rf5c16_device::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect) {
	for (int x = 0; x < 320; x++) {
		for (int y = 0; y < 200; y++) {
			int linPos = 320 * y + x;
			int linPosSrc = linPos / 8;
			int linPosSrcRem = linPos % 8;
			size_t basePtr = backgroundBase * 2 + linPosSrc * 4;

			bool pvR = is_bit_set(space().read_byte(basePtr + 0), 7 - linPosSrcRem);
			bool pvG = is_bit_set(space().read_byte(basePtr + 1), 7 - linPosSrcRem);
			bool pvB = is_bit_set(space().read_byte(basePtr + 2), 7 - linPosSrcRem);
			// bool pvL = is_bit_set(space().read_byte(basePtr + 3), 7 - linPosSrcRem);
			// uint8_t col = pvR | pvG << 1 | pvB << 2 | pvL << 3;
			uint8_t col = pvR | pvG << 1 | pvB << 2;
			m_bitmap.pix(y, x) = col;
		}
	}
	for (size_t y = 0; y < 25; y++) {
		for (size_t x = 0; x < 40; x++) {
			uint16_t ch = space().read_word((foregroundBase + (y * 40 + x)) * 2);
			size_t charBase = charGenBase + (ch & 0xFF) * 8;
			for (size_t fy = 0; fy < 8; fy++) {
				for (size_t fx = 0; fx < 8; fx++) {
					bool pv = is_bit_set(space().read_word((charBase + fy) * 2), 7 - fx);
					m_bitmap.pix(y * 8 + fy, x * 8 + fx) |= pv ? 0b111 : 0b000;
				}
			}
		}
	}
	
	copybitmap(bitmap, m_bitmap, 0, 0, 0, 0, cliprect);
	return 0;
}

//-------------------------------------------------
//  reg_r - read from register port
//-------------------------------------------------

uint8_t rf5c16_device::reg_r(uint8_t offset) {
	uint8_t value = 0xFF;
	switch (offset) {
  case 0: // transferReg L
    value = space().read_word(transferAddr * 2) & 0xFF;
		// LOG("RF5C16 Mem %X Read: %02X\n", transferAddr * 2, value);
    if (transferMode == 0xA) {
      transferAddr++;
    }
    break;
  case 1: // transferReg M
    value = space().read_word(transferAddr * 2) >> 8;
    break;
  case 2: // transferAddr L
    value = transferAddr & 0xFF;
    break;
  case 3: // transferAddr M
    value = transferAddr >> 8;
    break;
  case 4:
    value = flags;
    break;
  }

	// LOG("RF5C16 Register %X Read: %02X\n", offset, value);
	return value;
}

//-------------------------------------------------
//  reg_w - write to register port
//-------------------------------------------------

void rf5c16_device::reg_w(uint8_t offset, uint8_t data) {
	// LOG("RF5C16 Register %X Write: %02X\n", offset, data);

	switch (offset) {
  case 0x0: // transferReg L
    if (upperSet) {
      transferReg &= 0xFF00;
      transferReg |= data;
    } else {
      transferReg = data;
    }
    space().write_word(transferAddr * 2, transferReg);
		LOG("RF5C16 Mem %X Write: %02X\n", transferAddr * 2, transferReg);
    upperSet = false;
    if (!(transferMode & 0b10)) {
      transferAddr++;
    }
    break;
  case 0x1: // transferReg M
    upperSet = true;
    transferReg &= 0x00FF;
    transferReg |= data << 8;
    // space().write_word(transferAddr, transferReg);
    break;
  case 0x2: // transferAddr L
    transferAddr &= 0xFF00;
    transferAddr |= data;
    break;
  case 0x3: // transferAddr M
    transferAddr &= 0x00FF;
    transferAddr |= data << 8;
    break;
  case 0x4:
    transferMode = data;
    break;
  case 0x5:
		displayMode = data >> 6;
		foregroundOn = (data >> 5) & 0b1;
		backgroundOn = (data >> 4) & 0b1;
		rOn = (data >> 3) & 0b1;
		mOrS = (data >> 2) & 0b1;
		ah = data & 0b11;
		break;
	case 0x6:
		cursorCoordH = (cursorCoordH & 0xFF00) | data;
		break;
	case 0x7:
		cursorCoordV = data;
		break;
	case 0x8:
		charGenBase = ((data >> 3) & 0b11111) << 8;
		cursorCoordH = (cursorCoordH & 0x00FF) | (data << 8);
		break;
	case 0x9:
		foregroundBase = data << 8;
		break;
	case 0xA:
		backgroundBase = (backgroundBase & 0xFF00) | data;
		break;
	case 0xB:
		backgroundBase = (backgroundBase & 0x00FF) | (data << 8);
		break;
	case 0xC:
		fg3rdColor = (data >> 4) & 0b1111;
		fg2rdColor = data & 0b1111;
		break;
	case 0xD:
		bg3rdColor = (data >> 4) & 0b1111;
		bg2rdColor = data & 0b1111;
		break;
	case 0xE:
		bdColorPeripheral = (data >> 4) & 0b1111;
		bdColorCenter = data & 0b1111;
		break;
	case 0xF:
		dotScrollV = (data >> 4) & 0b1111;
		dotScrollH = data & 0b111;
		break;
  }
}

uint32_t rf5c16_device::col_to_rgb(uint8_t color) {
  uint32_t out = 0xFF000000;
  uint32_t bright = color & 0b1000 ? 0xFF : 0x7F;
  if (color & 0b001)
    out |= bright << 16;
  if (color & 0b010)
    out |= bright << 8;
  if (color & 0b100)
    out |= bright << 0;
  return out;
}

bool rf5c16_device::is_bit_set(uint16_t num, int bit) {
	return 1 == ((num >> bit) & 1);
}
