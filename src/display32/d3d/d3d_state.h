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
    v9x_u32 texture_alpha_diffuse;
} V9X_D3D_STATE_RAW;

/* Fill every state field of draw from raw; the surfaces are left alone. */
void v9x_d3d_state_fill(const V9X_D3D_STATE_RAW *raw, V9X_R3D_DRAW *draw);

/* One texture blend stage's combine as DrawPrimitives2 states it: the
 * D3DTSS_COLOROP, COLORARG1/2, ALPHAOP and ALPHAARG1/2 values, raw. */
typedef struct v9x_d3d_state_stage {
    v9x_u32 color_op;
    v9x_u32 color_arg1;
    v9x_u32 color_arg2;
    v9x_u32 alpha_op;
    v9x_u32 alpha_arg1;
    v9x_u32 alpha_arg2;
} V9X_D3D_STATE_STAGE;

#define V9X_D3D_STAGES_ONE    0ul  /* stage 1 changes nothing          */
#define V9X_D3D_STAGES_TWO    1ul  /* one pass on two units            */
#define V9X_D3D_STAGES_REFUSE 2ul  /* stage 1 needs what no unit does */

/*
 * Stages 0 and 1 as a two-unit draw's combines: on TWO, the color_op and
 * alpha_op of unit0 and unit1 (V9X_R3D_ABI_COLOROP_* or
 * V9X_R3D_COLOROP_ADD, V9X_R3D_ABI_ALPHAOP_*), in the render interface's
 * sense - unit 0 on the vertex colour, unit 1 on unit 0's result, each
 * texel the factor. Nothing else of either unit is written. Only the
 * combinations whose arithmetic is exactly that are TWO: Direct3D asks
 * ValidateTextureStageState first, and an application told no draws its
 * own passes.
 */
v9x_u32 v9x_d3d_state_stages(const V9X_D3D_STATE_STAGE *stage0,
                             const V9X_D3D_STATE_STAGE *stage1,
                             V9X_R3D_TEXTURE *unit0, V9X_R3D_TEXTURE *unit1);

/*
 * Stage 2 folded into stage 0 where the product allows it: stage 2 a
 * MODULATE of CURRENT and DIFFUSE with its alpha passed on, stage 1 a
 * MODULATE of its texel and CURRENT, and stage 0 selecting its texel draw
 * t0 * t1 * d, which is stage 0 as MODULATE of TEXTURE and DIFFUSE under
 * the same stage 1. Then stage0's colour is rewritten so and 1 returned;
 * otherwise nothing is written and 0 returned. Two-unit engines draw no
 * third stage of their own.
 */
v9x_u32 v9x_d3d_state_stage2_fold(V9X_D3D_STATE_STAGE *stage0,
                                  const V9X_D3D_STATE_STAGE *stage1,
                                  const V9X_D3D_STATE_STAGE *stage2);

/*
 * 1 when stage 0, drawn as one DX5 texture blend, wants the vertex's alpha
 * where that blend would take the texel's: its colour is MODULATE or a
 * SELECTARG of the texture, and its alpha a SELECTARG of DIFFUSE or
 * CURRENT (the vertex's at stage 0), uncomplemented. DX5's MODULATE and
 * DECAL take the texel's alpha whenever the texture has one. 0 otherwise.
 * The draw carries it as texture.alpha_diffuse (r3d.h).
 */
v9x_u32 v9x_d3d_state_stage0_alpha_diffuse(const V9X_D3D_STATE_STAGE *stage0);

/*
 * A draw's states packed into one word for the state census (V9XTRACE
 * Census*): texture op 3:0 and textured 4 (op 0 when untextured),
 * alpha_diffuse 5, blend enable 6, source 10:7 and destination 14:11, fog
 * 15, specular 16, flat 17, alpha test 18, colour key 19, depth test 20
 * and write 21, texture address 24:22, minification 27:25 and
 * magnification 29:28 filters, a second unit 31. Fields past their width
 * are cut to it.
 */
v9x_u32 v9x_d3d_state_census_key(const V9X_R3D_DRAW *draw);

/*
 * Whether the depth unit is engaged: ZENABLE set, a depth surface bound,
 * and that surface with a pitch. The same three-part test every engine
 * makes (DDK D3DRENDR.C:266): an application may legally set ZENABLE with
 * no Z buffer, and acting on the state alone would point the depth unit at
 * offset 0, the visible framebuffer.
 */
/*
 * A DDBLT_DEPTHFILL value as the engine's depth buffer holds it: the bits of
 * a depth_bits-wide word, moved down by shift. The fill value is on the full
 * scale of its format (FFFFh the far plane in 16 bits); an engine whose depth
 * occupies fewer bits - the SiS 6326 compares 15 of its 16 - names the
 * difference in its limits (depth_fill_shift).
 */
v9x_u32 v9x_d3d_state_depth_fill(v9x_u32 value, v9x_u32 depth_bits,
                                 v9x_u32 shift);

/*
 * A depth on the from_bits full scale onto the wider to_bits one, its top
 * bits replicated into the new low ones, so the far plane stays the far
 * plane: FFFFh in 16 bits is FFFFFFh in 24 and FFFFFFFFh in 32. to_bits is
 * at most 32 and at most twice from_bits.
 */
v9x_u32 v9x_d3d_state_depth_widen(v9x_u32 value, v9x_u32 from_bits,
                                  v9x_u32 to_bits);

/*
 * A depth on the from_bits full scale onto the to_bits one, either way:
 * narrowed by dropping its low bits, widened as v9x_d3d_state_depth_widen
 * does. 0 and the far plane stay exact. The render interface's clear
 * carries 24 bits (r3d_abi.h, ABI 6) and a buffer holds 16, 24 or 32.
 */
v9x_u32 v9x_d3d_state_depth_rescale(v9x_u32 value, v9x_u32 from_bits,
                                    v9x_u32 to_bits);

v9x_u32 v9x_d3d_state_depth_active(v9x_u32 z_enable, v9x_u32 depth_bound,
                                   v9x_u32 depth_pitch);

#endif
