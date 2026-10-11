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
    state->stw_hint = 0ul;
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
 * TEXTURE). On the alpha unit, also the other value scaled by the texel's
 * alpha (SCALE_OTHER by TEXTURE_ALPHA), which there is the texture scaled by
 * the other value: Carmageddon II's menu text takes its glyph mask that way,
 * and read as UNKNOWN every glyph drew as a solid quad (netbook,
 * 2026-10-11). On the colour unit that factor scales a colour by an alpha,
 * which no op says. Everything else, and any inverted output, is UNKNOWN.
 */
static unsigned int v9x_glide_combine_result(const V9X_GLIDE_COMBINE *c,
                                             v9x_u16 alpha_unit)
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
    if (alpha_unit && c->factor == V9X_GLIDE_COMBINE_FACTOR_TEXTURE_ALPHA) {
        if (c->other == V9X_GLIDE_COMBINE_OTHER_CONSTANT) {
            return V9X_GLIDE_RESULT_TEXTURE_CONSTANT;
        }
        if (c->other == V9X_GLIDE_COMBINE_OTHER_ITERATED) {
            return V9X_GLIDE_RESULT_TEXTURE_ITERATED;
        }
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

static v9x_u16 v9x_glide_blend_reads_alpha(v9x_u32 factor);

static void v9x_glide_map_combine(const V9X_GLIDE_STATE *state,
                                  V9X_GLIDE_DRAW_SETUP *out)
{
    unsigned int color = v9x_glide_combine_result(&state->color, V9X_FALSE);
    unsigned int alpha = v9x_glide_combine_result(&state->alpha, V9X_TRUE);
    v9x_u16 alpha_read = (state->alpha_func != V9X_GLIDE_CMP_ALWAYS ||
                          v9x_glide_blend_reads_alpha(state->blend_src) ||
                          v9x_glide_blend_reads_alpha(state->blend_dst)) ?
                         V9X_TRUE : V9X_FALSE;

    /* An alpha nothing reads cannot make a draw textured. Carmageddon II
     * leaves its alpha as the texture's for opaque geometry with no
     * texture source; taken as textured, that geometry was skipped and the
     * screen went black (netbook, 2026-10-11). The chroma key, which does
     * read the texture's alpha, is applied after this (state_map). */
    if (!alpha_read && alpha != V9X_GLIDE_RESULT_UNKNOWN &&
        v9x_glide_result_textured(alpha) && !v9x_glide_result_textured(color)) {
        alpha = V9X_GLIDE_RESULT_ITERATED;
    }

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

/* GrScreenResolution_t, GR_RESOLUTION_320x200 (0) to _400x300 (15). */
static const v9x_u16 v9x_glide_resolutions[][2] = {
    { 320u, 200u }, { 320u, 240u }, { 400u, 256u }, { 512u, 384u },
    { 640u, 200u }, { 640u, 350u }, { 640u, 400u }, { 640u, 480u },
    { 800u, 600u }, { 960u, 720u }, { 856u, 480u }, { 512u, 256u },
    { 1024u, 768u }, { 1280u, 1024u }, { 1600u, 1200u }, { 400u, 300u }
};

#define V9X_GLIDE_RESOLUTIONS \
    (sizeof(v9x_glide_resolutions) / sizeof(v9x_glide_resolutions[0]))

v9x_u16 v9x_glide_resolution_size(v9x_u32 resolution, v9x_u32 *width,
                                  v9x_u32 *height)
{
    if (resolution >= V9X_GLIDE_RESOLUTIONS) {
        return V9X_FALSE;
    }
    *width = v9x_glide_resolutions[resolution][0];
    *height = v9x_glide_resolutions[resolution][1];
    return V9X_TRUE;
}

/* GrColorFormat_t: ARGB 0, ABGR 1, RGBA 2, BGRA 3. */
#define V9X_GLIDE_COLORFORMAT_ABGR 1ul
#define V9X_GLIDE_COLORFORMAT_RGBA 2ul
#define V9X_GLIDE_COLORFORMAT_BGRA 3ul

v9x_u32 v9x_glide_color_to_argb(v9x_u32 color, v9x_u32 color_format)
{
    if (color_format == V9X_GLIDE_COLORFORMAT_ABGR) {
        return (color & 0xFF00FF00ul) | ((color & 0xFFul) << 16) |
               ((color >> 16) & 0xFFul);
    }
    if (color_format == V9X_GLIDE_COLORFORMAT_RGBA) {
        return (color >> 8) | (color << 24);
    }
    if (color_format == V9X_GLIDE_COLORFORMAT_BGRA) {
        return ((color & 0xFFul) << 24) | (((color >> 8) & 0xFFul) << 16) |
               (((color >> 16) & 0xFFul) << 8) | (color >> 24);
    }
    return color;
}

/* ABGR and BGRA swap channels symmetrically, so they are their own
 * inverses; RGBA rotates the other way. */
v9x_u32 v9x_glide_argb_to_color(v9x_u32 argb, v9x_u32 color_format)
{
    if (color_format == V9X_GLIDE_COLORFORMAT_RGBA) {
        return (argb << 8) | (argb >> 24);
    }
    return v9x_glide_color_to_argb(argb, color_format);
}

/*
 * guColorCombineFunction's presets, GR_COLORCOMBINE_ZERO (0) to _ONE (16),
 * as the grColorCombine arguments the Glide 2.x utility library passes:
 * function, factor, local, other, invert. Written from the Glide 2.x
 * headers' enumerations and the utility's documented table, not read from a
 * 3dfx binary; the presets Carmageddon II uses are the textured ones, whose
 * effect on the screen is the check. NONE is ZERO for the factor, CONSTANT
 * for local and other, as glide.h defines it.
 */
#define V9X_GLIDE_CF_SCALE_OTHER_ADD_LOCAL        4u
#define V9X_GLIDE_CF_SCALE_OTHER_ADD_LOCAL_ALPHA  5u
#define V9X_GLIDE_CF_SCALE_OTHER_MINUS_LOCAL      6u
#define V9X_GLIDE_CF_BLEND                        7u
#define V9X_GLIDE_CX_LOCAL_ALPHA                  3u
#define V9X_GLIDE_CX_TEXTURE_ALPHA                4u

static const v9x_u8 v9x_glide_gu_presets[17][5] = {
    { 0u, 0u, 1u, 2u, 0u },     /* ZERO */
    { 1u, 0u, 1u, 2u, 0u },     /* CCRGB: the constant */
    { 1u, 0u, 0u, 2u, 0u },     /* ITRGB: the vertex */
    { 1u, 0u, 0u, 2u, 0u },     /* ITRGB_DELTA0 */
    { 3u, 8u, 1u, 1u, 0u },     /* DECAL_TEXTURE */
    { 3u, 1u, 1u, 1u, 0u },     /* TEXTURE_TIMES_CCRGB */
    { 3u, 1u, 0u, 1u, 0u },     /* TEXTURE_TIMES_ITRGB */
    { 3u, 1u, 0u, 1u, 0u },     /* TEXTURE_TIMES_ITRGB_DELTA0 */
    { V9X_GLIDE_CF_SCALE_OTHER_ADD_LOCAL_ALPHA, 1u, 0u, 1u, 0u },
                                /* TEXTURE_TIMES_ITRGB_ADD_ALPHA */
    { 3u, V9X_GLIDE_CX_LOCAL_ALPHA, 0u, 1u, 0u },
                                /* TEXTURE_TIMES_ALPHA */
    { V9X_GLIDE_CF_SCALE_OTHER_ADD_LOCAL, V9X_GLIDE_CX_LOCAL_ALPHA, 0u, 1u, 0u },
                                /* TEXTURE_TIMES_ALPHA_ADD_ITRGB */
    { V9X_GLIDE_CF_SCALE_OTHER_ADD_LOCAL, 8u, 0u, 1u, 0u },
                                /* TEXTURE_ADD_ITRGB */
    { V9X_GLIDE_CF_SCALE_OTHER_MINUS_LOCAL, 8u, 0u, 1u, 0u },
                                /* TEXTURE_SUB_ITRGB */
    { V9X_GLIDE_CF_BLEND, V9X_GLIDE_CX_TEXTURE_ALPHA, 0u, 2u, 0u },
                                /* CCRGB_BLEND_ITRGB_ON_TEXALPHA */
    { V9X_GLIDE_CF_BLEND, V9X_GLIDE_CX_LOCAL_ALPHA, 0u, 1u, 0u },
                                /* DIFF_SPEC_A */
    { V9X_GLIDE_CF_SCALE_OTHER_ADD_LOCAL, V9X_GLIDE_CX_LOCAL_ALPHA, 0u, 1u, 0u },
                                /* DIFF_SPEC_B */
    { 0u, 0u, 1u, 2u, 1u }      /* ONE: zero, inverted */
};

v9x_u16 v9x_glide_gu_color_combine(v9x_u32 preset, V9X_GLIDE_COMBINE *out)
{
    const v9x_u8 *row;

    if (preset >= sizeof(v9x_glide_gu_presets) / sizeof(v9x_glide_gu_presets[0])) {
        return V9X_FALSE;
    }
    row = v9x_glide_gu_presets[preset];
    out->function = row[0];
    out->factor = row[1];
    out->local = row[2];
    out->other = row[3];
    out->invert = row[4];
    return V9X_TRUE;
}

/* Whether a blend factor reads the source alpha. Destination alpha is
 * not the fragment's, and the 16-bit targets have none. */
static v9x_u16 v9x_glide_blend_reads_alpha(v9x_u32 factor)
{
    return (factor == V9X_GLIDE_BLEND_SRC_ALPHA ||
            factor == V9X_GLIDE_BLEND_ONE_MINUS_SRC_ALPHA ||
            factor == V9X_GLIDE_BLEND_ALPHA_SATURATE) ? V9X_TRUE : V9X_FALSE;
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
    out->key_alpha = V9X_FALSE;

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
     * discards exactly those. Glide keys before the alpha combine, so the
     * texture's alpha must reach the test even when the combine takes
     * alpha from the vertex or a constant: alone when nothing else reads
     * the source alpha, scaled by the fragment's when a blend or the
     * game's own test does (an unkeyed texel's alpha is one). Diablo II
     * keys with a ZERO or constant alpha combine; the fragment's alpha
     * alone kept every keyed texel, and the Rage XL refused the draw
     * (A8U4I5, 2026-10-10). That holds only where the converted alpha is
     * the key alone; key_alpha tells the texture bind, which knows the
     * format, to keep the fragment's alpha for a format with its own. */
    if (state->chroma_mode == V9X_GLIDE_CHROMAKEY_ENABLE && out->textured) {
        out->key_texture = V9X_TRUE;
        if (out->alpha_op == V9X_R3D_ABI_ALPHAOP_FRAGMENT) {
            out->key_alpha = V9X_TRUE;
            out->alpha_op = (abi->alpha_test_enable ||
                             v9x_glide_blend_reads_alpha(state->blend_src) ||
                             v9x_glide_blend_reads_alpha(state->blend_dst)) ?
                            V9X_R3D_ABI_ALPHAOP_MODULATE :
                            V9X_R3D_ABI_ALPHAOP_REPLACE;
        }
        if (!abi->alpha_test_enable) {
            abi->alpha_test_enable = 1ul;
            abi->alpha_func = V9X_GLIDE_D3D_CMP_GREATER;
            abi->alpha_ref = 0ul;
        }
    }

    /* The texture's colour alone beside an alpha nothing reads (no blend on
     * it, no test, no key): the alpha may as well be the texel's, which makes
     * the op DECAL, one every single-unit engine has. Gen3 refused
     * Carmageddon II's menu, REPLACE beside the fragment's alpha on RGB565,
     * as UNSUPPORTED (netbook, 2026-10-11). */
    if (out->textured && out->color_op == V9X_R3D_ABI_COLOROP_REPLACE &&
        out->alpha_op == V9X_R3D_ABI_ALPHAOP_FRAGMENT &&
        !abi->alpha_test_enable &&
        !v9x_glide_blend_reads_alpha(state->blend_src) &&
        !v9x_glide_blend_reads_alpha(state->blend_dst)) {
        out->alpha_op = V9X_R3D_ABI_ALPHAOP_REPLACE;
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
