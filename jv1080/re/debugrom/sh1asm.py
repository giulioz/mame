#!/usr/bin/env python3
"""Minimal SH-1 (SuperH, big-endian) assembler for JV-1080 debug-ROM patches.

No SH cross-toolchain is available on this machine, so this hand-rolls the small
instruction subset the debug patch needs.  Two passes: resolve labels, then emit.
PC-relative literal loads use the `ldlit Rn, =value` pseudo-op, which allocates a
32-bit constant into the nearest following `.pool` (literal pool) directive.

Validated against real bytes from the ic20/ic15 Ghidra disassembly (see selftest).

Syntax:
  label:                      define a label
  .org 0xADDR                 set the current assembly address (for PC math)
  .pool                       emit collected literal-pool constants here (aligned)
  .long 0xVALUE [, ...]       emit 32-bit big-endian words
  .word 0xVALUE [, ...]       emit 16-bit big-endian words
  .byte 0xVALUE [, ...]       emit bytes
  .align                      align to 4 bytes (pad with 0x00)
  ; or # ...                  comment
  <mnemonic> operands

Registers: r0..r15 (r15=sp). pc/pr where relevant.
"""
import sys, re

class AsmError(Exception):
    pass

def reg(tok):
    tok = tok.strip().lower()
    m = re.fullmatch(r'r(\d+)', tok)
    if not m:
        raise AsmError("bad register %r" % tok)
    n = int(m.group(1))
    if not 0 <= n <= 15:
        raise AsmError("register out of range %r" % tok)
    return n

def imm(tok):
    tok = tok.strip()
    return int(tok, 0)

