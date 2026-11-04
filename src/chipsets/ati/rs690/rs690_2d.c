/*
 * ATI Radeon X1200 IGP (RS690) 2D engine register values. See
 * include\velocity9x\rs690_2d.h for the contract and the sources it follows.
 */
#include "velocity9x/rs690_2d.h"

#define RS690_YX(x, y)  (((v9x_u32)(y) << 16) | (v9x_u32)(x))

/* The GMC destination datatype for a pixel size, or 0 when the engine is
 * not used for it. */
static v9x_u32 v9x_rs690_datatype(v9x_u32 bytes_per_pixel)
{
    if (bytes_per_pixel == 1ul) {
        return V9X_RS690_DATATYPE_8BPP;
    }
    if (bytes_per_pixel == 2ul) {
        return V9X_RS690_DATATYPE_16BPP;
    }
    if (bytes_per_pixel == 4ul) {
        return V9X_RS690_DATATYPE_32BPP;
    }
    return 0ul;
}

static void v9x_rs690_emit(struct v9x_rs690_blt *blt, v9x_u32 offset,
                           v9x_u32 value)
{
    blt->offsets[blt->count] = offset;
    blt->values[blt->count] = value;
    ++blt->count;
}

/*
 * One rectangle on one surface: inside the pitch and inside the frame
 * buffer. Overflow-safe: the coordinates are bounded by the 13-bit fields
 * before any product is formed.
 */
