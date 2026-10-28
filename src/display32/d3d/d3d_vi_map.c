/*
 * The neutral draw description to Carrizo batch state; see d3d_vi_map.h.
 */
#include "d3d_vi_map.h"
#include "velocity9x/vi_shader.h"

/* The target's bytes per pixel and CB format; 0 for a format not drawn. */
static v9x_u32 v9x_d3d_vi_target(v9x_u32 format, v9x_u32 *cb_format,
                                 v9x_u32 *mask)
{
    if (format == V9X_R3D_FORMAT_RGB565) {
        *cb_format = V9X_VI_CB_FORMAT_5_6_5;
        *mask = 0xful;
        return 2ul;
    }
    if (format == V9X_R3D_FORMAT_XRGB8888) {
        /* The X byte is not the application's to have drawn: alpha
         * writes off, as Cedar's and Gen3's. */
        *cb_format = V9X_VI_CB_FORMAT_8_8_8_8;
        *mask = 0x7ul;
        return 4ul;
    }
    return 0ul;
}

/* A surface at offset from the heap, as a GPU address's low half, on 256
 * bytes and not carrying into the high half. */
static int v9x_d3d_vi_address(v9x_u32 heap_low, v9x_u32 offset,
                              v9x_u32 *low)
{
    *low = heap_low + offset;
    return *low >= heap_low && (*low & (V9X_D3D_VI_SURFACE_ALIGN - 1ul)) == 0ul;
}

/* A float's bits, for the built PS's literals. */
static v9x_u32 v9x_d3d_vi_float_bits(float value)
{
    union {
        float value;
        v9x_u32 bits;
    } converted;

    converted.value = value;
    return converted.bits;
}

/* field / max as a float's bits: a channel as the sampler or the CB has
 * it. */
static v9x_u32 v9x_d3d_vi_unorm(v9x_u32 field, v9x_u32 max)
{
    return v9x_d3d_vi_float_bits((float)(long)field / (float)(long)max);
}

/* Direct3D's blend factors to CB_BLEND0_CONTROL's, as Cedar's are mapped:
 * the destination-alpha factors are not offered (an XRGB target has no
 * alpha to read), SRCALPHASAT is a source factor only. */
static int v9x_d3d_vi_factor(v9x_u32 factor, int source, v9x_u32 *code)
{
    switch (factor) {
    case V9X_R3D_BLEND_ZERO:
        *code = V9X_VI_BLEND_ZERO;
        return 1;
    case V9X_R3D_BLEND_ONE:
        *code = V9X_VI_BLEND_ONE;
        return 1;
    case V9X_R3D_BLEND_SRCCOLOR:
        *code = V9X_VI_BLEND_SRC_COLOR;
        return 1;
    case V9X_R3D_BLEND_INVSRCCOLOR:
        *code = V9X_VI_BLEND_ONE_MINUS_SRC_COLOR;
        return 1;
    case V9X_R3D_BLEND_SRCALPHA:
        *code = V9X_VI_BLEND_SRC_ALPHA;
        return 1;
    case V9X_R3D_BLEND_INVSRCALPHA:
        *code = V9X_VI_BLEND_ONE_MINUS_SRC_ALPHA;
        return 1;
    case V9X_R3D_BLEND_DESTCOLOR:
        *code = V9X_VI_BLEND_DST_COLOR;
        return 1;
    case V9X_R3D_BLEND_INVDESTCOLOR:
        *code = V9X_VI_BLEND_ONE_MINUS_DST_COLOR;
        return 1;
    case V9X_R3D_BLEND_SRCALPHASAT:
        if (source) {
            *code = V9X_VI_BLEND_SRC_ALPHA_SATURATE;
            return 1;
        }
        return 0;
    }
    return 0;
}

/* Direct3D's D3DCMPFUNC (1..8) to the PS's v_cmpx, "alpha FUNC ref". */
static int v9x_d3d_vi_alpha_compare(v9x_u32 func, v9x_u32 *op)
{
    static const v9x_u32 ops[8] = {
        V9X_VI_VOPC_CMPX_F_F32, V9X_VI_VOPC_CMPX_LT_F32,
        V9X_VI_VOPC_CMPX_EQ_F32, V9X_VI_VOPC_CMPX_LE_F32,
        V9X_VI_VOPC_CMPX_GT_F32, V9X_VI_VOPC_CMPX_NEQ_F32,
        V9X_VI_VOPC_CMPX_GE_F32, V9X_VI_VOPC_CMPX_TRU_F32
    };

    if (func < V9X_R3D_CMP_NEVER || func > V9X_R3D_CMP_ALWAYS) {
        return 0;
    }
    *op = ops[func - V9X_R3D_CMP_NEVER];
    return 1;
}

