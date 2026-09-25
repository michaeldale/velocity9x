/*
 * Gen3 2D blits for DirectDraw: XY_COLOR_BLT and XY_SRC_COPY_BLT built, and
 * checked, as pure C. No MMIO; the HAL's eng_i9xx.c submits what this builds.
 * docs\plans\intel-gen3-directdraw-blits.md.
 *
 * Two opinions, as for the 3D streams: the builders refuse an operand they
 * cannot express, and the decoder re-derives every bound from the dwords it
 * is given. A stream reaches the ring only after passing both.
 *
 * Deliberately separate from v9x_i9xx_build_color_blt in i9xx_ring.c. That is
 * the Phase 4 diagnostic builder, 32-bpp only with scratch bounds, and its
 * output is CRC-pinned into the arm contract; changing it would change the
 * contract.
 */
#include "velocity9x/intel_gma.h"

static v9x_u32 v9x_i9xx_blt_depth(v9x_u32 bytes_per_pixel)
{
    if (bytes_per_pixel == 1ul) {
        return V9X_I9XX_BLT_DEPTH_8;
    }
    return V9X_I9XX_BLT_DEPTH_565;
}

/*
 * One rectangle on one surface, as the byte range it touches: from its first
 * pixel to one past its last. V9X_FALSE when the packet cannot carry the
 * operands or the range leaves [0, vram_bytes); `inside` then says which.
 *
 * Base and pitch are held to dword alignment. Nothing measured says the
 * blitter needs it, every surface this driver allocates has it, and the
 * CPU path serves anything that does not.
 */
static v9x_u16 v9x_i9xx_blt_extent(v9x_u32 bytes_per_pixel,
                                   v9x_u32 vram_bytes, v9x_u32 base,
                                   v9x_u32 pitch, v9x_u32 left, v9x_u32 top,
                                   v9x_u32 right, v9x_u32 bottom,
                                   v9x_u32 *first_out, v9x_u32 *end_out,
                                   v9x_u16 *inside)
{
    v9x_u32 first_row;
    v9x_u32 last_row;
    v9x_u32 right_bytes;

    *inside = V9X_TRUE;
    *first_out = 0ul;
    *end_out = 0ul;
    if ((bytes_per_pixel != 1ul && bytes_per_pixel != 2ul) ||
        pitch == 0ul || pitch >= V9X_I9XX_BLT_FIELD_LIMIT ||
        (pitch & 3ul) != 0ul || (base & 3ul) != 0ul ||
        left >= right || top >= bottom ||
        right >= V9X_I9XX_BLT_FIELD_LIMIT ||
        bottom >= V9X_I9XX_BLT_FIELD_LIMIT) {
        return V9X_FALSE;
    }
    right_bytes = right * bytes_per_pixel;
    if (right_bytes > pitch) {
        return V9X_FALSE;
    }

    /* Both products are below 2^30: pitch and rows are each under 2^15. */
    first_row = top * pitch;
    last_row = (bottom - 1ul) * pitch;
    if (base >= vram_bytes ||
        last_row + right_bytes > vram_bytes - base) {
        *inside = V9X_FALSE;
        return V9X_FALSE;
    }
    *first_out = base + first_row + left * bytes_per_pixel;
    *end_out = base + last_row + right_bytes;
    return V9X_TRUE;
}

/*
 * Would this copy read memory it has already written? XY_SRC_COPY_BLT has no
 * scan-direction control and nothing in the audit or the tree says what the
 * blitter does when the two rectangles intersect (plan decision 7), so any
 * sharing is refused.
 *
 * Two rectangles on one surface - the same base and pitch - overlap only if
 * they intersect; disjoint ones are safe in any order because no pixel is
 * both read and written. Anything else is compared as byte ranges, which is
 * conservative: two surfaces whose ranges interleave are aliases, and
 * DirectDraw does not make those.
 */
static v9x_u16 v9x_i9xx_blt_overlaps(v9x_u32 source_base,
                                     v9x_u32 source_pitch,
                                     v9x_u32 source_left, v9x_u32 source_top,
                                     v9x_u32 source_first, v9x_u32 source_end,
                                     v9x_u32 destination_base,
                                     v9x_u32 destination_pitch,
                                     v9x_u32 left, v9x_u32 top,
                                     v9x_u32 right, v9x_u32 bottom,
                                     v9x_u32 destination_first,
                                     v9x_u32 destination_end)
{
    v9x_u32 source_right = source_left + (right - left);
    v9x_u32 source_bottom = source_top + (bottom - top);

    if (source_end <= destination_first || destination_end <= source_first) {
        return V9X_FALSE;
    }
    if (source_base == destination_base &&
        source_pitch == destination_pitch) {
        if (source_right <= left || right <= source_left ||
            source_bottom <= top || bottom <= source_top) {
            return V9X_FALSE;
        }
    }
    return V9X_TRUE;
}

