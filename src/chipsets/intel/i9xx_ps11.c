/*
 * Pixel shaders 1.0 and 1.1 as Gen3 fragment programs. See
 * velocity9x\i9xx_ps11.h for what the program expects, and
 * docs\plans\gen3-ps11-pixel-shaders.md for the mapping this follows.
 *
 * The rules are the reference interpreter's (d3d_ps11_ref.c), as the
 * Haswell translator (hsw_ps11.c) keeps them: every arithmetic result
 * shifted, then saturated or held to [-1, 1]; a co-issued pair reads its
 * sources before either writes; registers read before they are written
 * read zero. The last is made true here, since the encoder will not read
 * an R register nothing wrote: the first read, or a first write of fewer
 * than four channels, is preceded by a move of zero.
 *
 * Registers:
 *
 *   R0, R1     r0, r1
 *   R2-R5      t0-t3, as each stage's instruction leaves it
 *   R6         a co-issued pair's first result, until the second is done
 *   R7-R9      a modified source, one a source slot
 *   R10, R11   a second and third constant moved out of an instruction
 *              (the fragment program reads one constant register an
 *              instruction)
 *   R12        an expansion's intermediate, and a lookup's address
 *   R13        the texm3x3 rows' dot products, x y z
 *   R14, R15   the reflection's eye vector and its scale
 *
 * Texture instructions come first in a 1.x shader, so every dependent
 * read (texkill, texbem, the texm3x3 lookups) is in that block, and the
 * phase count is the encoder's to keep.
 */
#include "velocity9x/i9xx_fs.h"
#include "velocity9x/i9xx_ps11.h"

#define V9X_I9XX_PS11_R_TEXTURE  2ul
#define V9X_I9XX_PS11_R_FIRST    6ul
#define V9X_I9XX_PS11_R_SOURCE   7ul
#define V9X_I9XX_PS11_R_CONST    10ul
#define V9X_I9XX_PS11_R_WORK     12ul
#define V9X_I9XX_PS11_R_DOTS     13ul
#define V9X_I9XX_PS11_R_EYE      14ul
#define V9X_I9XX_PS11_R_SCALE    15ul

/* The shader's registers, R0-R5: the ones that read zero unwritten. */
#define V9X_I9XX_PS11_SHADER_REGS 6ul

/* C16's channels. */
#define V9X_I9XX_PS11_HALF       V9X_I9XX_FS_SEL_X
#define V9X_I9XX_PS11_TWO        V9X_I9XX_FS_SEL_Y
#define V9X_I9XX_PS11_FOUR       V9X_I9XX_FS_SEL_Z

#define V9X_I9XX_PS11_NO_CONST   0xfffffffful

typedef struct v9x_i9xx_ps11_gen {
    struct v9x_i9xx_fs fs;
    const V9X_PS11_PROGRAM *program;
    const struct v9x_i9xx_ps11_desc *desc;
    v9x_u32 defined;        /* bit n: Rn (n < 6) holds a value */
    v9x_u32 pads;           /* texm3x3pad rows so far          */
    v9x_status failed;
} V9X_I9XX_PS11_GEN;

typedef union v9x_i9xx_ps11_float {
    float f;
    v9x_u32 bits;
} V9X_I9XX_PS11_FLOAT;

static v9x_u32 v9x_i9xx_ps11_bits(float value)
{
    V9X_I9XX_PS11_FLOAT u;

    u.f = value;
    return u.bits;
}

static float v9x_i9xx_ps11_value(v9x_u32 bits)
{
    V9X_I9XX_PS11_FLOAT u;

    u.bits = bits;
    return u.f;
}

static float v9x_i9xx_ps11_clamp(float value)
{
    if (!(value > -1.0f)) {
        return -1.0f;   /* NaN too, as the interpreter */
    }
    return value > 1.0f ? 1.0f : value;
}

void v9x_i9xx_ps11_constants(const V9X_PS11_PROGRAM *program,
                             const V9X_PS11_ENV *env,
                             const v9x_u32 *fog_rgb,
                             v9x_u32 out[V9X_I9XX_PS11_CONST_REGS * 4u])
{
    v9x_u32 k;
    v9x_u32 c;

    for (k = 0ul; k < 8ul; ++k) {
        for (c = 0ul; c < 4ul; ++c) {
            float value = env->c[k][c];

            if ((program->def_mask & (1ul << k)) != 0ul) {
                value = v9x_i9xx_ps11_value(program->def[k][c]);
            }
            out[4ul * k + c] =
                v9x_i9xx_ps11_bits(v9x_i9xx_ps11_clamp(value));
        }
    }
    for (k = 0ul; k < 4ul; ++k) {
        v9x_u32 bump = 4ul * (V9X_I9XX_PS11_CONST_BUMP + k);
        v9x_u32 luminance = 4ul * (V9X_I9XX_PS11_CONST_LUMINANCE + k);

        for (c = 0ul; c < 4ul; ++c) {
            out[bump + c] = v9x_i9xx_ps11_bits(env->bump[k][c]);
        }
        out[luminance + 0ul] = v9x_i9xx_ps11_bits(env->luminance[k][0]);
        out[luminance + 1ul] = v9x_i9xx_ps11_bits(env->luminance[k][1]);
        out[luminance + 2ul] = 0ul;
        out[luminance + 3ul] = 0ul;
    }

    k = 4ul * V9X_I9XX_PS11_CONST_FIXED;
    out[k + 0ul] = v9x_i9xx_ps11_bits(0.5f);
    out[k + 1ul] = v9x_i9xx_ps11_bits(2.0f);
    out[k + 2ul] = v9x_i9xx_ps11_bits(4.0f);
    out[k + 3ul] = 0ul;

    k = 4ul * V9X_I9XX_PS11_CONST_FOG;
    for (c = 0ul; c < 3ul; ++c) {
        out[k + c] = fog_rgb != 0 ? fog_rgb[c] : 0ul;
    }
    out[k + 3ul] = 0ul;
}

