/*
 * Direct3D render state to the neutral draw description. See d3d_state.h.
 *
 * The numbers below are d3dtypes.h's, restated here rather than taken from
 * the Windows-facing ABI header so this file compiles on the host. The test
 * holds them to d3dtypes.h; the assertions hold r3d.h to them.
 */
#include "d3d_state.h"

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
    draw->texture.address = raw->texture_address;
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

v9x_u32 v9x_d3d_state_depth_active(v9x_u32 z_enable, v9x_u32 depth_bound,
                                   v9x_u32 depth_pitch)
{
    if (z_enable == 0ul || depth_bound == 0ul || depth_pitch == 0ul) {
        return 0ul;
    }
    return 1ul;
}