v9x_status v9x_i9xx_build_fill_blt(const struct v9x_i9xx_blt *blt,
                                   v9x_u32 *stream, v9x_u32 capacity,
                                   v9x_u32 *written)
{
    v9x_u32 first;
    v9x_u32 end;
    v9x_u32 color;
    v9x_u16 inside;

    if (written != 0) {
        *written = 0ul;
    }
    if (blt == 0 || stream == 0 || written == 0 ||
        capacity < V9X_I9XX_BLT_COLOR_DWORDS + 1ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (v9x_i9xx_blt_extent(blt->bytes_per_pixel, blt->vram_bytes,
                            blt->destination_base, blt->destination_pitch,
                            blt->left, blt->top, blt->right, blt->bottom,
                            &first, &end, &inside) == V9X_FALSE) {
        return inside == V9X_FALSE ? V9X_STATUS_INSUFFICIENT_MEMORY
                                   : V9X_STATUS_INVALID_ARGUMENT;
    }

    /* Only the pixel's own bits: DirectDraw hands the fill colour over as a
     * DWORD whatever the depth, and bits above the pixel are not colour. */
    color = blt->bytes_per_pixel == 1ul ? (blt->color & 0x000000fful)
                                        : (blt->color & 0x0000fffful);
    stream[0] = V9X_I9XX_XY_COLOR_BLT_2D;
    stream[1] = v9x_i9xx_blt_depth(blt->bytes_per_pixel) |
                V9X_I9XX_BLT_ROP_PATCOPY | blt->destination_pitch;
    stream[2] = (blt->top << 16) | blt->left;
    stream[3] = (blt->bottom << 16) | blt->right;
    stream[4] = blt->destination_base;
    stream[5] = color;
    stream[6] = V9X_I9XX_MI_FLUSH;
    *written = V9X_I9XX_BLT_COLOR_DWORDS + 1ul;
    return V9X_STATUS_OK;
}

v9x_status v9x_i9xx_build_copy_blt(const struct v9x_i9xx_blt *blt,
                                   v9x_u32 *stream, v9x_u32 capacity,
                                   v9x_u32 *written)
{
    v9x_u32 destination_first;
    v9x_u32 destination_end;
    v9x_u32 source_first;
    v9x_u32 source_end;
    v9x_u16 inside;

    if (written != 0) {
        *written = 0ul;
    }
    if (blt == 0 || stream == 0 || written == 0 ||
        capacity < V9X_I9XX_BLT_COPY_DWORDS + 1ul ||
        blt->source_left >= V9X_I9XX_BLT_FIELD_LIMIT ||
        blt->source_top >= V9X_I9XX_BLT_FIELD_LIMIT ||
        blt->right <= blt->left || blt->bottom <= blt->top) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (v9x_i9xx_blt_extent(blt->bytes_per_pixel, blt->vram_bytes,
                            blt->destination_base, blt->destination_pitch,
                            blt->left, blt->top, blt->right, blt->bottom,
                            &destination_first, &destination_end,
                            &inside) == V9X_FALSE) {
        return inside == V9X_FALSE ? V9X_STATUS_INSUFFICIENT_MEMORY
                                   : V9X_STATUS_INVALID_ARGUMENT;
    }
    if (v9x_i9xx_blt_extent(blt->bytes_per_pixel, blt->vram_bytes,
                            blt->source_base, blt->source_pitch,
                            blt->source_left, blt->source_top,
                            blt->source_left + (blt->right - blt->left),
                            blt->source_top + (blt->bottom - blt->top),
                            &source_first, &source_end,
                            &inside) == V9X_FALSE) {
        return inside == V9X_FALSE ? V9X_STATUS_INSUFFICIENT_MEMORY
                                   : V9X_STATUS_INVALID_ARGUMENT;
    }
    if (v9x_i9xx_blt_overlaps(blt->source_base, blt->source_pitch,
                              blt->source_left, blt->source_top,
                              source_first, source_end,
                              blt->destination_base, blt->destination_pitch,
                              blt->left, blt->top, blt->right, blt->bottom,
                              destination_first, destination_end) !=
            V9X_FALSE) {
        return V9X_STATUS_UNSUPPORTED;
    }

    stream[0] = V9X_I9XX_XY_SRC_COPY_BLT;
    stream[1] = v9x_i9xx_blt_depth(blt->bytes_per_pixel) |
                V9X_I9XX_BLT_ROP_SRCCOPY | blt->destination_pitch;
    stream[2] = (blt->top << 16) | blt->left;
    stream[3] = (blt->bottom << 16) | blt->right;
    stream[4] = blt->destination_base;
    stream[5] = (blt->source_top << 16) | blt->source_left;
    stream[6] = blt->source_pitch;
    stream[7] = blt->source_base;
    stream[8] = V9X_I9XX_MI_FLUSH;
    *written = V9X_I9XX_BLT_COPY_DWORDS + 1ul;
    return V9X_STATUS_OK;
}

/*
 * The destination half both packets share, read back from the dwords: the
 * BR13 fields, then the rectangle, through the same extent rule.
 */
static v9x_u16 v9x_i9xx_decode_blt_destination(
    const v9x_u32 *packet, v9x_u32 rop, v9x_u32 bytes_per_pixel,
    v9x_u32 vram_bytes, v9x_u32 *first_out, v9x_u32 *end_out)
{
    v9x_u32 br13 = packet[1];
    v9x_u16 inside;

    if ((br13 & 0xffff0000ul) != (v9x_i9xx_blt_depth(bytes_per_pixel) | rop)) {
        return V9X_FALSE;
    }
    return v9x_i9xx_blt_extent(bytes_per_pixel, vram_bytes, packet[4],
                               br13 & 0x0000fffful,
                               packet[2] & 0x0000fffful, packet[2] >> 16,
                               packet[3] & 0x0000fffful, packet[3] >> 16,
                               first_out, end_out, &inside);
}

#define V9X_I9XX_BLT_REJECT(at) do { \
    if (rejected_index != 0) { *rejected_index = (at); } \
    return V9X_FALSE; \
} while (0)

