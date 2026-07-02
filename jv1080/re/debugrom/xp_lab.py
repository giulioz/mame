#!/usr/bin/env python3
"""
XP DSP experiment harness for the JV-1080 debug ROM.

Methodology (deliberate, to avoid the red herrings we hit earlier):
  * NO note-on / note-off and NO program change.  Both make the SH firmware
    re-upload the DSP program (workers write PRAM/CRAM) and corrupt experiments.
  * Keep the synth idle: at idle the readback path is reliable; during active
    voice/effect processing the readback latch is contaminated with live bus data.
  * Clear the whole DSP area, upload a test program programmatically, then
    continuously monitor IRAM1/2/3 and the host readback register.

Readback mechanism (CSP/ESP-style, confirmed in firmware map):
  Reading a DSP address is a two-step latch transaction.  A PEEK of 0x0c00_xxxx
  (the "trigger") latches that location's value into the host readback register
  0x3910 (low 16) / 0x3912 (high 16).  Then you read 0x3910(+0x3912).
    - CRAM  0x2c00-0x2dff : 16-bit  -> 0x3910 only
    - IRAM/PRAM  >= 0x3000: 32-bit  -> 0x3912:0x3910
  jvdebug.JV.read_dsp() does exactly this.

DSP memory map (host view):
    0x2c00-0x2dff  CRAM   288 x 16-bit coefficients
    0x3000-0x30ff  IRAM1   64 x 32-bit   (host-writable working RAM)
    0x3100-0x31ff  IRAM2   64 x 32-bit
    0x3200-0x32ff  IRAM3   64 x 32-bit   ("magic" -> breakpoint-ramp engine auto-updates it)
    0x3300-0x33ff  IRAM3 ramp targets (9-bit breakpoints)  + rates at 0x3928
    0x3400-0x387f  PRAM   288 x 32-bit program words (~28-bit effective)
    0x3900-0x393f  config / status / IRQ / readback registers

Config registers of interest (roles provisional, verify before trusting):
    0x3908  global engine configuration
    0x3914  global config word (reads 0x403f after boot)
    0x3916  DSP access/update control  -- boot init does 7(run)->wait->0(stop)->7(run)
    0x3924  routing/address/control
    0x3928-392e  IRAM3 group interpolation rates
  DO NOT read 0x3910-0x391f with read_dsp() (its 32-bit trigger spans the IRQ
  status reg 0x3918 and hangs the firmware).  Use rd_reg() (direct 16-bit PEEK).

Usage:
    from xp_lab import XPDSP
    x = XPDSP()
    x.clear_dsp()
    x.upload(pram={0:0x00007000, 1:0x01000000, 4:0x00800000}, cram={7:0x5000})
    x.monitor_iram(secs=8)
Or just run:  python3 xp_lab.py
"""
import time, sys
from jvdebug import JV, poke, peek, ping, decode_value

XP = 0x0c000000

# --- region helpers ---
CRAM  = lambda i: 0x2c00 + i * 2
IRAM1 = lambda i: 0x3000 + i * 4
IRAM2 = lambda i: 0x3100 + i * 4
IRAM3 = lambda i: 0x3200 + i * 4
IR3T  = lambda i: 0x3300 + i * 2      # IRAM3 ramp target (breakpoint)
PRAM  = lambda i: 0x3400 + i * 4


