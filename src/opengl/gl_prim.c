/*
 * The vertex pipeline (gl_prim.h): assembly, transform, clipping, culling,
 * viewport, batches. Pure; no C runtime, so the one float-to-integer
 * conversion (a colour channel) goes through a local fistp, as gl_state.c
 * does.
 */
#include "gl_prim.h"

#ifdef __WATCOMC__
static long v9x_gl_prim_to_long(double value);
#pragma aux v9x_gl_prim_to_long = \
    "sub esp,4" \
    "fistp dword ptr [esp]" \
    "pop eax" \
    parm [8087] value [eax] modify exact [eax];
#else
static long v9x_gl_prim_to_long(double value)
{
    return (long)(value + 0.5);
}
#endif

/* A 0..1 channel as a byte, clamped and rounded to nearest. */
static v9x_u32 v9x_gl_prim_byte(GLfloat value)
{
    long byte;

    if (!(value > 0.0f)) {
        return 0ul;
    }
    if (value >= 1.0f) {
        return 255ul;
    }
    byte = v9x_gl_prim_to_long((double)value * 255.0);
    return byte < 0l ? 0ul : (byte > 255l ? 255ul : (v9x_u32)byte);
}

void v9x_gl_pipeline_init(V9X_GL_PIPELINE *pipeline)
{
    unsigned int i;

    for (i = 0u; i < 4u; ++i) {
        pipeline->color[i] = 1.0f;
        pipeline->tex[i] = i == 3u ? 1.0f : 0.0f;
    }
    pipeline->normal[0] = 0.0f;
    pipeline->normal[1] = 0.0f;
    pipeline->normal[2] = 1.0f;
    pipeline->shade_model = V9X_GL_SMOOTH;
    pipeline->cull_face = V9X_GL_BACK;
    pipeline->front_face = V9X_GL_CCW;
    pipeline->depth_func = V9X_GL_LESS;
    pipeline->blend_src = V9X_GL_ONE;
    pipeline->blend_dst = V9X_GL_ZERO;
    pipeline->alpha_func = V9X_GL_ALWAYS;
    pipeline->alpha_ref = 0.0f;
    pipeline->depth_near = 0.0;
    pipeline->depth_far = 1.0;
    pipeline->mode = V9X_GL_TRIANGLES;
    pipeline->count = 0ul;
    pipeline->batch_triangles = 0ul;
    pipeline->sink = 0;
    pipeline->sink_user = 0;
    pipeline->sink_failures = 0ul;
}

void v9x_gl_pipeline_sink(V9X_GL_PIPELINE *pipeline, V9X_GL_SINK_FN sink,
                          void *user)
{
    pipeline->sink = sink;
    pipeline->sink_user = user;
}

void v9x_gl_prim_color(V9X_GL_PIPELINE *pipeline, GLfloat r, GLfloat g,
                       GLfloat b, GLfloat a)
{
    pipeline->color[0] = r;
    pipeline->color[1] = g;
    pipeline->color[2] = b;
    pipeline->color[3] = a;
}

void v9x_gl_prim_texcoord(V9X_GL_PIPELINE *pipeline, GLfloat s, GLfloat t,
                          GLfloat r, GLfloat q)
{
    pipeline->tex[0] = s;
    pipeline->tex[1] = t;
    pipeline->tex[2] = r;
    pipeline->tex[3] = q;
}

void v9x_gl_prim_normal(V9X_GL_PIPELINE *pipeline, GLfloat x, GLfloat y,
                        GLfloat z)
{
    pipeline->normal[0] = x;
    pipeline->normal[1] = y;
    pipeline->normal[2] = z;
}

static int v9x_gl_prim_allowed(V9X_GL_STATE *state)
{
    if (state->in_begin) {
        v9x_gl_state_error(state, V9X_GL_INVALID_OPERATION);
        return 0;
    }
    return 1;
}

void v9x_gl_prim_shade_model(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                             GLenum mode)
{
    if (!v9x_gl_prim_allowed(state)) {
        return;
    }
    if (mode != V9X_GL_FLAT && mode != V9X_GL_SMOOTH) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    pipeline->shade_model = mode;
}

void v9x_gl_prim_cull_face(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                           GLenum mode)
{
    if (!v9x_gl_prim_allowed(state)) {
        return;
    }
    if (mode != V9X_GL_FRONT && mode != V9X_GL_BACK &&
        mode != V9X_GL_FRONT_AND_BACK) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    pipeline->cull_face = mode;
}

