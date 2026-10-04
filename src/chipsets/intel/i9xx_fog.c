/*
 * The fog forms of the fragment programs and their constant, for the
 * 32-bit HAL alone. Split from i9xx_fragprog.c because the 16-bit driver
 * links that file into I9XXCODE, which has a 57,344-byte budget, and needs
 * only v9x_i9xx_fog_program_extent from it, for the decoder.
 */
#include "velocity9x/intel_gen3_3d.h"

/*
 * THE FOG PROGRAMS: each program in i9xx_fragprog.c, with its result blended
 * toward the fog colour by the vertex's fog factor. See intel_gen3_3d.h for
 * the arithmetic and its sources.
 *
 * Built from the same instructions those programs use - dcl, texld,
 * mul, mov - plus MAD with a constant and a negated, replicated source.
 * The colour each program computes goes to R1 (or is read where it already
 * is) instead of oC; the alpha is moved to oC.w unfogged, which is what
 * Direct3D's fog does.
 */
#define V9X_I9XX_FOG_REG_COLOR  1ul   /* R1: the program's colour */
#define V9X_I9XX_FOG_REG_TERM   2ul   /* R2: (1 - f) * fog colour  */

static v9x_u32 v9x_i9xx_fs_dcl(v9x_u32 *stream, v9x_u32 type, v9x_u32 nr)
{
    /* A sampler takes no channel mask; every other declaration takes all
     * four, one of the five masks the erratum allows. */
    stream[0] = V9X_I9XX_FS_D0_DCL |
                (type << V9X_I9XX_FS_TYPE_SHIFT) |
                (nr << V9X_I9XX_FS_NR_SHIFT) |
                (type == V9X_I9XX_FS_REG_TYPE_S ? 0ul
                                                : V9X_I9XX_FS_CHANNEL_ALL);
    stream[1] = 0ul;
    stream[2] = 0ul;
    return 3ul;
}

/* One arithmetic instruction: dest(type, nr, mask) = op(src0, src1, src2),
 * src0 with the given A1 swizzle bits, src1 and src2 with identity. */
static v9x_u32 v9x_i9xx_fs_arith(v9x_u32 *stream, v9x_u32 opcode,
                                 v9x_u32 dest_type, v9x_u32 dest_nr,
                                 v9x_u32 mask,
                                 v9x_u32 src0_type, v9x_u32 src0_nr,
                                 v9x_u32 src0_swizzle,
                                 v9x_u32 src1_type, v9x_u32 src1_nr,
                                 v9x_u32 src2_type, v9x_u32 src2_nr,
                                 v9x_u32 operands)
{
    stream[0] = opcode |
                (dest_type << V9X_I9XX_FS_TYPE_SHIFT) |
                (dest_nr << V9X_I9XX_FS_NR_SHIFT) |
                mask |
                (src0_type << V9X_I9XX_FS_A0_SRC0_TYPE_SHIFT) |
                (src0_nr << V9X_I9XX_FS_A0_SRC0_NR_SHIFT);
    stream[1] = src0_swizzle;
    stream[2] = 0ul;
    if (operands >= 2ul) {
        stream[1] |= (src1_type << V9X_I9XX_FS_A1_SRC1_TYPE_SHIFT) |
                     (src1_nr << V9X_I9XX_FS_A1_SRC1_NR_SHIFT) |
                     V9X_I9XX_FS_A1_SRC1_SWIZZLE_XY;
        stream[2] = V9X_I9XX_FS_A2_SRC1_SWIZZLE_ZW;
    }
    if (operands >= 3ul) {
        stream[2] |= (src2_type << V9X_I9XX_FS_A2_SRC2_TYPE_SHIFT) |
                     (src2_nr << V9X_I9XX_FS_A2_SRC2_NR_SHIFT) |
                     V9X_I9XX_FS_A2_SRC2_SWIZZLE_XYZW;
    }
    return 3ul;
}

