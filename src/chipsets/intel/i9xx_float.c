/*
 * IEEE-754 single-precision transport, by construction, without an FPU.
 *
 * The Gen3 vertex format takes screen coordinates as 32-bit floats in the
 * command stream (docs\decisions\2026-09-14-intel-gen3-3d-packet-audit.md
 * section 7: xf86 emits dstX + w directly through a float-to-uint32 pun). The
 * 16-bit driver must produce those dwords, and it has no business touching a
 * float type to do it: Win16 display drivers run with the FPU in whatever
 * state the interrupted application left it, this code can be entered from a
 * context where an x87 fault is not recoverable, and Open Watcom's 16-bit
 * floating-point emulation would drag a large library into an already-full
 * code segment.
 *
 * So the conversion is integer arithmetic that constructs the bit pattern
 * directly. Exact for every value below 2^24, which is every screen coordinate
 * this driver will ever emit by a factor of about twenty thousand.
 *
 * Both directions exist because INTEL3D0.TXT reports the vertices twice - as
 * raw bits and as decoded integers - so that a transport bug shows up in the
 * artefact rather than as a wrong picture. The decoder half is what makes that
 * redundancy meaningful rather than decorative.
 */
#include "velocity9x/intel_gen3_3d.h"

#define V9X_I9XX_FLOAT_SIGN         ((v9x_u32)0x80000000ul)
#define V9X_I9XX_FLOAT_EXP_MASK     ((v9x_u32)0x7f800000ul)
#define V9X_I9XX_FLOAT_MANTISSA     ((v9x_u32)0x007ffffful)
#define V9X_I9XX_FLOAT_EXP_SHIFT    23u
#define V9X_I9XX_FLOAT_EXP_BIAS     127u
/* 2^24: the first integer a single-precision float cannot represent exactly. */
#define V9X_I9XX_FLOAT_EXACT_LIMIT  ((v9x_u32)0x01000000ul)

/*
 * Build the bit pattern for a non-negative integer.
 *
 * Refuses at or above 2^24 rather than rounding. Rounding here would be a
 * silent loss of the exactness the whole design depends on, and no caller in
 * this driver has any use for a coordinate that large.
 */
v9x_u16 v9x_i9xx_float_from_int(v9x_u32 value, v9x_u32 *bits)
{
    v9x_u32 exponent;
    v9x_u32 mantissa;
    v9x_u32 shifted;

    if (bits == 0) {
        return V9X_I9XX_FLOAT_NOT_FINITE;
    }
    *bits = 0ul;
    if (value >= V9X_I9XX_FLOAT_EXACT_LIMIT) {
        return V9X_I9XX_FLOAT_TOO_LARGE;
    }
    if (value == 0ul) {
        /* Positive zero. Negative zero is never produced here, and is refused
         * on the way back in, so the two can never be confused. */
        return V9X_I9XX_FLOAT_OK;
    }

    /* Find the most significant set bit; that index is the exponent. */
    exponent = 0ul;
    shifted = value;
    while (shifted > 1ul) {
        shifted >>= 1;
        ++exponent;
    }

    /*
     * Normalise so the implicit leading 1 sits just above the mantissa field,
     * then drop it. exponent is at most 23 here, so the shift is in range.
     */
    mantissa = (value << (V9X_I9XX_FLOAT_EXP_SHIFT - (v9x_u16)exponent)) &
               V9X_I9XX_FLOAT_MANTISSA;
    *bits = ((exponent + V9X_I9XX_FLOAT_EXP_BIAS) <<
             V9X_I9XX_FLOAT_EXP_SHIFT) | mantissa;
    return V9X_I9XX_FLOAT_OK;
}

/*
 * Recover the integer from a bit pattern, refusing anything that is not a
 * non-negative finite integer - each with its own reason, because "the vertex
 * was wrong" is not a diagnosis and Phase 4 cost eight boots learning that.
 */
