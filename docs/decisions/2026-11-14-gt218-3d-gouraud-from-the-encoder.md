# GT218 Tesla 3D: Gouraud shading from our own encoded programs

Date: 2026-11-14

Phase 3 of [docs/plans/nvidia-gt218-3d.md](../plans/nvidia-gt218-3d.md),
on M-BXMFA8 (GeForce 210, GT218 A8), boot 117. Evidence:
`build/driver-results/m-bxmfa8-gt218-3d/p3g-V9XN5CH.INI` and
`p3g-exec.txt`.

## The encoder

Michael chose an encoder over hand assembly. `nv50_shader.c` (8165fb01)
encodes `mov`, `fmul`, `fmad`, `fadd`, `rcp`, `interp` and a 2D `tex`,
long and short, following Mesa's `CodeEmitterNV50` including its
source-file modes. Its host test rebuilds four of xf86-video-nouveau's
EXA programs from their disassembly, every word: the vertex program and
`PFP_S` (which drew the first triangle), `PFP_C` and `PFP_S_A8`. The test
caught one misreading on the way: EXA's long move from `a[]` sets the
input-file bit, so it is an attribute read, not a register.

## The programs

Built by the encoder at run time (f00445a6):

- vertex program, 6 long moves: `o[0..14] = a[0..14]`, position (x, y)
  then a four-component colour from attribute 1;
- fragment program: `interp $r0 v[0]` (w), `rcp $r4 $r0`, the four colour
  varyings perspective-correct into `$r0..$r3`, and a long move to carry
  the exit.

They sit beside EXA's in the same code areas (vertex program + 200h,
fragment program + 100h) and are selected per draw with `VP_START_ID`
and `FP_START_ID`. The setup is phase 1's, unchanged; the draw enables
attribute 1 as four components (`VP_ATTR_EN(0)` = F3h) and sends
`VTX_ATTR_4F(1)` before each position.

## Result: PASS at both depths

`V9XN5CH /lean /3d /3d:draw /3d:gouraud`. A 40x24 rectangle at (16, 8)
with red ramping 0 to 1 across, green 0 to 1 down, blue 0.5, compared
pixel by pixel with that model (within 2 steps a channel at 8 bits, 1
step at 5 or 6):

| | Inside | Outside | Top-left | Bottom-right |
| --- | --- | --- | --- | --- |
| 32 bpp | 960 of 960 | 8256 of 8256 | `03057F` | `FCFA7F` |
| 16 bpp | 960 of 960 | 8256 of 8256 | `002F` | `FFCF` |

The model's corners are red 3.2 and 251.8, green 5.3 and 249.7 at 8
bits, blue 127.5. The pusher and PGRAPH were clean and idle, with no
interrupts. The solid rectangles passed in the same run, 16 bpp now
against the `A61Ch` the first run measured.

So the encoder's output runs: our vertex program read a second
attribute and wrote four more outputs, and our fragment program
interpolated them perspective-correct and wrote the colour.

## What the corner values do not settle

The float-to-integer rule. At 8 bits the corners fit round-to-nearest
(251.8 to FCh) but blue 127.5 came out 7Fh. At 5 bits the Gouraud blue,
0.5, came out 15, while the first triangle's texel E1h came out 28,
which is floor(x 32) and not x 31. The texel and the interpolated colour
may convert differently. The tolerance absorbs it, and nothing here
depends on it yet; a measurement of chosen values is the way to settle
it if a check needs exact 16 bpp colours.

## Not covered

Textures larger than 1x1 (a TIC with a pitch), a depth buffer (which
needs a tiled target), blending, and the HAL.
