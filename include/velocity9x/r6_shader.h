/*
 * ATI R6xx (Radeon HD 2600 XT, RV630) shader words: the solid-fill vertex
 * and pixel shaders the 3D bring-up draws with.
 *
 * Pure policy, no I/O. The words are xf86-video-ati 6.14.6's
 * R600_solid_vs and R600_solid_ps (r600_shader.c), encoded with that
 * driver's R6xx field layouts (r600_shader.h: CF_DWORD1, CF_ALU_DWORD1,
 * CF_ALLOC_IMP_EXP_DWORD0/1, ALU_DWORD0, R6xx_ALU_DWORD1_OP2, VTX_DWORD0-2)
 * and opcode numbers (r600_reg_auto_r6xx.h). R6xx's ALU_DWORD1 is not
 * R7xx's or Evergreen's: ALU_INST starts at bit 8, after FOG_MERGE, so
 * nothing from eg_shader.c applies. The host test rebuilds both shaders
 * with xf86's own macros and holds these to them bit for bit.
 *
 * Shaders are arrays of dwords, little-endian, uploaded to VRAM at a
 * 256-byte boundary. Control-flow addresses count 8-byte units.
 */
#ifndef VELOCITY9X_R6_SHADER_H
#define VELOCITY9X_R6_SHADER_H

#include "velocity9x/status.h"

/* The shaders' lengths in dwords, and the GPRs R600PrepareSolid gives
 * them (vs_conf.num_gprs 2, ps_conf.num_gprs 1). */
#define V9X_R6_SOLID_VS_DWORDS 12u
#define V9X_R6_SOLID_PS_DWORDS 12u
#define V9X_R6_SOLID_VS_GPRS   2ul
#define V9X_R6_SOLID_PS_GPRS   1ul

/*
 * The vertex shader: a vertex clause fetching x, y as two floats from
 * buffer 0 (the VS's first fetch resource) into R1 as (x, y, 0, 1), R1
 * exported as the position and R0 as parameter 0 ("always export a param
 * whether it's used or not"). The vertex stride is 8 bytes.
 */
v9x_u32 v9x_r6_shader_solid_vs(v9x_u32 *out);

/*
 * The pixel shader: one ALU clause of four MOVs, R0 = PS constant 0
 * clamped, exported to MRT 0. The colour is the constant: SET_ALU_CONST
 * at the PS's constant 0 (v9x_r6_3d_solid).
 */
v9x_u32 v9x_r6_shader_solid_ps(v9x_u32 *out);

#endif
