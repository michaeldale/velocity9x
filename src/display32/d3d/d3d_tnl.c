/*
 * The Direct3D 7 T&L device's front end (d3d_tnl.h). Pure; the cosine and
 * square root are the x87's through pragmas, as the HAL links no C runtime.
 *
 * The equations the key carries are the specification note's (section 4),
 * where Direct3D 7's are marked recalled; the gate for them is the plan's
 * step 6 comparison with the runtime's own T&L on the same frame.
 */
#include "d3d_tnl.h"

#ifdef __WATCOMC__
static double v9x_d3d_tnl_cos(double value);
#pragma aux v9x_d3d_tnl_cos = "fcos" parm [8087] value [8087];
static double v9x_d3d_tnl_sqrt(double value);
#pragma aux v9x_d3d_tnl_sqrt = "fsqrt" parm [8087] value [8087];
#else
#include <math.h>
#define v9x_d3d_tnl_cos cos
#define v9x_d3d_tnl_sqrt sqrt
#endif

/* d3dtypes.h's D3DFVF_ bits read here. */
#define V9X_D3D_TNL_FVF_POSITION_MASK 0x00eul
#define V9X_D3D_TNL_FVF_XYZ           0x002ul
#define V9X_D3D_TNL_FVF_NORMAL        0x010ul
#define V9X_D3D_TNL_FVF_RESERVED1     0x020ul
#define V9X_D3D_TNL_FVF_DIFFUSE       0x040ul
#define V9X_D3D_TNL_FVF_SPECULAR      0x080ul
#define V9X_D3D_TNL_FVF_TEXCOUNT(f)   (((f) >> 8) & 0xful)
/* D3DFVF_TEXTUREFORMAT1..4 are encoded 3, 0, 1, 2 at bits 16 + 2i. */
#define V9X_D3D_TNL_FVF_TEXFLOATS(f, i) \
    ("\2\3\4\1"[((f) >> (16ul + 2ul * (i))) & 3ul])

/* SETLIGHT's data types (d3dhal.h). */
#define V9X_D3D_TNL_LIGHT_ENABLE  0ul
#define V9X_D3D_TNL_LIGHT_DISABLE 1ul
#define V9X_D3D_TNL_LIGHT_DATA    2ul

/* D3DMATERIALCOLORSOURCE and D3DFOGMODE. */
#define V9X_D3D_TNL_MCS_MATERIAL 0ul
#define V9X_D3D_TNL_MCS_COLOR1   1ul
#define V9X_D3D_TNL_MCS_COLOR2   2ul
#define V9X_D3D_TNL_FOG_NONE     0ul
#define V9X_D3D_TNL_FOG_EXP      1ul
#define V9X_D3D_TNL_FOG_EXP2     2ul
#define V9X_D3D_TNL_FOG_LINEAR   3ul

/* DP2 record numbers (d3d_dp2.h's, repeated so this file stays pure). */
#define V9X_D3D_TNL_OP_VIEWPORTINFO 28ul
#define V9X_D3D_TNL_OP_ZRANGE       32ul
#define V9X_D3D_TNL_OP_SETMATERIAL  33ul
#define V9X_D3D_TNL_OP_SETLIGHT     34ul
#define V9X_D3D_TNL_OP_CREATELIGHT  35ul
#define V9X_D3D_TNL_OP_SETTRANSFORM 36ul

/* D3DLIGHT7's theta and phi are whole cone angles in radians; the cone's
 * half-angles are what the spot test compares against. */
#define V9X_D3D_TNL_HALF_ANGLE 0.5

/* The payload is not aligned: little-endian bytes, as d3d_dp2.c reads. */
static v9x_u32 v9x_d3d_tnl_dword(const v9x_u8 *p)
{
    return (v9x_u32)p[0] | ((v9x_u32)p[1] << 8) | ((v9x_u32)p[2] << 16) |
           ((v9x_u32)p[3] << 24);
}

static float v9x_d3d_tnl_float(const v9x_u8 *p)
{
    union {
        v9x_u32 bits;
        float value;
    } u;

    u.bits = v9x_d3d_tnl_dword(p);
    return u.value;
}

