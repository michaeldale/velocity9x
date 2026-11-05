/*
 * The neutral draw description to RS690 3D builder state; see
 * d3d_rs690_map.h. The register meaning behind each code is in
 * rs690_3d.h; the texture combine choices are d3d_r128_map.c's reading of
 * Direct3D's DX5 texture blends.
 */
#include "d3d_rs690_map.h"

#define V9X_D3D_RS690_ALPHA_REF_MASK 0xfful
#define V9X_D3D_RS690_FOG_COLOR_MASK 0x00fffffful

/* The builder's texture limit (TX_FORMAT0's 11-bit size fields). */
#define V9X_D3D_RS690_TEXTURE_SIDE_MAX V9X_RS690_TEX_SIZE_MAX

/* Direct3D's compare functions, NEVER to ALWAYS, as ZB_ZSTENCILCNTL
 * numbers them: LEQUAL and EQUAL, GEQUAL and GREATER trade places. */
static int v9x_d3d_rs690_depth_func(v9x_u32 func, v9x_u32 *code)
{
    static const v9x_u32 codes[8] = {
        V9X_RS690_ZFUNC_NEVER, V9X_RS690_ZFUNC_LESS, V9X_RS690_ZFUNC_EQUAL,
        V9X_RS690_ZFUNC_LEQUAL, V9X_RS690_ZFUNC_GREATER,
        V9X_RS690_ZFUNC_NOTEQUAL, V9X_RS690_ZFUNC_GEQUAL,
        V9X_RS690_ZFUNC_ALWAYS
    };

    if (func < V9X_R3D_CMP_NEVER || func > V9X_R3D_CMP_ALWAYS) {
        return 0;
    }
    *code = codes[func - V9X_R3D_CMP_NEVER];
    return 1;
}

/* The alpha test's are Direct3D's less one (FG_ALPHA_FUNC 10:8). */
static int v9x_d3d_rs690_alpha_func(v9x_u32 func, v9x_u32 *code)
{
    if (func < V9X_R3D_CMP_NEVER || func > V9X_R3D_CMP_ALWAYS) {
        return 0;
    }
    *code = func - V9X_R3D_CMP_NEVER;
    return 1;
}

/*
 * Direct3D's blend factors to RB3D_CBLEND's GL codes. Destination alpha
 * is refused: neither target this engine draws keeps any (RGB565,
 * XRGB8888). SRCALPHASAT is a source factor only.
 */
static int v9x_d3d_rs690_factor(v9x_u32 factor, int source, v9x_u32 *code)
{
    switch (factor) {
    case V9X_R3D_BLEND_ZERO:
        *code = V9X_RS690_BLEND_ZERO;
        return 1;
    case V9X_R3D_BLEND_ONE:
        *code = V9X_RS690_BLEND_ONE;
        return 1;
    case V9X_R3D_BLEND_SRCCOLOR:
        *code = V9X_RS690_BLEND_SRC_COLOR;
        return 1;
    case V9X_R3D_BLEND_INVSRCCOLOR:
        *code = V9X_RS690_BLEND_INV_SRC_COLOR;
        return 1;
    case V9X_R3D_BLEND_SRCALPHA:
        *code = V9X_RS690_BLEND_SRC_ALPHA;
        return 1;
    case V9X_R3D_BLEND_INVSRCALPHA:
        *code = V9X_RS690_BLEND_INV_SRC_ALPHA;
        return 1;
    case V9X_R3D_BLEND_DESTCOLOR:
        *code = V9X_RS690_BLEND_DST_COLOR;
        return 1;
    case V9X_R3D_BLEND_INVDESTCOLOR:
        *code = V9X_RS690_BLEND_INV_DST_COLOR;
        return 1;
    case V9X_R3D_BLEND_SRCALPHASAT:
        if (source) {
            *code = V9X_RS690_BLEND_SRC_ALPHA_SAT;
            return 1;
        }
        return 0;
    }
    return 0;
}

/*
 * The filter level 0 is sampled with. Mip chains are placed but not
 * sampled, so a mip filter means the filter it uses within a level:
 * MIPNEAREST and LINEARMIPNEAREST point-sample a level, MIPLINEAR and
 * LINEARMIPLINEAR filter it bilinearly (the reading d3d_r128_map.c takes
 * from Mesa's r128 mapping).
 */
