/*
 * The neutral draw description to Rage 128 3D builder state; see
 * d3d_r128_map.h. The register meaning behind each code is in r128_3d.h,
 * from xf86-video-r128's r128_reg.h; the filter and combine choices follow
 * Mesa 7.11's r128_tex.c and r128_texstate.c.
 */
#include "d3d_r128_map.h"

/* 1024 texels a side: the builder's limit (TEX_SIZE_PITCH_C's fields). */
#define V9X_D3D_R128_TEXTURE_SIDE_MAX 1024ul
#define V9X_D3D_R128_ALPHA_REF_MASK   0xfful

/* The render interface's write-mask bits (r3d_abi.h V9X_R3D_ABI_WRITE_*). */
#define V9X_D3D_R128_WRITE_RED   1ul
#define V9X_D3D_R128_WRITE_GREEN 2ul
#define V9X_D3D_R128_WRITE_BLUE  4ul

/* An explicit draw's scissor and write mask, as the builder's. The scissor
 * must be a non-empty rectangle inside the target; one that is not goes to
 * the fallback rather than be clamped. */
static int v9x_d3d_r128_explicit(const V9X_R3D_DRAW *draw,
                                 struct v9x_r128_3d_state *state)
{
    if (draw->scissor_left >= draw->scissor_right ||
        draw->scissor_top >= draw->scissor_bottom ||
        draw->scissor_right > draw->target.width ||
        draw->scissor_bottom > draw->target.height) {
        return 0;
    }
    state->scissor_enable = 1ul;
    state->scissor_left = draw->scissor_left;
    state->scissor_top = draw->scissor_top;
    state->scissor_right = draw->scissor_right;
    state->scissor_bottom = draw->scissor_bottom;
    if ((draw->write_mask & V9X_D3D_R128_WRITE_RED) == 0ul) {
        state->masked_channels |= V9X_R128_CHANNEL_RED;
    }
    if ((draw->write_mask & V9X_D3D_R128_WRITE_GREEN) == 0ul) {
        state->masked_channels |= V9X_R128_CHANNEL_GREEN;
    }
    if ((draw->write_mask & V9X_D3D_R128_WRITE_BLUE) == 0ul) {
        state->masked_channels |= V9X_R128_CHANNEL_BLUE;
    }
    return 1;
}

/* A power of two up to 1024, as its log2; -1 otherwise. */
static int v9x_d3d_r128_log2(v9x_u32 side)
{
    int log2 = 0;

    if (side == 0ul || side > V9X_D3D_R128_TEXTURE_SIDE_MAX ||
        (side & (side - 1ul)) != 0ul) {
        return -1;
    }
    while ((1ul << log2) != side) {
        ++log2;
    }
    return log2;
}

/* Direct3D's compare functions, NEVER to ALWAYS, to the chip's codes
 * (Z_STEN_CNTL_C 6:4 and the alpha test share them). */
static int v9x_d3d_r128_compare(v9x_u32 func, v9x_u32 *code)
{
    static const v9x_u32 codes[8] = {
        V9X_R128_CMP_NEVER, V9X_R128_CMP_LESS, V9X_R128_CMP_EQUAL,
        V9X_R128_CMP_LESSEQUAL, V9X_R128_CMP_GREATER, V9X_R128_CMP_NOTEQUAL,
        V9X_R128_CMP_GREATEREQUAL, V9X_R128_CMP_ALWAYS
    };

    if (func < V9X_R3D_CMP_NEVER || func > V9X_R3D_CMP_ALWAYS) {
        return 0;
    }
    *code = codes[func - V9X_R3D_CMP_NEVER];
    return 1;
}

/*
 * Direct3D's blend factors to the chip's. Destination alpha is refused:
 * neither target this engine draws holds any (RGB565, XRGB8888).
 * SRCALPHASAT is a source factor only.
 */
