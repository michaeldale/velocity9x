/*
 * Tests for the T&L key rule and the reference evaluator
 * (include\velocity9x\r3d_tnl.h, src\display32\r3d\r3d_tnl_ref.c).
 *
 * Every expected value is worked by hand from the equations in
 * docs\specifications\haswell-vertex-stage-tnl.md, section 4, and the
 * arithmetic is written next to the case, so a failure can be checked
 * against the specification without running anything. Geometry is kept
 * axis-aligned or on round angles for that reason. Direct3D 7's equations
 * there are marked recalled; these cases encode them as recalled, and a
 * guest comparison against the runtime's T&L (plan step 6) is what
 * confirms or corrects them.
 */
#include <stdio.h>
#include <string.h>

#include "../../src/display32/r3d/r3d_tnl_ref.h"

static unsigned int tnl_failures = 0u;

#define TCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++tnl_failures; \
    } \
} while (0)

#define TNL_EPSILON 1.0e-5f

static int tnl_near(float actual, float expected)
{
    float difference = actual - expected;

    if (difference < 0.0f) {
        difference = -difference;
    }
    return difference <= TNL_EPSILON;
}

static int tnl_near4(const float *actual, float x, float y, float z, float w)
{
    return tnl_near(actual[0], x) && tnl_near(actual[1], y) &&
           tnl_near(actual[2], z) && tnl_near(actual[3], w);
}

static void tnl_set4(float *out, float x, float y, float z, float w)
{
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = w;
}

static void tnl_identity(float m[4][4])
{
    unsigned int row;

    memset(m, 0, sizeof(float) * 16u);
    for (row = 0u; row < 4u; ++row) {
        m[row][row] = 1.0f;
    }
}

static void tnl_key(V9X_R3D_TNL_KEY *key, v9x_u32 model)
{
    memset(key, 0, sizeof(*key));
    key->model = model;
}

/* Identity matrices, the GL/D3D infinite viewer +z, default colours white
 * and a distinguishable colour 1, everything else zero. */
static void tnl_constants(V9X_R3D_TNL_CONSTANTS *c)
{
    unsigned int row;

    memset(c, 0, sizeof(*c));
    tnl_identity(c->model_view_projection);
    tnl_identity(c->model_view);
    for (row = 0u; row < 3u; ++row) {
        c->normal[row][row] = 1.0f;
    }
    tnl_identity(c->texture[0]);
    tnl_identity(c->texture[1]);
    tnl_set4(c->viewer, 0.0f, 0.0f, 1.0f, 0.0f);
    tnl_set4(c->default_color[0], 1.0f, 1.0f, 1.0f, 1.0f);
    tnl_set4(c->default_color[1], 0.1f, 0.2f, 0.3f, 0.4f);
}

static void tnl_input(V9X_R3D_TNL_INPUT *in)
{
    memset(in, 0, sizeof(*in));
    tnl_set4(in->position, 0.0f, 0.0f, 0.0f, 1.0f);
    tnl_set4(in->normal, 0.0f, 0.0f, 1.0f, 0.0f);
}

/* A lit key with one light of the given type, normals in. */
static void tnl_lit_key(V9X_R3D_TNL_KEY *key, v9x_u32 model, v9x_u32 type)
{
    tnl_key(key, model);
    key->flags = V9X_R3D_TNL_LIGHTING;
    key->inputs = V9X_R3D_TNL_IN_NORMAL;
    key->light_count = 1ul;
    key->light_types = type;
}

