/*
 * SiS 6326 2D engine: register values for solid fill and screen copy.
 *
 * Pure policy, no I/O; see sis6326_engine.h. Register layout from the
 * datasheet (7.8.1), choices from what SiS's own driver was measured doing
 * (docs\decisions\2026-10-04-sis6326-first-survey.md), and the
 * reverse-direction start address from xf86-video-sis. Two of those - the
 * n - 1 size encoding and the last-byte start of a right-to-left copy - are
 * inferences until a guarded write probe measures them on the card.
 */
#include "velocity9x/sis6326_engine.h"

/*
 * Width and height are programmed as n - 1. Inferred from SiS's driver: its
 * last fill on both boards was the Start button face, 50x18 pixels at
 * (4, 458) on the screenshot, and the size register held 31h and 11h. The
 * datasheet says only "Rectangular Width/Height".
 */
#define V9X_SIS_SIZE_BIAS 1ul

#define V9X_SIS_COLOR_MASK 0x00fffffful
#define V9X_SIS_ROP_SHIFT 24

/* The pixel's colour in the 24-bit colour field. At 8 bpp the index is
 * replicated across the field, as SiS's driver left its foreground
 * (CC070707h); at 16 bpp the field holds the pixel. */
static v9x_u32 v9x_sis_color_field(v9x_u32 color, v9x_u32 bytes_per_pixel)
{
    if (bytes_per_pixel == 1ul) {
        color &= 0xfful;
        return color | (color << 8) | (color << 16);
    }
    return color & 0xfffful;
}

/*
 * A rectangle inside one surface, and its byte extent inside VRAM and the
 * engine's 22-bit address range. Returns OK, INVALID_ARGUMENT for a
 * rectangle the surface does not hold, or UNSUPPORTED for one the engine's
 * fields cannot express.
 */
