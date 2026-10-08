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
    if (dx >= dy) {
        off_y = 0.5f;
    } else {
        off_x = 0.5f;
    }

    /* a-, a+, b+ and a-, b+, b-: one quad, each end keeping its colour. */
    out[0] = *a;
    out[0].sx -= off_x;
    out[0].sy -= off_y;
    out[1] = *a;
    out[1].sx += off_x;
    out[1].sy += off_y;
    out[2] = *b;
    out[2].sx += off_x;
    out[2].sy += off_y;
    out[3] = out[0];
    out[4] = out[2];
    out[5] = *b;
    out[5].sx -= off_x;
    out[5].sy -= off_y;
}
