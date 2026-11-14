# Pixel shader 1.1 on Gen3 (GMA 900, 950, 3150)

Status: 2026-11-11. Step 1 (this mapping) done at the desk. Step 2 done
on the host: the encoder (`src\chipsets\intel\i9xx_fs.c`,
`include\velocity9x\i9xx_fs.h`) reproduces the untextured, sampling and
modulate programs dword for dword, and a host model of the program
(`tests\host\i9xx_fs_sim.c`) runs seven fixed programs to their
documented answers. Step 3 done on the host: the translator
(`src\chipsets\intel\i9xx_ps11.c`) matches the reference interpreter
per pixel in the model on every Haswell test shader. Nothing measured
on hardware. Started for 0.47.0 at
Michael's request. Follows
[`haswell-ddi8-pixel-shaders.md`](haswell-ddi8-pixel-shaders.md), whose
shared half this reuses whole.

## Goal

DirectX 8 pixel shaders 1.0 and 1.1 on the Gen3 engine, so that a GMA
900, 950 or 3150 runs what the HD 4600 runs: V9XD32 T19, and 3DMark 2001
SE's Nature, Pixel Shader and Advanced Pixel Shader tests. Speed is not
the goal; on a GMA 950 those tests will be slow.

## What exists, and what this adds

Shared, engine-neutral, done for Haswell:

- DDI 8 (GetDriverInfo2, `D3DCAPS8`, the format list), the runtime's
  short-lived surface halves, SETPIXELSHADER and its constants.
- `v9x_ps11_parse` (`include\velocity9x\ps11.h`, `d3d_ps11.c`): PS 1.0
  and 1.1 into an instruction list. tex, texcoord, texkill, texbem,
  texbeml and the texm3x3 family; texreg2ar/gb and texm3x2 refused.
- `v9x_ps11_run` (`d3d_ps11_ref.c`), the host-only reference interpreter
  every translator is held to, with its stated choices (texbeml on
  colour only, projection for tex and texbem only).
- The core's claim: an engine whose `V9X_D3D_ENGINE_OPS` has
  `draw_shader` gets PS 1.1, four stages, four coordinate sets and cube
  maps advertised (`d3d_core.c:8095-8124`).

What Gen3 needs:

1. A **general fragment-program encoder**. Gen3 today emits fixed
   programs (`i9xx_fragprog.c`, `i9xx_multitex.c`), under the GMA 950
   plan's rule that Phase 6 writes no shader compiler. That rule was for
   fixed function; this plan supersedes it for PS 1.1 only.
2. A **host model of the fragment program**, so the translator can be
   held to the reference interpreter as Haswell's was in its EU model.
3. The **translator**, PS 1.1 to i915 fragment program.
4. **`draw_shader`** in `d3d_i9xx.c`: the program, its constants, four
   samplers (2D, cube, the two bump formats), four coordinate sets.

## The target: the i915 fragment program

