/*
 * The neutral draw description to Radeon 9250 (R200) 3D builder state; see
 * d3d_rv280_map.h. The register meaning behind each code is in rv280_3d.h;
 * the texture combine choices are d3d_rs690_map.c's reading of Direct3D's
 * DX5 texture blends, restricted to the combines the probes drew.
 */
#include "d3d_rv280_map.h"

#define V9X_D3D_RV280_ALPHA_REF_MASK 0xfful

/* What every draw sends of D3DTLVERTEX: X, Y, Z, 1/W, colour, specular.
 * The specular dword is sent even with fog and specular off; it costs a
 * dword a vertex and keeps the layout one per draw kind. */
#define V9X_D3D_RV280_LAYOUT_BASE \
    (V9X_RV280_LAYOUT_Z | V9X_RV280_LAYOUT_W | V9X_RV280_LAYOUT_SPECULAR)

/*
 * Direct3D's compare functions, NEVER to ALWAYS, as RB3D_ZSTENCILCNTL's
 * Z_TEST and PP_MISC's ALPHA_TEST number them (NEVER 0, LESS 1, LEQUAL 2,
 * EQUAL 3, GEQUAL 4, GREATER 5, NEQUAL 6, ALWAYS 7): LEQUAL and EQUAL,
 * GEQUAL and GREATER trade places.
 */
static int v9x_d3d_rv280_compare(v9x_u32 func, v9x_u32 *code)
{
    static const v9x_u32 codes[8] = {
        0ul, 1ul, 3ul, 2ul, 5ul, 6ul, 4ul, 7ul
    };

    if (func < V9X_R3D_CMP_NEVER || func > V9X_R3D_CMP_ALWAYS) {
        return 0;
    }
    *code = codes[func - V9X_R3D_CMP_NEVER];
    return 1;
}

/*
 * Direct3D's blend factors to RB3D_BLENDCNTL's codes (rv280_3d.h).
 * Destination alpha is refused: neither target drawn here keeps any
 * (RGB565, XRGB8888). SRCALPHASAT (code 42) is not built.
 */
static int v9x_d3d_rv280_factor(v9x_u32 factor, v9x_u32 *code)
{
    switch (factor) {
    case V9X_R3D_BLEND_ZERO:
        *code = 32ul;
        return 1;
    case V9X_R3D_BLEND_ONE:
        *code = 33ul;
        return 1;
    case V9X_R3D_BLEND_SRCCOLOR:
        *code = 34ul;
        return 1;
    case V9X_R3D_BLEND_INVSRCCOLOR:
        *code = 35ul;
        return 1;
    case V9X_R3D_BLEND_DESTCOLOR:
        *code = 36ul;
        return 1;
    case V9X_R3D_BLEND_INVDESTCOLOR:
        *code = 37ul;
        return 1;
    case V9X_R3D_BLEND_SRCALPHA:
        *code = 38ul;
        return 1;
    case V9X_R3D_BLEND_INVSRCALPHA:
        *code = 39ul;
        return 1;
    }
    return 0;
}

/*
 * The filter level 0 is sampled with. Mip chains are not sampled, so a mip
 * filter means the filter it uses within a level: MIPNEAREST and
 * LINEARMIPNEAREST point-sample a level, MIPLINEAR and LINEARMIPLINEAR
 * filter it bilinearly (d3d_rs690_map.c's reading). Non-zero is linear.
 */
static int v9x_d3d_rv280_filter(v9x_u32 filter, v9x_u32 *linear)
{
    switch (filter) {
    case V9X_R3D_FILTER_NEAREST:
    case V9X_R3D_FILTER_MIPNEAREST:
    case V9X_R3D_FILTER_LINEARMIPNEAREST:
        *linear = 0ul;
        return 1;
    case V9X_R3D_FILTER_LINEAR:
    case V9X_R3D_FILTER_MIPLINEAR:
    case V9X_R3D_FILTER_LINEARMIPLINEAR:
        *linear = 1ul;
        return 1;
    }
    return 0;
}

/*
 * Direct3D's DX5 texture blends to blend stage 0. DECAL and COPY take the
 * texel's colour and alpha (an RGB565 texel's alpha reads as one). MODULATE
 * multiplies the colours and takes the texel's alpha when it has one, the
 * vertex's otherwise. MODULATEALPHA multiplies both. With alpha_diffuse (a
 * DX6+ stage whose alpha selects the vertex's, r3d.h), MODULATE and DECAL
 * take the vertex's alpha even from a texture that has its own. DECALALPHA
 * and ADD are not built.
 */
