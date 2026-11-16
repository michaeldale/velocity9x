/*
 * The GCN 3 one-pixel model; see gcn_sim.h. Field layouts are the ones
 * vi_shader.h documents and test_vi_3d.c pins to LLVM's GFX8 bytes.
 */
#include <math.h>
#include <string.h>

#include "gcn_sim.h"

/* Source operand numbers (GCN 3 ISA, "Scalar ALU operands"). */
#define SIM_SRC_VCC_LO     106ul
#define SIM_SRC_VCC_HI     107ul
#define SIM_SRC_M0         124ul
#define SIM_SRC_EXEC_LO    126ul
#define SIM_SRC_EXEC_HI    127ul
#define SIM_SRC_INT_FIRST  128ul    /* 0 */
#define SIM_SRC_INT_LAST   192ul    /* 64 */
#define SIM_SRC_NEG_FIRST  193ul    /* -1 */
#define SIM_SRC_NEG_LAST   208ul    /* -16 */
#define SIM_SRC_LITERAL    255ul
#define SIM_SRC_VGPR       256ul

/* The float inline constants 240-247 (0.5, -0.5, 1, -1, 2, -2, 4, -4). */
static const v9x_u32 sim_inline_float[8] = {
    0x3f000000ul, 0xbf000000ul, 0x3f800000ul, 0xbf800000ul,
    0x40000000ul, 0xc0000000ul, 0x40800000ul, 0xc0800000ul
};

static float sim_f(v9x_u32 bits)
{
    union {
        v9x_u32 u;
        float f;
    } c;

    c.u = bits;
    return c.f;
}

static v9x_u32 sim_u(float value)
{
    union {
        v9x_u32 u;
        float f;
    } c;

    c.f = value;
    return c.u;
}

static int sim_fail(struct v9x_gcn_sim *sim, v9x_u32 at, const char *why)
{
    sim->error_at = at;
    sim->error = why;
    return 0;
}

void v9x_gcn_sim_reset(struct v9x_gcn_sim *sim)
{
    memset(sim, 0, sizeof(*sim));
    sim->exec[0] = 1ul;
    sim->error_at = 0xfffffffful;
}

float v9x_gcn_sim_vgpr(const struct v9x_gcn_sim *sim, v9x_u32 n)
{
    return sim_f(sim->v[n]);
}

/* A 32-bit source; *literal_used set when it took the literal. */
static int sim_read(struct v9x_gcn_sim *sim, v9x_u32 src, v9x_u32 literal,
                    int has_literal, v9x_u32 *value, int *literal_used)
{
    if (src >= SIM_SRC_VGPR) {
        *value = sim->v[src - SIM_SRC_VGPR];
        return 1;
    }
    if (src < V9X_GCN_SIM_SGPRS) {
        *value = sim->s[src];
        return 1;
    }
    if (src == SIM_SRC_VCC_LO || src == SIM_SRC_VCC_HI) {
        *value = sim->vcc[src - SIM_SRC_VCC_LO];
        return 1;
    }
    if (src == SIM_SRC_EXEC_LO || src == SIM_SRC_EXEC_HI) {
        *value = sim->exec[src - SIM_SRC_EXEC_LO];
        return 1;
    }
    if (src == SIM_SRC_M0) {
        *value = sim->m0;
        return 1;
    }
    if (src >= SIM_SRC_INT_FIRST && src <= SIM_SRC_INT_LAST) {
        *value = src - SIM_SRC_INT_FIRST;
        return 1;
    }
    if (src >= SIM_SRC_NEG_FIRST && src <= SIM_SRC_NEG_LAST) {
        *value = (v9x_u32)0 - (src - SIM_SRC_INT_LAST);
        return 1;
    }
    if (src >= 240ul && src <= 247ul) {
        *value = sim_inline_float[src - 240ul];
        return 1;
    }
    if (src == SIM_SRC_LITERAL && has_literal) {
        *value = literal;
        if (literal_used != 0) {
            *literal_used = 1;
        }
        return 1;
    }
    return 0;
}

