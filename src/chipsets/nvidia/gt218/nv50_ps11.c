/*
 * Pixel shaders 1.0 and 1.1 as Tesla fragment programs. See
 * velocity9x\nv50_ps11.h for what the program expects.
 *
 * The rules are the reference interpreter's (d3d_ps11_ref.c), as the
 * Gen3 and Haswell translators keep them: every arithmetic result
 * shifted, then saturated or held to [-1, 1]; a co-issued pair reads its
 * sources before either writes; registers read before they are written
 * read zero.
 *
 * Tesla is scalar: each channel is its own instruction. Every channel an
 * instruction writes is computed into a result register first and moved
 * to its destination after, so a destination that is also a source
 * (mul r0, r0.wzyx, ...) and a co-issued pair both read what was there
 * before. Clamps are min and max against constants, which are exact,
 * rather than the saturate bit, which covers only [0, 1].
 *
 * A constant can sit only in an instruction's second or third source
 * slot (setSrcFileBits), and mov cannot read one. Constants are moved
 * into registers as 0 + c, against a register holding zero made at the
 * start from w - w (w is 1 / an interpolated positive weight, so finite).
 *
 * Kills are emitted last, from the interpolated coordinates the
 * interpreter tests: a kill does not change any value, and keeping it
 * after every texture fetch leaves the fetches' implicit derivatives
 * with whole quads.
 *
 * Registers:
 *
 *   $r0-$r3     r0 (the colour result)
 *   $r4-$r7     r1
 *   $r8-$r23    t0-t3
 *   $r24-$r31   v0, v1
 *   $r32-$r47   coordinate sets 0-3, x y z w
 *   $r48        1 / w;  $r49 zero
 *   $r50, $r51  the texm3x3 rows' first two dot products
 *   $r52-$r55   an instruction's result channels
 *   $r56-$r59   a co-issued second instruction's
 *   $r60-$r62   a modified source, one a source slot
 *   $r63        an intermediate
 */
#include "velocity9x/nv50_ps11.h"
#include "velocity9x/nv50_shader.h"

#define V9X_NV50_PS11_R_R        0ul
#define V9X_NV50_PS11_R_T        8ul
#define V9X_NV50_PS11_R_V        24ul
#define V9X_NV50_PS11_R_TC       32ul
#define V9X_NV50_PS11_R_W        48ul
#define V9X_NV50_PS11_R_ZERO     49ul
#define V9X_NV50_PS11_R_PAD      50ul
#define V9X_NV50_PS11_R_RESULT   52ul
#define V9X_NV50_PS11_R_RESULT2  56ul
#define V9X_NV50_PS11_R_SOURCE   60ul
#define V9X_NV50_PS11_R_WORK     63ul

/* The flags register the selects and kills use. */
#define V9X_NV50_PS11_FLAGS      0ul

#define V9X_NV50_PS11_STAGES     4ul
#define V9X_NV50_PS11_CLAMP      1.0f

#define L V9X_NV50_SH_LONG
#define S V9X_NV50_SH_SHORT
#define R(n) V9X_NV50_SH_R(n)

typedef struct v9x_nv50_ps11_gen {
    struct v9x_nv50_sh sh;
    const V9X_PS11_PROGRAM *program;
    const struct v9x_nv50_ps11_desc *desc;
    v9x_u32 pads;           /* texm3x3pad rows so far */
    v9x_u32 kill_stages;    /* bit N: texkill on stage N */
    v9x_u32 unsupported;
} V9X_NV50_PS11_GEN;

typedef union v9x_nv50_ps11_float {
    float f;
    v9x_u32 bits;
} V9X_NV50_PS11_FLOAT;

static v9x_u32 v9x_nv50_ps11_bits(float value)
{
    V9X_NV50_PS11_FLOAT u;

    u.f = value;
    return u.bits;
}

static float v9x_nv50_ps11_value(v9x_u32 bits)
{
    V9X_NV50_PS11_FLOAT u;

    u.bits = bits;
    return u.f;
}

