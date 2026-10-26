/*
 * Haswell vertex kernels from a T&L key (include\velocity9x\intel_hsw_vs.h).
 *
 * The kernel follows the reference evaluator's order (r3d_tnl_ref.c) so a
 * mismatch on the guest can be located the same way, with one regrouping:
 * the per-light sums are kept apart and multiplied by the material terms
 * once at the end,
 *
 *   colour 0 = emissive + ambient (scene + sum F La) + diffuse (sum F NL Ld)
 *   colour 1 = specular (sum F P Ls)
 *
 * which is the reference's sum by distributivity, a few roundings apart.
 * The ambient sum does not depend on the normal, so a two-sided key shares
 * it between the faces.
 *
 * Where the reference skips a light (out of range, a denominator not above
 * zero, N.L not above zero) the kernel computes the term and selects zero
 * by predicate, so the kernel stays straight-line: every value a skipped
 * light produces, NaN included, is replaced before it is summed.
 *
 * Built twice: the first pass finds which constants the kernel reads, which
 * fixes where the inputs start (the push constants come first in the
 * payload, Vol 7 p478-480); the second writes the words with that layout.
 * Both passes take the same path, so they produce the same constant map.
 */
#include "velocity9x/intel_hsw_vs.h"
#include "velocity9x/intel_hsw_eu.h"

/* Swizzles, ChanSel x y z w in 1:0, 3:2, 5:4, 7:6. */
#define V9X_HSW_VSG_XXXX ((v9x_u8)0x00u)
#define V9X_HSW_VSG_YYYY ((v9x_u8)0x55u)
#define V9X_HSW_VSG_ZZZZ ((v9x_u8)0xaau)
#define V9X_HSW_VSG_WWWW ((v9x_u8)0xffu)
#define V9X_HSW_VSG_XYZW V9X_HSW_EU_SWIZZLE_XYZW

/* Writemasks. */
#define V9X_HSW_VSG_M_X    ((v9x_u8)0x1u)
#define V9X_HSW_VSG_M_W    ((v9x_u8)0x8u)
#define V9X_HSW_VSG_M_XYZ  ((v9x_u8)0x7u)
#define V9X_HSW_VSG_M_XYZW ((v9x_u8)0xfu)

/* Float bit patterns. TINY is 2^-100: a squared length is raised to it
 * before RSQ, so a zero vector scales to zero instead of 0 x infinity. */
#define V9X_HSW_VSG_ZERO         ((v9x_u32)0x00000000ul)
#define V9X_HSW_VSG_ONE          ((v9x_u32)0x3f800000ul)
#define V9X_HSW_VSG_TINY         ((v9x_u32)0x0d800000ul)
#define V9X_HSW_VSG_MINUS_LOG2_E ((v9x_u32)0xbfb8aa3bul) /* -1.442695 */

/* The output registers (intel_hsw_vs.h). */
#define V9X_HSW_VSG_URB_HEADER ((v9x_u8)112u)
#define V9X_HSW_VSG_VUE_HEADER ((v9x_u8)113u)
#define V9X_HSW_VSG_POSITION   ((v9x_u8)114u)
#define V9X_HSW_VSG_ATTRIBUTES ((v9x_u8)115u)
/* g0 dword 5, where the URB write header's channel masks live. */
#define V9X_HSW_VSG_MASK_BYTE     ((v9x_u8)20u)
#define V9X_HSW_VSG_CHANNEL_MASKS ((v9x_u32)0x0000ff00ul)

/* Input offsets from the first input register. */
#define V9X_HSW_VSG_IN_POSITION 0u
#define V9X_HSW_VSG_IN_COLOR0   1u
#define V9X_HSW_VSG_IN_COLOR1   2u
#define V9X_HSW_VSG_IN_TEXTURE0 3u
#define V9X_HSW_VSG_IN_NORMAL   5u

/* Temporaries, as offsets from the first one after the inputs. */
#define V9X_HSW_VSG_T_EYE     0u
#define V9X_HSW_VSG_T_NORMAL  1u
#define V9X_HSW_VSG_T_VIEW    2u
#define V9X_HSW_VSG_T_AMBIENT 3u
#define V9X_HSW_VSG_T_DIFFUSE 4u  /* and 5 for the back face */
#define V9X_HSW_VSG_T_SPECULAR 6u /* and 7 for the back face */
#define V9X_HSW_VSG_T_L       8u
#define V9X_HSW_VSG_T_FACTOR  9u
#define V9X_HSW_VSG_T_NL      10u
#define V9X_HSW_VSG_T_HALF    11u
#define V9X_HSW_VSG_T_SCRATCH 12u /* 12-16 */
#define V9X_HSW_VSG_TEMPS     17u

#define V9X_HSW_VSG_UNUSED ((v9x_u8)0xffu)

struct v9x_hsw_vsg {
    v9x_u32 *out;
    v9x_u32 capacity;
    v9x_u32 used;
    int failed;
    const V9X_R3D_TNL_KEY *key;
    v9x_u8 input;           /* first input GRF */
    v9x_u8 temp;            /* first temporary GRF */
    v9x_u8 count;           /* vec4s in map */
    v9x_u8 map[V9X_R3D_TNL_VEC4S];
    v9x_u8 slot[V9X_R3D_TNL_VEC4S];
};

typedef struct v9x_hsw_eu_operand V9X_HSW_VSG_OP;

static void v9x_hsw_vsg_zero(void *object, v9x_u32 bytes)
{
    v9x_u8 *p = (v9x_u8 *)object;
    v9x_u32 index;

    for (index = 0ul; index < bytes; ++index) {
        p[index] = 0u;
    }
}

/* Encode in a local and store four whole words, so the destination is
 * never read: it may be the uncached aperture. */
static void v9x_hsw_vsg_emit(struct v9x_hsw_vsg *g,
                             const struct v9x_hsw_eu_inst *inst)
{
    v9x_u32 words[4];
    v9x_u32 index;

    if (g->failed) {
        return;
    }
    if (v9x_intel_hsw_eu_encode(inst, words) != V9X_STATUS_OK) {
        g->failed = 1;
        return;
    }
    if (g->out != 0) {
        if (g->capacity - g->used < 4ul || g->used > g->capacity) {
            g->failed = 2;
            return;
        }
        for (index = 0ul; index < 4ul; ++index) {
            g->out[g->used + index] = words[index];
        }
    }
    g->used += 4ul;
}

/* A vertex's vec4 in an Align16 register: <4;4,1>, the two vertices in
 * the two halves. */
static V9X_HSW_VSG_OP v9x_hsw_vsg_reg(v9x_u8 nr, v9x_u8 swizzle)
{
    V9X_HSW_VSG_OP op;

    v9x_hsw_vsg_zero(&op, sizeof(op));
    op.file = V9X_HSW_EU_FILE_GRF;
    op.type = V9X_HSW_EU_TYPE_F;
    op.nr = nr;
    op.vstride = V9X_HSW_EU_VSTRIDE_4;
    op.chan = swizzle;
    return op;
}

static V9X_HSW_VSG_OP v9x_hsw_vsg_temp(const struct v9x_hsw_vsg *g,
                                       v9x_u32 which, v9x_u8 swizzle)
{
    return v9x_hsw_vsg_reg((v9x_u8)(g->temp + which), swizzle);
}

static V9X_HSW_VSG_OP v9x_hsw_vsg_input(const struct v9x_hsw_vsg *g,
                                        v9x_u32 which, v9x_u8 swizzle)
{
    return v9x_hsw_vsg_reg((v9x_u8)(g->input + which), swizzle);
}

