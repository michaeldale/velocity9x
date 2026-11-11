/*
 * The Gen3 fragment-program encoder (src\chipsets\intel\i9xx_fs.c) and
 * the host model of the program (i9xx_fs_sim.c).
 *
 * Three kinds of check:
 *
 *   - the encoder reproduces, dword for dword, three of the fixed
 *     programs the 2026-09 audits derived and the netbook has run;
 *   - fields the fixed programs never set (saturate, negates, ZERO and
 *     ONE, a cube sampler, TEXLDP, U registers) land where Mesa's
 *     i915_reg.h puts them, the expected dwords worked from its shifts;
 *   - the model runs the fixed programs and gives what their comments
 *     say they compute, and runs encoded programs of every opcode the
 *     PS 1.1 translator will use.
 *
 * None of it shows the hardware reads the fields this way; step 4 of
 * docs\plans\gen3-ps11-pixel-shaders.md, the netbook probe, does.
 */
#include <math.h>
#include <stdio.h>

#include "velocity9x/i9xx_fs.h"
#include "i9xx_fs_sim.h"

static unsigned int failures = 0u;

#define CHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++failures; \
    } \
} while (0)

#define PROGRAM_CAPACITY 512u

#define R     V9X_I9XX_FS_REG_TYPE_R
#define T     V9X_I9XX_FS_REG_TYPE_T
#define CONST V9X_I9XX_FS_REG_TYPE_CONST
#define OC    V9X_I9XX_FS_REG_TYPE_OC
#define U     V9X_I9XX_FS_REG_TYPE_U
#define XYZW  V9X_I9XX_FS_MASK_XYZW

static int v9x_near(float a, float b)
{
    return fabs((double)a - (double)b) < 1e-5;
}

static int v9x_near4(const float *a, float x, float y, float z, float w)
{
    return v9x_near(a[0], x) && v9x_near(a[1], y) &&
           v9x_near(a[2], z) && v9x_near(a[3], w);
}

static void v9x_set4(float *to, float x, float y, float z, float w)
{
    to[0] = x;
    to[1] = y;
    to[2] = z;
    to[3] = w;
}

/* The test's texture: the texel is the coordinate itself in r, g, b and
 * a value naming the sampler in a, so a test can see what was sampled,
 * with what, and from where. */
struct v9x_sample_log {
    v9x_u32 sampler;
    v9x_u32 sample_type;
    v9x_u32 opcode;
    float coord[4];
};

static void v9x_sample(void *user, v9x_u32 sampler, v9x_u32 sample_type,
                       v9x_u32 opcode, const float coord[4], float rgba[4])
{
    struct v9x_sample_log *log = (struct v9x_sample_log *)user;
    unsigned int index;

    log->sampler = sampler;
    log->sample_type = sample_type;
    log->opcode = opcode;
    for (index = 0u; index < 4u; ++index) {
        log->coord[index] = coord[index];
    }
    rgba[0] = coord[0];
    rgba[1] = coord[1];
    rgba[2] = coord[2];
    rgba[3] = 0.25f + 0.0625f * (float)sampler;
}

static void v9x_sim_init(struct v9x_i9xx_fs_sim *sim,
                         struct v9x_sample_log *log)
{
    unsigned int reg;

    for (reg = 0u; reg < 11u; ++reg) {
        v9x_set4(sim->t[reg], 0.0f, 0.0f, 0.0f, 0.0f);
    }
    for (reg = 0u; reg < 32u; ++reg) {
        v9x_set4(sim->c[reg], 0.0f, 0.0f, 0.0f, 0.0f);
    }
    sim->sample = v9x_sample;
    sim->sample_user = log;
    v9x_i9xx_fs_sim_reset(sim);
}

/* ------------------------------------------------------------------ */
/* The fixed programs, rebuilt                                         */
/* ------------------------------------------------------------------ */

static void v9x_same(const v9x_u32 *a, v9x_u32 a_len,
                     const v9x_u32 *b, v9x_u32 b_len)
{
    v9x_u32 index;

    CHECK(a_len == b_len);
    if (a_len != b_len) {
        return;
    }
    for (index = 0ul; index < a_len; ++index) {
        if (a[index] != b[index]) {
            printf("  dword %lu: %08lx against %08lx\n",
                   (unsigned long)index, (unsigned long)a[index],
                   (unsigned long)b[index]);
        }
        CHECK(a[index] == b[index]);
    }
}

