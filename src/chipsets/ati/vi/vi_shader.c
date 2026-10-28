/*
 * GCN 3 instruction words and the bring-up shaders; see
 * include\velocity9x\vi_shader.h.
 */
#include "velocity9x/vi_shader.h"

#define V9X_VI_VOP1_PREFIX 0x7e000000ul
#define V9X_VI_VOPC_PREFIX 0x7c000000ul
#define V9X_VI_EXP_PREFIX  0xc4000000ul
#define V9X_VI_EXP_ALL     0x0000000ful
#define V9X_VI_EXP_DONE    0x00000800ul
#define V9X_VI_EXP_VM      0x00001000ul
/* FLAT's prefix and FLAT_STORE_DWORD (1Ch) in op 24:18. */
#define V9X_VI_FLAT_STORE_DWORD 0xdc700000ul
/* FLAT_LOAD_DWORDX4 (17h). */
#define V9X_VI_FLAT_LOAD_DWORDX4 0xdc5c0000ul
#define V9X_VI_SOPP_WAITCNT     0xbf8c0000ul
#define V9X_VI_SOPP_NOP         0xbf800000ul
#define V9X_VI_SOP1_MOV_B32     0xbe800000ul
#define V9X_VI_VINTRP_PREFIX    0xd4000000ul
/* MIMG's prefix and IMAGE_SAMPLE (20h) in op 24:18. */
#define V9X_VI_IMAGE_SAMPLE     0xf0800000ul
#define V9X_VI_DMASK_RGBA       0xful
/* The textured PS's SGPRs (vi_shader.h). */
#define V9X_VI_TEXTURE_T_SGPR   0ul
#define V9X_VI_TEXTURE_S_SGPR   8ul
/* 1/255 as an IEEE single, the D3DCOLOR byte scale. */
#define V9X_VI_FLOAT_1_OVER_255 0x3b808081ul
/* TL vertex: sx, sy, sz, rhw in the first 16 bytes, D3DCOLOR at 16. */
#define V9X_VI_TL_SHIFT         5ul
#define V9X_VI_TL_COLOUR_OFFSET 16ul
/* The probe address's user SGPRs, after the four each solid shader has. */
#define V9X_VI_PROBE_SGPR_LO    4ul
#define V9X_VI_PROBE_SGPR_HI    5ul

v9x_u32 v9x_vi_vop1(v9x_u32 op, v9x_u32 vdst, v9x_u32 src0)
{
    return V9X_VI_VOP1_PREFIX | ((vdst & 0xfful) << 17) |
           ((op & 0xfful) << 9) | (src0 & 0x1fful);
}

v9x_u32 v9x_vi_vop2(v9x_u32 op, v9x_u32 vdst, v9x_u32 src0, v9x_u32 vsrc1)
{
    return ((op & 0x3ful) << 25) | ((vdst & 0xfful) << 17) |
           ((vsrc1 & 0xfful) << 9) | (src0 & 0x1fful);
}

v9x_u32 v9x_vi_vopc(v9x_u32 op, v9x_u32 src0, v9x_u32 vsrc1)
{
    return V9X_VI_VOPC_PREFIX | ((op & 0xfful) << 17) |
           ((vsrc1 & 0xfful) << 9) | (src0 & 0x1fful);
}

v9x_u32 v9x_vi_exp(v9x_u32 target, int done, int vm, v9x_u32 v0, v9x_u32 v1,
                   v9x_u32 v2, v9x_u32 v3, v9x_u32 *out)
{
    out[0] = V9X_VI_EXP_PREFIX | ((target & 0x3ful) << 4) | V9X_VI_EXP_ALL |
             (done ? V9X_VI_EXP_DONE : 0ul) | (vm ? V9X_VI_EXP_VM : 0ul);
    out[1] = (v0 & 0xfful) | ((v1 & 0xfful) << 8) | ((v2 & 0xfful) << 16) |
             ((v3 & 0xfful) << 24);
    return 2ul;
}

v9x_u32 v9x_vi_flat_store_dword(v9x_u32 vaddr, v9x_u32 vdata, v9x_u32 *out)
{
    out[0] = V9X_VI_FLAT_STORE_DWORD;
    out[1] = (vaddr & 0xfful) | ((vdata & 0xfful) << 8);
    return 2ul;
}

