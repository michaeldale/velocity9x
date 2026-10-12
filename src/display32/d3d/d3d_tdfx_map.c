/*
 * The neutral draw description to VSA-100 3D builder state and vertices;
 * see d3d_tdfx_map.h. The measurements behind the units are in
 * docs\decisions\2026-10-12-voodoo4-3d-engine-draws-pixel-exact.md (Z in
 * depth counts, S and T in 256ths of the texture's width); the register
 * meanings behind each code are in docs\specifications\vsa100-registers.md
 * section 8.
 */
#include "d3d_tdfx_map.h"

/* Z16 holds depth counts 0-FFFFh; Direct3D's sz is 0..1 (TDFX3D Depth). */
#define V9X_D3D_TDFX_Z_SCALE      65535.0f
/* S and T count 256ths of the texture's width (TDFX3D Texture). */
#define V9X_D3D_TDFX_ST_SCALE     256.0f
#define V9X_D3D_TDFX_FLOAT_EXPONENT 0x7f800000ul

/* V9X_R3D_ABI_WRITE_RGB: red, green and blue all written. */
#define V9X_D3D_TDFX_WRITE_RGB    7ul
#define V9X_D3D_TDFX_TEXTURE_SIDE_MAX 256ul
#define V9X_D3D_TDFX_TEXEL_BYTES  2ul
#define V9X_D3D_TDFX_ALPHA_REF_MASK 0xfful

/* alphaMode factor codes beside the four tdfx_3d.h names: 2 is the other
 * surface's colour and 6 its inverse - destination colour as a source
 * factor, source colour as a destination one - and Fh saturate, source
 * only (section 8.23). */
#define V9X_D3D_TDFX_FACTOR_OTHER_COLOR     0x2ul
#define V9X_D3D_TDFX_FACTOR_INV_OTHER_COLOR 0x6ul
#define V9X_D3D_TDFX_FACTOR_SATURATE        0xful

static v9x_u32 v9x_d3d_tdfx_bits(float value)
{
    union {
        float value;
        v9x_u32 bits;
    } converted;

    converted.value = value;
    return converted.bits;
}

static int v9x_d3d_tdfx_finite(float value)
{
    return (v9x_d3d_tdfx_bits(value) & V9X_D3D_TDFX_FLOAT_EXPONENT) !=
           V9X_D3D_TDFX_FLOAT_EXPONENT;
}

/* A power of two up to 256, as its log2; -1 otherwise. */
static int v9x_d3d_tdfx_log2(v9x_u32 side)
{
    int log2 = 0;

    if (side == 0ul || side > V9X_D3D_TDFX_TEXTURE_SIDE_MAX ||
        (side & (side - 1ul)) != 0ul) {
        return -1;
    }
    while ((1ul << log2) != side) {
        ++log2;
    }
    return log2;
}

/* Direct3D's compare functions are the chip's codes plus one. */
static int v9x_d3d_tdfx_compare(v9x_u32 func, v9x_u32 *code)
{
    if (func < V9X_R3D_CMP_NEVER || func > V9X_R3D_CMP_ALWAYS) {
        return 0;
    }
    *code = func - V9X_R3D_CMP_NEVER;
    return 1;
}

/* The factors both sides take. Destination alpha is not among them: an
 * RGB565 target holds none. */
static int v9x_d3d_tdfx_common_factor(v9x_u32 factor, v9x_u32 *code)
{
    switch (factor) {
    case V9X_R3D_BLEND_ZERO:
        *code = V9X_TDFX_BLEND_ZERO;
        return 1;
    case V9X_R3D_BLEND_ONE:
        *code = V9X_TDFX_BLEND_ONE;
        return 1;
    case V9X_R3D_BLEND_SRCALPHA:
        *code = V9X_TDFX_BLEND_SRC_ALPHA;
        return 1;
    case V9X_R3D_BLEND_INVSRCALPHA:
        *code = V9X_TDFX_BLEND_INV_SRC_ALPHA;
        return 1;
    }
    return 0;
}

static int v9x_d3d_tdfx_source_factor(v9x_u32 factor, v9x_u32 *code)
{
    if (v9x_d3d_tdfx_common_factor(factor, code)) {
        return 1;
    }
    switch (factor) {
    case V9X_R3D_BLEND_DESTCOLOR:
        *code = V9X_D3D_TDFX_FACTOR_OTHER_COLOR;
        return 1;
    case V9X_R3D_BLEND_INVDESTCOLOR:
        *code = V9X_D3D_TDFX_FACTOR_INV_OTHER_COLOR;
        return 1;
    case V9X_R3D_BLEND_SRCALPHASAT:
        *code = V9X_D3D_TDFX_FACTOR_SATURATE;
        return 1;
    }
    return 0;
}