static V9X_HSW_VSG_OP v9x_hsw_vsg_neg(V9X_HSW_VSG_OP op)
{
    op.mod = V9X_HSW_EU_MOD_NEG;
    return op;
}

static V9X_HSW_VSG_OP v9x_hsw_vsg_abs(V9X_HSW_VSG_OP op)
{
    op.mod = V9X_HSW_EU_MOD_ABS;
    return op;
}

/* A constant vec4, pushed on first use; <0;4,1>, the same vec4 for both
 * vertices. */
static V9X_HSW_VSG_OP v9x_hsw_vsg_const(struct v9x_hsw_vsg *g, v9x_u32 vec4,
                                        v9x_u8 swizzle)
{
    V9X_HSW_VSG_OP op;
    v9x_u8 slot;

    if (g->slot[vec4] == V9X_HSW_VSG_UNUSED) {
        g->slot[vec4] = g->count;
        g->map[g->count] = (v9x_u8)vec4;
        ++g->count;
    }
    slot = g->slot[vec4];
    op = v9x_hsw_vsg_reg((v9x_u8)(1u + slot / 2u), swizzle);
    op.subnr = (v9x_u8)((slot % 2u) * 16u);
    op.vstride = V9X_HSW_EU_VSTRIDE_0;
    return op;
}

static V9X_HSW_VSG_OP v9x_hsw_vsg_imm(v9x_u32 bits)
{
    V9X_HSW_VSG_OP op;

    v9x_hsw_vsg_zero(&op, sizeof(op));
    op.file = V9X_HSW_EU_FILE_IMM;
    op.type = V9X_HSW_EU_TYPE_F;
    op.imm = bits;
    return op;
}

/* The null register as a source or destination. */
static V9X_HSW_VSG_OP v9x_hsw_vsg_null(void)
{
    V9X_HSW_VSG_OP op;

    v9x_hsw_vsg_zero(&op, sizeof(op));
    op.file = V9X_HSW_EU_FILE_ARF;
    op.type = V9X_HSW_EU_TYPE_F;
    op.vstride = V9X_HSW_EU_VSTRIDE_4;
    op.chan = V9X_HSW_VSG_M_XYZW;
    return op;
}

static void v9x_hsw_vsg_inst(struct v9x_hsw_eu_inst *inst, v9x_u8 opcode,
                             v9x_u8 dst, v9x_u8 mask)
{
    v9x_hsw_vsg_zero(inst, sizeof(*inst));
    inst->opcode = opcode;
    inst->exec_size = V9X_HSW_EU_EXEC_8;
    inst->align16 = 1u;
    inst->dst = v9x_hsw_vsg_reg(dst, mask);
}

/* An Align16 operation: dst.mask = src0 op src1; MOV ignores src1. */
static void v9x_hsw_vsg_op(struct v9x_hsw_vsg *g, v9x_u8 opcode, v9x_u8 dst,
                           v9x_u8 mask, V9X_HSW_VSG_OP src0,
                           V9X_HSW_VSG_OP src1, v9x_u8 saturate)
{
    struct v9x_hsw_eu_inst inst;

    v9x_hsw_vsg_inst(&inst, opcode, dst, mask);
    inst.saturate = saturate;
    inst.src0 = src0;
    if (opcode != V9X_HSW_EU_OP_MOV) {
        inst.src1 = src1;
    }
    v9x_hsw_vsg_emit(g, &inst);
}

static void v9x_hsw_vsg_mov(struct v9x_hsw_vsg *g, v9x_u8 dst, v9x_u8 mask,
                            V9X_HSW_VSG_OP src, v9x_u8 saturate)
{
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_MOV, dst, mask, src, src, saturate);
}

/* MATH: the function in the conditional-modifier field (Vol 2a p22). A
 * one-source function's src1 is null. */
static void v9x_hsw_vsg_math(struct v9x_hsw_vsg *g, v9x_u8 function,
                             v9x_u8 dst, V9X_HSW_VSG_OP src0,
                             V9X_HSW_VSG_OP src1)
{
    struct v9x_hsw_eu_inst inst;

    v9x_hsw_vsg_inst(&inst, V9X_HSW_EU_OP_MATH, dst, V9X_HSW_VSG_M_XYZW);
    inst.cond_mod = function;
    inst.src0 = src0;
    inst.src1 = src1;
    v9x_hsw_vsg_emit(g, &inst);
}

/* cmp.cond f0.0 null src0 src1: the flag per channel. */
static void v9x_hsw_vsg_cmp(struct v9x_hsw_vsg *g, v9x_u8 cond,
                            V9X_HSW_VSG_OP src0, V9X_HSW_VSG_OP src1)
{
    struct v9x_hsw_eu_inst inst;

    v9x_hsw_vsg_inst(&inst, V9X_HSW_EU_OP_CMP, 0u, V9X_HSW_VSG_M_XYZW);
    inst.dst = v9x_hsw_vsg_null();
    inst.cond_mod = cond;
    inst.src0 = src0;
    inst.src1 = src1;
    v9x_hsw_vsg_emit(g, &inst);
}

/* (+f0.0) sel dst src0 src1: src0 where the flag is set, src1 elsewhere;
 * inverted, the other way round (Vol 7 sel, PredCtrl Normal). */
static void v9x_hsw_vsg_sel(struct v9x_hsw_vsg *g, v9x_u8 dst, v9x_u8 mask,
                            V9X_HSW_VSG_OP src0, V9X_HSW_VSG_OP src1,
                            v9x_u8 invert)
{
    struct v9x_hsw_eu_inst inst;

    v9x_hsw_vsg_inst(&inst, V9X_HSW_EU_OP_SEL, dst, mask);
    inst.pred_ctrl = 1u;
    inst.pred_inv = invert;
    inst.src0 = src0;
    inst.src1 = src1;
    v9x_hsw_vsg_emit(g, &inst);
}

/* dst = src where cond(test, limit) holds, zero elsewhere. */
static void v9x_hsw_vsg_keep_if(struct v9x_hsw_vsg *g, v9x_u8 dst,
                                v9x_u8 cond, V9X_HSW_VSG_OP test,
                                V9X_HSW_VSG_OP limit)
{
    v9x_hsw_vsg_cmp(g, cond, test, limit);
    v9x_hsw_vsg_sel(g, dst, V9X_HSW_VSG_M_XYZW,
                    v9x_hsw_vsg_reg(dst, V9X_HSW_VSG_XYZW),
                    v9x_hsw_vsg_imm(V9X_HSW_VSG_ZERO), 0u);
}

/* sel.ge dst src imm: max(src, imm). */
static void v9x_hsw_vsg_max(struct v9x_hsw_vsg *g, v9x_u8 dst,
                            V9X_HSW_VSG_OP src, v9x_u32 bits)
{
    struct v9x_hsw_eu_inst inst;

    v9x_hsw_vsg_inst(&inst, V9X_HSW_EU_OP_SEL, dst, V9X_HSW_VSG_M_XYZW);
    inst.cond_mod = V9X_HSW_EU_COND_GE;
    inst.src0 = src;
    inst.src1 = v9x_hsw_vsg_imm(bits);
    v9x_hsw_vsg_emit(g, &inst);
}

/* vec.xyz scaled to unit length in place; a zero vector stays zero.
 * scratch receives the reciprocal length. */