v9x_u32 v9x_vi_flat_load_dwordx4(v9x_u32 vaddr, v9x_u32 vdst, v9x_u32 *out)
{
    out[0] = V9X_VI_FLAT_LOAD_DWORDX4;
    out[1] = (vaddr & 0xfful) | ((vdst & 0xfful) << 24);
    return 2ul;
}

v9x_u32 v9x_vi_s_waitcnt(v9x_u32 simm16)
{
    return V9X_VI_SOPP_WAITCNT | (simm16 & 0xfffful);
}

v9x_u32 v9x_vi_s_nop(v9x_u32 simm16)
{
    return V9X_VI_SOPP_NOP | (simm16 & 0xfffful);
}

v9x_u32 v9x_vi_s_mov_m0(v9x_u32 ssrc)
{
    return V9X_VI_SOP1_MOV_B32 | (V9X_VI_M0 << 16) | (ssrc & 0xfful);
}

v9x_u32 v9x_vi_image_sample(v9x_u32 vdata, v9x_u32 vaddr, v9x_u32 srsrc,
                            v9x_u32 ssamp, v9x_u32 dmask, v9x_u32 *out)
{
    out[0] = V9X_VI_IMAGE_SAMPLE | ((dmask & 0xful) << 8);
    out[1] = (vaddr & 0xfful) | ((vdata & 0xfful) << 8) |
             (((srsrc / 4ul) & 0x1ful) << 16) |
             (((ssamp / 4ul) & 0x1ful) << 21);
    return 2ul;
}

v9x_u32 v9x_vi_vintrp(v9x_u32 op, v9x_u32 vdst, v9x_u32 vsrc, v9x_u32 attr,
                      v9x_u32 chan)
{
    return V9X_VI_VINTRP_PREFIX | ((vdst & 0xfful) << 18) |
           ((op & 3ul) << 16) | ((attr & 0x3ful) << 10) |
           ((chan & 3ul) << 8) | (vsrc & 0xfful);
}

/* Vertex 1 takes x2 and vertex 2 takes y2: v_cndmask picks its second
 * operand where VCC is set, its first elsewhere. Both operands are VGPRs:
 * VCC is v_cndmask's one constant-bus read, and an SGPR operand as well
 * made every lane take the first operand on Carrizo (V9XCZ3D's probe VS,
 * M-XP7053: all three corners came out (x1, y1), VCC correct). Leaves x in
 * v1, y in v3, 0 in v4 and 1.0 in v5. */
static v9x_u32 v9x_vi_solid_vs_position(v9x_u32 *out)
{
    v9x_u32 count = 0ul;

    out[count++] = v9x_vi_vopc(V9X_VI_VOPC_CMP_EQ_U32, V9X_VI_INLINE_INT(1),
                               0ul);
    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 1ul, 0ul);
    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 2ul, 2ul);
    out[count++] = v9x_vi_vop2(V9X_VI_VOP2_CNDMASK_B32, 1ul, V9X_VI_VGPR(1),
                               2ul);
    out[count++] = v9x_vi_vopc(V9X_VI_VOPC_CMP_EQ_U32, V9X_VI_INLINE_INT(2),
                               0ul);
    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 3ul, 1ul);
    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 4ul, 3ul);
    out[count++] = v9x_vi_vop2(V9X_VI_VOP2_CNDMASK_B32, 3ul, V9X_VI_VGPR(3),
                               4ul);
    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 4ul, V9X_VI_INLINE_INT(0));
    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 5ul, V9X_VI_FLOAT_1);
    return count;
}

/* The parameter export the hardware pairs with the position carries
 * nothing the pixel shader reads. */
static v9x_u32 v9x_vi_solid_vs_exports(v9x_u32 *out)
{
    v9x_u32 count = 0ul;

    count += v9x_vi_exp(V9X_VI_EXP_PARAM0, 0, 0, 4ul, 4ul, 4ul, 4ul,
                        out + count);
    count += v9x_vi_exp(V9X_VI_EXP_POS0, 1, 0, 1ul, 3ul, 4ul, 5ul,
                        out + count);
    out[count++] = V9X_VI_S_ENDPGM;
    return count;
}

