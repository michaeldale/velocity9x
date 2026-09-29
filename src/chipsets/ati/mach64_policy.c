#include "velocity9x/ati_mach64_engine.h"

/*
 * Mobility-M draw acceptance.  Every accepted state has an isolated passing
 * Phase 4 scene on the Gateway Solo 2150 (docs/probe/ati-rage-mobility-m-
 * phase4-*); everything else is refused here, before a FIFO slot is
 * reserved, so an unsupported draw never emits a partial command stream.
 * Units measured separately (depth, texture, blend, alpha test, scissor)
 * are allowed together; combinations that share hardware are not: fog
 * borrows the blend unit and, per Utah-GLX, the texture-alpha path.
 */

/* Numbering shared with Direct3D and V9X_R3D_*. */
#define M64_TARGET_RGB565     1ul
#define M64_WRITE_MASK_RGB    7ul
#define M64_SHADE_FLAT        1ul
#define M64_SHADE_GOURAUD     2ul
#define M64_CMP_FIRST         1ul
#define M64_CMP_LAST          8ul
#define M64_ALPHA_REF_MAX     255ul
#define M64_DEPTH_BITS        16ul

#define M64_BLEND_ZERO         1ul
#define M64_BLEND_ONE          2ul
#define M64_BLEND_SRCCOLOR     3ul
#define M64_BLEND_INVSRCCOLOR  4ul
#define M64_BLEND_SRCALPHA     5ul
#define M64_BLEND_INVSRCALPHA  6ul
#define M64_BLEND_DESTCOLOR    9ul
#define M64_BLEND_INVDESTCOLOR 10ul

#define M64_FILTER_NEAREST          1ul
#define M64_FILTER_LINEAR           2ul
#define M64_FILTER_MIPNEAREST       3ul
#define M64_FILTER_LINEARMIPNEAREST 5ul
#define M64_FILTER_LINEARMIPLINEAR  6ul
#define M64_ADDRESS_WRAP   1ul
#define M64_ADDRESS_CLAMP  3ul

#define M64_TEXOP_DECAL      1ul
#define M64_TEXOP_MODULATE   2ul
#define M64_TEXOP_DECALALPHA 3ul
#define M64_TEXOP_COPY       7ul

/* Square power-of-two textures from 8 to 256 texels.  The Phase 4 scenes
 * bound only 8x8; the HAL probe's halves scene samples every size in the
 * range through the HAL (docs/probe/ati-rage-mobility-m-hal-d3d-*).  The
 * size and pitch encoding is a log2 field and the builder takes up to 1024,
 * but nothing above 256 has been drawn; raise the maximum only with a scene
 * that samples the larger texture.  Non-square textures are unmeasured. */
#define M64_TEXTURE_EDGE_MIN 8ul
#define M64_TEXTURE_EDGE_MAX 256ul

/*
 * Item 9 proved all 36 pairs of these.  Destination-alpha factors and
 * SRCALPHASAT are absent: RGB565 carries no alpha, and what the blender
 * reads in its place was not measured.
 */
static int v9x_m64_policy_source_factor(v9x_u32 factor)
{
    switch (factor) {
    case M64_BLEND_ZERO:
    case M64_BLEND_ONE:
    case M64_BLEND_SRCALPHA:
    case M64_BLEND_INVSRCALPHA:
    case M64_BLEND_DESTCOLOR:
    case M64_BLEND_INVDESTCOLOR:
        return 1;
    default:
        return 0;
    }
}

static int v9x_m64_policy_dest_factor(v9x_u32 factor)
{
    switch (factor) {
    case M64_BLEND_ZERO:
    case M64_BLEND_ONE:
    case M64_BLEND_SRCCOLOR:
    case M64_BLEND_INVSRCCOLOR:
    case M64_BLEND_SRCALPHA:
    case M64_BLEND_INVSRCALPHA:
        return 1;
    default:
        return 0;
    }
}

static v9x_u32 v9x_m64_policy_log2(v9x_u32 value)
{
    v9x_u32 result = 0ul;

    while (value > 1ul) {
        value >>= 1;
        ++result;
    }
    return result;
}

