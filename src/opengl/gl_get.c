/*
 * The state queries (gl_get.h). Each pname fills up to sixteen values as
 * doubles with one class; v9x_gl_get then converts to what was asked for.
 * No C runtime: the rounding goes through a local fistp.
 */
#include "gl_get.h"

#ifdef __WATCOMC__
static long v9x_gl_get_round(double value);
#pragma aux v9x_gl_get_round = \
    "sub esp,4" \
    "fistp dword ptr [esp]" \
    "pop eax" \
    parm [8087] value [eax] modify exact [eax];
#else
static long v9x_gl_get_round(double value)
{
    return value < 0.0 ? (long)(value - 0.5) : (long)(value + 0.5);
}
#endif

/* How a query's values convert (6.1.2). */
#define V9X_GL_CLASS_BOOLEAN 0
#define V9X_GL_CLASS_INTEGER 1   /* integers and enums */
#define V9X_GL_CLASS_FLOAT   2
#define V9X_GL_CLASS_MAPPED  3   /* colours and depths: [0,1] -> [0,2^31-1] */

typedef struct v9x_gl_values {
    double v[16];
    unsigned int count;
    int kind;
} V9X_GL_VALUES;

static void v9x_gl_one(V9X_GL_VALUES *out, int kind, double value)
{
    out->kind = kind;
    out->count = 1u;
    out->v[0] = value;
}

static void v9x_gl_four(V9X_GL_VALUES *out, int kind, double a, double b,
                        double c, double d)
{
    out->kind = kind;
    out->count = 4u;
    out->v[0] = a;
    out->v[1] = b;
    out->v[2] = c;
    out->v[3] = d;
}

static void v9x_gl_matrix_values(V9X_GL_VALUES *out, const V9X_GL_MATRIX *m)
{
    unsigned int i;

    out->kind = V9X_GL_CLASS_FLOAT;
    out->count = 16u;
    for (i = 0u; i < 16u; ++i) {
        out->v[i] = (double)m->m[i];
    }
}

/* The values of `pname`, or zero for one this implementation does not
 * know. */
