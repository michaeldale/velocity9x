# A 5:5:5 desktop needs three places to agree, and finding the third took a day

Date: 2026-09-02
Status: complete. Automatic under hardware Direct3D with a page control;
measured in software and, on 2026-09-03, on the emulated ViRGE with hardware
Direct3D, where nine colour keys flipped to 1 with no engine change. Real S3D
silicon (A8U4I5) still unmeasured - unreachable since 2026-09-02

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

## Completed, 2026-09-03: the third place, and a fourth nobody listed

The third place was one line. `ddi.c` passed `V9X_DE_FIVE6FIVE` to the DIB
engine for every 16 bpp mode; it now passes it only when the row is not a 5:5:5
one. That flag, not `ddpfDisplay`, is what DirectDraw's primary format follows:
with it withheld the probe read `D3DTargetRMask=31744` for the first time.

And then the rasterizer wrote `0xF800` into that surface anyway. The core's
classification added the day before only ran for display-layout targets; the
probe's 64x64 render target is offscreen, took the other branch, and kept a
default of 5:6:5. An offscreen target is now classified from its own
`ddpfSurface` when `DDRAWISURF_HASPIXELFORMAT` says it has one and from the
display's format when it does not, through one helper both branches share. A
side effect worth stating: an offscreen surface that is neither layout - 32 bpp,
say - passed the size checks before and was rasterized as 16 bits. It is refused
now.

Measured on **WIN98-S3NATIVE, 86Box, S3 Trio64**, boot 306, `HighColor=15`:

```
Colour=hc=15 row=276 set=275 is555=1 mask=31744
D3DTargetRMask=31744  D3DTargetGMask=992
D3DTrianglePixelRaw=31744   (0x7C00, red in 1555)
D3DBaseTextureRaw=992       (0x03E0, green)
D3DVertexAlphaBlendRaw=16399 (0x400F, half-alpha red over blue)
```

Every `*Ok`, `*Count` and `HwTri*` key is byte-identical to the 5:6:5 run. The
software engine passes its whole ladder into a 5:5:5 desktop, and the numbers
it wrote are the numbers the ViRGE engine has been writing all along - so the
instrument now agrees with that engine's output for the first time, in the mode
S3 shipped a switch for. The guest was returned to 5:6:5 (`Colour=hc=16
row=276 set=276 is555=0 mask=63488`) and reverified.

What this does not show: that the physical scanout is 5:5:5. A GDI screenshot
looked normal in both states, as it must. Three declared layouts agreeing plus a
probe whose derived expectations pass is the strongest evidence this driver can
produce about itself; the rest is a monitor.

## Policy, 2026-09-03: automatic under hardware Direct3D, and a page control

Both halves of the open policy question were taken.

**Automatic.** `v9x_highcolor_resolve(setting, d3d_state)` in
`src/common/vbe_modes.c`: an explicit 15 or 16 is obeyed; anything else - the
key absent, zero, a typo - is 5:5:5 exactly when Direct3D resolved to
`V9X_D3D_STATE_HARDWARE` and 5:6:5 otherwise. The chip's authority is already
folded into that state by `v9x_d3d_mode_resolve`, so a Trio64 with `Direct3D=0`
resolves to NONE and lands on 5:6:5 without the layout code knowing what a
Trio64 is. Nine host tests pin the table.

The decision needs the chip, and the chip is known only at Enable, so the
layout is read twice: at mode-table init for an explicit value, and again from
the Enable path once `v9x_hardware_acceptable` has matched the PCI id - before
the FIVE6FIVE flag, before the mode set, before DirectDraw publishes anything.
`v9x_modes16_resolve_layout` re-stamps every 16 bpp row that has a 15 bpp
sibling in both directions, so a layout that flips between boots leaves no
stale mask. `V9XHW.INI` gains `ColourLayout=` with one of four spellings
(`555-auto`, `555-ini`, `565-auto`, `565-ini`) and the `Colour=` boot line
gains `ini=`, so the file and the decision can be told apart on a machine read
after the fact.

**The page.** A third selector on the Velocity9x tab, "16-bit colour", with
Automatic, 5:6:5 and 5:5:5. Automatic deletes the key rather than writing 0,
so `SYSTEM.INI` stays readable by eye and by S3's own driver, which knows the
same two numbers. Both selectors now share one Apply and one "restart Windows"
notice. The dialog grew eleven dialog units to make room.

Measured on **WIN98-S3NATIVE, 86Box, S3 Trio64**, `Direct3D=2` (software):