void v9x_gl_prim_front_face(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                            GLenum mode)
{
    if (!v9x_gl_prim_allowed(state)) {
        return;
    }
    if (mode != V9X_GL_CW && mode != V9X_GL_CCW) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    pipeline->front_face = mode;
}

static int v9x_gl_prim_compare_valid(GLenum func)
{
    return func >= V9X_GL_NEVER && func <= V9X_GL_ALWAYS;
}

void v9x_gl_prim_depth_func(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                            GLenum func)
{
    if (!v9x_gl_prim_allowed(state)) {
        return;
    }
    if (!v9x_gl_prim_compare_valid(func)) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    pipeline->depth_func = func;
}

/* GL's blend factor as D3D's (r3d.h's) number, or zero for none; `source`
 * because SRC_ALPHA_SATURATE is a source factor only in GL 1.1 (4.1.6). */
static v9x_u32 v9x_gl_prim_factor(GLenum factor, int source)
{
    if (factor == V9X_GL_ZERO) {
        return 1ul;
    }
    if (factor == V9X_GL_ONE) {
        return 2ul;
    }
    if (factor >= 0x0300u && factor <= 0x0307u) {
        /* SRC_COLOR, ONE_MINUS_SRC_COLOR, SRC_ALPHA, ONE_MINUS_SRC_ALPHA,
         * DST_ALPHA, ONE_MINUS_DST_ALPHA, DST_COLOR, ONE_MINUS_DST_COLOR:
         * D3D's 3..10 in the same order. */
        return (v9x_u32)(factor - 0x0300u) + 3ul;
    }
    if (factor == V9X_GL_SRC_ALPHA_SATURATE && source) {
        return 11ul;
    }
    return 0ul;
}

void v9x_gl_prim_blend_func(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                            GLenum src, GLenum dst)
{
    if (!v9x_gl_prim_allowed(state)) {
        return;
    }
    /* GL 1.1 leaves SRC_COLOR and ONE_MINUS_SRC_COLOR out of the source
     * factors and DST_COLOR and ONE_MINUS_DST_COLOR out of the destination
     * ones (4.1.6, tables 4.1 and 4.2). */
    if (v9x_gl_prim_factor(src, 1) == 0ul || src == 0x0300u ||
        src == 0x0301u || v9x_gl_prim_factor(dst, 0) == 0ul ||
        dst == 0x0306u || dst == 0x0307u) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    pipeline->blend_src = src;
    pipeline->blend_dst = dst;
}

void v9x_gl_prim_alpha_func(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                            GLenum func, GLclampf ref)
{
    if (!v9x_gl_prim_allowed(state)) {
        return;
    }
    if (!v9x_gl_prim_compare_valid(func)) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    pipeline->alpha_func = func;
    pipeline->alpha_ref = ref < 0.0f ? 0.0f : (ref > 1.0f ? 1.0f : ref);
}

void v9x_gl_prim_depth_range(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                             GLclampd near_value, GLclampd far_value)
{
    if (!v9x_gl_prim_allowed(state)) {
        return;
    }
    pipeline->depth_near = near_value < 0.0 ? 0.0
                         : (near_value > 1.0 ? 1.0 : near_value);
    pipeline->depth_far = far_value < 0.0 ? 0.0
                        : (far_value > 1.0 ? 1.0 : far_value);
}

void v9x_gl_prim_flush(V9X_GL_PIPELINE *pipeline)
{
    if (pipeline->batch_triangles == 0ul) {
        return;
    }
    if (pipeline->sink == 0 ||
        !pipeline->sink(pipeline->sink_user, pipeline->batch,
                        pipeline->batch_triangles)) {
        ++pipeline->sink_failures;
    }
    pipeline->batch_triangles = 0ul;
}

void v9x_gl_prim_begin(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                       GLenum mode)
{
    if (state->in_begin) {
        v9x_gl_state_error(state, V9X_GL_INVALID_OPERATION);
        return;
    }
    if (mode > V9X_GL_POLYGON) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    state->in_begin = 1;
    pipeline->mode = mode;
    pipeline->count = 0ul;
}

void v9x_gl_prim_end(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline)
{
    if (!state->in_begin) {
        v9x_gl_state_error(state, V9X_GL_INVALID_OPERATION);
        return;
    }
    state->in_begin = 0;
    /* A primitive the vertices did not complete draws nothing more; what
     * is batched goes now, so the next state change finds the batch
     * empty. */
    v9x_gl_prim_flush(pipeline);
}

