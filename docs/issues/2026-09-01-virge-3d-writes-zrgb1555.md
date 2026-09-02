# The ViRGE's S3D triangle engine writes ZRGB1555 into an RGB565 target

Date: 2026-09-01
Status: open, measured, cause not established
Component: `src\display32\d3d\d3d_virge.c`

## The defect

The Direct3D render target is created by DDRAW in the primary's format. Queried
on the guest rather than assumed:

```
D3DTargetFormatValid=1
D3DTargetRMask=63488    (0xF800)
D3DTargetGMask=2016     (0x07E0)
D3DTargetBMask=31       (0x001F)
```

That is RGB565, on both guests. The S3D triangle engine writes ZRGB1555 into
it. Every colour it draws is therefore wrong on screen: red lands in the top
five bits of a six-bit green field plus part of red, and so on.

README has recorded this as an unresolved mismatch since the first Direct3D
work. It had never been visible from outside the driver, because the probe's
expected colours were the 1555 values - written to match the engine rather than
the surface.

## Measured, 2026-09-01

Both guests, `Build=4472955-dirty`, after the probe began deriving its expected
colours from the target's own pixel format
([record](../decisions/2026-09-01-probe-derives-expected-colours.md)).

| Key | Trio64, software | ViRGE, S3D | Expected colour |
|---|---|---|---|
| `D3DTrianglePixelOk` | 1 | **0** | red |
| `D3DSubpixelTriangleOk` | 1 | **0** | red |
| `D3DZCompareOk` | 1 | **0** | red, blue |
| `D3DZWriteMaskOk` | 1 | **0** | white, green, red |
| `D3DSpecularGouraudOk` | 1 | **0** | green |
| `D3DTrilinearBlendOk` | 0 | **0** | green/blue mix |
| `Tex4444PixelOk` | 0 | **0** | green |
| `D3DBaseTextureOk` | 0 | **0** | green |
| `D3DDepthFogOk` | 1 | **1** | blue |
| `D3DMipmapLevelSelectOk` | 0 | **1** | blue |
| `D3DTriangleShapeOk` | 1 | **1** | none - shape only |

The pattern is the proof, and it is not "the probe broke". **Every ViRGE key
whose expected colour is blue still passes**, because `0x001F` is blue in both
1555 and 565 and is the one colour the two formats agree on. Every key carrying
a red, green or white channel fails. A key that tests shape rather than colour
passes.

`D3DVertexAlphaBlendRaw=16399` on the ViRGE is `0x400F` - exactly the literal
the probe used to compare against. The engine is writing the 1555 bit pattern
into the 565 surface, unchanged.

The Trio64 column is the control: the software engine writes what the surface
declares and passes all six.

## What the cause is not

**Not the command word's format field.** `V9X_VIRGE_3D_CMD_GOURAUD_16` is
`0x80000007`, whose `0x4` was documented here as `cmdDEST_FMT_ZRGB1555`. It is
not a layout selector: 86Box decodes bits 4:2 as a *bit depth* -
`CMD_SET_FORMAT_8/16/24 = 0/1/2 << 2`
(`build\reference-vid_s3_virge.c:435-438`) - so `0x4` selects 16 bits per pixel
and says nothing about the channel layout. Changing it changes the pixel size,
not the format.

**In 86Box's model there is no selector at all.** The 3D path packs its output
through `RGB15(r, g, b, dest)` (`reference-vid_s3_virge.c:3524-3536`)
unconditionally for 16-bit destinations. On that model the S3D unit cannot
write RGB565 from the triangle engine, whatever the driver asks for.

## Real silicon, 2026-09-02: an S3D part does this

A physical **S3 Trio3D/2X** (`5333:8A13`, machine A8U4I5) was bound to this
driver's ViRGE path and run with hardware Direct3D. It writes
`D3DTrianglePixelRaw=31744` - `0x7C00`, red in ZRGB1555 - into a surface the
probe queried as RGB565 (`D3DTargetRMask=63488`). Its whole depth ladder reads
in 1555 too, rung for rung identical to the emulated ViRGE.

That is the same behaviour on hardware that 86Box models, which **removes
hypothesis 1 as the likely explanation**: an incomplete emulator model would
not be reproduced by a physical chip. It does not close the question - the
Trio3D is not the 86C375, and the ViRGE itself has still only been seen under
emulation - but the weight has moved decisively toward "the S3D engine really
does write 1555 into a 16-bit destination".

See [`2026-09-02-trio3d-on-the-s3-path.md`](../decisions/2026-09-02-trio3d-on-the-s3-path.md).

## What would settle it

Two possibilities, and the emulator cannot distinguish them because the
emulator is the model:

1. **86Box's model is incomplete** and real silicon has a destination-layout
   control the model does not implement. The Windows 98 DDK's own ViRGE driver
   is the place to look - `C:\98DDK\src\display\mini\s3v\` - specifically what
   it does when the desktop is 565, which is the ordinary case.
2. **The chip really does only write 1555 from the 3D engine**, and period
   drivers either ran the desktop at 15 bpp while 3D was in use or accepted the
   shift. If so this is not a driver defect at all, and what needs fixing is the
   surface description rather than the engine.

The physical ViRGE on the VLB machine can answer this where 86Box cannot. Until
one of the two is established, do not "fix" the engine: both plausible fixes -
changing the command word, or changing what format the driver reports for its
render targets - are wrong under the other hypothesis.

## Consequence for reading results

Six ViRGE keys now read 0 that read 1 before 2026-09-01, and none of them
changed behaviour. The 1555 expectations are preserved in the probe as the
fallback for a surface that will not describe itself, so a result file with
`D3DTargetFormatValid=0` is being read the old way and should be treated as
such.
