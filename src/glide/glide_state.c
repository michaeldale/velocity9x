/*
 * Glide render state to render-interface state (docs\plans\glide-2x-wrapper.md,
 * Phase 1). See glide_state.h. The interface takes Direct3D's numbers for
 * compare functions and blend factors (r3d_abi.h).
 */
#include "glide_state.h"
#include "glide_vertex.h"

/* D3DCMPFUNC runs NEVER (1) .. ALWAYS (8) in GrCmpFnc_t's order. */
#define V9X_GLIDE_D3D_CMP_FROM_GLIDE 1ul
#define V9X_GLIDE_D3D_CMP_GREATER    5ul

/* D3DBLEND. */
#define V9X_GLIDE_D3D_BLEND_ZERO         1ul
#define V9X_GLIDE_D3D_BLEND_ONE          2ul
#define V9X_GLIDE_D3D_BLEND_SRCCOLOR     3ul
#define V9X_GLIDE_D3D_BLEND_INVSRCCOLOR  4ul
#define V9X_GLIDE_D3D_BLEND_SRCALPHA     5ul
#define V9X_GLIDE_D3D_BLEND_INVSRCALPHA  6ul
#define V9X_GLIDE_D3D_BLEND_DESTALPHA    7ul
#define V9X_GLIDE_D3D_BLEND_INVDESTALPHA 8ul
#define V9X_GLIDE_D3D_BLEND_DESTCOLOR    9ul
#define V9X_GLIDE_D3D_BLEND_INVDESTCOLOR 10ul
#define V9X_GLIDE_D3D_BLEND_SRCALPHASAT  11ul

/* What a combine unit produces, as far as the interface can say it. */
#define V9X_GLIDE_RESULT_ITERATED         0u
#define V9X_GLIDE_RESULT_CONSTANT         1u
#define V9X_GLIDE_RESULT_TEXTURE          2u
#define V9X_GLIDE_RESULT_TEXTURE_ITERATED 3u
#define V9X_GLIDE_RESULT_TEXTURE_CONSTANT 4u
#define V9X_GLIDE_RESULT_UNKNOWN          5u

void v9x_glide_state_init(V9X_GLIDE_STATE *state, v9x_u32 width,
                          v9x_u32 height, v9x_u32 origin)
{
    state->origin = origin;
    state->width = width;
    state->height = height;
    state->depth_mode = V9X_GLIDE_DEPTH_DISABLE;
    state->depth_func = 1ul;    /* LESS */
    state->depth_mask = 1ul;
    state->alpha_func = V9X_GLIDE_CMP_ALWAYS;
    state->alpha_ref = 0ul;
    state->blend_src = V9X_GLIDE_BLEND_ONE;
    state->blend_dst = V9X_GLIDE_BLEND_ZERO;
    state->color.function = V9X_GLIDE_COMBINE_FUNCTION_LOCAL;
    state->color.factor = V9X_GLIDE_COMBINE_FACTOR_ZERO;
    state->color.local = V9X_GLIDE_COMBINE_LOCAL_ITERATED;
    state->color.other = V9X_GLIDE_COMBINE_OTHER_ITERATED;
    state->color.invert = 0ul;
    state->alpha = state->color;
    state->tex_rgb_function = V9X_GLIDE_COMBINE_FUNCTION_LOCAL;
    state->tex_alpha_function = V9X_GLIDE_COMBINE_FUNCTION_LOCAL;
    state->cull_mode = V9X_GLIDE_CULL_DISABLE;
    state->fog_mode = V9X_GLIDE_FOG_DISABLE;
    state->fog_color = 0ul;
    state->chroma_mode = V9X_GLIDE_CHROMAKEY_DISABLE;
    state->chroma_value = 0ul;
    state->constant_color = 0ul;
    state->clip_min_x = 0ul;
    state->clip_min_y = 0ul;
    state->clip_max_x = width;
    state->clip_max_y = height;
    state->clamp_s = V9X_GLIDE_TEXTURE_WRAP;
    state->clamp_t = V9X_GLIDE_TEXTURE_WRAP;
    state->min_filter = V9X_GLIDE_TEXTURE_BILINEAR;
    state->mag_filter = V9X_GLIDE_TEXTURE_BILINEAR;
}