v9x_u32 v9x_vi_shader_solid_vs(v9x_u32 *out)
{
    v9x_u32 count = v9x_vi_solid_vs_position(out);

    return count + v9x_vi_solid_vs_exports(out + count);
}

/* The colour export is the pixel shader's last, so it carries done, and vm
 * (the valid mask) as every pixel shader's final export must. */
v9x_u32 v9x_vi_shader_solid_ps(v9x_u32 *out)
{
    v9x_u32 count = 0ul;
    v9x_u32 index;

    for (index = 0ul; index < 4ul; ++index) {
        out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, index, index);
    }
    count += v9x_vi_exp(V9X_VI_EXP_MRT0, 1, 1, 0ul, 1ul, 2ul, 3ul,
                        out + count);
    out[count++] = V9X_VI_S_ENDPGM;
    return count;
}

/* The store is waited for before the solid body, so a wave that dies in
 * its exports has still left its marker. */
v9x_u32 v9x_vi_shader_probe_vs(v9x_u32 *out)
{
    v9x_u32 count = 0ul;

    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 6ul, V9X_VI_PROBE_SGPR_LO);
    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 7ul, V9X_VI_PROBE_SGPR_HI);
    count += v9x_vi_flat_store_dword(6ul, 0ul, out + count);
    out[count++] = v9x_vi_vop2(V9X_VI_VOP2_AND_B32, 6ul, V9X_VI_INLINE_INT(3),
                               0ul);
    out[count++] = v9x_vi_vop2(V9X_VI_VOP2_LSHLREV_B32, 6ul,
                               V9X_VI_INLINE_INT(2), 6ul);
    out[count++] = v9x_vi_vop2(V9X_VI_VOP2_ADD_U32, 6ul, V9X_VI_PROBE_SGPR_LO,
                               6ul);
    out[count++] = v9x_vi_vop2(V9X_VI_VOP2_ADD_U32, 6ul,
                               V9X_VI_INLINE_INT(16), 6ul);
    count += v9x_vi_flat_store_dword(6ul, 0ul, out + count);
    out[count++] = v9x_vi_s_waitcnt(V9X_VI_WAITCNT_ALL);
    count += v9x_vi_solid_vs_position(out + count);

    /* The position as computed: x 16 bytes on, y 16 more. VCC is free
     * again once the v_cndmasks have run. */
    out[count++] = v9x_vi_vop2(V9X_VI_VOP2_ADD_U32, 6ul,
                               V9X_VI_INLINE_INT(16), 6ul);
    count += v9x_vi_flat_store_dword(6ul, 1ul, out + count);
    out[count++] = v9x_vi_vop2(V9X_VI_VOP2_ADD_U32, 6ul,
                               V9X_VI_INLINE_INT(16), 6ul);
    count += v9x_vi_flat_store_dword(6ul, 3ul, out + count);

    /* What the selects read: v2 (x2), and VCC as the first compare sets
     * it, 1.0 from v5 where set. v4 is put back to the 0 the exports
     * carry. */
    out[count++] = v9x_vi_vop2(V9X_VI_VOP2_ADD_U32, 6ul,
                               V9X_VI_INLINE_INT(16), 6ul);
    count += v9x_vi_flat_store_dword(6ul, 2ul, out + count);
    out[count++] = v9x_vi_vopc(V9X_VI_VOPC_CMP_EQ_U32, V9X_VI_INLINE_INT(1),
                               0ul);
    out[count++] = v9x_vi_vop2(V9X_VI_VOP2_CNDMASK_B32, 4ul,
                               V9X_VI_INLINE_INT(0), 5ul);
    out[count++] = v9x_vi_vop2(V9X_VI_VOP2_ADD_U32, 6ul,
                               V9X_VI_INLINE_INT(16), 6ul);
    count += v9x_vi_flat_store_dword(6ul, 4ul, out + count);
    out[count++] = v9x_vi_s_waitcnt(V9X_VI_WAITCNT_ALL);
    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 4ul, V9X_VI_INLINE_INT(0));
    return count + v9x_vi_solid_vs_exports(out + count);
}