static int v9x_gl_query(const V9X_GL_STATE *state,
                        const V9X_GL_PIPELINE *pipeline,
                        const V9X_GL_TEXTURES *textures, GLenum pname,
                        V9X_GL_VALUES *out)
{
    const V9X_GL_MATRICES *matrices = &state->matrices;
    int is_565 = state->target_format == V9X_GL_TARGET_RGB565;

    switch (pname) {
    /* Transformation state (table 6.4). */
    case 0x0BA0u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER, matrices->mode);
        return 1;
    case 0x0BA6u:
        v9x_gl_matrix_values(out, v9x_gl_matrix_top(matrices,
                                                    V9X_GL_MODELVIEW));
        return 1;
    case 0x0BA7u:
        v9x_gl_matrix_values(out, v9x_gl_matrix_top(matrices,
                                                    V9X_GL_PROJECTION));
        return 1;
    case 0x0BA8u:
        v9x_gl_matrix_values(out, v9x_gl_matrix_top(matrices,
                                                    V9X_GL_TEXTURE));
        return 1;
    case 0x0BA3u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER,
                             matrices->modelview_top + 1u); return 1;
    case 0x0BA4u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER,
                             matrices->projection_top + 1u); return 1;
    case 0x0BA5u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER,
                             matrices->texture_top + 1u); return 1;
    case 0x0BA2u:
        v9x_gl_four(out, V9X_GL_CLASS_INTEGER, state->viewport[0],
                    state->viewport[1], state->viewport[2],
                    state->viewport[3]);
        return 1;
    case 0x0B70u:
        out->kind = V9X_GL_CLASS_MAPPED;
        out->count = 2u;
        out->v[0] = pipeline->depth_near;
        out->v[1] = pipeline->depth_far;
        return 1;
    /* Current values and coloring (tables 6.5, 6.8). */
    case 0x0B00u:
        v9x_gl_four(out, V9X_GL_CLASS_MAPPED, pipeline->color[0],
                    pipeline->color[1], pipeline->color[2],
                    pipeline->color[3]);
        return 1;
    case 0x0B03u:
        v9x_gl_four(out, V9X_GL_CLASS_FLOAT, pipeline->tex[0],
                    pipeline->tex[1], pipeline->tex[2], pipeline->tex[3]);
        return 1;
    case 0x0B54u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER,
                             pipeline->shade_model); return 1;
    /* Rasterization (table 6.9). */
    case 0x0B45u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER,
                             pipeline->cull_face); return 1;
    case 0x0B46u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER,
                             pipeline->front_face); return 1;
    case 0x0B40u:
        out->kind = V9X_GL_CLASS_INTEGER;
        out->count = 2u;
        out->v[0] = state->polygon_mode[0];
        out->v[1] = state->polygon_mode[1];
        return 1;
    case 0x0B21u: v9x_gl_one(out, V9X_GL_CLASS_FLOAT, state->line_width);
        return 1;
    case 0x0B11u: v9x_gl_one(out, V9X_GL_CLASS_FLOAT, state->point_size);
        return 1;
    /* Lines and points are aliased and one pixel wide here: the ranges
     * and granularities say exactly that. */
    case 0x0B22u:
    case 0x0B12u:
        out->kind = V9X_GL_CLASS_FLOAT;
        out->count = 2u;
        out->v[0] = 1.0;
        out->v[1] = 1.0;
        return 1;
    case 0x0B23u:
    case 0x0B13u: v9x_gl_one(out, V9X_GL_CLASS_FLOAT, 0.0); return 1;
    /* Texturing (table 6.12). */
    case 0x8069u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER, textures->bound);
        return 1;
    /* Pixel operations (table 6.15). */
    case 0x0C10u:
        v9x_gl_four(out, V9X_GL_CLASS_INTEGER, state->scissor[0],
                    state->scissor[1], state->scissor[2], state->scissor[3]);
        return 1;
    case 0x0BC1u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER,
                             pipeline->alpha_func); return 1;
    case 0x0BC2u: v9x_gl_one(out, V9X_GL_CLASS_MAPPED, pipeline->alpha_ref);
        return 1;
    case 0x0B74u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER,
                             pipeline->depth_func); return 1;
    case 0x0BE1u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER, pipeline->blend_src);
        return 1;
    case 0x0BE0u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER, pipeline->blend_dst);
        return 1;
    /* Framebuffer control (table 6.16). */
    case 0x0C01u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER, state->draw_buffer);
        return 1;
    case 0x0C02u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER, state->read_buffer);
        return 1;
    case 0x0C23u:
        v9x_gl_four(out, V9X_GL_CLASS_BOOLEAN, state->color_mask[0],
                    state->color_mask[1], state->color_mask[2],
                    state->color_mask[3]);
        return 1;
    case 0x0B72u: v9x_gl_one(out, V9X_GL_CLASS_BOOLEAN, state->depth_mask);
        return 1;
    case 0x0C22u:
        v9x_gl_four(out, V9X_GL_CLASS_MAPPED, state->clear_color[0],
                    state->clear_color[1], state->clear_color[2],
                    state->clear_color[3]);
        return 1;
    case 0x0B73u: v9x_gl_one(out, V9X_GL_CLASS_MAPPED, state->clear_depth);
        return 1;
    /* Pixel store (table 6.17). */
    case 0x0CF5u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER,
                             textures->unpack_alignment); return 1;
    case 0x0CF2u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER,
                             textures->unpack_row_length); return 1;
    case 0x0CF3u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER,
                             textures->unpack_skip_rows); return 1;
    case 0x0CF4u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER,
                             textures->unpack_skip_pixels); return 1;
    case 0x0CF0u:
    case 0x0CF1u:
    case 0x0D00u:
    case 0x0D01u: v9x_gl_one(out, V9X_GL_CLASS_BOOLEAN, 0.0); return 1;
    case 0x0D02u:
    case 0x0D03u:
    case 0x0D04u:
    case 0x0D05u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER,
                             textures->pack[pname - 0x0D00u]); return 1;
    /* Hints (table 6.19). */
    case 0x0C50u:
    case 0x0C51u:
    case 0x0C52u:
    case 0x0C53u:
    case 0x0C54u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER,
                             state->hints[pname - 0x0C50u]); return 1;
    /* Implementation limits (table 6.20): the specification's minimums,
     * which are this implementation's. */
    case 0x0D31u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER, 8.0); return 1;
    case 0x0D32u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER, 6.0); return 1;
    case 0x0D33u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER,
                             V9X_GL_TEXTURE_SIZE_MAX); return 1;
    case 0x0D36u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER,
                             V9X_GL_MODELVIEW_DEPTH); return 1;
    case 0x0D38u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER,
                             V9X_GL_PROJECTION_DEPTH); return 1;
    case 0x0D39u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER,
                             V9X_GL_TEXTURE_DEPTH); return 1;
    case 0x0D35u:
    case 0x0D3Bu: v9x_gl_one(out, V9X_GL_CLASS_INTEGER, 16.0); return 1;
    case 0x0D37u:
    case 0x0B31u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER, 64.0); return 1;
    case 0x0D30u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER, 8.0); return 1;
    case 0x0D34u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER, 32.0); return 1;
    case 0x0D3Au:
        out->kind = V9X_GL_CLASS_INTEGER;
        out->count = 2u;
        out->v[0] = 2048.0;
        out->v[1] = 2048.0;
        return 1;
    case 0x0D50u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER, 4.0); return 1;
    /* The pixel format (table 6.21): RGB 16 bits, 16-bit depth, double
     * buffered, nothing else. */
    case 0x0D52u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER, 5.0); return 1;
    case 0x0D53u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER, is_565 ? 6.0 : 5.0);
        return 1;
    case 0x0D54u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER, 5.0); return 1;
    case 0x0D56u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER, 16.0); return 1;
    case 0x0D51u:
    case 0x0D55u:
    case 0x0D57u:
    case 0x0D58u:
    case 0x0D59u:
    case 0x0D5Au:
    case 0x0D5Bu:
    case 0x0C00u: v9x_gl_one(out, V9X_GL_CLASS_INTEGER, 0.0); return 1;
    case 0x0C32u:
    case 0x0C31u: v9x_gl_one(out, V9X_GL_CLASS_BOOLEAN, 1.0); return 1;
    case 0x0C33u:
    case 0x0C30u: v9x_gl_one(out, V9X_GL_CLASS_BOOLEAN, 0.0); return 1;
    default:
        break;
    }
    /* Every glEnable capability is also a Boolean query (6.1.1). Only a
     * known capability counts: v9x_gl_state_cap answers zero for others,
     * so ask whether the name is one by enabling-free inspection. */
    {
        static const GLenum caps[] = {
            0x0BC0u, 0x0D80u, 0x0BE2u, 0x3000u, 0x3001u, 0x3002u, 0x3003u,
            0x3004u, 0x3005u, 0x0BF2u, 0x0B57u, 0x0B44u, 0x0B71u, 0x0BD0u,
            0x0B60u, 0x0BF1u, 0x4000u, 0x4001u, 0x4002u, 0x4003u, 0x4004u,
            0x4005u, 0x4006u, 0x4007u, 0x0B50u, 0x0B20u, 0x0B24u, 0x0D90u,
            0x0D91u, 0x0D92u, 0x0D93u, 0x0D94u, 0x0D95u, 0x0D96u, 0x0D97u,
            0x0D98u, 0x0DB0u, 0x0DB1u, 0x0DB2u, 0x0DB3u, 0x0DB4u, 0x0DB5u,
            0x0DB6u, 0x0DB7u, 0x0DB8u, 0x0BA1u, 0x0B10u, 0x8037u, 0x2A02u,
            0x2A01u, 0x0B41u, 0x0B42u, 0x0C11u, 0x0B90u, 0x0DE0u, 0x0DE1u,
            0x0C63u, 0x0C62u, 0x0C60u, 0x0C61u
        };
        unsigned int i;

        for (i = 0u; i < sizeof(caps) / sizeof(caps[0]); ++i) {
            if (caps[i] == pname) {
                v9x_gl_one(out, V9X_GL_CLASS_BOOLEAN,
                           v9x_gl_state_cap(state, pname) ? 1.0 : 0.0);
                return 1;
            }
        }
    }
    return 0;
}

