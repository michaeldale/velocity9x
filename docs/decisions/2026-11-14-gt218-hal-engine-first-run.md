# GT218 HAL engine, first run on a card: DirectDraw fill and copy on the 2D engine

Date: 2026-11-14

Step 4.4 of [docs/plans/nvidia-gt218-nv50-engine.md](../plans/nvidia-gt218-nv50-engine.md).
The nvidia family package built clean at d2aee239 (the code of 3751e5b8)
installed on M-BXMFA8 (GeForce 210, GT218 A8, 512 MiB). Evidence in
`build/driver-results/m-bxmfa8-gt218-boot108/`; the files it replaced,
and the baseline, in
`build/driver-results/m-bxmfa8-gt218-boot84/installed-before-4.4/`.

## Install

Boot 107 ran the BAR1-heap tier-0 nvidia driver (V9XHW.INI:
`Acceleration=none`, `EngineCaps=00000000`). V9XDISP.DRV, V9XMINI.VXD,
V9XHAL.DLL and V9XSETP.DLL were backed up, the new four staged at the
root and renamed in by WININIT.INI (plain renames), and the machine
restarted with the agent's reset a minute after the last write. Boot 108
came up to the desktop at 1024x768x16 by itself. All four installed
files hash-match the package, and the staged files and WININIT.INI are
gone. V9XHW.INI: `Acceleration=directdraw-fill-copy`,
`EngineCaps=00000003`, `GdiAcceleration=none`.

## DirectDraw, before and after

V9XDDP (d2aee239's) on boot 107, then on boot 108: `Result=COMPLETE`
both times, 330 keys each. Six keys differ that are not timings; the one
that matters is **`GblHalVidMemTotal` `07E80000` to `07D80000`, one
megabyte less**: the engine's reserve came off the heap. The others are
raw pointer words.

Pixel checks on boot 108, at the probe's own 16 bpp mode:
`BltFillPixelOk`, `SrcCopyPixelOk`, `KeySrcFillPixelOk`,
`OverlapDownPixelOk`, `OverlapRightPixelOk`, `OverlapPitchDownPixelOk`
and `OverlapPitchRightPixelOk` are all 1. `FlipPixelOk` is 0, as before
(this row claims no flip).

## That it was the engine, not the CPU fallback

A correct pixel does not say who drew it, so the card was asked.
`V9XN5CH /check` reads registers and writes none (it found its own
window's words unrelated to this channel and stopped, as designed):

| Register | Value | Meaning |
| --- | --- | --- |
| 2604h | `8007F042` | channel 1's RAMFC at 07F04200h, in the reserve at 07F00000h |
| 32F4h / 32ECh | `00007F20` / 1 | the driver's runlist, one channel |
| 40032Ch | `80007F00` | PGRAPH holds this channel's context |
| 3334h / 3330h | 8 / 8 | **eight IB entries fetched**: the 502D bind and seven operations |
| 3244h / 3240h | `07F220AC` both | the pusher consumed a 43-word push, the copy's length |
| 3228h | `000068E0` | no pusher error (bits 31:29) |
| 2100h, 400100h | 0, 0 | no PFIFO or PGRAPH interrupt |
| 100C90h | `000D0FFF` | trap enabled, none latched |
| 1704h | 0 | no BAR channel, as nv50_init.c leaves it |

Seven operations is the probe's seven pixel-checked blits. The driver
snapshot (V9XSNA7.INI, same boot and build) has `EngineType=15`,
`EngineResets=1`, `EngineIdleTimeouts=0`, `EngineFifoTimeouts=0`.

This is also the first run of a **copy between two surfaces**
(`v9x_nv50_push_blit`), which the channel probe never made:
`SrcCopyPixelOk=1`.

## Mode changes

V9XGDI `/auto`: PASS. V9XMSW `/cycle:4` and `/depth:4`: PASS, 4 of 4
each. V9XDDP again afterwards: COMPLETE, the same seven pixel checks 1.
A later snapshot shows `EnableCount=16`, `EngineResets=3`, timeouts 0,
the engine still valid: each Enable clears the engine's validated flag
(dd16.c republishes the record), so the next DirectDraw use brings the
channel up again from scratch. Three V9XDDP runs, three bring-ups, each
ending with IB get = put = 8 and no fault.

That is more work than the 4.0 measurement says a mode set needs (the
channel survives one), but it is the simple and safe behaviour and it
ran clean; it stays unless it shows up as a cost.

## Not covered

- **8 and 32 bpp through the HAL.** V9XDDP sets its own 16 bpp mode; a
  run with the desktop at 8 bpp still tested 16. The family's modes are
  8 and 16 bpp, so 32 bpp cannot be reached on this package at all. The
  channel probe measured the hardware at all three.
- A full-screen DOS box, standby and resume.
- The last snapshot did not refresh (`SnapshotReused=1`, counters
  unchanged, `ModeBpp=8` after the desktop was back at 16), so the final
  counters are from a little before the end of the session.
- V9XGDI and V9XMSW on the guest are an older build's (803ae36a); the
  package's own were not pushed, because of a path slip on the host.
- Nothing was watched on the monitor. The owner has not reported on
  the picture.

The machine is left on this build, boot 108, desktop 1024x768x16.