class XPDSP:
    def __init__(self, pace=0.012):
        self.jv = JV()
        self.pace = pace
        if not self.jv.xfer(ping()):
            raise RuntimeError("no PING reply — check MIDI link / power")

    # ---------- raw access ----------
    def wr(self, off, val, width):
        """Write an XP register/DSP word (no base needed)."""
        self.jv.send(poke(XP + off, width, val))
        time.sleep(self.pace)

    def rd_reg(self, off, width=2):
        """Direct PEEK of an XP register.
        NOTE (hardware): the DSP *control* registers (0x3908, 0x3914, 0x3916, 0x3924, ...)
        are WRITE-ONLY on real silicon — they read back 0. You can drive exec via
        0x3916 but cannot read its state. Readable status: 0x3912 low-nibble busy,
        0x391c bit6 (EFX-delay comparator). The host readback 0x3910/0x3912 holds the
        last trigger-latched DSP value."""
        for _ in range(3):
            r = self.jv.xfer(peek(XP + off, width), timeout=2.0)
            if r:
                return decode_value(r)
        return None

    def rd_dsp(self, off, width=4):
        """Read DSP memory via the readback latch (trigger + read 0x3910[/0x3912])."""
        return self.jv.read_dsp(off, width)

    def rd_readback_raw(self):
        """Read the host readback register 0x3912:0x3910 WITHOUT triggering first.
        Use to detect whether the DSP itself writes the readback register
        (CSP/ESP-style store-to-host). NB: any prior rd_dsp() leaves its latched
        value here, so call this in a loop that does no other DSP reads."""
        lo = self.rd_reg(0x3910, 2)
        hi = self.rd_reg(0x3912, 2)
        if lo is None or hi is None:
            return None
        return (hi << 16) | lo

    # ---------- bulk ops ----------
    def clear_dsp(self, iram3=True, verbose=True):
        """Zero PRAM, CRAM, IRAM1/2, and (optionally) IRAM3 current+targets.
        Does NOT touch 0x3916 or any exec control. Paced to avoid MIDI overflow."""
        if verbose: print("clear: PRAM(288)...", flush=True)
        for i in range(288): self.wr(PRAM(i), 0, 4)
        if verbose: print("clear: CRAM(288)...", flush=True)
        for i in range(288): self.wr(CRAM(i), 0, 2)
        if verbose: print("clear: IRAM1/2(128)...", flush=True)
        for i in range(64): self.wr(IRAM1(i), 0, 4)
        for i in range(64): self.wr(IRAM2(i), 0, 4)
        if iram3:
            if verbose: print("clear: IRAM3 current+targets (magic; may not stick)...", flush=True)
            for i in range(64): self.wr(IR3T(i), 0, 2)   # targets first (stop ramps)
            for i in range(64): self.wr(IRAM3(i), 0, 4)   # then current values
        if verbose: print("clear done.", flush=True)

    def upload(self, pram=None, cram=None, iram3=None):
        """Upload a program. pram/cram/iram3 are {slot_index: value} dicts."""
        for slot, val in (pram or {}).items(): self.wr(PRAM(slot),  val, 4)
        for slot, val in (cram or {}).items(): self.wr(CRAM(slot),  val, 2)
        for slot, val in (iram3 or {}).items(): self.wr(IRAM3(slot), val, 4)

    # ---------- verification ----------
    def verify(self, pram=None, cram=None, n_show=8):
        """Read back what we uploaded (idle, reliable)."""
        if pram:
            got = {s: self.rd_dsp(PRAM(s), 4) for s in list(pram)[:n_show]}
            print("PRAM verify:", {s: hex(v) for s, v in got.items()}, flush=True)
        if cram:
            got = {s: self.rd_dsp(CRAM(s), 2) for s in list(cram)[:n_show]}
            print("CRAM verify:", {s: hex(v) for s, v in got.items()}, flush=True)

    # ---------- monitoring ----------
    def snap_iram(self, n=16, banks=(1, 2, 3)):
        bases = {1: IRAM1, 2: IRAM2, 3: IRAM3}
        return {b: [self.rd_dsp(bases[b](i), 4) for i in range(n)] for b in banks}

    def monitor_iram(self, secs=8, n=16, banks=(1, 2, 3), period=0.4):
        """Continuously scan IRAM1/2/3 and print. Highlights slots that change."""
        print(f"monitor IRAM {banks} slots[0:{n}] for {secs}s (idle; no notes)...", flush=True)
        prev = None
        t0 = time.time()
        while time.time() - t0 < secs:
            snap = self.snap_iram(n, banks)
            for b in banks:
                row = snap[b]
                changed = ""
                if prev is not None:
                    changed = "".join("*" if row[i] != prev[b][i] else "." for i in range(n))
                cells = " ".join(f"{(v & 0xffffff):06x}" if v is not None else "??????" for v in row)
                print(f"  t={time.time()-t0:5.1f} IRAM{b} [{changed}] {cells}", flush=True)
            prev = snap
            print(flush=True)
            time.sleep(period)

    def monitor_readback(self, secs=8, period=0.05):
        """Poll ONLY the host readback register (no triggers) to see if the DSP
        writes it autonomously (store-to-host). Prints only on change."""
        print(f"poll host readback 0x3912:0x3910 for {secs}s (no triggers)...", flush=True)
        last = object()
        t0 = time.time()
        while time.time() - t0 < secs:
            v = self.rd_readback_raw()
            if v != last:
                print(f"  t={time.time()-t0:6.2f} readback = {hex(v) if v is not None else '?'}", flush=True)
                last = v
            time.sleep(period)


# Example: the factory EFX test microcode (known semantics: copies IRAM3[0:3]->[4:7]).
# Replace with your own test program.
def _demo():
    x = XPDSP()
    print("=== clear DSP ===", flush=True)
    x.clear_dsp()
    print("=== upload tiny test program ===", flush=True)
    # provisional load/store encoding (from factory Rosetta) — verify empirically
    x.upload(
        pram={0: 0x00007000, 1: 0x01000000, 4: 0x00800000},   # clear-acc / load IRAM3[0] / store IRAM3[4]
        cram={7: 0x5000},                                      # 1.0
        iram3={0: 0x00abc000},                                 # seed source
    )
    x.verify(pram={0: 0, 1: 0, 4: 0}, cram={7: 0})
    print("=== monitor (edit exec control below to try starting the DSP) ===", flush=True)
    # e.g. to experiment with the run/stop control (PROVISIONAL, may need a real clean state):
    #   x.wr(0x3916, 0, 2); time.sleep(0.25); x.wr(0x3916, 7, 2)
    x.monitor_iram(secs=6, n=12, banks=(1, 2, 3))


if __name__ == "__main__":
    _demo()
