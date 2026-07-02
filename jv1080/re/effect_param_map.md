# JV-1080 effect parameter → CRAM/PRAM slot map

Derived empirically in the emulator: instrument `xp_w` (log every CRAM/PRAM write), boot to
steady state, then send Roland **DT1** sysex `F0 41 10 6A 12 03 00 00 <xx> <val> <sum> F7` to change
one Patch-mode temporary-patch parameter at a time and record which slots the firmware rewrites.
Tooling: `jv1080re` (instrumented MAME build) + a python virtual MIDI port. Boot patch RFX = Stereo EQ.

DT1 base `03 00 00 xx` = Patch-mode temporary patch common. `xx` is the parameter offset.
Method note: sweep each `xx` with two extreme values (0x00 and 0x7f) so every coefficient-affecting
param is guaranteed to change; slots that move are that parameter's footprint.

## EFX / RFX (Stereo EQ) — CRAM 0–103, stereo-mirrored (R slot = L slot + 36)

| `xx` | CRAM (L / R) | interpretation `[H]` |
|---|---|---|
| `0x0c` | ALL 0–103 + PRAM 0–103 | **EFX TYPE** — swaps the whole RFX microcode+coeffs |
| `0x0e` | 12,13,14 / 48,49,50 | one band's coefficients |
| `0x10` | 15,16,17 / 51,52,53 | another band's coefficients |
| `0x11,0x12,0x13` | 1,18,19,20,21,23,25,37 / 54,55,56,57,59,61 | **one band's Freq/Gain/Q** (3 params → identical coeff set) |
| `0x14,0x15,0x16` | 5,26,27,28,29,31,33,41 / 62,63,64,65,67,69 | **another band's Freq/Gain/Q** |

Key structural facts (confirmed):
- Changing Freq **or** Gain **or** Q of a band rewrites the *same* set of that band's biquad
  coefficients (`0x11/0x12/0x13` identical; `0x14/0x15/0x16` identical).
- Every band is **L/R mirrored**: right-channel coeff slot = left slot + 36.
- The two simple bands (`0x0e`, `0x10`) touch 3 coeffs/side; the two parametric bands
  (`0x11–13`, `0x14–16`) touch ~8/side (a fuller biquad + shared taps like slot 1, 5, 37, 41).

## Reverb / Chorus — CRAM 104–255 (system region)

| `xx` | CRAM (SYS) / PRAM |
|---|---|
| `0x1a` | 246, 249 |
| `0x1b` | 224 |
| `0x1c` | 222 |
| `0x22` | 111 |
| `0x23` | 114, 125 |
| `0x24` | 115, 126 |
| `0x25` | 225 |
| `0x27` | big set (142…188) + **PRAM 64 slots** → reverb/chorus **TYPE** swap |
| `0x29` | 165, 181 |
| `0x2a` | 161, 162, 178, 179 |

## What this enables

We can now correlate a parameter's *value* with the resulting *coefficient values* (sweep one
param through its range, read the CRAM it writes) to decode the CRAM fixed-point format and the
biquad math — and, because each band's coeffs are known, attribute PRAM MAC ops to bands. Not-yet-
mapped offsets (`0x0d,0x0f,0x17,0x18,0x19,0x1d–21,0x26,0x28,0x2b+`) are likely discrete switches /
output routing that don't change coefficients, or need a value sweep — TBD.
