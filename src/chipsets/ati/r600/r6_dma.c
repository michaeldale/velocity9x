/*
 * ATI R6xx async DMA packets for DirectDraw fills and copies. See
 * include\velocity9x\r6_dma.h for the contract, the sources it follows and
 * the measured rules it keeps.
 */
#include "velocity9x/r6_dma.h"

#define V9X_R6_U32_MAX 0xfffffffful

/* Bits 39:32 of a GPU address, where each packet and DMA_RB_BASE put
 * them: a copy's dword 3 bits 23:16 for the destination and 7:0 for the
 * source (r600_copy_dma), the ring base's 31:24 once the address is
 * shifted right by 8. */
#define V9X_R6_ADDRESS_HIGH_MASK    0xfful
#define V9X_R6_COPY_DST_HIGH_SHIFT  16
#define V9X_R6_RING_BASE_SHIFT      8
#define V9X_R6_RING_BASE_HIGH_SHIFT 24

/* The narrowest row a COPY moved: one dword did nothing on M-NZHWBR. */
#define V9X_R6_ROW_DWORDS_MIN       2ul

static v9x_u32 v9x_r6_packet(v9x_u32 command, v9x_u32 count)
{
    return ((command & 0xful) << 28) | (count & 0xfffful);
}

/* *product = a * b, non-zero unless it overflows 32 bits. */
static int v9x_r6_mul(v9x_u32 a, v9x_u32 b, v9x_u32 *product)
{
    if (a != 0ul && b > V9X_R6_U32_MAX / a) {
        return 0;
    }
    *product = a * b;
    return 1;
}

/* *sum = a + b, non-zero unless it overflows 32 bits. */
static int v9x_r6_add(v9x_u32 a, v9x_u32 b, v9x_u32 *sum)
{
    if (b > V9X_R6_U32_MAX - a) {
        return 0;
    }
    *sum = a + b;
    return 1;
}

/*
 * One side of an operation: the heap offset of its first byte and of the
 * byte past its last, for a rectangle at (left, top) of width_bytes by
 * height rows on a surface at offset with the given pitch. Non-zero when
 * the rectangle lies inside its pitch and the whole run fits in vram_bytes
 * and, from gpu_base, does not carry out of the address's low 32 bits.
 */
static int v9x_r6_extent(v9x_u32 gpu_base, v9x_u32 vram_bytes,
                         v9x_u32 offset, v9x_u32 pitch, v9x_u32 left_bytes,
                         v9x_u32 top, v9x_u32 width_bytes, v9x_u32 height,
                         v9x_u32 *start, v9x_u32 *end)
{
    v9x_u32 right;
    v9x_u32 rows;
    v9x_u32 last;
    v9x_u32 gpu_end;

    if (!v9x_r6_add(left_bytes, width_bytes, &right) || right > pitch) {
        return 0;
    }
    if (!v9x_r6_mul(top, pitch, &rows) ||
        !v9x_r6_add(offset, rows, start) ||
        !v9x_r6_add(*start, left_bytes, start)) {
        return 0;
    }
    if (!v9x_r6_mul(height - 1ul, pitch, &last) ||
        !v9x_r6_add(*start, last, end) ||
        !v9x_r6_add(*end, width_bytes, end)) {
        return 0;
    }
    if (*end > vram_bytes || !v9x_r6_add(gpu_base, *end, &gpu_end)) {
        return 0;
    }
    return 1;
}

static int v9x_r6_pixel_size_ok(v9x_u32 bytes_per_pixel)
{
    return bytes_per_pixel == 1ul || bytes_per_pixel == 2ul ||
           bytes_per_pixel == 4ul;
}

/* The fill colour as one dword of repeated pixels. */
static v9x_u32 v9x_r6_replicate(v9x_u32 color, v9x_u32 bytes_per_pixel)
{
    if (bytes_per_pixel == 1ul) {
        return (color & 0xfful) * 0x01010101ul;
    }
    if (bytes_per_pixel == 2ul) {
        return (color & 0xfffful) * 0x00010001ul;
    }
    return color;
}

/* A row the engine copies right: on dwords, two dwords or more, inside one
 * packet. */
static int v9x_r6_row_ok(v9x_u32 row_bytes)
{
    return (row_bytes & 3ul) == 0ul &&
           row_bytes / 4ul >= V9X_R6_ROW_DWORDS_MIN &&
           row_bytes / 4ul <= V9X_R6_DMA_COUNT_MAX;
}

