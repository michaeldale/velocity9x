/*
 * Pixel shaders 1.0 and 1.1 as GCN 3 pixel shaders. See
 * velocity9x\vi_ps11.h for what the program expects.
 *
 * The rules are the reference interpreter's (d3d_ps11_ref.c), as the
 * Gen3, Haswell and Tesla translators keep them: every arithmetic result
 * shifted, then saturated or held to [-1, 1]; a co-issued pair reads its
 * sources before either writes; registers read before they are written
 * read zero.
 *
 * Each channel is its own instruction, as on Tesla. Every channel an
 * instruction writes is computed into a result VGPR first and moved to its
 * destination after, so a destination that is also a source and a
 * co-issued pair both read what was there before.
 *
 * Arithmetic is VOP3, whose sources take negation and whose result takes
 * the output modifier and clamp: the shift is OMOD (x2, x4, /2), exact,
 * and the saturate is CLAMP. OMOD is honoured only while f32 denormals are
 * flushed, which FLOAT_MODE C0h (vi_3d.c's RSRC1) does. Holding to
 * [-1, 1] is a v_max and a v_min against the inline -1.0 and 1.0. Every
 * number the program multiplies or offsets by (0, 0.5, 1, 2, 4 and their
 * negatives) is an inline constant; the shader's constants are SGPRs,
 * loaded from the table with s_load.
 *
 * A VALU instruction on GCN 1 to 3 reads at most one SGPR (the constant
 * bus; VCC counts, the same SGPR twice counts once). v9x_vi_ps11_alu
 * moves any second scalar source into a VGPR first, so two constants in
 * one instruction (mad r0, c2, c5, r1) still translate.
 *
 * The whole program runs in whole-quad mode, as the textured PSs in
 * vi_shader.c do, so the samplers' implicit derivatives see whole quads;
 * the pixels' own EXEC comes back before the kills and the export. Kills
 * are emitted last, from the interpolated coordinates the interpreter
 * tests, as the Tesla translator's are: a kill changes no value.
 *
 * VGPRs:
 *
 *   v0:v1       i and j; after the interpolation, the packed export
 *   v2-v9       r0, r1
 *   v10-v25     t0-t3
 *   v26-v33     v0, v1
 *   v34-v49     coordinate sets 0-3, x y z w
 *   v50, v51    the texm3x3 rows' first two dot products
 *   v52-v55     an instruction's result channels
 *   v56-v59     a co-issued second instruction's
 *   v60-v62     a modified source, one a source slot
 *   v63         an intermediate
 *   v64-v67     a sample's address
 *   v68         a second intermediate (N.N, a cube's 1 / |ma|)
 *   v69, v70    scalar sources moved off the constant bus
 *   v71         a cube's tc
 *
 * SGPRs: s0:s1 the table, s2 PRIM_MASK, s4:s5 the pixels' EXEC, then the
 * descriptors and constants the program reads, from s8 in fours (the
 * alignment s_load_dwordx4/x8 and image_sample's T# and S# need).
 */
#include "velocity9x/vi_ps11.h"
#include "velocity9x/vi_shader.h"

#define V9X_VI_PS11_R_R        2ul
#define V9X_VI_PS11_R_T        10ul
#define V9X_VI_PS11_R_V        26ul
#define V9X_VI_PS11_R_TC       34ul
#define V9X_VI_PS11_R_PAD      50ul
#define V9X_VI_PS11_R_RESULT   52ul
#define V9X_VI_PS11_R_RESULT2  56ul
#define V9X_VI_PS11_R_SOURCE   60ul
#define V9X_VI_PS11_R_WORK     63ul
#define V9X_VI_PS11_R_ADDR     64ul
#define V9X_VI_PS11_R_WORK2    68ul
#define V9X_VI_PS11_R_BUS      69ul
#define V9X_VI_PS11_R_CUBE_TC  71ul

#define V9X_VI_PS11_S_TABLE    0ul
#define V9X_VI_PS11_S_PRIM     2ul
#define V9X_VI_PS11_S_EXEC     4ul
#define V9X_VI_PS11_S_FIRST    8ul
/* Loads stop below this, leaving VCC and what the SPI adds room in the
 * 104 a wave may have. */
#define V9X_VI_PS11_S_LIMIT    96ul

/* Inline constants (GCN 3 ISA, "Scalar ALU operands"): 128 + n is the
 * integer n, F0h-F6h 0.5, -0.5, 1.0, -1.0, 2.0, -2.0, 4.0. */
#define V9X_VI_PS11_ZERO       0x80ul
#define V9X_VI_PS11_HALF       0xf0ul
#define V9X_VI_PS11_MINUS_HALF 0xf1ul
#define V9X_VI_PS11_ONE        0xf2ul
#define V9X_VI_PS11_MINUS_ONE  0xf3ul
#define V9X_VI_PS11_TWO        0xf4ul
#define V9X_VI_PS11_MINUS_TWO  0xf5ul

/* An operand: a source field (9 bits) and, in bit 16, its negation. */
#define V9X_VI_PS11_FIELD      0x1fful
#define V9X_VI_PS11_NEGATE     0x10000ul

/* VOP3 opcodes (vi_shader.h): the VOP2 and VOP1 ones promoted. */
#define V9X_VI_PS11_ADD     V9X_VI_VOP3_FROM_VOP2(V9X_VI_VOP2_ADD_F32)
#define V9X_VI_PS11_SUB     V9X_VI_VOP3_FROM_VOP2(V9X_VI_VOP2_SUB_F32)
#define V9X_VI_PS11_MUL     V9X_VI_VOP3_FROM_VOP2(V9X_VI_VOP2_MUL_F32)
#define V9X_VI_PS11_CNDMASK V9X_VI_VOP3_FROM_VOP2(V9X_VI_VOP2_CNDMASK_B32)
#define V9X_VI_PS11_RCP     V9X_VI_VOP3_FROM_VOP1(V9X_VI_VOP1_RCP_F32)

#define V9X_VI_PS11_STAGES     4ul
#define V9X_VI_PS11_DMASK_RGBA 0xful
#define V9X_VI_PS11_CLAMP      1.0f
#define V9X_VI_PS11_NONE       0xfffffffful

typedef struct v9x_vi_ps11_gen {
    v9x_u32 *words;
    v9x_u32 capacity;
    v9x_u32 count;
    v9x_u32 overflow;
    const V9X_PS11_PROGRAM *program;
    const struct v9x_vi_ps11_desc *desc;
    v9x_u32 pads;           /* texm3x3pad rows so far */
    v9x_u32 kill_stages;    /* bit N: texkill on stage N */
    v9x_u32 unsupported;
    /* Where each loaded block starts (an SGPR), V9X_VI_PS11_NONE if it
     * is not loaded. */
    v9x_u32 s_t[4];
    v9x_u32 s_s[4];
    v9x_u32 s_const[8];
    v9x_u32 s_bump[4];
    v9x_u32 s_luminance[4];
    v9x_u32 s_fog;
    v9x_u32 s_next;
} V9X_VI_PS11_GEN;

