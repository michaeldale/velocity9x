/*
 * Fixed-function transform and lighting: the key and the constant block
 * (docs\plans\hardware-tnl.md, step 2; the fields are licensed by
 * docs\specifications\haswell-vertex-stage-tnl.md).
 *
 * Both front ends - the OpenGL ICD and the Direct3D DDI 7 core - turn their
 * state into these two structures, and an engine that runs T&L takes
 * nothing else. The KEY names everything that changes the program an engine
 * builds (Haswell compiles one VS kernel per key); the CONSTANTS carry the
 * numbers that program reads. Two draws with equal keys run the same
 * program, so a key is compared as bytes: every reserved field and every
 * unused bit is zero, and v9x_r3d_tnl_key_check refuses a key that is not.
 *
 * What differs between the APIs and cannot be normalised away is in the
 * key (the spot formula, where specular goes, two-sided lighting). What can
 * be normalised the front end does before the constants are written, so no
 * engine sees it:
 *   - lights in eye space, whatever space the API gave them in;
 *   - a directional light as the unit vector toward the light;
 *   - OpenGL's lights have no range: the front end writes the largest
 *     float, so the range test is always applied and never fires;
 *   - an OpenGL spot with a 180 degree cutoff is a point light;
 *   - the infinite viewer's direction is a constant, so its sign is the
 *     front end's decision (the specification note leaves Direct3D's to be
 *     checked);
 *   - OpenGL's colour material is a choice of material source per term.
 *
 * Shared by the 32-bit HAL, the 32-bit ICD and the host tests, so held to
 * the rules of r3d_abi.h: every field is 32 bits (v9x_u32 or float), no
 * enum, no packing pragma, sizes asserted.
 */
#ifndef VELOCITY9X_R3D_TNL_H
#define VELOCITY9X_R3D_TNL_H

#include "velocity9x/status.h"
#include "velocity9x/types.h"

/* The most lights a key may name; Direct3D's dwMaxActiveLights. */
#define V9X_R3D_TNL_MAX_LIGHTS   8u
/* Texture coordinate sets passed through (and, for OpenGL, transformed). */
#define V9X_R3D_TNL_MAX_TEXTURES 2u

/* model: whose equations. They differ in the spot falloff and in where the
 * specular term goes (spec note, section 4). */
#define V9X_R3D_TNL_MODEL_D3D7   1ul
#define V9X_R3D_TNL_MODEL_GL11   2ul

/* flags. */
#define V9X_R3D_TNL_LIGHTING     0x00000001ul /* lit; otherwise colours pass */
#define V9X_R3D_TNL_SPECULAR     0x00000002ul /* compute the specular term */
#define V9X_R3D_TNL_LOCAL_VIEWER 0x00000004ul /* H from the vertex, not a constant */
#define V9X_R3D_TNL_NORMALIZE    0x00000008ul /* renormalise the eye normal */
#define V9X_R3D_TNL_TWO_SIDED    0x00000010ul /* OpenGL: back colours too */
#define V9X_R3D_TNL_FLAGS_ALL    0x0000001Ful

/* inputs: which attributes the vertex carries besides the position. An
 * absent colour reads the constant block's default for it. */
#define V9X_R3D_TNL_IN_NORMAL    0x00000001ul
#define V9X_R3D_TNL_IN_COLOR0    0x00000002ul
#define V9X_R3D_TNL_IN_COLOR1    0x00000004ul
#define V9X_R3D_TNL_IN_ALL       0x00000007ul

/* light_types: two bits per light, light i at bits 2i+1:2i. The values are
 * Direct3D's D3DLIGHTTYPE (dt:779-781), so the D3D front end copies them. */
#define V9X_R3D_TNL_LIGHT_POINT       1ul
#define V9X_R3D_TNL_LIGHT_SPOT        2ul
#define V9X_R3D_TNL_LIGHT_DIRECTIONAL 3ul
#define V9X_R3D_TNL_LIGHT_TYPE(types, i) (((types) >> ((i) * 2u)) & 3ul)

