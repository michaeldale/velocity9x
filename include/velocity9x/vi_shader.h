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
#define V9X_VI_VOPC_CMP_EQ_U32  0xcaul

#define V9X_VI_EXP_MRT0         0ul
#define V9X_VI_EXP_POS0         12ul
#define V9X_VI_EXP_PARAM0       32ul

#define V9X_VI_S_ENDPGM         0xbf810000ul

v9x_u32 v9x_vi_vop1(v9x_u32 op, v9x_u32 vdst, v9x_u32 src0);
v9x_u32 v9x_vi_vop2(v9x_u32 op, v9x_u32 vdst, v9x_u32 src0, v9x_u32 vsrc1);
v9x_u32 v9x_vi_vopc(v9x_u32 op, v9x_u32 src0, v9x_u32 vsrc1);

/* An export of four VGPRs (all enabled) to target: two dwords to out. */
v9x_u32 v9x_vi_exp(v9x_u32 target, int done, int vm, v9x_u32 v0, v9x_u32 v1,
                   v9x_u32 v2, v9x_u32 v3, v9x_u32 *out);

/*
 * The solid-rectangle vertex shader, for a RECTLIST: user SGPRs s0-s3 are
 * x1, y1, x2, y2 as float bits in screen pixels; v0 is VertexID. Vertex 0
 * is (x1, y1), 1 is (x2, y1), 2 is (x1, y2), the three corners a RECTLIST
 * takes. Exports position (x, y, 0, 1) and one parameter (0). Six VGPRs.
 */
#define V9X_VI_SOLID_VS_DWORDS  13ul
v9x_u32 v9x_vi_shader_solid_vs(v9x_u32 *out);

/* The solid pixel shader: user SGPRs s0-s3 are R, G, B, A as float bits,
 * exported to MRT0 as 32-bit floats. Four VGPRs written over the two the
 * interpolators load. */
#define V9X_VI_SOLID_PS_DWORDS  7ul
v9x_u32 v9x_vi_shader_solid_ps(v9x_u32 *out);

#endif
