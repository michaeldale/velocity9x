/*
 * ATI Rage 128 3D engine packet streams. See include\velocity9x\r128_3d.h
 * for the contract and the sources it follows.
 */
#include "velocity9x/r128_3d.h"
#include "velocity9x/r128_2d.h"

/* DP_GUI_MASTER_CNTL_C, as Mesa's r128DDInitState sets it: the 2D master
 * control with the destination clipped to the 3D scissor, source copy,
 * GMC_3D_FCN_EN (1 << 27) and GMC_WR_MSK_DIS (1 << 30). The destination
 * datatype is or-ed in. */
#define R128_GMC_DST_CLIPPING           0x00000008ul
#define R128_GMC_3D_FCN_EN              0x08000000ul
#define R128_GMC_WR_MSK_DIS             0x40000000ul

/* SC_TOP_LEFT_C / SC_BOTTOM_RIGHT_C: the 3D scissor, x in 12:0 and y in
 * 28:16, both corners inclusive (Mesa's r128UpdateClipping writes
 * x + width - 1). Opened to the maxima unless a scissor is given; the
 * render core clips. */
#define R128_SC_C_MAX                   0x1fff1ffful
#define R128_SC_LIMIT                   0x2000ul
#define R128_SC_Y_SHIFT                 16

/* PLANE_3D_MASK_C per channel: RGB565 in each 16-bit half, ARGB8888 with
 * the alpha byte always kept. */
#define R128_PLANE_565_RED              0xf800f800ul
#define R128_PLANE_565_GREEN            0x07e007e0ul
#define R128_PLANE_565_BLUE             0x001f001ful
#define R128_PLANE_8888_RED             0x00ff0000ul
#define R128_PLANE_8888_GREEN           0x0000ff00ul
#define R128_PLANE_8888_BLUE            0x000000fful
#define R128_PLANE_8888_ALL             0x00fffffful

/* Z_STEN_CNTL_C: 16-bit Z (2:1 = 0), Z test in 6:4, stencil test always
 * (7 << 12) with every stencil operation KEEP. */
#define R128_Z_TEST_SHIFT               4
#define R128_STENCIL_TEST_ALWAYS        0x00007000ul

/* TEX_CNTL_C (r128_reg.h). */
#define R128_Z_ENABLE                   0x00000001ul
#define R128_Z_WRITE_ENABLE             0x00000002ul
#define R128_TEXMAP_ENABLE              0x00000010ul
#define R128_SEC_TEXMAP_ENABLE          0x00000020ul
#define R128_FOG_ENABLE                 0x00000080ul
#define R128_DITHER_ENABLE              0x00000100ul
#define R128_ALPHA_ENABLE               0x00000200ul
#define R128_ALPHA_TEST_ENABLE          0x00000400ul
#define R128_SPEC_LIGHT_ENABLE          0x00000800ul
#define R128_TEX_CACHE_FLUSH            0x00800000ul
/* Mesa's LOD bias field, 31:24. */
#define R128_LOD_BIAS_DEFAULT           0x3f000000ul

/* MISC_3D_STATE_CNTL_REG and SCALE_3D_CNTL share the blend factors (19:16
 * source, 23:20 destination) and the alpha test (26:24). */
#define R128_BLEND_SRC_SHIFT            16
#define R128_BLEND_DST_SHIFT            20
#define R128_ALPHA_TEST_SHIFT           24
#define R128_REF_ALPHA_MASK             0x000000fful
/* MISC_SCALE_3D_TEXMAP_SHADE (2 << 8) | MISC_SCALE_PIX_REPLICATE (1 << 10);
 * ALPHA_COMB_ADD_CLAMP and FOG_VERTEX are zero. */
#define R128_MISC_3D_BASE               0x00000600ul
/* SCALE_3D_CNTL as Mesa's: dither table (1 << 1), dither reset (1 << 3),
 * TEXMAP_SHADE (2 << 6), PIX_REPLICATE (1 << 8), texture alpha from the
 * texture (1 << 30), 4-quadword cache lines (1 << 31). */
#define R128_SCALE_3D_BASE              0xc000018aul
/* TEX_CACHE_SPLIT (1 << 9): the texture cache shared between the units,
 * which Mesa sets whenever unit 1 is on. */
#define R128_TEX_CACHE_SPLIT            0x00000200ul

/* SETUP_CNTL: the colour mode in 5:3, triangles, S/T multiplied by W,
 * vertices 1 to 3 (1 << 14, 3 << 16), POLY_LINE_NOT_LAST (1 << 18), four
 * subpixel bits (1 << 19). */
#define R128_COLOR_FLAT_VERT_1          0x00000008ul
#define R128_COLOR_GOURAUD              0x00000020ul
#define R128_SETUP_BASE                 0x000f4000ul

