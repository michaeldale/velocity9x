/*
 * The neutral draw description to Evergreen batch state; see d3d_eg_map.h.
 * The register words are eg_3d.c's; the factor, function and filter codes
 * are evergreend.h's.
 */
#include "d3d_eg_map.h"
#include "velocity9x/eg_shader.h"

#define V9X_D3D_EG_ALPHA_REF_MASK 0xfful
/* A texture side the sampler takes; its field is 14 bits, and 2048 is what
 * the caps publish. */
#define V9X_D3D_EG_TEXTURE_SIDE_MAX 2048ul

/* Direct3D's compare functions, NEVER to ALWAYS, are the chip's ZFUNC and
 * ALPHA_FUNC codes plus one. */
static int v9x_d3d_eg_compare(v9x_u32 func, v9x_u32 *code)
{
    if (func < V9X_R3D_CMP_NEVER || func > V9X_R3D_CMP_ALWAYS) {
        return 0;
    }
    *code = func - V9X_R3D_CMP_NEVER;
    return 1;
}

/*
 * Direct3D's blend factors to the CB's. Destination alpha is refused:
 * neither target holds alpha the application put there (RGB565 has none,
 * and an XRGB8888 target's top byte is whatever was last written).
 * SRCALPHASAT is a source factor only.
 */
static int v9x_d3d_eg_factor(v9x_u32 factor, int source, v9x_u32 *code)
{
    switch (factor) {
    case V9X_R3D_BLEND_ZERO:
        *code = V9X_EG_BLEND_ZERO;
        return 1;
    case V9X_R3D_BLEND_ONE:
        *code = V9X_EG_BLEND_ONE;
        return 1;
    case V9X_R3D_BLEND_SRCCOLOR:
        *code = V9X_EG_BLEND_SRC_COLOR;
        return 1;
    case V9X_R3D_BLEND_INVSRCCOLOR:
        *code = V9X_EG_BLEND_ONE_MINUS_SRC_COLOR;
        return 1;
    case V9X_R3D_BLEND_SRCALPHA:
        *code = V9X_EG_BLEND_SRC_ALPHA;
        return 1;
    case V9X_R3D_BLEND_INVSRCALPHA:
        *code = V9X_EG_BLEND_ONE_MINUS_SRC_ALPHA;
        return 1;
    case V9X_R3D_BLEND_DESTCOLOR:
        *code = V9X_EG_BLEND_DST_COLOR;
        return 1;
    case V9X_R3D_BLEND_INVDESTCOLOR:
        *code = V9X_EG_BLEND_ONE_MINUS_DST_COLOR;
        return 1;
    case V9X_R3D_BLEND_SRCALPHASAT:
        if (source) {
            *code = V9X_EG_BLEND_SRC_ALPHA_SATURATE;
            return 1;
        }
        return 0;
    }
    return 0;
}

/*
 * Direct3D's filters as the filter within a level and the filter between
 * levels: MIPNEAREST point and nearest level, MIPLINEAR bilinear and
 * nearest level, LINEARMIPNEAREST point between two levels,
 * LINEARMIPLINEAR trilinear (the r128 engine reads them the same way, after
 * Mesa). A magnification filter is NEAREST or LINEAR, with no mip filter.
 */
static int v9x_d3d_eg_filter(v9x_u32 filter, int magnify, v9x_u32 *bilinear,
                             v9x_u32 *mip)
{
    *mip = V9X_EG_MIP_NONE;
    switch (filter) {
    case V9X_R3D_FILTER_NEAREST:
        *bilinear = 0ul;
        return 1;
    case V9X_R3D_FILTER_LINEAR:
        *bilinear = 1ul;
        return 1;
    case V9X_R3D_FILTER_MIPNEAREST:
        *bilinear = 0ul;
        *mip = V9X_EG_MIP_POINT;
        return !magnify;
    case V9X_R3D_FILTER_MIPLINEAR:
        *bilinear = 1ul;
        *mip = V9X_EG_MIP_POINT;
        return !magnify;
    case V9X_R3D_FILTER_LINEARMIPNEAREST:
        *bilinear = 0ul;
        *mip = V9X_EG_MIP_LINEAR;
        return !magnify;
    case V9X_R3D_FILTER_LINEARMIPLINEAR:
        *bilinear = 1ul;
        *mip = V9X_EG_MIP_LINEAR;
        return !magnify;
    }
    return 0;
}

