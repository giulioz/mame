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

protected:
	sh7032_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock, address_map_constructor internal_map);
	void sh703x_map(address_map &map) ATTR_COLD;

private:
	void sh7032_map(address_map &map) ATTR_COLD;
};

class sh7034_device : public sh7032_device
{
public:
	sh7034_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

private:
	void sh7034_map(address_map &map) ATTR_COLD;
};

DECLARE_DEVICE_TYPE(SH7032, sh7032_device)
DECLARE_DEVICE_TYPE(SH7034, sh7034_device)

#endif // MAME_CPU_SH_SH7032_H
