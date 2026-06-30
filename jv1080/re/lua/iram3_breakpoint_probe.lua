-- Exercises the XP IRAM3 breakpoint interpolator through the SH host bus.
-- Slot 63 is unused by the JV-1080 boot programs observed so far.

local machine = manager.machine
local cpu = machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local slot = tonumber(os.getenv("JV1080_IRAM3_SLOT") or "63")
local rate = tonumber(os.getenv("JV1080_IRAM3_RATE") or "4096")
local current_address = 0x04003200 + slot * 4
local target_address = 0x04003300 + slot * 2
local rate_address = 0x04003928 + (slot >> 4) * 2
local samples = {}

local function read_current(label)
    -- The DSP-memory access is a trigger; the value is returned through the
    -- high and low readback words rather than by the trigger access itself.
    space:read_u32(current_address)
    local high = space:read_u16(0x04003912)
    local low = space:read_u16(0x04003910)
    local value = (high << 16) | low
    table.insert(samples, { label = label, value = value })
    print(string.format("IRAM3_BP,%s,%08x", label, value))
    return value
end

emu.wait(12)
space:write_u16(rate_address, rate)
space:write_u32(current_address, 0)
space:write_u16(target_address, 0x01ff)

read_current("up_0")
emu.wait(0.00025)
read_current("up_025ms")
emu.wait(0.00075)
read_current("up_1ms")
emu.wait(0.003)
read_current("up_4ms")
emu.wait(0.016)
read_current("up_20ms")

space:write_u16(target_address, 0)
read_current("down_0")
emu.wait(0.001)
read_current("down_1ms")
emu.wait(0.019)
read_current("down_20ms")

local target = 0x01ff << 13
assert(samples[1].value == 0, "IRAM3 ramp advanced before audio time elapsed")
for index = 2, 5 do
    assert(samples[index].value > samples[index - 1].value, "IRAM3 upward ramp is not monotonic")
    assert(samples[index].value <= target, "IRAM3 upward ramp overshot its target")
end
assert(samples[7].value < samples[6].value, "IRAM3 downward ramp is not monotonic")
assert(samples[8].value < samples[7].value, "IRAM3 downward ramp stopped early")

print("IRAM3_BP_PASS")
machine:exit()