/*
 * A colour key in its texture's format as the floats the sampler returns
 * for B, G and R, memory order: each field's value over its maximum. The
 * tolerance is half a step of the finest of the three, so a texel the
 * sampler read point-for-point matches exactly one key; the PS compares
 * the squared distance, so it is squared here. A 1555 or 4444 key's alpha
 * is not compared: Direct3D keys on colour. Cedar's rule (d3d_eg_map.c).
 */
static void v9x_d3d_vi_key(v9x_u32 format, v9x_u32 key,
                           struct v9x_vi_d3d_ps *built)
{
    v9x_u32 shift_g = 8ul;
    v9x_u32 shift_r = 16ul;
    v9x_u32 max_b = 255ul;
    v9x_u32 max_g = 255ul;
    v9x_u32 max_r = 255ul;
    float tolerance;

    if (format == V9X_VI_IMG_FORMAT_5_6_5) {
        shift_g = 5ul;
        shift_r = 11ul;
        max_b = 31ul;
        max_g = 63ul;
        max_r = 31ul;
    } else if (format == V9X_VI_IMG_FORMAT_1_5_5_5) {
        shift_g = 5ul;
        shift_r = 10ul;
        max_b = 31ul;
        max_g = 31ul;
        max_r = 31ul;
    } else if (format == V9X_VI_IMG_FORMAT_4_4_4_4) {
        shift_g = 4ul;
        shift_r = 8ul;
        max_b = 15ul;
        max_g = 15ul;
        max_r = 15ul;
    }
    built->key = 1ul;
    built->key_colour[0] = v9x_d3d_vi_unorm(key & max_b, max_b);
    built->key_colour[1] = v9x_d3d_vi_unorm((key >> shift_g) & max_g, max_g);
    built->key_colour[2] = v9x_d3d_vi_unorm((key >> shift_r) & max_r, max_r);
    tolerance = 1.0f / (float)(long)(2ul * max_g);
    built->key_tolerance2 = v9x_d3d_vi_float_bits(tolerance * tolerance);
}

/* Direct3D's D3DCMPFUNC (1..8) to ZFUNC (FRAG_NEVER..FRAG_ALWAYS). */
static int v9x_d3d_vi_compare(v9x_u32 func, v9x_u32 *code)
{
    if (func < V9X_R3D_CMP_NEVER || func > V9X_R3D_CMP_ALWAYS) {
        return 0;
    }
    *code = func - V9X_R3D_CMP_NEVER;
    return 1;
}

/* A filter's within-level half (non-zero for bilinear) and its mip filter,
 * as Cedar's map reads Direct3D's names: MIPNEAREST point and the nearest
 * level, MIPLINEAR bilinear and the nearest level, LINEARMIPNEAREST point
 * between two levels, LINEARMIPLINEAR trilinear. */
static int v9x_d3d_vi_filter(v9x_u32 filter, v9x_u32 *bilinear,
                             v9x_u32 *mip)
{
    *mip = V9X_VI_MIP_FILTER_NONE;
    switch (filter) {
    case V9X_R3D_FILTER_NEAREST:
        *bilinear = 0ul;
        return 1;
    case V9X_R3D_FILTER_LINEAR:
        *bilinear = 1ul;
        return 1;
    case V9X_R3D_FILTER_MIPNEAREST:
        *bilinear = 0ul;
        *mip = V9X_VI_MIP_FILTER_POINT;
        return 1;
    case V9X_R3D_FILTER_MIPLINEAR:
        *bilinear = 1ul;
        *mip = V9X_VI_MIP_FILTER_POINT;
        return 1;
    case V9X_R3D_FILTER_LINEARMIPNEAREST:
        *bilinear = 0ul;
        *mip = V9X_VI_MIP_FILTER_LINEAR;
        return 1;
    case V9X_R3D_FILTER_LINEARMIPLINEAR:
        *bilinear = 1ul;
        *mip = V9X_VI_MIP_FILTER_LINEAR;
        return 1;
    }
    return 0;
}

