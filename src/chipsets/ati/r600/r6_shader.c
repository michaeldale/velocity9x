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

/* More opcodes and fields for the interpolated draws (r600_reg_auto_r6xx.h;
 * Mesa r600_isa.c's R600 column and r600_formats.h agree). */
#define R6_CF_INST_TEX          0x01ul
#define R6_CF_INST_EXPORT       0x27ul
#define R6_OP2_MUL              0x01ul
#define R6_TEX_INST_SAMPLE      0x10ul
#define R6_FMT_32_32_32_32_FLOAT 0x23ul
#define R6_SEL_Z                2ul
#define R6_SEL_W                3ul
/* TEX_WORD0 SRC_GPR 22:16; TEX_WORD1 DST_SEL_X..W 20:9, COORD_TYPE_X..W
 * 31:28 left 0 (unnormalised, texels); TEX_WORD2 SAMPLER_ID 19:15,
 * SRC_SEL_X..W 31:20. Resource 0 and sampler 0 are the PS's first. */
#define R6_TEX_SRC_GPR_SHIFT    16
#define R6_TEX_DST_SEL_SHIFT    9
#define R6_TEX_SRC_SEL_SHIFT    20

/* The layouts, in dwords: four CF slots then the first clause at 8 (CF
 * address 4, 8-byte units), on the 16-byte boundary a fetch clause needs;
 * the modulate PS's texture clause after its ALU clause, at 16. */
#define R6_CLAUSE_DWORD         8ul
#define R6_MOD_TEX_DWORD        16ul
#define R6_FETCH_DWORDS         4ul
/* Mesa's vtx.mega_fetch_count 0x1F, as a byte count (the field is bytes
 * less one). */
#define R6_FETCH_COUNT_MESA     32ul
#define R6_ALU_SLOTS            4ul

/* One vertex fetch into dst_gpr: count_bytes from offset, (x, y, sel_z,
 * sel_w) of data_format. The first fetch of a vertex sets MEGA_FETCH and
 * counts the whole stride; the others count their own bytes (xf86's copy
 * and composite VSs, r600_shader.c). Scaled, signed, as xf86 fetches
 * floats; neither changes a float. */
static v9x_u32 v9x_r6_shader_fetch(v9x_u32 dst_gpr, v9x_u32 offset,
                                   v9x_u32 count_bytes, v9x_u32 mega,
                                   v9x_u32 data_format, v9x_u32 sel_z,
                                   v9x_u32 sel_w, v9x_u32 *out)
{
    out[0] = (count_bytes - 1ul) << R6_VTX_MEGA_COUNT_SHIFT;
    out[1] = dst_gpr |
             (R6_SEL_X << R6_VTX_DST_SEL_SHIFT) |
             (R6_SEL_Y << (R6_VTX_DST_SEL_SHIFT + 3)) |
             (sel_z << (R6_VTX_DST_SEL_SHIFT + 6)) |
             (sel_w << (R6_VTX_DST_SEL_SHIFT + 9)) |
             (data_format << R6_VTX_FORMAT_SHIFT) |
             (R6_NUM_FORMAT_SCALED << R6_VTX_NUM_FORMAT_SHIFT) |
             R6_VTX_COMP_SIGNED;
    out[2] = offset | (mega != 0ul ? R6_VTX_MEGA_FETCH : 0ul);
    out[3] = 0ul;
    return R6_FETCH_DWORDS;
}

/* An export of one register, XYZW, EXPORT or EXPORT_DONE. */
static v9x_u32 v9x_r6_shader_export_word1_inst(v9x_u32 cf_inst,
                                               v9x_u32 end_of_program,
                                               v9x_u32 barrier)
{
    return R6_EXP_SWIZZLE_XYZW |
           (end_of_program << R6_CF_EOP_SHIFT) |
           (cf_inst << R6_CF_INST_SHIFT) |
           (barrier != 0ul ? R6_CF_BARRIER : 0ul);
}

/* Four OP2 slots, one per channel c: dst.c = op(src0.c, src1.c), written
 * and clamped, the last closing the group. Bank swizzle VEC_012 (0) reads
 * src0 in cycle 0 and src1 in cycle 1, one GPR per channel per cycle.
 * Selects below 128 are GPRs, 256 + n constant n. */
