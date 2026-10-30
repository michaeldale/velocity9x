/*
 * Gen7.5 kernels for pixel shaders 1.0 and 1.1; intel_hsw_ps11.h says what
 * they expect and write. Every rule of the shader is the reference
 * interpreter's (src\display32\d3d\d3d_ps11_ref.c), and
 * tests\host\test_intel_hsw_ps11.c holds each kernel to it in the host EU
 * model.
 *
 * Register map, SIMD8 Align1 float, one register a channel:
 *   g32-g39   r0, r1               g40-g55   t0-t3
 *   g56-g63   v0, v1               g64-g79   coordinate sets 0-3, xyzw
 *   g80-g87   results of an instruction, or of a co-issued pair, before
 *             they are written
 *   g88-g90   sources with a modifier applied
 *   g91-g95   work                 g96-g98   sampler payload u v r
 *   g99       the smallest texkill coordinate so far
 *   g100-g102 the texm3x3 dot products
 *   g103      the fog factor
 *   g112-g115 the colour written
 */
#include "velocity9x/intel_hsw_eu.h"
#include "velocity9x/intel_hsw_ps11.h"

#define V9X_HSW_PS11_G_R       ((v9x_u8)32u)
#define V9X_HSW_PS11_G_T       ((v9x_u8)40u)
#define V9X_HSW_PS11_G_V       ((v9x_u8)56u)
#define V9X_HSW_PS11_G_TC      ((v9x_u8)64u)
#define V9X_HSW_PS11_G_RESULT  ((v9x_u8)80u)
#define V9X_HSW_PS11_G_SOURCE  ((v9x_u8)88u)
#define V9X_HSW_PS11_G_WORK    ((v9x_u8)91u)
#define V9X_HSW_PS11_G_WORK2   ((v9x_u8)92u)
#define V9X_HSW_PS11_G_WORK3   ((v9x_u8)93u)
#define V9X_HSW_PS11_G_HOLD    ((v9x_u8)95u)   /* an immediate made a register */
#define V9X_HSW_PS11_G_PAYLOAD ((v9x_u8)96u)
#define V9X_HSW_PS11_G_KILL    ((v9x_u8)99u)
#define V9X_HSW_PS11_G_DOT     ((v9x_u8)100u)
#define V9X_HSW_PS11_G_FOG     ((v9x_u8)103u)
#define V9X_HSW_PS11_G_OUT     ((v9x_u8)112u)
#define V9X_HSW_PS11_G_BARY    ((v9x_u8)2u)

/* The flags: f0.0 for texkill, f0.1 for cnd. */
#define V9X_HSW_PS11_FLAG_KILL ((v9x_u8)0u)
#define V9X_HSW_PS11_FLAG_CND  ((v9x_u8)1u)

#define V9X_HSW_PS11_SIGN ((v9x_u32)0x80000000ul)

typedef struct v9x_hsw_ps11_gen {
    v9x_u32 *out;
    v9x_u32 capacity;
    v9x_u32 used;
    int failed;
    const V9X_PS11_PROGRAM *program;
    const struct v9x_hsw_ps11_desc *desc;
} V9X_HSW_PS11_GEN;

typedef struct v9x_hsw_eu_operand V9X_HSW_PS11_OP;

/* Float bits and back, without a library. */
typedef union v9x_hsw_ps11_float {
    v9x_u32 bits;
    float value;
} V9X_HSW_PS11_FLOAT;

static v9x_u32 v9x_hsw_ps11_bits(float value)
{
    V9X_HSW_PS11_FLOAT f;

    f.value = value;
    return f.bits;
}

static float v9x_hsw_ps11_value(v9x_u32 bits)
{
    V9X_HSW_PS11_FLOAT f;

    f.bits = bits;
    return f.value;
}

static void v9x_hsw_ps11_clear(void *p, v9x_u32 bytes)
{
    v9x_u8 *b = (v9x_u8 *)p;
    v9x_u32 i;

    for (i = 0ul; i < bytes; ++i) {
        b[i] = 0u;
    }
}

/* <8;8,1>:F, one channel a lane. */
static V9X_HSW_PS11_OP v9x_hsw_ps11_reg(v9x_u8 nr)
{
    V9X_HSW_PS11_OP op;

    v9x_hsw_ps11_clear(&op, sizeof(op));
    op.file = V9X_HSW_EU_FILE_GRF;
    op.type = V9X_HSW_EU_TYPE_F;
    op.nr = nr;
    op.vstride = V9X_HSW_EU_VSTRIDE_8;
    op.width = V9X_HSW_EU_WIDTH_8;
    op.hstride = V9X_HSW_EU_HSTRIDE_1;
    return op;
}

/* <0;1,0>:F, one dword of a register for every lane. */
static V9X_HSW_PS11_OP v9x_hsw_ps11_scalar(v9x_u8 nr, v9x_u32 dword)
{
    V9X_HSW_PS11_OP op = v9x_hsw_ps11_reg(nr);

    op.subnr = (v9x_u8)(dword * 4ul);
    op.vstride = V9X_HSW_EU_VSTRIDE_0;
    op.width = V9X_HSW_EU_WIDTH_1;
    op.hstride = V9X_HSW_EU_HSTRIDE_0;
    return op;
}

static V9X_HSW_PS11_OP v9x_hsw_ps11_imm(float value)
{
    V9X_HSW_PS11_OP op;

    v9x_hsw_ps11_clear(&op, sizeof(op));
    op.file = V9X_HSW_EU_FILE_IMM;
    op.type = V9X_HSW_EU_TYPE_F;
    op.imm = v9x_hsw_ps11_bits(value);
    return op;
}

static V9X_HSW_PS11_OP v9x_hsw_ps11_null(void)
{
    V9X_HSW_PS11_OP op;

    v9x_hsw_ps11_clear(&op, sizeof(op));
    op.file = V9X_HSW_EU_FILE_ARF;
    op.type = V9X_HSW_EU_TYPE_F;
    op.hstride = V9X_HSW_EU_HSTRIDE_1;
    return op;
}