static int v9x_d3d_rv280_combine(v9x_u32 op, int has_alpha,
                                 v9x_u32 alpha_diffuse, v9x_u32 *combine)
{
    switch (op) {
    case V9X_R3D_TEXOP_DECAL:
    case V9X_R3D_TEXOP_COPY:
        *combine = alpha_diffuse != 0ul
                       ? V9X_RV280_COMBINE_DECAL_DIFFUSE_ALPHA
                       : V9X_RV280_COMBINE_DECAL;
        return 1;
    case V9X_R3D_TEXOP_MODULATE:
        *combine = has_alpha && alpha_diffuse == 0ul
                       ? V9X_RV280_COMBINE_MODULATE_TEXEL_ALPHA
                       : V9X_RV280_COMBINE_MODULATE_DIFFUSE_ALPHA;
        return 1;
    case V9X_R3D_TEXOP_MODULATEALPHA:
        *combine = V9X_RV280_COMBINE_MODULATE;
        return 1;
    }
    return 0;
}

/* A power of two from 1 to the builder's 2048. */
static int v9x_d3d_rv280_texture_side(v9x_u32 side)
{
    if (side == 0ul || side > V9X_RV280_TEX_MAX_SIDE) {
        return 0;
    }
    return (side & (side - 1ul)) == 0ul;
}

static v9x_u32 v9x_d3d_rv280_map_texture(const V9X_R3D_DRAW *draw,
                                         const V9X_D3D_RV280_TEXTURE *resolved,
                                         v9x_u32 vram_base,
                                         V9X_D3D_RV280_STATE *state)
{
    const V9X_R3D_TEXTURE *sampler = &draw->texture;
    v9x_u32 min_linear;
    v9x_u32 mag_linear;

    if (resolved == 0 ||
        (resolved->bytes_per_pixel != 2ul &&
         resolved->bytes_per_pixel != 4ul)) {
        return V9X_D3D_RV280_REFUSE_TEXTURE_FORMAT;
    }
    if (!v9x_d3d_rv280_texture_side(resolved->width) ||
        !v9x_d3d_rv280_texture_side(resolved->height)) {
        return V9X_D3D_RV280_REFUSE_TEXTURE_SHAPE;
    }
    /* The texture unit's chroma key is not built yet. Enabled with no key
     * on the texture, it means nothing. */
    if (draw->color_key_enable != 0ul && resolved->has_key) {
        return V9X_D3D_RV280_REFUSE_COLOR_KEY;
    }
    /* The cylindrical WRAPU/WRAPV states have no chip function. */
    if (sampler->wrap_u != 0ul || sampler->wrap_v != 0ul) {
        return V9X_D3D_RV280_REFUSE_TEXTURE_ADDRESS;
    }
    switch (sampler->address) {
    case V9X_R3D_ADDRESS_WRAP:
        state->texture.clamp = 0ul;
        break;
    case V9X_R3D_ADDRESS_CLAMP:
        state->texture.clamp = 1ul;
        break;
    default:
        return V9X_D3D_RV280_REFUSE_TEXTURE_ADDRESS;
    }
    /* One filter both ways in the builder. */
    if (!v9x_d3d_rv280_filter(sampler->min_filter, &min_linear) ||
        !v9x_d3d_rv280_filter(sampler->mag_filter, &mag_linear) ||
        min_linear != mag_linear) {
        return V9X_D3D_RV280_REFUSE_TEXTURE_FILTER;
    }
    if (!v9x_d3d_rv280_combine(sampler->op, resolved->has_alpha,
                               sampler->alpha_diffuse, &state->combine)) {
        return V9X_D3D_RV280_REFUSE_TEXTURE_OP;
    }

    state->textured = 1ul;
    state->layout |= V9X_RV280_LAYOUT_ST;
    state->texture.fb_gpu_base = state->target.fb_gpu_base;
    state->texture.fb_bytes = state->target.fb_bytes;
    state->texture.offset = vram_base + resolved->offset;
    state->texture.pitch_bytes = resolved->pitch_bytes;
    state->texture.bytes_per_pixel = resolved->bytes_per_pixel;
    state->texture.format = resolved->format;
    state->texture.width = resolved->width;
    state->texture.height = resolved->height;
    state->texture.linear = min_linear;
    return V9X_D3D_RV280_REFUSE_NONE;
}