typedef union v9x_vi_ps11_float {
    float f;
    v9x_u32 bits;
} V9X_VI_PS11_FLOAT;

static v9x_u32 v9x_vi_ps11_bits(float value)
{
    V9X_VI_PS11_FLOAT u;

    u.f = value;
    return u.bits;
}

static float v9x_vi_ps11_value(v9x_u32 bits)
{
    V9X_VI_PS11_FLOAT u;

    u.bits = bits;
    return u.f;
}

static float v9x_vi_ps11_clamp(float value)
{
    if (!(value > -V9X_VI_PS11_CLAMP)) {
        return -V9X_VI_PS11_CLAMP;      /* NaN too, as the interpreter */
    }
    return value > V9X_VI_PS11_CLAMP ? V9X_VI_PS11_CLAMP : value;
}

void v9x_vi_ps11_constants(const V9X_PS11_PROGRAM *program,
                           const V9X_PS11_ENV *env,
                           v9x_u32 table[V9X_VI_PS11_TABLE_DWORDS])
{
    v9x_u32 reg;
    v9x_u32 c;
    float value;

    for (reg = 0ul; reg < 8ul; ++reg) {
        for (c = 0ul; c < 4ul; ++c) {
            if ((program->def_mask & (1ul << reg)) != 0ul) {
                value = v9x_vi_ps11_value(program->def[reg][c]);
            } else {
                value = env->c[reg][c];
            }
            table[V9X_VI_PS11_CONST + reg * 4ul + c] =
                v9x_vi_ps11_bits(v9x_vi_ps11_clamp(value));
        }
    }
    for (reg = 0ul; reg < V9X_VI_PS11_STAGES; ++reg) {
        for (c = 0ul; c < 4ul; ++c) {
            table[V9X_VI_PS11_BUMP + reg * 4ul + c] =
                v9x_vi_ps11_bits(env->bump[reg][c]);
        }
        table[V9X_VI_PS11_LUMINANCE + reg * 2ul] =
            v9x_vi_ps11_bits(env->luminance[reg][0]);
        table[V9X_VI_PS11_LUMINANCE + reg * 2ul + 1ul] =
            v9x_vi_ps11_bits(env->luminance[reg][1]);
    }
}

/* ---- Emission ------------------------------------------------------ */

static void v9x_vi_ps11_put(V9X_VI_PS11_GEN *gen, const v9x_u32 *w,
                            v9x_u32 n)
{
    v9x_u32 i;

    for (i = 0ul; i < n; ++i) {
        if (gen->count < gen->capacity) {
            gen->words[gen->count] = w[i];
        } else {
            gen->overflow = 1ul;
        }
        ++gen->count;
    }
}

static void v9x_vi_ps11_put1(V9X_VI_PS11_GEN *gen, v9x_u32 word)
{
    v9x_vi_ps11_put(gen, &word, 1ul);
}

/* An SGPR, VCC, M0 or EXEC: what the constant bus carries. Inline
 * constants and VGPRs do not count. */
static int v9x_vi_ps11_scalar(v9x_u32 field)
{
    return field < 104ul || field == V9X_VCC || field == V9X_VCC + 1ul ||
           field == V9X_VI_M0 || field == V9X_VI_EXEC ||
           field == V9X_VI_EXEC + 1ul;
}

/*
 * A VOP3 instruction: op, vdst, three operands (an unused one
 * V9X_VI_PS11_ZERO) and mods (V9X_VI_CLAMP, V9X_VI_OMOD_*). Negations
 * come from the operands. A second distinct scalar source is moved into
 * a VGPR first; v_cndmask_b32 reads VCC through src2, so any scalar a or
 * b is moved there.
 */
static void v9x_vi_ps11_alu(V9X_VI_PS11_GEN *gen, v9x_u32 op, v9x_u32 vdst,
                            v9x_u32 a, v9x_u32 b, v9x_u32 c, v9x_u32 mods)
{
    v9x_u32 operand[3];
    v9x_u32 w[2];
    v9x_u32 kept = V9X_VI_PS11_NONE;
    v9x_u32 spill = 0ul;
    v9x_u32 n;

    operand[0] = a;
    operand[1] = b;
    operand[2] = c;
    if (op == V9X_VI_PS11_CNDMASK) {
        kept = V9X_VCC;
    }
    for (n = 0ul; n < 3ul; ++n) {
        v9x_u32 field = operand[n] & V9X_VI_PS11_FIELD;

        if (!v9x_vi_ps11_scalar(field) || field == kept) {
            continue;
        }
        if (kept == V9X_VI_PS11_NONE) {
            kept = field;
            continue;
        }
        v9x_vi_ps11_put1(gen, v9x_vi_vop1(V9X_VI_VOP1_MOV_B32,
                                          V9X_VI_PS11_R_BUS + spill, field));
        operand[n] = V9X_VI_VGPR(V9X_VI_PS11_R_BUS + spill) |
                     (operand[n] & V9X_VI_PS11_NEGATE);
        ++spill;
    }
    for (n = 0ul; n < 3ul; ++n) {
        if ((operand[n] & V9X_VI_PS11_NEGATE) != 0ul) {
            mods |= V9X_VI_NEG(n);
        }
    }
    v9x_vi_vop3(op, vdst, operand[0] & V9X_VI_PS11_FIELD,
                operand[1] & V9X_VI_PS11_FIELD,
                operand[2] & V9X_VI_PS11_FIELD, mods, w);
    v9x_vi_ps11_put(gen, w, 2ul);
}

/* vdst = operand: v_mov_b32, or a multiply by 1.0 to negate. */
static void v9x_vi_ps11_copy(V9X_VI_PS11_GEN *gen, v9x_u32 vdst,
                             v9x_u32 operand)
{
    if ((operand & V9X_VI_PS11_NEGATE) != 0ul) {
        v9x_vi_ps11_alu(gen, V9X_VI_PS11_MUL, vdst, operand,
                        V9X_VI_PS11_ONE, V9X_VI_PS11_ZERO, 0ul);
        return;
    }
    v9x_vi_ps11_put1(gen, v9x_vi_vop1(V9X_VI_VOP1_MOV_B32, vdst, operand));
}

