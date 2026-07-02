#!/usr/bin/env python3
"""
Manual XP DSP driver for hands-on ISA probing.

Express a DSP program as (PRAM, CRAM) lines, one per slot (they are address-aligned:
PRAM slot k = 0x3400+4k is 32-bit, CRAM slot k = 0x2c00+2k is 16-bit):

    from xp_manual import XP
    xp = XP()
    xp.reset()                          # zero the whole DSP area + restore exec config (0x3916=7)

    (xp.program()
        .line_s(st=1, word=0xc0, col=0x00, cram=0x000042)       # slot 0 : PRAM=0x00007000, CRAM=0x0042
        .line_s(st=0, word=0x00, col=0x00, wb=1, cram=0x000000)       # slot 1
        .nop()                          # slot 2 : PRAM=0, CRAM=0
        .line_s(st=0, word=0x00, col=0x00, hi=0x80, cram=0x000000)       # slot 3
        .show()                         # print the table before sending (optional)
        .upload())                      # push to the chip
    # (optional) .verify() reads it back

    xp.monitor(bank=1, start=0, count=16)   # continuously print IRAM1[0..15] until Ctrl-C

Ground rules (see jv1080/re/XP_FACTS.md):
  * No note-on / no Program Change — they make the firmware re-upload the program.
  * Observe IRAM1/IRAM2. IRAM3 (0x3200) is the ramp-engine "magic" bank — don't trust it.
  * The DSP free-runs PRAM; we leave 0x3916 at 7 (run) and never "kick" it.
  * DSP memory is read via the latch (read a DSP addr -> value latches into 0x3910/0x3912);
    jvdebug.read_dsp() handles that. Control regs 0x39xx are write-only (read as 0).
  * NEVER read 0x39xx with read_dsp() — its width-4 trigger spans IRQ reg 0x3918 and hangs
    the firmware. Use xp.peek_reg() (direct 16-bit PEEK) for those.
"""
import sys, time
from jvdebug import JV, poke, peek, ping, decode_value

XP_BASE = 0x0c000000

# Region bases (host addresses, offset into the 0x0c00xxxx aperture)
PRAM_BASE  = 0x3400      # 288 x 32-bit program words
CRAM_BASE  = 0x2c00      # 288 x 16-bit coefficients
IRAM1_BASE = 0x3000      # 64 x 32-bit  (use as observable)
IRAM2_BASE = 0x3100      # 64 x 32-bit  (use as observable)
IRAM3_BASE = 0x3200      # 64 x 32-bit  (MAGIC: ramp engine; avoid)
IRAM3_TGT  = 0x3300      # 64 x 16-bit  ramp targets
N_SLOTS    = 288

# Execution config the firmware leaves set after boot (from the emulator boot trace, XP_FACTS C8).
# reset() restores these so a corrupted/half-poked state is put back into the known-good config.
EXEC_CONFIG = {
    0x3900: 0xffff, 0x3902: 0xffff, 0x3904: 0xffff, 0x3906: 0xffff,   # reset bitmaps
    0x3908: 0x1c19, 0x390a: 0x1818, 0x390c: 0x1818, 0x390e: 0x0808,   # global config
    0x3914: 0x403f,                                                    # global config word
    0x3924: 0xd200, 0x3926: 0x16d2,                                    # routing
    0x3928: 0x0100, 0x392a: 0x0100, 0x392c: 0x0300, 0x392e: 0x0300,   # IRAM3 ramp rates
}
EXEC_RUN_REG = 0x3916    # 7 = run (free-run normal), 4 = factory single-pass, 0 = stop
EXEC_RUN_VAL = 0x7


# ---------------- CRAM coefficient codec (XP_FACTS C14, byte-for-byte from SCCore setCRAM) ----
# value = sign_extend(raw[13:0]) << [0,1,2,4][raw[15:14]] / 8192
# anchors: 0x5000=+1.0  0x2000=-1.0  0x1000=+0.5  0xE000=-16.0  0x1FF0=+0.9980  0x3FE0=-0.0039
def cram_decode(raw):
    m = (raw & 0x3fff) - (0x4000 if raw & 0x2000 else 0)      # sext14
    return (m << (0, 1, 2, 4)[(raw >> 14) & 3]) / 8192.0

def cram_encode(value):
    """Smallest-exponent (finest-resolution) encoding of a float into the C14 format."""
    for exp, sh in enumerate((0, 1, 2, 4)):
        m = round(value * 8192.0 / (1 << sh))
        if -0x2000 <= m <= 0x1fff:
            return (exp << 14) | (m & 0x3fff)
    raise ValueError(f"{value} out of CRAM range (max +/-16)")