static int v9x_m64_policy_format_has_alpha(v9x_u32 format)
{
    return format == V9X_M64_TEXTURE_FORMAT_ARGB1555 ||
           format == V9X_M64_TEXTURE_FORMAT_ARGB4444;
}

static v9x_u32 v9x_m64_policy_texture(
                              const struct v9x_m64_draw_request *request,
                              struct v9x_m64_draw_decision *decision)
{
    int has_alpha;

    if (request->texture_format != V9X_M64_TEXTURE_FORMAT_RGB565 &&
        !v9x_m64_policy_format_has_alpha(request->texture_format)) {
        return V9X_M64_REFUSE_TEXTURE_FORMAT;
    }
    if (request->texture_width != request->texture_height ||
        request->texture_width < M64_TEXTURE_EDGE_MIN ||
        request->texture_width > M64_TEXTURE_EDGE_MAX ||
        (request->texture_width & (request->texture_width - 1ul)) != 0ul) {
        return V9X_M64_REFUSE_TEXTURE_SHAPE;
    }
    /* One level per halving at most: an 8x8 chain is 8, 4, 2, 1. */
    if (request->texture_levels == 0ul ||
        request->texture_levels > v9x_m64_policy_log2(request->texture_width)
                                  + 1ul) {
        return V9X_M64_REFUSE_TEXTURE_MIP;
    }

    /*
     * A chain may select a level, nearest or bilinear within it, or blend
     * two levels bilinearly (LINEARMIPLINEAR, the engine's TRILINEAR
     * function, which Mesa's driver used only to blend two textures).
     * MIPLINEAR, nearest within levels blended between them, has no engine
     * function. A single level takes no mip filter; the HAL folds one to
     * its base filter first, as Direct3D defines it.
     */
    if (request->texture_mag_filter != M64_FILTER_NEAREST &&
        request->texture_mag_filter != M64_FILTER_LINEAR) {
        return V9X_M64_REFUSE_TEXTURE_FILTER;
    }
    if (request->texture_min_filter != M64_FILTER_NEAREST &&
        request->texture_min_filter != M64_FILTER_LINEAR &&
        (request->texture_levels == 1ul ||
         (request->texture_min_filter != M64_FILTER_MIPNEAREST &&
          request->texture_min_filter != M64_FILTER_LINEARMIPNEAREST &&
          request->texture_min_filter != M64_FILTER_LINEARMIPLINEAR))) {
        return V9X_M64_REFUSE_TEXTURE_FILTER;
    }

    /* Mirror and border have no scene; cylindrical WRAPU/WRAPV change the
     * interpolation, which nothing measured. */
    if ((request->texture_address != M64_ADDRESS_WRAP &&
         request->texture_address != M64_ADDRESS_CLAMP) ||
        request->texture_wrap_u != 0ul || request->texture_wrap_v != 0ul) {
        return V9X_M64_REFUSE_TEXTURE_ADDRESS;
    }

    /*
     * Item 10 measured REPLACE (C=Ct, A=At), MODULATE (C=CtCf, A=At, which
     * is D3D's MODULATE and not MODULATEALPHA) and ALPHA_DECAL.  ALPHA_DECAL
     * on RGB565 weights by vertex alpha instead of giving Ct, so DECALALPHA
     * there is REPLACE.  No mode yields A=AtAf, so MODULATEALPHA refuses.
     */
    has_alpha = v9x_m64_policy_format_has_alpha(request->texture_format);
    switch (request->texture_op) {
    case M64_TEXOP_DECAL:
    case M64_TEXOP_COPY:
        decision->light_fcn = V9X_M64_TEX_LIGHT_FCN_REPLACE;
        break;
    case M64_TEXOP_MODULATE:
        decision->light_fcn = V9X_M64_TEX_LIGHT_FCN_MODULATE;
        break;
    case M64_TEXOP_DECALALPHA:
        decision->light_fcn = has_alpha
            ? V9X_M64_TEX_LIGHT_FCN_ALPHA_DECAL
            : V9X_M64_TEX_LIGHT_FCN_REPLACE;
        break;
    default:
        return V9X_M64_REFUSE_TEXTURE_OP;
    }
    decision->texture_alpha = has_alpha ? V9X_M64_TEX_MAP_AEN : 0ul;
    return V9X_M64_REFUSE_NONE;
}