static float v9x_nv50_ps11_clamp(float value)
{
    if (!(value > -V9X_NV50_PS11_CLAMP)) {
        return -V9X_NV50_PS11_CLAMP;    /* NaN too, as the interpreter */
    }
    return value > V9X_NV50_PS11_CLAMP ? V9X_NV50_PS11_CLAMP : value;
}

void v9x_nv50_ps11_constants(const V9X_PS11_PROGRAM *program,
                             const V9X_PS11_ENV *env,
                             v9x_u32 out[V9X_NV50_PS11_C_FLOATS])
{
    v9x_u32 reg;
    v9x_u32 c;
    float value;

    for (reg = 0ul; reg < V9X_NV50_PS11_C_FLOATS; ++reg) {
        out[reg] = 0ul;
    }
    for (reg = 0ul; reg < 8ul; ++reg) {
        for (c = 0ul; c < 4ul; ++c) {
            if ((program->def_mask & (1ul << reg)) != 0ul) {
                value = v9x_nv50_ps11_value(program->def[reg][c]);
            } else {
                value = env->c[reg][c];
            }
            out[V9X_NV50_PS11_C_CONST + reg * 4ul + c] =
                v9x_nv50_ps11_bits(v9x_nv50_ps11_clamp(value));
        }
    }
    for (reg = 0ul; reg < V9X_NV50_PS11_STAGES; ++reg) {
        for (c = 0ul; c < 4ul; ++c) {
            out[V9X_NV50_PS11_C_BUMP + reg * 4ul + c] =
                v9x_nv50_ps11_bits(env->bump[reg][c]);
        }
        out[V9X_NV50_PS11_C_LUMINANCE + reg * 2ul] =
            v9x_nv50_ps11_bits(env->luminance[reg][0]);
        out[V9X_NV50_PS11_C_LUMINANCE + reg * 2ul + 1ul] =
            v9x_nv50_ps11_bits(env->luminance[reg][1]);
    }
    out[V9X_NV50_PS11_C_HALF] = v9x_nv50_ps11_bits(0.5f);
    out[V9X_NV50_PS11_C_ONE] = v9x_nv50_ps11_bits(1.0f);
    out[V9X_NV50_PS11_C_MINUS_ONE] = v9x_nv50_ps11_bits(-1.0f);
    out[V9X_NV50_PS11_C_TWO] = v9x_nv50_ps11_bits(2.0f);
    out[V9X_NV50_PS11_C_FOUR] = v9x_nv50_ps11_bits(4.0f);
}

/* The block's float index as a constant operand. */
static v9x_u32 v9x_nv50_ps11_c(const V9X_NV50_PS11_GEN *gen, v9x_u32 index)
{
    return V9X_NV50_SH_C(gen->desc->slot, index * 4ul);
}

static v9x_u32 v9x_nv50_ps11_zero(void)
{
    return R(V9X_NV50_PS11_R_ZERO);
}

/* reg = the block's float index, as 0 + c. */
static void v9x_nv50_ps11_load(V9X_NV50_PS11_GEN *gen, v9x_u32 reg,
                               v9x_u32 index)
{
    v9x_nv50_sh_fadd(&gen->sh, L, R(reg), v9x_nv50_ps11_zero(),
                     v9x_nv50_ps11_c(gen, index));
}

/*
 * Source channel c of src, swizzled and modified, as a register: the
 * shader register itself when there is nothing to do, else scratch
 * register R_SOURCE + slot. The expansions round as the interpreter's
 * expressions do: 2x is exact, so x + x then - 1 is 2x - 1.
 */
