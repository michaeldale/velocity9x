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
#define M64_CMP_GREATER       5ul
#define M64_CMP_NOTEQUAL      6ul
#define M64_CMP_GREATEREQUAL  7ul
#define M64_CMP_ALWAYS        8ul
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
#define M64_FILTER_MIPLINEAR        4ul
#define M64_FILTER_LINEARMIPLINEAR  6ul
#define M64_ADDRESS_WRAP   1ul
#define M64_ADDRESS_CLAMP  3ul

#define M64_TEXOP_DECAL      1ul
#define M64_TEXOP_MODULATE   2ul
#define M64_TEXOP_DECALALPHA 3ul
#define M64_TEXOP_MODULATEALPHA 4ul
#define M64_TEXOP_COPY       7ul
#define M64_BLEND_SRCALPHASAT 11ul

/* Power-of-two edges from 2 to 256 texels, each on its own.  The Phase 4
 * scenes bound only 8x8; the HAL probe's halves scene samples every square
 * size in the range through the HAL (docs/probe/ati-rage-mobility-m-hal-d3d-*;
 * 4 and 2 on the Rage XL PCI, 2026-10-03, docs/probe/a8u4i5-rage-xl-pci-*),
 * and the texture-shape probe samples rectangles, single levels and chains
 * (2026-10-01).  1x1 has no halves scene and stays out.  The size and pitch
 * encoding is a log2 field and the builder takes up to 1024, but nothing
 * above 256 has been drawn; raise the maximum only with a scene that
 * samples the larger texture. */
#define M64_TEXTURE_EDGE_MIN 2ul
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

static int v9x_m64_policy_factor_reads_alpha(v9x_u32 factor)
{
    return factor == M64_BLEND_SRCALPHA || factor == M64_BLEND_INVSRCALPHA ||
           factor == M64_BLEND_SRCALPHASAT;
}

static int v9x_m64_policy_format_has_alpha(v9x_u32 format);

/* Whether the comparison passes for every alpha from `least` to 255. */
static int v9x_m64_policy_passes_from(v9x_u32 least, v9x_u32 func,
                                      v9x_u32 ref)
{
    switch (func) {
    case M64_CMP_GREATER:
    case M64_CMP_NOTEQUAL:     return least > ref;
    case M64_CMP_GREATEREQUAL: return least >= ref;
    case M64_CMP_ALWAYS:       return 1;
    default:                   return 0;
    }
}

/*
 * An alpha test that is no test: no texel alpha, so the tested alpha is
 * the vertex's (DECAL and COPY give the texel's, 255 without alpha), and
 * the comparison passes from the batch's least of it up. The engine's
 * vertex-alpha test source was never measured; a test that cannot discard
 * is simply not sent. Half-Life's HUD tests NOTEQUAL 0 over RGB565 sprites
 * and was refused whole (15,018 batches a run on the Rage XL, 2026-10-04).
 */
static int v9x_m64_policy_alpha_test_droppable(
                              const struct v9x_m64_draw_request *request)
{
    v9x_u32 least = request->vertex_alpha_min;

    if (request->alpha_test_enable == 0ul ||
        (request->textured != 0ul &&
         v9x_m64_policy_format_has_alpha(request->texture_format))) {
        return 0;
    }
    if (request->textured != 0ul &&
        (request->texture_op == M64_TEXOP_DECAL ||
         request->texture_op == M64_TEXOP_COPY)) {
        least = M64_ALPHA_REF_MAX;
    }
    return request->alpha_ref <= M64_ALPHA_REF_MAX &&
           v9x_m64_policy_passes_from(least, request->alpha_func,
                                      request->alpha_ref);
}

/* Whether anything after the texture stage reads the fragment's alpha:
 * the alpha test, or a blend factor of the source alpha. */
static int v9x_m64_policy_fragment_alpha_read(
                              const struct v9x_m64_draw_request *request)
{
    if (request->alpha_test_enable != 0ul &&
        !v9x_m64_policy_alpha_test_droppable(request)) {
        return 1;
    }
    return request->blend_enable != 0ul &&
           (v9x_m64_policy_factor_reads_alpha(request->src_blend) ||
            v9x_m64_policy_factor_reads_alpha(request->dst_blend));
}