/* vdst = max(low, min(1, vdst)) as two VOP2s, low an inline constant. */
static void v9x_vi_ps11_hold(V9X_VI_PS11_GEN *gen, v9x_u32 vdst,
                             v9x_u32 low)
{
    v9x_vi_ps11_put1(gen, v9x_vi_vop2(V9X_VI_VOP2_MAX_F32, vdst, low, vdst));
    v9x_vi_ps11_put1(gen, v9x_vi_vop2(V9X_VI_VOP2_MIN_F32, vdst,
                                      V9X_VI_PS11_ONE, vdst));
}

/* ---- Sources and arithmetic ---------------------------------------- */

/*
 * Source channel c of src, swizzled and modified, as an operand: the
 * register itself (an SGPR for a constant) when there is nothing to do or
 * only a negation, else scratch VGPR R_SOURCE + slot. The expansions
 * round as the interpreter's expressions do: 2x is exact, so x 2 - 1 in
 * one v_mad_f32 is 2x - 1.
 */
static v9x_u32 v9x_vi_ps11_source(V9X_VI_PS11_GEN *gen,
                                  const V9X_PS11_SRC *src, v9x_u32 c,
                                  v9x_u32 slot)
{
    v9x_u32 lane = ((v9x_u32)src->swizzle >> (2u * c)) & 3ul;
    v9x_u32 scratch = V9X_VI_PS11_R_SOURCE + slot;
    v9x_u32 x;

    switch (src->file) {
    case V9X_PS11_FILE_TEMP:
        x = V9X_VI_VGPR(V9X_VI_PS11_R_R + 4ul * src->reg + lane);
        break;
    case V9X_PS11_FILE_INPUT:
        x = V9X_VI_VGPR(V9X_VI_PS11_R_V + 4ul * src->reg + lane);
        break;
    case V9X_PS11_FILE_CONST:
        x = gen->s_const[src->reg] + lane;
        break;
    default:
        x = V9X_VI_VGPR(V9X_VI_PS11_R_T + 4ul * src->reg + lane);
        break;
    }
    switch (src->mod) {
    case V9X_PS11_SRC_NEG:
        return x | V9X_VI_PS11_NEGATE;
    case V9X_PS11_SRC_BIAS:
        v9x_vi_ps11_alu(gen, V9X_VI_PS11_ADD, scratch, x,
                        V9X_VI_PS11_MINUS_HALF, V9X_VI_PS11_ZERO, 0ul);
        return V9X_VI_VGPR(scratch);
    case V9X_PS11_SRC_BIASNEG:
        v9x_vi_ps11_alu(gen, V9X_VI_PS11_SUB, scratch, V9X_VI_PS11_HALF, x,
                        V9X_VI_PS11_ZERO, 0ul);
        return V9X_VI_VGPR(scratch);
    case V9X_PS11_SRC_SIGN:
        v9x_vi_ps11_alu(gen, V9X_VI_VOP3_MAD_F32, scratch, x,
                        V9X_VI_PS11_TWO, V9X_VI_PS11_MINUS_ONE, 0ul);
        return V9X_VI_VGPR(scratch);
    case V9X_PS11_SRC_SIGNNEG:
        v9x_vi_ps11_alu(gen, V9X_VI_VOP3_MAD_F32, scratch, x,
                        V9X_VI_PS11_MINUS_TWO, V9X_VI_PS11_ONE, 0ul);
        return V9X_VI_VGPR(scratch);
    case V9X_PS11_SRC_COMP:
        v9x_vi_ps11_alu(gen, V9X_VI_PS11_SUB, scratch, V9X_VI_PS11_ONE, x,
                        V9X_VI_PS11_ZERO, 0ul);
        return V9X_VI_VGPR(scratch);
    default:
        return x;
    }
}

/* The interpreter's shift and saturate as the last instruction's OMOD
 * and CLAMP. */
static v9x_u32 v9x_vi_ps11_mods(const V9X_PS11_INST *inst)
{
    v9x_u32 mods = 0ul;

    if (inst->shift == 1l) {
        mods = V9X_VI_OMOD_X2;
    } else if (inst->shift == 2l) {
        mods = V9X_VI_OMOD_X4;
    } else if (inst->shift == -1l) {
        mods = V9X_VI_OMOD_D2;
    }
    if (inst->saturate != 0u) {
        mods |= V9X_VI_CLAMP;
    }
    return mods;
}

/* After the shift and any saturate: an unsaturated result held to
 * [-1, 1]. */
static void v9x_vi_ps11_finish(V9X_VI_PS11_GEN *gen,
                               const V9X_PS11_INST *inst, v9x_u32 reg)
{
    if (inst->saturate == 0u) {
        v9x_vi_ps11_hold(gen, reg, V9X_VI_PS11_MINUS_ONE);
    }
}

