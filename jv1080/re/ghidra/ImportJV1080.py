# Imports the JV-1080's split SH7034 firmware into one address space and seeds
# the entry points that are known from the reset vectors and RTOS task table.
#@category JV-1080

from java.io import File, FileInputStream
from ghidra.program.model.data import PointerDataType
from ghidra.program.model.symbol import SourceType


def addr(value):
    return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value)


def add_initialized(name, start, path, length, read, write, execute):
    memory = currentProgram.getMemory()
    if memory.getBlock(addr(start)) is not None:
        return
    stream = FileInputStream(File(path))
    try:
        block = memory.createInitializedBlock(name, addr(start), stream, length, monitor, False)
        block.setRead(read)
        block.setWrite(write)
        block.setExecute(execute)
    finally:
        stream.close()


def add_uninitialized(name, start, length, read=True, write=True, execute=False):
    memory = currentProgram.getMemory()
    if memory.getBlock(addr(start)) is not None:
        return
    block = memory.createUninitializedBlock(name, addr(start), length, False)
    block.setRead(read)
    block.setWrite(write)
    block.setExecute(execute)


def label(address, name):
    try:
        createLabel(addr(address), name, True, SourceType.USER_DEFINED)
    except:
        pass


def entry(address, name):
    label(address, name)
    disassemble(addr(address))
    fn = getFunctionAt(addr(address))
    if fn is None:
        fn = createFunction(addr(address), name)
    if fn is not None:
        fn.setName(name, SourceType.USER_DEFINED)


args = getScriptArgs()
if len(args) != 1:
    raise RuntimeError("usage: ImportJV1080.py /path/to/roland_r00678167.ic20")

external_rom = args[0]
if File(external_rom).length() != 0x100000:
    raise RuntimeError("external program ROM must be exactly 1 MiB")

# The SH7034 exposes CS2 at both the normal and A27-set addresses.  Firmware
# code and literal pointers overwhelmingly use the latter (0x0a...), whereas
# the hardware map and MAME region use 0x02....  Keeping both views makes data
# references honest without rewriting the image.
add_initialized("program_rom_cs2",        0x02000000, external_rom, 0x100000, True, False, True)
add_initialized("program_rom_cs2_mirror", 0x0a000000, external_rom, 0x100000, True, False, True)

add_uninitialized("work_dram",          0x01000000, 0x20000)
add_uninitialized("work_dram_mirror",   0x09000000, 0x20000)
add_uninitialized("card_ram",           0x02280000, 0x80000)
add_uninitialized("nvram",              0x02380000, 0x10000)
add_uninitialized("xp_registers",       0x04000000, 0x4000)
add_uninitialized("gate_array",         0x04380000, 0x40)
add_uninitialized("sh7034_peripherals", 0x05fff000, 0x1000)
add_uninitialized("sh7034_internal_ram",0x07fff000, 0x1000)
add_uninitialized("rtos_ram",           0x0ffff000, 0x1000)

vector_names = {
    0: "reset_pc", 1: "reset_sp", 4: "illegal_instruction", 6: "illegal_slot",
    9: "address_error", 11: "nmi", 12: "user_break",
    64: "irq0", 65: "irq1", 66: "irq2", 67: "irq3",
    68: "irq4", 69: "irq5_ga", 70: "irq6", 71: "irq7",
    72: "dmac0", 76: "dmac1", 80: "itu0_imia", 81: "itu0_imib", 82: "itu0_ovi",
    84: "itu1_imia", 85: "itu1_imib", 86: "itu1_ovi",
    88: "itu2_imia", 89: "itu2_imib", 90: "itu2_ovi",
    92: "itu3_imia", 93: "itu3_imib", 94: "itu3_ovi",
    96: "itu4_imia", 97: "itu4_imib", 98: "itu4_ovi",
    100: "sci0_eri", 101: "sci0_rxi", 102: "sci0_txi", 103: "sci0_tei",
    104: "sci1_eri", 105: "sci1_rxi", 106: "sci1_txi", 107: "sci1_tei",
    108: "dmac0_dei", 109: "dmac1_dei", 110: "dmac2_dei", 111: "dmac3_dei",
}

