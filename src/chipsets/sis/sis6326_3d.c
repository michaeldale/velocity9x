/*
 * SiS 6326 3D engine: register values for one triangle.
 *
 * Pure policy, no I/O, integer-only; see sis6326_3d.h. Field layouts from
 * the datasheet (7.14), checked against what SiS's own HAL was caught
 * writing (docs\decisions\2026-10-04-sis6326-3d-state-under-sis-hal.md).
 * The meaning of the direction bit is not settled: callers choose it, and
 * the phase 1 probe measures which value each triangle needs.
 */
#include "velocity9x/sis6326_3d.h"

#define V9X_SIS3D_FLOAT_SIGN      0x80000000ul
#define V9X_SIS3D_FLOAT_BIAS      127ul
#define V9X_SIS3D_FLOAT_MANTISSA  23
#define V9X_SIS3D_Q4_SHIFT        4

#define V9X_SIS3D_BYTES_PER_PIXEL 2ul
#define V9X_SIS3D_DST_FORMAT_SHIFT 16
#define V9X_SIS3D_ROP_SHIFT       24
#define V9X_SIS3D_CLIP_HIGH_SHIFT 13
/* Z test mode 7 (always) in 8A04h D[18:16]; alpha test mode 7 (always) in
 * 8A0Ch D[26:24]. Neither test is enabled here; the values only keep the
 * registers from holding a mode that would refuse every pixel if a later
 * state enabled the test without setting its mode. */
#define V9X_SIS3D_Z_ALWAYS        0x00070000ul
#define V9X_SIS3D_ALPHA_ALWAYS    0x07000000ul
/* 8A28h: destination factor ZERO in D[31:28], source factor ONE in
 * D[27:24] - plain replacement, the value SiS's HAL left. */
#define V9X_SIS3D_BLEND_REPLACE   0x01000000ul

v9x_u32 v9x_sis3d_float_q4(v9x_s32 q)
{
    v9x_u32 sign = 0ul;
    v9x_u32 magnitude;
    v9x_u32 mantissa;
    int top;

    if (q == 0) {
        return 0ul;
    }
    if (q < 0) {
        sign = V9X_SIS3D_FLOAT_SIGN;
        magnitude = (v9x_u32)(-q);
    } else {
        magnitude = (v9x_u32)q;
    }

    top = 31;
    while ((magnitude & (1ul << top)) == 0ul) {
        --top;
    }
    if (top <= V9X_SIS3D_FLOAT_MANTISSA) {
        mantissa = magnitude << (V9X_SIS3D_FLOAT_MANTISSA - top);
    } else {
        mantissa = magnitude >> (top - V9X_SIS3D_FLOAT_MANTISSA);
    }
    return sign |
           (((v9x_u32)(top - V9X_SIS3D_Q4_SHIFT) + V9X_SIS3D_FLOAT_BIAS)
            << V9X_SIS3D_FLOAT_MANTISSA) |
           (mantissa & ((1ul << V9X_SIS3D_FLOAT_MANTISSA) - 1ul));
}

/* An IEEE single's bits as an unsigned key that orders like the value:
 * non-negative values gain the sign bit, negative ones are inverted. */
static v9x_u32 v9x_sis3d_sort_key(v9x_u32 bits)
{
    if ((bits & V9X_SIS3D_FLOAT_SIGN) != 0ul) {
        return ~bits;
    }
    return bits | V9X_SIS3D_FLOAT_SIGN;
}

void v9x_sis3d_order(const struct v9x_sis3d_vertex *vertices,
                     v9x_u32 *top, v9x_u32 *middle, v9x_u32 *bottom)
{
    v9x_u32 index[3];
    v9x_u32 swap;
    int pass;

    index[0] = 0u;
    index[1] = 1u;
    index[2] = 2u;
    /* Three-element bubble sort: stable, so equal Y keeps input order, as
     * SiS's HAL ordered its two vertices at y = 8.25. */
    for (pass = 0; pass < 2; ++pass) {
        if (v9x_sis3d_sort_key(vertices[index[1]].y) <
            v9x_sis3d_sort_key(vertices[index[0]].y)) {
            swap = index[0];
            index[0] = index[1];
            index[1] = swap;
        }
        if (v9x_sis3d_sort_key(vertices[index[2]].y) <
            v9x_sis3d_sort_key(vertices[index[1]].y)) {
            swap = index[1];
            index[1] = index[2];
            index[2] = swap;
        }
    }
    *top = index[0];
    *middle = index[1];
    *bottom = index[2];
}

