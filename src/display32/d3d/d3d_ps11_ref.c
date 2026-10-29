/*
 * The PS 1.0/1.1 reference interpreter (include\velocity9x\ps11.h): the
 * rules of docs\specifications\ddi8-on-win9x.md section 6 in plain C, one
 * pixel at a time. Host only, as r3d_tnl_ref.c is: it is not linked into
 * the HAL. The Gen7.5 translator's kernels are held to it
 * (tests\host\test_intel_hsw_ps11.c).
 *
 * Choices where the sources leave a gap, which the translator makes the
 * same way:
 * - texbeml multiplies the colour channels by the luminance term, not
 *   alpha, and does not clamp the term (Wine clamps it in fixed function
 *   only);
 * - projection divides u and v by w, for tex and texbem only;
 * - texm3x3tex samples the three dot products as they are, a cube
 *   stage's divided by their largest magnitude, as every cube lookup is.
 */
#include "velocity9x/ps11.h"

#define V9X_PS11_REF_ONE 1.0f

static float v9x_ps11_ref_clamp(float value, float low, float high)
{
    if (!(value > low)) {
        return low;     /* NaN too */
    }
    return value > high ? high : value;
}

static float v9x_ps11_ref_abs(float value)
{
    return value < 0.0f ? -value : value;
}

typedef struct v9x_ps11_ref {
    const V9X_PS11_PROGRAM *program;
    const V9X_PS11_ENV *env;
    const V9X_PS11_PIXEL *pixel;
    v9x_ps11_sample_fn sample;
    void *user;
    float r[2][4];
    float t[4][4];
    float pad[2];           /* the texm3x3 sequence's first two dots */
    v9x_u32 pads;           /* how many of them are in               */
    int killed;
} V9X_PS11_REF;

/* A constant: the shader's def, else the application's; clamped. */
static float v9x_ps11_ref_const(const V9X_PS11_REF *ref, v9x_u32 reg,
                                v9x_u32 c)
{
    float value;

    if ((ref->program->def_mask & (1ul << reg)) != 0ul) {
        v9x_u32 bits = ref->program->def[reg][c];
        float *as_float = (float *)&bits;

        value = *as_float;
    } else {
        value = ref->env->c[reg][c];
    }
    return v9x_ps11_ref_clamp(value, -V9X_PS11_REF_ONE, V9X_PS11_REF_ONE);
}

/* Source channel c: the register, swizzled, then modified. */
static float v9x_ps11_ref_source(const V9X_PS11_REF *ref,
                                 const V9X_PS11_SRC *src, v9x_u32 c)
{
    v9x_u32 lane = ((v9x_u32)src->swizzle >> (2u * c)) & 3u;
    float x;

    switch (src->file) {
    case V9X_PS11_FILE_TEMP:    x = ref->r[src->reg][lane]; break;
    case V9X_PS11_FILE_INPUT:   x = ref->pixel->v[src->reg][lane]; break;
    case V9X_PS11_FILE_CONST:   x = v9x_ps11_ref_const(ref, src->reg, lane);
        break;
    default:                    x = ref->t[src->reg][lane]; break;
    }
    switch (src->mod) {
    case V9X_PS11_SRC_NEG:     return -x;
    case V9X_PS11_SRC_BIAS:    return x - 0.5f;
    case V9X_PS11_SRC_BIASNEG: return 0.5f - x;
    case V9X_PS11_SRC_SIGN:    return 2.0f * x - 1.0f;
    case V9X_PS11_SRC_SIGNNEG: return 1.0f - 2.0f * x;
    case V9X_PS11_SRC_COMP:    return 1.0f - x;
    default:                   return x;
    }
}

/* An arithmetic result: shifted, then saturated or held to [-1, 1]. */
static float v9x_ps11_ref_finish(const V9X_PS11_INST *inst, float value)
{
    if (inst->shift == 1l) {
        value *= 2.0f;
    } else if (inst->shift == 2l) {
        value *= 4.0f;
    } else if (inst->shift == -1l) {
        value *= 0.5f;
    }
    if (inst->saturate != 0u) {
        return v9x_ps11_ref_clamp(value, 0.0f, V9X_PS11_REF_ONE);
    }
    return v9x_ps11_ref_clamp(value, -V9X_PS11_REF_ONE, V9X_PS11_REF_ONE);
}