static void test_key_check(void)
{
    V9X_R3D_TNL_KEY key;

    TCHECK(v9x_r3d_tnl_key_check(0) == V9X_STATUS_INVALID_ARGUMENT);

    tnl_key(&key, V9X_R3D_TNL_MODEL_D3D7);
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_OK);
    tnl_key(&key, V9X_R3D_TNL_MODEL_GL11);
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_OK);
    tnl_key(&key, 0ul);
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_INVALID_ARGUMENT);
    tnl_key(&key, 3ul);
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_INVALID_ARGUMENT);

    /* Unknown bits. */
    tnl_key(&key, V9X_R3D_TNL_MODEL_D3D7);
    key.flags = 0x20ul;
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_INVALID_ARGUMENT);
    tnl_key(&key, V9X_R3D_TNL_MODEL_D3D7);
    key.inputs = 0x8ul;
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_INVALID_ARGUMENT);

    /* Texture sets: the count, and no matrix bit past it. */
    tnl_key(&key, V9X_R3D_TNL_MODEL_GL11);
    key.texture_count = 2ul;
    key.texture_matrix = 3ul;
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_OK);
    key.texture_count = 3ul;
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_INVALID_ARGUMENT);
    key.texture_count = 1ul;
    key.texture_matrix = 2ul;
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_INVALID_ARGUMENT);

    /* Lights: count, a defined type for each, nothing past the count. */
    tnl_key(&key, V9X_R3D_TNL_MODEL_D3D7);
    key.light_count = 8ul;
    key.light_types = 0xFFFFul;  /* eight directional lights */
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_OK);
    key.light_count = 9ul;
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_INVALID_ARGUMENT);
    key.light_count = 1ul;
    key.light_types = 0ul;       /* type 0 is not a light */
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_INVALID_ARGUMENT);
    key.light_types = 0x7ul;     /* light 1 typed, but only one light */
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_INVALID_ARGUMENT);

    /* Material sources: 3 is undefined, nothing past the four terms. */
    tnl_key(&key, V9X_R3D_TNL_MODEL_D3D7);
    key.material_sources = 0x96ul; /* 2, 1, 1, 2 */
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_OK);
    key.material_sources = 0x3ul;
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_INVALID_ARGUMENT);
    key.material_sources = 0x100ul;
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_INVALID_ARGUMENT);

    /* Fog. */
    tnl_key(&key, V9X_R3D_TNL_MODEL_D3D7);
    key.fog_mode = V9X_R3D_TNL_FOG_EXP2;
    key.fog_distance = V9X_R3D_TNL_FOG_RANGE;
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_OK);
    key.fog_mode = 4ul;
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_INVALID_ARGUMENT);
    key.fog_mode = V9X_R3D_TNL_FOG_LINEAR;
    key.fog_distance = 2ul;
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_INVALID_ARGUMENT);

    /* Two-sided lighting exists only in OpenGL. */
    tnl_key(&key, V9X_R3D_TNL_MODEL_D3D7);
    key.flags = V9X_R3D_TNL_TWO_SIDED;
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_INVALID_ARGUMENT);
    key.model = V9X_R3D_TNL_MODEL_GL11;
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_OK);

    /* Reserved words. */
    tnl_key(&key, V9X_R3D_TNL_MODEL_D3D7);
    key.reserved[5] = 1ul;
    TCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_refusals(void)
{
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    V9X_R3D_TNL_INPUT in;
    V9X_R3D_TNL_OUTPUT out;

    tnl_key(&key, V9X_R3D_TNL_MODEL_D3D7);
    tnl_constants(&c);
    tnl_input(&in);
    TCHECK(v9x_r3d_tnl_ref_vertex(0, &c, &in, &out) == V9X_STATUS_INVALID_ARGUMENT);
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, 0, &in, &out) == V9X_STATUS_INVALID_ARGUMENT);
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, 0, &out) == V9X_STATUS_INVALID_ARGUMENT);
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, 0) == V9X_STATUS_INVALID_ARGUMENT);

    /* A bad key leaves the output alone. */
    key.model = 0ul;
    memset(&out, 0x5A, sizeof(out));
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_INVALID_ARGUMENT);
    TCHECK(((const unsigned char *)&out)[0] == 0x5Au);
    TCHECK(((const unsigned char *)&out)[sizeof(out) - 1u] == 0x5Au);
}

/* Unlit: the position through the matrix, colours and texture coordinates
 * passed through, an absent colour read from its default, no fog. */