/* The operand negated: an immediate's sign flipped, a register's source
 * modifier toggled. */
static V9X_HSW_PS11_OP v9x_hsw_ps11_neg(V9X_HSW_PS11_OP op)
{
    if (op.file == V9X_HSW_EU_FILE_IMM) {
        op.imm ^= V9X_HSW_PS11_SIGN;
    } else {
        op.mod ^= V9X_HSW_EU_MOD_NEG;
    }
    return op;
}

static V9X_HSW_PS11_OP v9x_hsw_ps11_abs(V9X_HSW_PS11_OP op)
{
    op.mod = V9X_HSW_EU_MOD_ABS;
    return op;
}

static void v9x_hsw_ps11_emit(V9X_HSW_PS11_GEN *g,
                              const struct v9x_hsw_eu_inst *inst)
{
    if (g->failed || g->capacity - g->used < 4ul ||
        v9x_intel_hsw_eu_encode(inst, g->out + g->used) != V9X_STATUS_OK) {
        g->failed = 1;
        return;
    }
    g->used += 4ul;
}

static void v9x_hsw_ps11_inst(struct v9x_hsw_eu_inst *inst, v9x_u8 opcode,
                              v9x_u8 dst)
{
    v9x_hsw_ps11_clear(inst, sizeof(*inst));
    inst->opcode = opcode;
    inst->exec_size = V9X_HSW_EU_EXEC_8;
    inst->dst = v9x_hsw_ps11_reg(dst);
}

static void v9x_hsw_ps11_mov(V9X_HSW_PS11_GEN *g, v9x_u8 dst,
                             V9X_HSW_PS11_OP src, v9x_u8 saturate)
{
    struct v9x_hsw_eu_inst inst;

    v9x_hsw_ps11_inst(&inst, V9X_HSW_EU_OP_MOV, dst);
    inst.saturate = saturate;
    inst.src0 = src;
    v9x_hsw_ps11_emit(g, &inst);
}

/*
 * A two-source operation. An immediate may only be the second source:
 * for add, mul and a sel by condition (max and min) the sources swap;
 * otherwise, or when both are immediates, the first is moved into a
 * register.
 */
static void v9x_hsw_ps11_op2(V9X_HSW_PS11_GEN *g, v9x_u8 opcode,
                             v9x_u8 cond, v9x_u8 dst, V9X_HSW_PS11_OP a,
                             V9X_HSW_PS11_OP b)
{
    struct v9x_hsw_eu_inst inst;

    if (a.file == V9X_HSW_EU_FILE_IMM && b.file != V9X_HSW_EU_FILE_IMM) {
        V9X_HSW_PS11_OP t = a;

        a = b;
        b = t;
    }
    if (a.file == V9X_HSW_EU_FILE_IMM) {
        v9x_hsw_ps11_mov(g, V9X_HSW_PS11_G_HOLD, a, 0u);
        a = v9x_hsw_ps11_reg(V9X_HSW_PS11_G_HOLD);
    }
    v9x_hsw_ps11_inst(&inst, opcode, dst);
    inst.cond_mod = cond;
    inst.src0 = a;
    inst.src1 = b;
    v9x_hsw_ps11_emit(g, &inst);
}

static void v9x_hsw_ps11_add(V9X_HSW_PS11_GEN *g, v9x_u8 dst,
                             V9X_HSW_PS11_OP a, V9X_HSW_PS11_OP b)
{
    v9x_hsw_ps11_op2(g, V9X_HSW_EU_OP_ADD, 0u, dst, a, b);
}

static void v9x_hsw_ps11_mul(V9X_HSW_PS11_GEN *g, v9x_u8 dst,
                             V9X_HSW_PS11_OP a, V9X_HSW_PS11_OP b)
{
    v9x_hsw_ps11_op2(g, V9X_HSW_EU_OP_MUL, 0u, dst, a, b);
}

/* sel.ge (max) or sel.l (min). */
static void v9x_hsw_ps11_sel(V9X_HSW_PS11_GEN *g, v9x_u8 cond, v9x_u8 dst,
                             V9X_HSW_PS11_OP a, V9X_HSW_PS11_OP b)
{
    v9x_hsw_ps11_op2(g, V9X_HSW_EU_OP_SEL, cond, dst, a, b);
}

/* math.inv dst src. */
static void v9x_hsw_ps11_inv(V9X_HSW_PS11_GEN *g, v9x_u8 dst,
                             V9X_HSW_PS11_OP src)
{
    struct v9x_hsw_eu_inst inst;

    v9x_hsw_ps11_inst(&inst, V9X_HSW_EU_OP_MATH, dst);
    inst.cond_mod = V9X_HSW_EU_MATH_INV;
    inst.src0 = src;
    inst.src1 = v9x_hsw_ps11_null();
    v9x_hsw_ps11_emit(g, &inst);
}

/* cmp.cond f0.<flag> null a b. */
static void v9x_hsw_ps11_cmp(V9X_HSW_PS11_GEN *g, v9x_u8 cond, v9x_u8 flag,
                             V9X_HSW_PS11_OP a, V9X_HSW_PS11_OP b)
{
    struct v9x_hsw_eu_inst inst;

    v9x_hsw_ps11_inst(&inst, V9X_HSW_EU_OP_CMP, 0u);
    inst.dst = v9x_hsw_ps11_null();
    inst.cond_mod = cond;
    inst.flag_subreg = flag;
    inst.src0 = a;
    inst.src1 = b;
    v9x_hsw_ps11_emit(g, &inst);
}

