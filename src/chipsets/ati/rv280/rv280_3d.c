/*
 * ATI Radeon 9250 (RV280) 3D engine register values. See
 * include\velocity9x\rv280_3d.h for the contract and the sources it follows.
 */
#include "velocity9x/rv280_3d.h"

static void v9x_rv280_3d_begin(struct v9x_rv280_3d_list *list)
{
    list->count = 0u;
}

static void v9x_rv280_3d_add(struct v9x_rv280_3d_list *list, v9x_u32 offset,
                             v9x_u32 value)
{
    if (list->count < V9X_RV280_3D_LIST_MAX) {
        list->offsets[list->count] = offset;
        list->values[list->count] = value;
        ++list->count;
    }
}

v9x_status v9x_rv280_build_3d_init(struct v9x_rv280_3d_list *list)
{
    if (list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_rv280_3d_begin(list);

    /* RADEONEngineInit clears RB3D_CNTL on every pre-R300 part before any
     * 3D (radeon_accel.c:528-531). */
    v9x_rv280_3d_add(list, V9X_RV280_RB3D_CNTL, 0ul);

    /* RADEONInit3DEngineMMIO, R200 branch (radeon_commonfuncs.c:777-803).
     * VAP_CNTL_STATUS's TCL_BYPASS is the RS300's only; on the RV280 TCL is
     * bypassed by leaving VAP_CNTL's TCL_ENABLE clear. */
    v9x_rv280_3d_add(list, V9X_RV280_SE_VAP_CNTL_STATUS, 0ul);
    v9x_rv280_3d_add(list, V9X_RV280_PP_CNTL_X, 0ul);
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXMULTI_CTL_0, 0ul);
    v9x_rv280_3d_add(list, V9X_RV280_SE_VTX_STATE_CNTL, 0ul);
    /* No viewport transform: the vertices arrive in screen space. */
    v9x_rv280_3d_add(list, V9X_RV280_SE_VTE_CNTL, 0ul);
    v9x_rv280_3d_add(list, V9X_RV280_SE_VAP_CNTL, V9X_RV280_VAP_CNTL_BYPASS);
    v9x_rv280_3d_add(list, V9X_RV280_RE_AUX_SCISSOR_CNTL, 0ul);
    v9x_rv280_3d_add(list, V9X_RV280_RE_CNTL, 0ul);
    v9x_rv280_3d_add(list, V9X_RV280_RB3D_PLANEMASK, 0xfffffffful);
    v9x_rv280_3d_add(list, V9X_RV280_SE_CNTL,
                     V9X_RV280_SE_CNTL_BASE | V9X_RV280_SE_DIFFUSE_GOURAUD);
    return V9X_STATUS_OK;
}

/* The colour buffer, blending off, and the shade model: what every draw's
 * state shares. Leaves the texture environment and SE_VTX_FMT_1 to the
 * caller. */
static v9x_status v9x_rv280_add_target(
    const struct v9x_rv280_3d_target *target, int gouraud,
    struct v9x_rv280_3d_list *list)
{
    v9x_u32 format;
    v9x_u32 pitch_pixels;
    v9x_u32 address;
    v9x_u32 last_row_end;

