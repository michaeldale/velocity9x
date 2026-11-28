/*
 * GDI screen-to-screen copies on the Gen3 blitter: the band plan and the
 * stream, as pure C. No MMIO; src\display16\gdi_gen3.c submits what this
 * builds. docs\plans\gen3-gdi-blitter.md.
 *
 * The packets are i9xx_blt.c's, and the stream passes i9xx_blt.c's decoder
 * before it leaves here, so the 2D allowlist the HAL's blits answer to is
 * the one these answer to as well.
 *
 * The 16-bit driver links this into LEAFCODE (velocity9x\leaf16.h), where a
 * 32-bit multiply or divide cannot be compiled: every one below has
 * operands under 2^16, which v9x_i9xx_gdi_band_count makes sure of by
 * refusing extents the blitter's 15-bit fields could not carry anyway.
 */
#include "velocity9x/i9xx_gdi.h"
#include "velocity9x/leaf16.h"

/* The signed move from source to destination along one axis. */
static v9x_s32 v9x_i9xx_gdi_move(v9x_u32 destination, v9x_u32 source)
{
    return (v9x_s32)(destination - source);
}

static v9x_u32 v9x_i9xx_gdi_magnitude(v9x_s32 value)
{
    return value < 0 ? (v9x_u32)(-value) : (v9x_u32)value;
}

/*
 * Do the two rectangles share a pixel? Same size, so they do exactly when
 * the move is shorter than the extent on both axes.
 */
static v9x_u16 v9x_i9xx_gdi_overlaps(const struct v9x_i9xx_gdi_copy *copy)
{
    v9x_u32 across =
        v9x_i9xx_gdi_magnitude(v9x_i9xx_gdi_move(copy->x, copy->source_x));
    v9x_u32 down =
        v9x_i9xx_gdi_magnitude(v9x_i9xx_gdi_move(copy->y, copy->source_y));

    return across < copy->width && down < copy->height ? V9X_TRUE : V9X_FALSE;
}