static int v9x_d3d_eg_address(v9x_u32 address, v9x_u32 *clamp)
{
    switch (address) {
    case V9X_R3D_ADDRESS_WRAP:
        *clamp = V9X_EG_TEX_WRAP;
        return 1;
    case V9X_R3D_ADDRESS_MIRROR:
        *clamp = V9X_EG_TEX_MIRROR;
        return 1;
    case V9X_R3D_ADDRESS_CLAMP:
        *clamp = V9X_EG_TEX_CLAMP_LAST_TEXEL;
        return 1;
    }
    return 0;
}

/*
 * Direct3D's DX5 texture blends to a pixel shader. DECAL and COPY take the
 * texel's colour and alpha (REPLACE). MODULATEALPHA multiplies colour and
 * alpha (MODULATE). MODULATE takes the texel's alpha when the texture has
 * one and the vertex's otherwise: without alpha the sampler reads alpha as
 * 1 and the product is the vertex's; with it, the product differs from
 * Direct3D only where the vertex's alpha is below 1. DECALALPHA, ADD and
 * the mask forms are not built.
 */
static int v9x_d3d_eg_op(v9x_u32 op, v9x_u32 *shader_op)
{
    switch (op) {
    case V9X_R3D_TEXOP_DECAL:
    case V9X_R3D_TEXOP_COPY:
        *shader_op = V9X_EG_D3D_OP_REPLACE;
        return 1;
    case V9X_R3D_TEXOP_MODULATE:
    case V9X_R3D_TEXOP_MODULATEALPHA:
        *shader_op = V9X_EG_D3D_OP_MODULATE;
        return 1;
    }
    return 0;
}

/* A float's bits for ref / 255, the SX's alpha reference. */
static v9x_u32 v9x_d3d_eg_alpha_ref(v9x_u32 ref)
{
    union {
        float value;
        v9x_u32 bits;
    } converted;

    converted.value = (float)(long)(ref & V9X_D3D_EG_ALPHA_REF_MASK) / 255.0f;
    return converted.bits;
}

