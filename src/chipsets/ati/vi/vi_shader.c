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
#define V9X_VI_SOPP_WAITCNT     0xbf8c0000ul
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

v9x_u32 v9x_vi_s_waitcnt(v9x_u32 simm16)
{
    return V9X_VI_SOPP_WAITCNT | (simm16 & 0xfffful);
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
