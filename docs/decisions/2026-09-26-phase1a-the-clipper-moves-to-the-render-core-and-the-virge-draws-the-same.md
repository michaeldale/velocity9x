# Phase 1a: the clipper, list builder and cull decision move to the neutral render core, and the ViRGE guest's Direct3D probe reports the same

Date: 2026-09-26
Plan: `docs/plans/opengl-1.1-icd.md`, Phase 1a
Guest: 86Box `Win86SE` (port 9869), Windows 98 SE, S3 ViRGE/DX in hardware
Direct3D mode, 1024x768x16, boots 633 (before) and 634 (after)
Evidence: `2026-09-26-phase1a-virge-V9XDD-before.ini` (HAL build
`1fa48b0-dirty`, the last Phase 0 deploy), `2026-09-26-phase1a-virge-V9XDD-after-run1.ini`
and `...-after-run2.ini` (HAL with this change, two runs on one boot)

## What moved

`src/display32/r3d/` now holds the neutral render core's first three files:

- `r3d.h`: `V9X_R3D_VERTEX`, D3DTLVERTEX's layout under a name that belongs
  to no API, and the clip and list-builder interfaces.
- `r3d_clip.c`: `v9x_d3d_lerp_vertex`, the finiteness test,
  `v9x_d3d_clip_triangle`, `v9x_d3d_triangle_on_target` and
  `v9x_d3d_draw_list` from `d3d_core.c`, the arithmetic untouched. What was
  read from the D3D context - the executing engine's guard band and the
  target's size - is an argument; what was a call into the core - the batch
  sink and the cull decision - is a callback. It carries its own fistp,
  as `d3d_zfixed.c` does, so the host build under another compiler gets a
  C round-half-even in its place.
- `r3d_cull.c/.h`: `d3d_cull.c/.h` renamed, symbols `v9x_r3d_cull_*` and
  `V9X_R3D_CULL_*`, values still D3DRENDERSTATE_CULLMODE's.

`d3d_core.c` keeps thin wrappers: `v9x_d3d_clip_triangle` supplies the
engine's `coordinate_limit` and the context's width and height;
`v9x_d3d_draw_list` builds a `V9X_R3D_LIST` whose sink is
`v9x_d3d_draw_batch` and whose cull decision is `v9x_d3d_triangle_culled`,
unchanged. The cast from `V9X_D3DTLVERTEX` to `V9X_R3D_VERTEX` is held by
compile-time size and `offsetof` assertions beside it.

Host tests: `tests/host/test_r3d_clip.c` characterises the moved behaviour
(the half-open guard band, NaN refused with a count, the right and bottom
edges at width and height, perspective-correct texture coordinates at a cut
only when rhw differs, runs as windows on the caller's array, a culled
triangle ending a run, a refused triangle failing the call without stopping
the list); `test_r3d_cull.c` is the cull test renamed. `check-tree` gains a
rule that nothing under `src\display32\r3d` names a chip, a DDHAL type or a
Direct3D render state.

## Measured

`V9XDDP.EXE` (859-line report, `Result=COMPLETE`) run before the deploy,
then twice after it. Excluding the keys that change with every process -
timings, uptimes, and the runtime's own pointers (`Gbl*`, `CbRaw*`) - the
before/after difference is **five `TexM_*` keys**, and the after/after
difference on one build is **ten** of the same family. `TexM_<size>_<fmt>_
<layout>_<filter>_Dmiss` is the probe's per-case delta of the HAL's
`done_missing` counter, the S3D 3D-done bit not being seen in time, and
`_L`/`_R`/`_Ok` are that case's sampled result; on the emulated ViRGE those
move from run to run with the same binary. Nothing else moved: the same
five contexts, three render targets, 41 textures, 587 `DrawPrimitives` and
4 `DrawOnePrimitive`, the same pixel ladder results, the same colour-key,
blend and Z results.

## Gates

- Host: `test_d3d_raster.c` hashes unchanged (the rasterizer is untouched);
  `test_i9xx_3d.c` green; the new `test_r3d_clip.c` and `test_r3d_cull.c`
  green. `run-checks` passed for all four families.
- 86Box ViRGE: as above.
- **Pending:** MICHAEL-NETBOOK (3DMark 99 at 1024x576 within noise of 643,
  3D WinBench 98 quality) - the netbook's agent at 10.0.1.254:9869 timed
  out on 2026-09-26. The Gen3 engine takes the same clipper through the
  same wrapper, with `clip_in_core=0` and its 4096 guard band, so what it
  exercises differently is the guard-band refusal path. V9XSOFT's hashes
  cannot change because `d3d_raster.c` is not in this diff.

## Standing

Phase 1a is done on the host and the ViRGE guest; the netbook gate runs
when the machine is back, before Phase 1d moves the Intel engine. Phase 1b
(`r3d.h`'s draw description and the appended `ops->draw`) is next.
