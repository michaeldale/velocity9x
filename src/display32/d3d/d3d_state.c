/*
 * Direct3D render state to the neutral draw description. See d3d_state.h.
 *
 * The numbers below are d3dtypes.h's, restated here rather than taken from
 * the Windows-facing ABI header so this file compiles on the host. The test
 * holds them to d3dtypes.h; the assertions hold r3d.h to them.
 */
#include "d3d_state.h"
#include "velocity9x/r3d_abi.h"

#define V9X_D3D_STATE_CMP_NEVER      1ul
#define V9X_D3D_STATE_CMP_ALWAYS     8ul
#define V9X_D3D_STATE_BLEND_ZERO     1ul
#define V9X_D3D_STATE_BLEND_SRCALPHASAT 11ul
#define V9X_D3D_STATE_FILTER_NEAREST 1ul
#define V9X_D3D_STATE_FILTER_LINEARMIPLINEAR 6ul
#define V9X_D3D_STATE_TADDRESS_WRAP  1ul
#define V9X_D3D_STATE_TADDRESS_BORDER 4ul
#define V9X_D3D_STATE_TBLEND_DECAL   1ul
#define V9X_D3D_STATE_TBLEND_ADD     8ul
#define V9X_D3D_STATE_SHADE_FLAT     1ul
#define V9X_D3D_STATE_SHADE_GOURAUD  2ul

/* The identity is a fact only while these hold. */
typedef char v9x_d3d_state_assert_cmp[
    (V9X_R3D_CMP_NEVER == V9X_D3D_STATE_CMP_NEVER &&
     V9X_R3D_CMP_ALWAYS == V9X_D3D_STATE_CMP_ALWAYS) ? 1 : -1];
typedef char v9x_d3d_state_assert_blend[
    (V9X_R3D_BLEND_ZERO == V9X_D3D_STATE_BLEND_ZERO &&
     V9X_R3D_BLEND_SRCALPHASAT == V9X_D3D_STATE_BLEND_SRCALPHASAT) ? 1 : -1];
typedef char v9x_d3d_state_assert_filter[
    (V9X_R3D_FILTER_NEAREST == V9X_D3D_STATE_FILTER_NEAREST &&
     V9X_R3D_FILTER_LINEARMIPLINEAR == V9X_D3D_STATE_FILTER_LINEARMIPLINEAR)
        ? 1 : -1];
typedef char v9x_d3d_state_assert_address[
    (V9X_R3D_ADDRESS_WRAP == V9X_D3D_STATE_TADDRESS_WRAP &&
     V9X_R3D_ADDRESS_BORDER == V9X_D3D_STATE_TADDRESS_BORDER) ? 1 : -1];
typedef char v9x_d3d_state_assert_texop[
    (V9X_R3D_TEXOP_DECAL == V9X_D3D_STATE_TBLEND_DECAL &&
     V9X_R3D_TEXOP_ADD == V9X_D3D_STATE_TBLEND_ADD) ? 1 : -1];
typedef char v9x_d3d_state_assert_shade[
    (V9X_R3D_SHADE_FLAT == V9X_D3D_STATE_SHADE_FLAT &&
     V9X_R3D_SHADE_GOURAUD == V9X_D3D_STATE_SHADE_GOURAUD) ? 1 : -1];

void v9x_d3d_state_fill(const V9X_D3D_STATE_RAW *raw, V9X_R3D_DRAW *draw)
{
    draw->depth_enable = raw->z_enable;
    draw->depth_write = raw->z_write;
    draw->depth_func = raw->z_func;
    draw->texture.min_filter = raw->texture_min;
    draw->texture.mag_filter = raw->texture_mag;
    draw->texture.op = raw->texture_blend;
    /*
     * A value d3dtypes.h does not define is taken as CLAMP. Half-Life's
     * Direct3D renderer sets TEXTUREADDRESSU/V to 0 for its sky on the
     * Gateway's Mach64 (AddressSeenU/V, 2026-10-01); every engine refused 0
     * and a refused batch is drawn by nobody, so the sky showed old frames.
     * CLAMP is what the same game's OpenGL renderer asks for its sky.
     */
    draw->texture.address =
        raw->texture_address >= V9X_R3D_ADDRESS_WRAP &&
        raw->texture_address <= V9X_R3D_ADDRESS_BORDER
            ? raw->texture_address : V9X_R3D_ADDRESS_CLAMP;
    draw->texture.border = raw->texture_border;
    draw->texture.wrap_u = raw->wrap_u;
    draw->texture.wrap_v = raw->wrap_v;
    draw->texture.wrap_either = raw->texture_wrap;
    draw->blend_enable = raw->alpha_blend_enable;
    draw->src_blend = raw->src_blend;
    draw->dst_blend = raw->dest_blend;
    draw->alpha_test_enable = raw->alpha_test_enable;
    draw->alpha_func = raw->alpha_func;
    draw->alpha_ref = raw->alpha_ref;
    draw->shade_mode = raw->shade_mode;
    draw->specular_enable = raw->specular_enable;
    draw->fog_enable = raw->fog_enable;
    draw->fog_color = raw->fog_color;
    draw->color_key_enable = raw->color_key_enable;
    draw->alpha_force = raw->alpha_force;
}