/* PM4_VC_FPU_SETUP: counter-clockwise front, both faces drawn (the core
 * culls), the colour mode in 6:5, four subpixel bits, 3D, trapezoid bits
 * off, flat shading from the first vertex (FLAT_SHADE_VERTEX_D3D, 0). */
#define R128_FPU_COLOR_FLAT             0x00000020ul
#define R128_FPU_COLOR_GOURAUD          0x00000040ul
#define R128_FPU_BASE                   0x0000039ful

/* PRIM_TEX_CNTL_C. */
#define R128_MIN_FILTER_SHIFT           1
#define R128_MAG_FILTER_SHIFT           4
#define R128_MIP_MAP_DISABLE            0x00000080ul
#define R128_TEX_CLAMP_S_CLAMP          0x00000200ul
#define R128_TEX_CLAMP_T_CLAMP          0x00001000ul
#define R128_TEX_DATATYPE_SHIFT         16

/* PRIM_TEXTURE_COMBINE_CNTL_C (r128_texstate.c): colour op 3:0, colour
 * factor 7:4, input factor 13:10, alpha op 17:14, alpha factor 21:18,
 * alpha input factor 27:25. Unit 0 interpolates from the vertex; unit 1's
 * SEC_TEX_COMBINE_CNTL_C takes unit 0's result (Mesa's INPUT_PREVIOUS). */
#define R128_COMB_DIS                   0x00000000ul
#define R128_COMB_MODULATE              0x00000003ul
#define R128_COMB_ADD                   0x00000006ul
#define R128_COMB_BLEND_TEXTURE         0x00000009ul
#define R128_COLOR_FACTOR_TEX           0x00000040ul
#define R128_INPUT_FACTOR_INT_COLOR     0x00001000ul
#define R128_COMB_ALPHA_DIS             0x00000000ul
#define R128_COMB_ALPHA_COPY_INP        0x00008000ul
#define R128_COMB_ALPHA_MODULATE        0x0000c000ul
#define R128_ALPHA_FACTOR_TEX_ALPHA     0x00180000ul
#define R128_INP_FACTOR_A_INT_ALPHA     0x04000000ul
#define R128_COMBINE_INPUT_INTERP       (R128_INPUT_FACTOR_INT_COLOR | \
                                         R128_INP_FACTOR_A_INT_ALPHA)
#define R128_INPUT_FACTOR_PREV_COLOR    0x00002000ul
#define R128_INP_FACTOR_A_PREV_ALPHA    0x08000000ul
#define R128_COMBINE_INPUT_PREVIOUS     (R128_INPUT_FACTOR_PREV_COLOR | \
                                         R128_INP_FACTOR_A_PREV_ALPHA)

/* SEC_TEX_CNTL_C is PRIM_TEX_CNTL_C's layout with SEC_SELECT_SEC_ST in bit
 * 0: sample with the vertex's second S/T pair. The block runs to
 * SEC_TEX_10_OFFSET_C, as r128_emit_tex1 writes it. */
#define R128_SEC_TEX_CNTL_C             0x1d00ul
#define R128_SEC_SELECT_SEC_ST          0x00000001ul
#define R128_SEC_TEXTURE_REGS           13u
#define R128_SEC_TEX_SIZE_PITCH_SHIFT   16

/* TEX_SIZE_PITCH_C: log2 pitch 3:0, log2 size 7:4, log2 height 11:8,
 * log2 smallest level 15:12. */
#define R128_TEX_SIZE_SHIFT             4
#define R128_TEX_HEIGHT_SHIFT           8
#define R128_TEX_MIN_SIZE_SHIFT         12
/* 1024 texels a side (the log2 fields hold 4 bits; Mesa offers 1024). */
#define R128_TEX_LOG2_MAX               10ul

/* Vertex format: RHW, DIFFUSE_ARGB, SPEC_FRGB, S_T, and S2_T2 (100h) with
 * two units. VC_CNTL: TRI_LIST (4) walking the vertices that follow
 * (WALK_RING, 30h), count in 31:16. */
#define R128_VC_FRMT_TL                 0x000000c9ul
#define R128_VC_FRMT_S2_T2              0x00000100ul
#define R128_VC_CNTL_TRI_LIST           0x00000034ul
#define R128_VC_CNTL_COUNT_SHIFT        16
/* VC_CNTL for indices (r128_drv.h): TRI_LIST 4 or TRI_STRIP 6, with
 * PRIM_WALK_IND 0x10. */
#define R128_VC_CNTL_IND_TRI_LIST       0x00000014ul
#define R128_VC_CNTL_IND_TRI_STRIP      0x00000016ul
/* And for a vertex buffer walked in order: TRI_LIST with PRIM_WALK_LIST
 * 0x20 (r128_drv.h). */
