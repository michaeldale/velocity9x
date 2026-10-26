/*
 * Tests for the Haswell vertex kernel generator (src\chipsets\intel\hsw_vs.c).
 *
 * Each kernel is run in the host EU model (hsw_eu_sim.c) over pairs of
 * vertices, as the hardware dispatches SIMD4x2, and what it writes to the
 * URB is compared with the reference evaluator (r3d_tnl_ref.c) for the same
 * key, constants and inputs. Agreement shows the program computes the
 * reference's equations with the registers and constants it names; it does
 * not show the EU's precision, nor that the reading of the ISA the model
 * encodes is the hardware's. The step 4 gate on the 4570 is what does.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "velocity9x/intel_hsw_vs.h"
#include "../../src/display32/r3d/r3d_tnl_ref.h"
#include "hsw_eu_sim.h"

static unsigned int vs_failures = 0u;

#define VCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++vs_failures; \
    } \
} while (0)

#define VS_WORDS 4096u
#define VS_PAIRS 12u

static v9x_u32 vs_words[VS_WORDS];
static unsigned long vs_seed = 12345ul;

/* A small LCG: the same sequence every run. */
static float vs_random(float low, float high)
{
    vs_seed = vs_seed * 1103515245ul + 12345ul;
    return low + (high - low) *
                     (float)((vs_seed >> 8) & 0xffffu) / 65535.0f;
}

static void vs_set4(float *out, float x, float y, float z, float w)
{
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = w;
}

static int vs_near(float actual, float expected)
{
    float difference = actual - expected;
    float scale = expected < 0.0f ? -expected : expected;

    if (difference < 0.0f) {
        difference = -difference;
    }
    if (scale < 1.0f) {
        scale = 1.0f;
    }
    return difference <= 2.0e-4f * scale;
}

/* A rotation about an oblique axis, then a translation away from the eye;
 * its upper 3x3 is orthonormal, so it is its own normal matrix. */