static v9x_u32 v9x_r6_shader_alu4(v9x_u32 alu_inst, v9x_u32 src0_sel,
                                  v9x_u32 src1_sel, v9x_u32 dst_gpr,
                                  v9x_u32 *out)
{
    v9x_u32 channel;

    for (channel = 0ul; channel < R6_ALU_SLOTS; ++channel) {
        out[channel * 2ul] =
            src0_sel | (channel << R6_ALU_SRC0_ELEM_SHIFT) |
            (src1_sel << R6_ALU_SRC1_SEL_SHIFT) |
            (channel << R6_ALU_SRC1_ELEM_SHIFT) |
            (channel == R6_ALU_SLOTS - 1ul ? R6_ALU_LAST : 0ul);
        out[channel * 2ul + 1ul] =
            R6_ALU_WRITE | (alu_inst << R6_ALU_INST_SHIFT) |
            (dst_gpr << R6_ALU_DST_GPR_SHIFT) |
            (channel << R6_ALU_DST_ELEM_SHIFT) | R6_ALU_CLAMP;
    }
    return R6_ALU_SLOTS * 2ul;
}

static v9x_u32 v9x_r6_shader_cf_alu(v9x_u32 addr, v9x_u32 *out)
{
    out[0] = addr;
    out[1] = ((R6_ALU_SLOTS - 1ul) << R6_CF_ALU_COUNT_SHIFT) |
             (R6_CF_INST_ALU << R6_CF_ALU_INST_SHIFT) | R6_CF_BARRIER;
    return 2ul;
}

v9x_u32 v9x_r6_shader_gouraud_vs(v9x_u32 *out)
{
    v9x_u32 at;

    /* 0: the vertex clause at 8, two fetches. */
    out[0] = R6_CLAUSE_DWORD / 2ul;
    out[1] = v9x_r6_shader_cf_word1(R6_CF_INST_VTX, 2ul, 0ul, 1ul);
    /* 1: R1 to the position; 2: R0 to parameter 0, the program's end. */
    out[2] = v9x_r6_shader_export_word0(R6_EXPORT_POS, R6_EXPORT_POS0, 1ul);
    out[3] = v9x_r6_shader_export_word1(1ul, 0ul, 1ul);
    out[4] = v9x_r6_shader_export_word0(R6_EXPORT_PARAM, 0ul, 0ul);
    out[5] = v9x_r6_shader_export_word1(1ul, 1ul, 0ul);
    /* 3: padding. */
    out[6] = 0ul;
    out[7] = 0ul;
    at = R6_CLAUSE_DWORD;
    at += v9x_r6_shader_fetch(1ul, 0ul, V9X_R6_GOURAUD_STRIDE, 1ul,
                              R6_FMT_32_32_32_32_FLOAT, R6_SEL_Z, R6_SEL_W,
                              out + at);
    at += v9x_r6_shader_fetch(0ul, 16ul, 16ul, 0ul,
                              R6_FMT_32_32_32_32_FLOAT, R6_SEL_Z, R6_SEL_W,
                              out + at);
    return at;
}

/* The Z-constant VS's layout: four CF slots, the ALU clause at 8 (one or
 * two slots), the vertex clause at 16 on its 16-byte boundary. */
#define R6_ZCONST_FETCH_DWORD   16ul
#define R6_ALU_CHAN_Z           2ul
#define R6_ALU_CHAN_W           3ul

v9x_u32 v9x_r6_shader_gouraud_vs_zconst(v9x_u32 write_w, v9x_u32 *out)
{
    v9x_u32 slots = write_w != 0ul ? 2ul : 1ul;
    v9x_u32 at;

    /* 0: the vertex clause at 16, two fetches; 1: the ALU clause at 8;
     * 2: R1 to the position; 3: R0 to parameter 0, the program's end. */
    out[0] = R6_ZCONST_FETCH_DWORD / 2ul;
    out[1] = v9x_r6_shader_cf_word1(R6_CF_INST_VTX, 2ul, 0ul, 1ul);
    out[2] = R6_CLAUSE_DWORD / 2ul;
    out[3] = ((slots - 1ul) << R6_CF_ALU_COUNT_SHIFT) |
             (R6_CF_INST_ALU << R6_CF_ALU_INST_SHIFT) | R6_CF_BARRIER;
    out[4] = v9x_r6_shader_export_word0(R6_EXPORT_POS, R6_EXPORT_POS0, 1ul);
    out[5] = v9x_r6_shader_export_word1(1ul, 0ul, 1ul);
    out[6] = v9x_r6_shader_export_word0(R6_EXPORT_PARAM, 0ul, 0ul);
    out[7] = v9x_r6_shader_export_word1(1ul, 1ul, 0ul);
    /* 8: R1.z = VS constant 0's x, unclamped (a Z may be any float); 10:
     * R1.w = its y. Each the last of its group when it is the last slot. */
    at = R6_CLAUSE_DWORD;
    out[at++] = R6_ALU_SRC_CFILE | (0ul << R6_ALU_SRC0_ELEM_SHIFT) |
                (write_w != 0ul ? 0ul : R6_ALU_LAST);
    out[at++] = R6_ALU_WRITE | (R6_OP2_MOV << R6_ALU_INST_SHIFT) |
                (1ul << R6_ALU_DST_GPR_SHIFT) |
                (R6_ALU_CHAN_Z << R6_ALU_DST_ELEM_SHIFT);
    if (write_w != 0ul) {
        out[at++] = R6_ALU_SRC_CFILE | (1ul << R6_ALU_SRC0_ELEM_SHIFT) |
                    R6_ALU_LAST;
        out[at++] = R6_ALU_WRITE | (R6_OP2_MOV << R6_ALU_INST_SHIFT) |
                    (1ul << R6_ALU_DST_GPR_SHIFT) |
                    (R6_ALU_CHAN_W << R6_ALU_DST_ELEM_SHIFT);
    }
    while (at < R6_ZCONST_FETCH_DWORD) {
        out[at++] = 0ul;
    }
    at += v9x_r6_shader_fetch(1ul, 0ul, V9X_R6_GOURAUD_STRIDE, 1ul,
                              R6_FMT_32_32_32_32_FLOAT, R6_SEL_Z, R6_SEL_W,
                              out + at);
    at += v9x_r6_shader_fetch(0ul, 16ul, 16ul, 0ul,
                              R6_FMT_32_32_32_32_FLOAT, R6_SEL_Z, R6_SEL_W,
                              out + at);
    return at;
}