v9x_u32 v9x_vi_shader_probe_ps(v9x_u32 *out)
{
    v9x_u32 count = 0ul;

    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 4ul, V9X_VI_PROBE_SGPR_LO);
    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 5ul, V9X_VI_PROBE_SGPR_HI);
    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 6ul, 0ul);
    count += v9x_vi_flat_store_dword(4ul, 6ul, out + count);
    out[count++] = v9x_vi_s_waitcnt(V9X_VI_WAITCNT_ALL);
    return count + v9x_vi_shader_solid_ps(out + count);
}

/* The vertex's address is s0:s1 + 32 x VertexID, carried into the high
 * dword: v_addc's inline 0 keeps VCC its one constant-bus read. FLAT has
 * no offset on GFX8, so the second half's address is computed too. */
v9x_u32 v9x_vi_shader_tl_vs(v9x_u32 *out)
{
    static const v9x_u32 channel_op[4] = {
        V9X_VI_VOP1_CVT_F32_UBYTE2, V9X_VI_VOP1_CVT_F32_UBYTE1,
        V9X_VI_VOP1_CVT_F32_UBYTE0, V9X_VI_VOP1_CVT_F32_UBYTE3
    };
    /* R, G, B, A land in v1, v2, v7, v8: the address VGPRs, free once the
     * loads are done. */
    static const v9x_u32 channel_vgpr[4] = { 1ul, 2ul, 7ul, 8ul };
    v9x_u32 count = 0ul;
    v9x_u32 index;

    out[count++] = v9x_vi_vop2(V9X_VI_VOP2_LSHLREV_B32, 1ul,
                               V9X_VI_INLINE_INT(V9X_VI_TL_SHIFT), 0ul);
    out[count++] = v9x_vi_vop2(V9X_VI_VOP2_ADD_U32, 1ul, 0ul, 1ul);
    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 2ul, 1ul);
    out[count++] = v9x_vi_vop2(V9X_VI_VOP2_ADDC_U32, 2ul, V9X_VI_INLINE_INT(0),
                               2ul);
    count += v9x_vi_flat_load_dwordx4(1ul, 3ul, out + count);
    out[count++] = v9x_vi_vop2(V9X_VI_VOP2_ADD_U32, 7ul,
                               V9X_VI_INLINE_INT(V9X_VI_TL_COLOUR_OFFSET), 1ul);
    out[count++] = v9x_vi_vop2(V9X_VI_VOP2_ADDC_U32, 8ul, V9X_VI_INLINE_INT(0),
                               2ul);
    count += v9x_vi_flat_load_dwordx4(7ul, 9ul, out + count);
    out[count++] = v9x_vi_s_waitcnt(V9X_VI_WAITCNT_ALL);

    /* D3DCOLOR is A8R8G8B8: blue the low byte. Each byte to a float, then
     * scaled by 1/255, the literal the one constant-bus read. */
    for (index = 0ul; index < 4ul; ++index) {
        out[count++] = v9x_vi_vop1(channel_op[index], channel_vgpr[index],
                                   V9X_VI_VGPR(9));
    }
    for (index = 0ul; index < 4ul; ++index) {
        out[count++] = v9x_vi_vop2(V9X_VI_VOP2_MUL_F32, channel_vgpr[index],
                                   V9X_VI_LITERAL, channel_vgpr[index]);
        out[count++] = V9X_VI_FLOAT_1_OVER_255;
    }
    count += v9x_vi_exp(V9X_VI_EXP_PARAM0, 0, 0, 1ul, 2ul, 7ul, 8ul,
                        out + count);

    /* Parameter 1: tu and tv as loaded (v11, v12), then 0 and 1.0 in the
     * colour and specular VGPRs, free after the conversions. */
    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 9ul, V9X_VI_INLINE_INT(0));
    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 10ul, V9X_VI_FLOAT_1);
    count += v9x_vi_exp(V9X_VI_EXP_PARAM0 + 1ul, 0, 0, 11ul, 12ul, 9ul, 10ul,
                        out + count);
    count += v9x_vi_exp(V9X_VI_EXP_POS0, 1, 0, 3ul, 4ul, 5ul, 6ul,
                        out + count);
    out[count++] = V9X_VI_S_ENDPGM;
    return count;
}