static int v9x_d3d_vi_address_mode(v9x_u32 address, v9x_u32 *mode)
{
    switch (address) {
    case V9X_R3D_ADDRESS_WRAP:
        *mode = V9X_VI_TEX_ADDRESS_WRAP;
        return 1;
    case V9X_R3D_ADDRESS_MIRROR:
        *mode = V9X_VI_TEX_ADDRESS_MIRROR;
        return 1;
    case V9X_R3D_ADDRESS_CLAMP:
        *mode = V9X_VI_TEX_ADDRESS_CLAMP;
        return 1;
    }
    return 0;
}

/*
 * Direct3D's DX5 texture blends to a pixel shader, as Cedar's map does.
 * DECAL and COPY take the texel's colour and alpha (the texture PS).
 * MODULATEALPHA multiplies colour and alpha (the modulate PS). MODULATE
 * takes the texel's alpha when the texture has one and the vertex's
 * otherwise: without alpha the sampler reads alpha as 1 and the product is
 * the vertex's; with it, the product differs from Direct3D only where the
 * vertex's alpha is below 1. DECALALPHA, ADD and the mask forms are not
 * built.
 */
static int v9x_d3d_vi_op(v9x_u32 op, v9x_u32 *ps)
{
    switch (op) {
    case V9X_R3D_TEXOP_DECAL:
    case V9X_R3D_TEXOP_COPY:
        *ps = V9X_D3D_VI_PS_TEXTURE;
        return 1;
    case V9X_R3D_TEXOP_MODULATE:
    case V9X_R3D_TEXOP_MODULATEALPHA:
        *ps = V9X_D3D_VI_PS_MODULATE;
        return 1;
    }
    return 0;
}

static v9x_u32 v9x_d3d_vi_map_texture(const V9X_R3D_DRAW *draw,
                                      const V9X_D3D_VI_TEXTURE *texture,
                                      v9x_u32 heap_high, v9x_u32 heap_low,
                                      struct v9x_vi_tl_draw *out,
                                      v9x_u32 *ps)
{
    const V9X_R3D_TEXTURE *sampler = &draw->texture;
    v9x_u32 bytes;
    v9x_u32 low;
    v9x_u32 mag;
    v9x_u32 min;
    v9x_u32 mode;
    v9x_u32 mag_mip;
    v9x_u32 mip;

    if (texture == 0) {
        return V9X_D3D_VI_REFUSE_TEXTURE_FORMAT;
    }
    if (texture->format == V9X_VI_IMG_FORMAT_8_8_8_8) {
        bytes = 4ul;
    } else if (texture->format == V9X_VI_IMG_FORMAT_5_6_5 ||
               texture->format == V9X_VI_IMG_FORMAT_1_5_5_5 ||
               texture->format == V9X_VI_IMG_FORMAT_4_4_4_4) {
        bytes = 2ul;
    } else {
        return V9X_D3D_VI_REFUSE_TEXTURE_FORMAT;
    }
    if (texture->width == 0ul || texture->height == 0ul ||
        texture->width > V9X_D3D_VI_TEXTURE_SIDE_MAX ||
        texture->height > V9X_D3D_VI_TEXTURE_SIDE_MAX ||
        texture->pitch_texels < texture->width ||
        ((texture->pitch_texels * bytes) % V9X_D3D_VI_ROW_ALIGN) != 0ul ||
        !v9x_d3d_vi_address(heap_low, texture->offset, &low)) {
        return V9X_D3D_VI_REFUSE_TEXTURE_SHAPE;
    }
    if (!v9x_d3d_vi_op(sampler->op, ps)) {
        return V9X_D3D_VI_REFUSE_TEXTURE_OP;
    }
    /* The cylindrical WRAPU/WRAPV states have no sampler function. */
    if (sampler->wrap_u != 0ul || sampler->wrap_v != 0ul ||
        !v9x_d3d_vi_address_mode(sampler->address, &mode)) {
        return V9X_D3D_VI_REFUSE_TEXTURE_ADDRESS;
    }
    /* The S# takes one within-level filter for both here: the
     * magnification filter's, which a texture mapped near 1:1 shows. The
     * mip filter is the minification filter's, over a chain the engine
     * resolved; a magnification filter has none. */
    if (!v9x_d3d_vi_filter(sampler->mag_filter, &mag, &mag_mip) ||
        !v9x_d3d_vi_filter(sampler->min_filter, &min, &mip)) {
        return V9X_D3D_VI_REFUSE_TEXTURE_FILTER;
    }
    if (texture->levels > 1ul && mip != V9X_VI_MIP_FILTER_NONE) {
        if (texture->levels > V9X_VI_MIP_LEVELS_MAX) {
            return V9X_D3D_VI_REFUSE_TEXTURE_SHAPE;
        }
        out->texture_last_level = texture->levels - 1ul;
        out->texture_mip_filter = mip;
    }
    out->texture_low = low;
    out->texture_high = heap_high;
    out->texture_width = texture->width;
    out->texture_height = texture->height;
    out->texture_pitch = texture->pitch_texels;
    out->texture_format = texture->format;
    out->texture_tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
    out->texture_bilinear = mag;
    out->texture_address = mode;
    out->ps_vgprs = *ps == V9X_D3D_VI_PS_MODULATE ? V9X_VI_MODULATE_PS_VGPRS
                                                 : V9X_VI_TEXTURE_PS_VGPRS;
    return V9X_D3D_VI_REFUSE_NONE;
}

