/*
 * Glide vertices to render-interface vertices (docs\plans\glide-2x-wrapper.md,
 * Phase 1). See glide_vertex.h for what each routine owes.
 *
 * No C runtime in GLIDE2X.DLL: a float-to-integer cast lowers to Watcom's
 * __CHP helper, which the DLL cannot link, so the one conversion here goes
 * through a local fistp as gl_prim.c's does. No libm either: the fog
 * table's W values are powers of two built by multiplication.
 */
#include "glide_vertex.h"

#ifdef __WATCOMC__
static long v9x_glide_to_long(double value);
#pragma aux v9x_glide_to_long = \
    "sub esp,4" \
    "fistp dword ptr [esp]" \
    "pop eax" \
    parm [8087] value [eax] modify exact [eax];
#else
static long v9x_glide_to_long(double value)
{
    return (long)value;     /* truncates; the caller corrects to nearest */
}
#endif

/* A 0..255 channel as a byte, clamped, halves rounded up. The x87 store
 * rounds to nearest-even; the correction makes both compilers agree. */
static v9x_u32 v9x_glide_byte(float value)
{
    long whole;
    double difference;

    if (!(value > 0.0f)) {
        return 0ul;
    }
    if (value >= 255.0f) {
        return 255ul;
    }
    whole = v9x_glide_to_long((double)value);
    difference = (double)value - (double)whole;
    if (difference >= 0.5) {
        ++whole;
    } else if (difference < -0.5) {
        --whole;
    }
    return whole < 0l ? 0ul : (whole > 255l ? 255ul : (v9x_u32)whole);
}

/* Glide's s and t span 0..256 along a texture's longer side and
 * 256 / ratio along the shorter (Reference Manual, "Texture coordinates"). */
v9x_u16 v9x_glide_texture_scales(v9x_u32 aspect, float *s_scale,
                                 float *t_scale)
{
    float full = 1.0f / (float)V9X_GLIDE_LOD_EDGE;

    if (aspect > V9X_GLIDE_ASPECT_1X8) {
        return V9X_FALSE;
    }
    *s_scale = full;
    *t_scale = full;
    if (aspect < V9X_GLIDE_ASPECT_1X1) {
        *t_scale = full * (float)(1ul << (V9X_GLIDE_ASPECT_1X1 - aspect));
    } else if (aspect > V9X_GLIDE_ASPECT_1X1) {
        *s_scale = full * (float)(1ul << (aspect - V9X_GLIDE_ASPECT_1X1));
    }
    return V9X_TRUE;
}

static float v9x_glide_unsnap(float value)
{
    if (value >= V9X_GLIDE_SNAP_THRESHOLD) {
        return value - V9X_GLIDE_SNAP_BIAS;
    }
    return value;
}

void v9x_glide_vertex_convert(const V9X_GLIDE_VERTEX_SETUP *setup,
                              const float *in, V9X_R3D_ABI_VERTEX *out)
{
    float oow = in[V9X_GLIDE_VERTEX_OOW];
    v9x_u32 rgb;
    v9x_u32 alpha;
    v9x_u32 fog = 0ul;

    out->sx = v9x_glide_unsnap(in[V9X_GLIDE_VERTEX_X]);
    out->sy = v9x_glide_unsnap(in[V9X_GLIDE_VERTEX_Y]);
    if (setup->origin == V9X_GLIDE_ORIGIN_LOWER_LEFT) {
        out->sy = setup->height - out->sy;
    }

    /* The W-buffer orders by W; 1 - 1/W does too, within 0..1 for W >= 1,
     * and it is the depth a Direct3D projection with the near plane at 1
     * would write. Its precision on a 16-bit Z is measured, not assumed
     * (plan, hazards). */
    out->rhw = oow;
    if (setup->depth_mode == V9X_GLIDE_DEPTH_WBUFFER) {
        out->sz = oow >= 1.0f ? 0.0f : (oow <= 0.0f ? 1.0f : 1.0f - oow);
    } else if (setup->depth_mode == V9X_GLIDE_DEPTH_ZBUFFER) {
        out->sz = in[V9X_GLIDE_VERTEX_OOZ] / 65535.0f;
    } else {
        out->sz = 0.0f;
    }

    if (oow != 0.0f) {
        out->tu = in[V9X_GLIDE_VERTEX_SOW] / oow * setup->s_scale;
        out->tv = in[V9X_GLIDE_VERTEX_TOW] / oow * setup->t_scale;
    } else {
        out->tu = 0.0f;
        out->tv = 0.0f;
    }

    if (setup->color_source == V9X_GLIDE_SOURCE_CONSTANT) {
        rgb = setup->constant_argb & 0x00FFFFFFul;
    } else {
        rgb = (v9x_glide_byte(in[V9X_GLIDE_VERTEX_R]) << 16) |
              (v9x_glide_byte(in[V9X_GLIDE_VERTEX_G]) << 8) |
              v9x_glide_byte(in[V9X_GLIDE_VERTEX_B]);
    }
    if (setup->alpha_source == V9X_GLIDE_SOURCE_CONSTANT) {
        alpha = setup->constant_argb >> 24;
    } else {
        alpha = v9x_glide_byte(in[V9X_GLIDE_VERTEX_A]);
    }
    out->color = (alpha << 24) | rgb;

    /* The interface's fog factor is specular alpha, 255 unfogged. Table
     * fog is looked up here per vertex at W = 1 / oow; the Voodoo looks it
     * up per pixel. Iterated-alpha fog uses the vertex alpha as the amount. */
    if (setup->fog_mode == V9X_GLIDE_FOG_TABLE && setup->fog_table != 0 &&
        oow > 0.0f) {
        fog = v9x_glide_fog_amount(setup->fog_table, 1.0f / oow);
    } else if (setup->fog_mode == V9X_GLIDE_FOG_ITERATED_ALPHA) {
        fog = v9x_glide_byte(in[V9X_GLIDE_VERTEX_A]);
    }
    out->specular = (255ul - fog) << 24;
}