static void test_unlit_transform(void)
{
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    V9X_R3D_TNL_INPUT in;
    V9X_R3D_TNL_OUTPUT out;

    tnl_key(&key, V9X_R3D_TNL_MODEL_D3D7);
    key.inputs = V9X_R3D_TNL_IN_COLOR0;
    key.texture_count = 1ul;
    tnl_constants(&c);
    tnl_set4(c.model_view_projection[0], 2.0f, 0.0f, 0.0f, 1.0f);
    tnl_set4(c.model_view_projection[1], 0.0f, 3.0f, 0.0f, -1.0f);
    tnl_set4(c.model_view_projection[2], 0.0f, 0.0f, 0.5f, 0.25f);
    tnl_set4(c.model_view_projection[3], 0.0f, 0.0f, 1.0f, 0.0f);
    tnl_input(&in);
    tnl_set4(in.position, 1.0f, 2.0f, 3.0f, 1.0f);
    tnl_set4(in.color[0], 0.2f, 0.4f, 0.6f, 0.8f);
    tnl_set4(in.texture[0], 0.5f, 0.25f, 0.0f, 1.0f);

    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    /* (2*1 + 1, 3*2 - 1, 0.5*3 + 0.25, 3) */
    TCHECK(tnl_near4(out.clip, 3.0f, 5.0f, 1.75f, 3.0f));
    TCHECK(tnl_near4(out.color[0], 0.2f, 0.4f, 0.6f, 0.8f));
    TCHECK(tnl_near4(out.color[1], 0.1f, 0.2f, 0.3f, 0.4f));
    TCHECK(tnl_near4(out.back_color[0], 0.2f, 0.4f, 0.6f, 0.8f));
    TCHECK(tnl_near(out.fog, 1.0f));
    TCHECK(tnl_near4(out.texture[0], 0.5f, 0.25f, 0.0f, 1.0f));

    /* Colours leave clamped to [0, 1]. */
    tnl_set4(in.color[0], 1.5f, -0.5f, 0.5f, 2.0f);
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near4(out.color[0], 1.0f, 0.0f, 0.5f, 1.0f));
}

/* OpenGL's texture matrix, applied only to the sets whose bit is set. */
static void test_texture_matrix(void)
{
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    V9X_R3D_TNL_INPUT in;
    V9X_R3D_TNL_OUTPUT out;

    tnl_key(&key, V9X_R3D_TNL_MODEL_GL11);
    key.texture_count = 2ul;
    key.texture_matrix = 1ul;
    tnl_constants(&c);
    tnl_set4(c.texture[0][0], 1.0f, 0.0f, 0.0f, 0.5f);
    tnl_set4(c.texture[0][1], 0.0f, 2.0f, 0.0f, 0.0f);
    tnl_set4(c.texture[1][0], 9.0f, 9.0f, 9.0f, 9.0f); /* must not apply */
    tnl_input(&in);
    tnl_set4(in.texture[0], 0.25f, 0.5f, 0.0f, 1.0f);
    tnl_set4(in.texture[1], 0.75f, 0.125f, 0.0f, 1.0f);

    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    /* (0.25 + 0.5, 2 * 0.5, 0, 1) */
    TCHECK(tnl_near4(out.texture[0], 0.75f, 1.0f, 0.0f, 1.0f));
    TCHECK(tnl_near4(out.texture[1], 0.75f, 0.125f, 0.0f, 1.0f));
}

/* Direct3D directional light, normal facing it: diffuse = Cd * Ld, alpha
 * Cd.a; then facing away: ambient and emissive only. */
static void test_directional(void)
{
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    V9X_R3D_TNL_INPUT in;
    V9X_R3D_TNL_OUTPUT out;

    tnl_lit_key(&key, V9X_R3D_TNL_MODEL_D3D7, V9X_R3D_TNL_LIGHT_DIRECTIONAL);
    tnl_constants(&c);
    tnl_set4(c.light[0].position, 0.0f, 0.0f, 1.0f, 0.0f);
    tnl_set4(c.light[0].diffuse, 1.0f, 0.5f, 0.25f, 1.0f);
    tnl_set4(c.material[0].diffuse, 0.5f, 0.5f, 0.5f, 0.8f);
    tnl_input(&in);

    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near4(out.color[0], 0.5f, 0.25f, 0.125f, 0.8f));
    TCHECK(tnl_near4(out.color[1], 0.0f, 0.0f, 0.0f, 0.0f));
    TCHECK(tnl_near4(out.clip, 0.0f, 0.0f, 0.0f, 1.0f));

    /* Facing away: (Ga + La) * Ca + Ce = (0.2 + 0.1) * (1, 0.5, 1) +
     * (0.05, 0, 0) = (0.35, 0.15, 0.3); alpha is still Cd.a. */
    tnl_set4(in.normal, 0.0f, 0.0f, -1.0f, 0.0f);
    tnl_set4(c.scene_ambient, 0.2f, 0.2f, 0.2f, 0.0f);
    tnl_set4(c.light[0].ambient, 0.1f, 0.1f, 0.1f, 0.0f);
    tnl_set4(c.material[0].ambient, 1.0f, 0.5f, 1.0f, 0.0f);
    tnl_set4(c.material[0].emissive, 0.05f, 0.0f, 0.0f, 0.0f);
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near4(out.color[0], 0.35f, 0.15f, 0.3f, 0.8f));
}

