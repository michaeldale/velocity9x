/*
 * Tests for OpenGL texture coordinate generation (src\opengl\gl_texgen.c):
 * table 6.14's initial values, glTexGen's errors, the three modes worked
 * by hand from 2.10.4, an eye plane taken through the model-view at the
 * call, and the enable mask.
 */
#include <stdio.h>

#include "../../src/opengl/gl_texgen.h"

static unsigned int gl_texgen_failures;

#define TGCHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++gl_texgen_failures; \
    } \
} while (0)

#define TG_MODELVIEW 0x1700u

static V9X_GL_STATE s;
static V9X_GL_TEXGEN g;

static int tg_near(float a, float b)
{
    float d = a - b;

    return d < 1.0e-5f && d > -1.0e-5f;
}

static void fresh(void)
{
    v9x_gl_state_init(&s);
    v9x_gl_texgen_init(&g);
}

static const V9X_GL_MATRIX *modelview(void)
{
    return v9x_gl_matrix_top(&s.matrices, TG_MODELVIEW);
}

static void set_mode(GLenum coord, GLenum mode)
{
    GLfloat value = (GLfloat)mode;

    v9x_gl_texgen_set(&s, &g, coord, V9X_GL_TEXTURE_GEN_MODE, &value,
                      modelview());
}

static void set_plane(GLenum coord, GLenum pname, GLfloat a, GLfloat b,
                      GLfloat c, GLfloat d)
{
    GLfloat p[4];

    p[0] = a;
    p[1] = b;
    p[2] = c;
    p[3] = d;
    v9x_gl_texgen_set(&s, &g, coord, pname, p, modelview());
}

static void test_initial(void)
{
    GLfloat out[4];

    fresh();
    TGCHECK(v9x_gl_texgen_get(&s, &g, V9X_GL_S, V9X_GL_TEXTURE_GEN_MODE,
                              out) == 1u);
    TGCHECK(out[0] == (GLfloat)V9X_GL_EYE_LINEAR);
    TGCHECK(v9x_gl_texgen_get(&s, &g, V9X_GL_S, V9X_GL_OBJECT_PLANE,
                              out) == 4u);
    TGCHECK(out[0] == 1.0f && out[1] == 0.0f && out[2] == 0.0f &&
            out[3] == 0.0f);
    TGCHECK(v9x_gl_texgen_get(&s, &g, V9X_GL_T, V9X_GL_EYE_PLANE,
                              out) == 4u);
    TGCHECK(out[0] == 0.0f && out[1] == 1.0f && out[2] == 0.0f &&
            out[3] == 0.0f);
    TGCHECK(v9x_gl_texgen_get(&s, &g, V9X_GL_Q, V9X_GL_OBJECT_PLANE,
                              out) == 4u);
    TGCHECK(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f &&
            out[3] == 0.0f);
    TGCHECK(s.error == V9X_GL_NO_ERROR);
    TGCHECK(v9x_gl_texgen_enabled(&s) == 0ul);
}

static void test_errors(void)
{
    GLfloat out[4];

    fresh();
    set_mode(0x1234u, V9X_GL_OBJECT_LINEAR);
    TGCHECK(s.error == V9X_GL_INVALID_ENUM);
    s.error = V9X_GL_NO_ERROR;
    set_mode(V9X_GL_S, 0x1234u);
    TGCHECK(s.error == V9X_GL_INVALID_ENUM);
    TGCHECK(g.mode[0] == V9X_GL_EYE_LINEAR);
    s.error = V9X_GL_NO_ERROR;
    /* SPHERE_MAP names S and T only. */
    set_mode(V9X_GL_R, V9X_GL_SPHERE_MAP);
    TGCHECK(s.error == V9X_GL_INVALID_ENUM);
    TGCHECK(g.mode[2] == V9X_GL_EYE_LINEAR);
    s.error = V9X_GL_NO_ERROR;
    set_mode(V9X_GL_T, V9X_GL_SPHERE_MAP);
    TGCHECK(s.error == V9X_GL_NO_ERROR);
    TGCHECK(g.mode[1] == V9X_GL_SPHERE_MAP);
    set_plane(V9X_GL_S, 0x1234u, 1.0f, 2.0f, 3.0f, 4.0f);
    TGCHECK(s.error == V9X_GL_INVALID_ENUM);
    s.error = V9X_GL_NO_ERROR;
    TGCHECK(v9x_gl_texgen_get(&s, &g, V9X_GL_S, 0x1234u, out) == 0u);
    TGCHECK(s.error == V9X_GL_INVALID_ENUM);
    s.error = V9X_GL_NO_ERROR;
    s.in_begin = 1;
    set_mode(V9X_GL_S, V9X_GL_OBJECT_LINEAR);
    TGCHECK(s.error == V9X_GL_INVALID_OPERATION);
    TGCHECK(g.mode[0] == V9X_GL_EYE_LINEAR);
}

