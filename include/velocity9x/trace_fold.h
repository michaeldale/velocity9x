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
 * A pair keeps two counts, one on the enter and one on the exit, and an enter
 * is folded only while they are equal. The ring exists to show the last
 * callback before a fault, and a call that never returned must still read as
 * one: "Flip enter x9, Flip exit x8" says the ninth did not come back.
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
#define V9X_TRACE_FOLD_PAIR     2u  /* count it on the entry before that     */

/*
 * The decision for event (id, detail), id carrying no count, given the two
 * newest entries as they stand: newest is the last one written, previous the
 * one before it. An empty slot is id 0, which no event uses.
 */
v9x_u32 v9x_trace_fold_decide(v9x_u16 previous_id, v9x_u16 newest_id,
                              v9x_u16 id, v9x_u32 detail,
                              v9x_u32 newest_detail);

/* The trace id an entry names, without its count or exit flag. */
v9x_u16 v9x_trace_fold_base(v9x_u16 entry_id);

/* Further occurrences an entry has counted; 0 for an event seen once. */
v9x_u16 v9x_trace_fold_count(v9x_u16 entry_id);

/* The entry id with one more occurrence counted. The caller has had a fold
 * decision, so the count is below its maximum. */
v9x_u16 v9x_trace_fold_bump(v9x_u16 entry_id);

#endif