static void v9x_d3d_tnl_put(v9x_u8 *p, v9x_u32 value)
{
    p[0] = (v9x_u8)value;
    p[1] = (v9x_u8)(value >> 8);
    p[2] = (v9x_u8)(value >> 16);
    p[3] = (v9x_u8)(value >> 24);
}

static void v9x_d3d_tnl_put_float(v9x_u8 *p, float value)
{
    union {
        v9x_u32 bits;
        float value;
    } u;

    u.value = value;
    v9x_d3d_tnl_put(p, u.bits);
}

/* count floats from bytes into out. */
static void v9x_d3d_tnl_floats(const v9x_u8 *p, float *out, v9x_u32 count)
{
    v9x_u32 i;

    for (i = 0ul; i < count; ++i) {
        out[i] = v9x_d3d_tnl_float(p + i * 4ul);
    }
}

static void v9x_d3d_tnl_identity(float m[4][4])
{
    v9x_u32 r;
    v9x_u32 c;

    for (r = 0ul; r < 4ul; ++r) {
        for (c = 0ul; c < 4ul; ++c) {
            m[r][c] = r == c ? 1.0f : 0.0f;
        }
    }
}

void v9x_d3d_tnl_init(V9X_D3D_TNL_STATE *state)
{
    v9x_u8 *bytes = (v9x_u8 *)state;
    v9x_u32 i;

    for (i = 0ul; i < (v9x_u32)sizeof(*state); ++i) {
        bytes[i] = 0u;
    }
    v9x_d3d_tnl_identity(state->world);
    v9x_d3d_tnl_identity(state->view);
    v9x_d3d_tnl_identity(state->projection);
    state->lighting = 1ul;
    state->color_vertex = 1ul;
    state->local_viewer = 1ul;
    state->source[V9X_R3D_TNL_TERM_DIFFUSE] = V9X_D3D_TNL_MCS_COLOR1;
    state->source[V9X_R3D_TNL_TERM_SPECULAR] = V9X_D3D_TNL_MCS_COLOR2;
    state->fog_end = 1.0f;
    state->fog_density = 1.0f;
    state->max_z = 1.0f;
}

static float v9x_d3d_tnl_bits_float(v9x_u32 value)
{
    union {
        v9x_u32 bits;
        float value;
    } u;

    u.bits = value;
    return u.value;
}

/* Whether a field's value would change, setting it when `apply`. */
static int v9x_d3d_tnl_set(v9x_u32 *field, v9x_u32 value, int apply)
{
    int changed = *field != value;

    if (apply) {
        *field = value;
    }
    return changed;
}

static int v9x_d3d_tnl_setf(float *field, v9x_u32 bits, int apply)
{
    float value = v9x_d3d_tnl_bits_float(bits);
    int changed = *field != value;

    if (apply) {
        *field = value;
    }
    return changed;
}