/* (+f0.<flag>) sel dst a b: a where the flag is set, b elsewhere. */
static void v9x_hsw_ps11_pick(V9X_HSW_PS11_GEN *g, v9x_u8 flag, v9x_u8 dst,
                              V9X_HSW_PS11_OP a, V9X_HSW_PS11_OP b)
{
    struct v9x_hsw_eu_inst inst;

    if (a.file == V9X_HSW_EU_FILE_IMM) {
        v9x_hsw_ps11_mov(g, V9X_HSW_PS11_G_HOLD, a, 0u);
        a = v9x_hsw_ps11_reg(V9X_HSW_PS11_G_HOLD);
    }
    v9x_hsw_ps11_inst(&inst, V9X_HSW_EU_OP_SEL, dst);
    inst.pred_ctrl = 1u;
    inst.flag_subreg = flag;
    inst.src0 = a;
    inst.src1 = b;
    v9x_hsw_ps11_emit(g, &inst);
}

/* pln(8) dst, attribute a, component c (Vol 7 pln; as hsw_eu.c's). */
static void v9x_hsw_ps11_pln(V9X_HSW_PS11_GEN *g, v9x_u8 dst, v9x_u32 a,
                             v9x_u32 c)
{
    struct v9x_hsw_eu_inst inst;

    v9x_hsw_ps11_inst(&inst, V9X_HSW_EU_OP_PLN, dst);
    inst.src0 = v9x_hsw_ps11_reg(
        (v9x_u8)(g->desc->setup_grf + 2ul * a + c / 2ul));
    inst.src0.subnr = (v9x_u8)((c % 2ul) * 16ul);
    inst.src0.vstride = V9X_HSW_EU_VSTRIDE_0;
    inst.src0.width = V9X_HSW_EU_WIDTH_1;
    inst.src0.hstride = V9X_HSW_EU_HSTRIDE_0;
    inst.src1 = v9x_hsw_ps11_reg(V9X_HSW_PS11_G_BARY);
    v9x_hsw_ps11_emit(g, &inst);
}

/* send(8) the SIMD8 "sample" message from the payload, mlen coordinates,
 * into stage's four texture registers: sampler stage, binding table entry
 * 1 + stage, no header. A stage with no texture is not sent for: its
 * registers get (0, 0, 0, 1), as the interpreter's lookup. */
static void v9x_hsw_ps11_sample(V9X_HSW_PS11_GEN *g, v9x_u32 stage,
                                v9x_u8 mlen)
{
    struct v9x_hsw_eu_inst inst;
    v9x_u32 function;
    v9x_u32 desc;
    v9x_u8 k;

    if ((g->desc->unbound & (1ul << stage)) != 0ul) {
        for (k = 0u; k < 4u; ++k) {
            v9x_hsw_ps11_mov(g, (v9x_u8)(V9X_HSW_PS11_G_T + 4ul * stage + k),
                             v9x_hsw_ps11_imm(k == 3u ? 1.0f : 0.0f), 0u);
        }
        return;
    }
    if (v9x_intel_hsw_eu_sampler_fc(V9X_HSW_SAMPLER_SIMD8, 0u, (v9x_u8)stage,
                                    (v9x_u8)(1ul + stage), &function) !=
            V9X_STATUS_OK ||
        v9x_intel_hsw_eu_send_desc(mlen, 4u, 0u, function, &desc) !=
            V9X_STATUS_OK) {
        g->failed = 1;
        return;
    }
    v9x_hsw_ps11_inst(&inst, V9X_HSW_EU_OP_SEND,
                      (v9x_u8)(V9X_HSW_PS11_G_T + 4ul * stage));
    inst.sfid = V9X_HSW_SFID_SAMPLER;
    inst.desc = desc;
    inst.dst.type = V9X_HSW_EU_TYPE_UW;
    inst.src0 = v9x_hsw_ps11_reg(V9X_HSW_PS11_G_PAYLOAD);
    inst.src0.type = V9X_HSW_EU_TYPE_UD;
    v9x_hsw_ps11_emit(g, &inst);
}

/* The payload's three coordinates over their largest magnitude: what the
 * sampler wants for a cube (Vol 7, sample message). */
static void v9x_hsw_ps11_cube(V9X_HSW_PS11_GEN *g)
{
    v9x_u8 k;

    v9x_hsw_ps11_sel(g, V9X_HSW_EU_COND_GE, V9X_HSW_PS11_G_WORK,
                     v9x_hsw_ps11_abs(v9x_hsw_ps11_reg(V9X_HSW_PS11_G_PAYLOAD)),
                     v9x_hsw_ps11_abs(
                         v9x_hsw_ps11_reg(V9X_HSW_PS11_G_PAYLOAD + 1u)));
    v9x_hsw_ps11_sel(g, V9X_HSW_EU_COND_GE, V9X_HSW_PS11_G_WORK,
                     v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK),
                     v9x_hsw_ps11_abs(
                         v9x_hsw_ps11_reg(V9X_HSW_PS11_G_PAYLOAD + 2u)));
    v9x_hsw_ps11_inv(g, V9X_HSW_PS11_G_WORK,
                     v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK));
    for (k = 0u; k < 3u; ++k) {
        v9x_hsw_ps11_mul(g, (v9x_u8)(V9X_HSW_PS11_G_PAYLOAD + k),
                         v9x_hsw_ps11_reg((v9x_u8)(V9X_HSW_PS11_G_PAYLOAD + k)),
                         v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK));
    }
}

static int v9x_hsw_ps11_is_cube(const V9X_HSW_PS11_GEN *g, v9x_u32 stage)
{
    return (g->desc->cube & (1ul << stage)) != 0ul;
}

/* Coordinate set stage's component c. */
static V9X_HSW_PS11_OP v9x_hsw_ps11_tc(v9x_u32 stage, v9x_u32 c)
{
    return v9x_hsw_ps11_reg((v9x_u8)(V9X_HSW_PS11_G_TC + 4ul * stage + c));
}