```
key absent               Colour=hc=16 ini=0  set=276 is555=0 mask=63488
                         ColourLayout=565-auto
page: 5:5:5, OK          SYSTEM.INI gains HighColor=15
reboot                   Colour=hc=15 ini=15 set=275 is555=1 mask=31744
                         ColourLayout=555-ini
                         D3DTargetRMask=31744  D3DTrianglePixelRaw=31744
                         every *Ok, *Count, HwTri* key identical to 5:6:5
restored                 Colour=hc=16 ini=0  set=276 is555=0 mask=63488
```

The page was driven through the guest's own mouse: the tab, the dropdown with
its three entries, the selection, OK, and the notice were each captured.

**What was not measured, and why.** `555-auto` - the case the policy exists
for - needs a chip that resolves to HARDWARE, and the only two available are an
86Box ViRGE guest that is not running and A8U4I5, which has been unreachable
since 2026-09-02. On this Trio64 automatic can only ever say 5:6:5, and did.
The host tests cover the rule; the machine does not yet.

## Measured, 2026-09-03: automatic 5:5:5 on the emulated ViRGE

The 86Box ViRGE/DX guest (`Win86SE`, port 9869, S3 ViRGE/DX 86C375 5333:8A01,
`Direct3D=0`) was started for this. Same build, two boots, and the only
difference between them is one SYSTEM.INI line.

**Control, `HighColor=16` forced** (boot 529):

```
Colour=hc=16 ini=16 row=279 set=279 is555=0 mask=63488   ColourLayout=565-ini
Direct3DMode=hardware   D3DTargetRMask=63488
D3DTrianglePixelRaw=31744 (0x7C00 into a 565 target)   D3DTrianglePixelOk=0
```

**Automatic, key absent** (boot 530):

```
Colour=hc=15 ini=0  row=279 set=278 is555=1 mask=31744   ColourLayout=555-auto
Direct3DMode=hardware   D3DTargetRMask=31744
D3DTrianglePixelRaw=31744 (0x7C00 into a 555 target)   D3DTrianglePixelOk=1
```

Mode 0x117 became 0x116 on its own, because the chip resolved to HARDWARE.
Nothing in the engine changed - the raw values are identical between the two
runs - and **nine keys went 0 to 1**: `D3DTrianglePixelOk`,
`D3DSubpixelTriangleOk`, `D3DSpecularGouraudOk`, `D3DZCompareOk`,
`D3DBaseTextureOk`, `Tex4444PixelOk`, `D3DVertexAlphaBlendOk`,
`D3DTrilinearBlendOk`, and `D3DMipmapLevelSelectOk` stayed 1. That is the
experiment the 1555 record defined, run on the emulator: the engine was never
wrong, the target's description was.

One key stayed 0, and it is a new finding rather than a leftover:
`D3DZWriteMaskOk`. With colours now comparable, the emulator leaves the masked
green draw's depth in the buffer and the real Trio3D does not - see
[`../issues/2026-09-03-86box-virge-ignores-depth-write-disable.md`](../issues/2026-09-03-86box-virge-ignores-depth-write-disable.md).
The mismatch was invisible while every colour key failed for the same reason.

The guest was left in the automatic state, which is its SYSTEM.INI as found.
Still not measured: the same on A8U4I5, the only real S3D silicon, unreachable
since 2026-09-02. On the emulator the S3D model's 1555 packing is by
construction; on the card it is a measurement (2026-09-02, one run, 5:6:5
desktop), and the 5:5:5 desktop closing it there is the one thing this record
still owes.

## What remains

1. ~~Declare the layout to GDI.~~ Done above; it was the `FIVE6FIVE` PDEVICE
   flag, and the `BI_RGB` header turned out not to be what the DIB engine reads.
2. **Run the experiment on silicon.** Done on the emulator above. On A8U4I5
   it needs no INI edit: boot with `Direct3D` absent or 0 and the desktop
   should come up `555-auto`. Then On A8U4I5 with hardware Direct3D and
   `HighColor=15`, every failing colour key should turn to 1 with no change to
   the engine. If it does not, the engine is packing something other than what
   both the emulator and S3's header say, which would be a more interesting
   finding than the fix.
3. ~~Decide the policy.~~ Decided 2026-09-03, above: automatic, with the page
   able to override either way.

A8U4I5 stopped answering on `10.0.1.172:9869` partway through this work and was
not reachable for any of it. Every measurement above is from the two 86Box
guests: the Trio64 for the software engine, the ViRGE/DX for the S3D one.

## Scope note

Nothing here touches ViRGE register code. The mode table is VESA numbering,
`d3d_core.c` is chip-neutral, and the rasterizer is this project's own
arithmetic - so the independence question raised in
[`../ddk-inputs.md`](../ddk-inputs.md) does not arise for any of it. The one
thing taken from the DDK is the idea that a 5:5:5 desktop is the answer, and
that is a fact about the silicon rather than anyone's expression of it.