/* A 64-bit scalar pair: an SGPR pair, VCC or EXEC. */
static int sim_read64(struct v9x_gcn_sim *sim, v9x_u32 src, v9x_u32 *pair)
{
    if (src + 1ul < V9X_GCN_SIM_SGPRS) {
        pair[0] = sim->s[src];
        pair[1] = sim->s[src + 1ul];
        return 1;
    }
    if (src == SIM_SRC_VCC_LO) {
        pair[0] = sim->vcc[0];
        pair[1] = sim->vcc[1];
        return 1;
    }
    if (src == SIM_SRC_EXEC_LO) {
        pair[0] = sim->exec[0];
        pair[1] = sim->exec[1];
        return 1;
    }
    return 0;
}

static int sim_write64(struct v9x_gcn_sim *sim, v9x_u32 dst,
                       const v9x_u32 *pair)
{
    if (dst + 1ul < V9X_GCN_SIM_SGPRS) {
        sim->s[dst] = pair[0];
        sim->s[dst + 1ul] = pair[1];
        return 1;
    }
    if (dst == SIM_SRC_VCC_LO) {
        sim->vcc[0] = pair[0];
        sim->vcc[1] = pair[1];
        return 1;
    }
    if (dst == SIM_SRC_EXEC_LO) {
        sim->exec[0] = pair[0];
        sim->exec[1] = pair[1];
        return 1;
    }
    return 0;
}

static int sim_write32(struct v9x_gcn_sim *sim, v9x_u32 dst, v9x_u32 value)
{
    if (dst < V9X_GCN_SIM_SGPRS) {
        sim->s[dst] = value;
        return 1;
    }
    if (dst == SIM_SRC_M0) {
        sim->m0 = value;
        return 1;
    }
    if (dst == SIM_SRC_VCC_LO || dst == SIM_SRC_VCC_HI) {
        sim->vcc[dst - SIM_SRC_VCC_LO] = value;
        return 1;
    }
    if (dst == SIM_SRC_EXEC_LO || dst == SIM_SRC_EXEC_HI) {
        sim->exec[dst - SIM_SRC_EXEC_LO] = value;
        return 1;
    }
    return 0;
}

/*
 * The constant bus (GCN 3 ISA, "VALU instruction operands"): a VALU
 * instruction reads at most one SGPR, VCC, M0, EXEC or literal; the same
 * SGPR read twice counts once. The model refuses a word that reads more,
 * which the hardware would run with garbage in one of them.
 */
static int sim_scalar(v9x_u32 field)
{
    return field < V9X_GCN_SIM_SGPRS || field == SIM_SRC_VCC_LO ||
           field == SIM_SRC_VCC_HI || field == SIM_SRC_M0 ||
           field == SIM_SRC_EXEC_LO || field == SIM_SRC_EXEC_HI ||
           field == SIM_SRC_LITERAL;
}

/* How many of VOP3's three source fields op reads: v_cndmask_b32 and
 * v_addc_u32 read src2 as their condition pair. */
static v9x_u32 sim_vop3_sources(v9x_u32 op)
{
    if (op == 0x100ul || op == 0x11cul) {
        return 3ul;
    }
    if (op >= 0x140ul && op < 0x1c0ul) {
        return 1ul;
    }
    if (op >= 0x1c0ul && op < 0x200ul) {
        return 3ul;
    }
    return 2ul;
}

static int sim_bus_ok(const v9x_u32 *fields, v9x_u32 count)
{
    v9x_u32 seen = 0xfffffffful;
    v9x_u32 n;

    for (n = 0ul; n < count; ++n) {
        if (!sim_scalar(fields[n]) || fields[n] == seen) {
            continue;
        }
        if (seen != 0xfffffffful) {
            return 0;
        }
        seen = fields[n];
    }
    return 1;
}

static int sim_lane_on(const struct v9x_gcn_sim *sim)
{
    return (sim->exec[0] & 1ul) != 0ul;
}

/* IEEE single to half, rounded toward zero (v_cvt_pkrtz_f16_f32). */
static v9x_u32 sim_half_rtz(float value)
{
    v9x_u32 bits = sim_u(value);
    v9x_u32 sign = (bits >> 16) & 0x8000ul;
    long exponent = (long)((bits >> 23) & 0xfful) - 127L + 15L;
    v9x_u32 mantissa = bits & 0x7ffffful;

    if (((bits >> 23) & 0xfful) == 0xfful) {
        return sign | 0x7c00ul | (mantissa != 0ul ? 0x200ul : 0ul);
    }
    if (exponent >= 31L) {
        return sign | 0x7bfful;     /* toward zero: the largest finite */
    }
    if (exponent <= 0L) {
        if (exponent < -10L) {
            return sign;
        }
        mantissa |= 0x800000ul;
        return sign | (mantissa >> (unsigned)(14L - exponent));
    }
    return sign | ((v9x_u32)exponent << 10) | (mantissa >> 13);
}

