/*
 * AMD Evergreen shader words and the 3D bring-up's shaders. See
 * include\velocity9x\eg_shader.h for the contract and its sources.
 */
#include "velocity9x/eg_shader.h"

/* Field positions, Mesa r600 eg_sq.h. */
#define EG_CF_COUNT_SHIFT           10
#define EG_CF_EOP                   0x00200000ul
#define EG_CF_INST_SHIFT            22
#define EG_CF_BARRIER               0x80000000ul
#define EG_CF_ALU_COUNT_SHIFT       18
#define EG_CF_ALU_INST_SHIFT        26
#define EG_EXPORT_TYPE_SHIFT        13
#define EG_EXPORT_GPR_SHIFT         15
#define EG_ALU_SRC0_CHAN_SHIFT      10
#define EG_ALU_SRC1_SEL_SHIFT       13
#define EG_ALU_SRC1_CHAN_SHIFT      23
#define EG_ALU_LAST                 0x80000000ul
#define EG_ALU_WRITE_MASK           0x00000010ul
#define EG_ALU_INST_SHIFT           7
#define EG_ALU_BANK_SWIZZLE_SHIFT   18
#define EG_ALU_DST_GPR_SHIFT        21
#define EG_ALU_DST_CHAN_SHIFT       29
#define EG_VTX_MEGA_COUNT_SHIFT     26
#define EG_VTX_SEL_X_SHIFT          9
#define EG_VTX_SEL_Y_SHIFT          12
#define EG_VTX_SEL_Z_SHIFT          15
#define EG_VTX_SEL_W_SHIFT          18
#define EG_VTX_FORMAT_SHIFT         22
#define EG_VTX_NUM_FORMAT_SHIFT     28
#define EG_VTX_FORMAT_COMP_SIGNED   0x40000000ul
#define EG_VTX_MEGA_FETCH           0x00080000ul
#define EG_TEX_SEL_X_SHIFT          9
#define EG_TEX_SEL_Y_SHIFT          12
#define EG_TEX_SEL_Z_SHIFT          15
#define EG_TEX_SEL_W_SHIFT          18
#define EG_TEX_SRC_SEL_X_SHIFT      20
#define EG_TEX_SRC_SEL_Y_SHIFT      23
#define EG_TEX_SRC_SEL_Z_SHIFT      26
#define EG_TEX_SRC_SEL_W_SHIFT      29

/* Linux's blit fetches with NUM_FORMAT_ALL 2 (scaled) and signed
 * components; for float formats neither changes the value. Its INTERP
 * slots use bank swizzle 5. Both are kept as the measured shaders have
 * them. */
#define EG_VTX_NUM_FORMAT_SCALED    2ul
#define EG_ALU_BANK_SWIZZLE_INTERP  5ul

v9x_u32 v9x_eg_cf_clause(v9x_u32 cf_inst, v9x_u32 addr, v9x_u32 count,
                         v9x_u32 *out)
{
    out[0] = addr;
    out[1] = EG_CF_BARRIER | (cf_inst << EG_CF_INST_SHIFT) |
             ((count - 1ul) << EG_CF_COUNT_SHIFT);
    return 2ul;
}

v9x_u32 v9x_eg_cf_alu(v9x_u32 addr, v9x_u32 count, v9x_u32 *out)
{
    out[0] = addr;
    out[1] = EG_CF_BARRIER | (V9X_EG_CF_ALU << EG_CF_ALU_INST_SHIFT) |
             ((count - 1ul) << EG_CF_ALU_COUNT_SHIFT);
    return 2ul;
}

v9x_u32 v9x_eg_cf_export(v9x_u32 type, v9x_u32 array_base, v9x_u32 gpr,
                         v9x_u32 done, v9x_u32 end_of_program,
                         v9x_u32 barrier, v9x_u32 *out)
{
    out[0] = array_base | (type << EG_EXPORT_TYPE_SHIFT) |
             (gpr << EG_EXPORT_GPR_SHIFT);
    out[1] = V9X_EG_SWIZZLE_XYZW |
             ((done != 0ul ? V9X_EG_CF_EXPORT_DONE : V9X_EG_CF_EXPORT) <<
              EG_CF_INST_SHIFT) |
             (end_of_program != 0ul ? EG_CF_EOP : 0ul) |
             (barrier != 0ul ? EG_CF_BARRIER : 0ul);
    return 2ul;
}

/* A fetch's MEGA_FETCH_COUNT is its byte count less one; the first fetch
 * of a vertex also sets MEGA_FETCH and counts the whole stride, as Linux's
 * blit does. A float fetch keeps the blit's scaled, signed form; a
 * normalised one (a D3DCOLOR's bytes to 0..1) is NUM_FORMAT_ALL 0 and
 * unsigned. */
