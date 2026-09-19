/*
 * The Gen3 display FIFO watermark. See include\velocity9x\i9xx_wm.h for why
 * this is a module of its own and what it is for.
 */
#include "velocity9x/i9xx_wm.h"

/*
 * DSPARB's plane-start fields. Plane B starts at bits 15:9 and plane C at
 * 22:16, each a count of FIFO entries, so plane A's size is B's start and
 * plane B's is the difference. i915 reads the same two fields in
 * i9xx_get_fifo_size.
 */
#define V9X_I9XX_DSPARB_BSTART_SHIFT 9u
#define V9X_I9XX_DSPARB_CSTART_SHIFT 16u
#define V9X_I9XX_DSPARB_START_MASK   ((v9x_u32)0x7ful)

/*
 * Rounding up without overflowing. The products below are bounded - a
 * pixel rate of a few hundred thousand kHz times four bytes times a
 * latency in hundreds - but the divide is written once here rather than
 * three times inline.
 */
static v9x_u32 v9x_i9xx_wm_div_up(v9x_u32 value, v9x_u32 divisor)
{
    if (divisor == 0ul) {
        return 0ul;
    }

    return (value + divisor - 1ul) / divisor;
}

v9x_u32 v9x_i9xx_wm_plane(v9x_u32 pixel_rate_khz, v9x_u32 cpp,
                          v9x_u32 fifo_size, v9x_u32 latency_ns)
{
    v9x_u32 entries;
    v9x_u32 size;

    /*
     * An inactive plane gets the whole of its FIFO less the guard, which
     * is what i915 gives one: there is no fetch to be late for.
     */
    if (pixel_rate_khz == 0ul || cpp == 0ul) {
        if (fifo_size <= V9X_I9XX_WM_GUARD) {
            return V9X_I9XX_WM_DEFAULT;
        }
        size = fifo_size - V9X_I9XX_WM_GUARD;
        return size > V9X_I9XX_WM_MAX ? V9X_I9XX_WM_MAX : size;
    }

    /*
     * i915's method 1: the bytes the display will consume during one
     * memory latency, as cachelines. latency_ns / 100 is the hundreds of
     * nanoseconds the divisor below is scaled for.
     */
    entries = v9x_i9xx_wm_div_up(pixel_rate_khz * cpp * (latency_ns / 100ul),
                                 10000ul);
    entries = v9x_i9xx_wm_div_up(entries, V9X_I9XX_WM_CACHELINE);

    /*
     * What is left of the FIFO once that much is in flight, less the
     * guard. Too little room and i915 takes the default rather than zero:
     * a watermark of zero is one no fetch ever satisfies.
     */
    if (fifo_size <= entries + V9X_I9XX_WM_GUARD) {
        return V9X_I9XX_WM_DEFAULT;
    }
    size = fifo_size - entries - V9X_I9XX_WM_GUARD;
    if (size > V9X_I9XX_WM_MAX) {
        return V9X_I9XX_WM_MAX;
    }
    if (size < V9X_I9XX_WM_DEFAULT) {
        return V9X_I9XX_WM_DEFAULT;
    }

    return size;
}

v9x_u32 v9x_i9xx_wm_fw_blc(v9x_u32 plane_a_wm, v9x_u32 plane_b_wm)
{
    /* Bits 8 and 24 are the burst lengths, one each for plane A and plane
     * B, and i915 writes them set on every update. */
    return ((plane_b_wm & 0x3ful) << 16) | (plane_a_wm & 0x3ful) |
           ((v9x_u32)1ul << 24) | ((v9x_u32)1ul << 8);
}

v9x_u16 v9x_i9xx_wm_fifo_split(v9x_u32 dsparb, v9x_u32 *plane_a,
                               v9x_u32 *plane_b)
{
    v9x_u32 b_start;
    v9x_u32 c_start;

    if (plane_a == 0 || plane_b == 0) {
        return V9X_FALSE;
    }
    b_start = (dsparb >> V9X_I9XX_DSPARB_BSTART_SHIFT) &
              V9X_I9XX_DSPARB_START_MASK;
    c_start = (dsparb >> V9X_I9XX_DSPARB_CSTART_SHIFT) &
              V9X_I9XX_DSPARB_START_MASK;

    /* A partition that does not increase leaves a plane no entries at all,
     * which is not a configuration to compute a watermark against. */
    if (b_start == 0ul || c_start <= b_start) {
        return V9X_FALSE;
    }
    *plane_a = b_start;
    *plane_b = c_start - b_start;

    return V9X_TRUE;
}