static int v9x_d3d_tdfx_destination_factor(v9x_u32 factor, v9x_u32 *code)
{
    if (v9x_d3d_tdfx_common_factor(factor, code)) {
        return 1;
    }
    switch (factor) {
    case V9X_R3D_BLEND_SRCCOLOR:
        *code = V9X_D3D_TDFX_FACTOR_OTHER_COLOR;
        return 1;
    case V9X_R3D_BLEND_INVSRCCOLOR:
        *code = V9X_D3D_TDFX_FACTOR_INV_OTHER_COLOR;
        return 1;
    }
    return 0;
}

/* Bilinear for this filter. A mip filter takes its within-level half and
 * the level the chip picks; LINEARMIPNEAREST and LINEARMIPLINEAR, which
 * blend two levels, are not built and take the nearest level. */
static int v9x_d3d_tdfx_filter(v9x_u32 filter, v9x_u32 *bilinear)
{
    switch (filter) {
    case V9X_R3D_FILTER_NEAREST:
    case V9X_R3D_FILTER_MIPNEAREST:
    case V9X_R3D_FILTER_LINEARMIPNEAREST:
        *bilinear = 0ul;
        return 1;
    case V9X_R3D_FILTER_LINEAR:
    case V9X_R3D_FILTER_MIPLINEAR:
    case V9X_R3D_FILTER_LINEARMIPLINEAR:
        *bilinear = 1ul;
        return 1;
    }
    return 0;
}

/*
 * Direct3D's DX5 texture blends the builder expresses. DECAL and COPY take
 * the texel's colour and alpha. MODULATE multiplies the colours and takes
 * the texel's alpha when it has one, the vertex's otherwise; an RGB565
 * texel's alpha is one, so multiplying the alphas gives the vertex's.
 * MODULATEALPHA multiplies both.
 */
static int v9x_d3d_tdfx_combine(v9x_u32 op, int has_alpha, v9x_u32 *combine)
{
    switch (op) {
    case V9X_R3D_TEXOP_DECAL:
    case V9X_R3D_TEXOP_COPY:
        *combine = V9X_TDFX_COMBINE_REPLACE;
        return 1;
    case V9X_R3D_TEXOP_MODULATE:
        *combine = has_alpha ? V9X_TDFX_COMBINE_MODULATE_TEXALPHA
                             : V9X_TDFX_COMBINE_MODULATE;
        return 1;
    case V9X_R3D_TEXOP_MODULATEALPHA:
        *combine = V9X_TDFX_COMBINE_MODULATE;
        return 1;
    }
    return 0;
}

static int v9x_d3d_tdfx_mip_filter(v9x_u32 filter)
{
    return filter == V9X_R3D_FILTER_MIPNEAREST ||
           filter == V9X_R3D_FILTER_MIPLINEAR ||
           filter == V9X_R3D_FILTER_LINEARMIPNEAREST ||
           filter == V9X_R3D_FILTER_LINEARMIPLINEAR;
}

static v9x_u32 v9x_d3d_tdfx_map_texture(const V9X_R3D_DRAW *draw,
                                        const V9X_D3D_TDFX_TEXTURE *resolved,
                                        v9x_u32 vram_base,
                                        struct v9x_tdfx_3d_state *state)
{
    const V9X_R3D_TEXTURE *sampler = &draw->texture;
    struct v9x_tdfx_3d_texture *texture = &state->texture;
    int log2;

