#!/usr/bin/env python3
"""Host-side client for the JV-1080 debug ROM (MIDI sysex peek/poke).

Private protocol: F0 7D <op> <nibblized payload> F7  (each byte -> hi nibble, lo nibble).
  ops: 00 PING, 01 SETFLAG, 10 POKE(addr4,width1,data4), 20 PEEK(addr4,width1), 21 PEEKDSP(off2)
Use addresses 0x0c00xxxx to reach the XP chip on CS4 (PEEKDSP for 0x2c00-0x38ff DSP RAM).

Needs python-rtmidi.  `python3 jvdebug.py` runs the bring-up self-test against the MiniFuse 2.
"""
import sys, time
import rtmidi


def nyb(value, nbytes):
    return [(value >> (k * 4)) & 0xf for k in range(nbytes * 2 - 1, -1, -1)]


def ping():               return [0xF0, 0x7D, 0x00, 0xF7]
def poke(addr, w, data):  return [0xF0, 0x7D, 0x10] + nyb(addr, 4) + nyb(w, 1) + nyb(data, 4) + [0xF7]
def peek(addr, w):        return [0xF0, 0x7D, 0x20] + nyb(addr, 4) + nyb(w, 1) + [0xF7]
def peekdsp(off):         return [0xF0, 0x7D, 0x21] + nyb(off, 2) + [0xF7]


def decode_value(reply):
    v = 0
    for n in reply[3:-1]:            # nibbles between op and F7
        v = (v << 4) | (n & 0xf)
    return v


class JV:
    def __init__(self, port_match="MiniFuse"):
        self.out = rtmidi.MidiOut()
        self.inp = rtmidi.MidiIn()
        self.inp.ignore_types(sysex=False, timing=True, active_sense=True)  # MUST receive sysex
        oi = next(i for i, p in enumerate(self.out.get_ports()) if port_match in p)
        ii = next(i for i, p in enumerate(self.inp.get_ports()) if port_match in p)
        self.out.open_port(oi)
        self.inp.open_port(ii)
        time.sleep(0.2)

    def _drain(self):
        while self.inp.get_message():
            pass

    def send(self, msg):
        self.out.send_message(msg)

    def read_dsp(self, off, width=4):
        """Universal XP DSP-RAM read via the readback latch, done as separate MIDI
        steps so the 32-bit PRAM/IRAM latch has time to settle.  The atomic op-0x21
        PEEKDSP reads the latch too quickly for the 32-bit regions on real silicon
        (CRAM's 16-bit latch is fast enough; PRAM/IRAM are not)."""
        self.xfer(peek(0x0c000000 + off, 2 if off < 0x3000 else 4))   # trigger
        lo = decode_value(self.xfer(peek(0x0c003910, 2)))
        if off < 0x3000:
            return lo                                                  # CRAM: 16-bit
        hi = decode_value(self.xfer(peek(0x0c003912, 2)))
        return (hi << 16) | lo

    def xfer(self, msg, timeout=1.5):
        self._drain()
        self.out.send_message(msg)
        t0 = time.time()
        while time.time() - t0 < timeout:
            m = self.inp.get_message()
            if m:
                data, _ = m
                if len(data) >= 2 and data[0] == 0xF0 and data[1] == 0x7D:
                    return data
            time.sleep(0.001)
        return None


def main():
    jv = JV()
    ok_all = True

    def show(name, ok, detail=""):
        nonlocal ok_all
        ok_all = ok_all and ok
        print(f"  {name:<24} {'PASS' if ok else 'FAIL'}   {detail}")

    print("== JV-1080 debug ROM bring-up ==")

    # 1. PING — proves real sysex -> hook -> dbg_shim -> reply out, on hardware
    r = jv.xfer(ping())
    show("PING", r == [0xF0, 0x7D, 0x00, 0x00, 0x01, 0xF7],
         "reply " + (" ".join("%02X" % b for b in r) if r else "<none>"))
    if r is None:
        print("\nNo reply to PING. Check: MiniFuse OUT->JV IN and JV OUT->MiniFuse IN both cabled.")
        return 1

    # 2. PEEK a known ROM word (task-0 descriptor entry = midi_task 0x0a02882e)
    r = jv.xfer(peek(0x0a000010, 4))
    v = decode_value(r) if r else None
    show("PEEK ROM 0x0a000010", v == 0x0a02882e, f"= {v:#010x}" if v is not None else "<none>")

    # 3. POKE + PEEK our reserved scratch RAM (0x0901ff04 — firmware never touches it)
    jv.send(poke(0x0901ff04, 4, 0xCAFEF00D)); time.sleep(0.05)
    r = jv.xfer(peek(0x0901ff04, 4))
    v = decode_value(r) if r else None
    show("POKE/PEEK RAM", v == 0xCAFEF00D, f"= {v:#010x}" if v is not None else "<none>")

    # 4. PEEK_DSP — REAL XP silicon reads (latched).  Non-destructive.
    for off, name in [(0x3400, "PRAM[0]"), (0x3600, "PRAM sys"), (0x2c00, "CRAM[0]"),
                      (0x3000, "IRAM1[0]"), (0x3908, "GLOBALcfg")]:
        r = jv.xfer(peekdsp(off))
        v = decode_value(r) if r else None
        print(f"  XP {name:<10} @0x{off:04x} = {v:#010x}" if v is not None else f"  XP {name} <no reply>")

    print("\n== %s ==" % ("ALL CORE TESTS PASS — debug ROM live on hardware" if ok_all else "SOME TESTS FAILED"))
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