static int v9x_d3d_r128_factor(v9x_u32 factor, int source, v9x_u32 *code)
{
    switch (factor) {
    case V9X_R3D_BLEND_ZERO:
        *code = V9X_R128_BLEND_ZERO;
        return 1;
    case V9X_R3D_BLEND_ONE:
        *code = V9X_R128_BLEND_ONE;
        return 1;
    case V9X_R3D_BLEND_SRCCOLOR:
        *code = V9X_R128_BLEND_SRCCOLOR;
        return 1;
    case V9X_R3D_BLEND_INVSRCCOLOR:
        *code = V9X_R128_BLEND_INVSRCCOLOR;
        return 1;
    case V9X_R3D_BLEND_SRCALPHA:
        *code = V9X_R128_BLEND_SRCALPHA;
        return 1;
    case V9X_R3D_BLEND_INVSRCALPHA:
        *code = V9X_R128_BLEND_INVSRCALPHA;
        return 1;
    case V9X_R3D_BLEND_DESTCOLOR:
        *code = V9X_R128_BLEND_DSTCOLOR;
        return 1;
    case V9X_R3D_BLEND_INVDESTCOLOR:
        *code = V9X_R128_BLEND_INVDSTCOLOR;
        return 1;
    case V9X_R3D_BLEND_SRCALPHASAT:
        if (source) {
            *code = V9X_R128_BLEND_SAT;
            return 1;
        }
        return 0;
    }
    return 0;
}

/* Direct3D's filters name the chip's six, in the same sense: MIPLINEAR is
 * bilinear within the nearest level, LINEARMIPNEAREST point-sampled
 * between two (Mesa maps GL_LINEAR_MIPMAP_NEAREST and
 * GL_NEAREST_MIPMAP_LINEAR to them). */
static int v9x_d3d_r128_filter(v9x_u32 filter, v9x_u32 *code)
{
    switch (filter) {
    case V9X_R3D_FILTER_NEAREST:
        *code = V9X_R128_FILTER_NEAREST;
        return 1;
    case V9X_R3D_FILTER_LINEAR:
        *code = V9X_R128_FILTER_LINEAR;
        return 1;
    case V9X_R3D_FILTER_MIPNEAREST:
        *code = V9X_R128_FILTER_MIPNEAREST;
        return 1;
    case V9X_R3D_FILTER_MIPLINEAR:
        *code = V9X_R128_FILTER_MIPLINEAR;
        return 1;
    case V9X_R3D_FILTER_LINEARMIPNEAREST:
        *code = V9X_R128_FILTER_LINEARMIPNEAREST;
        return 1;
    case V9X_R3D_FILTER_LINEARMIPLINEAR:
        *code = V9X_R128_FILTER_LINEARMIPLINEAR;
        return 1;
    }
    return 0;
}

/*
 * Direct3D's DX5 texture blends. DECAL and COPY take the texel's colour
 * and alpha. MODULATE multiplies the colours and takes the texel's alpha
 * when it has one, the vertex's otherwise. MODULATEALPHA multiplies both.
 * DECALALPHA blends the vertex colour to the texel's by the texel's alpha
 * and keeps the vertex's alpha. ADD and the mask forms are not built.
 */
static int v9x_d3d_r128_combine(v9x_u32 op, int has_alpha, v9x_u32 *combine)
{
    switch (op) {
    case V9X_R3D_TEXOP_DECAL:
    case V9X_R3D_TEXOP_COPY:
        *combine = V9X_R128_COMBINE_REPLACE;
        return 1;
    case V9X_R3D_TEXOP_MODULATE:
        *combine = has_alpha ? V9X_R128_COMBINE_MODULATE_TEXALPHA
                             : V9X_R128_COMBINE_MODULATE_RGB;
        return 1;
    case V9X_R3D_TEXOP_MODULATEALPHA:
        *combine = V9X_R128_COMBINE_MODULATE;
        return 1;
    case V9X_R3D_TEXOP_DECALALPHA:
        *combine = V9X_R128_COMBINE_DECAL_ALPHA;
        return 1;
    }
    return 0;
}

static int v9x_d3d_r128_mip_filter(v9x_u32 code)
{
    return code >= V9X_R128_FILTER_MIPNEAREST;
}

static v9x_u32 v9x_d3d_r128_map_texture(const V9X_R3D_DRAW *draw,
                                        const V9X_D3D_R128_TEXTURE *resolved,
                                        v9x_u32 vram_base,
                                        struct v9x_r128_3d_state *state)
{
    const V9X_R3D_TEXTURE *sampler = &draw->texture;
    struct v9x_r128_3d_texture *texture = &state->texture;
    int log2_width;
    int log2_height;
    v9x_u32 index;