memory = currentProgram.getMemory()
seen_handlers = set()
for index in range(112):
    slot = index * 4
    label(slot, "vector_%03d_%s" % (index, vector_names.get(index, "reserved")))
    try:
        createData(addr(slot), PointerDataType.dataType)
    except:
        pass
    handler = memory.getInt(addr(slot)) & 0xffffffff
    if index == 1 or handler in seen_handlers:
        continue
    if ((handler < 0x10000) or (0x0a000000 <= handler < 0x0a100000)) and (handler & 1) == 0:
        entry(handler, "isr_%s" % vector_names.get(index, "vector_%03d" % index))
        seen_handlers.add(handler)

entry(0x00000400, "rtos_initialize")
entry(0x00000554, "rtos_signal_event")
entry(0x000006f0, "rtos_tick_irq")
entry(0x00000748, "rtos_sleep_ticks")

# Names below are backed by both static register literals and runtime access
# PCs.  Event-number names remain intentionally neutral until their UI/source
# queues have been identified as RFX, chorus, or reverb beyond doubt.
entry(0x00001158, "xp_irq_dispatch")
entry(0x000014e6, "xp_update_voice_tva")
entry(0x00006d46, "xp_write_voice_topology")
entry(0x00006be8, "xp_update_voice_tvf")
entry(0x000073e4, "xp_mark_voice_active")
entry(0x00008508, "xp_write_dry_sends")
entry(0x00008564, "xp_write_effect_sends")
entry(0x00008a0e, "xp_update_voice_pitch")
entry(0x00009764, "xp_start_voice")
entry(0x00009a52, "xp_reset_voice")
entry(0x00009ac0, "xp_release_voice_reset")
entry(0x00009c00, "xp_read_busy_random")
entry(0x0000a268, "xp_update_all_voices")
entry(0x0000b44c, "wave_metadata_lookup")
entry(0x0000bb14, "xp_wave_rom_read_byte")
entry(0x0a008f44, "xp_dsp_initialize")
entry(0x0a009194, "xp_dsp_upload_program0")
entry(0x0a0097f8, "xp_rfx_worker")
entry(0x0a009b44, "xp_reverb_worker")
entry(0x0a009d60, "xp_chorus_worker")
entry(0x0a00b87a, "xp_initialize_registers")

task_names = [
    "midi_task", "panel_task", "internal_task_2", "display_task", "sequencer_task",
    "synth_task", "effects_task", "internal_task_7", "idle_task", "background_task",
]
task_table = 0x0a000010
for index, name in enumerate(task_names):
    descriptor = task_table + index * 12
    target = memory.getInt(addr(descriptor)) & 0xffffffff
    label(descriptor, "task_%d_descriptor" % index)
    if ((target < 0x10000) or (0x0a000000 <= target < 0x0a100000)) and (target & 1) == 0:
        entry(target, name)

label(0x04000000, "XP_REG_BASE")
label(0x0c000000, "XP_REG_BASE_MIRROR")
label(0x0c003900, "XP_VOICE_RESET_BITMAP")
label(0x0c003910, "XP_READBACK_LOW_WAVE")
label(0x0c003912, "XP_READBACK_HIGH_STATUS_RANDOM")
label(0x0c003916, "XP_DSP_ACCESS_CONTROL")
label(0x0c003918, "XP_IRQ_STATUS")
label(0x0c00391a, "XP_IRQ_DATA")
label(0x0c003920, "XP_WAVE_ADDRESS")
label(0x0c003922, "XP_WAVE_BANK")
label(0x0c003a00, "XP_MIX_SEND_0")
label(0x0c003a80, "XP_MIX_SEND_1")
label(0x0c003b00, "XP_MIX_SEND_2")
label(0x0c003b80, "XP_MIX_SEND_3")
label(0x0c003c00, "XP_WAVE_ROM_APERTURE_1K")
label(0x04380000, "GA_REG_BASE")
label(0x0438003c, "GA_IRQ_STATUS")
label(0x0a000000, "rtos_configuration")

println("JV-1080 memory map and %d vector slots imported" % 112)
