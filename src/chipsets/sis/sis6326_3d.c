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

#define V9X_SIS3D_Z_COMPARE_SHIFT     16
#define V9X_SIS3D_ALPHA_COMPARE_SHIFT 24
#define V9X_SIS3D_ALPHA_REF_SHIFT     16
#define V9X_SIS3D_BLEND_DST_SHIFT     28
#define V9X_SIS3D_BLEND_SRC_SHIFT     24

/* Texture registers (registers section 8). */
#define V9X_SIS3D_TEXEL_SHIFT         24
#define V9X_SIS3D_MAPPING_SHIFT       16
#define V9X_SIS3D_LEVELS_SHIFT        8
#define V9X_SIS3D_CLEAR_CACHE         0x00000010ul
#define V9X_SIS3D_MIN_MASK            0x00000007ul
#define V9X_SIS3D_TEXTURE_LOG2_MAX    9ul
#define V9X_SIS3D_BLEND_MASK_SHIFT    12
#define V9X_SIS3D_BLEND_MASK_BIT_MAX  7ul
#define V9X_SIS3D_PITCH_UNIT          4ul
#define V9X_SIS3D_PITCH_EVEN_SHIFT    16
/* Pitch field: (2m + 1) << (e + 2) bytes, e in D[10:7], m in D[6:0]. */
#define V9X_SIS3D_PITCH_UNIT_SHIFT    2
#define V9X_SIS3D_PITCH_UNIT_MASK     0x00000003ul
#define V9X_SIS3D_PITCH_EXPONENT_SHIFT 7
#define V9X_SIS3D_PITCH_EXPONENT_MAX  15ul
#define V9X_SIS3D_PITCH_ODD_MAX       255ul
#define V9X_SIS3D_TBLEND_COLOUR_MAX   0x3ful
#define V9X_SIS3D_TBLEND_COLOUR_SHIFT 26
#define V9X_SIS3D_TBLEND_ALPHA_SHIFT  24
#define V9X_SIS3D_LOG2_WIDTH_SHIFT    28
#define V9X_SIS3D_LOG2_HEIGHT_SHIFT   24

v9x_u32 v9x_sis3d_float_q4(v9x_s32 q)
{
    return v9x_sis3d_float_fixed(q, V9X_SIS3D_Q4_SHIFT);
}