static v9x_u32 v9x_eg_vtx_form(v9x_u32 dst_gpr, v9x_u32 offset,
                               v9x_u32 count_bytes, v9x_u32 mega,
                               v9x_u32 data_format, v9x_u32 normalised,
                               v9x_u32 sel_x, v9x_u32 sel_y, v9x_u32 sel_z,
                               v9x_u32 sel_w, v9x_u32 *out)
{
    out[0] = V9X_EG_VTX_FETCH |
             ((count_bytes - 1ul) << EG_VTX_MEGA_COUNT_SHIFT);
    out[1] = dst_gpr | (sel_x << EG_VTX_SEL_X_SHIFT) |
             (sel_y << EG_VTX_SEL_Y_SHIFT) | (sel_z << EG_VTX_SEL_Z_SHIFT) |
             (sel_w << EG_VTX_SEL_W_SHIFT) |
             (data_format << EG_VTX_FORMAT_SHIFT);
    if (normalised == 0ul) {
        out[1] |= (EG_VTX_NUM_FORMAT_SCALED << EG_VTX_NUM_FORMAT_SHIFT) |
                  EG_VTX_FORMAT_COMP_SIGNED;
    }
    out[2] = offset | (mega != 0ul ? EG_VTX_MEGA_FETCH : 0ul);
    out[3] = 0ul;
    return 4ul;
}

static v9x_u32 v9x_eg_vtx(v9x_u32 dst_gpr, v9x_u32 offset,
                          v9x_u32 count_bytes, v9x_u32 mega,
                          v9x_u32 data_format, v9x_u32 sel_x, v9x_u32 sel_y,
                          v9x_u32 sel_z, v9x_u32 sel_w, v9x_u32 *out)
{
    return v9x_eg_vtx_form(dst_gpr, offset, count_bytes, mega, data_format,
                           0ul, sel_x, sel_y, sel_z, sel_w, out);
}

v9x_u32 v9x_eg_vtx_fetch(v9x_u32 dst_gpr, v9x_u32 offset,
                         v9x_u32 fetch_bytes, v9x_u32 data_format,
                         v9x_u32 sel_x, v9x_u32 sel_y, v9x_u32 sel_z,
                         v9x_u32 sel_w, v9x_u32 *out)
{
    return v9x_eg_vtx(dst_gpr, offset, fetch_bytes, 0ul, data_format,
                      sel_x, sel_y, sel_z, sel_w, out);
}

v9x_u32 v9x_eg_alu_interp(v9x_u32 alu_inst, v9x_u32 slot, v9x_u32 param,
                          v9x_u32 dst_gpr, v9x_u32 write, v9x_u32 last,
                          v9x_u32 *out)
{
    out[0] = ((1ul - (slot & 1ul)) << EG_ALU_SRC0_CHAN_SHIFT) |
             ((V9X_EG_ALU_SRC_PARAM + param) << EG_ALU_SRC1_SEL_SHIFT) |
             (last != 0ul ? EG_ALU_LAST : 0ul);
    out[1] = (write != 0ul ? EG_ALU_WRITE_MASK : 0ul) |
             (alu_inst << EG_ALU_INST_SHIFT) |
             (EG_ALU_BANK_SWIZZLE_INTERP << EG_ALU_BANK_SWIZZLE_SHIFT) |
             (dst_gpr << EG_ALU_DST_GPR_SHIFT) |
             ((slot & 3ul) << EG_ALU_DST_CHAN_SHIFT);
    return 2ul;
}

/* Bank swizzle VEC_012 reads src0 in cycle 0 and src1 in cycle 1. A
 * group of four slots that each read R(a).c and R(b).c of their own
 * channel c reads one GPR per channel per cycle, within the read-port
 * limit. */
v9x_u32 v9x_eg_alu_op2(v9x_u32 alu_inst, v9x_u32 src0_gpr,
                       v9x_u32 src0_chan, v9x_u32 src1_gpr,
                       v9x_u32 src1_chan, v9x_u32 dst_gpr, v9x_u32 dst_chan,
                       v9x_u32 last, v9x_u32 *out)
{
    return v9x_eg_alu_op2_ex(alu_inst, src0_gpr, src0_chan, src1_gpr,
                             src1_chan, 0ul, dst_gpr, dst_chan, last, out);
}

/* ALU_WORD0's SRC1_NEG (bit 25); ALU_WORD1_OP2's CLAMP (bit 31) and
 * SRC0_ABS and SRC1_ABS (bits 0 and 1, eg_sq.h). */
#define EG_ALU_SRC1_NEG_BIT 0x02000000ul
#define EG_ALU_CLAMP_BIT    0x80000000ul
#define EG_ALU_SRC0_ABS_BIT 0x00000001ul
#define EG_ALU_SRC1_ABS_BIT 0x00000002ul

