/*
 * Intel Gen7.5 (Haswell): pipelined render submission's arithmetic.
 * velocity9x\intel_hsw_pipe.h says what each function answers and why;
 * docs\plans\intel-haswell-pipelined-submission.md is the design.
 */
#include "velocity9x/intel_hsw_pipe.h"

/* A specular or fog flag as one bit of a signature byte: any non-zero is
 * on, as the kernel builder reads them. */
static v9x_u32 v9x_hsw_pipe_flag(v9x_u8 value)
{
    return value != 0u ? 1ul : 0ul;
}

v9x_status v9x_intel_hsw_pipe_slot(v9x_u32 sequence,
                                   struct v9x_hsw_pipe_slot *slot)
{
    v9x_u32 index;

    if (slot == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    index = sequence & (V9X_HSW_PIPE_SLOTS - 1ul);

    /*
     * Slot 0 is the first half of each of today's areas, so a build that
     * only ever used slot 0 would place everything where the synchronous
     * path does. The state blocks are contiguous below the kernels; the
     * span runs to the end of the state area, so the workload's state size
     * covers the kernels from either base.
     */
    slot->index = index;
    slot->state_off = V9X_INTEL_HSW_STATE_OFF +
                      index * V9X_HSW_PIPE_STATE_BLOCK_BYTES;
    slot->state_span = V9X_INTEL_HSW_STATE_OFF + V9X_INTEL_HSW_STATE_BYTES -
                       slot->state_off;
    slot->kernel_base = V9X_INTEL_HSW_STATE_OFF + V9X_INTEL_HSW_KERNEL_OFF -
                        slot->state_off;
    slot->batch_off = V9X_INTEL_HSW_BATCH_OFF +
                      index * V9X_HSW_PIPE_BATCH_BYTES;
    slot->batch_bytes = V9X_HSW_PIPE_BATCH_BYTES;
    slot->vertex_off = V9X_INTEL_HSW_VERTEX_OFF +
                       index * V9X_HSW_PIPE_VERTEX_BYTES;
    slot->vertex_bytes = V9X_HSW_PIPE_VERTEX_BYTES;
    return V9X_STATUS_OK;
}

v9x_u16 v9x_intel_hsw_pipe_may_write(v9x_u32 sent, v9x_u32 done)
{
    /* Outstanding submissions, modulo 2^32. A done ahead of sent wraps to
     * a huge count, so it is never free. */
    return (v9x_u32)(sent - done) <= 1ul ? V9X_TRUE : V9X_FALSE;
}

v9x_u16 v9x_intel_hsw_pipe_idle(v9x_u32 sent, v9x_u32 done)
{
    return sent == done ? V9X_TRUE : V9X_FALSE;
}

v9x_u16 v9x_intel_hsw_pipe_draw_waits(v9x_u16 gdi_may_draw,
                                      v9x_u32 target_offset,
                                      v9x_u32 target_pitch,
                                      v9x_u32 target_height,
                                      v9x_u32 primary_bytes)
{
    v9x_u32 extent;

    if (!gdi_may_draw) {
        return V9X_FALSE;
    }
    if (primary_bytes == 0ul) {
        return V9X_TRUE;
    }
    /* An extent past 4 GiB cannot be placed, so it may cover anything. */
    if (target_height != 0ul && target_pitch > 0xfffffffful / target_height) {
        return V9X_TRUE;
    }
    extent = target_pitch * target_height;
    if (target_offset > 0xfffffffful - extent) {
        return V9X_TRUE;
    }
    /* The primary is [0, primary_bytes): the target overlaps it when it
     * starts below its end. */
    return target_offset < primary_bytes ? V9X_TRUE : V9X_FALSE;
}

v9x_u32 v9x_intel_hsw_pipe_kernel_slot(const struct v9x_hsw_ps_desc *desc)
{
    /* Stage 0 (six), stage 1 (two), specular, fog: 48 distinct slots, the
     * count the builder accepts. Flags count as on when non-zero, as the
     * builder reads them. */
    return (v9x_u32)desc->stage0 * 8ul + (v9x_u32)desc->stage1 * 4ul +
           v9x_hsw_pipe_flag(desc->specular) * 2ul +
           v9x_hsw_pipe_flag(desc->fog);
}

void v9x_intel_hsw_pipe_kernel_sig(const struct v9x_hsw_ps_desc *desc,
                                   v9x_u32 *sig)
{
    v9x_u32 fog;
    v9x_u32 index;

    fog = v9x_hsw_pipe_flag(desc->fog);
    sig[0] = V9X_HSW_PIPE_SIG_VALID | (v9x_u32)desc->stage0 |
             ((v9x_u32)desc->stage1 << 8) | (fog << 16) |
             (v9x_hsw_pipe_flag(desc->specular) << 24);

    /* The colour only matters to a fog kernel (hsw_eu.c reads it only with
     * fog), so without fog it is zero and a stale one costs no drain. */
    for (index = 0ul; index < 3ul; ++index) {
        sig[1ul + index] = fog != 0ul ? desc->fog_rgb[index] : 0ul;
    }
}

v9x_u16 v9x_intel_hsw_pipe_kernel_sig_equal(const v9x_u32 *a,
                                            const v9x_u32 *b)
{
    v9x_u32 index;

    for (index = 0ul; index < V9X_HSW_PIPE_SIG_DWORDS; ++index) {
        if (a[index] != b[index]) {
            return V9X_FALSE;
        }
    }
    return V9X_TRUE;
}
