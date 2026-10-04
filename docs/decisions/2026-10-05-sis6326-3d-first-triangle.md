# SiS 6326 3D: first triangles, the direction bit, sampling and ties

Date: 2026-10-05. Machine: A8U4I5 boot 210, SiS 6326 card 2 (rev 0Bh),
Velocity9x `sis` at 800x600x16. Evidence:
`docs/probe/a8u4i5-sis6326-3d-phase1-2026-10-05/`. Plan:
[sis-6326-hardware-3d.md](../plans/sis-6326-hardware-3d.md), phase 1.

## What was run

`SIS3D.EXE` drove the 3D registers through `SIS2D.VXD`, with every value
from the host-tested builder (`sis6326_3d.c`). That builder's tests
reproduce SiS's captured primitive word `00118682h` and its destination and
clip words.

For each shot, the probe:

1. set SR39 D2 (3D on) with the Turbo Queue off, as the driver leaves it;
2. guarded a 32x32 RGB565 target off-screen;
3. wrote the flat state (enable = primitive setup only, Z and alpha always,
   blend ONE/ZERO, no fog, clip to the target);
4. wrote 89F8h, then the 24 vertex dwords with vertex C's W last, which
   fires the engine;
5. waited on 89FCh D1 and read the target back.

At the end SR39 went back to 00h.

## Measured

**3D runs with the Turbo Queue off.** At enable, 89FCh read `002000FFh`.
Each shot was idle and empty again within 10-30 status reads, and every
painted pixel was exactly 07E0h.

**The direction bit.** Two triangles, mirror images, each fired with
TDRAWDIR 0 and 1:

| Triangle | TDRAWDIR 0 | TDRAWDIR 1 |
|---|---|---|
| Right: middle vertex right of the long edge, (4.25,3.25) (26.25,12.25) (10.25,28.25) | 248 px, every row as the reference | 24 px, one per row |
| Left: the mirror image | 24 px | 248 px, every row as the reference |

**The sampling point.** Run A compared against pixel-centre sampling,
(x + 0.5, y + 0.5): 20-24 rows per triangle differed. Sampling at the
integer corner (x, y), fitted offline and then used as the probe's own
reference in runs B and C, matched every row of both triangles.

**Ties.** Two triangles with integer vertices, whose edges run through
sample points:

| Triangle | Painted | Top-left rule | Bottom-right rule |
|---|---|---|---|
| TieTopLeft, (4,4) (20,4) (4,20) | 120 | 136, 16 rows differ | 120, every row matches |
| TieBottomRight, (20,4) (20,20) (4,20) | 136 | 120, 16 rows differ | 136, every row matches |

The same two moved up and left by 1/16 pixel painted 136 and 120. Every row
matched the top-left coverage of the unshifted triangles.

## What this settles

1. **TDRAWDIR (89F8h D7) is 1 exactly when the middle vertex lies left of
   the long top-to-bottom edge.** SiS's captured triangle agrees: its
   middle vertex was left, with D7 = 1. The wrong value draws one pixel per
   row and is not a near miss.
2. **The engine samples at integer pixel coordinates.** That is Direct3D's
   pixel centre (DX5-9), so TLVERTEX screen coordinates go to the engine
   unchanged. An OpenGL front end, whose centres sit at +0.5, needs its own
   offset.
3. **The engine owns samples exactly on bottom and right edges** - the
   mirror of Direct3D's top-left rule.
4. **A small up-left shift of every vertex gives Direct3D's coverage
   exactly**, measured with 1/16 pixel on vertices on the 1/16 grid. The
   driver applies such a shift.
5. The flat state - no Z, no alpha test, ONE/ZERO blend, no fog - and the
   RGB565 destination produce exactly the vertex colour.

## Not established

- **The smallest shift the setup engine resolves.** 1/16 pixel is right for
  vertices on the 1/16 grid. For arbitrary float vertices the shift must be
  below the setup engine's sub-pixel precision; phase 2 measures a smaller
  value.
- **Gouraud, Z, alpha test, blending, textures:** phases 2 and 3.
- **Whether the guard past each target stayed intact.** Only the 32x32
  target itself was read back; the clip was not tested against a triangle
  that crosses it.
