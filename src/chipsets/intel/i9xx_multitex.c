/*
 * Two texture units on Gen3 (docs\plans\gen3-sgis-multitexture.md): the
 * fragment program that combines them, its constants, and the vertex run
 * that carries both coordinate sets. 32-bit HAL and host tests only; see
 * V9X_I9XX_TWO_UNITS in intel_gen3_3d.h for why none of it is in I9XXCODE.
 *
 * Built from the instructions the fog programs use (i9xx_fog.c) plus ADD.
 * Every encoding is Mesa's i915_reg.h. DERIVED AND UNVALIDATED until a
 * capture on the netbook says otherwise.
 */
#include "velocity9x/intel_gen3_3d.h"

#if V9X_I9XX_TWO_UNITS

/* The temporaries: R0 and R1 are the two texels, R2 the colour each unit
 * leaves, R3 a difference DECAL and BLEND need, R4 the fog term. */
#define V9X_I9XX_MT_TEXEL0   0ul
#define V9X_I9XX_MT_TEXEL1   1ul
#define V9X_I9XX_MT_COLOR    2ul
#define V9X_I9XX_MT_DELTA    3ul
#define V9X_I9XX_MT_FOG_TERM 4ul

/* The constants: C0 the fog colour, C(1 + k) unit k's environment colour. */
#define V9X_I9XX_MT_CONST_FOG 0ul
#define V9X_I9XX_MT_CONST_ENV 1ul

/* Bytes of a float 1.0, the largest component a constant may carry. */
#define V9X_I9XX_MT_FLOAT_ONE ((v9x_u32)0x3f800000ul)

static v9x_u32 v9x_i9xx_mt_dcl(v9x_u32 *stream, v9x_u32 type, v9x_u32 nr)
{
    /* A sampler takes no channel mask; every other declaration takes all
     * four, one of the five masks the erratum allows (as i9xx_fog.c). */
    stream[0] = V9X_I9XX_FS_D0_DCL |
                (type << V9X_I9XX_FS_TYPE_SHIFT) |
                (nr << V9X_I9XX_FS_NR_SHIFT) |
                (type == V9X_I9XX_FS_REG_TYPE_S ? 0ul
                                                : V9X_I9XX_FS_CHANNEL_ALL);
    stream[1] = 0ul;
    stream[2] = 0ul;
    return 3ul;
}

/* texld R<unit> from S<unit> through T<unit>. */
static v9x_u32 v9x_i9xx_mt_texld(v9x_u32 *stream, v9x_u32 unit)
{
    stream[0] = V9X_I9XX_T0_TEXLD |
                (V9X_I9XX_FS_REG_TYPE_R << V9X_I9XX_T0_DEST_TYPE_SHIFT) |
                (unit << V9X_I9XX_T0_DEST_NR_SHIFT) |
                (unit << V9X_I9XX_T0_SAMPLER_NR_SHIFT);
    stream[1] = (V9X_I9XX_FS_REG_TYPE_T << V9X_I9XX_T1_ADDR_TYPE_SHIFT) |
                ((V9X_I9XX_FS_T_TEX0 + unit) << V9X_I9XX_T1_ADDR_NR_SHIFT);
    stream[2] = 0ul;
    return 3ul;
}

/* One arithmetic instruction, i9xx_fog.c's form with src1 optionally
 * negated: dest(type, nr, mask) = op(src0 with `swizzle0`, src1, src2). */