static void v9x_hsw_vsg_normalize(struct v9x_hsw_vsg *g, v9x_u8 vec,
                                  v9x_u8 scratch)
{
    V9X_HSW_VSG_OP v = v9x_hsw_vsg_reg(vec, V9X_HSW_VSG_XYZW);
    V9X_HSW_VSG_OP s = v9x_hsw_vsg_reg(scratch, V9X_HSW_VSG_XYZW);

    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_DP3, scratch, V9X_HSW_VSG_M_XYZW, v, v, 0u);
    v9x_hsw_vsg_max(g, scratch, s, V9X_HSW_VSG_TINY);
    v9x_hsw_vsg_math(g, V9X_HSW_EU_MATH_RSQ, scratch, s, v9x_hsw_vsg_null());
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_MUL, vec, V9X_HSW_VSG_M_XYZ, v, s, 0u);
}

/* The URB write header, the VUE header slot and the end are step 3's
 * (hsw_eu.c, v9x_intel_hsw_vs_kernel_position): Align1. */
static void v9x_hsw_vsg_header(struct v9x_hsw_vsg *g)
{
    struct v9x_hsw_eu_inst inst;

    v9x_hsw_vsg_zero(&inst, sizeof(inst));
    inst.opcode = V9X_HSW_EU_OP_MOV;
    inst.exec_size = V9X_HSW_EU_EXEC_8;
    inst.mask_disable = 1u;
    inst.dst.file = V9X_HSW_EU_FILE_GRF;
    inst.dst.type = V9X_HSW_EU_TYPE_UD;
    inst.dst.nr = V9X_HSW_VSG_URB_HEADER;
    inst.dst.hstride = V9X_HSW_EU_HSTRIDE_1;
    inst.src0.file = V9X_HSW_EU_FILE_GRF;
    inst.src0.type = V9X_HSW_EU_TYPE_UD;
    inst.src0.vstride = V9X_HSW_EU_VSTRIDE_8;
    inst.src0.width = V9X_HSW_EU_WIDTH_8;
    inst.src0.hstride = V9X_HSW_EU_HSTRIDE_1;
    v9x_hsw_vsg_emit(g, &inst);

    v9x_hsw_vsg_zero(&inst, sizeof(inst));
    inst.opcode = V9X_HSW_EU_OP_OR;
    inst.exec_size = V9X_HSW_EU_EXEC_1;
    inst.mask_disable = 1u;
    inst.dst.file = V9X_HSW_EU_FILE_GRF;
    inst.dst.type = V9X_HSW_EU_TYPE_UD;
    inst.dst.nr = V9X_HSW_VSG_URB_HEADER;
    inst.dst.subnr = V9X_HSW_VSG_MASK_BYTE;
    inst.dst.hstride = V9X_HSW_EU_HSTRIDE_1;
    inst.src0.file = V9X_HSW_EU_FILE_GRF;
    inst.src0.type = V9X_HSW_EU_TYPE_UD;
    inst.src0.subnr = V9X_HSW_VSG_MASK_BYTE;
    inst.src1.file = V9X_HSW_EU_FILE_IMM;
    inst.src1.type = V9X_HSW_EU_TYPE_UD;
    inst.src1.imm = V9X_HSW_VSG_CHANNEL_MASKS;
    v9x_hsw_vsg_emit(g, &inst);

    v9x_hsw_vsg_mov(g, V9X_HSW_VSG_VUE_HEADER, V9X_HSW_VSG_M_XYZW,
                    v9x_hsw_vsg_imm(V9X_HSW_VSG_ZERO), 0u);
}

static void v9x_hsw_vsg_end(struct v9x_hsw_vsg *g, v9x_u8 outputs)
{
    struct v9x_hsw_eu_inst inst;
    v9x_u32 function;
    v9x_u32 desc;

    if (v9x_intel_hsw_eu_urb_write_fc(0ul, 1u, 1u, &function) !=
            V9X_STATUS_OK ||
        v9x_intel_hsw_eu_send_desc((v9x_u8)(1u + outputs), 0u, 1u, function,
                                   &desc) != V9X_STATUS_OK) {
        g->failed = 1;
        return;
    }
    v9x_hsw_vsg_zero(&inst, sizeof(inst));
    inst.opcode = V9X_HSW_EU_OP_SEND;
    inst.exec_size = V9X_HSW_EU_EXEC_8;
    inst.sfid = V9X_HSW_SFID_URB;
    inst.eot = 1u;
    inst.desc = desc;
    inst.dst.file = V9X_HSW_EU_FILE_ARF;
    inst.dst.type = V9X_HSW_EU_TYPE_UD;
    inst.dst.hstride = V9X_HSW_EU_HSTRIDE_1;
    inst.src0.file = V9X_HSW_EU_FILE_GRF;
    inst.src0.type = V9X_HSW_EU_TYPE_UD;
    inst.src0.nr = V9X_HSW_VSG_URB_HEADER;
    inst.src0.vstride = V9X_HSW_EU_VSTRIDE_8;
    inst.src0.width = V9X_HSW_EU_WIDTH_8;
    inst.src0.hstride = V9X_HSW_EU_HSTRIDE_1;
    v9x_hsw_vsg_emit(g, &inst);
}

/* dst.mask = the four rows of a constant matrix (vec4 index first) against
 * src, one DP4 a row. */
static void v9x_hsw_vsg_transform(struct v9x_hsw_vsg *g, v9x_u8 dst,
                                  v9x_u32 first, v9x_u32 rows,
                                  V9X_HSW_VSG_OP src, v9x_u8 opcode)
{
    v9x_u32 row;

    for (row = 0ul; row < rows; ++row) {
        v9x_hsw_vsg_op(g, opcode, dst, (v9x_u8)(V9X_HSW_VSG_M_X << row), src,
                       v9x_hsw_vsg_const(g, first + row, V9X_HSW_VSG_XYZW),
                       0u);
    }
}

/* A vertex colour: its input, or the block's default when the vertex has
 * none. */
static V9X_HSW_VSG_OP v9x_hsw_vsg_color(struct v9x_hsw_vsg *g,
                                        v9x_u32 which, v9x_u8 swizzle)
{
    v9x_u32 bit = which == 0u ? V9X_R3D_TNL_IN_COLOR0 : V9X_R3D_TNL_IN_COLOR1;

    if ((g->key->inputs & bit) != 0ul) {
        return v9x_hsw_vsg_input(g, V9X_HSW_VSG_IN_COLOR0 + which, swizzle);
    }
    return v9x_hsw_vsg_const(g, V9X_R3D_TNL_VEC4_DEFAULT(which), swizzle);
}

/* What a material term reads for a face (r3d_tnl.h's material sources). */
static V9X_HSW_VSG_OP v9x_hsw_vsg_term(struct v9x_hsw_vsg *g, v9x_u32 face,
                                       v9x_u32 term, v9x_u32 field,
                                       v9x_u8 swizzle)
{
    v9x_u32 source = V9X_R3D_TNL_SOURCE(g->key->material_sources, term);

    if (source == V9X_R3D_TNL_SOURCE_COLOR0) {
        return v9x_hsw_vsg_color(g, 0u, swizzle);
    }
    if (source == V9X_R3D_TNL_SOURCE_COLOR1) {
        return v9x_hsw_vsg_color(g, 1u, swizzle);
    }
    return v9x_hsw_vsg_const(g, V9X_R3D_TNL_VEC4_MATERIAL(face) + field,
                             swizzle);
}

/*
 * Light i's direction toward it into T_L and its factor into T_FACTOR, or
 * for a directional light nothing: its vector is a constant and its factor
 * one. Returns whether the factor register holds the factor.
 */
