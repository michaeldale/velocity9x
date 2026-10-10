# The MGA-2164W draws flat and Gouraud trapezoids exactly as the specification's terms say

Date: 2026-10-10. A8U4I5 (10.0.1.172), boot 384, the Millennium II
MGA-2164W (102B:051B) on Velocity9x `matrox` (afb538c) at 800x600x16 and
800x600x32. Phase 1 of the
[hardware Direct3D plan](../plans/matrox-mga2164w-hardware-3d.md).
Evidence in `docs/probe/a8u4i5-mga2164w-trapezoids-2026-10-10/`.

## The instrument

`MGA2D.EXE` and its VxD, extended rather than copied: the VxD now finds
either chip and maps each one's apertures by its own order (2164W:
framebuffer BAR0, 16 MiB; control BAR1), allows the second drawing-register
block 2C00h-2DFCh on the 2164W only, and reads FIFOSTATUS's count as
<6:0> there. The new `/tri` mode draws trapezoids built by
`src/chipsets/matrox/mga_3d.c` (host-tested) into the guarded off-screen
region and compares each pixel with the same module's model of the
engine. Every case re-sends the per-mode setup in the same VxD call,
because the driver itself now draws on this engine between calls.

Regression first: the seven fill and copy cases pass on the 2164W at
16 bpp (`b384-MGA2D-fill-copy-800x600x16.TXT`). **FIFOSTATUS reads
`0x240`**: free count 64 in bit 6 plus bempty. The 2064W's six-bit mask
would read 0 and time out every FIFO wait; the 2164W specification's
field table (<5:0>) is wrong and its text (resets to 64) right.

## Edges

Five flat trapezoids (TRAP, atype RPL, solid), each edge given as
(x, dx, dy) and encoded with section 4.5.5.1's terms: AR0/AR6 = dY,
AR2/AR5 = -|dX|, AR1/AR4 = sdx ? dX + dY - 1 : -dX, SGN sdxl/sdxr. All
five match the model with zero mismatched pixels at 16 and at 32 bpp
(coverage maps in the TXT files):

| Case | Shape | Settles |
| --- | --- | --- |
| TriApex | one-pixel top row, 45-degree edges | the right edge is exclusive, the left inclusive |
| TriPoint | zero-width top row | an empty first row draws nothing; the next row is 2 wide |
| TriShallow | edges of slope -3/8 and 5/8 | rightward edges step on the first row boundary, leftward a row later: the error terms' asymmetric rounding, as modelled |
| TriConverge | edges closing, bottom half of a triangle | |
| TriLongEdge | edges over 16 rows, drawn for 8 | an edge's slope is independent of the trapezoid's length, so the long edge of a split triangle can be stated whole |

The engine's edges are integer Bresenham lines between integer
endpoints. There is no sub-pixel input anywhere in these registers; how
Direct3D's sub-pixel vertices and top-left rule map onto them is the
driver's problem, not the chip's (phase 4).

## Gouraud

TRAP with atype I and zmode NOZCMP (no depth read or write), DR4/6/7,
DR8/10/11 and DR12/14/15 as 9.15 start, x step and y step, at 32 bpp so
no dither touches the result. Two hypotheses for the per-row step were
modelled: EDGE (the start follows the left edge, gaining dx times the x
step whenever the edge moves dx columns, 86Box's model) and NONE (only
the y step).

| Case | EDGE | NONE |
| --- | --- | --- |
| GouraudFoldRight (edge moves right 1/row, red +2/pixel) | 0 mismatches | 224 |
| GouraudFoldLeft (edge moves left 1/row, red +3/pixel) | 0 | 168 |
| GouraudFraction (vertical edges, red from 64.5, +1/4 per pixel, +1/8 per row) | 0 | 0 |

So **the y registers are a pure d/dy and the engine itself adds the left
edge's column steps times d/dx.** The driver can give it plane-equation
gradients and the value at the first row's first pixel, with no per-edge
correction. The fraction case shows the level is the integer part,
truncated, of a value accumulating all 15 fraction bits (64.5 + 4 x 1/8
reaches 65 at row 4, not before). FCOL's top byte is stored as each
pixel's alpha at 32 bpp (`0x5A` in every Gouraud pixel).

## Not settled here

- 16 bpp Gouraud: dithered (MACCESS.nodither clear), not modelled, not
  run.
- Depth (phase 2) and textures (phase 3).
- Whether 2D00h is a start alias for the 2C00h block.
- V9XMSW reported `Result=INCOMPLETE` for the switch to 800x600x32 while
  V9XBOOT's Surface line shows the new mode; the INI was read while
  being written, probably (seen before with `get`). Not chased.
