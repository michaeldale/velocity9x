/*
 * The Direct3D 7 T&L device's front end (docs\plans\hardware-tnl.md, step
 * 6): a context's transform, light, material and lighting-state records, the
 * untransformed vertex formats, and the T&L key and constant block they
 * make (include\velocity9x\r3d_tnl.h). Pure, so the host tests hold it to
 * the DDK's layouts and Direct3D's equations; d3d_core.c feeds it the DP2
 * records and draws what it builds through the engine's draw_tnl.
 *
 * Direct3D's matrices are row-major for row vectors, v' = v M, and its
 * lights are in world space. The block wants rows for column vectors and
 * lights in eye space, so both are turned here and no engine sees either.
 * Lighting happens in eye (camera) space, where the viewer is at the origin
 * looking down +z.
 */
#ifndef VELOCITY9X_D3D_TNL_H
#define VELOCITY9X_D3D_TNL_H

#include "velocity9x/r3d_tnl.h"
#include "velocity9x/types.h"

/* Light indices a context keeps; a higher one is ignored and counted. */
#define V9X_D3D_TNL_LIGHT_SLOTS 32u

/* Render states the T&L device reads (d3dtypes.h, D3DRENDERSTATETYPE). */
#define V9X_D3D_TNL_RS_CULLMODE         22ul
#define V9X_D3D_TNL_RS_FOGENABLE        28ul
#define V9X_D3D_TNL_RS_SPECULARENABLE   29ul
#define V9X_D3D_TNL_RS_FOGTABLEMODE     35ul
#define V9X_D3D_TNL_RS_FOGSTART         36ul
#define V9X_D3D_TNL_RS_FOGEND           37ul
#define V9X_D3D_TNL_RS_FOGDENSITY       38ul
#define V9X_D3D_TNL_RS_RANGEFOGENABLE   48ul
#define V9X_D3D_TNL_RS_LIGHTING         137ul
#define V9X_D3D_TNL_RS_AMBIENT          139ul
#define V9X_D3D_TNL_RS_FOGVERTEXMODE    140ul
#define V9X_D3D_TNL_RS_COLORVERTEX      141ul
#define V9X_D3D_TNL_RS_LOCALVIEWER      142ul
#define V9X_D3D_TNL_RS_NORMALIZENORMALS 143ul
#define V9X_D3D_TNL_RS_DIFFUSEMATERIALSOURCE  145ul
#define V9X_D3D_TNL_RS_SPECULARMATERIALSOURCE 146ul
#define V9X_D3D_TNL_RS_AMBIENTMATERIALSOURCE  147ul
#define V9X_D3D_TNL_RS_EMISSIVEMATERIALSOURCE 148ul
#define V9X_D3D_TNL_RS_VERTEXBLEND      151ul

/* SETTRANSFORM's types (D3DTRANSFORMSTATETYPE). */
#define V9X_D3D_TNL_WORLD      1ul
#define V9X_D3D_TNL_VIEW       2ul
#define V9X_D3D_TNL_PROJECTION 3ul

/* D3DLIGHT7, 104 bytes (d3dtypes.h). */
typedef struct v9x_d3d_light7 {
    v9x_u32 type;               /* 1 point, 2 spot, 3 directional */
    float diffuse[4];
    float specular[4];
    float ambient[4];
    float position[3];
    float direction[3];
    float range;
    float falloff;
    float attenuation[3];
    float theta;
    float phi;
} V9X_D3D_LIGHT7;

/* D3DMATERIAL7, 68 bytes: diffuse, ambient, specular, emissive, power. */
typedef struct v9x_d3d_material7 {
    float diffuse[4];
    float ambient[4];
    float specular[4];
    float emissive[4];
    float power;
} V9X_D3D_MATERIAL7;

typedef char v9x_d3d_tnl_assert_light7[
    sizeof(V9X_D3D_LIGHT7) == 104u ? 1 : -1];
typedef char v9x_d3d_tnl_assert_material7[
    sizeof(V9X_D3D_MATERIAL7) == 68u ? 1 : -1];