int v9x_d3d_tnl_render_state(V9X_D3D_TNL_STATE *state, v9x_u32 rs,
                             v9x_u32 value, int apply)
{
    switch (rs) {
    case V9X_D3D_TNL_RS_FOGENABLE:
        return v9x_d3d_tnl_set(&state->fog_enable, value, apply);
    case V9X_D3D_TNL_RS_SPECULARENABLE:
        return v9x_d3d_tnl_set(&state->specular_enable, value, apply);
    case V9X_D3D_TNL_RS_FOGTABLEMODE:
        return v9x_d3d_tnl_set(&state->fog_table_mode, value, apply);
    case V9X_D3D_TNL_RS_FOGSTART:
        return v9x_d3d_tnl_setf(&state->fog_start, value, apply);
    case V9X_D3D_TNL_RS_FOGEND:
        return v9x_d3d_tnl_setf(&state->fog_end, value, apply);
    case V9X_D3D_TNL_RS_FOGDENSITY:
        return v9x_d3d_tnl_setf(&state->fog_density, value, apply);
    case V9X_D3D_TNL_RS_RANGEFOGENABLE:
        return v9x_d3d_tnl_set(&state->range_fog, value, apply);
    case V9X_D3D_TNL_RS_LIGHTING:
        return v9x_d3d_tnl_set(&state->lighting, value, apply);
    case V9X_D3D_TNL_RS_AMBIENT:
        return v9x_d3d_tnl_set(&state->ambient, value, apply);
    case V9X_D3D_TNL_RS_FOGVERTEXMODE:
        return v9x_d3d_tnl_set(&state->fog_vertex_mode, value, apply);
    case V9X_D3D_TNL_RS_COLORVERTEX:
        return v9x_d3d_tnl_set(&state->color_vertex, value, apply);
    case V9X_D3D_TNL_RS_LOCALVIEWER:
        return v9x_d3d_tnl_set(&state->local_viewer, value, apply);
    case V9X_D3D_TNL_RS_NORMALIZENORMALS:
        return v9x_d3d_tnl_set(&state->normalize, value, apply);
    case V9X_D3D_TNL_RS_DIFFUSEMATERIALSOURCE:
        return v9x_d3d_tnl_set(&state->source[V9X_R3D_TNL_TERM_DIFFUSE],
                               value, apply);
    case V9X_D3D_TNL_RS_SPECULARMATERIALSOURCE:
        return v9x_d3d_tnl_set(&state->source[V9X_R3D_TNL_TERM_SPECULAR],
                               value, apply);
    case V9X_D3D_TNL_RS_AMBIENTMATERIALSOURCE:
        return v9x_d3d_tnl_set(&state->source[V9X_R3D_TNL_TERM_AMBIENT],
                               value, apply);
    case V9X_D3D_TNL_RS_EMISSIVEMATERIALSOURCE:
        return v9x_d3d_tnl_set(&state->source[V9X_R3D_TNL_TERM_EMISSIVE],
                               value, apply);
    case V9X_D3D_TNL_RS_VERTEXBLEND:
        return v9x_d3d_tnl_set(&state->vertex_blend, value, apply);
    default:
        return 0;
    }
}

static void v9x_d3d_tnl_light_data(V9X_D3D_LIGHT7 *light, const v9x_u8 *p)
{
    light->type = v9x_d3d_tnl_dword(p);
    v9x_d3d_tnl_floats(p + 4, light->diffuse, 4ul);
    v9x_d3d_tnl_floats(p + 20, light->specular, 4ul);
    v9x_d3d_tnl_floats(p + 36, light->ambient, 4ul);
    v9x_d3d_tnl_floats(p + 52, light->position, 3ul);
    v9x_d3d_tnl_floats(p + 64, light->direction, 3ul);
    light->range = v9x_d3d_tnl_float(p + 76);
    light->falloff = v9x_d3d_tnl_float(p + 80);
    v9x_d3d_tnl_floats(p + 84, light->attenuation, 3ul);
    light->theta = v9x_d3d_tnl_float(p + 96);
    light->phi = v9x_d3d_tnl_float(p + 100);
}

