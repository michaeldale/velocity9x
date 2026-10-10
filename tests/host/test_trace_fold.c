/*
 * Tests for the HAL trace ring's repeat folding (include\velocity9x\trace_fold.h).
 *
 * The ring is modelled here as the writer in ddhal_core.c uses it: the rule
 * says append or count, and a count goes on the newest entry or the one
 * before. The cases are the shapes the Carmageddon II snapshots of 2026-10-10
 * were full of - a teardown of DestroySurface pairs, a render loop of Flip
 * pairs, D3dTargetLayout pushed with no exit - and the one the ring exists
 * for: a call that never returned has to stay visible.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/trace_fold.h"

static unsigned int trace_fold_failures = 0u;

#define TFCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++trace_fold_failures; \
    } \
} while (0)

#define TF_RING 16u
#define TF_EXIT V9X_TRACE_FOLD_EXIT_FLAG

/* Trace ids as win9x_ddraw_abi.h numbers them; only their distinctness
 * matters here. */
#define TF_FLIP      10u
#define TF_DESTROY   22u
#define TF_FLIPTOGDI 18u
#define TF_LAYOUT    39u

struct tf_ring {
    v9x_u16 id[TF_RING];
    v9x_u32 detail[TF_RING];
    unsigned int used;
};

static void tf_push(struct tf_ring *ring, v9x_u16 id, v9x_u32 detail)
{
    v9x_u16 third = ring->used >= 3u ? ring->id[ring->used - 3u] : 0u;
    v9x_u16 previous = ring->used >= 2u ? ring->id[ring->used - 2u] : 0u;
    v9x_u16 newest = ring->used >= 1u ? ring->id[ring->used - 1u] : 0u;
    v9x_u32 previous_detail = ring->used >= 2u
                                  ? ring->detail[ring->used - 2u] : 0ul;
    v9x_u32 action = v9x_trace_fold_decide(third, previous, newest,
                                           previous_detail, id, detail);

    if (action == V9X_TRACE_FOLD_NEWEST) {
        ring->id[ring->used - 1u] = v9x_trace_fold_bump(newest);
        ring->detail[ring->used - 1u] = detail;
        return;
    }
    if (action == V9X_TRACE_FOLD_CLOSE) {
        /* The pair before absorbs the call just entered: its enter takes
         * the newest argument, both halves count one more, and the
         * tentative enter is taken back out of the ring. */
        ring->id[ring->used - 3u] = v9x_trace_fold_bump(third);
        ring->detail[ring->used - 3u] = ring->detail[ring->used - 1u];
        ring->id[ring->used - 2u] = v9x_trace_fold_bump(previous);
        --ring->used;
        ring->id[ring->used] = 0u;
        ring->detail[ring->used] = 0ul;
        return;
    }
    if (ring->used < TF_RING) {
        ring->id[ring->used] = id;
        ring->detail[ring->used] = detail;
        ++ring->used;
    }
}

/* 546 textures freed: one pair takes the first 512, counted to the
 * maximum, and a second pair the other 34. Four entries, not 1,092, and
 * each enter keeps the newest surface pointer it saw. */
static void test_teardown_is_two_pairs(void)
{
    struct tf_ring ring;
    v9x_u32 i;

    memset(&ring, 0, sizeof(ring));
    for (i = 0ul; i < 546ul; ++i) {
        tf_push(&ring, TF_DESTROY, 0x83300000ul + i * 0x1C0ul);
        tf_push(&ring, (v9x_u16)(TF_DESTROY | TF_EXIT), 0ul);
    }
    TFCHECK(ring.used == 4u);
    TFCHECK(v9x_trace_fold_base(ring.id[0]) == TF_DESTROY);
    TFCHECK((ring.id[0] & TF_EXIT) == 0u);
    TFCHECK(v9x_trace_fold_count(ring.id[0]) == V9X_TRACE_FOLD_COUNT_MAX);
    TFCHECK(ring.detail[0] == 0x83300000ul + 511ul * 0x1C0ul);
    TFCHECK(v9x_trace_fold_base(ring.id[1]) == TF_DESTROY);
    TFCHECK((ring.id[1] & TF_EXIT) != 0u);
    TFCHECK(v9x_trace_fold_count(ring.id[1]) == V9X_TRACE_FOLD_COUNT_MAX);
    TFCHECK(v9x_trace_fold_count(ring.id[2]) == 33u);
    TFCHECK(v9x_trace_fold_count(ring.id[3]) == 33u);
    TFCHECK(ring.detail[2] == 0x83300000ul + 545ul * 0x1C0ul);
}

/* The call that never returned is the last entry, on its own, whatever
 * came before it. */
static void test_unreturned_call_stays_visible(void)
{
    struct tf_ring ring;

    memset(&ring, 0, sizeof(ring));
    tf_push(&ring, TF_FLIP, 0ul);
    tf_push(&ring, (v9x_u16)(TF_FLIP | TF_EXIT), 0ul);
    tf_push(&ring, TF_FLIP, 0ul);
    tf_push(&ring, (v9x_u16)(TF_FLIP | TF_EXIT), 0ul);
    tf_push(&ring, TF_FLIP, 7ul);
    TFCHECK(ring.used == 3u);
    TFCHECK(v9x_trace_fold_count(ring.id[0]) == 1u);
    TFCHECK(v9x_trace_fold_count(ring.id[1]) == 1u);
    TFCHECK((ring.id[2] & TF_EXIT) == 0u);
    TFCHECK(v9x_trace_fold_count(ring.id[2]) == 0u);
    TFCHECK(ring.detail[2] == 7ul);
}

