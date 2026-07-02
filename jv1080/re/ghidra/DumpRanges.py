# Dumps disassembly + raw bytes over explicit address ranges (function-agnostic).
# usage: DumpRanges.py output start:len [start:len ...]   (all hex)
#@category JV-1080
from java.io import File, PrintWriter

args = getScriptArgs()
space = currentProgram.getAddressFactory().getDefaultAddressSpace()
listing = currentProgram.getListing()
mem = currentProgram.getMemory()


def a(v):
    return space.getAddress(v)


out = PrintWriter(File(args[0]), "UTF-8")
for spec in args[1:]:
    s, l = spec.split(":")
    start = int(s, 16)
    length = int(l, 16)
    out.println("===== RANGE %08x +%x =====" % (start, length))
    try:
        disassemble(a(start))
    except:
        pass
    cur = a(start)
    while cur.getOffset() < start + length:
        ins = listing.getInstructionAt(cur)
        if ins is not None:
            b = " ".join("%02x" % (x & 0xff) for x in ins.getBytes())
            ref = ""
            try:
                refs = ins.getReferencesFrom()
                if refs:
                    ref = "  ; -> " + ",".join("%08x" % r.getToAddress().getOffset() for r in refs)
            except:
                pass
            out.println("%s  %-14s  %s%s" % (cur, b, ins.toString(), ref))
            cur = cur.add(ins.getLength())
        else:
            try:
                hi = mem.getByte(cur) & 0xff
                lo = mem.getByte(cur.add(1)) & 0xff
                out.println("%s  %02x %02x          .word 0x%02x%02x" % (cur, hi, lo, hi, lo))
            except:
                out.println("%s  ??" % cur)
            cur = cur.add(2)
    out.println("")
out.close()
println("DumpRanges done -> %s" % args[0])
