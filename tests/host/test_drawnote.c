/*
 * Tests for the present trace's record rule.
 *
 * The trace keeps one record per frame for the first draw batch after an
 * accepted flip, and deciding which batch that is was got wrong three times
 * on 2026-09-19, each time producing a reading of something that had not
 * happened. Every rule about it lives in src\common\drawnote.c and is
 * decided here, because the wrapper it governs sits in the HAL among DDHAL
 * types and MMIO and cannot be tested at all.
 *
 * The property that matters is that a record means a SUBMISSION. Neither
 * the backend's return value nor the order in which the wrapper runs is
 * evidence of one: the ViRGE path returns success without launching a
 * command for a degenerate triangle, a thin one, and a blend its unit
 * cannot express, and it returns failure from the middle of a batch whose
 * earlier triangles did launch.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/drawnote.h"

static unsigned int drawnote_failures = 0u;

#define DNCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++drawnote_failures; \
    } \
} while (0)

/* A fresh state records nothing: no flip has armed it. */
static void test_starts_disarmed(void)
{
    struct v9x_draw_note note;

    memset(&note, 0xff, sizeof(note));
    v9x_draw_note_reset(&note);
    DNCHECK(v9x_draw_note_should_record(&note, 1) == 0);
    DNCHECK(note.armed == (v9x_u16)0u);
}

/* The first submitting batch after a flip is recorded, once. */
static void test_one_record_per_flip(void)
{
    struct v9x_draw_note note;

    v9x_draw_note_reset(&note);
    v9x_draw_note_flip(&note);
    DNCHECK(v9x_draw_note_should_record(&note, 1) == 1);
    DNCHECK(v9x_draw_note_should_record(&note, 1) == 0);
    DNCHECK(v9x_draw_note_should_record(&note, 1) == 0);

    v9x_draw_note_flip(&note);
    DNCHECK(v9x_draw_note_should_record(&note, 1) == 1);
    v9x_draw_note_flip(&note);
    DNCHECK(v9x_draw_note_should_record(&note, 1) == 1);
}

/*
 * A batch that launched nothing does not spend the marker.
 *
 * The ViRGE path can succeed having drawn nothing at all. Recording that
 * would put a submission in the ring that never happened AND leave the
 * frame's real first draw untraced, so the fiction and the omission come
 * together.
 */
static void test_empty_batch_keeps_the_marker(void)
{
    struct v9x_draw_note note;

    v9x_draw_note_reset(&note);
    v9x_draw_note_flip(&note);
    DNCHECK(v9x_draw_note_should_record(&note, 0) == 0);
    DNCHECK(note.armed == (v9x_u16)1u);
    DNCHECK(v9x_draw_note_should_record(&note, 0) == 0);
    DNCHECK(v9x_draw_note_should_record(&note, 1) == 1);
    DNCHECK(v9x_draw_note_should_record(&note, 1) == 0);
}

/*
 * A batch that submitted and then failed IS a submission.
 *
 * v9x_d3d_virge_draw_triangles launches triangles one at a time and returns
 * failure from the middle of a batch, so the earlier ones are on the
 * engine. The caller passes submitted because something launched; the
 * return value never reaches this rule.
 */
static void test_partial_failure_is_a_submission(void)
{
    struct v9x_draw_note note;

    v9x_draw_note_reset(&note);
    v9x_draw_note_flip(&note);
    DNCHECK(v9x_draw_note_should_record(&note, 1) == 1);
    DNCHECK(note.armed == (v9x_u16)0u);
}

/* A refused batch that launched nothing leaves the marker for the
 * submission that follows it. */
static void test_refusal_defers_to_the_next_submission(void)
{
    struct v9x_draw_note note;

    v9x_draw_note_reset(&note);
    v9x_draw_note_flip(&note);
    DNCHECK(v9x_draw_note_should_record(&note, 0) == 0);
    DNCHECK(v9x_draw_note_should_record(&note, 1) == 1);
}

/* A null state decides nothing and does not fault. */
static void test_null_is_harmless(void)
{
    DNCHECK(v9x_draw_note_should_record(0, 1) == 0);
    v9x_draw_note_reset(0);
    v9x_draw_note_flip(0);
}

unsigned int v9x_run_drawnote_tests(void)
{
    test_starts_disarmed();
    test_one_record_per_flip();
    test_empty_batch_keeps_the_marker();
    test_partial_failure_is_a_submission();
    test_refusal_defers_to_the_next_submission();
    test_null_is_harmless();
    return drawnote_failures;
}
