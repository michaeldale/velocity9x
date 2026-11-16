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
 *   SOPP  s_waitcnt is BF8C0000h | simm16, s_nop BF800000h | simm16
 *   SOP1  101111101 sdst[22:16] op[15:8] ssrc0[7:0]; s_mov_b32 is op 0
 *   VINTRP 110101 vdst[25:18] op[17:16] attr[15:10] chan[9:8] vsrc[7:0]
 *         (vintrp.s, VI)
 *   MIMG  111100 ... op[24:18] unorm[12] dmask[11:8], then vaddr[7:0]
 *         vdata[15:8] srsrc/4[20:16] ssamp/4[25:21] (mimg.s, GCN)
 *
 * A source of V9X_VI_LITERAL takes the next dword as its value. GCN 3 VALU
 * instructions get one constant-bus read, an SGPR, VCC or a literal; the
 * encoders do not check it, the shaders below keep to it.
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
#define V9X_VI_LITERAL          0xfful
#define V9X_VI_M0               124ul

#define V9X_VI_VOP1_MOV_B32     0x01ul
#define V9X_VI_VOP1_CVT_F32_UBYTE0 0x11ul
#define V9X_VI_VOP1_CVT_F32_UBYTE1 0x12ul
#define V9X_VI_VOP1_CVT_F32_UBYTE2 0x13ul
#define V9X_VI_VOP1_CVT_F32_UBYTE3 0x14ul
#define V9X_VI_VOP2_ADD_F32     0x01ul
#define V9X_VI_VOP2_SUBREV_F32  0x03ul
#define V9X_VI_VOP2_MUL_F32     0x05ul
#define V9X_VI_VOP2_MAC_F32     0x16ul
/* v_cmpx_*_f32: VOPC, the compare written to EXEC as well as VCC
 * (gfx8_asm_vopc.s). Direct3D's D3DCMPFUNC 1..8 in order. */
#define V9X_VI_VOPC_CMPX_F_F32   0x50ul
#define V9X_VI_VOPC_CMPX_LT_F32  0x51ul
#define V9X_VI_VOPC_CMPX_EQ_F32  0x52ul
#define V9X_VI_VOPC_CMPX_LE_F32  0x53ul
#define V9X_VI_VOPC_CMPX_GT_F32  0x54ul
#define V9X_VI_VOPC_CMPX_NEQ_F32 0x5dul
#define V9X_VI_VOPC_CMPX_GE_F32  0x56ul
#define V9X_VI_VOPC_CMPX_TRU_F32 0x5ful
#define V9X_VI_VOP2_ADDC_U32    0x1cul
#define V9X_VI_VINTRP_P1_F32    0ul
#define V9X_VI_VINTRP_P2_F32    1ul
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
/* A compressed export: two VGPRs of packed 16-bit halves (x and y in
 * the first, z and w in the second), all channels: two dwords. */
v9x_u32 v9x_vi_exp_compr(v9x_u32 target, int done, int vm, v9x_u32 v01,
                         v9x_u32 v23, v9x_u32 *out);
/* v_cvt_pkrtz_f16_f32 vdst, src0, src1 (VOP3 296h on GFX8; LLVM's
 * gfx8_vop3.txt): src0 to the low half, src1 the high, as FP16 rounded
 * toward zero. Two dwords. */
v9x_u32 v9x_vi_cvt_pkrtz(v9x_u32 vdst, v9x_u32 src0, v9x_u32 src1,
                         v9x_u32 *out);

