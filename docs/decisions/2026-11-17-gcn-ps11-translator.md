# GCN: pixel shader 1.1 as GCN 3 code, held to the interpreter in a model

Date: 2026-11-17

Status: steps 1 and 2 of [ati-gcn-ps11.md](../plans/ati-gcn-ps11.md)
done, host only. Nothing has run on a card: step 3 (`V9XSICP /ps11` on
W9QI0DAD's Oland) is next, and nothing is in the HAL.

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

## Not done

- Nothing ran on the Oland, Mullins or Carrizo. In step 3 the probe
  judges every corpus program on the card at 32 and 16 bpp.
- `vi_ps11.c` is in the host build only. It joins the HAL in step 5.