static int v9x_hsw_vsg_light_factor(struct v9x_hsw_vsg *g, v9x_u32 i)
{
    v9x_u32 base = V9X_R3D_TNL_VEC4_LIGHT(i);
    v9x_u32 type = V9X_R3D_TNL_LIGHT_TYPE(g->key->light_types, i);
    v9x_u8 l = (v9x_u8)(g->temp + V9X_HSW_VSG_T_L);
    v9x_u8 f = (v9x_u8)(g->temp + V9X_HSW_VSG_T_FACTOR);
    v9x_u8 d2 = (v9x_u8)(g->temp + V9X_HSW_VSG_T_SCRATCH);
    v9x_u8 inv = (v9x_u8)(d2 + 1u);
    v9x_u8 d = (v9x_u8)(d2 + 2u);
    v9x_u8 den = (v9x_u8)(d2 + 3u);
    v9x_u8 t = (v9x_u8)(d2 + 4u);
    V9X_HSW_VSG_OP att;

    if (type == V9X_R3D_TNL_LIGHT_DIRECTIONAL) {
        return 0;
    }

    /* L = position - eye; d^2, 1/d, d; L normalised. */
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_ADD, l, V9X_HSW_VSG_M_XYZ,
                   v9x_hsw_vsg_const(g, base + V9X_R3D_TNL_LF_POSITION,
                                     V9X_HSW_VSG_XYZW),
                   v9x_hsw_vsg_neg(v9x_hsw_vsg_temp(g, V9X_HSW_VSG_T_EYE,
                                                    V9X_HSW_VSG_XYZW)),
                   0u);
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_DP3, d2, V9X_HSW_VSG_M_XYZW,
                   v9x_hsw_vsg_reg(l, V9X_HSW_VSG_XYZW),
                   v9x_hsw_vsg_reg(l, V9X_HSW_VSG_XYZW), 0u);
    v9x_hsw_vsg_max(g, inv, v9x_hsw_vsg_reg(d2, V9X_HSW_VSG_XYZW),
                    V9X_HSW_VSG_TINY);
    v9x_hsw_vsg_math(g, V9X_HSW_EU_MATH_RSQ, inv,
                     v9x_hsw_vsg_reg(inv, V9X_HSW_VSG_XYZW),
                     v9x_hsw_vsg_null());
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_MUL, d, V9X_HSW_VSG_M_XYZW,
                   v9x_hsw_vsg_reg(d2, V9X_HSW_VSG_XYZW),
                   v9x_hsw_vsg_reg(inv, V9X_HSW_VSG_XYZW), 0u);
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_MUL, l, V9X_HSW_VSG_M_XYZ,
                   v9x_hsw_vsg_reg(l, V9X_HSW_VSG_XYZW),
                   v9x_hsw_vsg_reg(inv, V9X_HSW_VSG_XYZW), 0u);

    /* 1 / (a0 + a1 d + a2 d^2), kept only for a denominator above zero and
     * a distance within the range. */
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_MUL, den, V9X_HSW_VSG_M_XYZW,
                   v9x_hsw_vsg_reg(d, V9X_HSW_VSG_XYZW),
                   v9x_hsw_vsg_const(g, base + V9X_R3D_TNL_LF_ATTENUATION,
                                     V9X_HSW_VSG_YYYY),
                   0u);
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_ADD, den, V9X_HSW_VSG_M_XYZW,
                   v9x_hsw_vsg_reg(den, V9X_HSW_VSG_XYZW),
                   v9x_hsw_vsg_const(g, base + V9X_R3D_TNL_LF_ATTENUATION,
                                     V9X_HSW_VSG_XXXX),
                   0u);
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_MUL, t, V9X_HSW_VSG_M_XYZW,
                   v9x_hsw_vsg_reg(d2, V9X_HSW_VSG_XYZW),
                   v9x_hsw_vsg_const(g, base + V9X_R3D_TNL_LF_ATTENUATION,
                                     V9X_HSW_VSG_ZZZZ),
                   0u);
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_ADD, den, V9X_HSW_VSG_M_XYZW,
                   v9x_hsw_vsg_reg(den, V9X_HSW_VSG_XYZW),
                   v9x_hsw_vsg_reg(t, V9X_HSW_VSG_XYZW), 0u);
    v9x_hsw_vsg_math(g, V9X_HSW_EU_MATH_INV, f,
                     v9x_hsw_vsg_reg(den, V9X_HSW_VSG_XYZW),
                     v9x_hsw_vsg_null());
    v9x_hsw_vsg_keep_if(g, f, V9X_HSW_EU_COND_G,
                        v9x_hsw_vsg_reg(den, V9X_HSW_VSG_XYZW),
                        v9x_hsw_vsg_imm(V9X_HSW_VSG_ZERO));
    att = v9x_hsw_vsg_const(g, base + V9X_R3D_TNL_LF_ATTENUATION,
                            V9X_HSW_VSG_WWWW);
    v9x_hsw_vsg_keep_if(g, f, V9X_HSW_EU_COND_LE,
                        v9x_hsw_vsg_reg(d, V9X_HSW_VSG_XYZW), att);

    if (type != V9X_R3D_TNL_LIGHT_SPOT) {
        return 1;
    }

    /* rho = -L . axis into d2; the cone factor into t. */
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_DP3, d2, V9X_HSW_VSG_M_XYZW,
                   v9x_hsw_vsg_reg(l, V9X_HSW_VSG_XYZW),
                   v9x_hsw_vsg_neg(v9x_hsw_vsg_const(
                       g, base + V9X_R3D_TNL_LF_DIRECTION,
                       V9X_HSW_VSG_XYZW)),
                   0u);
    if (g->key->model == V9X_R3D_TNL_MODEL_GL11) {
        /* rho^exponent, zero below the cutoff's cosine. */
        v9x_hsw_vsg_math(g, V9X_HSW_EU_MATH_POW, t,
                         v9x_hsw_vsg_reg(d2, V9X_HSW_VSG_XYZW),
                         v9x_hsw_vsg_const(g, base + V9X_R3D_TNL_LF_SPOT,
                                           V9X_HSW_VSG_YYYY));
        v9x_hsw_vsg_keep_if(g, t, V9X_HSW_EU_COND_GE,
                            v9x_hsw_vsg_reg(d2, V9X_HSW_VSG_XYZW),
                            v9x_hsw_vsg_const(
                                g, base + V9X_R3D_TNL_LF_SPOT,
                                V9X_HSW_VSG_XXXX));
    } else {
        /* ((rho - cos phi/2) / (cos theta/2 - cos phi/2))^falloff, zero
         * at or outside phi/2, one inside theta/2. */
        v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_ADD, t, V9X_HSW_VSG_M_XYZW,
                       v9x_hsw_vsg_reg(d2, V9X_HSW_VSG_XYZW),
                       v9x_hsw_vsg_neg(v9x_hsw_vsg_const(
                           g, base + V9X_R3D_TNL_LF_SPOT,
                           V9X_HSW_VSG_YYYY)),
                       0u);
        v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_MUL, t, V9X_HSW_VSG_M_XYZW,
                       v9x_hsw_vsg_reg(t, V9X_HSW_VSG_XYZW),
                       v9x_hsw_vsg_const(g, base + V9X_R3D_TNL_LF_SPOT,
                                         V9X_HSW_VSG_WWWW),
                       0u);
        v9x_hsw_vsg_math(g, V9X_HSW_EU_MATH_POW, t,
                         v9x_hsw_vsg_reg(t, V9X_HSW_VSG_XYZW),
                         v9x_hsw_vsg_const(g, base + V9X_R3D_TNL_LF_SPOT,
                                           V9X_HSW_VSG_ZZZZ));
        v9x_hsw_vsg_keep_if(g, t, V9X_HSW_EU_COND_G,
                            v9x_hsw_vsg_reg(d2, V9X_HSW_VSG_XYZW),
                            v9x_hsw_vsg_const(
                                g, base + V9X_R3D_TNL_LF_SPOT,
                                V9X_HSW_VSG_YYYY));
        v9x_hsw_vsg_cmp(g, V9X_HSW_EU_COND_G,
                        v9x_hsw_vsg_reg(d2, V9X_HSW_VSG_XYZW),
                        v9x_hsw_vsg_const(g, base + V9X_R3D_TNL_LF_SPOT,
                                          V9X_HSW_VSG_XXXX));
        v9x_hsw_vsg_sel(g, t, V9X_HSW_VSG_M_XYZW,
                        v9x_hsw_vsg_reg(t, V9X_HSW_VSG_XYZW),
                        v9x_hsw_vsg_imm(V9X_HSW_VSG_ONE), 1u);
    }
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_MUL, f, V9X_HSW_VSG_M_XYZW,
                   v9x_hsw_vsg_reg(f, V9X_HSW_VSG_XYZW),
                   v9x_hsw_vsg_reg(t, V9X_HSW_VSG_XYZW), 0u);
    return 1;
}

