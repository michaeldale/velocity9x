/*
 * Tests for OpenGL lighting state (src\opengl\gl_light.c): the initial
 * values of table 6.9, glLight's transforms and limits, glMaterial's faces,
 * glColorMaterial, the queries, and the key and constants a lit T&L draw
 * gets - checked by hand and, for one case, by running the result through
 * the reference evaluator against GL 1.1's equation worked by hand.
 */
#include <stdio.h>
#include <string.h>

#include "../../src/opengl/gl_light.h"
#include "../../src/display32/r3d/r3d_tnl_ref.h"

static unsigned int gl_light_failures;

#define LCHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++gl_light_failures; \
    } \
} while (0)

#define GL_FRONT_FACE_ 0x0404u
#define GL_BACK_FACE_  0x0405u
#define GL_BOTH_FACES_ 0x0408u

static V9X_GL_STATE s;
static V9X_GL_LIGHTING l;

static int light_near(float a, float b)
{
    float d = a - b;

    return d < 1.0e-5f && d > -1.0e-5f;
}

static void fresh(void)
{
    v9x_gl_state_init(&s);
    v9x_gl_lighting_init(&l);
}

/* Column-major: a translation by (tx, ty, tz) after a scale of 2, 4, 5. */
static void scale_translate(GLfloat *m, GLfloat tx, GLfloat ty, GLfloat tz)
{
    memset(m, 0, 16u * sizeof(GLfloat));
    m[0] = 2.0f;
    m[5] = 4.0f;
    m[10] = 5.0f;
    m[12] = tx;
    m[13] = ty;
    m[14] = tz;
    m[15] = 1.0f;
}

