# RV280 textures by register writes: decal, wrap, modulate, bilinear

Date: 2026-11-15

Machine: M-HNBJNV, Radeon 9250 (`1002:5960`), Windows 98 SE. The driver
was branch `ati-rv280`'s ati package (`539fb5ce-dirty`, boot 19). Probe:
`V9XR23D.EXE` + `R23D.VXD` at `13a3d826-dirty`. Phase 5 of
[`../plans/ati-rv280-radeon-9250.md`](../plans/ati-rv280-radeon-9250.md),
after the [triangle probe](2026-11-15-rv280-3d-draw-probe.md). Evidence:
[`../probe/rv280-texture-probe-2026-11-15/`](../probe/rv280-texture-probe-2026-11-15/),
the result file per mode and each texture test's 4 KiB read-back surface.

## What ran

`v9x_rv280_build_3d_textured_target` (`src/chipsets/ati/rv280/rv280_3d.c`)
writes texture unit 0 as xf86-video-ati 6.14.6's `R200TextureSetup` does
for a power-of-two texture (`radeon_exa_render.c:798-914`):
- **Filter:** `PP_TXFILTER_0`, nearest or MAG/MIN LINEAR, wrap or
  CLAMP_LAST.
- **Format:** `PP_TXFORMAT_0`, RGB565 or ARGB8888 with ALPHA_IN_MAP,
  log2 width and height.
- **Size, pitch, offset:** `PP_TXFORMAT_X_0` 0, `PP_TXSIZE_0`, and
  `PP_TXPITCH_0` (the pitch less 32), with `PP_TXOFFSET_0` the GPU
  address.
- **Enable:** `PP_CNTL` turns on texture unit 0 and blend stage 0.
- **Combines** for blend stage 0, with the texel sampled into R0:
  - decal is the X driver's no-mask blend, R0 × (1 − 0) + 0;
  - modulate is its mask form with diffuse for the mask, R0 × diffuse.
- **Vertex:** `SE_VTX_FMT_1` sets two components for texture 0, so the
  vertex is X, Y, colour, S, T.

The host test (`tests/host/test_rv280_3d.c`) had 30 checks fail against a
stub before the builder was written.

The texture is 32 x 16 texels at frame-buffer offset 16 MiB + 64 KiB,
filled with the VxD's gradient, so every texel differs from its
neighbours. Each quad is two triangles.

## Measured

| Test | 1024x768x16 | 1024x768x32 |
|---|---|---|
| TexDecal: 32 x 16 quad, nearest, every pixel its texel | 0 wrong | 0 wrong (RGB) |
| TexWrap: texture twice across 64 pixels, S 0 to 2, wrap | 0 wrong | 0 wrong |
| TexModulate: × flat colour 80C040h, within a step | 0 wrong, worst 1 | 0 wrong, worst 1 |
| TexBilinear: texels 0-31 x 0-7 magnified 2x, clamped | 0 wrong, worst 0.75 step | 0 wrong, worst 0.50 step |
| Pixels outside a quad changed, any test | 0 | 0 |
| Triangle tests and 2D fill after | PASS | PASS |

- **The texture setup works as written:** format, size, pitch, offset
  and filter, with the CP off.
- **Texture coordinates follow the colour in the vertex.** The dword
  order is X, Y, colour, S, T, the order Linux `r200.c` counts the
  fields in.
- **Nearest sampling is exact** at texel centres. Wrap repeats the
  texture.
- **Bilinear** lands within 0.75 of a step of the exact filter at the
  pixel centre. That is tighter than the RS690's judge allows, and its
  spread allowance was not needed.
- **Modulate** multiplies texel by vertex colour to within a step.

## Not checked

- Mipmaps, non-power-of-two textures, and texture formats other than the
  surface's own (ARGB1555, ARGB4444, palettised).
- Colour-keyed textures, alpha in the texture, alpha blending.
- Perspective: every vertex here is at the same depth, and no W or RHW
  is sent yet.
- Z, culling and fog. Those come next, with the HAL glue.
- The 32 bpp output's alpha byte is still not judged; it read 0 in the
  triangle probe.