static void test_reproduces_fixed_programs(void)
{
    v9x_u32 fixed[64];
    v9x_u32 encoded[64];
    v9x_u32 fixed_len;
    v9x_u32 encoded_len;
    struct v9x_i9xx_fs fs;

    /* dcl T8; mov oC, T8 */
    CHECK(v9x_i9xx_build_fragment_program(fixed, 64ul, &fixed_len) ==
          V9X_STATUS_OK);
    v9x_i9xx_fs_begin(&fs, encoded, 64ul);
    v9x_i9xx_fs_dcl_t(&fs, V9X_I9XX_FS_T_DIFFUSE, XYZW);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, OC, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(T, V9X_I9XX_FS_T_DIFFUSE), 0ul, 0ul);
    CHECK(v9x_i9xx_fs_end(&fs, &encoded_len) == V9X_STATUS_OK);
    v9x_same(encoded, encoded_len, fixed, fixed_len);

    /* dcl T0; dcl S0; texld oC, S0, T0 */
    CHECK(v9x_i9xx_build_sampling_program(fixed, 64ul, &fixed_len) ==
          V9X_STATUS_OK);
    v9x_i9xx_fs_begin(&fs, encoded, 64ul);
    v9x_i9xx_fs_dcl_t(&fs, 0ul, XYZW);
    v9x_i9xx_fs_dcl_s(&fs, 0ul, V9X_I9XX_FS_D0_SAMPLE_2D);
    v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXLD, OC, 0ul, 0ul, T, 0ul);
    CHECK(v9x_i9xx_fs_end(&fs, &encoded_len) == V9X_STATUS_OK);
    v9x_same(encoded, encoded_len, fixed, fixed_len);

    /* dcl T0; dcl S0; dcl T8; texld R0, S0, T0; mul oC, R0, T8 */
    CHECK(v9x_i9xx_build_modulate_program(fixed, 64ul, &fixed_len) ==
          V9X_STATUS_OK);
    v9x_i9xx_fs_begin(&fs, encoded, 64ul);
    v9x_i9xx_fs_dcl_t(&fs, 0ul, XYZW);
    v9x_i9xx_fs_dcl_s(&fs, 0ul, V9X_I9XX_FS_D0_SAMPLE_2D);
    v9x_i9xx_fs_dcl_t(&fs, V9X_I9XX_FS_T_DIFFUSE, XYZW);
    v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXLD, R, 0ul, 0ul, T, 0ul);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MUL, OC, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(R, 0ul),
                    V9X_I9XX_FS_SRC(T, V9X_I9XX_FS_T_DIFFUSE), 0ul);
    CHECK(v9x_i9xx_fs_end(&fs, &encoded_len) == V9X_STATUS_OK);
    v9x_same(encoded, encoded_len, fixed, fixed_len);
}

/* ------------------------------------------------------------------ */
/* Fields the fixed programs never set                                 */
/* ------------------------------------------------------------------ */

static void test_field_placement(void)
{
    v9x_u32 words[PROGRAM_CAPACITY];
    v9x_u32 length;
    v9x_u32 src0;
    v9x_u32 src1;
    v9x_u32 src2;
    struct v9x_i9xx_fs fs;
    const v9x_u32 *mad;
    const v9x_u32 *tex;

    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_dcl_t(&fs, 2ul, V9X_I9XX_FS_MASK_XYZ);
    v9x_i9xx_fs_dcl_s(&fs, 7ul, V9X_I9XX_FS_D0_SAMPLE_CUBE);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, R, 1ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(T, 2ul), 0ul, 0ul);

    /* mad_sat R3.xz, C5.(1, -w, 0, x), T2.(y, x, -w, -z), -R1.x yzw */
    src0 = v9x_i9xx_fs_swizzle(V9X_I9XX_FS_SRC(CONST, 5ul),
                               V9X_I9XX_FS_SEL_ONE, V9X_I9XX_FS_SEL_W,
                               V9X_I9XX_FS_SEL_ZERO, V9X_I9XX_FS_SEL_X);
    src0 = v9x_i9xx_fs_negate(src0, V9X_I9XX_FS_MASK_Y);
    src1 = v9x_i9xx_fs_swizzle(V9X_I9XX_FS_SRC(T, 2ul),
                               V9X_I9XX_FS_SEL_Y, V9X_I9XX_FS_SEL_X,
                               V9X_I9XX_FS_SEL_W, V9X_I9XX_FS_SEL_Z);
    src1 = v9x_i9xx_fs_negate(src1, V9X_I9XX_FS_MASK_Z |
                                    V9X_I9XX_FS_MASK_W);
    src2 = v9x_i9xx_fs_negate(V9X_I9XX_FS_SRC(R, 1ul), V9X_I9XX_FS_MASK_X);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MAD, R, 3ul,
                    V9X_I9XX_FS_MASK_X | V9X_I9XX_FS_MASK_Z, V9X_TRUE,
                    src0, src1, src2);

    /* texldp U2, S7, T2 */
    v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXLDP, U, 2ul, 7ul, T, 2ul);
    CHECK(v9x_i9xx_fs_end(&fs, &length) == V9X_STATUS_OK);
    CHECK(length == 1ul + 5ul * 3ul);

    /* _3DSTATE_PIXEL_SHADER_PROGRAM, (CMD_3D | 0x1d << 24 | 0x5 << 16),
     * length dwords - 2. */
    CHECK(words[0] == (0x7d050000ul | 14ul));

    /* D0_DCL 0x19 << 24, type T 1 << 19, nr 2 << 14, xyz 7 << 10. */
    CHECK(words[1] == (0x19000000ul | (1ul << 19) | (2ul << 14) |
                       (7ul << 10)));
    CHECK(words[2] == 0ul && words[3] == 0ul);

    /* Cube sampler: D0_SAMPLE_TYPE_CUBE 1 << 22, type S 3, nr 7, and no
     * channel mask. */
    CHECK(words[4] == (0x19000000ul | (1ul << 22) | (3ul << 19) |
                       (7ul << 14)));

    mad = &words[10];
    /* A0: MAD 4 << 24, saturate 1 << 22, dest R (0) 3 << 14, mask
     * x|z 5 << 10, src0 type CONST 2 << 7, nr 5 << 2. */
    CHECK(mad[0] == (0x04000000ul | (1ul << 22) | (0ul << 19) |
                     (3ul << 14) | (5ul << 10) | (2ul << 7) | (5ul << 2)));
    /* A1: src0 x ONE 5 << 28, y W 3 << 24 negated 1 << 27, z ZERO
     * 4 << 20, w X 0 << 16; src1 type T 1 << 13, nr 2 << 8, x Y 1 << 4,
     * y X 0 << 0. */
    CHECK(mad[1] == ((5ul << 28) | (3ul << 24) | (1ul << 27) |
                     (4ul << 20) | (0ul << 16) | (1ul << 13) | (2ul << 8) |
                     (1ul << 4) | (0ul << 0)));
    /* A2: src1 z W 3 << 28 negated 1 << 31, w Z 2 << 24 negated 1 << 27;
     * src2 type R 0 << 21, nr 1 << 16, x X 0 << 12 negated 1 << 15, y Y
     * 1 << 8, z Z 2 << 4, w W 3 << 0. */
    CHECK(mad[2] == ((3ul << 28) | (1ul << 31) | (2ul << 24) |
                     (1ul << 27) | (0ul << 21) | (1ul << 16) |
                     (0ul << 12) | (1ul << 15) | (1ul << 8) | (2ul << 4) |
                     (3ul << 0)));

    tex = &words[13];
    /* T0: TEXLDP 0x16 << 24, dest U 6 << 19, nr 2 << 14, sampler 7.
     * T1: address type T 1 << 24, nr 2 << 17. T2 zero. */
    CHECK(tex[0] == (0x16000000ul | (6ul << 19) | (2ul << 14) | 7ul));
    CHECK(tex[1] == ((1ul << 24) | (2ul << 17)));
    CHECK(tex[2] == 0ul);
}