/*
 * A Glide blend factor in Direct3D's numbering. GR_BLEND_COLOR (2) and
 * its inverse (6) mean the destination's colour as a source factor and
 * the source's as a destination factor. ALPHA_SATURATE (15) is
 * PREFOG_COLOR on the destination side, which has no equivalent; it and
 * the reserved values report V9X_FALSE and blend as ONE.
 */
static v9x_u16 v9x_glide_blend_factor(v9x_u32 factor, v9x_u16 destination,
                                      v9x_u32 *out)
{
    switch (factor) {
    case V9X_GLIDE_BLEND_ZERO:
        *out = V9X_GLIDE_D3D_BLEND_ZERO;
        return V9X_TRUE;
    case V9X_GLIDE_BLEND_SRC_ALPHA:
        *out = V9X_GLIDE_D3D_BLEND_SRCALPHA;
        return V9X_TRUE;
    case V9X_GLIDE_BLEND_COLOR:
        *out = destination ? V9X_GLIDE_D3D_BLEND_SRCCOLOR :
                             V9X_GLIDE_D3D_BLEND_DESTCOLOR;
        return V9X_TRUE;
    case V9X_GLIDE_BLEND_DST_ALPHA:
        *out = V9X_GLIDE_D3D_BLEND_DESTALPHA;
        return V9X_TRUE;
    case V9X_GLIDE_BLEND_ONE:
        *out = V9X_GLIDE_D3D_BLEND_ONE;
        return V9X_TRUE;
    case V9X_GLIDE_BLEND_ONE_MINUS_SRC_ALPHA:
        *out = V9X_GLIDE_D3D_BLEND_INVSRCALPHA;
        return V9X_TRUE;
    case V9X_GLIDE_BLEND_ONE_MINUS_COLOR:
        *out = destination ? V9X_GLIDE_D3D_BLEND_INVSRCCOLOR :
                             V9X_GLIDE_D3D_BLEND_INVDESTCOLOR;
        return V9X_TRUE;
    case V9X_GLIDE_BLEND_ONE_MINUS_DST_ALPHA:
        *out = V9X_GLIDE_D3D_BLEND_INVDESTALPHA;
        return V9X_TRUE;
    case V9X_GLIDE_BLEND_ALPHA_SATURATE:
        if (!destination) {
            *out = V9X_GLIDE_D3D_BLEND_SRCALPHASAT;
            return V9X_TRUE;
        }
        break;
    default:
        break;
    }
    *out = V9X_GLIDE_D3D_BLEND_ONE;
    return V9X_FALSE;
}

/*
 * What a colour or alpha combine produces, among the forms the interface
 * can draw: the local value (LOCAL), the other value (SCALE_OTHER by ONE),
 * or the texture scaled by the local value (SCALE_OTHER by LOCAL, other
 * TEXTURE). Everything else, and any inverted output, is UNKNOWN.
 */
static unsigned int v9x_glide_combine_result(const V9X_GLIDE_COMBINE *c)
{
    v9x_u32 local_constant = c->local == V9X_GLIDE_COMBINE_LOCAL_CONSTANT;

    if (c->invert || c->local > V9X_GLIDE_COMBINE_LOCAL_CONSTANT) {
        return V9X_GLIDE_RESULT_UNKNOWN;
    }
    if (c->function == V9X_GLIDE_COMBINE_FUNCTION_LOCAL) {
        return local_constant ? V9X_GLIDE_RESULT_CONSTANT :
                                V9X_GLIDE_RESULT_ITERATED;
    }
    if (c->function != V9X_GLIDE_COMBINE_FUNCTION_SCALE_OTHER) {
        return V9X_GLIDE_RESULT_UNKNOWN;
    }
    if (c->factor == V9X_GLIDE_COMBINE_FACTOR_ONE) {
        if (c->other == V9X_GLIDE_COMBINE_OTHER_TEXTURE) {
            return V9X_GLIDE_RESULT_TEXTURE;
        }
        if (c->other == V9X_GLIDE_COMBINE_OTHER_CONSTANT) {
            return V9X_GLIDE_RESULT_CONSTANT;
        }
        return V9X_GLIDE_RESULT_ITERATED;
    }
    if (c->factor == V9X_GLIDE_COMBINE_FACTOR_LOCAL &&
        c->other == V9X_GLIDE_COMBINE_OTHER_TEXTURE) {
        return local_constant ? V9X_GLIDE_RESULT_TEXTURE_CONSTANT :
                                V9X_GLIDE_RESULT_TEXTURE_ITERATED;
    }
    return V9X_GLIDE_RESULT_UNKNOWN;
}

