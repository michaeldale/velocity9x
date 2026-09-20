/*
 * The presented surface's read plan, pinned.
 *
 * This is the memory-safety check for a direct read of video memory, so the
 * refusals matter more than the acceptances and are tested first. The
 * overflow cases are the reason the module exists: a width and a pitch are
 * both values the hardware supplies, and the product of two of those is
 * what a naive bound would compute before comparing it to anything.
 */
#include <stdio.h>

#include "velocity9x/i9xx_cover.h"

static unsigned int cover_failures = 0u;

#define COVERCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++cover_failures; \
    } \
} while (0)

/* The netbook, as intel98 and intel99 captured it: 1024x576, DSPCNTR format
 * 5 (BGRX565), a 2048-byte stride, drawing into the second buffer. */
#define COVER_SRC     ((v9x_u32)0x03FF023Ful)
#define COVER_CNTR    ((v9x_u32)0x95000000ul)
#define COVER_STRIDE  ((v9x_u32)2048ul)
#define COVER_VRAM    ((v9x_u32)8060928ul)

static void test_the_netbook_as_captured(void)
{
    struct v9x_i9xx_cover_plan plan;

    COVERCHECK(v9x_i9xx_cover_plan(COVER_SRC, COVER_CNTR, COVER_STRIDE,
                                   0x00120000ul, COVER_VRAM, 8ul,
                                   &plan) == V9X_TRUE);
    COVERCHECK(plan.width == 1024ul);
    COVERCHECK(plan.height == 576ul);
    COVERCHECK(plan.pitch == 2048ul);
    COVERCHECK(plan.bytes_per_pixel == 2ul);
    COVERCHECK(plan.columns == 128ul);
    COVERCHECK(plan.rows == 72ul);
    /* The last sample of the last row stays inside the surface. */
    COVERCHECK((plan.rows - 1ul) * plan.step < plan.height);
    COVERCHECK((plan.columns - 1ul) * plan.step < plan.width);
}

/*
 * A row wider than its pitch is refused, and the test that refuses it must
 * not compute width * cpp. At a pitch of 2048 and two bytes a pixel the
 * limit is 1024 pixels, so 1025 is the first refusal.
 */
static void test_a_row_must_fit_the_pitch(void)
{
    struct v9x_i9xx_cover_plan plan;
    v9x_u32 src_1024 = (1023ul << 16) | 575ul;
    v9x_u32 src_1025 = (1024ul << 16) | 575ul;

    COVERCHECK(v9x_i9xx_cover_plan(src_1024, COVER_CNTR, 2048ul, 0ul,
                                   COVER_VRAM, 8ul, &plan) == V9X_TRUE);
    COVERCHECK(v9x_i9xx_cover_plan(src_1025, COVER_CNTR, 2048ul, 0ul,
                                   COVER_VRAM, 8ul, &plan) == V9X_FALSE);
    /* Four bytes a pixel halves what the same pitch holds. */
    COVERCHECK(v9x_i9xx_cover_plan(src_1024, 0x98000000ul, 2048ul, 0ul,
                                   COVER_VRAM, 8ul, &plan) == V9X_FALSE);
}

/*
 * The surface must fit what is left of video memory. The arithmetic is
 * height against the remaining bytes divided by pitch, never height times
 * pitch, which is the multiply that overflows.
 */
static void test_the_surface_must_fit_vram(void)
{
    struct v9x_i9xx_cover_plan plan;
    v9x_u32 exactly = 576ul * 2048ul;

    COVERCHECK(v9x_i9xx_cover_plan(COVER_SRC, COVER_CNTR, 2048ul,
                                   COVER_VRAM - exactly, COVER_VRAM, 8ul,
                                   &plan) == V9X_TRUE);
    COVERCHECK(v9x_i9xx_cover_plan(COVER_SRC, COVER_CNTR, 2048ul,
                                   COVER_VRAM - exactly + 2048ul,
                                   COVER_VRAM, 8ul, &plan) == V9X_FALSE);
    /* An offset at or past the end is refused before any subtraction. */
    COVERCHECK(v9x_i9xx_cover_plan(COVER_SRC, COVER_CNTR, 2048ul,
                                   COVER_VRAM, COVER_VRAM, 8ul,
                                   &plan) == V9X_FALSE);
    COVERCHECK(v9x_i9xx_cover_plan(COVER_SRC, COVER_CNTR, 2048ul,
                                   0xfffffffful, COVER_VRAM, 8ul,
                                   &plan) == V9X_FALSE);
}

