#include <stdio.h>
#include <string.h>

#include "velocity9x/nv50_grctx.h"

/*
 * The NV50 context program generator (src\chipsets\nvidia\gt218\
 * nv50_grctx.c), on the GT218's inputs as M-BXMFA8 reads them (chipset A8h,
 * PBUS 1540h F3010001h, DDR2/3: docs\decisions\2026-11-11-gt218-register-
 * probe.md).
 *
 * There is no reference output of nouveau's generator for A8 in the tree,
 * so these check structure, not bytes: every branch resolved and inside the
 * program, the program ends, it fits its store, the passes agree, and the
 * two inputs nouveau says matter change what it says they change.
 */

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* Instruction fields, from nouveau's ctxnv50.c CP_ defines. */
#define GRCTX_OP_MASK      0x00f00000ul
#define GRCTX_OP_BRA       0x00400000ul
#define GRCTX_BRA_MOD_MASK 0x000c0000ul
#define GRCTX_BRA_MOD_RET  0x00080000ul
#define GRCTX_BRA_IP_MASK  0x0001ff00ul
#define GRCTX_BRA_IP_SHIFT 8u
#define GRCTX_UNRESOLVED   0xff400000ul
#define GRCTX_END          0x0060000cul

#define GT218_CHIPSET 0xa8ul
#define GT218_UNITS   0xf3010001ul
#define IMAGE_MAX     0x20000ul

static v9x_u32 program_a[V9X_NV50_GRCTX_PROGRAM_MAX];
static v9x_u32 program_b[V9X_NV50_GRCTX_PROGRAM_MAX];
static v9x_u32 image_a[IMAGE_MAX];
static v9x_u32 image_b[IMAGE_MAX];

static v9x_status build(v9x_u32 units, v9x_u32 gddr5, v9x_u32 *program,
                        v9x_u32 *program_words, v9x_u32 *image,
                        v9x_u32 image_max, v9x_u32 *image_words)
{
    struct v9x_nv50_grctx_input input;

    input.chipset = GT218_CHIPSET;
    input.units = units;
    input.gddr5 = gddr5;
    return v9x_nv50_grctx_build(&input, program, program_words, image,
                                image_max, image_words);
}

static void test_gt218_program(void)
{
    v9x_u32 words = 0ul;
    v9x_u32 image_words = 0ul;
    v9x_u32 index;
    v9x_u32 unresolved = 0ul;
    v9x_u32 outside = 0ul;
    v9x_u32 branches = 0ul;
    v9x_u32 word;

    CHECK(build(GT218_UNITS, 0ul, program_a, &words, 0, 0ul,
                &image_words) == V9X_STATUS_OK);
    CHECK(words > 0ul && words <= V9X_NV50_GRCTX_PROGRAM_MAX);
    CHECK(words > 0ul && program_a[words - 1ul] == GRCTX_END);
    /* At least nouveau's padding, 400h words, beyond the 100h-word header. */
    CHECK(image_words > 0x500ul);

    for (index = 0ul; index < words; ++index) {
        word = program_a[index];
        if ((word & 0xfff00000ul) == GRCTX_UNRESOLVED) {
            ++unresolved;
            continue;
        }
        if ((word & GRCTX_OP_MASK) != GRCTX_OP_BRA ||
            (word & GRCTX_BRA_MOD_MASK) == GRCTX_BRA_MOD_RET) {
            continue;
        }
        ++branches;
        if (((word & GRCTX_BRA_IP_MASK) >> GRCTX_BRA_IP_SHIFT) >= words) {
            ++outside;
        }
    }
    CHECK(unresolved == 0ul);
    CHECK(outside == 0ul);
    CHECK(branches >= 8ul);

    printf("INFO nv50_grctx A8 units %08lX: program %lu words, image %lu "
           "words (%lu bytes)\n", (unsigned long)GT218_UNITS,
           (unsigned long)words, (unsigned long)image_words,
           (unsigned long)(image_words * 4ul));
}

static void test_passes_agree(void)
{
    v9x_u32 words_a = 0ul;
    v9x_u32 words_b = 0ul;
    v9x_u32 image_words_a = 0ul;
    v9x_u32 image_words_b = 0ul;
    v9x_u32 size_only = 0ul;
    v9x_u32 index;
    v9x_u32 nonzero = 0ul;

    CHECK(build(GT218_UNITS, 0ul, program_a, &words_a, image_a, IMAGE_MAX,
                &image_words_a) == V9X_STATUS_OK);
    CHECK(build(GT218_UNITS, 0ul, program_b, &words_b, image_b, IMAGE_MAX,
                &image_words_b) == V9X_STATUS_OK);
    CHECK(words_a == words_b);
    CHECK(image_words_a == image_words_b);
    CHECK(memcmp(program_a, program_b, words_a * 4ul) == 0);
    CHECK(memcmp(image_a, image_b, image_words_a * 4ul) == 0);

    /* The size-only call gives the same counts. */
    CHECK(build(GT218_UNITS, 0ul, program_b, &words_b, 0, 0ul,
                &size_only) == V9X_STATUS_OK);
    CHECK(size_only == image_words_a);

    for (index = 0ul; index < image_words_a; ++index) {
        if (image_a[index] != 0ul) {
            ++nonzero;
        }
    }
    CHECK(nonzero > 0ul);
    printf("INFO nv50_grctx A8 image: %lu non-zero default words\n",
           (unsigned long)nonzero);
}

