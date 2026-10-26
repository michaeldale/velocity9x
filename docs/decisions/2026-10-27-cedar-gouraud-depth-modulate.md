# The HD 5450 draws Gouraud, depth-tested and texture-modulated triangles from our own shaders

Date: 2026-10-27

Status: **measured, one card, one boot.** M-BXMFA8's HD 5450 (`1002:68F9`,
Cedar), boot 7, the ati build installed. Follows
[the linear colour buffer](2026-10-27-cedar-linear-colour-buffer.md).
Tool: `V9XEG3D.EXE` with `/gouraud`, `/depth` and `/modulate`; the shaders
come from `src\chipsets\ati\evergreen\eg_shader.c`. Captures:
[`../probe/m-bxmfa8-cedar-3d-phase3-2026-10-27/`](../probe/m-bxmfa8-cedar-3d-phase3-2026-10-27/).
This is Phase 3 of [the 3D plan](../plans/ati-evergreen-cedar-3d.md)
(its item 4, "a textured, Gouraud triangle").

## The encoder

`eg_shader.c` builds control-flow, ALU, vertex-fetch, texture-fetch and
export words from Mesa r600's `eg_sq.h` field layouts. Its host test
rebuilds Linux 3.9's hand-assembled blit VS and PS word for word, then
checks the new shaders field by field. On the card, the blit with the
encoder's shaders in place of the pasted arrays is byte-exact (p1, q4,
r5), as the pasted ones were.

## Results

Every draw: 64x8 `COLOR_8_8_8_8`, linear aligned, `NON_DISP_TILING_ORDER`
set, two triangles per rectangle (`DI_PT_TRILIST`), each pixel compared
with the expected value within +-2 per channel, and the 512 guard dwords
after the target checked.

| Draw | Runs | Result |
|---|---|---|
| blit (encoder shaders) | p1, q4, r5 | byte-exact, guard intact |
| `/gouraud`: red 0 to 1 across, green 0.5, blue 0.25, alpha 1 | p2, q3, r4 | all 512 within +-2 |
| `/depth`: Z16 1D-tiled, LESS, writes; red at z 0.5, green left half at 0.75, blue right half at 0.25 | q1, q2, r3 | red left, blue right, all 512 |
| `/modulate`: texel times (red ramp, 1, 0.5, 1) | m1, r1, r2 | all 512 within +-2 |

Gouraud's red bytes run `02 06 0A ... 7E 81 ... F9 FD`, the ramp sampled
at pixel centres. Green reads `80`, blue `40`. `INTERP_ZW` (`D7h`),
taken from Mesa's opcode table and never used by Linux's blit, works.

After the depth draw, the Z16 buffer holds `8000h` in all four left-hand
tiles and `4000h` in all four right-hand ones, every pixel of each tile
alike. Those are 0.5 and 0.25 as 16-bit unorm, and the green rectangle
left no trace. The DB needs no HTILE: Linux's default state forces HiZ off
(`DB_RENDER_OVERRIDE` `2Ah`) and disables compression
(`DB_RENDER_CONTROL` `60h`).

Modulate checks the parts Direct3D's default stage needs together: two
interpolants (`SPI_PS_IN_CONTROL_0` `NUM_INTERP` 2, `SPI_VS_OUT_ID_0`
semantic 1, `SPI_PS_INPUT_CNTL_1`, `SPI_VS_OUT_CONFIG` export count),
three fetches with a 40-byte stride, `EXPORT` then `EXPORT_DONE` for the
two parameters, a texture clause between two ALU clauses, and `MUL`
(`OP2` 1) with bank swizzle `VEC_012`. Pixel 0 reads `01010300` from
texel `01020304`: red 4 x 1/128 rounds to 0, green 3 x 1, blue 2 x 0.5,
alpha 1 x 1. Each channel scales by its own factor, so none is swapped.

## A probe bug, not the hardware

The first depth run (p3) drew last-writer-wins and left the Z buffer at
`FFFFh`. The cause was the probe: the DB block's `SET_CONTEXT_REG` gave a
count of 9 for 8 registers. The CP took the next packet's header as a
ninth value, written to `28060h`. It then parsed `0200h` as a type-0
packet and wrote `16h` to MMIO `0800h`. So `DB_DEPTH_CONTROL` was never
set. Neither register is named in Linux's or Mesa's Evergreen headers.
The card ran on, and every later run on the same boot passed, but what
those two writes did is not known. A `PACKET3` count is the number of
values; the HAL's packet builders need a host test that holds them to
it.

## Not measured

- Normalised texture coordinates, filtering, wrap modes, and any texture
  format but `8_8_8_8`. The sampler is the blit's (clamp, point) and the
  coordinates are in texels.
- Blending, alpha test, fog, specular, culling, Z24 and stencil.
- Colour buffers other than 32 bpp, and depth buffers other than Z16 1D
  tiled.
- Anything larger than 64x8, and more than one draw per ring session.