static void test_initial(void)
{
    GLfloat out[4];

    fresh();
    LCHECK(v9x_gl_get_light(&s, &l, V9X_GL_LIGHT0, V9X_GL_DIFFUSE, out) ==
           4u && out[0] == 1.0f && out[3] == 1.0f);
    LCHECK(v9x_gl_get_light(&s, &l, V9X_GL_LIGHT0 + 1u, V9X_GL_SPECULAR,
                            out) == 4u && out[0] == 0.0f && out[3] == 1.0f);
    LCHECK(v9x_gl_get_light(&s, &l, V9X_GL_LIGHT0, V9X_GL_POSITION, out) ==
           4u && out[2] == 1.0f && out[3] == 0.0f);
    LCHECK(v9x_gl_get_light(&s, &l, V9X_GL_LIGHT0, V9X_GL_SPOT_CUTOFF,
                            out) == 1u && out[0] == 180.0f);
    LCHECK(v9x_gl_get_material(&s, &l, GL_FRONT_FACE_, V9X_GL_DIFFUSE,
                               out) == 4u && light_near(out[0], 0.8f));
    LCHECK(v9x_gl_get_material(&s, &l, GL_BACK_FACE_, V9X_GL_AMBIENT,
                               out) == 4u && light_near(out[1], 0.2f));
    LCHECK(l.color_material_face == GL_BOTH_FACES_ &&
           l.color_material_mode == V9X_GL_AMBIENT_AND_DIFFUSE);
    /* A material query names one face. */
    LCHECK(v9x_gl_get_material(&s, &l, GL_BOTH_FACES_, V9X_GL_DIFFUSE,
                               out) == 0u &&
           v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
}

/* POSITION through the whole matrix, SPOT_DIRECTION through its 3x3. */
static void test_light_transform(void)
{
    GLfloat m[16];
    GLfloat p[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    GLfloat d[3] = { 0.0f, 1.0f, -1.0f };
    GLfloat out[4];

    fresh();
    scale_translate(m, 10.0f, 20.0f, 30.0f);
    v9x_gl_light(&s, &l, V9X_GL_LIGHT0 + 3u, V9X_GL_POSITION, p, 1, m);
    v9x_gl_light(&s, &l, V9X_GL_LIGHT0 + 3u, V9X_GL_SPOT_DIRECTION, d, 1, m);
    LCHECK(v9x_gl_get_light(&s, &l, V9X_GL_LIGHT0 + 3u, V9X_GL_POSITION,
                            out) == 4u &&
           out[0] == 12.0f && out[1] == 24.0f && out[2] == 35.0f &&
           out[3] == 1.0f);
    LCHECK(v9x_gl_get_light(&s, &l, V9X_GL_LIGHT0 + 3u,
                            V9X_GL_SPOT_DIRECTION, out) == 3u &&
           out[0] == 0.0f && out[1] == 4.0f && out[2] == -5.0f);
    LCHECK(v9x_gl_state_get_error(&s) == 0u);
}

static void test_light_limits(void)
{
    GLfloat m[16];
    GLfloat v[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
    GLfloat bad;

    fresh();
    scale_translate(m, 0.0f, 0.0f, 0.0f);
    v9x_gl_light(&s, &l, V9X_GL_LIGHT0 + 8u, V9X_GL_DIFFUSE, v, 1, m);
    LCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_light(&s, &l, V9X_GL_LIGHT0, V9X_GL_DIFFUSE, v, 0, m);
    LCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    bad = 129.0f;
    v9x_gl_light(&s, &l, V9X_GL_LIGHT0, V9X_GL_SPOT_EXPONENT, &bad, 0, m);
    LCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    bad = 91.0f;
    v9x_gl_light(&s, &l, V9X_GL_LIGHT0, V9X_GL_SPOT_CUTOFF, &bad, 0, m);
    LCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    bad = 180.0f;
    v9x_gl_light(&s, &l, V9X_GL_LIGHT0, V9X_GL_SPOT_CUTOFF, &bad, 0, m);
    LCHECK(v9x_gl_state_get_error(&s) == 0u);
    bad = -0.5f;
    v9x_gl_light(&s, &l, V9X_GL_LIGHT0, V9X_GL_LINEAR_ATTENUATION, &bad, 0,
                 m);
    LCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    bad = 0.25f;
    v9x_gl_light(&s, &l, V9X_GL_LIGHT0, V9X_GL_QUADRATIC_ATTENUATION, &bad,
                 0, m);
    LCHECK(l.light[0].attenuation[2] == 0.25f);
}

static void test_material_faces(void)
{
    GLfloat v[4] = { 0.1f, 0.2f, 0.3f, 0.4f };
    GLfloat shine = 200.0f;

    fresh();
    v9x_gl_material(&s, &l, GL_BACK_FACE_, V9X_GL_EMISSION, v, 1);
    LCHECK(l.material[1].emission[2] == 0.3f &&
           l.material[0].emission[2] == 0.0f);
    v9x_gl_material(&s, &l, GL_BOTH_FACES_, V9X_GL_AMBIENT_AND_DIFFUSE, v, 1);
    LCHECK(l.material[0].ambient[0] == 0.1f &&
           l.material[1].diffuse[3] == 0.4f);
    v9x_gl_material(&s, &l, GL_FRONT_FACE_, V9X_GL_SHININESS, &shine, 0);
    LCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    shine = 64.0f;
    v9x_gl_material(&s, &l, GL_FRONT_FACE_, V9X_GL_SHININESS, &shine, 0);
    LCHECK(l.material[0].shininess == 64.0f &&
           l.material[1].shininess == 0.0f);
    v9x_gl_material(&s, &l, GL_FRONT_FACE_, V9X_GL_DIFFUSE, v, 0);
    LCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_color_material(&s, &l, GL_FRONT_FACE_, V9X_GL_POSITION);
    LCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_color_material(&s, &l, GL_FRONT_FACE_, V9X_GL_SPECULAR);
    LCHECK(l.color_material_face == GL_FRONT_FACE_ &&
           l.color_material_mode == V9X_GL_SPECULAR);
}

static void key_from(V9X_R3D_TNL_KEY *key, V9X_R3D_TNL_CONSTANTS *c,
                     const GLfloat *mv)
{
    unsigned int r;
    unsigned int col;

    memset(key, 0, sizeof(*key));
    memset(c, 0, sizeof(*c));
    key->model = V9X_R3D_TNL_MODEL_GL11;
    key->inputs = V9X_R3D_TNL_IN_COLOR0 | V9X_R3D_TNL_IN_COLOR1;
    for (r = 0u; r < 4u; ++r) {
        for (col = 0u; col < 4u; ++col) {
            c->model_view[r][col] = mv[col * 4u + r];
            c->model_view_projection[r][col] = mv[col * 4u + r];
        }
    }
    v9x_gl_lighting_tnl(&s, &l, key, c);
}

/* The key's lights, in LIGHT0..7 order of the enabled ones, their types
 * and constants, and the flags. */
static void test_key(void)
{
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    GLfloat m[16];
    GLfloat p[4] = { 0.0f, 3.0f, 4.0f, 0.0f };
    GLfloat q[4] = { 2.0f, 4.0f, 6.0f, 2.0f };
    GLfloat cutoff = 60.0f;
    GLfloat white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    fresh();
    memset(m, 0, sizeof(m));
    m[0] = m[5] = m[10] = m[15] = 1.0f;

    /* Lighting off: nothing. */
    key_from(&key, &c, m);
    LCHECK(key.flags == 0ul && key.light_count == 0ul);

    v9x_gl_state_enable(&s, V9X_GL_LIGHTING_CAP, 1);
    v9x_gl_state_enable(&s, V9X_GL_LIGHT0 + 1u, 1);
    v9x_gl_state_enable(&s, V9X_GL_LIGHT0 + 4u, 1);
    v9x_gl_state_enable(&s, V9X_GL_LIGHT0 + 6u, 1);
    v9x_gl_light(&s, &l, V9X_GL_LIGHT0 + 1u, V9X_GL_POSITION, p, 1, m);
    v9x_gl_light(&s, &l, V9X_GL_LIGHT0 + 4u, V9X_GL_POSITION, q, 1, m);
    v9x_gl_light(&s, &l, V9X_GL_LIGHT0 + 6u, V9X_GL_POSITION, q, 1, m);
    v9x_gl_light(&s, &l, V9X_GL_LIGHT0 + 6u, V9X_GL_SPOT_CUTOFF, &cutoff, 0,
                 m);
    key_from(&key, &c, m);
    LCHECK(key.flags == V9X_R3D_TNL_LIGHTING);
    LCHECK(key.light_count == 3ul);
    LCHECK(V9X_R3D_TNL_LIGHT_TYPE(key.light_types, 0u) ==
           V9X_R3D_TNL_LIGHT_DIRECTIONAL);
    LCHECK(V9X_R3D_TNL_LIGHT_TYPE(key.light_types, 1u) ==
           V9X_R3D_TNL_LIGHT_POINT);
    LCHECK(V9X_R3D_TNL_LIGHT_TYPE(key.light_types, 2u) ==
           V9X_R3D_TNL_LIGHT_SPOT);
    LCHECK((key.inputs & V9X_R3D_TNL_IN_NORMAL) != 0ul);
    LCHECK(v9x_r3d_tnl_key_check(&key) == V9X_STATUS_OK);
    /* Directional (0, 3, 4) to its unit vector; positional over w. */
    LCHECK(light_near(c.light[0].position[1], 0.6f) &&
           light_near(c.light[0].position[2], 0.8f));
    LCHECK(c.light[1].position[0] == 1.0f && c.light[1].position[2] == 3.0f &&
           c.light[1].position[3] == 1.0f);
    LCHECK(c.light[1].attenuation[0] == 1.0f &&
           c.light[1].attenuation[3] > 1.0e38f);
    /* cos 60 = 0.5; the default direction (0, 0, -1). */
    LCHECK(light_near(c.light[2].spot[0], 0.5f) && c.light[2].spot[1] == 0.0f &&
           c.light[2].direction[2] == -1.0f);

    /* Specular from a light and the material: then the flag. */
    v9x_gl_light(&s, &l, V9X_GL_LIGHT0 + 4u, V9X_GL_SPECULAR, white, 1, m);
    key_from(&key, &c, m);
    LCHECK((key.flags & V9X_R3D_TNL_SPECULAR) == 0ul);
    v9x_gl_material(&s, &l, GL_FRONT_FACE_, V9X_GL_SPECULAR, white, 1);
    key_from(&key, &c, m);
    LCHECK((key.flags & V9X_R3D_TNL_SPECULAR) != 0ul);

    /* Colour material: the front, and the mode's terms from colour 0. */
    v9x_gl_state_enable(&s, V9X_GL_COLOR_MATERIAL, 1);
    key_from(&key, &c, m);
    LCHECK(V9X_R3D_TNL_SOURCE(key.material_sources,
                              V9X_R3D_TNL_TERM_AMBIENT) ==
               V9X_R3D_TNL_SOURCE_COLOR0 &&
           V9X_R3D_TNL_SOURCE(key.material_sources,
                              V9X_R3D_TNL_TERM_DIFFUSE) ==
               V9X_R3D_TNL_SOURCE_COLOR0 &&
           V9X_R3D_TNL_SOURCE(key.material_sources,
                              V9X_R3D_TNL_TERM_EMISSIVE) ==
               V9X_R3D_TNL_SOURCE_MATERIAL);
    v9x_gl_color_material(&s, &l, GL_BACK_FACE_, V9X_GL_EMISSION);
    key_from(&key, &c, m);
    LCHECK(key.material_sources == 0ul);

    /* Flags from the model and NORMALIZE; two-sided is not expressible. */
    v9x_gl_state_enable(&s, V9X_GL_NORMALIZE, 1);
    l.local_viewer = 1u;
    key_from(&key, &c, m);
    LCHECK((key.flags & V9X_R3D_TNL_NORMALIZE) != 0ul &&
           (key.flags & V9X_R3D_TNL_LOCAL_VIEWER) != 0ul);
    LCHECK(v9x_gl_lighting_expressible(&s, &l));
    l.two_side = 1u;
    LCHECK(!v9x_gl_lighting_expressible(&s, &l));
}

/* The normal matrix: the inverse transpose of the model-view's 3x3. For a
 * scale of 2, 4, 5 that is 1/2, 1/4, 1/5 on the diagonal. */
static void test_normal_matrix(void)
{
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    GLfloat m[16];

    fresh();
    v9x_gl_state_enable(&s, V9X_GL_LIGHTING_CAP, 1);
    scale_translate(m, 7.0f, 8.0f, 9.0f);
    key_from(&key, &c, m);
    LCHECK(light_near(c.normal[0][0], 0.5f) && light_near(c.normal[1][1], 0.25f) &&
           light_near(c.normal[2][2], 0.2f) && c.normal[0][1] == 0.0f &&
           c.normal[2][0] == 0.0f);
}

/*
 * The default state lit, through the reference evaluator: LIGHT0 is white
 * diffuse from +z (directional), the material ambient 0.2 and diffuse 0.8,
 * the model ambient 0.2. A normal +z gives (2.13.1)
 *   e + a_cm a_cs + d_cm d_cl (n.L) = 0 + 0.04 + 0.8 = 0.84,
 * alpha d_cm's 1. A normal at 60 degrees from +z gives 0.04 + 0.8 * 0.5.
 */
static void test_default_lit(void)
{
    V9X_R3D_TNL_KEY key;
    V9X_R3D_TNL_CONSTANTS c;
    V9X_R3D_TNL_INPUT in;
    V9X_R3D_TNL_OUTPUT out;
    GLfloat m[16];

    fresh();
    v9x_gl_state_enable(&s, V9X_GL_LIGHTING_CAP, 1);
    v9x_gl_state_enable(&s, V9X_GL_LIGHT0, 1);
    memset(m, 0, sizeof(m));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    key_from(&key, &c, m);
    memset(&in, 0, sizeof(in));
    in.position[3] = 1.0f;
    in.normal[2] = 1.0f;
    LCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    LCHECK(light_near(out.color[0][0], 0.84f) && light_near(out.color[0][2], 0.84f) &&
           light_near(out.color[0][3], 1.0f));
    in.normal[1] = 0.8660254f;
    in.normal[2] = 0.5f;
    LCHECK(v9x_r3d_tnl_ref_vertex(&key, &c, &in, &out) == V9X_STATUS_OK);
    LCHECK(light_near(out.color[0][1], 0.44f));
}

unsigned int v9x_run_gl_light_tests(void)
{
    gl_light_failures = 0u;
    test_initial();
    test_light_transform();
    test_light_limits();
    test_material_faces();
    test_key();
    test_normal_matrix();
    test_default_lit();
    return gl_light_failures;
}
