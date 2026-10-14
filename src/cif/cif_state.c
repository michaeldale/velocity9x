/*
 * ATI3DCIF rendering-context state to render-interface state
 * (docs\plans\ati-3dcif-wrapper.md, Phase 2). See cif_state.h. The
 * interface takes Direct3D's numbers for compare functions and blend
 * factors (r3d_abi.h).
 */
#include "cif_state.h"

/* D3DCMPFUNC for each C3D_EZCMP. The orders differ: CIF runs NEVER, LESS,
 * LEQUAL, EQUAL, GEQUAL, GREATER, NOTEQUAL, ALWAYS (0..7); Direct3D puts
 * EQUAL (3) before LESSEQUAL (4) and GREATER (5) before GREATEREQUAL (7),
 * so an offset of one would swap two pairs. */
static const v9x_u32 v9x_cif_d3d_cmp[V9X_C3D_EZCMP_ALWAYS + 1ul] = {
    1ul, 2ul, 4ul, 3ul, 7ul, 5ul, 6ul, 8ul
};
#define V9X_CIF_D3D_CMP_GREATER  5ul

/* D3DBLEND. */
#define V9X_CIF_D3D_BLEND_ZERO         1ul
#define V9X_CIF_D3D_BLEND_ONE          2ul
#define V9X_CIF_D3D_BLEND_SRCCOLOR     3ul
#define V9X_CIF_D3D_BLEND_INVSRCCOLOR  4ul
#define V9X_CIF_D3D_BLEND_SRCALPHA     5ul
#define V9X_CIF_D3D_BLEND_INVSRCALPHA  6ul
#define V9X_CIF_D3D_BLEND_DESTALPHA    7ul
#define V9X_CIF_D3D_BLEND_INVDESTALPHA 8ul
#define V9X_CIF_D3D_BLEND_DESTCOLOR    9ul
#define V9X_CIF_D3D_BLEND_INVDESTCOLOR 10ul

/* ATI3DCIF.H's default for each state id, 0..36: vertex type VTCF,
 * triangles, smooth shading, perspective THREE, MINPNT_MAG2BY2, source ONE,
 * destination ZERO, dither on, Z compare ALWAYS, composite function MAX
 * (3), composite factor 8, composite filter MIN2BY2_MAG2BY2, destination-
 * alpha test ALWAYS. Everything else is 0, the surfaces included. */
static const v9x_u32 v9x_cif_state_defaults[V9X_C3D_ERS_NUM] = {
    0ul, 3ul, 1ul, 0ul, 3ul, 0ul, 0ul, 0ul, 3ul, 1ul,
    0ul, 1ul, 0ul, 0ul, 0ul, 0ul, 0ul, 0ul, 1ul, 7ul,
    0ul, 0ul, 0ul, 0ul, 0ul, 0ul, 3ul, 8ul, 2ul, 0ul,
    0ul, 0ul, 7ul, 0ul, 0ul, 0ul, 0ul
};

void v9x_cif_state_init(V9X_CIF_STATE *state)
{
    unsigned int index;

    for (index = 0u; index < V9X_C3D_ERS_NUM; ++index) {
        state->value[index] = v9x_cif_state_defaults[index];
    }
    for (index = 0u; index < 4u; ++index) {
        state->viewport[index] = 0;
        state->scissor[index] = 0;
    }
    state->viewport_set = 0ul;
    state->scissor_set = 0ul;
}

v9x_u32 v9x_cif_state_set(V9X_CIF_STATE *state, v9x_u32 id, const void *data)
{
    const v9x_s32 *rect = (const v9x_s32 *)data;
    v9x_s32 *stored;
    unsigned int index;

    if (id >= V9X_C3D_ERS_NUM || data == 0) {
        return V9X_C3D_EC_BADPARAM;
    }
    if (id == V9X_C3D_ERS_SURF_VPORT || id == V9X_C3D_ERS_SURF_SCISSOR) {
        stored = id == V9X_C3D_ERS_SURF_VPORT ? state->viewport
                                              : state->scissor;
        for (index = 0u; index < 4u; ++index) {
            stored[index] = rect[index];
        }
        if (id == V9X_C3D_ERS_SURF_VPORT) {
            state->viewport_set = 1ul;
        } else {
            state->scissor_set = 1ul;
        }
        return V9X_C3D_EC_OK;
    }
    state->value[id] = *(const v9x_u32 *)data;
    return V9X_C3D_EC_OK;
}