/* material_sources: two bits per term, diffuse at 1:0, specular 3:2,
 * ambient 5:4, emissive 7:6. The values are Direct3D's D3DMATERIALCOLORSOURCE
 * (dt:1508-1513): the material, the vertex's colour 0 or its colour 1. A
 * source naming an absent colour reads that colour's default, which is what
 * the front end sets to the material when the API says to fall back. */
#define V9X_R3D_TNL_SOURCE_MATERIAL   0ul
#define V9X_R3D_TNL_SOURCE_COLOR0     1ul
#define V9X_R3D_TNL_SOURCE_COLOR1     2ul
#define V9X_R3D_TNL_TERM_DIFFUSE      0u
#define V9X_R3D_TNL_TERM_SPECULAR     1u
#define V9X_R3D_TNL_TERM_AMBIENT      2u
#define V9X_R3D_TNL_TERM_EMISSIVE     3u
#define V9X_R3D_TNL_SOURCE(sources, term) (((sources) >> ((term) * 2u)) & 3ul)

/* fog_mode, and the distance it is measured over. */
#define V9X_R3D_TNL_FOG_NONE     0ul
#define V9X_R3D_TNL_FOG_LINEAR   1ul
#define V9X_R3D_TNL_FOG_EXP      2ul
#define V9X_R3D_TNL_FOG_EXP2     3ul
#define V9X_R3D_TNL_FOG_DEPTH    0ul /* |eye z| */
#define V9X_R3D_TNL_FOG_RANGE    1ul /* |eye position| */

typedef struct v9x_r3d_tnl_key {
    v9x_u32 model;            /* V9X_R3D_TNL_MODEL_* */
    v9x_u32 flags;            /* V9X_R3D_TNL_* flags */
    v9x_u32 inputs;           /* V9X_R3D_TNL_IN_* */
    v9x_u32 texture_count;    /* 0 .. V9X_R3D_TNL_MAX_TEXTURES */
    v9x_u32 texture_matrix;   /* bit u: set u goes through its matrix */
    v9x_u32 light_count;      /* 0 .. V9X_R3D_TNL_MAX_LIGHTS */
    v9x_u32 light_types;      /* two bits per light */
    v9x_u32 material_sources; /* two bits per term */
    v9x_u32 fog_mode;         /* V9X_R3D_TNL_FOG_NONE .. EXP2 */
    v9x_u32 fog_distance;     /* V9X_R3D_TNL_FOG_DEPTH or RANGE */
    v9x_u32 reserved[6];      /* zero */
} V9X_R3D_TNL_KEY;

/*
 * One light, seven vec4s, in eye space.
 *
 *   position     a point light or spot's position; a directional light's
 *                unit vector toward the light (w unused)
 *   direction    a spot's unit axis, pointing away from the light
 *   ambient, diffuse, specular   colours (rgba)
 *   attenuation  a0, a1, a2, range: 1 / (a0 + a1 d + a2 d^2) and no light
 *                past range, for positional lights only
 *   spot         D3D7: cos(theta/2), cos(phi/2), falloff,
 *                      1 / (cos(theta/2) - cos(phi/2)), or 0 when equal
 *                GL11: cos(cutoff), exponent, 0, 0
 */
typedef struct v9x_r3d_tnl_light {
    float position[4];
    float direction[4];
    float ambient[4];
    float diffuse[4];
    float specular[4];
    float attenuation[4];
    float spot[4];
} V9X_R3D_TNL_LIGHT;

/* One face's material, five vec4s; power in the first float of the last. */
typedef struct v9x_r3d_tnl_material {
    float ambient[4];
    float diffuse[4];
    float specular[4];
    float emissive[4];
    float power[4];
} V9X_R3D_TNL_MATERIAL;

/*
 * The constant block. Matrices are stored as rows, so output component i
 * is the dot product of row i with the input: one DP4 per row on the EU.
 * The layout does not depend on the key, which keeps the push-constant
 * read length fixed; a program reads only what its key uses.
 *
 *   model_view_projection  object to clip
 *   model_view             object to eye (lighting, fog)
 *   normal                 rows of the inverse transpose of model_view's
 *                          upper 3x3 (w unused)
 *   texture                per set, applied when the key's bit is set
 *   scene_ambient          D3D7 RS_AMBIENT, GL11 LIGHT_MODEL_AMBIENT
 *   viewer                 the infinite viewer's unit direction in eye space
 *   material               front, back (back read only when two-sided)
 *   fog                    start, end, density, 1 / (end - start)
 *   default_color          colour 0 and colour 1 when the vertex lacks them
 */