/* A point light at (0, 0, 2) over a vertex at the origin: d = 2,
 * L = (0, 0, 1), N.L = 1. */
static void test_point_attenuation_and_range(void)
{
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    V9X_R3D_TNL_INPUT in;
    V9X_R3D_TNL_OUTPUT out;

    tnl_lit_key(&key, V9X_R3D_TNL_MODEL_D3D7, V9X_R3D_TNL_LIGHT_POINT);
    tnl_constants(&c);
    tnl_set4(c.light[0].position, 0.0f, 0.0f, 2.0f, 1.0f);
    tnl_set4(c.light[0].diffuse, 0.9f, 0.6f, 0.3f, 1.0f);
    tnl_set4(c.light[0].ambient, 0.5f, 0.5f, 0.5f, 0.0f);
    tnl_set4(c.light[0].attenuation, 1.0f, 1.0f, 0.25f, 10.0f);
    tnl_set4(c.material[0].diffuse, 1.0f, 1.0f, 1.0f, 1.0f);
    tnl_input(&in);

    /* 1 / (1 + 2 + 0.25 * 4) = 0.25: diffuse (0.225, 0.15, 0.075); the
     * material's ambient is zero, so the light's ambient adds nothing. */
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near4(out.color[0], 0.225f, 0.15f, 0.075f, 1.0f));

    /* Past the range the light gives nothing, ambient included:
     * a material ambient of 1 would otherwise add 0.25 * 0.5. */
    tnl_set4(c.material[0].ambient, 1.0f, 1.0f, 1.0f, 0.0f);
    c.light[0].attenuation[3] = 1.5f;
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near4(out.color[0], 0.0f, 0.0f, 0.0f, 1.0f));
    c.light[0].attenuation[3] = 10.0f;
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near4(out.color[0], 0.35f, 0.275f, 0.2f, 1.0f));
}

/*
 * A spot at (0, 0, 2) aimed down -z, over a vertex at (2, 0, 0) with normal
 * +z: L = (-1, 0, 1) / sqrt 2, N.L = rho = 0.7071068, d = 2.828427,
 * attenuation 1. Diffuse colours are white, so colour 0 = spot * N.L.
 */
static void spot_scene(V9X_R3D_TNL_KEY *key, V9X_R3D_TNL_CONSTANTS *c,
                       V9X_R3D_TNL_INPUT *in, v9x_u32 model)
{
    tnl_lit_key(key, model, V9X_R3D_TNL_LIGHT_SPOT);
    tnl_constants(c);
    tnl_set4(c->light[0].position, 0.0f, 0.0f, 2.0f, 1.0f);
    tnl_set4(c->light[0].direction, 0.0f, 0.0f, -1.0f, 0.0f);
    tnl_set4(c->light[0].diffuse, 1.0f, 1.0f, 1.0f, 1.0f);
    tnl_set4(c->light[0].attenuation, 1.0f, 0.0f, 0.0f, 3.4e38f);
    tnl_set4(c->material[0].diffuse, 1.0f, 1.0f, 1.0f, 1.0f);
    tnl_input(in);
    tnl_set4(in->position, 2.0f, 0.0f, 0.0f, 1.0f);
}

static void test_spot_d3d7(void)
{
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    V9X_R3D_TNL_INPUT in;
    V9X_R3D_TNL_OUTPUT out;

    spot_scene(&key, &c, &in, V9X_R3D_TNL_MODEL_D3D7);

    /* Between the cones: (0.7071068 - 0.5) * 2.5 = 0.5177670, falloff 1;
     * times N.L: 0.3661165. */
    tnl_set4(c.light[0].spot, 0.9f, 0.5f, 1.0f, 2.5f);
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near(out.color[0][0], 0.3661165f));

    /* Falloff 2: 0.5177670^2 * 0.7071068 = 0.1895621. */
    c.light[0].spot[2] = 2.0f;
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near(out.color[0][0], 0.1895621f));

    /* Outside the outer cone (rho 0.707 below cos(phi/2) 0.8): nothing. */
    tnl_set4(c.light[0].spot, 0.9f, 0.8f, 1.0f, 10.0f);
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near(out.color[0][0], 0.0f));

    /* Inside the inner cone: spot 1, colour N.L. */
    tnl_set4(c.light[0].spot, 0.7f, 0.5f, 1.0f, 5.0f);
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near(out.color[0][0], 0.7071068f));
}