static void vs_constants(V9X_R3D_TNL_CONSTANTS *c, v9x_u32 model)
{
    static const float mv[4][4] = {
        { 0.8f,  0.0f, -0.6f,  0.2f },
        { 0.36f, 0.8f,  0.48f, -0.1f },
        { 0.48f, -0.6f, 0.64f, -6.0f },
        { 0.0f,  0.0f,  0.0f,  1.0f }
    };
    static const float projection[4][4] = {
        { 1.5f, 0.0f, 0.1f, 0.0f },
        { 0.0f, 2.0f, -0.2f, 0.0f },
        { 0.0f, 0.0f, -1.02f, -0.202f },
        { 0.0f, 0.0f, -1.0f, 0.0f }
    };
    unsigned int row;
    unsigned int col;
    unsigned int k;
    unsigned int i;

    memset(c, 0, sizeof(*c));
    for (row = 0u; row < 4u; ++row) {
        for (col = 0u; col < 4u; ++col) {
            float sum = 0.0f;

            for (k = 0u; k < 4u; ++k) {
                sum += projection[row][k] * mv[k][col];
            }
            c->model_view_projection[row][col] = sum;
            c->model_view[row][col] = mv[row][col];
        }
    }
    for (row = 0u; row < 3u; ++row) {
        for (col = 0u; col < 3u; ++col) {
            c->normal[row][col] = mv[row][col];
        }
    }
    for (i = 0u; i < V9X_R3D_TNL_MAX_TEXTURES; ++i) {
        for (row = 0u; row < 4u; ++row) {
            for (col = 0u; col < 4u; ++col) {
                c->texture[i][row][col] = (row == col ? 1.0f : 0.0f) +
                                          0.1f * (float)(row + 2u * col + i);
            }
        }
    }
    vs_set4(c->scene_ambient, 0.1f, 0.15f, 0.2f, 1.0f);
    vs_set4(c->viewer, 0.0f, 0.0f, 1.0f, 0.0f);
    for (i = 0u; i < 2u; ++i) {
        V9X_R3D_TNL_MATERIAL *m = &c->material[i];

        vs_set4(m->ambient, 0.5f, 0.4f + 0.1f * (float)i, 0.3f, 1.0f);
        vs_set4(m->diffuse, 0.7f, 0.6f, 0.5f - 0.2f * (float)i, 0.8f);
        vs_set4(m->specular, 0.9f, 0.8f, 0.7f, 1.0f);
        vs_set4(m->emissive, 0.05f, 0.02f * (float)i, 0.0f, 0.0f);
        vs_set4(m->power, 12.0f + 8.0f * (float)i, 0.0f, 0.0f, 0.0f);
    }
    for (i = 0u; i < V9X_R3D_TNL_MAX_LIGHTS; ++i) {
        V9X_R3D_TNL_LIGHT *l = &c->light[i];
        float fi = (float)i;

        /* Positional lights sit around the object, 6 units in front of
         * the eye; a directional light's position is its unit vector. */
        vs_set4(l->position, -2.0f + 0.6f * fi, 1.5f - 0.3f * fi,
                -3.0f - 0.4f * fi, 1.0f);
        if ((i % 4u) == 3u) {
            vs_set4(l->position, 0.48f, 0.6f, 0.64f, 0.0f);
        }
        vs_set4(l->direction, 0.36f, -0.48f, -0.8f, 0.0f);
        vs_set4(l->ambient, 0.05f + 0.01f * fi, 0.04f, 0.03f, 1.0f);
        vs_set4(l->diffuse, 0.6f, 0.5f + 0.02f * fi, 0.4f, 1.0f);
        vs_set4(l->specular, 0.3f, 0.3f, 0.35f + 0.01f * fi, 1.0f);
        /* Light 2's range is short, so some vertices fall outside it. */
        vs_set4(l->attenuation, 0.6f, 0.08f, 0.01f,
                i == 2u ? 4.5f : 3.0e38f);
        if (model == V9X_R3D_TNL_MODEL_GL11) {
            vs_set4(l->spot, 0.5f, 3.0f, 0.0f, 0.0f);
        } else {
            vs_set4(l->spot, 0.9f, 0.6f, 1.5f, 1.0f / (0.9f - 0.6f));
        }
    }
    vs_set4(c->fog, 2.0f, 9.0f, 0.15f, 1.0f / (9.0f - 2.0f));
    vs_set4(c->default_color[0], 0.9f, 0.8f, 0.7f, 0.6f);
    vs_set4(c->default_color[1], 0.3f, 0.6f, 0.9f, 0.5f);
}

static void vs_input(V9X_R3D_TNL_INPUT *in)
{
    vs_set4(in->position, vs_random(-2.0f, 2.0f), vs_random(-2.0f, 2.0f),
            vs_random(-2.0f, 2.0f), 1.0f);
    vs_set4(in->normal, vs_random(-1.5f, 1.5f), vs_random(-1.5f, 1.5f),
            vs_random(-1.5f, 1.5f), 0.0f);
    vs_set4(in->color[0], vs_random(0.0f, 1.0f), vs_random(0.0f, 1.0f),
            vs_random(0.0f, 1.0f), vs_random(0.0f, 1.0f));
    vs_set4(in->color[1], vs_random(0.0f, 1.0f), vs_random(0.0f, 1.0f),
            vs_random(0.0f, 1.0f), vs_random(0.0f, 1.0f));
    /* The VF stores 0 and 1 after a two-float coordinate. */
    vs_set4(in->texture[0], vs_random(-1.0f, 2.0f), vs_random(-1.0f, 2.0f),
            0.0f, 1.0f);
    vs_set4(in->texture[1], vs_random(-1.0f, 2.0f), vs_random(-1.0f, 2.0f),
            0.0f, 1.0f);
}

/* Vertex v's vec4 into the half of a register SIMD4x2 gives it. */
static void vs_put(struct v9x_hsw_sim *sim, unsigned int nr, unsigned int v,
                   const float *value)
{
    unsigned int k;

    for (k = 0u; k < 4u; ++k) {
        sim->grf[nr][v * 4u + k] = v9x_hsw_sim_bits(value[k]);
    }
}

