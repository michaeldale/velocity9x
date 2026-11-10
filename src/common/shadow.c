/*
 * The vbe screen shadow's rectangle arithmetic. Pure; see
 * include\velocity9x\shadow.h for what it is for and why the DIB Engine's
 * rectangle is read as inclusive.
 */
#include "velocity9x/shadow.h"

void v9x_shadow_dirty_clear(struct v9x_shadow_dirty *dirty)
{
    dirty->rect.left = 0;
    dirty->rect.top = 0;
    dirty->rect.right = 0;
    dirty->rect.bottom = 0;
    dirty->pending = V9X_FALSE;
}

/* Clip to [0, width) x [0, height) and union into what is pending. */
static void v9x_shadow_union(struct v9x_shadow_dirty *dirty,
                             v9x_s32 left, v9x_s32 top,
                             v9x_s32 right, v9x_s32 bottom,
                             v9x_u16 width, v9x_u16 height)
{
    if (left < 0) {
        left = 0;
    }
    if (top < 0) {
        top = 0;
    }
    if (right > (v9x_s32)width) {
        right = (v9x_s32)width;
    }
    if (bottom > (v9x_s32)height) {
        bottom = (v9x_s32)height;
    }
    if (right <= left || bottom <= top) {
        return;
    }

    if (dirty->pending == V9X_FALSE) {
        dirty->rect.left = (v9x_s16)left;
        dirty->rect.top = (v9x_s16)top;
        dirty->rect.right = (v9x_s16)right;
        dirty->rect.bottom = (v9x_s16)bottom;
        dirty->pending = V9X_TRUE;
        return;
    }

    if (left < (v9x_s32)dirty->rect.left) {
        dirty->rect.left = (v9x_s16)left;
    }
    if (top < (v9x_s32)dirty->rect.top) {
        dirty->rect.top = (v9x_s16)top;
    }
    if (right > (v9x_s32)dirty->rect.right) {
        dirty->rect.right = (v9x_s16)right;
    }
    if (bottom > (v9x_s32)dirty->rect.bottom) {
        dirty->rect.bottom = (v9x_s16)bottom;
    }
}

void v9x_shadow_dirty_add_all(struct v9x_shadow_dirty *dirty,
                              v9x_u16 width, v9x_u16 height)
{
    v9x_shadow_union(dirty, 0, 0, (v9x_s32)width, (v9x_s32)height,
                     width, height);
}

void v9x_shadow_dirty_add(struct v9x_shadow_dirty *dirty,
                          v9x_s16 left, v9x_s16 top,
                          v9x_s16 right, v9x_s16 bottom,
                          v9x_u16 width, v9x_u16 height)
{
    if (right < left || bottom < top) {
        v9x_shadow_dirty_add_all(dirty, width, height);
        return;
    }

    /* Inclusive, so one past each far edge. In 32 bits: 32767 + 1 must not
     * wrap to the far left. */
    v9x_shadow_union(dirty, (v9x_s32)left, (v9x_s32)top,
                     (v9x_s32)right + 1l, (v9x_s32)bottom + 1l,
                     width, height);
}

void v9x_shadow_dirty_add_within(struct v9x_shadow_dirty *dirty,
                                 v9x_s16 left, v9x_s16 top,
                                 v9x_s16 right, v9x_s16 bottom,
                                 const struct v9x_shadow_rect *bound,
                                 v9x_u16 width, v9x_u16 height)
{
    v9x_s32 l;
    v9x_s32 t;
    v9x_s32 r;
    v9x_s32 b;

    if (bound == 0) {
        v9x_shadow_dirty_add(dirty, left, top, right, bottom, width, height);
        return;
    }
    if (right < left || bottom < top) {
        v9x_shadow_dirty_add_rect(dirty, bound, width, height);
        return;
    }

    l = (v9x_s32)left;
    t = (v9x_s32)top;
    r = (v9x_s32)right + 1l;
    b = (v9x_s32)bottom + 1l;
    if (l < (v9x_s32)bound->left) {
        l = (v9x_s32)bound->left;
    }
    if (t < (v9x_s32)bound->top) {
        t = (v9x_s32)bound->top;
    }
    if (r > (v9x_s32)bound->right) {
        r = (v9x_s32)bound->right;
    }
    if (b > (v9x_s32)bound->bottom) {
        b = (v9x_s32)bound->bottom;
    }
    v9x_shadow_union(dirty, l, t, r, b, width, height);
}

v9x_u16 v9x_shadow_output_bound(v9x_u16 style, v9x_u16 count,
                                const v9x_s16 *points,
                                struct v9x_shadow_rect *bound)
{
    v9x_s32 left;
    v9x_s32 top;
    v9x_s32 right;
    v9x_s32 bottom;
    v9x_u16 index;

    if (style == V9X_SHADOW_OS_BEGINNSCAN || style == V9X_SHADOW_OS_ENDNSCAN) {
        bound->left = 0;
        bound->top = 0;
        bound->right = 0;
        bound->bottom = 0;
        return V9X_TRUE;
    }
    if (points == 0) {
        return V9X_FALSE;
    }