v9x_u32 v9x_eg_alu_op2_ex(v9x_u32 alu_inst, v9x_u32 src0_sel,
                          v9x_u32 src0_chan, v9x_u32 src1_sel,
                          v9x_u32 src1_chan, v9x_u32 flags, v9x_u32 dst_gpr,
                          v9x_u32 dst_chan, v9x_u32 last, v9x_u32 *out)
{
    out[0] = src0_sel | (src0_chan << EG_ALU_SRC0_CHAN_SHIFT) |
             (src1_sel << EG_ALU_SRC1_SEL_SHIFT) |
             (src1_chan << EG_ALU_SRC1_CHAN_SHIFT) |
             ((flags & V9X_EG_ALU_NEG_SRC1) != 0ul ? EG_ALU_SRC1_NEG_BIT
                                                   : 0ul) |
             (last != 0ul ? EG_ALU_LAST : 0ul);
    out[1] = ((flags & V9X_EG_ALU_NO_WRITE) != 0ul ? 0ul
                                                   : EG_ALU_WRITE_MASK) |
             (alu_inst << EG_ALU_INST_SHIFT) |
             (dst_gpr << EG_ALU_DST_GPR_SHIFT) |
             (dst_chan << EG_ALU_DST_CHAN_SHIFT) |
             ((flags & V9X_EG_ALU_CLAMP) != 0ul ? EG_ALU_CLAMP_BIT : 0ul) |
             ((flags & V9X_EG_ALU_ABS_SRC0) != 0ul ? EG_ALU_SRC0_ABS_BIT
                                                   : 0ul) |
             ((flags & V9X_EG_ALU_ABS_SRC1) != 0ul ? EG_ALU_SRC1_ABS_BIT
                                                   : 0ul);
    return 2ul;
}

v9x_u32 v9x_eg_tex_sample(v9x_u32 src_gpr, v9x_u32 dst_gpr, v9x_u32 *out)
{
    out[0] = V9X_EG_TEX_SAMPLE | (src_gpr << 16);
    out[1] = dst_gpr | (V9X_EG_SEL_X << EG_TEX_SEL_X_SHIFT) |
             (V9X_EG_SEL_Y << EG_TEX_SEL_Y_SHIFT) |
             (V9X_EG_SEL_Z << EG_TEX_SEL_Z_SHIFT) |
             (V9X_EG_SEL_W << EG_TEX_SEL_W_SHIFT);
    out[2] = (V9X_EG_SEL_X << EG_TEX_SRC_SEL_X_SHIFT) |
             (V9X_EG_SEL_Y << EG_TEX_SRC_SEL_Y_SHIFT) |
             (V9X_EG_SEL_0 << EG_TEX_SRC_SEL_Z_SHIFT) |
             (V9X_EG_SEL_1 << EG_TEX_SRC_SEL_W_SHIFT);
    out[3] = 0ul;
    return 4ul;
}

/* COORD_TYPE_X and _Y (TEX_WORD1 bits 28 and 29, eg_sq.h): 1 takes the
 * coordinate as normalised, 0 to 1 across the texture, as Direct3D's are;
 * the blit's are texels. */
#define EG_TEX_COORD_NORMALISED_XY 0x30000000ul

v9x_u32 v9x_eg_tex_sample_normalised(v9x_u32 src_gpr, v9x_u32 dst_gpr,
                                     v9x_u32 *out)
{
    v9x_u32 count = v9x_eg_tex_sample(src_gpr, dst_gpr, out);

    out[1] |= EG_TEX_COORD_NORMALISED_XY;
    return count;
}

/* Pads to a 16-byte boundary: fetch clauses start on one. */
static v9x_u32 v9x_eg_pad(v9x_u32 *out, v9x_u32 at)
{
    while ((at & 3ul) != 0ul) {
        out[at++] = 0ul;
    }
    return at;
}

/* A VS of one fetch clause of two fetches (R1 then R0) at dword 8, then
 * POS0 from R1 and PARAM0 from R0. The fourth CF slot is left zero, as
 * Linux's blit leaves it, so the clause starts at dword 8. */
#define EG_VS_FETCH_DWORD 8ul

static v9x_u32 v9x_eg_vs_two_fetches(v9x_u32 stride, v9x_u32 second_offset,
                                     v9x_u32 second_bytes,
                                     v9x_u32 data_format, v9x_u32 sel_z,
                                     v9x_u32 sel_w, v9x_u32 *out)
{
    v9x_u32 at = 0ul;

    at += v9x_eg_cf_clause(V9X_EG_CF_VC, EG_VS_FETCH_DWORD / 2ul, 2ul,
                           out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_POS, V9X_EG_EXPORT_POS0, 1ul, 1ul,
                           0ul, 1ul, out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_PARAM, 0ul, 0ul, 1ul, 1ul, 0ul,
                           out + at);
    while (at < EG_VS_FETCH_DWORD) {
        out[at++] = 0ul;
    }
    at += v9x_eg_vtx(1ul, 0ul, stride, 1ul, data_format, V9X_EG_SEL_X,
                     V9X_EG_SEL_Y, sel_z, sel_w, out + at);
    at += v9x_eg_vtx(0ul, second_offset, second_bytes, 0ul, data_format,
                     V9X_EG_SEL_X, V9X_EG_SEL_Y, sel_z, sel_w, out + at);
    return at;
}

