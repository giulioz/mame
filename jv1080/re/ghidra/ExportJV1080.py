# Exports stable, diffable inventories from the analyzed Ghidra program.
#@category JV-1080

from java.io import File, PrintWriter


args = getScriptArgs()
if len(args) != 1:
    raise RuntimeError("usage: ExportJV1080.py /output/directory")

outdir = File(args[0])
outdir.mkdirs()


def writer(name):
    return PrintWriter(File(outdir, name), "UTF-8")


def clean(value):
    return str(value).replace("\\", "\\\\").replace("\t", "\\t").replace("\r", "\\r").replace("\n", "\\n")


fm = currentProgram.getFunctionManager()
functions = list(fm.getFunctions(True))

f = writer("functions.tsv")
f.println("entry\tname\tsize\tcallers\tcallees")
for fn in functions:
    callers = sorted([str(x.getEntryPoint()) for x in fn.getCallingFunctions(monitor)])
    callees = sorted([str(x.getEntryPoint()) for x in fn.getCalledFunctions(monitor)])
    f.println("%s\t%s\t%d\t%s\t%s" % (
        fn.getEntryPoint(), clean(fn.getName()), fn.getBody().getNumAddresses(),
        ",".join(callers), ",".join(callees)))
f.close()

f = writer("symbols.tsv")
f.println("address\ttype\tname\tprimary\tsource")
for sym in currentProgram.getSymbolTable().getAllSymbols(True):
    f.println("%s\t%s\t%s\t%s\t%s" % (
        sym.getAddress(), sym.getSymbolType(), clean(sym.getName()),
        sym.isPrimary(), sym.getSource()))
f.close()

f = writer("strings.tsv")
f.println("address\tlength\tvalue")
for data in currentProgram.getListing().getDefinedData(True):
    if data.hasStringValue():
        f.println("%s\t%d\t%s" % (data.getAddress(), data.getLength(), clean(data.getValue())))
f.close()

f = writer("memory.tsv")
f.println("start\tend\tname\tread\twrite\texecute\tinitialized")
for block in currentProgram.getMemory().getBlocks():
    f.println("%s\t%s\t%s\t%s\t%s\t%s\t%s" % (
        block.getStart(), block.getEnd(), clean(block.getName()), block.isRead(),
        block.isWrite(), block.isExecute(), block.isInitialized()))
f.close()

println("Exported %d functions to %s" % (len(functions), outdir.getAbsolutePath()))
