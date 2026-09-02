# The software rasterizer publishes RGB565; the ViRGE path cannot

Date: 2026-09-02
Status: implemented, measured on two targets

## The question

`docs/issues/2026-09-02-dxdiag-fails-at-enumtextureformats.md` recorded DxDiag
failing its Direct3D test at step 44 with `HRESULT = 0x00000000` - a successful
enumeration whose contents it rejected - and named a lead: both engines publish
ARGB1555 and ARGB4444 and no plain RGB565, while the display on every target
this driver serves is RGB565. The issue's second next-step asked whether RGB565
is publishable, and said explicitly that for the ViRGE path it is a hardware
question that needs a source rather than a guess.

## What the hardware says

The S3D texture unit takes its texel format from the 3D command register, bits
7:5, and 86Box's ViRGE model decodes that field at
`build/reference-vid_s3_virge.c:4564-4577`:

| Field value | Format   |
|-------------|----------|
| 0           | ARGB8888 |
| 1           | ARGB4444 |
| 2           | ARGB1555 |
| default     | ARGB1555 |

There is no RGB565 texel mode. Three values are defined and the rest fall back
to ARGB1555, so a driver that published RGB565 on this path would hand the
engine a surface it would sample as ARGB1555 - the red channel shifted one bit,
the green truncated to five, and no error anywhere.

So the answer differs by engine, and this is the first place the two
deliberately diverge.

## What changed

The software rasterizer gains RGB565 as a third texel format:

- `V9X_D3D_RASTER_TEXFMT_RGB565` in `d3d_raster.h`, accepted by
  `v9x_d3d_raster_texture_valid`, decoded in `v9x_d3d_raster_texel` with a new
  `v9x_d3d_raster_expand6` alongside the existing `expand5` - six bits to eight
  on the same replication rule, so 63 reaches 255 rather than 252.
- `v9x_d3d_soft_texture_format` classifies masks `0xf800/0x07e0/0x001f`.
- `v9x_d3d_soft_describe_caps` publishes it as `texture_formats[2]` with
  `dwFlags = DDPF_RGB` alone - no `DDPF_ALPHAPIXELS`, zero alpha mask, because
  the layout has no alpha bit - and sets `dwNumTextureFormats = 3`.

`v9x_d3d_virge_describe_caps` is untouched and still publishes two.

`V9X_DD_SHARED.texture_formats` widened from `[2]` to `[3]`, which moves every
member after it. The 16-bit driver and the 32-bit HAL must therefore be built
and deployed together; both were, by the `WININIT.INI` `[Rename]` route.
`runtime.asm` allocates the block at a fixed 4096 bytes and `sizeof` is 3096
before the widening, so the extra ~108-byte descriptor stays inside it and the
header's assertion still holds.

## Evidence

`tests/host/test_d3d_raster.c` gains `test_texture_format_decode_565`, which
draws a texture of `0x8400` and reads the pixel back. That word was chosen
because the three layouts disagree about it completely: 565 makes it red 16/31
and green 32/63, 1555 makes it red 1/31 and nothing else, 4444 makes it dark red
with no green. Green is the channel that decides, and only a 565 decode puts any
there. The test then re-declares the same texture as each of the other two
formats and checks that neither produces it.

`./scripts/run-checks.ps1` green.

**WIN98-S3NATIVE, 86Box, S3 Trio64 (5333:8811), `Direct3DMode=software`**, boot
297, `V9XDDP.EXE`:

```
Result=COMPLETE
TexFormatCount=3
TexFormat565=1   TexFormat1555=1   TexFormat4444=1
TexFmt2Flags=0x00000040   TexFmt2R=0x0000F800  TexFmt2G=0x000007E0  TexFmt2B=0x0000001F
D3DBaseTextureOk=1  Tex4444PixelOk=1  D3DTrianglePixelOk=1  D3DZCompareOk=1
```

Three formats, the third with `DDPF_RGB` and no alpha bit as written, and every
existing texture and depth assertion still passing.

**A8U4I5, physical, S3 Trio3D/2X (5333:8A13), `Direct3DMode=hardware`**, boot 8,
same probe binary:

```
Result=COMPLETE
TexFormatCount=2
TexFormat565=0   TexFormat1555=1   TexFormat4444=1
```

Two, unchanged, which is the point: the ABI widening did not disturb the
hardware path's list. Its `*Ok=0` keys - `D3DTrianglePixelOk`, `D3DZCompareOk`,
`D3DBaseTextureOk`, `Tex4444PixelOk` - are the already-filed ZRGB1555 defect and
not a regression: `D3DTrianglePixelRaw=31744` is `0x7C00`, red in ZRGB1555,
where the probe derives `0xF800` from the RGB565 target, and
`D3DBaseTextureRaw=992` is `0x03E0` against an expected `0x07E0`. The depth
ladder's semantics are still right - reject leaves red, accept writes blue -
in the wrong pixel layout. See
`docs/issues/2026-09-01-virge-3d-writes-zrgb1555.md`.

## What this does not fix

DxDiag's step 44 **in hardware mode**. The lead was that the enumeration offers
no format matching the display, and on the ViRGE path it still does not, because
the silicon has no RGB565 texel mode to publish. If the hypothesis is right,
step 44 should now pass in software mode and still fail in hardware - which is a
cheap discriminating test, and it has not been run.

It also says nothing about the black screen during Final Reality's fullscreen 3D
section in hardware mode
(`docs/decisions/2026-09-02-final-reality-on-a-real-trio3d.md`), which remains
unexplained.
