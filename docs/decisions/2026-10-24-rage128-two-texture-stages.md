# Two Direct3D texture stages on the Rage 128, and DDI 6 for every program on it

Date: 2026-10-24

Status: implemented for the Rage 128 only, measured on A8U4I5 (Pentium
III, Rage 128 Pro 5046, Win98 SE), boots 577 and 578. Every other engine
still reports one stage, and its behaviour is meant to be unchanged.

Plan: [d3d-two-stage-multitexture.md](../plans/d3d-two-stage-multitexture.md).
Why the Rage 128 went first: [rage128-dispbench-review.md](../plans/rage128-dispbench-review.md), item 3.

## What changed

- **Builder** (`r128_3d.c`): the second unit, as Mesa 7.11's r128 driver
  sets it and the DRM's `r128_emit_tex1` writes it.
  - `SEC_TEX_CNTL_C`, with `SEC_SELECT_SEC_ST` (sample with the vertex's
    second S/T pair), and `SEC_TEX_COMBINE_CNTL_C`, whose input is the
    previous unit's result.
  - Unit 1's size goes in `TEX_SIZE_PITCH_C`'s high half.
  - `SEC_TEXMAP_ENABLE` and `TEX_CACHE_SPLIT` are set.
  - The vertex format gains `S2_T2` (100h), making it 10 dwords.
  - Unit 1 only runs behind unit 0.
- **Combine ops**: replace, modulate, decal-alpha (blend by texel alpha)
  and add, each with the texel as the factor.
- **Mapper and engine** (`d3d_r128_map.c`, `d3d_r128.c`): a draw with
  `texcoords1` is mapped on both units, and the engine copies each
  vertex's pair after T. The limits now say `texture_units` 2 and a new
  `texture_stages` 2.
- **Stage pairing** (`d3d_state.c`, host-tested): stages 0 and 1 become
  one two-unit draw only where the arithmetic is exactly the units'.
  Stage 1 must combine the texture with CURRENT. Stage 0's CURRENT is the
  vertex colour. Everything else is refused at
  `ValidateTextureStageState`, and the application draws its own passes.
- **DrawPrimitives2 core** (`d3d_core.c`, `r3d_records.c`): stage 1's
  states are kept, and each vertex's pair travels beside it through the
  record run. A two-unit batch is culled with its pairs, never clipped or
  staged, and drawn with `texcoords1`. If the engine declines the second
  unit, the batch is drawn as stage 0 alone, as before.
- **Caps** at DDI 6: two blend stages, two simultaneous textures, two
  coordinate sets, and `D3DTEXOPCAPS_ADD`.
- **DDI 6 by default**: an engine with two stages gives every program
  DDI 6, the only interface that has stages. A program listed `=5` in
  `[Velocity9x.Direct3DDdi]` keeps DDI 5. Michael's decision.
- **New counters** (V9XTRACE): `TwoUnitBatches`, `TwoUnitDeclined`,
  `TwoUnitValidateRefused`.

## Measured

**dispbench DB3D, boot 577** (DDI 6 by a SYSTEM.INI entry):

| | ATI 4.13.7192 | Velocity9x |
|---|---:|---:|
| FillMultiTex | 57.0 Mpix/s | 69.8 Mpix/s |
| FillTex16Bilinear | 166.1 Mpix/s | 146.2 Mpix/s |

- FillMultiTex was refused (`single-texture-device`) before this change.
  It is now verified by DB3D's read-back; the pixels differ from the
  expected value by DB3D's tolerance.
- The trace counted 22 two-unit batches across the test.

**3DMark 2000 default run**, boot 576 (DDI 5, one stage) against boots
577/578 (DDI 6, two stages):

| | DDI 5 | DDI 6, two stages |
|---|---:|---:|
| Score | 1694 | 1618 |
| Fill rate, multi-texturing (Mtexels/s) | 131.1 | 189.0 |
| Emboss bump mapping, 3-pass / 2-pass (fps) | not supported | 58.2 / 60.1 |
| Game 1, low / medium / high (fps) | 37.2 / 26.4 / 11.0 | 31.9 / 22.0 / 7.8 |
| Game 2, low / medium / high (fps) | 26.2 / 22.3 / 18.0 | 30.8 / 24.1 / 18.1 |
| High polygon count, 1 light (KTriangles/s) | 1438 | 1079 |
| CPU speed (CPU 3D marks) | 197 | 157 |

- The review gives ATI's multi-texture fill as 178.2 Mtexels/s.
- Over one run, the trace counted 253,191 two-unit batches, none
  declined, no engine refusal, and one stage pair refused at validation.
- The boot 578 run, on the default with no SYSTEM.INI entry, gave the
  same figures to within 0.5%.
- An OBS recording of boot 578's first 164 s covered Game 1 and Game 2's
  lightmapped courtyard. Both drew without visible error: lightmaps were
  in place, there was no mosaic, and no texture was missing.

## What the evidence disputes or leaves open

- **The score fell.** That fall is the DDI 6 path itself, not the second
  unit. DB3D's TriBatch at DDI 6 is 0.53 Mtri/s against 0.95 at DDI 5.
  The single-texture and geometry-heavy tests lose what the
  multi-textured ones gain. Review item 4 (a faster DrawPrimitives2 path)
  is now what stands between the Rage 128 and both.
- **OpenGL is untested.** `texture_units` 2 also lets the OpenGL ICD
  multitexture on the Rage 128 through the same mapper. No GL game was
  run on this build.
- **The frames were checked by eye, not by pixel**, in about half of one
  3DMark run. DB3D's pixel check covers modulate only.
- **Unmeasured ops**: add, decal-alpha and replace on unit 1. They follow
  Mesa's encodings. MODULATE2X is refused rather than guessed.
- The VBE and SiS floppies each gained a third disk (the update
  checker), because the shared HAL grew by 10.7 KB.
