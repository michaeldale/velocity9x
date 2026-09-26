/* Neutral unit point/line coverage for the OpenGL render core. */
#include "r3d.h"

typedef union v9x_r3d_line_float {
    float value;
    v9x_u32 bits;
} V9X_R3D_LINE_FLOAT;

/* The HAL has no C runtime; Watcom lowers a float-to-long cast to __CHP.
 * Keep the conversion local for the same reason r3d_clip.c does. */
#ifdef __WATCOMC__
static long v9x_r3d_line_to_long(float value);
#pragma aux v9x_r3d_line_to_long = \
    "sub esp,4" \
    "fistp dword ptr [esp]" \
    "pop eax" \
    parm [8087] value [eax] modify exact [eax];
#else
static long v9x_r3d_line_to_long(float value)
{
    long whole = (long)value;
    float fraction = value - (float)whole;
    if (fraction > 0.5f || (fraction == 0.5f && (whole & 1l))) ++whole;
    else if (fraction < -0.5f ||
             (fraction == -0.5f && (whole & 1l))) --whole;
    return whole;
}
#endif

static int v9x_r3d_line_finite(float value)
{
    V9X_R3D_LINE_FLOAT stored;
    stored.value = value;
    return (stored.bits & 0x7f800000ul) != 0x7f800000ul;
}

static v9x_s32 v9x_r3d_line_floor(float value)
{
    v9x_s32 whole = (v9x_s32)v9x_r3d_line_to_long(value);
    if ((float)whole > value) {
        --whole;
    }
    return whole;
}

static v9x_u32 v9x_r3d_line_color(v9x_u32 a, v9x_u32 b, float t)
{
    v9x_u32 result = 0ul;
    unsigned int shift;
    for (shift = 0u; shift < 32u; shift += 8u) {
        float av = (float)((a >> shift) & 0xfful);
        float bv = (float)((b >> shift) & 0xfful);
        v9x_s32 value = (v9x_s32)v9x_r3d_line_to_long(
            av + (bv - av) * t);
        if (value < 0l) value = 0l;
        if (value > 255l) value = 255l;
        result |= (v9x_u32)value << shift;
    }
    return result;
}

static void v9x_r3d_line_vertex(const V9X_R3D_VERTEX *a,
                                const V9X_R3D_VERTEX *b,
                                float t, v9x_s32 x, v9x_s32 y,
                                V9X_R3D_VERTEX *out)
{
    out->sx = (float)x + 0.5f;
    out->sy = (float)y + 0.5f;
    out->sz = a->sz + (b->sz - a->sz) * t;
    out->rhw = a->rhw + (b->rhw - a->rhw) * t;
    out->color = v9x_r3d_line_color(a->color, b->color, t);
    out->specular = v9x_r3d_line_color(a->specular, b->specular, t);
    out->tu = a->tu + (b->tu - a->tu) * t;
    out->tv = a->tv + (b->tv - a->tv) * t;
}

/* Liang-Barsky against [0,width) x [0,height), returning parameters on the
 * original segment. The high edge is nudged inside without a library nextafter
 * dependency; 1/65536 pixel is below this core's subpixel precision. */
static int v9x_r3d_line_clip(float x0, float y0, float dx, float dy,
                             v9x_u32 width, v9x_u32 height,
                             float *begin, float *end)
{
    float p[4], q[4];
    unsigned int i;
    float hi_x = (float)width - 0.0000152587890625f;
    float hi_y = (float)height - 0.0000152587890625f;
    p[0] = -dx; q[0] = x0;
    p[1] = dx;  q[1] = hi_x - x0;
    p[2] = -dy; q[2] = y0;
    p[3] = dy;  q[3] = hi_y - y0;
    *begin = 0.0f;
    *end = 1.0f;
    for (i = 0u; i < 4u; ++i) {
        if (p[i] == 0.0f) {
            if (q[i] < 0.0f) return 0;
        } else {
            float r = q[i] / p[i];
            if (p[i] < 0.0f) {
                if (r > *end) return 0;
                if (r > *begin) *begin = r;
            } else {
                if (r < *begin) return 0;
                if (r < *end) *end = r;
            }
        }
    }
    return *begin <= *end;
}