    if (target == 0 || list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (target->bytes_per_pixel == 2ul) {
        format = V9X_RV280_COLOR_FORMAT_RGB565;
    } else if (target->bytes_per_pixel == 4ul) {
        format = V9X_RV280_COLOR_FORMAT_ARGB8888;
    } else {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* RB3D_COLORPITCH holds the pitch in pixels, a multiple of 8, at most
     * 1FF8h (radeon_reg.h; R200PrepareComposite :1024); RE_WIDTH_HEIGHT
     * the extent, up to R200CheckComposite's 2047. */
    if (target->pitch_bytes == 0ul ||
        target->pitch_bytes % target->bytes_per_pixel != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    pitch_pixels = target->pitch_bytes / target->bytes_per_pixel;
    if (pitch_pixels % V9X_RV280_COLOR_PITCH_UNIT != 0ul ||
        pitch_pixels > V9X_RV280_COLOR_PITCH_MAX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (target->width == 0ul || target->height == 0ul ||
        target->width > V9X_RV280_TARGET_MAX ||
        target->height > V9X_RV280_TARGET_MAX ||
        target->width > pitch_pixels) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* Inside the frame buffer, overflow-safe: every factor is bounded
     * above. RB3D_COLOROFFSET takes 16-byte-aligned GPU addresses. */
    if (target->offset >= target->fb_bytes ||
        target->offset % V9X_RV280_COLOR_OFFSET_ALIGN != 0ul ||
        target->fb_gpu_base % V9X_RV280_COLOR_OFFSET_ALIGN != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    last_row_end = (target->height - 1ul) * target->pitch_bytes +
                   target->width * target->bytes_per_pixel;
    if (last_row_end > target->fb_bytes - target->offset ||
        target->offset > 0xfffffffful - target->fb_gpu_base) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    address = target->fb_gpu_base + target->offset;

    v9x_rv280_3d_begin(list);
    /* R200PrepareComposite's colour buffer, blending off. */
    v9x_rv280_3d_add(list, V9X_RV280_RB3D_CNTL, format);
    v9x_rv280_3d_add(list, V9X_RV280_RB3D_COLOROFFSET, address);
    v9x_rv280_3d_add(list, V9X_RV280_RB3D_COLORPITCH, pitch_pixels);
    v9x_rv280_3d_add(list, V9X_RV280_RE_TOP_LEFT, 0ul);
    v9x_rv280_3d_add(list, V9X_RV280_RE_WIDTH_HEIGHT,
                     target->width | (target->height << 16));
    v9x_rv280_3d_add(list, V9X_RV280_RB3D_BLENDCNTL,
                     V9X_RV280_BLEND_ONE_ZERO);
    /* X, Y and a packed colour per vertex. */
    v9x_rv280_3d_add(list, V9X_RV280_SE_VTX_FMT_0,
                     V9X_RV280_VTX_FMT_PK_COLOR0);
    v9x_rv280_3d_add(list, V9X_RV280_SE_CNTL,
                     V9X_RV280_SE_CNTL_BASE |
                     (gouraud ? V9X_RV280_SE_DIFFUSE_GOURAUD
                              : V9X_RV280_SE_DIFFUSE_FLAT));
    return V9X_STATUS_OK;
}

v9x_status v9x_rv280_build_3d_target(const struct v9x_rv280_3d_target *target,
                                     int gouraud,
                                     struct v9x_rv280_3d_list *list)
{
    v9x_status status;

    status = v9x_rv280_add_target(target, gouraud, list);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    /* The diffuse colour through blend stage 0 with no texture unit,
     * measured (docs\decisions\2026-11-15-rv280-3d-draw-probe.md). */
    v9x_rv280_3d_add(list, V9X_RV280_PP_CNTL, V9X_RV280_PP_BLEND_0_ONLY);
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXCBLEND_0,
                     V9X_RV280_TXCBLEND_DIFFUSE);
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXCBLEND2_0,
                     V9X_RV280_TXBLEND2_TO_R0);
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXABLEND_0,
                     V9X_RV280_TXABLEND_DIFFUSE);
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXABLEND2_0,
                     V9X_RV280_TXBLEND2_TO_R0);
    /* No texture coordinates. */
    v9x_rv280_3d_add(list, V9X_RV280_SE_VTX_FMT_1, 0ul);
    return V9X_STATUS_OK;
}

/* log2 of a power of two from 1 to 2048, or 0xff for anything else. */
static v9x_u32 v9x_rv280_log2_side(v9x_u32 side)
{
    v9x_u32 shift;

    for (shift = 0ul; shift <= 11ul; ++shift) {
        if (side == (1ul << shift)) {
            return shift;
        }
    }
    return 0xfful;
}

v9x_status v9x_rv280_build_3d_textured_target(
    const struct v9x_rv280_3d_target *target, int gouraud,
    const struct v9x_rv280_3d_texture *texture, v9x_u32 combine,
    struct v9x_rv280_3d_list *list)
{
    v9x_u32 format;
    v9x_u32 log2_width;
    v9x_u32 log2_height;
    v9x_u32 filter;
    v9x_u32 cblend;
    v9x_u32 ablend;
    v9x_status status;