/* ------------------------------------------------------------------ */
/* Operands                                                            */
/* ------------------------------------------------------------------ */

static v9x_u32 v9x_i9xx_ps11_r(v9x_u32 nr)
{
    return V9X_I9XX_FS_SRC(V9X_I9XX_FS_REG_TYPE_R, nr);
}

static v9x_u32 v9x_i9xx_ps11_rep(v9x_u32 src, v9x_u32 sel)
{
    return v9x_i9xx_fs_swizzle(src, sel, sel, sel, sel);
}

/* One of C16's numbers in all four channels. */
static v9x_u32 v9x_i9xx_ps11_fixed(v9x_u32 sel)
{
    return v9x_i9xx_ps11_rep(
        V9X_I9XX_FS_SRC(V9X_I9XX_FS_REG_TYPE_CONST,
                        V9X_I9XX_PS11_CONST_FIXED), sel);
}

/* 1 in every channel, read through src's own register, so it adds no
 * second constant register to an instruction that reads src. */
static v9x_u32 v9x_i9xx_ps11_one(v9x_u32 src)
{
    return v9x_i9xx_ps11_rep(src, V9X_I9XX_FS_SEL_ONE);
}

/* (0, 0, 0, 1): an empty stage's lookup, and through ZERO alone the
 * zero an unwritten register reads. */
static v9x_u32 v9x_i9xx_ps11_empty(void)
{
    return v9x_i9xx_fs_swizzle(
        V9X_I9XX_FS_SRC(V9X_I9XX_FS_REG_TYPE_CONST,
                        V9X_I9XX_PS11_CONST_FIXED),
        V9X_I9XX_FS_SEL_ZERO, V9X_I9XX_FS_SEL_ZERO, V9X_I9XX_FS_SEL_ZERO,
        V9X_I9XX_FS_SEL_ONE);
}

static v9x_u32 v9x_i9xx_ps11_operands(v9x_u32 opcode)
{
    if (opcode == V9X_I9XX_FS_A0_MAD || opcode == V9X_I9XX_FS_A0_CMP) {
        return 3ul;
    }
    if (opcode == V9X_I9XX_FS_A0_MOV || opcode == V9X_I9XX_FS_A0_RCP) {
        return 1ul;
    }
    return 2ul;
}

/*
 * An arithmetic instruction through the encoder. A source reading a
 * second or third constant register is moved into R10 or R11 first: the
 * instruction may read one.
 */
static void v9x_i9xx_ps11_emit(V9X_I9XX_PS11_GEN *g, v9x_u32 opcode,
                               v9x_u32 dest_type, v9x_u32 dest_nr,
                               v9x_u32 mask, v9x_u32 saturate,
                               v9x_u32 src0, v9x_u32 src1, v9x_u32 src2)
{
    v9x_u32 srcs[3];
    v9x_u32 operands = v9x_i9xx_ps11_operands(opcode);
    v9x_u32 first = V9X_I9XX_PS11_NO_CONST;
    v9x_u32 spilled = 0ul;
    v9x_u32 index;

    srcs[0] = src0;
    srcs[1] = src1;
    srcs[2] = src2;
    for (index = 0ul; index < operands; ++index) {
        v9x_u32 type = (srcs[index] >> V9X_I9XX_FS_SRC_TYPE_SHIFT) & 7ul;
        v9x_u32 nr = srcs[index] & V9X_I9XX_FS_SRC_NR_MASK;
        v9x_u32 temp;

        if (type != V9X_I9XX_FS_REG_TYPE_CONST) {
            continue;
        }
        if (first == V9X_I9XX_PS11_NO_CONST || first == nr) {
            first = nr;
            continue;
        }
        temp = V9X_I9XX_PS11_R_CONST + spilled;
        ++spilled;
        v9x_i9xx_fs_alu(&g->fs, V9X_I9XX_FS_A0_MOV,
                        V9X_I9XX_FS_REG_TYPE_R, temp,
                        V9X_I9XX_FS_MASK_XYZW, V9X_FALSE,
                        srcs[index], 0ul, 0ul);
        srcs[index] = v9x_i9xx_ps11_r(temp);
    }
    v9x_i9xx_fs_alu(&g->fs, opcode, dest_type, dest_nr, mask, saturate,
                    srcs[0], srcs[1], srcs[2]);
}

