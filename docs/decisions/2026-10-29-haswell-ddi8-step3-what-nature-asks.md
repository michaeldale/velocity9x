# What 3DMark 2001 SE's Nature asks of a driver: pixel shader 1.1, cube maps, and five texture instructions

Date: 2026-10-29. Machine: the i5-4570 (W9QI5A83, HD 4600), boots 117 to
119, DirectX 9.0c, 3DMark 2001 SE build 330. Step 3 of
[`../plans/haswell-ddi8-pixel-shaders.md`](../plans/haswell-ddi8-pixel-shaders.md).
Evidence: [`../probe/hsw-4570-ddi8-step3-2026-10-29/`](../probe/hsw-4570-ddi8-step3-2026-10-29/).
Each V9XPS log holds the boots before it too (the file is appended).

## The measurement build

`V9X_D3D_PS_CLAIMED` in `d3d_core.c`, 0 on main and built with 1 for these
boots, makes a DDI 8 driver object claim pixel shader 1.1 and four
textures, logs every pixel and vertex shader's tokens to
`C:\V9XDIAG\V9XPS.LOG` with the first draw under each, and counts and
drops every draw made under a shader. Boots 118 and 119 added cube textures
to the claim, and boot 119 answered every ValidateTextureStageState with
one pass, logging the question. The logging and the dropping are on main
too; with the switch at 0 no program is told of shaders, so neither runs.

## What was measured

| Boot | Claimed | 3DMark 2001 SE |
|---|---|---|
| 117 | PS 1.1, 4 textures | Runtimes report PS `FFFF0101`, 4 stages and textures, value 1.0 (V9XD32 T17). 3DMark still lists Nature, Pixel Shader and Advanced Pixel Shader "Not supported by hardware"; no shader made |
| 118 | + cube textures | Nature and Pixel Shader no longer unsupported: 3DMark starts them and stops with "FATAL ERROR: failed to validate material"; no shader made |
| 119 | + every texture-stage validation answered one pass | 201 validations logged (stage 0 ops 2, 3, 4, a few two-stage pairs); then 3DMark dies with a general protection fault in USER.EXE `0003:10CD`, before any shader is made |

- **The gate is cube maps.** 3DMark's engine (`e2mfc.dll`) keeps
  per-display-mode support flags (`P_DisplayMode::setCubeMapSupport`,
  `setVolumeMapSupport`, `setBumpSupport`, read from its exports), and
  claiming cube textures was what moved Nature from unsupported to
  started.
- **Fixed-function materials are validated before shaders are made.** Our
  ValidateTextureStageState refuses stage pairs the two hardware stages
  cannot draw exactly, and that refusal is "failed to validate material".
  Which questions failed is not separated in the log: boot 119 answered
  all of them.
- **The boot 119 fault** is in 16-bit USER.EXE, not the HAL. It followed
  cube textures being claimed while nothing in the driver handles a cube's
  six faces; the cause is not traced.

## What the shaders use

3DMark assembles its shaders from text at run time; its executable carries
the assembler's messages and opcode names. The text is in `data.ras`, the
40 MB data archive, as plain text among compressed data. Six pixel shaders
are readable there (the rest of the archive is compressed, so others may
exist). By file, what each uses, not the text itself:

| Shader | Version | Uses |
|---|---|---|
| paint | 1.0 | tex, mov |
| underwaterpaint | 1.0 | tex, texkill, mov |
| (unnamed, beside them) | 1.0 | tex, texm3x3pad x2, texm3x3vspec (cube lookup), mov to .rgb and to .a |
| water (1.4) | 1.4 | texld, texcrd, bem, phase, lrp, ... |
| water_reflection_alpha | 1.1 | tex, texbem, mul, co-issued mul |
| water (1.0) | 1.0 | tex, texbem, mul |

Under a 1.1 claim the 1.4 water is not chosen (recalled: 3DMark picks the
1.4 path only on 1.4 hardware; not measured). So the translator needs:

- versions 1.0 and 1.1;
- tex, texkill, texbem (stage M's bump matrix), texm3x3pad and
  texm3x3vspec (a cube texture, the eye vector from the coordinates' w);
- mov and mul, write masks .rgb and .a, and co-issue;
- cube textures, sampled.

Vertex shaders (21 `vs.1.x` tokens; paint, underwaterpaint, water, water14
by name) run in the runtime's software path here and are not the driver's.

## What this changes in the plan

- **Cube textures move into scope** for step 5: the format list's
  CUBETEXTURE operation, cube surfaces held through CreateSurfaceEx (their
  six faces, each with mip levels), SURFTYPE_CUBE in SURFACE_STATE, and
  the sampler's cube-coordinate division (spec section 7).
- **Fixed-function validation needs more than two stages,** or Nature's
  materials are refused before any shader is made. What the refused
  questions were is the next measurement: log each refusal under the
  normal answer rather than answering one pass.
- **The step 4 instruction list is smaller than the plan's:** no texreg2ar,
  texreg2gb, texm3x2*, texm3x3tex, texm3x3spec, cnd, lrp, dp3 or mad
  appear in the readable shaders.

## Not checked

- No shader reached the driver: the token log is empty of shaders on all
  three boots. The instruction list comes from the shaders' text, not from
  tokens the runtime sent.
- Whether the unnamed texm3x3vspec shader belongs to Nature or to the
  Advanced Pixel Shader test.
- Whether more shaders sit in the compressed part of the archive.
