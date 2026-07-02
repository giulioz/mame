#!/usr/bin/env python3
"""
Full read-only dump of the XP DSP state (steady state, after boot, idle).

Reads PRAM / CRAM / IRAM1 / IRAM2 / IRAM3 / IRAM3-targets via the readback latch.
Does NOT poke anything and does NOT touch 0x3916 — pure observation. Also does not
read the 0x3918/0x391a IRQ regs (C11). Auto-detects the live MIDI port.

Usage:  python3 dump_dsp.py [outfile]
"""
import sys, time
from jvdebug import JV, ping

def open_live(cands=("UM-ONE", "MiniFuse")):
    for name in cands:
        for _ in range(3):
            jv = None
            try:
                jv = JV(port_match=name)
                if jv.xfer(ping()):
                    print(f"connected on '{name}'", flush=True)
                    return jv
            except Exception:
                pass
            if jv is not None:
                try: jv.out.close_port(); jv.inp.close_port()
                except Exception: pass
            time.sleep(0.3)
    raise RuntimeError("no live JV-1080 port found")

def dump(jv):
    # Single attempt per read (NO retry storm). Abort on 2 consecutive failures.
    # Every 64 reads: PING (liveness) + brief pause (let the firmware drain — tests/avoids
    # any cumulative-volume hang). If PING fails, abort before hammering.
    from jvdebug import ping
    fails = [0]; nread = [0]
    def checkpoint():
        if not jv.xfer(ping(), timeout=1.5):
            raise RuntimeError(f"firmware stopped answering PING after {nread[0]} reads — ABORTING "
                               "(power-cycle). This isolates the hang to read volume/rate.")
        time.sleep(0.25)
    def safe(off, width):
        nread[0] += 1
        if nread[0] % 64 == 0:
            checkpoint()
        try:
            v = jv.read_dsp(off, width)
        except Exception:
            v = None
        if v is None:
            fails[0] += 1
            if fails[0] >= 2:
                raise RuntimeError(f"2 consecutive read failures near {off:#06x} after {nread[0]} "
                                   "reads — ABORTING (power-cycle).")
            return -1
        fails[0] = 0
        return v
    rd32 = lambda off: safe(off, 4)
    rd16 = lambda off: safe(off, 2)
    out = {}
    # NOTE: do NOT read 0x3300 (IRAM3 ramp targets) — trigger-reading it HANGS the firmware
    # (write-only region; same hazard class as 0x39xx, see XP_FACTS C11). Skipped on purpose.
    print("PRAM (288)...", flush=True); out["pram"]  = [rd32(0x3400 + i*4) for i in range(288)]
    print("CRAM (288)...", flush=True); out["cram"]  = [rd16(0x2c00 + i*2) for i in range(288)]
    print("IRAM1 (64)...", flush=True); out["iram1"] = [rd32(0x3000 + i*4) for i in range(64)]
    print("IRAM2 (64)...", flush=True); out["iram2"] = [rd32(0x3100 + i*4) for i in range(64)]
    print("IRAM3 (64)...", flush=True); out["iram3"] = [rd32(0x3200 + i*4) for i in range(64)]
    return out

def write_report(out, path):
    with open(path, "w") as f:
        def sec(name, base, vals, w):
            nz = sum(1 for v in vals if v and v != -1)
            bad = sum(1 for v in vals if v == -1)
            f.write(f"\n## {name}  (base 0x{base:04x}, {len(vals)} slots, {nz} nonzero{f', {bad} UNREADABLE' if bad else ''})\n")
            for i, v in enumerate(vals):
                if v or (name == "PRAM" and i < 8):
                    cell = "UNREADABLE" if v == -1 else f"{v:0{w*2}x}"
                    f.write(f"  {i:3d}  0x{base+i*(4 if w==4 else 2):04x}  {cell}\n")
        f.write("# XP DSP steady-state dump (after boot, idle)\n")
        sec("PRAM",  0x3400, out["pram"],  4)
        sec("CRAM",  0x2c00, out["cram"],  2)
        sec("IRAM1", 0x3000, out["iram1"], 4)
        sec("IRAM2", 0x3100, out["iram2"], 4)
        sec("IRAM3", 0x3200, out["iram3"], 4)
    print(f"written {path}", flush=True)

def summarize(out):
    for name in ("pram", "cram", "iram1", "iram2", "iram3"):
        vals = out[name]
        nz = [i for i, v in enumerate(vals) if v]
        print(f"  {name:6}: {len(nz)}/{len(vals)} nonzero" + (f", last={nz[-1]}" if nz else ""), flush=True)
    print("  PRAM[0:8]:", [f"{v:08x}" for v in out["pram"][:8]], flush=True)
    print("  CRAM[0:8]:", [f"{v:04x}" for v in out["cram"][:8]], flush=True)

if __name__ == "__main__":
    outfile = sys.argv[1] if len(sys.argv) > 1 else \
        "/Users/giuliozausa/personal/programming/mame/jv1080/re/dsp_dump_steady.txt"
    jv = open_live()
    t = time.time()
    out = dump(jv)
    print(f"dump took {time.time()-t:.1f}s", flush=True)
    summarize(out)
    write_report(out, outfile)
