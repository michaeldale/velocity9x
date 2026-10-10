/*
 * The HAL trace ring's repeat folding. See include\velocity9x\trace_fold.h
 * for why a repeat is counted rather than appended, and why a pair keeps a
 * count on each half.
 */
#include "velocity9x/trace_fold.h"

v9x_u16 v9x_trace_fold_base(v9x_u16 entry_id)
{
    return (v9x_u16)(entry_id & V9X_TRACE_FOLD_ID_MASK);
}

v9x_u16 v9x_trace_fold_count(v9x_u16 entry_id)
{
    return (v9x_u16)((entry_id >> V9X_TRACE_FOLD_COUNT_SHIFT) &
                     V9X_TRACE_FOLD_COUNT_MAX);
}

v9x_u16 v9x_trace_fold_bump(v9x_u16 entry_id)
{
    if (v9x_trace_fold_count(entry_id) >= V9X_TRACE_FOLD_COUNT_MAX) {
        return entry_id;
    }

    return (v9x_u16)(entry_id + (1u << V9X_TRACE_FOLD_COUNT_SHIFT));
}

/* Whether entry is event id - same trace id, same direction - whatever it
 * has counted. Id 0, an empty slot, is no event. */
static int v9x_trace_fold_same(v9x_u16 entry_id, v9x_u16 id)
{
    if (v9x_trace_fold_base(entry_id) == 0u) {
        return 0;
    }

    return v9x_trace_fold_base(entry_id) == v9x_trace_fold_base(id) &&
           (entry_id & V9X_TRACE_FOLD_EXIT_FLAG) ==
               (id & V9X_TRACE_FOLD_EXIT_FLAG);
}

v9x_u32 v9x_trace_fold_decide(v9x_u16 previous_id, v9x_u16 newest_id,
                              v9x_u16 id, v9x_u32 detail,
                              v9x_u32 newest_detail)
{
    v9x_u16 enter = (v9x_u16)(id & (v9x_u16)~V9X_TRACE_FOLD_EXIT_FLAG);
    v9x_u16 exit = (v9x_u16)(enter | V9X_TRACE_FOLD_EXIT_FLAG);
    v9x_u16 previous_count = v9x_trace_fold_count(previous_id);
    v9x_u16 newest_count = v9x_trace_fold_count(newest_id);

    if ((id & V9X_TRACE_FOLD_EXIT_FLAG) != 0u) {
        /*
         * The exit of a pair whose enter was just counted: the enter is one
         * ahead and the result matches. A different result is a different
         * outcome and gets its own entry.
         */
        if (v9x_trace_fold_same(newest_id, exit) &&
            v9x_trace_fold_same(previous_id, enter) &&
            newest_detail == detail &&
            previous_count == (v9x_u16)(newest_count + 1u)) {
            return V9X_TRACE_FOLD_NEWEST;
        }
        return V9X_TRACE_FOLD_APPEND;
    }

    /* The same event again with nothing in between. For an event without an
     * exit this is the whole repeat; for one with an exit it is a call
     * entered again before the first returned, which the count still shows
     * against the exits that follow. */
    if (v9x_trace_fold_same(newest_id, enter)) {
        return newest_count < V9X_TRACE_FOLD_COUNT_MAX
                   ? V9X_TRACE_FOLD_NEWEST
                   : V9X_TRACE_FOLD_APPEND;
    }

    /* The pair again. Only a closed pair - every counted enter has its
     * exit - takes another enter; an open one keeps the unreturned call as
     * the last thing in the ring. */
    if (v9x_trace_fold_same(previous_id, enter) &&
        v9x_trace_fold_same(newest_id, exit) &&
        previous_count == newest_count &&
        previous_count < V9X_TRACE_FOLD_COUNT_MAX) {
        return V9X_TRACE_FOLD_PAIR;
    }

    return V9X_TRACE_FOLD_APPEND;
}
