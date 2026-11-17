# GCN: pixel shader 1.1 and DDI 8, after the HD 4600 and the GT218

Date: 2026-11-16. Status: steps 1 to 6 done on the Oland, step 7 in progress (4 and 5 below); steps 1, 2 and 3 recorded in
[`2026-11-17-gcn-ps11-translator.md`](../decisions/2026-11-17-gcn-ps11-translator.md).
Step 2's `v9x_vi_ps11_build` (`vi_ps11.c`) runs 19 programs in the GCN
model, each matching the interpreter, and each translates for GCN 2 and
1. Step 3, `V9XSICP /ps11:0-5`, ran six programs on the Oland at 32
and 16 bpp: every judged pixel was within tolerance, the CP was idle
afterwards, and all of it passed on the first build. The draw used
radeonsi's channel convention: T# swizzle Z Y X W, and COMP_SWAP ALT
for 8888 or STD_REV for 565. Step 5 must carry that convention into the
HAL.

Step 4 is done on the Oland alone
([2026-11-17-oland-two-texture-stages.md](../decisions/2026-11-17-oland-two-texture-stages.md)).
A per-draw two-unit PS loads both units' descriptors from a table, and
the TL2 VS reads unit 1's coordinates from a second stream. V9XD32 T12
and T13's two-unit form pass at 32 and 16 bpp, with nothing else changed.
3DMark 2000 scored once and faulted twice, the known fault at 004E23D1
in 3DMark's own keyframe code
([issue](../issues/2026-11-17-oland-3dmark2000-faults-with-two-stages.md)).
Carrizo and Mullins still draw one stage.

Step 5 is done on the Oland alone
([2026-11-17-oland-draw-shader-ddi8.md](../decisions/2026-11-17-oland-draw-shader-ddi8.md)).
The HAL draws pixel shaders and publishes DDI 8. V9XD32 T19 runs, with
the GT218's verdicts at the same step: Cube and Normal wait for step 6,
and FfSpecFog's 3 pixels are untraced on both. 3DMark 2001 SE completed
once (1915) and hung the machine once in Car Chase High, with the CP
stalled
([issue](../issues/2026-11-17-oland-3dmark2001se-cp-stall-hang.md)).
Step 6 is done on the Oland
([2026-11-17-oland-cubes-signed-formats.md](../decisions/2026-11-17-oland-cubes-signed-formats.md)).
Cubes are placed whole and sampled through a cube T#, and Q8W8V8U8 and
V8U8 are read as SNORM. V9XD32 T19 Cube and Normal pass with no bad pixel
at R5G6B5 and X8R8G8B8. 3DMark 2001 SE completed (3101) with no shader
draw refused. A cube's smaller levels and V8U8 are not yet read in a
judged draw. That run stalled the CP too, found afterwards (the issue).

Step 7 is done on the Oland in its sampling
([2026-11-17-oland-bc-1d-tiled.md](../decisions/2026-11-17-oland-bc-1d-tiled.md)).
Linear-aligned BC does not sample by addrlib's rule; 1D-thin tiles under
index 13 do, and the HAL places, locks and samples DXT in them. V9XD32
T19 Dxt1 and Dxt3 pass at both depths. Its 3DMark gate is not met:
the heap is the VBE's 16 MiB and 3DMark's DXT sets thrash it. Next:
the Evergreen rows' 128 MiB heap for the Oland, then 3DMark 2001 SE
again. The CP stall (three in four runs) is still open.

Step 1: `vi_shader.c` gains
`v9x_vi_vop3` and `v9x_vi_s_load`, and every new word is pinned to
LLVM's GFX8 bytes in `test_vi_3d.c`. The CI/SI rewrite maps them, plus
VOP3's promoted VOP1/VOP2/VOPC ranges and SMEM to SMRD + `s_nop`.
`test_cik_shader.c` checks this against LLVM's SICI bytes where `vop3.s`
has them, and against Mesa's ACO tables and emitter where it does not.
The one-pixel GCN 3 model is in `tests\host\gcn_sim.c`. It runs the
Direct3D pixel shaders that already draw correctly on the cards
(Gouraud, replace, modulate with specular, fog, alpha test, colour key)
and agrees with their results computed in C (`test_gcn_sim.c`).