/* One arithmetic instruction's four channels, before they are written. */
static void v9x_ps11_ref_arithmetic(const V9X_PS11_REF *ref,
                                    const V9X_PS11_INST *inst, float out[4])
{
    v9x_u32 c;

    if (inst->op == V9X_PS11_DP3) {
        float sum = 0.0f;

        for (c = 0ul; c < 3ul; ++c) {
            sum += v9x_ps11_ref_source(ref, &inst->src[0], c) *
                   v9x_ps11_ref_source(ref, &inst->src[1], c);
        }
        for (c = 0ul; c < 4ul; ++c) {
            out[c] = v9x_ps11_ref_finish(inst, sum);
        }
        return;
    }
    for (c = 0ul; c < 4ul; ++c) {
        float a = inst->sources > 0u
                      ? v9x_ps11_ref_source(ref, &inst->src[0], c) : 0.0f;
        float b = inst->sources > 1u
                      ? v9x_ps11_ref_source(ref, &inst->src[1], c) : 0.0f;
        float d = inst->sources > 2u
                      ? v9x_ps11_ref_source(ref, &inst->src[2], c) : 0.0f;
        float value;

        switch (inst->op) {
        case V9X_PS11_ADD: value = a + b; break;
        case V9X_PS11_SUB: value = a - b; break;
        case V9X_PS11_MUL: value = a * b; break;
        case V9X_PS11_MAD: value = a * b + d; break;
        case V9X_PS11_LRP: value = a * b + (1.0f - a) * d; break;
        case V9X_PS11_CND: value = a > 0.5f ? b : d; break;
        default:           value = a; break;  /* MOV */
        }
        out[c] = v9x_ps11_ref_finish(inst, value);
    }
}

static void v9x_ps11_ref_write(V9X_PS11_REF *ref, const V9X_PS11_INST *inst,
                               const float value[4])
{
    float *dst = inst->dst_file == V9X_PS11_FILE_TEMP
                     ? ref->r[inst->dst_reg] : ref->t[inst->dst_reg];
    v9x_u32 c;

    for (c = 0ul; c < 4ul; ++c) {
        if ((inst->dst_mask & (1u << c)) != 0u) {
            dst[c] = value[c];
        }
    }
}

/* A lookup on stage: a cube's three coordinates over their largest
 * magnitude first. */
static void v9x_ps11_ref_lookup(const V9X_PS11_REF *ref, v9x_u32 stage,
                                v9x_u32 count, float *coords, float rgba[4])
{
    if ((ref->env->cube & (1ul << stage)) != 0ul) {
        float largest = v9x_ps11_ref_abs(coords[0]);

        if (v9x_ps11_ref_abs(coords[1]) > largest) {
            largest = v9x_ps11_ref_abs(coords[1]);
        }
        if (v9x_ps11_ref_abs(coords[2]) > largest) {
            largest = v9x_ps11_ref_abs(coords[2]);
        }
        coords[0] /= largest;
        coords[1] /= largest;
        coords[2] /= largest;
        count = 3ul;
    }
    ref->sample(ref->user, stage, count, coords, rgba);
}

/* Stage's u and v, over w when the stage is projected. */
static void v9x_ps11_ref_uv(const V9X_PS11_REF *ref, v9x_u32 stage,
                            float uv[2])
{
    const float *tc = ref->pixel->tc[stage];

    uv[0] = tc[0];
    uv[1] = tc[1];
    if ((ref->env->projected & (1ul << stage)) != 0ul) {
        uv[0] /= tc[3];
        uv[1] /= tc[3];
    }
}

/* tN.rgb as a texm3x3 or texbem source reads it, modifiers applied. */
static void v9x_ps11_ref_vector(const V9X_PS11_REF *ref,
                                const V9X_PS11_SRC *src, float v[3])
{
    v9x_u32 c;

    for (c = 0ul; c < 3ul; ++c) {
        v[c] = v9x_ps11_ref_source(ref, src, c);
    }
}

