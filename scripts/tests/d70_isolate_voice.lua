-- license:BSD-3-Clause
-- copyright-holders:giulioz

local machine = manager.machine
local cpu = machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local patch = machine.ioport.ports[":KEY4"].fields["F4"]
local isolate = tonumber(os.getenv("D70_ISOLATE_VOICE") or "3")
local gain = {
	[1] = { 0x2a, 0x2b }, [2] = { 0x34, 0x35 },
	[3] = { 0x3e, 0x40 }, [4] = { 0x44, 0x45 }
}
local pressed = false
local released = false
local muted = false

local function timestamp()
	local now = machine.time
	return now.seconds + now.attoseconds / 1000000000000000000
end

local function set_gain(index, coefficient)
	space:write_u8(0x0a02, coefficient)
	space:write_u8(0x0a06, index)
end

emu.register_periodic(function()
	local now = timestamp()
	if not pressed and now >= 5.0 then
		patch:set_value(0)
		pressed = true
	elseif pressed and not released and now >= 5.2 then
		patch:set_value(1)
		released = true
	elseif not muted and now >= 10.2 then
		for voice = 1, 4 do
			if voice ~= isolate then
				set_gain(gain[voice][1], 0)
				set_gain(gain[voice][2], 0)
			end
		end
		muted = true
		print(string.format("D70ISOLATE voice=%d", isolate))
	end
end)