v9x_u32 v9x_d3d_state_depth_fill(v9x_u32 value, v9x_u32 depth_bits,
                                 v9x_u32 shift)
{
    if (depth_bits < 32ul) {
        value &= (1ul << depth_bits) - 1ul;
    }
    return value >> shift;
}

v9x_u32 v9x_d3d_state_depth_widen(v9x_u32 value, v9x_u32 from_bits,
                                  v9x_u32 to_bits)
{
    v9x_u32 gain;

    if (to_bits <= from_bits) {
        return value;
    }
    gain = to_bits - from_bits;
    return (value << gain) | (value >> (from_bits - gain));
}

v9x_u32 v9x_d3d_state_depth_rescale(v9x_u32 value, v9x_u32 from_bits,
                                    v9x_u32 to_bits)
{
    if (to_bits < from_bits) {
        return value >> (from_bits - to_bits);
    }
    return v9x_d3d_state_depth_widen(value, from_bits, to_bits);
}

v9x_u32 v9x_d3d_state_depth_active(v9x_u32 z_enable, v9x_u32 depth_bound,
                                   v9x_u32 depth_pitch)
{
    if (z_enable == 0ul || depth_bound == 0ul || depth_pitch == 0ul) {
        return 0ul;
    }
    return 1ul;
}

/* d3dtypes.h's D3DTEXTUREOP and D3DTA_* values the two-stage rule reads;
 * a modifier (COMPLEMENT, ALPHAREPLICATE) is any bit above the selector. */
#define V9X_D3D_STATE_TOP_DISABLE    1ul
#define V9X_D3D_STATE_TOP_SELECTARG1 2ul
#define V9X_D3D_STATE_TOP_SELECTARG2 3ul
#define V9X_D3D_STATE_TOP_MODULATE   4ul
#define V9X_D3D_STATE_TOP_ADD        7ul
#define V9X_D3D_STATE_TOP_BLENDTEXTUREALPHA 13ul
#define V9X_D3D_STATE_TA_SELECTMASK  0x0000000ful
#define V9X_D3D_STATE_TA_DIFFUSE     0ul
#define V9X_D3D_STATE_TA_CURRENT     1ul
#define V9X_D3D_STATE_TA_TEXTURE     2ul

/* A stage's colour that hands its input on unchanged; no unit op says
 * that, so it is only ever an answer about the stage. */
#define V9X_D3D_STATE_COLOR_PASS     0xfffffffeul
#define V9X_D3D_STATE_NONE           0xfffffffful

static int v9x_d3d_state_arg_texture(v9x_u32 arg)
{
    return arg == V9X_D3D_STATE_TA_TEXTURE;
}

/* The stage's input: CURRENT, which at stage 0 is the vertex colour, so
 * DIFFUSE is the input there too. A modified argument is neither. */
static int v9x_d3d_state_arg_input(v9x_u32 arg, int stage)
{
    return arg == V9X_D3D_STATE_TA_CURRENT ||
           (stage == 0 && arg == V9X_D3D_STATE_TA_DIFFUSE);
}

/* The texel with the input, either way round. */
static int v9x_d3d_state_args_texture_input(v9x_u32 arg1, v9x_u32 arg2,
                                            int stage)
{
    return (v9x_d3d_state_arg_texture(arg1) &&
            v9x_d3d_state_arg_input(arg2, stage)) ||
           (v9x_d3d_state_arg_input(arg1, stage) &&
            v9x_d3d_state_arg_texture(arg2));
}