    if (resolved == 0 || resolved->format == V9X_D3D_TDFX_TEXTURE_UNKNOWN) {
        return V9X_D3D_TDFX_REFUSE_TEXTURE_FORMAT;
    }
    /* Square, a power of two up to 256, rows packed, and any chain no
     * longer than the edge halves: the single-base layout the builder
     * writes (section 20 p.228). */
    log2 = v9x_d3d_tdfx_log2(resolved->width);
    if (log2 < 0 || resolved->height != resolved->width ||
        resolved->pitch_bytes != resolved->width * V9X_D3D_TDFX_TEXEL_BYTES ||
        resolved->levels > (v9x_u32)log2 + 1ul) {
        return V9X_D3D_TDFX_REFUSE_TEXTURE_SHAPE;
    }
    if (draw->color_key_enable != 0ul) {
        return V9X_D3D_TDFX_REFUSE_COLOR_KEY;
    }
    if (!v9x_d3d_tdfx_combine(sampler->op, resolved->has_alpha,
                              &texture->combine)) {
        return V9X_D3D_TDFX_REFUSE_TEXTURE_OP;
    }
    /* The cylindrical WRAPU/WRAPV states have no chip function. wrap_either
     * is the core's folded flag, set whatever the application asked, so it
     * is not read (d3d_sis6326_map.c found refusing on it refused every
     * textured draw). */
    if (sampler->wrap_u != 0ul || sampler->wrap_v != 0ul) {
        return V9X_D3D_TDFX_REFUSE_TEXTURE_ADDRESS;
    }
    switch (sampler->address) {
    case V9X_R3D_ADDRESS_WRAP:
        texture->clamp = 0ul;
        break;
    case V9X_R3D_ADDRESS_CLAMP:
        texture->clamp = 1ul;
        break;
    default:
        return V9X_D3D_TDFX_REFUSE_TEXTURE_ADDRESS;
    }
    if (!v9x_d3d_tdfx_filter(sampler->min_filter, &texture->min_bilinear) ||
        !v9x_d3d_tdfx_filter(sampler->mag_filter, &texture->mag_bilinear)) {
        return V9X_D3D_TDFX_REFUSE_TEXTURE_FILTER;
    }
    texture->offset = vram_base + resolved->offset;
    texture->format = resolved->format;
    texture->log2_size = (v9x_u32)log2;
    /* Minification chooses the levels; a plain filter reads level 0. */
    texture->levels = 1ul;
    if (v9x_d3d_tdfx_mip_filter(sampler->min_filter) &&
        resolved->levels > 1ul) {
        texture->levels = resolved->levels;
    }
    state->texture_enable = 1ul;
    return V9X_D3D_TDFX_REFUSE_NONE;
}

v9x_u32 v9x_d3d_tdfx_map_draw(const V9X_R3D_DRAW *draw,
                              const V9X_D3D_TDFX_TEXTURE *resolved,
                              v9x_u32 vram_base,
                              v9x_u32 vram_bytes,
                              v9x_u32 specular_rgb,
                              struct v9x_tdfx_3d_state *state)
{
    v9x_u32 code;
    unsigned char *bytes = (unsigned char *)state;
    v9x_u32 index;

    for (index = 0ul; index < sizeof(*state); ++index) {
        bytes[index] = 0u;
    }
    /*
     * An OpenGL draw through the render interface is honoured exactly or
     * refused (r3d.h, explicit_state). The builder clips to the whole
     * target and writes all of RGB, and samples surface textures only.
     */
    if (draw->explicit_state != 0ul &&
        (draw->write_mask != V9X_D3D_TDFX_WRITE_RGB ||
         draw->texture.levels != 0 ||
         draw->scissor_left != 0ul || draw->scissor_top != 0ul ||
         draw->scissor_right != draw->target.width ||
         draw->scissor_bottom != draw->target.height)) {
        return V9X_D3D_TDFX_REFUSE_EXPLICIT;
    }
    if (draw->target.format != V9X_R3D_FORMAT_RGB565) {
        return V9X_D3D_TDFX_REFUSE_TARGET;
    }
    if (draw->shade_mode != V9X_R3D_SHADE_FLAT &&
        draw->shade_mode != V9X_R3D_SHADE_GOURAUD) {
        return V9X_D3D_TDFX_REFUSE_SHADE;
    }
    /* Neither is built yet; SPECULARENABLE defaults on, so specular is
     * refused only where a vertex would add colour. */
    if (draw->fog_enable != 0ul) {
        return V9X_D3D_TDFX_REFUSE_FOG;
    }
    if (draw->specular_enable != 0ul && specular_rgb != 0ul) {
        return V9X_D3D_TDFX_REFUSE_SPECULAR;
    }

    state->vram_bytes = vram_base + vram_bytes;
    state->target.offset = vram_base + draw->target.offset;
    state->target.pitch_bytes = draw->target.pitch;
    state->width = draw->target.width;
    state->height = draw->target.height;

