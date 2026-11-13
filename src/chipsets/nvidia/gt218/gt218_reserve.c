/*
 * The GT218 engine reserve's place: see include\velocity9x\gt218_reserve.h.
 * No I/O; linked into the 16-bit driver and the host tests.
 */
#include "velocity9x/gt218_reserve.h"

unsigned long v9x_gt218_reserve_start(unsigned long usable_bytes,
                                      unsigned long visible_bytes)
{
    unsigned long start;

    if (usable_bytes < V9X_GT218_RESERVE_BYTES) {
        return 0ul;
    }
    /* The last whole megabyte that fits, so the start is aligned even if
     * the host granted an odd-sized mapping. */
    start = (usable_bytes - V9X_GT218_RESERVE_BYTES) &
            ~(V9X_GT218_RESERVE_BYTES - 1ul);
    if (start == 0ul || start < visible_bytes) {
        return 0ul;
    }
    if (start + V9X_GT218_RESERVE_BYTES > V9X_GT218_RESERVE_VM_BYTES) {
        return 0ul;
    }
    return start;
}
