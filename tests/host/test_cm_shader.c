#include <stdio.h>
#include <string.h>

#include "velocity9x/cm_shader.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* Linux v3.9 cayman_blit_shaders.c, the little-endian words: the blit
 * Linux ran on every Cayman-generation part before its 3.10 rework. */
static const v9x_u32 linux_cayman_vs[16] = {
    0x00000004ul, 0x80400400ul, 0x0000a03cul, 0x95000688ul,
    0x00004000ul, 0x15000688ul, 0x00000000ul, 0x88000000ul,
    0x04000000ul, 0x67961001ul, 0x00000000ul, 0x00000000ul,
    0x04000000ul, 0x67961000ul, 0x00000008ul, 0x00000000ul
};

static const v9x_u32 linux_cayman_ps[20] = {
    0x00000004ul, 0xa00c0000ul, 0x00000008ul, 0x80400000ul,
    0x00000000ul, 0x95000688ul, 0x00000000ul, 0x88000000ul,
    0x00380400ul, 0x00146b10ul, 0x00380000ul, 0x20146b10ul,
    0x00380400ul, 0x40146b00ul, 0x80380000ul, 0x60146b00ul,
    0x00000010ul, 0x000d1000ul, 0xb0800000ul, 0x00000000ul
};

static v9x_u32 shader[V9X_CM_SHADER_DWORDS_MAX];

static void reset(void)
{
    memset(shader, 0xcc, sizeof(shader));
}

static void test_blit(void)
{
    v9x_u32 count;
    v9x_u32 index;

    reset();
    count = v9x_cm_shader_blit_vs(shader);
    CHECK(count == 16ul);
    for (index = 0ul; index < 16ul; ++index) {
        CHECK(shader[index] == linux_cayman_vs[index]);
    }
    reset();
    count = v9x_cm_shader_blit_ps(shader);
    CHECK(count == 20ul);
    for (index = 0ul; index < 20ul; ++index) {
        CHECK(shader[index] == linux_cayman_ps[index]);
    }
}

static void test_cf_end(void)
{
    reset();
    CHECK(v9x_cm_cf_end(shader) == 2ul);
    /* Barrier set, CF_INST 20h, nothing else. */
    CHECK(shader[0] == 0ul);
    CHECK(shader[1] == 0x88000000ul);
    CHECK(shader[2] == 0xccccccccul);
}

unsigned int v9x_run_cm_shader_tests(void)
{
    failures = 0u;
    test_blit();
    test_cf_end();
    return failures;
}
