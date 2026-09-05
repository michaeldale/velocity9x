# CrystalMark Retro on the 86Box Trio64 guest: text on against text off

Date: 2026-09-06
Status: measured, two runs per column, on an emulator. Directional only;
BARRY was unreachable and is where this has to be repeated.

The first measurement of [build 005](2026-09-06-gdi-accel-005-text.md)
against the benchmark that motivated it. The two BARRY records
([baseline](2026-08-27-crystalmark-barry-baseline.md),
[accelerated](2026-08-27-crystalmark-barry-accelerated.md)) are on physical
silicon and their numbers are not comparable to these. What is comparable is
the two columns below against each other: same guest, same boot count apart
from one reboot, same mode, one INI key changed.

## Configuration

| | |
|---|---|
| Host | 86Box 6.0, `Win98SE-Trio64` profile, agent `:9871`, emulated S3 Trio32/64 86C764, 4 MB |
| OS | Windows 98 SE, 4.10 build 2222, emulated Pentium MMX, 128 MB |
| Driver | Velocity9x `29fb2d8` (build 005), `GdiAccel=1`, fill, copy and overlap on |
| Mode | **800x600x16**, set through the registry and a reboot |
| Tool | CrystalMark Retro 2.1.0 for 9x, pushed to `C:\CMR` |
| Tests | 2D only, twice per column. CPU and Disk not run: on an emulator sharing a host they measure the host's moment, not the driver |
| Column A | `GdiAccelText=1`, `GdiAcceleration=gdi-fill-copy-overlap-text` |
| Column B | `GdiAccelText=0`, `GdiAcceleration=gdi-fill-copy-overlap`, after a reboot |

## Scores

| Test | Text on, run 1 | Text on, run 2 | Text off, run 1 | Text off, run 2 |
|---|---|---|---|---|
| **Text** | **4** | **4** | **2** | **2** |
| Square | 488 | 494 | 489 | 492 |
| Circle | 214 | 215 | 212 | 214 |
| Image | 150 | 151 | 150 | 151 |

Screenshots:
[text on](../images/crystalmark-86box-trio64-text-on-800x600x16.png),
[text off](../images/crystalmark-86box-trio64-text-off-800x600x16.png).

## Reading it

**Text doubled and nothing else moved.** The three other 2D scores vary by at
most six points across four runs with no pattern between the columns, which is
what the controls are for: Square, Circle and Image do not touch ordinal 14,
and a change in them would have meant the environment moved rather than the
driver. They did not. Text went from 2 to 4 in both pairs.

**The score is still tiny, and the emulator is the wrong instrument to say how
tiny it should be.** Every PIX_TRANS word is a port write, every eight of them
a port read for the FIFO poll, and every string an idle wait at each end.
86Box charges each of those a fixed emulated cost that has nothing to do with
what a Trio64 on a PCI bus charges. So the ratio here says the path is faster
than the DIB Engine's software expansion in this environment, and says nothing
about the ratio on hardware in either direction. BARRY's Text scored 3 with
software text; what it scores with `GdiAccelText=1` is the number this work is
for, and it has not been taken.

**Square and Image are higher than BARRY's** (488 against 275, 150 against 98)
and Circle too (214 against 134). That is the emulated machine, not the
driver: a different CPU model, host-speed memory, and a fill engine that
completes in the emulator's own time. It is listed so nobody reads it as a
gain.

## An observation, not a result

CrystalMark's GPU line reads `Velocity9x S3 ViRGE/DX 86C375` on this guest,
whose adapter is the Trio64 (`V9XHW.INI` says `S3 Trio32/64 86C764`). The
string is the display class key's `DriverDesc`, which this cloned guest has
carried since an earlier install; the family INF names the chip correctly
([the merge record](2026-08-16-s3-family-merge.md)). Cosmetic, and the
guest's, not the driver's.

## State left behind

The guest's `SYSTEM.INI` has `GdiAccelText=0` - the shipping default - and
the mode is 800x600x16. `C:\CMR` stays on the guest for the next run.