    if (resolved == 0 || resolved->datatype == V9X_D3D_R128_TEXTURE_UNKNOWN) {
        return V9X_D3D_R128_REFUSE_TEXTURE_FORMAT;
    }
    /* Powers of two, rows packed: TEX_SIZE_PITCH_C gives the pitch as the
     * width's log2. */
    log2_width = v9x_d3d_r128_log2(resolved->width);
    log2_height = v9x_d3d_r128_log2(resolved->height);
    if (log2_width < 0 || log2_height < 0 ||
        resolved->pitch_bytes !=
            resolved->width * resolved->bytes_per_texel ||
        resolved->levels == 0ul ||
        resolved->levels > V9X_R128_TEXTURE_LEVELS) {
        return V9X_D3D_R128_REFUSE_TEXTURE_SHAPE;
    }
    if (draw->color_key_enable != 0ul) {
        return V9X_D3D_R128_REFUSE_COLOR_KEY;
    }
    if (!v9x_d3d_r128_combine(sampler->op, resolved->has_alpha,
                              &texture->combine)) {
        return V9X_D3D_R128_REFUSE_TEXTURE_OP;
    }
    /* The cylindrical WRAPU/WRAPV states have no chip function. */
    if (sampler->wrap_u != 0ul || sampler->wrap_v != 0ul) {
        return V9X_D3D_R128_REFUSE_TEXTURE_ADDRESS;
    }
    switch (sampler->address) {
    case V9X_R3D_ADDRESS_WRAP:
        texture->clamp_s = 0ul;
        texture->clamp_t = 0ul;
        break;
    case V9X_R3D_ADDRESS_CLAMP:
        texture->clamp_s = 1ul;
        texture->clamp_t = 1ul;
        break;
    default:
        return V9X_D3D_R128_REFUSE_TEXTURE_ADDRESS;
    }
    if (!v9x_d3d_r128_filter(sampler->min_filter, &texture->min_filter) ||
        !v9x_d3d_r128_filter(sampler->mag_filter, &texture->mag_filter) ||
        texture->mag_filter > V9X_R128_FILTER_LINEAR) {
        return V9X_D3D_R128_REFUSE_TEXTURE_FILTER;
    }

    texture->enable = 1ul;
    texture->datatype = resolved->datatype;
    texture->width_log2 = (v9x_u32)log2_width;
    texture->height_log2 = (v9x_u32)log2_height;
    /* A mip filter reads the chain; with none it falls back to the filter
     * within level 0, which is what the sampler does with the chain
     * disabled. A plain filter reads level 0 whatever is attached. */
    if (v9x_d3d_r128_mip_filter(texture->min_filter)) {
        if (resolved->levels > 1ul) {
            texture->levels = resolved->levels;
        } else {
            texture->levels = 1ul;
            texture->min_filter =
                texture->min_filter == V9X_R128_FILTER_MIPNEAREST ||
                texture->min_filter == V9X_R128_FILTER_LINEARMIPNEAREST ?
                V9X_R128_FILTER_NEAREST : V9X_R128_FILTER_LINEAR;
        }
    } else {
        texture->levels = 1ul;
    }
    for (index = 0ul; index < texture->levels; ++index) {
        texture->offsets[index] = vram_base + resolved->offsets[index];
    }
    return V9X_D3D_R128_REFUSE_NONE;
}

v9x_u32 v9x_d3d_r128_map_draw(const V9X_R3D_DRAW *draw,
                              const V9X_D3D_R128_TEXTURE *resolved,
                              v9x_u32 vram_base,
                              v9x_u32 vram_bytes,
                              v9x_u32 depth_bytes,
                              struct v9x_r128_3d_state *state)
{
    v9x_u32 code;
    unsigned char *bytes = (unsigned char *)state;
    v9x_u32 index;

