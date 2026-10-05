# GL_SGIS_multitexture on Gen3, with the software engine as its reference

Date: 2026-10-05. Status: steps 1-3 done and measured on the software
guest; step 4 coded and host-tested, waiting for the netbook. Started at
Michael's request ("do multitexture on Gen3"). Machine for the hardware
steps: MICHAEL-NETBOOK (945GSE / GMA 950), offline when this was written.

- Steps 1-2 (a7cd710): ABI 4, the ICD's units, entry points and
  extension string.
- Step 3 (64acf20): the CPU rasterizer and software engine; V9XGLP's
  SGIS section exact and Quake 2 using the extension on 86Box
  `Win98SE-Fast-D3D`
  (`docs/decisions/2026-10-05-sgis-multitexture-software-engine.md`).
- Step 4 (f7286f4): Gen3's two-unit program, state, vertex run and
  decoder licence, compiled into the 32-bit HAL only; I9XXCODE unchanged
  at 0xdfd9. No two-unit dword has run on the GPU.

## Why

Half-Life 1.1.1.0, GLQuake and Quake 2 draw the world twice without a
second texture unit: the surface texture, then the lightmap blended over
it. The ICD's measured costs scale with that geometry (Quake 2 demo2 on
the netbook, `docs/decisions/2026-10-01-gl-vertex-path-profile-quake2-netbook.md`):
`glVertex*` 1.86 us a vertex, 31% of wall; the held batch drawn when a
polygon cannot join it 5.67 us a polygon, behind an 84 us render-interface
draw for about every twelve polygons. The lightmap pass changes texture on
nearly every surface, and a texture change is what ends a batch.

All three games look for `GL_SGIS_multitexture` and none for
`GL_ARB_multitexture`
(`docs/decisions/2026-09-26-opengl-icd-interface-research.md`, Quake
census):

- tokens `TEXTURE0_SGIS` 0x835E, `TEXTURE1_SGIS` 0x835F;
- entry points through `wglGetProcAddress`: `glSelectTextureSGIS`,
  `glMTexCoord2fSGIS` (GLQuake, Quake 2);
- GLQuake matches `"GL_SGIS_multitexture "` with a trailing space, so the
  name must not end the string;
- Quake 2 modulates on unit 1; GLQuake uses `GL_BLEND` on unit 1 with an
  inverted luminance lightmap and the default black env colour.

**Unmeasured:** what Half-Life asks for and which env modes it sets per
unit. The ICD logs both (step 2); the first Half-Life run answers it.

## Design

### Render interface, ABI 4

The 32-byte screen-space vertex stays as it is, everywhere: every engine,
the clipper and the golden pipeline hash are built on it. The second
coordinate pair travels beside it.

- `V9X_R3D_ABI_DRAW` gains `texture1` (a `V9X_R3D_ABI_TEXTURE`, storage
  NONE when one unit) and `texcoords1`: two floats a vertex, `tu1, tv1`,
  in the vertex order, already divided as `tu`/`tv` are.
- `V9X_R3D_ABI_DESCRIBE` gains `texture_units`: 2 where the engine combines
  two textures in one pass, 1 otherwise. The ICD advertises the extension
  only on 2.
- Unit 1's coordinates are interpolated with the vertex's `rhw`. That is
  exact when unit 0's q is 1, which is every SGIS call the games make
  (`glMTexCoord2fSGIS` has no q). A projective unit-0 coordinate under
  multitexture is not exact and is recorded as such, not refused.
- A two-unit draw does not go through the HAL's list builder, which may
  clip, cull and stage, and so would separate a vertex from its
  `texcoords1` entry. The ICD has already clipped to the drawable and
  culled. The HAL checks every triangle is on the target and refuses the
  draw as INVALID otherwise.
- An engine whose describe says one unit refuses `texture1` centrally in
  the render interface: UNSUPPORTED.

### Combine

GL 1.1 table 3.18, unit 0 then unit 1, the previous colour feeding the
next unit:

| mode | colour | alpha (RGBA texture) |
|---|---|---|
| REPLACE | Ct | At |
| MODULATE | Cp Ct | Ap At |
| DECAL | Cp (1 - At) + Ct At | Ap |
| BLEND | Cp (1 - Ct) + Cc Ct | Ap At |