static v9x_u32 v9x_nv50_ps11_source(V9X_NV50_PS11_GEN *gen,
                                    const V9X_PS11_SRC *src, v9x_u32 c,
                                    v9x_u32 slot)
{
    v9x_u32 lane = ((v9x_u32)src->swizzle >> (2u * c)) & 3ul;
    v9x_u32 scratch = V9X_NV50_PS11_R_SOURCE + slot;
    v9x_u32 x;

    switch (src->file) {
    case V9X_PS11_FILE_TEMP:
        x = R(V9X_NV50_PS11_R_R + 4ul * src->reg + lane);
        break;
    case V9X_PS11_FILE_INPUT:
        x = R(V9X_NV50_PS11_R_V + 4ul * src->reg + lane);
        break;
    case V9X_PS11_FILE_CONST:
        v9x_nv50_ps11_load(gen, scratch,
                           V9X_NV50_PS11_C_CONST + 4ul * src->reg + lane);
        x = R(scratch);
        break;
    default:
        x = R(V9X_NV50_PS11_R_T + 4ul * src->reg + lane);
        break;
    }
    switch (src->mod) {
    case V9X_PS11_SRC_NEG:
        v9x_nv50_sh_fadd(&gen->sh, L, R(scratch), V9X_NV50_SH_NEG(x),
                         v9x_nv50_ps11_zero());
        return R(scratch);
    case V9X_PS11_SRC_BIAS:
        v9x_nv50_sh_fadd(&gen->sh, L, R(scratch), x,
                         V9X_NV50_SH_NEG(v9x_nv50_ps11_c(
                             gen, V9X_NV50_PS11_C_HALF)));
        return R(scratch);
    case V9X_PS11_SRC_BIASNEG:
        v9x_nv50_sh_fadd(&gen->sh, L, R(scratch), V9X_NV50_SH_NEG(x),
                         v9x_nv50_ps11_c(gen, V9X_NV50_PS11_C_HALF));
        return R(scratch);
    case V9X_PS11_SRC_SIGN:
        v9x_nv50_sh_fadd(&gen->sh, L, R(scratch), x, x);
        v9x_nv50_sh_fadd(&gen->sh, L, R(scratch), R(scratch),
                         V9X_NV50_SH_NEG(v9x_nv50_ps11_c(
                             gen, V9X_NV50_PS11_C_ONE)));
        return R(scratch);
    case V9X_PS11_SRC_SIGNNEG:
        v9x_nv50_sh_fadd(&gen->sh, L, R(scratch), x, x);
        v9x_nv50_sh_fadd(&gen->sh, L, R(scratch), V9X_NV50_SH_NEG(R(scratch)),
                         v9x_nv50_ps11_c(gen, V9X_NV50_PS11_C_ONE));
        return R(scratch);
    case V9X_PS11_SRC_COMP:
        v9x_nv50_sh_fadd(&gen->sh, L, R(scratch), V9X_NV50_SH_NEG(x),
                         v9x_nv50_ps11_c(gen, V9X_NV50_PS11_C_ONE));
        return R(scratch);
    default:
        return x;
    }
}

/* The interpreter's finish: shifted, then saturated or held to
 * [-1, 1]. */
static void v9x_nv50_ps11_finish(V9X_NV50_PS11_GEN *gen,
                                 const V9X_PS11_INST *inst, v9x_u32 reg)
{
    v9x_u32 scale = 0ul;

    if (inst->shift == 1l) {
        scale = V9X_NV50_PS11_C_TWO;
    } else if (inst->shift == 2l) {
        scale = V9X_NV50_PS11_C_FOUR;
    } else if (inst->shift == -1l) {
        scale = V9X_NV50_PS11_C_HALF;
    }
    if (scale != 0ul) {
        v9x_nv50_sh_fmul(&gen->sh, L, R(reg), R(reg),
                         v9x_nv50_ps11_c(gen, scale));
    }
    if (inst->saturate != 0u) {
        v9x_nv50_sh_fmax(&gen->sh, R(reg), R(reg), v9x_nv50_ps11_zero());
    } else {
        v9x_nv50_sh_fmax(&gen->sh, R(reg), R(reg),
                         v9x_nv50_ps11_c(gen, V9X_NV50_PS11_C_MINUS_ONE));
    }
    v9x_nv50_sh_fmin(&gen->sh, R(reg), R(reg),
                     v9x_nv50_ps11_c(gen, V9X_NV50_PS11_C_ONE));
}