int v9x_r3d_draw_point(const V9X_R3D_VERTEX *vertex,
                       v9x_u32 width, v9x_u32 height,
                       V9X_R3D_FRAGMENT_FN fragment, void *user)
{
    v9x_s32 x, y;
    V9X_R3D_VERTEX covered;
    if (vertex == 0 || fragment == 0 || width == 0ul || height == 0ul ||
        !v9x_r3d_line_finite(vertex->sx) ||
        !v9x_r3d_line_finite(vertex->sy)) return -1;
    x = v9x_r3d_line_floor(vertex->sx);
    y = v9x_r3d_line_floor(vertex->sy);
    if (x < 0l || y < 0l || x >= (v9x_s32)width || y >= (v9x_s32)height)
        return 0;
    covered = *vertex;
    covered.sx = (float)x + 0.5f;
    covered.sy = (float)y + 0.5f;
    return fragment(user, x, y, &covered) ? 1 : -1;
}

int v9x_r3d_draw_line(const V9X_R3D_VERTEX *first,
                      const V9X_R3D_VERTEX *last,
                      v9x_u32 width, v9x_u32 height,
                      V9X_R3D_FRAGMENT_FN fragment, void *user)
{
    float dx, dy, begin, end, cx0, cy0, cx1, cy1, length2;
    v9x_s32 x0, y0, x1, y1, sx, sy, err, count = 0;
    v9x_s32 adx, ady;
    if (first == 0 || last == 0 || fragment == 0 || width == 0ul ||
        height == 0ul || !v9x_r3d_line_finite(first->sx) ||
        !v9x_r3d_line_finite(first->sy) ||
        !v9x_r3d_line_finite(last->sx) || !v9x_r3d_line_finite(last->sy))
        return -1;
    dx = last->sx - first->sx;
    dy = last->sy - first->sy;
    length2 = dx * dx + dy * dy;
    if (length2 == 0.0f) return 0;
    if (!v9x_r3d_line_clip(first->sx, first->sy, dx, dy, width, height,
                           &begin, &end)) return 0;
    cx0 = first->sx + dx * begin; cy0 = first->sy + dy * begin;
    cx1 = first->sx + dx * end;   cy1 = first->sy + dy * end;
    /* Division and multiply can put a clipped zero a few ulps below zero;
     * clamp the computed endpoints back to the rectangle before flooring. */
    if (cx0 < 0.0f) cx0 = 0.0f;
    if (cy0 < 0.0f) cy0 = 0.0f;
    if (cx1 < 0.0f) cx1 = 0.0f;
    if (cy1 < 0.0f) cy1 = 0.0f;
    if (cx0 >= (float)width) cx0 = (float)width - 0.0000152587890625f;
    if (cx1 >= (float)width) cx1 = (float)width - 0.0000152587890625f;
    if (cy0 >= (float)height) cy0 = (float)height - 0.0000152587890625f;
    if (cy1 >= (float)height) cy1 = (float)height - 0.0000152587890625f;
    x0 = v9x_r3d_line_floor(cx0); y0 = v9x_r3d_line_floor(cy0);
    x1 = v9x_r3d_line_floor(cx1); y1 = v9x_r3d_line_floor(cy1);
    adx = x1 >= x0 ? x1 - x0 : x0 - x1;
    ady = y1 >= y0 ? y1 - y0 : y0 - y1;
    sx = x0 < x1 ? 1l : -1l;
    sy = y0 < y1 ? 1l : -1l;
    err = adx - ady;
    for (;;) {
        V9X_R3D_VERTEX covered;
        float px = (float)x0 + 0.5f;
        float py = (float)y0 + 0.5f;
        float t = ((px - first->sx) * dx + (py - first->sy) * dy) / length2;
        v9x_s32 twice;
        /* The final endpoint belongs to the following strip segment. A line
         * clipped by the target still owns its last visible pixel: only the
         * application's actual final endpoint is excluded. */
        if (!(end == 1.0f && x0 == x1 && y0 == y1)) {
            if (t < begin) t = begin;
            if (t > end) t = end;
            v9x_r3d_line_vertex(first, last, t, x0, y0, &covered);
            if (!fragment(user, x0, y0, &covered)) return -1;
            ++count;
        }
        if (x0 == x1 && y0 == y1) break;
        twice = err * 2l;
        if (twice > -ady) { err -= ady; x0 += sx; }
        if (twice < adx) { err += adx; y0 += sy; }
    }
    return (int)count;
}
