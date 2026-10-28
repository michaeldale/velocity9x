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

/* Vertex 1 takes x2 and vertex 2 takes y2: v_cndmask picks its VGPR
 * operand where VCC is set, its first operand elsewhere. The parameter
 * export the hardware pairs with the position carries nothing the pixel
 * shader reads. */
v9x_u32 v9x_vi_shader_solid_vs(v9x_u32 *out)
{
    v9x_u32 count = 0ul;

    out[count++] = v9x_vi_vopc(V9X_VI_VOPC_CMP_EQ_U32, V9X_VI_INLINE_INT(1),
                               0ul);
    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 2ul, 2ul);
    out[count++] = v9x_vi_vop2(V9X_VI_VOP2_CNDMASK_B32, 1ul, 0ul, 2ul);
    out[count++] = v9x_vi_vopc(V9X_VI_VOPC_CMP_EQ_U32, V9X_VI_INLINE_INT(2),
                               0ul);
    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 3ul, 3ul);
    out[count++] = v9x_vi_vop2(V9X_VI_VOP2_CNDMASK_B32, 3ul, 1ul, 3ul);
    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 4ul, V9X_VI_INLINE_INT(0));
    out[count++] = v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, 5ul, V9X_VI_FLOAT_1);
    count += v9x_vi_exp(V9X_VI_EXP_PARAM0, 0, 0, 4ul, 4ul, 4ul, 4ul,
                        out + count);
    count += v9x_vi_exp(V9X_VI_EXP_POS0, 1, 0, 1ul, 3ul, 4ul, 5ul,
                        out + count);
    out[count++] = V9X_VI_S_ENDPGM;
    return count;
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