static v9x_u32 v9x_m64_policy_check(
                              const struct v9x_m64_draw_request *request,
                              struct v9x_m64_draw_decision *decision)
{
    v9x_u32 reason;

    /* Only RGB565 targets were drawn; XRGB1555 waits for its own scene. */
    if (request->target_format != M64_TARGET_RGB565 ||
        request->target_width == 0ul || request->target_height == 0ul) {
        return V9X_M64_REFUSE_TARGET_FORMAT;
    }
    if (request->scissor_left >= request->scissor_right ||
        request->scissor_top >= request->scissor_bottom ||
        request->scissor_right > request->target_width ||
        request->scissor_bottom > request->target_height) {
        return V9X_M64_REFUSE_SCISSOR;
    }

    /* DP_WRITE_MASK was only ever all-ones. */
    if (request->write_mask != M64_WRITE_MASK_RGB) {
        return V9X_M64_REFUSE_WRITE_MASK;
    }
    if (request->shade_mode != M64_SHADE_FLAT &&
        request->shade_mode != M64_SHADE_GOURAUD) {
        return V9X_M64_REFUSE_SHADE;
    }
    if (request->depth_enable != 0ul &&
        (request->depth_bits != M64_DEPTH_BITS ||
         request->depth_func < M64_CMP_FIRST ||
         request->depth_func > M64_CMP_LAST)) {
        return V9X_M64_REFUSE_DEPTH;
    }

    if (request->textured != 0ul) {
        reason = v9x_m64_policy_texture(request, decision);
        if (reason != V9X_M64_REFUSE_NONE) {
            return reason;
        }
    }

    if (request->blend_enable != 0ul &&
        (!v9x_m64_policy_source_factor(request->src_blend) ||
         !v9x_m64_policy_dest_factor(request->dst_blend))) {
        return V9X_M64_REFUSE_BLEND_FACTOR;
    }

    /* Items 7 and 8 compared texel alpha only.  The vertex-alpha source is
     * a separate ALPHA_TST_CNTL bit that no scene set. */
    if (request->alpha_test_enable != 0ul &&
        (request->alpha_func < M64_CMP_FIRST ||
         request->alpha_func > M64_CMP_LAST ||
         request->alpha_ref > M64_ALPHA_REF_MAX ||
         request->textured == 0ul ||
         !v9x_m64_policy_format_has_alpha(request->texture_format))) {
        return V9X_M64_REFUSE_ALPHA_TEST;
    }

    /* Fog is the blend unit mixing with DP_FOG_CLR, so it cannot also
     * blend with the framebuffer.  Item 12 measured it untextured; over a
     * texture, as Mesa's driver fogs, the HAL probe's D3DFogTex scene
     * (2026-09-29). */
    if (request->fog_enable != 0ul && request->blend_enable != 0ul) {
        return V9X_M64_REFUSE_FOG_WITH_BLEND;
    }

    /* Specular is ALPHA_TST_CNTL SPECULAR_LIGHT_EN over the specular words'
     * RGB (Mesa's driver); with fog the same words also carry the fog
     * factor, a combination not measured.  Colour key and forced alpha
     * have no scene. */
    if (request->specular_enable != 0ul && request->fog_enable != 0ul) {
        return V9X_M64_REFUSE_SPECULAR;
    }
    if (request->color_key_enable != 0ul) {
        return V9X_M64_REFUSE_COLOR_KEY;
    }
    if (request->alpha_force != 0ul) {
        return V9X_M64_REFUSE_ALPHA_FORCE;
    }
    return V9X_M64_REFUSE_NONE;
}

v9x_u32 v9x_m64_check_draw(const struct v9x_m64_draw_request *request,
                           struct v9x_m64_draw_decision *decision)
{
    v9x_u32 reason;

    if (decision != 0) {
        decision->light_fcn = 0ul;
        decision->texture_alpha = 0ul;
    }
    if (request == 0 || decision == 0) {
        return V9X_M64_REFUSE_ARGUMENT;
    }

    reason = v9x_m64_policy_check(request, decision);
    if (reason != V9X_M64_REFUSE_NONE) {
        decision->light_fcn = 0ul;
        decision->texture_alpha = 0ul;
    }
    return reason;
}