#define R128_VC_CNTL_LIST_TRI_LIST      0x00000024ul

/* PM4_VC_FPU_SETUP's face bits (r128_reg.h): FRONT_DIR 0 (1 = CCW),
 * BACKFACE 2:1 and FRONTFACE 4:3, 0 cull and 3 solid. FRONT_DIR_CCW is
 * counter-clockwise on the screen, y down: Direct3D's CCW cull, which keeps
 * clockwise triangles, is FRONT_DIR clear with the back face culled (18h).
 * The opposite, read from Mesa's y-flipped emit, culled the faces Direct3D
 * keeps: Half-Life's walls went missing, and V9XD32 T9 failed once its
 * indexed draws reached this path at DDI 6 (A8U4I5 boots 583-585,
 * 2026-10-24); it had passed only through the core's culling. */
#define R128_FPU_FACE_MASK              0x0000001ful
#define R128_FPU_FACE_BOTH              0x0000001ful
#define R128_FPU_FACE_KEEP_SCREEN_CW    0x00000018ul
#define R128_FPU_FACE_KEEP_SCREEN_CCW   0x00000019ul

/* Mesa's constant colour, stencil reference/mask and plane mask. */
#define R128_CONSTANT_COLOR             0x00fffffful
#define R128_STEN_REF_MASK              0xffff0000ul

static void v9x_r128_3d_emit(struct v9x_r128_3d_stream *stream,
                             v9x_u32 value)
{
    stream->dwords[stream->count++] = value;
}

static v9x_u32 v9x_r128_3d_datatype(v9x_u32 bytes_per_pixel)
{
    if (bytes_per_pixel == 2ul) {
        return V9X_R128_DATATYPE_16BPP;
    }
    if (bytes_per_pixel == 4ul) {
        return V9X_R128_DATATYPE_32BPP;
    }
    return 0ul;
}

/*
 * A surface as the engine addresses it: 32-byte aligned (the offset fields
 * count 32-byte units), its pitch a whole number of 8-pixel units within
 * the 11-bit field, and its rows inside memory. Returns the pitch in units,
 * or 0 when any of that fails.
 */
static v9x_u32 v9x_r128_3d_surface_units(v9x_u32 vram_bytes, v9x_u32 offset,
                                         v9x_u32 pitch_bytes,
                                         v9x_u32 bytes_per_pixel)
{
    v9x_u32 unit = V9X_R128_PITCH_PIXEL_UNIT * bytes_per_pixel;

    if (bytes_per_pixel == 0ul || pitch_bytes == 0ul ||
        pitch_bytes % unit != 0ul ||
        pitch_bytes / unit > V9X_R128_PITCH_UNITS_MAX ||
        offset % V9X_R128_OFFSET_ALIGN != 0ul || offset >= vram_bytes) {
        return 0ul;
    }
    return pitch_bytes / unit;
}

/* COMBINE_OPS: the stated colour and alpha ops on the unit's input, the
 * texel as factor; 0xFFFFFFFF for an op past the tables. */
static v9x_u32 v9x_r128_3d_ops_combine(v9x_u32 color_op, v9x_u32 alpha_op,
                                       v9x_u32 input)
{
    v9x_u32 combine = R128_COLOR_FACTOR_TEX | R128_ALPHA_FACTOR_TEX_ALPHA |
                      input;

    switch (color_op) {
    case V9X_R128_COLOROP_REPLACE:
        combine |= R128_COMB_DIS;
        break;
    case V9X_R128_COLOROP_MODULATE:
        combine |= R128_COMB_MODULATE;
        break;
    case V9X_R128_COLOROP_DECAL_ALPHA:
        combine |= R128_COMB_BLEND_TEXTURE;
        break;
    case V9X_R128_COLOROP_ADD:
        combine |= R128_COMB_ADD;
        break;
    default:
        return 0xfffffffful;
    }
    switch (alpha_op) {
    case V9X_R128_ALPHAOP_INPUT:
        return combine | R128_COMB_ALPHA_COPY_INP;
    case V9X_R128_ALPHAOP_REPLACE:
        return combine | R128_COMB_ALPHA_DIS;
    case V9X_R128_ALPHAOP_MODULATE:
        return combine | R128_COMB_ALPHA_MODULATE;
    default:
        return 0xfffffffful;
    }
}

/* A unit's combine register. Unit 1 takes stated ops only: every other
 * code is written in terms of the interpolated colour, which unit 1's
 * input is not. */
