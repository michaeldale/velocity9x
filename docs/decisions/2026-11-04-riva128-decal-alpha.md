# RIVA 128ZX: DECALALPHA, by DECAL or two passes

Date: 2026-11-04

Machine: M-7V636C (remote help), RIVA 128ZX 8 MiB, Windows 98 first
edition, boots 32-33, desktop 800x600x16 at 5:5:5. Files:
`docs/probe/m7v636c-riva128-games-2026-11-04/b33-*`.

## Why

DECALALPHA is vertex x (1 - At) + texel x At, alpha the vertex's. The D3D
object's combine is texel x vertex colour, with no lerp between them, so
the HAL refused the op (policy slot 4, M64TexOpRefused 8). V9XDDP's two
checks drew nothing:
- Tex8DecalAlpha4444: green texels at alpha 8/15 over a red vertex; both
  channels near half.
- Tex8DecalAlpha565: no alpha, so pure green.

## What was built

In `d3d_nv3.c`:
- A texture with no alpha, or whose alphas are all 1 (the alpha scan),
  takes DECALALPHA as DECAL, which is what it is there.
- Otherwise two passes a triangle: the vertex colour, untextured and
  opaque; then the texel by the source alpha blend, the vertex white, Z
  following the first pass (as the specular pass does). The sum is the
  formula exactly. Fog is linear, so the two fogged passes add to the
  fogged combine.
- Only for a draw that replaces the pixel, with no alpha test and no
  colour key: the second pass must cover exactly what the first drew.
  Anything else with partial alpha is still refused.
- The caps add DECALALPHA.

## Boot 33

Tex8DecalAlpha4444 reads 3DE0h (red 15, green 15 of 31) and
Tex8DecalAlpha565 03E0h; both Ok. Tex8Decal unchanged. 215 of 227 `Ok`
keys, none of boot 32's lost. 594 batches drawn, 5 refused (from 7: the
two DECALALPHA draws), all five unplaced textures, the 8888 surfaces.
No engine timeout.

## Left on the default run

The bilinear half texel, the two trilinear blend checks, the 8888 pair
([recorded as expected](2026-11-04-riva128-no-8888-textures.md)) and
FlipPixelOk.
