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

/* Direct3D's D3DCMPFUNC (1..8) to ZFUNC (FRAG_NEVER..FRAG_ALWAYS). */
static int v9x_d3d_vi_compare(v9x_u32 func, v9x_u32 *code)
{
    if (func < V9X_R3D_CMP_NEVER || func > V9X_R3D_CMP_ALWAYS) {
        return 0;
    }
    *code = func - V9X_R3D_CMP_NEVER;
    return 1;
}

/* A within-level filter: the mip filters sample level 0 alone, so each
 * takes its within-level half. Non-zero for bilinear. */
static int v9x_d3d_vi_filter(v9x_u32 filter, v9x_u32 *bilinear)
{
    switch (filter) {
    case V9X_R3D_FILTER_NEAREST:
    case V9X_R3D_FILTER_MIPNEAREST:
    case V9X_R3D_FILTER_MIPLINEAR:
        *bilinear = 0ul;
        return 1;
    case V9X_R3D_FILTER_LINEAR:
    case V9X_R3D_FILTER_LINEARMIPNEAREST:
    case V9X_R3D_FILTER_LINEARMIPLINEAR:
        *bilinear = 1ul;
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

    if (texture == 0) {
        return V9X_D3D_VI_REFUSE_TEXTURE_FORMAT;
    }
    if (texture->format == V9X_VI_IMG_FORMAT_8_8_8_8) {
        bytes = 4ul;
    } else if (texture->format == V9X_VI_IMG_FORMAT_5_6_5) {
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
    /* The S# takes one filter for both here: the magnification filter's,
     * which a texture mapped near 1:1 shows. */
    if (!v9x_d3d_vi_filter(sampler->mag_filter, &mag) ||
        !v9x_d3d_vi_filter(sampler->min_filter, &min)) {
        return V9X_D3D_VI_REFUSE_TEXTURE_FILTER;
    }
    if (draw->color_key_enable != 0ul && texture->has_key != 0ul) {
        return V9X_D3D_VI_REFUSE_COLOR_KEY;
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
                            v9x_u32 *ps)
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

    /* Step 2 and 3's: refused and counted. */
    if (draw->blend_enable != 0ul) {
        return V9X_D3D_VI_REFUSE_BLEND;
    }
    if (draw->alpha_test_enable != 0ul) {
        return V9X_D3D_VI_REFUSE_ALPHA_TEST;
    }
    if (draw->fog_enable != 0ul) {
        return V9X_D3D_VI_REFUSE_FOG;
    }

    if (draw->texture.object != 0) {
        code = v9x_d3d_vi_map_texture(draw, texture, heap_high, heap_low,
                                      out, ps);
        if (code != V9X_D3D_VI_REFUSE_NONE) {
            return code;
        }
    } else {
        out->ps_vgprs = 0ul;
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
