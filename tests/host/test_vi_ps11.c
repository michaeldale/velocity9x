/*
 * The GCN pixel shader 1.x programs (src\chipsets\ati\vi\vi_ps11.c), each
 * run in the one-pixel GCN 3 model (gcn_sim.c) and held to the reference
 * interpreter (src\display32\d3d\d3d_ps11_ref.c): r0 within a float
 * tolerance, the exported FP16 colour within FP16's, and the same pixels
 * killed. Each program's GCN 2 and GCN 1 rewrites must translate. Step 2
 * of docs\plans\ati-gcn-ps11.md.
 *
 * The model decodes our own words by the fields test_vi_3d.c pins to
 * LLVM's GFX8 bytes, refuses any instruction reading two scalars (the
 * constant bus), and starts every register the program does not load as
 * garbage. What it cannot show is the hardware disagreeing with the ISA
 * guide and Mesa; that is step 3's probe.
 *
 * The shaders are test_nv50_ps11.c's (after test_i9xx_ps11.c's and
 * test_intel_hsw_ps11.c's), so the four translators answer to the same
 * cases, with the same pixel planes and the same smooth texture function.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gcn_sim.h"
#include "velocity9x/cik_shader.h"
#include "velocity9x/ps11.h"
#include "velocity9x/vi_ps11.h"
#include "velocity9x/vi_shader.h"

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

#define PROGRAM_DWORDS 4096ul
#define TOLERANCE      2.0e-4f
/* The export is FP16 rounded toward zero: 2^-10 relative, and r0 is held
 * to [-1, 1]. */
#define FP16_TOLERANCE 2.0e-3f
#define PIXELS         8ul
#define ATTRIBUTES     6ul       /* v0, v1, then coordinate sets 0 to 3 */
#define GARBAGE        0xccccccccul

/* The T# TYPE (dword 3, 31:28; SQ_RSRC_IMG_*), and where the test puts
 * the stage: dword 0, the S#'s dword 0 the stage + 100h. */
#define TYPE_2D        9ul
#define TYPE_3D        10ul
#define TYPE_CUBE      11ul
#define TABLE_LOW      0x00800000ul
#define TABLE_HIGH     0x00000001ul

static v9x_u32 words[128];
static v9x_u32 used;
static v9x_u32 program_words[PROGRAM_DWORDS];
static v9x_u32 rewritten[PROGRAM_DWORDS];
static v9x_u32 table[V9X_VI_PS11_TABLE_DWORDS];
/* Programs run through the model, and the largest one's size. */
static v9x_u32 checked;
static v9x_u32 largest;
static v9x_u32 most_sgprs;
/* Fog after the shader for the next cases, and its colour. */
static v9x_u32 fog_on;
static float fog_rgb[3];
/* The alpha test for the next cases: its v_cmpx and reference. */
static v9x_u32 alpha_on;
static v9x_u32 alpha_func;
static float alpha_ref;
/* The run the sampler is called from, and what it found wrong. */
static struct v9x_gcn_sim *current;
static const char *sampler_wrong;

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

/*
 * The model's sampler. The stage and kind come from the T# the program
 * loaded, so a stage read from the wrong table slot shows. A 2D image
 * reads u and v, a 3D one three coordinates; a cube's address is
 * (sc / |ma| + 1.5, tc / |ma| + 1.5, face), turned back into the
 * direction over its largest magnitude by v_cube*'s face table
 * (gcn_sim.c's sim_cube, after Mesa's build_cube_select), the
 * coordinates the interpreter samples. DA must be set for a cube only.
 */
