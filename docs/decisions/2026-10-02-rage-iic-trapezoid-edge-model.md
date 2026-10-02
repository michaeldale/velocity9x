# Rage IIC trapezoids: rows, inclusive-exclusive spans, and an edge walk measured exactly

Date: 2026-10-02
Machine: A8U4I5, 10.0.1.172, ATI 3D Rage IIC AGP `1002:4757` rev `7A`,
boot 135, Velocity9x `06f760b` installed (2D engine as `ATI_RAGE2`).
Tool: `ATIRX.EXE` / `ATIRX.VXD` (`1e4789b`, plus the `/set2` and `/set3`
scene tables in the working tree).
Evidence: [`../probe/a8u4i5-rage-iic-registers-2026-10-02/`](../probe/a8u4i5-rage-iic-registers-2026-10-02/)
`BOOT135-ATIRX-1.TXT`, `-2.TXT`, `-3.TXT`.

Phase 2 of [the Rage IIC plan](../plans/ati-rage-iic-hardware-3d.md). The
register notes ([ati-rage2-3d-engine.md](../specifications/ati-rage2-3d-engine.md))
named the trapezoid's registers and trigger but not how it steps.

## Method

Twenty flat-colour trapezoids were drawn into a guarded 64x64 RGB565
block at VRAM 2 MiB, using the 2D datapath (`SCALE_3D_CNTL` 0, source
`DP_FRGD_CLR`). Each was the encoder's stream (`rage2_trap.c`):
`DST_CNTL`, `DST_Y_X`, the leading terms, the trailing terms, then
`DST_BRES_LNTH` with bits 31 and 15, `TRAIL_X` and the length. Every
batch was logged before it was executed. The block was read back after
an idle wait and the read barrier.

Across all 20 scenes:
- nothing was drawn outside the scissor;
- both 4 KiB guard bands were intact;
- the FIFO and idle waits never timed out, and nothing was reset;
- the agent and desktop stayed up.

## Measured

1. **The length field counts scanlines.** Length 8 from Y 16 drew rows
   16-23 in every scene.
2. **A span covers `[leading, trailing)` in the fill direction.**
   - Leading 16 and trailing 32, filled right, drew 16-31.
   - Leading 40 and trailing 24, with `TRAP_FILL_DIR` clear (fill left),
     drew 25-40.
   - When the leading X reaches the trailing X the span is empty (B4,
     row 23).
3. **`TRAP_FILL_DIR` picks the side the trailing edge is on**, as the RRG
   says. `DST_X_DIR` sets the leading edge's step direction (A5 moved it
   left) and `TRAIL_X_DIR` the trailing edge's.
4. **`DST_Y_MAJOR` has no effect** on any trapezoid tried (S1/S2, A1/A2).
5. **The edge walk.** On each row the edge steps one pixel and adds DEC
   to its error while the error is non-negative. Once it is negative, the
   row's X is settled; the engine moves to the next row and adds INC.
   With `ERR` the written error, `I` = INC and `D` = DEC < 0, the edge on
   row *k* (0-based) is offset from its start by

       steps(k) = max(0, floor((ERR + k*I) / -D) + 1)

   With `DST_BRES_SIGN` set, a zero error counts as negative, and the
   count becomes `max(0, ceil((ERR + k*I) / -D))` (B6 against B7).
6. **DEC = 0 gives a vertical edge** whatever ERR and INC are (S3, A3,
   B3). Read literally, rule 5 would never finish such a row; the engine
   does not step at all.

Each scene against rule 5, where X is the leading edge on rows 16..23,
starting at 16:

| Scene | ERR, INC, DEC | Predicted | Measured |
|---|---|---|---|
| B1 | -1, 8, -8 | 16 17 18 .. 23 | 16 17 18 .. 23 |
| B2 | 0, 0, -8 | 17 throughout | 17 throughout |
| B4 | 7, 8, -2 | 20 24 28 .. 44, then 48 = trailing, empty | the same |
| B5 | 3, 8, -2 | 18 22 26 .. 46 | 18 22 26 .. 46 |
| B6 | 8, 8, -8 | 18 19 .. 25 | 18 19 .. 25 |
| B7 | 8, 8, -8, `DST_BRES_SIGN` | 17 18 .. 24 | 17 18 .. 24 |
| B8 | 0, 8, -16 | 17 17 18 18 19 19 20 20 | the same |
| A1, A2 | 0, 8, -8 | 17 .. 24 | 17 .. 24 |
| A6 | 0, 16, -16 | 17 .. 24 | 17 .. 24 |
| B9 (trailing, from 32) | 7, 8, -2 | exclusive end 36 40 44 .. | spans end 35 39 43 .., clipped at 55 |

## What this disputes

- **The RRG's stepping rule.** It reads: negative error, axial step and
  add INC; otherwise diagonal step and add DEC. Read that way, A1
  (0, 8, -8) steps every other row. It stepped every row. The edge acts as
  an X-major Bresenham walk whatever `DST_Y_MAJOR` says. My set 3 rule
  ("add DEC, step if negative") was fitted to sets 1-2 and failed B4, B5,
  B6 and B8; this record replaces it.
- **The spec's inference that `DST_Y_MAJOR` matters for edges.**

## What it means for triangle setup

An edge moving `dx` pixels over `dy` rows, with any starting sub-pixel
offset, is exact with DEC = `-dy*s` and INC = `dx*s` for a scale *s*, and
ERR chosen for the offset. The `+1` and the floor are what the setup has
to put the first-row rounding into. Negative slopes use the direction
bits, not negative terms. That arithmetic belongs in
`src\chipsets\ati\rage2_setup.c`, host-tested against a CPU rasteriser
that uses rules 1, 2 and 5.

## Not established

- Interpolated colour, Z or texture on a trapezoid. All of this is the
  2D datapath with a flat colour.
- Behaviour beyond the 18-bit range, or with lengths over 8.
- Whether a second trapezoid with bit 31 clear inherits the trailing
  edge's state.
- Any boot but 135, so repeatability across boots.
- Why DEC = 0 does not hang. That it does not is measured three times.
