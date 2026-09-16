#include "velocity9x/intel_gma.h"

static void v9x_i9xx_zero_layout(struct v9x_i9xx_sandbox_layout *layout)
{
    v9x_u32 *word = (v9x_u32 *)layout;
    v9x_u16 index;
    for (index = 0u;
         index < (v9x_u16)(sizeof(*layout) / sizeof(v9x_u32)); ++index) {
        word[index] = 0ul;
    }
}

static v9x_u16 v9x_i9xx_is_power_of_two(v9x_u32 value)
{
    return value != 0ul && (value & (value - 1ul)) == 0ul;
}

v9x_status V9X_I9XX_FAR v9x_i9xx_sandbox_calculate(
    v9x_u32 vbe_bytes, v9x_u32 bsm,
    struct v9x_i9xx_sandbox_layout *layout)
{
    v9x_u32 reserve_offset;

    if (layout == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_i9xx_zero_layout(layout);
    if (vbe_bytes < V9X_I9XX_GTT_RESERVE_BYTES ||
        (vbe_bytes & (V9X_I9XX_SANDBOX_PAGE_BYTES - 1ul)) != 0ul ||
        (bsm & (V9X_I9XX_SANDBOX_PAGE_BYTES - 1ul)) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    reserve_offset = vbe_bytes - V9X_I9XX_GTT_RESERVE_BYTES;
    if (bsm > 0xfffffffful - vbe_bytes) {
        return V9X_STATUS_INTEGER_OVERFLOW;
    }

    layout->heap_bytes = reserve_offset;
    layout->reserve_offset = reserve_offset;
    layout->reserve_physical = bsm + reserve_offset;
    layout->ring_offset = reserve_offset;
    layout->ring_physical = layout->reserve_physical;
    layout->ring_bytes = V9X_I9XX_RING_BYTES;
    layout->hws_offset = reserve_offset + V9X_I9XX_RING_BYTES;
    layout->hws_physical = bsm + layout->hws_offset;
    layout->scratch_offset = layout->hws_offset +
                             V9X_I9XX_SANDBOX_PAGE_BYTES;
    layout->scratch_physical = bsm + layout->scratch_offset;
    layout->scratch_bytes = V9X_I9XX_SANDBOX_PAGE_BYTES;

    /*
     * The render target follows the scratch page, which therefore doubles as
     * its lower guard, and an upper guard page follows the target. The scratch
     * page's position is deliberately still reserve + 0x11000 whatever the
     * reserve size, because loader.asm pins that offset and the two guard
     * probes at +0x11000 and +0x11ffc; only the base moved at Phase 5.
     */
    layout->target_offset = layout->scratch_offset +
                            V9X_I9XX_SANDBOX_PAGE_BYTES;
    layout->target_physical = bsm + layout->target_offset;
    layout->target_bytes = V9X_I9XX_TARGET_BYTES;
    layout->target_pitch = V9X_I9XX_TARGET_PITCH;
    layout->guard_upper_offset = layout->target_offset +
                                 V9X_I9XX_TARGET_BYTES;
    layout->guard_upper_physical = bsm + layout->guard_upper_offset;

    /*
     * The texture takes the page above the target's upper guard, so that
     * guard serves as its lower one, and a guard page of its own follows.
     *
     * Page aligned because the reserve is allocated in pages and MAP_STATE's
     * address requirement is not stated by either reference tree - the audit
     * records that as a choice rather than a finding, and a page cannot be
     * less safe than a smaller alignment.
     */
    layout->texture_offset = layout->guard_upper_offset +
                             V9X_I9XX_SANDBOX_PAGE_BYTES;
    layout->texture_physical = bsm + layout->texture_offset;
    layout->texture_bytes = V9X_I9XX_TEXTURE_BYTES;
    layout->texture_pitch = V9X_I9XX_TEXTURE_PITCH;
    layout->texture_guard_offset = layout->texture_offset +
                                   V9X_I9XX_SANDBOX_PAGE_BYTES;
    layout->texture_guard_physical = bsm + layout->texture_guard_offset;

    /*
     * Everything above must fit inside the reserve. This is arithmetic on
     * compile-time constants today, but it is checked rather than asserted in
     * a comment: the target size and the reserve size are separate constants,
     * and a future mode change that grows one without the other would
     * otherwise silently place the texture and its guard - or part of the
     * target - in the published DirectDraw heap.
     */
    if (layout->texture_guard_offset + V9X_I9XX_SANDBOX_PAGE_BYTES >
            reserve_offset + V9X_I9XX_GTT_RESERVE_BYTES) {
        v9x_i9xx_zero_layout(layout);
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }

    return V9X_STATUS_OK;
}

v9x_status v9x_i9xx_ring_free_space(
    v9x_u32 head, v9x_u32 tail, v9x_u32 ring_bytes,
    v9x_u32 *free_bytes)
{
    if (free_bytes == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *free_bytes = 0ul;
    if (ring_bytes < V9X_I9XX_SANDBOX_PAGE_BYTES ||
        v9x_i9xx_is_power_of_two(ring_bytes) == V9X_FALSE ||
        head >= ring_bytes || tail >= ring_bytes ||
        (head & 7ul) != 0ul || (tail & 7ul) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *free_bytes = (head - tail - V9X_I9XX_RING_GUARD_BYTES) &
                  (ring_bytes - 1ul);
    return V9X_STATUS_OK;
}

v9x_status v9x_i9xx_ring_plan(
    v9x_u32 head, v9x_u32 tail, v9x_u32 ring_bytes,
    v9x_u32 command_dwords, struct v9x_i9xx_ring_plan *plan)
{
    v9x_u32 free_bytes;
    v9x_u32 command_bytes;
    v9x_u32 remaining;
    v9x_u32 consumed;

    if (plan == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    plan->command_tail = 0ul;
    plan->next_tail = 0ul;
    plan->pad_dwords = 0ul;
    plan->command_dwords = 0ul;
    plan->consumed_bytes = 0ul;
    if (command_dwords == 0ul || (command_dwords & 1ul) != 0ul ||
        command_dwords > 0x3ffffffful ||
        v9x_i9xx_ring_free_space(head, tail, ring_bytes, &free_bytes) !=
            V9X_STATUS_OK) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    command_bytes = command_dwords * 4ul;
    if (command_bytes > ring_bytes - V9X_I9XX_RING_GUARD_BYTES) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    remaining = ring_bytes - tail;
    consumed = command_bytes;
    plan->command_tail = tail;
    if (command_bytes > remaining) {
        plan->pad_dwords = remaining / 4ul;
        plan->command_tail = 0ul;
        consumed += remaining;
    }
    if (consumed > free_bytes) {
        plan->command_tail = 0ul;
        plan->pad_dwords = 0ul;
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    plan->command_dwords = command_dwords;
    plan->consumed_bytes = consumed;
    plan->next_tail = (plan->command_tail + command_bytes) &
                      (ring_bytes - 1ul);
    return V9X_STATUS_OK;
}

v9x_status v9x_i9xx_build_mi_probe(v9x_u32 *stream, v9x_u32 capacity,
                                    v9x_u32 *written)
{
    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0 || capacity < 2ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    stream[0] = V9X_I9XX_MI_NOOP;
    stream[1] = V9X_I9XX_MI_FLUSH;
    *written = 2ul;
    return V9X_STATUS_OK;
}

/*
 * Shift-add 32-bit multiply. Open Watcom would otherwise emit a call to its
 * __U4M helper, which lives in clibc.lib's _TEXT and so cannot be reached by
 * a near call from the I9XXCODE segment this unit is compiled into
 * (docs\plans\intel-gma950-phase5.md). __U4M is compiler-generated and cannot
 * be declared __far, so the multiply has to go at source rather than be
 * redirected. Both call sites are bounds arithmetic run a handful of times per
 * ring attempt, so the loop's cost is unobservable; test_i9xx_ring.c's
 * existing expectations are the proof of equivalence.
 */
static v9x_u32 v9x_i9xx_mul32(v9x_u32 left, v9x_u32 right)
{
    v9x_u32 result = 0ul;

    while (right != 0ul) {
        if ((right & 1ul) != 0ul) {
            result += left;
        }
        left <<= 1;
        right >>= 1;
    }

    return result;
}

v9x_status v9x_i9xx_build_color_blt(
    v9x_u32 destination, v9x_u16 width, v9x_u16 height,
    v9x_u16 pitch, v9x_u32 color,
    v9x_u32 scratch_offset, v9x_u32 scratch_bytes,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 row_bytes = (v9x_u32)width * 4ul;
    v9x_u32 extent;
    v9x_u32 scratch_end;

    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0 || capacity < 6ul ||
        width == 0u || height == 0u || pitch == 0u ||
        pitch >= 32768u || (pitch & 3u) != 0u || row_bytes > pitch ||
        scratch_bytes == 0ul ||
        scratch_offset > 0xfffffffful - scratch_bytes) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    scratch_end = scratch_offset + scratch_bytes;
    extent = v9x_i9xx_mul32((v9x_u32)height - 1ul, (v9x_u32)pitch);
    if (extent > 0xfffffffful - row_bytes ||
        destination < scratch_offset ||
        destination > 0xfffffffful - extent - row_bytes ||
        destination + extent + row_bytes > scratch_end) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }

    stream[0] = V9X_I9XX_XY_COLOR_BLT;
    stream[1] = V9X_I9XX_BLT_DEPTH_32 | V9X_I9XX_BLT_ROP_PATCOPY |
                (v9x_u32)pitch;
    stream[2] = 0ul;
    stream[3] = ((v9x_u32)height << 16) | (v9x_u32)width;
    stream[4] = destination;
    stream[5] = color;
    *written = 6ul;
    return V9X_STATUS_OK;
}

v9x_status v9x_i9xx_decode_phase4_stream(
    const v9x_u32 *stream, v9x_u32 dword_count,
    v9x_u32 scratch_offset, v9x_u32 scratch_bytes)
{
    v9x_u32 index = 0ul;
    v9x_u32 scratch_end;

    if (stream == 0 || dword_count == 0ul || (dword_count & 1ul) != 0ul ||
        scratch_bytes == 0ul ||
        scratch_offset > 0xfffffffful - scratch_bytes) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    scratch_end = scratch_offset + scratch_bytes;
    while (index < dword_count) {
        v9x_u32 command = stream[index];
        if (command == V9X_I9XX_MI_NOOP || command == V9X_I9XX_MI_FLUSH) {
            ++index;
        } else if (command == V9X_I9XX_XY_COLOR_BLT) {
            v9x_u32 br13;
            v9x_u32 dimensions;
            v9x_u32 width;
            v9x_u32 height;
            v9x_u32 pitch;
            v9x_u32 destination;
            v9x_u32 extent;
            if (dword_count - index < 6ul) {
                return V9X_STATUS_INVALID_ARGUMENT;
            }
            br13 = stream[index + 1ul];
            dimensions = stream[index + 3ul];
            width = dimensions & 0xfffful;
            height = dimensions >> 16;
            pitch = br13 & 0xfffful;
            destination = stream[index + 4ul];
            if (stream[index + 2ul] != 0ul ||
                (br13 & 0xffff0000ul) !=
                    (V9X_I9XX_BLT_DEPTH_32 | V9X_I9XX_BLT_ROP_PATCOPY) ||
                width == 0ul || height == 0ul || pitch >= 32768ul ||
                (pitch & 3ul) != 0ul || width * 4ul > pitch) {
                return V9X_STATUS_INVALID_ARGUMENT;
            }
            extent = v9x_i9xx_mul32(height - 1ul, pitch);
            if (extent > 0xfffffffful - width * 4ul ||
                destination < scratch_offset ||
                destination > 0xfffffffful - extent - width * 4ul ||
                destination + extent + width * 4ul > scratch_end) {
                return V9X_STATUS_INSUFFICIENT_MEMORY;
            }
            index += 6ul;
        } else {
            return V9X_STATUS_UNSUPPORTED;
        }
    }
    return V9X_STATUS_OK;
}