/* A swizzle of a swizzle picks through the first, negates included. */
static void test_swizzle_composes(void)
{
    v9x_u32 a;
    v9x_u32 b;

    a = v9x_i9xx_fs_swizzle(V9X_I9XX_FS_SRC(R, 4ul),
                            V9X_I9XX_FS_SEL_W, V9X_I9XX_FS_SEL_Z,
                            V9X_I9XX_FS_SEL_Y, V9X_I9XX_FS_SEL_ONE);
    a = v9x_i9xx_fs_negate(a, V9X_I9XX_FS_MASK_X);
    /* (-w, z, y, 1) then (y, x, w, x): (z, -w, 1, -w) */
    b = v9x_i9xx_fs_swizzle(a, V9X_I9XX_FS_SEL_Y, V9X_I9XX_FS_SEL_X,
                            V9X_I9XX_FS_SEL_W, V9X_I9XX_FS_SEL_X);
    CHECK(b == v9x_i9xx_fs_negate(
                   v9x_i9xx_fs_swizzle(V9X_I9XX_FS_SRC(R, 4ul),
                                       V9X_I9XX_FS_SEL_Z, V9X_I9XX_FS_SEL_W,
                                       V9X_I9XX_FS_SEL_ONE,
                                       V9X_I9XX_FS_SEL_W),
                   V9X_I9XX_FS_MASK_Y | V9X_I9XX_FS_MASK_W));
    /* Picking ZERO drops the negate that channel had. */
    b = v9x_i9xx_fs_swizzle(a, V9X_I9XX_FS_SEL_ZERO, V9X_I9XX_FS_SEL_Y,
                            V9X_I9XX_FS_SEL_Z, V9X_I9XX_FS_SEL_W);
    CHECK(b == v9x_i9xx_fs_swizzle(V9X_I9XX_FS_SRC(R, 4ul),
                                   V9X_I9XX_FS_SEL_ZERO, V9X_I9XX_FS_SEL_Z,
                                   V9X_I9XX_FS_SEL_Y, V9X_I9XX_FS_SEL_ONE));
}

/* ------------------------------------------------------------------ */
/* Refusals                                                            */
/* ------------------------------------------------------------------ */

static v9x_status v9x_end(struct v9x_i9xx_fs *fs)
{
    v9x_u32 length = 99ul;
    v9x_status status = v9x_i9xx_fs_end(fs, &length);

    if (status != V9X_STATUS_OK) {
        CHECK(length == 0ul);
    }
    return status;
}

static void test_one_constant_register(void)
{
    v9x_u32 words[PROGRAM_CAPACITY];
    struct v9x_i9xx_fs fs;

    /* The same constant twice is one register: allowed. */
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MAD, R, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(CONST, 1ul),
                    v9x_i9xx_fs_negate(V9X_I9XX_FS_SRC(CONST, 1ul), XYZW),
                    V9X_I9XX_FS_SRC(CONST, 1ul));
    CHECK(v9x_end(&fs) == V9X_STATUS_OK);

    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_ADD, R, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(CONST, 1ul),
                    V9X_I9XX_FS_SRC(CONST, 2ul), 0ul);
    CHECK(v9x_end(&fs) == V9X_STATUS_UNSUPPORTED);

    /* A third source an opcode does not take is not read. */
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_ADD, R, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(CONST, 1ul),
                    V9X_I9XX_FS_SRC(CONST, 1ul),
                    V9X_I9XX_FS_SRC(CONST, 2ul));
    CHECK(v9x_end(&fs) == V9X_STATUS_OK);
}