/* emit into an R register. */
static void v9x_i9xx_ps11_op(V9X_I9XX_PS11_GEN *g, v9x_u32 opcode,
                             v9x_u32 dest_nr, v9x_u32 mask,
                             v9x_u32 saturate,
                             v9x_u32 src0, v9x_u32 src1, v9x_u32 src2)
{
    v9x_i9xx_ps11_emit(g, opcode, V9X_I9XX_FS_REG_TYPE_R, dest_nr, mask,
                       saturate, src0, src1, src2);
}

/* A shader register about to be read, or written in part: if nothing
 * has written it, it reads zero, so make it so. */
static void v9x_i9xx_ps11_ensure(V9X_I9XX_PS11_GEN *g, v9x_u32 nr)
{
    if ((g->defined & (1ul << nr)) != 0ul) {
        return;
    }
    v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MOV, nr, V9X_I9XX_FS_MASK_XYZW,
                     V9X_FALSE, v9x_i9xx_ps11_rep(v9x_i9xx_ps11_empty(),
                                                  V9X_I9XX_FS_SEL_X),
                     0ul, 0ul);
    g->defined |= 1ul << nr;
}

/* The R register a shader destination or temp/texture source names. */
static v9x_u32 v9x_i9xx_ps11_shader_reg(v9x_u32 file, v9x_u32 reg)
{
    return file == V9X_PS11_FILE_TEMP ? reg
                                      : V9X_I9XX_PS11_R_TEXTURE + reg;
}

/* A shader source, swizzled, its modifier applied: the register itself
 * for none or a negate, else the modified value in R7 + slot. */
static v9x_u32 v9x_i9xx_ps11_source(V9X_I9XX_PS11_GEN *g,
                                    const V9X_PS11_SRC *src, v9x_u32 slot)
{
    v9x_u32 temp = V9X_I9XX_PS11_R_SOURCE + slot;
    v9x_u32 base;
    v9x_u32 swizzle = src->swizzle;

    switch (src->file) {
    case V9X_PS11_FILE_INPUT:
        base = V9X_I9XX_FS_SRC(V9X_I9XX_FS_REG_TYPE_T,
                               V9X_I9XX_FS_T_DIFFUSE + src->reg);
        break;
    case V9X_PS11_FILE_CONST:
        base = V9X_I9XX_FS_SRC(V9X_I9XX_FS_REG_TYPE_CONST, src->reg);
        break;
    default:
        base = v9x_i9xx_ps11_shader_reg(src->file, src->reg);
        v9x_i9xx_ps11_ensure(g, base);
        base = v9x_i9xx_ps11_r(base);
        break;
    }
    base = v9x_i9xx_fs_swizzle(base, swizzle & 3ul, (swizzle >> 2) & 3ul,
                               (swizzle >> 4) & 3ul, (swizzle >> 6) & 3ul);

    switch (src->mod) {
    case V9X_PS11_SRC_NEG:
        return v9x_i9xx_fs_negate(base, V9X_I9XX_FS_MASK_XYZW);
    case V9X_PS11_SRC_BIAS:         /* x - 0.5 */
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_ADD, temp, V9X_I9XX_FS_MASK_XYZW,
                         V9X_FALSE, base,
                         v9x_i9xx_fs_negate(
                             v9x_i9xx_ps11_fixed(V9X_I9XX_PS11_HALF),
                             V9X_I9XX_FS_MASK_XYZW), 0ul);
        break;
    case V9X_PS11_SRC_BIASNEG:      /* 0.5 - x */
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_ADD, temp, V9X_I9XX_FS_MASK_XYZW,
                         V9X_FALSE,
                         v9x_i9xx_fs_negate(base, V9X_I9XX_FS_MASK_XYZW),
                         v9x_i9xx_ps11_fixed(V9X_I9XX_PS11_HALF), 0ul);
        break;
    case V9X_PS11_SRC_SIGN:         /* 2x - 1 */
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MAD, temp, V9X_I9XX_FS_MASK_XYZW,
                         V9X_FALSE, base,
                         v9x_i9xx_ps11_fixed(V9X_I9XX_PS11_TWO),
                         v9x_i9xx_fs_negate(v9x_i9xx_ps11_one(base),
                                            V9X_I9XX_FS_MASK_XYZW));
        break;
    case V9X_PS11_SRC_SIGNNEG:      /* 1 - 2x */
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MAD, temp, V9X_I9XX_FS_MASK_XYZW,
                         V9X_FALSE, base,
                         v9x_i9xx_fs_negate(
                             v9x_i9xx_ps11_fixed(V9X_I9XX_PS11_TWO),
                             V9X_I9XX_FS_MASK_XYZW),
                         v9x_i9xx_ps11_one(base));
        break;
    case V9X_PS11_SRC_COMP:         /* 1 - x */
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_ADD, temp, V9X_I9XX_FS_MASK_XYZW,
                         V9X_FALSE, v9x_i9xx_ps11_one(base),
                         v9x_i9xx_fs_negate(base, V9X_I9XX_FS_MASK_XYZW),
                         0ul);
        break;
    default:
        return base;
    }
    return v9x_i9xx_ps11_r(temp);
}