static void test_spot_gl11(void)
{
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    V9X_R3D_TNL_INPUT in;
    V9X_R3D_TNL_OUTPUT out;

    spot_scene(&key, &c, &in, V9X_R3D_TNL_MODEL_GL11);

    /* Inside the cutoff: rho^2 = 0.5; times N.L: 0.3535534. */
    tnl_set4(c.light[0].spot, 0.6f, 2.0f, 0.0f, 0.0f);
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near(out.color[0][0], 0.3535534f));

    /* Outside it: a hard edge, nothing. */
    c.light[0].spot[0] = 0.75f;
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near(out.color[0][0], 0.0f));
}

/*
 * Specular with the infinite viewer: L = (0, 0.6, 0.8), N = +z, V = +z.
 * H = norm(0, 0.6, 1.8), N.H = 1.8 / 1.8973666 = 0.9486833, power 2: 0.9.
 * Diffuse: N.L 0.8 * 0.5 = 0.4.
 */
static void specular_scene(V9X_R3D_TNL_KEY *key, V9X_R3D_TNL_CONSTANTS *c,
                           V9X_R3D_TNL_INPUT *in, v9x_u32 model)
{
    tnl_lit_key(key, model, V9X_R3D_TNL_LIGHT_DIRECTIONAL);
    key->flags |= V9X_R3D_TNL_SPECULAR;
    tnl_constants(c);
    tnl_set4(c->light[0].position, 0.0f, 0.6f, 0.8f, 0.0f);
    tnl_set4(c->light[0].diffuse, 1.0f, 1.0f, 1.0f, 1.0f);
    tnl_set4(c->light[0].specular, 1.0f, 1.0f, 1.0f, 1.0f);
    tnl_set4(c->material[0].diffuse, 0.5f, 0.5f, 0.5f, 1.0f);
    tnl_set4(c->material[0].specular, 1.0f, 0.5f, 0.25f, 1.0f);
    c->material[0].power[0] = 2.0f;
    tnl_input(in);
}

static void test_specular(void)
{
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    V9X_R3D_TNL_INPUT in;
    V9X_R3D_TNL_OUTPUT out;

    /* Direct3D: specular is its own colour, alpha 0. */
    specular_scene(&key, &c, &in, V9X_R3D_TNL_MODEL_D3D7);
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near4(out.color[0], 0.4f, 0.4f, 0.4f, 1.0f));
    TCHECK(tnl_near4(out.color[1], 0.9f, 0.45f, 0.225f, 0.0f));

    /* Without the flag, no specular. */
    key.flags &= ~V9X_R3D_TNL_SPECULAR;
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near4(out.color[1], 0.0f, 0.0f, 0.0f, 0.0f));

    /* OpenGL 1.1: summed into the primary and clamped; colour 1 zero. */
    specular_scene(&key, &c, &in, V9X_R3D_TNL_MODEL_GL11);
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near4(out.color[0], 1.0f, 0.85f, 0.625f, 1.0f));
    TCHECK(tnl_near4(out.color[1], 0.0f, 0.0f, 0.0f, 0.0f));

    /* N.L at or below zero: no specular even though N.H is positive
     * (H = norm(0, 0.6, 0.2), N.H = 0.316). */
    specular_scene(&key, &c, &in, V9X_R3D_TNL_MODEL_D3D7);
    tnl_set4(c.light[0].position, 0.0f, 0.6f, -0.8f, 0.0f);
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near4(out.color[1], 0.0f, 0.0f, 0.0f, 0.0f));
}

/*
 * The local viewer: a vertex at (2, 0, -2) in eye space, L = +z, N = +z.
 * Toward the eye: (-1, 0, 1) / sqrt 2. H = norm(-0.7071068, 0, 1.7071068),
 * N.H = 1.7071068 / 1.8477591 = 0.9238795, power 1. The infinite viewer
 * would give H = +z and 1.
 */
