-- Enters the JV-1080 manufacturing test mode using its hidden front-panel
-- chord and captures the initial CPU/version screen.  Set
-- JV1080_FACTORY_SELECT to a comma-separated sequence of PORT:FIELD items (for
-- example BUTTONS1:1/9,BUTTONS5:VALUE (push)) to exercise service screens.
-- Always use an isolated NVRAM directory:
-- some of the service operations intentionally overwrite user RAM.

local machine = manager.machine
local screen = machine.screens[":screen"]
local buttons1 = machine.ioport.ports[":BUTTONS1"].fields
local buttons4 = machine.ioport.ports[":BUTTONS4"].fields
local buttons5 = machine.ioport.ports[":BUTTONS5"].fields
local output = os.getenv("JV1080_FACTORY_TEST_SNAP") or "/tmp/jv1080-factory-test"
local hold_gate = os.getenv("JV1080_FACTORY_HOLD_GATE") == "1"
local selection = os.getenv("JV1080_FACTORY_SELECT")
local step_wait = tonumber(os.getenv("JV1080_FACTORY_STEP_WAIT")) or 2.0
local xp_trace_enabled = os.getenv("JV1080_FACTORY_XP_TRACE") == "1"
local cpu = machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local trace = {}
local xp_trace = {}

local function pc()
    local item = cpu.state["CURPC"] or cpu.state["PC"]
    return item and item.value or 0
end

local function press(field, seconds)
    field:set_value(1)
    emu.wait(seconds or 0.15)
    field:clear_value()
    emu.wait(0.25)
end

local function dump_input_state(label)
    local words = {}
    for index = 0, 3 do
        table.insert(words, string.format("%04x", space:read_u16(0x0101f2a2 + index * 2)))
    end
    print(string.format("JV1080_FACTORY_INPUT,%s,bitmap=%s,c18=%02x,c1b=%02x,c22=%02x,dispatch=%08x",
        label, table.concat(words, ":"), space:read_u8(0x0101f20c + 0x18),
        space:read_u8(0x0101f20c + 0x1b), space:read_u8(0x0101f20c + 0x22),
        space:read_u32(0x0101e02c)))
end

