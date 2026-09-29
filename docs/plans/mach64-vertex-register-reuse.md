# Mach64: reuse vertex registers across a fan or strip

Status: implemented and physically validated on the Gateway, 2026-09-29.  The
six permutations and full-versus-partial packet comparison are pixel-exact;
the existing probe and Quake 2 are stable, but no measurable speedup was found. See
`../decisions/2026-09-29-mach64-vertex-register-reuse-physical.md`.

## Why

Quake 2 on the Gateway Solo 2150 (Rage Mobility-M, ~450 MHz PIII, 640x480
fullscreen) runs at about 8.4 fps against the stock ATI driver's 22 to 24.
Temporary HAL timing buckets over one run (commit `81c2e86`, removed since)
put the Mach64 draw at:

| Part | Cycles a triangle |
|---|---|
| Register writes (`v9x_m64_emit_batch`) | about 3,500 |
| Setup build (`v9x_m64_build_setup`) | about 730 |
| Policy, map, validation, Win16 mutex | small |

A textured triangle is about 20 MMIO writes, so a write costs about 320 ns
on this bus. The writes are the largest single cost the driver controls.

## The idea

The setup engine draws a triangle from `VERTEX_1_*`, `VERTEX_2_*` and
`VERTEX_3_*` when `ONE_OVER_AREA` is written. Each vertex is six registers
(S, T, W, Z, ARGB, X_Y) plus the specular word when fog or specular is on.

Consecutive triangles of a fan share two vertices, and of a strip two as
well. If the registers still hold those vertices, the next triangle needs
only the new vertex's six words and `ONE_OVER_AREA`: 7 writes instead of
about 19, roughly 2.7 times fewer.

Quake 2's world polygons are fans (GL_POLYGON), and the ICD already keeps a
fan's vertices together in a batch.

## Design

In `d3d_mach64.c`, per batch:

1. Keep the three slots' last-written register values (the six words each)
   and whether each slot is known. A batch starts with all three unknown:
   state emission, 2D fills and copies may have touched nothing in the vertex
   block, but that is an assumption to measure, not to take.
2. For each triangle, match its three built vertices against the slots by
   value, all six words equal. Put unmatched vertices in the slots not
   matched. Write only those slots, then `ONE_OVER_AREA`.
3. `ONE_OVER_AREA` must be computed for the vertex order the slots now hold,
   not the order the triangle arrived in. `v9x_m64_build_setup` preserves the
   winding sign today; it needs a form that takes the slot order.

The builder stays pure and host-tested: add a function that, given the
previous slot contents and a triangle, returns the permutation, the writes
and the area word. Test first:

- a fan of five triangles writes 19, then 7, 7, 7, 7;
- the permuted triangle's area word has the sign its slot order implies;
- a triangle sharing nothing writes all three slots;
- flat shading writes the provoking colour into every slot it writes, so a
  matched vertex whose ARGB differs is not a match.

## What must be measured first (probe scenes)

1. **Do the vertex registers survive a triangle?** Draw a triangle, then
   write only vertex 3 and `ONE_OVER_AREA` for a second triangle sharing
   vertices 1 and 2. Compare its pixels with the same second triangle written
   in full. This is the whole premise; no ATI documentation here says it.
2. **Winding under permutation.** The same triangle with its vertices in all
   six slot orders, each with its own area word: identical pixels.
3. **After a state batch, a fill and a copy**: whether the vertex registers
   are disturbed. If they are, invalidate the slots at those points, which is
   the safe default anyway.
4. **The Mobility screen-copy commit race** (`eng_mach64.c`) is on the 2D
   path. Watch for it anyway, since the engine is shared.

Until scene 1 passes, nothing changes in the draw path.

## Expected gain, and what it does not fix

On Quake 2's fans, about 2.7 times fewer register writes. At the measured
3,500 cycles a triangle that is about 2,200 cycles saved, roughly 20 ms a
frame at the current triangle rate. That is an estimate from the counts, not
a measurement. It does not touch:

- the ICD's non-vertex call overhead (about half the frame is outside
  anything timed so far);
- texture uploads (lightmap refreshes, about 7 ms a frame);
- particles, which are refused (MODULATE on an alpha texture multiplies
  alphas, which the engine cannot).

## The larger alternative

Bus-master command DMA (the GUI master and a descriptor table in system
memory) would take the CPU off the bus altogether. It needs physically
contiguous memory from a VxD and has its own safety work. Register reuse is
smaller, measurable in one probe cycle, and does not rule DMA out later.