float v9x_glide_fog_index_to_w(unsigned int index)
{
    float power = 8.0f;     /* 2^3 */
    unsigned int step;

    for (step = 0u; step < (index >> 2); ++step) {
        power *= 2.0f;
    }
    return power / (float)(8u - (index & 3u));
}

v9x_u32 v9x_glide_fog_amount(const v9x_u8 *table, float w)
{
    unsigned int index;
    float low;
    float high;
    float fraction;
    float amount;

    if (!(w > v9x_glide_fog_index_to_w(0u))) {
        return (v9x_u32)table[0];
    }
    high = v9x_glide_fog_index_to_w(0u);
    for (index = 1u; index < V9X_GLIDE_FOG_TABLE_SIZE; ++index) {
        low = high;
        high = v9x_glide_fog_index_to_w(index);
        if (w <= high) {
            fraction = (w - low) / (high - low);
            amount = (float)table[index - 1u] +
                     fraction * ((float)table[index] - (float)table[index - 1u]);
            return v9x_glide_byte(amount);
        }
    }
    return (v9x_u32)table[V9X_GLIDE_FOG_TABLE_SIZE - 1u];
}

/*
 * Glide culls on the sign of the area in the coordinates it was given. A
 * lower-left origin's conversion flipped y, which flipped the sign, so it
 * is flipped back. Which winding Glide calls negative is taken from the
 * Reference Manual's wording and is unmeasured: NFS II SE culls nothing
 * in a race (census).
 */
v9x_u16 v9x_glide_cull_keep(v9x_u32 cull_mode, v9x_u32 origin,
                            const V9X_R3D_ABI_VERTEX *a,
                            const V9X_R3D_ABI_VERTEX *b,
                            const V9X_R3D_ABI_VERTEX *c)
{
    float area;

    if (cull_mode == V9X_GLIDE_CULL_DISABLE) {
        return V9X_TRUE;
    }
    area = (b->sx - a->sx) * (c->sy - a->sy) - (c->sx - a->sx) * (b->sy - a->sy);
    if (origin == V9X_GLIDE_ORIGIN_LOWER_LEFT) {
        area = -area;
    }
    if (cull_mode == V9X_GLIDE_CULL_NEGATIVE) {
        return area < 0.0f ? V9X_FALSE : V9X_TRUE;
    }
    if (cull_mode == V9X_GLIDE_CULL_POSITIVE) {
        return area > 0.0f ? V9X_FALSE : V9X_TRUE;
    }
    return V9X_TRUE;
}

/* A packed colour's channels mixed `t` of the way from a to b. */
static v9x_u32 v9x_glide_mix_color(v9x_u32 a, v9x_u32 b, float t)
{
    v9x_u32 result = 0ul;
    unsigned int shift;
    float from;
    float to;

    for (shift = 0u; shift < 32u; shift += 8u) {
        from = (float)((a >> shift) & 0xFFul);
        to = (float)((b >> shift) & 0xFFul);
        result |= v9x_glide_byte(from + (to - from) * t) << shift;
    }
    return result;
}

/* The point `t` of the way along a screen-space edge. Depth and rhw are
 * linear in screen space; s and t are, divided by w, so they are carried
 * as tu * rhw and divided again. */
