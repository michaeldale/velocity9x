# 3DCIF: Pixie, Fairy and Laputa's blended sprites are refused

Date: 2026-10-14

Status: Open, parked by Michael on 2026-10-14. A decision is owed before
code.

## What is seen

On both the Rage IIC (boot 456) and the Rage XL (boot 469), the three
demos refuse two draws a frame, UNSUPPORTED:

| Demo | Refused on the Rage IIC | Refused on the Rage XL |
|---|---|---|
| Pixie | 880 | 1,718 |
| Fairy | 1,172 | 1,784 |
| Laputa | 914 | 2,740 |

Evidence: `docs/probe/a8u4i5-rage-iic-3dcif-phase3-2026-10-14/demo-summaries.txt`
and `docs/probe/a8u4i5-rage-xl-3dcif-2026-10-14/demo-summaries.txt`.

The draws use a keyed 1555 texture, MODULATE on colour and alpha,
blended SRCALPHA/INVSRCALPHA, with no depth buffer. The vertices' alpha
is below 255: the Rage IIC run that used texel alpha alone where every
vertex was opaque refused them all the same (`cif-p3-6`). So the blend
needs texel alpha times vertex alpha.
- **The Rage II cannot:**
  [2026-10-03-rage-iic-texel-times-vertex-alpha-refused.md](2026-10-03-rage-iic-texel-times-vertex-alpha-refused.md).
- **The Rage Pro class cannot either:** `mach64_policy.c`, "No mode
  yields A=AtAf".

## The choices

1. **Leave them refused**, as now: the sprites do not draw.
2. **Texel alpha alone:** the sprite's shape is right, but a fading
   sprite shows at full strength.
3. **Vertex alpha alone:** fades are right, but the sprite's transparent
   edges are lost. The chroma key could still cut the shape where the
   texture is keyed.
4. **Two passes:** no single-pass equivalent is known on either engine,
   so it is unexplored.

Not watched on the monitor: which sprites these are, and how much of
each demo they cover.
