/*
 * Validation of the render interface's requests, before anything past a
 * struct header is dereferenced or anything is emitted
 * (docs\plans\opengl-1.1-icd.md, Phase 3).
 *
 * Pure: every check is arithmetic over project-owned descriptors. The HAL's
 * boundary resolves a DirectDraw surface into a V9X_R3D_SURFACE and hands
 * it here; it is the only part that touches a DDHAL structure, and it checks
 * readability, not layout. What these functions cannot establish - that an
 * allocation is still live, that a VRAM range is still this surface's - is
 * the ICD's, through the generation and its surface-lost tracking.
 *
 * Each returns a V9X_R3D_RESULT_* value: OK, or the refusal that applies.
 * A refusal means nothing was read beyond what the check needed and nothing
 * was emitted.
 */
#ifndef VELOCITY9X_R3D_VALIDATE_H
#define VELOCITY9X_R3D_VALIDATE_H

#include "velocity9x/r3d_abi.h"
#include "r3d.h"

/*
 * A render target's layout: RGB565 or XRGB1555, 1..dimension_max on each
 * edge, an even pitch that holds a row, and every byte from `offset` to the
 * last pixel of the last row inside `vram_bytes`, computed without
 * overflow.
 */
v9x_u32 v9x_r3d_validate_surface(const V9X_R3D_SURFACE *surface,
                                 v9x_u32 vram_bytes,
                                 v9x_u32 dimension_max);

/* A 16-bit depth buffer for `target`: the same extent rule, and at least
 * the target's width and height. */
v9x_u32 v9x_r3d_validate_depth(const V9X_R3D_SURFACE *depth,
                               const V9X_R3D_SURFACE *target,
                               v9x_u32 vram_bytes,
                               v9x_u32 dimension_max);

/* A CPU texture chain: 1..V9X_R3D_ABI_LEVELS_MAX levels, level 0 a power of
 * two on each edge up to size_max, each later edge max(1, half), a pitch
 * that holds the row and declared storage that holds the level. */
v9x_u32 v9x_r3d_validate_levels(const V9X_R3D_ABI_LEVEL *levels,
                                v9x_u32 level_count,
                                v9x_u32 size_max);

/* The texture description: storage, format and every sampling field in
 * range; a CPU chain through v9x_r3d_validate_levels, a HW texture with a
 * surface named. NONE needs nothing else. */
v9x_u32 v9x_r3d_validate_texture(const V9X_R3D_ABI_TEXTURE *texture,
                                 v9x_u32 size_max);

/* Fragment state against the target's extent: every function and factor
 * in range, the reference a byte, the mask inside RGB, and the scissor
 * inside the target and not inverted. */
v9x_u32 v9x_r3d_validate_state(const V9X_R3D_ABI_STATE *state,
                               v9x_u32 width, v9x_u32 height);

/*
 * A draw request's header, in the order that dereferences least: null is
 * INVALID; struct_bytes other than this build's is ABI, and nothing after
 * it is read; a generation other than `generation` is STALE; then a named
 * target, vertices, a count of 1..V9X_R3D_ABI_BATCH_MAX, and the texture.
 * The surfaces and the state are checked once resolved, by the three
 * functions above.
 */
v9x_u32 v9x_r3d_validate_draw(const V9X_R3D_ABI_DRAW *draw,
                              v9x_u32 generation,
                              v9x_u32 texture_size_max);

/* A clear request's header, the same way, then its rectangles against the
 * target's extent once that is known: every one inside and not inverted. */
v9x_u32 v9x_r3d_validate_clear(const V9X_R3D_ABI_CLEAR *clear,
                               v9x_u32 generation);
v9x_u32 v9x_r3d_validate_rects(const V9X_R3D_ABI_RECT *rects,
                               v9x_u32 rect_count,
                               v9x_u32 width, v9x_u32 height);

#endif /* VELOCITY9X_R3D_VALIDATE_H */
