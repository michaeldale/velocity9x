# M-GB8KRE: V9XD32's T15 found 796 heap words zeroed, once, under the RS690 Direct3D HAL

Date: 2026-11-05. Machine: M-GB8KRE (RS690 1002:791F, UMA, 2 GiB, Win98
SE). Build: branch `ati-rs690`, 8e9654c2-dirty, with the register-write
Direct3D HAL (`docs/decisions/2026-11-05-rs690-d3d-hal-first-boot.md`).

Status: fixed on the branch, not committed (boot 71). A 3D draw that starts
while a 2D fill is still running sends colour-cache lines to wrong
addresses: some land elsewhere in the frame buffer, some in system RAM
near physical 06000000h. `WAIT_UNTIL` in the register FIFO does not hold
the 3D engine back with the CP off. A CPU idle poll does. The HAL now
polls for idle before every 3D draw (`v9x_rs690_3d_begin`). See "Cause,
boots 68-71" below. Why the bad lines land at that physical window is
not known.

## What was seen

Boot e's first V9XD32 run (640x480x32, the first V9XD32 run after the
restart), in `docs/probe/rs690-d3d-hal-2026-11-05/V9XD32-boot-e-run5-t15-heap-zeroed.TXT`:

- `T15HeapBad=0x31C`: 796 words of the 32 MiB fill read 00000000.
- The first eight are contiguous from linear 01335740h, and the last
  is 013C805Ch.
- First seen at stage `12b-t14-after-child`, after T14's child process
  created its own device and surfaces, drew, and exited.
- The map and file regions were clean.
- Page-frame numbers are not known: PFRM.VXD was not on the machine
  (`pte FFFFFFFF`).

Runs 6, 7 and 8 at 32 bpp and run 9 at 16 bpp, same boot, all passed T15
with Failures 0. Under the signed tier-0 `vbe` driver (2026-11-03) T15
passed at both depths.

## Why it matters

On this UMA chip the GPU's MC addresses are physical addresses. The
frame buffer is the carve-out at 78000000h, and below it is ordinary
system RAM. A colour, Z or 2D destination address outside the carve-out
would write zeros, or a clear's colour, into some process's pages.
Whole words zeroed in a band is a write, not the single flipped bit of
A8U4I5's DRAM fault (`2026-10-25-rage128-t15-sentinel-corruption.md`).

## Hunt, 2026-11-05 afternoon (boots 61-67)

Captures: `docs/probe/rs690-d3d-hal-2026-11-05/t15-hunt/`.

**Reproducible, per boot.**

- Boots 62, 64 and 67: every full V9XD32 run failed T15 (15 of 15 in
  boot 62).
- Boot 66: three clean runs.
- The damage is always first seen right after T14's child.
- It persists with `/t14nomode`, `/t14clean` and `/t14release3d`, so it
  is neither the child's mode set nor DirectDraw's cleanup of a dead
  process.

**A fixed physical window.** With PFRM.VXD beside V9XD32, every bad
word in every run lay at physical 06001390h-06098A9Ch, whatever its
linear address. That is the size of the first 600 KiB of a frame buffer
surface.

**The child's own pixels, written by the 3D engine.** A bare V9XD32
(`/t15bare /t15watch`) holding its heap on those frames was hit by V9XD32
`/child` alone: 200 logged words. Of those:

- 141 were zeroed whole, the child's DDBLT_COLORFILL of 0.
- 45 kept their original top byte with the low three bytes zeroed.
- 14 read xx00FF00h with the original top byte: the child's green
  triangles (FF00FF00h).

Three bytes written with the fourth kept is what this HAL's RB3D does on
an XRGB target (RB3D_COLOR_CHANNEL_MASK 7). No CPU copy keeps the
destination's top byte.

**Only the hardware Direct3D path does it.**

| Run | Hits |
|---|---|
| The register-only 3D probe (V9XRS3D) | none |
| V9XDDP, DirectDraw fills and flips | none |
| Two V9XMSW mode changes | none |
| Software Direct3D (`[Velocity9x] Direct3D=2`), boot 63 | three V9XD32 runs and two children clean |
| The same tests with `Direct3D=0`, boot 64 (control) | everything failed again |

**The driver programs correct addresses.** The HAL now records the
lowest and highest colour MC address of every draw and every 2D fill
destination (V9XTRACE `M64FogDropped`, `ScanSampleOffset`,
`IndexedDeclineRangeMax`, `IndexedDrawnRangeMax`). In boot 67, a
corrupting boot, all of them lay in 78000000h-782EE740h, base
78000000h.

**The memory controller maps them where expected.** V9XRS3D `/mc`
reads the MC registers through MC_INDEX / MC_DATA; its VxD refuses an
index write with MC_IND_WR_EN. In both boot 66 (clean) and boot 67
(corrupting):

- `MCCFG_FB_LOCATION` = 7FFF7800h, a window of 78000000h-7FFFFFFFh.
- `K8_FB_LOCATION` = 78000000h, high byte 0: the UMA frame buffer is
  at physical 78000000h.

**So the contradiction stands.** The child's 3D and 2D writes reach
physical 0600xxxxh while every address programmed was in the frame
buffer, and the frame buffer is not there. The child at 16 bpp, and the
child at 32 bpp in boot 67, caused no hits in a watcher whose frames, in
that boot, evidently did not cover the window. That does not separate
anything.

## Cause, boots 68-71 (2026-11-05 evening)

Captures: `docs/probe/rs690-d3d-hal-2026-11-05/t15-cause/`.