typedef struct v9x_r3d_tnl_constants {
    float model_view_projection[4][4];
    float model_view[4][4];
    float normal[3][4];
    float texture[V9X_R3D_TNL_MAX_TEXTURES][4][4];
    float scene_ambient[4];
    float viewer[4];
    V9X_R3D_TNL_MATERIAL material[2];
    V9X_R3D_TNL_LIGHT light[V9X_R3D_TNL_MAX_LIGHTS];
    float fog[4];
    float default_color[2][4];
} V9X_R3D_TNL_CONSTANTS;

/*
 * The block as an array of vec4s (16 bytes each), for an engine that pushes
 * only the vec4s its program reads (hsw_vs.c). Light i's field k is
 * V9X_R3D_TNL_VEC4_LIGHT(i) + k, k one of the V9X_R3D_TNL_LF_* below;
 * material face f's is V9X_R3D_TNL_VEC4_MATERIAL(f) + V9X_R3D_TNL_MF_*.
 * tests\host\test_intel_hsw_vs.c holds these to the structure's offsets.
 */
#define V9X_R3D_TNL_VEC4_MVP           0u
#define V9X_R3D_TNL_VEC4_MODEL_VIEW    4u
#define V9X_R3D_TNL_VEC4_NORMAL        8u
#define V9X_R3D_TNL_VEC4_TEXTURE(u)    (11u + 4u * (u))
#define V9X_R3D_TNL_VEC4_SCENE_AMBIENT 19u
#define V9X_R3D_TNL_VEC4_VIEWER        20u
#define V9X_R3D_TNL_VEC4_MATERIAL(f)   (21u + 5u * (f))
#define V9X_R3D_TNL_VEC4_LIGHT(i)      (31u + 7u * (i))
#define V9X_R3D_TNL_VEC4_FOG           87u
#define V9X_R3D_TNL_VEC4_DEFAULT(c)    (88u + (c))
#define V9X_R3D_TNL_VEC4S              90u

#define V9X_R3D_TNL_MF_AMBIENT  0u
#define V9X_R3D_TNL_MF_DIFFUSE  1u
#define V9X_R3D_TNL_MF_SPECULAR 2u
#define V9X_R3D_TNL_MF_EMISSIVE 3u
#define V9X_R3D_TNL_MF_POWER    4u

#define V9X_R3D_TNL_LF_POSITION    0u
#define V9X_R3D_TNL_LF_DIRECTION   1u
#define V9X_R3D_TNL_LF_AMBIENT     2u
#define V9X_R3D_TNL_LF_DIFFUSE     3u
#define V9X_R3D_TNL_LF_SPECULAR    4u
#define V9X_R3D_TNL_LF_ATTENUATION 5u
#define V9X_R3D_TNL_LF_SPOT        6u

/* 64 bytes of key; the block in whole GRFs (32 bytes), and within the 64
 * GRFs 3DSTATE_CONSTANT_VS can read (spec note, "Push constants"). */
typedef char v9x_r3d_tnl_assert_key[sizeof(V9X_R3D_TNL_KEY) == 64 ? 1 : -1];
typedef char v9x_r3d_tnl_assert_constants[
    (sizeof(V9X_R3D_TNL_CONSTANTS) % 32u) == 0u &&
    sizeof(V9X_R3D_TNL_CONSTANTS) <= 64u * 32u ? 1 : -1];
typedef char v9x_r3d_tnl_assert_vec4s[
    sizeof(V9X_R3D_TNL_CONSTANTS) == V9X_R3D_TNL_VEC4S * 16u ? 1 : -1];

/*
 * Whether a key is well formed: a known model, no unknown bit, counts in
 * range, every light type and material source a defined value, no light
 * type or source bit set past the counts, two-sided only for OpenGL, and
 * the reserved words zero. Equal well-formed keys are equal byte for byte.
 */
v9x_status v9x_r3d_tnl_key_check(const V9X_R3D_TNL_KEY *key);

#endif
