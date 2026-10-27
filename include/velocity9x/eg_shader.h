/*
 * AMD Evergreen shader words: control flow, ALU, vertex fetch, texture
 * fetch and export, and the few shaders the 3D bring-up uses.
 *
 * Pure policy, no I/O. Field layouts are Mesa's r600 eg_sq.h (Evergreen),
 * the clause and export forms its eg_asm.c eg_bytecode_cf_build; opcode
 * numbers are those Linux 3.9's hand-assembled blit shaders carry
 * (build\external\evergreen\). The encoder is held to those shaders bit
 * for bit by the host test, which rebuilds them.
 *
 * Shaders are arrays of dwords, little-endian, uploaded to VRAM at a
 * 256-byte boundary. Control-flow addresses count 8-byte units.
 */
#ifndef VELOCITY9X_EG_SHADER_H
#define VELOCITY9X_EG_SHADER_H

#include "velocity9x/status.h"

/* Control-flow instructions (CF_WORD1 / CF_ALLOC_EXPORT_WORD1 CF_INST,
 * and CF_ALU_WORD1 CF_INST for ALU). */
#define V9X_EG_CF_TC            0x01ul  /* texture fetch clause */
#define V9X_EG_CF_VC            0x02ul  /* vertex fetch clause */
#define V9X_EG_CF_EXPORT        0x53ul  /* not the last of its type */
#define V9X_EG_CF_EXPORT_DONE   0x54ul
#define V9X_EG_CF_ALU           0x08ul

/* Export types and array bases. */
#define V9X_EG_EXPORT_PIXEL     0ul
#define V9X_EG_EXPORT_POS       1ul
#define V9X_EG_EXPORT_PARAM     2ul
#define V9X_EG_EXPORT_POS0      60ul

/* ALU instructions (OP2) and sources. */
#define V9X_EG_ALU_MUL          0x01ul
#define V9X_EG_ALU_INTERP_XY    0xd6ul
#define V9X_EG_ALU_INTERP_ZW    0xd7ul
#define V9X_EG_ALU_SRC_PARAM    0x1c0ul

/* Fetches. */
#define V9X_EG_VTX_FETCH        0x00ul
#define V9X_EG_TEX_SAMPLE       0x10ul
#define V9X_EG_FMT_8_8_8_8      0x1aul
#define V9X_EG_FMT_32_32_FLOAT  0x1eul
#define V9X_EG_FMT_32_32_32_32_FLOAT 0x23ul

/* Swizzle selects. */
#define V9X_EG_SEL_X            0ul
#define V9X_EG_SEL_Y            1ul
#define V9X_EG_SEL_Z            2ul
#define V9X_EG_SEL_W            3ul
#define V9X_EG_SEL_0            4ul
#define V9X_EG_SEL_1            5ul
#define V9X_EG_SWIZZLE_XYZW     0x688ul  /* X | Y << 3 | Z << 6 | W << 9 */

/* Encoders: each writes its words to out and returns how many. */
v9x_u32 v9x_eg_cf_clause(v9x_u32 cf_inst, v9x_u32 addr, v9x_u32 count,
                         v9x_u32 *out);
v9x_u32 v9x_eg_cf_alu(v9x_u32 addr, v9x_u32 count, v9x_u32 *out);
/* done marks the last export of its type (EXPORT_DONE, else EXPORT). */
v9x_u32 v9x_eg_cf_export(v9x_u32 type, v9x_u32 array_base, v9x_u32 gpr,
                         v9x_u32 done, v9x_u32 end_of_program,
                         v9x_u32 barrier, v9x_u32 *out);
/* A vertex fetch from buffer 0 indexed by R0.x: dst_gpr takes
 * fetch_bytes at offset as data_format, its four channels selected. */
v9x_u32 v9x_eg_vtx_fetch(v9x_u32 dst_gpr, v9x_u32 offset,
                         v9x_u32 fetch_bytes, v9x_u32 data_format,
                         v9x_u32 sel_x, v9x_u32 sel_y, v9x_u32 sel_z,
                         v9x_u32 sel_w, v9x_u32 *out);
/* One INTERP_XY or INTERP_ZW slot: barycentric R0.(1 - slot % 2) against
 * a parameter, into dst_gpr.(slot), written or masked. */
