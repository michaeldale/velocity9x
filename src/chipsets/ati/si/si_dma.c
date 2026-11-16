/*
 * AMD Southern Islands DMA packets for Evergreen's DirectDraw plans; see
 * include\velocity9x\si_dma.h.
 */
#include "velocity9x/si_dma.h"

/* sid.h DMA_PACKET's fields, and the high halves' places: a copy's in
 * dwords 3 and 4 bits 7:0, a fill's in dword 3 bits 23:16
 * (amdgpu si_dma_emit_copy_buffer and si_dma_emit_fill_buffer). */
#define V9X_SI_DMA_COMMAND_SHIFT        28
#define V9X_SI_DMA_COUNT_MASK           0x000ffffful
#define V9X_SI_DMA_HIGH_MASK            0x000000fful
#define V9X_SI_DMA_FILL_HIGH_SHIFT      16

static v9x_u32 v9x_si_packet(v9x_u32 command, v9x_u32 flags, v9x_u32 count)
{
    return (command << V9X_SI_DMA_COMMAND_SHIFT) | flags |
           (count & V9X_SI_DMA_COUNT_MASK);
}

/* Packets a row of row_bytes takes in chunks of the most one may carry. */
static v9x_u32 v9x_si_chunks(v9x_u32 row_bytes)
{
    return row_bytes / V9X_SI_DMA_CHUNK_BYTES_MAX +
           (row_bytes % V9X_SI_DMA_CHUNK_BYTES_MAX != 0ul ? 1ul : 0ul);
}

/* The span the rows of one side cover, [*start, *end), from the first
 * row's address and a step that is negative when the plan runs
 * bottom-up. Zero when it wraps the 32-bit space. */
static int v9x_si_span(v9x_u32 first, v9x_u32 step, v9x_u32 rows,
                       v9x_u32 row_bytes, v9x_u32 *start, v9x_u32 *end)
{
    v9x_u32 last;
    v9x_u32 low;
    v9x_u32 high;

    last = first + (rows - 1ul) * step;
    low = first;
    high = last;
    if ((step & 0x80000000ul) != 0ul) {
        low = last;
        high = first;
    }
    if (high > 0xfffffffful - row_bytes) {
        return 0;
    }
    *start = low;
    *end = high + row_bytes;
    return 1;
}

/* Non-zero when some source row and some destination row of a plan whose
 * two sides step by the same pitch share a byte. Row i of the source and
 * row i + k of the destination are delta + k * pitch apart (delta the
 * first rows' distance), so they overlap when that is within row_bytes of
 * 0; it is smallest at the k nearest -delta / pitch, clamped to the rows
 * there are. Both sides are inside one heap well under 2 GiB, so the
 * distances fit a long. */
static int v9x_si_rows_overlap(long delta, long pitch, long rows,
                               long row_bytes)
{
    long k;
    long distance;
    int pass;

    if (pitch < 0l) {
        pitch = -pitch;
    }
    if (pitch == 0l || rows == 1l) {
        distance = delta < 0l ? -delta : delta;
        return distance < row_bytes;
    }
    k = -delta / pitch;
    for (pass = 0; pass < 3; ++pass) {
        long candidate = k - 1l + (long)pass;

        if (candidate < 1l - rows) {
            candidate = 1l - rows;
        }
        if (candidate > rows - 1l) {
            candidate = rows - 1l;
        }
        distance = delta + candidate * pitch;
        if (distance < 0l) {
            distance = -distance;
        }
        if (distance < row_bytes) {
            return 1;
        }
    }
    return 0;
}

v9x_status v9x_si_dma_plan_check(const struct v9x_eg_dma_plan *plan)
{
    v9x_u32 source_start;
    v9x_u32 source_end;
    v9x_u32 destination_start;
    v9x_u32 destination_end;

    if (plan == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (plan->rows == 0ul || plan->row_bytes == 0ul ||
        v9x_si_chunks(plan->row_bytes) > V9X_SI_DMA_ROW_PACKETS_MAX) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (plan->kind == V9X_EG_DMA_PLAN_FILL) {
        /* The fill writes whole dwords (eg_dma.c refuses any other). */
        return (plan->row_bytes & 3ul) == 0ul ?
               V9X_STATUS_OK : V9X_STATUS_UNSUPPORTED;
    }
    if (plan->kind != V9X_EG_DMA_PLAN_COPY) {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* Every SI copy is a byte copy, and no byte copy has been measured
     * overlapping itself on either engine: refuse any overlap, between
     * rows or inside one, rather than trust the plan's row order. */
    if (!v9x_si_span(plan->source, plan->source_step, plan->rows,
                     plan->row_bytes, &source_start, &source_end) ||
        !v9x_si_span(plan->destination, plan->destination_step, plan->rows,
                     plan->row_bytes, &destination_start,
                     &destination_end)) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (source_start >= destination_end || destination_start >= source_end) {
        return V9X_STATUS_OK;
    }

    /* The spans interleave, as two rectangles side by side on one surface
     * do. With one pitch on both sides the rows can be compared exactly;
     * with two, the interleave is refused. */
    if (plan->source_step != plan->destination_step ||
        source_end - source_start > 0x7ffffffful ||
        destination_end - destination_start > 0x7ffffffful) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (v9x_si_rows_overlap((long)(plan->destination - plan->source),
                            (long)plan->source_step, (long)plan->rows,
                            (long)plan->row_bytes)) {
        return V9X_STATUS_UNSUPPORTED;
    }
    return V9X_STATUS_OK;
}

v9x_u32 v9x_si_dma_plan_row(const struct v9x_eg_dma_plan *plan,
                            v9x_u32 index, v9x_u32 address_high,
                            v9x_u32 *out)
{
    v9x_u32 destination;
    v9x_u32 source;
    v9x_u32 remaining;
    v9x_u32 chunk;
    v9x_u32 high;
    v9x_u32 used;

    if (plan == 0 || out == 0 || index >= plan->rows ||
        v9x_si_dma_plan_check(plan) != V9X_STATUS_OK) {
        return 0ul;
    }
    high = address_high & V9X_SI_DMA_HIGH_MASK;
    destination = plan->destination + index * plan->destination_step;
    source = plan->source + index * plan->source_step;

    /* Chunks in address order. A fill is the same value everywhere and a
     * copy's spans do not overlap (checked above), so the order is free;
     * every chunk but the last is FFFF8h bytes, so each starts on a dword
     * where the row did. */
    used = 0ul;
    for (remaining = plan->row_bytes; remaining != 0ul;
         remaining -= chunk) {
        chunk = remaining < V9X_SI_DMA_CHUNK_BYTES_MAX ?
                remaining : V9X_SI_DMA_CHUNK_BYTES_MAX;
        if (plan->kind == V9X_EG_DMA_PLAN_FILL) {
            out[used] = v9x_si_packet(V9X_EG_DMA_CMD_CONSTANT_FILL, 0ul,
                                      chunk / 4ul);
            out[used + 1ul] = destination;
            out[used + 2ul] = plan->value;
            out[used + 3ul] = high << V9X_SI_DMA_FILL_HIGH_SHIFT;
            used += V9X_SI_DMA_FILL_DWORDS;
        } else {
            out[used] = v9x_si_packet(V9X_EG_DMA_CMD_COPY,
                                      V9X_SI_DMA_COPY_BYTES_BIT, chunk);
            out[used + 1ul] = destination;
            out[used + 2ul] = source;
            out[used + 3ul] = high;
            out[used + 4ul] = high;
            used += V9X_SI_DMA_COPY_DWORDS;
            source += chunk;
        }
        destination += chunk;
    }
    return used;
}