static v9x_u32 v9x_i9xx_mt_arith(v9x_u32 *stream, v9x_u32 opcode,
                                 v9x_u32 dest_type, v9x_u32 dest_nr,
                                 v9x_u32 mask,
                                 v9x_u32 src0_type, v9x_u32 src0_nr,
                                 v9x_u32 swizzle0,
                                 v9x_u32 src1_type, v9x_u32 src1_nr,
                                 v9x_u32 negate1,
                                 v9x_u32 src2_type, v9x_u32 src2_nr,
                                 v9x_u32 operands)
{
    stream[0] = opcode |
                (dest_type << V9X_I9XX_FS_TYPE_SHIFT) |
                (dest_nr << V9X_I9XX_FS_NR_SHIFT) |
                mask |
                (src0_type << V9X_I9XX_FS_A0_SRC0_TYPE_SHIFT) |
                (src0_nr << V9X_I9XX_FS_A0_SRC0_NR_SHIFT);
    stream[1] = swizzle0;
    stream[2] = 0ul;
    if (operands >= 2ul) {
        stream[1] |= (src1_type << V9X_I9XX_FS_A1_SRC1_TYPE_SHIFT) |
                     (src1_nr << V9X_I9XX_FS_A1_SRC1_NR_SHIFT) |
                     V9X_I9XX_FS_A1_SRC1_SWIZZLE_XY;
        stream[2] = V9X_I9XX_FS_A2_SRC1_SWIZZLE_ZW;
        if (negate1 != 0ul) {
            stream[1] |= V9X_I9XX_FS_A1_NEGATE_SRC1_XY;
            stream[2] |= V9X_I9XX_FS_A2_NEGATE_SRC1_ZW;
        }
    }
    if (operands >= 3ul) {
        stream[2] |= (src2_type << V9X_I9XX_FS_A2_SRC2_TYPE_SHIFT) |
                     (src2_nr << V9X_I9XX_FS_A2_SRC2_NR_SHIFT) |
                     V9X_I9XX_FS_A2_SRC2_SWIZZLE_XYZW;
    }
    return 3ul;
}

static v9x_u16 v9x_i9xx_mt_known(const struct v9x_i9xx_combine *units)
{
    v9x_u32 unit;

    if (units == 0) {
        return V9X_FALSE;
    }
    for (unit = 0ul; unit < V9X_I9XX_TEXTURE_UNITS_MAX; ++unit) {
        if (units[unit].colour_op < V9X_I9XX_COMBINE_REPLACE ||
            units[unit].colour_op > V9X_I9XX_COMBINE_BLEND ||
            units[unit].alpha_op > V9X_I9XX_ALPHA_MODULATE) {
            return V9X_FALSE;
        }
    }
    return V9X_TRUE;
}

/* Instructions (three dwords each) one unit's colour combine takes. */
static v9x_u32 v9x_i9xx_mt_colour_steps(v9x_u32 op)
{
    return (op == V9X_I9XX_COMBINE_DECAL || op == V9X_I9XX_COMBINE_BLEND)
               ? 2ul : 1ul;
}

/*
 * Whether a unit writes R2.w: unit 0 always, since its alpha must be in R2
 * for unit 1 and the output to read; unit 1 unless it keeps unit 0's.
 */
static v9x_u16 v9x_i9xx_mt_writes_alpha(v9x_u32 unit, v9x_u32 alpha_op)
{
    return (unit == 0ul || alpha_op != V9X_I9XX_ALPHA_KEEP) ? V9X_TRUE
                                                            : V9X_FALSE;
}

v9x_u32 v9x_i9xx_two_unit_program_extent(
    const struct v9x_i9xx_combine *units, v9x_u32 fog)
{
    /* The header; T0, T1, S0, S1 and T8 declared, and T9 with fog; two
     * texlds; the combines; then the output: two MADs and a MOV with fog,
     * one MOV without. */
    v9x_u32 instructions = 5ul + 2ul;
    v9x_u32 unit;

    if (v9x_i9xx_mt_known(units) == V9X_FALSE) {
        return 0ul;
    }
    for (unit = 0ul; unit < V9X_I9XX_TEXTURE_UNITS_MAX; ++unit) {
        instructions += v9x_i9xx_mt_colour_steps(units[unit].colour_op);
        if (v9x_i9xx_mt_writes_alpha(unit, units[unit].alpha_op) !=
                V9X_FALSE) {
            instructions += 1ul;
        }
    }
    instructions += fog != 0ul ? 1ul + 3ul : 1ul;
    return 1ul + instructions * 3ul;
}

/*
 * One unit into R2, from its texel R<unit> and the previous colour: T8 for
 * unit 0, R2 for unit 1. Each instruction reads its sources before it
 * writes, so R2 may be both.
 */
