# Pixel shader 1.1 on Gen3 (GMA 900, 950, 3150)

Status: 2026-11-11. Step 1 (this mapping) done at the desk; nothing coded
or measured. Started for 0.47.0 at Michael's request. Follows
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
2. **Encoder and host model.** `src\chipsets\intel\i9xx_fs.c`: emit any
   of the instructions above, each field placement checked against both
   sources, as the audit did. A host interpreter of the encoded program
   (`tests\host\`), registers as floats, TEXLD through a callback like
   the interpreter's. Host tests: encodings against the dwords the two
   sources imply; the existing fixed programs (`i9xx_fragprog.c`,
   `i9xx_multitex.c`) run in the model and give what they are documented
   to give.
3. **Translator.** `src\chipsets\intel\i9xx_ps11.c`: PS 1.1 to an encoded
   program. Host tests: every program the Haswell tests use, and
   Nature's, run in the model and match `v9x_ps11_run` per pixel;
   programs over a limit are refused, not mistranslated.
4. **Probe on the netbook** (W9QI2BE2, GMA 950): a Gen3 probe in the style
   of `V9XHSWMM /ps11`, drawing translated programs outside the HAL and
   comparing pixels with the interpreter. Also settles the two bump
   formats and cube sampling on the silicon.
5. **HAL.** `draw_shader` in `d3d_i9xx.c`: program and constants per
   draw (cached by program), four samplers, the vertex with four
   coordinate sets. Gate: V9XD32 T19 at 16 and 32 bpp on the netbook.
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
- Whether DDI 8 is offered on Gen3 at all today: the gate Haswell needed
  (`DDHALINFO_GETDRIVERINFO2`) is in the core, but nothing has asked on a
  Gen3 machine. Step 4 or 5 will see.
