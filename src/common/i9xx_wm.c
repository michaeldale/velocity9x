/*
 * The Gen3 display FIFO watermark. See include\velocity9x\i9xx_wm.h for why
 * this is a module of its own and what it is for.
 */
#include "velocity9x/i9xx_wm.h"

/*
 * DSPARB's plane-start fields, as i915 reads them in i9xx_get_fifo_size:
 * BSTART in bits 6:0 and CSTART in 13:7, each a count of FIFO entries. So
 * plane A's size is BSTART and plane B's is CSTART less BSTART.
 *
 * The first cut of this file had them at 9 and 16, which intel93 refused
 * rather than believed: the netbook's DSPARB reads 0x00001D9C, whose bits
 * above 13 are zero, so the wrong shifts gave a CSTART of nothing and the
 * split declined. With the right ones it is plane A 28, plane B 31 - a
 * plausible partition of a FIFO this size, and the arithmetic then has an
 * answer. The refusal is why a wrong number was never reported as a right
 * one.
 */
#define V9X_I9XX_DSPARB_BSTART_SHIFT 0u
#define V9X_I9XX_DSPARB_CSTART_SHIFT 7u
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
     * What is left of the FIFO once that much is in flight, less the guard,
     * then the three clamps intel_calculate_wm applies IN ITS ORDER: the
     * maximum, then the default for a result with no room in it, then the
     * burst floor last of all.
     *
     * The order is the point. This file's first cut returned the default
     * early for the no-room case, which meant a tight mode escaped the
     * floor entirely and got 1 - and the host test agreed with it, because
     * the test was written from the same misreading. Upstream lets the
     * default fall through to the floor and so the answer there is 8.
     */
    if (fifo_size <= entries + V9X_I9XX_WM_GUARD) {
        size = V9X_I9XX_WM_DEFAULT;
    } else {
        size = fifo_size - entries - V9X_I9XX_WM_GUARD;
        if (size > V9X_I9XX_WM_MAX) {
            size = V9X_I9XX_WM_MAX;
        }
    }

    if (size < V9X_I9XX_WM_MIN_BURST) {
        return V9X_I9XX_WM_MIN_BURST;
    }

    return size;
}

v9x_u32 v9x_i9xx_wm_cpp_from_dspcntr(v9x_u32 dspcntr)
{
    /*
     * DISPPLANE_* from i915, bits 29:26. Only the formats a Gen3 plane can
     * be programmed to are named; anything else returns zero and the caller
     * computes nothing.
     */
    switch ((dspcntr & 0x3c000000ul) >> 26) {
    case 0x2ul:                 /* 8bpp indexed                            */
        return 1ul;
    case 0x3ul:                 /* BGRA5551                                */
    case 0x4ul:                 /* BGRX5551                                */
    case 0x5ul:                 /* BGRX565 - the netbook's                 */
        return 2ul;
    case 0x6ul:                 /* BGRX8888                                */
    case 0x7ul:                 /* BGRA8888                                */
    case 0x8ul:                 /* RGBX1010102                             */
    case 0x9ul:                 /* RGBA1010102                             */
    case 0xaul:                 /* BGRX1010102                             */
    case 0xeul:                 /* RGBX8888                                */
    case 0xful:                 /* RGBA8888                                */
        return 4ul;
    case 0xcul:                 /* RGBX16161616F                           */
        return 8ul;
    default:
        return 0ul;
    }
}

v9x_u32 v9x_i9xx_wm_fw_blc(v9x_u32 plane_a_wm, v9x_u32 plane_b_wm)
{
    /* Bits 8 and 24 are the burst lengths, one each for plane A and plane
     * B, and i915 writes them set on every update. */
    return ((plane_b_wm & 0x3ful) << 16) | (plane_a_wm & 0x3ful) |
           ((v9x_u32)1ul << 24) | ((v9x_u32)1ul << 8);
}

v9x_u32 v9x_i9xx_wm_fw_blc_merge(v9x_u32 existing, v9x_u32 plane_a_wm,
                                 v9x_u32 plane_b_wm)
{
    return (existing & ~V9X_I9XX_WM_FW_BLC_MANAGED) |
           (v9x_i9xx_wm_fw_blc(plane_a_wm, plane_b_wm) &
            V9X_I9XX_WM_FW_BLC_MANAGED);
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