v9x_u32 v9x_eg_shader_blit_vs(v9x_u32 *out)
{
    /* Two floats each, z and w filled 0 and 1. */
    return v9x_eg_vs_two_fetches(16ul, 8ul, 8ul, V9X_EG_FMT_32_32_FLOAT,
                                 V9X_EG_SEL_0, V9X_EG_SEL_1, out);
}

v9x_u32 v9x_eg_shader_gouraud_vs(v9x_u32 *out)
{
    /* Position x, y, z, w then colour r, g, b, a: four floats each. */
    return v9x_eg_vs_two_fetches(32ul, 16ul, 16ul,
                                 V9X_EG_FMT_32_32_32_32_FLOAT, V9X_EG_SEL_Z,
                                 V9X_EG_SEL_W, out);
}

v9x_u32 v9x_eg_shader_blit_ps(v9x_u32 *out)
{
    v9x_u32 at = 0ul;
    v9x_u32 slot;

    at += v9x_eg_cf_alu(3ul, 4ul, out + at);
    at += v9x_eg_cf_clause(V9X_EG_CF_TC, 8ul, 1ul, out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_PIXEL, 0ul, 0ul, 1ul, 1ul, 1ul,
                           out + at);
    for (slot = 0ul; slot < 4ul; ++slot) {
        at += v9x_eg_alu_interp(V9X_EG_ALU_INTERP_XY, slot, 0ul, 0ul,
                                slot < 2ul, slot == 3ul, out + at);
    }
    at = v9x_eg_pad(out, at);
    at += v9x_eg_tex_sample(0ul, 0ul, out + at);
    return at;
}

/* All four channels of a parameter into R0: Z and W first, so the
 * barycentrics in R0.xy are read before the XY group overwrites them
 * (Mesa does the same). Eight slots. */
static v9x_u32 v9x_eg_interp_all(v9x_u32 param, v9x_u32 dst_gpr,
                                 v9x_u32 *out)
{
    v9x_u32 at = 0ul;
    v9x_u32 slot;

    for (slot = 0ul; slot < 4ul; ++slot) {
        at += v9x_eg_alu_interp(V9X_EG_ALU_INTERP_ZW, slot, param, dst_gpr,
                                slot >= 2ul, slot == 3ul, out + at);
    }
    for (slot = 0ul; slot < 4ul; ++slot) {
        at += v9x_eg_alu_interp(V9X_EG_ALU_INTERP_XY, slot, param, dst_gpr,
                                slot < 2ul, slot == 3ul, out + at);
    }
    return at;
}

static v9x_u32 v9x_eg_interp_colour(v9x_u32 param, v9x_u32 *out)
{
    return v9x_eg_interp_all(param, 0ul, out);
}

v9x_u32 v9x_eg_shader_gouraud_ps(v9x_u32 *out)
{
    v9x_u32 at = 0ul;

    at += v9x_eg_cf_alu(2ul, 8ul, out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_PIXEL, 0ul, 0ul, 1ul, 1ul, 1ul,
                           out + at);
    at += v9x_eg_interp_colour(0ul, out + at);
    return at;
}

/* The modulate VS: one fetch clause of three fetches at dword 8, then
 * POS0 from R1, PARAM0 (EXPORT) from R0 and PARAM1 (EXPORT_DONE) from
 * R2, the end of the program. */
v9x_u32 v9x_eg_shader_modulate_vs(v9x_u32 *out)
{
    v9x_u32 at = 0ul;

    at += v9x_eg_cf_clause(V9X_EG_CF_VC, EG_VS_FETCH_DWORD / 2ul, 3ul,
                           out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_POS, V9X_EG_EXPORT_POS0, 1ul, 1ul,
                           0ul, 1ul, out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_PARAM, 0ul, 0ul, 0ul, 0ul, 0ul,
                           out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_PARAM, 1ul, 2ul, 1ul, 1ul, 0ul,
                           out + at);
    at += v9x_eg_vtx(1ul, 0ul, V9X_EG_MODULATE_STRIDE, 1ul,
                     V9X_EG_FMT_32_32_32_32_FLOAT, V9X_EG_SEL_X,
                     V9X_EG_SEL_Y, V9X_EG_SEL_Z, V9X_EG_SEL_W, out + at);
    at += v9x_eg_vtx(0ul, 16ul, 16ul, 0ul, V9X_EG_FMT_32_32_32_32_FLOAT,
                     V9X_EG_SEL_X, V9X_EG_SEL_Y, V9X_EG_SEL_Z, V9X_EG_SEL_W,
                     out + at);
    at += v9x_eg_vtx(2ul, 32ul, 8ul, 0ul, V9X_EG_FMT_32_32_FLOAT,
                     V9X_EG_SEL_X, V9X_EG_SEL_Y, V9X_EG_SEL_0, V9X_EG_SEL_1,
                     out + at);
    return at;
}

