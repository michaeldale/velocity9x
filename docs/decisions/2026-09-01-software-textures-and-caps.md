# The software engine textures, and its caps say only what it does

Date: 2026-09-01
Branch: `main`
Plan: [`s3-trio64-voodoo2-hybrid-3d.md`](../plans/s3-trio64-voodoo2-hybrid-3d.md),
mode 2, work-order steps 8 and 9.

Guests: `Win98SE-Trio64` (86Box, S3 Trio32/64 86C764, 800x600x16, port 9871,
`Direct3D=2`) and `Win86SE` (ViRGE/DX, 1024x768x16, port 9869, hardware mode)
as the control. Same `s3` binary, `V9XHAL.DLL` 35,840 bytes,
`Build=4472955-dirty`.

## The sampler

One texture, point or bilinear, decal or modulate, from ARGB1555 or ARGB4444 -
the same two formats the ViRGE accepts and classified the same way, so that the
two engines stay interchangeable rather than accepting different surfaces.

> Superseded in part, 2026-09-02: the software engine gained RGB565 as a third
> format and the ViRGE did not, because the S3D texture unit has no RGB565 texel
> mode. The interchangeability argument below held until the hardware turned out
> not to allow it. See
> [`2026-09-02-software-rgb565-textures.md`](2026-09-02-software-rgb565-textures.md).

**Texture coordinates hold exactly one repeat, and that is arithmetic rather
than a choice.** The edge interpolator forms `max(from, to) * denominator` with
the denominator being the triangle's height in subpixels, at most 32752, so
anything it carries has to stay under 65566 or the product leaves a signed
32-bit integer. Depth already sits at 65535 against that bound. A coordinate
able to express two repeats would need twice it.

> Superseded, 2026-09-02: the interpolator now divides before it multiplies,
> its largest intermediate no longer depends on what it carries, and the engine
> publishes WRAP as well as CLAMP. The reasoning below was right about the
> bound and wrong about it being fundamental. See
> [`2026-09-02-software-texture-wrap.md`](2026-09-02-software-texture-wrap.md).

So the sampler clamps, and the caps publish `D3DPTADDRESSCAPS_CLAMP` without
`WRAP`. That is the correct pairing rather than a shortfall hidden by
omission - the alternative was to wrap a coordinate the interpolator cannot
carry, which puts a seam through the middle of every triangle instead of
stretching an edge texel.

The filter comes from the magnification state for both directions. Choosing per
pixel needs a texel-density derivative this rasterizer does not compute, and
the minification states applications actually set are mostly the MIP variants,
which mean nothing without mip selection - mapping those to point would make a
`LINEARMIPLINEAR` request come out sharper than a `LINEAR` one.

## The caps, and what is deliberately missing

`describe_caps` now publishes: RGB colour model, float TL vertices, execute
buffers from system memory, texturing from device memory, 16-bit render target,
16-bit Z, all eight comparison functions, flat and Gouraud RGB shading,
specular Gouraud, subpixel setup, point and bilinear filtering, decal and
modulate blending, square power-of-two textures, clamped addressing.

Not published, each because nothing implements it: `WRAP`; the four mip filter
caps; `D3DPTEXTURECAPS_ALPHA` and every alpha blend cap; `PERSPECTIVE`;
`D3DPTBLENDCAPS_COPY`; and the fog caps - the core folds fog into the vertex
colour before the engine sees it, so a fogged flat triangle already comes out
right, but the evidence for that is one probe rung and it goes in when there is
a ladder behind it.

`DDPF_ALPHAPIXELS` **is** set on both published texture formats while the
sampler ignores alpha. That is not the advertise-then-ignore pattern: it
describes the surface layout an application must create, not a capability the
engine offers. The capability is `D3DPTEXTURECAPS_ALPHA`, and that is absent.

## Measured

Trio64, software mode - every key below was 0 or absent before this change:

```
TexFormatCount=2      TexFormat1555=1      TexFormat4444=1
D3DBaseTextureOk=1    D3DBaseTextureRaw=2016   (0x07E0, green in RGB565)
Tex4444PixelOk=1      Tex4444Raw=2016
D3DTrianglePixelOk=1  D3DSubpixelTriangleOk=1  D3DTriangleShapeOk=1
D3DZCompareOk=1       D3DZWriteMaskOk=1        D3DSpecularGouraudOk=1
D3DDepthFogOk=1       D3DContextCycleOk=1      Result=COMPLETE
```

`Tex4444PixelOk=1` is the one that proves the format is decoded rather than
guessed. The texel is `0xF0F0`, opaque pure green in 4444; the same sixteen
bits read as ARGB1555 are red 28 of 31, green 7 and blue 16 - strong red and
blue with little green - so a misread format cannot produce this result. The
same argument is a host test.

The published caps, read back through `IDirect3DDevice2::GetCaps`:

```
D3DDevice2HwTriRaster=48     ZTEST | SUBPIXEL
D3DDevice2HwTriZCmp=255      all eight
D3DDevice2HwTriShade=522     FLATRGB | GOURAUDRGB | SPECULARGOURAUDRGB
D3DDevice2HwTriTexture=34    POW2 | SQUAREONLY
D3DDevice2HwTriFilter=3      NEAREST | LINEAR
D3DDevice2HwTriBlend=3       DECAL | MODULATE
D3DDevice2HwTriAddress=4     CLAMP
D3DDevice2HwZDepth=1024      DDBD_16
D3DDevice2HwTriSrcBlend=0    D3DDevice2HwTriDestBlend=0
```

The three keys that read 0 on the Trio64 read 0 correctly:
`D3DMipmapLevelSelectOk` and `D3DTrilinearBlendOk` need mip selection, and
`D3DVertexAlphaBlendOk` needs alpha blending. None of the three is advertised.

## The ViRGE, unchanged

`TexFormatCount=2`, `D3DTriangleShapeOk=1`, `D3DMipmapLevelSelectOk=1`,
`D3DDepthFogOk=1`, `Result=COMPLETE`, and its own caps word for word what it
published before. The keys reading 0 are the six the format defect takes -
`D3DBaseTextureRaw=992` is `0x03E0`, green in ZRGB1555, written into an RGB565
surface. See [the issue](../issues/2026-09-01-virge-3d-writes-zrgb1555.md).

Both engines still lose the last row and column
([issue](2026-09-01-clipper-loses-last-row-and-column.md) - `D3DEdgeRightRaw=0`
and `D3DEdgeBottomRaw=0` on both, with centre and top-left drawn).

## What this does not establish

- **Anything about speed.** Every measurement here is a 64x64 target on an
  emulated guest. Work-order step 1 - VRAM versus system-memory write cost on
  BARRY - has still never been run, and no period machine has drawn a textured
  triangle through this.
- **That an application is happy.** Final Reality and Ironfield have not been
  run against the software mode. The caps are honest about the engine; whether
  a game that reads them does something sensible with `CLAMP` and no alpha is a
  separate question.
- **The tier-0 and ATI families**, which take the same code path and were not
  booted.