class XP:
    # def __init__(self, port_match="MiniFuse", pace=0.013):
    def __init__(self, port_match="UM-ONE", pace=0.013):
        self.jv = JV(port_match=port_match)
        self.pace = pace                       # >=~12ms/POKE to avoid MIDI wire overflow
        if not self.jv.xfer(ping()):
            raise RuntimeError("no PING reply — check MIDI link / power")

    # ---------------- raw access ----------------
    def poke(self, off, width, val):
        self.jv.send(poke(XP_BASE + off, width, val))
        time.sleep(self.pace)

    def peek_reg(self, off, width=2):
        """Direct PEEK of a register. Use for 0x39xx control/status regs (write-only ones read 0).
        Do NOT use read_dsp() on 0x39xx (hangs the firmware)."""
        for _ in range(3):
            r = self.jv.xfer(peek(XP_BASE + off, width), timeout=2.0)
            if r:
                return decode_value(r)
        return None

    # ---------------- DSP memory reads (via latch) ----------------
    def read_pram(self, i):  return self.jv.read_dsp(PRAM_BASE + i * 4, 4)
    def read_cram(self, i):  return self.jv.read_dsp(CRAM_BASE + i * 2, 2)
    def read_iram(self, bank, i):
        base = {1: IRAM1_BASE, 2: IRAM2_BASE, 3: IRAM3_BASE}[bank]
        return self.jv.read_dsp(base + i * 4, 4)

    # ---------------- bulk setup ----------------
    def zero_dsp(self, iram3=True, verbose=True):
        """Zero PRAM, CRAM, IRAM1, IRAM2 (and IRAM3 current+targets). 0x3916 untouched."""
        if verbose: print("zero PRAM(288)...", flush=True)
        for i in range(N_SLOTS): self.poke(PRAM_BASE + i * 4, 4, 0)
        if verbose: print("zero CRAM(288)...", flush=True)
        for i in range(N_SLOTS): self.poke(CRAM_BASE + i * 2, 2, 0x0000)
        if verbose: print("zero IRAM1/2(128)...", flush=True)
        for i in range(64): self.poke(IRAM1_BASE + i * 4, 4, 0x0000)
        for i in range(64): self.poke(IRAM2_BASE + i * 4, 4, 0x0000)
        if iram3:
            if verbose: print("zero IRAM3 targets+current (magic; may not stick)...", flush=True)
            for i in range(64): self.poke(IRAM3_TGT + i * 2, 2, 0x0000)   # stop ramps first
            for i in range(64): self.poke(IRAM3_BASE + i * 4, 4, 0x0000)
        if verbose: print("zero done.", flush=True)

    def set_config(self, run=True, verbose=True):
        """Restore the boot exec config and (optionally) set the run register to 7."""
        if verbose: print("set exec config...", flush=True)
        for off, val in EXEC_CONFIG.items():
            self.poke(off, 2, val)
        if run:
            self.poke(EXEC_RUN_REG, 2, EXEC_RUN_VAL)   # 0x3916 = 7 (run / free-run)
        if verbose: print("config set (0x3916=%d)." % (EXEC_RUN_VAL if run else -1), flush=True)

    def reset(self):
        """Full clean slate: zero the DSP area, then restore exec config + run."""
        self.zero_dsp()
        self.set_config()

    def run(self):   self.poke(EXEC_RUN_REG, 2, 0x7)   # free-run
    def stop(self):  self.poke(EXEC_RUN_REG, 2, 0x0)   # stop
    def test_run(self): self.poke(EXEC_RUN_REG, 2, 0x4)  # factory single-pass value (provisional)

    # ---------------- program builder ----------------
    def program(self):
        return Program(self)

    # ---------------- monitor ----------------
    def monitor(self, bank=1, start=0, count=16, period=0.4, addrs=None):
        """Continuously print a range of IRAM values until Ctrl-C.
        bank/start/count select IRAM{bank}[start:start+count]; or pass explicit `addrs`
        = list of (label, host_offset, width)."""
        if addrs is None:
            base = {1: IRAM1_BASE, 2: IRAM2_BASE, 3: IRAM3_BASE}[bank]
            addrs = [(f"{base+ (start+i)*4:04x}", base + (start + i) * 4, 4) for i in range(count)]
        print(f"monitoring {len(addrs)} slots (Ctrl-C to stop). '*' = changed since last row.", flush=True)
        prev = None
        t0 = time.time()
        try:
            while True:
                row = [self.jv.read_dsp(off, w) for (_, off, w) in addrs]
                marks = "".join("*" if (prev and row[i] != prev[i]) else "." for i in range(len(row)))
                cells = " ".join(f"{(v & 0xffffff):06x}" if v is not None else "??????" for v in row)
                print(f"t={time.time()-t0:6.1f} [{marks}] {cells}", flush=True)
                prev = row
                time.sleep(period)
        except KeyboardInterrupt:
            print("\n(stopped)", flush=True)