v9x_status v9x_i9xx_build_fog_program(
    v9x_u32 program, v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 extent = v9x_i9xx_fog_program_extent(program);
    v9x_u32 at = 0ul;
    v9x_u32 color_type;
    v9x_u32 color_nr;
    v9x_u32 alpha_type;
    v9x_u32 alpha_nr;
    v9x_u16 textured = (program != V9X_I9XX_FOGPROG_UNTEXTURED) ? V9X_TRUE
                                                               : V9X_FALSE;
    v9x_u16 modulate = (textured != V9X_FALSE &&
                        program != V9X_I9XX_TEXPROG_DECAL) ? V9X_TRUE
                                                           : V9X_FALSE;

    if (written != 0) { *written = 0ul; }
    if (extent == 0ul || stream == 0 || written == 0 || capacity < extent) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* Where the colour and the alpha come from, per program: the vertex
     * colour (T8), the texel (R0), or their product (R1). */
    if (textured == V9X_FALSE) {
        color_type = V9X_I9XX_FS_REG_TYPE_T;
        color_nr = V9X_I9XX_FS_T_DIFFUSE;
        alpha_type = color_type;
        alpha_nr = color_nr;
    } else if (modulate == V9X_FALSE) {
        color_type = V9X_I9XX_FS_REG_TYPE_R;
        color_nr = 0ul;
        alpha_type = color_type;
        alpha_nr = color_nr;
    } else {
        color_type = V9X_I9XX_FS_REG_TYPE_R;
        color_nr = V9X_I9XX_FOG_REG_COLOR;
        if (program == V9X_I9XX_TEXPROG_MODULATE_TEXALPHA) {
            alpha_type = V9X_I9XX_FS_REG_TYPE_R;
            alpha_nr = 0ul;
        } else if (program == V9X_I9XX_TEXPROG_MODULATE_DIFFALPHA) {
            alpha_type = V9X_I9XX_FS_REG_TYPE_T;
            alpha_nr = V9X_I9XX_FS_T_DIFFUSE;
        } else {
            alpha_type = color_type;
            alpha_nr = color_nr;
        }
    }

    stream[at++] = V9X_I9XX_3DSTATE_PIXEL_SHADER | (extent - 2ul);

    /* Declarations first, in the order the programs above use. */
    if (textured != V9X_FALSE) {
        at += v9x_i9xx_fs_dcl(stream + at, V9X_I9XX_FS_REG_TYPE_T,
                              V9X_I9XX_FS_T_TEX0);
        at += v9x_i9xx_fs_dcl(stream + at, V9X_I9XX_FS_REG_TYPE_S, 0ul);
    }
    if (textured == V9X_FALSE || modulate != V9X_FALSE) {
        at += v9x_i9xx_fs_dcl(stream + at, V9X_I9XX_FS_REG_TYPE_T,
                              V9X_I9XX_FS_T_DIFFUSE);
    }
    at += v9x_i9xx_fs_dcl(stream + at, V9X_I9XX_FS_REG_TYPE_T,
                          V9X_I9XX_FS_T_SPECULAR);

    if (textured != V9X_FALSE) {
        /* texld R0 <- S0, T0: the sampling programs' load, into R0. */
        stream[at++] = V9X_I9XX_T0_TEXLD |
                       (V9X_I9XX_FS_REG_TYPE_R << V9X_I9XX_T0_DEST_TYPE_SHIFT) |
                       (0ul << V9X_I9XX_T0_DEST_NR_SHIFT) |
                       (0ul << V9X_I9XX_T0_SAMPLER_NR_SHIFT);
        stream[at++] = (V9X_I9XX_FS_REG_TYPE_T << V9X_I9XX_T1_ADDR_TYPE_SHIFT) |
                       (V9X_I9XX_FS_T_TEX0 << V9X_I9XX_T1_ADDR_NR_SHIFT);
        stream[at++] = 0ul;
    }
    if (modulate != V9X_FALSE) {
        /* mul R1, R0, T8: the modulate product, all four channels. */
        at += v9x_i9xx_fs_arith(stream + at, V9X_I9XX_FS_A0_MUL,
                                V9X_I9XX_FS_REG_TYPE_R, V9X_I9XX_FOG_REG_COLOR,
                                V9X_I9XX_FS_CHANNEL_ALL,
                                V9X_I9XX_FS_REG_TYPE_R, 0ul,
                                V9X_I9XX_FS_A1_SWIZZLE_XYZW,
                                V9X_I9XX_FS_REG_TYPE_T, V9X_I9XX_FS_T_DIFFUSE,
                                0ul, 0ul, 2ul);
    }
    /* mad R2, -T9.wwww, C0, C0: the fog colour weighted by (1 - f). All
     * four channels, so nothing later reads an unwritten temporary. */
    at += v9x_i9xx_fs_arith(stream + at, V9X_I9XX_FS_A0_MAD,
                            V9X_I9XX_FS_REG_TYPE_R, V9X_I9XX_FOG_REG_TERM,
                            V9X_I9XX_FS_CHANNEL_ALL,
                            V9X_I9XX_FS_REG_TYPE_T, V9X_I9XX_FS_T_SPECULAR,
                            V9X_I9XX_FS_A1_SWIZZLE_WWWW |
                                V9X_I9XX_FS_A1_NEGATE_SRC0,
                            V9X_I9XX_FS_REG_TYPE_CONST, 0ul,
                            V9X_I9XX_FS_REG_TYPE_CONST, 0ul, 3ul);
    /* mad oC.xyz, T9.wwww, colour, R2. */
    at += v9x_i9xx_fs_arith(stream + at, V9X_I9XX_FS_A0_MAD,
                            V9X_I9XX_FS_REG_TYPE_OC, 0ul,
                            V9X_I9XX_FS_CHANNEL_XYZ,
                            V9X_I9XX_FS_REG_TYPE_T, V9X_I9XX_FS_T_SPECULAR,
                            V9X_I9XX_FS_A1_SWIZZLE_WWWW,
                            color_type, color_nr,
                            V9X_I9XX_FS_REG_TYPE_R, V9X_I9XX_FOG_REG_TERM,
                            3ul);
    /* mov oC.w, alpha: fog does not touch alpha. */
    at += v9x_i9xx_fs_arith(stream + at, V9X_I9XX_FS_A0_MOV,
                            V9X_I9XX_FS_REG_TYPE_OC, 0ul,
                            V9X_I9XX_FS_CHANNEL_W,
                            alpha_type, alpha_nr,
                            V9X_I9XX_FS_A1_SWIZZLE_XYZW,
                            0ul, 0ul, 0ul, 0ul, 1ul);

    if (at != extent) {
        return V9X_STATUS_INVALID_STATE;
    }
    *written = at;
    return V9X_STATUS_OK;
}