v9x_u32 v9x_eg_alu_interp(v9x_u32 alu_inst, v9x_u32 slot, v9x_u32 param,
                          v9x_u32 dst_gpr, v9x_u32 write, v9x_u32 last,
                          v9x_u32 *out);
/* A two-source vector op, R(dst).(dst_chan) = R(src0).(chan0) op
 * R(src1).(chan1), written, bank swizzle VEC_012. */
v9x_u32 v9x_eg_alu_op2(v9x_u32 alu_inst, v9x_u32 src0_gpr,
                       v9x_u32 src0_chan, v9x_u32 src1_gpr,
                       v9x_u32 src1_chan, v9x_u32 dst_gpr, v9x_u32 dst_chan,
                       v9x_u32 last, v9x_u32 *out);
/* The general two-source form: each source a GPR number or
 * V9X_EG_ALU_SRC_LITERAL (its chan then picks literal x, y, z or w), with
 * flags V9X_EG_ALU_*. */
#define V9X_EG_ALU_ADD          0x00ul
#define V9X_EG_ALU_MAX          0x03ul
#define V9X_EG_ALU_KILLGT       0x2dul  /* kill where src0 > src1 */
#define V9X_EG_ALU_SRC_LITERAL  0xfdul
#define V9X_EG_ALU_NEG_SRC1     0x1ul
#define V9X_EG_ALU_CLAMP        0x2ul
#define V9X_EG_ALU_ABS_SRC0     0x4ul
#define V9X_EG_ALU_ABS_SRC1     0x8ul
#define V9X_EG_ALU_NO_WRITE     0x10ul
v9x_u32 v9x_eg_alu_op2_ex(v9x_u32 alu_inst, v9x_u32 src0_sel,
                          v9x_u32 src0_chan, v9x_u32 src1_sel,
                          v9x_u32 src1_chan, v9x_u32 flags, v9x_u32 dst_gpr,
                          v9x_u32 dst_chan, v9x_u32 last, v9x_u32 *out);
/* SAMPLE from resource and sampler 0 at unnormalised R(src).xy into
 * R(dst).xyzw. */
v9x_u32 v9x_eg_tex_sample(v9x_u32 src_gpr, v9x_u32 dst_gpr, v9x_u32 *out);
/* The same at a normalised R(src).xy, 0 to 1 across the texture. */
v9x_u32 v9x_eg_tex_sample_normalised(v9x_u32 src_gpr, v9x_u32 dst_gpr,
                                     v9x_u32 *out);

/* The shaders. Each writes up to V9X_EG_SHADER_DWORDS_MAX dwords and
 * returns the count. */
#define V9X_EG_SHADER_DWORDS_MAX 128u

/* Linux 3.9's blit: VS fetches position (R1) and texture coordinate (R0)
 * as two floats each, 16-byte stride; PS interpolates and samples. */
v9x_u32 v9x_eg_shader_blit_vs(v9x_u32 *out);
v9x_u32 v9x_eg_shader_blit_ps(v9x_u32 *out);

/* Gouraud: VS fetches position (R1) and colour (R0) as four floats each,
 * 32-byte stride, and exports them; PS interpolates all four colour
 * channels and exports them. */
v9x_u32 v9x_eg_shader_gouraud_vs(v9x_u32 *out);
v9x_u32 v9x_eg_shader_gouraud_ps(v9x_u32 *out);

/* Modulate, Direct3D's default texture stage: VS fetches position (R1)
 * and colour (R0) as four floats and a texel coordinate (R2) as two,
 * 40-byte stride, and exports them as POS0, PARAM0 and PARAM1; PS
 * interpolates the coordinate into R1 and the colour into R0, samples R1
 * and exports colour times texel. The VS needs 3 GPRs, the PS 2; the SPI
 * needs two interpolants, semantics 0 and 1. */
#define V9X_EG_MODULATE_STRIDE  40ul
v9x_u32 v9x_eg_shader_modulate_vs(v9x_u32 *out);
v9x_u32 v9x_eg_shader_modulate_ps(v9x_u32 *out);

/* Direct3D: the VS fetches the render core's 32-byte TL vertex (position
 * R1, colour R0 as normalised bytes in memory order, and textured, tu/tv
 * R2), 3 GPRs textured and 2 not; the PS for one texture op takes 2. */