/* ------------------------------------------------------------------ */
/* Arithmetic                                                          */
/* ------------------------------------------------------------------ */

/* One arithmetic instruction's result into R dest under its mask:
 * the operation, the shift, then saturate or the hold to [-1, 1]. */
static void v9x_i9xx_ps11_arithmetic(V9X_I9XX_PS11_GEN *g,
                                     const V9X_PS11_INST *inst,
                                     v9x_u32 dest)
{
    v9x_u32 mask = inst->dst_mask;
    v9x_u32 saturate = inst->saturate != 0u ? V9X_TRUE : V9X_FALSE;
    v9x_u32 op_saturate = inst->shift == 0l ? saturate : V9X_FALSE;
    v9x_u32 work = v9x_i9xx_ps11_r(V9X_I9XX_PS11_R_WORK);
    v9x_u32 d = v9x_i9xx_ps11_r(dest);
    v9x_u32 a = 0ul;
    v9x_u32 b = 0ul;
    v9x_u32 c = 0ul;

    if (inst->sources > 0u) {
        a = v9x_i9xx_ps11_source(g, &inst->src[0], 0ul);
    }
    if (inst->sources > 1u) {
        b = v9x_i9xx_ps11_source(g, &inst->src[1], 1ul);
    }
    if (inst->sources > 2u) {
        c = v9x_i9xx_ps11_source(g, &inst->src[2], 2ul);
    }

    switch (inst->op) {
    case V9X_PS11_ADD:
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_ADD, dest, mask, op_saturate,
                         a, b, 0ul);
        break;
    case V9X_PS11_SUB:
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_ADD, dest, mask, op_saturate,
                         a, v9x_i9xx_fs_negate(b, V9X_I9XX_FS_MASK_XYZW),
                         0ul);
        break;
    case V9X_PS11_MUL:
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MUL, dest, mask, op_saturate,
                         a, b, 0ul);
        break;
    case V9X_PS11_MAD:
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MAD, dest, mask, op_saturate,
                         a, b, c);
        break;
    case V9X_PS11_DP3:
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_DP3, dest, mask, op_saturate,
                         a, b, 0ul);
        break;
    case V9X_PS11_LRP:
        /* a b + (1 - a) c = a (b - c) + c */
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_ADD, V9X_I9XX_PS11_R_WORK,
                         V9X_I9XX_FS_MASK_XYZW, V9X_FALSE, b,
                         v9x_i9xx_fs_negate(c, V9X_I9XX_FS_MASK_XYZW), 0ul);
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MAD, dest, mask, op_saturate,
                         a, work, c);
        break;
    case V9X_PS11_CND:
        /* a > 0.5 ? b : c. CMP takes src1 where src0 >= 0, so src0 is
         * 0.5 - a: c where a <= 0.5, b where a > 0.5, exactly. */
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_ADD, V9X_I9XX_PS11_R_WORK,
                         V9X_I9XX_FS_MASK_XYZW, V9X_FALSE,
                         v9x_i9xx_ps11_fixed(V9X_I9XX_PS11_HALF),
                         v9x_i9xx_fs_negate(a, V9X_I9XX_FS_MASK_XYZW), 0ul);
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_CMP, dest, mask, op_saturate,
                         work, c, b);
        break;
    default:    /* MOV */
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MOV, dest, mask, op_saturate,
                         a, 0ul, 0ul);
        break;
    }

    if (inst->shift != 0l) {
        v9x_u32 factor = inst->shift == 1l ? V9X_I9XX_PS11_TWO :
                         (inst->shift == 2l ? V9X_I9XX_PS11_FOUR
                                            : V9X_I9XX_PS11_HALF);

        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MUL, dest, mask, saturate,
                         d, v9x_i9xx_ps11_fixed(factor), 0ul);
    }
    if (saturate) {
        return;
    }
    v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MAX, dest, mask, V9X_FALSE, d,
                     v9x_i9xx_fs_negate(v9x_i9xx_ps11_one(d),
                                        V9X_I9XX_FS_MASK_XYZW), 0ul);
    v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MIN, dest, mask, V9X_FALSE, d,
                     v9x_i9xx_ps11_one(d), 0ul);
}

/* The shader register an arithmetic instruction writes, zeroed first if
 * it is written in part and has never been written. */
static v9x_u32 v9x_i9xx_ps11_dest(V9X_I9XX_PS11_GEN *g,
                                  const V9X_PS11_INST *inst)
{
    v9x_u32 nr = v9x_i9xx_ps11_shader_reg(inst->dst_file, inst->dst_reg);

    if (inst->dst_mask != V9X_I9XX_FS_MASK_XYZW) {
        v9x_i9xx_ps11_ensure(g, nr);
    }
    return nr;
}

/* ------------------------------------------------------------------ */
/* Texture instructions                                                */
/* ------------------------------------------------------------------ */

static v9x_u32 v9x_i9xx_ps11_coords(v9x_u32 stage)
{
    return V9X_I9XX_FS_SRC(V9X_I9XX_FS_REG_TYPE_T, stage);
}

