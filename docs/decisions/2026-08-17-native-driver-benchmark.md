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

## Tier-0 on the Mach64, same day, same binary

The `vbe` package on `Win98SE-Mach64VT2`, all three renderer paths:

| Renderer | Tier-0 Mach64 | Velocity9x ViRGE | What it exercises |
|---|---|---|---|
| Video + `BltFast` | **6 FPS**, 106 frames | 18 FPS, 310 frames | our `Blt` callback, once per frame |
| Direct backbuffer | **31 FPS**, 528 frames | 19 FPS | no blit at all; raw writes into VRAM |
| System RAM | **39 FPS**, 665 frames | 27 FPS | the HEL's system-to-video copy |

Two things fall out of this, and they point in opposite directions.

**The blitter is worth about 3x on the path that matters.** Tier-0 gets 6 FPS
where the ViRGE's engine gets 18, on identical work. That gap *is* tier-0: no
acceleration by construction, so `BltFast` lands in `blt_cpu.c` and every byte
of a 640x480x16 frame crosses the aperture twice. It is the clearest argument
yet for a native `eng_mach64.c`, and it puts a number on what that would buy.

**But the Mach64 guest is the faster machine on every path that avoids our
blitter** - 31 against 19, and 39 against 27. Both guests are the same `ym430tx`
Pentium MMX with 128 MiB, so that is 86Box's `mach64vt2` framebuffer being
cheaper to write than its `virge_dx_pci` one, not anything about these drivers.
It also means the 6-versus-18 comparison understates the blitter: on equally
fast silicon the CPU path would look worse still, and cross-card FPS numbers
here measure the emulator as much as the driver.

**Caveat that matters more than the numbers.** These runs happened while D5 is
open, and D5 makes 16 bpp scanout wrong on this guest. Ironfield runs
640x480x16, so the display was garbled for all three. The throughput figures are
still real - the game rendered and presented the same work either way, and the
frame counts are self-consistent - but nobody watching would have called this a
working benchmark, and the numbers should be re-taken once D5 is fixed before
they are quoted as tier-0's performance.

## What this does not cover

The Trio64 has no retail-driver reference guest, so its 16 FPS still has nothing
to be compared against.

**The Mach64 has no native comparison yet either, and it is the obvious next
measurement.** `docs\vm-environment.md` records that after a card change Windows
binds ATI's own in-box driver on this guest - `DXATI.INF`, "ATI Graphics Pro
Turbo PCI (atim64 - VT)" at 1024x768x16 - so a stock-driver number for this card
is obtainable. It needs the display driver swapped back through the GUI and then
swapped again, on a guest that is snapshotted, so it is work rather than a
blocker. Until that exists, the 6 FPS above says what tier-0 costs against our
own accelerated driver on a different card, which is not the same question.

Installing Ironfield put a game directory on the reference guest. Nothing else
about it changed - it carries no Velocity9x binaries and remains a stock-driver
reference, which is the property that makes it useful.