static v9x_u32 v9x_d3d_eg_map_texture(const V9X_R3D_DRAW *draw,
                                      const V9X_D3D_EG_TEXTURE *texture,
                                      v9x_u32 heap_gpu,
                                      struct v9x_eg_3d_state *state,
                                      v9x_u32 *ps_slot)
{
    const V9X_R3D_TEXTURE *sampler = &draw->texture;
    v9x_u32 shader_op;
    v9x_u32 mag;
    v9x_u32 min;
    v9x_u32 mip;
    v9x_u32 clamp;

    if (texture == 0 || texture->format == V9X_D3D_EG_TEXTURE_UNKNOWN) {
        return V9X_D3D_EG_REFUSE_TEXTURE_FORMAT;
    }
    if (texture->width == 0ul || texture->height == 0ul ||
        texture->width > V9X_D3D_EG_TEXTURE_SIDE_MAX ||
        texture->height > V9X_D3D_EG_TEXTURE_SIDE_MAX ||
        texture->pitch_texels < texture->width ||
        v9x_eg_3d_texture_pitch(texture->format, texture->pitch_texels) !=
            texture->pitch_texels ||
        ((heap_gpu + texture->offset) &
         (V9X_D3D_EG_SURFACE_ALIGN - 1ul)) != 0ul) {
        return V9X_D3D_EG_REFUSE_TEXTURE_SHAPE;
    }
    if (!v9x_d3d_eg_op(sampler->op, &shader_op)) {
        return V9X_D3D_EG_REFUSE_TEXTURE_OP;
    }
    /* The cylindrical WRAPU/WRAPV states have no sampler function. */
    if (sampler->wrap_u != 0ul || sampler->wrap_v != 0ul ||
        !v9x_d3d_eg_address(sampler->address, &clamp)) {
        return V9X_D3D_EG_REFUSE_TEXTURE_ADDRESS;
    }
    if (!v9x_d3d_eg_filter(sampler->mag_filter, 1, &mag, &mip) ||
        !v9x_d3d_eg_filter(sampler->min_filter, 0, &min, &mip)) {
        return V9X_D3D_EG_REFUSE_TEXTURE_FILTER;
    }
    /* A chain is sampled across its levels under a mip filter; one level,
     * or a plain filter, reads level 0 alone. */
    if (texture->levels > 1ul && mip != V9X_EG_MIP_NONE) {
        state->texture_levels = texture->levels;
        state->texture_mip_gpu = heap_gpu + texture->mip_offset;
        state->mip_filter = mip;
    } else {
        state->texture_levels = 1ul;
        state->mip_filter = V9X_EG_MIP_NONE;
    }
    state->has_texture = 1ul;
    state->texture_gpu = heap_gpu + texture->offset;
    state->texture_width = texture->width;
    state->texture_height = texture->height;
    state->texture_pitch = texture->pitch_texels;
    state->texture_format = texture->format;
    state->sampler_word0 = v9x_eg_3d_sampler(mag, min, clamp, clamp);
    state->vs_gprs = 3ul;
    state->ps_gprs = 2ul;
    state->interpolants = 2ul;
    *ps_slot = V9X_EG_SLOT_PS_FIRST + shader_op;
    return V9X_D3D_EG_REFUSE_NONE;
}