static void test_object_linear(void)
{
    GLfloat object[4] = { 1.0f, 2.0f, 3.0f, 1.0f };
    GLfloat normal[3] = { 0.0f, 0.0f, 1.0f };
    GLfloat tex[4] = { 7.0f, 8.0f, 9.0f, 1.0f };

    fresh();
    set_mode(V9X_GL_S, V9X_GL_OBJECT_LINEAR);
    set_plane(V9X_GL_S, V9X_GL_OBJECT_PLANE, 1.0f, 2.0f, 3.0f, 4.0f);
    /* The model-view does not enter: object coordinates. */
    v9x_gl_state_translate(&s, 5.0f, 0.0f, 0.0f);
    v9x_gl_texgen_apply(&g, 0x1ul, modelview(), object, normal, 0, tex);
    TGCHECK(tg_near(tex[0], 1.0f + 4.0f + 9.0f + 4.0f));
    /* T, R and Q are not in the mask and keep their values. */
    TGCHECK(tex[1] == 8.0f && tex[2] == 9.0f && tex[3] == 1.0f);
    TGCHECK(s.error == V9X_GL_NO_ERROR);
}

static void test_eye_linear(void)
{
    GLfloat object[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    GLfloat normal[3] = { 0.0f, 0.0f, 1.0f };
    GLfloat tex[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    GLfloat out[4];

    fresh();
    /* Under a translation by (1, 0, 0), the plane x = 0 is stored as
     * p M^-1 = (1, 0, 0, -1). */
    v9x_gl_state_translate(&s, 1.0f, 0.0f, 0.0f);
    set_plane(V9X_GL_S, V9X_GL_EYE_PLANE, 1.0f, 0.0f, 0.0f, 0.0f);
    TGCHECK(v9x_gl_texgen_get(&s, &g, V9X_GL_S, V9X_GL_EYE_PLANE,
                              out) == 4u);
    TGCHECK(tg_near(out[0], 1.0f) && tg_near(out[1], 0.0f) &&
            tg_near(out[2], 0.0f) && tg_near(out[3], -1.0f));
    /* Under the same matrix the plane is the object's: the origin is on
     * it. */
    v9x_gl_texgen_apply(&g, 0x1ul, modelview(), object, normal, 0, tex);
    TGCHECK(tg_near(tex[0], 0.0f));
    /* The model-view changes after the call: the plane stays where it was
     * in eye space, and the origin is now one unit short of it. */
    v9x_gl_state_load_identity(&s);
    v9x_gl_texgen_apply(&g, 0x1ul, modelview(), object, normal, 0, tex);
    TGCHECK(tg_near(tex[0], -1.0f));
    /* T's default eye plane (0, 1, 0, 0) set under the identity. */
    object[1] = 3.0f;
    v9x_gl_texgen_apply(&g, 0x2ul, modelview(), object, normal, 0, tex);
    TGCHECK(tg_near(tex[1], 3.0f));
}

static void test_sphere_map(void)
{
    GLfloat object[4] = { 0.0f, 0.0f, -1.0f, 1.0f };
    GLfloat facing[3] = { 0.0f, 0.0f, 1.0f };
    GLfloat tilted[3] = { 0.0f, 0.70710678f, 0.70710678f };
    GLfloat tex[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    fresh();
    set_mode(V9X_GL_S, V9X_GL_SPHERE_MAP);
    set_mode(V9X_GL_T, V9X_GL_SPHERE_MAP);
    /* Facing the eye: u = (0, 0, -1), r = (0, 0, 1), m = 4. */
    v9x_gl_texgen_apply(&g, 0x3ul, modelview(), object, facing, 0, tex);
    TGCHECK(tg_near(tex[0], 0.5f) && tg_near(tex[1], 0.5f));
    /* Tilted 45 degrees up: r = (0, 1, 0), m = 2 sqrt 2,
     * t = 1 / (2 sqrt 2) + 1/2. */
    v9x_gl_texgen_apply(&g, 0x3ul, modelview(), object, tilted, 0, tex);
    TGCHECK(tg_near(tex[0], 0.5f));
    TGCHECK(tg_near(tex[1], 0.85355339f));
}

/* Off the axis, the length of n' matters (with u = (0, 0, -1) it cancels:
 * t depends on n's direction alone). Eye (0.5, 0, -1), n (0, 0.6, 0.8);
 * the expected values worked in double precision from 2.10.4's formulas. */
static void test_sphere_map_scaled(void)
{
    GLfloat object[4] = { 0.5f, 0.0f, -1.0f, 1.0f };
    GLfloat normal[3] = { 0.0f, 0.6f, 0.8f };
    GLfloat tex[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    fresh();
    set_mode(V9X_GL_S, V9X_GL_SPHERE_MAP);
    set_mode(V9X_GL_T, V9X_GL_SPHERE_MAP);
    v9x_gl_texgen_apply(&g, 0x3ul, modelview(), object, normal, 0, tex);
    TGCHECK(tg_near(tex[0], 0.64139649f));
    TGCHECK(tg_near(tex[1], 0.77148127f));
    /* Scaled by 2, the same eye position from half the object one: n'
     * through the inverse transpose is half length. Without NORMALIZE it
     * stays so (2.10.3) and the coordinates move; with it they return. */
    v9x_gl_state_scale(&s, 2.0f, 2.0f, 2.0f);
    object[0] = 0.25f;
    object[2] = -0.5f;
    v9x_gl_texgen_apply(&g, 0x3ul, modelview(), object, normal, 0, tex);
    TGCHECK(tg_near(tex[0], 0.85373959f));
    TGCHECK(tg_near(tex[1], 0.66979500f));
    v9x_gl_texgen_apply(&g, 0x3ul, modelview(), object, normal, 1, tex);
    TGCHECK(tg_near(tex[0], 0.64139649f));
    TGCHECK(tg_near(tex[1], 0.77148127f));
}

static void test_enabled(void)
{
    fresh();
    v9x_gl_state_enable(&s, V9X_GL_TEXTURE_GEN_S, 1);
    v9x_gl_state_enable(&s, V9X_GL_TEXTURE_GEN_Q, 1);
    TGCHECK(v9x_gl_texgen_enabled(&s) == 0x9ul);
    v9x_gl_state_enable(&s, V9X_GL_TEXTURE_GEN_S, 0);
    TGCHECK(v9x_gl_texgen_enabled(&s) == 0x8ul);
    TGCHECK(s.error == V9X_GL_NO_ERROR);
}

unsigned int v9x_run_gl_texgen_tests(void)
{
    gl_texgen_failures = 0u;
    test_initial();
    test_errors();
    test_object_linear();
    test_eye_linear();
    test_sphere_map();
    test_sphere_map_scaled();
    test_enabled();
    if (gl_texgen_failures == 0u) {
        printf("gl_texgen: all tests passed\n");
    }
    return gl_texgen_failures;
}