static int vs_match(const struct v9x_hsw_sim *sim, unsigned int reg,
                    unsigned int v, const float *expected, unsigned int count)
{
    unsigned int k;

    for (k = 0u; k < count; ++k) {
        float actual = v9x_hsw_sim_float(sim->message[reg][v * 4u + k]);

        if (!vs_near(actual, expected[k])) {
            printf("  message %u vertex %u component %u: %f, expected %f\n",
                   reg, v, k, (double)actual, (double)expected[k]);
            return 0;
        }
    }
    return 1;
}

/*
 * Run a key over VS_PAIRS pairs of vertices and compare each with the
 * reference. Returns the program for further checks.
 */
static void vs_run(const char *name, const V9X_R3D_TNL_KEY *key,
                   struct v9x_hsw_vs_program *program)
{
    static struct v9x_hsw_sim sim;
    V9X_R3D_TNL_CONSTANTS constants;
    V9X_R3D_TNL_INPUT in[2];
    V9X_R3D_TNL_OUTPUT expected[2];
    v9x_u32 packed[V9X_R3D_TNL_VEC4S * 4u];
    unsigned int pair;
    unsigned int v;
    unsigned int g;
    unsigned int unit;
    unsigned int failures_before = vs_failures;
    int two_sided = (key->flags & V9X_R3D_TNL_TWO_SIDED) != 0ul;
    unsigned int c0 = 3u;
    unsigned int c1 = two_sided ? 5u : 4u;
    unsigned int t0 = two_sided ? 7u : 5u;

    memset(program, 0, sizeof(*program));
    VCHECK(v9x_intel_hsw_vs_kernel_build(key, vs_words, VS_WORDS, program) ==
           V9X_STATUS_OK);
    if (program->dwords == 0ul) {
        printf("  %s: no kernel\n", name);
        return;
    }
    vs_constants(&constants, key->model);
    v9x_intel_hsw_vs_constants(program, &constants, packed);

    for (pair = 0u; pair < VS_PAIRS; ++pair) {
        memset(&sim, 0, sizeof(sim));
        /* g0: two URB handles and a dword 5 the header ORs into. */
        sim.grf[0][0] = 0x11u;
        sim.grf[0][1] = 0x22u;
        sim.grf[0][5] = 0x5u;
        for (g = 0u; g < (unsigned int)program->constant_grfs * 8u; ++g) {
            sim.grf[1u + g / 8u][g % 8u] = packed[g];
        }
        for (v = 0u; v < 2u; ++v) {
            unsigned int base = 1u + program->constant_grfs;

            vs_input(&in[v]);
            vs_put(&sim, base + 0u, v, in[v].position);
            vs_put(&sim, base + 1u, v, in[v].color[0]);
            vs_put(&sim, base + 2u, v, in[v].color[1]);
            vs_put(&sim, base + 3u, v, in[v].texture[0]);
            vs_put(&sim, base + 4u, v, in[v].texture[1]);
            if (program->inputs == V9X_HSW_VSK_INPUTS_NORMAL) {
                vs_put(&sim, base + 5u, v, in[v].normal);
            }
            VCHECK(v9x_r3d_tnl_ref_vertex(key, &constants, &in[v],
                                          &expected[v]) == V9X_STATUS_OK);
        }
        VCHECK(v9x_hsw_sim_run(&sim, vs_words, program->dwords) == 1);
        VCHECK(sim.message_length == 1u + program->outputs);
        VCHECK(sim.message[0][0] == 0x11u && sim.message[0][1] == 0x22u &&
               sim.message[0][5] == 0xff05u);
        for (v = 0u; v < 2u; ++v) {
            float c1_expected[4];
            const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

            VCHECK(vs_match(&sim, 1u, v, zero, 4u));
            VCHECK(vs_match(&sim, 2u, v, expected[v].clip, 4u));
            VCHECK(vs_match(&sim, c0, v, expected[v].color[0], 4u));
            memcpy(c1_expected, expected[v].color[1], sizeof(c1_expected));
            if (key->fog_mode != V9X_R3D_TNL_FOG_NONE) {
                c1_expected[3] = expected[v].fog;
            }
            VCHECK(vs_match(&sim, c1, v, c1_expected, 4u));
            if (two_sided) {
                VCHECK(vs_match(&sim, 4u, v, expected[v].back_color[0], 4u));
                memcpy(c1_expected, expected[v].back_color[1],
                       sizeof(c1_expected));
                if (key->fog_mode != V9X_R3D_TNL_FOG_NONE) {
                    c1_expected[3] = expected[v].fog;
                }
                VCHECK(vs_match(&sim, 6u, v, c1_expected, 4u));
            }
            for (unit = 0u; unit < key->texture_count; ++unit) {
                VCHECK(vs_match(&sim, t0 + unit, v, expected[v].texture[unit],
                                4u));
            }
        }
        if (vs_failures != failures_before) {
            printf("  %s: pair %u\n", name, pair);
            return;
        }
    }
}