/* ---- One triangle through clipping to the batch --------------------- */

/* The six frustum planes, as the signed distance of a clip-space vertex
 * inside when non-negative (2.11): w + x, w - x, w + y, w - y, w + z,
 * w - z. */
static GLfloat v9x_gl_prim_plane(const V9X_GL_VERTEX *v, unsigned int plane)
{
    GLfloat w = v->clip[3];

    switch (plane) {
    case 0u: return w + v->clip[0];
    case 1u: return w - v->clip[0];
    case 2u: return w + v->clip[1];
    case 3u: return w - v->clip[1];
    case 4u: return w + v->clip[2];
    default: return w - v->clip[2];
    }
}

static void v9x_gl_prim_lerp(V9X_GL_VERTEX *out, const V9X_GL_VERTEX *a,
                             const V9X_GL_VERTEX *b, GLfloat t)
{
    unsigned int i;

    for (i = 0u; i < 4u; ++i) {
        out->clip[i] = a->clip[i] + (b->clip[i] - a->clip[i]) * t;
        out->color[i] = a->color[i] + (b->color[i] - a->color[i]) * t;
        out->tex[i] = a->tex[i] + (b->tex[i] - a->tex[i]) * t;
    }
}

/* A triangle clipped against the six planes grows by at most one vertex
 * per plane. */
#define V9X_GL_PRIM_CLIP_MAX 9u

/*
 * Sutherland-Hodgman in clip space, where colour and texture coordinates
 * interpolate linearly (2.11: the clipped attributes are the linear blend
 * of the edge's). Returns the vertex count, 0 when nothing is left.
 */
static unsigned int v9x_gl_prim_clip(V9X_GL_VERTEX *polygon,
                                     unsigned int count)
{
    V9X_GL_VERTEX scratch[V9X_GL_PRIM_CLIP_MAX];
    unsigned int plane;

    for (plane = 0u; plane < 6u && count != 0u; ++plane) {
        unsigned int out = 0u;
        unsigned int i;

        for (i = 0u; i < count; ++i) {
            const V9X_GL_VERTEX *current = &polygon[i];
            const V9X_GL_VERTEX *previous = &polygon[i == 0u ? count - 1u
                                                             : i - 1u];
            GLfloat dc = v9x_gl_prim_plane(current, plane);
            GLfloat dp = v9x_gl_prim_plane(previous, plane);

            if ((dc >= 0.0f) != (dp >= 0.0f) &&
                out < V9X_GL_PRIM_CLIP_MAX) {
                v9x_gl_prim_lerp(&scratch[out++], previous, current,
                                 dp / (dp - dc));
            }
            if (dc >= 0.0f && out < V9X_GL_PRIM_CLIP_MAX) {
                scratch[out++] = *current;
            }
        }
        for (i = 0u; i < out; ++i) {
            polygon[i] = scratch[i];
        }
        count = out;
    }
    return count;
}

/* A clipped vertex in window coordinates (y up), as floats. */
typedef struct v9x_gl_window {
    GLfloat x;
    GLfloat y;
    GLfloat z;
    GLfloat rhw;
} V9X_GL_WINDOW;

static GLfloat v9x_gl_prim_clamp(GLfloat value, GLfloat low, GLfloat high)
{
    if (!(value > low)) {
        return low;
    }
    return value < high ? value : high;
}

