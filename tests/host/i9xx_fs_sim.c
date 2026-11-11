/*
 * The host model of the Gen3 fragment program. See i9xx_fs_sim.h for
 * what it models and what it refuses.
 *
 * Field positions are numbers here, read from Mesa gallium's
 * drivers\i915\i915_reg.h, not the encoder's names: a disagreement
 * between the two is what the tests are for.
 */
#include <math.h>

#include "i9xx_fs_sim.h"

#define SIM_HEADER        0x7d050000ul
#define SIM_HEADER_MASK   0xffff0000ul
#define SIM_LENGTH_MASK   0x000001fful

#define SIM_OP_ADD     0x01ul
#define SIM_OP_MOV     0x02ul
#define SIM_OP_MUL     0x03ul
#define SIM_OP_MAD     0x04ul
#define SIM_OP_DP2ADD  0x05ul
#define SIM_OP_DP3     0x06ul
#define SIM_OP_DP4     0x07ul
#define SIM_OP_FRC     0x08ul
#define SIM_OP_RCP     0x09ul
#define SIM_OP_RSQ     0x0aul
#define SIM_OP_EXP     0x0bul
#define SIM_OP_LOG     0x0cul
#define SIM_OP_CMP     0x0dul
#define SIM_OP_MIN     0x0eul
#define SIM_OP_MAX     0x0ful
#define SIM_OP_FLR     0x10ul
#define SIM_OP_SGE     0x13ul
#define SIM_OP_SLT     0x14ul
#define SIM_OP_TEXLD   0x15ul
#define SIM_OP_TEXLDP  0x16ul
#define SIM_OP_TEXLDB  0x17ul
#define SIM_OP_TEXKILL 0x18ul
#define SIM_OP_DCL     0x19ul

#define SIM_R      0ul
#define SIM_T      1ul
#define SIM_CONST  2ul
#define SIM_S      3ul
#define SIM_OC     4ul
#define SIM_OD     5ul
#define SIM_U      6ul

#define SIM_SEL_ZERO 4ul
#define SIM_SEL_ONE  5ul

void v9x_i9xx_fs_sim_reset(struct v9x_i9xx_fs_sim *sim)
{
    unsigned int reg;
    unsigned int channel;

    for (reg = 0u; reg < 16u; ++reg) {
        for (channel = 0u; channel < 4u; ++channel) {
            sim->r[reg][channel] = 0.0f;
        }
        sim->s_type[reg] = 0ul;
    }
    for (reg = 0u; reg < 4u; ++reg) {
        for (channel = 0u; channel < 4u; ++channel) {
            sim->u[reg][channel] = 0.0f;
        }
    }
    for (channel = 0u; channel < 4u; ++channel) {
        sim->oc[channel] = 0.0f;
        sim->od[channel] = 0.0f;
    }
    sim->r_written = 0ul;
    sim->u_written = 0ul;
    sim->oc_written = 0ul;
    sim->t_declared = 0ul;
    sim->s_declared = 0ul;
    sim->executed = 0ul;
    sim->samples = 0ul;
    sim->killed = 0;
    sim->error = 0;
    sim->error_at = 0ul;
}

/* The register `type`, `nr` as a source, or null (with an error) when it
 * cannot be read. */
static const float *sim_source_reg(struct v9x_i9xx_fs_sim *sim,
                                   v9x_u32 type, v9x_u32 nr)
{
    if (type == SIM_R && nr < 16ul &&
        (sim->r_written & (1ul << nr)) != 0ul) {
        return sim->r[nr];
    }
    if (type == SIM_U && nr < 4ul &&
        (sim->u_written & (1ul << nr)) != 0ul) {
        return sim->u[nr];
    }
    if (type == SIM_T && nr < 11ul &&
        (sim->t_declared & (1ul << nr)) != 0ul) {
        return sim->t[nr];
    }
    if (type == SIM_CONST && nr < 32ul) {
        return sim->c[nr];
    }
    return 0;
}

