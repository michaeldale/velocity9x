/*
 * The Direct3D core's pixel shader state: d3d_ps.h. Pure.
 */
#include "d3d_ps.h"

/* D3DTEXF_POINT and D3DTADDRESS_WRAP, a stage's defaults (d3d8types.h). */
#define V9X_D3D_PS_FILTER_POINT 1u
#define V9X_D3D_PS_ADDRESS_WRAP 1u
/* TEXCOORDINDEX's set is its low half; the high half is the generation
 * mode, which only a T&L pipeline acts on. */
#define V9X_D3D_PS_SET_MASK     0x0000fffful

static float v9x_d3d_ps_float(v9x_u32 bits)
{
    union {
        v9x_u32 bits;
        float value;
    } convert;

    convert.bits = bits;
    return convert.value;
}

void v9x_d3d_ps_stage_reset(V9X_D3D_PS_STAGE *stage, v9x_u32 index)
{
    v9x_u32 i;

    stage->texture = 0ul;
    stage->min_filter = V9X_D3D_PS_FILTER_POINT;
    stage->mag_filter = V9X_D3D_PS_FILTER_POINT;
    stage->mip_filter = 0ul;
    stage->address_u = V9X_D3D_PS_ADDRESS_WRAP;
    stage->address_v = V9X_D3D_PS_ADDRESS_WRAP;
    for (i = 0ul; i < 4ul; ++i) {
        stage->bump[i] = 0.0f;
    }
    stage->lscale = 0.0f;
    stage->loffset = 0.0f;
    stage->texcoord = index;
    stage->transform = 0ul;
}

int v9x_d3d_ps_stage_state(V9X_D3D_PS_STAGE *stage, v9x_u32 state,
                           v9x_u32 value)
{
    switch (state) {
    case V9X_D3D_PS_TSS_TEXTUREMAP:
        stage->texture = value;
        return 1;
    case V9X_D3D_PS_TSS_BUMPENVMAT00:
    case V9X_D3D_PS_TSS_BUMPENVMAT01:
    case V9X_D3D_PS_TSS_BUMPENVMAT10:
    case V9X_D3D_PS_TSS_BUMPENVMAT11:
        stage->bump[state - V9X_D3D_PS_TSS_BUMPENVMAT00] =
            v9x_d3d_ps_float(value);
        return 1;
    case V9X_D3D_PS_TSS_BUMPENVLSCALE:
        stage->lscale = v9x_d3d_ps_float(value);
        return 1;
    case V9X_D3D_PS_TSS_BUMPENVLOFFSET:
        stage->loffset = v9x_d3d_ps_float(value);
        return 1;
    case V9X_D3D_PS_TSS_TEXCOORDINDEX:
        stage->texcoord = value;
        return 1;
    case V9X_D3D_PS_TSS_ADDRESS:
        /* DirectX 6's state sets both. */
        stage->address_u = value;
        stage->address_v = value;
        return 1;
    case V9X_D3D_PS_TSS_ADDRESSU:
        stage->address_u = value;
        return 1;
    case V9X_D3D_PS_TSS_ADDRESSV:
        stage->address_v = value;
        return 1;
    case V9X_D3D_PS_TSS_MAGFILTER:
        stage->mag_filter = value;
        return 1;
    case V9X_D3D_PS_TSS_MINFILTER:
        stage->min_filter = value;
        return 1;
    case V9X_D3D_PS_TSS_MIPFILTER:
        stage->mip_filter = value;
        return 1;
    case V9X_D3D_PS_TSS_TRANSFORMFLAGS:
        stage->transform = value;
        return 1;
    default:
        return 0;
    }
}

void v9x_d3d_ps_set_constants(float c[8][4], v9x_u32 first, v9x_u32 count,
                              const v9x_u32 *bits)
{
    v9x_u32 reg;
    v9x_u32 i;

    for (reg = 0ul; reg < count; ++reg) {
        if (first + reg >= 8ul) {
            return;
        }
        for (i = 0ul; i < 4ul; ++i) {
            c[first + reg][i] = v9x_d3d_ps_float(bits[reg * 4ul + i]);
        }
    }
}

