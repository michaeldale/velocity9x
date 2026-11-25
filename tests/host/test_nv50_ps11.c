/*
 * The Tesla pixel shader 1.x programs (src\chipsets\nvidia\gt218\
 * nv50_ps11.c), each run in a host model of the instructions the encoder
 * emits and held to the reference interpreter (src\display32\d3d\
 * d3d_ps11_ref.c): r0 within a float tolerance, and the same pixels
 * killed. Step 2 of docs\plans\nvidia-gt218-ps11.md.
 *
 * The model decodes the words themselves, by the field positions
 * test_nv50_shader.c holds to Mesa and envydis, and executes them one
 * fragment at a time: a mistranslation or a word that encodes something
 * other than meant shows here as a wrong colour or a model stop. What it
 * cannot show is the hardware disagreeing with Mesa; that is step 3's
 * probe.
 *
 * The shaders are test_i9xx_ps11.c's (after test_intel_hsw_ps11.c's), so
 * the three translators answer to the same cases, with the same pixel
 * planes and the same smooth texture function.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "velocity9x/nv50_ps11.h"
#include "velocity9x/nv50_shader.h"
#include "velocity9x/ps11.h"
#include "nv50_sh_sim.h"

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

#define PROGRAM_DWORDS 2048ul
#define TOLERANCE  2.0e-4f
#define PIXELS     8ul
#define ATTRIBUTES 6ul       /* v0, v1, then coordinate sets 0 to 3 */
/* The weight the model's varyings carry: v[0] holds it, the rest the
 * attribute times it, so 1 / v[0] times v[n] is the attribute exactly
 * (a power of two). */
#define WEIGHT     0.5f

static v9x_u32 words[128];
static v9x_u32 used;
static v9x_u32 program_words[PROGRAM_DWORDS];
/* Programs run through the model, and the largest one's size. */
static v9x_u32 checked;
static v9x_u32 largest;
/* Fog after the shader for the next cases, and its colour. */
static v9x_u32 fog_on;
static float fog_rgb[3];

static void put(v9x_u32 word)
{
    words[used++] = word;
}

