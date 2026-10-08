/*
 * Glide vertices to render-interface vertices (docs\plans\glide-2x-wrapper.md,
 * Phase 1): the snap bias, the origin, depth from ooz or oow, texture
 * coordinates out of Glide's 0..256 space, colour, and fog in specular
 * alpha. Also the per-vertex table-fog lookup, culling, and lines widened
 * into triangles, since the render interface draws triangle lists only.
 *
 * Pure: no Windows headers.
 */
#ifndef VELOCITY9X_GLIDE_VERTEX_H
#define VELOCITY9X_GLIDE_VERTEX_H

#include "velocity9x/types.h"
#include "velocity9x/r3d_abi.h"
#include "glide_api.h"

/*
 * The first twelve floats of a GrVertex: x, y, z, r, g, b, ooz, a, oow,
 * then TMU 0's sow, tow, oow. They are the same whatever TMU count the
 * game was built for. TMU 0's oow is not read: NFS II SE leaves it
 * uninitialised (census) and Glide divides by the vertex oow unless told
 * otherwise by a hint no census title has used.
 */
#define V9X_GLIDE_VERTEX_X    0u
#define V9X_GLIDE_VERTEX_Y    1u
#define V9X_GLIDE_VERTEX_R    3u
#define V9X_GLIDE_VERTEX_G    4u
#define V9X_GLIDE_VERTEX_B    5u
#define V9X_GLIDE_VERTEX_OOZ  6u
#define V9X_GLIDE_VERTEX_A    7u
#define V9X_GLIDE_VERTEX_OOW  8u
#define V9X_GLIDE_VERTEX_SOW  9u
#define V9X_GLIDE_VERTEX_TOW  10u
#define V9X_GLIDE_VERTEX_FLOATS 12u

/* Where a vertex's colour or alpha comes from, as the combine decides. */
#define V9X_GLIDE_SOURCE_ITERATED 0ul
#define V9X_GLIDE_SOURCE_CONSTANT 1ul

typedef struct v9x_glide_vertex_setup {
    v9x_u32 origin;         /* V9X_GLIDE_ORIGIN_* */
    float height;           /* the screen's lines, for the lower-left origin */
    v9x_u32 depth_mode;     /* V9X_GLIDE_DEPTH_* */
    float s_scale;          /* texture units per Glide s unit */
    float t_scale;
    v9x_u32 color_source;   /* V9X_GLIDE_SOURCE_* */
    v9x_u32 alpha_source;
    v9x_u32 constant_argb;  /* grConstantColorValue, ARGB */
    v9x_u32 fog_mode;       /* V9X_GLIDE_FOG_*, source byte */
    const v9x_u8 *fog_table; /* V9X_GLIDE_FOG_TABLE_SIZE entries, or null */
} V9X_GLIDE_VERTEX_SETUP;

/* The scales that turn a texture's Glide s and t (0..256 along its longer
 * side) into 0..1, for an aspect ratio. V9X_FALSE for an unknown aspect. */
v9x_u16 v9x_glide_texture_scales(v9x_u32 aspect, float *s_scale,
                                 float *t_scale);

void v9x_glide_vertex_convert(const V9X_GLIDE_VERTEX_SETUP *setup,
                              const float *in, V9X_R3D_ABI_VERTEX *out);

/* The W at which fog table entry `index` applies (the Reference Manual's
 * guFogTableIndexToW): 2^(3 + index / 4) / (8 - index % 4). */
float v9x_glide_fog_index_to_w(unsigned int index);

/* The table's fog amount at W, 0 (none) to 255 (all fog colour),
 * interpolated between the two entries around W. */
v9x_u32 v9x_glide_fog_amount(const v9x_u8 *table, float w);

/* Whether a converted triangle survives the cull mode. The signed area
 * is taken in the coordinates Glide was given, so a lower-left origin's
 * flip does not reverse it. */
v9x_u16 v9x_glide_cull_keep(v9x_u32 cull_mode, v9x_u32 origin,
                            const V9X_R3D_ABI_VERTEX *a,
                            const V9X_R3D_ABI_VERTEX *b,
                            const V9X_R3D_ABI_VERTEX *c);

/* A line as two triangles one pixel wide, its width across the line's
 * minor axis, as the Voodoo draws a line (out holds six vertices). */
void v9x_glide_line_triangles(const V9X_R3D_ABI_VERTEX *a,
                              const V9X_R3D_ABI_VERTEX *b,
                              V9X_R3D_ABI_VERTEX *out);

#endif /* VELOCITY9X_GLIDE_VERTEX_H */
