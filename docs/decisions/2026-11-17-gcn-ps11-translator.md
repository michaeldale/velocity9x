# GCN: pixel shader 1.1 as GCN 3 code, held to the interpreter in a model and on the Oland

Date: 2026-11-17

Status: steps 1, 2 and 3 of [ati-gcn-ps11.md](../plans/ati-gcn-ps11.md)
done. Step 3 was measured on W9QI0DAD (Oland XT `1002:6610`, FX-6300,
Win98 SE) at boot 52, with `V9XSICP /ps11:N` built from 639763a1 plus
the probe stage (`639763a1-dirty` in the reports). It ran right after a
`reset`, with no Direct3D in between. Nothing is in the HAL yet:
`draw_shader` and DDI 8 are step 5.

## Step 1: the encoder and a model (ed674aae)

VOP3 with source negate and absolute value, `clamp` and `omod`;
`v_mad_f32`, `v_min_f32`, `v_max_f32`, `v_sub_f32`, `v_rcp_f32`,
`v_fract_f32`, `v_cmp_*` into VCC or an SGPR pair, the four `v_cube*`, and
`s_load_dwordx2/x4/x8`. Each word is pinned to LLVM's GFX8 bytes in
`test_vi_3d.c`, and its GCN 2 and GCN 1 rewrite to LLVM's SICI bytes (or
Mesa ACO's tables where `vop3.s` has none) in `test_cik_shader.c`. SMEM
becomes SMRD plus an `s_nop`, so programs keep their length and branch
targets. `tests\host\gcn_sim.c` runs one pixel of our GCN 3 words; it
agrees with the Direct3D pixel shaders that already draw right on the
cards.

## Step 2: the translator

`v9x_vi_ps11_build` (`src\chipsets\ati\vi\vi_ps11.c`,
`include\velocity9x\vi_ps11.h`) lowers `V9X_PS11_PROGRAM` the way the
Tesla translator does: each written channel computed into a result VGPR
and moved after, kills last, the program in whole-quad mode with the
pixels' EXEC back before the kills and the export. 72 VGPRs; two user
SGPRs hold the address of a 108-dword table (four T#s, four S#s,
c0-c7, bump matrices, luminance, fog colour), read with `s_load` only
for the blocks a program uses.

