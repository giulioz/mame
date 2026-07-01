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


if __name__ == "__main__":
    xp = XP()
    xp.reset()

    IRAM3 = [
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x01ff01ff, # some vol (silent if 0)
        0x00000000,
        0x00000000,
        0x00000000,
        0x01ff01ff, # some vol (silent if 0)
        0x01ff01ff, # reverb vol
        0x00000000,
        0x00000000,
        0x00000000,
        0x01ff01ff # chorus vol?
    ]
    for i, v in enumerate(IRAM3):
        if v: xp.poke(0x3200 + i*4, 4, v)
    
    (xp.program()
        # EQ L
        .line(0x000074c0, 0x000000)
        .line(0x00007521, 0x003d24)
        .line(0x00000030, 0x000009)
        .line(0x0000f500, 0x000000)
        .line(0x00007580, 0x000000)
        .line(0x000075e1, 0x000000)
        .line(0x00000030, 0x000009)
        .line(0x0000f5c0, 0x000000)
        .line(0x00005280, 0x000000)
        .line(0x00000021, 0x00e000)
        .line(0x00007db0, 0x000005)
        .line(0x04007430, 0x0002d1)
        .line(0x00007465, 0x002190)
        .line(0x0000f415, 0x00508b)
        .line(0x04000023, 0x001f87)
        .line(0x040074a3, 0x006ff8)
        .line(0x0000f455, 0x00932e)
        .line(0x00000023, 0x003359)
        .line(0x000074e3, 0x00572c)
        .line(0x0000f495, 0x000000)
        .line(0x00007523, 0x0062df)
        .line(0x00000023, 0x005000)
        .line(0x000074f0, 0x000003)
        .line(0x00007494, 0x000323)
        .line(0x00007563, 0x005000)
        .line(0x0000f4d5, 0x00973e)
        .line(0x000075a3, 0x000000)
        .line(0x0000f555, 0x000000)
        .line(0x000075e3, 0x000000)
        .line(0x00000023, 0x000000)
        .line(0x000075b0, 0x000003)
        .line(0x00007554, 0x000c7c)
        .line(0x00000023, 0x005000)
        .line(0x0000f595, 0x000000)
        .line(0x00000030, 0x000003)
        .line(0x0800d400, 0x000000) # send efx L
        # d500 sends to something chorus
        # d540-d57f sends to efx out L
        # d580 sends to efx out R
        # d5c0 sends to something chorus
        
        # EQ R
        .line(0x000076c0, 0x000000)
        .line(0x00007721, 0x003d24)
        .line(0x00000030, 0x000009)
        .line(0x0000f700, 0x000000)
        .line(0x00007780, 0x000000)
        .line(0x000077e1, 0x000000)
        .line(0x00000030, 0x000009)
        .line(0x0000f7c0, 0x000000)
        .line(0x000052c0, 0x000000)
        .line(0x00000021, 0x00e000)
        .line(0x00007db0, 0x000005)
        .line(0x00007630, 0x0002d1)
        .line(0x00007665, 0x002190)
        .line(0x0000f615, 0x00508b)
        .line(0x00000023, 0x001f87)
        .line(0x000076a3, 0x006ff8)
        .line(0x0000f655, 0x00932e)
        .line(0x00000023, 0x003359)
        .line(0x000076e3, 0x00572c)
        .line(0x0000f695, 0x000000)
        .line(0x00007723, 0x0062df)
        .line(0x00000023, 0x005000)
        .line(0x000076f0, 0x000003)
        .line(0x00007694, 0x000323)
        .line(0x00007763, 0x005000)
        .line(0x0000f6d5, 0x00973e)
        .line(0x000077a3, 0x000000)
        .line(0x0000f755, 0x000000)
        .line(0x000077e3, 0x000000)
        .line(0x00000023, 0x000000)
        .line(0x000077b0, 0x000003)
        .line(0x00007754, 0x000c7c)
        .line(0x00000023, 0x005000)
        .line(0x0000f795, 0x000000)
        .line(0x00000030, 0x000003)
        .line(0x0000d580, 0x000000)  # send efx R
        
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
        .line(0x00005230, 0x000001)
        .line(0x00007221, 0x00e000)
        .line(0x000051b0, 0x000009)
        .line(0x00007194, 0x001ff0)
        .line(0x0000c030, 0x000009)
        .line(0x0000f051, 0x003fe0)
        .line(0x000071f0, 0x000009)
        .line(0x0000f19f, 0x000432)
        .line(0x000051f0, 0x001001)
        .line(0x0000f1f0, 0x002801)
        .line(0x00000014, 0x000183)
        .line(0x0000c0ef, 0x008001)
        .line(0x00000020, 0x000000)
        .line(0x0000000f, 0x001000)
        .line(0x0000b020, 0x000000)
        .line(0x00000030, 0x001001)
        .line(0x00007030, 0x000231)
        .line(0x00000030, 0x000325)
        .line(0x000071f0, 0x000003)
        .line(0x0000d41f, 0x008400)
        .line(0x00000030, 0x002801)
        .line(0x00000011, 0x000183)
        .line(0x0000002f, 0x008001)
        .line(0x00000020, 0x000000)
        .line(0x0000000f, 0x001000)
        .line(0x0000b020, 0x000000)
        .line(0x00000030, 0x001001)
        .line(0x00007030, 0x000231)
        .line(0x00005430, 0x000325)
        .line(0x00007063, 0x001000)
        .line(0x0000d455, 0x001000)
        .line(0x00007fa3, 0x001fff)
        .line(0x00005270, 0x0002d5)
        .line(0x018073e5, 0x00e000)
        .line(0x0000d489, 0x000000)
        .line(0x04007295, 0x001ff0)
        .line(0x0000d1b0, 0x000009)
        .line(0x00000011, 0x003fe0)
        .line(0x040072d9, 0x001fff)
        .line(0x0400f2a5, 0x000000)
        .line(0x00910003, 0x000000)
        .line(0x000bf2d5, 0x000800)
        .line(0x0091d1f5, 0x003000)
        .line(0x015f0033, 0x005000)
        .line(0x01900015, 0x001000)
        .line(0x00000033, 0x003000)
        .line(0x00920030, 0x000003)
        .line(0x00380031, 0x005000)
        .line(0x01910015, 0x001000)
        .line(0x000c0033, 0x003000)
        .line(0x00940030, 0x000003)
        .line(0x01800031, 0x005000)
        .line(0x01910015, 0x001000)
        .line(0x01600033, 0x000000)
        .line(0x00a10030, 0x000003)
        .line(0x00e90031, 0x005000)
        .line(0x01920015, 0x000000)
        .line(0x00397333, 0x005000)
        .line(0x0099f3a5, 0x000000)
        .line(0x0b6373b0, 0x000003)
        .line(0x0096f330, 0x000001)
        .line(0x0162b014, 0x0012c0)
        .line(0x009a0033, 0x003000)
        .line(0x01de0033, 0x005000)
        .line(0x01945295, 0x001000)
        .line(0x01810023, 0x00e000)
        .line(0x01967035, 0x003000)
        .line(0x0163d2b9, 0x005000)
        .line(0x01994015, 0x001000)
        .line(0x016340e3, 0x00e000)
        .line(0x019a5325, 0x00e000)
        .line(0x01dfc025, 0x00e000)
        .line(0x00afc0f0, 0x000005)
        .line(0x008ec040, 0x000000)
        .line(0x00a97371, 0x005000)
        .line(0x013b73a5, 0x000000)
        .line(0x00a3b030, 0x000003)
        .line(0x01b6f354, 0x0012c0)
        .line(0x00ac0033, 0x003000)
        .line(0x01450033, 0x005000)
        .line(0x01a152d5, 0x001000)
        .line(0x00ea0023, 0x00e000)
        .line(0x01a37035, 0x003000)
        .line(0x01b7d2f9, 0x005000)
        .line(0x01a95355, 0x001000)
        .line(0x013c53a3, 0x00e000)
        .line(0x01ac70e5, 0x00e000)
        .line(0x0146c130, 0x000005)
        .line(0x0098c0b0, 0x001804)
        .line(0x01b553f0, 0x001001)
        .line(0x009ff0f1, 0x005000)
        .line(0x00e90025, 0x00e000)
        .line(0x00ab0015, 0x005000)
        .line(0x0045c175, 0x005000)
        .line(0x00af0033, 0x005000)
        .line(0x01ff0003, 0x000000)
        .line(0x009a0015, 0x005000)
        .line(0x0030d335, 0x005000)
        .line(0x00a00033, 0x005000)
        .line(0x0178d4c5, 0x000000)
        .line(0x00af0015, 0x005000)
        .line(0x00c1d375, 0x005000)
        .line(0x00ae0033, 0x005000)
        .line(0x011d0030, 0x000003)
        .line(0x00007eb1, 0x005000)
        .line(0x00005570, 0x000003)
        .line(0x0000d500, 0x000000)
        .line(0x000055b0, 0x0002e1)
        .line(0x00007db0, 0x0002e5)
        .line(0x000052f0, 0x0002d5)
        .line(0x00007df0, 0x0002d5)
        .line(0x0000f030, 0x0002e5)
        .line(0x000052b0, 0x000003)
        .line(0x0000d5b0, 0x0002e1)
        .line(0x00007015, 0x001000)
        .line(0x000055b0, 0x000002)
        .line(0x0000d555, 0x001000)
        .line(0x000054b0, 0x000003)
        .line(0x0000f011, 0x001fff)
        .line(0x00007025, 0x005000)
        .line(0x00005423, 0x000000)
        .line(0x0000f3e5, 0x000000)
        .line(0x00007ff0, 0x000003)
        .line(0x0000f200, 0x000000)
        .line(0x00005400, 0x000000)
        .line(0x00005470, 0x0002e1)
        .line(0x00007ef0, 0x0002e5)
        .line(0x0000d430, 0x000005)
        .line(0x0000d440, 0x000000)
        .line(0x000054c0, 0x000000)
        .line(0x00005530, 0x0002e1)
        .line(0x00004030, 0x0002e5)
        .line(0x0000d4f0, 0x000005)
        .line(0x0000d500, 0x000000)
        .line(0x00005430, 0x000004)
        .line(0x000054f0, 0x000002)
        .line(0x000040f0, 0x000002)
        .line(0x0000c000, 0x000000)
        .line(0x00005470, 0x000004)
        .line(0x00005530, 0x000002)
        .line(0x00005570, 0x000002)
        .line(0x0000c0c0, 0x000000)
        .line(0x00004021, 0x001fff)
        .line(0x000055b0, 0x000009)
        .line(0x0000c001, 0x000000)
        .line(0x000040e5, 0x001fff)
        .line(0x0000d289, 0x000000)
        .line(0x0000c0f0, 0x000005)
        .line(0x01afd2c0, 0x000000)
        .line(0x01ffd380, 0x000000)
        .line(0x0000d3c0, 0x000000)

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