static float *sim_dest_reg(struct v9x_i9xx_fs_sim *sim, v9x_u32 type,
                           v9x_u32 nr)
{
    if (type == SIM_R && nr < 16ul) {
        sim->r_written |= 1ul << nr;
        return sim->r[nr];
    }
    if (type == SIM_U && nr < 4ul) {
        sim->u_written |= 1ul << nr;
        return sim->u[nr];
    }
    if (type == SIM_OC && nr == 0ul) {
        return sim->oc;
    }
    if (type == SIM_OD && nr == 0ul) {
        return sim->od;
    }
    return 0;
}

/* One swizzled, negated channel: selector in the low three bits, negate
 * in bit 3. */
static int sim_channel(const float *reg, v9x_u32 nibble, float *out)
{
    v9x_u32 sel = nibble & 7ul;
    float value;

    if (sel <= 3ul) {
        value = reg[sel];
    } else if (sel == SIM_SEL_ZERO) {
        value = 0.0f;
    } else if (sel == SIM_SEL_ONE) {
        value = 1.0f;
    } else {
        return 0;
    }
    *out = (nibble & 8ul) != 0ul ? -value : value;
    return 1;
}

/*
 * Read source `which` (0-2) of an arithmetic instruction a0..a2 into
 * value[4]. Each channel's selector and negate are gathered into a
 * nibble from where Mesa puts them.
 */
static int sim_source(struct v9x_i9xx_fs_sim *sim, const v9x_u32 *a,
                      unsigned int which, float value[4],
                      v9x_u32 *type_out, v9x_u32 *nr_out,
                      v9x_u32 nibbles[4])
{
    v9x_u32 type;
    v9x_u32 nr;
    const float *reg;
    unsigned int channel;

    if (which == 0u) {
        type = (a[0] >> 7) & 7ul;
        nr = (a[0] >> 2) & 0x1ful;
        nibbles[0] = ((a[1] >> 28) & 7ul) | ((a[1] >> 31) & 1ul) << 3;
        nibbles[1] = ((a[1] >> 24) & 7ul) | ((a[1] >> 27) & 1ul) << 3;
        nibbles[2] = ((a[1] >> 20) & 7ul) | ((a[1] >> 23) & 1ul) << 3;
        nibbles[3] = ((a[1] >> 16) & 7ul) | ((a[1] >> 19) & 1ul) << 3;
    } else if (which == 1u) {
        type = (a[1] >> 13) & 7ul;
        nr = (a[1] >> 8) & 0x1ful;
        nibbles[0] = ((a[1] >> 4) & 7ul) | ((a[1] >> 7) & 1ul) << 3;
        nibbles[1] = ((a[1] >> 0) & 7ul) | ((a[1] >> 3) & 1ul) << 3;
        nibbles[2] = ((a[2] >> 28) & 7ul) | ((a[2] >> 31) & 1ul) << 3;
        nibbles[3] = ((a[2] >> 24) & 7ul) | ((a[2] >> 27) & 1ul) << 3;
    } else {
        type = (a[2] >> 21) & 7ul;
        nr = (a[2] >> 16) & 0x1ful;
        nibbles[0] = ((a[2] >> 12) & 7ul) | ((a[2] >> 15) & 1ul) << 3;
        nibbles[1] = ((a[2] >> 8) & 7ul) | ((a[2] >> 11) & 1ul) << 3;
        nibbles[2] = ((a[2] >> 4) & 7ul) | ((a[2] >> 7) & 1ul) << 3;
        nibbles[3] = ((a[2] >> 0) & 7ul) | ((a[2] >> 3) & 1ul) << 3;
    }

    reg = sim_source_reg(sim, type, nr);
    if (reg == 0) {
        return 0;
    }
    for (channel = 0u; channel < 4u; ++channel) {
        if (!sim_channel(reg, nibbles[channel], &value[channel])) {
            return 0;
        }
    }
    *type_out = type;
    *nr_out = nr;
    return 1;
}

static int sim_replicated(const v9x_u32 nibbles[4])
{
    return nibbles[1] == nibbles[0] && nibbles[2] == nibbles[0] &&
           nibbles[3] == nibbles[0];
}

