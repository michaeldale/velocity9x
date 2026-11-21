#include <stdio.h>

#include "velocity9x/r500_fp.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/*
 * Golden words: printed by a scratch program compiled against Mesa 22.3's
 * src\gallium\drivers\r300\r300_reg.h, OR-ing the R500_* defines in the
 * shapes xf86-video-ati 6.14.6's R500 composite builds
 * (radeon_exa_render.c:1975-2130), 2026-11-21. The colour program is the
 * one the first-draw probe ran on M-5CAPM3
 * (docs\decisions\2026-11-21-rv515-3d-first-draw.md).
 */
static const v9x_u32 xf86_single_tex[6] = {
    0x00187807ul, 0x06400000ul, 0xe4000400ul, 0ul, 0ul, 0ul
};
static const v9x_u32 xf86_single_out[6] = {
    0x001f8105ul, 0x00000400ul, 0x00000400ul, 0x00db2220ul, 0x00c8c000ul,
    0x20490000ul
};
static const v9x_u32 xf86_mask_out[6] = {
    0x001f8105ul, 0x00000400ul, 0x00000400ul, 0x00442220ul, 0x0068c000ul,
    0x20490000ul
};
static const v9x_u32 colour_out[6] = {
    0x001f8101ul, 0ul, 0ul, 0x00db0220ul, 0x00c0c000ul, 0x20490000ul
};
static const v9x_u32 texkill_t2[6] = {
    0x00000007ul, 0x02800000ul, 0x0000e402ul, 0ul, 0ul, 0ul
};
static const v9x_u32 sub_const_t2[6] = {
    0x00007800ul, 0x00040000ul, 0x00040000ul, 0x00db0a20ul, 0x00c2c020ul,
    0x1a221020ul
};

static int same(const v9x_u32 *actual, const v9x_u32 *expected)
{
    v9x_u32 index;

    for (index = 0ul; index < V9X_R500_FP_WORDS_PER_INST; ++index) {
        if (actual[index] != expected[index]) {
            printf("  word %lu: 0x%08lx, expected 0x%08lx\n",
                   index, actual[index], expected[index]);
            return 0;
        }
    }
    return 1;
}

/* The program the first draw ran: one OUT, temp0 * 1 + 0. */
static void test_colour(void)
{
    struct v9x_r500_fp fp;
    struct v9x_r500_alu alu;

    v9x_r500_fp_begin(&fp);
    v9x_r500_alu_init(&alu);
    alu.flags = V9X_R500_ALU_OUT | V9X_R500_ALU_CLAMP;
    v9x_r500_fp_alu(&fp, &alu);
    CHECK(v9x_r500_fp_end(&fp) == V9X_STATUS_OK);
    CHECK(fp.count == 1ul);
    CHECK(fp.max_temp == 0ul);
    CHECK(same(fp.words, colour_out));
}

/* xf86's single-texture composite: LD temp0 from temp0, then OUT
 * temp0 * 1, with the unused slot 1 naming temp 1 as xf86 writes it. */
static void test_xf86_single(void)
{
    struct v9x_r500_fp fp;
    struct v9x_r500_alu alu;

    v9x_r500_fp_begin(&fp);
    v9x_r500_fp_tex(&fp, V9X_R500_TEX_LD, 0ul, 0ul, 0ul);
    v9x_r500_alu_init(&alu);
    alu.rgb_addr[1] = 1ul;
    alu.alpha_addr[1] = 1ul;
    alu.rgb_slot[V9X_R500_OP_B] = 1ul;
    alu.alpha_slot[V9X_R500_OP_B] = 1ul;
    alu.flags = V9X_R500_ALU_OUT | V9X_R500_ALU_CLAMP |
                V9X_R500_ALU_TEX_WAIT;
    v9x_r500_fp_alu(&fp, &alu);
    CHECK(v9x_r500_fp_end(&fp) == V9X_STATUS_OK);
    CHECK(fp.count == 2ul);
    CHECK(fp.max_temp == 1ul);
    CHECK(same(&fp.words[0], xf86_single_tex));
    CHECK(same(&fp.words[6], xf86_single_out));
}

/* The mask variant's ALU: temp0 * temp1 (MODULATE's shape). */
static void test_xf86_mask(void)
{
    struct v9x_r500_fp fp;
    struct v9x_r500_alu alu;

    v9x_r500_fp_begin(&fp);
    v9x_r500_alu_init(&alu);
    alu.rgb_addr[1] = 1ul;
    alu.alpha_addr[1] = 1ul;
    alu.rgb_slot[V9X_R500_OP_B] = 1ul;
    alu.rgb_swizzle[V9X_R500_OP_B] = V9X_R500_RGB_IDENTITY;
    alu.alpha_slot[V9X_R500_OP_B] = 1ul;
    alu.alpha_swizzle[V9X_R500_OP_B] = V9X_R500_SWZ_A;
    alu.flags = V9X_R500_ALU_OUT | V9X_R500_ALU_CLAMP |
                V9X_R500_ALU_TEX_WAIT;
    v9x_r500_fp_alu(&fp, &alu);
    CHECK(v9x_r500_fp_end(&fp) == V9X_STATUS_OK);
    CHECK(same(fp.words, xf86_mask_out));
}