v9x_u32 v9x_d3d_vi_map_draw(const V9X_R3D_DRAW *draw,
                            const V9X_D3D_VI_TEXTURE *texture,
                            v9x_u32 heap_high,
                            v9x_u32 heap_low,
                            v9x_u32 depth_bytes,
                            struct v9x_vi_tl_draw *out,
                            v9x_u32 *ps,
                            struct v9x_vi_d3d_ps *built)
{
    unsigned char *bytes = (unsigned char *)out;
    v9x_u32 bytes_per_pixel;
    v9x_u32 index;
    v9x_u32 code;
    v9x_u32 low;

    for (index = 0ul; index < sizeof(*out); ++index) {
        bytes[index] = 0u;
    }
    *ps = V9X_D3D_VI_PS_GOURAUD;
    /* The render interface's explicit draws (OpenGL, Glide) carry a
     * scissor and a write mask this builder does not take yet; levels in
     * CPU memory and a second unit are not sampled. */
    if (draw->explicit_state != 0ul || draw->texture.levels != 0 ||
        draw->texcoords1 != 0 || draw->texture1.object != 0) {
        return V9X_D3D_VI_REFUSE_EXPLICIT;
    }

    /* The target: linear aligned, rows of whole 64 bytes and whole 8-pixel
     * tiles, its base on 256 bytes, its slice whole 64-pixel tiles. */
    bytes_per_pixel = v9x_d3d_vi_target(draw->target.format, &out->format,
                                        &out->target_mask);
    if (bytes_per_pixel == 0ul || draw->target.width == 0ul ||
        draw->target.height == 0ul ||
        (draw->target.pitch % V9X_D3D_VI_ROW_ALIGN) != 0ul ||
        (draw->target.pitch % (8ul * bytes_per_pixel)) != 0ul ||
        draw->target.pitch / bytes_per_pixel < draw->target.width ||
        ((draw->target.pitch / bytes_per_pixel) * draw->target.height) %
            64ul != 0ul ||
        !v9x_d3d_vi_address(heap_low, draw->target.offset, &low)) {
        return V9X_D3D_VI_REFUSE_TARGET;
    }
    out->target_low = low;
    out->target_high = heap_high;
    out->pitch_pixels = draw->target.pitch / bytes_per_pixel;
    out->height = draw->target.height;
    out->tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
    out->d3d_pixel_centres = 1ul;

    /* Depth, when enabled and bound: Z16 as d3d_vi.c places it, its rows
     * padded to the tile. Disabled, the DB has no buffer and neither tests
     * nor writes, which is Direct3D's ZENABLE FALSE. */
    if (draw->depth_enable != 0ul && draw->depth.object != 0 &&
        draw->depth.pitch != 0ul) {
        if (depth_bytes != 2ul ||
            (draw->depth.pitch % V9X_D3D_VI_DEPTH_PITCH_UNIT) != 0ul ||
            draw->depth.pitch / 2ul < draw->target.width ||
            draw->depth.height < draw->target.height ||
            !v9x_d3d_vi_address(heap_low, draw->depth.offset, &low)) {
            return V9X_D3D_VI_REFUSE_DEPTH_SURFACE;
        }
        if (!v9x_d3d_vi_compare(draw->depth_func, &code)) {
            return V9X_D3D_VI_REFUSE_DEPTH_FUNC;
        }
        out->depth_low = low;
        out->depth_high = heap_high;
        out->depth_func = code;
        out->depth_write = draw->depth_write != 0ul ? 1ul : 0ul;
        out->depth_pitch = draw->depth.pitch / 2ul;
        out->depth_height =
            (draw->depth.height + V9X_D3D_VI_DEPTH_TILE_ROWS - 1ul) &
            ~(V9X_D3D_VI_DEPTH_TILE_ROWS - 1ul);
    }

