# The MGA-2064W drawing engine under DirectDraw: fill and copy measured on silicon

Date: 2026-10-09. 86Box `Win98SE-Millennium` (boots 33-35) and A8U4I5
(boots 356-357), the physical MGA-2064W, 8 MiB.

Phase 2 of the [plan](../plans/matrox-millennium-family.md). The register
reference is [mga2064w-2d-engine.md](../specifications/mga2064w-2d-engine.md),
reconciled from the MGA-1064SG specification and FreeBE's Millennium driver;
no 2064W databook was available. Everything below is measured.

## What was built

- `src/chipsets/matrox/mga_engine.c`: host-tested register values for the
  per-mode setup (MACCESS pwidth, PLNWT, an open clip window), a TRAP fill
  and a BITBLT copy. Destinations are addressed in xy form through PITCH and
  YDSTORG written per operation; copy sources linearly through AR0/AR3/AR5.
  Destination pitches outside the linearizer's list, or origins off its
  32/64-pixel grid, are declined to the CPU.
- `MGA2D.EXE` + `MGA2D.VXD`: the write probe, in the SIS2D pattern. Seven
  cases into a 1 MiB region 1 MiB below the end of VRAM, each compared with
  the intended image including a guard border.
- The driver: engine type `MGA_2064W` (7; renamed `MGA` the same day, when
  the Millennium II joined the family), a mini-VDD map of BAR0's 16 KiB
  (`V9XMINI_FN_MGA_MMIO_MAP`, 1Ch), the chip's engine hook, and
  `src/display32/engines/eng_mga.c`.

## The write probe on silicon

A8U4I5 under the tier-0 driver, all seven cases at 8, 16 and 32 bpp
(800x600 desktops), with the setup writes: **PASS, 0 mismatches**
(`docs/probe/a8u4i5-mga2064w-engine-2026-10-09/MGA2D-*-SETUP.TXT`). It
settles the open items of the reference:

| Question | Answer |
|---|---|
| Fill right edge | Exclusive: column x+w written 0/7 rows, x+w-1 7/7 (`FXBNDRY = (x+w)<<16 \| x`) |
| Blit right edge | Inclusive: x+w 0/n, x+w-1 n/n (`(x+w-1)<<16 \| x`) |
| Overlap | Down (sdy), right (scanleft) and up-left (forward) all memmove-correct |
| AR0 beyond 18 bits | Works: every copy's source is near 3.6 M pixels (region at 7 MiB) |
| Long rows | 600x3 fill and copy correct |
| Idle | STATUS `00000020h` before, `00000024h` after; bit 16 clear |

**The setup is required.** The same runs with `/nosetup`, straight after a
V9XMSW mode set, fail every case at every depth, and change *nothing* in
the region (`Changed=none`) while the engine runs and goes idle
(7-11 idle-wait reads). Whatever the BIOS leaves in the clip window, plane
mask or MACCESS excludes the region; which of them was not separated. The
desktop read back clean afterwards.

## Two defects the guest found first

**AR5 masked to 18 bits drew the bottom-up copy wrong.** The builder first
wrote a negative source pitch as its documented 18-bit field (`3FC40h` for
-960). In the guest, V9XDDP's display-pitch downward overlap failed
(`OverlapPitchDownPixelOk=0`) while right-to-left passed. Writing the full
sign-extended dword (`FFFFFC40h`), as FreeBE and xf86-video-mga do, fixed
it (`guest/V9XDD-ar5-full.INI`). That 86Box keeps the register at 32 bits
is the likely reading; the silicon was only ever run with the full dword.

**The first validation made the idle poll lie.** `eng_mga.c` writes the
per-mode setup on first validation, and the first caller is often
DirectDraw's non-blocking CANBLT poll. The five writes sat in the FIFO,
STATUS read busy, and the poll answered WASSTILLDRAWING with nothing drawing:
V9XDDP's fill cell never ran (`BltCanHr=0x8876021C`). Validation now drains
the setup, bounded, before claiming the engine (`guest/V9XDD-drain.INI`).

## DirectDraw on the physical card

Boot 357, engine build, 800x600x16 desktop at stride 1920:

- `Acceleration=directdraw-fill-copy`, `Stage=enable-ok`.
- V9XDDP COMPLETE: `BltCanHr=0`, fill `BltFillPixelOk=1`, all four overlap
  cells pixel-correct, 6.9 MiB off-screen.
- HAL snapshot: `EngineType=7`, `CountBltEngine=4` of `CountBlt=6`,
  `EngineIdleTimeouts=0`. The two CPU blits are V9XDDP's 128-pixel
  off-screen surface, whose 256-byte pitch the linearizer has no entry for:
  declined by design.

Back-buffer fill time as V9XDDP measures it: 56 ms on the engine, 73 ms on
the CPU at tier-0, 1 ms under MGAPDX64. The last most likely reports done
before it is - not checked - so it is not a comparison. RPL fills; BLK
block-write fills are untried on this WRAM card.

## Not established

- GDI is still drawn by the CPU (Phase 3).
- Surfaces whose pitch the linearizer cannot address go to the CPU. ylin=1
  linear destinations, or asking DirectDraw for aligned surface pitches,
  would widen this; neither is done.
- Only 800x600 desktops were probed at register level; DirectDraw ran at
  V9XDDP's own exclusive mode.
- The monitor has still not been looked at under this driver.

## Gates

`check-tree`, `build-host` (with `test_mga_engine.c`) and `run-checks`
green. `build-active-package -Family matrox` builds and audits.