/* flat_store_dword v[vaddr:vaddr+1], vdata: two dwords to out. */
v9x_u32 v9x_vi_flat_store_dword(v9x_u32 vaddr, v9x_u32 vdata, v9x_u32 *out);
/* flat_load_dwordx4 v[vdst:vdst+3], v[vaddr:vaddr+1]: two dwords to out. */
v9x_u32 v9x_vi_flat_load_dwordx4(v9x_u32 vaddr, v9x_u32 vdst, v9x_u32 *out);
v9x_u32 v9x_vi_s_waitcnt(v9x_u32 simm16);
v9x_u32 v9x_vi_s_nop(v9x_u32 simm16);
/* s_cbranch_execz, simm16 the signed dword offset from the next word. */
v9x_u32 v9x_vi_s_cbranch_execz(v9x_u32 simm16);
/* exp null off, off, off, off done vm: a pixel shader's last export when
 * every pixel was killed. Two dwords to out. */
v9x_u32 v9x_vi_exp_null_done(v9x_u32 *out);
/* s_mov_b32 m0, s<ssrc>. */
v9x_u32 v9x_vi_s_mov_m0(v9x_u32 ssrc);
/* SOP1's 64-bit moves for the whole-quad mode a sampling PS runs in:
 * s_mov_b64 sdst, ssrc (op 1) and s_wqm_b64 sdst, ssrc (op 7), EXEC as
 * V9X_VI_EXEC. Pinned against LLVM's GFX8 tests (gfx8_asm_sop1.s):
 * s_mov_b64 s[10:11], exec BE8A017Eh, s_wqm_b64 exec, s[2:3] BEFE0702h. */
#define V9X_VI_EXEC             126ul
v9x_u32 v9x_vi_s_mov_b64(v9x_u32 sdst, v9x_u32 ssrc);
v9x_u32 v9x_vi_s_wqm_b64(v9x_u32 sdst, v9x_u32 ssrc);
/* image_sample v[vdata..], v[vaddr..], s[srsrc:srsrc+7], s[ssamp:ssamp+3]
 * with dmask, unnormalised off: two dwords to out. srsrc and ssamp are
 * multiples of 4. */
v9x_u32 v9x_vi_image_sample(v9x_u32 vdata, v9x_u32 vaddr, v9x_u32 srsrc,
                            v9x_u32 ssamp, v9x_u32 dmask, v9x_u32 *out);
/* MIMG's DA bit (14), ORed into an image_sample's first word: the address
 * declares an array slice, as a cube's face is (LLVM sets it for every
 * cube sample, SIInstrInfo's dim table). The same bit on GCN 1, 2 and 3:
 * mimg.s "image_atomic_add v11, v7, s[8:15] dmask:0x1 da" is flags 41h
 * in byte 1 under SICI and GFX89 alike. */
#define V9X_VI_MIMG_DA          0x00004000ul
/* v_interp_p1/p2_f32 vdst, vsrc, attr<attr>.<chan>. */
v9x_u32 v9x_vi_vintrp(v9x_u32 op, v9x_u32 vdst, v9x_u32 vsrc, v9x_u32 attr,
                      v9x_u32 chan);

/*
 * For pixel shader 1.1 (docs\plans\ati-gcn-ps11.md step 1).
 *
 * VOP1/VOP2/VOPC opcodes beside the ones above (gfx8_asm_vop1.s,
 * gfx8_vop3.txt; Mesa's aco_opcodes.py GFX8 column). VOPC 40h-4Fh are the
 * v_cmp_*_f32 that write VCC (or an SGPR pair in VOP3), in the same order
 * as the v_cmpx_* run at 50h.
 */
#define V9X_VI_VOP1_RCP_F32     0x22ul
#define V9X_VI_VOP2_SUB_F32     0x02ul
#define V9X_VI_VOP2_MIN_F32     0x0aul
#define V9X_VI_VOP2_MAX_F32     0x0bul
#define V9X_VI_VOPC_CMP_LT_F32  0x41ul
#define V9X_VI_VOPC_CMP_GT_F32  0x44ul
#define V9X_VI_VOPC_CMP_GE_F32  0x46ul
#define V9X_VCC                 106ul

