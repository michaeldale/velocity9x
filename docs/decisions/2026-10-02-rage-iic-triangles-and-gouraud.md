# Rage IIC: CPU-set-up triangles, Gouraud and Z16, exact on the card

Date: 2026-10-02
Machine: A8U4I5, 10.0.1.172, ATI 3D Rage IIC AGP `1002:4757` rev `7A`,
boot 136, Velocity9x `06f760b` installed.
Tool: `ATIRX.EXE` modes `/tri`, `/regs`, `/shade`, `/gouraud`, built on
`rage2_setup.c` and `rage2_trap.c`.
Evidence: [`../probe/a8u4i5-rage-iic-registers-2026-10-02/`](../probe/a8u4i5-rage-iic-registers-2026-10-02/)
`BOOT136-ATIRX-TRI.TXT`, `-REGS.TXT`, `-SHADE.TXT`, `-GOURAUD.TXT`.

Phase 3 of [the Rage IIC plan](../plans/ati-rage-iic-hardware-3d.md), on
top of [the measured edge walk](2026-10-02-rage-iic-trapezoid-edge-model.md).

## Flat triangles

`v9x_r2_setup_triangle` splits a 1/16-pixel triangle at its middle vertex
into at most two trapezoids. Coverage is centre sampling with the top-left
rule:
- the leading edge on row *r* is `ceil(x_left(r + 1/2) - 1/2)`;
- the trailing edge is `ceil(x_right(r + 1/2) - 1/2)`, exclusive.

The edge terms reproduce that row by row: DEC = -dyF, INC = dxF, and an
ERR carrying the first row's rounding.

Host test: an emulator of the measured walk, fed setup's trapezoids, must
draw exactly what an independent per-pixel edge-function rasteriser
covers. It ran on 4,000 generated triangles plus named and degenerate
cases, and with a shared edge drawn once. It was watched failing against
a stub.

Card, `/tri`: 12 named and 64 generated triangles, every pixel of a
guarded 64x64 block compared with the same reference function.
**18,514 pixels drawn, 18,514 expected, 0 mismatches**, guards intact.

## Interpolator formats

Measured by writing all-ones, zero and `55555555` and reading back, with
`SCALE_3D_CNTL` set to shading:

| Registers | Implemented bits | Reading |
|---|---|---|
| colour and alpha START, X_INC, Y_INC | 24:4 | S.8.12, integer 23:16 |
| Z START, X_INC, Y_INC | 28:0 | S.16.12, integer 27:12 |
| S/T `*_INC2` | 26:0 | S.10.16 |
| `S/T_XINC_START`, `S/T_Y_INC` | 27:0 | S.11.16 |
| `S/T_START` | 25:5 | 10.11 |
| `TEX_SIZE_PITCH` | 11:0 | three 4-bit fields |
| `Z_OFF_PITCH` | 31:22, 19:0 | as `DST_OFF_PITCH` |
| `Z_CNTL` | 0, 1, 2, 6:4, 8 | the Rage Pro layout plus bit 2 |

## The shading arithmetic

Measured with `/shade`: 32x8 rectangles and one sloped edge, with the
pixels dumped. The datapath is `SCALE_3D_CNTL = C0` (shading),
`DP_SRC = 500` (3D source), dither and rounding off.

1. Colour is a plane anchored at the trapezoid's `DST_Y_X` pixel:
   - X_INC is added per pixel along the span, and also when the leading
     edge steps in X (G6);
   - Y_INC is added per row (G2).
2. The output truncates: the 8.12 value to 8 bits, then 8 bits to 5 or 6
   (G1, G3, G4). Constant 255/128/64 gives `FC08`.
3. The accumulator is modular with a 9-bit integer part (G7, G8):
   - 0-255 is the colour;
   - 256-383 saturates to 255;
   - 384-511 (that is, -128 to -1) reads as 0.
4. X_INC counts steps in `DST_X_DIR`'s direction. When the leading edge
   leans left (`DST_X_DIR` clear), a left-to-right span gets -X_INC per
   pixel. The first `/gouraud` run assumed +X: every left-leaning
   triangle came out with its spans mirrored (pairs of pixels about the
   anchor swapped, e.g. G26 `C5C7`/`D628`). The setup now writes the
   gradient negated in that case.

## Gouraud triangles

`v9x_r2_setup_shade` computes the colour plane's gradients from the three
vertices. It rounds them to the register's 12 fraction bits and sets
START to the plane at each trapezoid's anchor plus one half, for
round-to-nearest. The arithmetic is integer only: a bit-serial division
for the gradients, and START taken modulo 2^25, where the field wraps.
The HAL links no float runtime. A gradient of 256 levels a pixel or more
does not fit S.8.12 and is refused (`V9X_STATUS_UNSUPPORTED`).

Host test, 2,000 generated triangles with random vertex colours:
641,466 pixels compared. Each channel the engine model gives is within
**one** 8-bit level of the ideal plane rounded to nearest, and inside
the -128..383 window. 62 sliver trapezoids were refused as too steep.

Card, `/gouraud`, after the direction rule: 60 generated triangles,
**8,701 pixels, 0 mismatches** against the engine model's 565, no
timeouts or resets.

## Z16

`/zbuf` (`BOOT136-ATIRX-Z.TXT`) draws red rectangles on the shading
datapath with `v9x_r2_build_z_state`. The Z surface is a guarded 64x64
block at VRAM 2 MiB + 64 KiB, at the colour target's pitch. The
`Z_CNTL` meanings are the Rage Pro's: `Z_EN` bit 0, `Z_TEST` 6:4,
`Z_MASK` bit 8.

| Scene | Result |
|---|---|
| Write, constant Z `1234`, test always | rectangle all `1234` |
| Write, START `1000`, X_INC `100`, Y_INC `10` | exactly `1000 + 100*dx + 10*dy`: row 16 `1000..2F00`, row 23 `1070..2F70` |
| Test only, Z `4000` against bands 3000 / 4000 / 5000 / 4000 | never: none; less: the 5000 band; less-equal: 4000 and 5000; equal: the 4000 bands; greater-equal: 3000 and 4000; greater: 3000; not-equal: 3000 and 5000; always: all |
| Less with write | the 5000 band drawn and its Z now `4000` |
| Z clear: the 2D engine fill on the Z surface | all 256 pixels `ABCD` |

The comparisons are incoming against stored, in Direct3D's sense. With
writes off the Z block was unchanged in every compare scene. No Z pixel
outside the rectangle changed, both Z guards were intact, and there were
no timeouts or resets.

## Not established
- Dithering and `ROUND_EN`, both off here.
- Alpha and fog interpolation.
- How large targets behave. All scenes are in a 64x64 block; the setup
  is bounded to 2047 pixels, and the encoder caps a trapezoid at 64
  rows, which a full-screen triangle exceeds.
- Any boot but 136 for these modes.