static void vs_key(V9X_R3D_TNL_KEY *key, v9x_u32 model)
{
    memset(key, 0, sizeof(*key));
    key->model = model;
    key->inputs = V9X_R3D_TNL_IN_COLOR0 | V9X_R3D_TNL_IN_COLOR1;
    key->texture_count = 2u;
}

/* The block's vec4 indices agree with the structure, at compile time. */
typedef char vs_assert_vec4_0[
    (offsetof(V9X_R3D_TNL_CONSTANTS, model_view_projection) ==
           V9X_R3D_TNL_VEC4_MVP * 16u) ? 1 : -1];
typedef char vs_assert_vec4_1[
    (offsetof(V9X_R3D_TNL_CONSTANTS, model_view) ==
           V9X_R3D_TNL_VEC4_MODEL_VIEW * 16u) ? 1 : -1];
typedef char vs_assert_vec4_2[
    (offsetof(V9X_R3D_TNL_CONSTANTS, normal) ==
           V9X_R3D_TNL_VEC4_NORMAL * 16u) ? 1 : -1];
typedef char vs_assert_vec4_3[
    (offsetof(V9X_R3D_TNL_CONSTANTS, texture[1]) ==
           V9X_R3D_TNL_VEC4_TEXTURE(1u) * 16u) ? 1 : -1];
typedef char vs_assert_vec4_4[
    (offsetof(V9X_R3D_TNL_CONSTANTS, scene_ambient) ==
           V9X_R3D_TNL_VEC4_SCENE_AMBIENT * 16u) ? 1 : -1];
typedef char vs_assert_vec4_5[
    (offsetof(V9X_R3D_TNL_CONSTANTS, viewer) ==
           V9X_R3D_TNL_VEC4_VIEWER * 16u) ? 1 : -1];
typedef char vs_assert_vec4_6[
    (offsetof(V9X_R3D_TNL_CONSTANTS, material[1].power) ==
           (V9X_R3D_TNL_VEC4_MATERIAL(1u) + V9X_R3D_TNL_MF_POWER) *
               16u) ? 1 : -1];
typedef char vs_assert_vec4_7[
    (offsetof(V9X_R3D_TNL_CONSTANTS, light[7].spot) ==
           (V9X_R3D_TNL_VEC4_LIGHT(7u) + V9X_R3D_TNL_LF_SPOT) * 16u) ? 1 : -1];
typedef char vs_assert_vec4_8[
    (offsetof(V9X_R3D_TNL_CONSTANTS, fog) ==
           V9X_R3D_TNL_VEC4_FOG * 16u) ? 1 : -1];
typedef char vs_assert_vec4_9[
    (offsetof(V9X_R3D_TNL_CONSTANTS, default_color[1]) ==
           V9X_R3D_TNL_VEC4_DEFAULT(1u) * 16u) ? 1 : -1];