static unsigned int sim_operands(v9x_u32 op)
{
    if (op == SIM_OP_MAD || op == SIM_OP_DP2ADD || op == SIM_OP_CMP) {
        return 3u;
    }
    if (op == SIM_OP_ADD || op == SIM_OP_MUL || op == SIM_OP_DP3 ||
        op == SIM_OP_DP4 || op == SIM_OP_MIN || op == SIM_OP_MAX ||
        op == SIM_OP_SGE || op == SIM_OP_SLT) {
        return 2u;
    }
    if (op == SIM_OP_MOV || op == SIM_OP_FRC || op == SIM_OP_RCP ||
        op == SIM_OP_RSQ || op == SIM_OP_EXP || op == SIM_OP_LOG ||
        op == SIM_OP_FLR) {
        return 1u;
    }
    return 0u;
}

static int sim_alu(struct v9x_i9xx_fs_sim *sim, const v9x_u32 *a)
{
    v9x_u32 op = (a[0] >> 24) & 0x1ful;
    unsigned int operands = sim_operands(op);
    float src[3][4];
    float result[4];
    v9x_u32 nibbles[3][4];
    v9x_u32 type;
    v9x_u32 nr;
    v9x_u32 const_nr = 0xfffffffful;
    v9x_u32 mask;
    float *dest;
    unsigned int index;
    unsigned int channel;
    float scalar;

    if (operands == 0u) {
        return 0;
    }
    for (index = 0u; index < operands; ++index) {
        if (!sim_source(sim, a, index, src[index], &type, &nr,
                        nibbles[index])) {
            return 0;
        }
        if (type == SIM_CONST) {
            if (const_nr != 0xfffffffful && const_nr != nr) {
                return 0;
            }
            const_nr = nr;
        }
    }

    for (channel = 0u; channel < 4u; ++channel) {
        float x = src[0][channel];
        float y = operands > 1u ? src[1][channel] : 0.0f;
        float z = operands > 2u ? src[2][channel] : 0.0f;

        switch (op) {
        case SIM_OP_ADD: result[channel] = x + y; break;
        case SIM_OP_MOV: result[channel] = x; break;
        case SIM_OP_MUL: result[channel] = x * y; break;
        case SIM_OP_MAD: result[channel] = x * y + z; break;
        case SIM_OP_CMP: result[channel] = x >= 0.0f ? y : z; break;
        case SIM_OP_MIN: result[channel] = x < y ? x : y; break;
        case SIM_OP_MAX: result[channel] = x > y ? x : y; break;
        case SIM_OP_SGE: result[channel] = x >= y ? 1.0f : 0.0f; break;
        case SIM_OP_SLT: result[channel] = x < y ? 1.0f : 0.0f; break;
        case SIM_OP_FLR: result[channel] = (float)floor((double)x); break;
        case SIM_OP_FRC:
            result[channel] = x - (float)floor((double)x);
            break;
        default: result[channel] = 0.0f; break;
        }
    }

    if (op == SIM_OP_DP3 || op == SIM_OP_DP4 || op == SIM_OP_DP2ADD) {
        unsigned int terms = op == SIM_OP_DP4 ? 4u :
                             (op == SIM_OP_DP3 ? 3u : 2u);

        scalar = 0.0f;
        for (channel = 0u; channel < terms; ++channel) {
            scalar += src[0][channel] * src[1][channel];
        }
        if (op == SIM_OP_DP2ADD) {
            if (!sim_replicated(nibbles[2])) {
                return 0;
            }
            scalar += src[2][0];
        }
        for (channel = 0u; channel < 4u; ++channel) {
            result[channel] = scalar;
        }
    }

    if (op == SIM_OP_RCP || op == SIM_OP_RSQ || op == SIM_OP_EXP ||
        op == SIM_OP_LOG) {
        if (!sim_replicated(nibbles[0])) {
            return 0;
        }
        scalar = src[0][0];
        if (op == SIM_OP_RCP) {
            scalar = 1.0f / scalar;
        } else if (op == SIM_OP_RSQ) {
            scalar = (float)(1.0 / sqrt((double)scalar));
        } else if (op == SIM_OP_EXP) {
            scalar = (float)pow(2.0, (double)scalar);
        } else {
            scalar = (float)(log((double)scalar) / log(2.0));
        }
        for (channel = 0u; channel < 4u; ++channel) {
            result[channel] = scalar;
        }
    }

    dest = sim_dest_reg(sim, (a[0] >> 19) & 7ul, (a[0] >> 14) & 0xful);
    if (dest == 0) {
        return 0;
    }
    mask = (a[0] >> 10) & 0xful;
    for (channel = 0u; channel < 4u; ++channel) {
        float value = result[channel];

        if ((mask & (1ul << channel)) == 0ul) {
            continue;
        }
        if ((a[0] & (1ul << 22)) != 0ul) {
            value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        }
        dest[channel] = value;
    }
    if (dest == sim->oc) {
        sim->oc_written |= mask;
    }
    return 1;
}