/*
 * VOP3 (GFX8): 110100 op[25:16] clamp[15] abs[10:8] vdst[7:0], then
 * neg[31:29] omod[28:27] src2[26:18] src1[17:9] src0[8:0]. A VOP2 opcode
 * is VOP3 100h + op, a VOP1 opcode 140h + op, a VOPC opcode itself (vdst
 * then the SGPR pair written). The VOP3-only ones used here are v_mad_f32
 * 1C1h and v_cube* 1C4h-1C7h (gfx8_vop3.txt).
 */
#define V9X_VI_VOP3_FROM_VOP2(op) (0x100ul + (v9x_u32)(op))
#define V9X_VI_VOP3_FROM_VOP1(op) (0x140ul + (v9x_u32)(op))
#define V9X_VI_VOP3_MAD_F32     0x1c1ul
#define V9X_VI_VOP3_CUBEID_F32  0x1c4ul
#define V9X_VI_VOP3_CUBESC_F32  0x1c5ul
#define V9X_VI_VOP3_CUBETC_F32  0x1c6ul
#define V9X_VI_VOP3_CUBEMA_F32  0x1c7ul
/* mods for v9x_vi_vop3: |src n| and -src n (n 0-2), the result clamped
 * to [0, 1], and the output modifier (OMOD 1 x2, 2 x4, 3 /2), applied
 * before the clamp. */
#define V9X_VI_ABS(n)           (1ul << (n))
#define V9X_VI_NEG(n)           (8ul << (n))
#define V9X_VI_CLAMP            0x40ul
#define V9X_VI_OMOD_X2          0x100ul
#define V9X_VI_OMOD_X4          0x200ul
#define V9X_VI_OMOD_D2          0x300ul
/* Three sources; an unused one 0. Two dwords to out. */
v9x_u32 v9x_vi_vop3(v9x_u32 op, v9x_u32 vdst, v9x_u32 src0, v9x_u32 src1,
                    v9x_u32 src2, v9x_u32 mods, v9x_u32 *out);

/*
 * s_load_dwordx<n> s[sdst..], s[sbase:sbase+1], offset (SMEM, GFX8:
 * 110000 op[25:18] imm[17] glc[16] sdata[12:6] sbase/2[5:0], then the
 * byte offset; gfx8_asm_smem.s). dwords is 2, 4 or 8 (op 1, 2, 3); the
 * offset a multiple of 4 below 1024, so it translates to GCN 1 and 2's
 * SMRD (cik_shader.h). Two dwords to out; 0 for an argument out of
 * range.
 */
v9x_u32 v9x_vi_s_load(v9x_u32 dwords, v9x_u32 sdst, v9x_u32 sbase,
                      v9x_u32 offset, v9x_u32 *out);

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

/*
 * The Direct3D TL-vertex VS: user SGPRs s0:s1 are the vertex buffer's
 * 64-bit address, v0 is VertexID. Each vertex is the render core's 32
 * bytes: sx, sy, sz, rhw, D3DCOLOR (A8R8G8B8), specular, tu, tv. Two FLAT
 * loads fetch it; the position export is sx, sy, sz, rhw as loaded (the
 * VTE takes screen-space XY, Z and 1/W); parameter 0 is the D3DCOLOR's
 * bytes in memory order (B, G, R, A) as floats, parameter 1 is (tu, tv, 0,
 * 1), parameter 2 the specular's four bytes in the same order, the fourth
 * the fog factor. Seventeen VGPRs. With textures sampled in memory order
 * too and the CB writing x to the lowest field, the colour path never
 * swizzles, as Cedar's does.
 */
#define V9X_VI_TL_VERTEX_BYTES  32ul
#define V9X_VI_TL_VS_DWORDS     46ul
#define V9X_VI_TL_VS_VGPRS      17ul
v9x_u32 v9x_vi_shader_tl_vs(v9x_u32 *out);

