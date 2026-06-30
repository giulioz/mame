-- license:BSD-3-Clause
-- copyright-holders:giulioz

local machine = manager.machine
local cpu = machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local regs = cpu.spaces["register"]
local dlmoog = os.getenv("D70_TRACE_DLMOOG") == "1"
local schizoid = os.getenv("D70_TRACE_SCHIZOID") == "1"
local rcc_solo = tonumber(os.getenv("D70_TRACE_RCC_SOLO") or "")
local trace_rcc = os.getenv("D70_TRACE_RCC") == "1"
local trace_voice_state = os.getenv("D70_TRACE_VOICE_STATE") == "1"
local patch = (dlmoog or schizoid)
	and machine.ioport.ports[":KEY2"].fields[schizoid and "Number 5" or "Number 4"]
	or machine.ioport.ports[":KEY4"].fields["F4"]
local bank = machine.ioport.ports[":KEY1"].fields[(dlmoog or schizoid) and "Bank 5" or "Bank 2"]
local performance = machine.ioport.ports[":KEY0"].fields["Performance"]
local taps = {}
local selected_voice = 0
local lp = {}
local started = {}
local last_step = {}
local last_step_time = {}
local last_step_pc = {}
local bank_pressed = not (dlmoog or schizoid)
local bank_released = not (dlmoog or schizoid)
local performance_pressed = not (dlmoog or schizoid)
local performance_released = not (dlmoog or schizoid)
local pressed = false
local released = false
local reported = false
local solo_applied = false
local rcc_bytes = {}
local rcc_state = {}
local rcc_program = {}
local rcc_reported = false
local voice_state_reported = false
local current_bank = 0

local gain_program = {
	{ 0x04, 0x05 }, { 0x0c, 0x0d }, { 0x15, 0x16 }, { 0x20, 0x21 },
	{ 0x24, 0x25 }, { 0x2a, 0x2b }, { 0x34, 0x35 }, { 0x3e, 0x40 },
	{ 0x44, 0x45 }, { 0x4c, 0x50 }, { 0x55, 0x56 }, { 0x5e, 0x60 },
	{ 0x64, 0x65 }, { 0x71, 0x72 }, { 0x76, 0x77 }, { 0x7d, 0x7e },
	{ 0x85, 0x89 }, { 0x8c, 0x8f }, { 0x93, 0x96 }, { 0x9d, 0x9e },
	{ 0xa4, 0xa7 }, { 0xab, 0xac }, { 0xb3, 0xb4 }, { 0xbf, 0xc0 },
	{ 0xc3, 0xc4 }, { 0xcd, 0xce }, { 0xd7, 0xd8 }, { 0xdf, 0xe0 },
	{ 0xff, 0xff }, { 0xef, 0xf0 }, { 0xf5, 0xf6 }, { 0xfe, 0xff }
}

local function apply_rcc_solo()
	if rcc_solo == nil then
		return
	end
	for voice_index = 0, 31 do
		if voice_index ~= rcc_solo then
			local program_voice = ((voice_index + 4) & 31) + 1
			if program_voice ~= 29 then
				for _, program_index in ipairs(gain_program[program_voice]) do
					space:write_u8(0x0a02, 0)
					space:write_u8(0x0a06, program_index)
				end
			end
		end
	end
	print(string.format("D70DEMO RCC_SOLO=%d", rcc_solo))
end

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

taps[#taps + 1] = space:install_write_tap(0x0100, 0x0101, "d70_demo_trace_bank", function(_, data, mask)
	each_byte(0x0100, data, mask, function(_, byte)
		current_bank = byte
	end)
end)

local function voice(index)
	if lp[index] == nil then
		lp[index] = { regs = {}, reg_pc = {}, starts = 0, stops = 0, step_changes = 0, min_step = 0xffff, max_step = 0, mode = 0 }
	end
	return lp[index]
end