static int sim_tex(struct v9x_i9xx_fs_sim *sim, const v9x_u32 *t)
{
    v9x_u32 op = (t[0] >> 24) & 0x1ful;
    v9x_u32 sampler = t[0] & 0xful;
    const float *addr;
    float coord[4];
    float texel[4];
    float *dest;
    unsigned int channel;

    if (t[2] != 0ul) {
        return 0;
    }
    addr = sim_source_reg(sim, (t[1] >> 24) & 7ul, (t[1] >> 17) & 0xful);
    if (addr == 0) {
        return 0;
    }
    for (channel = 0u; channel < 4u; ++channel) {
        coord[channel] = addr[channel];
    }

    if (op == SIM_OP_TEXKILL) {
        for (channel = 0u; channel < 4u; ++channel) {
            if (coord[channel] < 0.0f) {
                sim->killed = 1;
            }
        }
        return 1;
    }

    if ((sim->s_declared & (1ul << sampler)) == 0ul || sim->sample == 0) {
        return 0;
    }
    if (op == SIM_OP_TEXLDP) {
        for (channel = 0u; channel < 3u; ++channel) {
            coord[channel] = coord[channel] / coord[3];
        }
    }
    sim->sample(sim->sample_user, sampler, sim->s_type[sampler], op,
                coord, texel);
    ++sim->samples;

    dest = sim_dest_reg(sim, (t[0] >> 19) & 7ul, (t[0] >> 14) & 0xful);
    if (dest == 0) {
        return 0;
    }
    for (channel = 0u; channel < 4u; ++channel) {
        dest[channel] = texel[channel];
    }
    if (dest == sim->oc) {
        sim->oc_written |= 0xful;
    }
    return 1;
}

static int sim_dcl(struct v9x_i9xx_fs_sim *sim, const v9x_u32 *d)
{
    v9x_u32 type = (d[0] >> 19) & 7ul;
    v9x_u32 nr = (d[0] >> 14) & 0x1ful;

    if (d[1] != 0ul || d[2] != 0ul) {
        return 0;
    }
    if (type == SIM_T && nr < 11ul) {
        sim->t_declared |= 1ul << nr;
        return 1;
    }
    if (type == SIM_S && nr < 16ul) {
        sim->s_declared |= 1ul << nr;
        sim->s_type[nr] = (d[0] >> 22) & 3ul;
        return 1;
    }
    return 0;
}

int v9x_i9xx_fs_sim_run(struct v9x_i9xx_fs_sim *sim, const v9x_u32 *words,
                        v9x_u32 dwords)
{
    v9x_u32 at;

    if (dwords < 4ul || (dwords - 1ul) % 3ul != 0ul ||
        (words[0] & SIM_HEADER_MASK) != SIM_HEADER ||
        (words[0] & SIM_LENGTH_MASK) != dwords - 2ul) {
        sim->error = 1;
        sim->error_at = 0ul;
        return 0;
    }

    for (at = 1ul; at < dwords; at += 3ul) {
        v9x_u32 op = (words[at] >> 24) & 0x1ful;
        int ok;

        if (op == SIM_OP_DCL) {
            ok = sim_dcl(sim, &words[at]);
        } else if (op >= SIM_OP_TEXLD && op <= SIM_OP_TEXKILL) {
            ok = sim_tex(sim, &words[at]);
        } else {
            ok = sim_alu(sim, &words[at]);
        }
        if (!ok) {
            sim->error = 1;
            sim->error_at = at;
            return 0;
        }
        ++sim->executed;
    }
    return 1;
}
