/*
 * 3dfx VSA-100 2D engine register values. See include\velocity9x\tdfx_2d.h
 * for the contract and docs\specifications\vsa100-registers.md section 6 for
 * the Napalm citations.
 */
#include "velocity9x/tdfx_2d.h"

/* Pixel format codes for dstFormat and srcFormat (section 7.2.9). 24 bpp
 * (code 4) is not offered: no desktop or surface here uses it. */
#define TDFX_FORMAT_8BPP            1ul
#define TDFX_FORMAT_16BPP           3ul
#define TDFX_FORMAT_32BPP           5ul

#define TDFX_XY(x, y)               (((v9x_u32)(y) << 16) | (v9x_u32)(x))

/* The format code for a pixel size, or 0 when the engine is not used for
 * it. */
static v9x_u32 v9x_tdfx_format(v9x_u32 bytes_per_pixel)
{
    if (bytes_per_pixel == 1ul) {
        return TDFX_FORMAT_8BPP;
    }
    if (bytes_per_pixel == 2ul) {
        return TDFX_FORMAT_16BPP;
    }
    if (bytes_per_pixel == 4ul) {
        return TDFX_FORMAT_32BPP;
    }
    return 0ul;
}

static void v9x_tdfx_emit(struct v9x_tdfx_blt *blt, v9x_u32 offset,
                          v9x_u32 value)
{
    blt->offsets[blt->count] = offset;
    blt->values[blt->count] = value;
    ++blt->count;
}

/*
 * One rectangle on one surface: it must lie inside the pitch, inside the
 * engine's coordinate fields, and inside memory. Overflow-safe: every
 * product is bounded by the field limits before it is formed.
 */
