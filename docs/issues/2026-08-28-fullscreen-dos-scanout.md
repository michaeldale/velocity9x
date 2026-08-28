# A full-screen DOS box comes back with a corrupt band across the top

Date: 2026-08-28
Status: **open - fix written, untested on hardware**. The stride re-assert
shipped in 0.6.1 without being run on a machine, so 0.6.1 deliberately claims
no fix for this defect. It must reproduce and then pass on the HP Mini 110
before the issue is closed.

Reported by CentaurHauls on an Acer NAV50 (Pineview, Windows Me, vbe package,
build `200cd31`): switching a DOS box to full screen and back leaves the
desktop with a corrupt strip along the top edge, and on that machine the
system then froze. Reproduced on the HP Mini 110 here, which is what makes
this fixable rather than remote.

Photograph and reporter's notes:
`claude\personal\v9x-centaurhauls-acer\source-data\`.

The HP Mini 110 has since been seen to hang on the way *into* a DOS box,
before any prompt is drawn, on both 0.6.0 and 0.6.1 - filed separately as
`docs\issues\2026-08-28-dos-box-entry-hang-gma950.md`. Whether that shares a
cause with this band is unknown; they are kept apart until something says
otherwise.

## What the picture says

The band is confined to the top scanlines. The rest of the desktop is intact,
correctly coloured and correctly positioned. That is the signature of the
scanout origin and stride, not of lost palette, lost extended mode state or a
memory scribble - a wrong pixel format would corrupt the whole frame, and a
wrong aperture would not draw a desktop at all.

## Why it happens

Not what was first written here. The first diagnosis was that the mini-VDD
hooks neither end of the `PRE_HIRES_TO_VGA` / `POST_VGA_TO_HIRES` round trip,
so the master VDD's default VGA restore was the only thing running, and the
fix drafted was a chip-agnostic CRTC snapshot in the mini-VDD. Both halves
were wrong, and the second only because the first was.

**The display driver is re-entered.** `ResetHiResMode` (`src\display16\ddi.c`)
is the Win16 entry the VDD calls on the way back, and `ReEnable` handles the
same case where the mode is unchanged - its comment already says "e.g.
returning from a full-screen DOS box". Both call `V9xHardwareReset`, which
re-issues `4F02h`.

**What that path omits is the stride.** `V9xHardwareEnable` finishes a tier-0
mode set by calling `v9x_vbe_default_pitch`, which asks `4F06h` what the card
is really scanning at and corrects it; a BIOS may accept a mode set and then
scan at a stride of its own choosing (see the 4F06h commentary in
`src\display16\hw\vbe16.c`). `V9xHardwareReset` never called it. So after a
DOS box the mode came back and the stride did not, and a driver drawing at one
stride while the card scans at another is exactly the picture in the
photograph.

The mini-VDD is left alone. Adding speculative register pokes to a VxD when
the defect is a missing call one layer up is the wrong trade, and the reverted
draft is recorded here only so the reasoning is not repeated.

## The fix

`V9xHardwareReset` gains the branch `V9xHardwareEnable` has always had:

```c
if (v9x_hw16.read_aperture == 0 && v9x_vbe_default_pitch() == 0u) {
    return 0u;
}
```

Families with their own `post_mode_set` return before it, as they already did,
so nothing changes for the S3 or Matrox paths.

## What is not yet known

**Whether the stride was in fact what was wrong.** The reasoning is sound and
the asymmetry is real, but no machine has run the fixed build. The band is
consistent with a stride mismatch and inconsistent with a lost pixel format,
which is as far as a photograph goes.

**Whether this addresses the freeze.** It does not obviously. The reporter saw
both corruption and a hang, and this fix is about the picture. The NAV50 also
carried a duplicate devnode - two driver instances on the same adapter, see
`docs\decisions\2026-08-28-pineview-vbe-mode-list.md` finding 3 - which is a
better candidate for a hang and is separately fixed by the `PCI\CC_0300`
compatible id. Neither claim is measured.

## Test to run on the HP Mini 110

1. Boot the vbe package, note the desktop mode.
2. Open a DOS box, switch it to full screen, exit back to Windows.
3. Repeat, and also switch modes in Display Properties first, so the stride
   being re-asserted is not the one the boot mode happened to leave.

Pass is a clean desktop on return. A clean desktop that still hangs later
narrows the freeze to the devnode; a band that persists says the stride was not the
whole of it, and the next suspect is the display start rather than 4F06h.
