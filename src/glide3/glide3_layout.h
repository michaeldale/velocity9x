/*
 * Glide 3 to the shared Glide engine's terms (docs\plans\glide-3x-wrapper.md):
 * the vertex layout grVertexLayout declares, a vertex read through it into
 * a Glide 2 GrVertex (glide_vertex.h), GrTexInfo's log2 LOD and aspect
 * numbers turned into Glide 2's, and a primitive's vertices as triangles
 * or lines. GLIDE3X.DLL's front end (glide3_dll.c) calls these, then the
 * engine in src\glide\glide_core.c.
 *
 * Glide API values are facts from 3dfx's Glide 3 glide.h; nothing of it is
 * copied (licence rule in the plan). Pure: no Windows headers.
 */
#ifndef VELOCITY9X_GLIDE3_LAYOUT_H
#define VELOCITY9X_GLIDE3_LAYOUT_H

#include "velocity9x/types.h"
#include "../glide/glide_vertex.h"

/* grVertexLayout params (glide.h GR_PARAM_*) and modes. */
#define V9X_GLIDE3_PARAM_XY      0x01ul
#define V9X_GLIDE3_PARAM_Z       0x02ul
#define V9X_GLIDE3_PARAM_W       0x03ul
#define V9X_GLIDE3_PARAM_Q       0x04ul
#define V9X_GLIDE3_PARAM_FOG_EXT 0x05ul
#define V9X_GLIDE3_PARAM_A       0x10ul
#define V9X_GLIDE3_PARAM_RGB     0x20ul
#define V9X_GLIDE3_PARAM_PARGB   0x30ul
#define V9X_GLIDE3_PARAM_ST0     0x40ul
#define V9X_GLIDE3_PARAM_ST1     0x41ul
#define V9X_GLIDE3_PARAM_ST2     0x42ul
#define V9X_GLIDE3_PARAM_Q0      0x50ul
#define V9X_GLIDE3_PARAM_Q1      0x51ul
#define V9X_GLIDE3_PARAM_Q2      0x52ul
#define V9X_GLIDE3_PARAM_DISABLE 0x00ul

/* grDrawVertexArray modes (glide.h). */
#define V9X_GLIDE3_POINTS                  0ul
#define V9X_GLIDE3_LINE_STRIP              1ul
#define V9X_GLIDE3_LINES                   2ul
#define V9X_GLIDE3_POLYGON                 3ul
#define V9X_GLIDE3_TRIANGLE_STRIP          4ul
#define V9X_GLIDE3_TRIANGLE_FAN            5ul
#define V9X_GLIDE3_TRIANGLES               6ul
#define V9X_GLIDE3_TRIANGLE_STRIP_CONTINUE 7ul
#define V9X_GLIDE3_TRIANGLE_FAN_CONTINUE   8ul

/*
 * Where each parameter the engine uses sits in a vertex: a byte offset
 * plus one, zero when disabled. Fog coordinates and TMUs 1 and 2 are
 * accepted and not read (one TMU, no fog-coordinate fog).
 */
typedef struct v9x_glide3_layout {
    v9x_u32 xy;
    v9x_u32 z;
    v9x_u32 w;
    v9x_u32 q;
    v9x_u32 a;
    v9x_u32 rgb;
    v9x_u32 pargb;
    v9x_u32 st0;
    v9x_u32 q0;
} V9X_GLIDE3_LAYOUT;

/* Every parameter disabled. */
void v9x_glide3_layout_init(V9X_GLIDE3_LAYOUT *layout);

/* grVertexLayout(param, offset, mode). V9X_FALSE for a param glide.h does
 * not define; it changes nothing. */
v9x_u16 v9x_glide3_layout_set(V9X_GLIDE3_LAYOUT *layout, v9x_u32 param,
                              v9x_u32 offset, v9x_u32 mode);

/*
 * The vertex at `vertex` as GrVertex floats (out holds
 * V9X_GLIDE_VERTEX_FLOATS): x and y; ooz from Z; oow from Q, else 1/W,
 * else Q0, else 1; red, green and blue from RGB floats or from PARGB
 * (always 0xAARRGGBB, whatever the colour format: glide.h); alpha from A
 * or PARGB; s/w and t/w from ST0, already premultiplied as Glide 2's sow
 * and tow are; TMU 0's oow from Q0, else the vertex oow. A colour or alpha
 * the layout leaves out reads as 255, so a combine that ignores it draws
 * the same.
 */
void v9x_glide3_vertex_read(const V9X_GLIDE3_LAYOUT *layout,
                            const v9x_u8 *vertex, float *out);

/*
 * A Glide 3 GrTexInfo (smallLodLog2, largeLodLog2, aspectRatioLog2,
 * format, data: five 32-bit fields) as Glide 2's (smallLod, largeLod,
 * aspectRatio, format, data). Glide 3 counts LOD as log2 of the larger
 * side (GR_LOD_LOG2_256 = 8) where Glide 2 counts down from 256
 * (GR_LOD_256 = 0), and aspect as log2 of width over height (8x1 = 3)
 * where Glide 2 counts from 8x1 = 0 to 1x8 = 6. The format numbers and
 * the data pointer carry over. V9X_FALSE for a value outside those ranges.
 */
v9x_u16 v9x_glide3_texinfo_to_glide2(const v9x_u32 *glide3, v9x_u32 *glide2);
/* One Glide 3 log2 LOD or aspect as Glide 2's; V9X_FALSE when outside. */
v9x_u16 v9x_glide3_lod_to_glide2(v9x_u32 lod_log2, v9x_u32 *lod);
v9x_u16 v9x_glide3_aspect_to_glide2(v9x_u32 aspect_log2, v9x_u32 *aspect);

/*
 * The triangles a primitive of `count` vertices makes, and the vertex
 * indices of triangle n. Polygons and fans pivot on vertex 0; strips
 * swap the first two indices of every odd triangle so each keeps the
 * winding of the first. The continue modes extend a previous call's
 * strip or fan, which this does not keep; they make none. So do points
 * and lines.
 */
v9x_u32 v9x_glide3_triangle_count(v9x_u32 mode, v9x_u32 count);
void v9x_glide3_triangle_at(v9x_u32 mode, v9x_u32 n, v9x_u32 *indices);

/* The same for GR_LINES (pairs) and GR_LINE_STRIP (each vertex to the
 * next); every other mode makes none. */
v9x_u32 v9x_glide3_line_count(v9x_u32 mode, v9x_u32 count);
void v9x_glide3_line_at(v9x_u32 mode, v9x_u32 n, v9x_u32 *indices);

#endif /* VELOCITY9X_GLIDE3_LAYOUT_H */
