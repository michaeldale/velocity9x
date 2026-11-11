/*
 * The Gen3 pixel shader 1.x programs (src\chipsets\intel\i9xx_ps11.c),
 * each run in the host model of the fragment program (i9xx_fs_sim.c) on
 * eight pixels and held to the reference interpreter
 * (src\display32\d3d\d3d_ps11_ref.c): r0 within a float tolerance, and
 * the same pixels killed.
 *
 * The shaders are test_intel_hsw_ps11.c's, written after the instruction
 * patterns 3DMark 2001 SE's use, so the two translators answer to the
 * same cases. The pixels' inputs are that file's planes at eight points,
 * and the texture is its smooth function of the coordinates, shared by
 * both sides. A cube lookup reaches the shared texture divided by its
 * largest magnitude on both sides: the interpreter divides, and the
 * model's sampler stands in for a cube sampler, which does not care.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "i9xx_fs_sim.h"
#include "velocity9x/i9xx_fs.h"
#include "velocity9x/i9xx_ps11.h"
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

#define PROGRAM_DWORDS 512ul
#define TOLERANCE  2.0e-4f
#define PIXELS     8ul
#define ATTRIBUTES 6ul       /* v0, v1, then coordinate sets 0 to 3 */

/* test_i9xx_shader.c: the program in a whole stream through the shader
 * licence (step 5d). */
v9x_u16 v9x_test_shader_licence(const v9x_u32 *program, v9x_u32 dwords,
                                v9x_u32 units, v9x_u32 cubes,
                                v9x_u32 constants, v9x_u32 *where);

static v9x_u32 words[128];
static v9x_u32 used;
static v9x_u32 fog_on;
/* The licence's expected answer for the next case: licensed, or (for a
 * texm3x3 lookup on a stage that is not a cube, declared VOLUME, which
 * Gen3 places nothing for) refused as a shader. */
static v9x_u16 licence_expect = V9X_I9XX_P5_OK;
static float fog_rgb[3];
static v9x_u32 program_words[PROGRAM_DWORDS];

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