static void test_local_viewer(void)
{
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    V9X_R3D_TNL_INPUT in;
    V9X_R3D_TNL_OUTPUT out;

    tnl_lit_key(&key, V9X_R3D_TNL_MODEL_D3D7, V9X_R3D_TNL_LIGHT_DIRECTIONAL);
    key.flags |= V9X_R3D_TNL_SPECULAR;
    tnl_constants(&c);
    tnl_set4(c.light[0].position, 0.0f, 0.0f, 1.0f, 0.0f);
    tnl_set4(c.light[0].specular, 1.0f, 1.0f, 1.0f, 1.0f);
    tnl_set4(c.material[0].specular, 1.0f, 1.0f, 1.0f, 1.0f);
    c.material[0].power[0] = 1.0f;
    tnl_input(&in);
    tnl_set4(in.position, 2.0f, 0.0f, -2.0f, 1.0f);

    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near(out.color[1][0], 1.0f));
    key.flags |= V9X_R3D_TNL_LOCAL_VIEWER;
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near(out.color[1][0], 0.9238795f));
}

/* Material sources: the diffuse term from the vertex's colour 0, then from
 * an absent colour 1, which reads its default. */
static void test_material_sources(void)
{
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    V9X_R3D_TNL_INPUT in;
    V9X_R3D_TNL_OUTPUT out;

    tnl_lit_key(&key, V9X_R3D_TNL_MODEL_D3D7, V9X_R3D_TNL_LIGHT_DIRECTIONAL);
    key.inputs |= V9X_R3D_TNL_IN_COLOR0;
    key.material_sources = V9X_R3D_TNL_SOURCE_COLOR0
                           << (V9X_R3D_TNL_TERM_DIFFUSE * 2u);
    tnl_constants(&c);
    tnl_set4(c.light[0].position, 0.0f, 0.0f, 1.0f, 0.0f);
    tnl_set4(c.light[0].diffuse, 1.0f, 1.0f, 1.0f, 1.0f);
    tnl_set4(c.material[0].diffuse, 0.9f, 0.9f, 0.9f, 0.9f);
    tnl_input(&in);
    tnl_set4(in.color[0], 0.2f, 0.4f, 0.6f, 0.5f);

    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near4(out.color[0], 0.2f, 0.4f, 0.6f, 0.5f));

    key.material_sources = V9X_R3D_TNL_SOURCE_COLOR1
                           << (V9X_R3D_TNL_TERM_DIFFUSE * 2u);
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near4(out.color[0], 0.1f, 0.2f, 0.3f, 0.4f));
}

/* Two-sided: a normal facing away from the light lights the back with the
 * negated normal and the back material. */
static void test_two_sided(void)
{
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    V9X_R3D_TNL_INPUT in;
    V9X_R3D_TNL_OUTPUT out;

    tnl_lit_key(&key, V9X_R3D_TNL_MODEL_GL11, V9X_R3D_TNL_LIGHT_DIRECTIONAL);
    key.flags |= V9X_R3D_TNL_TWO_SIDED;
    tnl_constants(&c);
    tnl_set4(c.light[0].position, 0.0f, 0.0f, 1.0f, 0.0f);
    tnl_set4(c.light[0].diffuse, 1.0f, 1.0f, 1.0f, 1.0f);
    tnl_set4(c.material[0].diffuse, 1.0f, 0.0f, 0.0f, 1.0f);
    tnl_set4(c.material[1].diffuse, 0.0f, 1.0f, 0.0f, 0.5f);
    tnl_input(&in);
    tnl_set4(in.normal, 0.0f, 0.0f, -1.0f, 0.0f);

    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near4(out.color[0], 0.0f, 0.0f, 0.0f, 1.0f));
    TCHECK(tnl_near4(out.back_color[0], 0.0f, 1.0f, 0.0f, 0.5f));
}

/* The normal matrix and NORMALIZE: a normal of length 2 doubles N.L unless
 * renormalised; a matrix that swaps x and z turns +x into +z. */
static void test_normals(void)
{
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    V9X_R3D_TNL_INPUT in;
    V9X_R3D_TNL_OUTPUT out;

    tnl_lit_key(&key, V9X_R3D_TNL_MODEL_D3D7, V9X_R3D_TNL_LIGHT_DIRECTIONAL);
    tnl_constants(&c);
    tnl_set4(c.light[0].position, 0.0f, 0.0f, 1.0f, 0.0f);
    tnl_set4(c.light[0].diffuse, 0.25f, 0.25f, 0.25f, 1.0f);
    tnl_set4(c.material[0].diffuse, 1.0f, 1.0f, 1.0f, 1.0f);
    tnl_input(&in);
    tnl_set4(in.normal, 0.0f, 0.0f, 2.0f, 0.0f);

    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near(out.color[0][0], 0.5f));
    key.flags |= V9X_R3D_TNL_NORMALIZE;
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near(out.color[0][0], 0.25f));

    tnl_set4(c.normal[0], 0.0f, 0.0f, 1.0f, 0.0f);
    tnl_set4(c.normal[2], 1.0f, 0.0f, 0.0f, 0.0f);
    tnl_set4(in.normal, 1.0f, 0.0f, 0.0f, 0.0f);
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near(out.color[0][0], 0.25f));
}

