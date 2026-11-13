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

/*
 * The interpolated draws (V9XR63D /gouraud, /depth, /modulate), built
 * from the same R6xx layouts, every opcode and field checked against both
 * xf86's r600_shader.h / r600_reg_auto_r6xx.h and Mesa r600's r600_sq.h /
 * r600_isa.c / r600_formats.h in the host test. Not yet run on a card.
 *
 * R6xx has no INTERP instructions: the SPI writes PS input n, already
 * interpolated, into GPR n before the shader starts (xf86's copy PS
 * samples R0 directly), so these PSs read R0 and R1 as they are.
 *
 * Longest of these shaders, in dwords, for a caller's buffers.
 */
#define V9X_R6_SHADER_DWORDS_MAX    24u

/*
 * Gouraud: a vertex of x, y, z, w then r, g, b, a, eight floats (32
 * bytes). The VS fetches the position into R1 and the colour into R0 in
 * one vertex clause, as xf86's copy VS fetches its two pairs (the first
 * fetch counting the whole stride as the mega-fetch), and exports R1 as
 * the position and R0 as parameter 0. The PS is the solid PS with R0 in
 * place of constant 0: four clamped MOVs, R0 to MRT 0.
 */
#define V9X_R6_GOURAUD_STRIDE       32ul
#define V9X_R6_GOURAUD_VS_DWORDS    16u
#define V9X_R6_GOURAUD_PS_DWORDS    12u
#define V9X_R6_GOURAUD_VS_GPRS      2ul
#define V9X_R6_GOURAUD_PS_GPRS      1ul
v9x_u32 v9x_r6_shader_gouraud_vs(v9x_u32 *out);
v9x_u32 v9x_r6_shader_gouraud_ps(v9x_u32 *out);

/*
 * A diagnostic Gouraud VS (V9XR63D /zlit:, /posw): the same fetches, then
 * an ALU clause that overwrites the position's z with VS constant 0's x
 * and, with write_w, its w with constant 0's y, before the exports. The
 * VS's constant 0 is SQ_ALU_CONSTANT 256 (SET_ALU_CONST at 31000h,
 * r600_reg_r6xx.h SQ_ALU_CONSTANT_vs), read as constant-file entry 256
 * under DX9_CONSTS as the solid PS reads its own. 24 dwords, 2 GPRs.
 */
#define V9X_R6_ZCONST_VS_DWORDS     24u
#define V9X_R6_VS_CONSTANT_0        0x31000ul
v9x_u32 v9x_r6_shader_gouraud_vs_zconst(v9x_u32 write_w, v9x_u32 *out);

/*
 * A diagnostic: the Gouraud PS that also exports Z, PS constant 0's x
 * clamped, through the pixel export at CF_PIXEL_Z (61), before the colour
 * (Mesa's order: only the last pixel export is EXPORT_DONE). Used with
 * SQ_PGM_EXPORTS_PS EXPORT_Z and DB_SHADER_CONTROL Z_EXPORT_ENABLE, it
 * gives the depth block a Z that bypasses the clipper and the
 * rasteriser's interpolation (V9XR63D /zexport:). Two GPRs.
 */
#define V9X_R6_ZEXPORT_PS_DWORDS    18u
#define V9X_R6_ZEXPORT_PS_GPRS      2ul
v9x_u32 v9x_r6_shader_gouraud_zexport_ps(v9x_u32 *out);

/*
 * Modulate, Direct3D's default stage: a vertex of x, y, z, w, r, g, b, a
 * and the texel coordinate u, v (ten floats, 40 bytes). The VS fetches
 * the position into R1, (u, v, 0, 1) into R2 and the colour into R0 -
 * R0, which holds the vertex index every fetch reads, last - each fetch
 * in Mesa's form (each its own mega-fetch, MEGA_FETCH_COUNT 1Fh: the
 * stride is past xf86's single mega-fetch), and exports
 * R1 as the position, R0 as parameter 0 and R2 as parameter 1, as xf86's
 * composite VS exports its two parameters (EXPORT, then EXPORT_DONE). The
 * PS samples texture 0 with sampler 0 at R1, unnormalised as xf86's copy
 * PS samples, into R1, then R2 = R1 * R0 in all four channels, clamped,
 * as xf86's composite PS multiplies, and exports R2.
 */
#define V9X_R6_MODULATE_STRIDE      40ul
#define V9X_R6_MODULATE_VS_DWORDS   20u
#define V9X_R6_MODULATE_PS_DWORDS   20u
#define V9X_R6_MODULATE_VS_GPRS     3ul
#define V9X_R6_MODULATE_PS_GPRS     3ul
v9x_u32 v9x_r6_shader_modulate_vs(v9x_u32 *out);
v9x_u32 v9x_r6_shader_modulate_ps(v9x_u32 *out);

/*
 * Direct3D, for d3d_r6.c: Evergreen's D3D shaders (eg_shader.h) in the
 * R6xx forms above. The VS fetches the render core's 32-byte TL vertex as
 * it is (r3d.h, V9X_R3D_VERTEX): sx, sy, sz, rhw into R1, exported as the
 * position (PA_CL_VTE_CNTL takes x, y, z as divided and w as rhw, the
 * VTE word Evergreen measured perspective-correct); textured, tu and tv
 * at 24 into R2 as (u, v, 0, 1), parameter 1; the D3DCOLOR at 16 into R0
 * as four normalised unsigned bytes in memory order - B, G, R, A - last,
 * parameter 0. The target and texture are then in the builder's
 * d3d_order. Every fetch a mega-fetch, the modulate VS's rule.
 *
 * The PS for one op: NONE the Gouraud PS (R0 to MRT 0); REPLACE samples
 * at R1, normalised, and exports the texel; MODULATE is the modulate PS
 * with normalised coordinates, colour times texel in all four channels.
 * Not yet run on a card.
 */
#define V9X_R6_D3D_STRIDE           32ul
#define V9X_R6_D3D_OP_NONE          0ul
#define V9X_R6_D3D_OP_REPLACE       1ul
#define V9X_R6_D3D_OP_MODULATE      2ul
#define V9X_R6_D3D_VS_GPRS(textured) ((textured) != 0ul ? 3ul : 2ul)
#define V9X_R6_D3D_PS_GPRS(op) ((op) == V9X_R6_D3D_OP_MODULATE ? 3ul : \
                                (op) == V9X_R6_D3D_OP_REPLACE ? 2ul : 1ul)
v9x_u32 v9x_r6_shader_d3d_vs(v9x_u32 textured, v9x_u32 *out);
v9x_u32 v9x_r6_shader_d3d_ps(v9x_u32 op, v9x_u32 *out);

/* The HAL's shader slots, each 256 bytes in the CP's window (r6_cp.c):
 * the two Direct3D VSs, then the PS for each op in V9X_R6_D3D_OP_* order. */
#define V9X_R6_SLOT_VS              0ul
#define V9X_R6_SLOT_VS_TEXTURED     1ul
#define V9X_R6_SLOT_PS_FIRST        2ul /* + V9X_R6_D3D_OP_* */
#define V9X_R6_SLOT_COUNT           5ul

#endif