v9x_u32 v9x_r6_shader_gouraud_ps(v9x_u32 *out)
{
    v9x_u32 at = 0ul;

    /* The solid PS's layout (measured on M-NZHWBR), the MOVs reading the
     * interpolated R0 rather than constant 0. */
    at += v9x_r6_shader_cf_alu(R6_SOLID_PS_ALU_ADDR, out + at);
    out[at++] = v9x_r6_shader_export_word0(R6_EXPORT_PIXEL, 0ul, 0ul);
    out[at++] = v9x_r6_shader_export_word1(1ul, 1ul, 1ul);
    at += v9x_r6_shader_alu4(R6_OP2_MOV, 0ul, 0ul, 0ul, out + at);
    return at;
}

/* The Z-export PS's export base (xf86 r600_shader.h CF_PIXEL_Z; Mesa
 * r600_shader.c "61 computed Z vector") and its ALU slots: the Gouraud
 * PS's four MOVs, then R1.x = constant 0.x in a group of its own. */
#define R6_EXPORT_PIXEL_Z       61ul
#define R6_ZEXPORT_ALU_SLOTS    5ul

v9x_u32 v9x_r6_shader_gouraud_zexport_ps(v9x_u32 *out)
{
    v9x_u32 at;

    /* 0: the ALU clause at 8, five slots; 1: R1 to the Z export (EXPORT,
     * not the last pixel export); 2: R0 to MRT 0 (EXPORT_DONE), the
     * program's end, as Mesa ends a PS's pixel exports; 3: padding. */
    out[0] = R6_CLAUSE_DWORD / 2ul;
    out[1] = ((R6_ZEXPORT_ALU_SLOTS - 1ul) << R6_CF_ALU_COUNT_SHIFT) |
             (R6_CF_INST_ALU << R6_CF_ALU_INST_SHIFT) | R6_CF_BARRIER;
    out[2] = v9x_r6_shader_export_word0(R6_EXPORT_PIXEL, R6_EXPORT_PIXEL_Z,
                                        1ul);
    out[3] = v9x_r6_shader_export_word1_inst(R6_CF_INST_EXPORT, 0ul, 1ul);
    out[4] = v9x_r6_shader_export_word0(R6_EXPORT_PIXEL, 0ul, 0ul);
    out[5] = v9x_r6_shader_export_word1(1ul, 1ul, 1ul);
    out[6] = 0ul;
    out[7] = 0ul;
    at = R6_CLAUSE_DWORD;
    at += v9x_r6_shader_alu4(R6_OP2_MOV, 0ul, 0ul, 0ul, out + at);
    out[at++] = R6_ALU_SRC_CFILE | R6_ALU_LAST;
    out[at++] = R6_ALU_WRITE | (R6_OP2_MOV << R6_ALU_INST_SHIFT) |
                (1ul << R6_ALU_DST_GPR_SHIFT) | R6_ALU_CLAMP;
    return at;
}