/* An arithmetic instruction's written channels into result + c. */
static void v9x_vi_ps11_arithmetic(V9X_VI_PS11_GEN *gen,
                                   const V9X_PS11_INST *inst,
                                   v9x_u32 result)
{
    v9x_u32 work = V9X_VI_VGPR(V9X_VI_PS11_R_WORK);
    v9x_u32 mods = v9x_vi_ps11_mods(inst);
    v9x_u32 c;
    v9x_u32 a;
    v9x_u32 b;
    v9x_u32 d;

    if (inst->op == V9X_PS11_DP3) {
        for (c = 0ul; c < 3ul; ++c) {
            a = v9x_vi_ps11_source(gen, &inst->src[0], c, 0ul);
            b = v9x_vi_ps11_source(gen, &inst->src[1], c, 1ul);
            if (c == 0ul) {
                v9x_vi_ps11_alu(gen, V9X_VI_PS11_MUL, V9X_VI_PS11_R_WORK, a,
                                b, V9X_VI_PS11_ZERO, 0ul);
            } else {
                v9x_vi_ps11_alu(gen, V9X_VI_VOP3_MAD_F32, V9X_VI_PS11_R_WORK,
                                a, b, work, c == 2ul ? mods : 0ul);
            }
        }
        v9x_vi_ps11_finish(gen, inst, V9X_VI_PS11_R_WORK);
        for (c = 0ul; c < 4ul; ++c) {
            if ((inst->dst_mask & (1u << c)) != 0u) {
                v9x_vi_ps11_copy(gen, result + c, work);
            }
        }
        return;
    }

    for (c = 0ul; c < 4ul; ++c) {
        v9x_u32 out = result + c;

        if ((inst->dst_mask & (1u << c)) == 0u) {
            continue;
        }
        a = v9x_vi_ps11_source(gen, &inst->src[0], c, 0ul);
        b = inst->sources > 1u
                ? v9x_vi_ps11_source(gen, &inst->src[1], c, 1ul) : a;
        d = inst->sources > 2u
                ? v9x_vi_ps11_source(gen, &inst->src[2], c, 2ul) : a;
        switch (inst->op) {
        case V9X_PS11_ADD:
            v9x_vi_ps11_alu(gen, V9X_VI_PS11_ADD, out, a, b,
                            V9X_VI_PS11_ZERO, mods);
            break;
        case V9X_PS11_SUB:
            v9x_vi_ps11_alu(gen, V9X_VI_PS11_SUB, out, a, b,
                            V9X_VI_PS11_ZERO, mods);
            break;
        case V9X_PS11_MUL:
            v9x_vi_ps11_alu(gen, V9X_VI_PS11_MUL, out, a, b,
                            V9X_VI_PS11_ZERO, mods);
            break;
        case V9X_PS11_MAD:
            v9x_vi_ps11_alu(gen, V9X_VI_VOP3_MAD_F32, out, a, b, d, mods);
            break;
        case V9X_PS11_LRP:
            /* a b + (1 - a) d as a (b - d) + d. */
            v9x_vi_ps11_alu(gen, V9X_VI_PS11_SUB, V9X_VI_PS11_R_WORK, b, d,
                            V9X_VI_PS11_ZERO, 0ul);
            v9x_vi_ps11_alu(gen, V9X_VI_VOP3_MAD_F32, out, a, work, d, mods);
            break;
        case V9X_PS11_CND:
            /* a > 0.5 ? b : d: v_cmp_gt into VCC, then v_cndmask_b32,
             * which on GCN 3 takes no source modifiers: a negated b or d
             * is made first. Not a float operation, so the shift and
             * saturate are a multiply by 1.0 after it. */
            if ((b & V9X_VI_PS11_NEGATE) != 0ul) {
                v9x_vi_ps11_copy(gen, V9X_VI_PS11_R_SOURCE + 1ul, b);
                b = V9X_VI_VGPR(V9X_VI_PS11_R_SOURCE + 1ul);
            }
            if ((d & V9X_VI_PS11_NEGATE) != 0ul) {
                v9x_vi_ps11_copy(gen, V9X_VI_PS11_R_SOURCE + 2ul, d);
                d = V9X_VI_VGPR(V9X_VI_PS11_R_SOURCE + 2ul);
            }
            v9x_vi_ps11_alu(gen, V9X_VI_VOPC_CMP_GT_F32, V9X_VCC, a,
                            V9X_VI_PS11_HALF, V9X_VI_PS11_ZERO, 0ul);
            v9x_vi_ps11_alu(gen, V9X_VI_PS11_CNDMASK, out, d, b, V9X_VCC, 0ul);
            if (mods != 0ul) {
                v9x_vi_ps11_alu(gen, V9X_VI_PS11_MUL, out, V9X_VI_VGPR(out),
                                V9X_VI_PS11_ONE, V9X_VI_PS11_ZERO, mods);
            }
            break;
        default:                /* MOV */
            if (mods != 0ul) {
                v9x_vi_ps11_alu(gen, V9X_VI_PS11_MUL, out, a,
                                V9X_VI_PS11_ONE, V9X_VI_PS11_ZERO, mods);
            } else {
                v9x_vi_ps11_copy(gen, out, a);
            }
            break;
        }
        v9x_vi_ps11_finish(gen, inst, out);
    }
}

static void v9x_vi_ps11_write(V9X_VI_PS11_GEN *gen, const V9X_PS11_INST *inst,
                              v9x_u32 result)
{
    v9x_u32 base = inst->dst_file == V9X_PS11_FILE_TEMP
                       ? V9X_VI_PS11_R_R + 4ul * inst->dst_reg
                       : V9X_VI_PS11_R_T + 4ul * inst->dst_reg;
    v9x_u32 c;

    for (c = 0ul; c < 4ul; ++c) {
        if ((inst->dst_mask & (1u << c)) != 0u) {
            v9x_vi_ps11_copy(gen, base + c, V9X_VI_VGPR(result + c));
        }
    }
}

/* ---- Texture instructions ------------------------------------------ */

/* t = (0, 0, 0, 1): an empty stage's lookup. */
static void v9x_vi_ps11_empty(V9X_VI_PS11_GEN *gen, v9x_u32 t)
{
    v9x_u32 c;

    for (c = 0ul; c < 3ul; ++c) {
        v9x_vi_ps11_copy(gen, t + c, V9X_VI_PS11_ZERO);
    }
    v9x_vi_ps11_copy(gen, t + 3ul, V9X_VI_PS11_ONE);
}

/* The address's u and v: stage's, over w when the stage is projected. */
static void v9x_vi_ps11_uv(V9X_VI_PS11_GEN *gen, v9x_u32 stage)
{
    v9x_u32 tc = V9X_VI_PS11_R_TC + 4ul * stage;
    v9x_u32 addr = V9X_VI_PS11_R_ADDR;

    if ((gen->desc->projected & (1ul << stage)) != 0ul) {
        v9x_vi_ps11_alu(gen, V9X_VI_PS11_RCP, V9X_VI_PS11_R_WORK,
                        V9X_VI_VGPR(tc + 3ul), V9X_VI_PS11_ZERO,
                        V9X_VI_PS11_ZERO, 0ul);
        v9x_vi_ps11_alu(gen, V9X_VI_PS11_MUL, addr, V9X_VI_VGPR(tc),
                        V9X_VI_VGPR(V9X_VI_PS11_R_WORK), V9X_VI_PS11_ZERO,
                        0ul);
        v9x_vi_ps11_alu(gen, V9X_VI_PS11_MUL, addr + 1ul,
                        V9X_VI_VGPR(tc + 1ul),
                        V9X_VI_VGPR(V9X_VI_PS11_R_WORK), V9X_VI_PS11_ZERO,
                        0ul);
        return;
    }
    v9x_vi_ps11_copy(gen, addr, V9X_VI_VGPR(tc));
    v9x_vi_ps11_copy(gen, addr + 1ul, V9X_VI_VGPR(tc + 1ul));
}

/*
 * The direction in the address's three VGPRs to a cube face and its
 * coordinates, as Mesa's ac_prepare_cube_coords: (sc / |ma| + 1.5,
 * tc / |ma| + 1.5, face). The 1.5 is not an inline constant and VOP3
 * takes no literal on GCN 3, so it is + 1.0 in the v_mad and + 0.5 after.
 * The face is computed last: its instruction reads x y z before it
 * writes over z.
 */