/* An arithmetic instruction's written channels into result + c. */
static void v9x_nv50_ps11_arithmetic(V9X_NV50_PS11_GEN *gen,
                                     const V9X_PS11_INST *inst,
                                     v9x_u32 result)
{
    struct v9x_nv50_sh *sh = &gen->sh;
    v9x_u32 work = R(V9X_NV50_PS11_R_WORK);
    v9x_u32 c;
    v9x_u32 a;
    v9x_u32 b;
    v9x_u32 d;

    if (inst->op == V9X_PS11_DP3) {
        a = v9x_nv50_ps11_source(gen, &inst->src[0], 0ul, 0ul);
        b = v9x_nv50_ps11_source(gen, &inst->src[1], 0ul, 1ul);
        v9x_nv50_sh_fmul(sh, L, work, a, b);
        for (c = 1ul; c < 3ul; ++c) {
            a = v9x_nv50_ps11_source(gen, &inst->src[0], c, 0ul);
            b = v9x_nv50_ps11_source(gen, &inst->src[1], c, 1ul);
            v9x_nv50_sh_fmad(sh, L, work, a, b, work);
        }
        v9x_nv50_ps11_finish(gen, inst, V9X_NV50_PS11_R_WORK);
        for (c = 0ul; c < 4ul; ++c) {
            if ((inst->dst_mask & (1u << c)) != 0u) {
                v9x_nv50_sh_mov(sh, S, R(result + c), work);
            }
        }
        return;
    }

    for (c = 0ul; c < 4ul; ++c) {
        v9x_u32 out = R(result + c);

        if ((inst->dst_mask & (1u << c)) == 0u) {
            continue;
        }
        a = v9x_nv50_ps11_source(gen, &inst->src[0], c, 0ul);
        b = inst->sources > 1u
                ? v9x_nv50_ps11_source(gen, &inst->src[1], c, 1ul) : a;
        d = inst->sources > 2u
                ? v9x_nv50_ps11_source(gen, &inst->src[2], c, 2ul) : a;
        switch (inst->op) {
        case V9X_PS11_ADD:
            v9x_nv50_sh_fadd(sh, L, out, a, b);
            break;
        case V9X_PS11_SUB:
            v9x_nv50_sh_fadd(sh, L, out, a, V9X_NV50_SH_NEG(b));
            break;
        case V9X_PS11_MUL:
            v9x_nv50_sh_fmul(sh, L, out, a, b);
            break;
        case V9X_PS11_MAD:
            v9x_nv50_sh_fmad(sh, L, out, a, b, d);
            break;
        case V9X_PS11_LRP:
            /* a b + (1 - a) d as a (b - d) + d. */
            v9x_nv50_sh_fadd(sh, L, work, b, V9X_NV50_SH_NEG(d));
            v9x_nv50_sh_fmad(sh, L, out, a, work, d);
            break;
        case V9X_PS11_CND:
            /* a > 0.5 ? b : d, as a set into the flags and two moves
             * predicated on it. */
            v9x_nv50_sh_set(sh, V9X_NV50_SH_NONE, V9X_NV50_SH_CC_GT, a,
                            v9x_nv50_ps11_c(gen, V9X_NV50_PS11_C_HALF),
                            V9X_NV50_PS11_FLAGS);
            v9x_nv50_sh_predicate(sh, V9X_NV50_SH_CC_NE,
                                  V9X_NV50_PS11_FLAGS);
            v9x_nv50_sh_mov(sh, L, out, b);
            v9x_nv50_sh_predicate(sh, V9X_NV50_SH_CC_EQ,
                                  V9X_NV50_PS11_FLAGS);
            v9x_nv50_sh_mov(sh, L, out, d);
            break;
        default:                /* MOV */
            v9x_nv50_sh_mov(sh, S, out, a);
            break;
        }
        v9x_nv50_ps11_finish(gen, inst, result + c);
    }
}

static void v9x_nv50_ps11_write(V9X_NV50_PS11_GEN *gen,
                                const V9X_PS11_INST *inst, v9x_u32 result)
{
    v9x_u32 base = inst->dst_file == V9X_PS11_FILE_TEMP
                       ? V9X_NV50_PS11_R_R + 4ul * inst->dst_reg
                       : V9X_NV50_PS11_R_T + 4ul * inst->dst_reg;
    v9x_u32 c;

    for (c = 0ul; c < 4ul; ++c) {
        if ((inst->dst_mask & (1u << c)) != 0u) {
            v9x_nv50_sh_mov(&gen->sh, S, R(base + c), R(result + c));
        }
    }
}