static void v9x_glide_mix_vertex(const V9X_R3D_ABI_VERTEX *a,
                                 const V9X_R3D_ABI_VERTEX *b, float t,
                                 V9X_R3D_ABI_VERTEX *out)
{
    float rhw = a->rhw + (b->rhw - a->rhw) * t;
    float su = a->tu * a->rhw + (b->tu * b->rhw - a->tu * a->rhw) * t;
    float sv = a->tv * a->rhw + (b->tv * b->rhw - a->tv * a->rhw) * t;

    out->sx = a->sx + (b->sx - a->sx) * t;
    out->sy = a->sy + (b->sy - a->sy) * t;
    out->sz = a->sz + (b->sz - a->sz) * t;
    out->rhw = rhw;
    out->tu = rhw != 0.0f ? su / rhw : a->tu + (b->tu - a->tu) * t;
    out->tv = rhw != 0.0f ? sv / rhw : a->tv + (b->tv - a->tv) * t;
    out->color = v9x_glide_mix_color(a->color, b->color, t);
    out->specular = v9x_glide_mix_color(a->specular, b->specular, t);
}

/* The signed distance inside one edge of the rectangle: 0 left, 1 right,
 * 2 top, 3 bottom. */
static float v9x_glide_clip_distance(const V9X_R3D_ABI_VERTEX *v,
                                     unsigned int edge, const float *rect)
{
    switch (edge) {
    case 0u: return v->sx - rect[0];
    case 1u: return rect[2] - v->sx;
    case 2u: return v->sy - rect[1];
    default: return rect[3] - v->sy;
    }
}

/* Sutherland-Hodgman against the four edges, then a fan. A triangle
 * clipped by four lines has at most seven corners. */
unsigned int v9x_glide_clip_rect(const V9X_R3D_ABI_VERTEX *triangle,
                                 float left, float top, float right,
                                 float bottom, V9X_R3D_ABI_VERTEX *out)
{
    V9X_R3D_ABI_VERTEX polygons[2][7];
    float rect[4];
    unsigned int count = 3u;
    unsigned int next_count;
    unsigned int edge;
    unsigned int i;
    unsigned int current = 0u;
    float d0;
    float d1;

    rect[0] = left;
    rect[1] = top;
    rect[2] = right;
    rect[3] = bottom;
    for (i = 0u; i < 3u; ++i) {
        polygons[0][i] = triangle[i];
    }
    for (edge = 0u; edge < 4u && count >= 3u; ++edge) {
        const V9X_R3D_ABI_VERTEX *in = polygons[current];
        V9X_R3D_ABI_VERTEX *result = polygons[current ^ 1u];

        next_count = 0u;
        for (i = 0u; i < count; ++i) {
            const V9X_R3D_ABI_VERTEX *a = &in[i];
            const V9X_R3D_ABI_VERTEX *b = &in[(i + 1u) % count];

            d0 = v9x_glide_clip_distance(a, edge, rect);
            d1 = v9x_glide_clip_distance(b, edge, rect);
            if (d0 >= 0.0f && next_count < 7u) {
                result[next_count++] = *a;
            }
            if (((d0 >= 0.0f) != (d1 >= 0.0f)) && next_count < 7u) {
                v9x_glide_mix_vertex(a, b, d0 / (d0 - d1), &result[next_count++]);
            }
        }
        count = next_count;
        current ^= 1u;
    }
    if (count < 3u) {
        return 0u;
    }
    for (i = 0u; i + 2u < count; ++i) {
        out[i * 3u] = polygons[current][0];
        out[i * 3u + 1u] = polygons[current][i + 1u];
        out[i * 3u + 2u] = polygons[current][i + 2u];
    }
    return count - 2u;
}

void v9x_glide_line_triangles(const V9X_R3D_ABI_VERTEX *a,
                              const V9X_R3D_ABI_VERTEX *b,
                              V9X_R3D_ABI_VERTEX *out)
{
    float dx = b->sx - a->sx;
    float dy = b->sy - a->sy;
    float off_x = 0.0f;
    float off_y = 0.0f;

    if (dx < 0.0f) {
        dx = -dx;
    }
    if (dy < 0.0f) {
        dy = -dy;
    }
    /* One pixel across the minor axis, from the line's coordinate to the
     * next whole pixel: a line through integer y lights row y whether the
     * engine samples at a pixel's corner or its centre. Centred on the
     * line (y - 0.5 to y + 0.5) it lit nothing at y = 400 on Gen3, whose
     * sample point sat on the strip's excluded edge (V9XGLIDP, netbook,
     * 2026-10-08). Which row a Voodoo lights for an integer y is not
     * measured here. */
    if (dx >= dy) {
        off_y = 1.0f;
    } else {
        off_x = 1.0f;
    }

    /* a, a+, b+ and a, b+, b: one quad, each end keeping its colour. */
    out[0] = *a;
    out[1] = *a;
    out[1].sx += off_x;
    out[1].sy += off_y;
    out[2] = *b;
    out[2].sx += off_x;
    out[2].sy += off_y;
    out[3] = out[0];
    out[4] = out[2];
    out[5] = *b;
}
