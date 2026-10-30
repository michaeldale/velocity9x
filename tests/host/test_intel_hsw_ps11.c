/*
 * The Gen7.5 pixel shader 1.x kernels (src\chipsets\intel\hsw_ps11.c),
 * each run in the host EU model (hsw_eu_sim.c) on eight pixels and held to
 * the reference interpreter (src\display32\d3d\d3d_ps11_ref.c): r0 within
 * a float tolerance, and the same pixels killed.
 *
 * The inputs are linear planes the kernel interpolates itself, push
 * constants in the layout intel_hsw_ps11.h gives, and one texture lookup
 * both sides share: a smooth function of the coordinates, so a mismatch
 * is the program's logic, not a sampler's. The shaders are written for
 * these tests, after the instruction patterns 3DMark 2001 SE's use
 * (docs\decisions\2026-10-29-haswell-ddi8-step3-what-nature-asks.md).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "hsw_eu_sim.h"
#include "velocity9x/intel_hsw_ps11.h"
#include "velocity9x/ps11.h"

static unsigned int failures;
#define CHECK(e) do { if (!(e)) { ++failures; \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
} } while (0)

/* Token builders, as test_d3d_ps11.c. */
#define VER(major, minor) (0xffff0000ul | ((major) << 8) | (minor))
#define END               0x0000fffful
#define COISSUE           0x40000000ul
#define DST(file, reg, mask, mod, shift) \
    (0x80000000ul | ((v9x_u32)(file) << 28) | ((v9x_u32)(shift) << 24) | \
     ((v9x_u32)(mod) << 20) | ((v9x_u32)(mask) << 16) | (v9x_u32)(reg))
#define SRC(file, reg, swizzle, mod) \
    (0x80000000ul | ((v9x_u32)(file) << 28) | ((v9x_u32)(mod) << 24) | \
     ((v9x_u32)(swizzle) << 16) | (v9x_u32)(reg))
#define R V9X_PS11_FILE_TEMP
#define V V9X_PS11_FILE_INPUT
#define C V9X_PS11_FILE_CONST
#define T V9X_PS11_FILE_TEXTURE
#define XYZW 0xe4u
#define AAAA 0xffu
#define ALL  0xfu
#define RGB  0x7u
#define A    0x8u

#define OP_MOV          1ul
#define OP_ADD          2ul
#define OP_SUB          3ul
#define OP_MAD          4ul
#define OP_MUL          5ul
#define OP_DP3          8ul
#define OP_LRP          18ul
#define OP_CND          80ul
#define OP_DEF          81ul
#define OP_TEXCOORD     64ul
#define OP_TEXKILL      65ul
#define OP_TEX          66ul
#define OP_TEXBEM       67ul
#define OP_TEXBEML      68ul
#define OP_TEXM3X3PAD   73ul
#define OP_TEXM3X3TEX   74ul
#define OP_TEXM3X3SPEC  76ul
#define OP_TEXM3X3VSPEC 77ul

#define CONST_GRF  ((v9x_u8)4u)
#define SETUP_GRF  ((v9x_u8)11u)
#define KERNEL_DWORDS 2048ul
#define TOLERANCE  2.0e-4f

static v9x_u32 words[128];
/* Fog after the shader for the next check_case: off, or on with this
 * colour. */
static v9x_u8 fog_on;
static float fog_rgb[3];
static v9x_u32 used;
static v9x_u32 kernel[KERNEL_DWORDS];

static void put(v9x_u32 word)
{
    words[used++] = word;
}

/* The shared texture: smooth in every coordinate, different per stage
 * and channel, within [0, 1]. */
static void texel(v9x_u32 stage, v9x_u32 count, const float *coords,
                  float rgba[4])
{
    float s = 0.0f;
    v9x_u32 k;

    for (k = 0ul; k < count; ++k) {
        s += coords[k] * (0.31f + 0.17f * (float)k);
    }
    for (k = 0ul; k < 4ul; ++k) {
        rgba[k] = 0.5f + 0.45f * (float)sin((double)(s + 0.7f * (float)k +
                                                     0.3f * (float)stage));
    }
}

static void interpreter_texel(void *user, v9x_u32 stage, v9x_u32 count,
                              const float *coords, float rgba[4])
{
    (void)user;
    texel(stage, count, coords, rgba);
}

