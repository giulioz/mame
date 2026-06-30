-- Correlates the ten documented Patch Structure values (and the four booster
-- levels) with firmware-generated XP bank-0x2000 words.

local machine = manager.machine
local cpu = machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local screen = machine.screens[":screen"]
local buttons1 = machine.ioport.ports[":BUTTONS1"].fields
local buttons2 = machine.ioport.ports[":BUTTONS2"].fields
local buttons3 = machine.ioport.ports[":BUTTONS3"].fields
local buttons4 = machine.ioport.ports[":BUTTONS4"].fields
local buttons5 = machine.ioport.ports[":BUTTONS5"].fields
local output = os.getenv("JV1080_STRUCTURE_EXACT") or "/tmp/jv1080_structure_exact.csv"
local snapshot_prefix = os.getenv("JV1080_STRUCTURE_SNAP") or "/tmp/jv1080-structure"
local writes = {}
local f

structure_write_tap = space:install_write_tap(0x04002000, 0x040020ff,
    "jv1080_structure_exact_write", function(offset, data, mask)
        table.insert(writes, string.format("%08x:%08x:%08x", offset, data, mask))
    end)

local function press(field, seconds)
    field:set_value(1)
    emu.wait(seconds or 0.08)
    field:clear_value()
    emu.wait(0.15)
end

local function preview()
    writes = {}
    press(buttons1["PREVIEW"], 0.18)
    emu.wait(0.12)
    return table.concat(writes, ";")
end

local function configs()
    local result = {}
    for voice = 0, 63 do
        local value = space:read_u16(0x01008778 + voice * 2)
        if value ~= 0 then
            table.insert(result, string.format("%d:%04x", voice, value))
        end
    end
    return table.concat(result, ";")
end

local function record(kind, structure, booster)
    local captured = preview()
    f:write(string.format("%s,%d,%d,%s,%s\n",
        kind, structure, booster, configs(), captured))
end

f = assert(io.open(output, "w"))
f:write("kind,structure,booster,config_words,xp_2000_writes\n")

emu.wait(12)
press(buttons2["PATCH"])
press(buttons3["PRESET"])
press(buttons3["A"])
-- PR-A:030 is known from the factory sweep to enable a paired structure.
for _ = 1, 29 do press(buttons4["INC"], 0.04) end
emu.wait(1.0)
screen:snapshot(string.format("%s-patch.png", snapshot_prefix))
press(buttons1["PARAMETER"])
press(buttons1["1/9"])
for _ = 1, 6 do press(buttons4["Cursor Down"]) end
-- Clamp the factory value down to TYPE 1 so the sweep has a known origin.
for _ = 1, 10 do press(buttons4["DEC"], 0.04) end

for structure = 1, 10 do
    screen:snapshot(string.format("%s-type-%02d.png", snapshot_prefix, structure))
    record("structure", structure, 0)
    if structure ~= 10 then press(buttons4["INC"]) end
end

-- Return to type 3, where Booster is editable, then select its field.
for _ = 1, 7 do press(buttons4["DEC"]) end
press(buttons5["Cursor Right"])
for booster = 0, 3 do
    screen:snapshot(string.format("%s-booster-%d.png", snapshot_prefix, booster))
    record("booster", 3, booster)
    if booster ~= 3 then press(buttons4["INC"]) end
end

f:close()
print("JV1080_STRUCTURE_EXACT_WRITTEN " .. output)