static void v9x_vi_ps11_cube(V9X_VI_PS11_GEN *gen)
{
    v9x_u32 x = V9X_VI_VGPR(V9X_VI_PS11_R_ADDR);
    v9x_u32 y = V9X_VI_VGPR(V9X_VI_PS11_R_ADDR + 1ul);
    v9x_u32 z = V9X_VI_VGPR(V9X_VI_PS11_R_ADDR + 2ul);
    v9x_u32 sc = V9X_VI_PS11_R_ADDR + 3ul;
    v9x_u32 tc = V9X_VI_PS11_R_CUBE_TC;
    v9x_u32 ma = V9X_VI_PS11_R_WORK2;
    v9x_u32 n;

    v9x_vi_ps11_alu(gen, V9X_VI_VOP3_CUBESC_F32, sc, x, y, z, 0ul);
    v9x_vi_ps11_alu(gen, V9X_VI_VOP3_CUBETC_F32, tc, x, y, z, 0ul);
    v9x_vi_ps11_alu(gen, V9X_VI_VOP3_CUBEMA_F32, ma, x, y, z, 0ul);
    v9x_vi_ps11_alu(gen, V9X_VI_VOP3_CUBEID_F32, V9X_VI_PS11_R_ADDR + 2ul,
                    x, y, z, 0ul);
    v9x_vi_ps11_alu(gen, V9X_VI_PS11_RCP, ma, V9X_VI_VGPR(ma),
                    V9X_VI_PS11_ZERO, V9X_VI_PS11_ZERO, V9X_VI_ABS(0));
    v9x_vi_ps11_alu(gen, V9X_VI_VOP3_MAD_F32, V9X_VI_PS11_R_ADDR,
                    V9X_VI_VGPR(sc), V9X_VI_VGPR(ma), V9X_VI_PS11_ONE, 0ul);
    v9x_vi_ps11_alu(gen, V9X_VI_VOP3_MAD_F32, V9X_VI_PS11_R_ADDR + 1ul,
                    V9X_VI_VGPR(tc), V9X_VI_VGPR(ma), V9X_VI_PS11_ONE, 0ul);
    for (n = 0ul; n < 2ul; ++n) {
        v9x_vi_ps11_put1(gen, v9x_vi_vop2(V9X_VI_VOP2_ADD_F32,
                                          V9X_VI_PS11_R_ADDR + n,
                                          V9X_VI_PS11_HALF,
                                          V9X_VI_PS11_R_ADDR + n));
    }
}

/* A lookup on stage into t, its coordinates in the address VGPRs. */
static void v9x_vi_ps11_lookup(V9X_VI_PS11_GEN *gen, v9x_u32 stage,
                               v9x_u32 t)
{
    v9x_u32 w[2];
    int cube = (gen->desc->cube & (1ul << stage)) != 0ul;

    if ((gen->desc->unbound & (1ul << stage)) != 0ul) {
        v9x_vi_ps11_empty(gen, t);
        return;
    }
    if (cube) {
        v9x_vi_ps11_cube(gen);
    }
    v9x_vi_image_sample(t, V9X_VI_PS11_R_ADDR, gen->s_t[stage],
                        gen->s_s[stage], V9X_VI_PS11_DMASK_RGBA, w);
    if (cube) {
        w[0] |= V9X_VI_MIMG_DA;
    }
    v9x_vi_ps11_put(gen, w, 2ul);
    v9x_vi_ps11_put1(gen, v9x_vi_s_waitcnt(V9X_VI_WAITCNT_ALL));
}

/* dst = tc . n, the stage's coordinates with src's three channels. */
static void v9x_vi_ps11_dot(V9X_VI_PS11_GEN *gen, v9x_u32 stage,
                            const V9X_PS11_SRC *src, v9x_u32 dst)
{
    v9x_u32 tc = V9X_VI_PS11_R_TC + 4ul * stage;
    v9x_u32 n[3];
    v9x_u32 c;

    for (c = 0ul; c < 3ul; ++c) {
        n[c] = v9x_vi_ps11_source(gen, src, c, c);
    }
    v9x_vi_ps11_alu(gen, V9X_VI_PS11_MUL, dst, V9X_VI_VGPR(tc), n[0],
                    V9X_VI_PS11_ZERO, 0ul);
    for (c = 1ul; c < 3ul; ++c) {
        v9x_vi_ps11_alu(gen, V9X_VI_VOP3_MAD_F32, dst, V9X_VI_VGPR(tc + c),
                        n[c], V9X_VI_VGPR(dst), 0ul);
    }
}

