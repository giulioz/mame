# Exports exact instructions for the functions containing supplied addresses.
#@category JV-1080

from java.io import File, PrintWriter


args = getScriptArgs()
if len(args) < 2:
    raise RuntimeError("usage: ExportDisassembly.py output hex-address...")

space = currentProgram.getAddressFactory().getDefaultAddressSpace()
listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()
functions = set()
for raw in args[1:]:
    fn = fm.getFunctionContaining(space.getAddress(int(raw, 16)))
    if fn is not None:
        functions.add(fn)

out = PrintWriter(File(args[0]), "UTF-8")
for fn in sorted(functions, key=lambda item: item.getEntryPoint().getOffset()):
    out.println("FUNCTION %s %s" % (fn.getEntryPoint(), fn.getName(True)))
    instructions = listing.getInstructions(fn.getBody(), True)
    while instructions.hasNext():
        instruction = instructions.next()
        out.println("%s  %-28s  %s" % (
            instruction.getAddress(),
            " ".join("%02x" % (byte & 0xff) for byte in instruction.getBytes()),
            instruction.toString(),
        ))
    out.println()
out.close()
println("Exported %d disassemblies to %s" % (len(functions), args[0]))
