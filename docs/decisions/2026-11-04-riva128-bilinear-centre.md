# RIVA 128ZX: bilinear moved half a texel onto Direct3D's centres

Date: 2026-11-04

Machine: M-7V636C (remote help), RIVA 128ZX 8 MiB, Windows 98 first
edition, boots 33-34, desktop 800x600x16 at 5:5:5. Files:
`docs/probe/m7v636c-riva128-games-2026-11-04/b34-*`.

## Why

The state probe's TexelFoh case (2026-11-03,
[record](2026-11-03-riva128-d3d-state.md)) measured the chip's bilinear
interpolator: a texel's colour is exact at u = i / W, half a texel left of
Direct3D's (i + 0.5) / W, and the same in v. V9XDDP's Tex8Bilinear samples
an 8x8 texture, green in its left half and blue in its right, at u = 0.5,
where Direct3D blends texels 3 and 4 evenly. Boot 33 read 001Fh, texel 4
alone.

## What was built

For a textured draw under bilinear, `d3d_nv3.c` takes 0.5 / width off u
and 0.5 / height off v (level 0's texel) as it converts each vertex,
before the builder's scaling for non-square copies. Point sampling is
untouched; it already matched (TexelZohCorner). Under a mip chain a
smaller level stays a fraction of its texel off. The white texture of the
specular and decal-alpha passes is one colour, so the shift does not
matter there.

## Boot 34

Tex8LinearRaw 0210h (green 16, blue 16 of 31), Tex8BilinearOk 1;
Tex8LinNearestRaw 001Fh as before. 216 of 227 `Ok` keys, none of boot
33's lost. 594 batches drawn, 5 refused (the 8888 surfaces), no engine
timeout.

## What is left on the default run

- D3DTrilinearBlend and MipTriOk: no blend between levels (MipTriDegraded
  passes).
- The 8888 pair, recorded as expected.
- FlipPixelOk.
- D3DZP* (2) and Mixed* (4), which run only under `/zprivate` and
  `/mixed` and pass there.

Half-Life was not rerun on this build; its bilinear textures moved half a
texel, which a screenshot at this size would not show.