/* sum += a * b, or += a * b * factor when there is one. */
static void v9x_hsw_vsg_accumulate(struct v9x_hsw_vsg *g, v9x_u8 sum,
                                   V9X_HSW_VSG_OP a, V9X_HSW_VSG_OP b,
                                   int factored)
{
    v9x_u8 t = (v9x_u8)(g->temp + V9X_HSW_VSG_T_SCRATCH);

    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_MUL, t, V9X_HSW_VSG_M_XYZW, a, b, 0u);
    if (factored) {
        v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_MUL, t, V9X_HSW_VSG_M_XYZW,
                       v9x_hsw_vsg_reg(t, V9X_HSW_VSG_XYZW),
                       v9x_hsw_vsg_temp(g, V9X_HSW_VSG_T_FACTOR,
                                        V9X_HSW_VSG_XYZW),
                       0u);
    }
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_ADD, sum, V9X_HSW_VSG_M_XYZW,
                   v9x_hsw_vsg_reg(sum, V9X_HSW_VSG_XYZW),
                   v9x_hsw_vsg_reg(t, V9X_HSW_VSG_XYZW), 0u);
}

/* The normal for a face: the eye normal, negated for the back. */
static V9X_HSW_VSG_OP v9x_hsw_vsg_face_normal(const struct v9x_hsw_vsg *g,
                                              v9x_u32 face)
{
    V9X_HSW_VSG_OP n = v9x_hsw_vsg_temp(g, V9X_HSW_VSG_T_NORMAL,
                                        V9X_HSW_VSG_XYZW);

    return face == 0u ? n : v9x_hsw_vsg_neg(n);
}

/* One light's diffuse and specular sums for one face. */
static void v9x_hsw_vsg_light_face(struct v9x_hsw_vsg *g, v9x_u32 i,
                                   v9x_u32 face, int factored)
{
    v9x_u32 base = V9X_R3D_TNL_VEC4_LIGHT(i);
    int directional = V9X_R3D_TNL_LIGHT_TYPE(g->key->light_types, i) ==
                      V9X_R3D_TNL_LIGHT_DIRECTIONAL;
    v9x_u8 nl = (v9x_u8)(g->temp + V9X_HSW_VSG_T_NL);
    v9x_u8 nh = (v9x_u8)(g->temp + V9X_HSW_VSG_T_SCRATCH + 1u);
    v9x_u8 p = (v9x_u8)(g->temp + V9X_HSW_VSG_T_SCRATCH + 2u);
    V9X_HSW_VSG_OP l;

    l = directional
        ? v9x_hsw_vsg_const(g, base + V9X_R3D_TNL_LF_POSITION,
                            V9X_HSW_VSG_XYZW)
        : v9x_hsw_vsg_temp(g, V9X_HSW_VSG_T_L, V9X_HSW_VSG_XYZW);

    /* max(N.L, 0), zero for NaN too: the select keeps it only above
     * zero. */
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_DP3, nl, V9X_HSW_VSG_M_XYZW,
                   v9x_hsw_vsg_face_normal(g, face), l, 0u);
    v9x_hsw_vsg_keep_if(g, nl, V9X_HSW_EU_COND_G,
                        v9x_hsw_vsg_reg(nl, V9X_HSW_VSG_XYZW),
                        v9x_hsw_vsg_imm(V9X_HSW_VSG_ZERO));
    v9x_hsw_vsg_accumulate(g,
                           (v9x_u8)(g->temp + V9X_HSW_VSG_T_DIFFUSE + face),
                           v9x_hsw_vsg_reg(nl, V9X_HSW_VSG_XYZW),
                           v9x_hsw_vsg_const(g,
                                             base + V9X_R3D_TNL_LF_DIFFUSE,
                                             V9X_HSW_VSG_XYZW),
                           factored);
    if ((g->key->flags & V9X_R3D_TNL_SPECULAR) == 0ul) {
        return;
    }

    /* (N.H)^power, kept only where N.H and N.L are both above zero. */
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_DP3, nh, V9X_HSW_VSG_M_XYZW,
                   v9x_hsw_vsg_face_normal(g, face),
                   v9x_hsw_vsg_temp(g, V9X_HSW_VSG_T_HALF, V9X_HSW_VSG_XYZW),
                   0u);
    v9x_hsw_vsg_math(g, V9X_HSW_EU_MATH_POW, p,
                     v9x_hsw_vsg_reg(nh, V9X_HSW_VSG_XYZW),
                     v9x_hsw_vsg_const(g, V9X_R3D_TNL_VEC4_MATERIAL(face) +
                                              V9X_R3D_TNL_MF_POWER,
                                       V9X_HSW_VSG_XXXX));
    v9x_hsw_vsg_keep_if(g, p, V9X_HSW_EU_COND_G,
                        v9x_hsw_vsg_reg(nh, V9X_HSW_VSG_XYZW),
                        v9x_hsw_vsg_imm(V9X_HSW_VSG_ZERO));
    v9x_hsw_vsg_keep_if(g, p, V9X_HSW_EU_COND_G,
                        v9x_hsw_vsg_reg(nl, V9X_HSW_VSG_XYZW),
                        v9x_hsw_vsg_imm(V9X_HSW_VSG_ZERO));
    v9x_hsw_vsg_accumulate(g,
                           (v9x_u8)(g->temp + V9X_HSW_VSG_T_SPECULAR + face),
                           v9x_hsw_vsg_reg(p, V9X_HSW_VSG_XYZW),
                           v9x_hsw_vsg_const(g,
                                             base + V9X_R3D_TNL_LF_SPECULAR,
                                             V9X_HSW_VSG_XYZW),
                           factored);
}