`test_vi_ps11.c` runs the corpus `test_nv50_ps11.c` uses (after Gen3's
and Haswell's), plus a negated, saturated, shifted `cnd`, a fogged `cnd`
and a program that loads every block. It runs them from garbage
registers and attributes. All 19 programs match `v9x_ps11_run` within
2e-4 in r0's VGPRs, and within 2e-3 in the FP16 export. Kills are the
same. Each program's GCN 2 and GCN 1 rewrites translate without
refusal. The largest program is 227 words, and the most SGPRs any
program uses is 96.

Five mutations, each caught:

- `v_cndmask` operands swapped: `cnd` cases wrong.
- The constant-bus spill removed: the model stops on "constant bus" in
  four programs.
- DA dropped from cube samples: the sampler reports DA on every cube
  pixel.
- The cube's +0.5 changed to +1.0: every cube case wrong.
- `_x2` given OMOD x4: the arithmetic case wrong.

### What the sources settled, and what is assumed

- **The constant bus.** A GCN 1-3 VALU instruction reads one SGPR,
  VCC, M0, EXEC or literal. VOP2 `v_cndmask_b32` reads VCC, so its src0
  cannot be an SGPR. The model now refuses a word that breaks this, and
  `test_gcn_sim.c` checks the refusal. The translator moves a second
  scalar into a VGPR (`mad r0, c2, c5, r1`; texbeml's scale and offset).
- **OMOD is the shift.** OMOD is honoured only while f32 denormals are
  flushed. FLOAT_MODE C0h, which `vi_3d.c`'s RSRC1 sets, flushes them
  (denorm f32 bits 5:4 are 0). `clamp` is the saturate. The [-1, 1] hold
  is a `v_max` and a `v_min` against inline constants. The Tesla
  translator used min and max for both, having no exact OMOD.
- **`v_cndmask_b32` and `v_mov_b32` take no source modifiers** in
  LLVM's GFX8 definitions. A negated `cnd` operand is made into a VGPR
  first. A shift or saturate after `cnd` or `mov` is a multiply by 1.0
  that carries OMOD and clamp.
- **Cube lookups** follow Mesa's `ac_prepare_cube_coords`:
  `(sc / |ma| + 1.5, tc / |ma| + 1.5, face)`. The 1.5 is not an inline
  constant and VOP3 takes no literal on GFX8, so it is `+ 1.0` inside the
  `v_mad` and `+ 0.5` after. The sample sets MIMG's DA bit (14). LLVM's
  `mimg.s` puts DA at the same bit under SICI and GFX89 (the
  `image_atomic_add ... da` lines, flags 41h). The model's face table is
  Mesa's `build_cube_select`, which the test inverts to recover the
  direction the interpreter samples. Mesa and the ISA guide agree on
  this convention, but nothing has been measured. Step 6 is the first
  run on a card.
- **SGPR budget.** Loads stop at s96, which leaves VCC and anything the
  SPI adds within the 104 SGPRs a wave may have. Four stages, all eight
  constants, a texbeml and fog together need 100, so that program is
  refused rather than spilled. The test checks that the same program
  without fog loads every block and runs. No shader in the corpus comes
  near the limit. Whether 3DMark 2001 SE's shaders do is not known.

## Step 3: on the Oland

`V9XSICP /ps11:N [/bpp16]` (`tools\diag\si_cp_win32.c`) brings the CP up
as `/rect` does, then draws one program as a rectangle. It judges every
pixel against `v9x_ps11_run`, which is linked into the probe, given the
same inputs and nearest samples of the same texels. The check is the
GT218's `/3d:ps11`, with its programs, attribute planes and texels:

- **The draw.** A rectangle (8, 4)-(56, 44) as two triangles, w 1, into
  a 64x48 target filled with a guard value first. Six 112-byte wide
  vertices (`d3d_ps.h`'s layout) carry the six attributes as planes
  over the rectangle.
- **New parts of the draw:**
  - `si_shader.c`'s new wide VS: seven `buffer_load_dwordx4 idxen`
    through a V# of stride 112, the position to POS0 and the rest to
    parameters 0-5;
  - the PS's two user SGPRs, the table's address;
  - RSRC1 SGPRS sized from the program;
  - four 8x8 A8R8G8B8 textures, nearest, clamped. These are
    `vi_3d.h`'s new appended `v9x_vi_tl_draw` fields, where zero keeps
    the old meaning.
- **Channels in their PS 1.1 places.** The T#s' DST_SEL is Z Y X W, so a
  texel's red is x. CB_COLOR0_INFO COMP_SWAP is SWAP_ALT for 8888 and
  SWAP_STD_REV for 565, so x is written to red. That is radeonsi's
  choice for BGRA and B5G6R5 (Mesa's `r600_translate_colorswap`; the
  field is at 12:11 in Linux's `gfx_6_0_sh_mask.h`). The HAL today draws
  in memory order (x is blue throughout), which fixed function does not
  notice. PS 1.1 does notice: constants, dot products and `.b` swizzles
  name channels. This run is the first measurement of the convention.
- **The rule.** A pixel is judged good within two steps a channel at 8
  bits (alpha included) and one step at 5 or 6 bits. A sample within
  0.05 texel of an edge, or a kill coordinate within 0.01 of zero, is
  left unjudged. Killed pixels must still read the guard, and so must
  every pixel outside the rectangle.

Six programs: the GT218's five (Arith, FourTex, Kill, Bem, Cnd), and Fog.
Fog's `mad r0, c2, c5, t0` makes the translator move a constant off the
constant bus, and its fog runs after the shader. Reports:
[`../probe/oland-ps11-w9qi0dad-2026-11-17/boot52/`](../probe/oland-ps11-w9qi0dad-2026-11-17/boot52/).

| Program (words, SGPRs) | judged, bad | unjudged | killed | worst step, 32 / 16 bpp |
|---|---|---|---|---|
| Arith (227, 20) | 1920, 0 | 0 | 0 | 1 / 1 |
| FourTex (159, 60) | 840, 0 | 1080 | 0 | 1 / 0 |
| Kill (81, 20) | 1220, 0 | 383 | 317 | 0 / 0 |
| Bem (89, 36) | 1303, 0 | 617 | 0 | 0 / 1 |
| Cnd (92, 12) | 1920, 0 | 0 | 0 | 0 / 0 |
| Fog (129, 32) | 1572, 0 | 348 | 0 | 1 / 1 |

The counts are the same at both depths. No pixel outside the rectangle
changed in any run. Every fence landed, the read pointer reached the
write pointer (700h), and GRBM_STATUS read A0003028h at the fence and
00003028h afterwards, the value before the run and the one every earlier
Oland probe run left. All twelve runs passed on the first build.

This settles the following, measured on GCN 1:

- **SMRD loads.** The table reads (the SMEM-to-SMRD rewrite, through
  the scalar cache) fetch descriptors and constants that sample and
  compute right. FourTex loads four T# and S# pairs.
- **Channel convention.** The T# swizzle and COMP_SWAP put red where
  PS 1.1 wants it, in all four channels at 32 bpp and all three at
  16 bpp. A wrong swap would have exchanged red and blue in every judged
  pixel.
- **Modifiers and kills.** OMOD and clamp give the interpreter's shift
  and saturate (Arith). The `v_cmp` and `v_cndmask` pair selects either
  way (Cnd). Kills through `v_cmpx` leave the guard, and the
  whole-quad-mode sampling around them holds (Kill).
- **Dependent reads.** texbem's dependent read through the bump matrix
  SGPRs is right (Bem).
- **Constant bus and fog.** The constant-bus spill and the fog blend are
  right (Fog).

Not covered by this run: cube maps and the texm3x3 family (step 6), and
texbeml. Mullins and Carrizo were not run.

## Not done

- Step 4 (two texture stages for fixed function) and step 5
  (`draw_shader` in the HAL, DDI 8 on the Oland row).
- The HAL still draws in memory order. Step 5 has to give PS 1.1 draws
  the swizzle and swap measured here, and colours in RGBA order from
  the wide vertex.