taps[#taps + 1] = space:install_write_tap(0x0900, 0x091f, "d70_demo_trace_lp", function(offset, data, mask)
	each_byte(offset, data, mask, function(address, byte)
		local reg = address & 0x1f
		if reg == 0x1f then
			selected_voice = byte & 0x1f
		elseif reg < 0x10 then
			local info = voice(selected_voice)
			info.regs[reg] = byte
			info.reg_pc[reg] = (current_bank << 16) | cpu.state["PC"].value
			if reg == 0x03 then
				info.tone_pointer = regs:read_u16(0x80)
				info.wave_pointer = regs:read_u16(0x84)
			end
			if reg == 0x04 then
				local step = ((info.regs[0x05] or 0) << 8) | byte
				if last_step[selected_voice] ~= step then
					info.step_changes = info.step_changes + 1
					info.min_step = math.min(info.min_step, step)
					info.max_step = math.max(info.max_step, step)
					last_step[selected_voice] = step
					last_step_time[selected_voice] = timestamp()
					last_step_pc[selected_voice] = (current_bank << 16) | cpu.state["PC"].value
					if started[selected_voice] and timestamp() >= 9.5 then
						print(string.format("D70DEMO STEP t=%.9f v=%02d value=%04x",
							timestamp(), selected_voice, step))
					end
				end
			elseif reg == 0x06 and started[selected_voice] and timestamp() >= 9.5 then
				print(string.format("D70DEMO ENV t=%.9f v=%02d value=%02x%02x",
					timestamp(), selected_voice, info.regs[0x07] or 0, byte))
			end
		elseif reg == 0x11 or reg == 0x13 or reg == 0x15 or reg == 0x17 then
			local base = ((reg >> 1) & 3) * 8
			for bit = 0, 7 do
				local index = base + bit
				local enabled = (byte & (1 << bit)) ~= 0
				if enabled and not started[index] then
					local info = voice(index)
					info.starts = info.starts + 1
					info.mode = info.regs[3] or 0
					started[index] = true
					print(string.format("D70DEMO START t=%.6f pc=%06x step_pc=%06x v=%02d mode=%02x step=%04x env=%02x%02x addr=%02x%02x%02x%02x end=%02x%02x loop=%02x%02x",
						timestamp(), (current_bank << 16) | cpu.state["PC"].value, last_step_pc[index] or 0,
						index, info.regs[3] or 0, last_step[index] or 0,
						info.regs[0x07] or 0, info.regs[0x06] or 0,
						info.regs[0x0b] or 0, info.regs[0x0a] or 0, info.regs[9] or 0, info.regs[8] or 0,
						info.regs[0x0d] or 0, info.regs[0x0c] or 0, info.regs[0x0f] or 0, info.regs[0x0e] or 0))
					if trace_voice_state then
						local bytes = {}
						for register = 0, 15 do
							bytes[#bytes + 1] = string.format("%02x", info.regs[register] or 0)
						end
						print(string.format("D70DEMO REGS v=%02d bytes=%s", index, table.concat(bytes)))
						local pcs = {}
						for register = 0, 15 do
							pcs[#pcs + 1] = string.format("%06x", info.reg_pc[register] or 0)
						end
						print(string.format("D70DEMO REGPCS v=%02d pcs=%s", index, table.concat(pcs, ",")))
						local descriptor = {}
						for byte_offset = 0, 9 do
							descriptor[#descriptor + 1] = string.format("%02x", space:read_u8((info.wave_pointer or 0) + byte_offset))
						end
						local tone_name = {}
						for byte_offset = 0, 9 do
							local character = space:read_u8((info.tone_pointer or 0) + byte_offset)
							tone_name[#tone_name + 1] = character >= 0x20 and character <= 0x7e and string.char(character) or "."
						end
						print(string.format("D70DEMO SOURCE v=%02d tone=%04x name='%s' wave=%04x descriptor=%s",
							index, info.tone_pointer or 0, table.concat(tone_name), info.wave_pointer or 0, table.concat(descriptor)))
					end
				elseif not enabled and started[index] then
					voice(index).stops = voice(index).stops + 1
					started[index] = false
				end
			end
		end
	end)
end)

taps[#taps + 1] = space:install_write_tap(0x0a00, 0x0a0f, "d70_demo_trace_rcc", function(offset, data, mask)
	each_byte(offset, data, mask, function(address, byte)
		local reg = address & 0x0f
		rcc_bytes[reg] = byte
		if reg == 0x04 then
			rcc_state[byte] = { rcc_bytes[0] or 0, rcc_bytes[1] or 0, rcc_bytes[2] or 0 }
		elseif reg == 0x06 then
			rcc_program[byte] = { rcc_bytes[0] or 0, rcc_bytes[1] or 0, rcc_bytes[2] or 0 }
		end
		if trace_rcc and reg == 0x06 then
			local index = byte
			if index == 0x2a or index == 0x2b or index == 0x34
				or index == 0x35 or index == 0x3e or index == 0x40 then
				print(string.format("D70DEMO RCC t=%.9f index=%02x word=%02x%02x%02x",
					timestamp(), index, rcc_bytes[2] or 0, rcc_bytes[1] or 0, rcc_bytes[0] or 0))
			end
		end
	end)
end)

emu.register_periodic(function()
	local now = timestamp()
	if not performance_pressed and now >= 3.4 then
		performance:set_value(0)
		performance_pressed = true
	elseif performance_pressed and not performance_released and now >= 3.6 then
		performance:set_value(1)
		performance_released = true
	elseif not bank_pressed and now >= 4.0 then
		bank:set_value(0)
		bank_pressed = true
	elseif bank_pressed and not bank_released and now >= 4.2 then
		bank:set_value(1)
		bank_released = true
	elseif not pressed and now >= 5.0 then
		patch:set_value(0)
		pressed = true
		print("D70DEMO PATCH_DOWN")
	elseif pressed and not released and now >= 5.2 then
		patch:set_value(1)
		released = true
		print("D70DEMO PATCH_UP")
	elseif trace_rcc and not rcc_reported and now >= 9.0 then
		rcc_reported = true
		for _, index in ipairs({ 0x08, 0x09, 0x0a, 0x0b, 0x0e, 0x0f, 0x10, 0x11 }) do
			local value = rcc_state[index] or { 0, 0, 0 }
			print(string.format("D70DEMO RCC_STATE index=%02x word=%02x%02x%02x",
				index, value[3], value[2], value[1]))
		end
		for _, index in ipairs({ 0x01, 0x03, 0x1d, 0x1f, 0x2e, 0x38, 0x6e, 0x70, 0x71,
			0x92, 0xb0, 0xb1, 0xb2, 0xb3, 0xc1, 0xc9, 0xcb, 0xcc, 0xd1, 0xd3, 0xd6 }) do
			local value = rcc_program[index] or { 0, 0, 0 }
			print(string.format("D70DEMO RCC_PROGRAM index=%02x word=%02x%02x%02x",
				index, value[3], value[2], value[1]))
		end
	elseif not solo_applied and now >= 10.2 then
		solo_applied = true
		apply_rcc_solo()
	elseif trace_voice_state and not voice_state_reported and now >= 10.3 then
		voice_state_reported = true
		for base = 0xc700, 0xc9e0, 0x20 do
			print(string.format("D70DEMO STATE base=%04x v01=%02x v02=%02x v03=%02x",
				base, space:read_u8(base + 1), space:read_u8(base + 2), space:read_u8(base + 3)))
		end
	elseif not reported and now >= 16.0 then
		reported = true
		local used = 0
		local alternate = 0
		for index = 0, 31 do
			local info = lp[index]
			if info ~= nil then
				if info.starts > 0 then
					used = used + 1
					if (info.mode & 0x80) ~= 0 then
						alternate = alternate + 1
					end
				end
				print(string.format("D70DEMO VOICE v=%02d starts=%d stops=%d steps=%d range=%04x-%04x",
					index, info.starts, info.stops, info.step_changes, info.min_step, info.max_step))
			end
		end
		if dlmoog then
			local passed = used == 4 and alternate == 0
			print(string.format("D70DLMOOG PASS=%d voices=%d alternate=%d",
				passed and 1 or 0, used, alternate))
		elseif schizoid then
			local passed = used == 3 and alternate == 1
			print(string.format("D70SCHIZOID PASS=%d voices=%d alternate=%d",
				passed and 1 or 0, used, alternate))
		else
			local passed = used == 28 and alternate >= 7
				and voice(28).starts == 1 and voice(29).starts == 1
				and voice(0).starts == 0 and voice(24).starts == 0
			print(string.format("D70CHORUSBELL PASS=%d voices=%d alternate=%d high=%d,%d reserved=%d,%d",
				passed and 1 or 0, used, alternate, voice(28).starts, voice(29).starts,
				voice(0).starts, voice(24).starts))
		end
		print("D70DEMO DONE")
	end
end)

_G.d70_demo_trace_taps = taps