static v9x_u32 v9x_i9xx_mt_unit(v9x_u32 *stream, v9x_u32 unit,
                                const struct v9x_i9xx_combine *combine)
{
    v9x_u32 at = 0ul;
    v9x_u32 prev_type = unit == 0ul ? V9X_I9XX_FS_REG_TYPE_T
                                    : V9X_I9XX_FS_REG_TYPE_R;
    v9x_u32 prev_nr = unit == 0ul ? V9X_I9XX_FS_T_DIFFUSE : V9X_I9XX_MT_COLOR;
    v9x_u32 r = V9X_I9XX_FS_REG_TYPE_R;

    switch (combine->colour_op) {
    case V9X_I9XX_COMBINE_REPLACE:
        /* mov R2.xyz, Rt */
        at += v9x_i9xx_mt_arith(stream + at, V9X_I9XX_FS_A0_MOV, r,
                                V9X_I9XX_MT_COLOR, V9X_I9XX_FS_CHANNEL_XYZ,
                                r, unit, V9X_I9XX_FS_A1_SWIZZLE_XYZW,
                                0ul, 0ul, 0ul, 0ul, 0ul, 1ul);
        break;
    case V9X_I9XX_COMBINE_MODULATE:
        /* mul R2.xyz, Cp, Rt */
        at += v9x_i9xx_mt_arith(stream + at, V9X_I9XX_FS_A0_MUL, r,
                                V9X_I9XX_MT_COLOR, V9X_I9XX_FS_CHANNEL_XYZ,
                                prev_type, prev_nr,
                                V9X_I9XX_FS_A1_SWIZZLE_XYZW,
                                r, unit, 0ul, 0ul, 0ul, 2ul);
        break;
    case V9X_I9XX_COMBINE_DECAL:
        /* add R3, Rt, -Cp; mad R2.xyz, Rt.wwww, R3, Cp. All four channels
         * of R3, so the MAD reads nothing unwritten. */
        at += v9x_i9xx_mt_arith(stream + at, V9X_I9XX_FS_A0_ADD, r,
                                V9X_I9XX_MT_DELTA, V9X_I9XX_FS_CHANNEL_ALL,
                                r, unit, V9X_I9XX_FS_A1_SWIZZLE_XYZW,
                                prev_type, prev_nr, 1ul, 0ul, 0ul, 2ul);
        at += v9x_i9xx_mt_arith(stream + at, V9X_I9XX_FS_A0_MAD, r,
                                V9X_I9XX_MT_COLOR, V9X_I9XX_FS_CHANNEL_XYZ,
                                r, unit, V9X_I9XX_FS_A1_SWIZZLE_WWWW,
                                r, V9X_I9XX_MT_DELTA, 0ul,
                                prev_type, prev_nr, 3ul);
        break;
    default:
        /* BLEND: add R3, Cc, -Cp; mad R2.xyz, Rt, R3, Cp. */
        at += v9x_i9xx_mt_arith(stream + at, V9X_I9XX_FS_A0_ADD, r,
                                V9X_I9XX_MT_DELTA, V9X_I9XX_FS_CHANNEL_ALL,
                                V9X_I9XX_FS_REG_TYPE_CONST,
                                V9X_I9XX_MT_CONST_ENV + unit,
                                V9X_I9XX_FS_A1_SWIZZLE_XYZW,
                                prev_type, prev_nr, 1ul, 0ul, 0ul, 2ul);
        at += v9x_i9xx_mt_arith(stream + at, V9X_I9XX_FS_A0_MAD, r,
                                V9X_I9XX_MT_COLOR, V9X_I9XX_FS_CHANNEL_XYZ,
                                r, unit, V9X_I9XX_FS_A1_SWIZZLE_XYZW,
                                r, V9X_I9XX_MT_DELTA, 0ul,
                                prev_type, prev_nr, 3ul);
        break;
    }

    if (combine->alpha_op == V9X_I9XX_ALPHA_REPLACE) {
        /* mov R2.w, Rt */
        at += v9x_i9xx_mt_arith(stream + at, V9X_I9XX_FS_A0_MOV, r,
                                V9X_I9XX_MT_COLOR, V9X_I9XX_FS_CHANNEL_W,
                                r, unit, V9X_I9XX_FS_A1_SWIZZLE_XYZW,
                                0ul, 0ul, 0ul, 0ul, 0ul, 1ul);
    } else if (combine->alpha_op == V9X_I9XX_ALPHA_MODULATE) {
        /* mul R2.w, Ap, Rt */
        at += v9x_i9xx_mt_arith(stream + at, V9X_I9XX_FS_A0_MUL, r,
                                V9X_I9XX_MT_COLOR, V9X_I9XX_FS_CHANNEL_W,
                                prev_type, prev_nr,
                                V9X_I9XX_FS_A1_SWIZZLE_XYZW,
                                r, unit, 0ul, 0ul, 0ul, 2ul);
    } else if (unit == 0ul) {
        /* mov R2.w, T8: unit 0 keeping the fragment's alpha. */
        at += v9x_i9xx_mt_arith(stream + at, V9X_I9XX_FS_A0_MOV, r,
                                V9X_I9XX_MT_COLOR, V9X_I9XX_FS_CHANNEL_W,
                                prev_type, prev_nr,
                                V9X_I9XX_FS_A1_SWIZZLE_XYZW,
                                0ul, 0ul, 0ul, 0ul, 0ul, 1ul);
    }
    return at;
}