static float as_float(v9x_u32 bits)
{
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

/* The shared texture: test_intel_hsw_ps11.c's. */
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

/* ---- The comparison ----------------------------------------------- */

/* The model's sampler: 2D reads u and v; 3D three coordinates; a cube
 * three, taken as they come. The GT218's cube fetch does not divide by the
 * largest magnitude: it maps a face coordinate c to (c + 1) / 2 as given,
 * so the program must project the direction first. Measured on W9QI0DAD
 * (V9XD32 T19 Cube, 2026-11-25): with the direction passed unscaled, 1,049
 * of 1,053 pixels match (c + 1) / 2 and 67 the divided form
 * (docs\decisions\2026-11-25-gt218-cube-maps.md). This model divided until
 * then, which is how the unscaled translator passed it. */
static void sim_texel(v9x_u32 unit, v9x_u32 argc, v9x_u32 cube,
                      const float *coords, float rgba[4])
{
    float c[3];
    v9x_u32 k;

    (void)cube;
    for (k = 0ul; k < 3ul; ++k) {
        c[k] = k < argc ? coords[k] : 0.0f;
    }
    texel(unit, argc, c, rgba);
}

/* Attribute a, component c at pixel p: test_intel_hsw_ps11.c's planes
 * at its barycentrics, so the same pixels straddle a kill. */
static float attribute(v9x_u32 p, v9x_u32 a, v9x_u32 c)
{
    float b1 = 0.05f + 0.11f * (float)p;
    float b2 = 0.9f - 0.1f * (float)p;
    float pp = 0.6f - 0.13f * (float)a + 0.07f * (float)c;
    float q = -0.4f + 0.11f * (float)c - 0.05f * (float)a;
    float r = 0.15f + 0.09f * (float)((a * 3ul + c) % 7ul) - 0.2f;

    return pp * b1 + q * b2 + r;
}

static void zero_env(V9X_PS11_ENV *env)
{
    memset(env, 0, sizeof(*env));
}

static void check_program(const char *name, const V9X_PS11_PROGRAM *program,
                          const V9X_PS11_ENV *env, v9x_u32 expect_killed_some)
{
    static struct v9x_nv50_sim sim;
    struct v9x_nv50_ps11_desc desc;
    struct v9x_nv50_ps11_info info;
    v9x_u32 constants[V9X_NV50_PS11_C_FLOATS];
    v9x_u32 p;
    v9x_u32 a;
    v9x_u32 c;
    v9x_u32 killed = 0ul;
    v9x_status status;
    int bad = 0;

    desc.cube = env->cube;
    desc.projected = env->projected;
    desc.unbound = env->unbound;
    desc.slot = 2ul;
    desc.fog = fog_on;
    status = v9x_nv50_ps11_build(program_words, PROGRAM_DWORDS, program,
                                 &desc, &info);
    if (status != V9X_STATUS_OK) {
        printf("FAIL %s: does not build (status %lu)\n", name,
               (unsigned long)status);
        ++failures;
        return;
    }
    CHECK(info.registers == V9X_NV50_PS11_REGISTERS);
    ++checked;
    if (info.words > largest) {
        largest = info.words;
    }
    CHECK((info.kills != 0ul) == (program->has_kill != 0ul));
    v9x_nv50_ps11_constants(program, env, constants);
    for (c = 0ul; c < 3ul; ++c) {
        float value = fog_rgb[c];

        memcpy(&constants[V9X_NV50_PS11_C_FOG + c], &value, sizeof(value));
    }

    for (p = 0ul; p < PIXELS; ++p) {
        V9X_PS11_PIXEL pixel;
        float out[4];
        int kept;

        memset(&sim, 0xcc, sizeof(sim));  /* registers start as garbage */
        sim.killed = 0;
        sim.error = 0;
        sim.slot = desc.slot;
        memset(sim.flags, 0, sizeof(sim.flags));
        for (a = 0ul; a < V9X_NV50_PS11_C_FLOATS; ++a) {
            sim.c[a] = as_float(constants[a]);
        }
        sim.v[V9X_NV50_PS11_V_W / 4ul] = WEIGHT;
        for (a = 0ul; a < ATTRIBUTES; ++a) {
            for (c = 0ul; c < 4ul; ++c) {
                float value = attribute(p, a, c);

                if (a < 2ul) {
                    pixel.v[a][c] = value;
                } else {
                    pixel.tc[a - 2ul][c] = value;
                }
                sim.v[(V9X_NV50_PS11_V_DIFFUSE / 4ul) + a * 4ul + c] =
                    value * WEIGHT;
            }
        }
        kept = v9x_ps11_run(program, env, &pixel, interpreter_texel, 0, out);
        if (!kept) {
            ++killed;
        }
        /* Fog as Gen3's test applies it: r0 clamped, then (c - fog) f +
         * fog with f v1.w. */
        if (fog_on) {
            float f = pixel.v[1][3];

            for (c = 0ul; c < 4ul; ++c) {
                out[c] = out[c] < 0.0f ? 0.0f : out[c] > 1.0f ? 1.0f : out[c];
            }
            for (c = 0ul; c < 3ul; ++c) {
                out[c] = (out[c] - fog_rgb[c]) * f + fog_rgb[c];
            }
        }
        if (!v9x_nv50_sim_run(&sim, program_words, info.words, sim_texel)) {
            printf("FAIL %s: the model stopped at word %lu (%s)\n", name,
                   (unsigned long)sim.error_at, sim.error);
            ++failures;
            return;
        }
        if (kept != !sim.killed) {
            printf("FAIL %s: pixel %lu kill differs (interpreter %s)\n",
                   name, (unsigned long)p, kept ? "kept" : "killed");
            bad = 1;
        }
        if (!kept) {
            continue;
        }
        for (c = 0ul; c < 4ul; ++c) {
            float got = v9x_nv50_sim_reg(&sim, c);

            if (!(fabs((double)(got - out[c])) <= (double)TOLERANCE)) {
                printf("FAIL %s: pixel %lu channel %lu: program %f, "
                       "interpreter %f\n", name, (unsigned long)p,
                       (unsigned long)c, (double)got, (double)out[c]);
                bad = 1;
            }
        }
    }
    if (expect_killed_some && (killed == 0ul || killed == PIXELS)) {
        printf("FAIL %s: the inputs killed %lu of %lu; the test needs some "
               "of each\n", name, (unsigned long)killed,
               (unsigned long)PIXELS);
        bad = 1;
    }
    if (bad) {
        ++failures;
    }
}

static void check_case(const char *name, const V9X_PS11_ENV *env,
                       v9x_u32 expect_killed_some)
{
    V9X_PS11_PROGRAM program;
    v9x_u32 where;

    if (v9x_ps11_parse(words, used, &program, &where) != V9X_PS11_OK) {
        printf("FAIL %s: does not parse at %lu\n", name,
               (unsigned long)where);
        ++failures;
        return;
    }
    check_program(name, &program, env, expect_killed_some);
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
    env.cube = 1ul;
    check_case("tex cube", &env, 0ul);
    env.cube = 0ul;
    env.projected = 1ul;
    check_case("tex projected", &env, 0ul);

    /* tex t0; texkill t1; mov r0, t0 */
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
     * t0 (a 3D texture, not a cube); mul r0, t3, t0 */
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

    /* Two constants in one instruction, and r1 read before it is
     * written (it is zero). */
    zero_env(&env);
    env.c[2][0] = 0.3f; env.c[2][1] = -0.4f; env.c[2][2] = 0.5f;
    env.c[2][3] = 0.6f;
    env.c[5][0] = 0.25f; env.c[5][1] = 0.5f; env.c[5][2] = -0.75f;
    env.c[5][3] = 1.0f;
    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_MAD); put(DST(R, 0, ALL, 0, 0)); put(SRC(C, 2, XYZW, 0));
    put(SRC(C, 5, XYZW, 0)); put(SRC(R, 1, XYZW, 0));
    put(OP_ADD); put(DST(R, 0, RGB, 0, 0)); put(SRC(R, 0, XYZW, 0));
    put(SRC(C, 5, XYZW, 6));
    put(END);
    check_case("two constants", &env, 0ul);

    /* cnd both ways: r0.a from v0.a + v1.a, either side of 0.5. */
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
    fog_on = 1ul;
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
    fog_on = 0ul;

    /* An unbound stage reads (0, 0, 0, 1); texkill still kills. */
    zero_env(&env);
    env.unbound = 1ul;
    used = 0ul;
    put(VER(1ul, 0ul));
    put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
    put(OP_TEXKILL); put(DST(T, 1, ALL, 0, 0));
    put(OP_MOV); put(DST(R, 0, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(END);
    check_case("texkill unbound", &env, 1ul);
    used = 0ul;
    put(VER(1ul, 0ul));
    put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
    put(OP_MOV); put(DST(R, 0, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(END);
    check_case("tex unbound", &env, 0ul);

    /* A shader that reads nothing and writes nothing outputs r0's zero. */
    zero_env(&env);
    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_MOV); put(DST(R, 1, ALL, 0, 0)); put(SRC(V, 0, XYZW, 0));
    put(END);
    check_case("r0 never written", &env, 0ul);
}

/* What is not translated is refused. */
static void refusals(void)
{
    V9X_PS11_PROGRAM program;
    struct v9x_nv50_ps11_desc desc;
    struct v9x_nv50_ps11_info info;
    v9x_u32 where;

    desc.cube = 0ul;
    desc.projected = 0ul;
    desc.unbound = 0ul;
    desc.slot = 2ul;
    desc.fog = 0ul;

    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
    put(OP_TEXBEM); put(DST(T, 1, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(OP_MOV); put(DST(R, 0, ALL, 0, 0)); put(SRC(T, 1, XYZW, 0));
    put(END);
    CHECK(v9x_ps11_parse(words, used, &program, &where) == V9X_PS11_OK);
    CHECK(v9x_nv50_ps11_build(program_words, PROGRAM_DWORDS, &program,
                              &desc, &info) == V9X_STATUS_OK);

    /* texbem on a cube stage. */
    desc.cube = 0x2ul;
    CHECK(v9x_nv50_ps11_build(program_words, PROGRAM_DWORDS, &program,
                              &desc, &info) == V9X_STATUS_UNSUPPORTED);
    desc.cube = 0ul;

    /* Too little room. */
    CHECK(v9x_nv50_ps11_build(program_words, 16ul, &program, &desc,
                              &info) == V9X_STATUS_INSUFFICIENT_MEMORY);
}

/* The constant block's layout (nv50_ps11.h). */
static void constants(void)
{
    V9X_PS11_PROGRAM program;
    V9X_PS11_ENV env;
    v9x_u32 out[V9X_NV50_PS11_C_FLOATS];
    v9x_u32 where;

    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_DEF); put(DST(C, 3, ALL, 0, 0));
    put(0x3f400000ul); put(0xbf000000ul); put(0x3e800000ul);
    put(0x3fc00000ul);                       /* 0.75 -0.5 0.25 1.5 */
    put(OP_MOV); put(DST(R, 0, ALL, 0, 0)); put(SRC(C, 3, XYZW, 0));
    put(END);
    CHECK(v9x_ps11_parse(words, used, &program, &where) == V9X_PS11_OK);

    zero_env(&env);
    env.c[0][0] = 0.25f;
    env.c[3][0] = -0.125f;          /* the def wins */
    env.c[7][1] = 2.0f;             /* clamped      */
    env.bump[2][3] = -0.75f;
    env.luminance[1][1] = 0.5f;
    v9x_nv50_ps11_constants(&program, &env, out);

    CHECK(as_float(out[0]) == 0.25f);
    CHECK(as_float(out[12]) == 0.75f && as_float(out[15]) == 1.0f);
    CHECK(as_float(out[29]) == 1.0f);
    CHECK(as_float(out[V9X_NV50_PS11_C_BUMP + 2ul * 4ul + 3ul]) == -0.75f);
    CHECK(as_float(out[V9X_NV50_PS11_C_LUMINANCE + 1ul * 2ul + 1ul]) ==
          0.5f);
    CHECK(as_float(out[V9X_NV50_PS11_C_HALF]) == 0.5f &&
          as_float(out[V9X_NV50_PS11_C_ONE]) == 1.0f &&
          as_float(out[V9X_NV50_PS11_C_MINUS_ONE]) == -1.0f &&
          as_float(out[V9X_NV50_PS11_C_TWO]) == 2.0f &&
          as_float(out[V9X_NV50_PS11_C_FOUR]) == 4.0f);
}

unsigned int v9x_run_nv50_ps11_tests(void)
{
    failures = 0u;
    checked = 0ul;
    largest = 0ul;
    cases();
    printf("INFO nv50_ps11: %lu programs held to the interpreter, the "
           "largest %lu words\n", (unsigned long)checked,
           (unsigned long)largest);
    refusals();
    constants();
    return failures;
}
