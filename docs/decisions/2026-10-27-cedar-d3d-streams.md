# The HD 5450 draws the Direct3D HAL's own streams: TL vertices, D3DCOLOR, perspective, Z16 and 565

Date: 2026-10-27

Status: **measured, one card, one boot.** M-BXMFA8's HD 5450 (`1002:68F9`,
Cedar), boot 7. Follows
[Gouraud, depth and modulate](2026-10-27-cedar-gouraud-depth-modulate.md).
Tool: `V9XEG3D.EXE`, now built on `src\chipsets\ati\evergreen\eg_3d.c`
(the PM4 builders the HAL will use) and `eg_shader.c`'s Direct3D shaders.
Captures:
[`../probe/m-bxmfa8-cedar-d3d-streams-2026-10-27/`](../probe/m-bxmfa8-cedar-d3d-streams-2026-10-27/).
This is item 5 of [the 3D plan](../plans/ati-evergreen-cedar-3d.md), the
step before the HAL: its stream on the card first, where a run costs
seconds rather than a reboot.

## What moved into eg_3d.c, and the regression

The probe's hand-written `evergreen_gpu_init` writes, ME_INITIALIZE, the
clear-state preamble and `set_default_state` (the 227-dword default
context included) are now `eg_3d.c`'s table and builders. Every packet's
count is derived from its values, and the host test walks each stream
header by header, so a miscount like Phase 3's fails there. The four
earlier draws ran once each on the shared code, all passing: the blit
(a1, byte-exact), `/gouraud` (a2), `/depth` (a3) and `/modulate` (a4).

## The Direct3D draws

All through `v9x_eg_3d_build_state`. The vertices are the render core's TL
vertex unchanged: sx, sy, sz, rhw, D3DCOLOR, specular, tu, tv, 32 bytes.
The target is 64x8, linear general with `NON_DISP_TILING_ORDER`.

| Draw | Runs | Result |
|---|---|---|
| `/d3dcolour`: one D3DCOLOR `80402010`, untextured | a5, c5, d6 | every pixel `80402010` |
| `/d3dtex`: textured (normalised u, v), white, rhw 1 | a6, c3, d5; c4 with `/centre0` | byte-exact copy of the source |
| `/d3dpersp`: rhw 1 at the left, 0.25 at the right | d1, d2, d3 | all 512 texels exact |
| `/d3dz`: Z16 through the builder, three rectangles | a8, c6, d7 | red left, blue right |
| `/d3d565`: D3DCOLOR `FF848242` into a 565 target | a9, c7, d8 | every pixel `8408h` |

The colour path never swizzles. The VS fetches the D3DCOLOR as four
normalised bytes in memory order (B, G, R, A) and the PS exports them in
that order. The CB's `SWAP_STD` writes x to the low byte (8888) or the
low bits (565), which is where Direct3D keeps blue. `80402010` came back
`80402010`, and 84h/82h/42h came back as 5:6:5 `8408h`.

## PA_CL_VTE_CNTL: the field read was wrong

The builder first set `VTX_W0_FMT` (bit 10), reading it as "w is 1/w",
with Direct3D's rhw in the position's w. The texels came out with the
perspective weighting inverted (a7). The probe's own expected model was
wrong as well: it took u to depend on x alone, but v varies along a row
when rhw does. Both were checked against a host-side computation of the
exact perspective-correct texel, triangle by triangle:

| Run | PA_CL_VTE_CNTL | Exact texels of 512 |
|---|---|---|
| a7, b2, c8, d4 | `700h` (XY, Z, W0 formats) | 0 |
| b1, c1, c2 | `300h` (XY and Z formats only) | 512 |
| b3 | `200h` | 8 |
| b4 | `600h` | 8 |

So `W0_FMT` stays clear: with it clear, the rhw given as w weights the
perspective barycentrics correctly. The builder now always writes `300h`.
The probe checks the exact model in integer arithmetic (rhw 1 and 0.25
scale to 4 and 1, and the pixel centres' barycentrics times 128 are
integers). d1 to d3 pass on that check.

## Pixel centres

`/d3dtex` copied byte-exact with `PIX_CENTER_HALF` set (centres at +0.5:
each pixel samples its texel's centre) and also with it clear (c4: each
pixel samples its texel's left edge, and point sampling rounds down). A
1:1 copy does not tell the two apart beyond that. Direct3D's convention
is centres at whole coordinates. Which setting a Direct3D application
needs is V9XDDP TexPhase's question, on the HAL.

## Not measured

- Bilinear filtering, wrap and mirror, and texture formats other than
  8888 (565, 1555 and 4444 textures).
- Blending and alpha test, though both builders' words are host-tested.
- More than one batch per ring session, and the vertex buffer anywhere
  but beside the shaders.
- Specular and fog: the VS does not fetch the specular dword.