int v9x_d3d_tnl_record(V9X_D3D_TNL_STATE *state, v9x_u32 op, v9x_u32 count,
                       const v9x_u8 *payload, v9x_u32 bytes)
{
    v9x_u32 i;
    v9x_u32 at = 0ul;

    switch (op) {
    case V9X_D3D_TNL_OP_SETTRANSFORM:
        for (i = 0ul; i < count && at + 68ul <= bytes; ++i, at += 68ul) {
            v9x_u32 type = v9x_d3d_tnl_dword(payload + at);
            float(*m)[4] = type == V9X_D3D_TNL_WORLD ? state->world
                         : type == V9X_D3D_TNL_VIEW ? state->view
                         : type == V9X_D3D_TNL_PROJECTION ? state->projection
                         : 0;

            if (m != 0) {
                v9x_d3d_tnl_floats(payload + at + 4ul, &m[0][0], 16ul);
            }
        }
        return 1;

    case V9X_D3D_TNL_OP_SETLIGHT:
        for (i = 0ul; i < count && at + 8ul <= bytes; ++i) {
            v9x_u32 index = v9x_d3d_tnl_dword(payload + at);
            v9x_u32 type = v9x_d3d_tnl_dword(payload + at + 4ul);

            at += 8ul;
            if (type == V9X_D3D_TNL_LIGHT_DATA) {
                if (at + 104ul > bytes) {
                    break;
                }
                if (index < V9X_D3D_TNL_LIGHT_SLOTS) {
                    v9x_d3d_tnl_light_data(&state->light[index],
                                           payload + at);
                }
                at += 104ul;
            }
            if (index >= V9X_D3D_TNL_LIGHT_SLOTS) {
                ++state->ignored;
                state->ignored_reasons |= V9X_D3D_TNL_IGNORED_SLOT;
                continue;
            }
            if (type == V9X_D3D_TNL_LIGHT_ENABLE) {
                state->light_enabled |= 1ul << index;
            } else if (type == V9X_D3D_TNL_LIGHT_DISABLE) {
                state->light_enabled &= ~(1ul << index);
            }
        }
        return 1;

    case V9X_D3D_TNL_OP_CREATELIGHT:
        for (i = 0ul; i < count && at + 4ul <= bytes; ++i, at += 4ul) {
            if (v9x_d3d_tnl_dword(payload + at) >= V9X_D3D_TNL_LIGHT_SLOTS) {
                ++state->ignored;
                state->ignored_reasons |= V9X_D3D_TNL_IGNORED_SLOT;
            }
        }
        return 1;

    case V9X_D3D_TNL_OP_SETMATERIAL:
        for (i = 0ul; i < count && at + 68ul <= bytes; ++i, at += 68ul) {
            v9x_d3d_tnl_floats(payload + at, state->material.diffuse, 4ul);
            v9x_d3d_tnl_floats(payload + at + 16ul, state->material.ambient,
                               4ul);
            v9x_d3d_tnl_floats(payload + at + 32ul, state->material.specular,
                               4ul);
            v9x_d3d_tnl_floats(payload + at + 48ul, state->material.emissive,
                               4ul);
            state->material.power = v9x_d3d_tnl_float(payload + at + 64ul);
        }
        return 1;

    case V9X_D3D_TNL_OP_VIEWPORTINFO:
        for (i = 0ul; i < count && at + 16ul <= bytes; ++i, at += 16ul) {
            state->viewport[0] = v9x_d3d_tnl_dword(payload + at);
            state->viewport[1] = v9x_d3d_tnl_dword(payload + at + 4ul);
            state->viewport[2] = v9x_d3d_tnl_dword(payload + at + 8ul);
            state->viewport[3] = v9x_d3d_tnl_dword(payload + at + 12ul);
            state->viewport_set = 1ul;
        }
        return 1;

    case V9X_D3D_TNL_OP_ZRANGE:
        for (i = 0ul; i < count && at + 8ul <= bytes; ++i, at += 8ul) {
            state->min_z = v9x_d3d_tnl_float(payload + at);
            state->max_z = v9x_d3d_tnl_float(payload + at + 4ul);
        }
        return 1;

    default:
        return 0;
    }
}

int v9x_d3d_tnl_layout(v9x_u32 fvf, V9X_D3D_TNL_LAYOUT *layout)
{
    v9x_u32 at = 12ul;
    v9x_u32 i;

    if ((fvf & V9X_D3D_TNL_FVF_POSITION_MASK) != V9X_D3D_TNL_FVF_XYZ) {
        return 0;
    }
    layout->sets = V9X_D3D_TNL_FVF_TEXCOUNT(fvf);
    if (layout->sets > 8ul) {
        return 0;
    }
    layout->normal = V9X_D3D_TNL_ABSENT;
    layout->diffuse = V9X_D3D_TNL_ABSENT;
    layout->specular = V9X_D3D_TNL_ABSENT;
    if ((fvf & V9X_D3D_TNL_FVF_NORMAL) != 0ul) {
        layout->normal = at;
        at += 12ul;
    }
    /* DX7's reserved DWORD (DX8's point size): skipped. */
    if ((fvf & V9X_D3D_TNL_FVF_RESERVED1) != 0ul) {
        at += 4ul;
    }
    if ((fvf & V9X_D3D_TNL_FVF_DIFFUSE) != 0ul) {
        layout->diffuse = at;
        at += 4ul;
    }
    if ((fvf & V9X_D3D_TNL_FVF_SPECULAR) != 0ul) {
        layout->specular = at;
        at += 4ul;
    }
    for (i = 0ul; i < layout->sets; ++i) {
        layout->tex[i] = at;
        layout->tex_floats[i] = (v9x_u32)V9X_D3D_TNL_FVF_TEXFLOATS(fvf, i);
        at += layout->tex_floats[i] * 4ul;
    }
    layout->stride = at;
    return 1;
}