**The judge.** V9XRS3D `/physscan` reads physical RAM through the VxD's
read-only window (op 10, `_MapPhysToLinear`). For each page it reports
a checksum and counts of green (`xx00FF00h`) and zero words. Boots 68-70
scanned 160 pages from 06000000h. From boot 70 on, it scanned 2048 pages
from 05C00000h. The scans run before and after two V9XD32 `/child`
runs.

| Boot | HAL | Green words after child 1, child 2 |
|---|---|---|
| 68 | no 3D register writes (diagnostic build) | 0, 0 |
| 69 | normal | 72, 157 |
| 70 | normal, with Linux `r420_pipes_init` values in the 3D init | 49, 143 |

So it is the 3D engine. The Linux pipe setup does not change it.

**The register probe draws cleanly in the probe's own order.**
`/strayat=<offset>` draws 40 batches of the child's green 12x12 quads:

- TL vertices, the XRGB channel mask, 32 bpp, 2560-byte pitch;
- each batch is a CPU idle wait, the state, the quads, the finish, and
  another CPU idle wait.

This added no green words at any of these placements:

- fb offset 1000000h, 12C000h or 0 on a 16 bpp desktop;
- offset 0 on a 32 bpp desktop;
- with `/strayfill`, a 2D fill of the target before each batch, with
  a CPU idle wait between fill and draw.

Screenshots show the quads landing at offset 0
(`stray0-shot.png`, `stray0-32-shot.png`).

**In the HAL's order it reproduces, with no Direct3D involved.**
`/strayhal` copies `d3d_rs690.c` and `eng_rs690.c`. It runs four frames.
Each frame is:

- `wait_idle` and its 2D destination cache flush;
- a 2D fill of the 640x480 target;
- eight draws of the 170-quad list. Each draw is `WAIT_UNTIL` 2D
  idle-clean, the state, triangles in runs of 64 behind `VF_CNTL`, and
  the finish, with no CPU wait between draws.

Two runs added 84 and 102 green words. The frame shows quads with short
horizontal runs missing, plus green specks scattered through the rest
of the frame buffer (`strayhal-shot.png`, `bis-b2-run4.png`).

**Bisection, one 4-frame run each, new green words:**

| Variant | New |
|---|---|
| as the HAL | 93, 117 |
| CPU idle wait after every draw (`/hidle`) | 28 |
| no 2D cache flush (`/hnoflush`) | 97 |
| every frame at offset 0 (`/hzero`) | 106 |
| **no 2D fill** (`/hnofill`) | **0** |
| fill, then the 2D cache flush in the FIFO (`/hfillflush`) | 113 |
| draws' `WAIT_UNTIL` = 2D, 3D and host idle-clean, 70000h (`/hwaitall`) | 113 |
| **fill, then a CPU idle wait** (`/hfillidle`) | **0, 0** |

The trigger is the fill followed by 3D before the 2D engine is idle.
Neither form of `WAIT_UNTIL` written over MMIO stops it, and neither
does the 2D flush. A CPU poll of `RBBM_STATUS` does: 64 free entries
and bit 31 clear, then the 2D flush. The `/hfillidle` frame is clean
(`bis-b2-run5.png`). The flip, the CRTC and the second process are not
needed.

This kills these hypotheses:

- drawing at fb offset 0, MC exactly 78000000h;
- the flip;
- a second process's state;
- the XRGB mask alone;
- a bad MC address.

Linux sends its 2D/3D switch `WAIT_UNTIL` through the CP. Whether
xf86-video-ati's MMIO path hits the same thing on this chip has not
been checked.

**The fix.** `v9x_rs690_3d_begin` (`eng_rs690.c`) now calls the
engine's `wait_idle(1)` before each draw. That is the same poll and 2D
flush that blits, Lock and flips already use. Boot 71, fixed HAL:

- Five `/child` runs: the 8 MiB window stayed all zero words
  (`fix-a.TXT`, `fix-c1..3.TXT`). In boot 70 it took green and other
  values.
- V9XD32, full: four runs at 32 bpp and two at `/bpp16`. Each had
  T14 PASS, T15 heap/map/file 0 and `Failures=0`. Two more runs were
  labelled 16 because the desktop was at 16 bpp, but V9XD32 set
  32 bpp for them.
- The V9XTRACE snapshot (`fix71-snap.INI`) shows 13702 hardware draws,
  798628 triangles, 0 refused and 0 unrenderable.

**Still unexplained.** Why misdirected lines land at physical
06000000h-0609xxxxh and not in the carve-out. It may be the colour
cache's tag losing its high address bits, but that has not been
measured. The cost of the idle wait per draw has not been measured
either.

## What would have settled it (before boots 68-71)

- **Is this the 3D engine at all?** A HAL build that maps and counts
  draws but writes no 3D registers. If a corrupting boot still corrupts,
  the 3D engine is cleared.
- **Is it the XRGB channel mask's read-modify-write?** Turn
  `x_channel` off: corruption gone in a corrupting boot would implicate
  the RB3D destination cache.
- **What register state does the chip hold after the child?** Read back
  `RB3D_COLOROFFSET0` and `RB3D_CCTL` (if readable), comparing the
  parent's state with the child's.
- **Which watcher frames are covered?** Each of these needs a boot where
  the watcher's heap holds 0600xxxxh frames, or V9XD32's own T15 as the
  judge (it fails every run in a corrupting boot). Use `/t15frame=` to
  confirm coverage first.
- A mechanism unknown to Linux and Mesa is possible: they never run
  two clients' 3D on this chip without the CP.