v9x_status v9x_i9xx_build_two_unit_program(
    const struct v9x_i9xx_combine *units, v9x_u32 fog,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 extent = v9x_i9xx_two_unit_program_extent(units, fog);
    v9x_u32 at = 0ul;
    v9x_u32 unit;
    v9x_u32 r = V9X_I9XX_FS_REG_TYPE_R;

    if (written != 0) { *written = 0ul; }
    if (extent == 0ul || stream == 0 || written == 0 || capacity < extent) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    stream[at++] = V9X_I9XX_3DSTATE_PIXEL_SHADER | (extent - 2ul);
    for (unit = 0ul; unit < V9X_I9XX_TEXTURE_UNITS_MAX; ++unit) {
        at += v9x_i9xx_mt_dcl(stream + at, V9X_I9XX_FS_REG_TYPE_T,
                              V9X_I9XX_FS_T_TEX0 + unit);
    }
    for (unit = 0ul; unit < V9X_I9XX_TEXTURE_UNITS_MAX; ++unit) {
        at += v9x_i9xx_mt_dcl(stream + at, V9X_I9XX_FS_REG_TYPE_S, unit);
    }
    at += v9x_i9xx_mt_dcl(stream + at, V9X_I9XX_FS_REG_TYPE_T,
                          V9X_I9XX_FS_T_DIFFUSE);
    if (fog != 0ul) {
        at += v9x_i9xx_mt_dcl(stream + at, V9X_I9XX_FS_REG_TYPE_T,
                              V9X_I9XX_FS_T_SPECULAR);
    }
    /* Both texels first: one texture phase, before any arithmetic. */
    for (unit = 0ul; unit < V9X_I9XX_TEXTURE_UNITS_MAX; ++unit) {
        at += v9x_i9xx_mt_texld(stream + at, unit);
    }
    for (unit = 0ul; unit < V9X_I9XX_TEXTURE_UNITS_MAX; ++unit) {
        at += v9x_i9xx_mt_unit(stream + at, unit, &units[unit]);
    }

    if (fog != 0ul) {
        /* The fog programs' tail on R2 (i9xx_fog.c): R4 = (1 - f) * C0,
         * oC.xyz = f * R2 + R4, oC.w = R2.w. */
        at += v9x_i9xx_mt_arith(stream + at, V9X_I9XX_FS_A0_MAD, r,
                                V9X_I9XX_MT_FOG_TERM, V9X_I9XX_FS_CHANNEL_ALL,
                                V9X_I9XX_FS_REG_TYPE_T,
                                V9X_I9XX_FS_T_SPECULAR,
                                V9X_I9XX_FS_A1_SWIZZLE_WWWW |
                                    V9X_I9XX_FS_A1_NEGATE_SRC0,
                                V9X_I9XX_FS_REG_TYPE_CONST,
                                V9X_I9XX_MT_CONST_FOG, 0ul,
                                V9X_I9XX_FS_REG_TYPE_CONST,
                                V9X_I9XX_MT_CONST_FOG, 3ul);
        at += v9x_i9xx_mt_arith(stream + at, V9X_I9XX_FS_A0_MAD,
                                V9X_I9XX_FS_REG_TYPE_OC, 0ul,
                                V9X_I9XX_FS_CHANNEL_XYZ,
                                V9X_I9XX_FS_REG_TYPE_T,
                                V9X_I9XX_FS_T_SPECULAR,
                                V9X_I9XX_FS_A1_SWIZZLE_WWWW,
                                r, V9X_I9XX_MT_COLOR, 0ul,
                                r, V9X_I9XX_MT_FOG_TERM, 3ul);
        at += v9x_i9xx_mt_arith(stream + at, V9X_I9XX_FS_A0_MOV,
                                V9X_I9XX_FS_REG_TYPE_OC, 0ul,
                                V9X_I9XX_FS_CHANNEL_W,
                                r, V9X_I9XX_MT_COLOR,
                                V9X_I9XX_FS_A1_SWIZZLE_XYZW,
                                0ul, 0ul, 0ul, 0ul, 0ul, 1ul);
    } else {
        at += v9x_i9xx_mt_arith(stream + at, V9X_I9XX_FS_A0_MOV,
                                V9X_I9XX_FS_REG_TYPE_OC, 0ul,
                                V9X_I9XX_FS_CHANNEL_ALL,
                                r, V9X_I9XX_MT_COLOR,
                                V9X_I9XX_FS_A1_SWIZZLE_XYZW,
                                0ul, 0ul, 0ul, 0ul, 0ul, 1ul);
    }

    if (at != extent) {
        return V9X_STATUS_INVALID_STATE;
    }
    *written = at;
    return V9X_STATUS_OK;
}

