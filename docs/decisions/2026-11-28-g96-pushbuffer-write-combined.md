# GeForce 9500 GT (G96): the BAR1 map write-combined through PAT, and the pushbuffer stores gone from the profile

Date: 2026-11-28

Follows [2026-11-28-g96-3dmark2001se-profile.md](2026-11-28-g96-3dmark2001se-profile.md).
M-Z2T02D (GitHub issue 30), boot 22. Evidence:
[g96-write-combining-m-z2t02d-2026-11-28/](../probe/g96-write-combining-m-z2t02d-2026-11-28/).

## What the profile record got wrong

It read `PatWrite=ok pages=00004000` as the G96's write-combined range.
That key, and `PatReserve`, were the HD 2500's: `V9XBOOT.INI` is
updated key by key, never rewritten, and the intel-gma driver that ran
on this machine before wrote them (`hi=00070101` is its record). The
nvidia family had no write-combining route here at all: the MTRR plan
refused (`Mtrr ... r=11`, `MtrrWrite=refused`: the BIOS uses all eight
variable MTRRs) and the GT218 and G96 rows carried no
`write_combine_fallback`. So every CPU write to video memory on this
card, the desktop's and the pushbuffer's, was uncached.

## The change

`gt218_hw16.c` gives both Tesla rows a fallback that write-combines the
whole BAR1 mapping through PAT entry 4 (`pat.h`, the vbe family's and the
HD 4600's route), not only the 14 MiB the VBE reports: every byte of
the 128 MiB map is the card's memory (the measure hook), and the engine's
reserve, with the pushbuffers, is at its top. The nvidia family now
builds `pat.c` and `runtime.asm`'s `V9X_PAT_ROUTE` verbs; `d3dmode.c` and
`vsync.c` moved to LEAFCODE, as on intel-gma, to keep `_TEXT` within
its budget (it came out 287 bytes over with `pat.c`).

## Measured, boot 22

`V9XBOOT.INI`: `MtrrWrite=refused`, then `PatWrite=ok pages=00008000`,
all 32,768 pages of the map. The clock raise still runs
(`V9XN5CLK.INI` `Result=raised`).

The same profiled 3DMark 2001 SE run as the record before (V9XPROF,
3DMark's defaults, agent-driven), under the same conditions, with
write-combining as the only change:

| | Boot 21, uncached | Boot 22, write-combined |
|---|---|---|
| 3DMark 2001 SE score | 1106 | **2077** |
| Samples | 476,149 | 513,226 |
| `v9x_nv50_push_word`'s store (B04163E0h-B0416403h) | about 89,300 (18.7%) | about 6,600 (1.3%) |
| `v9x_nv50_read32` (B0415010h), polling | 93,835 (19.7%) | 166,463 (32.4%) |
| `v9x_vga_in_vblank` | 43,100 (9.1%) | 65,686 (12.8%) |
| Game 1 Car Chase, low detail | 12.4 fps (boot 13, no profiler) | 23.3 fps |

Also on boot 22, no profiler: V9XD32 at 32 and 16 bpp fails only T5, as
before; V9XGDI `Result=PASS`; Quake 3 demo001 (640x480x32 windowed):
1346 frames, 21.4 s, **62.9 fps**, against 44.1 to 45.8 fps on the boots
before.

These are the effect of one change on one machine, recorded; not a
benchmark comparison between cards.

## What is left

With the stores cheap, the CPU spends a third of the run polling for
the GPU: every 3D batch still waits for the one before to finish
(`v9x_nv50_3d_begin` drains). That is the second item of the profile
record, planned in [nvidia-tesla-3d-push-ring.md](../plans/nvidia-tesla-3d-push-ring.md).

The GeForce 210 shares the rows and gets the fallback too; on W9QI0DAD
its MTRR route worked (2026-11-15), so the fallback would not have run
there. Not run on a GT218 since.

Gates: check-tree, build-host, run-checks.