/* A kill and a temp write with a constant and negation: the fields the
 * xf86 shapes leave at zero. */
static void test_kill_and_const(void)
{
    struct v9x_r500_fp fp;
    struct v9x_r500_alu alu;

    v9x_r500_fp_begin(&fp);
    v9x_r500_fp_tex(&fp, V9X_R500_TEX_KILL, 0ul, 2ul, 0ul);
    v9x_r500_alu_init(&alu);
    alu.rgb_addr[1] = V9X_R500_ADDR_CONST;
    alu.alpha_addr[1] = V9X_R500_ADDR_CONST;
    alu.rgb_mod[V9X_R500_OP_A] = V9X_R500_MOD_NEG;
    alu.alpha_mod[V9X_R500_OP_A] = V9X_R500_MOD_NEG;
    alu.rgb_slot[V9X_R500_OP_C] = 1ul;
    alu.rgb_swizzle[V9X_R500_OP_C] = V9X_R500_RGB_IDENTITY;
    alu.alpha_slot[V9X_R500_OP_C] = 1ul;
    alu.alpha_swizzle[V9X_R500_OP_C] = V9X_R500_SWZ_A;
    alu.dest = 2ul;
    alu.flags = V9X_R500_ALU_WRITE_RGB | V9X_R500_ALU_WRITE_ALPHA;
    v9x_r500_fp_alu(&fp, &alu);
    CHECK(v9x_r500_fp_end(&fp) == V9X_STATUS_OK);
    /* The kill is not last: LAST goes on the ALU only. */
    CHECK(same(&fp.words[0], texkill_t2));
    CHECK(fp.words[6] == (sub_const_t2[0] | 0x00000100ul));
    CHECK(fp.words[7] == sub_const_t2[1]);
    CHECK(fp.words[8] == sub_const_t2[2]);
    CHECK(fp.words[9] == sub_const_t2[3]);
    CHECK(fp.words[10] == sub_const_t2[4]);
    CHECK(fp.words[11] == sub_const_t2[5]);
    CHECK(fp.max_temp == 2ul);
}

/* Out-of-range fields, an empty program and a full one are refused, and a
 * refusal sticks. */
static void test_refusals(void)
{
    struct v9x_r500_fp fp;
    struct v9x_r500_alu alu;
    v9x_u32 index;

    v9x_r500_fp_begin(&fp);
    CHECK(v9x_r500_fp_end(&fp) == V9X_STATUS_INVALID_STATE);

    v9x_r500_fp_begin(&fp);
    v9x_r500_alu_init(&alu);
    alu.rgb_slot[V9X_R500_OP_A] = 3ul;
    v9x_r500_fp_alu(&fp, &alu);
    CHECK(fp.status == V9X_STATUS_INVALID_ARGUMENT);
    v9x_r500_alu_init(&alu);
    v9x_r500_fp_alu(&fp, &alu);
    CHECK(fp.count == 0ul);
    CHECK(v9x_r500_fp_end(&fp) == V9X_STATUS_INVALID_ARGUMENT);

    v9x_r500_fp_begin(&fp);
    v9x_r500_alu_init(&alu);
    alu.rgb_swizzle[V9X_R500_OP_B] = V9X_R500_RGB_SWIZZLE(7ul, 0ul, 0ul);
    v9x_r500_fp_alu(&fp, &alu);
    CHECK(fp.status == V9X_STATUS_INVALID_ARGUMENT);

    v9x_r500_fp_begin(&fp);
    v9x_r500_alu_init(&alu);
    alu.rgb_addr[2] = 128ul;
    v9x_r500_fp_alu(&fp, &alu);
    CHECK(fp.status == V9X_STATUS_INVALID_ARGUMENT);

    v9x_r500_fp_begin(&fp);
    v9x_r500_fp_tex(&fp, 3ul, 0ul, 0ul, 0ul);
    CHECK(fp.status == V9X_STATUS_INVALID_ARGUMENT);

    v9x_r500_fp_begin(&fp);
    v9x_r500_alu_init(&alu);
    for (index = 0ul; index <= V9X_R500_FP_MAX_INSTRUCTIONS; ++index) {
        v9x_r500_fp_alu(&fp, &alu);
    }
    CHECK(fp.count == V9X_R500_FP_MAX_INSTRUCTIONS);
    CHECK(fp.status == V9X_STATUS_INSUFFICIENT_MEMORY);
}

unsigned int v9x_run_r500_fp_tests(void)
{
    failures = 0u;
    test_colour();
    test_xf86_single();
    test_xf86_mask();
    test_kill_and_const();
    test_refusals();
    return failures;
}