/* The modulate PS's layout: four CF words, the interpolation clause (12
 * slots) at dword 8, the multiply (4 slots) at 32, the texture clause at
 * 40, on a 16-byte boundary. */
#define EG_MOD_INTERP_DWORD 8ul
#define EG_MOD_MUL_DWORD    32ul
#define EG_MOD_TEX_DWORD    40ul

v9x_u32 v9x_eg_shader_modulate_ps(v9x_u32 *out)
{
    v9x_u32 at = 0ul;
    v9x_u32 slot;

    at += v9x_eg_cf_alu(EG_MOD_INTERP_DWORD / 2ul, 12ul, out + at);
    at += v9x_eg_cf_clause(V9X_EG_CF_TC, EG_MOD_TEX_DWORD / 2ul, 1ul,
                           out + at);
    at += v9x_eg_cf_alu(EG_MOD_MUL_DWORD / 2ul, 4ul, out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_PIXEL, 0ul, 0ul, 1ul, 1ul, 1ul,
                           out + at);
    /* The coordinate into R1.xy first: the colour's XY group overwrites
     * the barycentrics in R0.xy, so it goes last. */
    for (slot = 0ul; slot < 4ul; ++slot) {
        at += v9x_eg_alu_interp(V9X_EG_ALU_INTERP_XY, slot, 1ul, 1ul,
                                slot < 2ul, slot == 3ul, out + at);
    }
    at += v9x_eg_interp_colour(0ul, out + at);
    for (slot = 0ul; slot < 4ul; ++slot) {
        at += v9x_eg_alu_op2(V9X_EG_ALU_MUL, 0ul, slot, 1ul, slot, 0ul,
                             slot, slot == 3ul, out + at);
    }
    at += v9x_eg_tex_sample(1ul, 1ul, out + at);
    return at;
}

/*
 * The Direct3D VS: the render core's TL vertex as it is (r3d.h,
 * V9X_R3D_VERTEX, 32 bytes): sx, sy, sz and rhw as four floats into R1,
 * exported as the position (PA_CL_VTE_CNTL then takes x, y and z as
 * already divided and w as 1/w); the D3DCOLOR at 16 as four normalised
 * bytes into R0, in memory order - B, G, R, A - which the colour buffer's
 * SWAP_STD writes back in the same order; textured, tu and tv at 24 into
 * R2, z and w filled 0 and 1. The specular dword at 20 is not fetched.
 */
#define EG_D3D_COLOUR_OFFSET 16ul
#define EG_D3D_UV_OFFSET     24ul

v9x_u32 v9x_eg_shader_d3d_vs(v9x_u32 textured, v9x_u32 *out)
{
    v9x_u32 at = 0ul;

    at += v9x_eg_cf_clause(V9X_EG_CF_VC, EG_VS_FETCH_DWORD / 2ul,
                           textured != 0ul ? 3ul : 2ul, out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_POS, V9X_EG_EXPORT_POS0, 1ul, 1ul,
                           0ul, 1ul, out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_PARAM, 0ul, 0ul, textured == 0ul,
                           textured == 0ul, 0ul, out + at);
    if (textured != 0ul) {
        at += v9x_eg_cf_export(V9X_EG_EXPORT_PARAM, 1ul, 2ul, 1ul, 1ul, 0ul,
                               out + at);
    }
    while (at < EG_VS_FETCH_DWORD) {
        out[at++] = 0ul;
    }
    at += v9x_eg_vtx(1ul, 0ul, V9X_EG_D3D_STRIDE, 1ul,
                     V9X_EG_FMT_32_32_32_32_FLOAT, V9X_EG_SEL_X,
                     V9X_EG_SEL_Y, V9X_EG_SEL_Z, V9X_EG_SEL_W, out + at);
    at += v9x_eg_vtx_form(0ul, EG_D3D_COLOUR_OFFSET, 4ul, 0ul,
                          V9X_EG_FMT_8_8_8_8, 1ul, V9X_EG_SEL_X,
                          V9X_EG_SEL_Y, V9X_EG_SEL_Z, V9X_EG_SEL_W, out + at);
    if (textured != 0ul) {
        at += v9x_eg_vtx(2ul, EG_D3D_UV_OFFSET, 8ul, 0ul,
                         V9X_EG_FMT_32_32_FLOAT, V9X_EG_SEL_X, V9X_EG_SEL_Y,
                         V9X_EG_SEL_0, V9X_EG_SEL_1, out + at);
    }
    return at;
}

