#!/usr/bin/env python3
"""
Zero the DSP completely, then re-upload a captured program (dsp_snapshot.py) and check
whether it runs — using the firmware's own known-good Stereo EQ as the test program.

Execution signal (audio-free): the captured dump showed IRAM1/IRAM2 held nonzero L/R
filter state even at idle. So after we zero IRAM1/2 and upload only PRAM/CRAM/IRAM3, if
the EQ actually executes it should RE-DEVELOP nonzero IRAM1/2 state on its own. If it does
-> poked programs execute (proven with a known-good program). Then you can play a note to
hear it.

Reads are single-attempt with abort-on-repeat (never a retry storm, XP_FACTS C11).
Recovery if the sound breaks: send a Program Change -> firmware re-uploads a clean program.

Usage:  python3 restore_dsp.py            # leave IRAM1/2 zeroed (execution test)
        python3 restore_dsp.py full       # also upload the captured IRAM1/2 state
"""
import sys, time
from xp_manual import XP
import dsp_snapshot as S

def restore(xp, upload_iram12=False):
    fails = [0]
    def rd(fn, *a):
        for _ in range(1):
            try:
                v = fn(*a)
                if v is not None:
                    fails[0] = 0; return v
            except Exception:
                pass
        fails[0] += 1
        if fails[0] >= 2:
            raise RuntimeError("2 consecutive read failures — ABORTING (power-cycle if hung).")
        return -1

    print("1) zero the whole DSP...", flush=True)
    xp.zero_dsp()
    print("2) restore exec config (0x3916=7 run)...", flush=True)
    xp.set_config()
    z = sum(1 for i in range(24) for b in (1, 2) if rd(xp.read_iram, b, i))
    print(f"   IRAM1/2 after zero: {z} nonzero (want 0)", flush=True)

    print(f"3) upload program: PRAM({sum(1 for v in S.PRAM if v)}) CRAM({sum(1 for v in S.CRAM if v)}) "
          f"IRAM3({sum(1 for v in S.IRAM3 if v)})...", flush=True)
    for i, v in enumerate(S.PRAM):
        if v: xp.poke(0x3400 + i*4, 4, v)
    for i, v in enumerate(S.CRAM):
        if v: xp.poke(0x2c00 + i*2, 2, v)
    for i, v in enumerate(S.IRAM3):
        if v: xp.poke(0x3200 + i*4, 4, v)
    # if upload_iram12:
    #     for i, v in enumerate(S.IRAM1):
    #         if v: xp.poke(0x3000 + i*4, 4, v)
    #     for i, v in enumerate(S.IRAM2):
    #         if v: xp.poke(0x3100 + i*4, 4, v)

    # print("4) let it run ~0.6s, then check...", flush=True)
    # time.sleep(0.6)

    # pram = [rd(xp.read_pram, i) for i in range(6)]
    # resident = all(pram[i] == (S.PRAM[i] & 0x0fffffff) for i in range(6))
    # print("   PRAM[0:6]:", [f"{v:08x}" for v in pram], "-> resident:", "YES" if resident else "NO", flush=True)

    # dev = [rd(xp.read_iram, b, i) for b in (1, 2) for i in range(24)]
    # nz = sum(1 for v in dev if v and v != -1)
    # print(f"   IRAM1/2 after run: {nz} nonzero", flush=True)
    # if nz > 0:
    #     print("   >>> IRAM1/2 RE-DEVELOPED state -> the EQ is EXECUTING (poked program runs!)", flush=True)
    #     print("       IRAM1[0:6]:", [f"{(rd(xp.read_iram,1,i) & 0xffffff):06x}" for i in range(6)], flush=True)
    # else:
    #     print("   IRAM1/2 stayed zero -> either it needs audio input to build state, or it isn't running.", flush=True)

    # print("\n>>> Now PLAY A NOTE and listen — it should sound (Stereo EQ).", flush=True)
    # print("    Recover a clean firmware program anytime with a Program Change.", flush=True)
    # return {"resident": resident, "iram12_nonzero": nz}


if __name__ == "__main__":
    xp = XP()
    restore(xp, upload_iram12=(len(sys.argv) > 1 and sys.argv[1] == "full"))
