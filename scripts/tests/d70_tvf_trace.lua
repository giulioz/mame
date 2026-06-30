-- license:BSD-3-Clause
-- copyright-holders:giulioz

local machine = manager.machine
local cpu = machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local schizoid = os.getenv("D70_TRACE_SCHIZOID") == "1"
local bank = machine.ioport.ports[":KEY1"].fields[schizoid and "Bank 5" or "Bank 2"]
local patch = machine.ioport.ports[":KEY4"].fields["F5"]
local taps = {}
local lp_selected = 0
local lp_bytes = {}
local tvf_bytes = {}
local tvf_dest = 0
local tvf = {}
local bank_pressed = false
local bank_released = false
local pressed = false
local released = false
local reported = false

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

local function info(context)
	if tvf[context] == nil then
		tvf[context] = {
			pitch = {}, damping = {}, mode = {}, cutoff_current = {}, cutoff_rate = {},
			gain_current = {}, gain_rate = {}, cutoff = {}, gain = {},
			last_pitch = nil, last_damping = nil, last_mode = nil,
			last_cutoff_current = nil, last_cutoff_rate = nil,
			last_gain_current = nil, last_gain_rate = nil,
			last_cutoff = nil, last_gain = nil
		}
	end
	return tvf[context]
end

local function changed(values, value)
	values[value] = true
end

taps[#taps + 1] = space:install_write_tap(0x0900, 0x091f, "d70_tvf_trace_lp", function(offset, data, mask)
	each_byte(offset, data, mask, function(address, byte)
		local reg = address & 0x1f
		if reg == 0x1f then
			lp_selected = byte & 0x1f
		elseif reg < 0x10 then
			lp_bytes[lp_selected] = lp_bytes[lp_selected] or {}
			lp_bytes[lp_selected][reg] = byte
			if reg == 0x04 then
				local value = ((lp_bytes[lp_selected][0x05] or 0) << 8) | byte
				local state = info(lp_selected)
				changed(state.pitch, value)
				if value ~= state.last_pitch and timestamp() >= 4.5 then
					print(string.format("D70TVF t=%.9f v=%02d pitch=%04x", timestamp(), lp_selected, value))
				end
				state.last_pitch = value
			end
		end
	end)
end)

taps[#taps + 1] = space:install_write_tap(0x0c00, 0x0c7f, "d70_tvf_trace", function(offset, data, mask)
	each_byte(offset, data, mask, function(address, byte)
		local reg = address & 0x7f
		if timestamp() >= 4.9 and timestamp() < 5.1
			and (reg == 0x30 or reg == 0x31 or reg == 0x34 or reg == 0x35) then
			print(string.format("D70TVFRAW t=%.9f pc=%05x reg=%02x byte=%02x",
				timestamp(), cpu.state["PC"].value, reg, byte))
		end
		tvf_bytes[reg] = byte
		if reg == 0x41 then
			tvf_dest = (((tvf_bytes[0x41] or 0) << 8) | (tvf_bytes[0x40] or 0)) & 0x1f
		elseif reg == 0x09 or reg == 0x0b
			or reg == 0x21 or reg == 0x23 or reg == 0x25 or reg == 0x27
			or reg == 0x31 or reg == 0x35 then
			local base = reg - 1
			local value = ((tvf_bytes[reg] or 0) << 8) | (tvf_bytes[base] or 0)
			local state = info(tvf_dest)
			local field
			if base == 0x08 then field = "damping"
			elseif base == 0x0a then field = "mode"
			elseif base == 0x20 then field = "cutoff_current"
			elseif base == 0x22 then field = "cutoff_rate"
			elseif base == 0x24 then field = "gain_current"
			elseif base == 0x26 then field = "gain_rate"
			elseif base == 0x30 then field = "cutoff"
			else field = "gain" end
			changed(state[field], value)
			local last = "last_" .. field
			if value ~= state[last] and timestamp() >= 4.5 then
				print(string.format("D70TVF t=%.9f v=%02d %s=%04x", timestamp(), tvf_dest, field, value))
			end
			state[last] = value
		end
	end)
end)

local function count(values)
	local result = 0
	for _ in pairs(values) do result = result + 1 end
	return result
end

emu.register_periodic(function()
	local now = timestamp()
	if not bank_pressed and now >= 3.0 then
		bank:set_value(0)
		bank_pressed = true
	elseif bank_pressed and not bank_released and now >= 3.2 then
		bank:set_value(1)
		bank_released = true
	elseif not pressed and now >= 4.0 then
		patch:set_value(0)
		pressed = true
	elseif pressed and not released and now >= 4.2 then
		patch:set_value(1)
		released = true
	elseif not reported and now >= 14.0 then
		reported = true
		for context = 0, 31 do
			local state = tvf[context]
			if state ~= nil then
				print(string.format("D70TVFSUM v=%02d pitch=%d damping=%d mode=%d ccur=%d crate=%d gcur=%d grate=%d cutoff=%d gain=%d lastmode=%04x",
					context, count(state.pitch), count(state.damping), count(state.mode),
					count(state.cutoff_current), count(state.cutoff_rate), count(state.gain_current), count(state.gain_rate),
					count(state.cutoff), count(state.gain),
					state.last_mode or 0))
			end
		end
	end
end)

_G.d70_tvf_trace_taps = taps