static void sim_texel(void *user, v9x_u32 sampler, v9x_u32 bti,
                      v9x_u32 count, const float *coords, float rgba[4])
{
    (void)user;
    /* Stage N is sampler N and binding table entry 1 + N. */
    if (bti != sampler + 1ul) {
        rgba[0] = rgba[1] = rgba[2] = rgba[3] = -99.0f;
        return;
    }
    texel(sampler, count, coords, rgba);
}

/* Plane coefficients for attribute a, component c: p, q, r. */
static void plane(v9x_u32 a, v9x_u32 c, float pqr[3])
{
    pqr[0] = 0.6f - 0.13f * (float)a + 0.07f * (float)c;
    pqr[1] = -0.4f + 0.11f * (float)c - 0.05f * (float)a;
    pqr[2] = 0.15f + 0.09f * (float)((a * 3ul + c) % 7ul) - 0.2f;
}

/* Run program's kernel on eight pixels and the interpreter on each. */
static void check_case(const char *name, const V9X_PS11_ENV *env,
                       v9x_u32 expect_killed_some)
{
    static struct v9x_hsw_sim sim;
    V9X_PS11_PROGRAM program;
    struct v9x_hsw_ps11_desc desc;
    v9x_u32 where;
    v9x_u32 dwords = 0ul;
    v9x_u32 channel;
    v9x_u32 a;
    v9x_u32 c;
    v9x_u32 killed = 0ul;
    int bad = 0;

    if (v9x_ps11_parse(words, used, &program, &where) != V9X_PS11_OK) {
        printf("FAIL %s: does not parse at %lu\n", name,
               (unsigned long)where);
        ++failures;
        return;
    }
    desc.const_grf = CONST_GRF;
    desc.setup_grf = SETUP_GRF;
    desc.cube = env->cube;
    desc.projected = env->projected;
    desc.unbound = env->unbound;
    desc.fog = fog_on;
    for (c = 0ul; c < 3ul; ++c) {
        desc.fog_rgb[c] = v9x_hsw_sim_bits(fog_rgb[c]);
    }
    if (v9x_intel_hsw_ps11_build(kernel, KERNEL_DWORDS, &program, &desc,
                                 &dwords) != V9X_STATUS_OK) {
        printf("FAIL %s: does not build\n", name);
        ++failures;
        return;
    }

    memset(&sim, 0, sizeof(sim));
    sim.sample = sim_texel;
    for (channel = 0ul; channel < 8ul; ++channel) {
        sim.grf[2][channel] = v9x_hsw_sim_bits(0.05f + 0.11f * (float)channel);
        sim.grf[3][channel] = v9x_hsw_sim_bits(0.9f - 0.1f * (float)channel);
    }
    for (a = 0ul; a < V9X_HSW_PS11_ATTRIBUTES; ++a) {
        for (c = 0ul; c < 4ul; ++c) {
            float pqr[3];
            v9x_u32 *reg = sim.grf[SETUP_GRF + 2ul * a + c / 2ul];
            v9x_u32 at = 4ul * (c % 2ul);

            plane(a, c, pqr);
            reg[at + 0ul] = v9x_hsw_sim_bits(pqr[0]);
            reg[at + 1ul] = v9x_hsw_sim_bits(pqr[1]);
            reg[at + 3ul] = v9x_hsw_sim_bits(pqr[2]);
        }
    }
    for (a = 0ul; a < 8ul; ++a) {
        for (c = 0ul; c < 4ul; ++c) {
            float value = env->c[a][c];

            value = value > 1.0f ? 1.0f : (value < -1.0f ? -1.0f : value);
            sim.grf[CONST_GRF + a / 2ul][4ul * (a % 2ul) + c] =
                v9x_hsw_sim_bits(value);
        }
    }
    for (a = 0ul; a < 4ul; ++a) {
        for (c = 0ul; c < 4ul; ++c) {
            sim.grf[CONST_GRF + V9X_HSW_PS11_CONST_BUMP + a / 2ul]
                   [4ul * (a % 2ul) + c] = v9x_hsw_sim_bits(env->bump[a][c]);
        }
        sim.grf[CONST_GRF + V9X_HSW_PS11_CONST_LUMINANCE][2ul * a] =
            v9x_hsw_sim_bits(env->luminance[a][0]);
        sim.grf[CONST_GRF + V9X_HSW_PS11_CONST_LUMINANCE][2ul * a + 1ul] =
            v9x_hsw_sim_bits(env->luminance[a][1]);
    }

    if (!v9x_hsw_sim_run(&sim, kernel, dwords)) {
        printf("FAIL %s: the kernel did not run to its end (error %d, "
               "%lu executed)\n", name, sim.error,
               (unsigned long)sim.executed);
        ++failures;
        return;
    }

    for (channel = 0ul; channel < 8ul; ++channel) {
        V9X_PS11_PIXEL pixel;
        float b1 = v9x_hsw_sim_float(sim.grf[2][channel]);
        float b2 = v9x_hsw_sim_float(sim.grf[3][channel]);
        float out[4];
        int kept;

        for (a = 0ul; a < V9X_HSW_PS11_ATTRIBUTES; ++a) {
            for (c = 0ul; c < 4ul; ++c) {
                float pqr[3];
                float value;

                plane(a, c, pqr);
                value = pqr[0] * b1 + pqr[1] * b2 + pqr[2];
                if (a < V9X_HSW_PS11_ATTR_TEXCOORD) {
                    pixel.v[a][c] = value;
                } else {
                    pixel.tc[a - V9X_HSW_PS11_ATTR_TEXCOORD][c] = value;
                }
            }
        }
        kept = v9x_ps11_run(&program, env, &pixel, interpreter_texel, 0,
                            out);
        if (!kept) {
            ++killed;
        }
        if (kept != (int)((sim.rt_mask >> channel) & 1ul)) {
            printf("FAIL %s: pixel %lu kill differs (interpreter %s)\n",
                   name, (unsigned long)channel, kept ? "kept" : "killed");
            bad = 1;
        }
        if (!kept) {
            continue;
        }
        /* Fog: the colour clamped, then (colour - fog) f + fog with f the
         * specular alpha, as Direct3D fogs a shader's result. */
        if (fog_on) {
            float f = pixel.v[1][3];

            for (c = 0ul; c < 4ul; ++c) {
                out[c] = out[c] < 0.0f ? 0.0f : out[c] > 1.0f ? 1.0f : out[c];
            }
            for (c = 0ul; c < 3ul; ++c) {
                out[c] = (out[c] - fog_rgb[c]) * f + fog_rgb[c];
            }
        }
        for (c = 0ul; c < 4ul; ++c) {
            float got = v9x_hsw_sim_float(sim.message[c][channel]);

            if (fabs((double)(got - out[c])) > (double)TOLERANCE) {
                printf("FAIL %s: pixel %lu channel %lu: kernel %f, "
                       "interpreter %f\n", name, (unsigned long)channel,
                       (unsigned long)c, (double)got, (double)out[c]);
                bad = 1;
            }
        }
    }
    if (expect_killed_some && (killed == 0ul || killed == 8ul)) {
        printf("FAIL %s: the inputs killed %lu of 8; the test needs some "
               "of each\n", name, (unsigned long)killed);
        bad = 1;
    }
    CHECK(sim.message_length == 4ul);
    if (bad) {
        ++failures;
    }
}

