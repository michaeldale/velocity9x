# The software rasterizer draws triangles and depth-tests them, and the probe's expected colours are the ViRGE's

Date: 2026-09-01
Branch: `main`
Plan: [`s3-trio64-voodoo2-hybrid-3d.md`](../plans/s3-trio64-voodoo2-hybrid-3d.md),
mode 2, work-order steps 6 and 7.

Guests: `Win98SE-Trio64` (86Box, S3 Trio32/64 86C764, 4 MiB, 800x600x16, host
port 9871, `Direct3D=2`) and `Win86SE` (ViRGE/DX 86C375, 1024x768x16, port
9869, hardware mode). Same `s3` family binary on both, `V9XHAL.DLL` 33,792
bytes, `Build=2fee8ce-dirty`.

## What was measured

Three things, and the third was not what the run was for.

### 1. It is a triangle, not a bounding box

Nothing in the existing ladder could tell those apart. The probe's triangle is
`(8.25, 8.25)`, `(55.75, 8.25)`, `(8.25, 55.75)` and it read the pixel at
`(16,16)`, which is inside both the triangle and its box. Stage 1 of mode 2
shipped a deliberate bounding-box stub, so `D3DTrianglePixelRaw` had been
reporting a pass for a fill that was not rendering.

`D3DTriangleOutsideRaw` reads `(48,48)`. The hypotenuse is `x + y = 64`, so
that pixel is far outside the triangle and squarely inside its box, and the
surface is cleared to zero first.

| | Trio64 (software) | ViRGE (hardware) |
|---|---|---|
| `D3DTrianglePixelRaw` | 63488 | 31744 |
| `D3DTriangleOutsideRaw` | **0** | **0** |
| `D3DTriangleShapeOk` | **1** | **1** |

The ViRGE column is the control: a known-good hardware rasterizer answers the
new key the same way, which is what says the key measures shape rather than
something incidental to the software path.

### 2. Depth testing and the write mask work on the software engine

Both of the probe's existing depth ladders behave, rung for rung, on a chip
with no depth unit at all. Read as colours rather than as constants:

| Rung | What it proves | Trio64 raw | as RGB565 | ViRGE raw | as ZRGB1555 |
|---|---|---|---|---|---|
| `D3DZInitRaw` | 0.5 red, ALWAYS, write | 63488 | red | 31744 | red |
| `D3DZRejectRaw` | 0.75 green, LESS - rejected | 63488 | still red | 31744 | still red |
| `D3DZAcceptRaw` | 0.25 blue, LESS - accepted | 31 | blue | 31 | blue |
| `D3DZUpdateRaw` | 0.5 white, LESS - rejected, so C's write landed | 31 | still blue | 31 | still blue |
| `D3DZNoWriteRaw` | 0.125 green with the write masked off | 2016 | green | 992 | green |
| `D3DZMaskRaw` | 0.1875 red, accepted only if the masked draw wrote nothing | 63488 | red | 31744 | red |

Every rung of both ladders is correct on the Trio64. Blue is `0x001F` in both
formats and matches numerically; every other rung differs from the ViRGE by
exactly the format and by nothing else.

### 3. The probe's expected colours are the ViRGE's, and that hides passes

`D3DZCompareOk=0` and `D3DZWriteMaskOk=0` on the Trio64 **despite every rung
being right.** The ladders fold a colour comparison into their verdict, against
`0x7C00` and `0x03E0` - red and green in **ZRGB1555**. The software engine
writes **RGB565**, which is the format the surface is described as.

This is the same mismatch `D3DTrianglePixelOk` has reported since 2026-08-30,
and it has now hidden three passing keys rather than one. README records the
underlying defect: the S3D triangle engine writes native ZRGB1555 into a
surface described as RGB565. **The probe's expectations were written to match
the buggy engine**, so the engine writing the format the surface declares is
the one that fails.

Not changed here. Making the probe derive its expected colours from the render
target's pixel format would flip the ViRGE's whole ladder from green to red in
one step, and that is a decision about a baseline three chips have been
validated against - it needs its own record, not a side effect of this one.

## What this does not establish

- **Nothing about textures.** `TexFormatCount=0` and `D3DBaseTextureOk=0` on
  the Trio64, both correct and deliberate: `texture_format` declines every
  surface and `describe_caps` advertises no format to match.
- **Nothing about caps.** Depth now works and is still not advertised -
  `dwDeviceZBufferBitDepth` is zero and no `dwZCmpCaps` are published. That is
  work-order step 9 and deliberately after textures, so one caps change carries
  one measurement. Until then no application that checks caps will use the
  depth buffer that this run proves works.
- **Nothing about speed.** A 64x64 target says nothing about a rasterizer's
  cost. BARRY is the machine for that, and it has still never run a 3D
  benchmark.
- **Nothing about the tier-0 or ATI families**, which take the same code path
  and were not booted.

## No regression on the ViRGE

Same binary, hardware mode: `D3DTrianglePixelOk=1`, `D3DSubpixelTriangleOk=1`,
`D3DZCompareOk=1`, `D3DZWriteMaskOk=1`, `TexFormatCount=2`,
`D3DBaseTextureOk=1`, `D3DContextCycleOk=1`, `Result=COMPLETE`. Every
functional key matches the baseline recorded on 2026-08-30.

## The finding the run was not looking for

Both engines lose the last row and the last column of a full-target triangle -
see [`docs/issues/2026-09-01-clipper-loses-last-row-and-column.md`](../issues/2026-09-01-clipper-loses-last-row-and-column.md).
The host test for the rasterizer's own worst-case interpolation ran into it
first, as an off-by-one in an expected coverage count, and the ViRGE control
run is what turned it from a property of the new rasterizer into a property of
the chip-neutral clipper.