v9x_u32 v9x_r6_shader_modulate_vs(v9x_u32 *out)
{
    v9x_u32 at;

    out[0] = R6_CLAUSE_DWORD / 2ul;
    out[1] = v9x_r6_shader_cf_word1(R6_CF_INST_VTX, 3ul, 0ul, 1ul);
    out[2] = v9x_r6_shader_export_word0(R6_EXPORT_POS, R6_EXPORT_POS0, 1ul);
    out[3] = v9x_r6_shader_export_word1(1ul, 0ul, 1ul);
    /* Parameter 0 (EXPORT) from R0, parameter 1 (EXPORT_DONE, the
     * program's end) from R2, neither with a barrier: xf86's composite VS. */
    out[4] = v9x_r6_shader_export_word0(R6_EXPORT_PARAM, 0ul, 0ul);
    out[5] = v9x_r6_shader_export_word1_inst(R6_CF_INST_EXPORT, 0ul, 0ul);
    out[6] = v9x_r6_shader_export_word0(R6_EXPORT_PARAM, 1ul, 2ul);
    out[7] = v9x_r6_shader_export_word1_inst(R6_CF_INST_EXPORT_DONE, 1ul,
                                             0ul);
    /* Every fetch indexes the buffer by R0.x, the vertex index, so R0 is
     * fetched last: every xf86 VS (copy, xv, composite) writes R0 with
     * its clause's last fetch. The first version fetched the colour into
     * R0 second and the coordinate after it; on M-NZHWBR (V9XR63D
     * /modulate, run a) the colour the PS multiplied by was wrong in every
     * channel. */
    /* Each fetch a mega-fetch of its own, MEGA_FETCH_COUNT 1Fh with
     * MEGA_FETCH set, as Mesa's R600 fetch shader fetches every element
     * (r600_create_vertex_fetch_shader; r600_bytecode_vtx_build sets
     * MEGA_FETCH(1) on every fetch before Cayman). The 40-byte stride is
     * past anything xf86 fetches as one mega-fetch (24 bytes at most).
     * A version with MEGA_FETCH clear on all three - mini-fetches with no
     * mega-fetch before them - drew the colour as (r, g / 2, b / 2, 1)
     * on M-NZHWBR at every vertex address (V9XR63D runs e and f). */
    at = R6_CLAUSE_DWORD;
    at += v9x_r6_shader_fetch(1ul, 0ul, R6_FETCH_COUNT_MESA, 1ul,
                              R6_FMT_32_32_32_32_FLOAT, R6_SEL_Z, R6_SEL_W,
                              out + at);
    at += v9x_r6_shader_fetch(2ul, 32ul, R6_FETCH_COUNT_MESA, 1ul,
                              R6_FMT_32_32_FLOAT, R6_SEL_0, R6_SEL_1,
                              out + at);
    at += v9x_r6_shader_fetch(0ul, 16ul, R6_FETCH_COUNT_MESA, 1ul,
                              R6_FMT_32_32_32_32_FLOAT, R6_SEL_Z, R6_SEL_W,
                              out + at);
    return at;
}

v9x_u32 v9x_r6_shader_modulate_ps(v9x_u32 *out)
{
    v9x_u32 at;

    /* 0: the texture clause at 16, one sample; 1: the ALU clause at 8;
     * 2: R2 to MRT 0, the program's end; 3: padding. */
    out[0] = R6_MOD_TEX_DWORD / 2ul;
    out[1] = v9x_r6_shader_cf_word1(R6_CF_INST_TEX, 1ul, 0ul, 1ul);
    (void)v9x_r6_shader_cf_alu(R6_CLAUSE_DWORD / 2ul, out + 2);
    out[4] = v9x_r6_shader_export_word0(R6_EXPORT_PIXEL, 0ul, 2ul);
    out[5] = v9x_r6_shader_export_word1(1ul, 1ul, 1ul);
    out[6] = 0ul;
    out[7] = 0ul;
    /* 8-15: R2.c = R1.c * R0.c, exactly xf86's composite MUL (texel
     * first, into a third GPR, which is exported): no slot writes a GPR
     * the group reads. */
    at = R6_CLAUSE_DWORD;
    at += v9x_r6_shader_alu4(R6_OP2_MUL, 1ul, 0ul, 2ul, out + at);
    /* 16-19: R1 = texture 0 at R1's (x, y, 0, 1), XYZW. */
    out[at++] = R6_TEX_INST_SAMPLE | (1ul << R6_TEX_SRC_GPR_SHIFT);
    out[at++] = 1ul |
                (R6_SEL_X << R6_TEX_DST_SEL_SHIFT) |
                (R6_SEL_Y << (R6_TEX_DST_SEL_SHIFT + 3)) |
                (R6_SEL_Z << (R6_TEX_DST_SEL_SHIFT + 6)) |
                (R6_SEL_W << (R6_TEX_DST_SEL_SHIFT + 9));
    out[at++] = (R6_SEL_X << R6_TEX_SRC_SEL_SHIFT) |
                (R6_SEL_Y << (R6_TEX_SRC_SEL_SHIFT + 3)) |
                (R6_SEL_0 << (R6_TEX_SRC_SEL_SHIFT + 6)) |
                (R6_SEL_1 << (R6_TEX_SRC_SEL_SHIFT + 9));
    out[at++] = 0ul;
    return at;
}
