-- Sweeps the twelve editable fields of every Insert EFX algorithm and records
-- the resulting XP DSP writes/PRAM/CRAM deltas.  Every edit is followed by a
-- released-and-retriggered PREVIEW note; this is a required part of the test,
-- not just an audible convenience.

local machine = manager.machine
local cpu = machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local screen = machine.screens[":screen"]
local b1 = machine.ioport.ports[":BUTTONS1"].fields
local b2 = machine.ioport.ports[":BUTTONS2"].fields
local b4 = machine.ioport.ports[":BUTTONS4"].fields
local b5 = machine.ioport.ports[":BUTTONS5"].fields

local output = os.getenv("JV1080_RFX_SWEEP") or "/tmp/jv1080_rfx_parameter_sweep.csv"
local snapshot_prefix = os.getenv("JV1080_RFX_SNAP")
local first_effect = tonumber(os.getenv("JV1080_RFX_FIRST") or "0")
local effect_count = tonumber(os.getenv("JV1080_RFX_COUNT") or "40")
local last_effect = math.min(39, first_effect + effect_count - 1)
local writes = {}

rfx_sweep_write_tap = space:install_write_tap(0x04002c00, 0x0400387f,
    "jv1080_rfx_parameter_write", function(offset, data, mask)
        table.insert(writes, string.format("%08x:%08x:%08x", offset, data, mask))
    end)

local function press(field, down_time, after_time)
    field:set_value(1)
    emu.wait(down_time or 0.07)
    field:clear_value()
    emu.wait(after_time or 0.12)
end

local function fresh_note()
    -- Releasing PREVIEW before returning guarantees that the next call creates
    -- a new voice/note-on rather than observing a voice created before an edit.
    press(b1["PREVIEW"], 0.14, 0.14)
end

local function goto_rfx_page(page)
    -- Re-selecting an already-active group preserves its current page.  Walk
    -- upward past every Effects sub-page (the UI clamps at PATCH OUTPUT), then
    -- count down from that known origin.  page=0 is EFX TYPE; page=1..3 are
    -- the editable EFX pages.
    press(b1["2/10"], 0.04, 0.08)
    for _ = 1, 12 do press(b4["Cursor Up"], 0.03, 0.05) end
    for _ = 0, page do press(b4["Cursor Down"], 0.04, 0.08) end
end

local function snapshot_dsp()
    local result = { pram = {}, cram = {} }
    for slot = 0, 287 do
        result.pram[slot] = space:read_u32(0x04003400 + slot * 4)
        result.cram[slot] = space:read_u16(0x04002c00 + slot * 2)
    end
    return result
end

local function deltas(before, after, member, width)
    local result = {}
    for slot = 0, 287 do
        local old = before[member][slot]
        local new = after[member][slot]
        if old ~= new then
            table.insert(result, string.format("%03x:%0" .. width .. "x>%0" .. width .. "x", slot, old, new))
        end
    end
    return table.concat(result, ";")
end

local function csv(value)
    value = tostring(value or "")
    if string.find(value, '[,\"\n]') then
        return '"' .. string.gsub(value, '"', '""') .. '"'
    end
    return value
end

local f = assert(io.open(output, "w"))
f:write("effect,page,field,direction,pram_delta,cram_delta,xp_parameter_writes,fresh_note\n")

emu.wait(12)
press(b2["PATCH"])
press(b1["PARAMETER"])
goto_rfx_page(0)

-- Move to the requested first type.  The factory patch starts at type 01,
-- which is firmware selector/template index zero.
for _ = 1, first_effect do press(b4["INC"], 0.04, 0.08) end

for effect = first_effect, last_effect do
    -- The type change itself is also validated with a fresh note.
    fresh_note()

    for page = 1, 3 do
        for field = 1, 4 do
            goto_rfx_page(page)
            for _ = 2, field do press(b5["Cursor Right"], 0.04, 0.08) end
            if snapshot_prefix and field == 1 then
                screen:snapshot(string.format("%s-%02d-p%d.png", snapshot_prefix, effect, page))
            end
            local before = snapshot_dsp()
            writes = {}
            press(b4["INC"], 0.04, 0.10)
            local direction = "INC"
            -- Factory values often sit at their upper boundary (EQ gains and
            -- output level are examples).  Use one decrement when INC caused
            -- no XP update so that the field still receives a real edit.
            if #writes == 0 then
                press(b4["DEC"], 0.04, 0.10)
                direction = "DEC"
            end
            local parameter_writes = table.concat(writes, ";")

            fresh_note()
            local after = snapshot_dsp()
            local row = {
                effect,
                page,
                field,
                direction,
                deltas(before, after, "pram", 8),
                deltas(before, after, "cram", 4),
                parameter_writes,
                "yes",
            }
            for index, value in ipairs(row) do row[index] = csv(value) end
            f:write(table.concat(row, ","), "\n")
            f:flush()
        end
    end

    if effect ~= last_effect then
        goto_rfx_page(0)
        press(b4["INC"], 0.04, 0.12)
    end
end

f:close()
print("JV1080_RFX_PARAMETER_SWEEP_WRITTEN " .. output)
machine:exit()
