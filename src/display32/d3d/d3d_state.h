/*
 * Direct3D render state to the neutral core's draw description.
 *
 * Pure: no DDHAL header, so scripts\build-host.ps1 compiles it and
 * tests\host\test_d3d_state.c holds it to its rules. The core copies its
 * context's raw render-state DWORDs into V9X_D3D_STATE_RAW and this fills
 * the state half of a V9X_R3D_DRAW; the surfaces (target, depth, texture
 * object) are the core's to resolve, because they are DirectDraw surfaces.
 *
 * Today the translation is the identity, and this file exists to say so
 * where the compiler can check it: every V9X_R3D_* value a field takes is
 * asserted equal to the D3D number the raw field carries. When a concept
 * stops mapping one to one - the plan's colour/alpha combine ops - the rule
 * lives here with a test, not in an engine.
 */
#ifndef VELOCITY9X_D3D_STATE_H
#define VELOCITY9X_D3D_STATE_H

#include "velocity9x/types.h"
/* Relative, so the host build, which has no src\display32 include path,
 * finds it the same way the HAL does. */
#include "../r3d/r3d.h"

/* The context's render state as the runtime set it, raw D3D values. */
typedef struct v9x_d3d_state_raw {
    v9x_u32 z_enable;
    v9x_u32 z_write;
    v9x_u32 z_func;
    v9x_u32 alpha_blend_enable;
    v9x_u32 src_blend;
    v9x_u32 dest_blend;
    v9x_u32 texture_min;
    v9x_u32 texture_mag;
    v9x_u32 texture_blend;
    v9x_u32 texture_address;
    v9x_u32 texture_border;
    v9x_u32 texture_wrap;
    v9x_u32 wrap_u;
    v9x_u32 wrap_v;
    v9x_u32 shade_mode;
    v9x_u32 specular_enable;
    v9x_u32 fog_enable;
    v9x_u32 fog_color;
    v9x_u32 alpha_test_enable;
    v9x_u32 alpha_func;
    v9x_u32 alpha_ref;
    v9x_u32 alpha_force;
    v9x_u32 color_key_enable;
} V9X_D3D_STATE_RAW;

/* Fill every state field of draw from raw; the surfaces are left alone. */
void v9x_d3d_state_fill(const V9X_D3D_STATE_RAW *raw, V9X_R3D_DRAW *draw);

/*
 * Whether the depth unit is engaged: ZENABLE set, a depth surface bound,
 * and that surface with a pitch. The same three-part test every engine
 * makes (DDK D3DRENDR.C:266): an application may legally set ZENABLE with
 * no Z buffer, and acting on the state alone would point the depth unit at
 * offset 0, the visible framebuffer.
 */
v9x_u32 v9x_d3d_state_depth_active(v9x_u32 z_enable, v9x_u32 depth_bound,
                                   v9x_u32 depth_pitch);

#endif
