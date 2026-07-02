#!/usr/bin/env python3
# Minimal SH-1 (SH7034) disassembler with PC-relative literal-pool resolution and
# light register-constant tracking, for JV-1080 firmware RE.
# Usage: python3 sh1dis.py <vaddr-hex> [num-instrs]   e.g.  python3 sh1dis.py 0x0a008f44 240
# Resolves mov.w/mov.l @(disp,PC) literals to absolute values so MMIO addresses
# (e.g. XP 0x0c00xxxx) and immediates (#7) are visible; annotates stores to XP regs.
# Not a complete decoder: unhandled opcodes print as ".word xxxx".
import sys, struct
ROM=open("/Users/giuliozausa/personal/programming/mame/jv1080/roland_r00678167.ic20","rb").read()
def r16(fo): return struct.unpack(">H",ROM[fo:fo+2])[0]
def r32(fo): return struct.unpack(">I",ROM[fo:fo+4])[0]
def s8(x): return x-256 if x>=128 else x
def disas(vaddr, n=160):
    base=vaddr & 0xfffff           # file offset
    reg=[None]*16
    out=[]
    i=0; stop=0
    while i<n:
        fo=base+i*2; pc=vaddr+i*2; w=r16(fo)
        t=f"{pc:08x}: {w:04x}  "; ann=""
        nn=(w>>8)&0xf; mm=(w>>4)&0xf; d8=w&0xff; d4=w&0xf
        if (w&0xf000)==0xe000:
            v=s8(d8)&0xffffffff; reg[nn]=v; t+=f"mov #{s8(d8):#x}, r{nn}"
        elif (w&0xf000)==0x9000:  # mov.w @(d,PC),Rn
            a=pc+4+(d8<<1); val=r16(a&0xfffff); reg[nn]=val if val<0x8000 else val|0xffff0000; t+=f"mov.w @(pc,{d8}),r{nn}"; ann=f"; r{nn}=[{a:08x}]={val:#06x}"
        elif (w&0xf000)==0xd000:  # mov.l @(d,PC),Rn
            a=(pc&~3)+4+(d8<<2); val=r32(a&0xfffff); reg[nn]=val; t+=f"mov.l @(pc,{d8}),r{nn}"; ann=f"; r{nn}=[{a:08x}]={val:#010x}"
        elif (w&0xf00f)==0x6003: reg[nn]=reg[mm]; t+=f"mov r{mm}, r{nn}"
        elif (w&0xf00f)==0x2000: t+=f"mov.b r{mm}, @r{nn}"; ann=tgt(reg,nn,mm,'b')
        elif (w&0xf00f)==0x2001: t+=f"mov.w r{mm}, @r{nn}"; ann=tgt(reg,nn,mm,'w')
        elif (w&0xf00f)==0x2002: t+=f"mov.l r{mm}, @r{nn}"; ann=tgt(reg,nn,mm,'l')
        elif (w&0xf00f)==0x6000: t+=f"mov.b @r{mm}, r{nn}"; reg[nn]=None
        elif (w&0xf00f)==0x6001: t+=f"mov.w @r{mm}, r{nn}"; reg[nn]=None
        elif (w&0xf00f)==0x6002: t+=f"mov.l @r{mm}, r{nn}"; reg[nn]=None
        elif (w&0xf000)==0x1000: t+=f"mov.l r{mm}, @({d4*4},r{nn})"; ann=tgt(reg,nn,mm,'l',d4*4)
        elif (w&0xf000)==0x5000: t+=f"mov.l @({d4*4},r{mm}), r{nn}"; reg[nn]=None
        elif (w&0xff00)==0x8100: t+=f"mov.w r0, @({d4*2},r{nn})"; ann=tgt(reg,nn,0,'w',d4*2)
        elif (w&0xff00)==0x8000: t+=f"mov.b r0, @({d4},r{nn})"; ann=tgt(reg,nn,0,'b',d4)
        elif (w&0xf000)==0x7000:
            if reg[nn] is not None: reg[nn]=(reg[nn]+s8(d8))&0xffffffff
            t+=f"add #{s8(d8):#x}, r{nn}"
        elif (w&0xf00f)==0x300c: t+=f"add r{mm}, r{nn}"; reg[nn]=None
        elif (w&0xf00f)==0x3008: t+=f"sub r{mm}, r{nn}"; reg[nn]=None
        elif (w&0xf0ff)==0x400b: t+=f"jsr @r{nn}"; ann=f"; ->{reg[nn]:#010x}" if reg[nn] else ""
        elif (w&0xf0ff)==0x402b: t+=f"jmp @r{nn}"
        elif (w&0xff00)==0xb000: tgt2=pc+4+(s12(w)<<1); t+=f"bsr {tgt2:08x}"
        elif (w&0xff00)==0xa000: tgt2=pc+4+(s12(w)<<1); t+=f"bra {tgt2:08x}"
        elif (w&0xff00)==0x8900: t+=f"bt {pc+4+(s8(d8)<<1):08x}"
        elif (w&0xff00)==0x8b00: t+=f"bf {pc+4+(s8(d8)<<1):08x}"
        elif (w&0xff00)==0x8d00: t+=f"bt/s {pc+4+(s8(d8)<<1):08x}"
        elif (w&0xff00)==0x8f00: t+=f"bf/s {pc+4+(s8(d8)<<1):08x}"
        elif w==0x000b: t+="rts"; stop=2
        elif w==0x0009: t+="nop"
        elif (w&0xf0ff)==0x4010: t+=f"dt r{nn}"
        elif (w&0xff00)==0xc800: t+=f"tst #{d8:#x}, r0"
        elif (w&0xff00)==0x8800: t+=f"cmp/eq #{s8(d8):#x}, r0"
        elif (w&0xf0ff)==0x4000: t+=f"shll r{nn}"
        elif (w&0xf0ff)==0x4001: t+=f"shlr r{nn}"
        elif (w&0xf0ff)==0x4008: t+=f"shll2 r{nn}"; 
        elif (w&0xf0ff)==0x4018: t+=f"shll8 r{nn}"
        elif (w&0xf0ff)==0x4028: t+=f"shll16 r{nn}"
        else: t+=f".word {w:04x}"
        out.append(t+("   "+ann if ann else ""))
        if stop:
            stop-=1
            if stop==0: break
        i+=1
    return "\n".join(out)
def s12(w):
    v=w&0xfff
    return v-0x1000 if v>=0x800 else v
def tgt(reg,nn,mm,sz,disp=0):
    a=reg[nn]
    if a is None: return ""
    a=(a+disp)&0xffffffff
    v=reg[mm] if mm is not None and reg[mm] is not None else None
    xp = (a & 0x0fffffff)
    note=""
    if (a>>24) in (0x04,0x0c) and 0x3900<=(a&0xffff)<=0x39ff: note="  <<<<< XP CONTROL REG"
    if (a>>24) in (0x04,0x0c) and 0x3400<=(a&0xffff)<=0x38ff: note="  (PRAM/IRAM)"
    if (a>>24) in (0x04,0x0c) and 0x2c00<=(a&0xffff)<=0x2dff: note="  (CRAM)"
    return f"; STORE r{mm}{'' if v is None else f'(={v:#x})'} -> [{a:08x}]{note}"
import sys
va=int(sys.argv[1],16) if len(sys.argv)>1 else 0x0a008f44
n=int(sys.argv[2]) if len(sys.argv)>2 else 240
print(disas(va,n))
