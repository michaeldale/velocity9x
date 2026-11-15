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

/* The raster state's codes and its Z buffer, against the target's frame
 * buffer: V9X_STATUS_OK or INVALID_ARGUMENT. */
static v9x_status v9x_rv280_check_raster(
    const struct v9x_rv280_3d_target *target,
    const struct v9x_rv280_3d_raster *raster)
{
    v9x_u32 z_pitch_pixels;
    v9x_u32 z_bytes;

    if (raster->z_func > V9X_RV280_Z_TEST_ALWAYS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (raster->blend_enable &&
        (raster->blend_src < V9X_RV280_BLEND_FACTOR_FIRST ||
         raster->blend_src > V9X_RV280_BLEND_FACTOR_LAST ||
         raster->blend_dst < V9X_RV280_BLEND_FACTOR_FIRST ||
         raster->blend_dst > V9X_RV280_BLEND_FACTOR_LAST)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (raster->alpha_test_enable &&
        (raster->alpha_func > 7ul || raster->alpha_ref > 255ul)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* The fields each feature reads must be in the vertex: Z for the Z
     * buffer, the specular colour (its alpha the fog factor) for fog and
     * specular. */
    if (raster->z_enable &&
        (target->layout & V9X_RV280_LAYOUT_Z) == 0u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if ((raster->fog_enable || raster->specular_enable) &&
        (target->layout & V9X_RV280_LAYOUT_SPECULAR) == 0u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (raster->fog_enable &&
        (raster->fog_colour & ~V9X_RV280_FOG_COLOR_MASK) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if ((raster->masked_channels &
         ~(V9X_RV280_CHANNEL_RED | V9X_RV280_CHANNEL_GREEN |
           V9X_RV280_CHANNEL_BLUE)) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* A scissor is a non-empty rectangle inside the target. */
    if (raster->scissor_enable &&
        (raster->scissor_left >= raster->scissor_right ||
         raster->scissor_top >= raster->scissor_bottom ||
         raster->scissor_right > target->width ||
         raster->scissor_bottom > target->height)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (!raster->z_enable) {
        return V9X_STATUS_OK;
    }

    /* A 16-bit Z buffer the size of the target: RB3D_DEPTHPITCH in pixels,
     * a multiple of 8, at most 1FF8h; RB3D_DEPTHOFFSET aligned as
     * RB3D_COLOROFFSET is (an assumption: neither source states its
     * alignment). */
    if (raster->z_pitch_bytes == 0ul || raster->z_pitch_bytes % 2ul != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    z_pitch_pixels = raster->z_pitch_bytes / 2ul;
    if (z_pitch_pixels % V9X_RV280_COLOR_PITCH_UNIT != 0ul ||
        z_pitch_pixels > V9X_RV280_DEPTH_PITCH_MAX ||
        z_pitch_pixels < target->width ||
        raster->z_offset % V9X_RV280_COLOR_OFFSET_ALIGN != 0ul ||
        raster->z_offset >= target->fb_bytes ||
        raster->z_offset > 0xfffffffful - target->fb_gpu_base) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* Bounded above by 1FF8h x 2 x 2047 bytes: no overflow. */
    z_bytes = (target->height - 1ul) * raster->z_pitch_bytes +
              target->width * 2ul;
    if (z_bytes > target->fb_bytes - raster->z_offset) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return V9X_STATUS_OK;
}

/* The colour buffer, the raster state, and the shade model: what every
 * draw's state shares. Leaves PP_CNTL, the texture environment and
 * SE_VTX_FMT_1 to the caller, with the PP_CNTL bits the raster state needs
 * in *pp_cntl_extra. */
static v9x_status v9x_rv280_add_target(
    const struct v9x_rv280_3d_target *target, int gouraud,
    struct v9x_rv280_3d_list *list, v9x_u32 *pp_cntl_extra)
{
    const struct v9x_rv280_3d_raster *raster;
    v9x_u32 format;
    v9x_u32 pitch_pixels;
    v9x_u32 address;
    v9x_u32 last_row_end;
    v9x_u32 cntl;
    v9x_u32 blend;
    v9x_u32 zstencil;
    v9x_u32 misc;
    v9x_u32 vertex_format;
    v9x_u32 setup;
    v9x_u32 fog;
    v9x_u32 planemask;
    v9x_u32 re_cntl;
    v9x_u32 top_left;
    v9x_u32 width_height;

    if (target == 0 || list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *pp_cntl_extra = 0ul;
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
        target->pitch_bytes % target->bytes_per_pixel != 0ul ||
        target->pitch_bytes % V9X_RV280_COLOR_PITCH_ALIGN != 0ul) {
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

    /* No raster state: Z off and ALWAYS without writes, blending off, the
     * alpha test off and PASS. */
    raster = target->raster;
    cntl = format;
    blend = V9X_RV280_BLEND_ONE_ZERO;
    zstencil = V9X_RV280_Z_TEST_ALWAYS << V9X_RV280_Z_TEST_SHIFT;
    misc = 7ul << V9X_RV280_ALPHA_TEST_SHIFT;
    fog = V9X_RV280_FOG_USE_SPEC_ALPHA;
    /* Colour, alpha, specular, fog and discrete fog shaded alike; both
     * faces drawn, clockwise front. */
    setup = V9X_RV280_SE_CNTL_BASE |
            (gouraud ? V9X_RV280_SE_DIFFUSE_GOURAUD |
                           V9X_RV280_SE_ALPHA_GOURAUD |
                           V9X_RV280_SE_SHADE_GOURAUD_REST
                     : V9X_RV280_SE_DIFFUSE_FLAT |
                           V9X_RV280_SE_ALPHA_FLAT |
                           V9X_RV280_SE_SHADE_FLAT_REST);
    /* The vertex: X, Y always, then what the layout names. */
    vertex_format = V9X_RV280_VTX_FMT_PK_COLOR0;
    if ((target->layout & V9X_RV280_LAYOUT_Z) != 0u) {
        vertex_format |= V9X_RV280_VTX_FMT_Z0;
    }
    if ((target->layout & V9X_RV280_LAYOUT_W) != 0u) {
        vertex_format |= V9X_RV280_VTX_FMT_W0;
    }
    if ((target->layout & V9X_RV280_LAYOUT_SPECULAR) != 0u) {
        vertex_format |= V9X_RV280_VTX_FMT_PK_COLOR1;
    }
    if (raster != 0) {
        if (v9x_rv280_check_raster(target, raster) != V9X_STATUS_OK) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        if (raster->z_enable) {
            cntl |= V9X_RV280_RB3D_Z_ENABLE;
            zstencil = (raster->z_func << V9X_RV280_Z_TEST_SHIFT) |
                       (raster->z_write ? V9X_RV280_Z_WRITE_ENABLE : 0ul);
        }
        if (raster->cull_back) {
            setup &= ~V9X_RV280_SE_BFACE_SOLID;
        }
        if (raster->cull_front) {
            setup &= ~V9X_RV280_SE_FFACE_SOLID;
        }
        if (raster->front_ccw) {
            setup |= V9X_RV280_SE_FFACE_CULL_CCW;
        }
        if (raster->fog_enable) {
            fog |= raster->fog_colour;
            *pp_cntl_extra |= V9X_RV280_PP_FOG_ENABLE;
        }
        if (raster->specular_enable) {
            *pp_cntl_extra |= V9X_RV280_PP_SPECULAR_ENABLE;
        }
        if (raster->blend_enable) {
            cntl |= V9X_RV280_RB3D_ALPHA_BLEND;
            blend = (raster->blend_src << V9X_RV280_BLEND_SRC_SHIFT) |
                    (raster->blend_dst << V9X_RV280_BLEND_DST_SHIFT);
        }
        if (raster->alpha_test_enable) {
            misc = raster->alpha_ref |
                   (raster->alpha_func << V9X_RV280_ALPHA_TEST_SHIFT);
            *pp_cntl_extra |= V9X_RV280_PP_ALPHA_TEST_ENABLE;
        }
    }

    /* The bits left unwritten: the X byte and any masked channel. The mask
     * needs its enable bit (V9XD32 on M-HNBJNV read FF13579Bh for
     * 0013579Bh without it). */
    planemask = V9X_RV280_PLANEMASK_ALL;
    if (raster != 0) {
        int wide = target->bytes_per_pixel == 4ul;

        if (raster->keep_x && wide) {
            planemask &= V9X_RV280_PLANEMASK_NO_X;
        }
        if ((raster->masked_channels & V9X_RV280_CHANNEL_RED) != 0ul) {
            planemask &= ~(wide ? V9X_RV280_PLANE_RED_8888
                                : V9X_RV280_PLANE_RED_565);
        }
        if ((raster->masked_channels & V9X_RV280_CHANNEL_GREEN) != 0ul) {
            planemask &= ~(wide ? V9X_RV280_PLANE_GREEN_8888
                                : V9X_RV280_PLANE_GREEN_565);
        }
        if ((raster->masked_channels & V9X_RV280_CHANNEL_BLUE) != 0ul) {
            planemask &= ~(wide ? V9X_RV280_PLANE_BLUE_8888
                                : V9X_RV280_PLANE_BLUE_565);
        }
    }
    if (planemask != V9X_RV280_PLANEMASK_ALL) {
        cntl |= V9X_RV280_RB3D_PLANE_MASK;
    }
    /* The scissor, as Mesa's r200_vtbl_update_scissor writes it: the
     * corners inclusive. Without one, the extent the probes drew with. */
    re_cntl = V9X_RV280_RE_PERSPECTIVE;
    top_left = 0ul;
    width_height = target->width | (target->height << 16);
    if (raster != 0 && raster->scissor_enable) {
        re_cntl |= V9X_RV280_RE_SCISSOR;
        top_left = raster->scissor_left | (raster->scissor_top << 16);
        width_height = (raster->scissor_right - 1ul) |
                       ((raster->scissor_bottom - 1ul) << 16);
    }

    v9x_rv280_3d_begin(list);
    /* R200PrepareComposite's colour buffer. */
    v9x_rv280_3d_add(list, V9X_RV280_RB3D_CNTL, cntl);
    v9x_rv280_3d_add(list, V9X_RV280_RB3D_COLOROFFSET, address);
    v9x_rv280_3d_add(list, V9X_RV280_RB3D_COLORPITCH, pitch_pixels);
    v9x_rv280_3d_add(list, V9X_RV280_RE_TOP_LEFT, top_left);
    v9x_rv280_3d_add(list, V9X_RV280_RE_WIDTH_HEIGHT, width_height);
    v9x_rv280_3d_add(list, V9X_RV280_RB3D_BLENDCNTL, blend);
    v9x_rv280_3d_add(list, V9X_RV280_RB3D_PLANEMASK, planemask);
    /* The Z buffer, 16-bit integer Z, stencil off (radeon_reg.h:2151-2234;
     * no R200 path in the X driver writes it). */
    if (raster != 0 && raster->z_enable) {
        v9x_rv280_3d_add(list, V9X_RV280_RB3D_DEPTHOFFSET,
                         target->fb_gpu_base + raster->z_offset);
        v9x_rv280_3d_add(list, V9X_RV280_RB3D_DEPTHPITCH,
                         raster->z_pitch_bytes / 2ul);
    }
    v9x_rv280_3d_add(list, V9X_RV280_RB3D_ZSTENCILCNTL, zstencil);
    v9x_rv280_3d_add(list, V9X_RV280_PP_MISC, misc);
    v9x_rv280_3d_add(list, V9X_RV280_PP_FOG_COLOR, fog);
    v9x_rv280_3d_add(list, V9X_RV280_SE_VTX_FMT_0, vertex_format);
    /* With W in the vertex, X, Y and Z arrive divided and the W field holds
     * 1/W, which the rasterizer keeps for perspective; without, the init's
     * values: no flags, W forced to one. */
    if ((target->layout & V9X_RV280_LAYOUT_W) != 0u) {
        v9x_rv280_3d_add(list, V9X_RV280_SE_VTE_CNTL,
                         V9X_RV280_VTE_XYZ_DIVIDED);
        v9x_rv280_3d_add(list, V9X_RV280_SE_VAP_CNTL,
                         V9X_RV280_VAP_CNTL_KEEP_W);
    } else {
        v9x_rv280_3d_add(list, V9X_RV280_SE_VTE_CNTL, 0ul);
        v9x_rv280_3d_add(list, V9X_RV280_SE_VAP_CNTL,
                         V9X_RV280_VAP_CNTL_BYPASS);
    }
    v9x_rv280_3d_add(list, V9X_RV280_SE_CNTL, setup);
    /* Perspective-correct interpolation: with W forced to one it is the
     * affine result, with 1/W it corrects. */
    v9x_rv280_3d_add(list, V9X_RV280_RE_CNTL, re_cntl);
    return V9X_STATUS_OK;
}

v9x_status v9x_rv280_build_3d_target(const struct v9x_rv280_3d_target *target,
                                     int gouraud,
                                     struct v9x_rv280_3d_list *list)
{
    v9x_status status;
    v9x_u32 pp_extra;

    status = v9x_rv280_add_target(target, gouraud, list, &pp_extra);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    /* The diffuse colour through blend stage 0 with no texture unit,
     * measured (docs\decisions\2026-11-15-rv280-3d-draw-probe.md). */
    v9x_rv280_3d_add(list, V9X_RV280_PP_CNTL,
                     V9X_RV280_PP_BLEND_0_ONLY | pp_extra);
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
    v9x_u32 pp_extra;
    v9x_status status;

    if (texture == 0 || target == 0 || list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* The format and its texel size must agree. */
    switch (texture->format) {
    case V9X_RV280_TEXFMT_DEFAULT:
        if (texture->bytes_per_pixel == 2ul) {
            format = V9X_RV280_TXFORMAT_RGB565;
        } else if (texture->bytes_per_pixel == 4ul) {
            format = V9X_RV280_TXFORMAT_ARGB8888 |
                     V9X_RV280_TXFORMAT_ALPHA_IN_MAP;
        } else {
            return V9X_STATUS_UNSUPPORTED;
        }
        break;
    case V9X_RV280_TEXFMT_ARGB1555:
    case V9X_RV280_TEXFMT_ARGB4444:
        if (texture->bytes_per_pixel != 2ul) {
            return V9X_STATUS_UNSUPPORTED;
        }
        format = (texture->format == V9X_RV280_TEXFMT_ARGB1555
                      ? V9X_RV280_TXFORMAT_ARGB1555
                      : V9X_RV280_TXFORMAT_ARGB4444) |
                 V9X_RV280_TXFORMAT_ALPHA_IN_MAP;
        break;
    case V9X_RV280_TEXFMT_XRGB8888:
        if (texture->bytes_per_pixel != 4ul) {
            return V9X_STATUS_UNSUPPORTED;
        }
        format = V9X_RV280_TXFORMAT_ARGB8888;
        break;
    default:
        return V9X_STATUS_UNSUPPORTED;
    }
    switch (combine) {
    case V9X_RV280_COMBINE_DECAL:
        cblend = V9X_RV280_TXCBLEND_TEXEL;
        ablend = V9X_RV280_TXABLEND_TEXEL;
        break;
    case V9X_RV280_COMBINE_MODULATE:
        cblend = V9X_RV280_TXCBLEND_MODULATE;
        ablend = V9X_RV280_TXABLEND_MODULATE;
        break;
    case V9X_RV280_COMBINE_MODULATE_TEXEL_ALPHA:
        cblend = V9X_RV280_TXCBLEND_MODULATE;
        ablend = V9X_RV280_TXABLEND_TEXEL;
        break;
    case V9X_RV280_COMBINE_MODULATE_DIFFUSE_ALPHA:
        cblend = V9X_RV280_TXCBLEND_MODULATE;
        ablend = V9X_RV280_TXABLEND_DIFFUSE;
        break;
    case V9X_RV280_COMBINE_DECAL_DIFFUSE_ALPHA:
        cblend = V9X_RV280_TXCBLEND_TEXEL;
        ablend = V9X_RV280_TXABLEND_DIFFUSE;
        break;
    default:
        return V9X_STATUS_UNSUPPORTED;
    }

    /* R200TextureSetup's repeat path: a power of two on each side, its
     * size in TXFORMAT, the offset 32-byte aligned (CHECK_OFFSET 1Fh).
     * The rows are the texture's own rounded up to 32 bytes, where the
     * sampler steps a power-of-two texture's rows (V9X_RV280_TEX_ROW_ALIGN);
     * the probes measured rows of 64 and 128 bytes, where the two agree. */
    log2_width = v9x_rv280_log2_side(texture->width);
    log2_height = v9x_rv280_log2_side(texture->height);
    if (log2_width == 0xfful || log2_height == 0xfful ||
        texture->pitch_bytes !=
            ((texture->width * texture->bytes_per_pixel +
              V9X_RV280_TEX_ROW_ALIGN - 1ul) &
             ~(V9X_RV280_TEX_ROW_ALIGN - 1ul)) ||
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

    status = v9x_rv280_add_target(target, gouraud, list, &pp_extra);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    filter = (texture->linear ? V9X_RV280_TXFILTER_LINEAR : 0ul) |
             (texture->clamp ? V9X_RV280_TXFILTER_CLAMP_LAST : 0ul);
    v9x_rv280_3d_add(list, V9X_RV280_PP_CNTL,
                     V9X_RV280_PP_TEX0_BLEND0 | pp_extra);
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
    int colour_order_rgba, v9x_u32 layout, struct v9x_rv280_3d_list *list)
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
        /* Z0 and W0 are SE_VTX_FMT_0's bits 0 and 1, so they follow X and
         * Y. */
        if ((layout & V9X_RV280_LAYOUT_Z) != 0u) {
            v9x_rv280_3d_add(list, V9X_RV280_SE_PORT_DATA0,
                             vertices[index].z_bits);
        }
        if ((layout & V9X_RV280_LAYOUT_W) != 0u) {
            v9x_rv280_3d_add(list, V9X_RV280_SE_PORT_DATA0,
                             vertices[index].w_bits);
        }
        v9x_rv280_3d_add(list, V9X_RV280_SE_PORT_DATA0,
                         vertices[index].colour);
        if ((layout & V9X_RV280_LAYOUT_SPECULAR) != 0u) {
            v9x_rv280_3d_add(list, V9X_RV280_SE_PORT_DATA0,
                             vertices[index].specular);
        }
        /* SE_VTX_FMT_1's fields follow SE_VTX_FMT_0's (r200.c counts
         * them in that order): texture 0's S, T after the colour. */
        if ((layout & V9X_RV280_LAYOUT_ST) != 0u) {
            v9x_rv280_3d_add(list, V9X_RV280_SE_PORT_DATA0,
                             vertices[index].s_bits);
            v9x_rv280_3d_add(list, V9X_RV280_SE_PORT_DATA0,
                             vertices[index].t_bits);
        }
    }
    return V9X_STATUS_OK;
}

v9x_status v9x_rv280_build_3d_finish(int flush_z,
                                     struct v9x_rv280_3d_list *list)
{
    if (list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_rv280_3d_begin(list);
    if (flush_z) {
        v9x_rv280_3d_add(list, V9X_RV280_RB3D_ZCACHE_CTLSTAT,
                         V9X_RV280_ZC_FLUSH_ALL);
    }
    v9x_rv280_3d_add(list, V9X_RV280_WAIT_UNTIL, V9X_RV280_WAIT_3D_IDLECLEAN);
    return V9X_STATUS_OK;
}