/* Stage's u and v into the payload, over w when the stage is projected. */
static void v9x_hsw_ps11_uv(V9X_HSW_PS11_GEN *g, v9x_u32 stage)
{
    if ((g->desc->projected & (1ul << stage)) != 0ul) {
        v9x_hsw_ps11_inv(g, V9X_HSW_PS11_G_WORK, v9x_hsw_ps11_tc(stage, 3ul));
        v9x_hsw_ps11_mul(g, V9X_HSW_PS11_G_PAYLOAD,
                         v9x_hsw_ps11_tc(stage, 0ul),
                         v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK));
        v9x_hsw_ps11_mul(g, (v9x_u8)(V9X_HSW_PS11_G_PAYLOAD + 1u),
                         v9x_hsw_ps11_tc(stage, 1ul),
                         v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK));
        return;
    }
    v9x_hsw_ps11_mov(g, V9X_HSW_PS11_G_PAYLOAD, v9x_hsw_ps11_tc(stage, 0ul),
                     0u);
    v9x_hsw_ps11_mov(g, (v9x_u8)(V9X_HSW_PS11_G_PAYLOAD + 1u),
                     v9x_hsw_ps11_tc(stage, 1ul), 0u);
}

static float v9x_hsw_ps11_clamp(float value)
{
    if (!(value > -1.0f)) {
        return -1.0f;
    }
    return value > 1.0f ? 1.0f : value;
}

/* The application's constant c_k component c, as the push block holds it. */
static V9X_HSW_PS11_OP v9x_hsw_ps11_const(const V9X_HSW_PS11_GEN *g,
                                          v9x_u32 k, v9x_u32 c)
{
    return v9x_hsw_ps11_scalar((v9x_u8)(g->desc->const_grf + k / 2ul),
                               4ul * (k % 2ul) + c);
}

/*
 * A shader source's channel c as an operand, its modifier applied. A
 * def constant is folded to an immediate here; anything else needing more
 * than a negate is computed into source temporary slot.
 */
static V9X_HSW_PS11_OP v9x_hsw_ps11_source(V9X_HSW_PS11_GEN *g,
                                           const V9X_PS11_SRC *src,
                                           v9x_u32 c, v9x_u32 slot)
{
    v9x_u32 lane = ((v9x_u32)src->swizzle >> (2ul * c)) & 3ul;
    v9x_u8 temp = (v9x_u8)(V9X_HSW_PS11_G_SOURCE + slot);
    V9X_HSW_PS11_OP base;

    switch (src->file) {
    case V9X_PS11_FILE_TEMP:
        base = v9x_hsw_ps11_reg(
            (v9x_u8)(V9X_HSW_PS11_G_R + 4ul * src->reg + lane));
        break;
    case V9X_PS11_FILE_INPUT:
        base = v9x_hsw_ps11_reg(
            (v9x_u8)(V9X_HSW_PS11_G_V + 4ul * src->reg + lane));
        break;
    case V9X_PS11_FILE_TEXTURE:
        base = v9x_hsw_ps11_reg(
            (v9x_u8)(V9X_HSW_PS11_G_T + 4ul * src->reg + lane));
        break;
    default:
        if ((g->program->def_mask & (1ul << src->reg)) != 0ul) {
            float x = v9x_hsw_ps11_clamp(
                v9x_hsw_ps11_value(g->program->def[src->reg][lane]));

            switch (src->mod) {
            case V9X_PS11_SRC_NEG:     x = -x; break;
            case V9X_PS11_SRC_BIAS:    x = x - 0.5f; break;
            case V9X_PS11_SRC_BIASNEG: x = 0.5f - x; break;
            case V9X_PS11_SRC_SIGN:    x = 2.0f * x - 1.0f; break;
            case V9X_PS11_SRC_SIGNNEG: x = 1.0f - 2.0f * x; break;
            case V9X_PS11_SRC_COMP:    x = 1.0f - x; break;
            default: break;
            }
            return v9x_hsw_ps11_imm(x);
        }
        base = v9x_hsw_ps11_const(g, src->reg, lane);
        break;
    }
    switch (src->mod) {
    case V9X_PS11_SRC_NEG:
        return v9x_hsw_ps11_neg(base);
    case V9X_PS11_SRC_BIAS:
        v9x_hsw_ps11_add(g, temp, base, v9x_hsw_ps11_imm(-0.5f));
        break;
    case V9X_PS11_SRC_BIASNEG:
        v9x_hsw_ps11_add(g, temp, v9x_hsw_ps11_neg(base),
                         v9x_hsw_ps11_imm(0.5f));
        break;
    case V9X_PS11_SRC_SIGN:
        v9x_hsw_ps11_mul(g, temp, base, v9x_hsw_ps11_imm(2.0f));
        v9x_hsw_ps11_add(g, temp, v9x_hsw_ps11_reg(temp),
                         v9x_hsw_ps11_imm(-1.0f));
        break;
    case V9X_PS11_SRC_SIGNNEG:
        v9x_hsw_ps11_mul(g, temp, base, v9x_hsw_ps11_imm(-2.0f));
        v9x_hsw_ps11_add(g, temp, v9x_hsw_ps11_reg(temp),
                         v9x_hsw_ps11_imm(1.0f));
        break;
    case V9X_PS11_SRC_COMP:
        v9x_hsw_ps11_add(g, temp, v9x_hsw_ps11_neg(base),
                         v9x_hsw_ps11_imm(1.0f));
        break;
    default:
        return base;
    }
    return v9x_hsw_ps11_reg(temp);
}

/* An arithmetic result in place: shifted, then saturated or held to
 * [-1, 1]. */
static void v9x_hsw_ps11_finish(V9X_HSW_PS11_GEN *g,
                                const V9X_PS11_INST *inst, v9x_u8 reg)
{
    V9X_HSW_PS11_OP r = v9x_hsw_ps11_reg(reg);

    if (inst->shift == 1l) {
        v9x_hsw_ps11_mul(g, reg, r, v9x_hsw_ps11_imm(2.0f));
    } else if (inst->shift == 2l) {
        v9x_hsw_ps11_mul(g, reg, r, v9x_hsw_ps11_imm(4.0f));
    } else if (inst->shift == -1l) {
        v9x_hsw_ps11_mul(g, reg, r, v9x_hsw_ps11_imm(0.5f));
    }
    if (inst->saturate != 0u) {
        v9x_hsw_ps11_mov(g, reg, r, 1u);
        return;
    }
    v9x_hsw_ps11_sel(g, V9X_HSW_EU_COND_GE, reg, r, v9x_hsw_ps11_imm(-1.0f));
    v9x_hsw_ps11_sel(g, V9X_HSW_EU_COND_L, reg, r, v9x_hsw_ps11_imm(1.0f));
}