/* Step 3's case: unlit, both colours. Only the four MVP rows are pushed,
 * two GRFs, as the position-only kernel reads. */
static void test_unlit(void)
{
    V9X_R3D_TNL_KEY key;
    struct v9x_hsw_vs_program program;

    vs_key(&key, V9X_R3D_TNL_MODEL_GL11);
    vs_run("unlit", &key, &program);
    VCHECK(program.constant_grfs == 2u);
    VCHECK(program.constant_count == 4u);
    VCHECK(program.constant_map[0] == 0u && program.constant_map[3] == 3u);
    VCHECK(program.inputs == V9X_HSW_VSK_INPUTS);
    VCHECK(program.outputs == V9X_HSW_VSK_OUTPUTS);

    /* No colours: the defaults. */
    key.inputs = 0ul;
    vs_run("unlit defaults", &key, &program);
    VCHECK(program.constant_count == 6u);
}

static void test_fog_and_texture(void)
{
    V9X_R3D_TNL_KEY key;
    struct v9x_hsw_vs_program program;
    v9x_u32 mode;

    for (mode = V9X_R3D_TNL_FOG_LINEAR; mode <= V9X_R3D_TNL_FOG_EXP2;
         ++mode) {
        vs_key(&key, V9X_R3D_TNL_MODEL_D3D7);
        key.fog_mode = mode;
        vs_run("fog depth", &key, &program);
        key.fog_distance = V9X_R3D_TNL_FOG_RANGE;
        vs_run("fog range", &key, &program);
    }
    vs_key(&key, V9X_R3D_TNL_MODEL_GL11);
    key.texture_matrix = 3ul;
    vs_run("texture matrices", &key, &program);
    key.texture_count = 1u;
    key.texture_matrix = 1ul;
    vs_run("texture matrix 0", &key, &program);
}

/* Direct3D 7: every light type, the material sources, specular apart. */
static void test_d3d_lights(void)
{
    V9X_R3D_TNL_KEY key;
    struct v9x_hsw_vs_program program;

    vs_key(&key, V9X_R3D_TNL_MODEL_D3D7);
    key.flags = V9X_R3D_TNL_LIGHTING;
    key.inputs |= V9X_R3D_TNL_IN_NORMAL;
    key.light_count = 1ul;
    key.light_types = V9X_R3D_TNL_LIGHT_DIRECTIONAL;
    vs_run("d3d directional", &key, &program);
    VCHECK(program.inputs == V9X_HSW_VSK_INPUTS_NORMAL);

    key.flags |= V9X_R3D_TNL_SPECULAR;
    key.light_count = 4ul;
    key.light_types = V9X_R3D_TNL_LIGHT_POINT |
                      (V9X_R3D_TNL_LIGHT_SPOT << 2) |
                      (V9X_R3D_TNL_LIGHT_POINT << 4) |
                      (V9X_R3D_TNL_LIGHT_DIRECTIONAL << 6);
    vs_run("d3d four lights", &key, &program);

    key.flags |= V9X_R3D_TNL_LOCAL_VIEWER | V9X_R3D_TNL_NORMALIZE;
    vs_run("d3d local viewer, normalize", &key, &program);

    /* Diffuse and ambient from colour 0, specular from colour 1, emissive
     * from colour 1 too. */
    key.material_sources = V9X_R3D_TNL_SOURCE_COLOR0 |
                           (V9X_R3D_TNL_SOURCE_COLOR1 << 2) |
                           (V9X_R3D_TNL_SOURCE_COLOR0 << 4) |
                           (V9X_R3D_TNL_SOURCE_COLOR1 << 6);
    vs_run("d3d material sources", &key, &program);

    /* The same sources with no vertex colours: the defaults. */
    key.inputs = V9X_R3D_TNL_IN_NORMAL;
    key.fog_mode = V9X_R3D_TNL_FOG_EXP;
    key.fog_distance = V9X_R3D_TNL_FOG_RANGE;
    vs_run("d3d sources from defaults, fog", &key, &program);

    /* No normals: ambient and emissive only. */
    key.inputs = V9X_R3D_TNL_IN_COLOR0 | V9X_R3D_TNL_IN_COLOR1;
    vs_run("d3d no normals", &key, &program);
}