/* A face's two colours from the sums into out0 and out1. */
static void v9x_hsw_vsg_face_out(struct v9x_hsw_vsg *g, v9x_u32 face,
                                 v9x_u8 out0, v9x_u8 out1)
{
    v9x_u8 sum = (v9x_u8)(g->temp + V9X_HSW_VSG_T_SCRATCH);
    v9x_u8 t = (v9x_u8)(sum + 1u);
    v9x_u8 spec = (v9x_u8)(sum + 2u);
    int specular = (g->key->flags & V9X_R3D_TNL_SPECULAR) != 0ul;

    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_ADD, sum, V9X_HSW_VSG_M_XYZW,
                   v9x_hsw_vsg_temp(g, V9X_HSW_VSG_T_AMBIENT,
                                    V9X_HSW_VSG_XYZW),
                   v9x_hsw_vsg_const(g, V9X_R3D_TNL_VEC4_SCENE_AMBIENT,
                                     V9X_HSW_VSG_XYZW),
                   0u);
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_MUL, sum, V9X_HSW_VSG_M_XYZW,
                   v9x_hsw_vsg_reg(sum, V9X_HSW_VSG_XYZW),
                   v9x_hsw_vsg_term(g, face, V9X_R3D_TNL_TERM_AMBIENT,
                                    V9X_R3D_TNL_MF_AMBIENT,
                                    V9X_HSW_VSG_XYZW),
                   0u);
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_ADD, sum, V9X_HSW_VSG_M_XYZW,
                   v9x_hsw_vsg_reg(sum, V9X_HSW_VSG_XYZW),
                   v9x_hsw_vsg_term(g, face, V9X_R3D_TNL_TERM_EMISSIVE,
                                    V9X_R3D_TNL_MF_EMISSIVE,
                                    V9X_HSW_VSG_XYZW),
                   0u);
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_MUL, t, V9X_HSW_VSG_M_XYZW,
                   v9x_hsw_vsg_temp(g, V9X_HSW_VSG_T_DIFFUSE + face,
                                    V9X_HSW_VSG_XYZW),
                   v9x_hsw_vsg_term(g, face, V9X_R3D_TNL_TERM_DIFFUSE,
                                    V9X_R3D_TNL_MF_DIFFUSE,
                                    V9X_HSW_VSG_XYZW),
                   0u);
    v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_ADD, sum, V9X_HSW_VSG_M_XYZW,
                   v9x_hsw_vsg_reg(sum, V9X_HSW_VSG_XYZW),
                   v9x_hsw_vsg_reg(t, V9X_HSW_VSG_XYZW), 0u);
    if (specular) {
        v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_MUL, spec, V9X_HSW_VSG_M_XYZW,
                       v9x_hsw_vsg_temp(g, V9X_HSW_VSG_T_SPECULAR + face,
                                        V9X_HSW_VSG_XYZW),
                       v9x_hsw_vsg_term(g, face, V9X_R3D_TNL_TERM_SPECULAR,
                                        V9X_R3D_TNL_MF_SPECULAR,
                                        V9X_HSW_VSG_XYZW),
                       0u);
    }

    /* OpenGL 1.1 adds the specular sum into the primary colour; Direct3D
     * keeps it apart. Colour 1's alpha is zero either way, unless fog
     * overwrites it. */
    if (specular && g->key->model == V9X_R3D_TNL_MODEL_GL11) {
        v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_ADD, sum, V9X_HSW_VSG_M_XYZW,
                       v9x_hsw_vsg_reg(sum, V9X_HSW_VSG_XYZW),
                       v9x_hsw_vsg_reg(spec, V9X_HSW_VSG_XYZW), 0u);
    }
    v9x_hsw_vsg_mov(g, out0, V9X_HSW_VSG_M_XYZ,
                    v9x_hsw_vsg_reg(sum, V9X_HSW_VSG_XYZW), 1u);
    v9x_hsw_vsg_mov(g, out0, V9X_HSW_VSG_M_W,
                    v9x_hsw_vsg_term(g, face, V9X_R3D_TNL_TERM_DIFFUSE,
                                     V9X_R3D_TNL_MF_DIFFUSE,
                                     V9X_HSW_VSG_XYZW),
                    1u);
    if (specular && g->key->model == V9X_R3D_TNL_MODEL_D3D7) {
        v9x_hsw_vsg_mov(g, out1, V9X_HSW_VSG_M_XYZ,
                        v9x_hsw_vsg_reg(spec, V9X_HSW_VSG_XYZW), 1u);
        v9x_hsw_vsg_mov(g, out1, V9X_HSW_VSG_M_W,
                        v9x_hsw_vsg_imm(V9X_HSW_VSG_ZERO), 0u);
    } else {
        v9x_hsw_vsg_mov(g, out1, V9X_HSW_VSG_M_XYZW,
                        v9x_hsw_vsg_imm(V9X_HSW_VSG_ZERO), 0u);
    }
}

/* The lit colours for every face the key draws. */
static void v9x_hsw_vsg_lighting(struct v9x_hsw_vsg *g, v9x_u32 faces,
                                 const v9x_u8 *out0, const v9x_u8 *out1)
{
    const V9X_R3D_TNL_KEY *key = g->key;
    v9x_u8 n = (v9x_u8)(g->temp + V9X_HSW_VSG_T_NORMAL);
    v9x_u8 view = (v9x_u8)(g->temp + V9X_HSW_VSG_T_VIEW);
    v9x_u8 half = (v9x_u8)(g->temp + V9X_HSW_VSG_T_HALF);
    v9x_u8 scratch = (v9x_u8)(g->temp + V9X_HSW_VSG_T_SCRATCH);
    V9X_HSW_VSG_OP zero = v9x_hsw_vsg_imm(V9X_HSW_VSG_ZERO);
    V9X_HSW_VSG_OP v;
    v9x_u32 face;
    v9x_u32 i;
    int factored;

    /* The eye normal; a key without normals lights a zero one, which gets
     * ambient and emissive only, as the reference does. */
    if ((key->inputs & V9X_R3D_TNL_IN_NORMAL) != 0ul) {
        v9x_hsw_vsg_transform(g, n, V9X_R3D_TNL_VEC4_NORMAL, 3ul,
                              v9x_hsw_vsg_input(g, V9X_HSW_VSG_IN_NORMAL,
                                                V9X_HSW_VSG_XYZW),
                              V9X_HSW_EU_OP_DP3);
        if ((key->flags & V9X_R3D_TNL_NORMALIZE) != 0ul) {
            v9x_hsw_vsg_normalize(g, n, scratch);
        }
    } else {
        v9x_hsw_vsg_mov(g, n, V9X_HSW_VSG_M_XYZW, zero, 0u);
    }

    /* Toward the viewer: from the vertex to the eye, or the constant. */
    v = v9x_hsw_vsg_reg(view, V9X_HSW_VSG_XYZW);
    if ((key->flags & V9X_R3D_TNL_SPECULAR) != 0ul) {
        if ((key->flags & V9X_R3D_TNL_LOCAL_VIEWER) != 0ul) {
            v9x_hsw_vsg_mov(g, view, V9X_HSW_VSG_M_XYZW,
                            v9x_hsw_vsg_neg(v9x_hsw_vsg_temp(
                                g, V9X_HSW_VSG_T_EYE, V9X_HSW_VSG_XYZW)),
                            0u);
            v9x_hsw_vsg_normalize(g, view, scratch);
        } else {
            v = v9x_hsw_vsg_const(g, V9X_R3D_TNL_VEC4_VIEWER,
                                  V9X_HSW_VSG_XYZW);
        }
    }

    v9x_hsw_vsg_mov(g, (v9x_u8)(g->temp + V9X_HSW_VSG_T_AMBIENT),
                    V9X_HSW_VSG_M_XYZW, zero, 0u);
    for (face = 0ul; face < faces; ++face) {
        v9x_hsw_vsg_mov(g, (v9x_u8)(g->temp + V9X_HSW_VSG_T_DIFFUSE + face),
                        V9X_HSW_VSG_M_XYZW, zero, 0u);
        if ((key->flags & V9X_R3D_TNL_SPECULAR) != 0ul) {
            v9x_hsw_vsg_mov(g,
                            (v9x_u8)(g->temp + V9X_HSW_VSG_T_SPECULAR + face),
                            V9X_HSW_VSG_M_XYZW, zero, 0u);
        }
    }

    for (i = 0ul; i < key->light_count; ++i) {
        v9x_u32 base = V9X_R3D_TNL_VEC4_LIGHT(i);
        V9X_HSW_VSG_OP l;

        factored = v9x_hsw_vsg_light_factor(g, i);
        l = factored
            ? v9x_hsw_vsg_temp(g, V9X_HSW_VSG_T_L, V9X_HSW_VSG_XYZW)
            : v9x_hsw_vsg_const(g, base + V9X_R3D_TNL_LF_POSITION,
                                V9X_HSW_VSG_XYZW);

        /* The ambient sum, shared by the faces. */
        if (factored) {
            v9x_hsw_vsg_accumulate(
                g, (v9x_u8)(g->temp + V9X_HSW_VSG_T_AMBIENT),
                v9x_hsw_vsg_const(g, base + V9X_R3D_TNL_LF_AMBIENT,
                                  V9X_HSW_VSG_XYZW),
                v9x_hsw_vsg_temp(g, V9X_HSW_VSG_T_FACTOR, V9X_HSW_VSG_XYZW),
                0);
        } else {
            v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_ADD,
                           (v9x_u8)(g->temp + V9X_HSW_VSG_T_AMBIENT),
                           V9X_HSW_VSG_M_XYZW,
                           v9x_hsw_vsg_temp(g, V9X_HSW_VSG_T_AMBIENT,
                                            V9X_HSW_VSG_XYZW),
                           v9x_hsw_vsg_const(g,
                                             base + V9X_R3D_TNL_LF_AMBIENT,
                                             V9X_HSW_VSG_XYZW),
                           0u);
        }

        /* The half vector depends on the light, not the face. */
        if ((key->flags & V9X_R3D_TNL_SPECULAR) != 0ul) {
            v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_ADD, half, V9X_HSW_VSG_M_XYZ, l, v,
                           0u);
            v9x_hsw_vsg_normalize(g, half, scratch);
        }
        for (face = 0ul; face < faces; ++face) {
            v9x_hsw_vsg_light_face(g, i, face, factored);
        }
    }

    for (face = 0ul; face < faces; ++face) {
        v9x_hsw_vsg_face_out(g, face, out0[face], out1[face]);
    }
}