static void test_counts(void)
{
    v9x_u32 words[PROGRAM_CAPACITY];
    struct v9x_i9xx_fs fs;
    v9x_u32 index;

    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    for (index = 0ul; index < V9X_I9XX_FS_MAX_ALU; ++index) {
        v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, R, 0ul, XYZW, V9X_FALSE,
                        V9X_I9XX_FS_SRC(CONST, 0ul), 0ul, 0ul);
    }
    CHECK(fs.failed == V9X_STATUS_OK);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, R, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(CONST, 0ul), 0ul, 0ul);
    CHECK(v9x_end(&fs) == V9X_STATUS_UNSUPPORTED);

    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_dcl_t(&fs, 0ul, XYZW);
    v9x_i9xx_fs_dcl_s(&fs, 0ul, V9X_I9XX_FS_D0_SAMPLE_2D);
    for (index = 0ul; index < V9X_I9XX_FS_MAX_TEX; ++index) {
        v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXLD, R, index % 16ul, 0ul,
                        T, 0ul);
    }
    CHECK(fs.failed == V9X_STATUS_OK);
    v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXLD, R, 0ul, 0ul, T, 0ul);
    CHECK(v9x_end(&fs) == V9X_STATUS_UNSUPPORTED);

    /* Every T and every sampler is 27 declarations, the limit; a
     * repeated declaration is not emitted again. */
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    for (index = 0ul; index < V9X_I9XX_FS_T_COUNT; ++index) {
        v9x_i9xx_fs_dcl_t(&fs, index, XYZW);
    }
    for (index = 0ul; index < V9X_I9XX_FS_S_COUNT; ++index) {
        v9x_i9xx_fs_dcl_s(&fs, index, V9X_I9XX_FS_D0_SAMPLE_2D);
    }
    v9x_i9xx_fs_dcl_t(&fs, 0ul, XYZW);
    CHECK(fs.dcl == V9X_I9XX_FS_MAX_DCL);
    CHECK(v9x_end(&fs) == V9X_STATUS_OK);

    /* Capacity. */
    v9x_i9xx_fs_begin(&fs, words, 4ul);
    v9x_i9xx_fs_dcl_t(&fs, 0ul, XYZW);
    CHECK(fs.failed == V9X_STATUS_OK);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, OC, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(T, 0ul), 0ul, 0ul);
    CHECK(v9x_end(&fs) == V9X_STATUS_INSUFFICIENT_MEMORY);
}

/* Mesa's rule (i915_fpc_emit.c): a texture read whose address is an R
 * written in the current phase starts a new one, as does a texture
 * load into oC; at most four. */
