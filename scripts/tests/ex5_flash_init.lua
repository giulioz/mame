-- Yamaha EX5R blank IC10 initialization regression.
-- Run with an empty NVRAM directory:
--   ./mame ex5r -rompath . -nvram_directory /tmp/ex5-nvram -video none
--     -sound none -autoboot_script scripts/tests/ex5_flash_init.lua

local machine = manager.machine
local space = machine.devices[":subcpu"].spaces["program"]
local done = false

local function expect_words(address, expected)
	for index, value in ipairs(expected) do
		local actual = space:read_u16(address + (index - 1) * 2)
		assert(actual == value, string.format("%06x = %04x, expected %04x", address + (index - 1) * 2, actual, value))
	end
end

emu.register_periodic(function()
	if not done and machine.time.seconds >= 8 then
		done = true
		expect_words(0x600000, { 0x4558, 0x352f, 0x3720, 0x464c, 0x5365 }) -- "EX5/7 FLSe"
		expect_words(0x690000, { 0x496e, 0x6974, 0x2050, 0x6572, 0x666f, 0x726d }) -- "Init Perform"
		print("EX5R IC10 initialization PASS")
		machine:exit()
	end
end)
