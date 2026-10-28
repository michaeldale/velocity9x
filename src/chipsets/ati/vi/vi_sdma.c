/*
 * AMD Carrizo SDMA packets for Evergreen's DirectDraw plans; see
 * include\velocity9x\vi_sdma.h.
 */
#include "velocity9x/vi_sdma.h"

/* The highest low half MC_VM_FB_LOCATION may give: past it, the ring
 * window's 244 MiB could carry into the high half. */
#define V9X_VI_FB_LOW_MAX 0xf0000000ul

static v9x_u32 v9x_vi_header(v9x_u32 op, v9x_u32 sub)
{
    return (op & 0xfful) | ((sub & 0xfful) << 8);
}

static v9x_u32 v9x_vi_fill_packet(v9x_u32 destination, v9x_u32 high,
                                  v9x_u32 value, v9x_u32 bytes, v9x_u32 *out)
{
    out[0] = v9x_vi_header(V9X_VI_SDMA_OP_CONST_FILL, 0ul) |
             (V9X_VI_SDMA_FILL_SIZE_DWORD << V9X_VI_SDMA_FILL_SIZE_SHIFT);
    out[1] = destination;
    out[2] = high;
    out[3] = value;
    out[4] = bytes;
    return V9X_VI_SDMA_FILL_DWORDS;
}

v9x_status v9x_vi_sdma_plan_check(const struct v9x_eg_dma_plan *plan)
{
    if (plan == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (plan->row_bytes == 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (plan->kind == V9X_EG_DMA_PLAN_FILL) {
        return plan->row_bytes <= 2ul * V9X_VI_SDMA_CHUNK_BYTES_MAX ?
               V9X_STATUS_OK : V9X_STATUS_UNSUPPORTED;
    }
    /* A copy row is never split: two packets in either order would read
     * what the other wrote when the run overlaps itself. */
    if (plan->kind == V9X_EG_DMA_PLAN_COPY) {
        return plan->row_bytes <= V9X_VI_SDMA_CHUNK_BYTES_MAX ?
               V9X_STATUS_OK : V9X_STATUS_UNSUPPORTED;
    }
    return V9X_STATUS_UNSUPPORTED;
}

v9x_u32 v9x_vi_sdma_plan_row(const struct v9x_eg_dma_plan *plan,
                             v9x_u32 index, v9x_u32 address_high,
                             v9x_u32 *out)
{
    v9x_u32 destination;
    v9x_u32 source;
    v9x_u32 count;

    if (plan == 0 || out == 0 || index >= plan->rows ||
        v9x_vi_sdma_plan_check(plan) != V9X_STATUS_OK) {
        return 0ul;
    }
    destination = plan->destination + index * plan->destination_step;
    if (plan->kind == V9X_EG_DMA_PLAN_FILL) {
        if (plan->row_bytes <= V9X_VI_SDMA_CHUNK_BYTES_MAX) {
            return v9x_vi_fill_packet(destination, address_high, plan->value,
                                      plan->row_bytes, out);
        }
        /* A fill is the same value everywhere, so its halves may go in
         * either order. The chunk is a dword multiple, so the second
         * starts on a dword as the first did. */
        count = v9x_vi_fill_packet(destination, address_high, plan->value,
                                   V9X_VI_SDMA_CHUNK_BYTES_MAX, out);
        return count + v9x_vi_fill_packet(
            destination + V9X_VI_SDMA_CHUNK_BYTES_MAX, address_high,
            plan->value, plan->row_bytes - V9X_VI_SDMA_CHUNK_BYTES_MAX,
            out + count);
    }

    /* COPY_LINEAR counts bytes whatever the alignment, so the plan's
     * dword and byte copies are the same packet. */
    source = plan->source + index * plan->source_step;
    out[0] = v9x_vi_header(V9X_VI_SDMA_OP_COPY, V9X_VI_SDMA_SUB_LINEAR);
    out[1] = plan->row_bytes;
    out[2] = 0ul;
    out[3] = source;
    out[4] = address_high;
    out[5] = destination;
    out[6] = address_high;
    return V9X_VI_SDMA_COPY_DWORDS;
}

v9x_u32 v9x_vi_sdma_fence(v9x_u32 address_low, v9x_u32 address_high,
                          v9x_u32 value, v9x_u32 *out)
{
    out[0] = v9x_vi_header(V9X_VI_SDMA_OP_FENCE, 0ul);
    out[1] = address_low & ~3ul;
    out[2] = address_high;
    out[3] = value;
    return V9X_VI_SDMA_FENCE_DWORDS;
}

v9x_u32 v9x_vi_sdma_pad(v9x_u32 position)
{
    v9x_u32 tail = position % V9X_VI_SDMA_ALIGN_DWORDS;

    return tail == 0ul ? 0ul : V9X_VI_SDMA_ALIGN_DWORDS - tail;
}

v9x_u32 v9x_vi_sdma_ring_advance(v9x_u32 wptr, v9x_u32 count)
{
    return (wptr + count + v9x_vi_sdma_pad(wptr + count)) &
           (V9X_VI_SDMA_RING_DWORDS - 1ul);
}

v9x_status v9x_vi_fb_start(v9x_u32 fb_location, v9x_u32 *high,
                           v9x_u32 *low)
{
    v9x_u32 field = fb_location & V9X_VI_FB_START_MASK;
    v9x_u32 start_low;

    if (high == 0 || low == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    start_low = (field & 0xfful) << 24;
    if (start_low > V9X_VI_FB_LOW_MAX) {
        return V9X_STATUS_UNSUPPORTED;
    }
    *high = field >> 8;
    *low = start_low;
    return V9X_STATUS_OK;
}