v9x_u32 v9x_d3d_eg_map_draw(const V9X_R3D_DRAW *draw,
                            const V9X_D3D_EG_TEXTURE *texture,
                            v9x_u32 heap_gpu,
                            v9x_u32 depth_bytes,
                            struct v9x_eg_3d_state *state,
                            v9x_u32 *vs_slot,
                            v9x_u32 *ps_slot)
{
    unsigned char *bytes = (unsigned char *)state;
    v9x_u32 bytes_per_pixel;
    v9x_u32 index;
    v9x_u32 code;
    v9x_u32 src;
    v9x_u32 dst;

    for (index = 0ul; index < sizeof(*state); ++index) {
        bytes[index] = 0u;
    }
    /* The render interface's explicit draws (OpenGL, Glide) carry a
     * scissor, a write mask and CPU levels this engine does not take yet;
     * its CPU fallback draws them. */
    if (draw->explicit_state != 0ul || draw->texture.levels != 0 ||
        draw->texcoords1 != 0 || draw->texture1.object != 0) {
        return V9X_D3D_EG_REFUSE_EXPLICIT;
    }

    /* The target: RGB565 or XRGB8888, linear general, its pitch a whole
     * number of 8-pixel tiles and its base on 256 bytes. */
    if (draw->target.format == V9X_R3D_FORMAT_RGB565) {
        bytes_per_pixel = 2ul;
        state->target_format = V9X_EG_COLOR_5_6_5;
    } else if (draw->target.format == V9X_R3D_FORMAT_XRGB8888) {
        /* The X byte is not the application's to have drawn: alpha
         * writes off, as Gen3's (V9XD32 at 32 bpp). */
        bytes_per_pixel = 4ul;
        state->target_format = V9X_EG_COLOR_8_8_8_8;
    } else {
        return V9X_D3D_EG_REFUSE_TARGET;
    }
    if (draw->target.width == 0ul || draw->target.height == 0ul ||
        (draw->target.pitch % (8ul * bytes_per_pixel)) != 0ul ||
        draw->target.pitch / bytes_per_pixel < draw->target.width ||
        ((heap_gpu + draw->target.offset) &
         (V9X_D3D_EG_SURFACE_ALIGN - 1ul)) != 0ul) {
        return V9X_D3D_EG_REFUSE_TARGET;
    }
    state->target_gpu = heap_gpu + draw->target.offset;
    state->target_pitch = draw->target.pitch / bytes_per_pixel;
    state->target_height = draw->target.height;
    state->scissor_right = draw->target.width;
    state->scissor_bottom = draw->target.height;

    /* Depth, when enabled and bound: Z16 as d3d_eg.c places it, rows padded
     * to the tile. Disabled, the DB has no buffer and neither tests nor
     * writes, which is Direct3D's ZENABLE FALSE. */
    if (draw->depth_enable != 0ul && draw->depth.object != 0 &&
        draw->depth.pitch != 0ul) {
        if (depth_bytes != 2ul ||
            (draw->depth.pitch % V9X_D3D_EG_DEPTH_PITCH_UNIT) != 0ul ||
            draw->depth.pitch / 2ul < draw->target.width ||
            draw->depth.height < draw->target.height ||
            ((heap_gpu + draw->depth.offset) &
             (V9X_D3D_EG_SURFACE_ALIGN - 1ul)) != 0ul) {
            return V9X_D3D_EG_REFUSE_DEPTH_SURFACE;
        }
        if (!v9x_d3d_eg_compare(draw->depth_func, &code)) {
            return V9X_D3D_EG_REFUSE_DEPTH_FUNC;
        }
        state->has_depth = 1ul;
        state->depth_gpu = heap_gpu + draw->depth.offset;
        state->depth_pitch = draw->depth.pitch / 2ul;
        state->depth_height =
            (draw->depth.height + V9X_D3D_EG_DEPTH_TILE_ROWS - 1ul) &
            ~(V9X_D3D_EG_DEPTH_TILE_ROWS - 1ul);
        state->depth_control =
            v9x_eg_3d_depth_control(1ul, draw->depth_write, code);
    }

    if (draw->blend_enable != 0ul) {
        if (!v9x_d3d_eg_factor(draw->src_blend, 1, &src) ||
            !v9x_d3d_eg_factor(draw->dst_blend, 0, &dst)) {
            return V9X_D3D_EG_REFUSE_BLEND;
        }
        state->blend_control = v9x_eg_3d_blend(1ul, src, dst);
    } else {
        state->blend_control = v9x_eg_3d_blend(0ul, 0ul, 0ul);
    }
    if (draw->alpha_test_enable != 0ul) {
        if (!v9x_d3d_eg_compare(draw->alpha_func, &code)) {
            return V9X_D3D_EG_REFUSE_ALPHA_FUNC;
        }
        state->alpha_test_control = v9x_eg_3d_alpha_test(1ul, code);
        state->alpha_ref = v9x_d3d_eg_alpha_ref(draw->alpha_ref);
    } else {
        state->alpha_test_control =
            v9x_eg_3d_alpha_test(0ul, V9X_EG_FUNC_ALWAYS);
    }
    /* A colour key needs a kill in the pixel shader, not built yet. */
    if (draw->color_key_enable != 0ul && draw->texture.object != 0) {
        return V9X_D3D_EG_REFUSE_COLOR_KEY;
    }

    /* rhw in w, perspective-correct; pixel centres at whole coordinates,
     * Direct3D's. */
    state->perspective = 1ul;
    state->pixel_centre_half = 0ul;
    if (draw->texture.object == 0) {
        state->vs_gprs = 2ul;
        state->ps_gprs = 1ul;
        state->interpolants = 1ul;
        *vs_slot = V9X_EG_SLOT_VS;
        *ps_slot = V9X_EG_SLOT_PS_FIRST + V9X_EG_D3D_OP_NONE;
        return V9X_D3D_EG_REFUSE_NONE;
    }
    *vs_slot = V9X_EG_SLOT_VS_TEXTURED;
    return v9x_d3d_eg_map_texture(draw, texture, heap_gpu, state, ps_slot);
}