class Program:
    """Build a program as consecutive (PRAM, CRAM) slots, then .upload()."""
    def __init__(self, xp):
        self.xp = xp
        self.pram = {}     # slot -> 32-bit word
        self.cram = {}     # slot -> 16-bit word
        self._slot = 0

    def line(self, pram=0, cram=0):
        """Append one instruction at the next 1bit + aligned CRAM 16-bit."""
        self.pram[self._slot] = pram & 0xffffffff
        self.cram[self._slot] = cram & 0xffff
        self._slot += 1
        return self

    def line_s(self, st=0, word=0, col=0, hi=0, wb=0, ext=0, cram=0, op=None, sm=None, sel=None):
        """Append one instruction from its DECODED fields (jv1080/re/xp_dsp_isa_decoded.md,
        XP_FACTS C16-C23 -- the st/word/column encoding, hardware-probed 2026-07-02).

        The 32-bit PRAM word:
            ext  bits[31:25]  ERAM/DRAM control bits [H]: bit26 (ext=2) set on data-carrying
                              ERAM pairs, bit27 (ext=4) on the EQ's L out send -- likely
                              start-DRAM-read/write style strobes; 0 in delay-free programs
            wb   bit [24]     part of the parallel/ERAM channel: addr[8] in an ERAM address
                              pair second word (use eram_pair()); "writeback" elsewhere [H]
            hi   bits[23:16]  parallel ERAM address byte: bit23 = pair marker; walks as a +3
                              counter on the reverb allpass chain; 0 in delay-free programs
            st   bits[15:14]  STORE CONTROL:  3 = store accumulator to mem[addr14] (hw-proven)
                              0/1/2 = no-store / ? / ?  (verbs open; the old op-nibble was
                              st<<2 | addr14[13:12]: op0->st0, op4/5/7->st1, opB->st2,
                              opC/D/E/F->st3)
            word bits[13:6]   memory WORD address (addr14[13:6]); IRAM entry = word for
                              addr-tops 0/1 (opC/D stores: entry 0-255 across IRAM1/2/3/tgt;
                              at word<64 a store writes BOTH banks k and k+64)
            col  bits[5:0]    COLUMN = ALU operand/mode select (and plausibly the TDM bus
                              slot).  Hardware truth table (st=0 datapath):
                                0x00-0x03 keep acc     0x04/0x05 zero acc   0x07/0x08 negate
                                0x0F acc+const         0x18 trunc(acc*coef)-acc
                                0x1F load const        0x2F -const*0x3FF(?) 0x3F const-acc
        addr14 = word<<6 | col.  CRAM decode is PER-COLUMN: multiply columns use the C14 float
        format (cram_encode/decode); const columns use sat24(sext15(raw[14:0])<<(raw15?13:0)).
        The accumulator is 24-bit SATURATING.  Known words: st1 word 0x4A/0x4B = EFX in L/R;
        st3 word 0x55/0x56 = EFX out L/R (0x54/0x57 chorus sends?).  ERAM store pages (old
        opE/F) move with CRAM -- open.
        FREE-RUN CAVEAT: the DSP loops every sample -- a lone '+=' column integrates per pass
        and saturates (~0.9 s @32 kHz); prefix a load column (0x1F) to reset acc each pass.
        Legacy op/sm/sel kwargs are still accepted and converted (op[1:0]<<12|sm<<9|sel).
        `cram` is the paired 16-bit CRAM[slot]. Reconstructs PRAM and delegates to line()."""
        if op is not None or sm is not None or sel is not None:
            a14 = (((op or 0) & 3) << 12) | (((sm or 0) & 7) << 9) | ((sel or 0) & 0x1ff)
            st, word, col = ((op or 0) >> 2) & 3, (a14 >> 6) & 0xff, a14 & 0x3f
        pram = (((ext & 0x7f) << 25) | ((wb & 1) << 24) | ((hi & 0xff) << 16)
                | ((st & 3) << 14) | ((word & 0xff) << 6) | (col & 0x3f))
        return self.line(pram, cram)

    # hardware-confirmed building blocks (XP_FACTS C20)
    def bus_in(self, ch="L", **kw):
        """Read the EFX input bus (st1; L = word 0x4A, R = word 0x4B, col 0)."""
        return self.line_s(st=1, word=0x4a if ch.upper() == "L" else 0x4b, col=0, **kw)

    def efx_out(self, ch="L", **kw):
        """Write the EFX output (st3 store; L = word 0x55 with ext=4 as in the ROM EQ, R = 0x56)."""
        if ch.upper() == "L":
            return self.line_s(st=3, word=0x55, col=0, ext=kw.pop("ext", 4), **kw)
        return self.line_s(st=3, word=0x56, col=0, **kw)

    def eram_pair(self, addr, l1=None, l2=None):
        """Append the two-word ERAM tap-address pair (XP_FACTS C19, hardware-confirmed):
            addr[15:9] -> word1[22:16]  (+ bit23 pair marker)
            addr[8:0]  -> word2[24:16]  (bit24 of word2 = addr[8] -- an ADDRESS bit, not wb)
        addr is in SAMPLES at 32 kHz: addr = base + ms*32 (Triple Tap Delay base = 0x6000,
        so the 200 ms center tap = 0x6000 + 200*32 = 0x7900).
        l1/l2 = optional dicts of line_s kwargs for the low-half instructions riding under
        the pair. Defaults produce a pure address carrier (low halves 0, like the JV left tap).
        Example (the center tap's exact shape, incl. its ctrl bit26 = ext 2 on word2):
            .eram_pair(0x6000 + 200*32,
                       l1=dict(st=2, word=0xd3, col=0x00),
                       l2=dict(st=1, word=0xc0, col=0x21, ext=2))
        """
        l1 = dict(l1 or {}); l2 = dict(l2 or {})
        l1['hi'] = 0x80 | ((addr >> 9) & 0x7f)   # bit23 marker + addr[15:9]
        l2['wb'] = (addr >> 8) & 1               # addr[8]
        l2['hi'] = addr & 0xff                   # addr[7:0]
        self.line_s(**l1)
        return self.line_s(**l2)

    def nop(self, n=1):
        """Advance n slots leaving PRAM=0, CRAM=0 (explicitly, so upload writes them)."""
        for _ in range(n):
            self.line(0, 0)
        return self

    def at(self, slot, pram=None, cram=None):
        """Set a specific slot (does not move the append cursor)."""
        if pram is not None: self.pram[slot] = pram & 0xffffffff
        if cram is not None: self.cram[slot] = cram & 0xffff
        return self

    # known-word annotations (hardware-confirmed, XP_FACTS C20 + user notes): (st, word) -> label
    _ANNOT = {
        (1, 0x4a): "bus-in L", (1, 0x4b): "bus-in R",
        (3, 0x55): "EFX out L", (3, 0x56): "EFX out R",
        (3, 0x54): "chorus send?", (3, 0x57): "chorus send?",
    }
    _COLNAMES = {0x04: "zero", 0x05: "zero", 0x07: "neg", 0x08: "neg", 0x0f: "acc+c",
                 0x18: "a*c-a", 0x1f: "ld c", 0x2f: "c*?", 0x3f: "c-acc"}

    def show(self):
        """Print the program with decoded st/word/col fields + CRAM coef (mini-disassembler)."""
        slots = sorted(set(self.pram) | set(self.cram))
        print("slot   PRAM     st word col  hi wb ext | CRAM  coef      | note")
        for s in slots:
            p, c = self.pram.get(s, 0), self.cram.get(s, 0)
            st, word, col = (p >> 14) & 3, (p >> 6) & 0xff, p & 0x3f
            hi, wb, ext = (p >> 16) & 0xff, (p >> 24) & 1, (p >> 25) & 0x7f
            notes = []
            if (st, word) in self._ANNOT: notes.append(self._ANNOT[(st, word)])
            if col in self._COLNAMES: notes.append(f"col:{self._COLNAMES[col]}")
            if hi & 0x80: notes.append("[eram-pair hi?]")
            print(f"{s:4d}  {p:08x}   {st}  {word:02x}  {col:02x}   {hi:02x}  {wb}  {ext:02x} | "
                  f"{c:04x} {cram_decode(c):+9.4f} | {' '.join(notes)}")
        return self

    def upload(self, verbose=True):
        """Write the defined PRAM/CRAM slots to the chip. Assumes a prior xp.reset()/zero_dsp()
        put every other slot to 0. Does NOT touch 0x3916 (DSP keeps free-running)."""
        slots = sorted(set(self.pram) | set(self.cram))
        for s in slots:
            self.xp.poke(PRAM_BASE + s * 4, 4, self.pram.get(s, 0))
            self.xp.poke(CRAM_BASE + s * 2, 2, self.cram.get(s, 0))
        if verbose:
            print(f"uploaded {len(slots)} slot(s).", flush=True)
        return self

    def verify(self):
        """Read back the defined slots (idle, reliable) and flag mismatches."""
        ok = True
        for s in sorted(set(self.pram) | set(self.cram)):
            gp = self.xp.read_pram(s); gc = self.xp.read_cram(s)
            wp = self.pram.get(s, 0) & 0x0fffffff        # PRAM is ~28-bit on readback
            wc = self.cram.get(s, 0)
            bad = (gp != wp) or (gc != wc)
            ok = ok and not bad
            print(f"slot {s:3d}: PRAM {gp:08x} (want {wp:08x}) CRAM {gc:04x} (want {wc:04x}) {'MISMATCH' if bad else ''}")
        return ok