static v9x_u16 v9x_i9xx_ps11_bound(const V9X_I9XX_PS11_GEN *g,
                                   v9x_u32 stage)
{
    return (v9x_u16)((g->desc->unbound & (1ul << stage)) == 0ul);
}

/* Stage's lookup at R address into its register, or (0, 0, 0, 1) for a
 * stage with no texture. */
static void v9x_i9xx_ps11_lookup(V9X_I9XX_PS11_GEN *g, v9x_u32 opcode,
                                 v9x_u32 stage, v9x_u32 addr_type,
                                 v9x_u32 addr_nr)
{
    v9x_u32 dest = V9X_I9XX_PS11_R_TEXTURE + stage;

    if (v9x_i9xx_ps11_bound(g, stage)) {
        v9x_i9xx_fs_tex(&g->fs, opcode, V9X_I9XX_FS_REG_TYPE_R, dest, stage,
                        addr_type, addr_nr);
    } else {
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MOV, dest, V9X_I9XX_FS_MASK_XYZW,
                         V9X_FALSE, v9x_i9xx_ps11_empty(), 0ul, 0ul);
    }
    g->defined |= 1ul << dest;
}

static void v9x_i9xx_ps11_texbem(V9X_I9XX_PS11_GEN *g,
                                 const V9X_PS11_INST *inst, v9x_u32 stage)
{
    v9x_u32 work = v9x_i9xx_ps11_r(V9X_I9XX_PS11_R_WORK);
    v9x_u32 tc = v9x_i9xx_ps11_coords(stage);
    v9x_u32 bump = V9X_I9XX_FS_SRC(V9X_I9XX_FS_REG_TYPE_CONST,
                                   V9X_I9XX_PS11_CONST_BUMP + stage);
    v9x_u32 luminance = V9X_I9XX_FS_SRC(V9X_I9XX_FS_REG_TYPE_CONST,
                                        V9X_I9XX_PS11_CONST_LUMINANCE +
                                            stage);
    v9x_u32 dest = V9X_I9XX_PS11_R_TEXTURE + stage;
    v9x_u32 uv = tc;
    v9x_u32 s;

    /* An empty stage's lookup is (0, 0, 0, 1), and texbeml's scale of
     * its colour leaves it so. */
    if (!v9x_i9xx_ps11_bound(g, stage)) {
        v9x_i9xx_ps11_lookup(g, V9X_I9XX_T0_TEXLD, stage, 0ul, 0ul);
        return;
    }
    s = v9x_i9xx_ps11_source(g, &inst->src[0], 0ul);

    /* u and v over w for a projected stage, as the interpreter: the
     * offset is added after the divide. */
    if ((g->desc->projected & (1ul << stage)) != 0ul) {
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_RCP, V9X_I9XX_PS11_R_WORK,
                         V9X_I9XX_FS_MASK_W, V9X_FALSE,
                         v9x_i9xx_ps11_rep(tc, V9X_I9XX_FS_SEL_W), 0ul, 0ul);
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MUL, V9X_I9XX_PS11_R_WORK,
                         V9X_I9XX_FS_MASK_X | V9X_I9XX_FS_MASK_Y, V9X_FALSE,
                         tc, v9x_i9xx_ps11_rep(work, V9X_I9XX_FS_SEL_W), 0ul);
        uv = work;
    }

    /* u + m00 du + m10 dv, v + m01 du + m11 dv */
    v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MAD, V9X_I9XX_PS11_R_WORK,
                     V9X_I9XX_FS_MASK_X | V9X_I9XX_FS_MASK_Y, V9X_FALSE,
                     v9x_i9xx_ps11_rep(s, V9X_I9XX_FS_SEL_X),
                     v9x_i9xx_fs_swizzle(bump, V9X_I9XX_FS_SEL_X,
                                         V9X_I9XX_FS_SEL_Y,
                                         V9X_I9XX_FS_SEL_X,
                                         V9X_I9XX_FS_SEL_Y),
                     uv);
    v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MAD, V9X_I9XX_PS11_R_WORK,
                     V9X_I9XX_FS_MASK_X | V9X_I9XX_FS_MASK_Y, V9X_FALSE,
                     v9x_i9xx_ps11_rep(s, V9X_I9XX_FS_SEL_Y),
                     v9x_i9xx_fs_swizzle(bump, V9X_I9XX_FS_SEL_Z,
                                         V9X_I9XX_FS_SEL_W,
                                         V9X_I9XX_FS_SEL_Z,
                                         V9X_I9XX_FS_SEL_W),
                     work);
    v9x_i9xx_ps11_lookup(g, V9X_I9XX_T0_TEXLD, stage,
                         V9X_I9XX_FS_REG_TYPE_R, V9X_I9XX_PS11_R_WORK);
    if (inst->op != V9X_PS11_TEXBEML) {
        return;
    }

    /* texbeml: the colour by s.z scale + offset; alpha untouched. */
    v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MAD, V9X_I9XX_PS11_R_WORK,
                     V9X_I9XX_FS_MASK_X, V9X_FALSE,
                     v9x_i9xx_ps11_rep(s, V9X_I9XX_FS_SEL_Z),
                     v9x_i9xx_ps11_rep(luminance, V9X_I9XX_FS_SEL_X),
                     v9x_i9xx_ps11_rep(luminance, V9X_I9XX_FS_SEL_Y));
    v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MUL, dest, V9X_I9XX_FS_MASK_XYZ,
                     V9X_FALSE, v9x_i9xx_ps11_r(dest),
                     v9x_i9xx_ps11_rep(work, V9X_I9XX_FS_SEL_X), 0ul);
}

