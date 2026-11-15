/*
 * A host model of the Tesla fragment instructions the encoder emits
 * (src\chipsets\nvidia\gt218\nv50_shader.c): it decodes the words by the
 * field positions test_nv50_shader.c holds to Mesa and envydis, and runs
 * them one fragment at a time. test_nv50_ps11.c holds the pixel shader
 * translator to the interpreter with it, test_nv50_d3d.c the two-unit
 * Direct3D programs to their combine arithmetic. What it cannot show is
 * the hardware disagreeing with Mesa; the probe stages are for that.
 */
#include <math.h>

#include "nv50_sh_sim.h"

static float as_float(v9x_u32 bits)
{
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static v9x_u32 as_bits(float value)
{
    v9x_u32 bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}



static int sim_fail(struct v9x_nv50_sim *sim, v9x_u32 at, const char *why)
{
    sim->error_at = at;
    sim->error = why;
    return 0;
}

float v9x_nv50_sim_reg(const struct v9x_nv50_sim *sim, v9x_u32 n)
{
    return as_float(sim->r[n]);
}

/* A flags register against a predicate condition: the value's zero and
 * sign as a set leaves them. */
static int sim_cond(const struct v9x_nv50_sim *sim, v9x_u32 cc, v9x_u32 reg)
{
    v9x_u32 value = sim->flags[reg];
    int zero = value == 0ul;
    int sign = (value & 0x80000000ul) != 0ul;

    switch (cc) {
    case V9X_NV50_SH_CC_NEVER:  return 0;
    case V9X_NV50_SH_CC_LT:     return sign && !zero;
    case V9X_NV50_SH_CC_EQ:     return zero;
    case V9X_NV50_SH_CC_LE:     return sign || zero;
    case V9X_NV50_SH_CC_GT:     return !sign && !zero;
    case V9X_NV50_SH_CC_NE:     return !zero;
    case V9X_NV50_SH_CC_GE:     return !sign || zero;
    default:                    return 1;
    }
}

/* An ordered float comparison by a set condition. */
static int sim_compare(v9x_u32 cc, float a, float b)
{
    switch (cc) {
    case V9X_NV50_SH_CC_LT:     return a < b;
    case V9X_NV50_SH_CC_EQ:     return a == b;
    case V9X_NV50_SH_CC_LE:     return a <= b;
    case V9X_NV50_SH_CC_GT:     return a > b;
    case V9X_NV50_SH_CC_NE:     return a < b || a > b;
    case V9X_NV50_SH_CC_GE:     return a >= b;
    case V9X_NV50_SH_CC_ALWAYS: return 1;
    default:                    return 0;
    }
}

static float sim_sat(float value)
{
    if (!(value > 0.0f)) {
        return 0.0f;
    }
    return value > 1.0f ? 1.0f : value;
}

/* A register operand; refuses past the allocation. */
static int sim_src_reg(struct v9x_nv50_sim *sim, v9x_u32 n, v9x_u32 at, float *out)
{
    if (n >= V9X_NV50_SIM_REGS) {
        return sim_fail(sim, at, "source register past the allocation");
    }
    *out = v9x_nv50_sim_reg(sim, n);
    return 1;
}

static int sim_src_const(struct v9x_nv50_sim *sim, v9x_u32 buffer, v9x_u32 id,
                         v9x_u32 at, float *out)
{
    if (buffer != sim->slot || id >= V9X_NV50_SIM_CONSTS) {
        return sim_fail(sim, at, "constant outside the block");
    }
    *out = sim->c[id];
    return 1;
}

static int sim_write(struct v9x_nv50_sim *sim, v9x_u32 w0, v9x_u32 w1, v9x_u32 value,
                     v9x_u32 at)
{
    v9x_u32 dst = (w0 >> 2) & 0x7ful;

    if ((w1 & 0x40ul) != 0ul) {
        sim->flags[(w1 >> 4) & 3ul] = value;
    }
    if ((w1 & 0x8ul) != 0ul) {
        if (dst == V9X_NV50_SIM_BUCKET) {
            return 1;
        }
        return sim_fail(sim, at, "a fragment program wrote an output");
    }
    if (dst >= V9X_NV50_SIM_REGS) {
        return sim_fail(sim, at, "destination past the allocation");
    }
    sim->r[dst] = value;
    return 1;
}

/* The source slots slots names (bit k: slot k) of a long instruction
 * (setSrcFileBits: slot 1 a constant with word 0 bit 23, slot 2 with
 * bit 24; the input file is not read by these programs). Word 1's slot 2
 * field is other fields in a two-source form, so only named slots are
 * read. */
#define SLOTS_01  3ul
#define SLOTS_02  5ul
#define SLOTS_012 7ul
static int sim_long_sources(struct v9x_nv50_sim *sim, v9x_u32 w0, v9x_u32 w1,
                            v9x_u32 at, v9x_u32 slots, float src[3])
{
    v9x_u32 buffer = (w1 >> 22) & 0xful;

    if ((w1 & 0x00200000ul) != 0ul) {
        return sim_fail(sim, at, "input file in a fragment program");
    }
    src[1] = 0.0f;
    src[2] = 0.0f;
    if (!sim_src_reg(sim, (w0 >> 9) & 0x7ful, at, &src[0])) {
        return 0;
    }
    if ((slots & 2ul) != 0ul) {
        if ((w0 & 0x00800000ul) != 0ul) {
            if (!sim_src_const(sim, buffer, (w0 >> 16) & 0x7ful, at,
                               &src[1])) {
                return 0;
            }
        } else if (!sim_src_reg(sim, (w0 >> 16) & 0x7ful, at, &src[1])) {
            return 0;
        }
    } else if ((w0 & 0x00800000ul) != 0ul) {
        return sim_fail(sim, at, "a constant in an unread slot");
    }
    if ((slots & 4ul) == 0ul) {
        if ((w0 & 0x01000000ul) != 0ul) {
            return sim_fail(sim, at, "a constant in an unread slot");
        }
        return 1;
    }
    if ((w0 & 0x01000000ul) != 0ul) {
        return sim_src_const(sim, buffer, (w1 >> 14) & 0x7ful, at, &src[2]);
    }
    return sim_src_reg(sim, (w1 >> 14) & 0x7ful, at, &src[2]);
}


static int sim_long(struct v9x_nv50_sim *sim, v9x_u32 w0, v9x_u32 w1, v9x_u32 at,
                    v9x_nv50_sim_sample_fn sample)
{
    v9x_u32 op = w0 >> 28;
    v9x_u32 top = w1 >> 29;
    float src[3];
    float result;
    volatile float product;

    if ((w0 & 3ul) == 3ul) {
        if (op != 0ul) {
            return sim_fail(sim, at, "flow instruction other than kil");
        }
        sim->killed = 1;
        return 1;
    }
    switch (op) {
    case 0x1ul:
        if ((w1 & 0x04000000ul) == 0ul || (w1 & 0x00200000ul) != 0ul) {
            return sim_fail(sim, at, "mov other than b32 from a register");
        }
        if (!sim_src_reg(sim, (w0 >> 9) & 0x7ful, at, &src[0])) {
            return 0;
        }
        return sim_write(sim, w0, w1, as_bits(src[0]), at);
    case 0x9ul:
        if (top != 0ul || !sim_long_sources(sim, w0, w1, at, 1ul, src)) {
            return sim_fail(sim, at, "sfn other than rcp");
        }
        return sim_write(sim, w0, w1, as_bits(1.0f / src[0]), at);
    case 0xbul:
        if (!sim_long_sources(sim, w0, w1, at,
                              top <= 1ul ? SLOTS_02 : SLOTS_01, src)) {
            return 0;
        }
        if (top <= 1ul) {       /* add, sat in bit 29 */
            float a = (w1 & 0x04000000ul) != 0ul ? -src[0] : src[0];
            float b = (w1 & 0x08000000ul) != 0ul ? -src[2] : src[2];

            result = a + b;
            if ((w1 & 0x20000000ul) != 0ul) {
                result = sim_sat(result);
            }
            return sim_write(sim, w0, w1, as_bits(result), at);
        }
        {
            float a = src[0];
            float b = src[1];

            if ((w1 & 0x00100000ul) != 0ul) {
                a = (float)fabs((double)a);
            }
            if ((w1 & 0x00080000ul) != 0ul) {
                b = (float)fabs((double)b);
            }
            if ((w1 & 0x04000000ul) != 0ul) {
                a = -a;
            }
            if ((w1 & 0x08000000ul) != 0ul) {
                b = -b;
            }
            if (top == 3ul) {
                return sim_write(sim, w0, w1,
                                 sim_compare((w1 >> 14) & 0xful, a, b)
                                     ? 0xfffffffful : 0ul,
                                 at);
            }
            if (top == 4ul) {
                return sim_write(sim, w0, w1, as_bits(a > b ? a : b), at);
            }
            if (top == 5ul) {
                return sim_write(sim, w0, w1, as_bits(a < b ? a : b), at);
            }
            return sim_fail(sim, at, "opcode b, unknown form");
        }
    case 0xcul:
        if ((w1 & 0xc0000000ul) != 0ul ||
            !sim_long_sources(sim, w0, w1, at, SLOTS_01, src)) {
            return sim_fail(sim, at, "opcode c other than mul f32");
        }
        result = src[0] * src[1];
        if ((w1 & 0x08000000ul) != 0ul) {
            result = -result;
        }
        if ((w1 & 0x00100000ul) != 0ul) {
            result = sim_sat(result);
        }
        return sim_write(sim, w0, w1, as_bits(result), at);
    case 0xeul:
        if ((w1 & 0xc0000000ul) != 0ul ||
            !sim_long_sources(sim, w0, w1, at, SLOTS_012, src)) {
            return sim_fail(sim, at, "opcode e other than mad f32");
        }
        /* Multiply, round, add, round (envydis: not fused). */
        product = src[0] * src[1];
        if ((w1 & 0x04000000ul) != 0ul) {
            product = -product;
        }
        result = product + ((w1 & 0x08000000ul) != 0ul ? -src[2] : src[2]);
        if ((w1 & 0x20000000ul) != 0ul) {
            result = sim_sat(result);
        }
        return sim_write(sim, w0, w1, as_bits(result), at);
    case 0xful:
    {
        v9x_u32 dst = (w0 >> 2) & 0x7ful;
        v9x_u32 argc = ((w0 >> 22) & 3ul) + 1ul;
        v9x_u32 cube = (w0 & 0x08000000ul) != 0ul ? 1ul : 0ul;
        v9x_u32 mask = ((w0 >> 25) & 3ul) | ((w1 >> 12) & 0xcul);
        v9x_u32 unit = (w0 >> 9) & 0x7ful;
        float coords[4];
        float rgba[4];
        v9x_u32 k;

        if ((w0 & 0x01000000ul) != 0ul || (w1 & 0xe0000000ul) != 0ul ||
            unit != ((w0 >> 17) & 0xful) || mask != 0xful ||
            dst + 4ul > V9X_NV50_SIM_REGS) {
            return sim_fail(sim, at, "tex other than a plain fetch");
        }
        for (k = 0ul; k < argc; ++k) {
            coords[k] = v9x_nv50_sim_reg(sim, dst + k);
        }
        sample(unit, argc, cube, coords, rgba);
        for (k = 0ul; k < 4ul; ++k) {
            sim->r[dst + k] = as_bits(rgba[k]);
        }
        return 1;
    }
    default:
        return sim_fail(sim, at, "long opcode the encoder does not emit");
    }
}

static int sim_short(struct v9x_nv50_sim *sim, v9x_u32 w0, v9x_u32 at)
{
    v9x_u32 op = w0 >> 28;
    v9x_u32 dst = (w0 >> 2) & 0x3ful;   /* six bits: envydis sdst_bf */
    float a;
    float b;
    float result;

    if (dst >= V9X_NV50_SIM_REGS) {
        return sim_fail(sim, at, "short destination past the allocation");
    }
    if (op == 0x8ul) {
        v9x_u32 slot = (w0 >> 16) & 0xfful;

        if (slot >= V9X_NV50_SIM_VARYINGS) {
            return sim_fail(sim, at, "varying past the model's");
        }
        result = sim->v[slot];
        if ((w0 & 0x02000000ul) != 0ul) {
            if (!sim_src_reg(sim, (w0 >> 9) & 0x3ful, at, &a)) {
                return 0;
            }
            result *= a;
        }
        sim->r[dst] = as_bits(result);
        return 1;
    }
    if ((w0 & 0x01800000ul) != 0ul) {
        return sim_fail(sim, at, "short form with a constant or input");
    }
    if (!sim_src_reg(sim, (w0 >> 9) & 0x3ful, at, &a) ||
        !sim_src_reg(sim, (w0 >> 16) & 0x3ful, at, &b)) {
        return 0;
    }
    switch (op) {
    case 0x1ul:
        if ((w0 & 0x00008000ul) == 0ul) {
            return sim_fail(sim, at, "short mov other than b32");
        }
        sim->r[dst] = sim->r[(w0 >> 9) & 0x3ful];
        return 1;
    case 0x9ul:
        sim->r[dst] = as_bits(1.0f / a);
        return 1;
    case 0xbul:
        if ((w0 & 0x00008000ul) != 0ul) {
            a = -a;
        }
        if ((w0 & 0x00400000ul) != 0ul) {
            b = -b;
        }
        result = a + b;
        break;
    case 0xcul:
        result = a * b;
        if ((w0 & 0x00008000ul) != 0ul) {
            result = -result;
        }
        break;
    default:
        return sim_fail(sim, at, "short opcode the encoder does not emit");
    }
    if ((w0 & 0x00000100ul) != 0ul) {
        result = sim_sat(result);
    }
    sim->r[dst] = as_bits(result);
    return 1;
}

/* Run program from word 0 to its exit. */
int v9x_nv50_sim_run(struct v9x_nv50_sim *sim, const v9x_u32 *code, v9x_u32 count,
                   v9x_nv50_sim_sample_fn sample)
{
    v9x_u32 pc = 0ul;

    while (pc < count) {
        v9x_u32 w0 = code[pc];
        v9x_u32 w1;

        if ((w0 & 1ul) == 0ul) {
            if (!sim_short(sim, w0, pc)) {
                return 0;
            }
            ++pc;
            continue;
        }
        if ((pc & 1ul) != 0ul) {
            return sim_fail(sim, pc, "long instruction off a 64-bit word");
        }
        if (pc + 1ul >= count) {
            return sim_fail(sim, pc, "long instruction cut off");
        }
        w1 = code[pc + 1ul];
        /* A predicated instruction, kil included, runs where its flags
         * pass. */
        if (sim_cond(sim, (w1 >> 7) & 0x1ful, (w1 >> 12) & 3ul)) {
            if (!sim_long(sim, w0, w1, pc, sample)) {
                return 0;
            }
        }
        if ((w0 & 3ul) != 3ul && (w1 & 1ul) != 0ul) {
            return 1;
        }
        pc += 2ul;
    }
    return sim_fail(sim, pc, "ran off the end without an exit");
}