/* Set `set`'s u and v into out, by d3d_dp2.c's rule. */
static void v9x_d3d_tnl_uv(const V9X_D3D_TNL_LAYOUT *layout, v9x_u32 set,
                           const v9x_u8 *source, v9x_u8 *out)
{
    v9x_u32 offset;
    v9x_u32 floats;

    if (layout->sets == 0ul) {
        v9x_d3d_tnl_put(out, 0ul);
        v9x_d3d_tnl_put(out + 4, 0ul);
        return;
    }
    if (set >= layout->sets) {
        set = 0ul;
    }
    offset = layout->tex[set];
    floats = layout->tex_floats[set];
    v9x_d3d_tnl_put(out, v9x_d3d_tnl_dword(source + offset));
    v9x_d3d_tnl_put(out + 4, floats >= 2ul
                                 ? v9x_d3d_tnl_dword(source + offset + 4ul)
                                 : 0ul);
}

void v9x_d3d_tnl_vertex(const V9X_D3D_TNL_LAYOUT *layout, v9x_u32 set0,
                        v9x_u32 set1, const v9x_u8 *source, v9x_u8 *vertex,
                        float *normal, float *uv1)
{
    v9x_u32 k;

    for (k = 0ul; k < 3ul; ++k) {
        v9x_d3d_tnl_put(vertex + k * 4ul, v9x_d3d_tnl_dword(source + k * 4ul));
    }
    v9x_d3d_tnl_put_float(vertex + 12, 1.0f);
    v9x_d3d_tnl_put(vertex + 16, layout->diffuse != V9X_D3D_TNL_ABSENT
                                     ? v9x_d3d_tnl_dword(source +
                                                         layout->diffuse)
                                     : 0xfffffffful);
    v9x_d3d_tnl_put(vertex + 20, layout->specular != V9X_D3D_TNL_ABSENT
                                     ? v9x_d3d_tnl_dword(source +
                                                         layout->specular)
                                     : 0ul);
    v9x_d3d_tnl_uv(layout, set0, source, vertex + 24);
    if (uv1 != 0) {
        v9x_d3d_tnl_uv(layout, set1, source, (v9x_u8 *)uv1);
    }
    for (k = 0ul; k < 3ul; ++k) {
        normal[k] = layout->normal != V9X_D3D_TNL_ABSENT
                        ? v9x_d3d_tnl_float(source + layout->normal + k * 4ul)
                        : 0.0f;
    }
}

/* out = a b, Direct3D's row-major 4x4 product. */
static void v9x_d3d_tnl_multiply(float out[4][4], const float a[4][4],
                                 const float b[4][4])
{
    v9x_u32 r;
    v9x_u32 c;
    v9x_u32 k;

    for (r = 0ul; r < 4ul; ++r) {
        for (c = 0ul; c < 4ul; ++c) {
            float sum = 0.0f;

            for (k = 0ul; k < 4ul; ++k) {
                sum += a[r][k] * b[k][c];
            }
            out[r][c] = sum;
        }
    }
}

/* The block's rows for a row-vector matrix: row i is column i. */
static void v9x_d3d_tnl_rows(float out[4][4], const float m[4][4])
{
    v9x_u32 r;
    v9x_u32 c;

    for (r = 0ul; r < 4ul; ++r) {
        for (c = 0ul; c < 4ul; ++c) {
            out[r][c] = m[c][r];
        }
    }
}

/* The normal rows: the inverse transpose of the model-view's 3x3 in the
 * block's (column-vector) form, as the cofactors over the determinant. A
 * singular matrix keeps itself. */
