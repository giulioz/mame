-- Walks factory patches and records the firmware's structure/booster-derived
-- per-voice state alongside XP bank 0x2000 writes at voice allocation.

local machine = manager.machine
local cpu = machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local buttons1 = machine.ioport.ports[":BUTTONS1"].fields
local buttons4 = machine.ioport.ports[":BUTTONS4"].fields
local output = os.getenv("JV1080_STRUCTURE_SWEEP") or "/tmp/jv1080_structure_sweep.csv"
local f = assert(io.open(output, "w"))
local patch = -1
local writes = {}

structure_write_tap = space:install_write_tap(0x04002000, 0x040020ff,
    "jv1080_structure_write", function(offset, data, mask)
        if patch >= 0 then
            table.insert(writes, string.format("%08x:%08x:%08x", offset, data, mask))
        end
    end)

local function press(field, seconds)
    field:set_value(1)
    emu.wait(seconds)
    field:clear_value()
end

f:write("patch,structure_bytes,config_words,xp_2000_writes\n")
emu.wait(12)
for index = 0, 127 do
    patch = index
    writes = {}
    press(buttons1["PREVIEW"], 0.12)
    emu.wait(0.08)
    local structures = {}
    local configs = {}
    for voice = 0, 63 do
        table.insert(structures, string.format("%02x", space:read_u8(0x01003af4 + voice)))
        table.insert(configs, string.format("%04x", space:read_u16(0x01008778 + voice * 2)))
    end
    f:write(string.format("%d,%s,%s,%s\n", index, table.concat(structures, ":"),
        table.concat(configs, ":"), table.concat(writes, ";")))
    press(buttons4["INC"], 0.05)
    emu.wait(0.15)
end
f:close()
print("JV1080_STRUCTURE_SWEEP_WRITTEN " .. output)
