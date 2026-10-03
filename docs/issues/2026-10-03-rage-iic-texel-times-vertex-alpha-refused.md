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

## Next

First count how many distinct vertex alphas Quake 2's particle batches
carry. Then choose a candidate and write its ATIRX scene before touching
the policy.
