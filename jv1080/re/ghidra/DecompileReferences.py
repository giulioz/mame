# Finds references to supplied data/code addresses and decompiles the referring
# functions plus a bounded call neighborhood.
#@category JV-1080

from java.io import File, PrintWriter
from ghidra.app.decompiler import DecompInterface


args = getScriptArgs()
if len(args) < 3:
    raise RuntimeError("usage: DecompileReferences.py output depth hex-address...")

output = File(args[0])
depth = int(args[1])
space = currentProgram.getAddressFactory().getDefaultAddressSpace()
fm = currentProgram.getFunctionManager()
references = currentProgram.getReferenceManager()

requested = []
roots = set()
for raw in args[2:]:
    target = space.getAddress(int(raw, 16))
    refs = list(references.getReferencesTo(target))
    requested.append((target, refs))
    for ref in refs:
        fn = fm.getFunctionContaining(ref.getFromAddress())
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
out.println("REQUESTED REFERENCES")
for target, refs in requested:
    out.println("\nTARGET %s" % target)
    for ref in refs:
        fn = fm.getFunctionContaining(ref.getFromAddress())
        out.println("  %s %s %s" % (
            ref.getFromAddress(), ref.getReferenceType(),
            fn.getName(True) if fn else "<data/no function>"))

for fn in sorted(functions, key=lambda item: item.getEntryPoint().getOffset()):
    callers = sorted([str(x.getEntryPoint()) for x in fn.getCallingFunctions(monitor)])
    callees = sorted([str(x.getEntryPoint()) for x in fn.getCalledFunctions(monitor)])
    out.println("\n================================================================================")
    out.println("FUNCTION %s %s size=%d" % (
        fn.getEntryPoint(), fn.getName(True), fn.getBody().getNumAddresses()))
    out.println("CALLERS " + ",".join(callers))
    out.println("CALLEES " + ",".join(callees))
    result = decompiler.decompileFunction(fn, 60, monitor)
    if result.decompileCompleted():
        out.println(result.getDecompiledFunction().getC())
    else:
        out.println("<decompile failed: %s>" % result.getErrorMessage())

out.close()
decompiler.dispose()
println("Decompiled %d referring functions to %s" % (
    len(functions), output.getAbsolutePath()))