static v9x_u32 v9x_r128_3d_combine(const struct v9x_r128_3d_texture *texture,
                                   v9x_u32 unit)
{
    if (texture->combine == V9X_R128_COMBINE_OPS) {
        return v9x_r128_3d_ops_combine(texture->color_op, texture->alpha_op,
                                       unit == 0ul
                                           ? R128_COMBINE_INPUT_INTERP
                                           : R128_COMBINE_INPUT_PREVIOUS);
    }
    if (unit != 0ul) {
        return 0xfffffffful;
    }
    switch (texture->combine) {
    case V9X_R128_COMBINE_REPLACE:
        return R128_COMB_DIS | R128_COLOR_FACTOR_TEX |
               R128_COMB_ALPHA_DIS | R128_ALPHA_FACTOR_TEX_ALPHA |
               R128_COMBINE_INPUT_INTERP;
    case V9X_R128_COMBINE_MODULATE:
        return R128_COMB_MODULATE | R128_COLOR_FACTOR_TEX |
               R128_COMB_ALPHA_MODULATE | R128_ALPHA_FACTOR_TEX_ALPHA |
               R128_COMBINE_INPUT_INTERP;
    case V9X_R128_COMBINE_DECAL:
        return R128_COMB_DIS | R128_COLOR_FACTOR_TEX |
               R128_COMB_ALPHA_COPY_INP | R128_ALPHA_FACTOR_TEX_ALPHA |
               R128_COMBINE_INPUT_INTERP;
    case V9X_R128_COMBINE_MODULATE_RGB:
        return R128_COMB_MODULATE | R128_COLOR_FACTOR_TEX |
               R128_COMB_ALPHA_COPY_INP | R128_ALPHA_FACTOR_TEX_ALPHA |
               R128_COMBINE_INPUT_INTERP;
    case V9X_R128_COMBINE_MODULATE_TEXALPHA:
        return R128_COMB_MODULATE | R128_COLOR_FACTOR_TEX |
               R128_COMB_ALPHA_DIS | R128_ALPHA_FACTOR_TEX_ALPHA |
               R128_COMBINE_INPUT_INTERP;
    case V9X_R128_COMBINE_DECAL_ALPHA:
        return R128_COMB_BLEND_TEXTURE | R128_COLOR_FACTOR_TEX |
               R128_COMB_ALPHA_COPY_INP | R128_ALPHA_FACTOR_TEX_ALPHA |
               R128_COMBINE_INPUT_INTERP;
    default:
        return 0xfffffffful;
    }
}

static int v9x_r128_3d_texture_datatype(v9x_u32 datatype)
{
    return datatype == V9X_R128_TEX_ARGB1555 ||
           datatype == V9X_R128_TEX_RGB565 ||
           datatype == V9X_R128_TEX_ARGB8888 ||
           datatype == V9X_R128_TEX_ARGB4444;
}

/* One unit's registers: its control, combine, its half of
 * TEX_SIZE_PITCH_C, and the eleven level offsets in register order. */
struct v9x_r128_3d_unit_words {
    v9x_u32 control;
    v9x_u32 combine;
    v9x_u32 size_pitch;
    v9x_u32 offsets[V9X_R128_TEXTURE_LEVELS];
};

static v9x_status v9x_r128_3d_unit_words(
    const struct v9x_r128_3d_texture *texture, v9x_u32 unit,
    v9x_u32 vram_bytes, struct v9x_r128_3d_unit_words *words)
{
    v9x_u32 size_log2;
    v9x_u32 min_log2;
    v9x_u32 control;
    v9x_u32 combine;
    v9x_u32 index;

    if (!v9x_r128_3d_texture_datatype(texture->datatype) ||
        texture->width_log2 > R128_TEX_LOG2_MAX ||
        texture->height_log2 > R128_TEX_LOG2_MAX ||
        texture->min_filter > V9X_R128_FILTER_LINEARMIPLINEAR ||
        texture->mag_filter > V9X_R128_FILTER_LINEAR) {
        return V9X_STATUS_UNSUPPORTED;
    }
    size_log2 = texture->width_log2 > texture->height_log2 ?
                texture->width_log2 : texture->height_log2;
    if (texture->levels == 0ul || texture->levels > size_log2 + 1ul ||
        texture->levels > V9X_R128_TEXTURE_LEVELS) {
        return V9X_STATUS_UNSUPPORTED;
    }
    /* A mip filter needs a chain; without one the sampler is told so. */
    if (texture->levels == 1ul &&
        texture->min_filter > V9X_R128_FILTER_LINEAR) {
        return V9X_STATUS_UNSUPPORTED;
    }
    combine = v9x_r128_3d_combine(texture, unit);
    if (combine == 0xfffffffful) {
        return V9X_STATUS_UNSUPPORTED;
    }
    for (index = 0ul; index < texture->levels; ++index) {
        if (texture->offsets[index] % V9X_R128_OFFSET_ALIGN != 0ul ||
            texture->offsets[index] >= vram_bytes) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }
    min_log2 = size_log2 - (texture->levels - 1ul);

    control = (texture->min_filter << R128_MIN_FILTER_SHIFT) |
              (texture->mag_filter << R128_MAG_FILTER_SHIFT) |
              (texture->datatype << R128_TEX_DATATYPE_SHIFT);
    if (texture->levels == 1ul) {
        control |= R128_MIP_MAP_DISABLE;
    }
    if (texture->clamp_s != 0ul) {
        control |= R128_TEX_CLAMP_S_CLAMP;
    }
    if (texture->clamp_t != 0ul) {
        control |= R128_TEX_CLAMP_T_CLAMP;
    }
    if (unit != 0ul) {
        control |= R128_SEC_SELECT_SEC_ST;
    }

    words->control = control;
    words->combine = combine;
    words->size_pitch = texture->width_log2 |
                        (size_log2 << R128_TEX_SIZE_SHIFT) |
                        (texture->height_log2 << R128_TEX_HEIGHT_SHIFT) |
                        (min_log2 << R128_TEX_MIN_SIZE_SHIFT);
    /* The offset registers count up from the smallest level: register k
     * holds level levels-1-k, as Mesa's r128UploadTexImages writes them.
     * Written base-first, a chain had the sampler read its 1x1 level as the
     * base (3DMark 2000's helicopter, OBS, A8U4I5 boot 527). Registers past
     * the chain hold the base. */
    for (index = 0ul; index < V9X_R128_TEXTURE_LEVELS; ++index) {
        words->offsets[index] = texture->offsets[0];
        if (index < texture->levels) {
            words->offsets[index] =
                texture->offsets[texture->levels - 1ul - index];
        }
    }
    return V9X_STATUS_OK;
}

