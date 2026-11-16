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

/*
 * The TL VS for a two-unit draw (docs\plans\ati-gcn-ps11.md step 4): the
 * TL VS's program with a third load, unit 1's coordinates from a second
 * stream through a second V# in s4-s7, 16 bytes a vertex (tu1, tv1, 0,
 * 1) read buffer_load_dwordx4 IDXEN at VertexID into v17-v20, and
 * exported as parameter 3 ahead of the position. Eight user SGPRs.
 */
#define V9X_SI_TL2_VS_DWORDS     (V9X_SI_TL_VS_DWORDS + 4ul)
#define V9X_SI_TL2_VS_VGPRS      24ul
#define V9X_SI_TL2_VS_USER_SGPRS 8ul
#define V9X_SI_TL2_STREAM_BYTES  16ul
v9x_status v9x_si_shader_tl2_vs(v9x_u32 *out, v9x_u32 *count);

/*
 * The wide-vertex VS for pixel shader 1.x (docs\plans\ati-gcn-ps11.md):
 * d3d_ps.h's wide vertex, 28 floats (sx sy sz rhw, diffuse, specular,
 * stage 0-3's coordinates, four each), read by seven MUBUF
 * buffer_load_dwordx4 IDXEN at VertexID through a strided V# in s0-s3
 * (vi_3d.h's vertex_stride, 112), and passed on as loaded: the position
 * to POS0, the six others to parameters 0-5, vi_ps11.h's order. The
 * exports are GCN 3 words rewritten by v9x_si_shader_from_vi.
 */
#define V9X_SI_WIDE_VS_DWORDS     30ul
#define V9X_SI_WIDE_VS_VGPRS      32ul
#define V9X_SI_WIDE_VERTEX_BYTES  112ul
#define V9X_SI_WIDE_PARAMS        6ul
v9x_status v9x_si_shader_wide_vs(v9x_u32 *out, v9x_u32 *count);

#endif
