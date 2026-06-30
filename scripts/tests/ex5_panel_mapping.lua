-- Yamaha EX5R logical panel-map regression.
-- Run with: ./mame ex5r -rompath . -skip_gameinfo -sound none
--   -autoboot_script scripts/tests/ex5_panel_mapping.lua

local machine = manager.machine
local space = machine.devices[":maincpu"].spaces["program"]
local controls = {
	{ "PANEL0", "SC2", 57 }, { "PANEL0", "SC1", 56 },
	{ "PANEL0", "VOICE", 58 }, { "PANEL0", "PERFORM", 59 },
	{ "PANEL0", "SONG", 60 }, { "PANEL0", "PATTERN", 61 },
	{ "PANEL0", "SAMPLE", 62 }, { "PANEL0", "UTILITY", 63 },
	{ "PANEL1", "DISK", 64 }, { "PANEL1", "EDIT/COMPARE", 65 },
	{ "PANEL1", "JOB", 66 }, { "PANEL1", "STORE", 67 },
	{ "PANEL1", "ARPEGGIO", 68 }, { "PANEL1", "KNOB MODE", 69 },
	{ "PANEL1", "TOP", 72 }, { "PANEL1", "REW", 73 },
	{ "PANEL2", "FWD", 74 }, { "PANEL2", "KEY MAP", 70 },
	{ "PANEL2", "BYPASS", 71 }, { "PANEL2", "REC", 75 },
	{ "PANEL2", "STOP", 76 }, { "PANEL2", "PLAY", 77 },
	{ "PANEL2", "SHIFT", 45 }, { "PANEL2", "F1", 0 },
	{ "PANEL3", "F2", 1 }, { "PANEL3", "F3", 2 },
	{ "PANEL3", "F4", 3 }, { "PANEL3", "F5", 4 },
	{ "PANEL3", "F6", 5 }, { "PANEL3", "F7", 6 },
	{ "PANEL3", "F8", 7 }, { "PANEL3", "EXIT", 44 },
	{ "PANEL4", "CANCEL", 55 }, { "PANEL4", "CURSOR/DATA", 54 },
	{ "PANEL4", "DEC/NO", 48 }, { "PANEL4", "Cursor Up", 53 },
	{ "PANEL4", "INC/YES", 49 }, { "PANEL4", "Cursor Left", 51 },
	{ "PANEL4", "Cursor Down", 52 }, { "PANEL4", "Cursor Right", 50 },
	{ "PANEL5", "7", 39 }, { "PANEL5", "8", 40 },
	{ "PANEL5", "9", 41 }, { "PANEL5", "4", 36 },
	{ "PANEL5", "5", 37 }, { "PANEL5", "6", 38 },
	{ "PANEL5", "1", 33 }, { "PANEL5", "2", 34 },
	{ "PANEL6", "3", 35 }, { "PANEL6", "0", 32 },
	{ "PANEL6", "Keypad -", 42 }, { "PANEL6", "ENTER", 43 },
}

local function panel_words()
	local result = {}
	for word = 0, 4 do result[word + 1] = space:read_u16(0x758224 + word * 2) end
	return result
end

local function check(control, pressed)
	local expected_word = control[3] // 16
	local expected_mask = pressed and (1 << (control[3] % 16)) or 0
	for word, value in ipairs(panel_words()) do
		local expected = (word - 1 == expected_word) and expected_mask or 0
		assert(value == expected, string.format("%s: word %d = %04x, expected %04x", control[2], word - 1, value, expected))
	end
end

local index = 1
local stage = 0
local started = false
emu.register_periodic(function()
	if not started then
		started = machine.time.seconds >= 6
		return
	end
	if controls[index] then
		local control = controls[index]
		local field = machine.ioport.ports[":" .. control[1]].fields[control[2]]
		if stage == 0 then
			field:set_value(1)
			stage = 1
		elseif stage == 1 then
			check(control, true)
			field:set_value(0)
			stage = 2
		else
			check(control, false)
			index = index + 1
			stage = 0
		end
	elseif not controls[index] then
		print(string.format("EX5R panel mapping PASS (%d switches)", #controls))
		machine:exit()
	end
end)