/* t = (0, 0, 0, 1): an empty stage's lookup. */
static void v9x_nv50_ps11_empty(V9X_NV50_PS11_GEN *gen, v9x_u32 t)
{
    v9x_u32 c;

    for (c = 0ul; c < 3ul; ++c) {
        v9x_nv50_sh_mov(&gen->sh, S, R(t + c), v9x_nv50_ps11_zero());
    }
    v9x_nv50_ps11_load(gen, t + 3ul, V9X_NV50_PS11_C_ONE);
}

/* t, t + 1 = stage's u and v, over w when the stage is projected. */
static void v9x_nv50_ps11_uv(V9X_NV50_PS11_GEN *gen, v9x_u32 stage,
                             v9x_u32 t)
{
    v9x_u32 tc = V9X_NV50_PS11_R_TC + 4ul * stage;
    v9x_u32 work = R(V9X_NV50_PS11_R_WORK);

    if ((gen->desc->projected & (1ul << stage)) != 0ul) {
        v9x_nv50_sh_rcp(&gen->sh, L, work, R(tc + 3ul));
        v9x_nv50_sh_fmul(&gen->sh, L, R(t), R(tc), work);
        v9x_nv50_sh_fmul(&gen->sh, L, R(t + 1ul), R(tc + 1ul), work);
        return;
    }
    v9x_nv50_sh_mov(&gen->sh, S, R(t), R(tc));
    v9x_nv50_sh_mov(&gen->sh, S, R(t + 1ul), R(tc + 1ul));
}

/* A lookup on stage with count coordinates already in t onwards. */
static void v9x_nv50_ps11_lookup(V9X_NV50_PS11_GEN *gen, v9x_u32 stage,
                                 v9x_u32 count, v9x_u32 t)
{
    v9x_u32 cube = (gen->desc->cube & (1ul << stage)) != 0ul ? 1ul : 0ul;

    if ((gen->desc->unbound & (1ul << stage)) != 0ul) {
        v9x_nv50_ps11_empty(gen, t);
        return;
    }
    v9x_nv50_sh_tex_n(&gen->sh, R(t), stage, stage, 0xful,
                      cube != 0ul ? 3ul : count, cube);
}

/* dst = tc . n, the stage's coordinates with src's three channels. */
static void v9x_nv50_ps11_dot(V9X_NV50_PS11_GEN *gen, v9x_u32 stage,
                              const V9X_PS11_SRC *src, v9x_u32 dst)
{
    v9x_u32 tc = V9X_NV50_PS11_R_TC + 4ul * stage;
    v9x_u32 n[3];
    v9x_u32 c;

    for (c = 0ul; c < 3ul; ++c) {
        n[c] = v9x_nv50_ps11_source(gen, src, c, c);
    }
    v9x_nv50_sh_fmul(&gen->sh, L, R(dst), R(tc), n[0]);
    v9x_nv50_sh_fmad(&gen->sh, L, R(dst), R(tc + 1ul), n[1], R(dst));
    v9x_nv50_sh_fmad(&gen->sh, L, R(dst), R(tc + 2ul), n[2], R(dst));
}