class Assembler:
    def __init__(self, base=0):
        self.base = base
        self.addr = base
        self.out = bytearray()
        self.labels = {}
        self.fixups = []          # (kind, addr_in_out, pcaddr, target_label_or_value, extra)
        self.pool = []            # list of (value, [fixup positions]) pending for next .pool
        self.pool_syms = {}       # value -> assigned pool label name (per-pool)
        self._pool_id = 0

    # ---- emit helpers ----
    def emit16(self, w):
        self.out.append((w >> 8) & 0xff)
        self.out.append(w & 0xff)
        self.addr += 2

    def emit32(self, w):
        self.out.append((w >> 24) & 0xff)
        self.out.append((w >> 16) & 0xff)
        self.out.append((w >> 8) & 0xff)
        self.out.append(w & 0xff)
        self.addr += 4

    # ---- two pass ----
    def assemble(self, text):
        lines = text.splitlines()
        # Pass 1: measure + labels.  We assemble for real but with placeholder
        # displacements, recording fixups; pass 2 patches displacements.
        for raw in lines:
            self.line(raw)
        # flush any pending pool implicitly at end
        if self.pool:
            self._emit_pool()
        # resolve fixups
        for fx in self.fixups:
            self._resolve(fx)
        return bytes(self.out)

    def line(self, raw):
        s = raw.split(';', 1)[0]
        s = s.split('#', 1)[0] if not s.strip().startswith('#') else ''
        s = raw.split(';', 1)[0]
        # allow '#' only as immediate marker, so strip comments only on ';'
        s = s.rstrip()
        if not s.strip():
            return
        # label?
        m = re.match(r'^\s*([A-Za-z_.][\w.]*)\s*:\s*(.*)$', s)
        if m and not m.group(1).startswith('.'):
            name = m.group(1)
            if name in self.labels:
                raise AsmError("dup label %s" % name)
            self.labels[name] = self.addr
            rest = m.group(2).strip()
            if rest:
                self.instr(rest)
            return
        self.instr(s.strip())

    def instr(self, s):
        parts = s.split(None, 1)
        op = parts[0].lower()
        args = parts[1] if len(parts) > 1 else ''
        argl = self._split_ops(args) if args.strip() else []
        handler = getattr(self, 'op_' + op.replace('.', '_').replace('/', '_'), None)
        if op.startswith('.'):
            handler = getattr(self, 'dir_' + op[1:], None)
        if handler is None:
            raise AsmError("unknown op %r" % op)
        handler(argl)

    @staticmethod
    def _split_ops(args):
        out, cur, depth = [], '', 0
        for ch in args:
            if ch == '(':
                depth += 1; cur += ch
            elif ch == ')':
                depth -= 1; cur += ch
            elif ch == ',' and depth == 0:
                out.append(cur.strip()); cur = ''
            else:
                cur += ch
        if cur.strip():
            out.append(cur.strip())
        return out

    # ---- directives ----
    def dir_org(self, a):
        self.addr = imm(a[0])

    def dir_long(self, a):
        for x in a:
            self.emit32(imm(x) & 0xffffffff)

    def dir_word(self, a):
        for x in a:
            self.emit16(imm(x) & 0xffff)

    def dir_byte(self, a):
        for x in a:
            self.out.append(imm(x) & 0xff)
            self.addr += 1

    def dir_align(self, a):
        while self.addr & 3:
            self.out.append(0)
            self.addr += 1

    def dir_pool(self, a):
        self._emit_pool()

    def _emit_pool(self):
        while self.addr & 3:
            self.out.append(0)
            self.addr += 1
        for value, positions in self.pool:
            here = self.addr
            self.emit32(value & 0xffffffff)
            for (out_pos, pcaddr) in positions:
                disp = (here - ((pcaddr & ~3) + 4)) // 4
                if disp < 0 or disp > 0xff:
                    raise AsmError("pool out of range disp=%d (pc=%x pool=%x)" % (disp, pcaddr, here))
                # patch the low byte of the mov.l @(disp,pc),Rn already emitted
                self.out[out_pos + 1] = disp & 0xff
        self.pool = []
        self.pool_syms = {}

    # ---- fixup resolution for branches ----
    def _resolve(self, fx):
        kind, out_pos, pcaddr, target = fx
        if isinstance(target, int):
            tgt = target
        else:
            if target not in self.labels:
                raise AsmError("undefined label %s" % target)
            tgt = self.labels[target]
        if kind == 'disp12':   # bra/bsr: PC+4+disp*2, 12-bit signed
            disp = (tgt - (pcaddr + 4)) // 2
            if not -2048 <= disp <= 2047:
                raise AsmError("bra/bsr out of range %d" % disp)
            w = (self.out[out_pos] << 8) | self.out[out_pos + 1]
            w = (w & 0xf000) | (disp & 0x0fff)
            self.out[out_pos] = (w >> 8) & 0xff
            self.out[out_pos + 1] = w & 0xff
        elif kind == 'disp8':  # bt/bf: PC+4+disp*2, 8-bit signed
            disp = (tgt - (pcaddr + 4)) // 2
            if not -128 <= disp <= 127:
                raise AsmError("bt/bf out of range %d" % disp)
            self.out[out_pos + 1] = disp & 0xff

    def _branch(self, base_op, target, kind):
        out_pos = len(self.out)
        pcaddr = self.addr
        self.emit16(base_op)
        tgt = None
        try:
            tgt = imm(target)
        except (ValueError, AsmError):
            tgt = target
        self.fixups.append((kind, out_pos, pcaddr, tgt))

    # ---- instructions ----
    def op_nop(self, a): self.emit16(0x0009)
    def op_rts(self, a): self.emit16(0x000b)
    def op_clrt(self, a): self.emit16(0x0008)
    def op_sett(self, a): self.emit16(0x0018)

    def op_mov(self, a):
        # mov #imm,Rn  OR  mov Rm,Rn
        dst = reg(a[1])
        if a[0].startswith('#'):
            v = imm(a[0][1:])
            if not -128 <= v <= 127 and not 0 <= v <= 255:
                raise AsmError("mov #imm out of range %d (use ldlit)" % v)
            self.emit16(0xe000 | (dst << 8) | (v & 0xff))
        else:
            src = reg(a[0])
            self.emit16(0x6003 | (dst << 8) | (src << 4))

    def _memref(self, tok):
        # returns ('reg', n) | ('postinc', n) | ('predec', n) | ('dispreg', disp, n) | ('pc', )
        tok = tok.strip()
        m = re.fullmatch(r'@r(\d+)', tok)
        if m: return ('reg', int(m.group(1)))
        m = re.fullmatch(r'@r(\d+)\+', tok)
        if m: return ('postinc', int(m.group(1)))
        m = re.fullmatch(r'@-r(\d+)', tok)
        if m: return ('predec', int(m.group(1)))
        m = re.fullmatch(r'@\(\s*([^,]+)\s*,\s*r(\d+)\s*\)', tok)
        if m: return ('dispreg', imm(m.group(1)), int(m.group(2)))
        raise AsmError("bad memref %r" % tok)

    def _movsz(self, a, sz):
        # sz: 0=byte 1=word 2=long.  SH operand order is src,dst.
        left, right = a[0].strip(), a[1].strip()
        if right.startswith('@') and not left.startswith('@'):
            # store: reg -> mem   (mov.x Rm,@dst)
            ref = self._memref(right)
            rm = reg(left)
            if ref[0] == 'reg':
                self.emit16(0x2000 | (ref[1] << 8) | (rm << 4) | sz)
            elif ref[0] == 'predec':
                self.emit16(0x2004 | (ref[1] << 8) | (rm << 4) | sz)
            elif ref[0] == 'dispreg':
                disp, rn = ref[1], ref[2]
                if sz == 2:
                    self.emit16(0x1000 | (rn << 8) | (rm << 4) | (disp // 4 & 0xf))
                else:
                    if rm != 0:
                        raise AsmError("mov.b/w Rm,@(disp,Rn) requires Rm=r0")
                    base = 0x8000 if sz == 0 else 0x8100
                    self.emit16(base | (rn << 4) | (disp // (1 if sz == 0 else 2) & 0xf))
            else:
                raise AsmError("unsupported store ref")
        elif left.startswith('@') and not right.startswith('@'):
            # load: mem -> reg   (mov.x @src,Rn)
            ref = self._memref(left)
            rn = reg(right)
            if ref[0] == 'reg':
                self.emit16(0x6000 | (rn << 8) | (ref[1] << 4) | sz)
            elif ref[0] == 'postinc':
                self.emit16(0x6004 | (rn << 8) | (ref[1] << 4) | sz)
            elif ref[0] == 'dispreg':
                disp, rm = ref[1], ref[2]
                if sz == 2:
                    self.emit16(0x5000 | (rn << 8) | (rm << 4) | (disp // 4 & 0xf))
                else:
                    if rn != 0:
                        raise AsmError("mov.b/w @(disp,Rm),Rn requires Rn=r0")
                    base = 0x8400 if sz == 0 else 0x8500
                    self.emit16(base | (rm << 4) | (disp // (1 if sz == 0 else 2) & 0xf))
            else:
                raise AsmError("unsupported load ref")
        else:
            raise AsmError("bad mov.%s operands" % 'bwl'[sz])

    def op_mov_b(self, a): self._movsz(a, 0)
    def op_mov_w(self, a): self._movsz(a, 1)
    def op_mov_l(self, a): self._movsz(a, 2)

    def op_ldlit(self, a):
        # ldlit Rn, =0xVALUE  -> mov.l @(disp,pc),Rn ; constant goes to next .pool
        rn = reg(a[0])
        t = a[1].strip()
        if not t.startswith('='):
            raise AsmError("ldlit needs =value")
        value = imm(t[1:]) & 0xffffffff
        out_pos = len(self.out)
        pcaddr = self.addr
        self.emit16(0xd000 | (rn << 8))   # disp filled by _emit_pool
        # register this position under the value
        for entry in self.pool:
            if entry[0] == value:
                entry[1].append((out_pos, pcaddr))
                break
        else:
            self.pool.append((value, [(out_pos, pcaddr)]))

    def op_add(self, a):
        if a[0].startswith('#'):
            self.emit16(0x7000 | (reg(a[1]) << 8) | (imm(a[0][1:]) & 0xff))
        else:
            self.emit16(0x300c | (reg(a[1]) << 8) | (reg(a[0]) << 4))
    def op_sub(self, a): self.emit16(0x3008 | (reg(a[1]) << 8) | (reg(a[0]) << 4))

    def op_cmp_eq(self, a):
        if a[0].startswith('#'):
            self.emit16(0x8800 | (imm(a[0][1:]) & 0xff))
        else:
            self.emit16(0x3000 | (reg(a[1]) << 8) | (reg(a[0]) << 4))
    def op_cmp_hs(self, a): self.emit16(0x3002 | (reg(a[1]) << 8) | (reg(a[0]) << 4))
    def op_cmp_ge(self, a): self.emit16(0x3003 | (reg(a[1]) << 8) | (reg(a[0]) << 4))
    def op_cmp_hi(self, a): self.emit16(0x3006 | (reg(a[1]) << 8) | (reg(a[0]) << 4))
    def op_cmp_gt(self, a): self.emit16(0x3007 | (reg(a[1]) << 8) | (reg(a[0]) << 4))
    def op_cmp_pl(self, a): self.emit16(0x4015 | (reg(a[0]) << 8))
    def op_cmp_pz(self, a): self.emit16(0x4011 | (reg(a[0]) << 8))

    def op_tst(self, a):
        if a[0].startswith('#'):
            self.emit16(0xc800 | (imm(a[0][1:]) & 0xff))
        else:
            self.emit16(0x2008 | (reg(a[1]) << 8) | (reg(a[0]) << 4))
    def op_and(self, a):
        if a[0].startswith('#'):
            self.emit16(0xc900 | (imm(a[0][1:]) & 0xff))
        else:
            self.emit16(0x2009 | (reg(a[1]) << 8) | (reg(a[0]) << 4))
    def op_or(self, a):
        if a[0].startswith('#'):
            self.emit16(0xcb00 | (imm(a[0][1:]) & 0xff))
        else:
            self.emit16(0x200b | (reg(a[1]) << 8) | (reg(a[0]) << 4))
    def op_xor(self, a):
        if a[0].startswith('#'):
            self.emit16(0xca00 | (imm(a[0][1:]) & 0xff))
        else:
            self.emit16(0x200a | (reg(a[1]) << 8) | (reg(a[0]) << 4))

    def op_extu_b(self, a): self.emit16(0x600c | (reg(a[1]) << 8) | (reg(a[0]) << 4))
    def op_extu_w(self, a): self.emit16(0x600d | (reg(a[1]) << 8) | (reg(a[0]) << 4))
    def op_exts_b(self, a): self.emit16(0x600e | (reg(a[1]) << 8) | (reg(a[0]) << 4))
    def op_exts_w(self, a): self.emit16(0x600f | (reg(a[1]) << 8) | (reg(a[0]) << 4))

    def op_shll(self, a): self.emit16(0x4000 | (reg(a[0]) << 8))
    def op_shlr(self, a): self.emit16(0x4001 | (reg(a[0]) << 8))
    def op_shll2(self, a): self.emit16(0x4008 | (reg(a[0]) << 8))
    def op_shlr2(self, a): self.emit16(0x4009 | (reg(a[0]) << 8))
    def op_shll8(self, a): self.emit16(0x4018 | (reg(a[0]) << 8))
    def op_shlr8(self, a): self.emit16(0x4019 | (reg(a[0]) << 8))
    def op_shll16(self, a): self.emit16(0x4028 | (reg(a[0]) << 8))
    def op_shlr16(self, a): self.emit16(0x4029 | (reg(a[0]) << 8))
    def op_shar(self, a): self.emit16(0x4021 | (reg(a[0]) << 8))

    def op_jmp(self, a): self.emit16(0x402b | (self._atreg(a[0]) << 8))
    def op_jsr(self, a): self.emit16(0x400b | (self._atreg(a[0]) << 8))
    def _atreg(self, tok):
        m = re.fullmatch(r'@r(\d+)', tok.strip())
        if not m: raise AsmError("jmp/jsr needs @Rn")
        return int(m.group(1))

    def op_bra(self, a): self._branch(0xa000, a[0], 'disp12')
    def op_bsr(self, a): self._branch(0xb000, a[0], 'disp12')
    def op_bt(self, a): self._branch(0x8900, a[0], 'disp8')
    def op_bf(self, a): self._branch(0x8b00, a[0], 'disp8')
    def op_bt_s(self, a): self._branch(0x8d00, a[0], 'disp8')
    def op_bf_s(self, a): self._branch(0x8f00, a[0], 'disp8')

    def op_sts_l(self, a):
        if a[0].lower() == 'pr':
            self.emit16(0x4022 | (self._predec(a[1]) << 8))
        elif a[0].lower() == 'macl':
            self.emit16(0x4012 | (self._predec(a[1]) << 8))
        else: raise AsmError("sts.l ?")
    def op_lds_l(self, a):
        if a[1].lower() == 'pr':
            self.emit16(0x4026 | (self._postinc(a[0]) << 8))
        elif a[1].lower() == 'macl':
            self.emit16(0x4016 | (self._postinc(a[0]) << 8))
        else: raise AsmError("lds.l ?")
    def op_stc_l(self, a):
        if a[0].lower() == 'sr':
            self.emit16(0x4003 | (self._predec(a[1]) << 8))
        elif a[0].lower() == 'gbr':
            self.emit16(0x4013 | (self._predec(a[1]) << 8))
        else: raise AsmError("stc.l ?")
    def op_ldc_l(self, a):
        if a[1].lower() == 'sr':
            self.emit16(0x4007 | (self._postinc(a[0]) << 8))
        elif a[1].lower() == 'gbr':
            self.emit16(0x4017 | (self._postinc(a[0]) << 8))
        else: raise AsmError("ldc.l ?")
    def op_stc(self, a):
        if a[0].lower() == 'sr': self.emit16(0x0002 | (reg(a[1]) << 8))
        elif a[0].lower() == 'gbr': self.emit16(0x0012 | (reg(a[1]) << 8))
        else: raise AsmError("stc ?")
    def op_ldc(self, a):
        if a[1].lower() == 'sr': self.emit16(0x400e | (reg(a[0]) << 8))
        elif a[1].lower() == 'gbr': self.emit16(0x401e | (reg(a[0]) << 8))
        else: raise AsmError("ldc ?")
    def _predec(self, tok):
        m = re.fullmatch(r'@-r(\d+)', tok.strip()); return int(m.group(1))
    def _postinc(self, tok):
        m = re.fullmatch(r'@r(\d+)\+', tok.strip()); return int(m.group(1))


def assemble(text, base=0):
    return Assembler(base).assemble(text)


def selftest():
    # Validate against real bytes from the ic20/ic15 Ghidra disassembly.
    cases = [
        ("mov.l r14,@-r15", "2fe6"),
        ("mov.l r13,@-r15", "2fd6"),
        ("sts.l pr,@-r15", "4f22"),
        ("lds.l @r15+,pr", "4f26"),
        ("mov.l @r15+,r14", "6ef6"),
        ("rts", "000b"),
        ("nop", "0009"),
        ("mov #0x0,r4", "e400"),
        ("mov #0xf,r10", "ea0f"),
        ("mov #0x1,r13", "ed01"),
        ("mov r0,r14", "6e03"),
        ("mov r4,r9", "6943"),
        ("jsr @r3", "430b"),
        ("jsr @r11", "4b0b"),
        ("jmp @r0", "402b"),
        ("mov.b r2,@r3", "2320"),
        ("mov.b @r3,r0", "6030"),
        ("mov.w @r9,r3", "6391"),
        ("mov.w r3,@r7", "2731"),
        ("mov.l @r3,r2", "6232"),
        ("mov.b @r4+,r13", "6d44"),
        ("extu.b r0,r0", "600c"),
        ("extu.w r4,r4", "644d"),
        ("exts.b r14,r14", "6eee"),
        ("add #0x1,r14", "7e01"),
        ("add #-0x80,r12", "7c80"),
        ("add r3,r14", "3e3c"),
        ("add r4,r12", "3c4c"),
        ("cmp/eq #0x0,r0", "8800"),
        ("cmp/eq #0xe,r0", "880e"),
        ("cmp/eq r3,r2", "3230"),
        ("cmp/ge r11,r4", "34b3"),
        ("cmp/hi r11,r7", "37b6"),
        ("cmp/pz r12", "4c11"),
        ("cmp/pl r5", "4515"),
        ("tst r0,r0", "2008"),
        ("tst r4,r4", "2448"),
        ("tst #0x4,r0", "c804"),
        ("and r13,r0", "20d9"),
        ("and #0x1f,r0", "c91f"),
        ("or #0x70,r0", "cb70"),
        ("shll8 r0", "4018"),
        ("shll2 r14", "4e08"),
        ("shar r12", "4c21"),
        ("stc sr,r0", "0002"),
        ("ldc r0,sr", "400e"),
        ("stc.l gbr,@-r15", "4f13"),
        ("mov.b r0,@(0x1,r10)", "80a1"),
        ("mov.b @(0x1,r15),r0", "84f1"),
        ("mov.l r3,@(0x4,r15)", "1f31"),
        ("mov.l @(0x14,r14),r13", "5de5"),
        ("mov.w @(0x0,r12),r0", "85c0"),
    ]
    ok = True
    for src, exp in cases:
        try:
            got = assemble(src).hex()
        except Exception as e:
            print("FAIL %-28s -> EXC %s" % (src, e)); ok = False; continue
        if got != exp:
            print("FAIL %-28s -> got %s exp %s" % (src, got, exp)); ok = False
    # branch + ldlit round-trip
    blob = assemble("""
      .org 0x0a059d48
      start:
        ldlit r3, =0x0a000200
        jmp @r3
        nop
      loop:
        bra loop
        nop
        bt loop
      .pool
    """, base=0x0a059d48)
    # first insn must be mov.l @(disp,pc),r3 = 0xd3?? ; pool at end
    if blob[0] != 0xd3:
        print("FAIL ldlit opcode"); ok = False
    print("selftest:", "PASS" if ok else "FAILED", "(%d cases)" % len(cases))
    return ok


if __name__ == '__main__':
    if len(sys.argv) >= 2 and sys.argv[1] == 'selftest':
        sys.exit(0 if selftest() else 1)
    if len(sys.argv) >= 3:
        base = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0
        data = assemble(open(sys.argv[1]).read(), base)
        sys.stdout.buffer.write(data)
    else:
        print(__doc__)