static void test_refusals(void)
{
    struct v9x_nv50_grctx_input input;
    v9x_u32 words = 0ul;
    v9x_u32 image_words = 0ul;

    CHECK(build(GT218_UNITS, 0ul, program_a, &words, image_a, 0x100ul,
                &image_words) == V9X_STATUS_INTEGER_OVERFLOW);
    input.chipset = GT218_CHIPSET;
    input.units = GT218_UNITS;
    input.gddr5 = 0ul;
    CHECK(v9x_nv50_grctx_build(0, program_a, &words, 0, 0ul,
                               &image_words) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_nv50_grctx_build(&input, 0, &words, 0, 0ul, &image_words) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/* nouveau: GDDR5 changes one default (407D08h) and nothing in the
 * program. */
static void test_gddr5(void)
{
    v9x_u32 words_a = 0ul;
    v9x_u32 words_b = 0ul;
    v9x_u32 image_words_a = 0ul;
    v9x_u32 image_words_b = 0ul;
    v9x_u32 index;
    v9x_u32 differing = 0ul;

    CHECK(build(GT218_UNITS, 0ul, program_a, &words_a, image_a, IMAGE_MAX,
                &image_words_a) == V9X_STATUS_OK);
    CHECK(build(GT218_UNITS, 1ul, program_b, &words_b, image_b, IMAGE_MAX,
                &image_words_b) == V9X_STATUS_OK);
    CHECK(words_a == words_b);
    CHECK(memcmp(program_a, program_b, words_a * 4ul) == 0);
    CHECK(image_words_a == image_words_b);
    for (index = 0ul; index < image_words_a; ++index) {
        if (image_a[index] != image_b[index]) {
            ++differing;
        }
    }
    CHECK(differing == 1ul);
}

/* M-BXMFA8's mask has one TP (bits 9:0 = 001h); a second TP (bit 1) is
 * more per-TP state, so a larger image. */
static void test_units(void)
{
    v9x_u32 words = 0ul;
    v9x_u32 one = 0ul;
    v9x_u32 two = 0ul;

    CHECK(build(GT218_UNITS, 0ul, program_a, &words, 0, 0ul, &one) ==
          V9X_STATUS_OK);
    CHECK(build(GT218_UNITS | 0x00000002ul, 0ul, program_a, &words, 0, 0ul,
                &two) == V9X_STATUS_OK);
    CHECK(two > one);
}

/* The G96 (chipset 96h, 1540h F3010001h as M-Z2T02D reads it): nouveau's
 * pre-A0h branches give a program that ends, has every branch resolved
 * and inside it, and an image that fits the channel's context slot
 * (V9X_NV50_GRCTX_MAX_BYTES, 512 KiB). */
static void test_g96_program(void)
{
    struct v9x_nv50_grctx_input input;
    v9x_u32 words = 0ul;
    v9x_u32 image_words = 0ul;
    v9x_u32 index;
    v9x_u32 word;
    v9x_u32 target;
    v9x_u32 bad = 0ul;

    input.chipset = 0x96ul;
    input.units = GT218_UNITS;
    input.gddr5 = 0ul;
    CHECK(v9x_nv50_grctx_build(&input, program_a, &words, image_a,
                               IMAGE_MAX, &image_words) == V9X_STATUS_OK);
    CHECK(words > 0ul && words <= V9X_NV50_GRCTX_PROGRAM_MAX);
    CHECK(words > 0ul && program_a[words - 1ul] == GRCTX_END);
    CHECK(image_words > 0x500ul && image_words * 4ul <= 0x80000ul);
    for (index = 0ul; index < words; ++index) {
        word = program_a[index];
        if ((word & 0xfff00000ul) == GRCTX_UNRESOLVED) {
            ++bad;
            continue;
        }
        if ((word & GRCTX_OP_MASK) != GRCTX_OP_BRA ||
            (word & GRCTX_BRA_MOD_MASK) == GRCTX_BRA_MOD_RET) {
            continue;
        }
        target = (word & GRCTX_BRA_IP_MASK) >> GRCTX_BRA_IP_SHIFT;
        if (target >= words) {
            ++bad;
        }
    }
    CHECK(bad == 0ul);
    printf("INFO nv50_grctx 96 program: %lu words, image %lu words\n",
           (unsigned long)words, (unsigned long)image_words);
}

unsigned int v9x_run_nv50_grctx_tests(void)
{
    failures = 0u;
    test_g96_program();
    test_gt218_program();
    test_passes_agree();
    test_refusals();
    test_gddr5();
    test_units();
    return failures;
}