    for (index = 0ul; index < sizeof(*state); ++index) {
        bytes[index] = 0u;
    }
    /* The render interface's explicit draws (OpenGL, Glide) are drawn here
     * with their scissor and write mask, undithered as every draw of this
     * engine is. CPU-resident levels and a second unit are not sampled:
     * the software fallback draws those (d3d_core.c, measured to agree in
     * a mixed frame on A8U4I5). */
    if (draw->texture.levels != 0 || draw->texcoords1 != 0 ||
        draw->texture1.object != 0 || draw->texture1.levels != 0) {
        return V9X_D3D_R128_REFUSE_EXPLICIT;
    }
    if (draw->explicit_state != 0ul &&
        !v9x_d3d_r128_explicit(draw, state)) {
        return V9X_D3D_R128_REFUSE_EXPLICIT;
    }
    /* Every other draw is scissored to its target: the core no longer
     * clips for this engine, so a triangle reaching off the target is
     * drawn by the setup engine inside its guard band, and the scissor is
     * what keeps it off the memory past the target's edges. */
    if (draw->explicit_state == 0ul) {
        state->scissor_enable = 1ul;
        state->scissor_left = 0ul;
        state->scissor_top = 0ul;
        state->scissor_right = draw->target.width;
        state->scissor_bottom = draw->target.height;
    }
    switch (draw->target.format) {
    case V9X_R3D_FORMAT_RGB565:
        state->target_bytes_per_pixel = 2ul;
        break;
    case V9X_R3D_FORMAT_XRGB8888:
        state->target_bytes_per_pixel = 4ul;
        break;
    default:
        return V9X_D3D_R128_REFUSE_TARGET;
    }
    if (draw->shade_mode != V9X_R3D_SHADE_FLAT &&
        draw->shade_mode != V9X_R3D_SHADE_GOURAUD) {
        return V9X_D3D_R128_REFUSE_SHADE;
    }

    state->vram_bytes = vram_base + vram_bytes;
    state->target_offset = vram_base + draw->target.offset;
    state->target_pitch_bytes = draw->target.pitch;
    state->flat_shade = draw->shade_mode == V9X_R3D_SHADE_FLAT ? 1ul : 0ul;
    /* Vertex fog through the specular dword's alpha, the specular colour
     * added: both the chip's own, from the vertex as it comes. */
    state->fog_enable = draw->fog_enable != 0ul ? 1ul : 0ul;
    state->fog_color = draw->fog_color & 0x00fffffful;
    state->specular_enable = draw->specular_enable != 0ul ? 1ul : 0ul;
    state->src_blend = V9X_R128_BLEND_ONE;
    state->dst_blend = V9X_R128_BLEND_ZERO;
    state->alpha_func = V9X_R128_CMP_ALWAYS;

    /* Depth needs the state, a bound Z16 surface and a pitch. */
    if (draw->depth_enable != 0ul && draw->depth.object != 0 &&
        draw->depth.pitch != 0ul) {
        if (depth_bytes != 2ul) {
            return V9X_D3D_R128_REFUSE_DEPTH_SURFACE;
        }
        if (!v9x_d3d_r128_compare(draw->depth_func, &code)) {
            return V9X_D3D_R128_REFUSE_DEPTH_FUNC;
        }
        state->depth_enable = 1ul;
        state->depth_write = draw->depth_write != 0ul ? 1ul : 0ul;
        state->depth_func = code;
        state->depth_offset = vram_base + draw->depth.offset;
        state->depth_pitch_bytes = draw->depth.pitch;
        state->depth_bytes_per_pixel = 2ul;
    }
    if (draw->alpha_test_enable != 0ul) {
        if (!v9x_d3d_r128_compare(draw->alpha_func, &code)) {
            return V9X_D3D_R128_REFUSE_ALPHA_FUNC;
        }
        state->alpha_test_enable = 1ul;
        state->alpha_func = code;
        state->alpha_ref = draw->alpha_ref & V9X_D3D_R128_ALPHA_REF_MASK;
    }
    if (draw->blend_enable != 0ul) {
        if (!v9x_d3d_r128_factor(draw->src_blend, 1, &state->src_blend) ||
            !v9x_d3d_r128_factor(draw->dst_blend, 0, &state->dst_blend)) {
            return V9X_D3D_R128_REFUSE_BLEND;
        }
        state->blend_enable = 1ul;
    }
    if (draw->texture.object == 0) {
        return V9X_D3D_R128_REFUSE_NONE;
    }
    return v9x_d3d_r128_map_texture(draw, resolved, vram_base, state);
}