/* The Gouraud PS: parameter 0 interpolated (PERSP_CENTER, v0:v1) and
 * exported to MRT0. No user SGPRs, so PRIM_MASK, which v_interp reads
 * through M0, is s0. Six VGPRs.
 *
 * Every Direct3D PS exports compressed: its colour packed to FP16 pairs
 * by v_cvt_pkrtz_f16_f32 into two VGPRs free by then (the barycentrics
 * v0:v1 here, the texture coordinates v2:v3 in the textured ones), the
 * MRT's SPI_SHADER_COL_FORMAT FP16_ABGR (v9x_vi_tl_draw). Four 32-bit
 * floats drew right into an 8-bit target until blending was on, when the
 * CB took them as 16-bit halves - three pixels in four wrong
 * (docs\decisions\2026-10-28-carrizo-d3d-blend-and-shader-fixed-function.md).
 * radeonsi never exports 32 bits a channel to an 8-bit target either. */
#define V9X_VI_GOURAUD_PS_DWORDS 17ul
v9x_u32 v9x_vi_shader_gouraud_ps(v9x_u32 *out);

/*
 * The textured pixel shaders. User SGPRs s0-s7 are the image descriptor
 * (T#), s8-s11 the sampler (S#), so PRIM_MASK is s12. Parameter 1's (u, v)
 * is interpolated into v2:v3 and sampled into v4-v7, in whole-quad mode
 * with the pixels' EXEC kept in s14-s15 and put back after the sample.
 * Replace exports the texel (eight VGPRs); modulate multiplies it by
 * parameter 0, interpolated into v8-v11 (twelve VGPRs).
 */
#define V9X_VI_TEXTURE_USER_SGPRS 12ul
#define V9X_VI_TEXTURE_PS_DWORDS  19ul
#define V9X_VI_TEXTURE_PS_VGPRS   8ul
#define V9X_VI_MODULATE_PS_DWORDS 31ul
#define V9X_VI_MODULATE_PS_VGPRS  12ul
v9x_u32 v9x_vi_shader_texture_ps(v9x_u32 *out);
v9x_u32 v9x_vi_shader_modulate_ps(v9x_u32 *out);

/*
 * Every Direct3D pixel shader from one builder: op is V9X_VI_PS_COLOUR
 * (the Gouraud PS), _TEXTURE or _MODULATE, the three above word for word
 * when specular is zero. With specular, parameter 2 is interpolated after
 * any texturing and added to the colour's three channels before the
 * export, Direct3D's colour plus specular; the CB's UNORM conversion
 * clamps the sum. Its VGPRs are each shader's _SPECULAR_VGPRS.
 */
#define V9X_VI_PS_COLOUR    0ul
#define V9X_VI_PS_TEXTURE   1ul
#define V9X_VI_PS_MODULATE  2ul
/* DECALALPHA, built only (v9x_vi_shader_d3d_ps_built): parameter 0 into
 * v8-v11 as for modulate, then the colour diffuse + At (texel - diffuse)
 * with the diffuse alpha, left in v8-v11. */
#define V9X_VI_PS_DECALALPHA 3ul
#define V9X_VI_GOURAUD_PS_SPECULAR_VGPRS  12ul
#define V9X_VI_TEXTURE_PS_SPECULAR_VGPRS  12ul
#define V9X_VI_MODULATE_PS_SPECULAR_VGPRS 16ul
#define V9X_VI_SPECULAR_PS_DWORDS         9ul
v9x_u32 v9x_vi_shader_d3d_ps(v9x_u32 op, int specular, v9x_u32 *out);

