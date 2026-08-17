# Velocity9x against the retail S3 driver, Ironfield RTS

Date: 2026-08-17
Status: accepted

The first measurement of this driver against the stock one. Every Ironfield
number recorded before today compares Velocity9x with earlier Velocity9x
(`2026-08-14-virge-blitter.md`, `2026-08-16-restructure-baseline.md`), which
tracks regressions but never answered the obvious question: how far off the
retail driver are we?

## Result

| Driver | FPS | Frames / elapsed | Exact |
|---|---|---|---|
| Retail S3 `s3v.drv` on `Win98SE-Native-S3` | 19 | 305 / 15346 ms | 19.9 f/s |
| Velocity9x ViRGE on `Win86SE` | 18 | 310 / 16603 ms | 18.7 f/s |

**About 6% behind the retail driver**, on the presentation path that actually
reaches our blitter.

## Why this comparison is fair

- **Same binary.** `IRONFIELD.EXE` was copied out of the Velocity9x guest and
  into the reference guest, CRC32 `C4D60A12` on both, rather than installed
  separately. Same `ironfield.ini` too, CRC32 `6F33733B`.
- **Same command line**, the one the earlier baselines used:
  `-benchmark -fullscreen -renderer-video`.
- **Same emulated card.** Both guests are 86Box `virge_dx_pci`; the reference
  guest differs only in carrying S3's retail driver instead of ours.
- **Same mode.** Both logs record 640x480 - Ironfield sets its own fullscreen
  mode, so the desktop depth the guests happen to sit at does not enter into it.
- **Same renderer path.** `RendererMode=3`, `UseBltFast=1`, MMX on, music off,
  and both logs confirm `Video + BltFast (BltFast active)`.

The `-benchmark` switch also suppresses the launcher and completion dialogs, so
neither run raised a modal window that could wedge the remote agent.

## Reading it

The honest summary is parity within a few per cent, not a win and not a
shortfall worth acting on. Two things are worth noting before anyone quotes it:

- The elapsed times differ - 15.3 s against 16.6 s for a nominal 15 s run - so
  the integer FPS the game prints is doing some rounding. Frames over elapsed
  time is the fairer figure and gives 19.9 against 18.7, which is the same
  conclusion.
- This measures one path. `Video + BltFast` is the one that dispatches to our
  `Blt` callback once per frame, which is why it is the interesting one, but
  Ironfield's other two renderers never reach the driver's blitter at all
  (direct backbuffer does not blit, and system-to-video stays with the HEL
  because `dwSVBCaps` is zero). A comparison on those would measure the HEL and
  the emulator, not this driver.

## What this does not cover

The Trio64 has no retail-driver reference guest, so its 16 FPS still has nothing
to be compared against. Neither does tier-0: the generic VBE path has no
acceleration at all by construction, so an Ironfield number there would measure
`blt_cpu.c`, and the 3 FPS already recorded for the CPU copy in
`2026-08-14-virge-blitter.md` is that measurement in all but name.

Installing Ironfield put a game directory on the reference guest. Nothing else
about it changed - it carries no Velocity9x binaries and remains a stock-driver
reference, which is the property that makes it useful.
