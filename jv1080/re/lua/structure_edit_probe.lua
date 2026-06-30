-- Walk the Patch/Common edit pages, saving the LCD after each cursor move.
-- This is intentionally non-destructive: it does not change a parameter.

local machine = manager.machine
local screen = machine.screens[":screen"]
local buttons1 = machine.ioport.ports[":BUTTONS1"].fields
local buttons2 = machine.ioport.ports[":BUTTONS2"].fields
local buttons4 = machine.ioport.ports[":BUTTONS4"].fields
local output = os.getenv("JV1080_UI_PROBE") or "/tmp/jv1080-ui"

local function press(field, seconds)
    field:set_value(1)
    emu.wait(seconds or 0.08)
    field:clear_value()
    emu.wait(0.15)
end

local function snap(index)
    screen:snapshot(string.format("%s-%02d.png", output, index))
end

emu.wait(12)
press(buttons2["PATCH"])
press(buttons1["PARAMETER"])
press(buttons1["1/9"])
snap(0)

for index = 1, 16 do
    press(buttons4["Cursor Down"])
    snap(index)
end

print("JV1080_UI_PROBE_WRITTEN " .. output)