The render interface's per-texture `color_op`/`alpha_op`/`env_color`
already carry this for one unit; unit 1 takes the same fields. Fog follows
the second unit, then the alpha test, as the CPU rasterizer contract
orders it for one.

### Gen3

The part is built for it: eight samplers, a nibble per coordinate set in
S2, MAP_STATE and SAMPLER_STATE sized `3 * count`
(`docs/decisions/2026-09-16-intel-gen3-texture-packet-audit.md`). The
packet builders already take a count; `test_sampler_state` builds a
two-sampler packet. Everything around them is one unit:

- S2 is `TEXTURED_UNIT0` or all absent; unit 1 is `0xffffff00`.
- The vertex is 7 dwords (+1 fog); with unit 1 it is 9 (+1).
- The fragment programs are fixed builders that sample S0/T0 only. A
  two-unit program is a new builder, HAL only (beside `i9xx_fog.c`, kept
  out of the 16-bit I9XXCODE segment): texld R0 from S0/T0, texld R1 from
  S1/T1, the two combines on the diffuse colour, the BLEND env colours as
  constants, then the fog form. Gen3 has no LRP; DECAL and BLEND are an
  ADD with a negated source and a MAD.
- The decoder refuses any stream with two units. It gains a two-unit
  licence: both maps' addresses in the limits, map index 1 for sampler 1,
  S2's two-unit value, the 9/10-dword vertex, and the program checked word
  for word against the builder's output for the draw's ops, not by length.
- Capacity: 64 fogged two-unit triangles are 1,921 dwords of primitive
  against a 2,048-dword stream. The stream grows to fit, or the engine
  splits the batch; decided against the ring plan when it is written.

### Software engine

The reference, and the fallback: Gen3 refuses CPU texture levels, which is
what the ICD sends when a hardware copy cannot be made, and the render
interface falls back to the software engine only on Gen3. So a two-unit
draw that falls back needs the CPU rasterizer to combine two units. The
CPU rasterizer grows a second sampler with its own u/v on the shared q,
the combine chained per the table above, alpha chained inside the texture
stage. Its contract (`docs/specifications/cpu-rasterizer-contract.md`)
gets the two-unit section first, pinned by tests.

## Steps

1. **ABI 4.** Header, validator, describe's `texture_units`; the render
   interface converts `texture1` and `texcoords1` and refuses them on a
   one-unit engine. Host tests on the validator first.
2. **ICD.** Per-unit binding, enable, env mode and colour;
   `glSelectTextureSGIS`, `glMTexCoord2fSGIS`, `glMTexCoord2fvSGIS` through
   `DrvGetProcAddress`; `GL_SGIS_multitexture ` in `GL_EXTENSIONS` when
   describe says two units; the second coordinate through the pipeline,
   clipper and held batch; both units' hardware copies at draw. Unit-1 env
   modes counted in `V9X_GL.LOG`. Host tests first; the golden pipeline
   hash must not move.
3. **CPU rasterizer and software engine.** Contract section, tests, the
   second unit; the software engine says two units.
   - **Guest gate (no netbook):** V9XGLP grows an SGIS section (a quad with
     each env mode on unit 1, read back); Quake 2 demo on the Fast-D3D
     software guest (9878) finds `GL_SGIS_multitexture`, and a
     `timerefresh` frame matches the same frame with
     `gl_ext_multitexture 0`.
4. **Gen3.** Program builder, two-unit state, vertex run and decoder
   licence, each with dword tests; the engine binds two surfaces and
   accepts two-unit explicit draws; describe says two units.
   - **Netbook gate:** V9XGLP SGIS section exact against the software
     engine; Quake 2 demo1 `timerefresh` and Half-Life `-gl timedemo
     mwd5`, with and without multitexture (`gl_ext_multitexture 0`; for
     Half-Life the census of step 2 says how). Scores recorded, not
     compared as targets.

## Not in scope

`GL_ARB_multitexture`; texture coordinate sets selected apart from
textures (`glSelectTextureCoordSetSGIS`); per-unit vertex arrays; the
texture matrix (not applied for unit 0 either). Other engines keep one
unit; a hardware unit count is theirs to measure.