/* One edge of a texture the engine may sample: a power of two in range. */
static int v9x_m64_policy_edge(v9x_u32 edge)
{
    return edge >= M64_TEXTURE_EDGE_MIN && edge <= M64_TEXTURE_EDGE_MAX &&
           (edge & (edge - 1ul)) == 0ul;
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
    if (!v9x_m64_policy_edge(request->texture_width) ||
        !v9x_m64_policy_edge(request->texture_height)) {
        return V9X_M64_REFUSE_TEXTURE_SHAPE;
    }
    /* One level per halving of the larger edge at most: an 8x8 chain is
     * 8, 4, 2, 1, and a 64x16 one runs 64x16 to 1x1 in seven. */
    if (request->texture_levels == 0ul ||
        request->texture_levels >
            v9x_m64_policy_log2(request->texture_width >
                                    request->texture_height
                                ? request->texture_width
                                : request->texture_height) + 1ul) {
        return V9X_M64_REFUSE_TEXTURE_MIP;
    }

    /*
     * A chain may select a level, nearest or bilinear within it
     * (MIPNEAREST, MIPLINEAR), or blend two levels bilinearly
     * (LINEARMIPLINEAR, the engine's TRILINEAR function, which Mesa's
     * driver used only to blend two textures). LINEARMIPNEAREST, nearest
     * within two levels blended between them, has no engine function. A
     * single level takes no mip filter; the HAL folds one to its base
     * filter first, as Direct3D defines it.
     */
    if (request->texture_mag_filter != M64_FILTER_NEAREST &&
        request->texture_mag_filter != M64_FILTER_LINEAR) {
        return V9X_M64_REFUSE_TEXTURE_FILTER;
    }
    if (request->texture_min_filter != M64_FILTER_NEAREST &&
        request->texture_min_filter != M64_FILTER_LINEAR &&
        (request->texture_levels == 1ul ||
         (request->texture_min_filter != M64_FILTER_MIPNEAREST &&
          request->texture_min_filter != M64_FILTER_MIPLINEAR &&
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
     * there is REPLACE.  No mode yields A=AtAf, so MODULATEALPHA is MODULATE
     * only where the difference cannot be seen: nothing reads the alpha, or
     * every vertex alpha is 255 and AtAf is At.  Half-Life's Direct3D
     * renderer draws its world this way; refusing it left three quarters of
     * its batches undrawn on the Gateway (2026-10-01).
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
    case M64_TEXOP_MODULATEALPHA:
        if (v9x_m64_policy_fragment_alpha_read(request) &&
            request->vertex_alpha_opaque == 0ul) {
            return V9X_M64_REFUSE_TEXTURE_OP;
        }
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

/* The render interface's combine numbers (r3d_abi.h), which a second
 * texture's request carries. */
#define M64_COLOROP_MODULATE 2ul
#define M64_ALPHAOP_FRAGMENT 0ul

/*
 * A second texture, modulated with the first in the same pass. Mesa 7.10's
 * driver is the reference: it drew unit 1's MODULATE through the composite
 * and sent every other unit-1 environment to software, and it ran every
 * texture with MIP_MAP_DISABLE, which the builder does here too, so a mip
 * filter samples level 0. The composite is formed before TEX_LIGHT_FCN
 * (ATI's Rage Pro note), so unit 0's REPLACE and MODULATE are the light
 * function over the product, while its DECAL by texel alpha would lerp the
 * product, not unit 0's colour, and is refused. COMP_ALPHA is never set,
 * so the fragment keeps unit 0's alpha: unit 1's own alpha is allowed only
 * where nothing reads it. Fog and specular over a composite have no scene.
 */
static v9x_u32 v9x_m64_policy_composite(
                              const struct v9x_m64_draw_request *request)
{
    if (request->composite_format != V9X_M64_TEXTURE_FORMAT_RGB565 &&
        !v9x_m64_policy_format_has_alpha(request->composite_format)) {
        return V9X_M64_REFUSE_COMPOSITE;
    }
    if (!v9x_m64_policy_edge(request->composite_width) ||
        !v9x_m64_policy_edge(request->composite_height)) {
        return V9X_M64_REFUSE_COMPOSITE;
    }
    if (request->composite_mag_filter != M64_FILTER_NEAREST &&
        request->composite_mag_filter != M64_FILTER_LINEAR) {
        return V9X_M64_REFUSE_COMPOSITE;
    }
    if (request->composite_min_filter != M64_FILTER_NEAREST &&
        request->composite_min_filter != M64_FILTER_LINEAR &&
        request->composite_min_filter != M64_FILTER_MIPNEAREST &&
        request->composite_min_filter != M64_FILTER_MIPLINEAR &&
        request->composite_min_filter != M64_FILTER_LINEARMIPLINEAR) {
        return V9X_M64_REFUSE_COMPOSITE;
    }
    if (request->composite_address != M64_ADDRESS_WRAP &&
        request->composite_address != M64_ADDRESS_CLAMP) {
        return V9X_M64_REFUSE_COMPOSITE;
    }
    if (request->composite_color_op != M64_COLOROP_MODULATE) {
        return V9X_M64_REFUSE_COMPOSITE;
    }
    if (request->composite_alpha_op != M64_ALPHAOP_FRAGMENT &&
        v9x_m64_policy_fragment_alpha_read(request)) {
        return V9X_M64_REFUSE_COMPOSITE;
    }
    if (request->texture_op == M64_TEXOP_DECALALPHA &&
        v9x_m64_policy_format_has_alpha(request->texture_format)) {
        return V9X_M64_REFUSE_COMPOSITE;
    }
    if (request->fog_enable != 0ul || request->specular_enable != 0ul) {
        return V9X_M64_REFUSE_COMPOSITE;
    }
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
    if (request->composite != 0ul) {
        if (request->textured == 0ul) {
            return V9X_M64_REFUSE_COMPOSITE;
        }
        reason = v9x_m64_policy_composite(request);
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
    if (v9x_m64_policy_alpha_test_droppable(request)) {
        decision->alpha_test_dropped = 1ul;
    } else if (request->alpha_test_enable != 0ul &&
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
        decision->alpha_test_dropped = 0ul;
    }
    if (request == 0 || decision == 0) {
        return V9X_M64_REFUSE_ARGUMENT;
    }

    reason = v9x_m64_policy_check(request, decision);
    if (reason != V9X_M64_REFUSE_NONE) {
        decision->light_fcn = 0ul;
        decision->texture_alpha = 0ul;
        decision->alpha_test_dropped = 0ul;
    }
    return reason;
}