static void sim_sample(const v9x_u32 *t_sharp, const v9x_u32 *s_sharp,
                       const float *address, v9x_u32 dmask, float rgba[4])
{
    v9x_u32 stage = t_sharp[0];
    v9x_u32 type = t_sharp[3] >> 28;
    int da = (current->mimg_word & V9X_VI_MIMG_DA) != 0ul;
    float d[3];
    float sn;
    float tn;
    long face;

    if (stage > 3ul || s_sharp[0] != 0x100ul + stage) {
        sampler_wrong = "T# and S# not one stage's";
        stage &= 3ul;
    }
    if (dmask != 0xful) {
        sampler_wrong = "dmask";
    }
    if (da != (type == TYPE_CUBE)) {
        sampler_wrong = "DA";
    }
    if (type == TYPE_2D) {
        texel(stage, 2ul, address, rgba);
        return;
    }
    if (type == TYPE_3D) {
        texel(stage, 3ul, address, rgba);
        return;
    }
    sn = (address[0] - 1.5f) * 2.0f;
    tn = (address[1] - 1.5f) * 2.0f;
    face = (long)floor((double)address[2] + 0.5);
    switch (face) {
    case 0L: d[0] = 1.0f;  d[1] = -tn;   d[2] = -sn;   break;
    case 1L: d[0] = -1.0f; d[1] = -tn;   d[2] = sn;    break;
    case 2L: d[0] = sn;    d[1] = 1.0f;  d[2] = tn;    break;
    case 3L: d[0] = sn;    d[1] = -1.0f; d[2] = -tn;   break;
    case 4L: d[0] = sn;    d[1] = -tn;   d[2] = 1.0f;  break;
    case 5L: d[0] = -sn;   d[1] = -tn;   d[2] = -1.0f; break;
    default:
        sampler_wrong = "cube face";
        d[0] = d[1] = d[2] = 0.0f;
        break;
    }
    texel(stage, 3ul, d, rgba);
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

/* The table a draw would hand the program: each stage's T# typed as the
 * program samples it, its S#, the constants, the fog colour. */
static void fill_table(const V9X_PS11_PROGRAM *program,
                       const V9X_PS11_ENV *env)
{
    v9x_u32 stage;
    v9x_u32 i;

    for (i = 0ul; i < V9X_VI_PS11_TABLE_DWORDS; ++i) {
        table[i] = GARBAGE;
    }
    for (stage = 0ul; stage < 4ul; ++stage) {
        v9x_u32 type = TYPE_2D;

        for (i = 0ul; i < program->count; ++i) {
            v9x_u32 op = program->inst[i].op;

            if (program->inst[i].dst_reg == stage &&
                (op == V9X_PS11_TEXM3X3TEX || op == V9X_PS11_TEXM3X3SPEC ||
                 op == V9X_PS11_TEXM3X3VSPEC)) {
                type = TYPE_3D;
            }
        }
        if ((env->cube & (1ul << stage)) != 0ul) {
            type = TYPE_CUBE;
        }
        for (i = 0ul; i < 8ul; ++i) {
            table[V9X_VI_PS11_T_SHARP + 8ul * stage + i] = 0ul;
        }
        table[V9X_VI_PS11_T_SHARP + 8ul * stage] = stage;
        table[V9X_VI_PS11_T_SHARP + 8ul * stage + 3ul] = type << 28;
        table[V9X_VI_PS11_S_SHARP + 4ul * stage] = 0x100ul + stage;
    }
    v9x_vi_ps11_constants(program, env, table);
    for (i = 0ul; i < 3ul; ++i) {
        table[V9X_VI_PS11_FOG + i] = as_bits(fog_rgb[i]);
    }
    table[V9X_VI_PS11_ALPHA_REF] = as_bits(alpha_ref);
}

static void check_program(const char *name, const V9X_PS11_PROGRAM *program,
                          const V9X_PS11_ENV *env, v9x_u32 expect_killed_some)
{
    static struct v9x_gcn_sim sim;
    struct v9x_vi_ps11_desc desc;
    struct v9x_vi_ps11_info info;
    v9x_u32 p;
    v9x_u32 a;
    v9x_u32 c;
    v9x_u32 killed = 0ul;
    v9x_u32 alpha_killed = 0ul;
    v9x_u32 failed_at = 0ul;
    v9x_status status;
    int bad = 0;

    desc.cube = env->cube;
    desc.projected = env->projected;
    desc.unbound = env->unbound;
    desc.fog = fog_on;
    desc.alpha_test = alpha_on;
    desc.alpha_func = alpha_func;
    status = v9x_vi_ps11_build(program_words, PROGRAM_DWORDS, program, &desc,
                               &info);
    if (status != V9X_STATUS_OK) {
        printf("FAIL %s: does not build (status %lu)\n", name,
               (unsigned long)status);
        ++failures;
        return;
    }
    CHECK(info.vgprs == V9X_VI_PS11_VGPRS);
    ++checked;
    if (info.words > largest) {
        largest = info.words;
    }
    if (info.sgprs > most_sgprs) {
        most_sgprs = info.sgprs;
    }
    CHECK((info.kills != 0ul) ==
          (program->has_kill != 0ul || alpha_on != 0ul));

    /* GCN 2 and GCN 1 take every word. */
    memcpy(rewritten, program_words, info.words * sizeof(v9x_u32));
    if (v9x_cik_shader_from_vi(rewritten, info.words, &failed_at) !=
        V9X_STATUS_OK) {
        printf("FAIL %s: the GCN 2 rewrite refuses word %lu (%08lx)\n", name,
               (unsigned long)failed_at,
               (unsigned long)program_words[failed_at]);
        bad = 1;
    }
    memcpy(rewritten, program_words, info.words * sizeof(v9x_u32));
    if (v9x_si_shader_from_vi(rewritten, info.words, &failed_at) !=
        V9X_STATUS_OK) {
        printf("FAIL %s: the GCN 1 rewrite refuses word %lu (%08lx)\n", name,
               (unsigned long)failed_at,
               (unsigned long)program_words[failed_at]);
        bad = 1;
    }

    fill_table(program, env);
    for (p = 0ul; p < PIXELS; ++p) {
        V9X_PS11_PIXEL pixel;
        float out[4];
        int kept;

        v9x_gcn_sim_reset(&sim);
        for (a = 2ul; a < V9X_GCN_SIM_VGPRS; ++a) {
            sim.v[a] = GARBAGE;
        }
        for (a = 3ul; a < V9X_GCN_SIM_SGPRS; ++a) {
            sim.s[a] = GARBAGE;
        }
        sim.s[0] = TABLE_LOW;
        sim.s[1] = TABLE_HIGH;
        sim.s[2] = 0x12345678ul;        /* PRIM_MASK */
        sim.memory = table;
        sim.memory_low = TABLE_LOW;
        sim.memory_high = TABLE_HIGH;
        sim.memory_dwords = V9X_VI_PS11_TABLE_DWORDS;
        sim.sample = sim_sample;
        for (a = 0ul; a < V9X_GCN_SIM_ATTRS; ++a) {
            for (c = 0ul; c < 4ul; ++c) {
                sim.attr[a][c] = as_float(GARBAGE);
            }
        }
        for (a = 0ul; a < ATTRIBUTES; ++a) {
            for (c = 0ul; c < 4ul; ++c) {
                float value = attribute(p, a, c);

                if (a < 2ul) {
                    pixel.v[a][c] = value;
                } else {
                    pixel.tc[a - 2ul][c] = value;
                }
                sim.attr[a][c] = value;
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
        /* The alpha test on r0.a clamped: GE or LT, the two cases use. */
        if (alpha_on && kept) {
            float a = out[3] < 0.0f ? 0.0f : out[3] > 1.0f ? 1.0f : out[3];
            int pass = alpha_func == V9X_VI_VOPC_CMPX_GE_F32 ? a >= alpha_ref
                                                             : a < alpha_ref;

            if (!pass) {
                kept = 0;
                ++killed;
                ++alpha_killed;
            }
        }
        current = &sim;
        sampler_wrong = 0;
        if (!v9x_gcn_sim_run(&sim, program_words, info.words)) {
            printf("FAIL %s: the model stopped at word %lu (%s)\n", name,
                   (unsigned long)sim.error_at, sim.error);
            ++failures;
            return;
        }
        if (sampler_wrong != 0) {
            printf("FAIL %s: pixel %lu sampled wrong (%s)\n", name,
                   (unsigned long)p, sampler_wrong);
            bad = 1;
        }
        if (!sim.done) {
            printf("FAIL %s: pixel %lu ended without a done export\n", name,
                   (unsigned long)p);
            bad = 1;
        }
        if (kept != !sim.killed || kept != sim.exported) {
            printf("FAIL %s: pixel %lu kill differs (interpreter %s)\n",
                   name, (unsigned long)p, kept ? "kept" : "killed");
            bad = 1;
        }
        if (!kept) {
            continue;
        }
        for (c = 0ul; c < 4ul; ++c) {
            float got = v9x_gcn_sim_vgpr(&sim, 2ul + c);

            if (!(fabs((double)(got - out[c])) <= (double)TOLERANCE)) {
                printf("FAIL %s: pixel %lu channel %lu: program %f, "
                       "interpreter %f\n", name, (unsigned long)p,
                       (unsigned long)c, (double)got, (double)out[c]);
                bad = 1;
            }
            if (!(fabs((double)(sim.mrt0[c] - out[c])) <=
                  (double)FP16_TOLERANCE)) {
                printf("FAIL %s: pixel %lu channel %lu: exported %f, "
                       "interpreter %f\n", name, (unsigned long)p,
                       (unsigned long)c, (double)sim.mrt0[c],
                       (double)out[c]);
                bad = 1;
            }
        }
    }
    if (alpha_on && (alpha_killed == 0ul || alpha_killed == PIXELS)) {
        printf("FAIL %s: the alpha test killed %lu of %lu; the test needs "
               "some of each\n", name, (unsigned long)alpha_killed,
               (unsigned long)PIXELS);
        bad = 1;
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

    /* cnd with a negated constant, saturated and shifted: the select's
     * operands made plain first, the shift a multiply after. */
    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_ADD); put(DST(R, 0, A, 0, 0)); put(SRC(V, 0, AAAA, 0));
    put(SRC(V, 1, AAAA, 0));
    put(OP_CND); put(DST(R, 0, ALL, 1, 1)); put(SRC(R, 0, AAAA, 0));
    put(SRC(C, 0, XYZW, 1)); put(SRC(V, 1, XYZW, 1));
    put(END);
    check_case("cnd negated saturated", &env, 0ul);

    /* The same fogged: some channels leave [0, 1] and are clamped first. */
    fog_on = 1ul;
    fog_rgb[0] = 0.25f; fog_rgb[1] = 0.5f; fog_rgb[2] = 1.0f;
    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_ADD); put(DST(R, 0, A, 0, 0)); put(SRC(V, 0, AAAA, 0));
    put(SRC(V, 1, AAAA, 0));
    put(OP_CND); put(DST(R, 0, ALL, 0, 0)); put(SRC(R, 0, AAAA, 0));
    put(SRC(C, 0, XYZW, 0)); put(SRC(V, 1, XYZW, 0));
    put(END);
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

    /* The alpha test after fog, and beside texkill: the texel's alpha
     * against a reference either way. */
    alpha_on = 1ul;
    alpha_func = V9X_VI_VOPC_CMPX_GE_F32;
    alpha_ref = 0.87f;     /* the texel alpha runs 0.80 to 0.92 */
    check_case("texkill fog alpha GE", &env, 0ul);
    fog_on = 0ul;
    alpha_func = V9X_VI_VOPC_CMPX_LT_F32;
    used = 0ul;
    put(VER(1ul, 0ul));
    put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
    put(OP_MOV); put(DST(R, 0, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(END);
    check_case("alpha LT", &env, 0ul);
    alpha_on = 0ul;
    alpha_ref = 0.0f;

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
    V9X_PS11_ENV env;
    struct v9x_vi_ps11_desc desc;
    struct v9x_vi_ps11_info info;
    v9x_u32 where;
    v9x_u32 k;

    desc.cube = 0ul;
    desc.projected = 0ul;
    desc.unbound = 0ul;
    desc.fog = 0ul;
    desc.alpha_test = 0ul;
    desc.alpha_func = 0ul;

    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
    put(OP_TEXBEM); put(DST(T, 1, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(OP_MOV); put(DST(R, 0, ALL, 0, 0)); put(SRC(T, 1, XYZW, 0));
    put(END);
    CHECK(v9x_ps11_parse(words, used, &program, &where) == V9X_PS11_OK);
    CHECK(v9x_vi_ps11_build(program_words, PROGRAM_DWORDS, &program, &desc,
                            &info) == V9X_STATUS_OK);

    /* texbem on a cube stage. */
    desc.cube = 0x2ul;
    CHECK(v9x_vi_ps11_build(program_words, PROGRAM_DWORDS, &program, &desc,
                            &info) == V9X_STATUS_UNSUPPORTED);
    desc.cube = 0ul;

    /* Too little room. */
    CHECK(v9x_vi_ps11_build(program_words, 16ul, &program, &desc, &info) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);

    /* Four stages' descriptors, all eight constants, a texbeml's bump and
     * luminance and the fog colour: more than the SGPRs the loads may
     * have. Without the fog it fits, and runs. */
    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
    put(OP_TEX); put(DST(T, 1, ALL, 0, 0));
    put(OP_TEX); put(DST(T, 2, ALL, 0, 0));
    put(OP_TEXBEML); put(DST(T, 3, ALL, 0, 0)); put(SRC(T, 2, XYZW, 0));
    put(OP_MAD); put(DST(R, 0, ALL, 0, 0)); put(SRC(C, 0, XYZW, 0));
    put(SRC(C, 1, XYZW, 0)); put(SRC(C, 2, XYZW, 0));
    put(OP_MAD); put(DST(R, 1, ALL, 0, 0)); put(SRC(C, 3, XYZW, 0));
    put(SRC(C, 4, XYZW, 0)); put(SRC(C, 5, XYZW, 0));
    put(OP_MAD); put(DST(R, 0, ALL, 0, 0)); put(SRC(C, 6, XYZW, 0));
    put(SRC(C, 7, XYZW, 0)); put(SRC(R, 0, XYZW, 0));
    put(OP_MAD); put(DST(R, 0, ALL, 0, 0)); put(SRC(T, 3, XYZW, 0));
    put(SRC(R, 1, XYZW, 0)); put(SRC(R, 0, XYZW, 0));
    put(END);
    CHECK(v9x_ps11_parse(words, used, &program, &where) == V9X_PS11_OK);
    desc.fog = 1ul;
    CHECK(v9x_vi_ps11_build(program_words, PROGRAM_DWORDS, &program, &desc,
                            &info) == V9X_STATUS_UNSUPPORTED);
    zero_env(&env);
    for (k = 0ul; k < 8ul; ++k) {
        env.c[k][0] = 0.1f * (float)k;
        env.c[k][1] = -0.05f * (float)k;
        env.c[k][2] = 0.3f;
        env.c[k][3] = 0.5f - 0.1f * (float)k;
    }
    env.bump[3][0] = 0.1f;
    env.bump[3][3] = -0.2f;
    env.luminance[3][0] = 0.5f;
    env.luminance[3][1] = 0.25f;
    check_program("every block loaded", &program, &env, 0ul);
}

/* The table's layout (vi_ps11.h). */
static void constants(void)
{
    V9X_PS11_PROGRAM program;
    V9X_PS11_ENV env;
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
    table[0] = 0xdeadbeeful;
    table[V9X_VI_PS11_FOG] = 0xfeedfaceul;
    v9x_vi_ps11_constants(&program, &env, table);

    CHECK(table[0] == 0xdeadbeeful);                /* descriptors kept */
    CHECK(table[V9X_VI_PS11_FOG] == 0xfeedfaceul);  /* fog the caller's */
    CHECK(as_float(table[V9X_VI_PS11_CONST]) == 0.25f);
    CHECK(as_float(table[V9X_VI_PS11_CONST + 12ul]) == 0.75f &&
          as_float(table[V9X_VI_PS11_CONST + 15ul]) == 1.0f);
    CHECK(as_float(table[V9X_VI_PS11_CONST + 29ul]) == 1.0f);
    CHECK(as_float(table[V9X_VI_PS11_BUMP + 2ul * 4ul + 3ul]) == -0.75f);
    CHECK(as_float(table[V9X_VI_PS11_LUMINANCE + 1ul * 2ul + 1ul]) == 0.5f);
}

unsigned int v9x_run_vi_ps11_tests(void)
{
    failures = 0u;
    checked = 0ul;
    largest = 0ul;
    most_sgprs = 0ul;
    cases();
    refusals();
    printf("INFO vi_ps11: %lu programs held to the interpreter, the "
           "largest %lu words, at most %lu SGPRs\n", (unsigned long)checked,
           (unsigned long)largest, (unsigned long)most_sgprs);
    constants();
    return failures;
}