static v9x_u32 as_bits(float value)
{
    v9x_u32 bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
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

/* The model's sampler: 2D reads u and v; a volume three coordinates; a
 * cube three, over their largest magnitude. */
static void sim_texel(void *user, v9x_u32 sampler, v9x_u32 sample_type,
                      v9x_u32 opcode, const float coord[4], float rgba[4])
{
    float c[3];
    float largest;
    v9x_u32 k;

    (void)user;
    (void)opcode;
    for (k = 0ul; k < 3ul; ++k) {
        c[k] = coord[k];
    }
    if (sample_type == 0ul) {
        texel(sampler, 2ul, c, rgba);
        return;
    }
    if (sample_type == 1ul) {
        largest = (float)fabs((double)c[0]);
        for (k = 1ul; k < 3ul; ++k) {
            if ((float)fabs((double)c[k]) > largest) {
                largest = (float)fabs((double)c[k]);
            }
        }
        for (k = 0ul; k < 3ul; ++k) {
            c[k] /= largest;
        }
    }
    texel(sampler, 3ul, c, rgba);
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
    static struct v9x_i9xx_fs_sim sim;
    struct v9x_i9xx_ps11_desc desc;
    v9x_u32 constants[V9X_I9XX_PS11_CONST_REGS * 4u];
    v9x_u32 fog_bits[3];
    v9x_u32 dwords = 0ul;
    v9x_u32 p;
    v9x_u32 a;
    v9x_u32 c;
    v9x_u32 killed = 0ul;
    v9x_status status;
    int bad = 0;

    desc.cube = env->cube;
    desc.projected = env->projected;
    desc.unbound = env->unbound;
    desc.fog = fog_on;
    status = v9x_i9xx_ps11_build(program_words, PROGRAM_DWORDS, program,
                                 &desc, &dwords);
    if (status != V9X_STATUS_OK) {
        printf("FAIL %s: does not build (status %lu)\n", name,
               (unsigned long)status);
        ++failures;
        return;
    }
    /* The licence: the stages the program declares samplers for, cubes
     * where the draw says so. */
    {
        v9x_u32 units = 0ul;
        v9x_u32 where = 0ul;
        v9x_u32 at;
        v9x_u16 answer;

        for (at = 1ul; at + 2ul < dwords; at += 3ul) {
            if ((program_words[at] & 0x1f000000ul) == 0x19000000ul &&
                ((program_words[at] >> 19) & 7ul) == 3ul) {
                units |= 1ul << ((program_words[at] >> 14) & 0xful);
            }
        }
        answer = v9x_test_shader_licence(program_words, dwords, units,
                                         env->cube & units,
                                         V9X_I9XX_PS11_CONST_REGS, &where);
        if (answer != licence_expect) {
            printf("FAIL %s: licence %u at dword %lu, expected %u\n", name,
                   (unsigned int)answer, (unsigned long)where,
                   (unsigned int)licence_expect);
            ++failures;
        }
    }

    for (c = 0ul; c < 3ul; ++c) {
        fog_bits[c] = as_bits(fog_rgb[c]);
    }
    v9x_i9xx_ps11_constants(program, env, fog_bits, constants);

    memset(&sim, 0, sizeof(sim));
    sim.sample = sim_texel;
    for (a = 0ul; a < V9X_I9XX_PS11_CONST_REGS; ++a) {
        for (c = 0ul; c < 4ul; ++c) {
            sim.c[a][c] = as_float(constants[4ul * a + c]);
        }
    }

    for (p = 0ul; p < PIXELS; ++p) {
        V9X_PS11_PIXEL pixel;
        float out[4];
        int kept;

        for (a = 0ul; a < ATTRIBUTES; ++a) {
            for (c = 0ul; c < 4ul; ++c) {
                float value = attribute(p, a, c);

                if (a < 2ul) {
                    pixel.v[a][c] = value;
                    sim.t[8ul + a][c] = value;
                } else {
                    pixel.tc[a - 2ul][c] = value;
                    sim.t[a - 2ul][c] = value;
                }
            }
        }
        kept = v9x_ps11_run(program, env, &pixel, interpreter_texel, 0, out);
        if (!kept) {
            ++killed;
        }
        v9x_i9xx_fs_sim_reset(&sim);
        if (!v9x_i9xx_fs_sim_run(&sim, program_words, dwords)) {
            printf("FAIL %s: the model stopped at dword %lu\n", name,
                   (unsigned long)sim.error_at);
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
        if (fog_on) {
            float f = pixel.v[1][3];

            for (c = 0ul; c < 4ul; ++c) {
                out[c] = out[c] < 0.0f ? 0.0f : out[c] > 1.0f ? 1.0f : out[c];
            }
            for (c = 0ul; c < 3ul; ++c) {
                out[c] = (out[c] - fog_rgb[c]) * f + fog_rgb[c];
            }
        }
        if (sim.oc_written != 0xful) {
            printf("FAIL %s: oC written %lx\n", name,
                   (unsigned long)sim.oc_written);
            bad = 1;
        }
        for (c = 0ul; c < 4ul; ++c) {
            if (fabs((double)(sim.oc[c] - out[c])) > (double)TOLERANCE) {
                printf("FAIL %s: pixel %lu channel %lu: program %f, "
                       "interpreter %f\n", name, (unsigned long)p,
                       (unsigned long)c, (double)sim.oc[c], (double)out[c]);
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
    licence_expect = V9X_I9XX_P5_SHADER;
    check_case("texm3x3tex", &env, 0ul);
    licence_expect = V9X_I9XX_P5_OK;

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

/* What the fragment program cannot hold is refused. */
static void refusals(void)
{
    V9X_PS11_PROGRAM program;
    V9X_PS11_ENV env;
    struct v9x_i9xx_ps11_desc desc;
    v9x_u32 where;
    v9x_u32 dwords;

    desc.cube = 0ul;
    desc.projected = 0ul;
    desc.unbound = 0ul;
    desc.fog = 0ul;

    /* tex t0; texbem t1, t0; texbem t2, t1; texbem t3, t2: four phases,
     * the limit. */
    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
    put(OP_TEXBEM); put(DST(T, 1, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(OP_TEXBEM); put(DST(T, 2, ALL, 0, 0)); put(SRC(T, 1, XYZW, 0));
    put(OP_TEXBEM); put(DST(T, 3, ALL, 0, 0)); put(SRC(T, 2, XYZW, 0));
    put(OP_MOV); put(DST(R, 0, ALL, 0, 0)); put(SRC(T, 3, XYZW, 0));
    put(END);
    CHECK(v9x_ps11_parse(words, used, &program, &where) == V9X_PS11_OK);
    CHECK(v9x_i9xx_ps11_build(program_words, PROGRAM_DWORDS, &program,
                              &desc, &dwords) == V9X_STATUS_OK);
    zero_env(&env);
    env.bump[1][0] = 0.1f; env.bump[2][3] = 0.2f; env.bump[3][1] = -0.3f;
    check_program("texbem chain", &program, &env, 0ul);

    /* A kill on stage 0's coordinates ahead of it is a fifth phase. The
     * program is assembled by hand: whether the parser takes texkill and
     * tex on one stage is not this test's question. */
    {
        V9X_PS11_PROGRAM five = program;
        v9x_u32 i;

        for (i = five.count; i > 0ul; --i) {
            five.inst[i] = five.inst[i - 1ul];
        }
        ++five.count;
        memset(&five.inst[0], 0, sizeof(five.inst[0]));
        five.inst[0].op = (v9x_u8)V9X_PS11_TEXKILL;
        five.inst[0].dst_file = (v9x_u8)V9X_PS11_FILE_TEXTURE;
        five.inst[0].dst_reg = 0u;
        five.inst[0].dst_mask = 0xfu;
        CHECK(v9x_i9xx_ps11_build(program_words, PROGRAM_DWORDS, &five,
                                  &desc, &dwords) == V9X_STATUS_UNSUPPORTED);
    }

    /* texbem on a cube stage. */
    desc.cube = 0x2ul;
    CHECK(v9x_i9xx_ps11_build(program_words, PROGRAM_DWORDS, &program,
                              &desc, &dwords) == V9X_STATUS_UNSUPPORTED);
    desc.cube = 0ul;

    /* Too little room. */
    CHECK(v9x_i9xx_ps11_build(program_words, 8ul, &program, &desc,
                              &dwords) == V9X_STATUS_INSUFFICIENT_MEMORY);
}

/* The constant block's layout (i9xx_ps11.h). */
static void constants(void)
{
    V9X_PS11_PROGRAM program;
    V9X_PS11_ENV env;
    v9x_u32 out[V9X_I9XX_PS11_CONST_REGS * 4u];
    v9x_u32 fog[3];
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
    fog[0] = as_bits(0.1f);
    fog[1] = as_bits(0.2f);
    fog[2] = as_bits(0.3f);
    v9x_i9xx_ps11_constants(&program, &env, fog, out);

    CHECK(as_float(out[0]) == 0.25f);
    CHECK(as_float(out[12]) == 0.75f && as_float(out[15]) == 1.0f);
    CHECK(as_float(out[29]) == 1.0f);
    CHECK(as_float(out[4ul * (V9X_I9XX_PS11_CONST_BUMP + 2ul) + 3ul]) ==
          -0.75f);
    CHECK(as_float(out[4ul * (V9X_I9XX_PS11_CONST_LUMINANCE + 1ul) + 1ul])
          == 0.5f);
    CHECK(as_float(out[4ul * V9X_I9XX_PS11_CONST_FIXED + 0ul]) == 0.5f &&
          as_float(out[4ul * V9X_I9XX_PS11_CONST_FIXED + 1ul]) == 2.0f &&
          as_float(out[4ul * V9X_I9XX_PS11_CONST_FIXED + 2ul]) == 4.0f);
    CHECK(out[4ul * V9X_I9XX_PS11_CONST_FOG + 2ul] == fog[2] &&
          out[4ul * V9X_I9XX_PS11_CONST_FOG + 3ul] == 0ul);

    v9x_i9xx_ps11_constants(&program, &env, 0, out);
    CHECK(out[4ul * V9X_I9XX_PS11_CONST_FOG + 0ul] == 0ul);
}

unsigned int v9x_run_i9xx_ps11_tests(void)
{
    failures = 0u;
    cases();
    refusals();
    constants();
    return failures;
}