static void v9x_gl_prim_window(const V9X_GL_STATE *state,
                               const V9X_GL_PIPELINE *pipeline,
                               const V9X_GL_VERTEX *v, V9X_GL_WINDOW *out)
{
    GLfloat rhw = 1.0f / v->clip[3];
    GLfloat xd = v->clip[0] * rhw;
    GLfloat yd = v->clip[1] * rhw;
    GLfloat zd = v->clip[2] * rhw;

    /* 2.10.1: xw = (px/2) xd + ox, zw = ((f-n)/2) zd + (n+f)/2. */
    out->x = (GLfloat)state->viewport[0] +
             (xd + 1.0f) * 0.5f * (GLfloat)state->viewport[2];
    out->y = (GLfloat)state->viewport[1] +
             (yd + 1.0f) * 0.5f * (GLfloat)state->viewport[3];
    out->z = (GLfloat)(((pipeline->depth_far - pipeline->depth_near) * 0.5) *
                           zd +
                       (pipeline->depth_near + pipeline->depth_far) * 0.5);
    out->rhw = rhw;

    /*
     * A clipped vertex lies inside the view volume, so its window position
     * lies inside the viewport and its depth inside the depth range - in
     * exact arithmetic. In floats the clip and the divide can leave it a
     * rounding step outside, or at -0.0, and Gen3's stream builder refuses
     * both (a sign bit or a coordinate past the surface names a write
     * outside it). Clamping to the bounds the mathematics already
     * guarantees moves no correct vertex. The low bounds are tested as
     * "not above", which also replaces -0.0 by the bound's +0.0.
     */
    out->x = v9x_gl_prim_clamp(out->x, (GLfloat)state->viewport[0],
                               (GLfloat)state->viewport[0] +
                                   (GLfloat)state->viewport[2]);
    out->y = v9x_gl_prim_clamp(out->y, (GLfloat)state->viewport[1],
                               (GLfloat)state->viewport[1] +
                                   (GLfloat)state->viewport[3]);
    out->z = pipeline->depth_near <= pipeline->depth_far
        ? v9x_gl_prim_clamp(out->z, (GLfloat)pipeline->depth_near,
                            (GLfloat)pipeline->depth_far)
        : v9x_gl_prim_clamp(out->z, (GLfloat)pipeline->depth_far,
                            (GLfloat)pipeline->depth_near);
}

static v9x_u32 v9x_gl_prim_argb(const GLfloat *color)
{
    return (v9x_gl_prim_byte(color[3]) << 24) |
           (v9x_gl_prim_byte(color[0]) << 16) |
           (v9x_gl_prim_byte(color[1]) << 8) |
           v9x_gl_prim_byte(color[2]);
}

static void v9x_gl_prim_emit(const V9X_GL_STATE *state,
                             V9X_R3D_ABI_VERTEX *out,
                             const V9X_GL_VERTEX *v,
                             const V9X_GL_WINDOW *w)
{
    GLfloat q = v->tex[3] != 0.0f ? v->tex[3] : 1.0f;

    out->sx = w->x;
    out->sy = (GLfloat)state->drawable_height - w->y;
    out->sz = w->z;
    /* The texture divisor: 1/w, or q/w for a projective coordinate, with
     * tu/tv the divided s/q and t/q (the rasterizer contract). */
    out->rhw = w->rhw * q;
    out->color = v9x_gl_prim_argb(v->color);
    out->specular = 0xff000000ul;
    out->tu = v->tex[0] / q;
    out->tv = v->tex[1] / q;
}

static void v9x_gl_prim_triangle(V9X_GL_STATE *state,
                                 V9X_GL_PIPELINE *pipeline,
                                 const V9X_GL_VERTEX *a,
                                 const V9X_GL_VERTEX *b,
                                 const V9X_GL_VERTEX *c,
                                 const V9X_GL_VERTEX *provoking)
{
    V9X_GL_VERTEX polygon[V9X_GL_PRIM_CLIP_MAX];
    V9X_GL_WINDOW window[V9X_GL_PRIM_CLIP_MAX];
    unsigned int count;
    unsigned int i;
    GLfloat area = 0.0f;

    polygon[0] = *a;
    polygon[1] = *b;
    polygon[2] = *c;
    if (pipeline->shade_model == V9X_GL_FLAT) {
        for (i = 0u; i < 3u; ++i) {
            polygon[i].color[0] = provoking->color[0];
            polygon[i].color[1] = provoking->color[1];
            polygon[i].color[2] = provoking->color[2];
            polygon[i].color[3] = provoking->color[3];
        }
    }
    count = v9x_gl_prim_clip(polygon, 3u);
    if (count < 3u) {
        return;
    }
    for (i = 0u; i < count; ++i) {
        v9x_gl_prim_window(state, pipeline, &polygon[i], &window[i]);
    }
    /* Facing from the signed area in window coordinates (2.13.1),
     * positive counter-clockwise; the clipped polygon keeps the
     * triangle's orientation. */
    for (i = 0u; i < count; ++i) {
        unsigned int j = i + 1u == count ? 0u : i + 1u;

        area += window[i].x * window[j].y - window[j].x * window[i].y;
    }
    if (v9x_gl_state_cap(state, V9X_GL_CULL_FACE)) {
        int front = pipeline->front_face == V9X_GL_CCW ? area > 0.0f
                                                        : area < 0.0f;

        if (pipeline->cull_face == V9X_GL_FRONT_AND_BACK ||
            (pipeline->cull_face == V9X_GL_FRONT && front) ||
            (pipeline->cull_face == V9X_GL_BACK && !front)) {
            return;
        }
    }
    for (i = 1u; i + 1u < count; ++i) {
        V9X_R3D_ABI_VERTEX *out;

        if (pipeline->batch_triangles >= V9X_R3D_ABI_BATCH_MAX) {
            v9x_gl_prim_flush(pipeline);
        }
        out = &pipeline->batch[pipeline->batch_triangles * 3ul];
        v9x_gl_prim_emit(state, &out[0], &polygon[0], &window[0]);
        v9x_gl_prim_emit(state, &out[1], &polygon[i], &window[i]);
        v9x_gl_prim_emit(state, &out[2], &polygon[i + 1u], &window[i + 1u]);
        ++pipeline->batch_triangles;
    }
    /* A full batch goes as soon as it fills, not at the next triangle. */
    if (pipeline->batch_triangles >= V9X_R3D_ABI_BATCH_MAX) {
        v9x_gl_prim_flush(pipeline);
    }
}