v9x_u32 v9x_sis3d_float_fixed(v9x_s32 q, int fraction_bits)
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
           ((v9x_u32)(top - fraction_bits + (int)V9X_SIS3D_FLOAT_BIAS)
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

/*
 * A surface of width x height 16-bit pixels at offset, inside VRAM and the
 * 22-bit address range, with a pitch its field holds. INVALID_ARGUMENT for
 * one the caller got wrong, UNSUPPORTED for one the fields cannot express.
 */
static v9x_status v9x_sis3d_check_surface(v9x_u32 vram_bytes, v9x_u32 offset,
                                          v9x_u32 pitch_bytes,
                                          v9x_u32 width, v9x_u32 height)
{
    v9x_u32 bytes;

    if (width == 0ul || height == 0ul || pitch_bytes == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (pitch_bytes > V9X_SIS3D_DST_PITCH_MAX ||
        width > V9X_SIS3D_CLIP_MAX + 1ul ||
        height > V9X_SIS3D_CLIP_MAX + 1ul ||
        offset >= V9X_SIS3D_ADDRESS_LIMIT) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (width > pitch_bytes / V9X_SIS3D_BYTES_PER_PIXEL) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    bytes = pitch_bytes * height;
    if (offset >= vram_bytes || bytes > vram_bytes - offset) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (bytes > V9X_SIS3D_ADDRESS_LIMIT - offset) {
        return V9X_STATUS_UNSUPPORTED;
    }
    return V9X_STATUS_OK;
}

v9x_status v9x_sis3d_build_flat_state(const struct v9x_sis3d_target *target,
                                      struct v9x_sis3d_writes *writes)
{
    v9x_status status;

    if (target == 0 || writes == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    writes->count = 0u;

    status = v9x_sis3d_check_surface(target->vram_bytes, target->offset,
                                     target->pitch_bytes, target->width,
                                     target->height);
    if (status != V9X_STATUS_OK) {
        return status;
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

v9x_status v9x_sis3d_build_state(const struct v9x_sis3d_state *state,
                                 struct v9x_sis3d_writes *writes)
{
    const struct v9x_sis3d_target *target;
    v9x_status status;

    if (state == 0 || writes == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    writes->count = 0u;
    target = &state->target;

    if (state->z_compare > V9X_SIS3D_CMP_ALWAYS ||
        state->alpha_compare > V9X_SIS3D_CMP_ALWAYS ||
        state->alpha_reference > 0xfful ||
        state->blend_destination > V9X_SIS3D_BLEND_INV_DST_ALPHA ||
        state->blend_source > V9X_SIS3D_BLEND_BOTH_INV_SRC_ALPHA ||
        state->blend_source == V9X_SIS3D_BLEND_SRC_COLOR ||
        state->blend_source == V9X_SIS3D_BLEND_INV_SRC_COLOR) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_sis3d_check_surface(target->vram_bytes, target->offset,
                                     target->pitch_bytes, target->width,
                                     target->height);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    /* The Z buffer matters only when the engine reads or writes it. */
    if ((state->enable & (V9X_SIS3D_ENABLE_Z_TEST |
                          V9X_SIS3D_ENABLE_Z_WRITE)) != 0ul) {
        status = v9x_sis3d_check_surface(target->vram_bytes,
                                         state->z_offset,
                                         state->z_pitch_bytes,
                                         target->width, target->height);
        if (status != V9X_STATUS_OK) {
            return status;
        }
    }

    v9x_sis3d_emit(writes, V9X_SIS3D_ENABLE, state->enable);
    v9x_sis3d_emit(writes, V9X_SIS3D_Z_SET,
                   V9X_SIS3D_Z16 |
                   (state->z_compare << V9X_SIS3D_Z_COMPARE_SHIFT) |
                   (state->z_pitch_bytes & V9X_SIS3D_DST_PITCH_MAX));
    v9x_sis3d_emit(writes, V9X_SIS3D_Z_BASE, state->z_offset);
    v9x_sis3d_emit(writes, V9X_SIS3D_ALPHA_SET,
                   (state->alpha_compare << V9X_SIS3D_ALPHA_COMPARE_SHIFT) |
                   (state->alpha_reference << V9X_SIS3D_ALPHA_REF_SHIFT));
    v9x_sis3d_emit(writes, V9X_SIS3D_DST_SET,
                   (V9X_SIS3D_ROP_COPY << V9X_SIS3D_ROP_SHIFT) |
                   (V9X_SIS3D_DST_RGB565 << V9X_SIS3D_DST_FORMAT_SHIFT) |
                   target->pitch_bytes);
    v9x_sis3d_emit(writes, V9X_SIS3D_DST_BASE, target->offset);
    v9x_sis3d_emit(writes, V9X_SIS3D_FOG, 0ul);
    v9x_sis3d_emit(writes, V9X_SIS3D_BLEND,
                   (state->blend_destination << V9X_SIS3D_BLEND_DST_SHIFT) |
                   (state->blend_source << V9X_SIS3D_BLEND_SRC_SHIFT));
    v9x_sis3d_emit(writes, V9X_SIS3D_CLIP_TB,
                   (0ul << V9X_SIS3D_CLIP_HIGH_SHIFT) |
                   (target->height - 1ul));
    v9x_sis3d_emit(writes, V9X_SIS3D_CLIP_LR,
                   (0ul << V9X_SIS3D_CLIP_HIGH_SHIFT) |
                   (target->width - 1ul));
    return V9X_STATUS_OK;
}

v9x_status v9x_sis3d_texture_pitch_field(v9x_u32 pitch_bytes,
                                         v9x_u32 *field)
{
    v9x_u32 exponent = 0ul;
    v9x_u32 odd;

    if (field == 0 || pitch_bytes == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if ((pitch_bytes & V9X_SIS3D_PITCH_UNIT_MASK) != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    odd = pitch_bytes >> V9X_SIS3D_PITCH_UNIT_SHIFT;
    while ((odd & 1ul) == 0ul) {
        odd >>= 1;
        ++exponent;
    }
    if (exponent > V9X_SIS3D_PITCH_EXPONENT_MAX ||
        odd > V9X_SIS3D_PITCH_ODD_MAX) {
        return V9X_STATUS_UNSUPPORTED;
    }
    *field = (exponent << V9X_SIS3D_PITCH_EXPONENT_SHIFT) | (odd >> 1);
    return V9X_STATUS_OK;
}

/* Bytes per texel of the formats the builder accepts; 0 for the rest. */
static v9x_u32 v9x_sis3d_texel_bytes(v9x_u32 format)
{
    if (format == V9X_SIS3D_TEXEL_RGB555 ||
        format == V9X_SIS3D_TEXEL_RGB565 ||
        format == V9X_SIS3D_TEXEL_ARGB1555 ||
        format == V9X_SIS3D_TEXEL_ARGB4444) {
        return 2ul;
    }
    if (format == V9X_SIS3D_TEXEL_ARGB8888) {
        return 4ul;
    }
    return 0ul;
}

/* A side's log2 at a level: halved per level, never below one texel. */
static v9x_u32 v9x_sis3d_mip_log2(v9x_u32 log2_size, v9x_u32 level)
{
    return log2_size > level ? log2_size - level : 0ul;
}

/* A level's tight pitch: its row, rounded up to the 4-byte pitch unit.
 * Rows are powers of two, so the engine's OR of row and column is safe. */
static v9x_u32 v9x_sis3d_mip_pitch(const struct v9x_sis3d_texture *texture,
                                   v9x_u32 texel_bytes, v9x_u32 level)
{
    v9x_u32 row = texel_bytes << v9x_sis3d_mip_log2(texture->log2_width,
                                                    level);

    return row < V9X_SIS3D_PITCH_UNIT ? V9X_SIS3D_PITCH_UNIT : row;
}

/* The pitch field of level 1-9, or 0 past the chain's last level. */
static v9x_u32 v9x_sis3d_mip_pitch_field(
    const struct v9x_sis3d_texture *texture, v9x_u32 texel_bytes,
    v9x_u32 level)
{
    v9x_u32 field = 0ul;

    if (level > texture->levels) {
        return 0ul;
    }
    /* A power of two from 4 to 2048 bytes always encodes. */
    (void)v9x_sis3d_texture_pitch_field(
        v9x_sis3d_mip_pitch(texture, texel_bytes, level), &field);
    return field;
}

v9x_status v9x_sis3d_build_texture(const struct v9x_sis3d_texture *texture,
                                   struct v9x_sis3d_writes *writes)
{
    v9x_u32 texel_bytes;
    v9x_u32 row_bytes;
    v9x_u32 level_bytes;
    v9x_u32 pitch_field;
    v9x_u32 level;
    v9x_u32 offset;
    v9x_status status;

    if (texture == 0 || writes == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    writes->count = 0u;

    if (texture->log2_width > V9X_SIS3D_TEXTURE_LOG2_MAX ||
        texture->log2_height > V9X_SIS3D_TEXTURE_LOG2_MAX ||
        texture->levels > (texture->log2_width > texture->log2_height
                               ? texture->log2_width
                               : texture->log2_height) ||
        texture->blend_mask_bit > V9X_SIS3D_BLEND_MASK_BIT_MAX ||
        texture->mapping > 0xfful ||
        (texture->filter & ~(V9X_SIS3D_MAG_LINEAR |
                             V9X_SIS3D_MIN_MASK)) != 0ul ||
        (texture->filter & V9X_SIS3D_MIN_MASK) >
            V9X_SIS3D_MIN_LINEAR_MIP_LINEAR ||
        texture->colour_mode > V9X_SIS3D_TBLEND_COLOUR_MAX ||
        texture->alpha_mode > V9X_SIS3D_TBLEND_APIX_ATEX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    texel_bytes = v9x_sis3d_texel_bytes(texture->format);
    if (texel_bytes == 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    status = v9x_sis3d_texture_pitch_field(texture->pitch_bytes,
                                           &pitch_field);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    /* The row term is ORed into the column offset (measured 2026-10-05):
     * the pitch's lowest set bit must cover the row. */
    row_bytes = texel_bytes << texture->log2_width;
    if ((texture->pitch_bytes & (0ul - texture->pitch_bytes)) < row_bytes) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* Rows are at most 512 x 4 bytes and pitches at most 2^17, so the
     * level size stays below 2^26. */
    level_bytes = texture->pitch_bytes << texture->log2_height;
    if (texture->offset >= texture->vram_bytes ||
        level_bytes > texture->vram_bytes - texture->offset) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (level_bytes > V9X_SIS3D_ADDRESS_LIMIT - texture->offset) {
        return V9X_STATUS_UNSUPPORTED;
    }
    for (level = 1ul; level <= texture->levels; ++level) {
        level_bytes = v9x_sis3d_mip_pitch(texture, texel_bytes, level) <<
                      v9x_sis3d_mip_log2(texture->log2_height, level);
        offset = texture->level_offsets[level - 1ul];
        if (offset >= texture->vram_bytes ||
            level_bytes > texture->vram_bytes - offset) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        if (offset >= V9X_SIS3D_ADDRESS_LIMIT ||
            level_bytes > V9X_SIS3D_ADDRESS_LIMIT - offset) {
            return V9X_STATUS_UNSUPPORTED;
        }
    }

    v9x_sis3d_emit(writes, V9X_SIS3D_TEXTURE_SET,
                   (texture->format << V9X_SIS3D_TEXEL_SHIFT) |
                   (texture->mapping << V9X_SIS3D_MAPPING_SHIFT) |
                   (texture->blend_mask_bit << V9X_SIS3D_BLEND_MASK_SHIFT) |
                   (texture->levels << V9X_SIS3D_LEVELS_SHIFT) |
                   (texture->clear_cache ? V9X_SIS3D_CLEAR_CACHE : 0ul) |
                   texture->filter);
    v9x_sis3d_emit(writes, V9X_SIS3D_TEXTURE_BLEND,
                   (texture->colour_mode << V9X_SIS3D_TBLEND_COLOUR_SHIFT) |
                   (texture->alpha_mode << V9X_SIS3D_TBLEND_ALPHA_SHIFT));
    v9x_sis3d_emit(writes, V9X_SIS3D_TEXTURE_BASE0, texture->offset);
    /* Two levels a register: the even one in the high field. */
    v9x_sis3d_emit(writes, V9X_SIS3D_TEXTURE_PITCH01,
                   (pitch_field << V9X_SIS3D_PITCH_EVEN_SHIFT) |
                   v9x_sis3d_mip_pitch_field(texture, texel_bytes, 1ul));
    v9x_sis3d_emit(writes, V9X_SIS3D_TEXTURE_SIZE,
                   (texture->log2_width << V9X_SIS3D_LOG2_WIDTH_SHIFT) |
                   (texture->log2_height << V9X_SIS3D_LOG2_HEIGHT_SHIFT));
    for (level = 1ul; level <= texture->levels; ++level) {
        v9x_sis3d_emit(writes, V9X_SIS3D_TEXTURE_BASE0 + level * 4ul,
                       texture->level_offsets[level - 1ul]);
    }
    for (level = 2ul; level <= texture->levels; level += 2ul) {
        v9x_sis3d_emit(writes, V9X_SIS3D_TEXTURE_PITCH01 + level * 2ul,
                       (v9x_sis3d_mip_pitch_field(texture, texel_bytes,
                                                  level)
                        << V9X_SIS3D_PITCH_EVEN_SHIFT) |
                       v9x_sis3d_mip_pitch_field(texture, texel_bytes,
                                                 level + 1ul));
    }
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