v9x_status v9x_r6_dma_plan_fill(const struct v9x_r6_dma_fill *fill,
                                struct v9x_r6_dma_plan *plan)
{
    v9x_u32 width_bytes;
    v9x_u32 left_bytes;
    v9x_u32 start;
    v9x_u32 end;

    if (fill == 0 || plan == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (!v9x_r6_pixel_size_ok(fill->bytes_per_pixel)) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (fill->width == 0ul || fill->height == 0ul ||
        !v9x_r6_mul(fill->width, fill->bytes_per_pixel, &width_bytes) ||
        !v9x_r6_mul(fill->left, fill->bytes_per_pixel, &left_bytes) ||
        !v9x_r6_extent(fill->gpu_base, fill->vram_bytes,
                       fill->target_offset, fill->pitch_bytes, left_bytes,
                       fill->top, width_bytes, fill->height, &start, &end)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* Every row starts and ends on a dword and is a row COPY can move. */
    if (((fill->gpu_base + start) & 3ul) != 0ul || !v9x_r6_row_ok(width_bytes) ||
        (fill->height > 1ul && (fill->pitch_bytes & 3ul) != 0ul)) {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* The CPU seeds the first row; every other row is copied from it, so
     * each packet reads only what the seed wrote before the submission. */
    plan->seed_offset = start;
    plan->seed_bytes = width_bytes;
    plan->value = v9x_r6_replicate(fill->color, fill->bytes_per_pixel);
    plan->rows = fill->height - 1ul;
    plan->row_dwords = width_bytes / 4ul;
    plan->source = fill->gpu_base + start;
    plan->source_step = 0ul;
    plan->destination = fill->gpu_base + start + fill->pitch_bytes;
    plan->destination_step = fill->pitch_bytes;
    if (plan->rows == 0ul) {
        plan->destination = plan->source;
    }
    return V9X_STATUS_OK;
}

v9x_status v9x_r6_dma_plan_copy(const struct v9x_r6_dma_copy *copy,
                                struct v9x_r6_dma_plan *plan)
{
    v9x_u32 width_bytes;
    v9x_u32 source_left;
    v9x_u32 destination_left;
    v9x_u32 source_start;
    v9x_u32 source_end;
    v9x_u32 destination_start;
    v9x_u32 destination_end;
    v9x_u32 last_row;
    int overlap;

    if (copy == 0 || plan == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (!v9x_r6_pixel_size_ok(copy->bytes_per_pixel)) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (copy->width == 0ul || copy->height == 0ul ||
        !v9x_r6_mul(copy->width, copy->bytes_per_pixel, &width_bytes) ||
        !v9x_r6_mul(copy->source_left, copy->bytes_per_pixel, &source_left) ||
        !v9x_r6_mul(copy->destination_left, copy->bytes_per_pixel,
                    &destination_left) ||
        !v9x_r6_extent(copy->gpu_base, copy->vram_bytes, copy->source_offset,
                       copy->source_pitch_bytes, source_left,
                       copy->source_top, width_bytes, copy->height,
                       &source_start, &source_end) ||
        !v9x_r6_extent(copy->gpu_base, copy->vram_bytes,
                       copy->destination_offset,
                       copy->destination_pitch_bytes, destination_left,
                       copy->destination_top, width_bytes, copy->height,
                       &destination_start, &destination_end)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* No byte copy on R6xx: both sides on dwords, every row too. */
    if (((copy->gpu_base + source_start) & 3ul) != 0ul ||
        ((copy->gpu_base + destination_start) & 3ul) != 0ul ||
        !v9x_r6_row_ok(width_bytes) ||
        (copy->height > 1ul &&
         ((copy->source_pitch_bytes & 3ul) != 0ul ||
          (copy->destination_pitch_bytes & 3ul) != 0ul))) {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* Overlapping spans: only between rows of one pitch can the row order
     * be chosen so that no packet reads a row an earlier one wrote. */
    overlap = source_start < destination_end && destination_start < source_end;
    if (overlap && copy->source_pitch_bytes != copy->destination_pitch_bytes) {
        return V9X_STATUS_UNSUPPORTED;
    }

    plan->seed_offset = 0ul;
    plan->seed_bytes = 0ul;
    plan->value = 0ul;
    plan->source = copy->gpu_base + source_start;
    plan->destination = copy->gpu_base + destination_start;

    /* Contiguous rows are one run while it fits one packet: a single
     * packet's memmove was measured, even overlapping. */
    if (copy->height == 1ul ||
        (copy->source_pitch_bytes == width_bytes &&
         copy->destination_pitch_bytes == width_bytes &&
         (source_end - source_start) / 4ul <= V9X_R6_DMA_COUNT_MAX)) {
        plan->rows = 1ul;
        plan->row_dwords = (source_end - source_start) / 4ul;
        plan->source_step = 0ul;
        plan->destination_step = 0ul;
        return V9X_STATUS_OK;
    }

    plan->rows = copy->height;
    plan->row_dwords = width_bytes / 4ul;
    plan->source_step = copy->source_pitch_bytes;
    plan->destination_step = copy->destination_pitch_bytes;
    /* A destination below its overlapping source runs bottom-up: then no
     * packet reads a row an earlier packet wrote, which inside one
     * submission the engine did not order. */
    if (overlap && destination_start > source_start) {
        last_row = (copy->height - 1ul) * copy->source_pitch_bytes;
        plan->source += last_row;
        plan->destination += last_row;
        plan->source_step = 0ul - copy->source_pitch_bytes;
        plan->destination_step = 0ul - copy->destination_pitch_bytes;
    }
    return V9X_STATUS_OK;
}

v9x_u32 v9x_r6_dma_plan_row(const struct v9x_r6_dma_plan *plan,
                            v9x_u32 index, v9x_u32 address_high,
                            v9x_u32 *out)
{
    v9x_u32 high;

    if (plan == 0 || out == 0 || index >= plan->rows) {
        return 0ul;
    }
    high = address_high & V9X_R6_ADDRESS_HIGH_MASK;
    out[0] = v9x_r6_packet(V9X_R6_DMA_CMD_COPY, plan->row_dwords);
    out[1] = plan->destination + index * plan->destination_step;
    out[2] = plan->source + index * plan->source_step;
    out[3] = (high << V9X_R6_COPY_DST_HIGH_SHIFT) | high;
    return V9X_R6_DMA_COPY_DWORDS;
}

v9x_u32 v9x_r6_dma_fence(v9x_u32 address_high, v9x_u32 gpu_address,
                         v9x_u32 value, v9x_u32 *out)
{
    out[0] = v9x_r6_packet(V9X_R6_DMA_CMD_FENCE, 0ul);
    out[1] = gpu_address & ~3ul;
    out[2] = address_high & V9X_R6_ADDRESS_HIGH_MASK;
    out[3] = value;
    return V9X_R6_DMA_FENCE_DWORDS;
}

v9x_u32 v9x_r6_dma_ring_base(v9x_u32 address_high, v9x_u32 address_low)
{
    return ((address_high & V9X_R6_ADDRESS_HIGH_MASK) <<
            V9X_R6_RING_BASE_HIGH_SHIFT) |
           (address_low >> V9X_R6_RING_BASE_SHIFT);
}

v9x_u32 v9x_r6_dma_pad(v9x_u32 position)
{
    v9x_u32 tail = position % V9X_R6_DMA_ALIGN_DWORDS;

    return tail == 0ul ? 0ul : V9X_R6_DMA_ALIGN_DWORDS - tail;
}

v9x_u32 v9x_r6_dma_ring_advance(v9x_u32 wptr, v9x_u32 count)
{
    return (wptr + count + v9x_r6_dma_pad(wptr + count)) &
           (V9X_R6_RING_DWORDS - 1ul);
}

v9x_status v9x_r6_fb_start(v9x_u32 fb_location, v9x_u32 *high,
                           v9x_u32 *low)
{
    v9x_u32 field = fb_location & V9X_R6_FB_START_MASK;
    v9x_u32 start_low;

    /* 16 MiB units: the field's low byte is bits 31:24, its high byte bits
     * 39:32. */
    start_low = (field & 0xfful) << V9X_R6_FB_UNIT_SHIFT;
    if (high == 0 || low == 0 || start_low > V9X_R6_FB_LOW_MAX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *high = field >> 8;
    *low = start_low;
    return V9X_STATUS_OK;
}