/* The texture blocks: unit 0's control, combine, both units' sizes and
 * its offsets; then unit 1's control, combine and offsets, as
 * r128_emit_tex0 and r128_emit_tex1 write them. */
static void v9x_r128_3d_texture_blocks(
    const struct v9x_r128_3d_unit_words *unit0,
    const struct v9x_r128_3d_unit_words *unit1,
    struct v9x_r128_3d_stream *stream)
{
    v9x_u32 index;

    v9x_r128_3d_emit(stream, V9X_R128_PACKET0(V9X_R128_PRIM_TEX_CNTL_C,
                                              V9X_R128_TEXTURE_REGS - 1u));
    v9x_r128_3d_emit(stream, unit0->control);
    v9x_r128_3d_emit(stream, unit0->combine);
    v9x_r128_3d_emit(stream, unit0->size_pitch |
                             (unit1 != 0 ? unit1->size_pitch <<
                                           R128_SEC_TEX_SIZE_PITCH_SHIFT
                                         : 0ul));
    for (index = 0ul; index < V9X_R128_TEXTURE_LEVELS; ++index) {
        v9x_r128_3d_emit(stream, unit0->offsets[index]);
    }
    if (unit1 == 0) {
        return;
    }
    v9x_r128_3d_emit(stream, V9X_R128_PACKET0(R128_SEC_TEX_CNTL_C,
                                              R128_SEC_TEXTURE_REGS - 1u));
    v9x_r128_3d_emit(stream, unit1->control);
    v9x_r128_3d_emit(stream, unit1->combine);
    for (index = 0ul; index < V9X_R128_TEXTURE_LEVELS; ++index) {
        v9x_r128_3d_emit(stream, unit1->offsets[index]);
    }
}

/* PLANE_3D_MASK_C: every bit written but the masked channels'. A 32 bpp
 * target is XRGB8888 to DirectDraw but the engine's only 32-bit datatype is
 * ARGB8888, which writes alpha into the X byte (V9XD32 read 0xFF13579B on
 * A8U4I5); the alpha byte is always kept. */
static v9x_u32 v9x_r128_3d_plane_mask(v9x_u32 bytes_per_pixel,
                                      v9x_u32 masked_channels)
{
    v9x_u32 mask;

    if (bytes_per_pixel == 4ul) {
        mask = R128_PLANE_8888_ALL;
        if ((masked_channels & V9X_R128_CHANNEL_RED) != 0ul) {
            mask &= ~R128_PLANE_8888_RED;
        }
        if ((masked_channels & V9X_R128_CHANNEL_GREEN) != 0ul) {
            mask &= ~R128_PLANE_8888_GREEN;
        }
        if ((masked_channels & V9X_R128_CHANNEL_BLUE) != 0ul) {
            mask &= ~R128_PLANE_8888_BLUE;
        }
        return mask;
    }
    mask = 0xfffffffful;
    if ((masked_channels & V9X_R128_CHANNEL_RED) != 0ul) {
        mask &= ~R128_PLANE_565_RED;
    }
    if ((masked_channels & V9X_R128_CHANNEL_GREEN) != 0ul) {
        mask &= ~R128_PLANE_565_GREEN;
    }
    if ((masked_channels & V9X_R128_CHANNEL_BLUE) != 0ul) {
        mask &= ~R128_PLANE_565_BLUE;
    }
    return mask;
}