/* One arithmetic instruction's written channels into result set `set`
 * (g80 + 4 set + channel). Nothing is written to a shader register. */
static void v9x_hsw_ps11_arithmetic(V9X_HSW_PS11_GEN *g,
                                    const V9X_PS11_INST *inst, v9x_u32 set)
{
    v9x_u8 base = (v9x_u8)(V9X_HSW_PS11_G_RESULT + 4ul * set);
    v9x_u32 c;

    if (inst->op == V9X_PS11_DP3) {
        for (c = 0ul; c < 3ul; ++c) {
            V9X_HSW_PS11_OP a = v9x_hsw_ps11_source(g, &inst->src[0], c, 0ul);
            V9X_HSW_PS11_OP b = v9x_hsw_ps11_source(g, &inst->src[1], c, 1ul);

            if (c == 0ul) {
                v9x_hsw_ps11_mul(g, V9X_HSW_PS11_G_WORK, a, b);
            } else {
                v9x_hsw_ps11_mul(g, V9X_HSW_PS11_G_WORK2, a, b);
                v9x_hsw_ps11_add(g, V9X_HSW_PS11_G_WORK,
                                 v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK),
                                 v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK2));
            }
        }
        v9x_hsw_ps11_finish(g, inst, V9X_HSW_PS11_G_WORK);
        for (c = 0ul; c < 4ul; ++c) {
            if ((inst->dst_mask & (1u << c)) != 0u) {
                v9x_hsw_ps11_mov(g, (v9x_u8)(base + c),
                                 v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK), 0u);
            }
        }
        return;
    }
    for (c = 0ul; c < 4ul; ++c) {
        v9x_u8 res = (v9x_u8)(base + c);
        V9X_HSW_PS11_OP a;
        V9X_HSW_PS11_OP b;
        V9X_HSW_PS11_OP d;

        if ((inst->dst_mask & (1u << c)) == 0u) {
            continue;
        }
        a = v9x_hsw_ps11_source(g, &inst->src[0], c, 0ul);
        b = inst->sources > 1u ? v9x_hsw_ps11_source(g, &inst->src[1], c, 1ul)
                               : a;
        d = inst->sources > 2u ? v9x_hsw_ps11_source(g, &inst->src[2], c, 2ul)
                               : a;
        switch (inst->op) {
        case V9X_PS11_ADD:
            v9x_hsw_ps11_add(g, res, a, b);
            break;
        case V9X_PS11_SUB:
            v9x_hsw_ps11_add(g, res, a, v9x_hsw_ps11_neg(b));
            break;
        case V9X_PS11_MUL:
            v9x_hsw_ps11_mul(g, res, a, b);
            break;
        case V9X_PS11_MAD:
            v9x_hsw_ps11_mul(g, V9X_HSW_PS11_G_WORK, a, b);
            v9x_hsw_ps11_add(g, res, v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK), d);
            break;
        case V9X_PS11_LRP:
            /* s0 s1 + (1 - s0) s2 = (s1 - s2) s0 + s2 */
            v9x_hsw_ps11_add(g, V9X_HSW_PS11_G_WORK, b, v9x_hsw_ps11_neg(d));
            v9x_hsw_ps11_mul(g, V9X_HSW_PS11_G_WORK,
                             v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK), a);
            v9x_hsw_ps11_add(g, res, v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK), d);
            break;
        case V9X_PS11_CND:
            v9x_hsw_ps11_cmp(g, V9X_HSW_EU_COND_G, V9X_HSW_PS11_FLAG_CND, a,
                             v9x_hsw_ps11_imm(0.5f));
            v9x_hsw_ps11_pick(g, V9X_HSW_PS11_FLAG_CND, res, b, d);
            break;
        default:
            v9x_hsw_ps11_mov(g, res, a, 0u);
            break;
        }
        v9x_hsw_ps11_finish(g, inst, res);
    }
}

/* Result set `set` into the instruction's destination, written channels
 * only. */
static void v9x_hsw_ps11_commit(V9X_HSW_PS11_GEN *g,
                                const V9X_PS11_INST *inst, v9x_u32 set)
{
    v9x_u8 dst = (v9x_u8)((inst->dst_file == V9X_PS11_FILE_TEMP
                               ? V9X_HSW_PS11_G_R : V9X_HSW_PS11_G_T) +
                          4ul * inst->dst_reg);
    v9x_u32 c;

    for (c = 0ul; c < 4ul; ++c) {
        if ((inst->dst_mask & (1u << c)) != 0u) {
            v9x_hsw_ps11_mov(g, (v9x_u8)(dst + c),
                             v9x_hsw_ps11_reg((v9x_u8)(V9X_HSW_PS11_G_RESULT +
                                                       4ul * set + c)),
                             0u);
        }
    }
}

/* A texm3x3 row: coordinate set stage's xyz . the source's rgb, into
 * dot register `row`. */
static void v9x_hsw_ps11_row(V9X_HSW_PS11_GEN *g, const V9X_PS11_INST *inst,
                             v9x_u32 row)
{
    v9x_u8 dot = (v9x_u8)(V9X_HSW_PS11_G_DOT + row);
    v9x_u32 c;

    for (c = 0ul; c < 3ul; ++c) {
        V9X_HSW_PS11_OP n = v9x_hsw_ps11_source(g, &inst->src[0], c, c);

        if (c == 0ul) {
            v9x_hsw_ps11_mul(g, dot, v9x_hsw_ps11_tc(inst->dst_reg, 0ul), n);
        } else {
            v9x_hsw_ps11_mul(g, V9X_HSW_PS11_G_WORK,
                             v9x_hsw_ps11_tc(inst->dst_reg, c), n);
            v9x_hsw_ps11_add(g, dot, v9x_hsw_ps11_reg(dot),
                             v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK));
        }
    }
}

