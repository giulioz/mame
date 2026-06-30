-- license:BSD-3-Clause
-- copyright-holders:giulioz

local cpu = manager.machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local selected_voice = 0
local pitch_high = {}
local voices = {}
local taps = {}
local reported = false

local function timestamp()
	local now = manager.machine.time
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

local function voice_info(index)
	local result = voices[index]
	if result == nil then
		result = {
			allocated = false,
			active = false,
			stopped = false,
			phase_corrected = false,
			baseline = nil,
			bend_up = nil,
			bend_down = nil,
			center = nil,
			lfo_values = {},
			lfo_last = nil,
			lfo_last_time = nil,
			lfo_interval_sum = 0.0,
			lfo_interval_count = 0
		}
		voices[index] = result
	end
	return result
end

local function pitch_write(address, data)
	local reg = address & 0x1f
	if reg == 0x1f then
		selected_voice = data & 0x1f
	elseif reg == 0x05 then
		pitch_high[selected_voice] = data
	elseif reg == 0x04 then
		local step = ((pitch_high[selected_voice] or 0) << 8) | data
		local now = timestamp()
		local info = voice_info(selected_voice)
		if now >= 10.75 and now < 11.0 then
			info.baseline = step
		elseif now >= 11.1 and now < 12.0 then
			info.bend_up = step
		elseif now >= 12.1 and now < 13.0 then
			info.bend_down = step
		elseif now >= 13.1 and now < 14.0 then
			info.center = step
		elseif now >= 14.0 and now < 16.0 and step ~= info.lfo_last then
			info.lfo_values[step] = true
			if info.lfo_last_time ~= nil then
				info.lfo_interval_sum = info.lfo_interval_sum + now - info.lfo_last_time
				info.lfo_interval_count = info.lfo_interval_count + 1
			end
			info.lfo_last = step
			info.lfo_last_time = now
		end
	end
end

taps[#taps + 1] = space:install_write_tap(0x0900, 0x091f, "d70_pitch_regression_lp", function(offset, data, mask)
	each_byte(offset, data, mask, pitch_write)
end)

taps[#taps + 1] = space:install_write_tap(0xc780, 0xc85f, "d70_pitch_regression_voice", function(offset, data, mask)
	each_byte(offset, data, mask, function(address, byte)
		local base = address & 0xffe0
		local index = address & 0x1f
		local info = voice_info(index)
		if base == 0xc780 and byte == 0x3c and timestamp() >= 9.5 then
			info.allocated = true
		elseif base == 0xc820 and (byte & 0x02) ~= 0 then
			info.phase_corrected = true
		elseif base == 0xc840 then
			if byte ~= 0 then
				info.active = true
			elseif info.active then
				info.stopped = true
			end
		end
	end)
end)

local function count_keys(values)
	local count = 0
	for _ in pairs(values) do
		count = count + 1
	end
	return count
end

local function cents(ratio)
	return 1200.0 * math.log(ratio) / math.log(2.0)
end

emu.register_periodic(function()
	if reported or timestamp() < 18.0 then
		return
	end
	reported = true

	local checked = 0
	local passed = true
	local summaries = {}
	for index, info in pairs(voices) do
		if info.allocated then
			local complete = info.baseline ~= nil and info.bend_up ~= nil
				and info.bend_down ~= nil and info.center ~= nil
			local up = complete and cents(info.bend_up / info.baseline) or 9999.0
			local down = complete and cents(info.bend_down / info.baseline) or 9999.0
			local cadence = info.lfo_interval_count > 0
				and info.lfo_interval_sum / info.lfo_interval_count or 0.0
			local distinct = count_keys(info.lfo_values)
			local voice_pass = complete and math.abs(up - 200.0) < 0.5
				and math.abs(down + 200.0) < 0.5
				and info.center == info.baseline
				and distinct >= 8 and cadence >= 0.018 and cadence <= 0.024
				and info.phase_corrected and info.active and info.stopped
			passed = passed and voice_pass
			checked = checked + 1
			summaries[#summaries + 1] = string.format(
				"v%02d=%d,up=%.3f,down=%.3f,lfo=%d,dt=%.6f,phase=%d,stop=%d",
				index, voice_pass and 1 or 0, up, down, distinct, cadence,
				info.phase_corrected and 1 or 0, info.stopped and 1 or 0)
		end
	end
	passed = passed and checked >= 2
	print(string.format("D70PITCH PASS=%d voices=%d %s",
		passed and 1 or 0, checked, table.concat(summaries, " ")))
end)

_G.d70_pitch_regression_taps = taps