static void v9x_nv50_ps11_texture(V9X_NV50_PS11_GEN *gen,
                                  const V9X_PS11_INST *inst)
{
    struct v9x_nv50_sh *sh = &gen->sh;
    v9x_u32 stage = inst->dst_reg;
    v9x_u32 t = V9X_NV50_PS11_R_T + 4ul * stage;
    v9x_u32 tc = V9X_NV50_PS11_R_TC + 4ul * stage;
    v9x_u32 work = R(V9X_NV50_PS11_R_WORK);
    v9x_u32 cube = (gen->desc->cube & (1ul << stage)) != 0ul ? 1ul : 0ul;
    v9x_u32 c;

    switch (inst->op) {
    case V9X_PS11_TEX:
        if (cube != 0ul) {
            for (c = 0ul; c < 3ul; ++c) {
                v9x_nv50_sh_mov(sh, S, R(t + c), R(tc + c));
            }
            v9x_nv50_ps11_lookup(gen, stage, 3ul, t);
            return;
        }
        v9x_nv50_ps11_uv(gen, stage, t);
        v9x_nv50_ps11_lookup(gen, stage, 2ul, t);
        return;

    case V9X_PS11_TEXCOORD:
        for (c = 0ul; c < 3ul; ++c) {
            v9x_nv50_sh_fmax(sh, R(t + c), R(tc + c), v9x_nv50_ps11_zero());
            v9x_nv50_sh_fmin(sh, R(t + c), R(t + c),
                             v9x_nv50_ps11_c(gen, V9X_NV50_PS11_C_ONE));
        }
        v9x_nv50_ps11_load(gen, t + 3ul, V9X_NV50_PS11_C_ONE);
        return;

    case V9X_PS11_TEXKILL:
        gen->kill_stages |= 1ul << stage;
        return;

    case V9X_PS11_TEXBEM:
    case V9X_PS11_TEXBEML:
    {
        v9x_u32 bump = V9X_NV50_PS11_C_BUMP + 4ul * stage;
        v9x_u32 du;
        v9x_u32 dv;

        if (cube != 0ul) {
            gen->unsupported = 1ul;
            return;
        }
        du = v9x_nv50_ps11_source(gen, &inst->src[0], 0ul, 0ul);
        dv = v9x_nv50_ps11_source(gen, &inst->src[0], 1ul, 1ul);
        v9x_nv50_ps11_uv(gen, stage, t);
        /* u + m00 du + m10 dv, v + m01 du + m11 dv, in that order. */
        v9x_nv50_sh_fmad(sh, L, R(t), du, v9x_nv50_ps11_c(gen, bump),
                         R(t));
        v9x_nv50_sh_fmad(sh, L, R(t), dv, v9x_nv50_ps11_c(gen, bump + 2ul),
                         R(t));
        v9x_nv50_sh_fmad(sh, L, R(t + 1ul), du,
                         v9x_nv50_ps11_c(gen, bump + 1ul), R(t + 1ul));
        v9x_nv50_sh_fmad(sh, L, R(t + 1ul), dv,
                         v9x_nv50_ps11_c(gen, bump + 3ul), R(t + 1ul));
        v9x_nv50_ps11_lookup(gen, stage, 2ul, t);
        if (inst->op == V9X_PS11_TEXBEML) {
            v9x_u32 lum = V9X_NV50_PS11_C_LUMINANCE + 2ul * stage;
            v9x_u32 z = v9x_nv50_ps11_source(gen, &inst->src[0], 2ul, 2ul);

            v9x_nv50_sh_fmul(sh, L, work, z, v9x_nv50_ps11_c(gen, lum));
            v9x_nv50_sh_fadd(sh, L, work, work,
                             v9x_nv50_ps11_c(gen, lum + 1ul));
            for (c = 0ul; c < 3ul; ++c) {
                v9x_nv50_sh_fmul(sh, S, R(t + c), R(t + c), work);
            }
        }
        return;
    }

    case V9X_PS11_TEXM3X3PAD:
        v9x_nv50_ps11_dot(gen, stage, &inst->src[0],
                          V9X_NV50_PS11_R_PAD + (gen->pads & 1ul));
        ++gen->pads;
        return;

    default:
    {
        /* The third row, then the lookup with the three dots N, or with
         * the reflection R = 2 (N.E) / (N.N) N - E. */
        v9x_u32 e[3];
        v9x_u32 nn = R(V9X_NV50_PS11_R_PAD);

        v9x_nv50_ps11_dot(gen, stage, &inst->src[0], t + 2ul);
        v9x_nv50_sh_mov(sh, S, R(t), R(V9X_NV50_PS11_R_PAD));
        v9x_nv50_sh_mov(sh, S, R(t + 1ul), R(V9X_NV50_PS11_R_PAD + 1ul));
        gen->pads = 0ul;
        if (inst->op == V9X_PS11_TEXM3X3TEX) {
            v9x_nv50_ps11_lookup(gen, stage, 3ul, t);
            return;
        }
        if (inst->op == V9X_PS11_TEXM3X3VSPEC) {
            e[0] = R(V9X_NV50_PS11_R_TC + 4ul * (stage - 2ul) + 3ul);
            e[1] = R(V9X_NV50_PS11_R_TC + 4ul * (stage - 1ul) + 3ul);
            e[2] = R(tc + 3ul);
        } else {
            for (c = 0ul; c < 3ul; ++c) {
                v9x_nv50_ps11_load(gen, V9X_NV50_PS11_R_SOURCE + c,
                                   V9X_NV50_PS11_C_CONST +
                                       4ul * inst->src[1].reg + c);
                e[c] = R(V9X_NV50_PS11_R_SOURCE + c);
            }
        }
        v9x_nv50_sh_fmul(sh, L, work, R(t), e[0]);
        v9x_nv50_sh_fmad(sh, L, work, R(t + 1ul), e[1], work);
        v9x_nv50_sh_fmad(sh, L, work, R(t + 2ul), e[2], work);
        v9x_nv50_sh_fmul(sh, L, nn, R(t), R(t));
        v9x_nv50_sh_fmad(sh, L, nn, R(t + 1ul), R(t + 1ul), nn);
        v9x_nv50_sh_fmad(sh, L, nn, R(t + 2ul), R(t + 2ul), nn);
        v9x_nv50_sh_rcp(sh, L, nn, nn);
        v9x_nv50_sh_fadd(sh, L, work, work, work);
        v9x_nv50_sh_fmul(sh, L, work, work, nn);
        for (c = 0ul; c < 3ul; ++c) {
            v9x_nv50_sh_fmad(sh, L, R(t + c), work, R(t + c),
                             V9X_NV50_SH_NEG(e[c]));
        }
        v9x_nv50_ps11_lookup(gen, stage, 3ul, t);
        return;
    }
    }
}