/*
 * The Direct3D PS for one texture op: NONE exports the interpolated
 * colour (the Gouraud PS); REPLACE samples at the interpolated, normalised
 * coordinate and exports the texel; MODULATE is the modulate PS with
 * normalised coordinates, colour times texel in all four channels.
 */
#define EG_D3D_REPLACE_TEX_DWORD 16ul

v9x_u32 v9x_eg_shader_d3d_ps(v9x_u32 op, v9x_u32 *out)
{
    v9x_u32 at = 0ul;
    v9x_u32 slot;

    if (op == V9X_EG_D3D_OP_MODULATE) {
        at = v9x_eg_shader_modulate_ps(out);
        out[EG_MOD_TEX_DWORD + 1ul] |= EG_TEX_COORD_NORMALISED_XY;
        return at;
    }
    if (op != V9X_EG_D3D_OP_REPLACE) {
        return v9x_eg_shader_gouraud_ps(out);
    }
    at += v9x_eg_cf_alu(EG_VS_FETCH_DWORD / 2ul, 4ul, out + at);
    at += v9x_eg_cf_clause(V9X_EG_CF_TC, EG_D3D_REPLACE_TEX_DWORD / 2ul, 1ul,
                           out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_PIXEL, 0ul, 0ul, 1ul, 1ul, 1ul,
                           out + at);
    while (at < EG_VS_FETCH_DWORD) {
        out[at++] = 0ul;
    }
    for (slot = 0ul; slot < 4ul; ++slot) {
        at += v9x_eg_alu_interp(V9X_EG_ALU_INTERP_XY, slot, 1ul, 1ul,
                                slot < 2ul, slot == 3ul, out + at);
    }
    at += v9x_eg_tex_sample_normalised(1ul, 0ul, out + at);
    return at;
}

v9x_u32 v9x_eg_shader_d3d_vs_lit(v9x_u32 textured, v9x_u32 *out)
{
    v9x_u32 at = 0ul;
    v9x_u32 fetch_at;

    /* Four CF words untextured, five textured: the fetch clause follows
     * on the next 16-byte boundary, at dword 8 or 12. */
    fetch_at = textured != 0ul ? 12ul : 8ul;
    at += v9x_eg_cf_clause(V9X_EG_CF_VC, fetch_at / 2ul,
                           textured != 0ul ? 4ul : 3ul, out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_POS, V9X_EG_EXPORT_POS0, 1ul, 1ul,
                           0ul, 1ul, out + at);
    at += v9x_eg_cf_export(V9X_EG_EXPORT_PARAM, 0ul, 0ul, 0ul, 0ul, 0ul,
                           out + at);
    if (textured != 0ul) {
        at += v9x_eg_cf_export(V9X_EG_EXPORT_PARAM, 1ul, 2ul, 0ul, 0ul, 0ul,
                               out + at);
    }
    at += v9x_eg_cf_export(V9X_EG_EXPORT_PARAM, textured != 0ul ? 2ul : 1ul,
                           3ul, 1ul, 1ul, 0ul, out + at);
    at = v9x_eg_pad(out, at);
    at += v9x_eg_vtx(1ul, 0ul, V9X_EG_D3D_STRIDE, 1ul,
                     V9X_EG_FMT_32_32_32_32_FLOAT, V9X_EG_SEL_X,
                     V9X_EG_SEL_Y, V9X_EG_SEL_Z, V9X_EG_SEL_W, out + at);
    at += v9x_eg_vtx_form(0ul, EG_D3D_COLOUR_OFFSET, 4ul, 0ul,
                          V9X_EG_FMT_8_8_8_8, 1ul, V9X_EG_SEL_X,
                          V9X_EG_SEL_Y, V9X_EG_SEL_Z, V9X_EG_SEL_W, out + at);
    if (textured != 0ul) {
        at += v9x_eg_vtx(2ul, EG_D3D_UV_OFFSET, 8ul, 0ul,
                         V9X_EG_FMT_32_32_FLOAT, V9X_EG_SEL_X, V9X_EG_SEL_Y,
                         V9X_EG_SEL_0, V9X_EG_SEL_1, out + at);
    }
    at += v9x_eg_vtx_form(3ul, V9X_EG_D3D_SPECULAR_OFFSET, 4ul, 0ul,
                          V9X_EG_FMT_8_8_8_8, 1ul, V9X_EG_SEL_X,
                          V9X_EG_SEL_Y, V9X_EG_SEL_Z, V9X_EG_SEL_W, out + at);
    return at;
}

/* The 24 significant bits of a single: the leading one and 23 more. */
#define EG_FLOAT_SIGNIFICAND_BITS 24ul
#define EG_FLOAT_EXPONENT_BIAS    127ul