void v9x_d3d_ps_env(const float c[8][4],
                    const V9X_D3D_PS_STAGE stage[V9X_D3D_PS_STAGES],
                    v9x_u32 cube, V9X_PS11_ENV *env)
{
    v9x_u32 s;
    v9x_u32 i;

    for (s = 0ul; s < 8ul; ++s) {
        for (i = 0ul; i < 4ul; ++i) {
            env->c[s][i] = c[s][i];
        }
    }
    env->cube = cube;
    env->projected = 0ul;
    for (s = 0ul; s < V9X_D3D_PS_STAGES; ++s) {
        for (i = 0ul; i < 4ul; ++i) {
            env->bump[s][i] = stage[s].bump[i];
        }
        env->luminance[s][0] = stage[s].lscale;
        env->luminance[s][1] = stage[s].loffset;
        if ((stage[s].transform & V9X_D3D_PS_TTFF_PROJECTED) != 0ul) {
            env->projected |= 1ul << s;
        }
    }
}

/* A D3DCOLOR (A R G B from the top byte down) as r g b a in [0, 1]. */
static void v9x_d3d_ps_color(v9x_u32 color, float *out)
{
    out[0] = (float)((color >> 16) & 0xfful) / 255.0f;
    out[1] = (float)((color >> 8) & 0xfful) / 255.0f;
    out[2] = (float)(color & 0xfful) / 255.0f;
    out[3] = (float)((color >> 24) & 0xfful) / 255.0f;
}

static v9x_u32 v9x_d3d_ps_word(const v9x_u8 *at)
{
    return (v9x_u32)at[0] | ((v9x_u32)at[1] << 8) |
           ((v9x_u32)at[2] << 16) | ((v9x_u32)at[3] << 24);
}

void v9x_d3d_ps_wide_vertex(const V9X_DP2_FVF *layout,
                            const V9X_D3D_PS_STAGE stage[V9X_D3D_PS_STAGES],
                            const v9x_u8 *source,
                            float out[V9X_D3D_PS_WIDE_FLOATS])
{
    v9x_u32 s;
    v9x_u32 i;
    v9x_u32 set;

    /* XYZRHW leads every layout v9x_dp2_fvf_layout accepts. */
    for (i = 0ul; i < 4ul; ++i) {
        out[i] = v9x_d3d_ps_float(v9x_d3d_ps_word(source + 4ul * i));
    }
    if (layout->diffuse != V9X_DP2_FVF_ABSENT) {
        v9x_d3d_ps_color(v9x_d3d_ps_word(source + layout->diffuse), out + 4);
    } else {
        for (i = 4ul; i < 8ul; ++i) {
            out[i] = 1.0f;
        }
    }
    if (layout->specular != V9X_DP2_FVF_ABSENT) {
        v9x_d3d_ps_color(v9x_d3d_ps_word(source + layout->specular),
                         out + 8);
    } else {
        for (i = 8ul; i < 11ul; ++i) {
            out[i] = 0.0f;
        }
        out[11] = 1.0f;
    }
    for (s = 0ul; s < V9X_D3D_PS_STAGES; ++s) {
        float *t = out + 12ul + 4ul * s;

        t[0] = 0.0f;
        t[1] = 0.0f;
        t[2] = 0.0f;
        t[3] = 1.0f;
        set = stage[s].texcoord & V9X_D3D_PS_SET_MASK;
        if (set >= layout->tex_sets) {
            continue;
        }
        for (i = 0ul; i < layout->tex_floats[set] && i < 4ul; ++i) {
            t[i] = v9x_d3d_ps_float(
                v9x_d3d_ps_word(source + layout->tex[set] + 4ul * i));
        }
    }
}