v9x_status v9x_r128_3d_build_state(const struct v9x_r128_3d_state *state,
                                   struct v9x_r128_3d_stream *stream)
{
    v9x_u32 scissor_top_left = 0ul;
    v9x_u32 scissor_bottom_right = R128_SC_C_MAX;
    v9x_u32 datatype;
    v9x_u32 target_units;
    v9x_u32 depth_units = 0ul;
    v9x_u32 tex_cntl;
    v9x_u32 blend;
    v9x_u32 scale;
    struct v9x_r128_3d_unit_words unit0;
    struct v9x_r128_3d_unit_words unit1;
    int two_units;
    v9x_status status;

    if (state == 0 || stream == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    stream->count = 0u;

    datatype = v9x_r128_3d_datatype(state->target_bytes_per_pixel);
    if (datatype == 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    target_units = v9x_r128_3d_surface_units(state->vram_bytes,
                                             state->target_offset,
                                             state->target_pitch_bytes,
                                             state->target_bytes_per_pixel);
    if (target_units == 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (state->depth_enable != 0ul) {
        if (state->depth_bytes_per_pixel != 2ul ||
            state->depth_func > V9X_R128_CMP_ALWAYS) {
            return V9X_STATUS_UNSUPPORTED;
        }
        depth_units = v9x_r128_3d_surface_units(state->vram_bytes,
                                                state->depth_offset,
                                                state->depth_pitch_bytes,
                                                2ul);
        if (depth_units == 0ul) {
            return V9X_STATUS_UNSUPPORTED;
        }
    }
    if (state->src_blend > V9X_R128_BLEND_SAT ||
        state->dst_blend > V9X_R128_BLEND_SAT ||
        state->alpha_func > V9X_R128_CMP_ALWAYS) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (state->scissor_enable != 0ul) {
        if (state->scissor_left >= state->scissor_right ||
            state->scissor_top >= state->scissor_bottom ||
            state->scissor_right > R128_SC_LIMIT ||
            state->scissor_bottom > R128_SC_LIMIT) {
            return V9X_STATUS_UNSUPPORTED;
        }
        scissor_top_left = state->scissor_left |
                           (state->scissor_top << R128_SC_Y_SHIFT);
        scissor_bottom_right = (state->scissor_right - 1ul) |
                               ((state->scissor_bottom - 1ul) <<
                                R128_SC_Y_SHIFT);
    }

    /* The texture units before anything is written: the second only runs
     * behind the first (Mesa moves a lone GL_TEXTURE1 onto unit 0). */
    two_units = state->texture1.enable != 0ul;
    if (two_units && state->texture.enable == 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (state->texture.enable != 0ul) {
        status = v9x_r128_3d_unit_words(&state->texture, 0ul,
                                        state->vram_bytes, &unit0);
        if (status != V9X_STATUS_OK) {
            return status;
        }
    }
    if (two_units) {
        status = v9x_r128_3d_unit_words(&state->texture1, 1ul,
                                        state->vram_bytes, &unit1);
        if (status != V9X_STATUS_OK) {
            return status;
        }
    }

    tex_cntl = R128_TEX_CACHE_FLUSH | R128_LOD_BIAS_DEFAULT;
    if (state->depth_enable != 0ul) {
        tex_cntl |= R128_Z_ENABLE;
        if (state->depth_write != 0ul) {
            tex_cntl |= R128_Z_WRITE_ENABLE;
        }
    }
    if (state->texture.enable != 0ul) {
        tex_cntl |= R128_TEXMAP_ENABLE;
    }
    if (two_units) {
        tex_cntl |= R128_SEC_TEXMAP_ENABLE;
    }
    if (state->fog_enable != 0ul) {
        tex_cntl |= R128_FOG_ENABLE;
    }
    if (state->dither_enable != 0ul) {
        tex_cntl |= R128_DITHER_ENABLE;
    }
    if (state->blend_enable != 0ul) {
        tex_cntl |= R128_ALPHA_ENABLE;
    }
    if (state->alpha_test_enable != 0ul) {
        tex_cntl |= R128_ALPHA_TEST_ENABLE;
    }
    if (state->specular_enable != 0ul) {
        tex_cntl |= R128_SPEC_LIGHT_ENABLE;
    }
    blend = (state->blend_enable != 0ul ?
             (state->src_blend << R128_BLEND_SRC_SHIFT) |
             (state->dst_blend << R128_BLEND_DST_SHIFT) :
             (V9X_R128_BLEND_ONE << R128_BLEND_SRC_SHIFT) |
             (V9X_R128_BLEND_ZERO << R128_BLEND_DST_SHIFT)) |
            ((state->alpha_test_enable != 0ul ? state->alpha_func :
              V9X_R128_CMP_ALWAYS) << R128_ALPHA_TEST_SHIFT);

    v9x_r128_3d_emit(stream, V9X_R128_PACKET0(V9X_R128_PC_GUI_CTLSTAT, 0u));
    v9x_r128_3d_emit(stream, V9X_R128_PC_RI_GUI | V9X_R128_PC_FLUSH_GUI);

    /* The context block, as r128_emit_context writes it. */
    v9x_r128_3d_emit(stream, V9X_R128_PACKET0(V9X_R128_DST_PITCH_OFFSET_C,
                                              V9X_R128_CONTEXT_REGS - 1u));
    v9x_r128_3d_emit(stream, (target_units << V9X_R128_PITCH_SHIFT) |
                             (state->target_offset / V9X_R128_OFFSET_ALIGN));
    v9x_r128_3d_emit(stream, V9X_R128_GMC_DST_PITCH_OFFSET_CNTL |
                             R128_GMC_DST_CLIPPING |
                             V9X_R128_GMC_BRUSH_SOLID_COLOR |
                             (datatype << V9X_R128_GMC_DST_DATATYPE_SHIFT) |
                             V9X_R128_GMC_SRC_DATATYPE_COLOR |
                             (V9X_R128_ROP3_S << V9X_R128_GMC_ROP3_SHIFT) |
                             V9X_R128_DP_SRC_SOURCE_MEMORY |
                             R128_GMC_3D_FCN_EN |
                             V9X_R128_GMC_CLR_CMP_CNTL_DIS |
                             V9X_R128_GMC_AUX_CLIP_DIS |
                             R128_GMC_WR_MSK_DIS);
    v9x_r128_3d_emit(stream, scissor_top_left);
    v9x_r128_3d_emit(stream, scissor_bottom_right);
    v9x_r128_3d_emit(stream, state->depth_enable != 0ul ?
                             state->depth_offset : 0ul);
    v9x_r128_3d_emit(stream, depth_units);
    v9x_r128_3d_emit(stream, ((state->depth_enable != 0ul ?
                               state->depth_func : V9X_R128_CMP_ALWAYS) <<
                              R128_Z_TEST_SHIFT) |
                             R128_STENCIL_TEST_ALWAYS);
    v9x_r128_3d_emit(stream, tex_cntl);
    v9x_r128_3d_emit(stream, R128_MISC_3D_BASE | blend |
                             (state->alpha_ref & R128_REF_ALPHA_MASK));
    v9x_r128_3d_emit(stream, 0ul);
    v9x_r128_3d_emit(stream, 0xfffffffful);
    v9x_r128_3d_emit(stream, state->fog_color & 0x00fffffful);

    v9x_r128_3d_emit(stream, V9X_R128_PACKET0(V9X_R128_SETUP_CNTL, 0u));
    v9x_r128_3d_emit(stream, R128_SETUP_BASE |
                             (state->flat_shade != 0ul ?
                              R128_COLOR_FLAT_VERT_1 : R128_COLOR_GOURAUD));
    v9x_r128_3d_emit(stream, V9X_R128_PACKET0(V9X_R128_PM4_VC_FPU_SETUP,
                                              0u));
    v9x_r128_3d_emit(stream, (R128_FPU_BASE & ~R128_FPU_FACE_MASK) |
                             (state->cull == V9X_R128_CULL_CCW ?
                              R128_FPU_FACE_KEEP_SCREEN_CW :
                              state->cull == V9X_R128_CULL_CW ?
                              R128_FPU_FACE_KEEP_SCREEN_CCW :
                              R128_FPU_FACE_BOTH) |
                             (state->flat_shade != 0ul ?
                              R128_FPU_COLOR_FLAT : R128_FPU_COLOR_GOURAUD));
    scale = R128_SCALE_3D_BASE | blend;
    if (two_units) {
        scale |= R128_TEX_CACHE_SPLIT;
    }
    v9x_r128_3d_emit(stream, V9X_R128_PACKET0(V9X_R128_SCALE_3D_CNTL_REG,
                                              0u));
    v9x_r128_3d_emit(stream, scale);
    v9x_r128_3d_emit(stream, V9X_R128_PACKET0(V9X_R128_WINDOW_XY_OFFSET,
                                              0u));
    v9x_r128_3d_emit(stream, 0ul);
    /* CONSTANT_COLOR_C, both border colours, STEN_REF_MASK_C,
     * PLANE_3D_MASK_C. */
    v9x_r128_3d_emit(stream, V9X_R128_PACKET0(V9X_R128_CONSTANT_COLOR_C,
                                              4u));
    v9x_r128_3d_emit(stream, R128_CONSTANT_COLOR);
    v9x_r128_3d_emit(stream, 0ul);
    v9x_r128_3d_emit(stream, 0ul);
    v9x_r128_3d_emit(stream, R128_STEN_REF_MASK);
    v9x_r128_3d_emit(stream,
                     v9x_r128_3d_plane_mask(state->target_bytes_per_pixel,
                                            state->masked_channels));

    if (state->texture.enable != 0ul) {
        v9x_r128_3d_texture_blocks(&unit0, two_units ? &unit1 : 0, stream);
    }
    return V9X_STATUS_OK;
}

void v9x_r128_3d_build_flush(v9x_u32 out[V9X_R128_FLUSH_DWORDS])
{
    out[0] = V9X_R128_PACKET0(V9X_R128_PC_GUI_CTLSTAT, 0u);
    out[1] = V9X_R128_PC_FLUSH_GUI;
}

v9x_status v9x_r128_3d_build_indexed(v9x_u32 index_count, int strip,
                                     v9x_u32 vertex_offset,
                                     v9x_u32 vertex_count,
                                     v9x_u32 header[V9X_R128_INDX_HEADER_DWORDS])
{
    v9x_u32 index_dwords;

    if (header == 0 || index_count < 3ul ||
        index_count > V9X_R128_INDX_INDICES_MAX || vertex_count == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    index_dwords = (index_count + 1ul) / 2ul;
    header[0] = V9X_R128_PACKET3(V9X_R128_PACKET3_3D_RNDR_GEN_INDX_PRIM,
                                 4ul + index_dwords - 1ul);
    header[1] = vertex_offset;
    header[2] = vertex_count;
    header[3] = R128_VC_FRMT_TL;
    header[4] = (strip ? R128_VC_CNTL_IND_TRI_STRIP :
                         R128_VC_CNTL_IND_TRI_LIST) |
                (index_count << R128_VC_CNTL_COUNT_SHIFT);
    return V9X_STATUS_OK;
}

v9x_u32 v9x_r128_3d_pack_indices(const v9x_u16 *indices, v9x_u32 count,
                                 v9x_u32 base, v9x_u32 *out)
{
    v9x_u32 at;
    v9x_u32 written = 0ul;

    for (at = 0ul; at + 1ul < count; at += 2ul) {
        out[written++] = (((v9x_u32)indices[at] - base) & 0xfffful) |
                         ((((v9x_u32)indices[at + 1ul] - base) & 0xfffful)
                          << 16);
    }
    if (at < count) {
        out[written++] = ((v9x_u32)indices[at] - base) & 0xfffful;
    }
    return written;
}

v9x_status v9x_r128_3d_build_list(v9x_u32 vertex_count,
                                  v9x_u32 vertex_offset, int two_units,
                                  v9x_u32 header[V9X_R128_LIST_HEADER_DWORDS])
{
    if (header == 0 || vertex_count == 0ul || vertex_count % 3ul != 0ul ||
        vertex_count > V9X_R128_LIST_VERTICES_MAX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* Four body dwords and no indices: n is 3. */
    header[0] = V9X_R128_PACKET3(V9X_R128_PACKET3_3D_RNDR_GEN_INDX_PRIM, 3ul);
    header[1] = vertex_offset;
    header[2] = vertex_count;
    header[3] = R128_VC_FRMT_TL | (two_units ? R128_VC_FRMT_S2_T2 : 0ul);
    header[4] = R128_VC_CNTL_LIST_TRI_LIST |
                (vertex_count << R128_VC_CNTL_COUNT_SHIFT);
    return V9X_STATUS_OK;
}

v9x_status v9x_r128_3d_build_prim(v9x_u32 triangle_count, int two_units,
                                  v9x_u32 header[V9X_R128_PRIM_HEADER_DWORDS])
{
    v9x_u32 vertices;

    if (header == 0 || triangle_count == 0ul ||
        triangle_count > V9X_R128_PRIM_TRIANGLES_MAX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    vertices = triangle_count * 3ul;
    /* Format and control, then the vertices: n counts them less one. */
    header[0] = V9X_R128_PACKET3(V9X_R128_PACKET3_3D_RNDR_GEN_PRIM,
                                 2ul + vertices *
                                       (two_units ? V9X_R128_VERTEX2_DWORDS
                                                  : V9X_R128_VERTEX_DWORDS) -
                                 1ul);
    header[1] = R128_VC_FRMT_TL | (two_units ? R128_VC_FRMT_S2_T2 : 0ul);
    header[2] = R128_VC_CNTL_TRI_LIST |
                (vertices << R128_VC_CNTL_COUNT_SHIFT);
    return V9X_STATUS_OK;
}