static void v9x_ps11_ref_texture(V9X_PS11_REF *ref, const V9X_PS11_INST *inst)
{
    v9x_u32 stage = inst->dst_reg;
    const float *tc = ref->pixel->tc[stage];
    float *t = ref->t[stage];
    float coords[3];
    float n[3];
    float uv[2];
    v9x_u32 c;

    switch (inst->op) {
    case V9X_PS11_TEX:
        if ((ref->env->cube & (1ul << stage)) != 0ul) {
            coords[0] = tc[0];
            coords[1] = tc[1];
            coords[2] = tc[2];
            v9x_ps11_ref_lookup(ref, stage, 3ul, coords, t);
            return;
        }
        v9x_ps11_ref_uv(ref, stage, uv);
        coords[0] = uv[0];
        coords[1] = uv[1];
        v9x_ps11_ref_lookup(ref, stage, 2ul, coords, t);
        return;

    case V9X_PS11_TEXCOORD:
        for (c = 0ul; c < 3ul; ++c) {
            t[c] = v9x_ps11_ref_clamp(tc[c], 0.0f, V9X_PS11_REF_ONE);
        }
        t[3] = V9X_PS11_REF_ONE;
        return;

    case V9X_PS11_TEXKILL:
        if (tc[0] < 0.0f || tc[1] < 0.0f || tc[2] < 0.0f) {
            ref->killed = 1;
        }
        return;

    case V9X_PS11_TEXBEM:
    case V9X_PS11_TEXBEML:
    {
        const float *m = ref->env->bump[stage];
        float du = v9x_ps11_ref_source(ref, &inst->src[0], 0ul);
        float dv = v9x_ps11_ref_source(ref, &inst->src[0], 1ul);

        v9x_ps11_ref_uv(ref, stage, uv);
        coords[0] = uv[0] + m[0] * du + m[2] * dv;
        coords[1] = uv[1] + m[1] * du + m[3] * dv;
        v9x_ps11_ref_lookup(ref, stage, 2ul, coords, t);
        if (inst->op == V9X_PS11_TEXBEML) {
            float l = v9x_ps11_ref_source(ref, &inst->src[0], 2ul) *
                          ref->env->luminance[stage][0] +
                      ref->env->luminance[stage][1];

            for (c = 0ul; c < 3ul; ++c) {
                t[c] *= l;
            }
        }
        return;
    }

    case V9X_PS11_TEXM3X3PAD:
        v9x_ps11_ref_vector(ref, &inst->src[0], n);
        ref->pad[ref->pads & 1ul] =
            tc[0] * n[0] + tc[1] * n[1] + tc[2] * n[2];
        ++ref->pads;
        return;

    default:
    {
        /* The third row, then the lookup with the three dots N. */
        float dots[3];
        float e[3];

        v9x_ps11_ref_vector(ref, &inst->src[0], n);
        dots[0] = ref->pad[0];
        dots[1] = ref->pad[1];
        dots[2] = tc[0] * n[0] + tc[1] * n[1] + tc[2] * n[2];
        ref->pads = 0ul;
        if (inst->op == V9X_PS11_TEXM3X3TEX) {
            v9x_ps11_ref_lookup(ref, stage, 3ul, dots, t);
            return;
        }
        if (inst->op == V9X_PS11_TEXM3X3VSPEC) {
            e[0] = ref->pixel->tc[stage - 2ul][3];
            e[1] = ref->pixel->tc[stage - 1ul][3];
            e[2] = tc[3];
        } else {
            for (c = 0ul; c < 3ul; ++c) {
                e[c] = v9x_ps11_ref_const(ref, inst->src[1].reg, c);
            }
        }
        {
            /* R = 2 (N.E) / (N.N) N - E */
            float ne = dots[0] * e[0] + dots[1] * e[1] + dots[2] * e[2];
            float nn = dots[0] * dots[0] + dots[1] * dots[1] +
                       dots[2] * dots[2];
            float k = 2.0f * ne / nn;

            for (c = 0ul; c < 3ul; ++c) {
                coords[c] = k * dots[c] - e[c];
            }
        }
        v9x_ps11_ref_lookup(ref, stage, 3ul, coords, t);
        return;
    }
    }
}

int v9x_ps11_run(const V9X_PS11_PROGRAM *program, const V9X_PS11_ENV *env,
                 const V9X_PS11_PIXEL *pixel, v9x_ps11_sample_fn sample,
                 void *user, float out[4])
{
    V9X_PS11_REF ref;
    v9x_u32 i;
    v9x_u32 c;

    ref.program = program;
    ref.env = env;
    ref.pixel = pixel;
    ref.sample = sample;
    ref.user = user;
    ref.pad[0] = 0.0f;
    ref.pad[1] = 0.0f;
    ref.pads = 0ul;
    ref.killed = 0;
    for (c = 0ul; c < 4ul; ++c) {
        ref.r[0][c] = 0.0f;
        ref.r[1][c] = 0.0f;
        ref.t[0][c] = 0.0f;
        ref.t[1][c] = 0.0f;
        ref.t[2][c] = 0.0f;
        ref.t[3][c] = 0.0f;
    }

    for (i = 0ul; i < program->count; ++i) {
        const V9X_PS11_INST *inst = &program->inst[i];

        if (inst->op == V9X_PS11_NOP) {
            continue;
        }
        if (V9X_PS11_IS_TEXTURE(inst->op)) {
            v9x_ps11_ref_texture(&ref, inst);
            continue;
        }
        /* A co-issued pair reads its sources before either writes. */
        if (i + 1ul < program->count && program->inst[i + 1ul].coissue) {
            float first[4];
            float second[4];

            v9x_ps11_ref_arithmetic(&ref, inst, first);
            v9x_ps11_ref_arithmetic(&ref, &program->inst[i + 1ul], second);
            v9x_ps11_ref_write(&ref, inst, first);
            v9x_ps11_ref_write(&ref, &program->inst[i + 1ul], second);
            ++i;
            continue;
        }
        {
            float value[4];

            v9x_ps11_ref_arithmetic(&ref, inst, value);
            v9x_ps11_ref_write(&ref, inst, value);
        }
    }
    for (c = 0ul; c < 4ul; ++c) {
        out[c] = ref.r[0][c];
    }
    return ref.killed ? 0 : 1;
}