v9x_u32 v9x_d3d_ps_create(V9X_D3D_PS_SHADER *table, v9x_u32 slots,
                          v9x_u32 *serial, const void *owner,
                          v9x_u32 handle, const v9x_u32 *tokens,
                          v9x_u32 dwords)
{
    V9X_D3D_PS_SHADER *slot = 0;
    v9x_u32 where;
    v9x_u32 status;
    v9x_u32 i;

    /* A live handle is replaced; otherwise the first free slot. */
    for (i = 0ul; i < slots; ++i) {
        if (table[i].owner == owner && table[i].handle == handle &&
            table[i].serial != 0ul) {
            slot = &table[i];
            break;
        }
    }
    for (i = 0ul; slot == 0 && i < slots; ++i) {
        if (table[i].serial == 0ul) {
            slot = &table[i];
        }
    }
    if (slot == 0) {
        return V9X_D3D_PS_FULL;
    }
    slot->serial = 0ul;
    status = v9x_ps11_parse(tokens, dwords, &slot->program, &where);
    if (status != V9X_PS11_OK) {
        return status;
    }
    slot->owner = owner;
    slot->handle = handle;
    if (++*serial == 0ul) {
        ++*serial;
    }
    slot->serial = *serial;
    return V9X_PS11_OK;
}

const V9X_D3D_PS_SHADER *v9x_d3d_ps_find(const V9X_D3D_PS_SHADER *table,
                                         v9x_u32 slots, const void *owner,
                                         v9x_u32 handle)
{
    v9x_u32 i;

    for (i = 0ul; i < slots; ++i) {
        if (table[i].serial != 0ul && table[i].owner == owner &&
            table[i].handle == handle) {
            return &table[i];
        }
    }
    return 0;
}

void v9x_d3d_ps_delete(V9X_D3D_PS_SHADER *table, v9x_u32 slots,
                       const void *owner, v9x_u32 handle)
{
    v9x_u32 i;

    for (i = 0ul; i < slots; ++i) {
        if (table[i].owner == owner && table[i].handle == handle) {
            table[i].serial = 0ul;
        }
    }
}

void v9x_d3d_ps_forget(V9X_D3D_PS_SHADER *table, v9x_u32 slots,
                       const void *owner)
{
    v9x_u32 i;

    for (i = 0ul; i < slots; ++i) {
        if (table[i].owner == owner) {
            table[i].serial = 0ul;
        }
    }
}

/* ps.1.1 tokens (docs\specifications\ddi8-on-win9x.md section 6): the
 * version and end tokens, an instruction's opcode and co-issue bit, and a
 * register token's file (30:28), shift (27:24), saturate (20), write mask
 * (19:16) or swizzle (23:16), and number. */
#define V9X_D3D_PSF_VERSION  0xffff0101ul
#define V9X_D3D_PSF_END      0x0000fffful
#define V9X_D3D_PSF_COISSUE  0x40000000ul
#define V9X_D3D_PSF_OP_MOV   1ul
#define V9X_D3D_PSF_OP_ADD   2ul
#define V9X_D3D_PSF_OP_MUL   5ul
#define V9X_D3D_PSF_OP_TEX   66ul
#define V9X_D3D_PSF_REG      0x80000000ul
#define V9X_D3D_PSF_SATURATE 0x00100000ul
#define V9X_D3D_PSF_XYZW     (0xe4ul << 16)
#define V9X_D3D_PSF_MASK_ALL 0xful
#define V9X_D3D_PSF_MASK_RGB 0x7ul
#define V9X_D3D_PSF_MASK_A   0x8ul

/* D3DTEXTUREOP and D3DTA (d3d8types.h); argument bits above the selector
 * are modifiers (COMPLEMENT, ALPHAREPLICATE), which are not translated. */
