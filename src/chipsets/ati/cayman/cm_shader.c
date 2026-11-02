/*
 * AMD Cayman shader words and the 3D bring-up's shaders. See
 * include\velocity9x\cm_shader.h for the contract and its sources.
 */
#include "velocity9x/cm_shader.h"
#include "velocity9x/eg_shader.h"

/* Field positions, Mesa r600 eg_sq.h, which Cayman shares. */
#define CM_CF_INST_SHIFT            22
#define CM_CF_BARRIER               0x80000000ul
#define CM_VTX_MEGA_COUNT_SHIFT     26
#define CM_VTX_SEL_X_SHIFT          9
#define CM_VTX_SEL_Y_SHIFT          12
#define CM_VTX_SEL_Z_SHIFT          15
#define CM_VTX_SEL_W_SHIFT          18
#define CM_VTX_FORMAT_SHIFT         22
#define CM_VTX_NUM_FORMAT_SHIFT     28
#define CM_VTX_FORMAT_COMP_SIGNED   0x40000000ul

/* Linux's Cayman blit keeps a mega-fetch count of 1 in every fetch's word
 * 0 and, as on Evergreen, NUM_FORMAT_ALL 2 (scaled) with signed
 * components, which change nothing for float formats. Kept as Linux has
 * them. */
#define CM_VTX_MEGA_COUNT_LINUX     1ul
#define CM_VTX_NUM_FORMAT_SCALED    2ul

/* The blit's layout: four CF slots (the fourth CF_END), so a VS's fetch
 * clause and a PS's ALU clause both start at dword 8, address 4. */
#define CM_CLAUSE_DWORD             8ul

v9x_u32 v9x_cm_cf_end(v9x_u32 *out)
{
    out[0] = 0ul;
    out[1] = CM_CF_BARRIER | (V9X_CM_CF_END << CM_CF_INST_SHIFT);
    return 2ul;
}

v9x_u32 v9x_cm_vtx_fetch(v9x_u32 dst_gpr, v9x_u32 offset,
                         v9x_u32 data_format, v9x_u32 sel_x, v9x_u32 sel_y,
                         v9x_u32 sel_z, v9x_u32 sel_w, v9x_u32 *out)
{
    out[0] = V9X_EG_VTX_FETCH |
             (CM_VTX_MEGA_COUNT_LINUX << CM_VTX_MEGA_COUNT_SHIFT);
    out[1] = dst_gpr | (sel_x << CM_VTX_SEL_X_SHIFT) |
             (sel_y << CM_VTX_SEL_Y_SHIFT) | (sel_z << CM_VTX_SEL_Z_SHIFT) |
             (sel_w << CM_VTX_SEL_W_SHIFT) |
             (data_format << CM_VTX_FORMAT_SHIFT) |
             (CM_VTX_NUM_FORMAT_SCALED << CM_VTX_NUM_FORMAT_SHIFT) |
             CM_VTX_FORMAT_COMP_SIGNED;
    out[2] = offset;
    out[3] = 0ul;
    return 4ul;
}

v9x_u32 v9x_cm_shader_blit_vs(v9x_u32 *out)
{
    v9x_u32 at = 0ul;

    /* Cayman has no vertex-fetch clause: the fetches run in a TC one. */
    at += v9x_eg_cf_clause(V9X_EG_CF_TC, CM_CLAUSE_DWORD / 2ul, 2ul,
                           out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_POS, V9X_EG_EXPORT_POS0, 1ul, 1ul,
                           0ul, 1ul, out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_PARAM, 0ul, 0ul, 1ul, 0ul, 0ul,
                           out + at);
    at += v9x_cm_cf_end(out + at);
    /* Position into R1, texture coordinate into R0: two floats each, z
     * and w filled 0 and 1. */
    at += v9x_cm_vtx_fetch(1ul, 0ul, V9X_EG_FMT_32_32_FLOAT, V9X_EG_SEL_X,
                           V9X_EG_SEL_Y, V9X_EG_SEL_0, V9X_EG_SEL_1,
                           out + at);
    at += v9x_cm_vtx_fetch(0ul, 8ul, V9X_EG_FMT_32_32_FLOAT, V9X_EG_SEL_X,
                           V9X_EG_SEL_Y, V9X_EG_SEL_0, V9X_EG_SEL_1,
                           out + at);
    return at;
}

v9x_u32 v9x_cm_shader_blit_ps(v9x_u32 *out)
{
    v9x_u32 at = 0ul;
    v9x_u32 slot;

    at += v9x_eg_cf_alu(CM_CLAUSE_DWORD / 2ul, 4ul, out + at);
    at += v9x_eg_cf_clause(V9X_EG_CF_TC, 8ul, 1ul, out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_PIXEL, 0ul, 0ul, 1ul, 0ul, 1ul,
                           out + at);
    at += v9x_cm_cf_end(out + at);
    /* The ALU clause fills dwords 8 to 15, so the texture clause at
     * address 8 needs no padding. */
    for (slot = 0ul; slot < 4ul; ++slot) {
        at += v9x_eg_alu_interp(V9X_EG_ALU_INTERP_XY, slot, 0ul, 0ul,
                                slot < 2ul, slot == 3ul, out + at);
    }
    at += v9x_eg_tex_sample(0ul, 0ul, out + at);
    return at;
}