/* A value as an integer by its class (6.1.2). */
static GLint v9x_gl_as_integer(const V9X_GL_VALUES *values, unsigned int i)
{
    double value = values->v[i];

    if (values->kind == V9X_GL_CLASS_BOOLEAN) {
        return value != 0.0 ? 1 : 0;
    }
    if (values->kind == V9X_GL_CLASS_MAPPED) {
        /* [0,1] (and [-1,1] in general) linearly onto the whole range. */
        if (value >= 1.0) {
            return 2147483647;
        }
        if (value <= -1.0) {
            return -2147483647 - 1;
        }
        return (GLint)v9x_gl_get_round(value * 2147483647.0);
    }
    return (GLint)v9x_gl_get_round(value);
}

void v9x_gl_get(V9X_GL_STATE *state, const V9X_GL_PIPELINE *pipeline,
                const V9X_GL_TEXTURES *textures, GLenum pname, int kind,
                void *out)
{
    V9X_GL_VALUES values;
    unsigned int i;

    if (state->in_begin) {
        v9x_gl_state_error(state, V9X_GL_INVALID_OPERATION);
        return;
    }
    if (!v9x_gl_query(state, pipeline, textures, pname, &values)) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    for (i = 0u; i < values.count; ++i) {
        switch (kind) {
        case V9X_GL_GET_BOOLEAN:
            ((GLboolean *)out)[i] = values.v[i] != 0.0 ? 1 : 0;
            break;
        case V9X_GL_GET_INTEGER:
            ((GLint *)out)[i] = v9x_gl_as_integer(&values, i);
            break;
        case V9X_GL_GET_FLOAT:
            ((GLfloat *)out)[i] = (GLfloat)values.v[i];
            break;
        default:
            ((GLdouble *)out)[i] = values.v[i];
            break;
        }
    }
}
