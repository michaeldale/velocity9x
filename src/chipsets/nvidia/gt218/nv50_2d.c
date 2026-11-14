/*
 * The GT218 (NV50 family) 2D pushbuffer words: class 502D fill and copy,
 * the object bind, the IB entry and the surface format for a depth.
 * include\velocity9x\nv50_chan.h declares them and cites the encodings.
 * Pure arithmetic, no I/O.
 *
 * A file of its own so the 16-bit driver can link the pushes without the
 * channel builder and the PGRAPH context generator behind it
 * (nv50_chan.c, nv50_grctx.c), which only the HAL and the probe need
 * (docs\plans\nvidia-gt218-gdi-engine.md, step 5.1).
 */
#include "velocity9x/nv50_chan.h"

void V9X_NV50_FAR v9x_nv50_push_init(struct v9x_nv50_push *push, v9x_u32 *words,
                                     v9x_u32 max)
{
    push->words = words;
    push->max = max;
    push->count = 0ul;
    push->overflow = 0ul;
}

static void v9x_nv50_push_word(struct v9x_nv50_push *push, v9x_u32 word)
{
    if (push->count >= push->max) {
        push->overflow = 1ul;
        return;
    }
    push->words[push->count++] = word;
}

void V9X_NV50_FAR v9x_nv50_push_method(struct v9x_nv50_push *push, v9x_u32 subc,
                                       v9x_u32 method, v9x_u32 count,
                                       const v9x_u32 *data)
{
    v9x_u32 index;

    v9x_nv50_push_word(push, V9X_NV50_PUSH_HEADER(subc, method, count));
    for (index = 0ul; index < count; ++index) {
        v9x_nv50_push_word(push, data[index]);
    }
}

static void v9x_nv50_push_one(struct v9x_nv50_push *push, v9x_u32 method,
                              v9x_u32 value)
{
    v9x_nv50_push_method(push, V9X_NV50_SUBC_2D, method, 1ul, &value);
}

void V9X_NV50_FAR v9x_nv50_push_bind_2d(struct v9x_nv50_push *push)
{
    v9x_nv50_push_one(push, V9X_NV50_MTHD_SET_OBJECT, V9X_NV50_HANDLE_2D);
    v9x_nv50_push_one(push, V9X_502D_SET_DST_CONTEXT_DMA,
                      V9X_NV50_HANDLE_DMA_VM);
}

void V9X_NV50_FAR v9x_nv50_push_fill(struct v9x_nv50_push *push, v9x_u32 dst,
                                     v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
                                     v9x_u32 format, v9x_u32 colour,
                                     v9x_u32 x, v9x_u32 y, v9x_u32 w, v9x_u32 h)
{
    v9x_u32 surface[10];
    v9x_u32 points[4];

    /* DST_FORMAT through DST_OFFSET_LOWER, 200h-224h, in one run: a
     * pitch-linear surface, one layer deep, block dimensions unused. */
    surface[0] = format;
    surface[1] = V9X_502D_LAYOUT_PITCH;
    surface[2] = 0ul;
    surface[3] = 1ul;
    surface[4] = 0ul;
    surface[5] = pitch;
    surface[6] = width;
    surface[7] = height;
    surface[8] = 0ul;
    surface[9] = dst;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_2D, V9X_502D_SET_DST_FORMAT,
                         10ul, surface);
    v9x_nv50_push_one(push, V9X_502D_SET_CLIP_ENABLE, 0ul);
    v9x_nv50_push_one(push, V9X_502D_SET_OPERATION,
                      V9X_502D_OPERATION_SRCCOPY);
    v9x_nv50_push_one(push, V9X_502D_RENDER_SOLID_PRIM_MODE,
                      V9X_502D_PRIM_RECTS);
    v9x_nv50_push_one(push, V9X_502D_SET_SOLID_COLOR_FORMAT, format);
    v9x_nv50_push_one(push, V9X_502D_SET_SOLID_COLOR, colour);

    /* One rectangle: its two corners, the second exclusive, as the old
     * nouveau fbcon fill passed them (recalled, not in the saved sources;
     * the probe's readback of the edges decides it). */
    points[0] = x;
    points[1] = y;
    points[2] = x + w;
    points[3] = y + h;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_2D, V9X_502D_SOLID_POINT_X0,
                         4ul, points);
}

void V9X_NV50_FAR v9x_nv50_push_copy(struct v9x_nv50_push *push, v9x_u32 surface,
                                     v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
                                     v9x_u32 format,
                                     v9x_u32 sx, v9x_u32 sy, v9x_u32 dx, v9x_u32 dy,
                                     v9x_u32 w, v9x_u32 h)
{
    v9x_nv50_push_blit(push, surface, pitch, width, height,
                       surface, pitch, width, height, format,
                       sx, sy, dx, dy, w, h);
}

