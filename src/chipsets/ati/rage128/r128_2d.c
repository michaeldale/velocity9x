/*
 * ATI Rage 128 2D engine register values. See include\velocity9x\r128_2d.h
 * for the contract and the sources it follows.
 */
#include "velocity9x/r128_2d.h"

#define R128_YX(x, y)   (((v9x_u32)(y) << 16) | (v9x_u32)(x))

/* The GMC destination datatype for a pixel size, or 0 when the engine is
 * not used for it. */
static v9x_u32 v9x_r128_datatype(v9x_u32 bytes_per_pixel)
{
    if (bytes_per_pixel == 1ul) {
        return V9X_R128_DATATYPE_8BPP;
    }
    if (bytes_per_pixel == 2ul) {
        return V9X_R128_DATATYPE_16BPP;
    }
    if (bytes_per_pixel == 4ul) {
        return V9X_R128_DATATYPE_32BPP;
    }
    return 0ul;
}

/* Pitch in the engine's 8-pixel units, or 0 when the pitch is not a whole
 * number of them or does not fit the 11-bit field. */
static v9x_u32 v9x_r128_pitch_units(v9x_u32 pitch_bytes,
                                    v9x_u32 bytes_per_pixel)
{
    v9x_u32 unit_bytes = V9X_R128_PITCH_PIXEL_UNIT * bytes_per_pixel;
    v9x_u32 units;

    if (pitch_bytes == 0ul || pitch_bytes % unit_bytes != 0ul) {
        return 0ul;
    }
    units = pitch_bytes / unit_bytes;
    if (units > V9X_R128_PITCH_UNITS_MAX) {
        return 0ul;
    }
    return units;
}

static void v9x_r128_emit(struct v9x_r128_blt *blt, v9x_u32 offset,
                          v9x_u32 value)
{
    blt->offsets[blt->count] = offset;
    blt->values[blt->count] = value;
    ++blt->count;
}

/*
 * One rectangle on one surface: inside the pitch and inside memory.
 * Overflow-safe: the coordinates are bounded by the 13-bit fields before
 * any product is formed.
 */