static void test_phases(void)
{
    v9x_u32 words[PROGRAM_CAPACITY];
    struct v9x_i9xx_fs fs;
    v9x_u32 index;

    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_dcl_t(&fs, 0ul, XYZW);
    v9x_i9xx_fs_dcl_s(&fs, 0ul, V9X_I9XX_FS_D0_SAMPLE_2D);
    v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXLD, R, 0ul, 0ul, T, 0ul);
    CHECK(fs.phases == 1ul);
    for (index = 1ul; index < V9X_I9XX_FS_MAX_PHASES; ++index) {
        v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXLD, R, index, 0ul,
                        R, index - 1ul);
    }
    CHECK(fs.phases == 4ul);
    CHECK(fs.failed == V9X_STATUS_OK);
    v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXLD, R, 4ul, 0ul, R, 3ul);
    CHECK(v9x_end(&fs) == V9X_STATUS_UNSUPPORTED);

    /* An address written in an earlier phase does not start one. */
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_dcl_t(&fs, 0ul, XYZW);
    v9x_i9xx_fs_dcl_s(&fs, 0ul, V9X_I9XX_FS_D0_SAMPLE_2D);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, R, 1ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(T, 0ul), 0ul, 0ul);
    v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXLD, R, 2ul, 0ul, R, 1ul);
    CHECK(fs.phases == 2ul);
    v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXLD, R, 3ul, 0ul, R, 1ul);
    CHECK(fs.phases == 2ul);
    /* A texel straight into oC does. */
    v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXLD, OC, 0ul, 0ul, T, 0ul);
    CHECK(fs.phases == 3ul);
    CHECK(v9x_end(&fs) == V9X_STATUS_OK);

    /* U registers do not survive a phase. */
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_dcl_t(&fs, 0ul, XYZW);
    v9x_i9xx_fs_dcl_s(&fs, 0ul, V9X_I9XX_FS_D0_SAMPLE_2D);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, U, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(T, 0ul), 0ul, 0ul);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, R, 1ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(U, 0ul), 0ul, 0ul);
    CHECK(fs.failed == V9X_STATUS_OK);
    v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXLD, R, 2ul, 0ul, R, 1ul);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_ADD, R, 3ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(U, 0ul), V9X_I9XX_FS_SRC(R, 2ul), 0ul);
    CHECK(v9x_end(&fs) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_malformed(void)
{
    v9x_u32 words[PROGRAM_CAPACITY];
    struct v9x_i9xx_fs fs;

    /* T read undeclared. */
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, OC, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(T, 3ul), 0ul, 0ul);
    CHECK(v9x_end(&fs) == V9X_STATUS_INVALID_ARGUMENT);

    /* R read unwritten. */
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, OC, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(R, 3ul), 0ul, 0ul);
    CHECK(v9x_end(&fs) == V9X_STATUS_INVALID_ARGUMENT);

    /* Sampler undeclared. */
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_dcl_t(&fs, 0ul, XYZW);
    v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXLD, R, 0ul, 1ul, T, 0ul);
    CHECK(v9x_end(&fs) == V9X_STATUS_INVALID_ARGUMENT);

    /* ...but TEXKILL takes none. */
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_dcl_t(&fs, 0ul, XYZW);
    v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXKILL, U, 0ul, 0ul, T, 0ul);
    CHECK(v9x_end(&fs) == V9X_STATUS_OK);

    /* A declaration after an instruction. */
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, R, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(CONST, 0ul), 0ul, 0ul);
    v9x_i9xx_fs_dcl_t(&fs, 0ul, XYZW);
    CHECK(v9x_end(&fs) == V9X_STATUS_INVALID_ARGUMENT);

    /* xz is not a legal declaration mask. */
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_dcl_t(&fs, 0ul, V9X_I9XX_FS_MASK_X | V9X_I9XX_FS_MASK_Z);
    CHECK(v9x_end(&fs) == V9X_STATUS_INVALID_ARGUMENT);

    /* A constant or T as a destination; an empty write mask. */
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, CONST, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(CONST, 0ul), 0ul, 0ul);
    CHECK(v9x_end(&fs) == V9X_STATUS_INVALID_ARGUMENT);
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, R, 0ul, 0ul, V9X_FALSE,
                    V9X_I9XX_FS_SRC(CONST, 0ul), 0ul, 0ul);
    CHECK(v9x_end(&fs) == V9X_STATUS_INVALID_ARGUMENT);

    /* A scalar operation on a source that is not one channel. */
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_RCP, R, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(CONST, 0ul), 0ul, 0ul);
    CHECK(v9x_end(&fs) == V9X_STATUS_INVALID_ARGUMENT);
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_RCP, R, 0ul, XYZW, V9X_FALSE,
                    v9x_i9xx_fs_negate(
                        v9x_i9xx_fs_swizzle(V9X_I9XX_FS_SRC(CONST, 0ul),
                                            V9X_I9XX_FS_SEL_Z,
                                            V9X_I9XX_FS_SEL_Z,
                                            V9X_I9XX_FS_SEL_Z,
                                            V9X_I9XX_FS_SEL_Z),
                        V9X_I9XX_FS_MASK_X),
                    0ul, 0ul);
    CHECK(v9x_end(&fs) == V9X_STATUS_INVALID_ARGUMENT);
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_RCP, R, 0ul, XYZW, V9X_FALSE,
                    v9x_i9xx_fs_swizzle(V9X_I9XX_FS_SRC(CONST, 0ul),
                                        V9X_I9XX_FS_SEL_Z, V9X_I9XX_FS_SEL_Z,
                                        V9X_I9XX_FS_SEL_Z,
                                        V9X_I9XX_FS_SEL_Z),
                    0ul, 0ul);
    CHECK(v9x_end(&fs) == V9X_STATUS_OK);

    /* An unknown opcode. */
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_alu(&fs, 0x15000000ul, R, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(CONST, 0ul), 0ul, 0ul);
    CHECK(v9x_end(&fs) == V9X_STATUS_INVALID_ARGUMENT);

    /* The first failure is the one returned; later calls do nothing. */
    v9x_i9xx_fs_begin(&fs, words, 4ul);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, OC, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(T, 3ul), 0ul, 0ul);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, OC, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(CONST, 0ul), 0ul, 0ul);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, OC, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(CONST, 0ul), 0ul, 0ul);
    CHECK(v9x_end(&fs) == V9X_STATUS_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* The model, on the fixed programs                                    */
/* ------------------------------------------------------------------ */

static int v9x_run_fixed(struct v9x_i9xx_fs_sim *sim, const v9x_u32 *words,
                         v9x_u32 length)
{
    int ok = v9x_i9xx_fs_sim_run(sim, words, length);

    if (!ok) {
        printf("  model error at dword %lu\n", (unsigned long)sim->error_at);
    }
    return ok;
}

static void test_model_fixed_programs(void)
{
    v9x_u32 words[64];
    v9x_u32 length;
    struct v9x_i9xx_fs_sim sim;
    struct v9x_sample_log log;
    const float f = 0.25f;

    v9x_sim_init(&sim, &log);
    v9x_set4(sim.t[0], 0.5f, 0.75f, 0.125f, 1.0f);
    v9x_set4(sim.t[8], 0.5f, 0.25f, 1.0f, 0.75f);
    v9x_set4(sim.t[9], 0.0f, 0.0f, 0.0f, f);
    v9x_set4(sim.c[0], 0.2f, 0.4f, 0.6f, 1.0f);

    /* Untextured: the diffuse colour. */
    CHECK(v9x_i9xx_build_fragment_program(words, 64ul, &length) ==
          V9X_STATUS_OK);
    v9x_i9xx_fs_sim_reset(&sim);
    CHECK(v9x_run_fixed(&sim, words, length));
    CHECK(v9x_near4(sim.oc, 0.5f, 0.25f, 1.0f, 0.75f));

    /* DECAL: the texel, sampled at T0 from S0, 2D. */
    CHECK(v9x_i9xx_build_texture_program(V9X_I9XX_TEXPROG_DECAL, words,
                                         64ul, &length) == V9X_STATUS_OK);
    v9x_i9xx_fs_sim_reset(&sim);
    CHECK(v9x_run_fixed(&sim, words, length));
    CHECK(sim.samples == 1ul && log.sampler == 0ul &&
          log.sample_type == 0ul && log.opcode == 0x15ul);
    CHECK(v9x_near4(sim.oc, 0.5f, 0.75f, 0.125f, 0.25f));

    /* MODULATE: texel times diffuse, alpha too. */
    CHECK(v9x_i9xx_build_texture_program(V9X_I9XX_TEXPROG_MODULATE_ALPHA,
                                         words, 64ul, &length) ==
          V9X_STATUS_OK);
    v9x_i9xx_fs_sim_reset(&sim);
    CHECK(v9x_run_fixed(&sim, words, length));
    CHECK(v9x_near4(sim.oc, 0.25f, 0.1875f, 0.125f, 0.1875f));

    /* MODULATE keeping the texel's alpha, then the diffuse alpha. */
    CHECK(v9x_i9xx_build_texture_program(
              V9X_I9XX_TEXPROG_MODULATE_TEXALPHA, words, 64ul, &length) ==
          V9X_STATUS_OK);
    v9x_i9xx_fs_sim_reset(&sim);
    CHECK(v9x_run_fixed(&sim, words, length));
    CHECK(v9x_near4(sim.oc, 0.25f, 0.1875f, 0.125f, 0.25f));
    CHECK(v9x_i9xx_build_texture_program(
              V9X_I9XX_TEXPROG_MODULATE_DIFFALPHA, words, 64ul, &length) ==
          V9X_STATUS_OK);
    v9x_i9xx_fs_sim_reset(&sim);
    CHECK(v9x_run_fixed(&sim, words, length));
    CHECK(v9x_near4(sim.oc, 0.25f, 0.1875f, 0.125f, 0.75f));

    /* Fog: colour f + fog (1 - f), f in T9.w, the fog colour in C0,
     * alpha unfogged. */
    CHECK(v9x_i9xx_build_fog_program(V9X_I9XX_FOGPROG_UNTEXTURED, words,
                                     64ul, &length) == V9X_STATUS_OK);
    v9x_i9xx_fs_sim_reset(&sim);
    CHECK(v9x_run_fixed(&sim, words, length));
    CHECK(v9x_near4(sim.oc, 0.5f * f + 0.2f * (1.0f - f),
                    0.25f * f + 0.4f * (1.0f - f),
                    1.0f * f + 0.6f * (1.0f - f), 0.75f));
    CHECK(v9x_i9xx_build_fog_program(V9X_I9XX_TEXPROG_MODULATE_ALPHA,
                                     words, 64ul, &length) ==
          V9X_STATUS_OK);
    v9x_i9xx_fs_sim_reset(&sim);
    CHECK(v9x_run_fixed(&sim, words, length));
    CHECK(v9x_near4(sim.oc, 0.25f * f + 0.2f * (1.0f - f),
                    0.1875f * f + 0.4f * (1.0f - f),
                    0.125f * f + 0.6f * (1.0f - f), 0.1875f));
}

/* ------------------------------------------------------------------ */
/* The model, on encoded programs                                      */
/* ------------------------------------------------------------------ */

/* Run `op` on C0 (and C0's swizzles as further sources) into R0, then
 * move R0 to oC. Returns the model's success. */
static int v9x_run_one(struct v9x_i9xx_fs_sim *sim, v9x_u32 opcode,
                       v9x_u32 mask, v9x_u32 saturate,
                       v9x_u32 src0, v9x_u32 src1, v9x_u32 src2)
{
    v9x_u32 words[PROGRAM_CAPACITY];
    v9x_u32 length;
    struct v9x_i9xx_fs fs;

    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_dcl_t(&fs, 1ul, XYZW);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, R, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(CONST, 7ul), 0ul, 0ul);
    v9x_i9xx_fs_alu(&fs, opcode, R, 0ul, mask, saturate, src0, src1, src2);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, OC, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(R, 0ul), 0ul, 0ul);
    if (v9x_i9xx_fs_end(&fs, &length) != V9X_STATUS_OK) {
        return 0;
    }
    v9x_i9xx_fs_sim_reset(sim);
    return v9x_i9xx_fs_sim_run(sim, words, length);
}