static v9x_u16 v9x_glide_result_textured(unsigned int result)
{
    return (result == V9X_GLIDE_RESULT_TEXTURE ||
            result == V9X_GLIDE_RESULT_TEXTURE_ITERATED ||
            result == V9X_GLIDE_RESULT_TEXTURE_CONSTANT) ? V9X_TRUE : V9X_FALSE;
}

static v9x_u32 v9x_glide_result_source(unsigned int result)
{
    return (result == V9X_GLIDE_RESULT_CONSTANT ||
            result == V9X_GLIDE_RESULT_TEXTURE_CONSTANT) ?
           V9X_GLIDE_SOURCE_CONSTANT : V9X_GLIDE_SOURCE_ITERATED;
}

static void v9x_glide_map_combine(const V9X_GLIDE_STATE *state,
                                  V9X_GLIDE_DRAW_SETUP *out)
{
    unsigned int color = v9x_glide_combine_result(&state->color);
    unsigned int alpha = v9x_glide_combine_result(&state->alpha);

    if (color == V9X_GLIDE_RESULT_UNKNOWN || alpha == V9X_GLIDE_RESULT_UNKNOWN) {
        out->recognized = V9X_FALSE;
    }

    /* Closest for an unknown unit: the texture modulated if the unit names
     * the texture at all, the vertex value otherwise. */
    if (color == V9X_GLIDE_RESULT_UNKNOWN) {
        color = state->color.other == V9X_GLIDE_COMBINE_OTHER_TEXTURE ?
                V9X_GLIDE_RESULT_TEXTURE_ITERATED : V9X_GLIDE_RESULT_ITERATED;
    }
    if (alpha == V9X_GLIDE_RESULT_UNKNOWN) {
        alpha = state->alpha.other == V9X_GLIDE_COMBINE_OTHER_TEXTURE ?
                V9X_GLIDE_RESULT_TEXTURE_ITERATED : V9X_GLIDE_RESULT_ITERATED;
    }

    out->color_source = v9x_glide_result_source(color);
    out->alpha_source = v9x_glide_result_source(alpha);
    out->textured = (v9x_glide_result_textured(color) ||
                     v9x_glide_result_textured(alpha)) ? V9X_TRUE : V9X_FALSE;
    if (!out->textured) {
        return;
    }

    /* The interface's colour op always involves the texel: a colour taken
     * from the vertex alone beside a textured alpha has no exact form. */
    if (color == V9X_GLIDE_RESULT_TEXTURE) {
        out->color_op = V9X_R3D_ABI_COLOROP_REPLACE;
    } else {
        out->color_op = V9X_R3D_ABI_COLOROP_MODULATE;
        if (!v9x_glide_result_textured(color)) {
            out->recognized = V9X_FALSE;
        }
    }
    if (alpha == V9X_GLIDE_RESULT_TEXTURE) {
        out->alpha_op = V9X_R3D_ABI_ALPHAOP_REPLACE;
    } else if (v9x_glide_result_textured(alpha)) {
        out->alpha_op = V9X_R3D_ABI_ALPHAOP_MODULATE;
    } else {
        out->alpha_op = V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    }

    /* The TMU must pass its texel through for "texture" to mean the texel. */
    if (state->tex_rgb_function != V9X_GLIDE_COMBINE_FUNCTION_LOCAL ||
        state->tex_alpha_function != V9X_GLIDE_COMBINE_FUNCTION_LOCAL) {
        out->recognized = V9X_FALSE;
    }
}

static v9x_u32 v9x_glide_clamp(v9x_u32 value, v9x_u32 limit)
{
    return value > limit ? limit : value;
}

