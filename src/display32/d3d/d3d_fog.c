/*
 * Direct3D table fog as a per-vertex factor; see d3d_fog.h.
 */
#include "d3d_fog.h"

/*
 * Float to long, rounding to nearest. The HAL links no runtime, so a cast
 * would pull in Watcom's __CHP helper; this is r3d_clip.c's fistp. The
 * factor is never a half, so the host's rounding need not match fistp's
 * ties.
 */
#ifdef __WATCOMC__
static long v9x_d3d_fog_to_long(float value);
#pragma aux v9x_d3d_fog_to_long = \
    "sub esp,4" \
    "fistp dword ptr [esp]" \
    "pop eax" \
    parm [8087] value [eax] modify exact [eax];
#else
static long v9x_d3d_fog_to_long(float value)
{
    return (long)(value + 0.5f);
}
#endif

/* Past this e^-x is under 1e-9, which no factor byte tells from zero. */
#define V9X_D3D_FOG_EXP_ZERO 21.0

/* 255, as a float, for the factor's scale. */
#define V9X_D3D_FOG_FACTOR_ONE 255.0f

/* The bits of 1.0f, Direct3D's default FOGTABLEEND and FOGTABLEDENSITY. */
#define V9X_D3D_FOG_FLOAT_ONE 0x3f800000ul

typedef union v9x_d3d_fog_bits {
    v9x_u32 bits;
    float value;
} V9X_D3D_FOG_BITS;

static float v9x_d3d_fog_float(v9x_u32 bits)
{
    V9X_D3D_FOG_BITS convert;

    convert.bits = bits;
    return convert.value;
}

/*
 * e^-x for x >= 0 without a C runtime, as gl_prim.c computes OpenGL's: halve
 * x to at most one half, eight Taylor terms, square back. The error stays
 * under 1e-6, far below the 1/255 a factor is stored to.
 */
static double v9x_d3d_fog_exp_negative(double x)
{
    double term = 1.0;
    double sum = 1.0;
    double divisor;
    unsigned int halvings = 0u;

    if (!(x > 0.0)) {
        return 1.0;
    }
    if (x > V9X_D3D_FOG_EXP_ZERO) {
        return 0.0;
    }
    while (x > 0.5) {
        x *= 0.5;
        ++halvings;
    }
    for (divisor = 1.0; divisor <= 8.0; divisor += 1.0) {
        term *= -x / divisor;
        sum += term;
    }
    while (halvings > 0u) {
        sum *= sum;
        --halvings;
    }
    return sum;
}

void v9x_d3d_fog_table_init(V9X_D3D_FOG_TABLE *table)
{
    table->mode = V9X_D3D_FOG_NONE;
    table->start = 0ul;
    table->end = V9X_D3D_FOG_FLOAT_ONE;
    table->density = V9X_D3D_FOG_FLOAT_ONE;
}

int v9x_d3d_fog_table_active(v9x_u32 fog_enable,
                             const V9X_D3D_FOG_TABLE *table)
{
    if (fog_enable == 0ul) {
        return 0;
    }
    return table->mode == V9X_D3D_FOG_EXP ||
           table->mode == V9X_D3D_FOG_EXP2 ||
           table->mode == V9X_D3D_FOG_LINEAR;
}

float v9x_d3d_fog_table_depth(const V9X_D3D_FOG_TABLE *table, float sz,
                              float rhw)
{
    float density = v9x_d3d_fog_float(table->density);

    if (!(rhw > 0.0f)) {
        return sz;
    }
    if (table->mode == V9X_D3D_FOG_LINEAR &&
        v9x_d3d_fog_float(table->end) > 1.0f) {
        return 1.0f / rhw;
    }
    if ((table->mode == V9X_D3D_FOG_EXP || table->mode == V9X_D3D_FOG_EXP2) &&
        density > 0.0f && density < 1.0f) {
        return 1.0f / rhw;
    }
    return sz;
}

v9x_u32 v9x_d3d_fog_table_factor(const V9X_D3D_FOG_TABLE *table,
                                 float depth)
{
    double f;
    double start;
    double end;
    double dd;

    if (table->mode == V9X_D3D_FOG_LINEAR) {
        start = (double)v9x_d3d_fog_float(table->start);
        end = (double)v9x_d3d_fog_float(table->end);
        /* No range to fog across: the division is undefined, and an
         * unfogged pixel is what the OpenGL side draws for the same. */
        if (!(end != start)) {
            return 255ul;
        }
        f = (end - (double)depth) / (end - start);
    } else if (table->mode == V9X_D3D_FOG_EXP ||
               table->mode == V9X_D3D_FOG_EXP2) {
        dd = (double)v9x_d3d_fog_float(table->density) * (double)depth;
        f = v9x_d3d_fog_exp_negative(table->mode == V9X_D3D_FOG_EXP2
                                         ? dd * dd : dd);
    } else {
        return 255ul;
    }
    /* Written so that a NaN from a hostile float lands at full fog. */
    if (!(f > 0.0)) {
        return 0ul;
    }
    if (f >= 1.0) {
        return 255ul;
    }
    return (v9x_u32)v9x_d3d_fog_to_long((float)f * V9X_D3D_FOG_FACTOR_ONE);
}

void v9x_d3d_fog_table_apply(const V9X_D3D_FOG_TABLE *table,
                             V9X_R3D_VERTEX *vertices, v9x_u32 count)
{
    v9x_u32 index;
    v9x_u32 factor;

    for (index = 0ul; index < count; ++index) {
        factor = v9x_d3d_fog_table_factor(
            table, v9x_d3d_fog_table_depth(table, vertices[index].sz,
                                           vertices[index].rhw));
        vertices[index].specular =
            (vertices[index].specular & 0x00fffffful) | (factor << 24);
    }
}
