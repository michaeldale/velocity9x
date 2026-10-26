#include <stdio.h>
#include <string.h>

#include "velocity9x/eg_shader.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* Linux v3.9 evergreen_blit_shaders.c, the little-endian words: the VS
 * and PS that drew byte-exact on M-BXMFA8's HD 5450
 * (docs\decisions\2026-10-27-cedar-first-3d-draw.md). */
static const v9x_u32 linux_blit_vs[16] = {
    0x00000004ul, 0x80800400ul, 0x0000a03cul, 0x95000688ul,
    0x00004000ul, 0x15200688ul, 0x00000000ul, 0x00000000ul,
    0x3c000000ul, 0x67961001ul, 0x00080000ul, 0x00000000ul,
    0x1c000000ul, 0x67961000ul, 0x00000008ul, 0x00000000ul
};

static const v9x_u32 linux_blit_ps[20] = {
    0x00000003ul, 0xa00c0000ul, 0x00000008ul, 0x80400000ul,
    0x00000000ul, 0x95200688ul, 0x00380400ul, 0x00146b10ul,
    0x00380000ul, 0x20146b10ul, 0x00380400ul, 0x40146b00ul,
    0x80380000ul, 0x60146b00ul, 0x00000000ul, 0x00000000ul,
    0x00000010ul, 0x000d1000ul, 0xb0800000ul, 0x00000000ul
};

static v9x_u32 shader[V9X_EG_SHADER_DWORDS_MAX];

static void reset(void)
{
    memset(shader, 0xcc, sizeof(shader));
}

static void test_blit(void)
{
    v9x_u32 count;
    v9x_u32 index;

    reset();
    count = v9x_eg_shader_blit_vs(shader);
    CHECK(count == 16ul);
    for (index = 0ul; index < 16ul; ++index) {
        CHECK(shader[index] == linux_blit_vs[index]);
    }
    reset();
    count = v9x_eg_shader_blit_ps(shader);
    CHECK(count == 20ul);
    for (index = 0ul; index < 20ul; ++index) {
        CHECK(shader[index] == linux_blit_ps[index]);
    }
}

static void test_gouraud_vs(void)
{
    v9x_u32 count;

    reset();
    count = v9x_eg_shader_gouraud_vs(shader);
    CHECK(count == 16ul);
    /* The control flow is the blit's: VC at dword 8 with two fetches,
     * POS0 from R1, PARAM0 from R0 and the end of the program. */
    CHECK(shader[0] == 0x00000004ul && shader[1] == 0x80800400ul);
    CHECK(shader[2] == 0x0000a03cul && shader[3] == 0x95000688ul);
    CHECK(shader[4] == 0x00004000ul && shader[5] == 0x15200688ul);
    CHECK(shader[6] == 0ul && shader[7] == 0ul);
    /* Fetch 1: R1 from offset 0, the whole 32-byte vertex counted and
     * mega fetch set, four floats, selects XYZW. */
    CHECK((shader[8] >> 26) == 31ul);
    CHECK((shader[9] & 0x7ful) == 1ul);
    CHECK(((shader[9] >> 22) & 0x3ful) == V9X_EG_FMT_32_32_32_32_FLOAT);
    CHECK(((shader[9] >> 9) & 0xffful) == 0x688ul);
    CHECK(shader[10] == 0x00080000ul);
    /* Fetch 2: R0 from offset 16, 16 bytes, no mega fetch. */
    CHECK((shader[12] >> 26) == 15ul);
    CHECK((shader[13] & 0x7ful) == 0ul);
    CHECK(((shader[13] >> 22) & 0x3ful) == V9X_EG_FMT_32_32_32_32_FLOAT);
    CHECK(shader[14] == 16ul);
}

static void test_gouraud_ps(void)
{
    v9x_u32 count;
    v9x_u32 slot;
    v9x_u32 w0;
    v9x_u32 w1;

    reset();
    count = v9x_eg_shader_gouraud_ps(shader);
    CHECK(count == 20ul);
    /* ALU clause at dword 4 (address 2), eight slots; then the colour
     * export of R0, done and the end of the program. */
    CHECK(shader[0] == 2ul);
    CHECK(shader[1] == (0x80000000ul | (8ul << 26) | (7ul << 18)));
    CHECK(shader[2] == 0ul && shader[3] == 0x95200688ul);
    for (slot = 0ul; slot < 8ul; ++slot) {
        w0 = shader[4ul + slot * 2ul];
        w1 = shader[5ul + slot * 2ul];
        /* INTERP_ZW for the first group, INTERP_XY for the second. */
        CHECK(((w1 >> 7) & 0x7fful) ==
              (slot < 4ul ? V9X_EG_ALU_INTERP_ZW : V9X_EG_ALU_INTERP_XY));
        /* Barycentric R0.y then R0.x, against PARAM0. */
        CHECK((w0 & 0x1fful) == 0ul);
        CHECK(((w0 >> 10) & 3ul) == 1ul - (slot & 1ul));
        CHECK(((w0 >> 13) & 0x1fful) == V9X_EG_ALU_SRC_PARAM);
        /* Destination R0, channel slot % 4; ZW writes z and w, XY writes
         * x and y; the last slot of each group ends it. */
        CHECK(((w1 >> 21) & 0x7ful) == 0ul);
        CHECK(((w1 >> 29) & 3ul) == (slot & 3ul));
        CHECK(((w1 >> 4) & 1ul) ==
              (slot < 4ul ? ((slot & 3ul) >= 2ul ? 1ul : 0ul) :
                            ((slot & 3ul) < 2ul ? 1ul : 0ul)));
        CHECK((w0 >> 31) == ((slot & 3ul) == 3ul ? 1ul : 0ul));
    }
}