v9x_u32 v9x_i9xx_two_unit_constants_mask(
    const struct v9x_i9xx_combine *units, v9x_u32 fog)
{
    v9x_u32 mask = fog != 0ul ? (1ul << V9X_I9XX_MT_CONST_FOG) : 0ul;
    v9x_u32 unit;

    if (v9x_i9xx_mt_known(units) == V9X_FALSE) {
        return 0ul;
    }
    for (unit = 0ul; unit < V9X_I9XX_TEXTURE_UNITS_MAX; ++unit) {
        if (units[unit].colour_op == V9X_I9XX_COMBINE_BLEND) {
            mask |= 1ul << (V9X_I9XX_MT_CONST_ENV + unit);
        }
    }
    return mask;
}

v9x_status v9x_i9xx_build_two_unit_constants(
    const struct v9x_i9xx_combine *units, v9x_u32 fog,
    const v9x_u32 *rgb_bits,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 mask;
    v9x_u32 count = 0ul;
    v9x_u32 reg;
    v9x_u32 at = 0ul;

    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0 || rgb_bits == 0 ||
        v9x_i9xx_mt_known(units) == V9X_FALSE) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    mask = v9x_i9xx_two_unit_constants_mask(units, fog);
    if (mask == 0ul) {
        return V9X_STATUS_OK;
    }
    for (reg = 0ul; reg < 3ul; ++reg) {
        if ((mask & (1ul << reg)) == 0ul) {
            continue;
        }
        ++count;
        if (rgb_bits[reg * 3ul] > V9X_I9XX_MT_FLOAT_ONE ||
            rgb_bits[reg * 3ul + 1ul] > V9X_I9XX_MT_FLOAT_ONE ||
            rgb_bits[reg * 3ul + 2ul] > V9X_I9XX_MT_FLOAT_ONE) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }
    if (capacity < 2ul + count * 4ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* The constants packet's layout: length (total - 2), the register
     * mask, then four floats per register named, in register order. */
    stream[at++] = V9X_I9XX_3DSTATE_PS_CONSTANTS | (count * 4ul);
    stream[at++] = mask;
    for (reg = 0ul; reg < 3ul; ++reg) {
        if ((mask & (1ul << reg)) == 0ul) {
            continue;
        }
        stream[at++] = rgb_bits[reg * 3ul];
        stream[at++] = rgb_bits[reg * 3ul + 1ul];
        stream[at++] = rgb_bits[reg * 3ul + 2ul];
        stream[at++] = V9X_I9XX_MT_FLOAT_ONE;
    }
    *written = at;
    return V9X_STATUS_OK;
}