/* The last texm3x3 row and its lookup: the dots themselves (tex), or
 * the reflection 2 N (N.E) / (N.N) - E with the eye from a constant
 * (spec) or the three rows' w (vspec). */
static void v9x_i9xx_ps11_m3x3(V9X_I9XX_PS11_GEN *g,
                               const V9X_PS11_INST *inst, v9x_u32 stage)
{
    v9x_u32 dots = v9x_i9xx_ps11_r(V9X_I9XX_PS11_R_DOTS);
    v9x_u32 scale = v9x_i9xx_ps11_r(V9X_I9XX_PS11_R_SCALE);
    v9x_u32 eye;
    v9x_u32 n;

    n = v9x_i9xx_ps11_source(g, &inst->src[0], 0ul);
    v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_DP3, V9X_I9XX_PS11_R_DOTS,
                     V9X_I9XX_FS_MASK_Z, V9X_FALSE,
                     v9x_i9xx_ps11_coords(stage), n, 0ul);
    g->pads = 0ul;
    if (!v9x_i9xx_ps11_bound(g, stage)) {
        v9x_i9xx_ps11_lookup(g, V9X_I9XX_T0_TEXLD, stage, 0ul, 0ul);
        return;
    }
    if (inst->op == V9X_PS11_TEXM3X3TEX) {
        v9x_i9xx_ps11_lookup(g, V9X_I9XX_T0_TEXLD, stage,
                             V9X_I9XX_FS_REG_TYPE_R, V9X_I9XX_PS11_R_DOTS);
        return;
    }

    if (inst->op == V9X_PS11_TEXM3X3VSPEC) {
        v9x_u32 row;

        for (row = 0ul; row < 3ul; ++row) {
            v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MOV, V9X_I9XX_PS11_R_EYE,
                             1ul << row, V9X_FALSE,
                             v9x_i9xx_ps11_rep(
                                 v9x_i9xx_ps11_coords(stage - 2ul + row),
                                 V9X_I9XX_FS_SEL_W),
                             0ul, 0ul);
        }
        eye = v9x_i9xx_ps11_r(V9X_I9XX_PS11_R_EYE);
    } else {
        eye = V9X_I9XX_FS_SRC(V9X_I9XX_FS_REG_TYPE_CONST, inst->src[1].reg);
    }

    /* scale.x = N.E, .y = N.N, .z = 1 / N.N; then .x = 2 N.E / N.N */
    v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_DP3, V9X_I9XX_PS11_R_SCALE,
                     V9X_I9XX_FS_MASK_X, V9X_FALSE, dots, eye, 0ul);
    v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_DP3, V9X_I9XX_PS11_R_SCALE,
                     V9X_I9XX_FS_MASK_Y, V9X_FALSE, dots, dots, 0ul);
    v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_RCP, V9X_I9XX_PS11_R_SCALE,
                     V9X_I9XX_FS_MASK_Z, V9X_FALSE,
                     v9x_i9xx_ps11_rep(scale, V9X_I9XX_FS_SEL_Y), 0ul, 0ul);
    v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MUL, V9X_I9XX_PS11_R_SCALE,
                     V9X_I9XX_FS_MASK_X, V9X_FALSE, scale,
                     v9x_i9xx_ps11_rep(scale, V9X_I9XX_FS_SEL_Z), 0ul);
    v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_ADD, V9X_I9XX_PS11_R_SCALE,
                     V9X_I9XX_FS_MASK_X, V9X_FALSE, scale, scale, 0ul);
    v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MAD, V9X_I9XX_PS11_R_WORK,
                     V9X_I9XX_FS_MASK_XYZ, V9X_FALSE, dots,
                     v9x_i9xx_ps11_rep(scale, V9X_I9XX_FS_SEL_X),
                     v9x_i9xx_fs_negate(eye, V9X_I9XX_FS_MASK_XYZW));
    v9x_i9xx_ps11_lookup(g, V9X_I9XX_T0_TEXLD, stage,
                         V9X_I9XX_FS_REG_TYPE_R, V9X_I9XX_PS11_R_WORK);
}