static void zero_env(V9X_PS11_ENV *env)
{
    memset(env, 0, sizeof(*env));
}

static void cases(void)
{
    V9X_PS11_ENV env;
    v9x_u32 k;

    /* tex t0; mov r0, t0 */
    zero_env(&env);
    used = 0ul;
    put(VER(1ul, 0ul));
    put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
    put(OP_MOV); put(DST(R, 0, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(END);
    check_case("tex", &env, 0ul);

    /* The same from a cube. */
    env.cube = 1ul;
    check_case("tex cube", &env, 0ul);

    /* tex t0; texkill t1; mov r0, t0: the planes put t1 either side of
     * zero across the eight pixels. */
    zero_env(&env);
    used = 0ul;
    put(VER(1ul, 0ul));
    put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
    put(OP_TEXKILL); put(DST(T, 1, ALL, 0, 0));
    put(OP_MOV); put(DST(R, 0, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(END);
    check_case("texkill", &env, 1ul);

    /* tex t0; texm3x3pad t1, t0; texm3x3pad t2, t0; texm3x3vspec t3, t0
     * (a cube); mov r0.rgb, t3; mov r0.a, v0.a */
    zero_env(&env);
    env.cube = 0x8ul;
    used = 0ul;
    put(VER(1ul, 0ul));
    put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
    put(OP_TEXM3X3PAD); put(DST(T, 1, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(OP_TEXM3X3PAD); put(DST(T, 2, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(OP_TEXM3X3VSPEC); put(DST(T, 3, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(OP_MOV); put(DST(R, 0, RGB, 0, 0)); put(SRC(T, 3, XYZW, 0));
    put(OP_MOV); put(DST(R, 0, A, 0, 0)); put(SRC(V, 0, AAAA, 0));
    put(END);
    check_case("texm3x3vspec", &env, 0ul);

    /* tex t0; texbem t1, t0; tex t2; tex t3; mul r0.rgb, t1, c0;
     * + mul r0.a, t2, t3 */
    zero_env(&env);
    env.c[0][0] = 0.9f; env.c[0][1] = 0.6f; env.c[0][2] = 0.3f;
    env.c[0][3] = 1.0f;
    env.bump[1][0] = 0.25f; env.bump[1][1] = -0.1f;
    env.bump[1][2] = 0.05f; env.bump[1][3] = 0.3f;
    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
    put(OP_TEXBEM); put(DST(T, 1, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(OP_TEX); put(DST(T, 2, ALL, 0, 0));
    put(OP_TEX); put(DST(T, 3, ALL, 0, 0));
    put(OP_MUL); put(DST(R, 0, RGB, 0, 0)); put(SRC(T, 1, XYZW, 0));
    put(SRC(C, 0, XYZW, 0));
    put(OP_MUL | COISSUE); put(DST(R, 0, A, 0, 0)); put(SRC(T, 2, XYZW, 0));
    put(SRC(T, 3, XYZW, 0));
    put(END);
    check_case("texbem co-issued", &env, 0ul);

    /* tex t0; texbeml t1, t0_bx2 on a projected stage; mov r0, t1 */
    env.projected = 0x2ul;
    env.luminance[1][0] = 0.7f;
    env.luminance[1][1] = 0.2f;
    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
    put(OP_TEXBEML); put(DST(T, 1, ALL, 0, 0)); put(SRC(T, 0, XYZW, 4));
    put(OP_MOV); put(DST(R, 0, ALL, 0, 0)); put(SRC(T, 1, XYZW, 0));
    put(END);
    check_case("texbeml projected", &env, 0ul);

    /* texcoord t0; texm3x3pad t1, t0_bx2; texm3x3pad t2, t0_bx2;
     * texm3x3spec t3, t0_bx2, c4 (a cube); mov r0, t3 */
    zero_env(&env);
    env.cube = 0x8ul;
    env.c[4][0] = 0.2f; env.c[4][1] = -0.6f; env.c[4][2] = 0.75f;
    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_TEXCOORD); put(DST(T, 0, ALL, 0, 0));
    put(OP_TEXM3X3PAD); put(DST(T, 1, ALL, 0, 0)); put(SRC(T, 0, XYZW, 4));
    put(OP_TEXM3X3PAD); put(DST(T, 2, ALL, 0, 0)); put(SRC(T, 0, XYZW, 4));
    put(OP_TEXM3X3SPEC); put(DST(T, 3, ALL, 0, 0)); put(SRC(T, 0, XYZW, 4));
    put(SRC(C, 4, XYZW, 0));
    put(OP_MOV); put(DST(R, 0, ALL, 0, 0)); put(SRC(T, 3, XYZW, 0));
    put(END);
    check_case("texm3x3spec", &env, 0ul);

    /* texcoord t0; texm3x3pad t1, t0; texm3x3pad t2, t0; texm3x3tex t3,
     * t0 (a volume, not a cube); mul r0, t3, t0 */
    zero_env(&env);
    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_TEXCOORD); put(DST(T, 0, ALL, 0, 0));
    put(OP_TEXM3X3PAD); put(DST(T, 1, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(OP_TEXM3X3PAD); put(DST(T, 2, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(OP_TEXM3X3TEX); put(DST(T, 3, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(OP_MUL); put(DST(R, 0, ALL, 0, 0)); put(SRC(T, 3, XYZW, 0));
    put(SRC(T, 0, XYZW, 0));
    put(END);
    check_case("texm3x3tex", &env, 0ul);

    /* Every arithmetic instruction and modifier, a def among them. */
    zero_env(&env);
    for (k = 0ul; k < 8ul; ++k) {
        env.c[k][0] = 0.1f * (float)k - 0.3f;
        env.c[k][1] = 1.5f - 0.2f * (float)k;    /* some clamp at 1 */
        env.c[k][2] = -1.25f + 0.25f * (float)k; /* some clamp at -1 */
        env.c[k][3] = 0.45f;
    }
    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_DEF); put(DST(C, 1, ALL, 0, 0));
    put(0x3f400000ul); put(0xbf000000ul); put(0x3e800000ul);
    put(0x3fc00000ul);                       /* 0.75 -0.5 0.25 1.5 */
    put(OP_MOV); put(DST(R, 0, ALL, 0, 0)); put(SRC(V, 0, XYZW, 0));
    put(OP_MAD); put(DST(R, 1, ALL, 1, 1)); put(SRC(C, 1, XYZW, 4));
    put(SRC(V, 1, XYZW, 6)); put(SRC(R, 0, AAAA, 1));
    put(OP_DP3); put(DST(R, 0, RGB, 0, 15)); put(SRC(R, 1, XYZW, 2));
    put(SRC(C, 2, XYZW, 3));
    put(OP_LRP | COISSUE); put(DST(R, 0, A, 0, 0)); put(SRC(V, 0, XYZW, 0));
    put(SRC(R, 1, XYZW, 0)); put(SRC(C, 2, XYZW, 0));
    put(OP_CND); put(DST(R, 1, ALL, 0, 0)); put(SRC(R, 0, AAAA, 0));
    put(SRC(R, 0, XYZW, 0)); put(SRC(V, 1, XYZW, 5));
    put(OP_ADD); put(DST(R, 0, ALL, 0, 0)); put(SRC(R, 1, XYZW, 0));
    put(SRC(C, 3, XYZW, 1));
    put(OP_SUB); put(DST(R, 0, RGB, 0, 0)); put(SRC(R, 0, XYZW, 0));
    put(SRC(C, 1, XYZW, 0));
    put(OP_MUL); put(DST(R, 0, ALL, 0, 2)); put(SRC(R, 0, XYZW, 0));
    put(SRC(V, 0, XYZW, 0));
    put(END);
    check_case("arithmetic", &env, 0ul);

    /* cnd both ways: r0.a from v0.a, which the planes put either side of
     * 0.5. */
    zero_env(&env);
    env.c[0][0] = 0.2f; env.c[0][1] = 0.4f; env.c[0][2] = 0.6f;
    env.c[0][3] = 0.8f;
    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_ADD); put(DST(R, 0, A, 0, 0)); put(SRC(V, 0, AAAA, 0));
    put(SRC(V, 1, AAAA, 0));
    put(OP_CND); put(DST(R, 0, ALL, 0, 0)); put(SRC(R, 0, AAAA, 0));
    put(SRC(C, 0, XYZW, 0)); put(SRC(V, 1, XYZW, 0));
    put(END);
    check_case("cnd", &env, 0ul);

    /* The same fogged: some channels leave [0, 1] and are clamped first. */
    fog_on = 1u;
    fog_rgb[0] = 0.25f; fog_rgb[1] = 0.5f; fog_rgb[2] = 1.0f;
    check_case("cnd fog", &env, 0ul);

    /* Fog with texkill: the kept pixels fogged, the rest still killed. */
    zero_env(&env);
    used = 0ul;
    put(VER(1ul, 0ul));
    put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
    put(OP_TEXKILL); put(DST(T, 1, ALL, 0, 0));
    put(OP_MOV); put(DST(R, 0, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(END);
    check_case("texkill fog", &env, 1ul);
    fog_on = 0u;

    /* A stage with no texture reads (0, 0, 0, 1) on both sides: 3DMark
     * 2001 SE's Advanced Pixel Shader test draws its reflection pass with
     * every stage cleared and tex t0 (the 4570, boot 144). texkill t1 still
     * kills. */
    zero_env(&env);
    env.unbound = 1ul;
    check_case("texkill unbound", &env, 1ul);
    {
        V9X_PS11_PROGRAM program;
        V9X_PS11_PIXEL pixel;
        v9x_u32 where;
        float out[4];

        used = 0ul;
        put(VER(1ul, 0ul));
        put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
        put(OP_MOV); put(DST(R, 0, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
        put(END);
        check_case("tex unbound", &env, 0ul);
        memset(&pixel, 0, sizeof(pixel));
        pixel.tc[0][0] = 0.3f;
        pixel.tc[0][1] = 0.6f;
        CHECK(v9x_ps11_parse(words, used, &program, &where) == V9X_PS11_OK);
        CHECK(v9x_ps11_run(&program, &env, &pixel, interpreter_texel, 0,
                           out) == 1);
        CHECK(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f &&
              out[3] == 1.0f);
    }
}

static void refusals(void)
{
    V9X_PS11_PROGRAM program;
    struct v9x_hsw_ps11_desc desc;
    v9x_u32 where;
    v9x_u32 dwords;

    used = 0ul;
    put(VER(1ul, 0ul));
    put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
    put(OP_MOV); put(DST(R, 0, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(END);
    CHECK(v9x_ps11_parse(words, used, &program, &where) == V9X_PS11_OK);
    desc.const_grf = CONST_GRF;
    desc.setup_grf = SETUP_GRF;
    desc.cube = 0ul;
    desc.projected = 0ul;
    desc.unbound = 0ul;
    desc.fog = 0u;
    /* Too little room, and registers that would overlap the kernel's. */
    CHECK(v9x_intel_hsw_ps11_build(kernel, 8ul, &program, &desc, &dwords) ==
          V9X_STATUS_INVALID_ARGUMENT);
    desc.setup_grf = 40u;
    CHECK(v9x_intel_hsw_ps11_build(kernel, KERNEL_DWORDS, &program, &desc,
                                   &dwords) == V9X_STATUS_INVALID_ARGUMENT);
}

/* The push block as the kernels read it (intel_hsw_ps11.h), packed from an
 * environment: c_k clamped in register k / 2, half k % 2; stage s's bump
 * matrix in register 4 + s / 2, half s % 2; its luminance terms in
 * register 6, dwords 2s and 2s + 1; the rest zero. */
static void constants(void)
{
    V9X_PS11_ENV env;
    v9x_u32 out[V9X_HSW_PS11_CONST_DWORDS];
    v9x_u32 k;

    zero_env(&env);
    for (k = 0ul; k < 8ul; ++k) {
        env.c[k][0] = 0.1f * (float)k;
        env.c[k][3] = -0.2f * (float)k;
    }
    env.c[7][1] = 2.0f;
    env.c[6][2] = -3.0f;
    env.bump[3][1] = 0.5f;
    env.bump[0][3] = -0.25f;
    env.luminance[2][0] = 0.75f;
    env.luminance[2][1] = 0.125f;
    for (k = 0ul; k < V9X_HSW_PS11_CONST_DWORDS; ++k) {
        out[k] = 0xdeadbeeful;
    }
    v9x_intel_hsw_ps11_constants(&env, out);
    CHECK(out[0] == v9x_hsw_sim_bits(0.0f));
    CHECK(out[4] == v9x_hsw_sim_bits(0.1f));
    CHECK(out[3 * 8 + 4 + 0] == v9x_hsw_sim_bits(0.7f));
    CHECK(out[3 * 8 + 4 + 3] == v9x_hsw_sim_bits(-1.0f));    /* -1.4 */
    CHECK(out[3 * 8 + 4 + 1] == v9x_hsw_sim_bits(1.0f));     /* 2.0 */
    CHECK(out[3 * 8 + 0 + 2] == v9x_hsw_sim_bits(-1.0f));    /* -3.0 */
    CHECK(out[(4 + 1) * 8 + 4 + 1] == v9x_hsw_sim_bits(0.5f));
    CHECK(out[4 * 8 + 0 + 3] == v9x_hsw_sim_bits(-0.25f));
    CHECK(out[6 * 8 + 4] == v9x_hsw_sim_bits(0.75f) &&
          out[6 * 8 + 5] == v9x_hsw_sim_bits(0.125f));
    CHECK(out[6 * 8 + 0] == 0ul && out[6 * 8 + 7] == 0ul);
}

unsigned int v9x_run_intel_hsw_ps11_tests(void)
{
    failures = 0u;
    cases();
    refusals();
    constants();
    return failures;
}