/*
 * The values that would overflow a 32-bit product if the bounds multiplied.
 * A height of 4096 against a pitch of 0x01000000 is 2^36; a naive check
 * would compute zero and accept it.
 */
static void test_the_products_that_overflow(void)
{
    struct v9x_i9xx_cover_plan plan;
    v9x_u32 tall = (1023ul << 16) | 4095ul;

    COVERCHECK(v9x_i9xx_cover_plan(tall, COVER_CNTR, 0x01000000ul, 0ul,
                                   COVER_VRAM, 8ul, &plan) == V9X_FALSE);
    COVERCHECK(v9x_i9xx_cover_plan(tall, COVER_CNTR, 0xfffffffcul, 0ul,
                                   COVER_VRAM, 8ul, &plan) == V9X_FALSE);
    COVERCHECK(v9x_i9xx_cover_plan(tall, COVER_CNTR, 2048ul, 0ul,
                                   0xfffffffful, 8ul, &plan) == V9X_TRUE);
}

/* A format no pixel can be read out of, and the zeros. */
static void test_the_refusals(void)
{
    struct v9x_i9xx_cover_plan plan;

    /* Format 0xc is 16161616F - eight bytes, declined rather than misread. */
    COVERCHECK(v9x_i9xx_cover_plan(COVER_SRC, 0xb0000000ul, 2048ul, 0ul,
                                   COVER_VRAM, 8ul, &plan) == V9X_FALSE);
    /* A format this driver does not know at all. */
    COVERCHECK(v9x_i9xx_cover_plan(COVER_SRC, 0x84000000ul, 2048ul, 0ul,
                                   COVER_VRAM, 8ul, &plan) == V9X_FALSE);
    COVERCHECK(v9x_i9xx_cover_plan(COVER_SRC, COVER_CNTR, 0ul, 0ul,
                                   COVER_VRAM, 8ul, &plan) == V9X_FALSE);
    COVERCHECK(v9x_i9xx_cover_plan(COVER_SRC, COVER_CNTR, 2048ul, 0ul,
                                   0ul, 8ul, &plan) == V9X_FALSE);
    COVERCHECK(v9x_i9xx_cover_plan(COVER_SRC, COVER_CNTR, 2048ul, 0ul,
                                   COVER_VRAM, 0ul, &plan) == V9X_FALSE);
    COVERCHECK(v9x_i9xx_cover_plan(COVER_SRC, COVER_CNTR, 2048ul, 0ul,
                                   COVER_VRAM, 8ul, 0) == V9X_FALSE);
}

/* A refused plan leaves the caller's structure untouched, so a caller that
 * ignores the return value walks its own stale plan rather than a partly
 * written one. */
static void test_a_refusal_writes_nothing(void)
{
    struct v9x_i9xx_cover_plan plan;

    plan.width = 0x5a5a5a5aul;
    plan.height = 0x5a5a5a5aul;
    plan.pitch = 0x5a5a5a5aul;
    plan.bytes_per_pixel = 0x5a5a5a5aul;
    COVERCHECK(v9x_i9xx_cover_plan(COVER_SRC, 0x84000000ul, 2048ul, 0ul,
                                   COVER_VRAM, 8ul, &plan) == V9X_FALSE);
    COVERCHECK(plan.width == 0x5a5a5a5aul);
    COVERCHECK(plan.height == 0x5a5a5a5aul);
    COVERCHECK(plan.pitch == 0x5a5a5a5aul);
    COVERCHECK(plan.bytes_per_pixel == 0x5a5a5a5aul);
}