static int v9x_d3d_rs690_filter(v9x_u32 filter, v9x_u32 *code)
{
    switch (filter) {
    case V9X_R3D_FILTER_NEAREST:
    case V9X_R3D_FILTER_MIPNEAREST:
    case V9X_R3D_FILTER_LINEARMIPNEAREST:
        *code = V9X_RS690_TEXFILTER_NEAREST;
        return 1;
    case V9X_R3D_FILTER_LINEAR:
    case V9X_R3D_FILTER_MIPLINEAR:
    case V9X_R3D_FILTER_LINEARMIPLINEAR:
        *code = V9X_RS690_TEXFILTER_LINEAR;
        return 1;
    }
    return 0;
}

/*
 * Direct3D's DX5 texture blends. DECAL and COPY take the texel's colour
 * and alpha (an RGB565 texel's alpha reads as one). MODULATE multiplies
 * the colours and takes the texel's alpha when it has one, the vertex's
 * otherwise. MODULATEALPHA multiplies both. DECALALPHA, ADD and the mask
 * forms are not built.
 */
static int v9x_d3d_rs690_combine(v9x_u32 op, int has_alpha,
                                 struct v9x_rs690_3d_state *state)
{
    switch (op) {
    case V9X_R3D_TEXOP_DECAL:
    case V9X_R3D_TEXOP_COPY:
        state->texture.combine = V9X_RS690_TEXCOMBINE_REPLACE;
        state->texture_alpha = V9X_RS690_TEXALPHA_TEXEL;
        return 1;
    case V9X_R3D_TEXOP_MODULATE:
        state->texture.combine = V9X_RS690_TEXCOMBINE_MODULATE;
        state->texture_alpha = has_alpha ? V9X_RS690_TEXALPHA_TEXEL
                                         : V9X_RS690_TEXALPHA_DIFFUSE;
        return 1;
    case V9X_R3D_TEXOP_MODULATEALPHA:
        state->texture.combine = V9X_RS690_TEXCOMBINE_MODULATE;
        state->texture_alpha = V9X_RS690_TEXALPHA_MODULATE;
        return 1;
    }
    return 0;
}

static v9x_u32 v9x_d3d_rs690_map_texture(const V9X_R3D_DRAW *draw,
                                         const V9X_D3D_RS690_TEXTURE *resolved,
                                         v9x_u32 vram_base,
                                         struct v9x_rs690_3d_state *state)
{
    const V9X_R3D_TEXTURE *sampler = &draw->texture;
    v9x_u32 min_filter;
    v9x_u32 mag_filter;

    if (resolved == 0 || resolved->format == V9X_D3D_RS690_TEXTURE_UNKNOWN) {
        return V9X_D3D_RS690_REFUSE_TEXTURE_FORMAT;
    }
    if (resolved->width == 0ul || resolved->height == 0ul ||
        resolved->width > V9X_D3D_RS690_TEXTURE_SIDE_MAX ||
        resolved->height > V9X_D3D_RS690_TEXTURE_SIDE_MAX) {
        return V9X_D3D_RS690_REFUSE_TEXTURE_SHAPE;
    }
    if (draw->color_key_enable != 0ul) {
        return V9X_D3D_RS690_REFUSE_COLOR_KEY;
    }
    /* The cylindrical WRAPU/WRAPV states have no chip function. */
    if (sampler->wrap_u != 0ul || sampler->wrap_v != 0ul) {
        return V9X_D3D_RS690_REFUSE_TEXTURE_ADDRESS;
    }
    switch (sampler->address) {
    case V9X_R3D_ADDRESS_WRAP:
        state->texture.address = V9X_RS690_TEXADDR_WRAP;
        break;
    case V9X_R3D_ADDRESS_CLAMP:
        state->texture.address = V9X_RS690_TEXADDR_CLAMP;
        break;
    default:
        return V9X_D3D_RS690_REFUSE_TEXTURE_ADDRESS;
    }
    /* One filter both ways in the builder. */
    if (!v9x_d3d_rs690_filter(sampler->min_filter, &min_filter) ||
        !v9x_d3d_rs690_filter(sampler->mag_filter, &mag_filter) ||
        min_filter != mag_filter) {
        return V9X_D3D_RS690_REFUSE_TEXTURE_FILTER;
    }
    if (!v9x_d3d_rs690_combine(sampler->op, resolved->has_alpha, state)) {
        return V9X_D3D_RS690_REFUSE_TEXTURE_OP;
    }
    state->textured = 1ul;
    state->texture.offset = vram_base + resolved->offset;
    state->texture.pitch_bytes = resolved->pitch_bytes;
    state->texture.format = resolved->format;
    state->texture.width = resolved->width;
    state->texture.height = resolved->height;
    state->texture.filter = min_filter;
    return V9X_D3D_RS690_REFUSE_NONE;
}