v9x_u32 v9x_i9xx_two_unit_run_dwords(v9x_u32 triangles, v9x_u32 fog)
{
    if (triangles == 0ul || triangles > V9X_I9XX_RUNTIME_MAX_TRIANGLES) {
        return 0ul;
    }
    return 1ul + triangles * V9X_I9XX_VERTEX_COUNT *
                     (V9X_I9XX_TWO_UNIT_VERTEX_DWORDS +
                      (fog != 0ul ? 1ul : 0ul));
}

/*
 * The runtime run's vertex, both coordinate sets after the colours: set 0,
 * then set 1, Mesa's attribute order. The coordinate, depth and rhw checks
 * are i9xx_vertex.c's, on the same predicates; they are the memory-safety
 * argument for the path, and a second copy is kept short for that reason.
 */
v9x_status v9x_i9xx_build_two_unit_run(
    const v9x_u32 *xyzw, const v9x_u32 *colors, const v9x_u32 *speculars,
    const v9x_u32 *uv0, const v9x_u32 *uv1,
    v9x_u32 triangles, v9x_u32 width, v9x_u32 height,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 run_dwords =
        v9x_i9xx_two_unit_run_dwords(triangles, speculars != 0 ? 1ul : 0ul);
    v9x_u32 width_bits = 0ul;
    v9x_u32 height_bits = 0ul;
    v9x_u32 one_bits = 0ul;
    v9x_u32 at = 0ul;
    v9x_u32 vertex;

    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0 || xyzw == 0 || colors == 0 ||
        uv0 == 0 || uv1 == 0 || width == 0ul || height == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (run_dwords == 0ul || capacity < run_dwords) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    if (v9x_i9xx_float_from_int(width, &width_bits) != V9X_I9XX_FLOAT_OK ||
        v9x_i9xx_float_from_int(height, &height_bits) != V9X_I9XX_FLOAT_OK ||
        v9x_i9xx_float_from_int(1ul, &one_bits) != V9X_I9XX_FLOAT_OK) {
        return V9X_STATUS_INVALID_STATE;
    }

    stream[at++] = V9X_I9XX_3DPRIMITIVE_INLINE | V9X_I9XX_PRIM3D_TRILIST |
                   (run_dwords - 2ul);
    for (vertex = 0ul; vertex < triangles * V9X_I9XX_VERTEX_COUNT; ++vertex) {
        v9x_u32 base = vertex * 4ul;
        v9x_u32 pair = vertex * 2ul;

        if (v9x_i9xx_float_in_range(xyzw[base], width_bits) == V9X_FALSE ||
            v9x_i9xx_float_in_range(xyzw[base + 1ul], height_bits) ==
                V9X_FALSE ||
            v9x_i9xx_float_in_range(xyzw[base + 2ul], one_bits) ==
                V9X_FALSE ||
            v9x_i9xx_float_positive_finite(xyzw[base + 3ul]) == V9X_FALSE ||
            v9x_i9xx_float_finite(uv0[pair]) == V9X_FALSE ||
            v9x_i9xx_float_finite(uv0[pair + 1ul]) == V9X_FALSE ||
            v9x_i9xx_float_finite(uv1[pair]) == V9X_FALSE ||
            v9x_i9xx_float_finite(uv1[pair + 1ul]) == V9X_FALSE) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        stream[at++] = xyzw[base];
        stream[at++] = xyzw[base + 1ul];
        stream[at++] = xyzw[base + 2ul];
        stream[at++] = xyzw[base + 3ul];
        stream[at++] = colors[vertex];
        if (speculars != 0) {
            stream[at++] = speculars[vertex];
        }
        stream[at++] = uv0[pair];
        stream[at++] = uv0[pair + 1ul];
        stream[at++] = uv1[pair];
        stream[at++] = uv1[pair + 1ul];
    }
    *written = at;
    return V9X_STATUS_OK;
}

#endif /* V9X_I9XX_TWO_UNITS */