static v9x_status v9x_sis_check_rect(v9x_u32 vram_bytes, v9x_u32 offset,
                                     v9x_u32 pitch_bytes,
                                     v9x_u32 bytes_per_pixel,
                                     v9x_u32 left, v9x_u32 top,
                                     v9x_u32 width, v9x_u32 height)
{
    v9x_u32 row_bytes;
    v9x_u32 last_row;
    v9x_u32 end;

    if (width == 0ul || height == 0ul || pitch_bytes == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (pitch_bytes > V9X_SIS_2D_PITCH_MAX ||
        height > V9X_SIS_2D_EXTENT_MAX) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (left > pitch_bytes / bytes_per_pixel ||
        width > pitch_bytes / bytes_per_pixel - left) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    row_bytes = width * bytes_per_pixel;
    if (row_bytes > V9X_SIS_2D_EXTENT_MAX) {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* The last byte touched, computed so nothing overflows: top and height
     * are each below 4097 and pitch below 4096. */
    last_row = top + height - 1ul;
    if (top > V9X_SIS_2D_ADDRESS_LIMIT || last_row > 0x00100000ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    end = last_row * pitch_bytes + left * bytes_per_pixel + row_bytes;

    /* Beyond the surface's memory is the caller's mistake; beyond the 22-bit
     * address field is a rectangle this engine cannot reach, whatever VRAM
     * a later revision might carry. */
    if (offset >= vram_bytes || end > vram_bytes - offset) {
        if (offset >= V9X_SIS_2D_ADDRESS_LIMIT) {
            return V9X_STATUS_UNSUPPORTED;
        }
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (offset >= V9X_SIS_2D_ADDRESS_LIMIT ||
        end > V9X_SIS_2D_ADDRESS_LIMIT - offset) {
        return V9X_STATUS_UNSUPPORTED;
    }
    return V9X_STATUS_OK;
}

static v9x_u32 v9x_sis_size(v9x_u32 width_bytes, v9x_u32 height)
{
    return ((height - V9X_SIS_SIZE_BIAS) << 16) |
           (width_bytes - V9X_SIS_SIZE_BIAS);
}

static void v9x_sis_emit(struct v9x_sis_blt *blt, v9x_u32 offset,
                         v9x_u32 value)
{
    blt->offsets[blt->count] = offset;
    blt->values[blt->count] = value;
    ++blt->count;
}

/*
 * Solid fill, as SiS's driver does it: the colour in the background
 * register as the source, ROP SRCCOPY in the foreground register's ROP
 * byte, pattern from the foreground, both directions increasing - command
 * 0034h, the word measured on both boards. Not at 24 bpp, where the
 * datasheet lists no register-sourced BitBlt (7.4.1).
 */
v9x_status v9x_sis_build_fill(const struct v9x_sis_fill *fill,
                              struct v9x_sis_blt *blt)
{
    v9x_status status;
    v9x_u32 color;

    if (fill == 0 || blt == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    blt->count = 0ul;
    blt->command = 0u;

    if (fill->bytes_per_pixel != 1ul && fill->bytes_per_pixel != 2ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    status = v9x_sis_check_rect(fill->vram_bytes, fill->target_offset,
                                fill->pitch_bytes, fill->bytes_per_pixel,
                                fill->left, fill->top, fill->width,
                                fill->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    color = v9x_sis_color_field(fill->color, fill->bytes_per_pixel);
    v9x_sis_emit(blt, V9X_SIS_2D_DST_ADDR,
                 fill->target_offset + fill->top * fill->pitch_bytes +
                 fill->left * fill->bytes_per_pixel);
    v9x_sis_emit(blt, V9X_SIS_2D_PITCH,
                 (fill->pitch_bytes << 16) | fill->pitch_bytes);
    v9x_sis_emit(blt, V9X_SIS_2D_SIZE,
                 v9x_sis_size(fill->width * fill->bytes_per_pixel,
                              fill->height));
    v9x_sis_emit(blt, V9X_SIS_2D_FG,
                 (V9X_SIS_ROP_SRCCOPY << V9X_SIS_ROP_SHIFT) | color);
    v9x_sis_emit(blt, V9X_SIS_2D_BG, color & V9X_SIS_COLOR_MASK);
    blt->command = (v9x_u16)(V9X_SIS_CMD1_BITBLT |
                             V9X_SIS_CMD0_Y_INCREASE |
                             V9X_SIS_CMD0_X_INCREASE |
                             V9X_SIS_CMD0_PATTERN_FG |
                             V9X_SIS_CMD0_SOURCE_BG);
    return V9X_STATUS_OK;
}

/*
 * Screen copy, source from video memory, ROP SRCCOPY.
 *
 * Overlap is decided only for one surface copied onto itself (same offset
 * and pitch); separate DirectDraw surfaces do not overlap. Within one
 * surface: a lower destination runs bottom-up, and a destination on the
 * same rows to the right runs right-to-left. A reversed axis starts on the
 * last row, and a reversed row starts on the last byte of its last pixel,
 * as xf86-video-sis's screen-to-screen copy does.
 */
v9x_status v9x_sis_build_copy(const struct v9x_sis_copy *copy,
                              struct v9x_sis_blt *blt)
{
    v9x_status status;
    v9x_u32 bpp;
    v9x_u32 row_bytes;
    v9x_u32 source_row;
    v9x_u32 destination_row;
    v9x_u32 source_byte;
    v9x_u32 destination_byte;
    v9x_u16 command;
    int same_surface;

    if (copy == 0 || blt == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    blt->count = 0ul;
    blt->command = 0u;

    bpp = copy->bytes_per_pixel;
    if (bpp != 1ul && bpp != 2ul && bpp != 3ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    status = v9x_sis_check_rect(copy->vram_bytes, copy->source_offset,
                                copy->source_pitch_bytes, bpp,
                                copy->source_left, copy->source_top,
                                copy->width, copy->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_sis_check_rect(copy->vram_bytes, copy->destination_offset,
                                copy->destination_pitch_bytes, bpp,
                                copy->destination_left,
                                copy->destination_top,
                                copy->width, copy->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    command = (v9x_u16)(V9X_SIS_CMD1_BITBLT | V9X_SIS_CMD0_SOURCE_VRAM |
                        V9X_SIS_CMD0_X_INCREASE | V9X_SIS_CMD0_Y_INCREASE);
    source_row = copy->source_top;
    destination_row = copy->destination_top;
    row_bytes = copy->width * bpp;
    source_byte = copy->source_left * bpp;
    destination_byte = copy->destination_left * bpp;

    same_surface = copy->source_offset == copy->destination_offset &&
                   copy->source_pitch_bytes ==
                       copy->destination_pitch_bytes;
    if (same_surface && copy->destination_top > copy->source_top) {
        command = (v9x_u16)(command & ~V9X_SIS_CMD0_Y_INCREASE);
        source_row += copy->height - 1ul;
        destination_row += copy->height - 1ul;
    } else if (same_surface &&
               copy->destination_top == copy->source_top &&
               copy->destination_left > copy->source_left) {
        command = (v9x_u16)(command & ~V9X_SIS_CMD0_X_INCREASE);
        source_byte += row_bytes - 1ul;
        destination_byte += row_bytes - 1ul;
    }

    v9x_sis_emit(blt, V9X_SIS_2D_SRC_ADDR,
                 copy->source_offset +
                 source_row * copy->source_pitch_bytes + source_byte);
    v9x_sis_emit(blt, V9X_SIS_2D_DST_ADDR,
                 copy->destination_offset +
                 destination_row * copy->destination_pitch_bytes +
                 destination_byte);
    v9x_sis_emit(blt, V9X_SIS_2D_PITCH,
                 (copy->destination_pitch_bytes << 16) |
                 copy->source_pitch_bytes);
    v9x_sis_emit(blt, V9X_SIS_2D_SIZE, v9x_sis_size(row_bytes, copy->height));
    v9x_sis_emit(blt, V9X_SIS_2D_FG,
                 V9X_SIS_ROP_SRCCOPY << V9X_SIS_ROP_SHIFT);
    blt->command = command;
    return V9X_STATUS_OK;
}

v9x_u32 v9x_sis_status_busy(v9x_u32 command_status)
{
    return command_status & V9X_SIS_STATUS_BUSY;
}