static float sim_from_half(v9x_u32 half)
{
    v9x_u32 sign = (half & 0x8000ul) << 16;
    v9x_u32 exponent = (half >> 10) & 0x1ful;
    v9x_u32 mantissa = half & 0x3fful;

    if (exponent == 0ul) {
        float value = (float)mantissa / 16777216.0f;   /* 2^-24 */

        return sign != 0ul ? -value : value;
    }
    if (exponent == 31ul) {
        return sim_f(sign | 0x7f800000ul | (mantissa << 13));
    }
    return sim_f(sign | ((exponent + 112ul) << 23) | (mantissa << 13));
}

static float sim_min(float a, float b)
{
    return a < b ? a : b;
}

static float sim_max(float a, float b)
{
    return a > b ? a : b;
}

/* v_cmp_*_f32's sixteen predicates, op & 0Fh. */
static int sim_compare(v9x_u32 which, float a, float b)
{
    int unordered = (a != a) || (b != b);

    switch (which) {
    case 0x0ul: return 0;
    case 0x1ul: return a < b;
    case 0x2ul: return a == b;
    case 0x3ul: return a <= b;
    case 0x4ul: return a > b;
    case 0x5ul: return a < b || a > b;
    case 0x6ul: return a >= b;
    case 0x7ul: return !unordered;
    case 0x8ul: return unordered;
    case 0x9ul: return !(a >= b);
    case 0xaul: return !(a < b || a > b);
    case 0xbul: return !(a > b);
    case 0xcul: return !(a <= b);
    case 0xdul: return !(a == b);
    case 0xeul: return !(a < b);
    default:    return 1;
    }
}

/*
 * The cube instructions, as radeonsi's build_cube_select (ac_llvm_build.c)
 * restates them: the major axis is z where |z| >= |x| and |y|, else y
 * where |y| >= |x|, else x; the face id 4/5, 2/3 or 0/1 (odd for a
 * negative axis); sc is x on z faces times the axis's sign, x on y faces,
 * z on x faces times minus the sign; tc is -y on x and z faces, z times
 * the sign on y faces; ma is twice the major coordinate.
 */
static void sim_cube(float x, float y, float z, float *id, float *sc,
                     float *tc, float *ma)
{
    float ax = (float)fabs(x);
    float ay = (float)fabs(y);
    float az = (float)fabs(z);

    if (az >= ax && az >= ay) {
        *id = z < 0.0f ? 5.0f : 4.0f;
        *sc = z < 0.0f ? -x : x;
        *tc = -y;
        *ma = 2.0f * z;
    } else if (ay >= ax) {
        *id = y < 0.0f ? 3.0f : 2.0f;
        *sc = x;
        *tc = y < 0.0f ? -z : z;
        *ma = 2.0f * y;
    } else {
        *id = x < 0.0f ? 1.0f : 0.0f;
        *sc = x < 0.0f ? z : -z;
        *tc = -y;
        *ma = 2.0f * x;
    }
}

/* abs then neg on a float source, VOP3's order. */
static float sim_modify(float value, v9x_u32 abs, v9x_u32 neg, v9x_u32 n)
{
    if ((abs & (1ul << n)) != 0ul) {
        value = (float)fabs(value);
    }
    if ((neg & (1ul << n)) != 0ul) {
        value = -value;
    }
    return value;
}

/* omod then clamp on a float result. */
static float sim_output(float value, v9x_u32 omod, int clamp)
{
    if (omod == 1ul) {
        value *= 2.0f;
    } else if (omod == 2ul) {
        value *= 4.0f;
    } else if (omod == 3ul) {
        value *= 0.5f;
    }
    if (clamp) {
        value = sim_max(0.0f, sim_min(1.0f, value));
    }
    return value;
}

/*
 * One VALU operation by its VOP3 number (VOPC below 100h, VOP2 from 100h,
 * VOP1 from 140h, VOP3-only above), sources as bits. Float results go
 * through omod and clamp. Returns 0 for an opcode the model does not run.
 * *cmp is set for a compare; *pair is the 64-bit operand for cndmask and
 * addc (their carry/condition).
 */
