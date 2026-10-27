/*
 * Tests for the Direct3D 7 T&L front end (src\display32\d3d\d3d_tnl.c):
 * the untransformed vertex formats, the DX7 records as the DDK lays them
 * out, and the key and constants they make - checked field by field and,
 * for the lit cases, through the reference evaluator against Direct3D 7's
 * equations worked by hand. Those equations are the specification note's,
 * marked recalled there; the guest comparison with the runtime's own T&L
 * is what confirms them.
 */
#include <stdio.h>
#include <string.h>

#include "../../src/display32/d3d/d3d_tnl.h"
#include "../../src/display32/r3d/r3d_tnl_ref.h"

static unsigned int d3d_tnl_failures;

#define DCHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++d3d_tnl_failures; \
    } \
} while (0)

static v9x_u8 rec[512];
static v9x_u32 rec_used;

static int d3d_near(float a, float b)
{
    float d = a - b;

    return d < 1.0e-4f && d > -1.0e-4f;
}

static void put32(v9x_u32 value)
{
    rec[rec_used] = (v9x_u8)value;
    rec[rec_used + 1u] = (v9x_u8)(value >> 8);
    rec[rec_used + 2u] = (v9x_u8)(value >> 16);
    rec[rec_used + 3u] = (v9x_u8)(value >> 24);
    rec_used += 4u;
}

static void putf(float value)
{
    v9x_u32 bits;

    memcpy(&bits, &value, sizeof(bits));
    put32(bits);
}

/* A SETTRANSFORM entry: the type, then sixteen floats, identity plus a
 * translation in the fourth row (Direct3D's row-vector layout). */
static void put_transform(v9x_u32 type, float tx, float ty, float tz)
{
    unsigned int r;
    unsigned int c;

    put32(type);
    for (r = 0u; r < 4u; ++r) {
        for (c = 0u; c < 4u; ++c) {
            float value = r == c ? 1.0f : 0.0f;

            if (r == 3u) {
                value = c == 0u ? tx : c == 1u ? ty : c == 2u ? tz : 1.0f;
            }
            putf(value);
        }
    }
}

/* A D3DLIGHT7 of a type with white diffuse, its position and direction,
 * range 1000, attenuation 1, 0, 0, theta 0.5 and phi 1 radians. */
static void put_light(v9x_u32 type, float px, float py, float pz, float dx,
                      float dy, float dz)
{
    unsigned int k;

    put32(type);
    for (k = 0u; k < 4u; ++k) {
        putf(1.0f);                         /* diffuse */
    }
    for (k = 0u; k < 4u; ++k) {
        putf(k == 3u ? 1.0f : 0.5f);        /* specular */
    }
    for (k = 0u; k < 4u; ++k) {
        putf(0.0f);                         /* ambient */
    }
    putf(px); putf(py); putf(pz);
    putf(dx); putf(dy); putf(dz);
    putf(1000.0f);                          /* range */
    putf(1.0f);                             /* falloff */
    putf(1.0f); putf(0.0f); putf(0.0f);     /* attenuation */
    putf(0.5f);                             /* theta */
    putf(1.0f);                             /* phi */
}

static void test_layout(void)
{
    V9X_D3D_TNL_LAYOUT layout;
    v9x_u8 source[64];
    v9x_u8 vertex[32];
    float normal[3];
    float uv1[2];
    float value;

    /* XYZ | NORMAL | DIFFUSE | TEX2: 12 + 12 + 4, two 2-float sets. */
    DCHECK(v9x_d3d_tnl_layout(0x0252ul, &layout) == 1);
    DCHECK(layout.normal == 12ul && layout.diffuse == 24ul &&
           layout.specular == V9X_D3D_TNL_ABSENT && layout.sets == 2ul &&
           layout.tex[0] == 28ul && layout.tex[1] == 36ul &&
           layout.stride == 44ul);
    /* XYZRHW and XYZB1 are not taken. */
    DCHECK(v9x_d3d_tnl_layout(0x0144ul, &layout) == 0);
    DCHECK(v9x_d3d_tnl_layout(0x0106ul, &layout) == 0);

    /* The vertex: w 1, the colour, set 1 for stage 0 and set 0 for 1. */
    memset(source, 0, sizeof(source));
    value = 2.0f; memcpy(source + 4, &value, 4);
    value = 0.5f; memcpy(source + 20, &value, 4);
    source[24] = 0x11u; source[27] = 0x80u;
    value = 0.25f; memcpy(source + 28, &value, 4);
    value = 0.75f; memcpy(source + 36, &value, 4);
    DCHECK(v9x_d3d_tnl_layout(0x0252ul, &layout) == 1);
    v9x_d3d_tnl_vertex(&layout, 1ul, 0ul, source, vertex, normal, uv1);
    memcpy(&value, vertex + 4, 4);
    DCHECK(value == 2.0f);
    memcpy(&value, vertex + 12, 4);
    DCHECK(value == 1.0f);
    DCHECK(vertex[16] == 0x11u && vertex[19] == 0x80u);
    DCHECK(vertex[20] == 0u && vertex[23] == 0u);
    memcpy(&value, vertex + 24, 4);
    DCHECK(value == 0.75f);
    DCHECK(uv1[0] == 0.25f && normal[2] == 0.5f);
}