/* Coordinate sets the program reads, and stages a texture instruction
 * writes. */
static void v9x_nv50_ps11_scan(const V9X_PS11_PROGRAM *program,
                               v9x_u32 *coords, v9x_u32 *written)
{
    v9x_u32 i;

    *coords = 0ul;
    *written = 0ul;
    for (i = 0ul; i < program->count; ++i) {
        const V9X_PS11_INST *inst = &program->inst[i];

        if (!V9X_PS11_IS_TEXTURE(inst->op)) {
            continue;
        }
        *coords |= 1ul << inst->dst_reg;
        if (inst->op != V9X_PS11_TEXKILL) {
            *written |= 1ul << inst->dst_reg;
        }
        if (inst->op == V9X_PS11_TEXM3X3VSPEC && inst->dst_reg >= 2u) {
            *coords |= 3ul << (inst->dst_reg - 2u);
        }
    }
}

v9x_status v9x_nv50_ps11_build(v9x_u32 *words, v9x_u32 capacity,
                               const V9X_PS11_PROGRAM *program,
                               const struct v9x_nv50_ps11_desc *desc,
                               struct v9x_nv50_ps11_info *info)
{
    V9X_NV50_PS11_GEN gen;
    struct v9x_nv50_sh *sh = &gen.sh;
    v9x_u32 w = R(V9X_NV50_PS11_R_W);
    v9x_u32 coords;
    v9x_u32 written;
    v9x_u32 stage;
    v9x_u32 i;
    v9x_u32 c;