v9x_u32 v9x_cif_color_to_argb(v9x_u32 color)
{
    return (V9X_C3D_COLOR_ALPHA(color) << 24) |
           (V9X_C3D_COLOR_RED(color) << 16) |
           (V9X_C3D_COLOR_GREEN(color) << 8) |
           V9X_C3D_COLOR_BLUE(color);
}

/*
 * A C3D blend factor in Direct3D's numbering. CIF's 2 and 3 are the
 * destination's colour on the source side and the source's on the
 * destination side, as Glide's GR_BLEND_COLOR is. A value past the eight
 * reports V9X_FALSE and blends as ONE.
 */
static v9x_u16 v9x_cif_blend_factor(v9x_u32 factor, v9x_u16 destination,
                                    v9x_u32 *out)
{
    switch (factor) {
    case V9X_C3D_EBLEND_ZERO:
        *out = V9X_CIF_D3D_BLEND_ZERO;
        return V9X_TRUE;
    case V9X_C3D_EBLEND_ONE:
        *out = V9X_CIF_D3D_BLEND_ONE;
        return V9X_TRUE;
    case V9X_C3D_EBLEND_COLOR:
        *out = destination ? V9X_CIF_D3D_BLEND_SRCCOLOR
                           : V9X_CIF_D3D_BLEND_DESTCOLOR;
        return V9X_TRUE;
    case V9X_C3D_EBLEND_INVCOLOR:
        *out = destination ? V9X_CIF_D3D_BLEND_INVSRCCOLOR
                           : V9X_CIF_D3D_BLEND_INVDESTCOLOR;
        return V9X_TRUE;
    case V9X_C3D_EBLEND_SRCALPHA:
        *out = V9X_CIF_D3D_BLEND_SRCALPHA;
        return V9X_TRUE;
    case V9X_C3D_EBLEND_INVSRCALPHA:
        *out = V9X_CIF_D3D_BLEND_INVSRCALPHA;
        return V9X_TRUE;
    case V9X_C3D_EBLEND_DSTALPHA:
        *out = V9X_CIF_D3D_BLEND_DESTALPHA;
        return V9X_TRUE;
    case V9X_C3D_EBLEND_INVDSTALPHA:
        *out = V9X_CIF_D3D_BLEND_INVDESTALPHA;
        return V9X_TRUE;
    default:
        break;
    }
    *out = V9X_CIF_D3D_BLEND_ONE;
    return V9X_FALSE;
}

/*
 * C3D_ETEXFILTER as minification, magnification and level selection.
 * The MIP modes blend between levels ("1x1 blend between maps", white
 * paper) and filter within one as their MIN part says; MIPTRI is the
 * trilinear one. The other modes, on a mipmapped texture, are taken to
 * pick the nearest level: the guide says only that MIP modes need a
 * mipmap, not that the others ignore one. Phase 3's probe checks it.
 */