v9x_u16 v9x_i9xx_decode_blt_stream(const v9x_u32 *stream,
                                   v9x_u32 dword_count,
                                   v9x_u32 bytes_per_pixel,
                                   v9x_u32 vram_bytes,
                                   v9x_u32 breadcrumb_offset,
                                   v9x_u32 *rejected_index)
{
    v9x_u32 index = 0ul;
    v9x_u16 saw_breadcrumb = V9X_FALSE;

    if (rejected_index != 0) {
        *rejected_index = 0ul;
    }
    if (stream == 0 || dword_count == 0ul || (dword_count & 1ul) != 0ul ||
        (bytes_per_pixel != 1ul && bytes_per_pixel != 2ul)) {
        return V9X_FALSE;
    }
    while (index < dword_count) {
        v9x_u32 command = stream[index];

        if (saw_breadcrumb != V9X_FALSE && command != V9X_I9XX_MI_NOOP) {
            /* Nothing is drawn after the mark that says drawing is done. */
            V9X_I9XX_BLT_REJECT(index);
        }
        if (command == V9X_I9XX_MI_NOOP || command == V9X_I9XX_MI_FLUSH) {
            ++index;

        } else if (command == V9X_I9XX_XY_COLOR_BLT_2D) {
            v9x_u32 first;
            v9x_u32 end;

            if (dword_count - index < V9X_I9XX_BLT_COLOR_DWORDS + 1ul) {
                V9X_I9XX_BLT_REJECT(index);
            }
            if (v9x_i9xx_decode_blt_destination(
                    stream + index, V9X_I9XX_BLT_ROP_PATCOPY,
                    bytes_per_pixel, vram_bytes, &first, &end) ==
                    V9X_FALSE) {
                V9X_I9XX_BLT_REJECT(index + 1ul);
            }
            /* The colour carries no bits above the pixel. */
            if ((bytes_per_pixel == 1ul &&
                 (stream[index + 5ul] & 0xffffff00ul) != 0ul) ||
                (bytes_per_pixel == 2ul &&
                 (stream[index + 5ul] & 0xffff0000ul) != 0ul)) {
                V9X_I9XX_BLT_REJECT(index + 5ul);
            }
            if (stream[index + 6ul] != V9X_I9XX_MI_FLUSH) {
                V9X_I9XX_BLT_REJECT(index + 6ul);
            }
            index += V9X_I9XX_BLT_COLOR_DWORDS + 1ul;

        } else if (command == V9X_I9XX_XY_SRC_COPY_BLT) {
            const v9x_u32 *packet = stream + index;
            v9x_u32 first;
            v9x_u32 end;
            v9x_u32 source_first;
            v9x_u32 source_end;
            v9x_u32 left;
            v9x_u32 top;
            v9x_u32 right;
            v9x_u32 bottom;
            v9x_u32 source_left;
            v9x_u32 source_top;
            v9x_u16 inside;

            if (dword_count - index < V9X_I9XX_BLT_COPY_DWORDS + 1ul) {
                V9X_I9XX_BLT_REJECT(index);
            }
            if (v9x_i9xx_decode_blt_destination(
                    packet, V9X_I9XX_BLT_ROP_SRCCOPY, bytes_per_pixel,
                    vram_bytes, &first, &end) == V9X_FALSE) {
                V9X_I9XX_BLT_REJECT(index + 1ul);
            }
            left = packet[2] & 0x0000fffful;
            top = packet[2] >> 16;
            right = packet[3] & 0x0000fffful;
            bottom = packet[3] >> 16;
            source_left = packet[5] & 0x0000fffful;
            source_top = packet[5] >> 16;
            if (source_left >= V9X_I9XX_BLT_FIELD_LIMIT ||
                source_top >= V9X_I9XX_BLT_FIELD_LIMIT ||
                v9x_i9xx_blt_extent(bytes_per_pixel, vram_bytes, packet[7],
                                    packet[6], source_left, source_top,
                                    source_left + (right - left),
                                    source_top + (bottom - top),
                                    &source_first, &source_end,
                                    &inside) == V9X_FALSE) {
                V9X_I9XX_BLT_REJECT(index + 5ul);
            }
            if (v9x_i9xx_blt_overlaps(packet[7], packet[6], source_left,
                                      source_top, source_first, source_end,
                                      packet[4], packet[1] & 0x0000fffful,
                                      left, top, right, bottom,
                                      first, end) != V9X_FALSE) {
                V9X_I9XX_BLT_REJECT(index + 7ul);
            }
            if (packet[8] != V9X_I9XX_MI_FLUSH) {
                V9X_I9XX_BLT_REJECT(index + 8ul);
            }
            index += V9X_I9XX_BLT_COPY_DWORDS + 1ul;

        } else if (command == V9X_I9XX_XY_COLOR_BLT &&
                   breadcrumb_offset != 0ul) {
            /*
             * The breadcrumb, in exactly the shape
             * v9x_i9xx_build_breadcrumb_stream emits: one 32-bit pixel at the
             * one destination named, any colour, its own MI_FLUSH and MI_NOOP,
             * directly behind an MI_FLUSH so that what it marks complete is
             * everything ahead of it.
             */
            if (dword_count - index < V9X_I9XX_BREADCRUMB_STREAM_DWORDS ||
                index == 0ul || stream[index - 1ul] != V9X_I9XX_MI_FLUSH ||
                stream[index + 1ul] != (V9X_I9XX_BLT_DEPTH_32 |
                                        V9X_I9XX_BLT_ROP_PATCOPY |
                                        (v9x_u32)V9X_I9XX_BREADCRUMB_PITCH) ||
                stream[index + 2ul] != 0ul ||
                stream[index + 3ul] != ((1ul << 16) | 1ul) ||
                stream[index + 4ul] != breadcrumb_offset ||
                stream[index + 6ul] != V9X_I9XX_MI_FLUSH ||
                stream[index + 7ul] != V9X_I9XX_MI_NOOP) {
                V9X_I9XX_BLT_REJECT(index);
            }
            saw_breadcrumb = V9X_TRUE;
            index += V9X_I9XX_BREADCRUMB_STREAM_DWORDS;

        } else {
            V9X_I9XX_BLT_REJECT(index);
        }
    }
    /* A breadcrumb named is a breadcrumb required: the submit will wait for
     * it, and a stream without one would make that wait a timeout. */
    if (breadcrumb_offset != 0ul && saw_breadcrumb == V9X_FALSE) {
        V9X_I9XX_BLT_REJECT(dword_count);
    }
    return V9X_TRUE;
}