/* ---- Assembly ------------------------------------------------------- */

void v9x_gl_prim_vertex(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                        GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    const V9X_GL_MATRIX *modelview;
    const V9X_GL_MATRIX *projection;
    V9X_GL_VERTEX v;
    GLfloat eye[4];
    GLfloat object[4];
    unsigned int row;
    v9x_u32 n;

    if (!state->in_begin) {
        return;
    }
    modelview = v9x_gl_matrix_top(&state->matrices, V9X_GL_MODELVIEW);
    projection = v9x_gl_matrix_top(&state->matrices, V9X_GL_PROJECTION);
    object[0] = x;
    object[1] = y;
    object[2] = z;
    object[3] = w;
    for (row = 0u; row < 4u; ++row) {
        eye[row] = modelview->m[row] * object[0] +
                   modelview->m[4u + row] * object[1] +
                   modelview->m[8u + row] * object[2] +
                   modelview->m[12u + row] * object[3];
    }
    for (row = 0u; row < 4u; ++row) {
        v.clip[row] = projection->m[row] * eye[0] +
                      projection->m[4u + row] * eye[1] +
                      projection->m[8u + row] * eye[2] +
                      projection->m[12u + row] * eye[3];
        v.color[row] = pipeline->color[row];
        v.tex[row] = pipeline->tex[row];
    }

    n = pipeline->count++;
    switch (pipeline->mode) {
    case V9X_GL_TRIANGLES:
        if (n % 3ul == 2ul) {
            v9x_gl_prim_triangle(state, pipeline, &pipeline->previous[1],
                                 &pipeline->previous[2], &v, &v);
        }
        break;
    case V9X_GL_TRIANGLE_STRIP:
        /* Triangle i is (i, i+1, i+2) for even i and (i+1, i, i+2) for odd
         * i, so every triangle of the strip faces the same way (2.6.1). */
        if (n >= 2ul) {
            if ((n & 1ul) == 0ul) {
                v9x_gl_prim_triangle(state, pipeline, &pipeline->previous[1],
                                     &pipeline->previous[2], &v, &v);
            } else {
                v9x_gl_prim_triangle(state, pipeline, &pipeline->previous[2],
                                     &pipeline->previous[1], &v, &v);
            }
        }
        break;
    case V9X_GL_TRIANGLE_FAN:
        if (n >= 2ul) {
            v9x_gl_prim_triangle(state, pipeline, &pipeline->first,
                                 &pipeline->previous[2], &v, &v);
        }
        break;
    case V9X_GL_QUADS:
        /* (v0, v1, v2, v3) as (v0, v1, v2) and (v0, v2, v3), flat-shaded
         * from v3. */
        if (n % 4ul == 3ul) {
            v9x_gl_prim_triangle(state, pipeline, &pipeline->previous[0],
                                 &pipeline->previous[1],
                                 &pipeline->previous[2], &v);
            v9x_gl_prim_triangle(state, pipeline, &pipeline->previous[0],
                                 &pipeline->previous[2], &v, &v);
        }
        break;
    case V9X_GL_QUAD_STRIP:
        /* Quad i is (v2i, v2i+1, v2i+3, v2i+2), flat-shaded from v2i+3. */
        if (n >= 3ul && (n & 1ul) == 1ul) {
            v9x_gl_prim_triangle(state, pipeline, &pipeline->previous[1],
                                 &pipeline->previous[2], &v, &v);
            v9x_gl_prim_triangle(state, pipeline, &pipeline->previous[1],
                                 &v, &pipeline->previous[2], &v);
        }
        break;
    case V9X_GL_POLYGON:
        /* A fan from the first vertex, flat-shaded from the first. */
        if (n >= 2ul) {
            v9x_gl_prim_triangle(state, pipeline, &pipeline->first,
                                 &pipeline->previous[2], &v,
                                 &pipeline->first);
        }
        break;
    default:
        /* POINTS and the LINES family: accepted, not drawn yet
         * (r3d_line.c's coverage comes with the plan's Phase 4 lines). */
        break;
    }

    if (n == 0ul) {
        pipeline->first = v;
    }
    pipeline->previous[0] = pipeline->previous[1];
    pipeline->previous[1] = pipeline->previous[2];
    pipeline->previous[2] = v;
}