static void v9x_i9xx_ps11_texture(V9X_I9XX_PS11_GEN *g,
                                  const V9X_PS11_INST *inst)
{
    v9x_u32 stage = inst->dst_reg;
    v9x_u32 dest = V9X_I9XX_PS11_R_TEXTURE + stage;
    v9x_u32 tc = v9x_i9xx_ps11_coords(stage);
    v9x_u32 xyz1 = v9x_i9xx_fs_swizzle(tc, V9X_I9XX_FS_SEL_X,
                                       V9X_I9XX_FS_SEL_Y, V9X_I9XX_FS_SEL_Z,
                                       V9X_I9XX_FS_SEL_ONE);
    v9x_u32 cube = (g->desc->cube & (1ul << stage)) != 0ul;

    switch (inst->op) {
    case V9X_PS11_TEX:
        /* A cube reads x, y and z as they are; a projected 2D stage u
         * and v over w (TEXLDP divides z too, which 2D does not read). */
        v9x_i9xx_ps11_lookup(g,
                             (!cube && (g->desc->projected &
                                        (1ul << stage)) != 0ul)
                                 ? V9X_I9XX_T0_TEXLDP : V9X_I9XX_T0_TEXLD,
                             stage, V9X_I9XX_FS_REG_TYPE_T, stage);
        return;

    case V9X_PS11_TEXCOORD:
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MOV, dest, V9X_I9XX_FS_MASK_XYZW,
                         V9X_TRUE, xyz1, 0ul, 0ul);
        g->defined |= 1ul << dest;
        return;

    case V9X_PS11_TEXKILL:
        /* 1.1 kills on x, y or z below zero and ignores w; TEXKILL may
         * test all four (i9xx_fs.h), so w is made one first. */
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MOV, V9X_I9XX_PS11_R_WORK,
                         V9X_I9XX_FS_MASK_XYZW, V9X_FALSE, xyz1, 0ul, 0ul);
        v9x_i9xx_fs_tex(&g->fs, V9X_I9XX_T0_TEXKILL,
                        V9X_I9XX_FS_REG_TYPE_U, 0ul, 0ul,
                        V9X_I9XX_FS_REG_TYPE_R, V9X_I9XX_PS11_R_WORK);
        return;

    case V9X_PS11_TEXBEM:
    case V9X_PS11_TEXBEML:
        if (cube) {
            g->failed = V9X_STATUS_UNSUPPORTED;
            return;
        }
        v9x_i9xx_ps11_texbem(g, inst, stage);
        return;

    case V9X_PS11_TEXM3X3PAD:
        v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_DP3, V9X_I9XX_PS11_R_DOTS,
                         (g->pads & 1ul) == 0ul ? V9X_I9XX_FS_MASK_X
                                                : V9X_I9XX_FS_MASK_Y,
                         V9X_FALSE, tc,
                         v9x_i9xx_ps11_source(g, &inst->src[0], 0ul), 0ul);
        ++g->pads;
        return;

    default:
        v9x_i9xx_ps11_m3x3(g, inst, stage);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Declarations and the program                                        */
/* ------------------------------------------------------------------ */

static v9x_u32 v9x_i9xx_ps11_sample_type(const V9X_I9XX_PS11_GEN *g,
                                         const V9X_PS11_INST *inst)
{
    if ((g->desc->cube & (1ul << inst->dst_reg)) != 0ul) {
        return V9X_I9XX_FS_D0_SAMPLE_CUBE;
    }
    if (inst->op == V9X_PS11_TEXM3X3TEX || inst->op == V9X_PS11_TEXM3X3SPEC ||
        inst->op == V9X_PS11_TEXM3X3VSPEC) {
        return V9X_I9XX_FS_D0_SAMPLE_VOLUME;
    }
    return V9X_I9XX_FS_D0_SAMPLE_2D;
}

/* Every T read and every sampler sampled, ahead of the instructions. */
static void v9x_i9xx_ps11_declare(V9X_I9XX_PS11_GEN *g)
{
    const V9X_PS11_PROGRAM *program = g->program;
    v9x_u32 i;
    v9x_u32 s;

    for (i = 0ul; i < program->count; ++i) {
        const V9X_PS11_INST *inst = &program->inst[i];
        v9x_u32 stage = inst->dst_reg;

        if (!V9X_PS11_IS_TEXTURE(inst->op)) {
            for (s = 0ul; s < inst->sources; ++s) {
                if (inst->src[s].file == V9X_PS11_FILE_INPUT) {
                    v9x_i9xx_fs_dcl_t(&g->fs,
                                      V9X_I9XX_FS_T_DIFFUSE +
                                          inst->src[s].reg,
                                      V9X_I9XX_FS_MASK_XYZW);
                }
            }
            continue;
        }
        if (inst->op != V9X_PS11_TEX || v9x_i9xx_ps11_bound(g, stage)) {
            v9x_i9xx_fs_dcl_t(&g->fs, stage, V9X_I9XX_FS_MASK_XYZW);
        }
        if (inst->op == V9X_PS11_TEXCOORD || inst->op == V9X_PS11_TEXKILL ||
            inst->op == V9X_PS11_TEXM3X3PAD ||
            !v9x_i9xx_ps11_bound(g, stage)) {
            continue;
        }
        v9x_i9xx_fs_dcl_s(&g->fs, stage,
                          v9x_i9xx_ps11_sample_type(g, inst));
    }
    if (g->desc->fog) {
        v9x_i9xx_fs_dcl_t(&g->fs, V9X_I9XX_FS_T_SPECULAR,
                          V9X_I9XX_FS_MASK_XYZW);
    }
}