static v9x_status v9x_tdfx_check_rect(v9x_u32 vram_bytes, v9x_u32 base,
                                      v9x_u32 pitch_bytes,
                                      v9x_u32 bytes_per_pixel,
                                      v9x_u32 left, v9x_u32 top,
                                      v9x_u32 width, v9x_u32 height)
{
    v9x_u32 last_row_end;

    if (width == 0ul || height == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (left > V9X_TDFX_COORD_MAX || top > V9X_TDFX_COORD_MAX ||
        width > V9X_TDFX_COORD_MAX + 1ul - left ||
        height > V9X_TDFX_COORD_MAX + 1ul - top) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if ((left + width) * bytes_per_pixel > pitch_bytes) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (base > V9X_TDFX_BASE_MAX || base >= vram_bytes) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* Byte after the last pixel, relative to base. */
    last_row_end = (top + height - 1ul) * pitch_bytes +
                   (left + width) * bytes_per_pixel;
    if (last_row_end > vram_bytes - base) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return V9X_STATUS_OK;
}

/* The writes every operation starts with: a NOP for a small one (the
 * research notes' erratum), the open clip, and no colour key or vsync wait
 * left over from whoever used the engine last. */
static void v9x_tdfx_emit_prologue(struct v9x_tdfx_blt *blt, v9x_u32 width,
                                   v9x_u32 height)
{
    blt->count = 0u;
    if (v9x_tdfx_small_blt(width, height) != 0ul) {
        v9x_tdfx_emit(blt, V9X_TDFX_2D_COMMAND,
                      V9X_TDFX_CMD_NOP | V9X_TDFX_CMD_INITIATE);
    }
    v9x_tdfx_emit(blt, V9X_TDFX_2D_CLIP0_MIN, 0ul);
    v9x_tdfx_emit(blt, V9X_TDFX_2D_CLIP0_MAX, V9X_TDFX_CLIP_OPEN_MAX);
    v9x_tdfx_emit(blt, V9X_TDFX_2D_COMMAND_EXTRA, 0ul);
}

v9x_u32 v9x_tdfx_small_blt(v9x_u32 width, v9x_u32 height)
{
    return (width <= V9X_TDFX_SMALL_BLT_PIXELS ||
            height <= V9X_TDFX_SMALL_BLT_PIXELS) ? 1ul : 0ul;
}

v9x_status v9x_tdfx_build_fill(const struct v9x_tdfx_fill *fill,
                               struct v9x_tdfx_blt *blt)
{
    v9x_u32 format;
    v9x_status status;

    if (fill == 0 || blt == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    blt->count = 0u;

    format = v9x_tdfx_format(fill->bytes_per_pixel);
    if (format == 0ul || fill->pitch_bytes > V9X_TDFX_STRIDE_MAX ||
        fill->rop > 0xfful) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (fill->bytes_per_pixel < 4ul &&
        (fill->color >> (fill->bytes_per_pixel * 8ul)) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_tdfx_check_rect(fill->vram_bytes, fill->target_offset,
                                 fill->pitch_bytes, fill->bytes_per_pixel,
                                 fill->left, fill->top, fill->width,
                                 fill->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    v9x_tdfx_emit_prologue(blt, fill->width, fill->height);
    v9x_tdfx_emit(blt, V9X_TDFX_2D_DST_BASE, fill->target_offset);
    v9x_tdfx_emit(blt, V9X_TDFX_2D_DST_FORMAT,
                  (format << V9X_TDFX_FORMAT_SHIFT) | fill->pitch_bytes);
    v9x_tdfx_emit(blt, V9X_TDFX_2D_COLOR_FORE, fill->color);
    v9x_tdfx_emit(blt, V9X_TDFX_2D_DST_SIZE,
                  TDFX_XY(fill->width, fill->height));
    v9x_tdfx_emit(blt, V9X_TDFX_2D_DST_XY, TDFX_XY(fill->left, fill->top));
    v9x_tdfx_emit(blt, V9X_TDFX_2D_COMMAND,
                  V9X_TDFX_CMD_RECT_FILL | V9X_TDFX_CMD_INITIATE |
                  (fill->rop << V9X_TDFX_CMD_ROP_SHIFT));
    return V9X_STATUS_OK;
}

v9x_status v9x_tdfx_build_copy(const struct v9x_tdfx_copy *copy,
                               struct v9x_tdfx_blt *blt)
{
    v9x_u32 format;
    v9x_u32 command;
    v9x_u32 source_x;
    v9x_u32 source_y;
    v9x_u32 destination_x;
    v9x_u32 destination_y;
    v9x_status status;

    if (copy == 0 || blt == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    blt->count = 0u;

    format = v9x_tdfx_format(copy->bytes_per_pixel);
    if (format == 0ul ||
        copy->source_pitch_bytes > V9X_TDFX_STRIDE_MAX ||
        copy->destination_pitch_bytes > V9X_TDFX_STRIDE_MAX) {
        return V9X_STATUS_UNSUPPORTED;
    }
    status = v9x_tdfx_check_rect(copy->vram_bytes, copy->source_offset,
                                 copy->source_pitch_bytes,
                                 copy->bytes_per_pixel, copy->source_left,
                                 copy->source_top, copy->width,
                                 copy->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_tdfx_check_rect(copy->vram_bytes, copy->destination_offset,
                                 copy->destination_pitch_bytes,
                                 copy->bytes_per_pixel,
                                 copy->destination_left,
                                 copy->destination_top, copy->width,
                                 copy->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    /*
     * Direction only matters when the rectangles overlap on one surface: a
     * destination below the source runs bottom to top, one to the right on
     * the same rows runs right to left. Everything else stays on the plain
     * forward copy. With either bit set, srcXY and dstXY name the corner the
     * copy starts from (section 7.2.8).
     */
    command = V9X_TDFX_CMD_BLT | V9X_TDFX_CMD_INITIATE |
              (V9X_TDFX_ROP_SRCCOPY << V9X_TDFX_CMD_ROP_SHIFT);
    source_x = copy->source_left;
    source_y = copy->source_top;
    destination_x = copy->destination_left;
    destination_y = copy->destination_top;
    if (copy->source_offset == copy->destination_offset &&
        copy->source_pitch_bytes == copy->destination_pitch_bytes &&
        copy->destination_left < copy->source_left + copy->width &&
        copy->source_left < copy->destination_left + copy->width &&
        copy->destination_top < copy->source_top + copy->height &&
        copy->source_top < copy->destination_top + copy->height) {
        if (copy->destination_top > copy->source_top) {
            command |= V9X_TDFX_CMD_Y_BOTTOM_TO_TOP;
            source_y += copy->height - 1ul;
            destination_y += copy->height - 1ul;
        } else if (copy->destination_top == copy->source_top &&
                   copy->destination_left > copy->source_left) {
            command |= V9X_TDFX_CMD_X_RIGHT_TO_LEFT;
            source_x += copy->width - 1ul;
            destination_x += copy->width - 1ul;
        }
    }

    v9x_tdfx_emit_prologue(blt, copy->width, copy->height);
    v9x_tdfx_emit(blt, V9X_TDFX_2D_SRC_BASE, copy->source_offset);
    v9x_tdfx_emit(blt, V9X_TDFX_2D_SRC_FORMAT,
                  (format << V9X_TDFX_FORMAT_SHIFT) | copy->source_pitch_bytes);
    v9x_tdfx_emit(blt, V9X_TDFX_2D_DST_BASE, copy->destination_offset);
    v9x_tdfx_emit(blt, V9X_TDFX_2D_DST_FORMAT,
                  (format << V9X_TDFX_FORMAT_SHIFT) |
                  copy->destination_pitch_bytes);
    v9x_tdfx_emit(blt, V9X_TDFX_2D_DST_SIZE,
                  TDFX_XY(copy->width, copy->height));
    v9x_tdfx_emit(blt, V9X_TDFX_2D_DST_XY,
                  TDFX_XY(destination_x, destination_y));
    v9x_tdfx_emit(blt, V9X_TDFX_2D_SRC_XY, TDFX_XY(source_x, source_y));
    v9x_tdfx_emit(blt, V9X_TDFX_2D_COMMAND, command);
    return V9X_STATUS_OK;
}

v9x_u32 v9x_tdfx_status_busy(v9x_u32 status)
{
    return (status & V9X_TDFX_2D_STATUS_BUSY) != 0ul ? 1ul : 0ul;
}
