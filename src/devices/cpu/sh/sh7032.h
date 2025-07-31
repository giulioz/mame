// license:BSD-3-Clause
// copyright-holders:Angelo Salese

// SH7032, sh1 variant

#ifndef MAME_CPU_SH_SH7032_H
#define MAME_CPU_SH_SH7032_H

#pragma once

#include "sh7021.h"

class sh7032_device : public sh7021_device
{
public:
	sh7032_device(const machine_config &mconfig, const char *_tag, device_t *_owner, uint32_t _clock);

private:
	void sh7032_map(address_map &map) ATTR_COLD;
};

DECLARE_DEVICE_TYPE(SH7032, sh7032_device)

#endif // MAME_CPU_SH_SH7032_H