if __name__ == "__main__":
    xp = XP()
    # xp.reset()

    IRAM3 = [
        0x00000000, # 0
        0x00000000, # 1
        0x00000000, # 2
        0x00000000, # 3
        0x00000000, # 4
        0x00000000, # 5
        0x00000000, # 6
        0x00000000, # 7
        0x00000000, # 8
        0x00000000, # 9
        0x00000000, # 10
        0x00000000, # 11
        0x00000000, # 12
        0x00000000, # 13
        0x00000000, # 14
        0x00000000, # 15
        0x00000000, # 16
        0x00000000, # 17
        0x00000000, # 18
        0x00000000, # 19
        0x00000000, # 20
        0x00000000, # 21
        0x00000000, # 22
        0x00000000, # 23
        0x00000000, # 24
        0x00000000, # 25
        0x00000000, # 26
        0x00000000, # 27
        0x00000000, # 28
        0x00000000, # 29
        0x00000000, # 30
        0x00000000, # 31
        0x00000000, # 32
        0x00000000, # 33
        0x00000000, # 34
        0x00000000, # 35
        0x00000000, # 36
        0x00000000, # 37
        0x00000000, # 38
        0x00000000, # 39
        0x00000000, # 40
        0x00000000, # 41
        0x00000000, # 42
        0x00000000, # 43
        0x00000000, # 44
        0x00000000, # 45
        0x00000000, # 46
        0x00000000, # 47
        0x00000000, # 48
        0x00000000, # 49
        0x00000000, # 50
        0x00000000, # 51
        0x00000000, # 52
        0x00000000, # 53
        0, #0x01ff01ff, # 54  some vol (silent if 0)
        0x00000000, # 55  also this one works
        0x00000000, # 56
        0x00000000, # 57
        0, #0x01ff01ff, # 58  some vol (silent if 0)
        0, #0x01ff01ff, # 59  reverb vol
        0x00000000, # 60
        0x00000000, # 61
        0x00000000, # 62
        0, #0x01ff01ff # 63  chorus vol?
    ]
    # for i, v in enumerate(IRAM3):
    #     if v: xp.poke(0x3200 + i*4, 4, v)



    # (xp.program()
    #     # # EQ L
    #     # .nop() #.line_s(st=1, word=0xd3, col=0x00, cram=0x000000)
    #     # .nop() #.line_s(st=1, word=0xd4, col=0x21, cram=0x003d24) # mid band 1
    #     # .nop() #.line_s(st=0, word=0x00, col=0x30, cram=0x000009)
    #     # .nop() #.line_s(st=3, word=0xd4, col=0x00, cram=0x000000)
    #     # .nop() #.line_s(st=1, word=0xd6, col=0x00, cram=0x000000)
    #     # .nop() #.line_s(st=1, word=0xd7, col=0x21, cram=0x000000)
    #     # .nop() #.line_s(st=0, word=0x00, col=0x30, cram=0x000009)
    #     # .nop() #.line_s(st=3, word=0xd7, col=0x00, cram=0x000000)
    #     # .nop() # .line_s(st=1, word=0x4a, col=0x00, cram=0x000000) # read bus input L (sel=0x080-0x0bf)
    #     # .nop() # .line_s(st=0, word=0x00, col=0x21, cram=0x00e000)
    #     # .nop() # .line_s(st=1, word=0xf6, col=0x30, cram=0x000005)
    #     # .nop() # .line_s(st=1, word=0xd0, col=0x30, ext=2, cram=0x0002d1)
    
    #     # .nop() # .line_s(st=1, word=0xd1, col=0x25, cram=0x002190) # low band
    #     # .nop() # .line_s(st=3, word=0xd0, col=0x15, cram=0x00508b) # low band
    #     # .nop() # .line_s(st=0, word=0x00, col=0x23, ext=2, cram=0x001f87) # low band
    
    #     # # .nop() # .line_s(st=1, word=0xd2, col=0x23, ext=2, cram=0x006ff8) # high band
    #     # .nop() # .line_s(st=0, word=0x00, col=0x00, ext=2, cram=0x000000)
    #     # .nop() # .line_s(st=3, word=0xd1, col=0x15, cram=0x00932e) # high band
    #     # .nop() # .line_s(st=0, word=0x00, col=0x23, cram=0x003359) # high band
    
    #     # .nop() # .line_s(st=1, word=0xd3, col=0x23, cram=0x00572c) # mid band 1
    #     # .nop() # .line_s(st=3, word=0xd2, col=0x15, cram=0x000000) # mid band 1
    #     # .nop() # .line_s(st=1, word=0xd4, col=0x23, cram=0x0062df) # mid band 1
    #     # .nop() # .line_s(st=0, word=0x00, col=0x23, cram=0x005000) # mid band 1
    #     # .nop() # .line_s(st=1, word=0xd3, col=0x30, cram=0x000003) # mid band 1
    #     # .nop() # .line_s(st=1, word=0xd2, col=0x14, cram=0x000323) # mid band 1
    #     # .nop() # .line_s(st=1, word=0xd5, col=0x23, cram=0x005000) # mid band 1
    #     # .nop() # .line_s(st=3, word=0xd3, col=0x15, cram=0x00973e) # mid band 1
    
    #     # .nop() # .line_s(st=1, word=0xd6, col=0x23, cram=0x000000) # mid band 2
    #     # .nop() # .line_s(st=3, word=0xd5, col=0x15, cram=0x000000) # mid band 2
    #     # .nop() # .line_s(st=1, word=0xd7, col=0x23, cram=0x000000) # mid band 2
    #     # .nop() # .line_s(st=0, word=0x00, col=0x23, cram=0x000000) # mid band 2
    #     # .nop() # .line_s(st=1, word=0xd6, col=0x30, cram=0x000003) # mid band 2
    #     # .nop() # .line_s(st=1, word=0xd5, col=0x14, cram=0x000c7c) # mid band 2
    #     # .nop() # .line_s(st=0, word=0x00, col=0x23, cram=0x005000) # mid band 2
    #     # .nop() # .line_s(st=3, word=0xd6, col=0x15, cram=0x000000) # mid band 2
    
    #     # .nop() # .line_s(st=0, word=0x00, col=0x30, cram=0x000003)
    #     # .nop() # .line_s(st=3, word=0x55, col=0x00, ext=4, cram=0x000000) # send efx L
    #     # # d500 sends to something chorus
    #     # # d540-d57f sends to efx out L
    #     # # d580 sends to efx out R
    #     # # d5c0 sends to something chorus



    #     # # # EQ R
    #     # .nop() # .line_s(st=1, word=0xdb, col=0x00, cram=0x000000)
    #     # .nop() # .line_s(st=1, word=0xdc, col=0x21, cram=0x003d24) # mid band 1
    #     # .nop() # .line_s(st=0, word=0x00, col=0x30, cram=0x000009)
    #     # .nop() # .line_s(st=3, word=0xdc, col=0x00, cram=0x000000)
    #     # .nop() # .line_s(st=1, word=0xde, col=0x00, cram=0x000000)
    #     # .nop() # .line_s(st=1, word=0xdf, col=0x21, cram=0x000000)
    #     # .nop() # .line_s(st=0, word=0x00, col=0x30, cram=0x000009)
    #     # .nop() # .line_s(st=3, word=0xdf, col=0x00, cram=0x000000)
    #     # .nop() # .line_s(st=1, word=0x4b, col=0x00, cram=0x000000) # read bus input R (sel=0x0c0-0x0ff)
    #     # .nop() # .line_s(st=0, word=0x00, col=0x21, cram=0x00e000)
    #     # .nop() # .line_s(st=1, word=0xf6, col=0x30, cram=0x000005)
    #     # .nop() # .line_s(st=1, word=0xd8, col=0x30, cram=0x0002d1)
    
    #     # .nop() # .line_s(st=1, word=0xd9, col=0x25, cram=0x002190) # low band
    #     # .nop() # .line_s(st=3, word=0xd8, col=0x15, cram=0x00508b) # low band
    #     # .nop() # .line_s(st=0, word=0x00, col=0x23, cram=0x001f87) # low band
    
    #     # .nop() # .line_s(st=1, word=0xda, col=0x23, cram=0x006ff8) # high band
    #     # .nop() # .line_s(st=3, word=0xd9, col=0x15, cram=0x00932e) # high band
    #     # .nop() # .line_s(st=0, word=0x00, col=0x23, cram=0x003359) # high band
    
    #     # .nop() # .line_s(st=1, word=0xdb, col=0x23, cram=0x00572c) # mid band 1
    #     # .nop() # .line_s(st=3, word=0xda, col=0x15, cram=0x000000) # mid band 1
    #     # .nop() # .line_s(st=1, word=0xdc, col=0x23, cram=0x0062df) # mid band 1
    #     # .nop() # .line_s(st=0, word=0x00, col=0x23, cram=0x005000) # mid band 1
    #     # .nop() # .line_s(st=1, word=0xdb, col=0x30, cram=0x000003) # mid band 1
    #     # .nop() # .line_s(st=1, word=0xda, col=0x14, cram=0x000323) # mid band 1
    #     # .nop() # .line_s(st=1, word=0xdd, col=0x23, cram=0x005000) # mid band 1
    #     # .nop() # .line_s(st=3, word=0xdb, col=0x15, cram=0x00973e) # mid band 1
    
    #     # .nop() # .line_s(st=1, word=0xde, col=0x23, cram=0x000000) # mid band 2
    #     # .nop() # .line_s(st=3, word=0xdd, col=0x15, cram=0x000000) # mid band 2
    #     # .nop() # .line_s(st=1, word=0xdf, col=0x23, cram=0x000000) # mid band 2
    #     # .nop() # .line_s(st=0, word=0x00, col=0x23, cram=0x000000) # mid band 2
    #     # .nop() # .line_s(st=1, word=0xde, col=0x30, cram=0x000003) # mid band 2
    #     # .nop() # .line_s(st=1, word=0xdd, col=0x14, cram=0x000c7c) # mid band 2
    #     # .nop() # .line_s(st=0, word=0x00, col=0x23, cram=0x005000) # mid band 2
    #     # .nop() # .line_s(st=3, word=0xde, col=0x15, cram=0x000000) # mid band 2
    
    #     # .nop() # .line_s(st=0, word=0x00, col=0x30, cram=0x000003)
    #     # .nop() # .line_s(st=3, word=0x56, col=0x00, cram=0x000000)  # send efx R



    #     # PASSTHROUGH
    #     .line_s(st=1, word=0x4a, col=0x00) # read bus input L (sel=0x080-0x0bf)
    #     # .line_s(st=1, word=0x4b, col=0x00) # read bus input R (sel=0x0c0-0x0ff)
    #     .line_s(st=0, word=0x00, col=0x21, cram=0x00e000) # coef: volume? capped?
    #     .line_s(st=1, word=0xf6, col=0x30)
    #     .line_s(st=1, word=0xd0, col=0x30, ext=2) # nop -> no left out
    #     .nop()
    #     .nop()
    #     .line_s(ext=2)
    #     .line_s(ext=2)
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .line_s(st=3, word=0x55, col=0x00, ext=4) # send efx L
    #     .line_s(st=3, word=0x56, col=0x00)  # send efx R



    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()



    #     # chorus
    #     .line_s(st=1, word=0x48, col=0x30, cram=0x000001)
    #     .line_s(st=1, word=0xc8, col=0x21, cram=0x00e000)
    #     .line_s(st=1, word=0x46, col=0x30, cram=0x000009)
    #     .line_s(st=1, word=0xc6, col=0x14, cram=0x001ff0)
    #     .line_s(st=3, word=0x00, col=0x30, cram=0x000009)
    #     .line_s(st=3, word=0xc1, col=0x11, cram=0x003fe0)
    #     .line_s(st=1, word=0xc7, col=0x30, cram=0x000009)
    #     .line_s(st=3, word=0xc6, col=0x1f, cram=0x000432) # coef: chorus rate
    #     .line_s(st=1, word=0x47, col=0x30, cram=0x001001)
        
    #     .line_s(st=3, word=0xc7, col=0x30, cram=0x002801)
    #     .line_s(st=0, word=0x00, col=0x14, cram=0x000183) # coef: chorus depth
    #     .line_s(st=3, word=0x03, col=0x2f, cram=0x008001) # coef: chorus delay
    #     .line_s(st=0, word=0x00, col=0x20, cram=0x000000)
    #     .line_s(st=0, word=0x00, col=0x0f, cram=0x001000)
    #     .line_s(st=2, word=0xc0, col=0x20, cram=0x000000)
    #     .line_s(st=0, word=0x00, col=0x30, cram=0x001001)
    #     .line_s(st=1, word=0xc0, col=0x30, cram=0x000231)
    #     .line_s(st=0, word=0x00, col=0x30, cram=0x000325)
        
    #     .line_s(st=1, word=0xc7, col=0x30, cram=0x000003)
    #     .line_s(st=3, word=0x50, col=0x1f, cram=0x008400)
        
    #     .line_s(st=0, word=0x00, col=0x30, cram=0x002801)
    #     .line_s(st=0, word=0x00, col=0x11, cram=0x000183) # coef: chorus depth
    #     .line_s(st=0, word=0x00, col=0x2f, cram=0x008001) # coef: chorus delay
    #     .line_s(st=0, word=0x00, col=0x20, cram=0x000000)
    #     .line_s(st=0, word=0x00, col=0x0f, cram=0x001000)
    #     .line_s(st=2, word=0xc0, col=0x20, cram=0x000000)
    #     .line_s(st=0, word=0x00, col=0x30, cram=0x001001)
    #     .line_s(st=1, word=0xc0, col=0x30, cram=0x000231)
    #     .line_s(st=1, word=0x50, col=0x30, cram=0x000325)
        
    #     .line_s(st=1, word=0xc1, col=0x23, cram=0x001000)
    #     .line_s(st=3, word=0x51, col=0x15, cram=0x001000)
    #     .line_s(st=1, word=0xfe, col=0x23, cram=0x001fff)
    #     .line_s(st=1, word=0x49, col=0x30, cram=0x0002d5)
    #     .line_s(st=1, word=0xcf, col=0x25, hi=0x80, wb=1, cram=0x00e000)
    #     .line_s(st=3, word=0x52, col=0x09, cram=0x000000) # nop -> no reverb
    #     .line_s(st=1, word=0xca, col=0x15, ext=2, cram=0x001ff0)
    #     .line_s(st=3, word=0x46, col=0x30, cram=0x000009) # nop -> no reverb
    #     .line_s(st=0, word=0x00, col=0x11, cram=0x003fe0)
    #     .line_s(st=1, word=0xcb, col=0x19, ext=2, cram=0x001fff)


    #     # reverb
    #     .line_s(st=3, word=0xca, col=0x25, ext=2, cram=0x000000)
    #     .line_s(st=0, word=0x00, col=0x03, hi=0x91, cram=0x000000)
    #     .line_s(st=3, word=0xcb, col=0x15, hi=0x0b, cram=0x000800)
    #     .line_s(st=3, word=0x47, col=0x35, hi=0x91, cram=0x003000)
    #     .line_s(st=0, word=0x00, col=0x33, hi=0x5f, wb=1, cram=0x005000)
    #     .line_s(st=0, word=0x00, col=0x15, hi=0x90, wb=1, cram=0x001000)
    #     .line_s(st=0, word=0x00, col=0x33, cram=0x003000)
    #     .line_s(st=0, word=0x00, col=0x30, hi=0x92, cram=0x000003)
    #     .line_s(st=0, word=0x00, col=0x31, hi=0x38, cram=0x005000)
    #     .line_s(st=0, word=0x00, col=0x15, hi=0x91, wb=1, cram=0x001000)
    #     .line_s(st=0, word=0x00, col=0x33, hi=0x0c, cram=0x003000)
    #     .line_s(st=0, word=0x00, col=0x30, hi=0x94, cram=0x000003)
    #     .line_s(st=0, word=0x00, col=0x31, hi=0x80, wb=1, cram=0x005000)
    #     .line_s(st=0, word=0x00, col=0x15, hi=0x91, wb=1, cram=0x001000)
    #     .line_s(st=0, word=0x00, col=0x33, hi=0x60, wb=1, cram=0x000000)
    #     .line_s(st=0, word=0x00, col=0x30, hi=0xa1, cram=0x000003)
    #     .line_s(st=0, word=0x00, col=0x31, hi=0xe9, cram=0x005000)
    #     .line_s(st=0, word=0x00, col=0x15, hi=0x92, wb=1, cram=0x000000)
        
    #     .line_s(st=1, word=0xcc, col=0x33, hi=0x39, cram=0x005000) # rev HF damp L
    #     .line_s(st=3, word=0xce, col=0x25, hi=0x99, cram=0x000000) # rev HF damp L
    #     .line_s(st=1, word=0xce, col=0x30, hi=0x63, wb=1, ext=5, cram=0x000003)
        
    #     .line_s(st=3, word=0xcc, col=0x30, hi=0x96, cram=0x000001)
        
    #     .line_s(st=2, word=0xc0, col=0x14, hi=0x62, wb=1, cram=0x0012c0)
    #     .line_s(st=0, word=0x00, col=0x33, hi=0x9a, cram=0x003000)
    #     .line_s(st=0, word=0x00, col=0x33, hi=0xde, wb=1, cram=0x005000)
    #     .line_s(st=1, word=0x4a, col=0x15, hi=0x94, wb=1, cram=0x001000)
    #     .line_s(st=0, word=0x00, col=0x23, hi=0x81, wb=1, cram=0x00e000)
    #     .line_s(st=1, word=0xc0, col=0x35, hi=0x96, wb=1, cram=0x003000)
    #     .line_s(st=3, word=0x4a, col=0x39, hi=0x63, wb=1, cram=0x005000)
    #     .line_s(st=1, word=0x00, col=0x15, hi=0x99, wb=1, cram=0x001000)
    #     .line_s(st=1, word=0x03, col=0x23, hi=0x63, wb=1, cram=0x00e000)
    #     .line_s(st=1, word=0x4c, col=0x25, hi=0x9a, wb=1, cram=0x00e000)
        
    #     .line_s(st=3, word=0x00, col=0x25, hi=0xdf, wb=1, cram=0x00e000)
    #     .line_s(st=3, word=0x03, col=0x30, hi=0xaf, cram=0x000005)
    #     .line_s(st=3, word=0x01, col=0x00, hi=0x8e, cram=0x000000)
        
    #     .line_s(st=1, word=0xcd, col=0x31, hi=0xa9, cram=0x005000) # rev HF damp R
    #     .line_s(st=1, word=0xce, col=0x25, hi=0x3b, wb=1, cram=0x000000) # rev HF damp R
    #     .line_s(st=2, word=0xc0, col=0x30, hi=0xa3, cram=0x000003)
        
    #     .line_s(st=3, word=0xcd, col=0x14, hi=0xb6, wb=1, cram=0x0012c0)
    #     .line_s(st=0, word=0x00, col=0x33, hi=0xac, cram=0x003000)
    #     .line_s(st=0, word=0x00, col=0x33, hi=0x45, wb=1, cram=0x005000)
    #     .line_s(st=1, word=0x4b, col=0x15, hi=0xa1, wb=1, cram=0x001000)
    #     .line_s(st=0, word=0x00, col=0x23, hi=0xea, cram=0x00e000)
    #     .line_s(st=1, word=0xc0, col=0x35, hi=0xa3, wb=1, cram=0x003000)
    #     .line_s(st=3, word=0x4b, col=0x39, hi=0xb7, wb=1, cram=0x005000)
    #     .line_s(st=1, word=0x4d, col=0x15, hi=0xa9, wb=1, cram=0x001000)
    #     .line_s(st=1, word=0x4e, col=0x23, hi=0x3c, wb=1, cram=0x00e000)
    #     .line_s(st=1, word=0xc3, col=0x25, hi=0xac, wb=1, cram=0x00e000)
        
    #     .line_s(st=3, word=0x04, col=0x30, hi=0x46, wb=1, cram=0x000005)
    #     .line_s(st=3, word=0x02, col=0x30, hi=0x98, cram=0x001804)
    #     .line_s(st=1, word=0x4f, col=0x30, hi=0xb5, wb=1, cram=0x001001)
    #     .line_s(st=3, word=0xc3, col=0x31, hi=0x9f, cram=0x005000)
    #     .line_s(st=0, word=0x00, col=0x25, hi=0xe9, cram=0x00e000)
    #     .line_s(st=0, word=0x00, col=0x15, hi=0xab, cram=0x005000)
    #     .line_s(st=3, word=0x05, col=0x35, hi=0x45, cram=0x005000)
    #     .line_s(st=0, word=0x00, col=0x33, hi=0xaf, cram=0x005000)
    #     .line_s(st=0, word=0x00, col=0x03, hi=0xff, wb=1, cram=0x000000)
    #     .line_s(st=0, word=0x00, col=0x15, hi=0x9a, cram=0x005000)
    #     .line_s(st=3, word=0x4c, col=0x35, hi=0x30, cram=0x005000)
    #     .line_s(st=0, word=0x00, col=0x33, hi=0xa0, cram=0x005000)
    #     .line_s(st=3, word=0x53, col=0x05, hi=0x78, wb=1, cram=0x000000) # reverb out L?
    #     .line_s(st=0, word=0x00, col=0x15, hi=0xaf, cram=0x005000)
    #     .line_s(st=3, word=0x4d, col=0x35, hi=0xc1, cram=0x005000)
    #     .line_s(st=0, word=0x00, col=0x33, hi=0xae, cram=0x005000)
    #     .line_s(st=0, word=0x00, col=0x30, hi=0x1d, wb=1, cram=0x000003)
        

    #     # mixing?
    #     .line_s(st=1, word=0xfa, col=0x31, cram=0x005000)
    #     .line_s(st=1, word=0x55, col=0x30, cram=0x000003)
    #     .line_s(st=3, word=0x54, col=0x00, cram=0x000000)
    #     .line_s(st=1, word=0x56, col=0x30, cram=0x0002e1)
    #     .line_s(st=1, word=0xf6, col=0x30, cram=0x0002e5)
    #     .line_s(st=1, word=0x4b, col=0x30, cram=0x0002d5)
    #     .line_s(st=1, word=0xf7, col=0x30, cram=0x0002d5)
    #     .line_s(st=3, word=0xc0, col=0x30, cram=0x0002e5)
    #     .line_s(st=1, word=0x4a, col=0x30, cram=0x000003)
    #     .line_s(st=3, word=0x56, col=0x30, cram=0x0002e1)
    #     .line_s(st=1, word=0xc0, col=0x15, cram=0x001000)
    #     .line_s(st=1, word=0x56, col=0x30, cram=0x000002)
    #     .line_s(st=3, word=0x55, col=0x15, cram=0x001000)
    #     .line_s(st=1, word=0x52, col=0x30, cram=0x000003)
    #     .line_s(st=3, word=0xc0, col=0x11, cram=0x001fff) # coef: EFX OUT to mix/reverb
    #     .line_s(st=1, word=0xc0, col=0x25, cram=0x005000)
    #     .line_s(st=1, word=0x50, col=0x23, cram=0x000000) # coef: EFX OUT to chorus
    #     .line_s(st=3, word=0xcf, col=0x25, cram=0x000000)
    #     .line_s(st=1, word=0xff, col=0x30, cram=0x000003)
    #     .line_s(st=3, word=0xc8, col=0x00, cram=0x000000)
    #     .line_s(st=1, word=0x50, col=0x00, cram=0x000000)
    #     .line_s(st=1, word=0x51, col=0x30, cram=0x0002e1)
    #     .line_s(st=1, word=0xfb, col=0x30, cram=0x0002e5)
    #     .line_s(st=3, word=0x50, col=0x30, cram=0x000005)
    #     .line_s(st=3, word=0x51, col=0x00, cram=0x000000)
    #     .line_s(st=1, word=0x53, col=0x00, cram=0x000000)
    #     .line_s(st=1, word=0x54, col=0x30, cram=0x0002e1)
    #     .line_s(st=1, word=0x00, col=0x30, cram=0x0002e5)
    #     .line_s(st=3, word=0x53, col=0x30, cram=0x000005)
    #     .line_s(st=3, word=0x54, col=0x00, cram=0x000000)
    #     .line_s(st=1, word=0x50, col=0x30, cram=0x000004)
    #     .line_s(st=1, word=0x53, col=0x30, cram=0x000002)
    #     .line_s(st=1, word=0x03, col=0x30, cram=0x000002)
    #     .line_s(st=3, word=0x00, col=0x00, cram=0x000000)
    #     .line_s(st=1, word=0x51, col=0x30, cram=0x000004)
    #     .line_s(st=1, word=0x54, col=0x30, cram=0x000002)
    #     .line_s(st=1, word=0x55, col=0x30, cram=0x000002)
    #     .line_s(st=3, word=0x03, col=0x00, cram=0x000000)
        
    #     .line_s(st=1, word=0x00, col=0x21, cram=0x001fff) # coef: EFX OUT assign L, nop -> rev becomes mono
    #     # EFX OUT: 0x021->mix, 0x061->out1, 0x0a1->out2
    #     .line_s(st=1, word=0x56, col=0x30, cram=0x000009) # nop -> dry goes away
    #     .line_s(st=3, word=0x00, col=0x01, cram=0x000000) # rev input L?
    #     # EFX OUT: 0x001->mix, 0x041->out1, 0x081->out2
        
    #     .line_s(st=1, word=0x03, col=0x25, cram=0x001fff) # coef: EFX OUT assign R
    #     # EFX OUT: 0x0e5->mix, 0x125->out1, 0x165->out2
    #     .line_s(st=3, word=0x4a, col=0x09, cram=0x000000) # main out R?
    #     .line_s(st=3, word=0x03, col=0x30, cram=0x000005) # rev input R?
    #     # EFX OUT: 0x0f0->mix, 0x130->out1, 0x170->out2
        
    #     .line_s(st=3, word=0x4b, col=0x00, hi=0xaf, wb=1, cram=0x000000)
    #     .line_s(st=3, word=0x4e, col=0x00, hi=0xff, wb=1, cram=0x000000)
    #     .line_s(st=3, word=0x4f, col=0x00, cram=0x000000)

    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .nop()
    #     .upload())

    # xp.reset()

    (xp.program()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .upload())

    for i in range(64): xp.poke(IRAM1_BASE + i * 4, 4, 0x0000)
    for i in range(64): xp.poke(IRAM2_BASE + i * 4, 4, 0x0000)
    for i in range(64): xp.poke(IRAM3_TGT + i * 2, 2, 0x0000)
    for i in range(64): xp.poke(IRAM3_BASE + i * 4, 4, 0x0000)

    # xp.poke(IRAM1_BASE + 0 * 4, 4, 0x0100)


    (xp.program()
        # .line_s(st=1, word=0xcf, col=0x30, cram=0x000000)
        # .line_s(st=3, word=0xce, col=0x1f, cram=0x000080) # coef: increment
        # .line_s(st=1, word=0x4f, col=0x30, cram=0x000000) # coef: 0x001001 => unsat?
        # .line_s(st=3, word=0xcf, col=0x30, cram=0x000000)
        
        # .line_s(st=1, word=0xc0, col=0x0f, cram=0x004234)
        # .line_s(st=0, word=0x00, col=0x1f, cram=0x000001) # acc += 0x123
        
        # .line_s(st=0, word=0x00, col=0x1f, cram=0x004242)
        # .nop()
        # .nop()
        # .line_s(st=2, word=0x80, col=0x1f, cram=0x000001)
        # .nop()
        # .nop()
        
        .line_s(st=0, word=0x00, col=0x1f, cram=0x000321)
        .nop()
        .nop()
        .line_s(st=0, word=0x00, col=0x1f, cram=0x000123)
        .nop()
        .nop()
        .line_s(st=3, word=0xff, col=0x00, cram=0x000000) # store acc?

        # op=0x8 -> store

        # op=0xf, sm=0, sel=0x000 -> store at IRAM[128-128]
        # op=0xf, sm=0, sel=0x1f0 -> store at IRAM[134-128]
        # op=0xf, sm=1, sel=0x1f0 -> store at IRAM[136-128]

        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .nop()
        .upload())


    xp.monitor(bank=1, start=0, count=256)
    # xp.monitor(addrs=[("",135*4+IRAM1_BASE,4), ("",199*4+IRAM1_BASE,4)], count=2)