    if (style == V9X_SHADOW_OS_POLYLINE && count != 0u) {
        left = right = (v9x_s32)points[0];
        top = bottom = (v9x_s32)points[1];
        for (index = 1u; index < count; ++index) {
            v9x_s32 x = (v9x_s32)points[2u * index];
            v9x_s32 y = (v9x_s32)points[2u * index + 1u];

            if (x < left) {
                left = x;
            }
            if (x > right) {
                right = x;
            }
            if (y < top) {
                top = y;
            }
            if (y > bottom) {
                bottom = y;
            }
        }
    } else if (style == V9X_SHADOW_OS_SCANLINES && count >= 2u) {
        top = bottom = (v9x_s32)points[1];
        left = (v9x_s32)points[2];
        right = (v9x_s32)points[3];
        for (index = 1u; index < count; ++index) {
            v9x_s32 x0 = (v9x_s32)points[2u * index];
            v9x_s32 x1 = (v9x_s32)points[2u * index + 1u];

            if (x0 < left) {
                left = x0;
            }
            if (x1 < left) {
                left = x1;
            }
            if (x0 > right) {
                right = x0;
            }
            if (x1 > right) {
                right = x1;
            }
        }
    } else if (style == V9X_SHADOW_OS_POLYSCANLINE && count != 0u) {
        const v9x_s16 *scan = points;
        v9x_u16 record;

        left = 32767l;
        right = -32768l;
        top = 32767l;
        bottom = -32768l;
        for (record = 0u; record < count; ++record) {
            v9x_u16 xs = (v9x_u16)scan[0];

            if (xs == 0u || (xs & 1u) != 0u) {
                return V9X_FALSE;
            }
            if ((v9x_s32)scan[1] < top) {
                top = (v9x_s32)scan[1];
            }
            /* Exclusive here; one row less, as the inclusive edge below
             * adds it back. */
            if ((v9x_s32)scan[2] - 1l > bottom) {
                bottom = (v9x_s32)scan[2] - 1l;
            }
            for (index = 0u; index < xs; ++index) {
                v9x_s32 x = (v9x_s32)scan[3u + index];

                if (x < left) {
                    left = x;
                }
                if (x > right) {
                    right = x;
                }
            }
            scan += 4u + xs;
        }
        if (right < left || bottom < top) {
            return V9X_FALSE;
        }
    } else {
        return V9X_FALSE;
    }

    /* Inclusive, as the access rectangle is read: one past each far edge,
     * saturated rather than wrapped. */
    right += 1l;
    bottom += 1l;
    if (right > 32767l) {
        right = 32767l;
    }
    if (bottom > 32767l) {
        bottom = 32767l;
    }
    bound->left = (v9x_s16)left;
    bound->top = (v9x_s16)top;
    bound->right = (v9x_s16)right;
    bound->bottom = (v9x_s16)bottom;
    return V9X_TRUE;
}

void v9x_shadow_dirty_add_rect(struct v9x_shadow_dirty *dirty,
                               const struct v9x_shadow_rect *rect,
                               v9x_u16 width, v9x_u16 height)
{
    v9x_shadow_union(dirty, (v9x_s32)rect->left, (v9x_s32)rect->top,
                     (v9x_s32)rect->right, (v9x_s32)rect->bottom,
                     width, height);
}

void v9x_shadow_cursor_rect(v9x_s16 x, v9x_s16 y,
                            v9x_s16 hot_x, v9x_s16 hot_y,
                            v9x_u16 shape_width, v9x_u16 shape_height,
                            struct v9x_shadow_rect *rect)
{
    v9x_s32 left = (v9x_s32)x - (v9x_s32)hot_x;
    v9x_s32 top = (v9x_s32)y - (v9x_s32)hot_y;
    v9x_s32 right = left + (v9x_s32)shape_width;
    v9x_s32 bottom = top + (v9x_s32)shape_height;

    if (shape_width == 0u || shape_height == 0u) {
        rect->left = 0;
        rect->top = 0;
        rect->right = 0;
        rect->bottom = 0;
        return;
    }

    /* Saturate rather than wrap: the result is clipped to the screen later,
     * and a wrapped edge would clip to the wrong side. */
    if (left < -32768l) {
        left = -32768l;
    }
    if (top < -32768l) {
        top = -32768l;
    }
    if (right > 32767l) {
        right = 32767l;
    }
    if (bottom > 32767l) {
        bottom = 32767l;
    }
    rect->left = (v9x_s16)left;
    rect->top = (v9x_s16)top;
    rect->right = (v9x_s16)right;
    rect->bottom = (v9x_s16)bottom;
}

v9x_u16 v9x_shadow_span(const struct v9x_shadow_rect *rect,
                        v9x_u16 bits_per_pixel, v9x_u32 pitch,
                        struct v9x_shadow_span *span)
{
    v9x_u32 bytes_per_pixel;
    v9x_u32 start;
    v9x_u32 end;

    if (bits_per_pixel != 8u && bits_per_pixel != 16u &&
        bits_per_pixel != 24u && bits_per_pixel != 32u) {
        return V9X_FALSE;
    }
    if (rect->left < 0 || rect->top < 0 ||
        rect->right <= rect->left || rect->bottom <= rect->top) {
        return V9X_FALSE;
    }

    bytes_per_pixel = (v9x_u32)bits_per_pixel / 8ul;
    start = (v9x_u32)rect->left * bytes_per_pixel;
    end = (v9x_u32)rect->right * bytes_per_pixel;
    start &= ~(v9x_u32)(V9X_SHADOW_ALIGN_BYTES - 1u);
    end = (end + (V9X_SHADOW_ALIGN_BYTES - 1u)) &
          ~(v9x_u32)(V9X_SHADOW_ALIGN_BYTES - 1u);
    if (end > pitch) {
        end = pitch;
    }
    if (end <= start) {
        return V9X_FALSE;
    }

    span->offset = (v9x_u32)rect->top * pitch + start;
    span->row_bytes = (v9x_u16)(end - start);
    span->rows = (v9x_u16)(rect->bottom - rect->top);
    return V9X_TRUE;
}