Asked for (Michael, 2026-11-16): pixel shader support on the Oland, as the
HD 4600 ([`haswell-ddi8-pixel-shaders.md`](haswell-ddi8-pixel-shaders.md))
and the GeForce 210 ([`nvidia-gt218-ps11.md`](nvidia-gt218-ps11.md)) have
it. Test machine: W9QI0DAD's Oland XT (`1002:6610`), on branch
`ati-oland-bringup`, where Direct3D, 3DMark 2000/2001 SE, OpenGL and Glide
already run ([`ati-oland-gfx6-3d.md`](ati-oland-gfx6-3d.md)).

## One back end, three chips

`d3d_vi.c` is the Direct3D back end of Carrizo (GCN 3), Mullins (GCN 2)
and the Oland (GCN 1). Its shaders are written once, in GCN 3 words
(`vi_shader.c`), and rewritten for the older ISAs by `cik_shader.c`
(`v9x_cik_shader_from_vi`, `v9x_si_shader_from_vi`). Pixel shaders go
the same way. The PS 1.1 translator emits GCN 3 words. Every new
instruction gets a row in the CI and SI rewrite tables, checked
against LLVM's tables. Carrizo and Mullins get the same code: a fix measured on one chip
goes to every chip of that design (Michael, 2026-10-04). Each chip's 16-bit row stamps
`V9X_DD_ENGINE_CAP_D3D_DDI8` only once that chip has passed the step 5
gate. The Oland comes first, and Carrizo and Mullins follow when their
machines are available.

## What carries over

As for the GT218: DDI 8 caps and formats, the PS 1.1 parser and IR
(`ps11.h`, `d3d_ps11.c`), the reference interpreter `v9x_ps11_run`, the
shader table, constants and the wide vertex (`d3d_ps.c`), the wide clipper
`v9x_r3d_clip_wide`, and fixed-function DOT3 / BUMPENVMAP through
`v9x_d3d_ps_fixed`. The back end writes the lowering and `draw_shader`.

**T&L is left out**, for the same reason as on the GT218: the HD 4600's
T&L device measured slower than the runtime's transform, and DDI 8 over
DDI 6 is how Gen3 and the GT218 got pixel shaders.

## The GCN back end today

- One texture: its T# and S# fill the PS's 12 user SGPRs
  (`V9X_VI_TEXTURE_USER_SGPRS`). Formats are 8888, XRGB8888 (W forced to
  1), 565, 1555 and 4444, with mips, and wrap or clamp addressing.
- `texture_stages = 1`. `clip_in_core = 0`: the PA's guard band.
- TL vertex 32 bytes, three parameters (`VI_TL_PARAMS`). The vertex
  shader is FLAT on GCN 2/3 and MUBUF on the Oland (`si_shader.c`).
- Built pixel shaders: `v9x_vi_shader_d3d_ps_built`, at most 96 dwords
  and 20 VGPRs, put into the vertex arena ahead of each batch.
- The encoder has: mov, add, subrev, mul, mac, and, lshlrev, cndmask,
  add_u32/addc, cvt_f32_ubyte*, cmpx_*, cvt_pkrtz (VOP3), interp
  p1/p2, exp (and compressed), image_sample, s_mov, s_waitcnt, s_nop,
  s_endpgm, s_cbranch_execz, flat load/store.

## Steps

Each step has a gate. Card steps run on W9QI0DAD, which restarts with
`reset`.

### 1. Encoder, and a host model of what it emits

