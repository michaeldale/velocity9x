# A 5:5:5 desktop needs three places to agree, and only two of them now do

Date: 2026-09-02
Status: mechanism partly built and measured; the experiment it exists for has
**not** been run

## What this was for

[`2026-09-02-s3d-writes-1555-because-it-can-only-write-1555.md`](2026-09-02-s3d-writes-1555-because-it-can-only-write-1555.md)
established that the S3D triangle engine has no RGB565 destination format, so
hardware Direct3D on that silicon only lands its colours in the right channels
when the desktop is 5:5:5 - which is what S3's own driver offers through a
`HighColor` key in `SYSTEM.INI`. That document listed four things standing in
the way of testing the same idea here. Three are now done. The fourth turned out
not to be on the list at all.

## Done, and measured

**The rasterizer reads its target's format.** `V9X_D3D_RASTER_TARGET` carries
`V9X_D3D_RASTER_PIXFMT_RGB565` or `_XRGB1555`, and the packer, the blend's
destination read, and the validator all follow it. It had assumed 5:6:5
throughout, which it should not have done regardless of any of this. Four host
tests, including one that draws saturated green into each layout - `0x03E0`
against `0x07E0`, either value read as the other format being a different
colour, so a rasterizer that packed one layout unconditionally fails it whichever
it chose.

**The core accepts a 5:5:5 render target.** `d3d_core.c` required exactly
`0xf800/0x07e0/0x001f` and returned zero otherwise, which refused the only
display mode in which hardware Direct3D on this silicon is correct. It now
recognises both layouts, records which, and still refuses anything else -
`context->target_format`, in the core's own vocabulary rather than the
rasterizer's, because `d3d_raster.h` is the software engine's private header and
the core is shared with an engine that has no rasterizer. `d3d_soft.c` asserts
the two numberings agree at compile time, the same arrangement the filter, blend
and comparison constants already use.

**The mode set and the DirectDraw mode list follow a setting.**
`v9x_vbe_mode_555` returns the 15 bpp VESA sibling of a 16 bpp mode number or
zero, and one function decides both the number programmed and the masks
published so they cannot disagree. `HighColor=15` in `[Velocity9x]` of
`SYSTEM.INI`, S3's key name and S3's values, default 5:6:5.

Measured on **WIN98-S3NATIVE, 86Box, S3 Trio64 (5333:8811)**, boot 303, through
a new `Colour=` line in `V9XBOOT.INI`:

```
setting absent   Colour=hc=16 row=276 set=276 is555=0 mask=63488
HighColor=15     Colour=hc=15 row=276 set=275 is555=1 mask=31744
```

Mode 0x114 becomes 0x113, the published mask becomes `0x7C00`, the guest boots
to a working desktop, and with the setting absent every probe key is
byte-identical to the run before any of this.

## Not done, and this is the finding

**The render target still reports 5:6:5.** With `HighColor=15` set and both
numbers above confirmed changed, the probe reads
`D3DTargetRMask=63488`. DirectDraw is not taking the primary surface's pixel
format from `DDHALINFO.vmiData.ddpfDisplay`, which the driver had just filled
with `0x7C00`.

The third place is GDI. `ddi.c` publishes a `BITMAPINFOHEADER` with
`biBitCount = 16` and `biCompression = BI_RGB`, and nothing in the driver
declares a channel layout to the DIB engine at all. That is what the desktop is
drawn in and, on this evidence, what DirectDraw's primary format follows. It was
never touched, because the four-item list in the previous document was drawn up
by reading the Direct3D path and did not look at GDI.

So with the setting on, the machine programs a 5:5:5 scanout and continues to
draw 5:6:5 pixels into it. **The physical display is wrong in that state and no
test this driver can run on itself can see it** - a GDI screenshot reads back
through the same DIB the driver filled, so it looks perfectly correct. That is
the trap [`../issues/2026-09-02-flippixelok-is-uninterpretable.md`](../issues/2026-09-02-flippixelok-is-uninterpretable.md)
records, met a second time, and the reason the `Colour=` line publishes numbers
rather than a rendered result.

The guest was returned to `HighColor=16` and reverified before this was written.

## What remains

1. **Declare the layout to GDI.** A 16 bpp `BI_RGB` DIB is 5:5:5 by Windows'
   own definition and a 5:6:5 one needs `BI_BITFIELDS` with a mask triple, so
   what the driver says today is already not what it programs. Which of the two
   the DIB engine actually believes is a measurement, not a reading - the
   desktop is visibly correct at 5:6:5 today, so something is deciding it
   elsewhere. S3's driver threads its `FIVE6FIVE` flag through the cursor and
   the refresh paths as well, which suggests the answer is not one field.
2. **Then run the experiment.** On A8U4I5 with hardware Direct3D and
   `HighColor=15`, every failing colour key should turn to 1 with no change to
   the engine. If it does not, the engine is packing something other than what
   both the emulator and S3's header say, which would be a more interesting
   finding than the fix.
3. **Decide the policy.** Whether a 5:5:5 desktop should be automatic when
   hardware Direct3D is selected on S3D silicon, offered in the settings page,
   or left as an INI key. Not decided here; the mechanism defaults to 5:6:5 and
   changes nothing until asked.

A8U4I5 stopped answering on `10.0.1.172:9869` partway through this work and was
not reachable for any of it. Every measurement above is from the 86Box guest.

## Scope note

Nothing here touches ViRGE register code. The mode table is VESA numbering,
`d3d_core.c` is chip-neutral, and the rasterizer is this project's own
arithmetic - so the independence question raised in
[`../ddk-inputs.md`](../ddk-inputs.md) does not arise for any of it. The one
thing taken from the DDK is the idea that a 5:5:5 desktop is the answer, and
that is a fact about the silicon rather than anyone's expression of it.