static void v9x_vi_ps11_texture(V9X_VI_PS11_GEN *gen,
                                const V9X_PS11_INST *inst)
{
    v9x_u32 stage = inst->dst_reg;
    v9x_u32 t = V9X_VI_PS11_R_T + 4ul * stage;
    v9x_u32 tc = V9X_VI_PS11_R_TC + 4ul * stage;
    v9x_u32 addr = V9X_VI_PS11_R_ADDR;
    v9x_u32 work = V9X_VI_VGPR(V9X_VI_PS11_R_WORK);
    v9x_u32 cube = (gen->desc->cube & (1ul << stage)) != 0ul ? 1ul : 0ul;
    v9x_u32 c;

    switch (inst->op) {
    case V9X_PS11_TEX:
        if (cube != 0ul) {
            for (c = 0ul; c < 3ul; ++c) {
                v9x_vi_ps11_copy(gen, addr + c, V9X_VI_VGPR(tc + c));
            }
        } else {
            v9x_vi_ps11_uv(gen, stage);
        }
        v9x_vi_ps11_lookup(gen, stage, t);
        return;

    case V9X_PS11_TEXCOORD:
        for (c = 0ul; c < 3ul; ++c) {
            v9x_vi_ps11_put1(gen, v9x_vi_vop2(V9X_VI_VOP2_MAX_F32, t + c,
                                              V9X_VI_PS11_ZERO, tc + c));
            v9x_vi_ps11_put1(gen, v9x_vi_vop2(V9X_VI_VOP2_MIN_F32, t + c,
                                              V9X_VI_PS11_ONE, t + c));
        }
        v9x_vi_ps11_copy(gen, t + 3ul, V9X_VI_PS11_ONE);
        return;

    case V9X_PS11_TEXKILL:
        gen->kill_stages |= 1ul << stage;
        return;

    case V9X_PS11_TEXBEM:
    case V9X_PS11_TEXBEML:
    {
        v9x_u32 bump = gen->s_bump[stage];
        v9x_u32 du;
        v9x_u32 dv;

        if (cube != 0ul) {
            gen->unsupported = 1ul;
            return;
        }
        du = v9x_vi_ps11_source(gen, &inst->src[0], 0ul, 0ul);
        dv = v9x_vi_ps11_source(gen, &inst->src[0], 1ul, 1ul);
        v9x_vi_ps11_uv(gen, stage);
        /* u + m00 du + m10 dv, v + m01 du + m11 dv, in that order. */
        v9x_vi_ps11_alu(gen, V9X_VI_VOP3_MAD_F32, addr, du, bump,
                        V9X_VI_VGPR(addr), 0ul);
        v9x_vi_ps11_alu(gen, V9X_VI_VOP3_MAD_F32, addr, dv, bump + 2ul,
                        V9X_VI_VGPR(addr), 0ul);
        v9x_vi_ps11_alu(gen, V9X_VI_VOP3_MAD_F32, addr + 1ul, du, bump + 1ul,
                        V9X_VI_VGPR(addr + 1ul), 0ul);
        v9x_vi_ps11_alu(gen, V9X_VI_VOP3_MAD_F32, addr + 1ul, dv, bump + 3ul,
                        V9X_VI_VGPR(addr + 1ul), 0ul);
        v9x_vi_ps11_lookup(gen, stage, t);
        if (inst->op == V9X_PS11_TEXBEML) {
            v9x_u32 lum = gen->s_luminance[stage];
            v9x_u32 z = v9x_vi_ps11_source(gen, &inst->src[0], 2ul, 2ul);

            v9x_vi_ps11_alu(gen, V9X_VI_VOP3_MAD_F32, V9X_VI_PS11_R_WORK, z,
                            lum, lum + 1ul, 0ul);
            for (c = 0ul; c < 3ul; ++c) {
                v9x_vi_ps11_alu(gen, V9X_VI_PS11_MUL, t + c,
                                V9X_VI_VGPR(t + c), work, V9X_VI_PS11_ZERO,
                                0ul);
            }
        }
        return;
    }

    case V9X_PS11_TEXM3X3PAD:
        v9x_vi_ps11_dot(gen, stage, &inst->src[0],
                        V9X_VI_PS11_R_PAD + (gen->pads & 1ul));
        ++gen->pads;
        return;

    default:
    {
        /* The third row, then the lookup with the three dots N, or with
         * the reflection R = 2 (N.E) / (N.N) N - E. */
        v9x_u32 e[3];
        v9x_u32 nn = V9X_VI_PS11_R_WORK2;

        v9x_vi_ps11_dot(gen, stage, &inst->src[0], addr + 2ul);
        v9x_vi_ps11_copy(gen, addr, V9X_VI_VGPR(V9X_VI_PS11_R_PAD));
        v9x_vi_ps11_copy(gen, addr + 1ul,
                         V9X_VI_VGPR(V9X_VI_PS11_R_PAD + 1ul));
        gen->pads = 0ul;
        if (inst->op == V9X_PS11_TEXM3X3TEX) {
            v9x_vi_ps11_lookup(gen, stage, t);
            return;
        }
        if (inst->op == V9X_PS11_TEXM3X3VSPEC) {
            e[0] = V9X_VI_VGPR(V9X_VI_PS11_R_TC + 4ul * (stage - 2ul) + 3ul);
            e[1] = V9X_VI_VGPR(V9X_VI_PS11_R_TC + 4ul * (stage - 1ul) + 3ul);
            e[2] = V9X_VI_VGPR(tc + 3ul);
        } else {
            for (c = 0ul; c < 3ul; ++c) {
                e[c] = gen->s_const[inst->src[1].reg] + c;
            }
        }
        v9x_vi_ps11_alu(gen, V9X_VI_PS11_MUL, V9X_VI_PS11_R_WORK,
                        V9X_VI_VGPR(addr), e[0], V9X_VI_PS11_ZERO, 0ul);
        v9x_vi_ps11_alu(gen, V9X_VI_PS11_MUL, nn, V9X_VI_VGPR(addr),
                        V9X_VI_VGPR(addr), V9X_VI_PS11_ZERO, 0ul);
        for (c = 1ul; c < 3ul; ++c) {
            v9x_vi_ps11_alu(gen, V9X_VI_VOP3_MAD_F32, V9X_VI_PS11_R_WORK,
                            V9X_VI_VGPR(addr + c), e[c], work, 0ul);
            v9x_vi_ps11_alu(gen, V9X_VI_VOP3_MAD_F32, nn,
                            V9X_VI_VGPR(addr + c), V9X_VI_VGPR(addr + c),
                            V9X_VI_VGPR(nn), 0ul);
        }
        v9x_vi_ps11_alu(gen, V9X_VI_PS11_RCP, nn, V9X_VI_VGPR(nn),
                        V9X_VI_PS11_ZERO, V9X_VI_PS11_ZERO, 0ul);
        v9x_vi_ps11_alu(gen, V9X_VI_PS11_ADD, V9X_VI_PS11_R_WORK, work, work,
                        V9X_VI_PS11_ZERO, 0ul);
        v9x_vi_ps11_alu(gen, V9X_VI_PS11_MUL, V9X_VI_PS11_R_WORK, work,
                        V9X_VI_VGPR(nn), V9X_VI_PS11_ZERO, 0ul);
        for (c = 0ul; c < 3ul; ++c) {
            v9x_vi_ps11_alu(gen, V9X_VI_VOP3_MAD_F32, addr + c, work,
                            V9X_VI_VGPR(addr + c),
                            e[c] | V9X_VI_PS11_NEGATE, 0ul);
        }
        v9x_vi_ps11_lookup(gen, stage, t);
        return;
    }
    }
}

/* ---- What the program reads ---------------------------------------- */

typedef struct v9x_vi_ps11_scan {
    v9x_u32 coords;         /* coordinate sets read */
    v9x_u32 written;        /* stages a texture instruction writes */
    v9x_u32 samples;        /* stages looked up */
    v9x_u32 bump;           /* stages with texbem or texbeml */
    v9x_u32 luminance;      /* stages with texbeml */
    v9x_u32 constants;      /* constant registers read */
} V9X_VI_PS11_SCAN;

static int v9x_vi_ps11_looks_up(v9x_u32 op)
{
    return op == V9X_PS11_TEX || op == V9X_PS11_TEXBEM ||
           op == V9X_PS11_TEXBEML || op == V9X_PS11_TEXM3X3TEX ||
           op == V9X_PS11_TEXM3X3SPEC || op == V9X_PS11_TEXM3X3VSPEC;
}

static void v9x_vi_ps11_scan(const V9X_PS11_PROGRAM *program,
                             V9X_VI_PS11_SCAN *scan)
{
    v9x_u32 i;
    v9x_u32 s;

    scan->coords = 0ul;
    scan->written = 0ul;
    scan->samples = 0ul;
    scan->bump = 0ul;
    scan->luminance = 0ul;
    scan->constants = 0ul;
    for (i = 0ul; i < program->count; ++i) {
        const V9X_PS11_INST *inst = &program->inst[i];
        v9x_u32 stage = inst->dst_reg;

        for (s = 0ul; s < inst->sources && s < 3ul; ++s) {
            if (inst->src[s].file == V9X_PS11_FILE_CONST) {
                scan->constants |= 1ul << inst->src[s].reg;
            }
        }
        if (!V9X_PS11_IS_TEXTURE(inst->op)) {
            continue;
        }
        scan->coords |= 1ul << stage;
        if (inst->op != V9X_PS11_TEXKILL) {
            scan->written |= 1ul << stage;
        }
        if (v9x_vi_ps11_looks_up(inst->op)) {
            scan->samples |= 1ul << stage;
        }
        if (inst->op == V9X_PS11_TEXBEM || inst->op == V9X_PS11_TEXBEML) {
            scan->bump |= 1ul << stage;
        }
        if (inst->op == V9X_PS11_TEXBEML) {
            scan->luminance |= 1ul << stage;
        }
        if (inst->op == V9X_PS11_TEXM3X3VSPEC && stage >= 2u) {
            scan->coords |= 3ul << (stage - 2u);
        }
    }
}