v9x_u32 v9x_sis3d_primitive(const struct v9x_sis3d_vertex *vertices,
                            v9x_u32 shade, int direction)
{
    v9x_u32 top;
    v9x_u32 middle;
    v9x_u32 bottom;

    v9x_sis3d_order(vertices, &top, &middle, &bottom);
    return (shade << V9X_SIS3D_SHADE_SHIFT) |
           (top << V9X_SIS3D_TOP_SHIFT) |
           (middle << V9X_SIS3D_MIDDLE_SHIFT) |
           (bottom << V9X_SIS3D_BOTTOM_SHIFT) |
           (V9X_SIS3D_FIRE_ON_TSWC << V9X_SIS3D_FIRE_SHIFT) |
           (direction ? V9X_SIS3D_DIRECTION_BIT : 0ul) |
           V9X_SIS3D_DRAW_TRIANGLE;
}

static void v9x_sis3d_emit(struct v9x_sis3d_writes *writes, v9x_u32 offset,
                           v9x_u32 value)
{
    writes->offsets[writes->count] = offset;
    writes->values[writes->count] = value;
    ++writes->count;
}

v9x_status v9x_sis3d_build_flat_state(const struct v9x_sis3d_target *target,
                                      struct v9x_sis3d_writes *writes)
{
    v9x_u32 bytes;

    if (target == 0 || writes == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    writes->count = 0u;

    if (target->width == 0ul || target->height == 0ul ||
        target->pitch_bytes == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (target->pitch_bytes > V9X_SIS3D_DST_PITCH_MAX ||
        target->width > V9X_SIS3D_CLIP_MAX + 1ul ||
        target->height > V9X_SIS3D_CLIP_MAX + 1ul ||
        target->offset >= V9X_SIS3D_ADDRESS_LIMIT) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (target->width > target->pitch_bytes / V9X_SIS3D_BYTES_PER_PIXEL) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    bytes = target->pitch_bytes * target->height;
    if (target->offset >= target->vram_bytes ||
        bytes > target->vram_bytes - target->offset) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (bytes > V9X_SIS3D_ADDRESS_LIMIT - target->offset) {
        return V9X_STATUS_UNSUPPORTED;
    }

    v9x_sis3d_emit(writes, V9X_SIS3D_ENABLE, V9X_SIS3D_ENABLE_PRIM_SETUP);
    v9x_sis3d_emit(writes, V9X_SIS3D_Z_SET, V9X_SIS3D_Z_ALWAYS);
    v9x_sis3d_emit(writes, V9X_SIS3D_ALPHA_SET, V9X_SIS3D_ALPHA_ALWAYS);
    v9x_sis3d_emit(writes, V9X_SIS3D_DST_SET,
                   (V9X_SIS3D_ROP_COPY << V9X_SIS3D_ROP_SHIFT) |
                   (V9X_SIS3D_DST_RGB565 << V9X_SIS3D_DST_FORMAT_SHIFT) |
                   target->pitch_bytes);
    v9x_sis3d_emit(writes, V9X_SIS3D_DST_BASE, target->offset);
    v9x_sis3d_emit(writes, V9X_SIS3D_FOG, 0ul);
    v9x_sis3d_emit(writes, V9X_SIS3D_BLEND, V9X_SIS3D_BLEND_REPLACE);
    /* Top/left in the high 13-bit field, bottom/right in the low one, both
     * inclusive: SiS clipped its 64x64 target to 0 and 63. The texture
     * registers are left alone; the enable word carries no texture bit. */
    v9x_sis3d_emit(writes, V9X_SIS3D_CLIP_TB,
                   (0ul << V9X_SIS3D_CLIP_HIGH_SHIFT) |
                   (target->height - 1ul));
    v9x_sis3d_emit(writes, V9X_SIS3D_CLIP_LR,
                   (0ul << V9X_SIS3D_CLIP_HIGH_SHIFT) |
                   (target->width - 1ul));
    return V9X_STATUS_OK;
}

void v9x_sis3d_build_vertices(const struct v9x_sis3d_vertex *vertices,
                              struct v9x_sis3d_writes *writes)
{
    v9x_u32 index;
    v9x_u32 base;

    writes->count = 0u;
    for (index = 0u; index < 3u; ++index) {
        base = V9X_SIS3D_VERTEX_A + index * V9X_SIS3D_VERTEX_STRIDE;
        v9x_sis3d_emit(writes, base + V9X_SIS3D_VERTEX_FS,
                       vertices[index].fog_specular);
        v9x_sis3d_emit(writes, base + V9X_SIS3D_VERTEX_Z, vertices[index].z);
        v9x_sis3d_emit(writes, base + V9X_SIS3D_VERTEX_X, vertices[index].x);
        v9x_sis3d_emit(writes, base + V9X_SIS3D_VERTEX_Y, vertices[index].y);
        v9x_sis3d_emit(writes, base + V9X_SIS3D_VERTEX_ARGB,
                       vertices[index].argb);
        v9x_sis3d_emit(writes, base + V9X_SIS3D_VERTEX_U, vertices[index].u);
        v9x_sis3d_emit(writes, base + V9X_SIS3D_VERTEX_V, vertices[index].v);
        v9x_sis3d_emit(writes, base + V9X_SIS3D_VERTEX_W, vertices[index].w);
    }
}