static int sim_valu(struct v9x_gcn_sim *sim, v9x_u32 op, v9x_u32 vdst,
                    const v9x_u32 *src, v9x_u32 abs, v9x_u32 neg,
                    v9x_u32 omod, int clamp, const v9x_u32 *pair,
                    int vop3, v9x_u32 sdst)
{
    float a = sim_modify(sim_f(src[0]), abs, neg, 0ul);
    float b = sim_modify(sim_f(src[1]), abs, neg, 1ul);
    float c = sim_modify(sim_f(src[2]), abs, neg, 2ul);
    float id;
    float sc;
    float tc;
    float ma;
    float result;
    v9x_u32 bits;
    v9x_u32 carry;

    /* VOPC: v_cmp (40h-4Fh) to VCC or the pair, v_cmpx (50h-5Fh) to EXEC
     * as well, v_cmp_eq_u32 (CAh). */
    if (op >= 0x40ul && op <= 0x5ful) {
        v9x_u32 out[2];

        out[0] = sim_compare(op & 0xful, a, b) && sim_lane_on(sim) ? 1ul : 0ul;
        out[1] = 0ul;
        if (op >= 0x50ul) {
            sim->exec[0] = out[0];
            sim->exec[1] = 0ul;
        }
        return vop3 ? sim_write64(sim, sdst, out) : sim_write64(sim, SIM_SRC_VCC_LO, out);
    }
    if (op == 0xcaul) {
        v9x_u32 out[2];

        out[0] = src[0] == src[1] && sim_lane_on(sim) ? 1ul : 0ul;
        out[1] = 0ul;
        return vop3 ? sim_write64(sim, sdst, out) : sim_write64(sim, SIM_SRC_VCC_LO, out);
    }

    if (!sim_lane_on(sim)) {
        return 1;
    }
    switch (op) {
    case 0x100ul:   /* v_cndmask_b32: the pair's lane picks src1 */
        bits = (pair[0] & 1ul) != 0ul ? src[1] : src[0];
        if ((abs | neg) != 0ul) {
            bits = sim_u(sim_modify(sim_f(bits), abs, neg,
                                    (pair[0] & 1ul) != 0ul ? 1ul : 0ul));
        }
        sim->v[vdst] = bits;
        return 1;
    case 0x101ul: result = a + b; break;    /* v_add_f32 */
    case 0x102ul: result = a - b; break;    /* v_sub_f32 */
    case 0x103ul: result = b - a; break;    /* v_subrev_f32 */
    case 0x105ul: result = a * b; break;    /* v_mul_f32 */
    case 0x10aul: result = sim_min(a, b); break;
    case 0x10bul: result = sim_max(a, b); break;
    case 0x112ul:   /* v_lshlrev_b32: src1 << src0 */
        sim->v[vdst] = src[1] << (src[0] & 31ul);
        return 1;
    case 0x113ul:
        sim->v[vdst] = src[0] & src[1];
        return 1;
    case 0x116ul:   /* v_mac_f32: dst += a b */
        result = a * b + sim_f(sim->v[vdst]);
        break;
    case 0x119ul:   /* v_add_u32, carry out to VCC */
        bits = src[0] + src[1];
        carry = bits < src[0] ? 1ul : 0ul;
        sim->v[vdst] = bits;
        sim->vcc[0] = carry;
        sim->vcc[1] = 0ul;
        return 1;
    case 0x11cul:   /* v_addc_u32, carry in and out VCC */
        bits = src[0] + src[1] + (pair[0] & 1ul);
        carry = (bits < src[0] || (bits == src[0] && (pair[0] & 1ul) != 0ul)) ? 1ul : 0ul;
        sim->v[vdst] = bits;
        sim->vcc[0] = carry;
        sim->vcc[1] = 0ul;
        return 1;
    case 0x141ul:   /* v_mov_b32 */
        sim->v[vdst] = (abs | neg) != 0ul ? sim_u(a) : src[0];
        if (clamp || omod != 0ul) {
            sim->v[vdst] = sim_u(sim_output(a, omod, clamp));
        }
        return 1;
    case 0x151ul: case 0x152ul: case 0x153ul: case 0x154ul:
        /* v_cvt_f32_ubyte0-3 */
        sim->v[vdst] = sim_u((float)(long)((src[0] >> (8u * (unsigned)(op - 0x151ul))) & 0xfful));
        return 1;
    case 0x15bul: result = a - (float)floor(a); break;  /* v_fract_f32 */
    case 0x162ul: result = 1.0f / a; break;             /* v_rcp_f32 */
    case 0x1c1ul: result = a * b + c; break;            /* v_mad_f32 */
    case 0x1c4ul: sim_cube(a, b, c, &id, &sc, &tc, &ma); result = id; break;
    case 0x1c5ul: sim_cube(a, b, c, &id, &sc, &tc, &ma); result = sc; break;
    case 0x1c6ul: sim_cube(a, b, c, &id, &sc, &tc, &ma); result = tc; break;
    case 0x1c7ul: sim_cube(a, b, c, &id, &sc, &tc, &ma); result = ma; break;
    case 0x296ul:   /* v_cvt_pkrtz_f16_f32 */
        sim->v[vdst] = sim_half_rtz(a) | (sim_half_rtz(b) << 16);
        return 1;
    default:
        return 0;
    }
    sim->v[vdst] = sim_u(sim_output(result, omod, clamp));
    return 1;
}