#define V9X_EG_D3D_STRIDE       32ul
#define V9X_EG_D3D_OP_NONE      0ul
#define V9X_EG_D3D_OP_REPLACE   1ul
#define V9X_EG_D3D_OP_MODULATE  2ul
v9x_u32 v9x_eg_shader_d3d_vs(v9x_u32 textured, v9x_u32 *out);
v9x_u32 v9x_eg_shader_d3d_ps(v9x_u32 op, v9x_u32 *out);

/*
 * The Direct3D shaders with specular and fog. The VS also fetches the
 * specular dword (offset 20, normalised bytes in memory order: B, G, R,
 * and the fog factor in A) into R3 and exports it as the last parameter:
 * PARAM0 colour, PARAM1 the texel coordinate when textured, then
 * specular. The PS takes the op as v9x_eg_shader_d3d_ps does, then adds
 * the specular RGB (clamped) when `specular`, then with `fog` blends to
 * fog_color (a D3DCOLOR's 0x00RRGGBB) by the factor: fog + f * (colour -
 * fog), alpha untouched. The fog colour is carried as ALU literals, so a
 * PS is built for each fog colour; at up to 92 dwords it is larger than a
 * shader slot. The VS uses 4 GPRs, the PS 3, and the SPI
 * V9X_EG_D3D_INTERPOLANTS of them.
 */
#define V9X_EG_D3D_SPECULAR_OFFSET 20ul
#define V9X_EG_D3D_INTERPOLANTS(textured) ((textured) != 0ul ? 3ul : 2ul)
v9x_u32 v9x_eg_shader_d3d_vs_lit(v9x_u32 textured, v9x_u32 *out);
v9x_u32 v9x_eg_shader_d3d_ps_lit(v9x_u32 op, v9x_u32 specular, v9x_u32 fog,
                                 v9x_u32 fog_color, v9x_u32 *out);

/*
 * The lit PS with a source colour key: before the op, a texel whose B, G
 * and R each lie within key_tolerance of key (floats' bits, in memory
 * order) is killed. The texel is compared as sampled, so a filtered
 * texel that blends the key with another colour is not. A keyed PS uses
 * one more GPR, V9X_EG_D3D_PS_GPRS of them, and its draw needs
 * DB_SHADER_CONTROL's KILL_ENABLE (eg_3d.h, state kill).
 */
struct v9x_eg_d3d_ps {
    v9x_u32 op;              /* V9X_EG_D3D_OP_* */
    v9x_u32 specular;
    v9x_u32 fog;
    v9x_u32 fog_color;       /* D3DCOLOR, 0x00RRGGBB */
    v9x_u32 color_key;
    v9x_u32 key[3];          /* B, G, R */
    v9x_u32 key_tolerance;
};
#define V9X_EG_D3D_PS_GPRS(color_key) ((color_key) != 0ul ? 4ul : 3ul)
v9x_u32 v9x_eg_shader_d3d_ps_built(const struct v9x_eg_d3d_ps *ps,
                                   v9x_u32 *out);

/* value / max as an IEEE single's bits, rounded to nearest, without
 * floating point: a unorm channel as the sampler reads it. max is 1 to
 * 65535, value at most max. */
v9x_u32 v9x_eg_unorm_float_bits(v9x_u32 value, v9x_u32 max);

/* The HAL's shader slots in its VRAM window (eg_3d.h,
 * V9X_EG_WINDOW_SHADERS, one V9X_EG_WINDOW_SHADER_BYTES each): the two
 * Direct3D VSs, the PS for each op in V9X_EG_D3D_OP_* order, then the two
 * lit VSs. A lit PS goes with its batch, not in a slot. */
#define V9X_EG_SLOT_VS           0ul
#define V9X_EG_SLOT_VS_TEXTURED  1ul
#define V9X_EG_SLOT_PS_FIRST     2ul /* + V9X_EG_D3D_OP_* */
#define V9X_EG_SLOT_VS_LIT       5ul
#define V9X_EG_SLOT_VS_LIT_TEXTURED 6ul
#define V9X_EG_SLOT_COUNT        7ul

#endif
