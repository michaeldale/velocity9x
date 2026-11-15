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

v9x_status v9x_rv280_build_3d_target(const struct v9x_rv280_3d_target *target,
                                     int gouraud,
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

    /* The diffuse colour through blend stage 0 with no texture unit: a
     * hypothesis (rv280_3d.h), measured by V9XR23D. */
    v9x_rv280_3d_add(list, V9X_RV280_PP_CNTL, V9X_RV280_PP_BLEND_0_ONLY);
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXCBLEND_0,
                     V9X_RV280_TXCBLEND_DIFFUSE);
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXCBLEND2_0,
                     V9X_RV280_TXBLEND2_TO_R0);
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXABLEND_0,
                     V9X_RV280_TXABLEND_DIFFUSE);
    v9x_rv280_3d_add(list, V9X_RV280_PP_TXABLEND2_0,
                     V9X_RV280_TXBLEND2_TO_R0);

    /* X, Y and a packed colour per vertex; no texture coordinates. */
    v9x_rv280_3d_add(list, V9X_RV280_SE_VTX_FMT_0,
                     V9X_RV280_VTX_FMT_PK_COLOR0);
    v9x_rv280_3d_add(list, V9X_RV280_SE_VTX_FMT_1, 0ul);
    v9x_rv280_3d_add(list, V9X_RV280_SE_CNTL,
                     V9X_RV280_SE_CNTL_BASE |
                     (gouraud ? V9X_RV280_SE_DIFFUSE_GOURAUD
                              : V9X_RV280_SE_DIFFUSE_FLAT));
    return V9X_STATUS_OK;
}

v9x_status v9x_rv280_build_3d_triangles(
    const struct v9x_rv280_3d_vertex *vertices, v9x_u32 count,
    int colour_order_rgba, struct v9x_rv280_3d_list *list)
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