/* The three-coordinate lookup on stage from the payload: a cube's
 * normalised first. */
static void v9x_hsw_ps11_lookup3(V9X_HSW_PS11_GEN *g, v9x_u32 stage)
{
    if (v9x_hsw_ps11_is_cube(g, stage)) {
        v9x_hsw_ps11_cube(g);
    }
    v9x_hsw_ps11_sample(g, stage, 3u);
}

static void v9x_hsw_ps11_texture(V9X_HSW_PS11_GEN *g,
                                 const V9X_PS11_INST *inst, v9x_u32 *pads,
                                 int *kills)
{
    v9x_u32 stage = inst->dst_reg;
    v9x_u8 t = (v9x_u8)(V9X_HSW_PS11_G_T + 4ul * stage);
    v9x_u8 k;

    switch (inst->op) {
    case V9X_PS11_TEX:
        if (v9x_hsw_ps11_is_cube(g, stage)) {
            for (k = 0u; k < 3u; ++k) {
                v9x_hsw_ps11_mov(g, (v9x_u8)(V9X_HSW_PS11_G_PAYLOAD + k),
                                 v9x_hsw_ps11_tc(stage, k), 0u);
            }
            v9x_hsw_ps11_lookup3(g, stage);
            return;
        }
        v9x_hsw_ps11_uv(g, stage);
        v9x_hsw_ps11_sample(g, stage, 2u);
        return;

    case V9X_PS11_TEXCOORD:
        for (k = 0u; k < 3u; ++k) {
            v9x_hsw_ps11_mov(g, (v9x_u8)(t + k), v9x_hsw_ps11_tc(stage, k),
                             1u);
        }
        v9x_hsw_ps11_mov(g, (v9x_u8)(t + 3u), v9x_hsw_ps11_imm(1.0f), 0u);
        return;

    case V9X_PS11_TEXKILL:
        /* The smallest of x, y and z, kept across every texkill; the
         * write at the end takes the pixels where it is not negative. */
        v9x_hsw_ps11_sel(g, V9X_HSW_EU_COND_L, V9X_HSW_PS11_G_WORK,
                         v9x_hsw_ps11_tc(stage, 0ul),
                         v9x_hsw_ps11_tc(stage, 1ul));
        v9x_hsw_ps11_sel(g, V9X_HSW_EU_COND_L,
                         *kills ? V9X_HSW_PS11_G_WORK : V9X_HSW_PS11_G_KILL,
                         v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK),
                         v9x_hsw_ps11_tc(stage, 2ul));
        if (*kills) {
            v9x_hsw_ps11_sel(g, V9X_HSW_EU_COND_L, V9X_HSW_PS11_G_KILL,
                             v9x_hsw_ps11_reg(V9X_HSW_PS11_G_KILL),
                             v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK));
        }
        *kills = 1;
        return;

    case V9X_PS11_TEXBEM:
    case V9X_PS11_TEXBEML:
    {
        /* u + m00 du + m10 dv, v + m01 du + m11 dv, from stage's own
         * matrix (push block registers 4 and 5). */
        v9x_u8 bump = (v9x_u8)(g->desc->const_grf + V9X_HSW_PS11_CONST_BUMP +
                               stage / 2ul);
        v9x_u32 at = 4ul * (stage % 2ul);
        V9X_HSW_PS11_OP du = v9x_hsw_ps11_source(g, &inst->src[0], 0ul, 0ul);
        V9X_HSW_PS11_OP dv = v9x_hsw_ps11_source(g, &inst->src[0], 1ul, 1ul);
        v9x_u8 u = V9X_HSW_PS11_G_PAYLOAD;
        v9x_u8 v = (v9x_u8)(V9X_HSW_PS11_G_PAYLOAD + 1u);

        v9x_hsw_ps11_uv(g, stage);
        v9x_hsw_ps11_mul(g, V9X_HSW_PS11_G_WORK, du,
                         v9x_hsw_ps11_scalar(bump, at + 0ul));
        v9x_hsw_ps11_add(g, u, v9x_hsw_ps11_reg(u),
                         v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK));
        v9x_hsw_ps11_mul(g, V9X_HSW_PS11_G_WORK, dv,
                         v9x_hsw_ps11_scalar(bump, at + 2ul));
        v9x_hsw_ps11_add(g, u, v9x_hsw_ps11_reg(u),
                         v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK));
        v9x_hsw_ps11_mul(g, V9X_HSW_PS11_G_WORK, du,
                         v9x_hsw_ps11_scalar(bump, at + 1ul));
        v9x_hsw_ps11_add(g, v, v9x_hsw_ps11_reg(v),
                         v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK));
        v9x_hsw_ps11_mul(g, V9X_HSW_PS11_G_WORK, dv,
                         v9x_hsw_ps11_scalar(bump, at + 3ul));
        v9x_hsw_ps11_add(g, v, v9x_hsw_ps11_reg(v),
                         v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK));
        v9x_hsw_ps11_sample(g, stage, 2u);
        if (inst->op == V9X_PS11_TEXBEML) {
            /* The colour times (blue scale + offset), push block
             * register 6. */
            v9x_u8 lum = (v9x_u8)(g->desc->const_grf +
                                  V9X_HSW_PS11_CONST_LUMINANCE);
            V9X_HSW_PS11_OP blue =
                v9x_hsw_ps11_source(g, &inst->src[0], 2ul, 2ul);

            v9x_hsw_ps11_mul(g, V9X_HSW_PS11_G_WORK, blue,
                             v9x_hsw_ps11_scalar(lum, 2ul * stage));
            v9x_hsw_ps11_add(g, V9X_HSW_PS11_G_WORK,
                             v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK),
                             v9x_hsw_ps11_scalar(lum, 2ul * stage + 1ul));
            for (k = 0u; k < 3u; ++k) {
                v9x_hsw_ps11_mul(g, (v9x_u8)(t + k),
                                 v9x_hsw_ps11_reg((v9x_u8)(t + k)),
                                 v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK));
            }
        }
        return;
    }

    case V9X_PS11_TEXM3X3PAD:
        v9x_hsw_ps11_row(g, inst, *pads);
        ++*pads;
        return;

    default:
    {
        V9X_HSW_PS11_OP e[3];

        v9x_hsw_ps11_row(g, inst, 2ul);
        *pads = 0ul;
        if (inst->op == V9X_PS11_TEXM3X3TEX) {
            for (k = 0u; k < 3u; ++k) {
                v9x_hsw_ps11_mov(g, (v9x_u8)(V9X_HSW_PS11_G_PAYLOAD + k),
                                 v9x_hsw_ps11_reg(
                                     (v9x_u8)(V9X_HSW_PS11_G_DOT + k)),
                                 0u);
            }
            v9x_hsw_ps11_lookup3(g, stage);
            return;
        }
        /* The eye vector: the three rows' w, or the constant. */
        for (k = 0u; k < 3u; ++k) {
            e[k] = inst->op == V9X_PS11_TEXM3X3VSPEC
                       ? v9x_hsw_ps11_tc(stage - 2ul + k, 3ul)
                       : v9x_hsw_ps11_source(g, &inst->src[1], k, k);
        }
        /* R = 2 (N.E) / (N.N) N - E */
        for (k = 0u; k < 3u; ++k) {
            V9X_HSW_PS11_OP n =
                v9x_hsw_ps11_reg((v9x_u8)(V9X_HSW_PS11_G_DOT + k));

            if (k == 0u) {
                v9x_hsw_ps11_mul(g, V9X_HSW_PS11_G_WORK, n, e[k]);
                v9x_hsw_ps11_mul(g, V9X_HSW_PS11_G_WORK2, n, n);
            } else {
                v9x_hsw_ps11_mul(g, V9X_HSW_PS11_G_WORK3, n, e[k]);
                v9x_hsw_ps11_add(g, V9X_HSW_PS11_G_WORK,
                                 v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK),
                                 v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK3));
                v9x_hsw_ps11_mul(g, V9X_HSW_PS11_G_WORK3, n, n);
                v9x_hsw_ps11_add(g, V9X_HSW_PS11_G_WORK2,
                                 v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK2),
                                 v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK3));
            }
        }
        v9x_hsw_ps11_inv(g, V9X_HSW_PS11_G_WORK2,
                         v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK2));
        v9x_hsw_ps11_mul(g, V9X_HSW_PS11_G_WORK,
                         v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK),
                         v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK2));
        v9x_hsw_ps11_mul(g, V9X_HSW_PS11_G_WORK,
                         v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK),
                         v9x_hsw_ps11_imm(2.0f));
        for (k = 0u; k < 3u; ++k) {
            v9x_u8 p = (v9x_u8)(V9X_HSW_PS11_G_PAYLOAD + k);

            v9x_hsw_ps11_mul(g, p,
                             v9x_hsw_ps11_reg((v9x_u8)(V9X_HSW_PS11_G_DOT + k)),
                             v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK));
            v9x_hsw_ps11_add(g, p, v9x_hsw_ps11_reg(p),
                             v9x_hsw_ps11_neg(e[k]));
        }
        v9x_hsw_ps11_lookup3(g, stage);
        return;
    }
    }
}