Add, each with a host test holding LLVM's bytes (`gfx8_asm_*.s` in
`build\external\carrizo\llvm-mc\`):
- VOP3 with source negate and absolute value, `clamp`, and `omod`
  (PS 1.1's `_x2`, `_x4`, `_d2` are omod 1, 2, 3);
- `v_mad_f32`, `v_min_f32`, `v_max_f32`, `v_sub_f32`,
  `v_rcp_f32`, `v_cmp_*` into an SGPR pair (for CND's select);
- the cube instructions (`v_cubeid`, `v_cubesc`, `v_cubetc`,
  `v_cubema`), for texm3x3 and cube stages (step 6 uses them);
- `s_load_dwordx4` / `x8` (SMEM on GCN 3), for descriptors and
  constants from a table in memory (step 2).

Then a host model of that subset: decode our own GCN 3 words and
execute them for one pixel (VGPRs and SGPRs per lane, a texture callback
for image_sample, kill through EXEC), the way the Haswell test runs an
EU model. The CI and SI rewrite tables gain each instruction that
differs, each checked against `VOP*Instructions.td`.
**Gate:** build-host.

### 2. Translator: PS 1.1 IR to a GCN pixel shader

`v9x_vi_ps11_build` in `src/chipsets/ati/vi/`, with its own header,
built like `v9x_nv50_ps11_build`:
- **Inputs:** diffuse, specular and four coordinate sets, interpolated
  with `v_interp_p1/p2`: six parameters.
- **Descriptors:** the user SGPRs hold one 64-bit address: a table of
  four T#s, four S#s and the constant block (c0-c7 clamped to [-1, 1],
  bump matrices, luminance, fog colour), read with `s_load`.
- **Output:** r0 exported to MRT0, with fog as Gen3's and Haswell's rule
  (`i9xx_ps11.h`).
- **Run on GCN 2 and GCN 1** through `v9x_cik_shader_from_vi` and
  `v9x_si_shader_from_vi`. Those need SMEM rewritten to SMRD, which has a
  different encoding and offsets in dwords on SI/CI.

**Gate:** build-host. Every program in `test_d3d_ps11.c`'s corpus, run
through the translator and the model, matches `v9x_ps11_run` within the
Gen3 test's precision. The CI and SI rewrites of each program translate
without refusal.

### 3. On the card, outside the HAL

`V9XSICP /ps11`, on the pattern of `/texture` and `/modulate`: the wide
vertex shader (MUBUF on SI), a translated program, a quad with up to
four textures, read back and compared with the interpreter.
**Gate:** the corpus exact or within the stated precision, at 32 and 16
bpp. No hang, and the CP idle afterwards.

### 4. Two texture stages

Two T#/S# pairs for the fixed-function path (`texture_stages = 2`),
through the same descriptor table. Without them the core claims DOT3 and
BUMPENVMAP with nothing to draw them (`d3d_core.c`'s stage pairing).
**Gate:** V9XD32 T10, T12 and T13 (they skip today, as on every GCN chip),
and 3DMark 2000 still runs (scores recorded, not compared).

### 5. `draw_shader` in the HAL, and DDI 8

- the wide vertex (28 floats) and its vertex shader, VI and SI;
- a program cache in the shader arena (programs keyed by the IR, env bits
  and fog, as on the GT218), translated per chip;
- the descriptor table in the vertex arena ahead of each batch;
- `v9x_r3d_clip_wide` in the back end;
- `V9X_DD_ENGINE_CAP_D3D_DDI8` in `oland_engine16.c`.

**Gate:** V9XD32 T17 and T19 (T19 needs a 32 bpp or R5G6B5 desktop) pass.
V9XD32 T3-T22 and V9XDDP are unchanged from boot 51's runs. 3DMark 2001 SE
runs with the pixel shader tests listed.

### 6. Cube maps and bump formats

Cube placement in `create_surface`, a cube T#, and the signed formats
Q8W8V8U8 and V8U8. **Gate:** T19 with cubes and the FfBem rows.

### 7. DXT

BC1-3 in a T#. GCN samples them in linear-aligned tiling, which is what
textures use here, but nobody has measured it on this card: probe first.
**Gate:** T19's Dxt1 rows, and 3DMark 2001 SE's texture tests.

### 8. 3DMark 2001 SE Nature

**Gate:** Nature runs, and its Image Quality frame is inspected.

## Risks

- **SMEM against SMRD.** SMEM (GCN 3) and SMRD (GCN 1/2) differ in
  encoding and in offset units. The descriptor loads are the first scalar
  memory reads in these shaders. The rewrite needs its own host test and
  a card run on the Oland before Mullins.
- **Instruction words on three ISAs.** An opcode right on GCN 3 and
  missing from a rewrite table is refused, which is safe. A wrong table
  entry hangs or misdraws. Each entry cites its `.td` line, and step 3
  runs every corpus program on the Oland.
- **VGPR budget.** A 24-instruction PS 1.1 program with four coordinate
  sets fits well inside 64 VGPRs, so occupancy is not a concern here.
