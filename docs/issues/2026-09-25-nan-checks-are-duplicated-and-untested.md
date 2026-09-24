# The Direct3D NaN checks are four static copies, three of them untested

Filed: 2026-09-25
Status: open - todo
Machine: none (host build and HAL disassembly only)

## What is there now

`613152f` (`docs\decisions\2026-09-25-nan-detection-in-the-z-conversion.md`)
moved every NaN guard in the Direct3D path off float comparisons, because
Open Watcom's compares never report unordered. Each file got its own
`static` bit test, since exporting a symbol is a design change:

- `src\display32\d3d\d3d_zfixed.c` - `v9x_d3d_z_is_nan`
- `src\display32\d3d\d3d_soft.c` - `v9x_d3d_soft_is_nan`
- `src\display32\d3d\d3d_virge.c` - an inline test in `v9x_d3d_fixed_scaled`
- `src\display32\d3d\d3d_core.c` - `v9x_d3d_coordinate_finite` (NaN or
  infinity), in the clipper's guard band and `v9x_d3d_triangle_on_target`

`d3d_cull.c` has a fifth test for non-finite values, written inline.

Only the zfixed one is exercised by a host test. The soft, ViRGE and core
files include the DDHAL and are not in the host build, so their checks
were verified by `wdis` of the rebuilt objects only: the bit test is present
and precedes the first `fcomp`. None of them was run.

## To do

1. Agree a shared, exported pair - NaN, and non-finite - in a leaf unit
   the host build compiles under both compilers (not `d3d_zfixed.c`, which
   is Watcom-only and ViRGE vocabulary). This is the design change the
   commit avoided.
2. Host-test it with bit-built +/-NaN, +/-infinity, +/-0 and a denormal.
3. Replace the four static copies and the `d3d_cull.c` inline test with it.
4. Consider moving the NaN-sensitive clamp logic of `v9x_d3d_soft_depth`,
   `v9x_d3d_soft_texcoord` and `v9x_d3d_fixed_scaled` into a leaf too, so
   the tables cover what the engines ship, not just the predicate.
5. Grep for new comparison-based NaN guards when this is picked up; the
   decision note lists what was found on 2026-09-25.