    if (words == 0 || program == 0 || desc == 0 || info == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_nv50_sh_init(sh, words, capacity);
    gen.program = program;
    gen.desc = desc;
    gen.pads = 0ul;
    gen.kill_stages = 0ul;
    gen.unsupported = 0ul;
    v9x_nv50_ps11_scan(program, &coords, &written);

    /* 1 / w, zero, then the inputs the program reads. */
    v9x_nv50_sh_interp(sh, w, V9X_NV50_SH_V(V9X_NV50_PS11_V_W),
                       V9X_NV50_SH_NONE);
    v9x_nv50_sh_rcp(sh, S, w, w);
    v9x_nv50_sh_fadd(sh, L, v9x_nv50_ps11_zero(), w, V9X_NV50_SH_NEG(w));
    for (i = 0ul; i < 2ul; ++i) {
        if ((program->inputs & (1ul << i)) == 0ul) {
            continue;
        }
        for (c = 0ul; c < 4ul; ++c) {
            v9x_nv50_sh_interp(sh, R(V9X_NV50_PS11_R_V + 4ul * i + c),
                               V9X_NV50_SH_V(V9X_NV50_PS11_V_DIFFUSE +
                                             0x10ul * i + 4ul * c),
                               w);
        }
    }
    for (stage = 0ul; stage < V9X_NV50_PS11_STAGES; ++stage) {
        if ((coords & (1ul << stage)) == 0ul) {
            continue;
        }
        for (c = 0ul; c < 4ul; ++c) {
            v9x_nv50_sh_interp(
                sh, R(V9X_NV50_PS11_R_TC + 4ul * stage + c),
                V9X_NV50_SH_V(V9X_NV50_PS11_V_COORDS +
                              V9X_NV50_PS11_V_COORD_STRIDE * stage +
                              4ul * c),
                w);
        }
    }
    /* r0, r1 and the stages no texture instruction writes read zero. */
    for (c = 0ul; c < 8ul; ++c) {
        v9x_nv50_sh_mov(sh, S, R(V9X_NV50_PS11_R_R + c), v9x_nv50_ps11_zero());
    }
    for (stage = 0ul; stage < V9X_NV50_PS11_STAGES; ++stage) {
        if ((written & (1ul << stage)) != 0ul) {
            continue;
        }
        for (c = 0ul; c < 4ul; ++c) {
            v9x_nv50_sh_mov(sh, S, R(V9X_NV50_PS11_R_T + 4ul * stage + c),
                            v9x_nv50_ps11_zero());
        }
    }

    for (i = 0ul; i < program->count; ++i) {
        const V9X_PS11_INST *inst = &program->inst[i];

        if (inst->op == V9X_PS11_NOP) {
            continue;
        }
        if (V9X_PS11_IS_TEXTURE(inst->op)) {
            v9x_nv50_ps11_texture(&gen, inst);
            continue;
        }
        if (i + 1ul < program->count && program->inst[i + 1ul].coissue) {
            v9x_nv50_ps11_arithmetic(&gen, inst, V9X_NV50_PS11_R_RESULT);
            v9x_nv50_ps11_arithmetic(&gen, &program->inst[i + 1ul],
                                     V9X_NV50_PS11_R_RESULT2);
            v9x_nv50_ps11_write(&gen, inst, V9X_NV50_PS11_R_RESULT);
            v9x_nv50_ps11_write(&gen, &program->inst[i + 1ul],
                                V9X_NV50_PS11_R_RESULT2);
            ++i;
            continue;
        }
        v9x_nv50_ps11_arithmetic(&gen, inst, V9X_NV50_PS11_R_RESULT);
        v9x_nv50_ps11_write(&gen, inst, V9X_NV50_PS11_R_RESULT);
    }

    /* texkill: any of the stage's x, y, z below zero. */
    for (stage = 0ul; stage < V9X_NV50_PS11_STAGES; ++stage) {
        if ((gen.kill_stages & (1ul << stage)) == 0ul) {
            continue;
        }
        for (c = 0ul; c < 3ul; ++c) {
            v9x_nv50_sh_set(sh, V9X_NV50_SH_NONE, V9X_NV50_SH_CC_LT,
                            R(V9X_NV50_PS11_R_TC + 4ul * stage + c),
                            v9x_nv50_ps11_zero(), V9X_NV50_PS11_FLAGS);
            v9x_nv50_sh_predicate(sh, V9X_NV50_SH_CC_NE, V9X_NV50_PS11_FLAGS);
            v9x_nv50_sh_kil(sh);
        }
    }

    /* End on a long instruction that is not a kill. */
    if (sh->last_size != V9X_NV50_SH_LONG || sh->last_flow != 0ul) {
        v9x_nv50_sh_mov(sh, L, R(3), R(3));
    }
    v9x_nv50_sh_exit(sh);

    if (gen.unsupported != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (sh->overflow != 0ul) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    if (sh->invalid != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    info->words = sh->count;
    info->registers = V9X_NV50_PS11_REGISTERS;
    info->kills = gen.kill_stages != 0ul ? 1ul : 0ul;
    info->coords = coords;
    return V9X_STATUS_OK;
}