v9x_status v9x_i9xx_gdi_band_count(const struct v9x_i9xx_gdi_copy *copy,
                                   v9x_u32 *count)
{
    v9x_u32 across;
    v9x_u32 down;
    v9x_u32 bands;

    if (count != 0) {
        *count = 0ul;
    }
    if (copy == 0 || count == 0 || copy->width == 0ul ||
        copy->height == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (copy->width >= V9X_I9XX_BLT_FIELD_LIMIT ||
        copy->height >= V9X_I9XX_BLT_FIELD_LIMIT) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (v9x_i9xx_gdi_overlaps(copy) == V9X_FALSE) {
        *count = 1ul;
        return V9X_STATUS_OK;
    }
    across = v9x_i9xx_gdi_magnitude(v9x_i9xx_gdi_move(copy->x, copy->source_x));
    down = v9x_i9xx_gdi_magnitude(v9x_i9xx_gdi_move(copy->y, copy->source_y));
    /* A copy onto itself changes nothing and needs no band. */
    if (across == 0ul && down == 0ul) {
        return V9X_STATUS_OK;
    }
    /* Under 2^16 on both sides: the move is shorter than the extent. */
    if (down != 0ul) {
        bands = (v9x_u32)((v9x_u16)(copy->height + down - 1ul) /
                          (v9x_u16)down);
    } else {
        bands = (v9x_u32)((v9x_u16)(copy->width + across - 1ul) /
                          (v9x_u16)across);
    }
    if (bands > V9X_I9XX_GDI_MAX_COPY_BANDS) {
        return V9X_STATUS_UNSUPPORTED;
    }
    /* More than one stream: worth it only for bands the CPU is slower at.
     * Both extents are below 32768 (the blitter's coordinate fields, checked
     * above), so the product fits, and bands is at most 2048. */
    if (bands > V9X_I9XX_GDI_MAX_BANDS &&
        V9X_LEAF_MUL16(copy->width, copy->height) <
            V9X_LEAF_MUL16(bands, V9X_I9XX_GDI_MIN_BAND_PIXELS)) {
        return V9X_STATUS_UNSUPPORTED;
    }
    *count = bands;
    return V9X_STATUS_OK;
}

/*
 * The span of band `index` along an axis of `extent` pixels cut into steps
 * of `step`, counted from the low end when `ascending` and from the high
 * end otherwise. The last band takes what is left.
 */
static void v9x_i9xx_gdi_span(v9x_u32 extent, v9x_u32 step, v9x_u32 index,
                              v9x_u16 ascending, v9x_u32 *start,
                              v9x_u32 *length)
{
    /* index is below 2048 bands and step below 32768 pixels. */
    v9x_u32 done = V9X_LEAF_MUL16(index, step);
    v9x_u32 left = extent - done;
    v9x_u32 size = left < step ? left : step;

    *length = size;
    *start = ascending != V9X_FALSE ? done : left - size;
}

void v9x_i9xx_gdi_band_at(const struct v9x_i9xx_gdi_copy *copy,
                          v9x_u32 index, struct v9x_i9xx_gdi_band *band)
{
    v9x_s32 across = v9x_i9xx_gdi_move(copy->x, copy->source_x);
    v9x_s32 down = v9x_i9xx_gdi_move(copy->y, copy->source_y);

    band->left = 0ul;
    band->top = 0ul;
    band->width = copy->width;
    band->height = copy->height;
    if (v9x_i9xx_gdi_overlaps(copy) == V9X_FALSE) {
        return;
    }
    /*
     * Moving up, the top band goes first: its destination is above the
     * source, in rows no band reads. Each later band writes the rows the
     * band before it read. Moving down, the same from the bottom. With no
     * vertical move, the same with columns.
     */
    if (down != 0) {
        v9x_i9xx_gdi_span(copy->height, v9x_i9xx_gdi_magnitude(down), index,
                          down < 0 ? V9X_TRUE : V9X_FALSE, &band->top,
                          &band->height);
        return;
    }
    v9x_i9xx_gdi_span(copy->width, v9x_i9xx_gdi_magnitude(across), index,
                      across < 0 ? V9X_TRUE : V9X_FALSE, &band->left,
                      &band->width);
}

v9x_status v9x_i9xx_gdi_build_copy(const struct v9x_i9xx_gdi_copy *copy,
                                   v9x_u32 first_band,
                                   v9x_u32 breadcrumb_offset,
                                   v9x_u32 sequence,
                                   v9x_u32 *stream, v9x_u32 capacity,
                                   v9x_u32 *written, v9x_u32 *bands_out)
{
    struct v9x_i9xx_gdi_band band;
    struct v9x_i9xx_blt blt;
    v9x_u32 bands;
    v9x_u32 last;
    v9x_u32 index;
    v9x_u32 at;
    v9x_u32 produced;
    v9x_u32 rejected;
    v9x_status status;

    if (written != 0) {
        *written = 0ul;
    }
    if (bands_out != 0) {
        *bands_out = 0ul;
    }
    if (stream == 0 || written == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_i9xx_gdi_band_count(copy, &bands);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    /* A copy onto itself has no band, and its one stream is the flush and
     * the breadcrumb. */
    if (first_band != 0ul && first_band >= bands) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    last = bands - first_band > V9X_I9XX_GDI_MAX_BANDS
               ? first_band + V9X_I9XX_GDI_MAX_BANDS
               : bands;
    if (capacity < 1ul + V9X_LEAF_MUL16(last - first_band,
                                        V9X_I9XX_BLT_COPY_DWORDS + 1ul) +
                       V9X_I9XX_BREADCRUMB_STREAM_DWORDS + 1ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    at = 0ul;
    stream[at++] = V9X_I9XX_MI_FLUSH;
    blt.bytes_per_pixel = copy->bytes_per_pixel;
    blt.vram_bytes = copy->vram_bytes;
    blt.destination_base = copy->base;
    blt.destination_pitch = copy->pitch;
    blt.source_base = copy->base;
    blt.source_pitch = copy->pitch;
    blt.color = 0ul;
    for (index = first_band; index < last; ++index) {
        v9x_i9xx_gdi_band_at(copy, index, &band);
        blt.left = copy->x + band.left;
        blt.top = copy->y + band.top;
        blt.right = blt.left + band.width;
        blt.bottom = blt.top + band.height;
        blt.source_left = copy->source_x + band.left;
        blt.source_top = copy->source_y + band.top;
        status = v9x_i9xx_build_copy_blt(&blt, stream + at, capacity - at,
                                         &produced);
        if (status != V9X_STATUS_OK) {
            return status;
        }
        at += produced;
    }
    status = v9x_i9xx_build_breadcrumb_stream(breadcrumb_offset, sequence,
                                              stream + at, capacity - at,
                                              &produced);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    at += produced;
    if ((at & 1ul) != 0ul) {
        stream[at++] = V9X_I9XX_MI_NOOP;
    }
    if (v9x_i9xx_decode_blt_stream(stream, at, copy->bytes_per_pixel,
                                   copy->vram_bytes, breadcrumb_offset,
                                   &rejected) == V9X_FALSE) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *written = at;
    if (bands_out != 0) {
        *bands_out = bands;
    }
    return V9X_STATUS_OK;
}