static void v9x_cif_filter(v9x_u32 filter, v9x_u32 mipmapped,
                           V9X_CIF_DRAW_SETUP *out)
{
    v9x_u32 between = V9X_R3D_ABI_MIP_POINT;

    out->min_filter = V9X_R3D_ABI_FILTER_NEAREST;
    out->mag_filter = V9X_R3D_ABI_FILTER_LINEAR;
    switch (filter) {
    case V9X_C3D_ETFILT_MINPNT_MAGPNT:
        out->mag_filter = V9X_R3D_ABI_FILTER_NEAREST;
        break;
    case V9X_C3D_ETFILT_MINPNT_MAG2BY2:
        break;
    case V9X_C3D_ETFILT_MIN2BY2_MAG2BY2:
        out->min_filter = V9X_R3D_ABI_FILTER_LINEAR;
        break;
    case V9X_C3D_ETFILT_MIPLIN_MAGPNT:
        out->mag_filter = V9X_R3D_ABI_FILTER_NEAREST;
        between = V9X_R3D_ABI_MIP_LINEAR;
        break;
    case V9X_C3D_ETFILT_MIPLIN_MAG2BY2:
        between = V9X_R3D_ABI_MIP_LINEAR;
        break;
    case V9X_C3D_ETFILT_MIPTRI_MAG2BY2:
        out->min_filter = V9X_R3D_ABI_FILTER_LINEAR;
        between = V9X_R3D_ABI_MIP_LINEAR;
        break;
    case V9X_C3D_ETFILT_MIN2BY2_MAGPNT:
        out->min_filter = V9X_R3D_ABI_FILTER_LINEAR;
        out->mag_filter = V9X_R3D_ABI_FILTER_NEAREST;
        break;
    default:
        out->recognized = V9X_FALSE;
        break;
    }
    out->mip = mipmapped ? between : V9X_R3D_ABI_MIP_NONE;
}

/* The texel op, for a textured draw. Chroma key: keyed texels become
 * alpha 0 (cif_texfmt.c) and the alpha test drops them; MODULATE keeps
 * the fragment's alpha for the rest. ALPHA hands the texel's alpha to the
 * blender. ALPHA_MASK ("bit 0: tex not drawn, otherwise alpha
 * interpolated") is approximated by the same test on the texel's alpha
 * times the fragment's, which also drops a fragment alpha of 0. */
static void v9x_cif_texel_op(v9x_u32 texop, V9X_CIF_DRAW_SETUP *out)
{
    switch (texop) {
    case V9X_C3D_ETEXOP_NONE:
        break;
    case V9X_C3D_ETEXOP_CHROMAKEY:
        out->key_texture = 1ul;
        out->alpha_op = V9X_R3D_ABI_ALPHAOP_MODULATE;
        out->state.alpha_test_enable = 1ul;
        break;
    case V9X_C3D_ETEXOP_ALPHA:
        out->alpha_op = V9X_R3D_ABI_ALPHAOP_REPLACE;
        break;
    case V9X_C3D_ETEXOP_ALPHA_MASK:
        out->alpha_op = V9X_R3D_ABI_ALPHAOP_MODULATE;
        out->state.alpha_test_enable = 1ul;
        out->recognized = V9X_FALSE;
        break;
    default:
        out->recognized = V9X_FALSE;
        break;
    }
}

/* Cuts the half-open [left, right) x [top, bottom) by a C3D_RECT, whose
 * bottom and right are taken as corner coordinates, exclusive. The guide
 * disagrees with itself here (chapter 2 calls them extents, the reference
 * corners); the reference is followed, and no demo sets either rectangle. */
static void v9x_cif_cut(const v9x_s32 *rect, v9x_s32 *left, v9x_s32 *top,
                        v9x_s32 *right, v9x_s32 *bottom)
{
    if (rect[V9X_CIF_RECT_LEFT] > *left) {
        *left = rect[V9X_CIF_RECT_LEFT];
    }
    if (rect[V9X_CIF_RECT_TOP] > *top) {
        *top = rect[V9X_CIF_RECT_TOP];
    }
    if (rect[V9X_CIF_RECT_RIGHT] < *right) {
        *right = rect[V9X_CIF_RECT_RIGHT];
    }
    if (rect[V9X_CIF_RECT_BOTTOM] < *bottom) {
        *bottom = rect[V9X_CIF_RECT_BOTTOM];
    }
}

/* A clipped edge as the interface's unsigned coordinate, never negative. */
static v9x_u32 v9x_cif_edge(v9x_s32 value)
{
    return value > 0 ? (v9x_u32)value : 0ul;
}