    /* Depth needs the state, a bound surface and a pitch. */
    if (draw->depth_enable != 0ul && draw->depth.object != 0 &&
        draw->depth.pitch != 0ul) {
        if (!v9x_d3d_tdfx_compare(draw->depth_func, &code)) {
            return V9X_D3D_TDFX_REFUSE_DEPTH_FUNC;
        }
        state->depth_enable = 1ul;
        state->depth.offset = vram_base + draw->depth.offset;
        state->depth.pitch_bytes = draw->depth.pitch;
        state->depth_func = code;
        state->depth_write = draw->depth_write != 0ul ? 1ul : 0ul;
    }
    if (draw->alpha_test_enable != 0ul) {
        if (!v9x_d3d_tdfx_compare(draw->alpha_func, &code)) {
            return V9X_D3D_TDFX_REFUSE_ALPHA_FUNC;
        }
        state->alpha_test = 1ul;
        state->alpha_func = code;
        state->alpha_ref = draw->alpha_ref & V9X_D3D_TDFX_ALPHA_REF_MASK;
    }
    if (draw->blend_enable != 0ul) {
        if (!v9x_d3d_tdfx_source_factor(draw->src_blend, &state->blend_src) ||
            !v9x_d3d_tdfx_destination_factor(draw->dst_blend,
                                             &state->blend_dst)) {
            return V9X_D3D_TDFX_REFUSE_BLEND;
        }
        state->blend_enable = 1ul;
    }
    if (draw->texture.object == 0) {
        return V9X_D3D_TDFX_REFUSE_NONE;
    }
    return v9x_d3d_tdfx_map_texture(draw, resolved, vram_base, state);
}

/* Z in depth counts, inside the buffer's range. */
static float v9x_d3d_tdfx_z(float sz)
{
    if (!(sz > 0.0f)) {
        return 0.0f;
    }
    if (sz >= 1.0f) {
        return V9X_D3D_TDFX_Z_SCALE;
    }
    return sz * V9X_D3D_TDFX_Z_SCALE;
}

int v9x_d3d_tdfx_triangle(const V9X_R3D_VERTEX *triangle,
                          v9x_u32 shade_mode,
                          int textured,
                          struct v9x_tdfx_3d_vertex *out)
{
    const V9X_R3D_VERTEX *colour;
    float area;
    v9x_u32 index;
    int flat = shade_mode == V9X_R3D_SHADE_FLAT;

    for (index = 0ul; index < 3ul; ++index) {
        if (!v9x_d3d_tdfx_finite(triangle[index].sx) ||
            !v9x_d3d_tdfx_finite(triangle[index].sy) ||
            !v9x_d3d_tdfx_finite(triangle[index].sz)) {
            return 0;
        }
        if (textured &&
            (!v9x_d3d_tdfx_finite(triangle[index].rhw) ||
             !(triangle[index].rhw > 0.0f) ||
             !v9x_d3d_tdfx_finite(triangle[index].tu) ||
             !v9x_d3d_tdfx_finite(triangle[index].tv))) {
            return 0;
        }
    }
    area = (triangle[1].sx - triangle[0].sx) *
               (triangle[2].sy - triangle[0].sy) -
           (triangle[2].sx - triangle[0].sx) *
               (triangle[1].sy - triangle[0].sy);
    if (!(area != 0.0f)) {
        return 0;
    }

    for (index = 0ul; index < 3ul; ++index) {
        const V9X_R3D_VERTEX *v = &triangle[index];

        /* Flat shading is Direct3D's: vertex 0's colour throughout. */
        colour = flat ? &triangle[0] : v;
        out[index].x = v->sx;
        out[index].y = v->sy;
        out[index].z = v9x_d3d_tdfx_z(v->sz);
        out[index].r = (float)((colour->color >> 16) & 0xfful);
        out[index].g = (float)((colour->color >> 8) & 0xfful);
        out[index].b = (float)(colour->color & 0xfful);
        out[index].a = (float)((colour->color >> 24) & 0xfful);
        if (textured) {
            out[index].oow = v->rhw;
            out[index].sow = v->tu * V9X_D3D_TDFX_ST_SCALE * v->rhw;
            out[index].tow = v->tv * V9X_D3D_TDFX_ST_SCALE * v->rhw;
        } else {
            out[index].oow = 1.0f;
            out[index].sow = 0.0f;
            out[index].tow = 0.0f;
        }
    }
    return 1;
}