local function dump_ui_state(label)
    local leds = {}
    for index = 0, 7 do
        leds[#leds + 1] = string.format("%02x", space:read_u8(0x04380010 + index))
    end
    print(string.format("JV1080_FACTORY_UI,%s,dispatch=%08x,screen=%04x,state=%04x,switch_step=%04x,midi_flag=%02x,efx_status=%02x,leds=%s",
        label, space:read_u32(0x0101e02c), space:read_u16(0x0901f230),
        space:read_u16(0x0901ed5c), space:read_u16(0x0901ed64),
        space:read_u8(0x0901ed54), space:read_u8(0x0901ed8e), table.concat(leds, ":")))
end

local function dump_adc_state(label)
    local values = {}
    for index = 0, 18 do
        values[#values + 1] = string.format("%04x", space:read_u16(0x0901da62 + index * 2))
    end
    local hardware = {}
    for index = 0, 3 do
        hardware[#hardware + 1] = string.format("%04x", space:read_u16(0x05fffee0 + index * 2))
    end
    print(string.format("JV1080_FACTORY_ADC,%s,table=%s,addr=%s,adcsr=%02x,adcr=%02x",
        label, table.concat(values, ":"), table.concat(hardware, ":"),
        space:read_u8(0x05fffef8), space:read_u8(0x05fffef9)))
end

emu.wait(12)
screen:snapshot(output .. "-00-boot.png")

factory_ga_read_tap = space:install_read_tap(0x04380000, 0x0438003f,
    "jv1080_factory_ga_read", function(offset, data, mask)
        table.insert(trace, string.format("%s,R,%08x,%08x,%08x,%08x",
            tostring(machine.time):gsub(",", ""), pc(), offset, data, mask))
    end)
factory_ga_write_tap = space:install_write_tap(0x04380000, 0x0438003f,
    "jv1080_factory_ga_write", function(offset, data, mask)
        table.insert(trace, string.format("%s,W,%08x,%08x,%08x,%08x",
            tostring(machine.time):gsub(",", ""), pc(), offset, data, mask))
    end)
factory_direct_key_tap = space:install_write_tap(0x0101f264, 0x0101f26b,
    "jv1080_factory_direct_keys", function(offset, data, mask)
        if (data & mask) ~= mask then
            table.insert(trace, string.format("%s,D,%08x,%08x,%08x,%08x",
                tostring(machine.time):gsub(",", ""), pc(), offset, data, mask))
        end
    end)
factory_key_fifo_read_tap = space:install_read_tap(0x01000530, 0x010005ff,
    "jv1080_factory_key_fifo", function(offset, data, mask)
        table.insert(trace, string.format("%s,K,%08x,%08x,%08x,%08x",
            tostring(machine.time):gsub(",", ""), pc(), offset, data, mask))
    end)

if xp_trace_enabled then
    factory_xp_read_tap = space:install_read_tap(0x04002c00, 0x0400391b,
        "jv1080_factory_xp_read", function(offset, data, mask)
            xp_trace[#xp_trace + 1] = string.format("%s,R,%08x,%08x,%08x,%08x",
                tostring(machine.time):gsub(",", ""), pc(), offset, data, mask)
        end)
    factory_xp_write_tap = space:install_write_tap(0x04002c00, 0x0400391b,
        "jv1080_factory_xp_write", function(offset, data, mask)
            xp_trace[#xp_trace + 1] = string.format("%s,W,%08x,%08x,%08x,%08x",
                tostring(machine.time):gsub(",", ""), pc(), offset, data, mask)
        end)
end

-- First enter the SHIFT+ENTER diagnostic gate.
buttons4["SHIFT"]:set_value(1)
buttons4["ENTER"]:set_value(1)
emu.wait(0.2)
if not hold_gate then
    buttons4["ENTER"]:clear_value()
    buttons4["SHIFT"]:clear_value()
end
emu.wait(1.0)
screen:snapshot(output .. "-01-gate.png")

-- Then hold both vertical cursors while pressing the VALUE encoder.
buttons4["Cursor Up"]:set_value(1)
buttons4["Cursor Down"]:set_value(1)
emu.wait(0.5)
screen:snapshot(output .. "-02-cursors.png")
dump_input_state("cursors")
buttons5["VALUE (push)"]:set_value(1)
emu.wait(1.0)
screen:snapshot(output .. "-03-value-held.png")
dump_input_state("value")
buttons5["VALUE (push)"]:clear_value()
emu.wait(0.5)
buttons4["ENTER"]:clear_value()
buttons4["SHIFT"]:clear_value()
buttons4["Cursor Up"]:clear_value()
buttons4["Cursor Down"]:clear_value()
emu.wait(6.0)

screen:snapshot(output .. "-entered.png")
dump_ui_state("entered")
dump_adc_state("entered")
print("JV1080_FACTORY_TEST_ENTERED")

if selection then
    local step = 0
    for item in selection:gmatch("[^,]+") do
        step = step + 1
        local port_name, field_name = item:match("^([^:]+):(.+)$")
        local port = port_name and machine.ioport.ports[":" .. port_name]
        local field = port and port.fields[field_name]
        assert(field, "unknown JV1080_FACTORY_SELECT " .. item)
        press(field, 0.2)
        emu.wait(step_wait)
        screen:snapshot(string.format("%s-selected-%02d.png", output, step))
        dump_ui_state(string.format("selected-%02d-%s", step, item))
        dump_adc_state(string.format("selected-%02d", step))
    end
end

local trace_file = assert(io.open(output .. "-ga.csv", "w"))
trace_file:write("time,kind,pc,address,data,mask\n")
for _, line in ipairs(trace) do trace_file:write(line, "\n") end
trace_file:close()
if xp_trace_enabled then
    local xp_trace_file = assert(io.open(output .. "-xp.csv", "w"))
    xp_trace_file:write("time,kind,pc,address,data,mask\n")
    for _, line in ipairs(xp_trace) do xp_trace_file:write(line, "\n") end
    xp_trace_file:close()
end
machine:exit()