/*
 * C0 = (red, green, blue, 1.0). Each component a float in [0, 1], checked
 * as a bit pattern: positive IEEE-754 magnitudes order as integers, so
 * anything above 0x3f800000 is greater than one, infinite or a NaN, and a
 * negative value has the sign bit and exceeds it too.
 */
#define V9X_I9XX_FLOAT_ONE_BITS ((v9x_u32)0x3f800000ul)

v9x_status v9x_i9xx_build_fog_constants(
    v9x_u32 red_bits, v9x_u32 green_bits, v9x_u32 blue_bits,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0 ||
        capacity < V9X_I9XX_FOG_CONSTANTS_DWORDS ||
        red_bits > V9X_I9XX_FLOAT_ONE_BITS ||
        green_bits > V9X_I9XX_FLOAT_ONE_BITS ||
        blue_bits > V9X_I9XX_FLOAT_ONE_BITS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    stream[0] = V9X_I9XX_3DSTATE_PS_CONSTANTS |
                (V9X_I9XX_FOG_CONSTANTS_DWORDS - 2ul);
    stream[1] = 1ul;                      /* C0 alone */
    stream[2] = red_bits;
    stream[3] = green_bits;
    stream[4] = blue_bits;
    stream[5] = V9X_I9XX_FLOAT_ONE_BITS;
    *written = V9X_I9XX_FOG_CONSTANTS_DWORDS;
    return V9X_STATUS_OK;
}