typedef struct v9x_d3d_tnl_state {
    float world[4][4];          /* D3D's layout: m[row][column] */
    float view[4][4];
    float projection[4][4];
    V9X_D3D_LIGHT7 light[V9X_D3D_TNL_LIGHT_SLOTS];
    v9x_u32 light_enabled;      /* one bit a slot */
    V9X_D3D_MATERIAL7 material;
    v9x_u32 lighting;
    v9x_u32 ambient;            /* D3DCOLOR */
    v9x_u32 specular_enable;
    v9x_u32 local_viewer;
    v9x_u32 normalize;
    v9x_u32 color_vertex;
    v9x_u32 source[4];          /* diffuse, specular, ambient, emissive */
    v9x_u32 fog_enable;
    v9x_u32 fog_table_mode;
    v9x_u32 fog_vertex_mode;
    v9x_u32 range_fog;
    float fog_start;
    float fog_end;
    float fog_density;
    v9x_u32 vertex_blend;
    /* VIEWPORTINFO and ZRANGE; viewport_set zero until one arrives, when
     * the target's whole extent is the viewport. */
    v9x_u32 viewport_set;
    v9x_u32 viewport[4];        /* x, y, width, height */
    float min_z;
    float max_z;
    /* Records and states seen that the key cannot carry, for the
     * diagnostics: a light index past the slots, more than eight lights
     * enabled, vertex blending, table fog with a projection that is not
     * W-friendly; counted, and which of them as V9X_D3D_TNL_IGNORED_*. */
    v9x_u32 ignored;
    v9x_u32 ignored_reasons;
} V9X_D3D_TNL_STATE;

#define V9X_D3D_TNL_IGNORED_SLOT      0x1ul
#define V9X_D3D_TNL_IGNORED_LIGHTS    0x2ul
#define V9X_D3D_TNL_IGNORED_BLEND     0x4ul
#define V9X_D3D_TNL_IGNORED_TABLE_FOG 0x8ul

/* Direct3D 7's initial state: identity matrices, no lights, a black
 * material, LIGHTING, COLORVERTEX and LOCALVIEWER on, diffuse from colour 1
 * and specular from colour 2. */
void v9x_d3d_tnl_init(V9X_D3D_TNL_STATE *state);

/* A render state: non-zero when it is one the T&L device reads and its
 * value would change, so a batch gathered under the old one must be drawn
 * first. Set only when `apply`; the caller still applies it to the
 * rasterisation state as before. */
int v9x_d3d_tnl_render_state(V9X_D3D_TNL_STATE *state, v9x_u32 rs,
                             v9x_u32 value, int apply);

/*
 * A DP2 record the T&L device keeps: SETTRANSFORM, SETLIGHT, CREATELIGHT,
 * SETMATERIAL, VIEWPORTINFO, ZRANGE; its length already checked by the
 * walker (d3d_dp2.c). Non-zero when the record was one of these.
 */
int v9x_d3d_tnl_record(V9X_D3D_TNL_STATE *state, v9x_u32 op, v9x_u32 count,
                       const v9x_u8 *payload, v9x_u32 bytes);

/*
 * An untransformed vertex format: D3DFVF_XYZ, an optional NORMAL, DIFFUSE,
 * SPECULAR and up to eight coordinate sets. Positions with blend weights,
 * XYZRHW and point sizes are not taken (0).
 */
#define V9X_D3D_TNL_ABSENT 0xfffffffful

typedef struct v9x_d3d_tnl_layout {
    v9x_u32 stride;
    v9x_u32 normal;             /* byte offsets, or V9X_D3D_TNL_ABSENT */
    v9x_u32 diffuse;
    v9x_u32 specular;
    v9x_u32 sets;
    v9x_u32 tex[8];
    v9x_u32 tex_floats[8];
} V9X_D3D_TNL_LAYOUT;

int v9x_d3d_tnl_layout(v9x_u32 fvf, V9X_D3D_TNL_LAYOUT *layout);

/*
 * One vertex as draw_tnl takes it: x y z 1 where sx sy sz rhw are, the
 * colours (opaque white and transparent black when absent, which the key
 * then does not read), set0's u and v; set1's in uv1 when uv1 is not null;
 * the normal (zero when absent) in normal. A set the vertex lacks reads set
 * 0; a vertex with no set reads 0, 0.
 */
void v9x_d3d_tnl_vertex(const V9X_D3D_TNL_LAYOUT *layout, v9x_u32 set0,
                        v9x_u32 set1, const v9x_u8 *source, v9x_u8 *vertex,
                        float *normal, float *uv1);

/*
 * The key and constant block for drawing a layout under a state. The
 * viewer, for a key without a local viewer, is (0, 0, -1): from a vertex in
 * front of the camera back toward it. Up to eight enabled lights, in slot
 * order; more are ignored and counted.
 */
void v9x_d3d_tnl_key(V9X_D3D_TNL_STATE *state,
                     const V9X_D3D_TNL_LAYOUT *layout,
                     V9X_R3D_TNL_KEY *key, V9X_R3D_TNL_CONSTANTS *constants);

#endif