static void v9x_d3d_tnl_normal_rows(float out[3][4], const float rows[4][4])
{
    float det;
    v9x_u32 r;
    v9x_u32 c;
    const float(*a)[4] = rows;

    out[0][0] = a[1][1] * a[2][2] - a[1][2] * a[2][1];
    out[0][1] = a[1][2] * a[2][0] - a[1][0] * a[2][2];
    out[0][2] = a[1][0] * a[2][1] - a[1][1] * a[2][0];
    out[1][0] = a[0][2] * a[2][1] - a[0][1] * a[2][2];
    out[1][1] = a[0][0] * a[2][2] - a[0][2] * a[2][0];
    out[1][2] = a[0][1] * a[2][0] - a[0][0] * a[2][1];
    out[2][0] = a[0][1] * a[1][2] - a[0][2] * a[1][1];
    out[2][1] = a[0][2] * a[1][0] - a[0][0] * a[1][2];
    out[2][2] = a[0][0] * a[1][1] - a[0][1] * a[1][0];
    det = a[0][0] * out[0][0] + a[0][1] * out[0][1] + a[0][2] * out[0][2];
    for (r = 0ul; r < 3ul; ++r) {
        for (c = 0ul; c < 3ul; ++c) {
            out[r][c] = det != 0.0f ? out[r][c] / det : a[r][c];
        }
        out[r][3] = 0.0f;
    }
}

static void v9x_d3d_tnl_copy4(float *out, const float *in)
{
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
    out[3] = in[3];
}

/* v (x y z, w) through the view matrix: a point with w 1, a direction
 * with 0. */
static void v9x_d3d_tnl_to_eye(const float view[4][4], const float *v,
                               float w, float *out)
{
    v9x_u32 c;

    for (c = 0ul; c < 3ul; ++c) {
        out[c] = v[0] * view[0][c] + v[1] * view[1][c] + v[2] * view[2][c] +
                 w * view[3][c];
    }
}

static void v9x_d3d_tnl_unit(float *v)
{
    double length = v9x_d3d_tnl_sqrt((double)v[0] * v[0] +
                                      (double)v[1] * v[1] +
                                      (double)v[2] * v[2]);

    if (length > 0.0) {
        v[0] = (float)(v[0] / length);
        v[1] = (float)(v[1] / length);
        v[2] = (float)(v[2] / length);
    }
}

static v9x_u32 v9x_d3d_tnl_fog_mode(v9x_u32 d3d)
{
    switch (d3d) {
    case V9X_D3D_TNL_FOG_LINEAR:
        return V9X_R3D_TNL_FOG_LINEAR;
    case V9X_D3D_TNL_FOG_EXP:
        return V9X_R3D_TNL_FOG_EXP;
    case V9X_D3D_TNL_FOG_EXP2:
        return V9X_R3D_TNL_FOG_EXP2;
    default:
        return V9X_R3D_TNL_FOG_NONE;
    }
}

/* One light into slot n of the block, in eye space; its key type. */
static v9x_u32 v9x_d3d_tnl_light(const V9X_D3D_TNL_STATE *state,
                                 const V9X_D3D_LIGHT7 *light,
                                 V9X_R3D_TNL_LIGHT *out)
{
    float cos_theta;
    float cos_phi;

    v9x_d3d_tnl_copy4(out->ambient, light->ambient);
    v9x_d3d_tnl_copy4(out->diffuse, light->diffuse);
    v9x_d3d_tnl_copy4(out->specular, light->specular);
    if (light->type == V9X_R3D_TNL_LIGHT_DIRECTIONAL) {
        /* The unit vector toward the light: the direction reversed. */
        v9x_d3d_tnl_to_eye(state->view, light->direction, 0.0f,
                           out->position);
        out->position[0] = -out->position[0];
        out->position[1] = -out->position[1];
        out->position[2] = -out->position[2];
        out->position[3] = 0.0f;
        v9x_d3d_tnl_unit(out->position);
        return V9X_R3D_TNL_LIGHT_DIRECTIONAL;
    }
    v9x_d3d_tnl_to_eye(state->view, light->position, 1.0f, out->position);
    out->position[3] = 1.0f;
    out->attenuation[0] = light->attenuation[0];
    out->attenuation[1] = light->attenuation[1];
    out->attenuation[2] = light->attenuation[2];
    out->attenuation[3] = light->range;
    if (light->type != V9X_R3D_TNL_LIGHT_SPOT) {
        return V9X_R3D_TNL_LIGHT_POINT;
    }
    v9x_d3d_tnl_to_eye(state->view, light->direction, 0.0f, out->direction);
    out->direction[3] = 0.0f;
    v9x_d3d_tnl_unit(out->direction);
    cos_theta = (float)v9x_d3d_tnl_cos((double)light->theta *
                                       V9X_D3D_TNL_HALF_ANGLE);
    cos_phi = (float)v9x_d3d_tnl_cos((double)light->phi *
                                     V9X_D3D_TNL_HALF_ANGLE);
    out->spot[0] = cos_theta;
    out->spot[1] = cos_phi;
    out->spot[2] = light->falloff;
    out->spot[3] = cos_theta != cos_phi ? 1.0f / (cos_theta - cos_phi)
                                        : 0.0f;
    return V9X_R3D_TNL_LIGHT_SPOT;
}