void V9X_NV50_FAR v9x_nv50_push_blit(struct v9x_nv50_push *push,
                                     v9x_u32 src_surface, v9x_u32 src_pitch,
                                     v9x_u32 src_width, v9x_u32 src_height,
                                     v9x_u32 dst_surface, v9x_u32 dst_pitch,
                                     v9x_u32 dst_width, v9x_u32 dst_height,
                                     v9x_u32 format,
                                     v9x_u32 sx, v9x_u32 sy, v9x_u32 dx, v9x_u32 dy,
                                     v9x_u32 w, v9x_u32 h)
{
    v9x_u32 dst[10];
    v9x_u32 src_format[2];
    v9x_u32 src[5];
    v9x_u32 blit[12];

    v9x_nv50_push_one(push, V9X_502D_SET_SRC_CONTEXT_DMA,
                      V9X_NV50_HANDLE_DMA_VM);

    /* The destination, as the fill sets it. */
    dst[0] = format;
    dst[1] = V9X_502D_LAYOUT_PITCH;
    dst[2] = 0ul;
    dst[3] = 1ul;
    dst[4] = 0ul;
    dst[5] = dst_pitch;
    dst[6] = dst_width;
    dst[7] = dst_height;
    dst[8] = 0ul;
    dst[9] = dst_surface;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_2D, V9X_502D_SET_DST_FORMAT,
                         10ul, dst);

    /* The source: format and layout at 230h, then pitch, width, height
     * and address from 244h. The words between belong to block-linear
     * surfaces and are left alone. */
    src_format[0] = format;
    src_format[1] = V9X_502D_LAYOUT_PITCH;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_2D, V9X_502D_SET_SRC_FORMAT,
                         2ul, src_format);
    src[0] = src_pitch;
    src[1] = src_width;
    src[2] = src_height;
    src[3] = 0ul;
    src[4] = src_surface;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_2D, V9X_502D_SET_SRC_PITCH,
                         5ul, src);

    v9x_nv50_push_one(push, V9X_502D_SET_CLIP_ENABLE, 0ul);
    v9x_nv50_push_one(push, V9X_502D_SET_OPERATION,
                      V9X_502D_OPERATION_SRCCOPY);
    v9x_nv50_push_one(push, V9X_502D_SET_SAFE_OVERLAP, 1ul);
    v9x_nv50_push_one(push, V9X_502D_BLIT_CONTROL,
                      V9X_502D_BLIT_POINT_CENTER);

    /* The blit, 8B0h-8DCh in one run: destination rectangle, a scale of
     * exactly 1 each way (fraction, integer), then the source corner
     * (fraction, integer). Writing the last word starts it. The order and
     * the control value are how nouveau's X driver copied (recalled, not
     * in the saved sources); the probe's readback decides them. */
    blit[0] = dx;
    blit[1] = dy;
    blit[2] = w;
    blit[3] = h;
    blit[4] = 0ul;
    blit[5] = 1ul;
    blit[6] = 0ul;
    blit[7] = 1ul;
    blit[8] = 0ul;
    blit[9] = sx;
    blit[10] = 0ul;
    blit[11] = sy;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_2D, V9X_502D_BLIT_DST_X0,
                         12ul, blit);
}

v9x_u32 V9X_NV50_FAR v9x_nv50_2d_format(v9x_u32 bytes_per_pixel)
{
    if (bytes_per_pixel == 1ul) {
        return V9X_502D_FORMAT_Y8;
    }
    if (bytes_per_pixel == 2ul) {
        return V9X_502D_FORMAT_R5G6B5;
    }
    if (bytes_per_pixel == 4ul) {
        return V9X_502D_FORMAT_X8R8G8B8;
    }
    return 0ul;
}

v9x_u32 V9X_NV50_FAR v9x_nv50_ib_next(v9x_u32 index)
{
    return (index + 1ul) % V9X_NV50_IB_ENTRIES;
}

/* A live channel as the registers show it: the same arithmetic as
 * v9x_nv50_chan_build's layout, which the host test holds them to. */
v9x_u32 V9X_NV50_FAR v9x_nv50_live_chan_entry(v9x_u32 base)
{
    return ((base + V9X_NV50_INST + V9X_NV50_INST_RAMFC) >> 8) |
           V9X_NV50_PFIFO_CHAN_ENABLE;
}

v9x_u32 V9X_NV50_FAR v9x_nv50_live_context(v9x_u32 base)
{
    /* PGRAPH's current context: bit 31 | the instance block >> 12. */
    return 0x80000000ul | ((base + V9X_NV50_INST) >> 12);
}

v9x_u32 V9X_NV50_FAR v9x_nv50_live_push(v9x_u32 base)
{
    return base + V9X_NV50_PUSH;
}

v9x_u32 V9X_NV50_FAR v9x_nv50_live_ib(v9x_u32 base)
{
    return base + V9X_NV50_IB;
}

void V9X_NV50_FAR v9x_nv50_ib_entry(v9x_u32 addr, v9x_u32 words, v9x_u32 *entry)
{
    /* Address low; address high (0 here) | length in dwords << 10. */
    entry[0] = addr;
    entry[1] = words << 10;
}