/* Two lights add. */
static void test_two_lights(void)
{
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    V9X_R3D_TNL_INPUT in;
    V9X_R3D_TNL_OUTPUT out;

    tnl_lit_key(&key, V9X_R3D_TNL_MODEL_D3D7, V9X_R3D_TNL_LIGHT_DIRECTIONAL);
    key.light_count = 2ul;
    key.light_types = V9X_R3D_TNL_LIGHT_DIRECTIONAL |
                      (V9X_R3D_TNL_LIGHT_POINT << 2u);
    tnl_constants(&c);
    tnl_set4(c.light[0].position, 0.0f, 0.0f, 1.0f, 0.0f);
    tnl_set4(c.light[0].diffuse, 0.25f, 0.0f, 0.0f, 1.0f);
    tnl_set4(c.light[1].position, 0.0f, 0.0f, 4.0f, 1.0f);
    tnl_set4(c.light[1].diffuse, 0.0f, 0.5f, 0.0f, 1.0f);
    tnl_set4(c.light[1].attenuation, 1.0f, 0.0f, 0.0f, 100.0f);
    tnl_set4(c.material[0].diffuse, 1.0f, 1.0f, 1.0f, 1.0f);
    tnl_input(&in);

    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near4(out.color[0], 0.25f, 0.5f, 0.0f, 1.0f));
}

/*
 * Fog over the eye position, which the model-view matrix moves: row 2
 * translates z by -3, so a vertex at (4, 0, 0) is at (4, 0, -3) in eye
 * space, depth 3, range 5. Linear from 1 to 5: (5 - d) / 4.
 */
static void test_fog(void)
{
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    V9X_R3D_TNL_INPUT in;
    V9X_R3D_TNL_OUTPUT out;

    tnl_key(&key, V9X_R3D_TNL_MODEL_GL11);
    key.fog_mode = V9X_R3D_TNL_FOG_LINEAR;
    key.fog_distance = V9X_R3D_TNL_FOG_DEPTH;
    tnl_constants(&c);
    tnl_set4(c.model_view[2], 0.0f, 0.0f, 1.0f, -3.0f);
    tnl_set4(c.fog, 1.0f, 5.0f, 0.25f, 0.25f);
    tnl_input(&in);
    tnl_set4(in.position, 4.0f, 0.0f, 0.0f, 1.0f);

    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near(out.fog, 0.5f));
    key.fog_distance = V9X_R3D_TNL_FOG_RANGE;
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near(out.fog, 0.0f));

    /* Clamped: at depth 0 the line gives 1.25. */
    key.fog_distance = V9X_R3D_TNL_FOG_DEPTH;
    tnl_set4(c.model_view[2], 0.0f, 0.0f, 1.0f, 0.0f);
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near(out.fog, 1.0f));

    /* Exponential at depth 2, density 0.25: e^-0.5 and e^-0.25. */
    tnl_set4(c.model_view[2], 0.0f, 0.0f, 1.0f, -2.0f);
    key.fog_mode = V9X_R3D_TNL_FOG_EXP;
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near(out.fog, 0.6065307f));
    key.fog_mode = V9X_R3D_TNL_FOG_EXP2;
    TCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    TCHECK(tnl_near(out.fog, 0.7788008f));
}

unsigned int v9x_run_r3d_tnl_tests(void)
{
    tnl_failures = 0u;
    test_key_check();
    test_refusals();
    test_unlit_transform();
    test_texture_matrix();
    test_directional();
    test_point_attenuation_and_range();
    test_spot_d3d7();
    test_spot_gl11();
    test_specular();
    test_local_viewer();
    test_material_sources();
    test_two_sided();
    test_normals();
    test_two_lights();
    test_fog();
    if (tnl_failures == 0u) {
        puts("PASS: r3d T&L key and reference evaluator");
    }
    return tnl_failures;
}
