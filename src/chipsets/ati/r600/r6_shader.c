/*
 * ATI R6xx shader words. See include\velocity9x\r6_shader.h for the
 * contract and the sources.
 */
#include "velocity9x/r6_shader.h"

/* CF_DWORD1 (r600_shader.h): COUNT 12:10 as count - 1, END_OF_PROGRAM 21,
 * CF_INST 29:23, BARRIER 31. */
#define R6_CF_COUNT_SHIFT       10
#define R6_CF_EOP_SHIFT         21
#define R6_CF_INST_SHIFT        23
#define R6_CF_BARRIER           0x80000000ul
/* CF_ALU_DWORD1: COUNT 24:18 as count - 1, CF_INST 29:26. */
#define R6_CF_ALU_COUNT_SHIFT   18
#define R6_CF_ALU_INST_SHIFT    26
/* CF_ALLOC_IMP_EXP_DWORD0: ARRAY_BASE 12:0, TYPE 14:13, RW_GPR 21:15.
 * DWORD1_SWIZ: SRC_SEL_X..W 11:0, BURST_COUNT 20:17 as count - 1, then
 * END_OF_PROGRAM, CF_INST and BARRIER where CF_DWORD1 has them. */
#define R6_EXP_TYPE_SHIFT       13
#define R6_EXP_GPR_SHIFT        15
#define R6_EXP_BURST_SHIFT      17
/* SRC_SEL X, Y, Z, W: SQ_SEL_X 0 | Y 1 << 3 | Z 2 << 6 | W 3 << 9. */
#define R6_EXP_SWIZZLE_XYZW     0x00000688ul

/* Opcodes (r600_reg_auto_r6xx.h). */
#define R6_CF_INST_VTX          0x02ul
#define R6_CF_INST_ALU          0x08ul
#define R6_CF_INST_EXPORT_DONE  0x28ul
#define R6_EXPORT_PIXEL         0ul
#define R6_EXPORT_POS           1ul
#define R6_EXPORT_PARAM         2ul
#define R6_EXPORT_POS0          60ul    /* CF_POS0 */
#define R6_OP2_MOV              0x19ul
#define R6_FMT_32_32_FLOAT      0x1eul
#define R6_NUM_FORMAT_SCALED    2ul
#define R6_SEL_X                0ul
#define R6_SEL_Y                1ul
#define R6_SEL_0                4ul
#define R6_SEL_1                5ul

/* ALU_DWORD0: SRC0_SEL 8:0, SRC0_ELEM 11:10, SRC1_SEL 21:13, SRC1_ELEM
 * 24:23, LAST 31. A SRC_SEL of 256 + n is constant-file entry n
 * (ALU_SRC_CFILE_BASE), which DX9_CONSTS in SQ_CONFIG selects. */
#define R6_ALU_SRC0_ELEM_SHIFT  10
#define R6_ALU_SRC1_SEL_SHIFT   13
#define R6_ALU_SRC1_ELEM_SHIFT  23
#define R6_ALU_LAST             0x80000000ul
#define R6_ALU_SRC_CFILE        256ul
/* R6xx_ALU_DWORD1_OP2: WRITE_MASK 4, FOG_MERGE 5, ALU_INST 17:8,
 * DST_GPR 27:21, DST_ELEM 30:29, CLAMP 31. */
#define R6_ALU_WRITE            0x00000010ul
#define R6_ALU_INST_SHIFT       8
#define R6_ALU_DST_GPR_SHIFT    21
#define R6_ALU_DST_ELEM_SHIFT   29
#define R6_ALU_CLAMP            0x80000000ul

/* VTX_DWORD0: SRC_GPR 22:16, MEGA_FETCH_COUNT 31:26 as bytes - 1.
 * VTX_DWORD1_GPR: DST_GPR 6:0, DST_SEL_X..W 20:9 (3 bits each),
 * DATA_FORMAT 27:22, NUM_FORMAT_ALL 29:28, FORMAT_COMP_ALL 30 (signed).
 * VTX_DWORD2: MEGA_FETCH 19. */
#define R6_VTX_MEGA_COUNT_SHIFT 26
#define R6_VTX_DST_SEL_SHIFT    9
#define R6_VTX_FORMAT_SHIFT     22
#define R6_VTX_NUM_FORMAT_SHIFT 28
#define R6_VTX_COMP_SIGNED      0x40000000ul
#define R6_VTX_MEGA_FETCH       0x00080000ul
/* The fetch: 8 bytes, two floats. */
#define R6_SOLID_FETCH_BYTES    8ul

/* Where the shaders' clauses start, in 8-byte units: the VS's fetch
 * clause after three CF words and one of padding, the PS's ALU clause
 * after two CF words. */
#define R6_SOLID_VS_FETCH_ADDR  4ul
#define R6_SOLID_PS_ALU_ADDR    2ul
#define R6_SOLID_PS_ALU_SLOTS   4ul

