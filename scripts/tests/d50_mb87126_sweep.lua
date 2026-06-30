-- license:BSD-3-Clause
-- copyright-holders:Giulio Zausa

-- Differential probe for the D-50 effect compilers.
--
-- The firmware owns the experiment: this script changes one live edit byte,
-- waits for routine 6655 and the asynchronous uploaders to settle, and then
-- reports changes to the CPU's IC28 shadows, the actual F800-F804 packets,
-- IC9's indirect register file, and the four observed IC8 host latches.
--
-- Set D50_MB87126_SWEEP=full for every legal EQ row, all chorus parameter
-- endpoints, all key/output combinations, all 16 ROM reverbs, and full
-- 0..100 mixer curves.  The default is a short representative sweep.

local machine = manager.machine
local cpu = machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local mode = os.getenv("D50_MB87126_SWEEP") or "quick"
local full = mode == "full"
local routing = mode == "routing"
local dump_baseline = os.getenv("D50_MB87126_DUMP") == "1"
local taps = {}

local DC_BASE = 0xdc00
local DD_BASE = 0xdd00

local ic28_register = { 0, 0, 0, 0, 0 }
local ic28_state = { {}, {} }
local ic9_address = 0
local ic9_high = 0
local ic9_state = {}
local ic8_state = {}
local transfers = {}
local tasks = {}

local function timestamp()
	local now = machine.time
	return now.seconds + now.attoseconds / 1000000000000000000
end

local function each_byte(offset, data, mask, callback)
	if (mask & 0x00ff) ~= 0 then
		callback(offset, data & 0xff)
	end
	if (mask & 0xff00) ~= 0 then
		callback(offset + 1, (data >> 8) & 0xff)
	end
end

local function copy_table(source)
	local result = {}
	for key, value in pairs(source) do
		result[key] = value
	end
	return result
end

local function shadow_snapshot()
	local result = { {}, {} }
	for selector = 1, 0x7f do
		local dc_address = DC_BASE + selector * 2
		local dd_address = DD_BASE + selector * 2
		result[1][selector] = (space:read_u8(dc_address) | (space:read_u8(dc_address + 1) << 8)) & 0x3fff
		result[2][selector] = (space:read_u8(dd_address) | (space:read_u8(dd_address + 1) << 8)) & 0x3fff
	end
	return result
end