/* send(8) the render target write from g112-g115, EOT; under the kill
 * flag when the shader has texkill. As hsw_eu.c's v9x_hsw_ps_rt_write. */
static void v9x_hsw_ps11_write(V9X_HSW_PS11_GEN *g, int kill)
{
    struct v9x_hsw_eu_inst inst;
    v9x_u32 function;
    v9x_u32 desc;

    if (v9x_intel_hsw_eu_rt_write_fc(1u, 0u, V9X_HSW_RT_SIMD8_LO, 0u,
                                     &function) != V9X_STATUS_OK ||
        v9x_intel_hsw_eu_send_desc(4u, 0u, 0u, function, &desc) !=
            V9X_STATUS_OK) {
        g->failed = 1;
        return;
    }
    v9x_hsw_ps11_inst(&inst, V9X_HSW_EU_OP_SEND, 0u);
    inst.sfid = V9X_HSW_SFID_DP_RC;
    inst.eot = 1u;
    inst.desc = desc;
    inst.dst = v9x_hsw_ps11_null();
    inst.dst.type = V9X_HSW_EU_TYPE_UD;
    inst.src0 = v9x_hsw_ps11_reg(V9X_HSW_PS11_G_OUT);
    inst.src0.type = V9X_HSW_EU_TYPE_UD;
    if (kill) {
        inst.pred_ctrl = 1u;
        inst.flag_subreg = V9X_HSW_PS11_FLAG_KILL;
    }
    v9x_hsw_ps11_emit(g, &inst);
}

void v9x_intel_hsw_ps11_constants(const V9X_PS11_ENV *env,
                                  v9x_u32 out[V9X_HSW_PS11_CONST_DWORDS])
{
    v9x_u32 k;
    v9x_u32 c;
    float value;

    for (k = 0ul; k < V9X_HSW_PS11_CONST_DWORDS; ++k) {
        out[k] = 0ul;
    }
    /* Clamped where the kernels read them (ddi8-on-win9x.md section 6). */
    for (k = 0ul; k < 8ul; ++k) {
        for (c = 0ul; c < 4ul; ++c) {
            value = env->c[k][c];
            value = value > 1.0f ? 1.0f : value < -1.0f ? -1.0f : value;
            out[(k / 2ul) * 8ul + 4ul * (k % 2ul) + c] =
                v9x_hsw_ps11_bits(value);
        }
    }
    for (k = 0ul; k < 4ul; ++k) {
        for (c = 0ul; c < 4ul; ++c) {
            out[((v9x_u32)V9X_HSW_PS11_CONST_BUMP + k / 2ul) * 8ul +
                4ul * (k % 2ul) + c] = v9x_hsw_ps11_bits(env->bump[k][c]);
        }
        out[(v9x_u32)V9X_HSW_PS11_CONST_LUMINANCE * 8ul + 2ul * k] =
            v9x_hsw_ps11_bits(env->luminance[k][0]);
        out[(v9x_u32)V9X_HSW_PS11_CONST_LUMINANCE * 8ul + 2ul * k + 1ul] =
            v9x_hsw_ps11_bits(env->luminance[k][1]);
    }
}