#define V9X_D3D_PSF_TOP_DISABLE    1ul
#define V9X_D3D_PSF_TOP_SELECT1    2ul
#define V9X_D3D_PSF_TOP_SELECT2    3ul
#define V9X_D3D_PSF_TOP_MODULATE   4ul
#define V9X_D3D_PSF_TOP_MODULATE2X 5ul
#define V9X_D3D_PSF_TOP_MODULATE4X 6ul
#define V9X_D3D_PSF_TOP_ADD        7ul
#define V9X_D3D_PSF_TA_DIFFUSE     0ul
#define V9X_D3D_PSF_TA_CURRENT     1ul
#define V9X_D3D_PSF_TA_TEXTURE     2ul
#define V9X_D3D_PSF_TA_SPECULAR    4ul
#define V9X_D3D_PSF_TA_SELECTOR    0x0000000ful

typedef struct v9x_d3d_psf {
    v9x_u32 *tokens;
    v9x_u32 count;
    int failed;
} V9X_D3D_PSF;

static void v9x_d3d_psf_put(V9X_D3D_PSF *b, v9x_u32 word)
{
    if (b->count >= V9X_D3D_PS_FIXED_DWORDS) {
        b->failed = 1;
        return;
    }
    b->tokens[b->count++] = word;
}

static v9x_u32 v9x_d3d_psf_reg(v9x_u32 file, v9x_u32 reg)
{
    return V9X_D3D_PSF_REG | (file << 28) | reg;
}

/* An argument as a source register token for stage `stage`; 0 for one
 * that has no register here. */
static v9x_u32 v9x_d3d_psf_source(v9x_u32 arg, v9x_u32 stage)
{
    if ((arg & ~V9X_D3D_PSF_TA_SELECTOR) != 0ul) {
        return 0ul;
    }
    switch (arg) {
    case V9X_D3D_PSF_TA_DIFFUSE:
        return v9x_d3d_psf_reg(V9X_PS11_FILE_INPUT, 0ul) | V9X_D3D_PSF_XYZW;
    case V9X_D3D_PSF_TA_SPECULAR:
        return v9x_d3d_psf_reg(V9X_PS11_FILE_INPUT, 1ul) | V9X_D3D_PSF_XYZW;
    case V9X_D3D_PSF_TA_TEXTURE:
        return v9x_d3d_psf_reg(V9X_PS11_FILE_TEXTURE, stage) |
               V9X_D3D_PSF_XYZW;
    case V9X_D3D_PSF_TA_CURRENT:
        return stage == 0ul
                   ? v9x_d3d_psf_reg(V9X_PS11_FILE_INPUT, 0ul) |
                         V9X_D3D_PSF_XYZW
                   : v9x_d3d_psf_reg(V9X_PS11_FILE_TEMP, 0ul) |
                         V9X_D3D_PSF_XYZW;
    default:
        return 0ul;
    }
}

/* Whether an op has a translation, and whether it reads arg1 and arg2. */
static int v9x_d3d_psf_op_reads(v9x_u32 op, int *reads1, int *reads2)
{
    switch (op) {
    case V9X_D3D_PSF_TOP_SELECT1:
        *reads1 = 1;
        *reads2 = 0;
        return 1;
    case V9X_D3D_PSF_TOP_SELECT2:
        *reads1 = 0;
        *reads2 = 1;
        return 1;
    case V9X_D3D_PSF_TOP_MODULATE:
    case V9X_D3D_PSF_TOP_MODULATE2X:
    case V9X_D3D_PSF_TOP_MODULATE4X:
    case V9X_D3D_PSF_TOP_ADD:
        *reads1 = 1;
        *reads2 = 1;
        return 1;
    default:
        return 0;
    }
}

/* Whether one combine (op, args) is translatable on `stage`, and whether
 * it samples the stage's texture. */
static int v9x_d3d_psf_check(v9x_u32 op, v9x_u32 arg1, v9x_u32 arg2,
                             v9x_u32 stage, int *samples)
{
    int reads1;
    int reads2;

    if (!v9x_d3d_psf_op_reads(op, &reads1, &reads2) ||
        (reads1 && v9x_d3d_psf_source(arg1, stage) == 0ul) ||
        (reads2 && v9x_d3d_psf_source(arg2, stage) == 0ul)) {
        return 0;
    }
    if ((reads1 && arg1 == V9X_D3D_PSF_TA_TEXTURE) ||
        (reads2 && arg2 == V9X_D3D_PSF_TA_TEXTURE)) {
        *samples = 1;
    }
    return 1;
}