/* The DX7 records as the DDK lays them out. */
static void test_records(void)
{
    V9X_D3D_TNL_STATE s;

    v9x_d3d_tnl_init(&s);
    DCHECK(s.lighting == 1ul && s.local_viewer == 1ul &&
           s.source[V9X_R3D_TNL_TERM_DIFFUSE] == 1ul &&
           s.source[V9X_R3D_TNL_TERM_SPECULAR] == 2ul && s.max_z == 1.0f);

    rec_used = 0u;
    put_transform(V9X_D3D_TNL_VIEW, 0.0f, 0.0f, 10.0f);
    put_transform(16ul, 9.0f, 9.0f, 9.0f);   /* a texture matrix: unused */
    DCHECK(v9x_d3d_tnl_record(&s, 36ul, 2ul, rec, rec_used) == 1);
    DCHECK(s.view[3][2] == 10.0f && s.world[3][2] == 0.0f);

    rec_used = 0u;
    put32(3ul); put32(2ul);
    put_light(3ul, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    put32(3ul); put32(0ul);
    put32(40ul); put32(0ul);                 /* past the slots */
    DCHECK(v9x_d3d_tnl_record(&s, 34ul, 3ul, rec, rec_used) == 1);
    DCHECK(s.light[3].type == 3ul && s.light[3].direction[2] == 1.0f &&
           s.light[3].theta == 0.5f && s.light_enabled == 0x8ul &&
           s.ignored == 1ul);

    rec_used = 0u;
    putf(1.0f); putf(0.5f); putf(0.25f); putf(1.0f);     /* diffuse */
    putf(1.0f); putf(1.0f); putf(1.0f); putf(1.0f);      /* ambient */
    putf(0.0f); putf(0.0f); putf(0.0f); putf(0.0f);      /* specular */
    putf(0.0f); putf(0.0f); putf(0.0f); putf(0.0f);      /* emissive */
    putf(5.0f);
    DCHECK(v9x_d3d_tnl_record(&s, 33ul, 1ul, rec, rec_used) == 1);
    DCHECK(s.material.diffuse[1] == 0.5f && s.material.power == 5.0f);

    rec_used = 0u;
    put32(10ul); put32(20ul); put32(300ul); put32(200ul);
    DCHECK(v9x_d3d_tnl_record(&s, 28ul, 1ul, rec, rec_used) == 1);
    rec_used = 0u;
    putf(0.25f); putf(0.75f);
    DCHECK(v9x_d3d_tnl_record(&s, 32ul, 1ul, rec, rec_used) == 1);
    DCHECK(s.viewport_set == 1ul && s.viewport[2] == 300ul &&
           s.min_z == 0.25f && s.max_z == 0.75f);
    DCHECK(v9x_d3d_tnl_record(&s, 42ul, 0ul, rec, 0ul) == 0);
}

/*
 * One directional light from the camera into the scene, direction +z, so
 * the vector toward it is -z; a face toward the camera has normal -z.
 * Diffuse = Ca Ga + Cd Ld (N.L) = (1, 1, 1) 64/255 + (1, 0.5, 0.25) 1,
 * clamped: (1, 0.750980, 0.500980), alpha Cd's 1.
 */
static void test_directional(void)
{
    V9X_D3D_TNL_STATE s;
    V9X_D3D_TNL_LAYOUT layout;
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    V9X_R3D_TNL_INPUT in;
    V9X_R3D_TNL_OUTPUT out;

    v9x_d3d_tnl_init(&s);
    rec_used = 0u;
    put32(0ul); put32(2ul);
    put_light(3ul, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    put32(0ul); put32(0ul);
    DCHECK(v9x_d3d_tnl_record(&s, 34ul, 2ul, rec, rec_used) == 1);
    s.material.diffuse[0] = 1.0f;
    s.material.diffuse[1] = 0.5f;
    s.material.diffuse[2] = 0.25f;
    s.material.diffuse[3] = 1.0f;
    s.material.ambient[0] = s.material.ambient[1] = s.material.ambient[2] =
        1.0f;
    DCHECK(v9x_d3d_tnl_render_state(&s, V9X_D3D_TNL_RS_AMBIENT,
                                    0x00404040ul, 1) == 1);
    /* The same value again changes nothing; a state the device does not
     * read is not its. */
    DCHECK(v9x_d3d_tnl_render_state(&s, V9X_D3D_TNL_RS_AMBIENT,
                                    0x00404040ul, 0) == 0);
    /* Asked without applying, a new value says it would change and is
     * not kept. */
    DCHECK(v9x_d3d_tnl_render_state(&s, V9X_D3D_TNL_RS_AMBIENT,
                                    0x00808080ul, 0) == 1 &&
           s.ambient == 0x00404040ul);
    DCHECK(v9x_d3d_tnl_render_state(&s, 7ul, 1ul, 1) == 0);
    /* XYZ | NORMAL: no colours, so the default sources fall back to the
     * material. */
    DCHECK(v9x_d3d_tnl_layout(0x0012ul, &layout) == 1);
    v9x_d3d_tnl_key(&s, &layout, &key, &c);
    DCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_OK);
    DCHECK(key.model == V9X_R3D_TNL_MODEL_D3D7 && key.light_count == 1ul &&
           V9X_R3D_TNL_LIGHT_TYPE(key.light_types, 0u) ==
               V9X_R3D_TNL_LIGHT_DIRECTIONAL &&
           key.material_sources == 0ul &&
           key.inputs == V9X_R3D_TNL_IN_NORMAL &&
           (key.flags & V9X_R3D_TNL_LOCAL_VIEWER) != 0ul);
    DCHECK(c.light[0].position[2] == -1.0f && c.viewer[2] == -1.0f);

    memset(&in, 0, sizeof(in));
    in.position[3] = 1.0f;
    in.normal[2] = -1.0f;
    DCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    DCHECK(d3d_near(out.color[0][0], 1.0f) &&
           d3d_near(out.color[0][1], 0.750980f) &&
           d3d_near(out.color[0][2], 0.500980f) &&
           d3d_near(out.color[0][3], 1.0f));

    /* With the vertex's diffuse, COLOR1 is the diffuse source again. */
    DCHECK(v9x_d3d_tnl_layout(0x0052ul, &layout) == 1);
    v9x_d3d_tnl_key(&s, &layout, &key, &c);
    DCHECK(V9X_R3D_TNL_SOURCE(key.material_sources,
                              V9X_R3D_TNL_TERM_DIFFUSE) ==
           V9X_R3D_TNL_SOURCE_COLOR0);
    /* COLORVERTEX off: the material for every term. */
    (void)v9x_d3d_tnl_render_state(&s, V9X_D3D_TNL_RS_COLORVERTEX, 0ul, 1);
    v9x_d3d_tnl_key(&s, &layout, &key, &c);
    DCHECK(key.material_sources == 0ul);
}

/* Transforms: world after view after projection, as rows for column
 * vectors; a light through the view matrix into eye space. */
static void test_transforms(void)
{
    V9X_D3D_TNL_STATE s;
    V9X_D3D_TNL_LAYOUT layout;
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    V9X_R3D_TNL_INPUT in;
    V9X_R3D_TNL_OUTPUT out;

    v9x_d3d_tnl_init(&s);
    rec_used = 0u;
    put_transform(V9X_D3D_TNL_WORLD, 1.0f, 2.0f, 3.0f);
    put_transform(V9X_D3D_TNL_VIEW, 0.0f, 0.0f, 10.0f);
    DCHECK(v9x_d3d_tnl_record(&s, 36ul, 2ul, rec, rec_used) == 1);
    rec_used = 0u;
    put32(1ul); put32(2ul);
    put_light(1ul, 4.0f, 5.0f, 6.0f, 0.0f, 0.0f, 1.0f);
    put32(1ul); put32(0ul);
    DCHECK(v9x_d3d_tnl_record(&s, 34ul, 2ul, rec, rec_used) == 1);
    (void)v9x_d3d_tnl_render_state(&s, V9X_D3D_TNL_RS_LIGHTING, 0ul, 1);
    DCHECK(v9x_d3d_tnl_layout(0x0002ul, &layout) == 1);
    v9x_d3d_tnl_key(&s, &layout, &key, &c);
    DCHECK(key.flags == 0ul && key.light_count == 0ul && key.inputs == 0ul);
    memset(&in, 0, sizeof(in));
    in.position[3] = 1.0f;
    DCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    DCHECK(out.clip[0] == 1.0f && out.clip[1] == 2.0f &&
           out.clip[2] == 13.0f && out.clip[3] == 1.0f);
    /* Unlit without colours: white and transparent black. */
    DCHECK(out.color[0][0] == 1.0f && out.color[0][3] == 1.0f &&
           out.color[1][0] == 0.0f && out.color[1][3] == 0.0f);

    (void)v9x_d3d_tnl_render_state(&s, V9X_D3D_TNL_RS_LIGHTING, 1ul, 1);
    v9x_d3d_tnl_key(&s, &layout, &key, &c);
    DCHECK(key.light_count == 1ul &&
           V9X_R3D_TNL_LIGHT_TYPE(key.light_types, 0u) ==
               V9X_R3D_TNL_LIGHT_POINT);
    DCHECK(c.light[0].position[0] == 4.0f &&
           c.light[0].position[2] == 16.0f &&
           c.light[0].attenuation[3] == 1000.0f);
}

/* A spot's cone constants from theta 0.5 and phi 1 radians. */
static void test_spot(void)
{
    V9X_D3D_TNL_STATE s;
    V9X_D3D_TNL_LAYOUT layout;
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;

    v9x_d3d_tnl_init(&s);
    rec_used = 0u;
    put32(0ul); put32(2ul);
    put_light(2ul, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f);
    put32(0ul); put32(0ul);
    DCHECK(v9x_d3d_tnl_record(&s, 34ul, 2ul, rec, rec_used) == 1);
    DCHECK(v9x_d3d_tnl_layout(0x0012ul, &layout) == 1);
    v9x_d3d_tnl_key(&s, &layout, &key, &c);
    DCHECK(V9X_R3D_TNL_LIGHT_TYPE(key.light_types, 0u) ==
           V9X_R3D_TNL_LIGHT_SPOT);
    /* cos 0.25 = 0.968912, cos 0.5 = 0.877583, and 1 / their difference
     * 10.94933. */
    DCHECK(d3d_near(c.light[0].spot[0], 0.968912f) &&
           d3d_near(c.light[0].spot[1], 0.877583f) &&
           c.light[0].spot[2] == 1.0f &&
           d3d_near(c.light[0].spot[3] / 10.94933f, 1.0f));
    DCHECK(c.light[0].direction[2] == 1.0f);
}

/* Fog: vertex fog by its mode and distance; table fog only under a
 * W-friendly projection, by eye depth; otherwise none, counted. */
static void test_fog(void)
{
    V9X_D3D_TNL_STATE s;
    V9X_D3D_TNL_LAYOUT layout;
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;

    v9x_d3d_tnl_init(&s);
    DCHECK(v9x_d3d_tnl_layout(0x0002ul, &layout) == 1);
    (void)v9x_d3d_tnl_render_state(&s, V9X_D3D_TNL_RS_FOGENABLE, 1ul, 1);
    (void)v9x_d3d_tnl_render_state(&s, V9X_D3D_TNL_RS_FOGVERTEXMODE, 3ul, 1);
    (void)v9x_d3d_tnl_render_state(&s, V9X_D3D_TNL_RS_RANGEFOGENABLE, 1ul, 1);
    (void)v9x_d3d_tnl_render_state(&s, V9X_D3D_TNL_RS_FOGSTART,
                                   0x40000000ul, 1);   /* 2.0 */
    (void)v9x_d3d_tnl_render_state(&s, V9X_D3D_TNL_RS_FOGEND,
                                   0x41200000ul, 1);   /* 10.0 */
    v9x_d3d_tnl_key(&s, &layout, &key, &c);
    DCHECK(key.fog_mode == V9X_R3D_TNL_FOG_LINEAR &&
           key.fog_distance == V9X_R3D_TNL_FOG_RANGE &&
           c.fog[0] == 2.0f && c.fog[1] == 10.0f &&
           d3d_near(c.fog[3], 0.125f));

    (void)v9x_d3d_tnl_render_state(&s, V9X_D3D_TNL_RS_FOGTABLEMODE, 1ul, 1);
    v9x_d3d_tnl_key(&s, &layout, &key, &c);
    DCHECK(key.fog_mode == V9X_R3D_TNL_FOG_NONE && s.ignored == 1ul);
    s.projection[2][3] = 1.0f;
    s.projection[3][3] = 0.0f;
    v9x_d3d_tnl_key(&s, &layout, &key, &c);
    DCHECK(key.fog_mode == V9X_R3D_TNL_FOG_EXP &&
           key.fog_distance == V9X_R3D_TNL_FOG_DEPTH);
}

unsigned int v9x_run_d3d_tnl_tests(void)
{
    d3d_tnl_failures = 0u;
    test_layout();
    test_records();
    test_directional();
    test_transforms();
    test_spot();
    test_fog();
    return d3d_tnl_failures;
}