static v9x_u32 v9x_rep(v9x_u32 src, v9x_u32 sel)
{
    return v9x_i9xx_fs_swizzle(src, sel, sel, sel, sel);
}

static void test_model_arithmetic(void)
{
    struct v9x_i9xx_fs_sim sim;
    struct v9x_sample_log log;
    v9x_u32 a = V9X_I9XX_FS_SRC(T, 1ul);
    v9x_u32 b = V9X_I9XX_FS_SRC(CONST, 3ul);

    v9x_sim_init(&sim, &log);
    v9x_set4(sim.t[1], 0.5f, -0.25f, 2.0f, 4.0f);
    v9x_set4(sim.c[3], 0.25f, 0.5f, -1.0f, 0.75f);
    v9x_set4(sim.c[7], 9.0f, 9.0f, 9.0f, 9.0f);

    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_ADD, XYZW, V9X_FALSE, a, b, 0ul));
    CHECK(v9x_near4(sim.oc, 0.75f, 0.25f, 1.0f, 4.75f));
    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_MUL, XYZW, V9X_FALSE, a, b, 0ul));
    CHECK(v9x_near4(sim.oc, 0.125f, -0.125f, -2.0f, 3.0f));
    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_MAD, XYZW, V9X_FALSE, a, b, a));
    CHECK(v9x_near4(sim.oc, 0.625f, -0.375f, 0.0f, 7.0f));
    /* dp3: 0.125 - 0.125 - 2 = -2; dp4 adds 3. */
    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_DP3, XYZW, V9X_FALSE, a, b, 0ul));
    CHECK(v9x_near4(sim.oc, -2.0f, -2.0f, -2.0f, -2.0f));
    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_DP4, XYZW, V9X_FALSE, a, b, 0ul));
    CHECK(v9x_near4(sim.oc, 1.0f, 1.0f, 1.0f, 1.0f));
    /* dp2add: a.x b.x + a.y b.y + c = 0.125 - 0.125 + 0.5, c one
     * channel replicated (refused otherwise: which channel the hardware
     * adds is not known). */
    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_DP2ADD, XYZW, V9X_FALSE,
                      a, b, v9x_rep(a, V9X_I9XX_FS_SEL_X)));
    CHECK(v9x_near4(sim.oc, 0.5f, 0.5f, 0.5f, 0.5f));
    CHECK(!v9x_run_one(&sim, V9X_I9XX_FS_A0_DP2ADD, XYZW, V9X_FALSE,
                       a, b, a));
    /* cmp: a >= 0 ? a : b, per channel. */
    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_CMP, XYZW, V9X_FALSE, a, a, b));
    CHECK(v9x_near4(sim.oc, 0.5f, 0.5f, 2.0f, 4.0f));
    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_MIN, XYZW, V9X_FALSE, a, b, 0ul));
    CHECK(v9x_near4(sim.oc, 0.25f, -0.25f, -1.0f, 0.75f));
    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_MAX, XYZW, V9X_FALSE, a, b, 0ul));
    CHECK(v9x_near4(sim.oc, 0.5f, 0.5f, 2.0f, 4.0f));
    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_SGE, XYZW, V9X_FALSE, a, b, 0ul));
    CHECK(v9x_near4(sim.oc, 1.0f, 0.0f, 1.0f, 1.0f));
    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_SLT, XYZW, V9X_FALSE, a, b, 0ul));
    CHECK(v9x_near4(sim.oc, 0.0f, 1.0f, 0.0f, 0.0f));
    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_FRC, XYZW, V9X_FALSE,
                      a, 0ul, 0ul));
    CHECK(v9x_near4(sim.oc, 0.5f, 0.75f, 0.0f, 0.0f));
    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_FLR, XYZW, V9X_FALSE,
                      a, 0ul, 0ul));
    CHECK(v9x_near4(sim.oc, 0.0f, -1.0f, 2.0f, 4.0f));
    /* The scalar operations, on a.w replicated. */
    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_RCP, XYZW, V9X_FALSE,
                      v9x_rep(a, V9X_I9XX_FS_SEL_W), 0ul, 0ul));
    CHECK(v9x_near4(sim.oc, 0.25f, 0.25f, 0.25f, 0.25f));
    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_RSQ, XYZW, V9X_FALSE,
                      v9x_rep(a, V9X_I9XX_FS_SEL_W), 0ul, 0ul));
    CHECK(v9x_near4(sim.oc, 0.5f, 0.5f, 0.5f, 0.5f));
    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_EXP, XYZW, V9X_FALSE,
                      v9x_rep(a, V9X_I9XX_FS_SEL_Z), 0ul, 0ul));
    CHECK(v9x_near4(sim.oc, 4.0f, 4.0f, 4.0f, 4.0f));
    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_LOG, XYZW, V9X_FALSE,
                      v9x_rep(a, V9X_I9XX_FS_SEL_W), 0ul, 0ul));
    CHECK(v9x_near4(sim.oc, 2.0f, 2.0f, 2.0f, 2.0f));

    /* Swizzle, ZERO, ONE and negate reach the operation. */
    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_MOV, XYZW, V9X_FALSE,
                      v9x_i9xx_fs_negate(
                          v9x_i9xx_fs_swizzle(a, V9X_I9XX_FS_SEL_W,
                                              V9X_I9XX_FS_SEL_ONE,
                                              V9X_I9XX_FS_SEL_ZERO,
                                              V9X_I9XX_FS_SEL_X),
                          V9X_I9XX_FS_MASK_X | V9X_I9XX_FS_MASK_Y),
                      0ul, 0ul));
    CHECK(v9x_near4(sim.oc, -4.0f, -1.0f, 0.0f, 0.5f));

    /* Saturate and the write mask: R0 held C7's nines. */
    CHECK(v9x_run_one(&sim, V9X_I9XX_FS_A0_MOV,
                      V9X_I9XX_FS_MASK_Y | V9X_I9XX_FS_MASK_W, V9X_TRUE,
                      a, 0ul, 0ul));
    CHECK(v9x_near4(sim.oc, 9.0f, 0.0f, 9.0f, 1.0f));
}