From the sources the 2026-09-14 packet audit pinned
([`../decisions/2026-09-14-intel-gen3-3d-packet-audit.md`](../decisions/2026-09-14-intel-gen3-3d-packet-audit.md)):
xf86-video-intel 2.20.19 `src\i915_reg.h` and Mesa classic i915
`i915_reg.h`/`i915_program.h`, both under `build\external\audit\`. To be
double-sourced again, field by field, when the encoder is written.

- Three dwords an instruction. Limits: 64 ALU, 32 texture, 27
  declarations, **4 texture indirections** (phases), 16 temporaries
  (`R`), 32 constants (`CONST`), `T0`-`T7` texture coordinates, `T8`
  diffuse, `T9` specular, `T10` fog in w, `oC` the colour out.
- ALU: ADD, MOV, MUL, MAD, DP2ADD, DP3, DP4, FRC, RCP, RSQ, EXP, LOG,
  CMP (`src0 >= 0 ? src1 : src2`), MIN, MAX, FLR, MOD, TRC, SGE, SLT.
  A saturate bit and a four-bit write mask per instruction.
- Sources: a per-channel swizzle that can select X, Y, Z, W, **ZERO or
  ONE**, and a per-channel negate. No other source modifier.
- **One constant register per instruction** (it may be read by several
  of its sources).
- Texture: TEXLD, TEXLDP (divides xyz by w), TEXLDB, TEXKILL (kills when
  **any of the four** channels of its address is negative). The address
  is a `T` register, or `R` for a dependent read, which starts a new
  phase. `oC` may be a texture destination once a phase.
- Declarations: each `T` read and each sampler, with sample type 2D,
  CUBE or VOLUME. T declarations allow only x, xy, xyz, w or xyzw, and
  never xz, xw or xzw on diffuse or specular (an erratum both sources
  record).
- Sampler formats include `MT_16BIT_88DVDU` (V8U8) and
  `MT_32BIT_QWVU8888` (Q8W8V8U8), the two bump formats Nature uses.

## The mapping

PS 1.1 register files: `r0`-`r1` to `R0`-`R1`; `v0`, `v1` to `T8`, `T9`;
`c0`-`c7` to `CONST0`-`CONST7`, clamped to [-1, 1] when loaded, as the
interpreter reads them (the shader's own `def` values the same); `t0`-`t3`
to `R2`-`R5`, holding what each texture instruction produced. Fixed
constants the expansions need (0.5, 2, the reflection's 2) go in
`CONST8` up.

| PS 1.1 | i915 |
|---|---|
| mov, add, mul, mad, dp3 | the same instruction |
| sub | ADD with src1 negated |
| lrp d, a, b, c | `ADD tmp, b, -c` then `MAD d, a, tmp, c` |
| cnd d, r0.a, b, c | `ADD tmp, half, -r0.a` then `CMP d, tmp, c, b` (`0.5 - a >= 0` picks c, so `a > 0.5` picks b, exactly) |
| source `-x` | the negate bits |
| `1 - x` | `ADD tmp, ONE, -x` (swizzle ONE) |
| `_bias` | `ADD tmp, x, -half` |
| `_bx2` | `MAD tmp, x, two, -ONE` |
| `_x2`, `_x4`, `_d2` | the instruction into a temporary, then `MUL` by 2, 4 or 0.5 |
| result range | the interpreter holds results to [-1, 1] unless saturated: `MIN`/`MAX` against ONE after each arithmetic result, or the saturate bit |
| write mask, co-issue | the write mask; a co-issued pair is two instructions with disjoint masks |
| two constants in one instruction | MOV one into a temporary first |
| tex tN | `DCL` the coordinate set and sampler, `TEXLD RN+2, TN, SN`; projected stage TEXLDP; cube stage CUBE |
| texcoord tN | `MOV_SAT RN+2.xyz, TN` and `MOV RN+2.w, ONE` |
| texkill tN | PS 1.1 kills on x, y or z below zero and ignores w; TEXKILL reads all four. `MOV tmp, TN.xyz1` then `TEXKILL tmp`: a dependent read, so one phase |
| texbem tN, tM | `MAD`s for u + m00 du + m10 dv and v + m01 du + m11 dv, matrix in constants, then TEXLD from that temporary: one phase |
| texbeml | texbem, then the colour by `ls * tM.b + lo` |
| texm3x3pad / tex / spec / vspec | DP3 rows into a temporary; tex samples it, a cube stage after dividing by the largest magnitude (`MAX` and `RCP`); spec and vspec form the reflection `2 N (N.E) / (N.N) - E` with `RCP`, the eye from a constant (spec) or the three coordinate sets' w (vspec); one phase |
| an unbound stage | no sample: `MOV RN+2, (0, 0, 0, 1)` by swizzle |
| fog after the shader | as Haswell's kernels: `(r0 - fog) f + fog` with f in `T9.w`, the colour in a constant |
| r0 out | `MOV oC, R0` (saturated: what the blender takes is [0, 1]) |

Budgets: PS 1.1 allows 8 arithmetic and 4 texture instructions. The
worst expansion here is about 6 ALU instructions for one arithmetic
instruction with three modified sources and a shift, so about 50 of the
64. Dependent reads (texkill, texbem, the texm3x3 family) each start a
phase; a program needing more than 4 phases is refused at
CreatePixelShader, as any other untranslatable program is.

## Steps

1. **Mapping** (this document). Done 2026-11-11.
2. **Encoder and host model.** Done 2026-11-11 on the host.
   `src\chipsets\intel\i9xx_fs.c` emits any declaration, arithmetic or
   texture instruction above. `tests\host\i9xx_fs_sim.c` decodes the
   dwords with its own field numbers from Mesa's header and runs them on
   floats, TEXLD through a callback. `tests\host\test_i9xx_fs.c` checks
   the encoder against three fixed programs dword for dword, and fields
   no fixed program sets against Mesa's shifts. It also runs the
   untextured, DECAL, three modulate and two fog programs in the model
   to their documented answers, and every opcode the translator will
   use. The two-unit programs (`i9xx_multitex.c`) are not run in the
   model.

   The encoder refuses, rather than fixes up:
   - two constant registers in one instruction;
   - more than 64 ALU, 32 texture or 27 declaration instructions;
   - more than 4 phases, counted by Mesa's rule;
   - a T register read before it is declared, an R register read
     before it is written, a U register read across a phase;
   - RCP, RSQ, EXP, LOG, or DP2ADD's addend, on a source that is not
     one channel replicated, since which channel the hardware takes is
     not known.
3. **Translator.** Done 2026-11-11 on the host.
   `src\chipsets\intel\i9xx_ps11.c` (`include\velocity9x\i9xx_ps11.h`)
   turns a parsed PS 1.0/1.1 program into a fragment program, given the
   draw's cube, projected and unbound stages and fog. It also builds the
   constant block the HAL will load: C0-C7 (def or application,
   clamped), the bump matrices, the luminance terms, (0.5, 2, 4) and the
   fog colour.

   `tests\host\test_i9xx_ps11.c` runs every shader
   `test_intel_hsw_ps11.c` runs on eight pixels in the model. Each
   result matches `v9x_ps11_run` within 2e-4, and the same pixels are
   killed. The shaders cover tex (2D, cube, projected), texkill,
   texbem/texbeml, the texm3x3 family, co-issue, every arithmetic
   instruction and modifier, fog and unbound stages. Two cases are new
   here: two constant registers in one instruction, and r0 never
   written. Nature's own shaders were not available to run; these are
   the Haswell tests' versions of its patterns.

   A program over a limit is refused: a fifth phase, and texbem on a
   cube stage. The largest test, eight arithmetic instructions with
   every modifier, is 38 instructions with its declarations, of 64 ALU
   allowed. A deliberately swapped CND made the comparison fail, so the
   comparison is not vacuous. The tests ran against a stub translator
   first.

   Two choices the hardware has not confirmed:
   - cube directions go to the sampler unscaled, where the interpreter
     divides by the largest magnitude (both drivers pass them unscaled);
   - texm3x3tex on a stage that is not a cube is declared VOLUME, the
     interpreter's three-coordinate lookup.

   Step 4 settles both.
4. **Folded into step 5** (2026-11-11, Michael's choice). The plan was a
   Gen3 probe like `V9XHSWMM /ps11`, drawing translated programs outside
   the HAL. No Gen3 MMIO probe exists to extend: it would need its own
   ring, GTT and surface setup, and Haswell's probe is 3,900 lines.
   V9XD32 T19 already judges every pixel of ten PS 1.1 programs against
   the same interpreter, through Direct3D 8: cube, texbem, texkill, fog
   and empty stages among them. So T19 on the netbook is the probe, once
   the HAL draws shaders. The cost: a failure is less isolated, since
   the HAL's state and texture placement are in the path too. T19
   reports each program separately, which narrows it.
5. **HAL.** Several sessions. `draw_shader` in `d3d_i9xx.c` makes the
   core advertise PS 1.1, cube maps and the Q8W8V8U8 and V8U8 formats
   at once (`d3d_core.c:8095-8170`). Gen3 has none of those today: its
   maps are 565, 1555 and 4444 only. Every stream also passes the
   allowlist decoder before the ring. In order:
   - **5a. Map formats.** Done 2026-11-11, host only. T19 makes its
     colour textures A8R8G8B8, and Gen3 published only 565, 1555 and
     4444, so ARGB8888 and XRGB8888 were needed as well as the two
     signed formats. Now:
     - the map builder and decoder accept `MAPSURF_32BIT` ARGB8888
       (180h), XRGB8888 (190h) and QWVU8888 (1A0h), and `MAPSURF_16BIT`
       88DVDU (128h), with four bytes a texel of pitch for the 32-bit
       ones. All of it sits behind `V9X_I9XX_TWO_UNITS`, so the 16-bit
       I9XXCODE build is unchanged;
     - `v9x_d3d_i9xx_map_format` classifies a surface's pixel format.
       The signed ones are read as Haswell reads them: D3DFORMAT or
       FourCC 60/63, or DirectX 7's BUMPDUDV for V8U8;
     - the mip layout and the map footprint take two or four bytes a
       texel;
     - the HAL places, verifies and binds all seven formats, and
       publishes ARGB8888 and XRGB8888 as texture formats four and
       five, as the HD 4600 does.

     The two 32-bit colour formats reach every Direct3D title on Gen3,
     not only shaders. The signed formats stay unpublished until 5e,
     since the core lists them only for an engine with `draw_shader`.
     Neither reference driver ever selects 88DVDU or QWVU8888, so their
     channel order and signed range come from the names alone.

     Measured the same day on the netbook (boot 301, 25e042f1):
     V9XD32's new T22 samples ARGB8888 and XRGB8888 texels exactly at
     32 bpp and to 565 rounding at 16, with every draw on the engine.
     3DMark 2001 SE, set to 32-bit textures, ran all 17 tests at 16 and
     32 bpp: 1.2 million engine draws each, none refused, no texture
     format refusals. No counter shows which format 3DMark's textures
     got. Record:
     [`../decisions/2026-11-11-gen3-32bit-textures-netbook.md`](../decisions/2026-11-11-gen3-32bit-textures-netbook.md).
   - **5b. Cube maps.** Done 2026-11-11, host only; nothing places a
     cube until 5e, since the core claims cube maps only for an engine
     with `draw_shader`.
     - **Layout.** `v9x_d3d_i9xx_layout_cube` follows Mesa gallium's
       `i9x5_texture_layout_cube`, which the 915 and the 945 both use
       for uncompressed formats: six faces in Direct3D's order, each
       face's top at a table offset times the edge, and each level
       stepped by a second table times its halved edge. The pitch is
       twice a face row aligned to 4 (not the 2D tree's 64); the rows
       are four edges. Host-tested against offsets worked by hand from
       the tables (16x16 by five levels, at 2 and 4 bytes; 256 and 1
       texel).
     - **Single-sourced.** xf86 samples no cubes, so Mesa gallium is the
       only source here.
     - **State.** A cube sampler needs nothing new: the builder's clamp
       form already sets TCX, TCY and TCZ to CLAMP_EDGE, Mesa's cube
       sampler. The map needs MS4's six face enables, bits 15-20 in
       both headers; that is 5c's to emit.
     - **Placement.** The HAL places a cube the runtime hands it whole
       (six faces, each surface's face from DDSCAPS2 and level from its
       edge, +X's top first) as one block. It remembers the cube by
       +X's offset, format and levels, and forgets it when the block is
       freed. This is Haswell's create_cube on the Gen3 layout.
   - **5c. Shader stream builders.** Done 2026-11-11, host only.
     - **State block** (`v9x_i9xx_build_shader_state`, `i9xx_3d.c`): the
       two-unit block's packets in its order. MAP_STATE and
       SAMPLER_STATE carry the sampled stages, which need not be
       contiguous. They are built compacted with the shared builders,
       then the enable masks and each SS3 map index are set to the stage
       numbers, as Mesa's update_samplers writes them. A cube's MS4
       gains the six face enables. S2 declares sets 0 to n-1 as
       TEXCOORDFMT_4D, and S4 always carries the specular colour (T9,
       the fog factor in its alpha).
     - **Constants packet** (`i9xx_shader.c`): C0 to n-1.
     - **Vertex run** (`i9xx_shader.c`): the core's 28-float wide
       vertices as position, diffuse and specular packed to ARGB
       (clamped), then the sets. It applies the runtime run's position
       checks, and refuses any non-finite coordinate in an emitted set.

     All of it is 32-bit only, so I9XXCODE is unchanged. Host tests
     (failing against stubs first, 27 checks) cover non-contiguous stages
     with a cube, S2/S4, the retarget, the constant layout, colour
     packing and the refusals. None of it has been decoded or run.
   - **5d. Decoder licence.** Done 2026-11-11, host only.
     `v9x_i9xx_decode_shader_stream` is a third mode of the one decoder
     walk, beside the two-unit one, 32-bit only.
     - **Declaration.** Besides the runtime limits (with no texture
       range, fog or cylinder), the caller declares the sampled stages,
       the cubes, the sets, the constant count and each stage's map.
     - **Map and sampler state.** Each entry must equal its declaration.
       A cube's MS4 must carry exactly the six face enables, and its
       sampler must be the clamp form. Each sampler must name its own
       stage's map, and each packet appears once.
     - **Vertex layout.** S2 must declare the sets and S4 the specular
       colour. Every coordinate must be finite, and positions get the
       runtime checks.
     - **Constants.** One packet for C0 to n-1, before the program.
     - **Program.** Read like the two-unit licence, for what names
       memory or state. It may declare only T0 to sets-1, T8, T9 and the
       sampled stages' samplers, CUBE or 2D as declared. Texture
       instructions may go only through declared samplers, and sources
       must be temporaries, declared T or loaded constants. It may write
       no oD, must write oC, and stays within the fragment program's
       limits.

     Host tests (failing against a stub first, 28 checks):
     - a whole stream is licensed, and is refused by the plain licence;
     - fourteen mutations are each refused: map address, cube face
       enables, stage mask, sampler map index, a declared non-cube, S2,
       constant count, 2D sampling of a cube, an undeclared sampler, a
       constant past the loaded ones, T past the sets, a vertex off the
       target, a NaN coordinate, a missing MAP_STATE; plus fog or a
       texture range declared beside a shader;
     - every step 3 test program in a stream is licensed, except
       texm3x3tex on a non-cube stage. That one is declared VOLUME and
       refused, as this plan says.
   - **5e. `draw_shader`.** Written 2026-11-11 in `d3d_i9xx.c` and in
     the ops table, with no INI key. It builds and links in the HAL, and
     run-checks is green. **Not run on any machine.**
     - **Stages.** Each sampled stage is a placed cube (as its block,
       clamped) or bound through the fixed-function bind, or else it is
       unbound.
     - **Program.** Translated once per shader and its cube, projected,
       unbound and fog bits, in an eight-entry cache. Its own
       declarations give the stages and sets the state block and licence
       follow.
     - **State and constants.** The target, depth, blend and alpha test
       are bound as the fixed-function draw binds them; the 18 constants
       come from `v9x_i9xx_ps11_constants`, with the draw's fog colour.
     - **Vertices.** Triangles wholly on the target go as they are; any
       other is cut to the target by `v9x_r3d_clip_wide` or dropped and
       counted (`ps11_triangles_dropped`). A vertex is made what the run
       builder takes: +0.0, depth at the edge, coordinates finite, and a
       triangle with no legal rhw dropped.
     - **Submission.** Runs as large as the 2,304-dword buffer holds
       after the head (about 26 triangles at four sets), each licensed by
       `v9x_i9xx_decode_shader_stream`, then submitted with the
       breadcrumb.
     - **Build fix.** The colour bytes convert by `fistp`, as `d3d_fog.c`
       does: the HAL links no runtime, and a cast called `__CHP`.

     With `draw_shader` set, the core claims PS 1.1, four stages, cube
     maps, Q8W8V8U8 and V8U8 for every Gen3 machine. **0.47.0 must not
     ship this without 5f.**
   - **5f. Netbook.** In progress 2026-11-11
     ([record](../decisions/2026-11-11-gen3-ps11-netbook-first-runs.md)).
     - **T19 PASS** at R5G6B5 and X8R8G8B8, all eight programs, after two
       fixes:
       - Gen3 offered DDI 8 (it never reached Gen3, since the core gave
         it only over DDI 7, which needs T&L);
       - the half-pixel move in `draw_shader`.
     - **3DMark 2001 SE** runs Nature (2.8 fps) and the shader tests,
       with no draw refused once signed textures and cubes the heap
       cannot hold are answered out of video memory.
     - **But it stops in Advanced Pixel Shader** on a CreateTexture the
       runtime refuses before the driver sees it ("D3DERR_INVALIDCALL").
       Before 5e it completed (489), skipping those tests, so this is a
       regression that blocks 0.47.0 until it is found or `draw_shader`
       is withheld.
     - **Found and fixed** (boots 306-308,
       [record](../decisions/2026-11-11-gen3-texture-size-2048.md)): a
       2048x2048 map against Gen3's 1024 cap. With 2048 claimed (a 2048
       edge sampled exactly by V9XTSHP) the run completes, 528, and
       Advanced Pixel Shader reports itself unsupported instead of
       stopping. It never draws on the netbook.

     The run as first planned: V9XD32 T19 at 16 and 32 bpp on the
     netbook (W9QI2BE2, GMA 950).
     This is old step 4's evidence: it settles the bump formats, cube
     sampling with an unscaled direction, and whether DDI 8 is offered
     on Gen3 at all.

   A texm3x3tex stage must hold a cube or a volume texture. Gen3 will
   place no volume textures, so such a draw with a 2D texture bound is
   refused rather than drawn as a volume.
6. **3DMark 2001 SE** on the netbook: Nature and the two pixel-shader
   tests start, draw, and refuse nothing. Then the GMA 3150 (TOMMO33) and
   the GMA 900 if their owners are about.
7. **Record.**

## Not known

- Whether the netbook's 3DMark 2001 SE asks for anything the HD 4600's
  did not (its Dragothic sky is already wrong there:
  [`../issues/2026-11-07-gma950-3dmark2001se-dragothic-sky-white.md`](../issues/2026-11-07-gma950-3dmark2001se-dragothic-sky-white.md)).
- The fragment program's precision: the sources call it fp32-capable;
  not measured.
- Which channels TEXKILL tests. `_3DSTATE_RASTER_RULES` selects
  TEXKILL_3D or TEXKILL_4D; xf86 and Mesa both send 4D, and this tree
  never sends the packet, so the netbook runs the hardware default. The
  mapping's `MOV tmp, TN.xyz1` gives the same answer under either.
- Which channel RCP, RSQ, EXP and LOG read (x, or each channel). Mesa
  always replicates the source; the encoder requires that, so either
  reading gives the same answer.
- Whether DDI 8 is offered on Gen3 at all today: the gate Haswell needed
  (`DDHALINFO_GETDRIVERINFO2`) is in the core, but nothing has asked on a
  Gen3 machine. Step 4 or 5 will see.