/* s_load_dwordx<dwords> of the table's dwords from index into SGPRs
 * from the next free four; returns the first. */
static v9x_u32 v9x_vi_ps11_load(V9X_VI_PS11_GEN *gen, v9x_u32 dwords,
                                v9x_u32 index)
{
    v9x_u32 sdst = gen->s_next;
    v9x_u32 w[2];

    gen->s_next += dwords < 4ul ? 4ul : dwords;
    if (gen->s_next > V9X_VI_PS11_S_LIMIT) {
        gen->unsupported = 1ul;
        return V9X_VI_PS11_S_FIRST;
    }
    if (v9x_vi_s_load(dwords, sdst, V9X_VI_PS11_S_TABLE, index * 4ul, w) ==
        0ul) {
        gen->unsupported = 1ul;
        return V9X_VI_PS11_S_FIRST;
    }
    v9x_vi_ps11_put(gen, w, 2ul);
    return sdst;
}

/* p1 and p2 of parameter attr's four channels into VGPRs from vdst. */
static void v9x_vi_ps11_interp(V9X_VI_PS11_GEN *gen, v9x_u32 vdst,
                               v9x_u32 attr)
{
    v9x_u32 c;

    for (c = 0ul; c < 4ul; ++c) {
        v9x_vi_ps11_put1(gen, v9x_vi_vintrp(V9X_VI_VINTRP_P1_F32, vdst + c,
                                            0ul, attr, c));
        v9x_vi_ps11_put1(gen, v9x_vi_vintrp(V9X_VI_VINTRP_P2_F32, vdst + c,
                                            1ul, attr, c));
    }
}

v9x_status v9x_vi_ps11_build(v9x_u32 *words, v9x_u32 capacity,
                             const V9X_PS11_PROGRAM *program,
                             const struct v9x_vi_ps11_desc *desc,
                             struct v9x_vi_ps11_info *info)
{
    V9X_VI_PS11_GEN gen;
    V9X_VI_PS11_SCAN scan;
    v9x_u32 w[2];
    v9x_u32 branch = V9X_VI_PS11_NONE;
    v9x_u32 stage;
    v9x_u32 i;
    v9x_u32 c;