/* The fog factor into colour 1's alpha (each face's). */
static void v9x_hsw_vsg_fog(struct v9x_hsw_vsg *g, v9x_u32 faces,
                            const v9x_u8 *out1)
{
    const V9X_R3D_TNL_KEY *key = g->key;
    v9x_u8 d = (v9x_u8)(g->temp + V9X_HSW_VSG_T_SCRATCH);
    v9x_u8 f = (v9x_u8)(d + 1u);
    V9X_HSW_VSG_OP eye = v9x_hsw_vsg_temp(g, V9X_HSW_VSG_T_EYE,
                                          V9X_HSW_VSG_XYZW);
    v9x_u32 face;

    if (key->fog_distance == V9X_R3D_TNL_FOG_RANGE) {
        v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_DP3, d, V9X_HSW_VSG_M_XYZW, eye, eye,
                       0u);
        v9x_hsw_vsg_math(g, V9X_HSW_EU_MATH_SQRT, d,
                         v9x_hsw_vsg_reg(d, V9X_HSW_VSG_XYZW),
                         v9x_hsw_vsg_null());
    } else {
        v9x_hsw_vsg_mov(g, d, V9X_HSW_VSG_M_XYZW,
                        v9x_hsw_vsg_abs(v9x_hsw_vsg_temp(
                            g, V9X_HSW_VSG_T_EYE, V9X_HSW_VSG_ZZZZ)),
                        0u);
    }
    if (key->fog_mode == V9X_R3D_TNL_FOG_LINEAR) {
        /* (end - d) / (end - start) */
        v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_ADD, f, V9X_HSW_VSG_M_XYZW,
                       v9x_hsw_vsg_const(g, V9X_R3D_TNL_VEC4_FOG,
                                         V9X_HSW_VSG_YYYY),
                       v9x_hsw_vsg_neg(v9x_hsw_vsg_reg(d, V9X_HSW_VSG_XYZW)),
                       0u);
        v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_MUL, f, V9X_HSW_VSG_M_XYZW,
                       v9x_hsw_vsg_reg(f, V9X_HSW_VSG_XYZW),
                       v9x_hsw_vsg_const(g, V9X_R3D_TNL_VEC4_FOG,
                                         V9X_HSW_VSG_WWWW),
                       0u);
    } else {
        /* e^-(density d), squared inside for EXP2; the EU's EXP is 2^x. */
        v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_MUL, f, V9X_HSW_VSG_M_XYZW,
                       v9x_hsw_vsg_reg(d, V9X_HSW_VSG_XYZW),
                       v9x_hsw_vsg_const(g, V9X_R3D_TNL_VEC4_FOG,
                                         V9X_HSW_VSG_ZZZZ),
                       0u);
        if (key->fog_mode == V9X_R3D_TNL_FOG_EXP2) {
            v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_MUL, f, V9X_HSW_VSG_M_XYZW,
                           v9x_hsw_vsg_reg(f, V9X_HSW_VSG_XYZW),
                           v9x_hsw_vsg_reg(f, V9X_HSW_VSG_XYZW), 0u);
        }
        v9x_hsw_vsg_op(g, V9X_HSW_EU_OP_MUL, f, V9X_HSW_VSG_M_XYZW,
                       v9x_hsw_vsg_reg(f, V9X_HSW_VSG_XYZW),
                       v9x_hsw_vsg_imm(V9X_HSW_VSG_MINUS_LOG2_E), 0u);
        v9x_hsw_vsg_math(g, V9X_HSW_EU_MATH_EXP, f,
                         v9x_hsw_vsg_reg(f, V9X_HSW_VSG_XYZW),
                         v9x_hsw_vsg_null());
    }
    for (face = 0ul; face < faces; ++face) {
        v9x_hsw_vsg_mov(g, out1[face], V9X_HSW_VSG_M_W,
                        v9x_hsw_vsg_reg(f, V9X_HSW_VSG_XXXX), 1u);
    }
}

