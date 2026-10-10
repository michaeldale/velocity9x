/*
 * Whether a new HAL trace event is appended to the ring or counted on an
 * entry already there: the decision half.
 *
 * The trace ring (V9X_DD_TRACE in win9x_ddraw_abi.h) is 56 entries deep. An
 * application that frees 546 textures on its way out writes 1,092 enter/exit
 * records, and the snapshot then shows nothing but the teardown: the five
 * Carmageddon II reports of 2026-10-10 (V9X-3D8STZ and others,
 * docs\issues\2026-10-10-carmageddon2-exits-after-intro-gma900.md) ended in
 * 56 DestroySurface records, and whatever the game did before it chose to
 * quit had been overwritten.
 *
 * So a repeat is counted instead of appended. Two shapes repeat:
 *
 *   - an enter/exit pair, the same callback returning the same result again
 *     and again (DestroySurface during a teardown, Flip in a render loop);
 *   - an event with no exit, pushed again (D3dTargetLayout, RenderLoop).
 *
 * A pair is folded only once the new call's exit is in and matches the pair
 * before it: the enter is appended as usual, and when its exit returns the
 * same result as that pair's, the pair absorbs it and the tentative enter is
 * taken back out. So a call that never returned is the last entry on its own,
 * which is what the ring exists to show, and two calls with different
 * results are never merged - the first version of this rule folded the
 * enter early, and on the ViRGE guest (2026-10-11) it put one GetDriverInfo
 * GUID against another's result.
 *
 * Encoding: the count of further occurrences sits in bits 6-14 of the entry's
 * id, between the id (bits 0-5, every trace id is below 64) and the exit flag
 * (bit 15). 0 means the event happened once. A count at its maximum stops
 * folding and the next occurrence starts a new entry, so the 546-texture
 * teardown takes four entries rather than 1,092.
 *
 * This header and src\common\trace_fold.c hold the rule and nothing else; the
 * HAL's writer applies it, host-tested in tests\host\test_trace_fold.c. The
 * 16-bit writer in dd16.c does not fold: its events are a handful per
 * DirectDraw object, and it writes a count of zero, which is correct.
 */
#ifndef VELOCITY9X_TRACE_FOLD_H
#define VELOCITY9X_TRACE_FOLD_H

#include "velocity9x/types.h"

/* Equal to V9X_DD_TRACE_EXIT_FLAG; ddhal_core.c asserts it. */
#define V9X_TRACE_FOLD_EXIT_FLAG   ((v9x_u16)0x8000u)
#define V9X_TRACE_FOLD_ID_MASK     ((v9x_u16)0x003Fu)
#define V9X_TRACE_FOLD_COUNT_SHIFT 6u
#define V9X_TRACE_FOLD_COUNT_MAX   ((v9x_u16)0x01FFu)

/* What the writer does with the new event. */
#define V9X_TRACE_FOLD_APPEND   0u  /* write it to the next slot             */
#define V9X_TRACE_FOLD_NEWEST   1u  /* count it on the newest entry          */
/* The event is the exit closing a repeat of the pair before: count one more
 * on that pair's enter (third) and exit (previous), give the enter the
 * newest entry's argument, and take the newest entry back out. */
#define V9X_TRACE_FOLD_CLOSE    2u

/*
 * The decision for event (id, detail), id carrying no count, given the three
 * newest entries as they stand - newest the last one written, previous and
 * third the two before it - and previous's detail. An empty slot is id 0,
 * which no event uses.
 */
v9x_u32 v9x_trace_fold_decide(v9x_u16 third_id, v9x_u16 previous_id,
                              v9x_u16 newest_id, v9x_u32 previous_detail,
                              v9x_u16 id, v9x_u32 detail);

/* The trace id an entry names, without its count or exit flag. */
v9x_u16 v9x_trace_fold_base(v9x_u16 entry_id);

/* Further occurrences an entry has counted; 0 for an event seen once. */
v9x_u16 v9x_trace_fold_count(v9x_u16 entry_id);

/* The entry id with one more occurrence counted. The caller has had a fold
 * decision, so the count is below its maximum. */
v9x_u16 v9x_trace_fold_bump(v9x_u16 entry_id);

#endif
