/*
 * GCN 1 (Southern Islands, Oland) shaders that cannot be had by
 * translating vi_shader.c's GCN 3 words (v9x_si_shader_from_vi), because
 * the GCN 3 program uses FLAT, which SI does not have.
 */
#ifndef VELOCITY9X_SI_SHADER_H
#define VELOCITY9X_SI_SHADER_H

#include "velocity9x/status.h"

/*
 * The Direct3D TL-vertex VS for SI. The same program as
 * v9x_vi_shader_tl_vs, translated, except the fetch: user SGPRs s0-s3 are
 * a buffer resource (V#) over the vertex buffer, and two MUBUF
 * buffer_load_dwordx4 instructions read each 32-byte vertex at
 * VertexID x 32 (OFFEN), the second at offset 16, into the VGPRs the FLAT
 * loads filled (v3-v6, v9-v12). Everything after the loads is the GCN 3
 * program's, rewritten. Seventeen VGPRs, as before.
 *
 * Writes at most V9X_SI_TL_VS_MAX dwords to out and sets *count.
 * V9X_STATUS_UNSUPPORTED if vi_shader.c's program no longer has the fetch
 * prologue this replaces, or its tail does not translate.
 */
#define V9X_SI_TL_VS_DWORDS     41ul
#define V9X_SI_TL_VS_MAX        64ul
#define V9X_SI_TL_VS_USER_SGPRS 4ul
/* The V# itself is written by vi_3d.c's TL draw in its GFX6 mode. */
v9x_status v9x_si_shader_tl_vs(v9x_u32 *out, v9x_u32 *count);

#endif