static v9x_u32 v9x_r6_shader_cf_word1(v9x_u32 cf_inst, v9x_u32 count,
                                      v9x_u32 end_of_program,
                                      v9x_u32 barrier)
{
    return ((count - 1ul) << R6_CF_COUNT_SHIFT) |
           (end_of_program << R6_CF_EOP_SHIFT) |
           (cf_inst << R6_CF_INST_SHIFT) |
           (barrier != 0ul ? R6_CF_BARRIER : 0ul);
}

static v9x_u32 v9x_r6_shader_export_word0(v9x_u32 type,
                                          v9x_u32 array_base, v9x_u32 gpr)
{
    return array_base | (type << R6_EXP_TYPE_SHIFT) |
           (gpr << R6_EXP_GPR_SHIFT);
}

/* EXPORT_DONE of XYZW, burst_count registers. */
static v9x_u32 v9x_r6_shader_export_word1(v9x_u32 burst_count,
                                          v9x_u32 end_of_program,
                                          v9x_u32 barrier)
{
    return R6_EXP_SWIZZLE_XYZW |
           ((burst_count - 1ul) << R6_EXP_BURST_SHIFT) |
           (end_of_program << R6_CF_EOP_SHIFT) |
           (R6_CF_INST_EXPORT_DONE << R6_CF_INST_SHIFT) |
           (barrier != 0ul ? R6_CF_BARRIER : 0ul);
}

v9x_u32 v9x_r6_shader_solid_vs(v9x_u32 *out)
{
    /* 0: the vertex clause, one fetch. */
    out[0] = R6_SOLID_VS_FETCH_ADDR;
    out[1] = v9x_r6_shader_cf_word1(R6_CF_INST_VTX, 1ul, 0ul, 1ul);
    /* 1: R1 to the position. */
    out[2] = v9x_r6_shader_export_word0(R6_EXPORT_POS, R6_EXPORT_POS0, 1ul);
    out[3] = v9x_r6_shader_export_word1(1ul, 0ul, 1ul);
    /* 2: R0 to parameter 0, the program's end. xf86 gives it BURST_COUNT
     * (0), which its macro also encodes as 0: one register. */
    out[4] = v9x_r6_shader_export_word0(R6_EXPORT_PARAM, 0ul, 0ul);
    out[5] = v9x_r6_shader_export_word1(1ul, 1ul, 0ul);
    /* 3: padding to the clause at 4. */
    out[6] = 0ul;
    out[7] = 0ul;
    /* 4/5: R1 = (x, y, 0, 1) from 8 bytes at R0.x's vertex, 32_32_FLOAT
     * scaled signed, as xf86 fetches it. */
    out[8] = (R6_SOLID_FETCH_BYTES - 1ul) << R6_VTX_MEGA_COUNT_SHIFT;
    out[9] = 1ul |
             (R6_SEL_X << R6_VTX_DST_SEL_SHIFT) |
             (R6_SEL_Y << (R6_VTX_DST_SEL_SHIFT + 3)) |
             (R6_SEL_0 << (R6_VTX_DST_SEL_SHIFT + 6)) |
             (R6_SEL_1 << (R6_VTX_DST_SEL_SHIFT + 9)) |
             (R6_FMT_32_32_FLOAT << R6_VTX_FORMAT_SHIFT) |
             (R6_NUM_FORMAT_SCALED << R6_VTX_NUM_FORMAT_SHIFT) |
             R6_VTX_COMP_SIGNED;
    out[10] = R6_VTX_MEGA_FETCH;
    out[11] = 0ul;
    return V9X_R6_SOLID_VS_DWORDS;
}

v9x_u32 v9x_r6_shader_solid_ps(v9x_u32 *out)
{
    v9x_u32 channel;

    /* 0: the ALU clause, four slots. */
    out[0] = R6_SOLID_PS_ALU_ADDR;
    out[1] = ((R6_SOLID_PS_ALU_SLOTS - 1ul) << R6_CF_ALU_COUNT_SHIFT) |
             (R6_CF_INST_ALU << R6_CF_ALU_INST_SHIFT) | R6_CF_BARRIER;
    /* 1: R0 to MRT 0, the program's end. */
    out[2] = v9x_r6_shader_export_word0(R6_EXPORT_PIXEL, 0ul, 0ul);
    out[3] = v9x_r6_shader_export_word1(1ul, 1ul, 1ul);
    /* 2-5: R0.c = C0.c, clamped, the last slot closing the group. Source 1
     * is R0.c, which MOV ignores; xf86 encodes it, so this does. */
    for (channel = 0ul; channel < 4ul; ++channel) {
        out[4ul + channel * 2ul] =
            R6_ALU_SRC_CFILE |
            (channel << R6_ALU_SRC0_ELEM_SHIFT) |
            (0ul << R6_ALU_SRC1_SEL_SHIFT) |
            (channel << R6_ALU_SRC1_ELEM_SHIFT) |
            (channel == 3ul ? R6_ALU_LAST : 0ul);
        out[5ul + channel * 2ul] =
            R6_ALU_WRITE | (R6_OP2_MOV << R6_ALU_INST_SHIFT) |
            (0ul << R6_ALU_DST_GPR_SHIFT) |
            (channel << R6_ALU_DST_ELEM_SHIFT) | R6_ALU_CLAMP;
    }
    return V9X_R6_SOLID_PS_DWORDS;
}