/* A stage's colour as a unit's colour op, COLOR_PASS, or NONE. */
static v9x_u32 v9x_d3d_state_stage_color(const V9X_D3D_STATE_STAGE *stage,
                                         int index)
{
    v9x_u32 arg1 = stage->color_arg1;
    v9x_u32 arg2 = stage->color_arg2;
    v9x_u32 selected;

    switch (stage->color_op) {
    case V9X_D3D_STATE_TOP_SELECTARG1:
    case V9X_D3D_STATE_TOP_SELECTARG2:
        selected = stage->color_op == V9X_D3D_STATE_TOP_SELECTARG1 ? arg1
                                                                    : arg2;
        if (v9x_d3d_state_arg_texture(selected)) {
            return V9X_R3D_ABI_COLOROP_REPLACE;
        }
        return v9x_d3d_state_arg_input(selected, index)
            ? V9X_D3D_STATE_COLOR_PASS : V9X_D3D_STATE_NONE;
    case V9X_D3D_STATE_TOP_MODULATE:
        return v9x_d3d_state_args_texture_input(arg1, arg2, index)
            ? V9X_R3D_ABI_COLOROP_MODULATE : V9X_D3D_STATE_NONE;
    case V9X_D3D_STATE_TOP_ADD:
        return v9x_d3d_state_args_texture_input(arg1, arg2, index)
            ? V9X_R3D_COLOROP_ADD : V9X_D3D_STATE_NONE;
    case V9X_D3D_STATE_TOP_BLENDTEXTUREALPHA:
        /* Arg1 At + Arg2 (1 - At): the unit's DECALALPHA only with the
         * texel as Arg1. */
        return v9x_d3d_state_arg_texture(arg1) &&
               v9x_d3d_state_arg_input(arg2, index)
            ? V9X_R3D_ABI_COLOROP_DECALALPHA : V9X_D3D_STATE_NONE;
    default:
        return V9X_D3D_STATE_NONE;
    }
}

/* A stage's alpha as a unit's alpha op, or NONE. DISABLE under an enabled
 * colour is taken as the input passed on. */
static v9x_u32 v9x_d3d_state_stage_alpha(const V9X_D3D_STATE_STAGE *stage,
                                         int index)
{
    v9x_u32 selected;

    switch (stage->alpha_op) {
    case V9X_D3D_STATE_TOP_DISABLE:
        return V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    case V9X_D3D_STATE_TOP_SELECTARG1:
    case V9X_D3D_STATE_TOP_SELECTARG2:
        selected = stage->alpha_op == V9X_D3D_STATE_TOP_SELECTARG1
            ? stage->alpha_arg1 : stage->alpha_arg2;
        if (v9x_d3d_state_arg_texture(selected)) {
            return V9X_R3D_ABI_ALPHAOP_REPLACE;
        }
        return v9x_d3d_state_arg_input(selected, index)
            ? V9X_R3D_ABI_ALPHAOP_FRAGMENT : V9X_D3D_STATE_NONE;
    case V9X_D3D_STATE_TOP_MODULATE:
        return v9x_d3d_state_args_texture_input(stage->alpha_arg1,
                                                stage->alpha_arg2, index)
            ? V9X_R3D_ABI_ALPHAOP_MODULATE : V9X_D3D_STATE_NONE;
    default:
        return V9X_D3D_STATE_NONE;
    }
}

v9x_u32 v9x_d3d_state_stages(const V9X_D3D_STATE_STAGE *stage0,
                             const V9X_D3D_STATE_STAGE *stage1,
                             V9X_R3D_TEXTURE *unit0, V9X_R3D_TEXTURE *unit1)
{
    v9x_u32 color0;
    v9x_u32 alpha0;
    v9x_u32 color1;
    v9x_u32 alpha1;

    /* A disabled stage ends the cascade there. */
    if (stage0->color_op == V9X_D3D_STATE_TOP_DISABLE ||
        stage1->color_op == V9X_D3D_STATE_TOP_DISABLE) {
        return V9X_D3D_STAGES_ONE;
    }
    color1 = v9x_d3d_state_stage_color(stage1, 1);
    alpha1 = v9x_d3d_state_stage_alpha(stage1, 1);
    if (color1 == V9X_D3D_STATE_COLOR_PASS &&
        alpha1 == V9X_R3D_ABI_ALPHAOP_FRAGMENT) {
        return V9X_D3D_STAGES_ONE;
    }
    /* A passed colour under a changed alpha has no unit op; nor does a
     * stage 0 that samples nothing under a stage 1 that does - the
     * hardware runs unit 1 only behind unit 0. */
    color0 = v9x_d3d_state_stage_color(stage0, 0);
    alpha0 = v9x_d3d_state_stage_alpha(stage0, 0);
    if (color1 == V9X_D3D_STATE_NONE || color1 == V9X_D3D_STATE_COLOR_PASS ||
        alpha1 == V9X_D3D_STATE_NONE || color0 == V9X_D3D_STATE_NONE ||
        color0 == V9X_D3D_STATE_COLOR_PASS || alpha0 == V9X_D3D_STATE_NONE) {
        return V9X_D3D_STAGES_REFUSE;
    }
    unit0->color_op = color0;
    unit0->alpha_op = alpha0;
    unit1->color_op = color1;
    unit1->alpha_op = alpha1;
    return V9X_D3D_STAGES_TWO;
}