/* A step of one samples every pixel; the counts are the dimensions. */
static void test_the_step(void)
{
    struct v9x_i9xx_cover_plan plan;

    COVERCHECK(v9x_i9xx_cover_plan(COVER_SRC, COVER_CNTR, 2048ul, 0ul,
                                   COVER_VRAM, 1ul, &plan) == V9X_TRUE);
    COVERCHECK(plan.columns == 1024ul);
    COVERCHECK(plan.rows == 576ul);
    /* A step larger than the surface still samples its first pixel. */
    COVERCHECK(v9x_i9xx_cover_plan(COVER_SRC, COVER_CNTR, 2048ul, 0ul,
                                   COVER_VRAM, 4096ul, &plan) == V9X_TRUE);
    COVERCHECK(plan.columns == 1ul);
    COVERCHECK(plan.rows == 1ul);
}

/*
 * The first accepted flip after a request keeps its capture.
 *
 * This is the defect the split exists for: arming used to clear the records
 * directly, and the arming path ran AFTER the sampler on the same flip, so
 * the first flip of a new mode wrote its record and then had it erased.
 * Applying the request at the sample makes the order irrelevant.
 */
static void test_the_first_flip_after_a_request_keeps_its_record(void)
{
    struct v9x_i9xx_cover_state state;

    state.rearm = 0ul;
    state.image_wanted = 0ul;
    state.records = 3ul;
    state.attempts = 2ul;

    v9x_i9xx_cover_request(&state);
    /* Nothing discarded yet - the requester may be on its way out of the
     * application whose results these are. */
    COVERCHECK(state.records == 3ul);
    COVERCHECK(state.attempts == 2ul);
    COVERCHECK(state.image_wanted == 1ul);

    COVERCHECK(v9x_i9xx_cover_begin(&state, 4ul) == V9X_TRUE);
    /* Now the old run is dropped, and THIS sample is the first of the new. */
    COVERCHECK(state.records == 1ul);
    COVERCHECK(state.attempts == 0ul);
    COVERCHECK(state.rearm == 0ul);

    /* A second request arriving after the sample does not erase it either,
     * until another sample is actually taken. */
    v9x_i9xx_cover_request(&state);
    COVERCHECK(state.records == 1ul);
}

/* Without a request the slots fill and then refuse, so the first frames of a
 * run are kept whole rather than the last overwriting them. */
static void test_the_slots_fill_once(void)
{
    struct v9x_i9xx_cover_state state;

    state.rearm = 0ul;
    state.image_wanted = 0ul;
    state.records = 0ul;
    state.attempts = 0ul;

    COVERCHECK(v9x_i9xx_cover_begin(&state, 2ul) == V9X_TRUE);
    COVERCHECK(v9x_i9xx_cover_begin(&state, 2ul) == V9X_TRUE);
    COVERCHECK(v9x_i9xx_cover_begin(&state, 2ul) == V9X_FALSE);
    COVERCHECK(state.records == 2ul);

    /* And a request re-opens them. */
    v9x_i9xx_cover_request(&state);
    COVERCHECK(v9x_i9xx_cover_begin(&state, 2ul) == V9X_TRUE);
    COVERCHECK(state.records == 1ul);
}

/* Zero slots refuses rather than writing past the array, and a null state
 * refuses rather than faulting. */
static void test_the_degenerate_cases(void)
{
    struct v9x_i9xx_cover_state state;

    state.rearm = 1ul;
    state.image_wanted = 1ul;
    state.records = 5ul;
    state.attempts = 5ul;
    COVERCHECK(v9x_i9xx_cover_begin(&state, 0ul) == V9X_FALSE);
    /* The request was still applied - the old run is gone, nothing recorded. */
    COVERCHECK(state.records == 0ul);
    COVERCHECK(v9x_i9xx_cover_begin(0, 4ul) == V9X_FALSE);
    v9x_i9xx_cover_request(0);
}

unsigned int v9x_run_i9xx_cover_tests(void)
{
    cover_failures = 0u;
    test_the_netbook_as_captured();
    test_a_row_must_fit_the_pitch();
    test_the_surface_must_fit_vram();
    test_the_products_that_overflow();
    test_the_refusals();
    test_a_refusal_writes_nothing();
    test_the_step();
    test_the_first_flip_after_a_request_keeps_its_record();
    test_the_slots_fill_once();
    test_the_degenerate_cases();

    return cover_failures;
}