/* One combine into r0's `mask` channels, saturated as D3D's stages are. */
static void v9x_d3d_psf_combine(V9X_D3D_PSF *b, v9x_u32 op, v9x_u32 arg1,
                                v9x_u32 arg2, v9x_u32 stage, v9x_u32 mask,
                                int coissue)
{
    v9x_u32 opcode = V9X_D3D_PSF_OP_MOV;
    v9x_u32 shift = 0ul;
    v9x_u32 dst;

    if (op == V9X_D3D_PSF_TOP_ADD) {
        opcode = V9X_D3D_PSF_OP_ADD;
    } else if (op == V9X_D3D_PSF_TOP_MODULATE ||
               op == V9X_D3D_PSF_TOP_MODULATE2X ||
               op == V9X_D3D_PSF_TOP_MODULATE4X) {
        opcode = V9X_D3D_PSF_OP_MUL;
        shift = op - V9X_D3D_PSF_TOP_MODULATE;
    }
    dst = v9x_d3d_psf_reg(V9X_PS11_FILE_TEMP, 0ul) | (shift << 24) |
          V9X_D3D_PSF_SATURATE | (mask << 16);
    v9x_d3d_psf_put(b, opcode | (coissue ? V9X_D3D_PSF_COISSUE : 0ul));
    v9x_d3d_psf_put(b, dst);
    if (op == V9X_D3D_PSF_TOP_SELECT2) {
        v9x_d3d_psf_put(b, v9x_d3d_psf_source(arg2, stage));
        return;
    }
    v9x_d3d_psf_put(b, v9x_d3d_psf_source(arg1, stage));
    if (opcode != V9X_D3D_PSF_OP_MOV) {
        v9x_d3d_psf_put(b, v9x_d3d_psf_source(arg2, stage));
    }
}

/* Whether a combine leaves stage 1's input as it is: a select of CURRENT. */
static int v9x_d3d_psf_passes(v9x_u32 op, v9x_u32 arg1, v9x_u32 arg2)
{
    return (op == V9X_D3D_PSF_TOP_SELECT1 &&
            arg1 == V9X_D3D_PSF_TA_CURRENT) ||
           (op == V9X_D3D_PSF_TOP_SELECT2 && arg2 == V9X_D3D_PSF_TA_CURRENT);
}

/* One stage's colour and alpha into r0: one instruction when they are the
 * same combine, else the colour and a co-issued alpha. An alpha DISABLE
 * keeps the input's alpha: copied on stage 0, already in r0 on stage 1,
 * as is any combine that passes CURRENT there. */
static void v9x_d3d_psf_stage(V9X_D3D_PSF *b, const V9X_D3D_STATE_STAGE *s,
                              v9x_u32 stage)
{
    v9x_u32 aop = s->alpha_op;
    v9x_u32 a1 = s->alpha_arg1;
    v9x_u32 a2 = s->alpha_arg2;
    int colour = 1;
    int alpha = 1;

    if (aop == V9X_D3D_PSF_TOP_DISABLE) {
        aop = V9X_D3D_PSF_TOP_SELECT1;
        a1 = V9X_D3D_PSF_TA_CURRENT;
    }
    if (stage != 0ul) {
        colour = !v9x_d3d_psf_passes(s->color_op, s->color_arg1,
                                     s->color_arg2);
        alpha = !v9x_d3d_psf_passes(aop, a1, a2);
    }
    if (colour && alpha && aop == s->color_op && a1 == s->color_arg1 &&
        a2 == s->color_arg2) {
        v9x_d3d_psf_combine(b, aop, a1, a2, stage, V9X_D3D_PSF_MASK_ALL, 0);
        return;
    }
    if (colour) {
        v9x_d3d_psf_combine(b, s->color_op, s->color_arg1, s->color_arg2,
                            stage, V9X_D3D_PSF_MASK_RGB, 0);
    }
    if (alpha) {
        v9x_d3d_psf_combine(b, aop, a1, a2, stage, V9X_D3D_PSF_MASK_A,
                            colour);
    }
}

