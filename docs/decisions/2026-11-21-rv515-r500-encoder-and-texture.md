# RV515 texturing through an R500 program from a host-tested encoder

Date: 2026-11-21
Machine: remote-help `M-5CAPM3`, Radeon X1300 `1002:7146`, boot 44,
800x600 at 16 and 32 bpp (ati `21e0358f` installed).
Code: `src\chipsets\ati\rv515\r500_fp.c`, `include\velocity9x\r500_fp.h`,
`tests\host\test_r500_fp.c`; probe `V9XR53D` from `d4a0fe79`'s tree.
Reports in
[`docs/probe/m-5capm3-rv515-2026-11-21/3d-texture/`](../probe/m-5capm3-rv515-2026-11-21/3d-texture/).
Plan: [`ati-rv515-x1300.md`](../plans/ati-rv515-x1300.md), phase 5.

## The encoder

`r500_fp.c` builds the R500's six-dword pixel-shader instructions:
- TEX LD and TEXKILL.
- ALU MADs: three source slots per unit (a temporary or a constant), a
  slot, swizzle and modifier for each of A, B and C, and an output or
  temporary write with optional clamp and texture wait.

It refuses out-of-range fields, a full program and an empty one, and a
refusal sticks.

The host test holds it to words printed by a scratch program compiled
against Mesa 22.3's `r300_reg.h`, which ORs the `R500_*` defines in the
shapes xf86-video-ati 6.14.6's R500 composite builds:
- the single-texture program;
- the texture-times-mask program;
- the colour program the first draw ran on this card;
- a TEXKILL;
- a negated constant written to a temporary.

The golden program is not in the tree, only its output and how it was
made. The constant and TEXKILL forms have not run on silicon.

## On the card

The probe's textured tests are the RS690 probe's, drawn through the R500
fixup. The setup that replaces the R300 rasterizer and shader writes:
- `R500_RS_IP_0` = `00FFE040`: S and T from interpolators 0 and 1, R and
  Q the constants 0 and 1, colour 0 RGBA.
- `R500_RS_INST_0` = `00050010`: texture coordinates into temp 0, colour
  into temp 1.
- The program from `r500_fp.c`: LD temp0 from unit 0, then OUT temp0 * 1
  (REPLACE) or temp0 * temp1 (MODULATE).

The R300 texture-unit registers (`TX_*`, 4400h-45FFh) are written as the
RS690 builder writes them.

| Test | 16 bpp | 32 bpp |
|---|---|---|
| TexDecal (1:1, nearest, clamp) | PASS, 0 bytes wrong | PASS, 0 bytes wrong |
| TexWrap (twice across, wrap) | PASS, 0 bytes wrong | PASS, 0 bytes wrong |
| TexModulate (texel * flat colour) | PASS, worst 0.88 step | PASS, worst 0.51 step |
| TexBilinear (2x, linear, clamp) | PASS, worst 0.75 step | PASS, worst 3.5 steps |
| QuadAfterTex (colour state again) | PASS, 0 bytes wrong | PASS, 0 bytes wrong |

Quad, Triangle, Gouraud and FillAfter3D passed again in the same runs.
The engine was idle at the end, and the machine stayed on boot 44.

TexBilinear's allowance is one step plus 1/32 of the four texels' spread,
the RS690 test's rule. At 32 bpp the gradient texture's neighbours differ
by more per channel, so 3.5 steps sits inside it. How close the R500's
filter is to exact was not looked at further.

## What it settles

- The R300 texture-unit registers serve the R500 for these formats
  (RGB565, ARGB8888), sizes, filters and wraps.
- A TEX + ALU program from the encoder, with a semaphore wait, samples
  and combines correctly.
- Switching from the textured state back to the colour state is clean.

## Not done

Z, blend, alpha test, cull, fog, specular, colour key (TEXKILL), the
TL-vertex state, mipmaps, and the other texture formats. Then the
Direct3D HAL on the RV515 row.