    /* Blending: the colour's and the alpha's factors alike, added. */
    if (draw->blend_enable != 0ul) {
        v9x_u32 src;
        v9x_u32 dst;

        if (!v9x_d3d_vi_factor(draw->src_blend, 1, &src) ||
            !v9x_d3d_vi_factor(draw->dst_blend, 0, &dst)) {
            return V9X_D3D_VI_REFUSE_BLEND;
        }
        out->blend_control = v9x_vi_3d_blend(src, dst);
    }

    /* The built PS's options: nothing yet, then whatever below needs. */
    for (index = 0ul; index < sizeof(*built); ++index) {
        ((unsigned char *)built)[index] = 0u;
    }
    if (draw->alpha_test_enable != 0ul) {
        if (!v9x_d3d_vi_alpha_compare(draw->alpha_func, &code)) {
            return V9X_D3D_VI_REFUSE_ALPHA_TEST;
        }
        built->alpha_test = 1ul;
        built->alpha_func = code;
        built->alpha_ref = v9x_d3d_vi_unorm(draw->alpha_ref & 0xfful, 255ul);
    }
    /* Fog towards the D3DCOLOR, its B, G, R in memory order. */
    if (draw->fog_enable != 0ul) {
        built->fog = 1ul;
        built->fog_colour[0] = v9x_d3d_vi_unorm(draw->fog_color & 0xfful,
                                                255ul);
        built->fog_colour[1] =
            v9x_d3d_vi_unorm((draw->fog_color >> 8) & 0xfful, 255ul);
        built->fog_colour[2] =
            v9x_d3d_vi_unorm((draw->fog_color >> 16) & 0xfful, 255ul);
    }

    if (draw->texture.object != 0) {
        code = v9x_d3d_vi_map_texture(draw, texture, heap_high, heap_low,
                                      out, ps);
        if (code != V9X_D3D_VI_REFUSE_NONE) {
            return code;
        }
        if (draw->color_key_enable != 0ul && texture->has_key != 0ul) {
            v9x_d3d_vi_key(texture->format, texture->key, built);
        }
    } else {
        out->ps_vgprs = 0ul;
    }

    /* Any of the three: the PS built for this draw, with the op and the
     * specular as the slot shader would have had them. */
    if (built->alpha_test != 0ul || built->fog != 0ul || built->key != 0ul) {
        built->op = *ps == V9X_D3D_VI_PS_TEXTURE ? V9X_VI_PS_TEXTURE :
                    (*ps == V9X_D3D_VI_PS_MODULATE ? V9X_VI_PS_MODULATE
                                                   : V9X_VI_PS_COLOUR);
        built->specular = draw->specular_enable != 0ul ? 1ul : 0ul;
        out->ps_vgprs = V9X_VI_D3D_PS_BUILT_VGPRS;
        out->ps_kill = built->alpha_test != 0ul || built->key != 0ul ? 1ul
                                                                     : 0ul;
        *ps = V9X_D3D_VI_PS_BUILT;
        return V9X_D3D_VI_REFUSE_NONE;
    }

    /* Specular, which DirectX 5 and 6 enable by default: each PS's
     * variant that adds it, with its VGPRs. */
    if (draw->specular_enable != 0ul) {
        if (*ps == V9X_D3D_VI_PS_GOURAUD) {
            out->ps_vgprs = V9X_VI_GOURAUD_PS_SPECULAR_VGPRS;
        } else if (*ps == V9X_D3D_VI_PS_TEXTURE) {
            out->ps_vgprs = V9X_VI_TEXTURE_PS_SPECULAR_VGPRS;
        } else {
            out->ps_vgprs = V9X_VI_MODULATE_PS_SPECULAR_VGPRS;
        }
        *ps += V9X_D3D_VI_PS_SPECULAR;
    }
    return V9X_D3D_VI_REFUSE_NONE;
}
