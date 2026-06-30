-- Captures XP bus accesses together with the exact firmware PC/PR.  Output is
-- deliberately aggregated so boot-time DSP uploads remain tractable.
-- Set JV1080_SCENARIO to boot, modulation, or demo.

local machine = manager.machine
local cpu = machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local scenario = os.getenv("JV1080_SCENARIO") or "boot"
local accesses = {}
local timeline = {}
local timeline_limit = tonumber(os.getenv("JV1080_TIMELINE_LIMIT") or "20000")
local trace_registers = os.getenv("JV1080_TRACE_REGS") == "1"
local trace_after = tonumber(os.getenv("JV1080_TRACE_AFTER") or "0")
local irq_only = os.getenv("JV1080_TRACE_IRQ_ONLY") == "1"
local voice_status = os.getenv("JV1080_TRACE_VOICE_STATUS") == "1"
local dsp_reads = os.getenv("JV1080_TRACE_DSP_READS") == "1"

local function timestamp()
    -- attotime's display form uses thousands-grouping commas, which would
    -- otherwise corrupt the CSV fields.
    return string.gsub(tostring(machine.time), ",", "")
end

local function state(name)
    local item = cpu.state[name]
    return item and item.value or 0
end

local function record(kind, offset, data, mask)
    local pc = state("CURPC")
    if pc == 0 then pc = state("PC") end
    local pr = state("PR")
    local key = string.format("%s,%08x,%08x,%08x", kind, pc, pr, offset)
    local item = accesses[key]
    if not item then
        item = { count = 0, first = timestamp(), last = "", values = {} }
        accesses[key] = item
    end
    item.count = item.count + 1
    item.last = timestamp()
    local value = data & mask
    item.values[value] = (item.values[value] or 0) + 1
    local now = timestamp()
    if tonumber(now) >= trace_after and #timeline < timeline_limit then
        local line = string.format("%s,%s,%08x,%08x,%08x,%08x,%08x",
            now, kind, pc, pr, offset, data, mask)
        if trace_registers then
            local registers = {}
            for index = 0, 14 do
                table.insert(registers, string.format("%08x", state("R" .. index)))
            end
            line = line .. "," .. table.concat(registers, ",")
        end
        table.insert(timeline, line)
    end
end

local xp_trace_start = irq_only and 0x04003918 or ((voice_status or dsp_reads) and 0x04003910 or 0x04000000)
local xp_trace_end = (irq_only or voice_status or dsp_reads) and 0x0400391b or 0x04003fff
xp_read_tap = space:install_read_tap(xp_trace_start, xp_trace_end, "jv1080_xp_read_trace",
    function(offset, data, mask) record("R", offset, data, mask) end)
if not dsp_reads then
    xp_write_tap = space:install_write_tap(xp_trace_start, xp_trace_end, "jv1080_xp_write_trace",
        function(offset, data, mask) record("W", offset, data, mask) end)
end
if voice_status then
    xp_voice_write_tap = space:install_write_tap(0x04000000, 0x040003ff,
        "jv1080_xp_voice_write_trace",
        function(offset, data, mask) record("W", offset, data, mask) end)
end
if dsp_reads then
    xp_dsp_trigger_read_tap = space:install_read_tap(0x04002c00, 0x040038ff,
        "jv1080_xp_dsp_trigger_read_trace",
        function(offset, data, mask) record("T", offset, data, mask) end)
end

-- This byte selects one of the firmware's XP DSP program/coefficient pairs.
-- Capturing it alongside XP writes ties worker activity to the static template
-- table without tracing the rest of the 128 KiB external RAM window.
if not irq_only and not voice_status and not dsp_reads then
    dsp_selector_write_tap = space:install_write_tap(0x0101f884, 0x0101f887,
        "jv1080_dsp_selector_trace",
        function(offset, data, mask)
            if (mask & 0x00ff0000) ~= 0 then
                record("S", offset + 1, (data >> 16) & 0xff, 0xff)
            end
        end)
end

local function press(field, seconds)
    field:set_value(1)
    emu.wait(seconds)
    field:clear_value()
end

if scenario == "modulation" then
    local b1 = machine.ioport.ports[":BUTTONS1"].fields
    local b4 = machine.ioport.ports[":BUTTONS4"].fields
    emu.wait(12)
    press(b4["INC"], 0.2)
    emu.wait(0.5)
    press(b1["PREVIEW"], 12)
elseif scenario == "demo" then
    local b4 = machine.ioport.ports[":BUTTONS4"].fields
    emu.wait(12)
    b4["SHIFT"]:set_value(1)
    emu.wait(0.3)
    press(b4["ENTER"], 0.2)
    emu.wait(0.2)
    b4["SHIFT"]:clear_value()
    emu.wait(0.5)
    press(b4["ENTER"], 0.2)
    emu.wait(15)
else
    emu.wait(15)
end

local output = os.getenv("JV1080_XP_TRACE") or ("/tmp/jv1080_xp_" .. scenario .. ".csv")
local f = assert(io.open(output, "w"))
f:write(string.format("# lfo_time_scale_09001f61=%02x\n", space:read_u8(0x01001f61)))
local sequencer_phases = {}
for index = 0, 9 do
    sequencer_phases[#sequencer_phases + 1] = string.format("%02x", space:read_u8(0x0901316c + index))
end
f:write(string.format("# sequencer_phase_index_limit_table=%02x,%02x,%s\n",
    space:read_u8(0x09013176), space:read_u8(0x09013177),
    table.concat(sequencer_phases, ":")))
f:write(string.format("# sh_bsc_bcr_wcr1_wcr2_wcr3=%04x,%04x,%04x,%04x\n",
    space:read_u16(0x05ffffa0), space:read_u16(0x05ffffa2),
    space:read_u16(0x05ffffa4), space:read_u16(0x05ffffa6)))
f:write("# timeline: time,kind,pc,pr,address,data,mask[,r0..r14]\n")
for _, line in ipairs(timeline) do f:write(line, "\n") end
f:write("# aggregate: kind,pc,pr,address,count,first,last,values\n")
local keys = {}
for key in pairs(accesses) do table.insert(keys, key) end
table.sort(keys)
for _, key in ipairs(keys) do
    local item = accesses[key]
    local values = {}
    for value, count in pairs(item.values) do
        table.insert(values, string.format("%08x:%d", value, count))
    end
    table.sort(values)
    f:write(string.format("%s,%d,%s,%s,%s\n", key, item.count, item.first, item.last,
        table.concat(values, ";")))
end
f:close()
print("JV1080_XP_TRACE_WRITTEN " .. output)