static v9x_status v9x_rs690_check_rect(v9x_u32 fb_bytes, v9x_u32 base,
                                       v9x_u32 pitch_bytes,
                                       v9x_u32 bytes_per_pixel,
                                       v9x_u32 left, v9x_u32 top,
                                       v9x_u32 width, v9x_u32 height)
{
    v9x_u32 last_row_end;

    if (width == 0ul || height == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (left > V9X_RS690_COORD_MAX || top > V9X_RS690_COORD_MAX ||
        width > V9X_RS690_COORD_MAX + 1ul - left ||
        height > V9X_RS690_COORD_MAX + 1ul - top) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if ((left + width) * bytes_per_pixel > pitch_bytes) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (base >= fb_bytes) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    last_row_end = (top + height - 1ul) * pitch_bytes +
                   (left + width) * bytes_per_pixel;
    if (last_row_end > fb_bytes - base) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return V9X_STATUS_OK;
}

/*
 * A surface's *_PITCH_OFFSET word, and the pixels its start lies past the
 * 1 KiB boundary the offset field counts in. The engine addresses from the
 * aligned GPU address; the remainder is moved onto x, which needs it to be
 * a whole number of pixels and x to stay inside its field. The pitch must
 * be a whole number of 64-byte units that fits the 8-bit field.
 */
static v9x_status v9x_rs690_surface(v9x_u32 fb_gpu_base, v9x_u32 fb_bytes,
                                    v9x_u32 base, v9x_u32 pitch_bytes,
                                    v9x_u32 bytes_per_pixel, v9x_u32 right,
                                    v9x_u32 *pitch_offset, v9x_u32 *x_bias)
{
    v9x_u32 units;
    v9x_u32 address;
    v9x_u32 remainder;

    if (pitch_bytes == 0ul || pitch_bytes % V9X_RS690_PITCH_UNIT != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    units = pitch_bytes / V9X_RS690_PITCH_UNIT;
    if (units > V9X_RS690_PITCH_UNITS_MAX) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (fb_gpu_base % V9X_RS690_OFFSET_ALIGN != 0ul ||
        fb_bytes > 0xfffffffful - fb_gpu_base) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    address = fb_gpu_base + base;
    remainder = address % V9X_RS690_OFFSET_ALIGN;
    if (remainder % bytes_per_pixel != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    *x_bias = remainder / bytes_per_pixel;
    if (right + *x_bias > V9X_RS690_COORD_MAX + 1ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    *pitch_offset = (units << V9X_RS690_PITCH_SHIFT) |
                    ((address - remainder) >> V9X_RS690_OFFSET_SHIFT);
    return V9X_STATUS_OK;
}

/* The registers Emit2DState writes ahead of every operation, less the
 * pitch-offsets, which the callers write after it as it does. */
static void v9x_rs690_emit_state(struct v9x_rs690_blt *blt,
                                 v9x_u32 master, v9x_u32 brush,
                                 v9x_u32 direction)
{
    v9x_rs690_emit(blt, V9X_RS690_DEFAULT_SC_BOTTOM_RIGHT, V9X_RS690_SC_MAX);
    v9x_rs690_emit(blt, V9X_RS690_DP_GUI_MASTER_CNTL, master);
    v9x_rs690_emit(blt, V9X_RS690_DP_BRUSH_FRGD_CLR, brush);
    v9x_rs690_emit(blt, V9X_RS690_DP_BRUSH_BKGD_CLR, 0ul);
    v9x_rs690_emit(blt, V9X_RS690_DP_SRC_FRGD_CLR, 0xfffffffful);
    v9x_rs690_emit(blt, V9X_RS690_DP_SRC_BKGD_CLR, 0ul);
    v9x_rs690_emit(blt, V9X_RS690_DP_WRITE_MASK, 0xfffffffful);
    v9x_rs690_emit(blt, V9X_RS690_DP_CNTL, direction);
}

v9x_status v9x_rs690_build_init(v9x_u32 dp_datatype_read,
                                struct v9x_rs690_blt *blt)
{
    if (blt == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    blt->count = 0u;

    /* RADEONEngineRestore, less the pitch-offsets and master control:
     * every operation writes its own. */
    v9x_rs690_emit(blt, V9X_RS690_DP_DATATYPE,
                   dp_datatype_read & ~V9X_RS690_HOST_BIG_ENDIAN_EN);
    v9x_rs690_emit(blt, V9X_RS690_DEFAULT_SC_BOTTOM_RIGHT, V9X_RS690_SC_MAX);
    v9x_rs690_emit(blt, V9X_RS690_DP_WRITE_MASK, 0xfffffffful);
    return V9X_STATUS_OK;
}

v9x_status v9x_rs690_build_fill(const struct v9x_rs690_fill *fill,
                                struct v9x_rs690_blt *blt)
{
    v9x_u32 datatype;
    v9x_u32 pitch_offset;
    v9x_u32 x_bias;
    v9x_status status;

    if (fill == 0 || blt == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    blt->count = 0u;

    datatype = v9x_rs690_datatype(fill->bytes_per_pixel);
    if (datatype == 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (fill->bytes_per_pixel < 4ul &&
        (fill->color >> (fill->bytes_per_pixel * 8ul)) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_rs690_check_rect(fill->fb_bytes, fill->target_offset,
                                  fill->pitch_bytes, fill->bytes_per_pixel,
                                  fill->left, fill->top, fill->width,
                                  fill->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_rs690_surface(fill->fb_gpu_base, fill->fb_bytes,
                               fill->target_offset, fill->pitch_bytes,
                               fill->bytes_per_pixel,
                               fill->left + fill->width, &pitch_offset,
                               &x_bias);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    /* RADEONPrepareSolid then RADEONSolid: pattern copy with a solid
     * brush; the write of DST_HEIGHT_WIDTH starts it. */
    v9x_rs690_emit_state(blt,
                         V9X_RS690_GMC_DST_PITCH_OFFSET_CNTL |
                         V9X_RS690_GMC_BRUSH_SOLID_COLOR |
                         (datatype << V9X_RS690_GMC_DST_DATATYPE_SHIFT) |
                         V9X_RS690_GMC_SRC_DATATYPE_COLOR |
                         (V9X_RS690_ROP3_P << V9X_RS690_GMC_ROP3_SHIFT) |
                         V9X_RS690_GMC_CLR_CMP_CNTL_DIS,
                         fill->color,
                         V9X_RS690_DST_X_LEFT_TO_RIGHT |
                         V9X_RS690_DST_Y_TOP_TO_BOTTOM);
    v9x_rs690_emit(blt, V9X_RS690_DST_PITCH_OFFSET, pitch_offset);
    v9x_rs690_emit(blt, V9X_RS690_DST_Y_X,
                   RS690_YX(fill->left + x_bias, fill->top));
    v9x_rs690_emit(blt, V9X_RS690_DST_HEIGHT_WIDTH,
                   (fill->height << 16) | fill->width);
    return V9X_STATUS_OK;
}

v9x_status v9x_rs690_build_copy(const struct v9x_rs690_copy *copy,
                                struct v9x_rs690_blt *blt)
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

    datatype = v9x_rs690_datatype(copy->bytes_per_pixel);
    if (datatype == 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    status = v9x_rs690_check_rect(copy->fb_bytes, copy->source_offset,
                                  copy->source_pitch_bytes,
                                  copy->bytes_per_pixel, copy->source_left,
                                  copy->source_top, copy->width,
                                  copy->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_rs690_check_rect(copy->fb_bytes, copy->destination_offset,
                                  copy->destination_pitch_bytes,
                                  copy->bytes_per_pixel,
                                  copy->destination_left,
                                  copy->destination_top, copy->width,
                                  copy->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_rs690_surface(copy->fb_gpu_base, copy->fb_bytes,
                               copy->source_offset, copy->source_pitch_bytes,
                               copy->bytes_per_pixel,
                               copy->source_left + copy->width,
                               &source_pitch_offset, &source_bias);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_rs690_surface(copy->fb_gpu_base, copy->fb_bytes,
                               copy->destination_offset,
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
     * edge (RADEONCopy, with EXA's xdir and ydir). Both can be reversed at
     * once.
     */
    direction = V9X_RS690_DST_X_LEFT_TO_RIGHT | V9X_RS690_DST_Y_TOP_TO_BOTTOM;
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
            direction &= ~V9X_RS690_DST_Y_TOP_TO_BOTTOM;
            source_y += copy->height - 1ul;
            destination_y += copy->height - 1ul;
        }
        if (copy->destination_left > copy->source_left) {
            direction &= ~V9X_RS690_DST_X_LEFT_TO_RIGHT;
            source_x += copy->width - 1ul;
            destination_x += copy->width - 1ul;
        }
    }

    /* RADEONDoPrepareCopy then RADEONCopy. */
    v9x_rs690_emit_state(blt,
                         V9X_RS690_GMC_DST_PITCH_OFFSET_CNTL |
                         V9X_RS690_GMC_SRC_PITCH_OFFSET_CNTL |
                         V9X_RS690_GMC_BRUSH_NONE |
                         (datatype << V9X_RS690_GMC_DST_DATATYPE_SHIFT) |
                         V9X_RS690_GMC_SRC_DATATYPE_COLOR |
                         (V9X_RS690_ROP3_S << V9X_RS690_GMC_ROP3_SHIFT) |
                         V9X_RS690_DP_SRC_SOURCE_MEMORY |
                         V9X_RS690_GMC_CLR_CMP_CNTL_DIS,
                         0xfffffffful, direction);
    v9x_rs690_emit(blt, V9X_RS690_DST_PITCH_OFFSET, destination_pitch_offset);
    v9x_rs690_emit(blt, V9X_RS690_SRC_PITCH_OFFSET, source_pitch_offset);
    v9x_rs690_emit(blt, V9X_RS690_SRC_Y_X, RS690_YX(source_x, source_y));
    v9x_rs690_emit(blt, V9X_RS690_DST_Y_X,
                   RS690_YX(destination_x, destination_y));
    v9x_rs690_emit(blt, V9X_RS690_DST_HEIGHT_WIDTH,
                   (copy->height << 16) | copy->width);
    return V9X_STATUS_OK;
}

v9x_u32 v9x_rs690_gui_idle(v9x_u32 rbbm_status)
{
    return ((rbbm_status & V9X_RS690_RBBM_FIFO_MASK) >=
                V9X_RS690_RBBM_FIFO_SLOTS &&
            (rbbm_status & V9X_RS690_RBBM_ACTIVE) == 0ul) ? 1ul : 0ul;
}

v9x_u32 v9x_rs690_fb_gpu_base(v9x_u32 hdp_fb_location)
{
    return (hdp_fb_location & V9X_RS690_FB_LOCATION_MASK) <<
           V9X_RS690_FB_LOCATION_SHIFT;
}

v9x_u32 v9x_rs690_cp_off(v9x_u32 cp_csq_cntl)
{
    return ((cp_csq_cntl >> V9X_RS690_CSQ_MODE_SHIFT) &
            V9X_RS690_CSQ_MODE_MASK) == 0ul ? 1ul : 0ul;
}
