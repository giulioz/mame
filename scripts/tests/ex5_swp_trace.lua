-- license:BSD-3-Clause
-- copyright-holders:Giulio Zausa
--
-- Trace the EX5/EX5R sub CPU's two SWP30B interfaces.  This intentionally
-- observes the CPU bus rather than SWP30 internals, so it also reports writes
-- to registers whose hardware meaning has not been established yet.

local machine = manager.machine
local cpu = machine.devices[":subcpu"]
local space = cpu.spaces["program"]
local taps = {}

local unknown_bits = { 38, 35, 34, 32, 3, 2, 1, 0 }
local interesting_controls = {
	[0x00] = true, [0x01] = true,
	[0x18] = true, [0x19] = true, [0x1a] = true,
	[0x26] = true, [0x27] = true, [0x29] = true, [0x2b] = true,
	[0x43] = true, [0x44] = true, [0x45] = true, [0x46] = true,
	[0x47] = true, [0x48] = true, [0x49] = true,
	[0x53] = true, [0x54] = true, [0x55] = true, [0x56] = true,
	[0x57] = true, [0x58] = true, [0x59] = true, [0x5a] = true,
	[0x5b] = true, [0x5c] = true,
}

local chips = {
	master = { base = 0x500000, controls = {}, program = {}, address = 0, words = {}, writes = 0, reported_writes = 0 },
	slave  = { base = 0x580000, controls = {}, program = {}, address = 0, words = {}, writes = 0, reported_writes = 0 },
}

local function timestamp()
	local now = machine.time
	return now.seconds + now.attoseconds / 1000000000000000000
end

local function engine_name(value)
	if value == 0x0003 then
		return "VL"
	elseif value == 0x0101 then
		return "AN"
	elseif value == 0x0001 then
		return "AN+FDSP"
	elseif value == 0x0000 then
		return "off/transition"
	elseif value == 0x0100 then
		return "transition"
	end
	return "unknown"
end

local function control_index(relative)
	local word = relative >> 1
	local channel = (word >> 6) & 0x3f
	local slot = word & 0x3f
	if slot == 0x0e or slot == 0x0f then
		return channel * 2 + slot - 0x0e
	end
	return nil
end

local function program_complete(name, chip)
	local opcode = (chip.words[0] << 48) | (chip.words[1] << 32) | (chip.words[2] << 16) | chip.words[3]
	chip.program[chip.address] = opcode
	chip.address = (chip.address + 1) % 0x180
	chip.writes = chip.writes + 1
	chip.last_program_write = timestamp()
end

local function write16(name, address, value)
	local chip = chips[name]
	local relative = address - chip.base
	local word = relative >> 1
	local channel = (word >> 6) & 0x3f
	local slot = word & 0x3f
	local control = control_index(relative)

	if slot == 0x0b then
		print(string.format("EX5SWP t=%.6f chip=%s slot0b channel=%02x value=%04x",
			timestamp(), name, channel, value))
	end

	if control == nil then
		return
	end

	local old = chip.controls[control]
	chip.controls[control] = value
	if interesting_controls[control] and old ~= value then
		print(string.format("EX5SWP t=%.6f chip=%s control=%02x old=%s value=%04x",
			timestamp(), name, control, old and string.format("%04x", old) or "----", value))
	end
	if control == 0x43 and old ~= value then
		print(string.format("EX5ENGINE t=%.6f chip=%s value=%04x state=%s",
			timestamp(), name, value, engine_name(value)))
	elseif control == 0x21 then
		chip.address = value < 0x180 and value or 0
	elseif control >= 0x22 and control <= 0x25 then
		chip.words[control - 0x22] = value
		if control == 0x25 then
			program_complete(name, chip)
		end
	end
end

local function install_chip(name, chip)
	taps[#taps + 1] = space:install_write_tap(chip.base, chip.base + 0x1fff,
		"ex5_swp_trace_" .. name, function(offset, data, mask)
		-- MB91103 is big-endian.  A 16-bit SWP access can occupy either half
		-- of the CPU's 32-bit data bus.
		if (mask & 0xffff0000) ~= 0 then
			write16(name, offset, (data >> 16) & 0xffff)
		end
		if (mask & 0x0000ffff) ~= 0 then
			write16(name, offset + 2, data & 0xffff)
		end
	end)
end

for name, chip in pairs(chips) do
	install_chip(name, chip)
end

local function report_program(name, chip, reason)
	local counts = {}
	for _, bit in ipairs(unknown_bits) do
		counts[bit] = 0
	end
	local populated = 0
	for _, opcode in pairs(chip.program) do
		populated = populated + 1
		for _, bit in ipairs(unknown_bits) do
			if ((opcode >> bit) & 1) ~= 0 then
				counts[bit] = counts[bit] + 1
			end
		end
	end
	local fields = {}
	for _, bit in ipairs(unknown_bits) do
		fields[#fields + 1] = string.format("b%d=%d", bit, counts[bit])
	end
	print(string.format("EX5MEG reason=%s chip=%s writes=%d populated=%d %s",
		reason, name, chip.writes, populated, table.concat(fields, " ")))
	chip.reported_writes = chip.writes
end

local reported = false
emu.register_periodic(function()
	local now = timestamp()
	for name, chip in pairs(chips) do
		if chip.writes ~= chip.reported_writes and chip.last_program_write ~= nil
				and now - chip.last_program_write >= 0.02 then
			report_program(name, chip, "idle")
		end
	end
	if not reported and now >= 10.0 then
		reported = true
		for name, chip in pairs(chips) do
			report_program(name, chip, "final")
		end
		print("EX5SWP DONE")
	end
end)

_G.ex5_swp_trace_taps = taps
