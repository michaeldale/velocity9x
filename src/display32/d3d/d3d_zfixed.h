/*
 * The ViRGE S3D depth format: 1.31 fixed point.
 *
 * The S3D unit's Z start and Z gradient registers take a 1.31 fixed-point
 * value - the DDK reaches the same encoding through MYFLINT31
 * (C:\98DDK\src\display\mini\s3v\D3DDRV.H:265). Converting into it is pure
 * arithmetic with one sharp edge, so it lives here rather than in the engine:
 * this translation unit includes nothing from the DDHAL side, which is what
 * lets scripts\build-host.ps1 compile it and tests\host\test_d3d_zfixed.c
 * hold it to a table.
 *
 * It stays under src\display32\d3d rather than src\common because a ViRGE
 * depth encoding is chip vocabulary, and chip vocabulary belongs on the engine
 * side of the split (docs\decisions\2026-08-29-d3d-core-engine-split.md). The
 * host build reaching in is a build-list entry, not a reason to move the file.
 *
 * The sharp edge: sz = 1.0 scales to exactly 2^31, which does not fit a signed
 * 32-bit integer. Every conversion route this driver has - x87 fistp in the
 * engine, a C cast here - produces something wrong there, and the wrong value
 * is catastrophic rather than merely inaccurate. See d3d_zfixed.c.
 */
#ifndef VELOCITY9X_D3D_ZFIXED_H
#define VELOCITY9X_D3D_ZFIXED_H

/*
 * The largest magnitude the 1.31 registers can carry, 0x7FFFFF80. Not
 * 0x7FFFFFFF: the limit is expressed as a float so the clamp can be applied
 * before conversion, and this is the largest value a 32-bit float represents
 * exactly below 2^31.
 */
#define V9X_D3D_Z_1_31_MAX 2147483520l

/*
 * A depth value, clamped to [0, 1]. Out-of-range and NaN resolve to the far
 * plane, because a garbage vertex should be occluded rather than occlude.
 */
long v9x_d3d_z_to_1_31_depth(float value);

/*
 * A depth gradient, genuinely signed and clamped to +/- the register limit.
 * NaN resolves to zero - a flat-depth triangle - rather than to a maximal
 * slope.
 */
long v9x_d3d_z_to_1_31_signed(float value);

#endif