v9x_status v9x_intel_hsw_ps11_build(v9x_u32 *out, v9x_u32 capacity,
                                    const V9X_PS11_PROGRAM *program,
                                    const struct v9x_hsw_ps11_desc *desc,
                                    v9x_u32 *used)
{
    V9X_HSW_PS11_GEN g;
    v9x_u32 pads = 0ul;
    int kills = 0;
    v9x_u32 i;
    v9x_u32 c;

    if (out == 0 || program == 0 || desc == 0 || used == 0 ||
        desc->const_grf <= V9X_HSW_PS11_G_BARY + 1u ||
        (v9x_u32)desc->const_grf + V9X_HSW_PS11_CONST_GRFS >
            (v9x_u32)desc->setup_grf ||
        (v9x_u32)desc->setup_grf + 2ul * V9X_HSW_PS11_ATTRIBUTES >
            (v9x_u32)V9X_HSW_PS11_G_R) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    g.out = out;
    g.capacity = capacity;
    g.used = 0ul;
    g.failed = 0;
    g.program = program;
    g.desc = desc;

    /* r0 starts at zero, so a channel the shader leaves is defined. */
    for (c = 0ul; c < 4ul; ++c) {
        v9x_hsw_ps11_mov(&g, (v9x_u8)(V9X_HSW_PS11_G_R + c),
                         v9x_hsw_ps11_imm(0.0f), 0u);
    }
    /* The inputs and coordinate sets the shader reads, interpolated. */
    for (i = 0ul; i < 2ul; ++i) {
        if ((program->inputs & (1ul << i)) == 0ul) {
            continue;
        }
        for (c = 0ul; c < 4ul; ++c) {
            v9x_hsw_ps11_pln(&g, (v9x_u8)(V9X_HSW_PS11_G_V + 4ul * i + c), i,
                             c);
        }
    }
    for (i = 0ul; i < 4ul; ++i) {
        if ((program->coords & (1ul << i)) == 0ul) {
            continue;
        }
        for (c = 0ul; c < 4ul; ++c) {
            v9x_hsw_ps11_pln(&g, (v9x_u8)(V9X_HSW_PS11_G_TC + 4ul * i + c),
                             V9X_HSW_PS11_ATTR_TEXCOORD + i, c);
        }
    }

    for (i = 0ul; i < program->count; ++i) {
        const V9X_PS11_INST *inst = &program->inst[i];

        if (inst->op == V9X_PS11_NOP) {
            continue;
        }
        if (V9X_PS11_IS_TEXTURE(inst->op)) {
            v9x_hsw_ps11_texture(&g, inst, &pads, &kills);
            continue;
        }
        /* A co-issued pair reads its sources before either writes. */
        v9x_hsw_ps11_arithmetic(&g, inst, 0ul);
        if (i + 1ul < program->count && program->inst[i + 1ul].coissue) {
            v9x_hsw_ps11_arithmetic(&g, &program->inst[i + 1ul], 1ul);
            v9x_hsw_ps11_commit(&g, inst, 0ul);
            v9x_hsw_ps11_commit(&g, &program->inst[i + 1ul], 1ul);
            ++i;
            continue;
        }
        v9x_hsw_ps11_commit(&g, inst, 0ul);
    }

    /* Fog blends the clamped result, so it is saturated first; without fog
     * the render target's own clamp does the same. */
    for (c = 0ul; c < 4ul; ++c) {
        v9x_hsw_ps11_mov(&g, (v9x_u8)(V9X_HSW_PS11_G_OUT + c),
                         v9x_hsw_ps11_reg((v9x_u8)(V9X_HSW_PS11_G_R + c)),
                         desc->fog ? 1u : 0u);
    }
    /* Fog: colour = (colour - fog) f + fog, f the specular alpha. */
    if (desc->fog) {
        v9x_hsw_ps11_pln(&g, V9X_HSW_PS11_G_FOG, 1ul, 3ul);
        for (c = 0ul; c < 3ul; ++c) {
            float fog = v9x_hsw_ps11_value(desc->fog_rgb[c]);

            v9x_hsw_ps11_add(&g, V9X_HSW_PS11_G_WORK,
                             v9x_hsw_ps11_reg(
                                 (v9x_u8)(V9X_HSW_PS11_G_OUT + c)),
                             v9x_hsw_ps11_imm(-fog));
            v9x_hsw_ps11_mul(&g, V9X_HSW_PS11_G_WORK,
                             v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK),
                             v9x_hsw_ps11_reg(V9X_HSW_PS11_G_FOG));
            v9x_hsw_ps11_add(&g, (v9x_u8)(V9X_HSW_PS11_G_OUT + c),
                             v9x_hsw_ps11_reg(V9X_HSW_PS11_G_WORK),
                             v9x_hsw_ps11_imm(fog));
        }
    }
    if (kills) {
        v9x_hsw_ps11_cmp(&g, V9X_HSW_EU_COND_GE, V9X_HSW_PS11_FLAG_KILL,
                         v9x_hsw_ps11_reg(V9X_HSW_PS11_G_KILL),
                         v9x_hsw_ps11_imm(0.0f));
    }
    v9x_hsw_ps11_write(&g, kills);
    if (g.failed) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *used = g.used;
    return V9X_STATUS_OK;
}
