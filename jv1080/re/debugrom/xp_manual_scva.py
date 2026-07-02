#!/usr/bin/env python3
"""
Manual XP DSP driver for hands-on ISA probing.

Express a DSP program as (PRAM, CRAM) lines, one per slot (they are address-aligned:
PRAM slot k = 0x3400+4k is 32-bit, CRAM slot k = 0x2c00+2k is 16-bit):

    from xp_manual import XP
    xp = XP()
    xp.reset()                          # zero the whole DSP area + restore exec config (0x3916=7)

    (xp.program()
        .line(0x00007000, 0x0042)       # slot 0 : PRAM=0x00007000, CRAM=0x0042
        .line(0x01000000, 0x0000)       # slot 1
        .nop()                          # slot 2 : PRAM=0, CRAM=0
        .line(0x00800000, 0x0000)       # slot 3
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
            for i in range(64): self.poke(IRAM3_TGT + i * 2, 2, 0)   # stop ramps first
            for i in range(64): self.poke(IRAM3_BASE + i * 4, 4, 0)
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
        """Append one instruction at the next slot: PRAM 32-bit + aligned CRAM 16-bit."""
        self.pram[self._slot] = pram & 0xffffffff
        self.cram[self._slot] = cram & 0xffff
        self._slot += 1
        return self

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

    def show(self):
        slots = sorted(set(self.pram) | set(self.cram))
        print("slot  PRAM(0x3400+)   CRAM(0x2c00+)")
        for s in slots:
            print(f"{s:4d}  {self.pram.get(s,0):08x}       {self.cram.get(s,0):04x}")
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


# ---- quick demo: clear, upload a 2-instruction probe, monitor IRAM1 ----
if __name__ == "__main__":
    xp = XP()
    xp.reset()
    
    (xp.program()
        .line(0x02007acc, 0xe000)
        .line(0x00c00025, 0x5000)
        .line(0x0002fb15, 0x5000)
        .line(0x0000b183, 0x0000)
        .line(0x00007ad5, 0xa000)
        .line(0x01c67c45, 0x0000)
        .line(0x0080fb70, 0x0005)
        .line(0x01cc7c30, 0x42e1)
        .line(0x01005eb0, 0x42e5)
        .line(0x00005cd9, 0x5000)
        .line(0x0000c2c9, 0x0000)
        .line(0x0000c270, 0x0005)
        .line(0x01d27b01, 0x0000)
        .line(0x01807c70, 0x42e5)
        .line(0x01d95d30, 0x42e5)
        .line(0x00005ed9, 0x5000)
        .line(0x0000c289, 0x0000)
        .line(0x0000c330, 0x0005)
        .line(0x01df5401, 0x0000)
        .line(0x00805465, 0x0000)
        .line(0x01e554a5, 0x0000)
        .line(0x010054e3, 0x0000)
        .line(0x00005523, 0x0000)
        .line(0x00005563, 0x0000)
        .line(0x000055a3, 0x0000)
        .line(0x000055e3, 0x0000)
        .line(0x00005623, 0x0000)
        .line(0x00005663, 0x0000)
        .line(0x000056a3, 0x0000)
        .line(0x000056e3, 0x0000)
        .line(0x00005723, 0x0000)
        .line(0x00005763, 0x0000)
        .line(0x000057a3, 0x0000)
        .line(0x000057e3, 0x0000)
        .line(0x00005823, 0x0000)
        .line(0x00005863, 0x0000)
        .line(0x000058a3, 0x0000)
        .line(0x000058e3, 0x0000)
        .line(0x00005923, 0x0000)
        .line(0x00005963, 0x0000)
        .line(0x000059a3, 0x0000)
        .line(0x000059e3, 0x0000)
        .line(0x00005a23, 0x0000)
        .line(0x00005a63, 0x0000)
        .line(0x00005aa3, 0x0000)
        .line(0x00005ae3, 0x0000)
        .line(0x00005b23, 0x0000)
        .line(0x00005b63, 0x0000)
        .line(0x00c05ba3, 0x0000)
        .line(0x00025be3, 0x0000)
        .line(0x00885c23, 0x0000)
        .line(0x0001b223, 0x5000)
        .line(0x0088b503, 0x0000)
        .line(0x04015415, 0xa000)
        .line(0x01ebb545, 0x0000)
        .line(0x0180f0f0, 0x0005)
        .line(0x01f25461, 0x0000)
        .line(0x000054a5, 0x0000)
        .line(0x000054e3, 0x0000)
        .line(0x00005523, 0x0000)
        .line(0x00005563, 0x0000)
        .line(0x000055a3, 0x0000)
        .line(0x000055e3, 0x0000)
        .line(0x00005623, 0x0000)
        .line(0x00005663, 0x0000)
        .line(0x000056a3, 0x0000)
        .line(0x000056e3, 0x0000)
        .line(0x00005723, 0x0000)
        .line(0x00005763, 0x0000)
        .line(0x000057a3, 0x0000)
        .line(0x000057e3, 0x0000)
        .line(0x00005823, 0x0000)
        .line(0x04005863, 0x0000)
        .line(0x040058a3, 0x0000)
        .line(0x000058e3, 0x0000)
        .line(0x00005923, 0x0000)
        .line(0x00005963, 0x0000)
        .line(0x000059a3, 0x0000)
        .line(0x000059e3, 0x0000)
        .line(0x00005a23, 0x0000)
        .line(0x00005a63, 0x0000)
        .line(0x00005aa3, 0x0000)
        .line(0x00005ae3, 0x0000)
        .line(0x00005b23, 0x0000)
        .line(0x00005b63, 0x0000)
        .line(0x00005ba3, 0x0000)
        .line(0x00885be3, 0x0000)
        .line(0x00015f63, 0x0000)
        .line(0x0000b5a3, 0x5000)
        .line(0x00887b43, 0x0000)
        .line(0x00017cd5, 0xa000)
        .line(0x01f8b5f0, 0x42e5)
        .line(0x00800030, 0x0003)
        .line(0x000072d1, 0x1ff0)
        .line(0x00000030, 0x0009)
        .line(0x08000011, 0x3fe0)
        .line(0x00007319, 0x5000)
        .line(0x0000f2e5, 0x0000)
        .line(0x000073b0, 0x0003)
        .line(0x0088f321, 0x0000)
        .line(0x00017e70, 0x0003)
        .line(0x0088b630, 0x42d1)
        .line(0x00017370, 0x0005)
        .line(0x0180b65f, 0x0400)
        .line(0x00000030, 0x1001)
        .line(0x0000f370, 0x2801)
        .line(0x00880011, 0x00f6)
        .line(0x0001002f, 0x1000)
        .line(0x0000b6a0, 0x0000)
        .line(0x0000000f, 0x1000)
        .line(0x0000b020, 0x0000)
        .line(0x00000030, 0x1001)
        .line(0x00007030, 0x0231)
        .line(0x00000030, 0x0325)
        .line(0x00887370, 0x0003)
        .line(0x0001f3df, 0x8400)
        .line(0x0000b6f0, 0x2801)
        .line(0x00880011, 0x00f6)
        .line(0x0001002f, 0x1000)
        .line(0x0000b720, 0x0000)
        .line(0x0000000f, 0x1000)
        .line(0x0000b020, 0x0000)
        .line(0x00000030, 0x1001)
        .line(0x00007030, 0x0231)
        .line(0x00000030, 0x0325)
        .line(0x00000030, 0x0003)
        .line(0x0000f380, 0x0000)
        .line(0x00007e80, 0x0000)
        .line(0x0200738c, 0xe000)
        .line(0x000073e5, 0x5000)
        .line(0x0000faf0, 0x0009)
        .line(0x00007ef0, 0x42d1)
        .line(0x00007f30, 0x42d5)
        .line(0x0000f270, 0x0005)
        .line(0x0000f280, 0x0000)
        .line(0x000073b0, 0x42e1)
        .line(0x00007d30, 0x42e5)
        .line(0x0000f0b0, 0x0005)
        .line(0x0000f040, 0x0000)
        .line(0x00887b40, 0x0000)
        .line(0x000170f0, 0x42e1)
        .line(0x0088b770, 0x0009)
        .line(0x000172b0, 0x0001)
        .line(0x0090b7b0, 0x0002)
        .line(0x00017111, 0x1ff0)
        .line(0x0000b7f0, 0x0009)
        .line(0x00007f91, 0x3fe0)
        .line(0x00c07159, 0x5000)
        .line(0x0002f125, 0x0000)
        .line(0x0000b1f3, 0x0000)
        .line(0x0000f170, 0x0003)
        .line(0x000071f0, 0x42d1)
        .line(0x000071a5, 0x0000)
        .line(0x01c07065, 0x0000)
        .line(0x00007f70, 0x0003)
        .line(0x0000f030, 0x42d1)
        .line(0x000071f0, 0x0009)
        .line(0x0088f040, 0x0000)
        .line(0x00017221, 0x0000)
        .line(0x0000b825, 0x0000)
        .line(0x00007030, 0x0003)
        .line(0x000070b0, 0x42d2)
        .line(0x00007fd9, 0x5000)
        .line(0x0000f0b0, 0x0005)
        .line(0x00007cb0, 0x42d1)
        .line(0x00007b70, 0x0005)
        .line(0x00885f30, 0x42e1)
        .line(0x00017263, 0xa000)
        .line(0x0088b870, 0x0000)
        .line(0x00017451, 0x1ff0)
        .line(0x0000b8b0, 0x0009)
        .line(0x00007d51, 0x3fe0)
        .line(0x00887419, 0x5000)
        .line(0x0001f465, 0x0000)
        .line(0x0000b8f0, 0x0003)
        .line(0x0000f430, 0x42d1)
        .line(0x00005fb0, 0x0005)
        .line(0x01885fe1, 0xa000)
        .line(0x00000025, 0xa000)
        .line(0x00887dd5, 0xc800)
        .line(0x00017715, 0xc800)
        .line(0x0488c070, 0x42f5)
        .line(0x0001c165, 0x0000)
        .line(0x0000b930, 0x0003)
        .line(0x018a7754, 0x0000)
        .line(0x01890023, 0x0000)
        .line(0x00000030, 0x0003)
        .line(0x018d7794, 0x0000)
        .line(0x01120023, 0x0000)
        .line(0x00000030, 0x0003)
        .line(0x019077d4, 0x0000)
        .line(0x009b0023, 0x0000)
        .line(0x00007e30, 0x0003)
        .line(0x01937494, 0x0000)
        .line(0x002478e3, 0x0000)
        .line(0x0000f025, 0x0000)
        .line(0x00887030, 0x0003)
        .line(0x0001f4b0, 0x42d1)
        .line(0x00000039, 0x0000)
        .line(0x00000033, 0x5000)
        .line(0x059574d5, 0x0000)
        .line(0x05ad7923, 0x0000)
        .line(0x01987865, 0x0000)
        .line(0x01367023, 0x0000)
        .line(0x0000f4f0, 0x42d9)
        .line(0x00007870, 0x0003)
        .line(0x01a00014, 0x0000)
        .line(0x01d17530, 0x0003)
        .line(0x01a375b0, 0x0004)
        .line(0x015a7630, 0x0002)
        .line(0x000076b0, 0x0002)
        .line(0x00007db0, 0x0002)
        .line(0x00007070, 0x42d1)
        .line(0x00007570, 0x0009)
        .line(0x0000f070, 0x0004)
        .line(0x000075f0, 0x0004)
        .line(0x00007670, 0x0002)
        .line(0x000076f0, 0x0002)
        .line(0x00887db0, 0x0002)
        .line(0x000170b0, 0x42d1)
        .line(0x00007839, 0x5000)
        .line(0x0088f0a5, 0x0000)
        .line(0x00010023, 0x5000)
        .line(0x0f9b0015, 0x0000)
        .line(0x00bf78b3, 0x5000)
        .line(0x019e0025, 0x0000)
        .line(0x00480023, 0x5000)
        .line(0x019e0015, 0x0000)
        .line(0x00e34270, 0x0003)
        .line(0x01a97961, 0xa000)
        .line(0x006c79a5, 0x0000)
        .line(0x0000f955, 0x5000)
        .line(0x000079a3, 0x0000)
        .line(0x000079e3, 0x0000)
        .line(0x0000f995, 0x5000)
        .line(0x00000023, 0x0000)
        .line(0x000042f0, 0x0003)
        .line(0x0000f9e1, 0xa000)
        .line(0x00007070, 0x0003)
        .line(0x00000030, 0x0002)
        .line(0x00004291, 0x0000)
        .line(0x00007a25, 0xa000)
        .line(0x0000c0f0, 0x0005)
        .line(0x00007a61, 0x0000)
        .line(0x0000fa15, 0x5000)
        .line(0x00007a63, 0x0000)
        .line(0x00007aa3, 0x0000)
        .line(0x0000fa55, 0x5000)
        .line(0x00000023, 0x0000)
        .line(0x00004330, 0x0003)
        .line(0x0000faa1, 0xa000)
        .line(0x000070b0, 0x0003)
        .line(0x00000030, 0x0002)
        .line(0x00000011, 0x0000)
        .line(0x00000030, 0x0005)
        .line(0x0000c1c0, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .line(0x00000000, 0x0000)
        .upload())

    xp.monitor(bank=1, start=0, count=256)
