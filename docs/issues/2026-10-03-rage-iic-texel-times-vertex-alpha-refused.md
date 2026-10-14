# Rage IIC refuses a blend by texel alpha times vertex alpha (Quake 2's particles)

Date: 2026-10-03. Machine: A8U4I5 (Rage IIC AGP `1002:4757`). Status:
open, deferred. Evidence: `docs/decisions/2026-10-03-rage-iic-opengl-first-runs.md`
(boot 148, `V9XGL.LOG`).

## Symptom

Quake 2 under `V9XGL.DLL` refused 2,807 batches (142,572 triangles) in
its demo timedemo. Of those, the alpha-tested ARGB4444 batches (textures
1152 and 1155, replace) are served since `eca481d` by the alpha-mask
texel rewrite. That has not been rerun under Quake 2; it is untested
there. The rest are GL_MODULATE of the 8x8 particle texture (1153,
ARGB4444) under vertex alpha 0xD4, blended SRCALPHA / INVSRCALPHA. The
blend needs the texel's alpha times the vertex's. TEX_MAP_AEN gives the
blend unit one or the other (A1, A5 in
`2026-10-02-rage-iic-filtering-formats-blending-fog.md`), so
`v9x_r2_check_draw` refuses it unless one of the two is 1.

## Candidates, none measured

- Premultiply: a shadow copy of the texture with alpha scaled by the
  batch's vertex alpha, when that alpha is constant across the batch, as
  Quake 2's particles are. It costs a copy per distinct vertex alpha.
- Bake the vertex alpha into the texel colour and blend ONE / INVSRCALPHA.
  This changes the colour, so it needs a scene to show the difference.
- Approximate with the texel alpha alone, and measure how wrong it looks.

## The Mach64 too (2026-10-04)

On the Rage XL, Quake 2's 6,507 refused batches a run (385,721 triangles)
are the passive check's `TEXTURE_OP`: 6,441 a run (`M64AcceptPolicy12`).
The ICD's retry with the CPU copy then fails `TEXTURE_FORMAT`, because
the Mach64 has no route for CPU textures, and the batches are not drawn
(`docs/probe/a8u4i5-rage-xl-pci-2026-10-03/q2-gl-timedemo-fixes/`). Which
op it is was inferred from Quake 2's particle state, not read: the draw
path's op mask is not set by the passive check.

## ATI3DCIF's keyed sprites too (2026-10-14)

Under the new ATI3DCIF.DLL, Pixie, Fairy and Laputa each have two draws a
frame refused UNSUPPORTED on A8U4I5 (boot 456). The texture is chroma-keyed
1555 (key texels alpha 0), MODULATE on colour and alpha, blended
SRCALPHA/INVSRCALPHA, with no depth buffer
(`docs/probe/a8u4i5-rage-iic-3dcif-phase3-2026-10-14/README.md`). Two
changes did not clear the refusal:
- dropping the alpha test, which is redundant under that blend;
- using the texel's alpha alone when every vertex alpha is 255.

So the vertex alpha is below 255 and the draw needs the key's 1-bit
alpha times the vertex alpha. That is the product this issue is about. A
premultiplied copy cannot carry it, since 1555 has one alpha bit.

## Next

First count how many distinct vertex alphas Quake 2's particle batches
carry. Then choose a candidate and write its ATIRX scene before touching
the policy.