void v9x_cif_state_map(const V9X_CIF_STATE *state, v9x_u32 width,
                       v9x_u32 height, const V9X_CIF_TEXTURE_INFO *texture,
                       V9X_CIF_DRAW_SETUP *out)
{
    const v9x_u32 *value = state->value;
    v9x_s32 left = 0;
    v9x_s32 top = 0;
    v9x_s32 right = (v9x_s32)width;
    v9x_s32 bottom = (v9x_s32)height;
    v9x_u32 z_mode = value[V9X_C3D_ERS_Z_MODE];
    v9x_u32 z_func = value[V9X_C3D_ERS_Z_CMP_FNC];
    v9x_u32 light = value[V9X_C3D_ERS_TMAP_LIGHT];

    out->recognized = V9X_TRUE;

    out->state.depth_enable = z_mode != V9X_C3D_EZMODE_OFF ? 1ul : 0ul;
    out->state.depth_write = z_mode == V9X_C3D_EZMODE_TESTON_WRITEZ ? 1ul : 0ul;
    if (z_func > V9X_C3D_EZCMP_ALWAYS) {
        z_func = V9X_C3D_EZCMP_ALWAYS;
        out->recognized = V9X_FALSE;
    }
    out->state.depth_func = v9x_cif_d3d_cmp[z_func];
    if (z_mode > V9X_C3D_EZMODE_TESTON_WRITEZ) {
        out->recognized = V9X_FALSE;
    }

    if (!v9x_cif_blend_factor(value[V9X_C3D_ERS_ALPHA_SRC], V9X_FALSE,
                              &out->state.src_blend) ||
        !v9x_cif_blend_factor(value[V9X_C3D_ERS_ALPHA_DST], V9X_TRUE,
                              &out->state.dst_blend)) {
        out->recognized = V9X_FALSE;
    }
    out->state.blend_enable =
        (out->state.src_blend == V9X_CIF_D3D_BLEND_ONE &&
         out->state.dst_blend == V9X_CIF_D3D_BLEND_ZERO) ? 0ul : 1ul;

    /* Fog and alpha blending exclude each other on the Rage (guide,
     * "Applying Fog"): with fog on the alpha is the fog factor, so a blend
     * set beside it cannot be honoured. */
    out->fog = value[V9X_C3D_ERS_FOG_EN] != 0ul ? 1ul : 0ul;
    out->state.fog_enable = out->fog;
    out->state.fog_color =
        v9x_cif_color_to_argb(value[V9X_C3D_ERS_FG_CLR]) & 0x00fffffful;
    if (out->fog && out->state.blend_enable) {
        out->state.blend_enable = 0ul;
        out->recognized = V9X_FALSE;
    }

    out->state.alpha_test_enable = 0ul;
    out->state.alpha_func = V9X_CIF_D3D_CMP_GREATER;
    out->state.alpha_ref = 0ul;
    out->state.write_mask = V9X_R3D_ABI_WRITE_RGB;

    out->textured = (value[V9X_C3D_ERS_TMAP_EN] != 0ul &&
                     value[V9X_C3D_ERS_TMAP_SELECT] != 0ul) ? 1ul : 0ul;
    out->key_texture = 0ul;
    out->texel_alpha = 0ul;
    out->white = 0ul;
    out->alpha_op = V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    out->color_op = V9X_R3D_ABI_COLOROP_REPLACE;
    out->address = V9X_R3D_ABI_ADDRESS_WRAP;
    v9x_cif_filter(value[V9X_C3D_ERS_TMAP_FILTER],
                   texture != 0 ? texture->mipmapped : 0ul, out);
    if (out->textured) {
        if (light == V9X_C3D_ETL_MODULATE) {
            out->color_op = V9X_R3D_ABI_COLOROP_MODULATE;
        } else if (light == V9X_C3D_ETL_ALPHA_DECAL) {
            out->color_op = V9X_R3D_ABI_COLOROP_DECALALPHA;
        } else if (light != V9X_C3D_ETL_NONE) {
            out->recognized = V9X_FALSE;
        }
        v9x_cif_texel_op(value[V9X_C3D_ERS_TMAP_TEXOP], out);

        /* Whether the texture's own alpha is read: the texel ops that
         * pass it on, and decal, which blends by it. Otherwise the DLL
         * draws an opaque copy, because the engines take MODULATE with
         * the fragment's alpha only on a texture without alpha. */
        out->texel_alpha =
            (value[V9X_C3D_ERS_TMAP_TEXOP] == V9X_C3D_ETEXOP_ALPHA ||
             value[V9X_C3D_ERS_TMAP_TEXOP] == V9X_C3D_ETEXOP_ALPHA_MASK ||
             out->color_op == V9X_R3D_ABI_COLOROP_DECALALPHA) ? 1ul : 0ul;

        /* The texel's colour with an alpha other than the texel's is no
         * D3D texture op, and the engines implement only those
         * (d3d_core.c, v9x_r3d_texture_op). MODULATE by a white vertex is
         * the same colour exactly, and keeps the alpha op. */
        if (out->color_op == V9X_R3D_ABI_COLOROP_REPLACE &&
            out->alpha_op != V9X_R3D_ABI_ALPHAOP_REPLACE) {
            out->color_op = V9X_R3D_ABI_COLOROP_MODULATE;
            out->white = 1ul;
        }

        /* A keyed texel's alpha is 0 after MODULATE, so SRCALPHA,
         * INVSRCALPHA blends it away; without a depth write the alpha
         * test then adds nothing, and the Rage IIC refuses a keyed,
         * blended draw that keeps it (A8U4I5, Pixie's sprites). */
        if (out->key_texture && out->state.blend_enable &&
            out->state.src_blend == V9X_CIF_D3D_BLEND_SRCALPHA &&
            out->state.dst_blend == V9X_CIF_D3D_BLEND_INVSRCALPHA &&
            !out->state.depth_write) {
            out->state.alpha_test_enable = 0ul;
        }

        /* The interface has one addressing mode for both axes. */
        if (texture != 0 && texture->clamp_s && texture->clamp_t) {
            out->address = V9X_R3D_ABI_ADDRESS_CLAMP;
        } else if (texture != 0 && (texture->clamp_s || texture->clamp_t)) {
            out->recognized = V9X_FALSE;
        }
    }

    out->shade = value[V9X_C3D_ERS_SHADE_MODE];
    out->solid_argb = v9x_cif_color_to_argb(value[V9X_C3D_ERS_SOLID_CLR]);

    /* The viewport's top-left is the vertices' origin, and it clips; the
     * scissor clips from the surface's own top-left (white paper,
     * "ATI3DCIF Viewport" and "Clipping Scissors"). */
    out->origin_x = 0.0f;
    out->origin_y = 0.0f;
    if (state->viewport_set) {
        out->origin_x = (float)state->viewport[V9X_CIF_RECT_LEFT];
        out->origin_y = (float)state->viewport[V9X_CIF_RECT_TOP];
        v9x_cif_cut(state->viewport, &left, &top, &right, &bottom);
    }
    if (state->scissor_set) {
        v9x_cif_cut(state->scissor, &left, &top, &right, &bottom);
    }
    out->state.scissor_left = v9x_cif_edge(left);
    out->state.scissor_top = v9x_cif_edge(top);
    out->state.scissor_right = v9x_cif_edge(right);
    out->state.scissor_bottom = v9x_cif_edge(bottom);

    /* Rage PRO states that change pixels and that the interface cannot
     * express. Dithering and the perspective level are quality hints and
     * are not flagged. */
    if (value[V9X_C3D_ERS_COMPOSITE_EN] != 0ul ||
        value[V9X_C3D_ERS_SPECULAR_EN] != 0ul ||
        value[V9X_C3D_ERS_ALPHA_DST_TEST_ENABLE] != 0ul ||
        value[V9X_C3D_ERS_LOD_BIAS_LEVEL] != 0ul ||
        value[V9X_C3D_ERS_ENHANCED_COLOR_RANGE_EN] != 0ul) {
        out->recognized = V9X_FALSE;
    }
}