v9x_u32 v9x_eg_unorm_float_bits(v9x_u32 value, v9x_u32 max)
{
    v9x_u32 scaled = value;
    v9x_u32 shift = 0ul;
    v9x_u32 quotient = 0ul;
    v9x_u32 bit;

    if (value == 0ul || max == 0ul) {
        return 0ul;
    }
    /* Scale into [max, 2 max): the quotient into [1, 2). */
    while (scaled < max) {
        scaled <<= 1;
        ++shift;
    }
    /* Long division, a bit at a time, so nothing overflows 32 bits; the
     * remainder then rounds the last bit, ties up. */
    for (bit = 0ul; bit < EG_FLOAT_SIGNIFICAND_BITS; ++bit) {
        quotient <<= 1;
        if (scaled >= max) {
            quotient |= 1ul;
            scaled -= max;
        }
        scaled <<= 1;
    }
    if (scaled >= max) {
        ++quotient;
    }
    if (quotient >> EG_FLOAT_SIGNIFICAND_BITS != 0ul) {
        quotient >>= 1;
        --shift;
    }
    return ((EG_FLOAT_EXPONENT_BIAS - shift) << 23) |
           (quotient & 0x007ffffful);
}

/* Three slots R(dst).c = R(src).c + L.c (or - L.c) for c in x, y, z, the
 * literal L in the group's trailing dwords: three, padded to four, as the
 * sequencer reads literals in pairs (Mesa's r600_asm.c). */
static v9x_u32 v9x_eg_literal_group(v9x_u32 src_gpr, v9x_u32 dst_gpr,
                                    v9x_u32 negate, const v9x_u32 *literal,
                                    v9x_u32 *out)
{
    v9x_u32 at = 0ul;
    v9x_u32 chan;

    for (chan = 0ul; chan < 3ul; ++chan) {
        at += v9x_eg_alu_op2_ex(V9X_EG_ALU_ADD, src_gpr, chan,
                                V9X_EG_ALU_SRC_LITERAL, chan,
                                negate != 0ul ? V9X_EG_ALU_NEG_SRC1 : 0ul,
                                dst_gpr, chan, chan == 2ul, out + at);
    }
    for (chan = 0ul; chan < 3ul; ++chan) {
        out[at++] = literal[chan];
    }
    out[at++] = 0ul;
    return at;
}

/*
 * The lit PS's layout: four CF words; the interpolation clause at dword
 * 8 (the coordinate into R1.xy when textured, specular into R2, then the
 * colour into R0 unless the op replaces it); the combine clause after it;
 * the texture clause last, on a 16-byte boundary.
 */
#define EG_LIT_ALU_DWORD 8ul

/* The key test's temporary: the GPR past the lit PS's three. */
#define EG_KEY_GPR 3ul

/*
 * The colour key test on the texel in R(texel): R3 = texel - key, R3.x =
 * max(|R3.x|, |R3.y|), R3.x = max(R3.x, |R3.z|), then KILLGT kills the
 * pixel where the tolerance exceeds that largest difference. The groups
 * read GPRs the group before wrote, as the fog's do (measured, V9XEG3D
 * /d3dfog).
 */
static v9x_u32 v9x_eg_key_test(v9x_u32 texel_gpr, const v9x_u32 *key,
                               v9x_u32 tolerance, v9x_u32 *out)
{
    v9x_u32 at = 0ul;

    at += v9x_eg_literal_group(texel_gpr, EG_KEY_GPR, 1ul, key, out + at);
    at += v9x_eg_alu_op2_ex(V9X_EG_ALU_MAX, EG_KEY_GPR, V9X_EG_SEL_X,
                            EG_KEY_GPR, V9X_EG_SEL_Y,
                            V9X_EG_ALU_ABS_SRC0 | V9X_EG_ALU_ABS_SRC1,
                            EG_KEY_GPR, V9X_EG_SEL_X, 1ul, out + at);
    at += v9x_eg_alu_op2_ex(V9X_EG_ALU_MAX, EG_KEY_GPR, V9X_EG_SEL_X,
                            EG_KEY_GPR, V9X_EG_SEL_Z, V9X_EG_ALU_ABS_SRC1,
                            EG_KEY_GPR, V9X_EG_SEL_X, 1ul, out + at);
    at += v9x_eg_alu_op2_ex(V9X_EG_ALU_KILLGT, V9X_EG_ALU_SRC_LITERAL,
                            V9X_EG_SEL_X, EG_KEY_GPR, V9X_EG_SEL_X,
                            V9X_EG_ALU_NO_WRITE, 0ul, V9X_EG_SEL_X, 1ul,
                            out + at);
    out[at++] = tolerance;
    out[at++] = 0ul;
    return at;
}