void v9x_d3d_tnl_key(V9X_D3D_TNL_STATE *state,
                     const V9X_D3D_TNL_LAYOUT *layout,
                     V9X_R3D_TNL_KEY *key, V9X_R3D_TNL_CONSTANTS *constants)
{
    float world_view[4][4];
    float all[4][4];
    v9x_u8 *bytes;
    v9x_u32 i;
    v9x_u32 n = 0ul;

    bytes = (v9x_u8 *)key;
    for (i = 0ul; i < (v9x_u32)sizeof(*key); ++i) {
        bytes[i] = 0u;
    }
    bytes = (v9x_u8 *)constants;
    for (i = 0ul; i < (v9x_u32)sizeof(*constants); ++i) {
        bytes[i] = 0u;
    }

    key->model = V9X_R3D_TNL_MODEL_D3D7;
    key->texture_count = layout->sets < V9X_R3D_TNL_MAX_TEXTURES
                             ? layout->sets : V9X_R3D_TNL_MAX_TEXTURES;
    if (layout->normal != V9X_D3D_TNL_ABSENT) {
        key->inputs |= V9X_R3D_TNL_IN_NORMAL;
    }
    if (layout->diffuse != V9X_D3D_TNL_ABSENT) {
        key->inputs |= V9X_R3D_TNL_IN_COLOR0;
    }
    if (layout->specular != V9X_D3D_TNL_ABSENT) {
        key->inputs |= V9X_R3D_TNL_IN_COLOR1;
    }

    /* Matrices: object to clip, object to eye, and the normals'. */
    v9x_d3d_tnl_multiply(world_view, state->world, state->view);
    v9x_d3d_tnl_multiply(all, world_view, state->projection);
    v9x_d3d_tnl_rows(constants->model_view_projection, all);
    v9x_d3d_tnl_rows(constants->model_view, world_view);
    v9x_d3d_tnl_normal_rows(constants->normal, constants->model_view);
    for (i = 0ul; i < V9X_R3D_TNL_MAX_TEXTURES; ++i) {
        constants->texture[i][0][0] = 1.0f;
        constants->texture[i][1][1] = 1.0f;
        constants->texture[i][2][2] = 1.0f;
        constants->texture[i][3][3] = 1.0f;
    }

    /* An absent diffuse is opaque white and an absent specular black. */
    constants->default_color[0][0] = 1.0f;
    constants->default_color[0][1] = 1.0f;
    constants->default_color[0][2] = 1.0f;
    constants->default_color[0][3] = 1.0f;

    if (state->vertex_blend != 0ul) {
        ++state->ignored;
        state->ignored_reasons |= V9X_D3D_TNL_IGNORED_BLEND;
    }

    /*
     * Fog. Vertex fog when the table is off; table fog only with a
     * W-friendly projection, whose w is the eye depth the table would be
     * read by, and in the same units. Anything else is not fogged here.
     */
    if (state->fog_enable != 0ul) {
        if (state->fog_table_mode != V9X_D3D_TNL_FOG_NONE) {
            if (state->projection[2][3] != 0.0f &&
                state->projection[3][3] == 0.0f) {
                key->fog_mode = v9x_d3d_tnl_fog_mode(state->fog_table_mode);
                key->fog_distance = V9X_R3D_TNL_FOG_DEPTH;
            } else {
                ++state->ignored;
                state->ignored_reasons |= V9X_D3D_TNL_IGNORED_TABLE_FOG;
            }
        } else {
            key->fog_mode = v9x_d3d_tnl_fog_mode(state->fog_vertex_mode);
            key->fog_distance = state->range_fog != 0ul
                                    ? V9X_R3D_TNL_FOG_RANGE
                                    : V9X_R3D_TNL_FOG_DEPTH;
        }
        constants->fog[0] = state->fog_start;
        constants->fog[1] = state->fog_end;
        constants->fog[2] = state->fog_density;
        constants->fog[3] = state->fog_end != state->fog_start
                                ? 1.0f / (state->fog_end - state->fog_start)
                                : 0.0f;
    }

    if (state->lighting == 0ul) {
        return;
    }
    key->flags |= V9X_R3D_TNL_LIGHTING;
    if (state->specular_enable != 0ul) {
        key->flags |= V9X_R3D_TNL_SPECULAR;
    }
    if (state->local_viewer != 0ul) {
        key->flags |= V9X_R3D_TNL_LOCAL_VIEWER;
    }
    if (state->normalize != 0ul) {
        key->flags |= V9X_R3D_TNL_NORMALIZE;
    }

    /* Material sources apply with COLORVERTEX, and fall back to the
     * material for a colour the vertex lacks. */
    if (state->color_vertex != 0ul) {
        for (i = 0ul; i < 4ul; ++i) {
            v9x_u32 source = state->source[i];

            if ((source == V9X_D3D_TNL_MCS_COLOR1 &&
                 layout->diffuse == V9X_D3D_TNL_ABSENT) ||
                (source == V9X_D3D_TNL_MCS_COLOR2 &&
                 layout->specular == V9X_D3D_TNL_ABSENT) ||
                source > V9X_D3D_TNL_MCS_COLOR2) {
                source = V9X_D3D_TNL_MCS_MATERIAL;
            }
            key->material_sources |= source << (2ul * i);
        }
    }

    for (i = 0ul; i < V9X_D3D_TNL_LIGHT_SLOTS; ++i) {
        const V9X_D3D_LIGHT7 *light = &state->light[i];
        v9x_u32 type;

        if ((state->light_enabled & (1ul << i)) == 0ul ||
            light->type < V9X_R3D_TNL_LIGHT_POINT ||
            light->type > V9X_R3D_TNL_LIGHT_DIRECTIONAL) {
            continue;
        }
        if (n == V9X_R3D_TNL_MAX_LIGHTS) {
            ++state->ignored;
            state->ignored_reasons |= V9X_D3D_TNL_IGNORED_LIGHTS;
            break;
        }
        type = v9x_d3d_tnl_light(state, light, &constants->light[n]);
        key->light_types |= type << (2ul * n);
        ++n;
    }
    key->light_count = n;

    v9x_d3d_tnl_copy4(constants->material[0].ambient,
                      state->material.ambient);
    v9x_d3d_tnl_copy4(constants->material[0].diffuse,
                      state->material.diffuse);
    v9x_d3d_tnl_copy4(constants->material[0].specular,
                      state->material.specular);
    v9x_d3d_tnl_copy4(constants->material[0].emissive,
                      state->material.emissive);
    constants->material[0].power[0] = state->material.power;
    constants->scene_ambient[0] =
        (float)((state->ambient >> 16) & 0xfful) / 255.0f;
    constants->scene_ambient[1] =
        (float)((state->ambient >> 8) & 0xfful) / 255.0f;
    constants->scene_ambient[2] = (float)(state->ambient & 0xfful) / 255.0f;
    constants->scene_ambient[3] =
        (float)((state->ambient >> 24) & 0xfful) / 255.0f;
    constants->viewer[2] = -1.0f;
}