v9x_u32 v9x_d3d_rv280_map_draw(const V9X_R3D_DRAW *draw,
                               const V9X_D3D_RV280_TEXTURE *resolved,
                               v9x_u32 fb_gpu_base,
                               v9x_u32 vram_base,
                               v9x_u32 vram_bytes,
                               v9x_u32 depth_bytes,
                               V9X_D3D_RV280_STATE *state)
{
    unsigned char *bytes = (unsigned char *)state;
    v9x_u32 index;
    v9x_u32 code;

    for (index = 0ul; index < sizeof(*state); ++index) {
        bytes[index] = 0u;
    }
    state->target.raster = &state->raster;

    /* The render interface's explicit draws (OpenGL, Glide) need the
     * scissor and write mask, not built here yet: refused, they go to its
     * CPU fallback. Levels in CPU memory are not sampled and there is one
     * unit: those are refused too. */
    if (draw->explicit_state != 0ul ||
        draw->texture.levels != 0 || draw->texture.level_count != 0ul ||
        draw->texcoords1 != 0 || draw->texture1.object != 0) {
        return V9X_D3D_RV280_REFUSE_EXPLICIT;
    }
    switch (draw->target.format) {
    case V9X_R3D_FORMAT_RGB565:
        state->target.bytes_per_pixel = 2ul;
        break;
    case V9X_R3D_FORMAT_XRGB8888:
        state->target.bytes_per_pixel = 4ul;
        state->raster.keep_x = 1ul;
        break;
    default:
        return V9X_D3D_RV280_REFUSE_TARGET;
    }
    /* Flat takes the first vertex's colours, Direct3D's convention and
     * SE_CNTL's flat shade (the probe's FlatOrder tests). */
    if (draw->shade_mode != V9X_R3D_SHADE_GOURAUD &&
        draw->shade_mode != V9X_R3D_SHADE_FLAT) {
        return V9X_D3D_RV280_REFUSE_SHADE;
    }
    state->gouraud = draw->shade_mode == V9X_R3D_SHADE_GOURAUD ? 1ul : 0ul;
    state->raster.specular_enable = draw->specular_enable != 0ul ? 1ul : 0ul;
    /* Vertex fog: the factor in the specular alpha (the core puts table
     * fog's there too), the colour's RGB. */
    if (draw->fog_enable != 0ul) {
        state->raster.fog_enable = 1ul;
        state->raster.fog_colour = draw->fog_color & V9X_RV280_FOG_COLOR_MASK;
    }

    state->layout = V9X_D3D_RV280_LAYOUT_BASE;
    state->target.layout = V9X_D3D_RV280_LAYOUT_BASE;
    state->target.fb_gpu_base = fb_gpu_base;
    state->target.fb_bytes = vram_base + vram_bytes;
    state->target.offset = vram_base + draw->target.offset;
    state->target.pitch_bytes = draw->target.pitch;
    state->target.width = draw->target.width;
    state->target.height = draw->target.height;
    /* The core culls before the engine sees a triangle: front and cull
     * fields stay zero, both faces drawn. */

    /* Depth needs the state, a bound Z16 surface and a pitch. */
    if (draw->depth_enable != 0ul && draw->depth.object != 0 &&
        draw->depth.pitch != 0ul) {
        if (depth_bytes != 2ul) {
            return V9X_D3D_RV280_REFUSE_DEPTH_SURFACE;
        }
        if (!v9x_d3d_rv280_compare(draw->depth_func, &code)) {
            return V9X_D3D_RV280_REFUSE_DEPTH_FUNC;
        }
        state->raster.z_enable = 1ul;
        state->raster.z_write = draw->depth_write != 0ul ? 1ul : 0ul;
        state->raster.z_func = code;
        state->raster.z_offset = vram_base + draw->depth.offset;
        state->raster.z_pitch_bytes = draw->depth.pitch;
    }
    if (draw->alpha_test_enable != 0ul) {
        if (!v9x_d3d_rv280_compare(draw->alpha_func, &code)) {
            return V9X_D3D_RV280_REFUSE_ALPHA_FUNC;
        }
        state->raster.alpha_test_enable = 1ul;
        state->raster.alpha_func = code;
        state->raster.alpha_ref = draw->alpha_ref &
                                  V9X_D3D_RV280_ALPHA_REF_MASK;
    }
    if (draw->blend_enable != 0ul) {
        if (!v9x_d3d_rv280_factor(draw->src_blend,
                                  &state->raster.blend_src) ||
            !v9x_d3d_rv280_factor(draw->dst_blend,
                                  &state->raster.blend_dst)) {
            return V9X_D3D_RV280_REFUSE_BLEND;
        }
        state->raster.blend_enable = 1ul;
    }
    if (draw->texture.object == 0) {
        return V9X_D3D_RV280_REFUSE_NONE;
    }
    return v9x_d3d_rv280_map_texture(draw, resolved, vram_base, state);
}