static void v9x_glide_map_scissor(const V9X_GLIDE_STATE *state,
                                  V9X_R3D_ABI_STATE *out)
{
    v9x_u32 min_y = v9x_glide_clamp(state->clip_min_y, state->height);
    v9x_u32 max_y = v9x_glide_clamp(state->clip_max_y, state->height);

    out->scissor_left = v9x_glide_clamp(state->clip_min_x, state->width);
    out->scissor_right = v9x_glide_clamp(state->clip_max_x, state->width);
    if (state->origin == V9X_GLIDE_ORIGIN_LOWER_LEFT) {
        out->scissor_top = state->height - max_y;
        out->scissor_bottom = state->height - min_y;
    } else {
        out->scissor_top = min_y;
        out->scissor_bottom = max_y;
    }
}

void v9x_glide_state_map(const V9X_GLIDE_STATE *state,
                         V9X_GLIDE_DRAW_SETUP *out)
{
    V9X_R3D_ABI_STATE *abi = &out->state;
    v9x_u32 source;

    out->recognized = V9X_TRUE;
    out->textured = V9X_FALSE;
    out->color_op = V9X_R3D_ABI_COLOROP_MODULATE;
    out->alpha_op = V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    out->key_texture = V9X_FALSE;

    abi->depth_enable = state->depth_mode != V9X_GLIDE_DEPTH_DISABLE ? 1ul : 0ul;
    abi->depth_write = abi->depth_enable && state->depth_mask ? 1ul : 0ul;
    abi->depth_func = (state->depth_func & 7ul) + V9X_GLIDE_D3D_CMP_FROM_GLIDE;

    if (!v9x_glide_blend_factor(state->blend_src, V9X_FALSE, &abi->src_blend) ||
        !v9x_glide_blend_factor(state->blend_dst, V9X_TRUE, &abi->dst_blend)) {
        out->recognized = V9X_FALSE;
    }
    abi->blend_enable = (state->blend_src == V9X_GLIDE_BLEND_ONE &&
                         state->blend_dst == V9X_GLIDE_BLEND_ZERO) ? 0ul : 1ul;

    abi->alpha_test_enable = state->alpha_func != V9X_GLIDE_CMP_ALWAYS ? 1ul : 0ul;
    abi->alpha_func = (state->alpha_func & 7ul) + V9X_GLIDE_D3D_CMP_FROM_GLIDE;
    abi->alpha_ref = state->alpha_ref & 0xFFul;

    source = state->fog_mode & V9X_GLIDE_FOG_SOURCE_MASK;
    abi->fog_enable = (source == V9X_GLIDE_FOG_TABLE ||
                       source == V9X_GLIDE_FOG_ITERATED_ALPHA) ? 1ul : 0ul;
    abi->fog_color = state->fog_color & 0x00FFFFFFul;
    abi->write_mask = V9X_R3D_ABI_WRITE_RGB;
    v9x_glide_map_scissor(state, abi);

    v9x_glide_map_combine(state, out);

    /* The chroma-key approximation (glide_texfmt.h): keyed texels arrive
     * with alpha 0, and a game that left the alpha test off gets one that
     * discards exactly those. */
    if (state->chroma_mode == V9X_GLIDE_CHROMAKEY_ENABLE && out->textured) {
        out->key_texture = V9X_TRUE;
        if (!abi->alpha_test_enable) {
            abi->alpha_test_enable = 1ul;
            abi->alpha_func = V9X_GLIDE_D3D_CMP_GREATER;
            abi->alpha_ref = 0ul;
        }
    }

    out->address = (state->clamp_s == V9X_GLIDE_TEXTURE_CLAMP &&
                    state->clamp_t == V9X_GLIDE_TEXTURE_CLAMP) ?
                   V9X_R3D_ABI_ADDRESS_CLAMP : V9X_R3D_ABI_ADDRESS_WRAP;
    if (state->clamp_s != state->clamp_t && out->textured) {
        out->recognized = V9X_FALSE;
    }
    out->min_filter = state->min_filter == V9X_GLIDE_TEXTURE_POINT ?
                      V9X_R3D_ABI_FILTER_NEAREST : V9X_R3D_ABI_FILTER_LINEAR;
    out->mag_filter = state->mag_filter == V9X_GLIDE_TEXTURE_POINT ?
                      V9X_R3D_ABI_FILTER_NEAREST : V9X_R3D_ABI_FILTER_LINEAR;
}