static void test_model_texture(void)
{
    v9x_u32 words[PROGRAM_CAPACITY];
    v9x_u32 length;
    struct v9x_i9xx_fs fs;
    struct v9x_i9xx_fs_sim sim;
    struct v9x_sample_log log;

    v9x_sim_init(&sim, &log);
    v9x_set4(sim.t[2], 0.5f, 0.25f, 1.0f, 2.0f);

    /* texldp from a cube sampler: x, y, z divided by w. */
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_dcl_t(&fs, 2ul, XYZW);
    v9x_i9xx_fs_dcl_s(&fs, 5ul, V9X_I9XX_FS_D0_SAMPLE_CUBE);
    v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXLDP, R, 0ul, 5ul, T, 2ul);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, OC, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(R, 0ul), 0ul, 0ul);
    CHECK(v9x_i9xx_fs_end(&fs, &length) == V9X_STATUS_OK);
    v9x_i9xx_fs_sim_reset(&sim);
    CHECK(v9x_i9xx_fs_sim_run(&sim, words, length));
    CHECK(log.sampler == 5ul && log.sample_type == 1ul &&
          log.opcode == 0x16ul);
    CHECK(v9x_near4(sim.oc, 0.25f, 0.125f, 0.5f, 0.25f + 5.0f * 0.0625f));

    /* A dependent read: the address from R. */
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_dcl_t(&fs, 2ul, XYZW);
    v9x_i9xx_fs_dcl_s(&fs, 1ul, V9X_I9XX_FS_D0_SAMPLE_2D);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_ADD, R, 3ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(T, 2ul), V9X_I9XX_FS_SRC(T, 2ul), 0ul);
    v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXLD, OC, 0ul, 1ul, R, 3ul);
    CHECK(v9x_i9xx_fs_end(&fs, &length) == V9X_STATUS_OK);
    v9x_i9xx_fs_sim_reset(&sim);
    CHECK(v9x_i9xx_fs_sim_run(&sim, words, length));
    CHECK(v9x_near4(sim.oc, 1.0f, 0.5f, 2.0f, 0.3125f));

    /* texkill: any of the four channels negative kills. */
    v9x_i9xx_fs_begin(&fs, words, PROGRAM_CAPACITY);
    v9x_i9xx_fs_dcl_t(&fs, 2ul, XYZW);
    v9x_i9xx_fs_tex(&fs, V9X_I9XX_T0_TEXKILL, U, 0ul, 0ul, T, 2ul);
    v9x_i9xx_fs_alu(&fs, V9X_I9XX_FS_A0_MOV, OC, 0ul, XYZW, V9X_FALSE,
                    V9X_I9XX_FS_SRC(T, 2ul), 0ul, 0ul);
    CHECK(v9x_i9xx_fs_end(&fs, &length) == V9X_STATUS_OK);
    v9x_i9xx_fs_sim_reset(&sim);
    CHECK(v9x_i9xx_fs_sim_run(&sim, words, length));
    CHECK(!sim.killed);
    CHECK(sim.samples == 0ul);
    sim.t[2][3] = -0.5f;
    v9x_i9xx_fs_sim_reset(&sim);
    CHECK(v9x_i9xx_fs_sim_run(&sim, words, length));
    CHECK(sim.killed);
}

