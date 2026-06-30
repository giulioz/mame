-- license:BSD-3-Clause
-- copyright-holders:Giulio Zausa
-- Regression test for opaque SWP30 registers used by the EX5 firmware.

local machine = manager.machine
local space = machine.devices[":subcpu"].spaces["program"]
local ran = false

local function control_address(base, index)
	local slot = 0x40 * (index >> 1) | 0x0e | (index & 1)
	return base + slot * 2
end

emu.register_periodic(function()
	if not ran and machine.time.seconds >= 1 then
		ran = true
		local slot0b = 0x500000 + (7 * 0x40 + 0x0b) * 2
		local control43 = control_address(0x500000, 0x43)
		local control5c = control_address(0x580000, 0x5c)
		space:write_u16(slot0b, 0x1357)
		space:write_u16(control43, 0x2468)
		space:write_u16(control5c, 0x55aa)
		local pass = space:read_u16(slot0b) == 0x1357
			and space:read_u16(control43) == 0x2468
			and space:read_u16(control5c) == 0x55aa
		print(string.format("EX5SWPLATCH PASS=%d slot0b=%04x control43=%04x control5c=%04x",
			pass and 1 or 0, space:read_u16(slot0b), space:read_u16(control43), space:read_u16(control5c)))
	end
end)
