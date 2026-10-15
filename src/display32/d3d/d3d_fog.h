/*
 * Direct3D table (pixel) fog, emulated per vertex.
 *
 * No engine this driver programs has a fog table: the Mach64 class, the
 * Rage II, the ViRGE, the SiS 6326 and Gen3 all fog from a factor carried
 * in the vertex specular alpha (docs\plans\rage-pro-table-fog-and-paletted-
 * textures.md). So table fog is a factor computed here for each vertex
 * from its depth and the FOGTABLE* render states, written into that alpha,
 * and interpolated by the engine like vertex fog. The factor is exact at
 * the vertices and linear across the triangle in screen space; a true
 * table is evaluated per pixel, so a large triangle spanning an EXP curve
 * fogs a little differently.
 *
 * Pure: no DDHAL header. tests\host\test_d3d_fog.c holds it.
 */
#ifndef VELOCITY9X_D3D_FOG_H
#define VELOCITY9X_D3D_FOG_H

#include "velocity9x/types.h"
#include "../r3d/r3d.h"

/* D3DFOGMODE, d3dtypes.h. */
#define V9X_D3D_FOG_NONE    0ul
#define V9X_D3D_FOG_EXP     1ul
#define V9X_D3D_FOG_EXP2    2ul
#define V9X_D3D_FOG_LINEAR  3ul

/* The FOGTABLE* render states as the runtime set them. START, END and
 * DENSITY are D3DVALUEs carried in DWORDs, so these are a float's bits. */
typedef struct v9x_d3d_fog_table {
    v9x_u32 mode;
    v9x_u32 start;
    v9x_u32 end;
    v9x_u32 density;
} V9X_D3D_FOG_TABLE;

/* Direct3D's defaults: NONE, start 0.0, end 1.0, density 1.0. */
void v9x_d3d_fog_table_init(V9X_D3D_FOG_TABLE *table);

/* Whether table fog replaces vertex fog: FOGENABLE set and a mode other
 * than NONE. An unknown mode is NONE. */
int v9x_d3d_fog_table_active(v9x_u32 fog_enable,
                             const V9X_D3D_FOG_TABLE *table);

/*
 * The depth the factor is taken at. Device Z, which is what Direct3D
 * defines for a device without D3DPRASTERCAPS_WFOG, and this driver does
 * not claim it. Two exceptions, each a title passing eye units regardless
 * of the caps, whose depth is then eye W, 1/rhw: a LINEAR range that ends
 * past 1.0, which device Z never reaches; and an EXP or EXP2 density below
 * 1.0, which over device Z's 0..1 fogs at most 63 % and in practice not
 * visibly - Thief II sets EXP at 0.0107 (A8U4I5, boot 479). Without a
 * usable rhw it stays Z.
 */
float v9x_d3d_fog_table_depth(const V9X_D3D_FOG_TABLE *table, float sz,
                              float rhw);

/* The fog factor at a depth, 0 (all fog colour) to 255 (no fog). */
v9x_u32 v9x_d3d_fog_table_factor(const V9X_D3D_FOG_TABLE *table,
                                 float depth);

/* Every vertex's specular alpha replaced by its table factor; the
 * specular RGB is left alone. */
void v9x_d3d_fog_table_apply(const V9X_D3D_FOG_TABLE *table,
                             V9X_R3D_VERTEX *vertices, v9x_u32 count);

#endif