/* The model refuses what the encoder refuses, read from raw dwords. */
static void test_model_refusals(void)
{
    v9x_u32 words[8];
    struct v9x_i9xx_fs_sim sim;
    struct v9x_sample_log log;

    v9x_sim_init(&sim, &log);

    /* mov oC, T3 with no declaration. */
    words[0] = 0x7d050000ul | 2ul;
    words[1] = 0x02000000ul | (4ul << 19) | (15ul << 10) | (1ul << 7) |
               (3ul << 2);
    words[2] = 0x01230000ul;
    words[3] = 0ul;
    CHECK(!v9x_i9xx_fs_sim_run(&sim, words, 4ul));
    CHECK(sim.error && sim.error_at == 1ul);

    /* add R0, C1, C2: two constant registers. */
    words[1] = 0x01000000ul | (15ul << 10) | (2ul << 7) | (1ul << 2);
    words[2] = 0x01230000ul | (2ul << 13) | (2ul << 8) | 0x01ul;
    words[3] = 0x23000000ul;
    v9x_i9xx_fs_sim_reset(&sim);
    CHECK(!v9x_i9xx_fs_sim_run(&sim, words, 4ul));

    /* A header whose length disagrees with the dwords given. */
    words[0] = 0x7d050000ul | 5ul;
    v9x_i9xx_fs_sim_reset(&sim);
    CHECK(!v9x_i9xx_fs_sim_run(&sim, words, 4ul));
}

unsigned int v9x_run_i9xx_fs_tests(void)
{
    failures = 0u;
    test_reproduces_fixed_programs();
    test_field_placement();
    test_swizzle_composes();
    test_one_constant_register();
    test_counts();
    test_phases();
    test_malformed();
    test_model_fixed_programs();
    test_model_arithmetic();
    test_model_texture();
    test_model_refusals();
    return failures;
}
