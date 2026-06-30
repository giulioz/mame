# Decompiles the functions containing supplied addresses plus a bounded call
# neighborhood.  This is used to turn runtime XP access PCs into call paths.
#@category JV-1080

from java.io import File, PrintWriter
from ghidra.app.decompiler import DecompInterface


args = getScriptArgs()
if len(args) < 3:
    raise RuntimeError("usage: DecompileJV1080.py output depth hex-address...")

output = File(args[0])
depth = int(args[1])
space = currentProgram.getAddressFactory().getDefaultAddressSpace()
fm = currentProgram.getFunctionManager()

roots = set()
requested = []
for raw in args[2:]:
    address = space.getAddress(int(raw, 16))
    fn = fm.getFunctionContaining(address)
    requested.append((address, fn))
    if fn is not None:
        roots.add(fn)

functions = set(roots)
frontier = set(roots)
for _ in range(depth):
    following = set()
    for fn in frontier:
        following.update(fn.getCalledFunctions(monitor))
        following.update(fn.getCallingFunctions(monitor))
    following.difference_update(functions)
    functions.update(following)
    frontier = following

decompiler = DecompInterface()
decompiler.openProgram(currentProgram)
out = PrintWriter(output, "UTF-8")
out.println("REQUESTED ADDRESSES")
for address, fn in requested:
    out.println("%s\t%s" % (address, fn.getName(True) if fn else "<no function>"))

for fn in sorted(functions, key=lambda item: item.getEntryPoint().getOffset()):
    callers = sorted([str(x.getEntryPoint()) for x in fn.getCallingFunctions(monitor)])
    callees = sorted([str(x.getEntryPoint()) for x in fn.getCalledFunctions(monitor)])
    out.println("\n================================================================================")
    out.println("FUNCTION %s %s size=%d" % (fn.getEntryPoint(), fn.getName(True), fn.getBody().getNumAddresses()))
    out.println("CALLERS " + ",".join(callers))
    out.println("CALLEES " + ",".join(callees))
    result = decompiler.decompileFunction(fn, 60, monitor)
    if result.decompileCompleted():
        out.println(result.getDecompiledFunction().getC())
    else:
        out.println("<decompile failed: %s>" % result.getErrorMessage())

out.close()
decompiler.dispose()
println("Decompiled %d functions to %s" % (len(functions), output.getAbsolutePath()))
