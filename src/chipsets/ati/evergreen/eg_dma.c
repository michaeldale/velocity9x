/*
 * AMD Evergreen async DMA packets for DirectDraw fills and copies. See
 * include\velocity9x\eg_dma.h for the contract and the sources it follows.
 */
#include "velocity9x/eg_dma.h"

#define V9X_EG_U32_MAX 0xfffffffful

static v9x_u32 v9x_eg_packet(v9x_u32 command, v9x_u32 sub_command,
                             v9x_u32 count)
{
    return ((command & 0xful) << 28) | ((sub_command & 0xfful) << 20) |
           (count & V9X_EG_DMA_COUNT_MAX);
}

/* *product = a * b, non-zero unless it overflows 32 bits. */
static int v9x_eg_mul(v9x_u32 a, v9x_u32 b, v9x_u32 *product)
{
    if (a != 0ul && b > V9X_EG_U32_MAX / a) {
        return 0;
    }
    *product = a * b;
    return 1;
}

/* *sum = a + b, non-zero unless it overflows 32 bits. */
static int v9x_eg_add(v9x_u32 a, v9x_u32 b, v9x_u32 *sum)
{
    if (b > V9X_EG_U32_MAX - a) {
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
 * and, from gpu_base, below 4 GiB.
 */
static int v9x_eg_extent(v9x_u32 gpu_base, v9x_u32 vram_bytes,
                         v9x_u32 offset, v9x_u32 pitch, v9x_u32 left_bytes,
                         v9x_u32 top, v9x_u32 width_bytes, v9x_u32 height,
                         v9x_u32 *start, v9x_u32 *end)
{
    v9x_u32 right;
    v9x_u32 rows;
    v9x_u32 last;
    v9x_u32 gpu_end;

    if (!v9x_eg_add(left_bytes, width_bytes, &right) || right > pitch) {
        return 0;
    }
    if (!v9x_eg_mul(top, pitch, &rows) ||
        !v9x_eg_add(offset, rows, start) ||
        !v9x_eg_add(*start, left_bytes, start)) {
        return 0;
    }
    if (!v9x_eg_mul(height - 1ul, pitch, &last) ||
        !v9x_eg_add(*start, last, end) ||
        !v9x_eg_add(*end, width_bytes, end)) {
        return 0;
    }
    if (*end > vram_bytes || !v9x_eg_add(gpu_base, *end, &gpu_end)) {
        return 0;
    }
    return 1;
}

static int v9x_eg_pixel_size_ok(v9x_u32 bytes_per_pixel)
{
    return bytes_per_pixel == 1ul || bytes_per_pixel == 2ul ||
           bytes_per_pixel == 4ul;
}

/* The fill colour as one dword of repeated pixels. */
static v9x_u32 v9x_eg_replicate(v9x_u32 color, v9x_u32 bytes_per_pixel)
{
    if (bytes_per_pixel == 1ul) {
        return (color & 0xfful) * 0x01010101ul;
    }
    if (bytes_per_pixel == 2ul) {
        return (color & 0xfffful) * 0x00010001ul;
    }
    return color;
}

v9x_status v9x_eg_dma_plan_fill(const struct v9x_eg_dma_fill *fill,
                                struct v9x_eg_dma_plan *plan)
{
    v9x_u32 width_bytes;
    v9x_u32 left_bytes;
    v9x_u32 start;
    v9x_u32 end;
    v9x_u32 total;

    if (fill == 0 || plan == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (!v9x_eg_pixel_size_ok(fill->bytes_per_pixel)) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (fill->width == 0ul || fill->height == 0ul ||
        !v9x_eg_mul(fill->width, fill->bytes_per_pixel, &width_bytes) ||
        !v9x_eg_mul(fill->left, fill->bytes_per_pixel, &left_bytes) ||
        !v9x_eg_extent(fill->gpu_base, fill->vram_bytes,
                       fill->target_offset, fill->pitch_bytes, left_bytes,
                       fill->top, width_bytes, fill->height, &start, &end)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* The fill writes whole dwords: every row must start and end on one. */
    if (((fill->gpu_base + start) & 3ul) != 0ul || (width_bytes & 3ul) != 0ul ||
        (fill->height > 1ul && (fill->pitch_bytes & 3ul) != 0ul)) {
        return V9X_STATUS_UNSUPPORTED;
    }

    plan->kind = V9X_EG_DMA_PLAN_FILL;
    plan->destination = fill->gpu_base + start;
    plan->source = 0ul;
    plan->source_step = 0ul;
    plan->value = v9x_eg_replicate(fill->color, fill->bytes_per_pixel);
    plan->byte_copy = 0ul;

    /* Contiguous rows are one run. */
    total = end - start;
    if ((fill->height == 1ul || fill->pitch_bytes == width_bytes) &&
        total / 4ul <= V9X_EG_DMA_COUNT_MAX) {
        plan->rows = 1ul;
        plan->row_bytes = total;
        plan->destination_step = 0ul;
        return V9X_STATUS_OK;
    }
    if (width_bytes / 4ul > V9X_EG_DMA_COUNT_MAX) {
        return V9X_STATUS_UNSUPPORTED;
    }
    plan->rows = fill->height;
    plan->row_bytes = width_bytes;
    plan->destination_step = fill->pitch_bytes;
    return V9X_STATUS_OK;
}

v9x_status v9x_eg_dma_plan_copy(const struct v9x_eg_dma_copy *copy,
                                struct v9x_eg_dma_plan *plan)
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
    int contiguous;

    if (copy == 0 || plan == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (!v9x_eg_pixel_size_ok(copy->bytes_per_pixel)) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (copy->width == 0ul || copy->height == 0ul ||
        !v9x_eg_mul(copy->width, copy->bytes_per_pixel, &width_bytes) ||
        !v9x_eg_mul(copy->source_left, copy->bytes_per_pixel, &source_left) ||
        !v9x_eg_mul(copy->destination_left, copy->bytes_per_pixel,
                    &destination_left) ||
        !v9x_eg_extent(copy->gpu_base, copy->vram_bytes, copy->source_offset,
                       copy->source_pitch_bytes, source_left,
                       copy->source_top, width_bytes, copy->height,
                       &source_start, &source_end) ||
        !v9x_eg_extent(copy->gpu_base, copy->vram_bytes,
                       copy->destination_offset,
                       copy->destination_pitch_bytes, destination_left,
                       copy->destination_top, width_bytes, copy->height,
                       &destination_start, &destination_end)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    plan->kind = V9X_EG_DMA_PLAN_COPY;
    plan->value = 0ul;
    plan->byte_copy =
        (((copy->gpu_base + source_start) & 3ul) != 0ul ||
         ((copy->gpu_base + destination_start) & 3ul) != 0ul ||
         (width_bytes & 3ul) != 0ul ||
         (copy->height > 1ul &&
          ((copy->source_pitch_bytes & 3ul) != 0ul ||
           (copy->destination_pitch_bytes & 3ul) != 0ul))) ? 1ul : 0ul;

    /* The spans overlap: only the dword copy was measured overlapping, and
     * only between rows of one pitch can the row order be chosen. */
    overlap = source_start < destination_end && destination_start < source_end;
    if (overlap && (plan->byte_copy != 0ul ||
                    copy->source_pitch_bytes !=
                    copy->destination_pitch_bytes)) {
        return V9X_STATUS_UNSUPPORTED;
    }

    contiguous = copy->height == 1ul ||
                 (copy->source_pitch_bytes == width_bytes &&
                  copy->destination_pitch_bytes == width_bytes);
    if (contiguous) {
        plan->rows = 1ul;
        plan->row_bytes = source_end - source_start;
        plan->source = copy->gpu_base + source_start;
        plan->destination = copy->gpu_base + destination_start;
        plan->source_step = 0ul;
        plan->destination_step = 0ul;
    } else {
        plan->rows = copy->height;
        plan->row_bytes = width_bytes;
        plan->source_step = copy->source_pitch_bytes;
        plan->destination_step = copy->destination_pitch_bytes;
        plan->source = copy->gpu_base + source_start;
        plan->destination = copy->gpu_base + destination_start;
        /* A destination below its overlapping source is read before it is
         * written only if the rows run bottom-up. */
        if (overlap && destination_start > source_start) {
            last_row = (copy->height - 1ul) * copy->source_pitch_bytes;
            plan->source += last_row;
            plan->destination += last_row;
            plan->source_step = 0ul - copy->source_pitch_bytes;
            plan->destination_step = 0ul - copy->destination_pitch_bytes;
        }
    }

    if ((plan->byte_copy != 0ul ? plan->row_bytes :
         plan->row_bytes / 4ul) > V9X_EG_DMA_COUNT_MAX) {
        return V9X_STATUS_UNSUPPORTED;
    }
    return V9X_STATUS_OK;
}

v9x_u32 v9x_eg_dma_plan_row(const struct v9x_eg_dma_plan *plan,
                            v9x_u32 index, v9x_u32 *out)
{
    v9x_u32 destination;
    v9x_u32 source;

    if (plan == 0 || out == 0 || index >= plan->rows) {
        return 0ul;
    }
    destination = plan->destination + index * plan->destination_step;
    if (plan->kind == V9X_EG_DMA_PLAN_FILL) {
        out[0] = v9x_eg_packet(V9X_EG_DMA_CMD_CONSTANT_FILL, 0ul,
                               plan->row_bytes / 4ul);
        out[1] = destination;
        out[2] = plan->value;
        out[3] = 0ul;
        return V9X_EG_DMA_FILL_DWORDS;
    }
    source = plan->source + index * plan->source_step;
    if (plan->byte_copy != 0ul) {
        out[0] = v9x_eg_packet(V9X_EG_DMA_CMD_COPY, V9X_EG_DMA_COPY_BYTE,
                               plan->row_bytes);
    } else {
        out[0] = v9x_eg_packet(V9X_EG_DMA_CMD_COPY, V9X_EG_DMA_COPY_DWORD,
                               plan->row_bytes / 4ul);
    }
    out[1] = destination;
    out[2] = source;
    out[3] = 0ul;
    out[4] = 0ul;
    return V9X_EG_DMA_COPY_DWORDS;
}

v9x_u32 v9x_eg_dma_fence(v9x_u32 gpu_address, v9x_u32 value, v9x_u32 *out)
{
    out[0] = v9x_eg_packet(V9X_EG_DMA_CMD_FENCE, 0ul, 0ul);
    out[1] = gpu_address & ~3ul;
    out[2] = 0ul;
    out[3] = value;
    return V9X_EG_DMA_FENCE_DWORDS;
}

v9x_u32 v9x_eg_dma_pad(v9x_u32 position)
{
    v9x_u32 tail = position % V9X_EG_DMA_ALIGN_DWORDS;

    return tail == 0ul ? 0ul : V9X_EG_DMA_ALIGN_DWORDS - tail;
}

v9x_u32 v9x_eg_dma_ring_advance(v9x_u32 wptr, v9x_u32 count)
{
    return (wptr + count + v9x_eg_dma_pad(wptr + count)) &
           (V9X_EG_RING_DWORDS - 1ul);
}

v9x_u32 v9x_eg_fb_start(v9x_u32 fb_location, v9x_u32 *ok)
{
    v9x_u32 field = fb_location & V9X_EG_FB_START_MASK;

    /* 16 MiB units of a 40-bit address: 256 and above is past 4 GiB. */
    if (field >= 0x100ul) {
        *ok = 0ul;
        return 0ul;
    }
    *ok = 1ul;
    return field << V9X_EG_FB_UNIT_SHIFT;
}