static void test_modulate_vs(void)
{
    v9x_u32 count;

    reset();
    count = v9x_eg_shader_modulate_vs(shader);
    CHECK(count == 20ul);
    /* VC at dword 8 with three fetches; POS0 from R1 (EXPORT_DONE,
     * barrier), PARAM0 from R0 (EXPORT), PARAM1 from R2 (EXPORT_DONE,
     * end of program). */
    CHECK(shader[0] == 4ul && shader[1] == 0x80800800ul);
    CHECK(shader[2] == 0x0000a03cul && shader[3] == 0x95000688ul);
    CHECK(shader[4] == 0x00004000ul && shader[5] == 0x14c00688ul);
    CHECK(shader[6] == 0x00014001ul && shader[7] == 0x15200688ul);
    /* R1: the whole 40-byte vertex, mega fetch. R0: 16 bytes at 16. R2:
     * two floats at 32, z and w filled 0 and 1. */
    CHECK((shader[8] >> 26) == 39ul && shader[10] == 0x00080000ul);
    CHECK((shader[9] & 0x7ful) == 1ul);
    CHECK((shader[12] >> 26) == 15ul && shader[14] == 16ul);
    CHECK((shader[13] & 0x7ful) == 0ul);
    CHECK((shader[16] >> 26) == 7ul && shader[18] == 32ul);
    CHECK((shader[17] & 0x7ful) == 2ul);
    CHECK(((shader[17] >> 22) & 0x3ful) == V9X_EG_FMT_32_32_FLOAT);
    CHECK(((shader[17] >> 9) & 0xffful) ==
          (V9X_EG_SEL_X | (V9X_EG_SEL_Y << 3) | (V9X_EG_SEL_0 << 6) |
           (V9X_EG_SEL_1 << 9)));
}

static void test_modulate_ps(void)
{
    v9x_u32 count;
    v9x_u32 slot;
    v9x_u32 w0;
    v9x_u32 w1;

    reset();
    count = v9x_eg_shader_modulate_ps(shader);
    CHECK(count == 44ul);
    /* ALU (12 slots at dword 8), TC (one fetch at dword 40), ALU (4 slots
     * at dword 32), the colour export of R0. */
    CHECK(shader[0] == 4ul);
    CHECK(shader[1] == (0x80000000ul | (8ul << 26) | (11ul << 18)));
    CHECK(shader[2] == 20ul && shader[3] == 0x80400000ul);
    CHECK(shader[4] == 16ul);
    CHECK(shader[5] == (0x80000000ul | (8ul << 26) | (3ul << 18)));
    CHECK(shader[6] == 0ul && shader[7] == 0x95200688ul);
    /* The coordinate: INTERP_XY of PARAM1 into R1.xy. */
    for (slot = 0ul; slot < 4ul; ++slot) {
        w0 = shader[8ul + slot * 2ul];
        w1 = shader[9ul + slot * 2ul];
        CHECK(((w1 >> 7) & 0x7fful) == V9X_EG_ALU_INTERP_XY);
        CHECK(((w0 >> 13) & 0x1fful) == V9X_EG_ALU_SRC_PARAM + 1ul);
        CHECK(((w1 >> 21) & 0x7ful) == 1ul);
        CHECK(((w1 >> 4) & 1ul) == (slot < 2ul ? 1ul : 0ul));
    }
    /* The colour: the Gouraud PS's eight slots, word for word. */
    {
        v9x_u32 gouraud[V9X_EG_SHADER_DWORDS_MAX];
        v9x_u32 index;

        v9x_eg_shader_gouraud_ps(gouraud);
        for (index = 0ul; index < 16ul; ++index) {
            CHECK(shader[16ul + index] == gouraud[4ul + index]);
        }
    }
    /* R0.c = R0.c * R1.c. */
    for (slot = 0ul; slot < 4ul; ++slot) {
        w0 = shader[32ul + slot * 2ul];
        w1 = shader[33ul + slot * 2ul];
        CHECK(((w1 >> 7) & 0x7fful) == V9X_EG_ALU_MUL);
        CHECK((w0 & 0x1fful) == 0ul && ((w0 >> 10) & 3ul) == slot);
        CHECK(((w0 >> 13) & 0x1fful) == 1ul && ((w0 >> 23) & 3ul) == slot);
        CHECK(((w1 >> 21) & 0x7ful) == 0ul && ((w1 >> 29) & 3ul) == slot);
        CHECK(((w1 >> 4) & 1ul) == 1ul && ((w1 >> 18) & 7ul) == 0ul);
        CHECK((w0 >> 31) == (slot == 3ul ? 1ul : 0ul));
    }
    /* SAMPLE R1.xy into R1. */
    CHECK(shader[40] == (V9X_EG_TEX_SAMPLE | (1ul << 16)));
    CHECK((shader[41] & 0x7ful) == 1ul);
}

unsigned int v9x_run_eg_shader_tests(void)
{
    failures = 0u;
    test_blit();
    test_gouraud_vs();
    test_gouraud_ps();
    test_modulate_vs();
    test_modulate_ps();
    return failures;
}
