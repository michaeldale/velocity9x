# Direct3D two-stage multitexture on engines with two texture units

Date: 2026-10-08. Status: not started. Written for a later session; no
code exists. Engines it would serve: the Rage Pro class's Mach64 (the
composite) and Gen3, both of which already draw two textures for
OpenGL.

## Where things stand

Two texture units reach OpenGL only, through the render interface:

- The Mach64 draws unit 1 MODULATE over unit 0 in one pass (the
  composite, TEX_ST_DIRECT;
  `docs/decisions/2026-10-08-rage-pro-composite-direct.md`), and Gen3
  draws its two-unit fragment program
  (`docs/decisions/2026-10-05-sgis-multitexture-gen3-netbook.md`). Both
  report `texture_units` 2 in their engine limits.
- The render interface carries the second unit as `V9X_R3D_DRAW.texture1`
  and `texcoords1`, and `v9x_r3d_draw_two_units` (d3d_core.c) sends it to
  `ops->draw`. Each engine's `accepts`/`draw` reads it.

Direct3D sees one texture on every engine:

- `wMaxTextureBlendStages` and `wMaxSimultaneousTextures` are 1
  (d3d_core.c, the caps builder near `V9xD3dValidateTextureStageState`).
- `v9x_d3d_dp2_stage_state` (d3d_core.c) drops every stage-1 state and
  counts it in `d3d_diagnostics.dp2_stage1_states`, which V9XTRACE reports
  as `Dp2Stage1States`. Stage 0 is translated to a DX5 texture blend by
  `v9x_d3d_dp2_express_stage`, and anything else is drawn as MODULATE
  and counted in `dp2_stage0_approximated`.
- `V9xD3dValidateTextureStageState` answers one pass for stage-0 ops the
  DX5 blends express (DISABLE, SELECTARG1/2, MODULATE, BLENDTEXTUREALPHA)
  and `D3DERR_UNSUPPORTEDCOLOROPERATION` for the rest.
- The DP2 vertex layout (`d3d_dp2.c`, `V9X_DP2_FVF_TEXCOUNT`) reads the
  FVF's texture-coordinate count, but only set 0 reaches the engine;
  every Direct3D draw leaves `texcoords1` null (r3d.h).

So a DX6 or later program that asks for two stages is told one and draws
its own passes. That is correct, only slower. Unlike OpenGL's
GL_SGIS_multitexture, Direct3D has the application validate first, so
offering two stages risks speed, not missing geometry: a combination
`ValidateTextureStageState` refuses is drawn in passes by the program.
The ICD's split (`v9x_gl_draw_split`) has no Direct3D counterpart and
does not need one.

## Is it worth doing: the open question first

No Direct3D program measured here has used stage 1. All 50 V9XTRACE
snapshots in `docs/probe` that carry `Dp2Stage1States` read 0, among them
Half-Life's Direct3D (DDI 6) runs on the Rage XL
(`a8u4i5-rage-xl-pci-2026-10-06/ddi6-halflife`, `ddi6-perf`) and UT99 on
the netbook. Half-Life's Direct3D renderer does not ask for a second
stage.

Step 0 is therefore a census, not code:

1. List DX6-DX8 titles of the period that use a second blend stage
   (lightmaps, detail textures, environment maps), with evidence: a
   released source, a DX6 SDK sample's stage setup, or a captured state
   stream. Candidates to check, none verified: 3DMark 2000 and 2001 SE
   (both installed on A8U4I5), Unreal and UT99's Direct3D renderer,
   DX6/DX7 SDK samples such as the multitexture sample, games of the
   Quake 3 engine's era that shipped D3D renderers.
2. Run each on A8U4I5 or the netbook and read `Dp2Stage1States` from
   snapshots taken either side. With the DDI 6 capture
   (`Dp2Capture=1`, ddi6 plan), record which stage-1 ops and arguments
   they set, since only stage 1 MODULATE is known to map to the
   composite.
3. Proceed only if a title that matters sends stage-1 states the engines
   could draw in one pass.

## The work, if the census says yes

All of it in the chip-neutral core except the limits each engine already
publishes:

1. Caps: report `wMaxTextureBlendStages` and `wMaxSimultaneousTextures`
   as the engine's `texture_units` (2 on the Mach64 and Gen3), and keep 1
   elsewhere. Check what DX6, DX7 and DX8's runtimes each require
   before they believe a second stage (the DDI 6 plan's runtime notes),
   and that DX8's `MaxSimultaneousTextures` comes from the same field.
2. Stage state: keep stage 1's texture handle, colour and alpha ops and
   arguments, address, filters and `TEXCOORDINDEX` in the context beside
   stage 0's (`stage_texture`, `stage_color_op` ...), instead of dropping
   them.
3. Translation: a pure, host-testable mapping from the two stages to the
   render interface's two-unit combine (`V9X_R3D_ABI_COLOROP_*`/
   `_ALPHAOP_*` per unit), or "not expressible". GL's table 3.18 is
   what texture1's combine already means. D3D's stage arithmetic
   (COLORARG1/2 with CURRENT, DIFFUSE, TEXTURE) has to be matched to it
   case by case, and only the exact cases accepted.
4. Vertices: carry the second coordinate set (the FVF's set 1, or the
   set `TEXCOORDINDEX` names) into `texcoords1` for the DP2 and DX5
   DrawPrimitive paths, and through any core-side clipping, which today
   parts a vertex from its `texcoords1` entry (`v9x_r3d_draw_two_units`
   skips the list builder for that reason; the Direct3D path uses it).
5. Validation: `V9xD3dValidateTextureStageState` answers one pass for a
   two-stage setup only when step 3 maps it and the engine's `accepts`
   would take it, and the precise error otherwise.
6. Engines: the Mach64 takes unit 1 MODULATE today. ATI's note
   ("Multitexturing with the ATI Rage Pro", 2000) also lists
   MODULATE2X, ADD (tex1 x diffuse + tex2) and a linear blend by a
   factor, which DX6 has stage ops for. `COMP_COMBINE` (TEX_CNTL [10:9])
   and `COMP_FACTOR` [7:4] are the fields, and which value gives which
   function is unmeasured. A probe like V9XGLP's level-tagged SgisMip
   section (one colour per case, read back) would settle it before any
   policy accepts them.

## Measurement gates

- Host tests for step 3's mapping and step 4's coordinate path, failing
  first.
- On A8U4I5 (Rage XL PCI) and the netbook (GMA 950): a Direct3D probe
  in V9XDDP or a new tool with the same cases V9XGLP's SGIS section
  draws (MODULATE, coordinates per unit, level selection with W != 1,
  which is what broke the composite once), read back.
- The census titles before and after, with `Dp2Stage1States`,
  refusals and frame rates recorded (not chased), and matched
  screenshots.

## Hazards already met on the OpenGL side

- The composite's mip level follows W unless S and T arrive
  premultiplied under TEX_ST_DIRECT (fixed in the Mach64 setup builder;
  any Direct3D path through `ops->draw` inherits it).
- Under the composite the first texture loses trilinear (the composite
  takes the blend function) and unit 1 samples one level only.
- On the Rage XL at game resolutions the chip, not the driver, is the
  limit (`docs/decisions/2026-10-08-rage-xl-draw-cost.md`). Single-pass
  pays only by saving fill, and only where the program's batches stay
  large. Quake 2 gained 16% and Half-Life (OpenGL) 8-12%.
- Gen3 measured multitexture slower than two passes in Quake 2 and
  Half-Life (OpenGL) on 2026-10-05, from whole-page lightmap re-uploads.
  A Direct3D title's upload pattern decides the same there.