v9x_u32 v9x_d3d_rs690_map_draw(const V9X_R3D_DRAW *draw,
                               const V9X_D3D_RS690_TEXTURE *resolved,
                               v9x_u32 fb_gpu_base,
                               v9x_u32 vram_base,
                               v9x_u32 vram_bytes,
                               v9x_u32 depth_bytes,
                               struct v9x_rs690_3d_state *state)
{
    unsigned char *bytes = (unsigned char *)state;
    v9x_u32 index;
    v9x_u32 code;

    for (index = 0ul; index < sizeof(*state); ++index) {
        bytes[index] = 0u;
    }

    /* The render interface's draws (explicit state, CPU-resident levels,
     * a second unit) are the software engine's for now. */
    if (draw->explicit_state != 0ul || draw->texture.levels != 0 ||
        draw->texture.level_count != 0ul || draw->texcoords1 != 0 ||
        draw->texture1.object != 0) {
        return V9X_D3D_RS690_REFUSE_EXPLICIT;
    }
    switch (draw->target.format) {
    case V9X_R3D_FORMAT_RGB565:
        state->target.bytes_per_pixel = 2ul;
        break;
    case V9X_R3D_FORMAT_XRGB8888:
        /* The top byte is X: written, it would carry the fragment's alpha
         * (V9XD32 on M-GB8KRE read FF13579Bh for 0013579Bh). */
        state->target.bytes_per_pixel = 4ul;
        state->x_channel = 1ul;
        break;
    default:
        return V9X_D3D_RS690_REFUSE_TARGET;
    }
    /* Flat takes the first vertex's colours, Direct3D's convention. */
    if (draw->shade_mode != V9X_R3D_SHADE_GOURAUD &&
        draw->shade_mode != V9X_R3D_SHADE_FLAT) {
        return V9X_D3D_RS690_REFUSE_SHADE;
    }
    state->flat = draw->shade_mode == V9X_R3D_SHADE_FLAT ? 1ul : 0ul;
    state->specular = draw->specular_enable != 0ul ? 1ul : 0ul;
    /* Vertex fog: the factor in the specular alpha (the core puts table
     * fog's there too), the colour 00RRGGBBh. */
    if (draw->fog_enable != 0ul) {
        state->fog = 1ul;
        state->fog_color = draw->fog_color & V9X_D3D_RS690_FOG_COLOR_MASK;
    }

    state->target.fb_gpu_base = fb_gpu_base;
    state->target.fb_bytes = vram_base + vram_bytes;
    state->target.offset = vram_base + draw->target.offset;
    state->target.pitch_bytes = draw->target.pitch;
    state->target.width = draw->target.width;
    state->target.height = draw->target.height;
    /* The core culls before the engine sees a triangle. */
    state->cull = V9X_RS690_CULL_NONE;

    /* Depth needs the state, a bound Z16 surface and a pitch. */
    if (draw->depth_enable != 0ul && draw->depth.object != 0 &&
        draw->depth.pitch != 0ul) {
        if (depth_bytes != 2ul) {
            return V9X_D3D_RS690_REFUSE_DEPTH_SURFACE;
        }
        if (!v9x_d3d_rs690_depth_func(draw->depth_func, &code)) {
            return V9X_D3D_RS690_REFUSE_DEPTH_FUNC;
        }
        state->depth_enable = 1ul;
        state->depth_write = draw->depth_write != 0ul ? 1ul : 0ul;
        state->depth_func = code;
        state->depth_offset = vram_base + draw->depth.offset;
        state->depth_pitch_bytes = draw->depth.pitch;
    }
    if (draw->alpha_test_enable != 0ul) {
        if (!v9x_d3d_rs690_alpha_func(draw->alpha_func, &code)) {
            return V9X_D3D_RS690_REFUSE_ALPHA_FUNC;
        }
        state->alpha_test_enable = 1ul;
        state->alpha_func = code;
        state->alpha_ref = draw->alpha_ref & V9X_D3D_RS690_ALPHA_REF_MASK;
    }
    if (draw->blend_enable != 0ul) {
        if (!v9x_d3d_rs690_factor(draw->src_blend, 1, &state->blend_src) ||
            !v9x_d3d_rs690_factor(draw->dst_blend, 0, &state->blend_dst)) {
            return V9X_D3D_RS690_REFUSE_BLEND;
        }
        state->blend_enable = 1ul;
    }
    if (draw->texture.object == 0) {
        return V9X_D3D_RS690_REFUSE_NONE;
    }
    return v9x_d3d_rs690_map_texture(draw, resolved, vram_base, state);
}