int v9x_gcn_sim_run(struct v9x_gcn_sim *sim, const v9x_u32 *code,
                    v9x_u32 count)
{
    v9x_u32 pc = 0ul;
    v9x_u32 steps = 0ul;

    while (pc < count) {
        v9x_u32 word = code[pc];
        v9x_u32 next = pc + 1ul < count ? code[pc + 1ul] : 0ul;
        int has_next = pc + 1ul < count;
        v9x_u32 src[3];
        v9x_u32 pair[2];
        v9x_u32 op;
        int literal_used = 0;

        if (++steps > 4096ul) {
            return sim_fail(sim, pc, "runaway");
        }
        ++sim->executed;
        pair[0] = sim->vcc[0];
        pair[1] = sim->vcc[1];
        src[2] = 0ul;

        /* VOPC 0111110 and VOP1 0111111, then VOP2 (bit 31 clear). */
        if ((word & 0xfe000000ul) == 0x7c000000ul) {
            op = (word >> 17) & 0xfful;
            if (!sim_read(sim, word & 0x1fful, next, has_next, &src[0], &literal_used)) {
                return sim_fail(sim, pc, "VOPC source");
            }
            src[1] = sim->v[(word >> 9) & 0xfful];
            if (!sim_valu(sim, op, 0ul, src, 0ul, 0ul, 0ul, 0, pair, 0, 0ul)) {
                return sim_fail(sim, pc, "VOPC opcode");
            }
            pc += literal_used ? 2ul : 1ul;
            continue;
        }
        if ((word & 0xfe000000ul) == 0x7e000000ul) {
            op = (word >> 9) & 0xfful;
            if (!sim_read(sim, word & 0x1fful, next, has_next, &src[0], &literal_used)) {
                return sim_fail(sim, pc, "VOP1 source");
            }
            src[1] = 0ul;
            if (!sim_valu(sim, 0x140ul + op, (word >> 17) & 0xfful, src, 0ul, 0ul,
                          0ul, 0, pair, 0, 0ul)) {
                return sim_fail(sim, pc, "VOP1 opcode");
            }
            pc += literal_used ? 2ul : 1ul;
            continue;
        }
        if ((word & 0x80000000ul) == 0ul) {
            op = (word >> 25) & 0x3ful;
            if ((op == 0x00ul || op == 0x1cul) && sim_scalar(word & 0x1fful)) {
                return sim_fail(sim, pc, "constant bus");   /* and VCC */
            }
            if (!sim_read(sim, word & 0x1fful, next, has_next, &src[0], &literal_used)) {
                return sim_fail(sim, pc, "VOP2 source");
            }
            src[1] = sim->v[(word >> 9) & 0xfful];
            if (!sim_valu(sim, 0x100ul + op, (word >> 17) & 0xfful, src, 0ul, 0ul,
                          0ul, 0, pair, 0, 0ul)) {
                return sim_fail(sim, pc, "VOP2 opcode");
            }
            pc += literal_used ? 2ul : 1ul;
            continue;
        }

        /* SOPP 101111111. */
        if ((word & 0xff800000ul) == 0xbf800000ul) {
            op = (word >> 16) & 0x7ful;
            if (op == 0x01ul) {     /* s_endpgm */
                sim->killed = !sim_lane_on(sim);
                return 1;
            }
            if (op == 0x08ul) {     /* s_cbranch_execz */
                v9x_u32 simm = word & 0xfffful;
                long offset = (simm & 0x8000ul) != 0ul ? (long)simm - 65536L : (long)simm;

                if (sim->exec[0] == 0ul && sim->exec[1] == 0ul) {
                    pc = (v9x_u32)((long)pc + 1L + offset);
                    continue;
                }
            } else if (op != 0x00ul && op != 0x0cul) {
                return sim_fail(sim, pc, "SOPP opcode");
            }
            ++pc;
            continue;
        }

        /* SOP1 101111101: s_mov_b32, s_mov_b64, s_wqm_b64. */
        if ((word & 0xff800000ul) == 0xbe800000ul) {
            v9x_u32 sdst = (word >> 16) & 0x7ful;
            v9x_u32 ssrc = word & 0xfful;

            op = (word >> 8) & 0xfful;
            if (op == 0x00ul) {
                if (!sim_read(sim, ssrc, next, has_next, &src[0], &literal_used) ||
                    !sim_write32(sim, sdst, src[0])) {
                    return sim_fail(sim, pc, "s_mov_b32 operand");
                }
                pc += literal_used ? 2ul : 1ul;
                continue;
            }
            if (op == 0x01ul || op == 0x07ul) {
                if (!sim_read64(sim, ssrc, pair)) {
                    return sim_fail(sim, pc, "SOP1 64-bit source");
                }
                if (op == 0x07ul) {
                    v9x_u32 half;
                    v9x_u32 quad;

                    for (half = 0ul; half < 2ul; ++half) {
                        v9x_u32 out = 0ul;

                        for (quad = 0ul; quad < 8ul; ++quad) {
                            if (((pair[half] >> (4u * (unsigned)quad)) & 0xful) != 0ul) {
                                out |= 0xful << (4u * (unsigned)quad);
                            }
                        }
                        pair[half] = out;
                    }
                }
                if (!sim_write64(sim, sdst, pair)) {
                    return sim_fail(sim, pc, "SOP1 64-bit destination");
                }
                ++pc;
                continue;
            }
            return sim_fail(sim, pc, "SOP1 opcode");
        }

        /* VINTRP 110101, one dword: p1 and p2 both leave the value. */
        if ((word & 0xfc000000ul) == 0xd4000000ul) {
            v9x_u32 attr = (word >> 10) & 0x3ful;

            op = (word >> 16) & 3ul;
            if (op > 1ul || attr >= V9X_GCN_SIM_ATTRS) {
                return sim_fail(sim, pc, "VINTRP form");
            }
            if (sim_lane_on(sim)) {
                sim->v[(word >> 18) & 0xfful] =
                    sim_u(sim->attr[attr][(word >> 8) & 3ul]);
            }
            ++pc;
            continue;
        }

        /* The two-dword formats. */
        if (!has_next) {
            return sim_fail(sim, pc, "cut off");
        }

        switch (word & 0xfc000000ul) {
        case 0xc0000000ul: {    /* SMEM s_load_dword(x2/x4/x8) */
            v9x_u32 dwords;
            v9x_u32 base[2];
            v9x_u32 at;
            v9x_u32 index;
            v9x_u32 sdata = (word >> 6) & 0x7ful;

            op = (word >> 18) & 0xfful;
            if (op > 3ul || (word & 0x20000ul) == 0ul) {
                return sim_fail(sim, pc, "SMEM form");
            }
            dwords = 1ul << (unsigned)op;
            base[0] = sim->s[(word & 0x3ful) * 2ul];
            base[1] = sim->s[(word & 0x3ful) * 2ul + 1ul];
            if (sim->memory == 0 || base[1] != sim->memory_high ||
                base[0] < sim->memory_low || (next & 3ul) != 0ul) {
                return sim_fail(sim, pc, "SMEM address");
            }
            at = (base[0] - sim->memory_low + next) / 4ul;
            if (at + dwords > sim->memory_dwords ||
                sdata + dwords > V9X_GCN_SIM_SGPRS) {
                return sim_fail(sim, pc, "SMEM range");
            }
            for (index = 0ul; index < dwords; ++index) {
                sim->s[sdata + index] = sim->memory[at + index];
            }
            pc += 2ul;
            continue;
        }
        case 0xc4000000ul: {    /* EXP */
            v9x_u32 target = (word >> 4) & 0x3ful;

            if ((word & 0x800ul) != 0ul) {
                sim->done = 1;
            }
            if (target == 0ul && sim_lane_on(sim)) {
                if ((word & 0x400ul) != 0ul) {
                    v9x_u32 lo = sim->v[next & 0xfful];
                    v9x_u32 hi = sim->v[(next >> 8) & 0xfful];

                    sim->mrt0[0] = sim_from_half(lo & 0xfffful);
                    sim->mrt0[1] = sim_from_half(lo >> 16);
                    sim->mrt0[2] = sim_from_half(hi & 0xfffful);
                    sim->mrt0[3] = sim_from_half(hi >> 16);
                } else {
                    v9x_u32 chan;

                    for (chan = 0ul; chan < 4ul; ++chan) {
                        sim->mrt0[chan] =
                            sim_f(sim->v[(next >> (8u * (unsigned)chan)) & 0xfful]);
                    }
                }
                sim->exported = 1;
            } else if (target != 0ul && target != 9ul) {
                return sim_fail(sim, pc, "EXP target");
            }
            pc += 2ul;
            continue;
        }
        case 0xd0000000ul: {    /* VOP3 */
            v9x_u32 n;
            v9x_u32 fields[3];

            op = (word >> 16) & 0x3fful;
            for (n = 0ul; n < 3ul; ++n) {
                fields[n] = (next >> (9u * (unsigned)n)) & 0x1fful;
            }
            if (!sim_bus_ok(fields, sim_vop3_sources(op))) {
                return sim_fail(sim, pc, "constant bus");
            }
            for (n = 0ul; n < 3ul; ++n) {
                v9x_u32 field = (next >> (9u * (unsigned)n)) & 0x1fful;

                if (field == SIM_SRC_LITERAL ||
                    !sim_read(sim, field, 0ul, 0, &src[n], 0)) {
                    return sim_fail(sim, pc, "VOP3 source");
                }
            }
            if (op == 0x100ul || op == 0x11cul) {
                if (!sim_read64(sim, (next >> 18) & 0x1fful, pair)) {
                    return sim_fail(sim, pc, "VOP3 condition");
                }
            }
            if (!sim_valu(sim, op, word & 0xfful, src, (word >> 8) & 7ul,
                          (next >> 29) & 7ul, (next >> 27) & 3ul,
                          (word & 0x8000ul) != 0ul, pair, 1, word & 0xfful)) {
                return sim_fail(sim, pc, "VOP3 opcode");
            }
            pc += 2ul;
            continue;
        }
        case 0xf0000000ul: {    /* MIMG image_sample */
            v9x_u32 dmask = (word >> 8) & 0xful;
            v9x_u32 vaddr = next & 0xfful;
            v9x_u32 vdata = (next >> 8) & 0xfful;
            v9x_u32 srsrc = ((next >> 16) & 0x1ful) * 4ul;
            v9x_u32 ssamp = ((next >> 21) & 0x1ful) * 4ul;
            float address[4];
            float rgba[4];
            v9x_u32 chan;
            v9x_u32 out = vdata;

            if (((word >> 18) & 0x7ful) != 0x20ul || sim->sample == 0) {
                return sim_fail(sim, pc, "MIMG opcode");
            }
            if (sim_lane_on(sim)) {
                for (chan = 0ul; chan < 4ul; ++chan) {
                    address[chan] = sim_f(sim->v[vaddr + chan]);
                }
                sim->mimg_word = word;
                sim->sample(&sim->s[srsrc], &sim->s[ssamp], address, dmask,
                            rgba);
                for (chan = 0ul; chan < 4ul; ++chan) {
                    if ((dmask & (1ul << chan)) != 0ul) {
                        sim->v[out++] = sim_u(rgba[chan]);
                    }
                }
            }
            pc += 2ul;
            continue;
        }
        default:
            break;
        }
        return sim_fail(sim, pc, "format");
    }
    return sim_fail(sim, pc, "ran off the end");
}