v9x_u16 v9x_i9xx_float_to_int(v9x_u32 bits, v9x_u32 *value)
{
    v9x_u32 exponent;
    v9x_u32 mantissa;
    v9x_u32 unbiased;

    if (value == 0) {
        return V9X_I9XX_FLOAT_NOT_FINITE;
    }
    *value = 0ul;
    mantissa = bits & V9X_I9XX_FLOAT_MANTISSA;
    exponent = (bits & V9X_I9XX_FLOAT_EXP_MASK) >> V9X_I9XX_FLOAT_EXP_SHIFT;

    if ((bits & V9X_I9XX_FLOAT_SIGN) != 0ul) {
        /* Negative zero is distinguished from every other negative, because it
         * is the one that compares equal to zero and would otherwise look like
         * a successful round trip. */
        if (exponent == 0ul && mantissa == 0ul) {
            return V9X_I9XX_FLOAT_NEGATIVE_ZERO;
        }
        return V9X_I9XX_FLOAT_NEGATIVE;
    }
    if (exponent == 0xfful) {
        /* Infinity and NaN share the all-ones exponent; neither is a
         * coordinate, and the caller does not need them told apart. */
        return V9X_I9XX_FLOAT_NOT_FINITE;
    }
    if (exponent == 0ul) {
        if (mantissa == 0ul) {
            return V9X_I9XX_FLOAT_OK;   /* positive zero */
        }
        return V9X_I9XX_FLOAT_DENORMAL;
    }
    if (exponent < V9X_I9XX_FLOAT_EXP_BIAS) {
        /* Between zero and one exclusive: a fraction, not an integer. */
        return V9X_I9XX_FLOAT_FRACTIONAL;
    }

    unbiased = exponent - V9X_I9XX_FLOAT_EXP_BIAS;
    if (unbiased > V9X_I9XX_FLOAT_EXP_SHIFT) {
        /*
         * At an unbiased exponent of 23 the mantissa's last bit is exactly the
         * units place, so 2^24 - 1 is the largest integer that survives and is
         * accepted. Anything above needs a negative shift and is refused - the
         * same boundary v9x_i9xx_float_from_int enforces from the other side.
         */
        return V9X_I9XX_FLOAT_TOO_LARGE;
    }
    /* Any mantissa bit below the units place is a fractional part. */
    if ((mantissa & ((1ul << (V9X_I9XX_FLOAT_EXP_SHIFT -
                              (v9x_u16)unbiased)) - 1ul)) != 0ul) {
        return V9X_I9XX_FLOAT_FRACTIONAL;
    }

    *value = (mantissa | (1ul << V9X_I9XX_FLOAT_EXP_SHIFT)) >>
             (V9X_I9XX_FLOAT_EXP_SHIFT - (v9x_u16)unbiased);
    return V9X_I9XX_FLOAT_OK;
}

/*
 * Positive, finite, and no greater than the limit.
 *
 * IEEE-754 positive magnitudes order exactly as unsigned integers, which is
 * what makes this one comparison rather than a decode - but ONLY while the
 * sign bit is clear. A negative float has bit 31 set and compares as a very
 * large positive one, so the sign is tested first and separately rather than
 * being folded into the same comparison.
 *
 * Infinity is 0x7F800000 and every NaN is above it, so any limit below
 * infinity excludes both without naming them.
 */
/*
 * A legal reciprocal homogeneous W: positive, finite, not zero.
 *
 * ONE predicate, used by the builder and by the decoder, because they are the
 * two independent judgements of the same value and a second copy is how they
 * would come to disagree.
 *
 * What it admits is anything a projection can produce. rhw is 1/W and Direct3D
 * defines it as varying - a vertex twice as far away carries half the rhw - so
 * the builder demanding exactly 1.0f refused ordinary projected geometry. It
 * went unnoticed because every scene this project has measured uses 1.0f.
 *
 * What it refuses has no reading as a reciprocal: zero (W infinite), negative
 * (a vertex behind the eye, which the core clips before this engine is
 * called), an infinity, and a NaN. Each hands the interpolator an undefined
 * span, and a rasteriser walking one is the failure the range checks on X, Y
 * and Z exist to prevent.
 *
 * A single unsigned comparison does it. IEEE-754 orders positive floats by
 * their bit patterns, so anything at or above 0x7f800000 is an infinity, a NaN
 * or negative - a negative has the sign bit set and therefore exceeds every
 * positive pattern when read unsigned. Zero and negative zero are excluded by
 * the same two tests.
 *
 * NOT a claim about what the hardware does with the value. This project has
 * measured no vertex with an rhw other than 1.0f on this part, and the first
 * application frame is where that starts being true.
 */
v9x_u16 v9x_i9xx_float_positive_finite(v9x_u32 bits)
{
    if (bits == 0ul || bits == 0x80000000ul) {
        return V9X_FALSE;
    }
    return bits < 0x7f800000ul ? V9X_TRUE : V9X_FALSE;
}

v9x_u16 v9x_i9xx_float_in_range(v9x_u32 bits, v9x_u32 limit_bits)
{
    if ((bits & 0x80000000ul) != 0ul) {
        /* Negative, including negative zero - which is a legal coordinate the
         * hardware would rasterise identically to positive zero, and is
         * refused anyway: a caller emitting it is a caller whose arithmetic
         * produced a sign nobody intended. */
        return V9X_FALSE;
    }
    return (bits <= limit_bits) ? V9X_TRUE : V9X_FALSE;
}
