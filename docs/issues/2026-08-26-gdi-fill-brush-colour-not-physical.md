# The GDI solid fill takes a logical RGB where the engine wants a physical colour

Date: 2026-08-26
Status: open, diagnosed, blocks `gdi-accel-001`
Found by: `V9XGDI.EXE /accel` on the ViRGE/DX guest (`:9869`), build
`gdi-accel-000` with `GdiAccelFill=1` set by hand

## Symptom

A `PATCOPY` solid fill executed by the ViRGE engine paints the wrong colour.
The rectangle's shape and position are correct; only its colour is wrong, and it
is wrong the same way every time - white.

Measured at 640x480x8 on the ViRGE guest:

| | screen | reference DC |
|---|---|---|
| pixel at (105, 230) | 255, 255, 255 (white) | 255, 255, 0 (yellow) |

32897 of 230400 compared bytes differ - about 14 per cent of the compared
region, which is one fill rectangle's worth. Everything else in the run agrees.

## Cause

The driver's own counters name it exactly. From the same run:

```
LastRop256=240          (0xF0 - PATCOPY, correctly classified)
LastBrushFlags=129      (0x81 - COLORSOLID | DIBENGBRUSH)
LastBpp=8
LastColor=16711935      (0x00FF00FF)
```

`0x00FF00FF` is `COLORREF` magenta - R=255, G=0, B=255 - and magenta is one of
the sixteen colours the harness draws with. So the value the dispatcher read out
of the realized brush's `FgColor` and wrote into the ViRGE's `PATTERN_FG` is a
**logical RGB triple**, not a physical palette index.

The ViRGE at 8 bpp takes the low byte of `PATTERN_FG` as the pixel value. The
low byte of `0x00FF00FF` is `0xFF`, and index 0xFF in the Windows 98 default
system palette is white. Which is precisely the white on the screen.

The gates and the classification are all correct. The brush is solid
(`COLORSOLID`), DIBENG realized it (`DIBENGBRUSH`), the ROP is PATCOPY, the
depth is 8. The single wrong thing is the colour's *space*.

`DIBENG.INC:189` labels the field "Physical fg color", which is what the
dispatcher was written against. On this path it is evidently not that - or it is
that only for some brush realizations, and the physical index has to be obtained
another way.

## What build 001 has to settle

Read the DDK's `RealizeObject` contract properly before changing a line. The
question is narrow: for a solid brush realized by `DIB_RealizeObjectExt` against
an 8-bpp screen PDEVICE, where does the physical pixel value live? Candidates,
in the order worth checking:

1. **`BrushBpp` says which.** It was not captured in this run and should be the
   first thing added to `V9X_GDI_STATS`. A brush realized at a depth other than
   the screen's would explain an RGB `FgColor` completely.
2. **The colour translation table.** A palettized device has one, and the driver
   already forwards `SetPaletteTranslate`/`GetPaletteTranslate`. If DIBENG
   expects the *driver* to translate, that table is the mechanism.
3. **A different field.** The header mirrors the common 14 bytes and its size
   asserts hold, so the offsets are right for what it declares - but if the
   physical colour for a solid brush lives in `Bits[0]` rather than `FgColor`,
   the size asserts would not notice.

## Why 16 bpp is not evidence either way

This was measured at 8 bpp only. At 16 bpp a logical RGB and a physical pixel
value are also different numbers, so the same defect almost certainly applies -
but the failure would look different (a wrong hue rather than uniform white),
and it has not been observed. Test both depths.

## Why this is not a build 000 defect

Nothing ships on it. Every primitive is compiled off at 000, so the shipping
driver declines every blit and the DIB Engine draws the desktop; the mode matrix
passes on all eleven modes on both S3 chips and on all six on the engine-less
`ati` guest with the fill unreachable. The fill was reached only by setting
`GdiAccelFill=1` in `SYSTEM.INI` by hand, which is the deliberate run
`docs/plans/gdi-accel-000-and-harness.md` Block 3 asks for - "purely to prove
the primitive can fire at all before build 001 claims it works."

It fired, and it is wrong. That is the run doing its job. The alternative -
finding this after 001 had been declared working - is what the plan's insistence
on landing the harness first was protecting against, and it is worth recording
that the protection paid for itself on its first use.

## Reproducing it

```powershell
# On the ViRGE guest, with the gdi-accel-000 package installed:
#   append to C:\WINDOWS\SYSTEM.INI
#     [Velocity9x]
#     GdiAccelFill=1
#   reboot, then confirm C:\V9XHW.INI says GdiAcceleration=gdi-fill
#   set an 8- or 16-bpp mode, then:
#     V9XGDI.EXE /accel
#   read C:\V9XACCE.INI
```

`Result=FAIL`, `Error=pixel-mismatch`, and the `Last*` keys carry the diagnosis.
`V9XGDI.EXE /stats` prints the same counters without drawing anything.