/* GCN 3 wants one wait state between a SALU write of M0 and a v_interp
 * that reads it; s_nop 0 is that one. */
v9x_u32 v9x_vi_shader_gouraud_ps(v9x_u32 *out)
{
    v9x_u32 count = 0ul;
    v9x_u32 chan;

    out[count++] = v9x_vi_s_mov_m0(0ul);
    out[count++] = v9x_vi_s_nop(0ul);
    for (chan = 0ul; chan < 4ul; ++chan) {
        out[count++] = v9x_vi_vintrp(V9X_VI_VINTRP_P1_F32, 2ul + chan, 0ul,
                                     0ul, chan);
        out[count++] = v9x_vi_vintrp(V9X_VI_VINTRP_P2_F32, 2ul + chan, 1ul,
                                     0ul, chan);
    }
    count += v9x_vi_exp(V9X_VI_EXP_MRT0, 1, 1, 2ul, 3ul, 4ul, 5ul,
                        out + count);
    out[count++] = V9X_VI_S_ENDPGM;
    return count;
}

/* M0 from PRIM_MASK in s12, after the descriptors; then parameter 1's u
 * and v into v2 and v3. */
static v9x_u32 v9x_vi_texture_ps_head(v9x_u32 *out)
{
    v9x_u32 count = 0ul;
    v9x_u32 chan;

    out[count++] = v9x_vi_s_mov_m0(V9X_VI_TEXTURE_USER_SGPRS);
    out[count++] = v9x_vi_s_nop(0ul);
    for (chan = 0ul; chan < 2ul; ++chan) {
        out[count++] = v9x_vi_vintrp(V9X_VI_VINTRP_P1_F32, 2ul + chan, 0ul,
                                     1ul, chan);
        out[count++] = v9x_vi_vintrp(V9X_VI_VINTRP_P2_F32, 2ul + chan, 1ul,
                                     1ul, chan);
    }
    return count;
}

/* The sample into v4-v7, waited for. */
static v9x_u32 v9x_vi_texture_ps_sample(v9x_u32 *out)
{
    v9x_u32 count;

    count = v9x_vi_image_sample(4ul, 2ul, V9X_VI_TEXTURE_T_SGPR,
                                V9X_VI_TEXTURE_S_SGPR, V9X_VI_DMASK_RGBA, out);
    out[count++] = v9x_vi_s_waitcnt(V9X_VI_WAITCNT_ALL);
    return count;
}

v9x_u32 v9x_vi_shader_texture_ps(v9x_u32 *out)
{
    v9x_u32 count = v9x_vi_texture_ps_head(out);

    count += v9x_vi_texture_ps_sample(out + count);
    count += v9x_vi_exp(V9X_VI_EXP_MRT0, 1, 1, 4ul, 5ul, 6ul, 7ul,
                        out + count);
    out[count++] = V9X_VI_S_ENDPGM;
    return count;
}

/* Direct3D's default stage: texel times diffuse. Both multiply operands
 * are VGPRs. */
v9x_u32 v9x_vi_shader_modulate_ps(v9x_u32 *out)
{
    v9x_u32 count = v9x_vi_texture_ps_head(out);
    v9x_u32 chan;

    for (chan = 0ul; chan < 4ul; ++chan) {
        out[count++] = v9x_vi_vintrp(V9X_VI_VINTRP_P1_F32, 8ul + chan, 0ul,
                                     0ul, chan);
        out[count++] = v9x_vi_vintrp(V9X_VI_VINTRP_P2_F32, 8ul + chan, 1ul,
                                     0ul, chan);
    }
    count += v9x_vi_texture_ps_sample(out + count);
    for (chan = 0ul; chan < 4ul; ++chan) {
        out[count++] = v9x_vi_vop2(V9X_VI_VOP2_MUL_F32, 4ul + chan,
                                   V9X_VI_VGPR(8ul + chan), 4ul + chan);
    }
    count += v9x_vi_exp(V9X_VI_EXP_MRT0, 1, 1, 4ul, 5ul, 6ul, 7ul,
                        out + count);
    out[count++] = V9X_VI_S_ENDPGM;
    return count;
}
