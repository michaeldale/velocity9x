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
        for (i = 8ul; i < 12ul; ++i) {
            out[i] = 0.0f;
        }
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