v9x_u32 v9x_d3d_ps_fixed(const V9X_D3D_STATE_STAGE *stage0,
                         const V9X_D3D_STATE_STAGE *stage1, int two_stages,
                         int specular,
                         v9x_u32 tokens[V9X_D3D_PS_FIXED_DWORDS])
{
    V9X_D3D_PSF b;
    int samples0 = 0;
    int samples1 = 0;
    int enabled0 = stage0->color_op != V9X_D3D_PSF_TOP_DISABLE;
    int enabled1 = enabled0 && two_stages &&
                   stage1->color_op != V9X_D3D_PSF_TOP_DISABLE;

    /* Every combine that runs must translate before anything is written. */
    if (enabled0 &&
        (!v9x_d3d_psf_check(stage0->color_op, stage0->color_arg1,
                            stage0->color_arg2, 0ul, &samples0) ||
         (stage0->alpha_op != V9X_D3D_PSF_TOP_DISABLE &&
          !v9x_d3d_psf_check(stage0->alpha_op, stage0->alpha_arg1,
                             stage0->alpha_arg2, 0ul, &samples0)))) {
        return 0ul;
    }
    if (enabled1 &&
        (!v9x_d3d_psf_check(stage1->color_op, stage1->color_arg1,
                            stage1->color_arg2, 1ul, &samples1) ||
         (stage1->alpha_op != V9X_D3D_PSF_TOP_DISABLE &&
          !v9x_d3d_psf_check(stage1->alpha_op, stage1->alpha_arg1,
                             stage1->alpha_arg2, 1ul, &samples1)))) {
        return 0ul;
    }

    b.tokens = tokens;
    b.count = 0ul;
    b.failed = 0;
    v9x_d3d_psf_put(&b, V9X_D3D_PSF_VERSION);
    /* 1.1 samples before any arithmetic. */
    if (samples0) {
        v9x_d3d_psf_put(&b, V9X_D3D_PSF_OP_TEX);
        v9x_d3d_psf_put(&b, v9x_d3d_psf_reg(V9X_PS11_FILE_TEXTURE, 0ul) |
                                (V9X_D3D_PSF_MASK_ALL << 16));
    }
    if (samples1) {
        v9x_d3d_psf_put(&b, V9X_D3D_PSF_OP_TEX);
        v9x_d3d_psf_put(&b, v9x_d3d_psf_reg(V9X_PS11_FILE_TEXTURE, 1ul) |
                                (V9X_D3D_PSF_MASK_ALL << 16));
    }
    if (enabled0) {
        v9x_d3d_psf_stage(&b, stage0, 0ul);
    } else {
        /* A disabled stage 0 ends the cascade: the vertex colour. */
        v9x_d3d_psf_combine(&b, V9X_D3D_PSF_TOP_SELECT1,
                            V9X_D3D_PSF_TA_DIFFUSE, 0ul, 0ul,
                            V9X_D3D_PSF_MASK_ALL, 0);
    }
    if (enabled1) {
        v9x_d3d_psf_stage(&b, stage1, 1ul);
    }
    if (specular) {
        v9x_d3d_psf_combine(&b, V9X_D3D_PSF_TOP_ADD,
                            V9X_D3D_PSF_TA_CURRENT, V9X_D3D_PSF_TA_SPECULAR,
                            1ul, V9X_D3D_PSF_MASK_RGB, 0);
    }
    v9x_d3d_psf_put(&b, V9X_D3D_PSF_END);
    return b.failed ? 0ul : b.count;
}