local function add_task(name, writes, settle)
	tasks[#tasks + 1] = { name = name, writes = writes, settle = settle or 0.30 }
end

local function add_parameter(name, address, values, settle)
	for _, value in ipairs(values) do
		add_task(string.format("%s=%d", name, value), { { address, value } }, settle)
	end
end

local parameter = {
	{ "upper_lf_frequency", 0xc4a5, full and 15 or 15 },
	{ "upper_lf_gain",      0xc4a6, full and 24 or 24 },
	{ "upper_hf_frequency", 0xc4a7, full and 21 or 21 },
	{ "upper_hf_q",         0xc4a8, full and 11 or 11 },
	{ "upper_hf_gain",      0xc4a9, full and 24 or 24 },
	{ "lower_lf_frequency", 0xc565, full and 15 or 15 },
	{ "lower_lf_gain",      0xc566, full and 24 or 24 },
	{ "lower_hf_frequency", 0xc567, full and 21 or 21 },
	{ "lower_hf_q",         0xc568, full and 11 or 11 },
	{ "lower_hf_gain",      0xc569, full and 24 or 24 },
}

if not routing then
	for _, item in ipairs(parameter) do
		local values = {}
		if full then
			for value = 0, item[3] do values[#values + 1] = value end
		else
			values = { 0, item[3] }
		end
		add_parameter(item[1], item[2], values)
	end
end

local chorus = {
	{ "upper_chorus_type",    0xc4aa, 7 },
	{ "upper_chorus_rate",    0xc4ab, 100 },
	{ "upper_chorus_depth",   0xc4ac, 100 },
	{ "upper_chorus_balance", 0xc4ad, 100 },
	{ "lower_chorus_type",    0xc56a, 7 },
	{ "lower_chorus_rate",    0xc56b, 100 },
	{ "lower_chorus_depth",   0xc56c, 100 },
	{ "lower_chorus_balance", 0xc56d, 100 },
}
if not routing then
	for _, item in ipairs(chorus) do
		local values = full and { 0, item[3] // 2, item[3] } or { 0, item[3] }
		add_parameter(item[1], item[2], values, 0.45)
	end
end

local key_values = (full or routing) and { 0, 1, 2, 3, 4 } or { 0, 4 }
local output_values = (full or routing) and { 0, 1, 2, 3 } or { 0, 3 }
for _, key_mode in ipairs(key_values) do
	for _, output_mode in ipairs(output_values) do
		add_task(string.format("key_mode=%d,output_mode=%d", key_mode, output_mode),
			{ { 0xc592, key_mode }, { 0xc59d, output_mode } }, 0.60)
	end
end

if full then
	local curve = {}
	for value = 0, 100 do curve[#curve + 1] = value end
	add_parameter("reverb_balance", 0xc59f, curve, 0.40)
	add_parameter("total_volume", 0xc5a0, curve, 0.30)
	add_parameter("tone_balance", 0xc5a1, curve, 0.30)
	local reverbs = {}
	for value = 0, 15 do reverbs[#reverbs + 1] = value end
	add_parameter("reverb_type", 0xc59e, reverbs, 1.80)
else
	add_parameter("reverb_balance", 0xc59f, { 0, 50, 100 }, 0.50)
	add_parameter("total_volume", 0xc5a0, { 0, 50, 100 })
	add_parameter("tone_balance", 0xc5a1, { 0, 50, 100 })
	add_parameter("reverb_type", 0xc59e, { 0, 1, 7, 15 }, 1.80)
end

taps[#taps + 1] = space:install_write_tap(0xf800, 0xf804, "d50_mb87126_ic28", function(offset, data, mask)
	each_byte(offset, data, mask, function(address, byte)
		local register = (address & 0xffff) - 0xf800
		ic28_register[register + 1] = byte
		if register == 4 then
			local selector = ic28_register[1] & 0x7f
			local dc = (ic28_register[2] | ((ic28_register[3] & 0x3f) << 8)) & 0x3fff
			local dd = (ic28_register[4] | ((byte & 0x3f) << 8)) & 0x3fff
			ic28_state[1][selector] = dc
			ic28_state[2][selector] = dd
			transfers[#transfers + 1] = { selector, dc, dd }
		end
	end)
end)

taps[#taps + 1] = space:install_write_tap(0xf000, 0xf007, "d50_mb87126_ic9", function(offset, data, mask)
	each_byte(offset, data, mask, function(address, byte)
		local register = address & 7
		if register == 7 then
			ic9_address = byte
		elseif register == 0 then
			ic9_high = byte
		elseif register == 1 then
			ic9_state[ic9_address] = ((ic9_high << 8) | byte) & 0xffff
		end
	end)
end)

taps[#taps + 1] = space:install_write_tap(0xe700, 0xe707, "d50_mb87126_ic8", function(offset, data, mask)
	each_byte(offset, data, mask, function(address, byte)
		ic8_state[address & 7] = byte
	end)
end)

local originals = {}
local task_index = 0
local phase = "boot"
local deadline = 8.0
local baseline_shadow = nil
local baseline_ic9 = nil
local baseline_ic8 = nil

local function set_writes(writes, restore)
	for _, write in ipairs(writes) do
		local address = write[1]
		if originals[address] == nil then originals[address] = space:read_u8(address) end
		space:write_u8(address, restore and originals[address] or write[2])
	end
end

local function restore_all()
	for address, value in pairs(originals) do
		space:write_u8(address, value)
	end
end

local function report(task)
	local after = shadow_snapshot()
	local dc_changes = {}
	local dd_changes = {}
	for selector = 1, 0x7f do
		if baseline_shadow[1][selector] ~= after[1][selector] then
			dc_changes[#dc_changes + 1] = string.format("%02X:%04X>%04X", selector,
				baseline_shadow[1][selector], after[1][selector])
		end
		if baseline_shadow[2][selector] ~= after[2][selector] then
			dd_changes[#dd_changes + 1] = string.format("%02X:%04X>%04X", selector,
				baseline_shadow[2][selector], after[2][selector])
		end
	end

	local ic9_changes = {}
	for address = 0, 0xff do
		local before = baseline_ic9[address]
		local value = ic9_state[address]
		if value ~= nil and before ~= value then
			ic9_changes[#ic9_changes + 1] = string.format("%02X:%04X>%04X", address, before or 0, value)
		end
	end
	local ic8_changes = {}
	for address = 0, 7 do
		local before = baseline_ic8[address]
		local value = ic8_state[address]
		if value ~= nil and before ~= value then
			ic8_changes[#ic8_changes + 1] = string.format("%u:%02X>%02X", address, before or 0, value)
		end
	end

	print(string.format("D50MB PARAM %s DC=[%s] DD=[%s] IC9=[%s] IC8=[%s] XFER=%d",
		task.name, table.concat(dc_changes, ","), table.concat(dd_changes, ","),
		table.concat(ic9_changes, ","), table.concat(ic8_changes, ","), #transfers))
end

emu.register_periodic(function()
	local now = timestamp()
	if now < deadline then return end

	if phase == "boot" then
		-- Capture every source byte before the first experiment.  Restoring only
		-- the current byte would let the previous endpoint contaminate the next
		-- compiler run (particularly key/output and reverb-balance matrices).
		for _, task in ipairs(tasks) do
			for _, write in ipairs(task.writes) do
				if originals[write[1]] == nil then originals[write[1]] = space:read_u8(write[1]) end
			end
		end
		if dump_baseline then
			local snapshot = shadow_snapshot()
			for lane = 1, 2 do
				local words = {}
				for selector = 1, 0x7f do
					words[#words + 1] = string.format("%02X:%04X", selector, snapshot[lane][selector])
				end
				print(string.format("D50MB BASE %s=[%s]", lane == 1 and "DC" or "DD", table.concat(words, ",")))
			end
			local words = {}
			for address = 0, 0xff do
				if ic9_state[address] ~= nil then
					words[#words + 1] = string.format("%02X:%04X", address, ic9_state[address])
				end
			end
			print(string.format("D50MB BASE IC9=[%s]", table.concat(words, ",")))
		end
		print(string.format("D50MB START mode=%s tasks=%d", mode, #tasks))
		phase = "next"
	end

	if phase == "next" then
		task_index = task_index + 1
		if task_index > #tasks then
			restore_all()
			print("D50MB DONE")
			phase = "done"
			return
		end
		restore_all()
		deadline = now + tasks[task_index].settle
		phase = "baseline"
	elseif phase == "baseline" then
		baseline_shadow = shadow_snapshot()
		baseline_ic9 = copy_table(ic9_state)
		baseline_ic8 = copy_table(ic8_state)
		transfers = {}
		set_writes(tasks[task_index].writes, false)
		deadline = now + tasks[task_index].settle
		phase = "observe"
	elseif phase == "observe" then
		report(tasks[task_index])
		phase = "next"
		deadline = now
	end
end)

_G.d50_mb87126_sweep_taps = taps