/* r0 to oC; fogged, (sat(r0) - fog) f + fog with f in T9.w. */
static void v9x_i9xx_ps11_output(V9X_I9XX_PS11_GEN *g)
{
    v9x_u32 r0 = v9x_i9xx_ps11_r(0ul);
    v9x_u32 work = v9x_i9xx_ps11_r(V9X_I9XX_PS11_R_WORK);
    v9x_u32 fog = V9X_I9XX_FS_SRC(V9X_I9XX_FS_REG_TYPE_CONST,
                                  V9X_I9XX_PS11_CONST_FOG);

    v9x_i9xx_ps11_ensure(g, 0ul);
    if (!g->desc->fog) {
        v9x_i9xx_ps11_emit(g, V9X_I9XX_FS_A0_MOV, V9X_I9XX_FS_REG_TYPE_OC,
                           0ul, V9X_I9XX_FS_MASK_XYZW, V9X_FALSE, r0,
                           0ul, 0ul);
        return;
    }
    v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_MOV, V9X_I9XX_PS11_R_WORK,
                     V9X_I9XX_FS_MASK_XYZW, V9X_TRUE, r0, 0ul, 0ul);
    v9x_i9xx_ps11_op(g, V9X_I9XX_FS_A0_ADD, V9X_I9XX_PS11_R_EYE,
                     V9X_I9XX_FS_MASK_XYZ, V9X_FALSE, work,
                     v9x_i9xx_fs_negate(fog, V9X_I9XX_FS_MASK_XYZW), 0ul);
    v9x_i9xx_ps11_emit(g, V9X_I9XX_FS_A0_MAD, V9X_I9XX_FS_REG_TYPE_OC, 0ul,
                       V9X_I9XX_FS_MASK_XYZ, V9X_FALSE,
                       v9x_i9xx_ps11_r(V9X_I9XX_PS11_R_EYE),
                       v9x_i9xx_ps11_rep(
                           V9X_I9XX_FS_SRC(V9X_I9XX_FS_REG_TYPE_T,
                                           V9X_I9XX_FS_T_SPECULAR),
                           V9X_I9XX_FS_SEL_W),
                       fog);
    v9x_i9xx_ps11_emit(g, V9X_I9XX_FS_A0_MOV, V9X_I9XX_FS_REG_TYPE_OC, 0ul,
                       V9X_I9XX_FS_MASK_W, V9X_FALSE, work, 0ul, 0ul);
}

v9x_status v9x_i9xx_ps11_build(v9x_u32 *out, v9x_u32 capacity,
                               const V9X_PS11_PROGRAM *program,
                               const struct v9x_i9xx_ps11_desc *desc,
                               v9x_u32 *used)
{
    V9X_I9XX_PS11_GEN g;
    v9x_u32 i;
    v9x_status status;

    if (used == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *used = 0ul;
    if (out == 0 || program == 0 || desc == 0 ||
        program->count > V9X_PS11_MAX_INSTRUCTIONS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    g.program = program;
    g.desc = desc;
    g.defined = 0ul;
    g.pads = 0ul;
    g.failed = V9X_STATUS_OK;
    v9x_i9xx_fs_begin(&g.fs, out, capacity);

    v9x_i9xx_ps11_declare(&g);
    for (i = 0ul; i < program->count && g.failed == V9X_STATUS_OK; ++i) {
        const V9X_PS11_INST *inst = &program->inst[i];
        v9x_u32 dest;

        if (inst->op == V9X_PS11_NOP) {
            continue;
        }
        if (V9X_PS11_IS_TEXTURE(inst->op)) {
            v9x_i9xx_ps11_texture(&g, inst);
            continue;
        }

        /* A co-issued pair reads its sources before either writes: the
         * first's result waits in R6 while the second is computed into
         * its own register. */
        if (i + 1ul < program->count && program->inst[i + 1ul].coissue) {
            const V9X_PS11_INST *second = &program->inst[i + 1ul];
            v9x_u32 first_dest = v9x_i9xx_ps11_dest(&g, inst);

            v9x_i9xx_ps11_arithmetic(&g, inst, V9X_I9XX_PS11_R_FIRST);
            dest = v9x_i9xx_ps11_dest(&g, second);
            v9x_i9xx_ps11_arithmetic(&g, second, dest);
            g.defined |= 1ul << dest;
            v9x_i9xx_ps11_op(&g, V9X_I9XX_FS_A0_MOV, first_dest,
                             inst->dst_mask, V9X_FALSE,
                             v9x_i9xx_ps11_r(V9X_I9XX_PS11_R_FIRST),
                             0ul, 0ul);
            g.defined |= 1ul << first_dest;
            ++i;
            continue;
        }
        dest = v9x_i9xx_ps11_dest(&g, inst);
        v9x_i9xx_ps11_arithmetic(&g, inst, dest);
        g.defined |= 1ul << dest;
    }
    if (g.failed != V9X_STATUS_OK) {
        return g.failed;
    }
    v9x_i9xx_ps11_output(&g);
    status = v9x_i9xx_fs_end(&g.fs, used);
    return status;
}