/*
 * The same shaders with what Direct3D's fixed function adds after them,
 * built for one draw, the constants as literals:
 *
 *   key        the colour key, on the texel as sampled (texture and
 *              modulate): a pixel whose B, G, R are within the tolerance
 *              of the key's is killed - their squared differences summed
 *              against key_tolerance2;
 *   fog        the colour lerped to fog_colour by parameter 2's w, the
 *              fog factor the core leaves in the specular alpha (1 is the
 *              colour, 0 the fog);
 *   alpha_test the final alpha against alpha_ref, kept where
 *              alpha FUNC ref holds (alpha_func a V9X_VI_VOPC_CMPX_*).
 *
 * Every colour value is a float's bits in memory order (B, G, R). A kill
 * is v_cmpx writing EXEC; a wave left with no pixel branches to
 * exp null done vm, as LLVM's early exit does. DB_SHADER_CONTROL must
 * then have KILL_ENABLE (v9x_vi_tl_draw's ps_kill). Up to
 * V9X_VI_D3D_PS_DWORDS_MAX dwords and V9X_VI_D3D_PS_BUILT_VGPRS VGPRs:
 * v16-v19 are its temporaries.
 */
struct v9x_vi_d3d_ps {
    v9x_u32 op;                 /* V9X_VI_PS_* */
    v9x_u32 specular;
    v9x_u32 key;
    v9x_u32 key_colour[3];
    v9x_u32 key_tolerance2;
    v9x_u32 fog;
    v9x_u32 fog_colour[3];
    v9x_u32 alpha_test;
    v9x_u32 alpha_func;
    v9x_u32 alpha_ref;
    /*
     * Two texture units in one pass (docs\plans\ati-gcn-ps11.md step 4);
     * op and key are then not read. Unit N combines its texel Ct, At with
     * what came in, the diffuse for unit 0 and unit 0's result for unit 1,
     * by V9X_VI_COMBINE_* (the Tesla programs' numbers, nv50_d3d.h). The
     * descriptors come from a table, not user SGPRs: s0:s1 its address,
     * so PRIM_MASK is s2; unit 0's T# and S# then unit 1's, at the
     * V9X_VI_TWO_UNIT_* offsets, loaded with s_load. Unit 1's (u, v) is
     * parameter 3's x and y (si_shader.h's TL2 VS).
     */
    v9x_u32 two_units;
    v9x_u32 color_op[2];
    v9x_u32 alpha_op[2];
};
#define V9X_VI_COMBINE_COLOR_REPLACE  0ul  /* Ct                  */
#define V9X_VI_COMBINE_COLOR_MODULATE 1ul  /* Cin Ct              */
#define V9X_VI_COMBINE_COLOR_DECAL    2ul  /* Cin + At (Ct - Cin) */
#define V9X_VI_COMBINE_COLOR_ADD      3ul  /* min(Cin + Ct, 1)    */
#define V9X_VI_COMBINE_ALPHA_FRAGMENT 0ul  /* Ain                 */
#define V9X_VI_COMBINE_ALPHA_REPLACE  1ul  /* At                  */
#define V9X_VI_COMBINE_ALPHA_MODULATE 2ul  /* Ain At              */
/* The two-unit table in dwords: T# 0, S# 0, T# 1, S# 1. */
#define V9X_VI_TWO_UNIT_T0            0ul
#define V9X_VI_TWO_UNIT_S0            8ul
#define V9X_VI_TWO_UNIT_T1            12ul
#define V9X_VI_TWO_UNIT_S1            20ul
#define V9X_VI_TWO_UNIT_TABLE_DWORDS  24ul
#define V9X_VI_TWO_UNIT_USER_SGPRS    2ul
#define V9X_VI_TWO_UNIT_PS_SGPRS      30ul
#define V9X_VI_TWO_UNIT_PS_VGPRS      24ul
#define V9X_VI_D3D_PS_DWORDS_MAX  160ul
#define V9X_VI_D3D_PS_BUILT_VGPRS 20ul
v9x_u32 v9x_vi_shader_d3d_ps_built(const struct v9x_vi_d3d_ps *ps,
                                   v9x_u32 *out);
#define V9X_VI_PROBE_PS_DWORDS  (6ul + V9X_VI_SOLID_PS_DWORDS)
v9x_u32 v9x_vi_shader_probe_vs(v9x_u32 *out);
v9x_u32 v9x_vi_shader_probe_ps(v9x_u32 *out);

#endif