v9x_u32 v9x_eg_shader_d3d_ps_lit(v9x_u32 op, v9x_u32 specular, v9x_u32 fog,
                                 v9x_u32 fog_color, v9x_u32 *out)
{
    struct v9x_eg_d3d_ps ps;

    ps.op = op;
    ps.specular = specular;
    ps.fog = fog;
    ps.fog_color = fog_color;
    ps.color_key = 0ul;
    ps.key[0] = 0ul;
    ps.key[1] = 0ul;
    ps.key[2] = 0ul;
    ps.key_tolerance = 0ul;
    return v9x_eg_shader_d3d_ps_built(&ps, out);
}

v9x_u32 v9x_eg_shader_d3d_ps_built(const struct v9x_eg_d3d_ps *ps,
                                   v9x_u32 *out)
{
    v9x_u32 at = EG_LIT_ALU_DWORD;
    v9x_u32 cf = 0ul;
    v9x_u32 op = ps->op;
    v9x_u32 specular = ps->specular;
    v9x_u32 fog = ps->fog;
    v9x_u32 fog_color = ps->fog_color;
    v9x_u32 textured = op != V9X_EG_D3D_OP_NONE;
    v9x_u32 interp_slots;
    v9x_u32 combine_at;
    v9x_u32 combine_slots;
    v9x_u32 literal[3];
    v9x_u32 slot;

    if (textured) {
        for (slot = 0ul; slot < 4ul; ++slot) {
            at += v9x_eg_alu_interp(V9X_EG_ALU_INTERP_XY, slot, 1ul, 1ul,
                                    slot < 2ul, slot == 3ul, out + at);
        }
    }
    at += v9x_eg_interp_all(textured ? 2ul : 1ul, 2ul, out + at);
    if (op != V9X_EG_D3D_OP_REPLACE) {
        at += v9x_eg_interp_colour(0ul, out + at);
    }
    interp_slots = (at - EG_LIT_ALU_DWORD) / 2ul;

    combine_at = at;
    /* The key on the texel as sampled, before the op changes it. */
    if (ps->color_key != 0ul && textured) {
        at += v9x_eg_key_test(op == V9X_EG_D3D_OP_REPLACE ? 0ul : 1ul,
                              ps->key, ps->key_tolerance, out + at);
    }
    if (op == V9X_EG_D3D_OP_MODULATE) {
        for (slot = 0ul; slot < 4ul; ++slot) {
            at += v9x_eg_alu_op2(V9X_EG_ALU_MUL, 0ul, slot, 1ul, slot, 0ul,
                                 slot, slot == 3ul, out + at);
        }
    }
    /* Specular adds to RGB and saturates; alpha is the diffuse's. */
    if (specular != 0ul) {
        for (slot = 0ul; slot < 3ul; ++slot) {
            at += v9x_eg_alu_op2_ex(V9X_EG_ALU_ADD, 0ul, slot, 2ul, slot,
                                    V9X_EG_ALU_CLAMP, 0ul, slot, slot == 2ul,
                                    out + at);
        }
    }
    /* Fog: R1 = R0 - fog, R1 *= f (R2.w), R0 = R1 + fog. The colour's
     * channels are in memory order, so x is blue. */
    if (fog != 0ul) {
        literal[0] = v9x_eg_unorm_float_bits(fog_color & 0xfful, 255ul);
        literal[1] = v9x_eg_unorm_float_bits((fog_color >> 8) & 0xfful,
                                             255ul);
        literal[2] = v9x_eg_unorm_float_bits((fog_color >> 16) & 0xfful,
                                             255ul);
        at += v9x_eg_literal_group(0ul, 1ul, 1ul, literal, out + at);
        for (slot = 0ul; slot < 3ul; ++slot) {
            at += v9x_eg_alu_op2(V9X_EG_ALU_MUL, 1ul, slot, 2ul,
                                 V9X_EG_SEL_W, 1ul, slot, slot == 2ul,
                                 out + at);
        }
        at += v9x_eg_literal_group(1ul, 0ul, 0ul, literal, out + at);
    }
    combine_slots = (at - combine_at) / 2ul;
    at = v9x_eg_pad(out, at);

    cf += v9x_eg_cf_alu(EG_LIT_ALU_DWORD / 2ul, interp_slots, out + cf);
    if (textured) {
        cf += v9x_eg_cf_clause(V9X_EG_CF_TC, at / 2ul, 1ul, out + cf);
        at += v9x_eg_tex_sample_normalised(
            1ul, op == V9X_EG_D3D_OP_REPLACE ? 0ul : 1ul, out + at);
    }
    if (combine_slots != 0ul) {
        cf += v9x_eg_cf_alu(combine_at / 2ul, combine_slots, out + cf);
    }
    cf += v9x_eg_cf_export(V9X_EG_EXPORT_PIXEL, 0ul, 0ul, 1ul, 1ul, 1ul,
                           out + cf);
    while (cf < EG_LIT_ALU_DWORD) {
        out[cf++] = 0ul;
    }
    return at;
}