/* A different result is a different outcome: the call keeps its own enter
 * and exit, with its own argument. GetDriverInfo on the ViRGE guest
 * (2026-10-11) answered one GUID with 0 and the next with 0x88760028, and
 * folding the enter early had put the second GUID on the first's result. */
static void test_different_result_is_appended(void)
{
    struct tf_ring ring;

    memset(&ring, 0, sizeof(ring));
    tf_push(&ring, TF_FLIP, 0x7DE41F80ul);
    tf_push(&ring, (v9x_u16)(TF_FLIP | TF_EXIT), 0ul);
    tf_push(&ring, TF_FLIP, 0xFFAA7540ul);
    tf_push(&ring, (v9x_u16)(TF_FLIP | TF_EXIT), 0x88760028ul);
    TFCHECK(ring.used == 4u);
    TFCHECK(v9x_trace_fold_count(ring.id[0]) == 0u);
    TFCHECK(ring.detail[0] == 0x7DE41F80ul);
    TFCHECK(ring.detail[1] == 0ul);
    TFCHECK(ring.detail[2] == 0xFFAA7540ul);
    TFCHECK(ring.detail[3] == 0x88760028ul);
}

/* An enter-only repeat on top of an open call does not close the pair
 * before it when the exit finally comes: two enters, one exit. */
static void test_reentered_call_is_not_closed(void)
{
    struct tf_ring ring;

    memset(&ring, 0, sizeof(ring));
    tf_push(&ring, TF_FLIP, 0ul);
    tf_push(&ring, (v9x_u16)(TF_FLIP | TF_EXIT), 0ul);
    tf_push(&ring, TF_FLIP, 0ul);
    tf_push(&ring, TF_FLIP, 0ul);
    tf_push(&ring, (v9x_u16)(TF_FLIP | TF_EXIT), 0ul);
    TFCHECK(ring.used == 4u);
    TFCHECK(v9x_trace_fold_count(ring.id[0]) == 0u);
    TFCHECK(v9x_trace_fold_count(ring.id[2]) == 1u);
}

/* An event with no exit, pushed with alternating details, folds onto
 * itself; the next different event is appended after it. */
static void test_exitless_event_folds(void)
{
    struct tf_ring ring;
    unsigned int i;

    memset(&ring, 0, sizeof(ring));
    for (i = 0u; i < 19u; ++i) {
        tf_push(&ring, TF_LAYOUT, (i & 1u) != 0u ? 0x05001082ul
                                                 : 0x05001002ul);
    }
    TFCHECK(ring.used == 1u);
    TFCHECK(v9x_trace_fold_count(ring.id[0]) == 18u);
    TFCHECK(ring.detail[0] == 0x05001002ul);
    tf_push(&ring, TF_FLIPTOGDI, 1ul);
    TFCHECK(ring.used == 2u);
}

/* Interleaved callbacks are not folded across each other. */
static void test_interleaved_pairs_are_kept(void)
{
    struct tf_ring ring;

    memset(&ring, 0, sizeof(ring));
    tf_push(&ring, TF_FLIP, 0ul);
    tf_push(&ring, (v9x_u16)(TF_FLIP | TF_EXIT), 0ul);
    tf_push(&ring, TF_DESTROY, 1ul);
    tf_push(&ring, (v9x_u16)(TF_DESTROY | TF_EXIT), 0ul);
    tf_push(&ring, TF_FLIP, 0ul);
    tf_push(&ring, (v9x_u16)(TF_FLIP | TF_EXIT), 0ul);
    TFCHECK(ring.used == 6u);
}

/* At the maximum count a new entry starts rather than wrapping to zero. */
static void test_count_saturates(void)
{
    struct tf_ring ring;
    v9x_u32 i;

    memset(&ring, 0, sizeof(ring));
    for (i = 0ul; i <= (v9x_u32)V9X_TRACE_FOLD_COUNT_MAX + 1ul; ++i) {
        tf_push(&ring, TF_LAYOUT, i);
    }
    TFCHECK(ring.used == 2u);
    TFCHECK(v9x_trace_fold_count(ring.id[0]) == V9X_TRACE_FOLD_COUNT_MAX);
    TFCHECK(v9x_trace_fold_count(ring.id[1]) == 0u);
}

/* An empty ring and an entry from the 16-bit writer, count zero, fold
 * like any other. */
static void test_empty_slots_never_match(void)
{
    TFCHECK(v9x_trace_fold_decide(0u, 0u, 0u, 0ul, TF_FLIP, 0ul) ==
            V9X_TRACE_FOLD_APPEND);
    TFCHECK(v9x_trace_fold_decide(0u, 0u, 0u, 0ul,
                                  (v9x_u16)(TF_FLIP | TF_EXIT), 0ul) ==
            V9X_TRACE_FOLD_APPEND);
}

unsigned int v9x_run_trace_fold_tests(void)
{
    trace_fold_failures = 0u;
    test_teardown_is_two_pairs();
    test_unreturned_call_stays_visible();
    test_different_result_is_appended();
    test_reentered_call_is_not_closed();
    test_exitless_event_folds();
    test_interleaved_pairs_are_kept();
    test_count_saturates();
    test_empty_slots_never_match();
    if (trace_fold_failures == 0u) {
        printf("PASS: trace ring repeat folding\n");
    }
    return trace_fold_failures;
}