    if (texture == 0 || target == 0 || list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (texture->bytes_per_pixel == 2ul) {
        format = V9X_RV280_TXFORMAT_RGB565;
    } else if (texture->bytes_per_pixel == 4ul) {
        format = V9X_RV280_TXFORMAT_ARGB8888 |
                 V9X_RV280_TXFORMAT_ALPHA_IN_MAP;
    } else {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (combine == V9X_RV280_COMBINE_DECAL) {
        cblend = V9X_RV280_TXCBLEND_TEXEL;
        ablend = V9X_RV280_TXABLEND_TEXEL;
    } else if (combine == V9X_RV280_COMBINE_MODULATE) {
        cblend = V9X_RV280_TXCBLEND_MODULATE;
        ablend = V9X_RV280_TXABLEND_MODULATE;
    } else {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* R200TextureSetup's repeat path: a power of two on each side, its
     * size in TXFORMAT, rows packed (RADEONPitchMatches) at a multiple of
     * 32 bytes, the offset 32-byte aligned (CHECK_OFFSET 1Fh). */
    log2_width = v9x_rv280_log2_side(texture->width);
    log2_height = v9x_rv280_log2_side(texture->height);
    if (log2_width == 0xfful || log2_height == 0xfful ||
        texture->pitch_bytes != texture->width * texture->bytes_per_pixel ||
        texture->pitch_bytes % V9X_RV280_TEX_ALIGN != 0ul ||
        texture->offset % V9X_RV280_TEX_ALIGN != 0ul ||
        texture->fb_gpu_base % V9X_RV280_TEX_ALIGN != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* Inside the frame buffer: at most 2048 x 2048 x 4 bytes, so the
     * product cannot overflow. */
    if (texture->offset >= texture->fb_bytes ||
        texture->pitch_bytes * texture->height >
            texture->fb_bytes - texture->offset ||
        texture->offset > 0xfffffffful - texture->fb_gpu_base) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    status = v9x_rv280_add_target(target, gouraud, list);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    filter = (texture->linear ? V9X_RV280_TXFILTER_LINEAR : 0ul) |
             (texture->clamp ? V9X_RV280_TXFILTER_CLAMP_LAST : 0ul);
    v9x_rv280_3d_add(list, V9X_RV280_PP_CNTL, V9X_RV280_PP_TEX0_BLEND0);
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXFILTER_0, filter);
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXFORMAT_0,
                     format |
                     (log2_width << V9X_RV280_TXFORMAT_WIDTH_SHIFT) |
                     (log2_height << V9X_RV280_TXFORMAT_HEIGHT_SHIFT));
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXFORMAT_X_0, 0ul);
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXSIZE_0,
                     (texture->width - 1ul) |
                     ((texture->height - 1ul) << 16));
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXPITCH_0,
                     texture->pitch_bytes - V9X_RV280_TEX_ALIGN);
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXOFFSET_0,
                     texture->fb_gpu_base + texture->offset);
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXCBLEND_0, cblend);
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXCBLEND2_0,
                     V9X_RV280_TXBLEND2_TO_R0);
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXABLEND_0, ablend);
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXABLEND2_0,
                     V9X_RV280_TXBLEND2_TO_R0);
    v9x_rv280_3d_add(list, V9X_RV280_SE_VTX_FMT_1,
                     V9X_RV280_VTX_FMT_1_TEX0_ST);
    return V9X_STATUS_OK;
}

v9x_status v9x_rv280_build_3d_triangles(
    const struct v9x_rv280_3d_vertex *vertices, v9x_u32 count,
    int colour_order_rgba, int textured, struct v9x_rv280_3d_list *list)
{
    v9x_u32 control;
    v9x_u32 index;

    if (vertices == 0 || list == 0 || count == 0u || count % 3u != 0u ||
        count > V9X_RV280_3D_MAX_VERTICES) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* RadeonCompositeTile's R200 SE_VF_CNTL, with a triangle list for its
     * rectangle list and no RADEON_MODE. */
    control = V9X_RV280_VF_TRIANGLE_LIST | V9X_RV280_VF_WALK_DATA |
              ((v9x_u32)count << V9X_RV280_VF_NUM_SHIFT);
    if (colour_order_rgba) {
        control |= V9X_RV280_VF_COLOR_ORDER_RGBA;
    }

    v9x_rv280_3d_begin(list);
    v9x_rv280_3d_add(list, V9X_RV280_SE_VF_CNTL, control);
    for (index = 0u; index < count; ++index) {
        v9x_rv280_3d_add(list, V9X_RV280_SE_PORT_DATA0,
                         vertices[index].x_bits);
        v9x_rv280_3d_add(list, V9X_RV280_SE_PORT_DATA0,
                         vertices[index].y_bits);
        v9x_rv280_3d_add(list, V9X_RV280_SE_PORT_DATA0,
                         vertices[index].colour);
        /* SE_VTX_FMT_1's fields follow SE_VTX_FMT_0's (r200.c counts
         * them in that order): texture 0's S, T after the colour. */
        if (textured) {
            v9x_rv280_3d_add(list, V9X_RV280_SE_PORT_DATA0,
                             vertices[index].s_bits);
            v9x_rv280_3d_add(list, V9X_RV280_SE_PORT_DATA0,
                             vertices[index].t_bits);
        }
    }
    return V9X_STATUS_OK;
}

v9x_status v9x_rv280_build_3d_finish(struct v9x_rv280_3d_list *list)
{
    if (list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_rv280_3d_begin(list);
    v9x_rv280_3d_add(list, V9X_RV280_WAIT_UNTIL, V9X_RV280_WAIT_3D_IDLECLEAN);
    return V9X_STATUS_OK;
}