    if (words == 0 || program == 0 || desc == 0 || info == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    gen.words = words;
    gen.capacity = capacity;
    gen.count = 0ul;
    gen.overflow = 0ul;
    gen.program = program;
    gen.desc = desc;
    gen.pads = 0ul;
    gen.kill_stages = 0ul;
    gen.unsupported = 0ul;
    gen.s_next = V9X_VI_PS11_S_FIRST;
    gen.s_fog = V9X_VI_PS11_NONE;
    for (i = 0ul; i < V9X_VI_PS11_STAGES; ++i) {
        gen.s_t[i] = V9X_VI_PS11_NONE;
        gen.s_s[i] = V9X_VI_PS11_NONE;
        gen.s_bump[i] = V9X_VI_PS11_NONE;
        gen.s_luminance[i] = V9X_VI_PS11_NONE;
    }
    for (i = 0ul; i < 8ul; ++i) {
        gen.s_const[i] = V9X_VI_PS11_NONE;
    }
    v9x_vi_ps11_scan(program, &scan);

    /* The pixels' EXEC kept and whole-quad mode on; M0 from PRIM_MASK. */
    v9x_vi_ps11_put1(&gen, v9x_vi_s_mov_b64(V9X_VI_PS11_S_EXEC,
                                            V9X_VI_EXEC));
    v9x_vi_ps11_put1(&gen, v9x_vi_s_wqm_b64(V9X_VI_EXEC, V9X_VI_EXEC));
    v9x_vi_ps11_put1(&gen, v9x_vi_s_mov_m0(V9X_VI_PS11_S_PRIM));

    /* The table's blocks the program reads, while M0 settles. */
    for (stage = 0ul; stage < V9X_VI_PS11_STAGES; ++stage) {
        if ((scan.samples & (1ul << stage)) == 0ul ||
            (desc->unbound & (1ul << stage)) != 0ul) {
            continue;
        }
        gen.s_t[stage] = v9x_vi_ps11_load(&gen, 8ul,
                                          V9X_VI_PS11_T_SHARP + 8ul * stage);
        gen.s_s[stage] = v9x_vi_ps11_load(&gen, 4ul,
                                          V9X_VI_PS11_S_SHARP + 4ul * stage);
    }
    for (i = 0ul; i < 8ul; ++i) {
        if ((scan.constants & (1ul << i)) != 0ul) {
            gen.s_const[i] = v9x_vi_ps11_load(&gen, 4ul,
                                              V9X_VI_PS11_CONST + 4ul * i);
        }
    }
    for (stage = 0ul; stage < V9X_VI_PS11_STAGES; ++stage) {
        if ((scan.bump & (1ul << stage)) != 0ul) {
            gen.s_bump[stage] =
                v9x_vi_ps11_load(&gen, 4ul, V9X_VI_PS11_BUMP + 4ul * stage);
        }
        if ((scan.luminance & (1ul << stage)) != 0ul) {
            gen.s_luminance[stage] = v9x_vi_ps11_load(
                &gen, 2ul, V9X_VI_PS11_LUMINANCE + 2ul * stage);
        }
    }
    if (desc->fog != 0ul) {
        gen.s_fog = v9x_vi_ps11_load(&gen, 4ul, V9X_VI_PS11_FOG);
    }
    /* GCN 3 wants one wait state between a SALU write of M0 and a
     * v_interp that reads it (vi_shader.c's PSs). */
    v9x_vi_ps11_put1(&gen, v9x_vi_s_nop(0ul));

    /* The inputs the program reads: v1 also for fog, whose factor is its
     * alpha. */
    for (i = 0ul; i < 2ul; ++i) {
        if ((program->inputs & (1ul << i)) == 0ul &&
            !(i == 1ul && desc->fog != 0ul)) {
            continue;
        }
        v9x_vi_ps11_interp(&gen, V9X_VI_PS11_R_V + 4ul * i,
                           V9X_VI_PS11_PARAM_DIFFUSE + i);
    }
    for (stage = 0ul; stage < V9X_VI_PS11_STAGES; ++stage) {
        if ((scan.coords & (1ul << stage)) != 0ul) {
            v9x_vi_ps11_interp(&gen, V9X_VI_PS11_R_TC + 4ul * stage,
                               V9X_VI_PS11_PARAM_COORDS + stage);
        }
    }
    v9x_vi_ps11_put1(&gen, v9x_vi_s_waitcnt(V9X_VI_WAITCNT_ALL));

    /* r0, r1 and the stages no texture instruction writes read zero. */
    for (c = 0ul; c < 8ul; ++c) {
        v9x_vi_ps11_copy(&gen, V9X_VI_PS11_R_R + c, V9X_VI_PS11_ZERO);
    }
    for (stage = 0ul; stage < V9X_VI_PS11_STAGES; ++stage) {
        if ((scan.written & (1ul << stage)) != 0ul) {
            continue;
        }
        for (c = 0ul; c < 4ul; ++c) {
            v9x_vi_ps11_copy(&gen, V9X_VI_PS11_R_T + 4ul * stage + c,
                             V9X_VI_PS11_ZERO);
        }
    }

    for (i = 0ul; i < program->count; ++i) {
        const V9X_PS11_INST *inst = &program->inst[i];

        if (inst->op == V9X_PS11_NOP) {
            continue;
        }
        if (V9X_PS11_IS_TEXTURE(inst->op)) {
            v9x_vi_ps11_texture(&gen, inst);
            continue;
        }
        if (i + 1ul < program->count && program->inst[i + 1ul].coissue) {
            v9x_vi_ps11_arithmetic(&gen, inst, V9X_VI_PS11_R_RESULT);
            v9x_vi_ps11_arithmetic(&gen, &program->inst[i + 1ul],
                                   V9X_VI_PS11_R_RESULT2);
            v9x_vi_ps11_write(&gen, inst, V9X_VI_PS11_R_RESULT);
            v9x_vi_ps11_write(&gen, &program->inst[i + 1ul],
                              V9X_VI_PS11_R_RESULT2);
            ++i;
            continue;
        }
        v9x_vi_ps11_arithmetic(&gen, inst, V9X_VI_PS11_R_RESULT);
        v9x_vi_ps11_write(&gen, inst, V9X_VI_PS11_R_RESULT);
    }

    /* The pixels' own EXEC back, so no kill or export acts for a helper
     * lane. */
    v9x_vi_ps11_put1(&gen, v9x_vi_s_mov_b64(V9X_VI_EXEC, V9X_VI_PS11_S_EXEC));

    /* texkill: any of the stage's x, y, z below zero. Each v_cmpx keeps
     * the pixels where 0 <= x; a wave left with none branches to the
     * null export, as vi_shader.c's kills do. */
    if (gen.kill_stages != 0ul) {
        for (stage = 0ul; stage < V9X_VI_PS11_STAGES; ++stage) {
            if ((gen.kill_stages & (1ul << stage)) == 0ul) {
                continue;
            }
            for (c = 0ul; c < 3ul; ++c) {
                v9x_vi_ps11_put1(&gen, v9x_vi_vopc(
                    V9X_VI_VOPC_CMPX_LE_F32, V9X_VI_PS11_ZERO,
                    V9X_VI_PS11_R_TC + 4ul * stage + c));
            }
        }
        branch = gen.count;
        v9x_vi_ps11_put1(&gen, v9x_vi_s_cbranch_execz(0ul));
    }

    /* Fog after the shader: r0 clamped, then its colour towards the fog
     * colour by v1.w, interpolated above when fog is on. */
    if (desc->fog != 0ul) {
        v9x_u32 factor = V9X_VI_VGPR(V9X_VI_PS11_R_V + 7ul);

        for (c = 0ul; c < 4ul; ++c) {
            v9x_vi_ps11_put1(&gen, v9x_vi_vop2(V9X_VI_VOP2_MAX_F32,
                                               V9X_VI_PS11_R_R + c,
                                               V9X_VI_PS11_ZERO,
                                               V9X_VI_PS11_R_R + c));
            v9x_vi_ps11_put1(&gen, v9x_vi_vop2(V9X_VI_VOP2_MIN_F32,
                                               V9X_VI_PS11_R_R + c,
                                               V9X_VI_PS11_ONE,
                                               V9X_VI_PS11_R_R + c));
        }
        for (c = 0ul; c < 3ul; ++c) {
            v9x_vi_ps11_alu(&gen, V9X_VI_PS11_SUB, V9X_VI_PS11_R_WORK,
                            V9X_VI_VGPR(V9X_VI_PS11_R_R + c), gen.s_fog + c,
                            V9X_VI_PS11_ZERO, 0ul);
            v9x_vi_ps11_alu(&gen, V9X_VI_VOP3_MAD_F32, V9X_VI_PS11_R_R + c,
                            V9X_VI_VGPR(V9X_VI_PS11_R_WORK), factor,
                            gen.s_fog + c, 0ul);
        }
    }

    /* r0 packed into v0:v1 and exported compressed (vi_shader.h). */
    v9x_vi_cvt_pkrtz(0ul, V9X_VI_VGPR(V9X_VI_PS11_R_R),
                     V9X_VI_VGPR(V9X_VI_PS11_R_R + 1ul), w);
    v9x_vi_ps11_put(&gen, w, 2ul);
    v9x_vi_cvt_pkrtz(1ul, V9X_VI_VGPR(V9X_VI_PS11_R_R + 2ul),
                     V9X_VI_VGPR(V9X_VI_PS11_R_R + 3ul), w);
    v9x_vi_ps11_put(&gen, w, 2ul);
    v9x_vi_exp_compr(V9X_VI_EXP_MRT0, 1, 1, 0ul, 1ul, w);
    v9x_vi_ps11_put(&gen, w, 2ul);
    v9x_vi_ps11_put1(&gen, V9X_VI_S_ENDPGM);
    if (branch != V9X_VI_PS11_NONE) {
        if (branch < gen.capacity) {
            gen.words[branch] =
                v9x_vi_s_cbranch_execz(gen.count - branch - 1ul);
        }
        v9x_vi_exp_null_done(w);
        v9x_vi_ps11_put(&gen, w, 2ul);
        v9x_vi_ps11_put1(&gen, V9X_VI_S_ENDPGM);
    }

    if (gen.unsupported != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (gen.overflow != 0ul) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    info->words = gen.count;
    info->vgprs = V9X_VI_PS11_VGPRS;
    info->sgprs = gen.s_next;
    info->kills = gen.kill_stages != 0ul ? 1ul : 0ul;
    info->coords = scan.coords;
    return V9X_STATUS_OK;
}
