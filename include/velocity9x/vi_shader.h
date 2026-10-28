/*
 * GCN 3 (GFX8, Carrizo) shader instruction words, and the bring-up shaders
 * built from them.
 *
 * Pure policy, no I/O. The encodings are the GFX8 ones LLVM's assembler
 * tests pin (llvm/test/MC/AMDGPU gfx8_asm_vop1.s, _vop2.s, _vopc.s, _exp.s,
 * _sopp.s; build\external\carrizo\llvm-mc\), and the host test checks each
 * encoder against those bytes:
 *
 *   VOP1  0111111 vdst[24:17] op[16:9] src0[8:0]
 *   VOP2  0 op[30:25] vdst[24:17] vsrc1[16:9] src0[8:0]
 *   VOPC  0111110 op[24:17] vsrc1[16:9] src0[8:0], result in VCC
 *   EXP   110001 ... vm[12] done[11] compr[10] target[9:4] en[3:0],
 *         then vsrc0..3, a byte each
 *   FLAT  110111 op[24:18] slc[17] glc[16], then addr[7:0] data[15:8]
 *         tfe[23] vdst[31:24] (gfx8_asm_flat.s)
 *   SOPP  s_waitcnt is BF8C0000h | simm16
 *
 * A source operand is an SGPR number, V9X_VI_VGPR(n), or an inline constant
 * (V9X_VI_INLINE_INT, V9X_VI_FLOAT_1).
 */
#ifndef VELOCITY9X_VI_SHADER_H
#define VELOCITY9X_VI_SHADER_H

#include "velocity9x/status.h"

#define V9X_VI_VGPR(n)          (256ul + (v9x_u32)(n))
/* Inline integers 0..64 are 128..192. */
#define V9X_VI_INLINE_INT(n)    (128ul + (v9x_u32)(n))
#define V9X_VI_FLOAT_1          0xf2ul

#define V9X_VI_VOP1_MOV_B32     0x01ul
#define V9X_VI_VOP2_CNDMASK_B32 0x00ul
#define V9X_VI_VOP2_LSHLREV_B32 0x12ul
#define V9X_VI_VOP2_AND_B32     0x13ul
#define V9X_VI_VOP2_ADD_U32     0x19ul
#define V9X_VI_VOPC_CMP_EQ_U32  0xcaul

#define V9X_VI_EXP_MRT0         0ul
#define V9X_VI_EXP_POS0         12ul
#define V9X_VI_EXP_PARAM0       32ul

#define V9X_VI_S_ENDPGM         0xbf810000ul
/* vmcnt, expcnt and lgkmcnt all 0: every outstanding access done. */
#define V9X_VI_WAITCNT_ALL      0ul

v9x_u32 v9x_vi_vop1(v9x_u32 op, v9x_u32 vdst, v9x_u32 src0);
v9x_u32 v9x_vi_vop2(v9x_u32 op, v9x_u32 vdst, v9x_u32 src0, v9x_u32 vsrc1);
v9x_u32 v9x_vi_vopc(v9x_u32 op, v9x_u32 src0, v9x_u32 vsrc1);

/* An export of four VGPRs (all enabled) to target: two dwords to out. */
v9x_u32 v9x_vi_exp(v9x_u32 target, int done, int vm, v9x_u32 v0, v9x_u32 v1,
                   v9x_u32 v2, v9x_u32 v3, v9x_u32 *out);

/* flat_store_dword v[vaddr:vaddr+1], vdata: two dwords to out. */
v9x_u32 v9x_vi_flat_store_dword(v9x_u32 vaddr, v9x_u32 vdata, v9x_u32 *out);
v9x_u32 v9x_vi_s_waitcnt(v9x_u32 simm16);

/*
 * The solid-rectangle vertex shader, for a RECTLIST: user SGPRs s0-s3 are
 * x1, y1, x2, y2 as float bits in screen pixels; v0 is VertexID. Vertex 0
 * is (x1, y1), 1 is (x2, y1), 2 is (x1, y2), the three corners a RECTLIST
 * takes. Exports position (x, y, 0, 1) and one parameter (0). Six VGPRs.
 */
#define V9X_VI_SOLID_VS_DWORDS  15ul
v9x_u32 v9x_vi_shader_solid_vs(v9x_u32 *out);

/* The solid pixel shader: user SGPRs s0-s3 are R, G, B, A as float bits,
 * exported to MRT0 as 32-bit floats. Four VGPRs written over the two the
 * interpolators load. */
#define V9X_VI_SOLID_PS_DWORDS  7ul
v9x_u32 v9x_vi_shader_solid_ps(v9x_u32 *out);

/*
 * Probe variants, to show which stage of a draw runs: each first stores a
 * marker through FLAT to the 64-bit address in user SGPRs s4 (low) and s5
 * (high), waits for it, then runs the solid shader. The VS stores its
 * VertexID twice: at the address, and at the address + 16 + 4 x (VertexID
 * & 3), a dword per vertex; then, before its exports, the position it
 * computed, x at +32 + 4 x (VertexID & 3) and y at +48, then v2 (x2) at
 * +64 and VCC from the first compare (1.0 where set, else 0) at +80, each
 * plus 4 x (VertexID & 3). The mask keeps a wrong VertexID's stores inside
 * the 96 bytes after the address; the adds cannot carry while the low
 * dword is below FFFFFFA0h (v6:v7 the address). The PS stores its red (s0; v4:v5 the
 * address, v6 the data). Both stay within the solid shaders' eight VGPRs.
 */
#define V9X_VI_PROBE_VS_DWORDS  (27ul + V9X_VI_SOLID_VS_DWORDS)
#define V9X_VI_PROBE_PS_DWORDS  (6ul + V9X_VI_SOLID_PS_DWORDS)
v9x_u32 v9x_vi_shader_probe_vs(v9x_u32 *out);
v9x_u32 v9x_vi_shader_probe_ps(v9x_u32 *out);

#endif