static v9x_status v9x_r128_check_rect(v9x_u32 vram_bytes, v9x_u32 base,
                                      v9x_u32 pitch_bytes,
                                      v9x_u32 bytes_per_pixel,
                                      v9x_u32 left, v9x_u32 top,
                                      v9x_u32 width, v9x_u32 height)
{
    v9x_u32 last_row_end;

    if (width == 0ul || height == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (left > V9X_R128_COORD_MAX || top > V9X_R128_COORD_MAX ||
        width > V9X_R128_COORD_MAX + 1ul - left ||
        height > V9X_R128_COORD_MAX + 1ul - top) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if ((left + width) * bytes_per_pixel > pitch_bytes) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (base >= vram_bytes) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    last_row_end = (top + height - 1ul) * pitch_bytes +
                   (left + width) * bytes_per_pixel;
    if (last_row_end > vram_bytes - base) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return V9X_STATUS_OK;
}

/*
 * A surface's *_PITCH_OFFSET word, and the pixels its start lies past the
 * 32-byte boundary the offset field counts in. The engine addresses from
 * the aligned offset; the remainder is moved onto x, which needs it to be
 * a whole number of pixels and x to stay inside its field.
 */
static v9x_status v9x_r128_surface(v9x_u32 base, v9x_u32 pitch_bytes,
                                   v9x_u32 bytes_per_pixel, v9x_u32 right,
                                   v9x_u32 *pitch_offset, v9x_u32 *x_bias)
{
    v9x_u32 units;
    v9x_u32 remainder;
    v9x_u32 aligned;

    units = v9x_r128_pitch_units(pitch_bytes, bytes_per_pixel);
    if (units == 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    remainder = base % V9X_R128_OFFSET_ALIGN;
    if (remainder % bytes_per_pixel != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    *x_bias = remainder / bytes_per_pixel;
    if (right + *x_bias > V9X_R128_COORD_MAX + 1ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    aligned = base - remainder;
    if ((aligned / V9X_R128_OFFSET_ALIGN) > V9X_R128_OFFSET_FIELD_MAX) {
        return V9X_STATUS_UNSUPPORTED;
    }
    *pitch_offset = (units << V9X_R128_PITCH_SHIFT) |
                    (aligned / V9X_R128_OFFSET_ALIGN);
    return V9X_STATUS_OK;
}

v9x_status v9x_r128_build_init(v9x_u32 bytes_per_pixel,
                               v9x_u32 primary_pitch_bytes,
                               v9x_u32 dp_datatype_read,
                               struct v9x_r128_blt *blt)
{
    v9x_u32 units;

    if (blt == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    blt->count = 0u;
    if (v9x_r128_datatype(bytes_per_pixel) == 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    units = v9x_r128_pitch_units(primary_pitch_bytes, bytes_per_pixel);
    if (units == 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* r128_accel.c R128EngineInit, less the reset and the GMC: every
     * operation writes its own master control. */
    v9x_r128_emit(blt, V9X_R128_SCALE_3D_CNTL, 0ul);
    v9x_r128_emit(blt, V9X_R128_DEFAULT_OFFSET, 0ul);
    v9x_r128_emit(blt, V9X_R128_DEFAULT_PITCH, units);
    v9x_r128_emit(blt, V9X_R128_AUX_SC_CNTL, 0ul);
    v9x_r128_emit(blt, V9X_R128_DEFAULT_SC_BOTTOM_RIGHT, V9X_R128_SC_MAX);
    v9x_r128_emit(blt, V9X_R128_SC_TOP_LEFT, 0ul);
    v9x_r128_emit(blt, V9X_R128_SC_BOTTOM_RIGHT, V9X_R128_SC_MAX);
    v9x_r128_emit(blt, V9X_R128_DP_WRITE_MASK, 0xfffffffful);
    v9x_r128_emit(blt, V9X_R128_DP_DATATYPE,
                  dp_datatype_read & ~V9X_R128_HOST_BIG_ENDIAN_EN);
    return V9X_STATUS_OK;
}

v9x_status v9x_r128_build_fill(const struct v9x_r128_fill *fill,
                               struct v9x_r128_blt *blt)
{
    v9x_u32 datatype;
    v9x_u32 pitch_offset;
    v9x_u32 x_bias;
    v9x_status status;

    if (fill == 0 || blt == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    blt->count = 0u;

    datatype = v9x_r128_datatype(fill->bytes_per_pixel);
    if (datatype == 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (fill->bytes_per_pixel < 4ul &&
        (fill->color >> (fill->bytes_per_pixel * 8ul)) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_r128_check_rect(fill->vram_bytes, fill->target_offset,
                                 fill->pitch_bytes, fill->bytes_per_pixel,
                                 fill->left, fill->top, fill->width,
                                 fill->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_r128_surface(fill->target_offset, fill->pitch_bytes,
                              fill->bytes_per_pixel,
                              fill->left + fill->width, &pitch_offset,
                              &x_bias);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    /* r128_accel.c solid fill: pattern ROP with a solid brush; the write
     * of DST_WIDTH_HEIGHT starts it. */
    v9x_r128_emit(blt, V9X_R128_DST_PITCH_OFFSET, pitch_offset);
    v9x_r128_emit(blt, V9X_R128_DP_GUI_MASTER_CNTL,
                  V9X_R128_GMC_DST_PITCH_OFFSET_CNTL |
                  V9X_R128_GMC_BRUSH_SOLID_COLOR |
                  (datatype << V9X_R128_GMC_DST_DATATYPE_SHIFT) |
                  V9X_R128_GMC_SRC_DATATYPE_COLOR |
                  (V9X_R128_ROP3_P << V9X_R128_GMC_ROP3_SHIFT) |
                  V9X_R128_GMC_CLR_CMP_CNTL_DIS |
                  V9X_R128_GMC_AUX_CLIP_DIS);
    v9x_r128_emit(blt, V9X_R128_DP_BRUSH_FRGD_CLR, fill->color);
    v9x_r128_emit(blt, V9X_R128_DP_CNTL,
                  V9X_R128_DST_X_LEFT_TO_RIGHT |
                  V9X_R128_DST_Y_TOP_TO_BOTTOM);
    v9x_r128_emit(blt, V9X_R128_DST_Y_X,
                  R128_YX(fill->left + x_bias, fill->top));
    v9x_r128_emit(blt, V9X_R128_DST_WIDTH_HEIGHT,
                  (fill->width << 16) | fill->height);
    return V9X_STATUS_OK;
}

v9x_status v9x_r128_build_copy(const struct v9x_r128_copy *copy,
                               struct v9x_r128_blt *blt)
{
    v9x_u32 datatype;
    v9x_u32 source_pitch_offset;
    v9x_u32 destination_pitch_offset;
    v9x_u32 source_bias;
    v9x_u32 destination_bias;
    v9x_u32 direction;
    v9x_u32 source_x;
    v9x_u32 source_y;
    v9x_u32 destination_x;
    v9x_u32 destination_y;
    v9x_status status;

    if (copy == 0 || blt == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    blt->count = 0u;

    datatype = v9x_r128_datatype(copy->bytes_per_pixel);
    if (datatype == 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    status = v9x_r128_check_rect(copy->vram_bytes, copy->source_offset,
                                 copy->source_pitch_bytes,
                                 copy->bytes_per_pixel, copy->source_left,
                                 copy->source_top, copy->width,
                                 copy->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_r128_check_rect(copy->vram_bytes, copy->destination_offset,
                                 copy->destination_pitch_bytes,
                                 copy->bytes_per_pixel,
                                 copy->destination_left,
                                 copy->destination_top, copy->width,
                                 copy->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_r128_surface(copy->source_offset, copy->source_pitch_bytes,
                              copy->bytes_per_pixel,
                              copy->source_left + copy->width,
                              &source_pitch_offset, &source_bias);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_r128_surface(copy->destination_offset,
                              copy->destination_pitch_bytes,
                              copy->bytes_per_pixel,
                              copy->destination_left + copy->width,
                              &destination_pitch_offset, &destination_bias);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    /*
     * Direction matters only when the rectangles overlap on one surface.
     * Then a destination below the source runs bottom to top, and one to
     * the right runs right to left; each reversed axis starts from the far
     * edge (r128_accel.c screen-to-screen copy, which takes XAA's xdir and
     * ydir the same way). Both can be reversed at once.
     */
    direction = V9X_R128_DST_X_LEFT_TO_RIGHT | V9X_R128_DST_Y_TOP_TO_BOTTOM;
    source_x = copy->source_left + source_bias;
    source_y = copy->source_top;
    destination_x = copy->destination_left + destination_bias;
    destination_y = copy->destination_top;
    if (copy->source_offset == copy->destination_offset &&
        copy->source_pitch_bytes == copy->destination_pitch_bytes &&
        copy->destination_left < copy->source_left + copy->width &&
        copy->source_left < copy->destination_left + copy->width &&
        copy->destination_top < copy->source_top + copy->height &&
        copy->source_top < copy->destination_top + copy->height) {
        if (copy->destination_top > copy->source_top) {
            direction &= ~V9X_R128_DST_Y_TOP_TO_BOTTOM;
            source_y += copy->height - 1ul;
            destination_y += copy->height - 1ul;
        }
        if (copy->destination_left > copy->source_left) {
            direction &= ~V9X_R128_DST_X_LEFT_TO_RIGHT;
            source_x += copy->width - 1ul;
            destination_x += copy->width - 1ul;
        }
    }

    v9x_r128_emit(blt, V9X_R128_SRC_PITCH_OFFSET, source_pitch_offset);
    v9x_r128_emit(blt, V9X_R128_DST_PITCH_OFFSET, destination_pitch_offset);
    v9x_r128_emit(blt, V9X_R128_DP_GUI_MASTER_CNTL,
                  V9X_R128_GMC_SRC_PITCH_OFFSET_CNTL |
                  V9X_R128_GMC_DST_PITCH_OFFSET_CNTL |
                  V9X_R128_GMC_BRUSH_NONE |
                  (datatype << V9X_R128_GMC_DST_DATATYPE_SHIFT) |
                  V9X_R128_GMC_SRC_DATATYPE_COLOR |
                  (V9X_R128_ROP3_S << V9X_R128_GMC_ROP3_SHIFT) |
                  V9X_R128_DP_SRC_SOURCE_MEMORY |
                  V9X_R128_GMC_CLR_CMP_CNTL_DIS |
                  V9X_R128_GMC_AUX_CLIP_DIS);
    v9x_r128_emit(blt, V9X_R128_DP_CNTL, direction);
    v9x_r128_emit(blt, V9X_R128_SRC_Y_X, R128_YX(source_x, source_y));
    v9x_r128_emit(blt, V9X_R128_DST_Y_X,
                  R128_YX(destination_x, destination_y));
    v9x_r128_emit(blt, V9X_R128_DST_HEIGHT_WIDTH,
                  (copy->height << 16) | copy->width);
    return V9X_STATUS_OK;
}

v9x_u32 v9x_r128_gui_fifo_free(v9x_u32 gui_stat)
{
    return gui_stat & V9X_R128_GUI_FIFO_MASK;
}

v9x_u32 v9x_r128_gui_idle(v9x_u32 gui_stat)
{
    return (v9x_r128_gui_fifo_free(gui_stat) >= V9X_R128_GUI_FIFO_SLOTS &&
            (gui_stat & V9X_R128_GUI_ACTIVE) == 0ul) ? 1ul : 0ul;
}

v9x_u32 v9x_r128_pm4_direct(v9x_u32 pm4_buffer_cntl)
{
    return ((pm4_buffer_cntl >> V9X_R128_PM4_MODE_SHIFT) &
            V9X_R128_PM4_MODE_MASK) == 0ul ? 1ul : 0ul;
}
