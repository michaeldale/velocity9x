# FlipPixelOk reads 0 on every target, including ones whose flips work

Filed: 2026-09-02
Status: open, instrument defect rather than driver defect
Affects: `tools\diag\ddraw_probe_win32.c`, the `FlipPixelOk` and `Flip20Ms` keys

## What it does

The check fills the back buffer red, flips, reads pixel (100,100) off the screen
DC with `GetPixel`, then repeats in blue, and passes only if both readbacks
match. The intent is to catch a flip that never moves the display and one stuck
on a single page.

`GetPixel` on the screen DC reads **GDI's page**. Once the driver performs a
real hardware flip, GDI keeps drawing to and reading from the page it owns, so
the readback cannot see the flipped-to buffer. **The check therefore fails
precisely when the flip works.**

The probe already knows this. The comment immediately above it reads:

> `/hold` pauses on each verification color so the emulated scanout can be
> captured from the host: GDI readback only sees the fixed GDI page once real
> flips are in play.

So `/hold` plus an external capture is the supported way to answer the question,
and the unswitched key is left publishing a verdict that cannot be right.

## Measured, 2026-09-02

Five targets, three chips, two driver families, one of them silicon:

| Target | `FlipPixelOk` | `Flip20Ms` | `FlipMaxMs` |
|---|---|---|---|
| `Win98SE-Trio64` (86Box, s3, software D3D) | 0 | 1 | 1 |
| `Win86SE` (86Box, ViRGE/DX, hardware D3D) | 0 | 1 | 0 |
| `Win98SE-Mach64VT2` (86Box, ati) | 0 | 0 | 0 |
| A8U4I5 (physical Trio3D, `vbe` tier-0) | 0 | 1 | 1 |
| A8U4I5 (physical Trio3D, `s3`, hardware D3D) | 0 | 0 | 0 |

`FlipHr=0x00000000` on all five - the flip itself reports success everywhere.

## Why this is filed

A key that is zero on every target the project has, including the ViRGE whose
page flipping is a headline feature and has been benchmarked, carries no
information. Worse, it reads as a defect: in this session it was taken as the
explanation for a black screen during a Final Reality run on real hardware, and
a wrong diagnosis was stated before the probe's own comment corrected it. That
is the second key in two days whose face value misled - the first being the
ZRGB1555 colour constants
([2026-09-01](2026-09-01-virge-3d-writes-zrgb1555.md)).

`Flip20Ms=0` and `FlipMaxMs=0` are the same artefact rather than a second
symptom: twenty flips completing in under a millisecond is what a display-start
write costs, not evidence that nothing happened.

## What it should be

Not simply deleted - the question it asks is a real one. Options, none costed:

- **Report it as not-applicable rather than as a failure.** A third state, so a
  reader can tell "the flip did not present" from "this check cannot see the
  answer". The cheapest fix and the one that stops the misreading.
- **Read the scanout rather than GDI**, by locking the primary surface through
  DirectDraw after the flip instead of going through a screen DC. Whether that
  sees the flipped page depends on what the driver returns for a locked primary
  and is worth establishing before relying on it.
- **Keep `/hold` as the answer** and make the unswitched key say so.

Nothing here should change until it is decided which of those is wanted; the
present state is at least consistent across every target.