void v9x_gl_prim_abi_state(V9X_GL_STATE *state,
                           const V9X_GL_PIPELINE *pipeline,
                           V9X_R3D_ABI_STATE *out)
{
    /* GL's comparison enums are 0x0200..0x0207 in D3D's order; D3D's are
     * 1..8. */
    out->depth_enable = v9x_gl_state_cap(state, 0x0B71u) ? 1ul : 0ul;
    out->depth_write = state->depth_mask ? 1ul : 0ul;
    out->depth_func = (v9x_u32)(pipeline->depth_func - V9X_GL_NEVER) + 1ul;
    out->blend_enable = v9x_gl_state_cap(state, V9X_GL_BLEND) ? 1ul
                                                                     : 0ul;
    out->src_blend = v9x_gl_prim_factor(pipeline->blend_src, 1);
    out->dst_blend = v9x_gl_prim_factor(pipeline->blend_dst, 0);
    out->alpha_test_enable =
        v9x_gl_state_cap(state, V9X_GL_ALPHA_TEST) ? 1ul : 0ul;
    out->alpha_func = (v9x_u32)(pipeline->alpha_func - V9X_GL_NEVER) + 1ul;
    out->alpha_ref = v9x_gl_prim_byte(pipeline->alpha_ref);
    out->fog_enable = 0ul;
    out->fog_color = 0ul;
    out->write_mask = (state->color_mask[0] ? V9X_R3D_ABI_WRITE_RED : 0ul) |
                      (state->color_mask[1] ? V9X_R3D_ABI_WRITE_GREEN : 0ul) |
                      (state->color_mask[2] ? V9X_R3D_ABI_WRITE_BLUE : 0ul);
    /* The scissor box in surface rows, or the whole drawable. */
    out->scissor_left = 0ul;
    out->scissor_top = 0ul;
    out->scissor_right = state->drawable_width;
    out->scissor_bottom = state->drawable_height;
    if (v9x_gl_state_cap(state, V9X_GL_SCISSOR_TEST)) {
        long left = state->scissor[0];
        long right = left + state->scissor[2];
        long bottom = state->scissor[1];
        long top = bottom + state->scissor[3];
        long width = (long)state->drawable_width;
        long height = (long)state->drawable_height;

        left = left < 0l ? 0l : (left > width ? width : left);
        right = right < left ? left : (right > width ? width : right);
        bottom = bottom < 0l ? 0l : (bottom > height ? height : bottom);
        top = top < bottom ? bottom : (top > height ? height : top);
        out->scissor_left = (v9x_u32)left;
        out->scissor_right = (v9x_u32)right;
        out->scissor_top = (v9x_u32)(height - top);
        out->scissor_bottom = (v9x_u32)(height - bottom);
    }
}

/* A blend factor that reads the source alpha (table 4.1/4.2). */
static int v9x_gl_prim_factor_reads_alpha(GLenum factor)
{
    return factor == V9X_GL_SRC_ALPHA ||
           factor == V9X_GL_ONE_MINUS_SRC_ALPHA ||
           factor == V9X_GL_SRC_ALPHA_SATURATE;
}

int v9x_gl_prim_fragment_alpha_used(const V9X_GL_STATE *state,
                                    const V9X_GL_PIPELINE *pipeline)
{
    if (v9x_gl_state_cap(state, V9X_GL_ALPHA_TEST)) {
        return 1;
    }
    if (!v9x_gl_state_cap(state, V9X_GL_BLEND)) {
        return 0;
    }
    return v9x_gl_prim_factor_reads_alpha(pipeline->blend_src) ||
           v9x_gl_prim_factor_reads_alpha(pipeline->blend_dst);
}