static void v9x_hsw_vsg_kernel(struct v9x_hsw_vsg *g, v9x_u8 outputs)
{
    const V9X_R3D_TNL_KEY *key = g->key;
    v9x_u32 faces = (key->flags & V9X_R3D_TNL_TWO_SIDED) != 0ul ? 2ul : 1ul;
    v9x_u8 out0[2];
    v9x_u8 out1[2];
    v9x_u8 tex0;
    v9x_u32 unit;
    int lit = (key->flags & V9X_R3D_TNL_LIGHTING) != 0ul;
    int positional = 0;
    v9x_u32 i;

    /* Where the colours go: front, back, front, back when two-sided, so
     * SBE's facing select reads [src] and [src + 1] (spec note, section
     * 2). */
    if (faces == 2ul) {
        out0[0] = V9X_HSW_VSG_ATTRIBUTES;
        out0[1] = (v9x_u8)(V9X_HSW_VSG_ATTRIBUTES + 1u);
        out1[0] = (v9x_u8)(V9X_HSW_VSG_ATTRIBUTES + 2u);
        out1[1] = (v9x_u8)(V9X_HSW_VSG_ATTRIBUTES + 3u);
        tex0 = (v9x_u8)(V9X_HSW_VSG_ATTRIBUTES + 4u);
    } else {
        out0[0] = V9X_HSW_VSG_ATTRIBUTES;
        out1[0] = (v9x_u8)(V9X_HSW_VSG_ATTRIBUTES + 1u);
        out0[1] = out0[0];
        out1[1] = out1[0];
        tex0 = (v9x_u8)(V9X_HSW_VSG_ATTRIBUTES + 2u);
    }

    v9x_hsw_vsg_header(g);
    v9x_hsw_vsg_transform(g, V9X_HSW_VSG_POSITION, V9X_R3D_TNL_VEC4_MVP, 4ul,
                          v9x_hsw_vsg_input(g, V9X_HSW_VSG_IN_POSITION,
                                            V9X_HSW_VSG_XYZW),
                          V9X_HSW_EU_OP_DP4);

    for (i = 0ul; i < key->light_count; ++i) {
        if (V9X_R3D_TNL_LIGHT_TYPE(key->light_types, i) !=
            V9X_R3D_TNL_LIGHT_DIRECTIONAL) {
            positional = 1;
        }
    }
    if (key->fog_mode != V9X_R3D_TNL_FOG_NONE ||
        (lit && (positional ||
                 (key->flags & (V9X_R3D_TNL_LOCAL_VIEWER |
                                V9X_R3D_TNL_SPECULAR)) ==
                     (V9X_R3D_TNL_LOCAL_VIEWER | V9X_R3D_TNL_SPECULAR)))) {
        v9x_hsw_vsg_transform(g, (v9x_u8)(g->temp + V9X_HSW_VSG_T_EYE),
                              V9X_R3D_TNL_VEC4_MODEL_VIEW, 3ul,
                              v9x_hsw_vsg_input(g, V9X_HSW_VSG_IN_POSITION,
                                                V9X_HSW_VSG_XYZW),
                              V9X_HSW_EU_OP_DP4);
    }

    /* Texture coordinates, through their matrix when the key says so. */
    for (unit = 0ul; unit < V9X_R3D_TNL_MAX_TEXTURES; ++unit) {
        V9X_HSW_VSG_OP in = v9x_hsw_vsg_input(g,
                                              V9X_HSW_VSG_IN_TEXTURE0 + unit,
                                              V9X_HSW_VSG_XYZW);

        if (unit < key->texture_count &&
            (key->texture_matrix & (1ul << unit)) != 0ul) {
            v9x_hsw_vsg_transform(g, (v9x_u8)(tex0 + unit),
                                  V9X_R3D_TNL_VEC4_TEXTURE(unit), 4ul, in,
                                  V9X_HSW_EU_OP_DP4);
        } else {
            v9x_hsw_vsg_mov(g, (v9x_u8)(tex0 + unit), V9X_HSW_VSG_M_XYZW, in,
                            0u);
        }
    }

    if (lit) {
        v9x_hsw_vsg_lighting(g, faces, out0, out1);
    } else {
        for (i = 0ul; i < faces; ++i) {
            v9x_hsw_vsg_mov(g, out0[i], V9X_HSW_VSG_M_XYZW,
                            v9x_hsw_vsg_color(g, 0u, V9X_HSW_VSG_XYZW), 1u);
            v9x_hsw_vsg_mov(g, out1[i], V9X_HSW_VSG_M_XYZW,
                            v9x_hsw_vsg_color(g, 1u, V9X_HSW_VSG_XYZW), 1u);
        }
    }
    if (key->fog_mode != V9X_R3D_TNL_FOG_NONE) {
        v9x_hsw_vsg_fog(g, faces, out1);
    }
    v9x_hsw_vsg_end(g, outputs);
}

static void v9x_hsw_vsg_pass(struct v9x_hsw_vsg *g,
                             const V9X_R3D_TNL_KEY *key, v9x_u32 *out,
                             v9x_u32 capacity, v9x_u8 constant_grfs,
                             v9x_u8 inputs, v9x_u8 outputs)
{
    v9x_u32 index;

    g->out = out;
    g->capacity = capacity;
    g->used = 0ul;
    g->failed = 0;
    g->key = key;
    g->input = (v9x_u8)(1u + constant_grfs);
    g->temp = (v9x_u8)(g->input + inputs);
    g->count = 0u;
    for (index = 0ul; index < V9X_R3D_TNL_VEC4S; ++index) {
        g->slot[index] = V9X_HSW_VSG_UNUSED;
        g->map[index] = 0u;
    }
    v9x_hsw_vsg_kernel(g, outputs);
}

v9x_status v9x_intel_hsw_vs_kernel_build(const V9X_R3D_TNL_KEY *key,
                                         v9x_u32 *out, v9x_u32 capacity,
                                         struct v9x_hsw_vs_program *program)
{
    struct v9x_hsw_vsg g;
    v9x_u8 inputs;
    v9x_u8 outputs;
    v9x_u8 grfs;
    v9x_u32 index;

    if (program == 0 || v9x_r3d_tnl_key_check(key) != V9X_STATUS_OK) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    inputs = (key->inputs & V9X_R3D_TNL_IN_NORMAL) != 0ul
        ? V9X_HSW_VSK_INPUTS_NORMAL : V9X_HSW_VSK_INPUTS;
    outputs = (key->flags & V9X_R3D_TNL_TWO_SIDED) != 0ul
        ? V9X_HSW_VSK_OUTPUTS_TWO_SIDED : V9X_HSW_VSK_OUTPUTS;

    /* Measure with the most constants, which leaves the temporaries
     * highest; then build with the count measured. */
    v9x_hsw_vsg_pass(&g, key, 0, 0ul, V9X_HSW_VSK_CONSTANT_GRFS_MAX, inputs,
                     outputs);
    if (g.failed) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    grfs = (v9x_u8)((g.count + 1u) / 2u);
    if (grfs == 0u) {
        grfs = 1u;
    }
    v9x_hsw_vsg_pass(&g, key, out, capacity, grfs, inputs, outputs);
    if (g.failed == 2) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    if (g.failed || (v9x_u8)((g.count + 1u) / 2u) > grfs) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    program->dwords = g.used;
    program->constant_grfs = grfs;
    program->inputs = inputs;
    program->outputs = outputs;
    program->constant_count = g.count;
    for (index = 0ul; index < V9X_R3D_TNL_VEC4S; ++index) {
        program->constant_map[index] = g.map[index];
    }
    return V9X_STATUS_OK;
}

void v9x_intel_hsw_vs_constants(const struct v9x_hsw_vs_program *program,
                                const V9X_R3D_TNL_CONSTANTS *block,
                                v9x_u32 *out)
{
    const v9x_u32 *source = (const v9x_u32 *)block;
    v9x_u32 vec4;
    v9x_u32 word;

    for (vec4 = 0ul; vec4 < (v9x_u32)program->constant_grfs * 2ul; ++vec4) {
        for (word = 0ul; word < 4ul; ++word) {
            out[vec4 * 4ul + word] =
                vec4 < program->constant_count
                    ? source[program->constant_map[vec4] * 4ul + word]
                    : 0ul;
        }
    }
}