/* OpenGL 1.1: cutoff spots, specular in the primary, two-sided. */
static void test_gl_lights(void)
{
    V9X_R3D_TNL_KEY key;
    struct v9x_hsw_vs_program program;

    vs_key(&key, V9X_R3D_TNL_MODEL_GL11);
    key.flags = V9X_R3D_TNL_LIGHTING | V9X_R3D_TNL_SPECULAR;
    key.inputs |= V9X_R3D_TNL_IN_NORMAL;
    key.light_count = 3ul;
    key.light_types = V9X_R3D_TNL_LIGHT_SPOT |
                      (V9X_R3D_TNL_LIGHT_POINT << 2) |
                      (V9X_R3D_TNL_LIGHT_SPOT << 4);
    vs_run("gl spots", &key, &program);

    key.flags |= V9X_R3D_TNL_TWO_SIDED | V9X_R3D_TNL_NORMALIZE;
    vs_run("gl two-sided", &key, &program);
    VCHECK(program.outputs == V9X_HSW_VSK_OUTPUTS_TWO_SIDED);

    /* GL_COLOR_MATERIAL on ambient and diffuse. */
    key.material_sources = V9X_R3D_TNL_SOURCE_COLOR0 |
                           (V9X_R3D_TNL_SOURCE_COLOR0 << 4);
    key.fog_mode = V9X_R3D_TNL_FOG_LINEAR;
    vs_run("gl colour material, fog", &key, &program);
}

/* The largest key: eight lights, every flag OpenGL allows, fog and both
 * texture matrices. It must build and fit the whole block. */
static void test_largest(void)
{
    V9X_R3D_TNL_KEY key;
    struct v9x_hsw_vs_program program;
    struct v9x_hsw_vs_program measured;
    unsigned int i;

    vs_key(&key, V9X_R3D_TNL_MODEL_GL11);
    key.flags = V9X_R3D_TNL_FLAGS_ALL;
    key.inputs = V9X_R3D_TNL_IN_ALL;
    key.texture_matrix = 3ul;
    key.light_count = V9X_R3D_TNL_MAX_LIGHTS;
    for (i = 0u; i < V9X_R3D_TNL_MAX_LIGHTS; ++i) {
        key.light_types |= (1ul + i % 3ul) << (2u * i);
    }
    key.fog_mode = V9X_R3D_TNL_FOG_EXP2;
    vs_run("gl largest", &key, &program);
    VCHECK(program.constant_grfs <= V9X_HSW_VSK_CONSTANT_GRFS_MAX);
    printf("  largest vertex kernel: %lu bytes, %u constant GRFs\n",
           (unsigned long)program.dwords * 4ul,
           (unsigned int)program.constant_grfs);

    /* Measuring writes nothing and agrees with the build. */
    VCHECK(v9x_intel_hsw_vs_kernel_build(&key, 0, 0ul, &measured) ==
           V9X_STATUS_OK);
    VCHECK(measured.dwords == program.dwords);
    VCHECK(measured.constant_grfs == program.constant_grfs);

    /* Too little room, and a malformed key. */
    VCHECK(v9x_intel_hsw_vs_kernel_build(&key, vs_words, 64ul, &measured) ==
           V9X_STATUS_INSUFFICIENT_MEMORY);
    key.reserved[0] = 1ul;
    VCHECK(v9x_intel_hsw_vs_kernel_build(&key, vs_words, VS_WORDS,
                                         &measured) ==
           V9X_STATUS_INVALID_ARGUMENT);
}

unsigned int v9x_run_intel_hsw_vs_tests(void)
{
    vs_failures = 0u;
    test_unlit();
    test_fog_and_texture();
    test_d3d_lights();
    test_gl_lights();
    test_largest();
    return vs_failures;
}
