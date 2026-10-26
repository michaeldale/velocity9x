/*
 * The host EU model (hsw_eu_sim.h).
 */
#include <math.h>
#include <string.h>

#include "hsw_eu_sim.h"
#include "velocity9x/intel_hsw_eu.h"

float v9x_hsw_sim_float(v9x_u32 bits)
{
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

v9x_u32 v9x_hsw_sim_bits(float value)
{
    v9x_u32 bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/* The element a register region names, as a flat dword index from g0. */
static v9x_u32 *v9x_hsw_sim_element(struct v9x_hsw_sim *sim, v9x_u32 nr,
                                    v9x_u32 element)
{
    v9x_u32 flat = nr * 8u + element;

    if (flat >= V9X_HSW_SIM_GRFS * 8u) {
        sim->error = 1;
        return &sim->grf[0][0];
    }
    return &sim->grf[flat / 8u][flat % 8u];
}

static v9x_u32 v9x_hsw_sim_vstride(v9x_u8 code)
{
    return code == 0u ? 0u : (1u << (code - 1u));
}

static v9x_u32 v9x_hsw_sim_width(v9x_u8 code)
{
    return 1u << code;
}

static v9x_u32 v9x_hsw_sim_hstride(v9x_u8 code)
{
    return code == 0u ? 0u : (1u << (code - 1u));
}

/* A source's raw bits for channel c. */
static v9x_u32 v9x_hsw_sim_source(struct v9x_hsw_sim *sim,
                                  const struct v9x_hsw_eu_inst *inst,
                                  const struct v9x_hsw_eu_operand *op,
                                  v9x_u32 c)
{
    v9x_u32 element;

    if (op->file == V9X_HSW_EU_FILE_IMM) {
        return op->imm;
    }
    if (op->file != V9X_HSW_EU_FILE_GRF) {
        return 0u;  /* null */
    }
    if (inst->align16) {
        v9x_u32 vertex = c >> 2;
        v9x_u32 component = c & 3u;
        v9x_u32 swizzle = (op->chan >> (2u * component)) & 3u;

        element = op->subnr / 4u + vertex * v9x_hsw_sim_vstride(op->vstride) +
                  swizzle;
    } else {
        v9x_u32 width = v9x_hsw_sim_width(op->width);

        element = op->subnr / 4u +
                  (c / width) * v9x_hsw_sim_vstride(op->vstride) +
                  (c % width) * v9x_hsw_sim_hstride(op->hstride);
    }
    return *v9x_hsw_sim_element(sim, op->nr, element);
}

static float v9x_hsw_sim_fsource(struct v9x_hsw_sim *sim,
                                 const struct v9x_hsw_eu_inst *inst,
                                 const struct v9x_hsw_eu_operand *op,
                                 v9x_u32 c)
{
    float value = v9x_hsw_sim_float(v9x_hsw_sim_source(sim, inst, op, c));

    if (op->file == V9X_HSW_EU_FILE_IMM) {
        return value;
    }
    if ((op->mod & V9X_HSW_EU_MOD_ABS) != 0u) {
        value = (float)fabs((double)value);
    }
    if ((op->mod & V9X_HSW_EU_MOD_NEG) != 0u) {
        value = -value;
    }
    return value;
}

static int v9x_hsw_sim_compare(v9x_u8 cond, float a, float b)
{
    switch (cond) {
    case V9X_HSW_EU_COND_Z:
        return a == b;
    case V9X_HSW_EU_COND_NZ:
        return a != b;
    case V9X_HSW_EU_COND_G:
        return a > b;
    case V9X_HSW_EU_COND_GE:
        return a >= b;
    case V9X_HSW_EU_COND_L:
        return a < b;
    case V9X_HSW_EU_COND_LE:
        return a <= b;
    default:
        return 0;
    }
}

static float v9x_hsw_sim_saturate(float value)
{
    if (!(value > 0.0f)) {
        return 0.0f;  /* NaN too */
    }
    return value > 1.0f ? 1.0f : value;
}

static float v9x_hsw_sim_math(struct v9x_hsw_sim *sim, v9x_u8 function,
                              float a, float b)
{
    double x = (double)a;

    switch (function) {
    case V9X_HSW_EU_MATH_INV:
        return (float)(1.0 / x);
    case V9X_HSW_EU_MATH_RSQ:
        return (float)(1.0 / sqrt(x));
    case V9X_HSW_EU_MATH_SQRT:
        return (float)sqrt(x);
    case V9X_HSW_EU_MATH_EXP:
        return (float)exp(x * 0.69314718055994530942);
    case V9X_HSW_EU_MATH_LOG:
        return (float)(log(x) / 0.69314718055994530942);
    case V9X_HSW_EU_MATH_POW:
        return (float)exp((double)b * log(x));
    default:
        sim->error = 1;
        return 0.0f;
    }
}

/* The destination dword for channel c, or null when the channel is not
 * written (Align16 writemask). */
static v9x_u32 *v9x_hsw_sim_dest(struct v9x_hsw_sim *sim,
                                 const struct v9x_hsw_eu_inst *inst,
                                 v9x_u32 c)
{
    if (inst->dst.file != V9X_HSW_EU_FILE_GRF) {
        return 0;
    }
    if (inst->align16) {
        if (((inst->dst.chan >> (c & 3u)) & 1u) == 0u) {
            return 0;
        }
        return v9x_hsw_sim_element(sim, inst->dst.nr,
                                   inst->dst.subnr / 4u + (c >> 2) * 4u +
                                       (c & 3u));
    }
    return v9x_hsw_sim_element(sim, inst->dst.nr,
                               inst->dst.subnr / 4u +
                                   c * v9x_hsw_sim_hstride(inst->dst.hstride));
}

static int v9x_hsw_sim_flag(const struct v9x_hsw_sim *sim,
                            const struct v9x_hsw_eu_inst *inst, v9x_u32 c)
{
    v9x_u32 which = (v9x_u32)inst->flag_reg * 2u + inst->flag_subreg;

    return (int)((sim->flag[which] >> c) & 1u);
}

static void v9x_hsw_sim_set_flag(struct v9x_hsw_sim *sim,
                                 const struct v9x_hsw_eu_inst *inst,
                                 v9x_u32 c, int value)
{
    v9x_u32 which = (v9x_u32)inst->flag_reg * 2u + inst->flag_subreg;

    if (value) {
        sim->flag[which] |= 1u << c;
    } else {
        sim->flag[which] &= ~(1u << c);
    }
}

static void v9x_hsw_sim_send(struct v9x_hsw_sim *sim,
                             const struct v9x_hsw_eu_inst *inst)
{
    v9x_u32 mlen = (inst->desc >> 25) & 0xfu;
    v9x_u32 index;

    if (inst->sfid != V9X_HSW_SFID_URB || mlen == 0u || mlen > 16u ||
        inst->src0.nr + mlen > V9X_HSW_SIM_GRFS) {
        sim->error = 1;
        return;
    }
    for (index = 0u; index < mlen; ++index) {
        memcpy(sim->message[index], sim->grf[inst->src0.nr + index],
               sizeof(sim->message[index]));
    }
    sim->message_length = mlen;
    if (inst->eot) {
        sim->ended = 1;
    }
}

/* One instruction. */
static void v9x_hsw_sim_step(struct v9x_hsw_sim *sim,
                             const struct v9x_hsw_eu_inst *inst)
{
    v9x_u32 channels = 1u << inst->exec_size;
    v9x_u32 results[16];
    int writes[16];
    v9x_u32 c;
    int is_float = inst->dst.type == V9X_HSW_EU_TYPE_F ||
                   (inst->dst.file == V9X_HSW_EU_FILE_ARF &&
                    inst->src0.type == V9X_HSW_EU_TYPE_F);

    if (inst->opcode == V9X_HSW_EU_OP_SEND) {
        v9x_hsw_sim_send(sim, inst);
        return;
    }
    if (channels > 16u) {
        sim->error = 1;
        return;
    }

    /* Read every source before any write, as the EU does. */
    for (c = 0u; c < channels; ++c) {
        float a;
        float b;
        float r = 0.0f;
        v9x_u32 bits = 0u;
        int keep = 1;

        writes[c] = 1;
        if (inst->pred_ctrl != 0u && inst->opcode != V9X_HSW_EU_OP_SEL) {
            writes[c] = v9x_hsw_sim_flag(sim, inst, c) ^ inst->pred_inv;
        }
        switch (inst->opcode) {
        case V9X_HSW_EU_OP_MOV:
            if (is_float) {
                r = v9x_hsw_sim_fsource(sim, inst, &inst->src0, c);
            } else {
                bits = v9x_hsw_sim_source(sim, inst, &inst->src0, c);
                keep = 0;
            }
            break;
        case V9X_HSW_EU_OP_OR:
            bits = v9x_hsw_sim_source(sim, inst, &inst->src0, c) |
                   v9x_hsw_sim_source(sim, inst, &inst->src1, c);
            keep = 0;
            break;
        case V9X_HSW_EU_OP_ADD:
            r = v9x_hsw_sim_fsource(sim, inst, &inst->src0, c) +
                v9x_hsw_sim_fsource(sim, inst, &inst->src1, c);
            break;
        case V9X_HSW_EU_OP_MUL:
            r = v9x_hsw_sim_fsource(sim, inst, &inst->src0, c) *
                v9x_hsw_sim_fsource(sim, inst, &inst->src1, c);
            break;
        case V9X_HSW_EU_OP_DP3:
        case V9X_HSW_EU_OP_DP4: {
            v9x_u32 base = c & ~3u;
            v9x_u32 terms = inst->opcode == V9X_HSW_EU_OP_DP4 ? 4u : 3u;
            v9x_u32 k;

            if (!inst->align16) {
                sim->error = 1;
                return;
            }
            for (k = 0u; k < terms; ++k) {
                r += v9x_hsw_sim_fsource(sim, inst, &inst->src0, base + k) *
                     v9x_hsw_sim_fsource(sim, inst, &inst->src1, base + k);
            }
            break;
        }
        case V9X_HSW_EU_OP_SEL:
            a = v9x_hsw_sim_fsource(sim, inst, &inst->src0, c);
            b = v9x_hsw_sim_fsource(sim, inst, &inst->src1, c);
            if (inst->pred_ctrl != 0u) {
                r = (v9x_hsw_sim_flag(sim, inst, c) ^ inst->pred_inv) ? a : b;
            } else {
                r = v9x_hsw_sim_compare(inst->cond_mod, a, b) ? a : b;
            }
            break;
        case V9X_HSW_EU_OP_CMP:
            a = v9x_hsw_sim_fsource(sim, inst, &inst->src0, c);
            b = v9x_hsw_sim_fsource(sim, inst, &inst->src1, c);
            v9x_hsw_sim_set_flag(sim, inst, c,
                                 v9x_hsw_sim_compare(inst->cond_mod, a, b));
            writes[c] = 0;
            break;
        case V9X_HSW_EU_OP_MATH:
            a = v9x_hsw_sim_fsource(sim, inst, &inst->src0, c);
            b = inst->cond_mod == V9X_HSW_EU_MATH_POW
                ? v9x_hsw_sim_fsource(sim, inst, &inst->src1, c) : 0.0f;
            r = v9x_hsw_sim_math(sim, inst->cond_mod, a, b);
            break;
        default:
            sim->error = 1;
            return;
        }
        if (keep) {
            if (inst->saturate) {
                r = v9x_hsw_sim_saturate(r);
            }
            bits = v9x_hsw_sim_bits(r);
        }
        results[c] = bits;
    }
    for (c = 0u; c < channels; ++c) {
        v9x_u32 *dst;

        if (!writes[c]) {
            continue;
        }
        dst = v9x_hsw_sim_dest(sim, inst, c);
        if (dst != 0) {
            *dst = results[c];
        }
    }
}

int v9x_hsw_sim_run(struct v9x_hsw_sim *sim, const v9x_u32 *words,
                    v9x_u32 dwords)
{
    struct v9x_hsw_eu_inst inst;
    v9x_u32 at;

    sim->ended = 0;
    sim->error = 0;
    sim->executed = 0u;
    sim->message_length = 0u;
    for (at = 0u; at + 4u <= dwords && !sim->ended && !sim->error;
         at += 4u) {
        if (v9x_intel_hsw_eu_decode(words + at, &inst) != V9X_STATUS_OK) {
            sim->error = 1;
            break;
        }
        v9x_hsw_sim_step(sim, &inst);
        ++sim->executed;
    }
    return sim->ended && !sim->error;
}
