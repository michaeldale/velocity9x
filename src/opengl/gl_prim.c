/*
 * The vertex pipeline (gl_prim.h): assembly, transform, clipping, culling,
 * viewport, batches. Pure; no C runtime, so the one float-to-integer
 * conversion (a colour channel) goes through a local fistp, as gl_state.c
 * does.
 */
#include "gl_prim.h"

/* Polygon offset's r (3.5.5): one step of a 16- or 24-bit depth buffer. */
#define V9X_GL_PRIM_STEP_16 (1.0f / 65535.0f)
#define V9X_GL_PRIM_STEP_24 (1.0f / 16777215.0f)

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

/* The TSC for the stage profile, read only when the front end asked for
 * one (V9X_GL_PIPELINE.profile), which the host tests never do. */
#ifdef __WATCOMC__
static v9x_u32 v9x_gl_prim_rdtsc_low(void);
#pragma aux v9x_gl_prim_rdtsc_low = 0x0f 0x31 value [eax] modify exact [eax edx];
#else
static v9x_u32 v9x_gl_prim_rdtsc_low(void)
{
    return 0ul;
}
#endif

/* Charge the cycles since *mark to a stage and move the mark to now. */
static void v9x_gl_prim_prof_mark(V9X_GL_PIPELINE *pipeline,
                                  unsigned int stage, v9x_u32 *mark)
{
    v9x_u32 now;
    v9x_u32 delta;
    v9x_u32 *pair;

    if (pipeline->profile == 0) {
        return;
    }
    now = v9x_gl_prim_rdtsc_low();
    delta = now - *mark;
    pair = &pipeline->profile[stage * 2u];
    pair[0] += delta;
    if (pair[0] < delta) {
        ++pair[1];
    }
    *mark = now;
}

static void v9x_gl_prim_prof_count(V9X_GL_PIPELINE *pipeline,
                                   unsigned int index)
{
    if (pipeline->profile != 0) {
        ++pipeline->profile[index];
    }
}

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
    pipeline->offset_factor = 0.0f;
    pipeline->offset_units = 0.0f;
    pipeline->offset_step = V9X_GL_PRIM_STEP_16;
    pipeline->offset_on = 0;
    pipeline->edge_flag = 1u;
    pipeline->index = 1.0f;
    pipeline->fog_mode = V9X_GL_EXP;
    pipeline->fog_density = 1.0f;
    pipeline->fog_start = 0.0f;
    pipeline->fog_end = 1.0f;
    for (i = 0u; i < 4u; ++i) {
        pipeline->fog_color[i] = 0.0f;
    }
    pipeline->fog_index = 0.0f;
    pipeline->fog_on = 0;
    pipeline->mode = V9X_GL_TRIANGLES;
    pipeline->count = 0ul;
    pipeline->ring_head = 0u;
    pipeline->begin_valid = 0;
    pipeline->batch_triangles = 0ul;
    pipeline->tex1[0] = 0.0f;
    pipeline->tex1[1] = 0.0f;
    pipeline->units = 1ul;
    pipeline->argb_valid = 0;
    pipeline->sink = 0;
    pipeline->sink_user = 0;
    pipeline->sink_failures = 0ul;
    pipeline->profile = 0;
    pipeline->tnl_sink = 0;
    pipeline->tnl_vertices = 0;
    pipeline->tnl_indices = 0;
    pipeline->tnl_allowed = 0;
    pipeline->tnl_capture = 0;
    pipeline->tnl_serial = 1ul;
    pipeline->tnl_vertex_count = 0ul;
    pipeline->tnl_index_count = 0ul;
    pipeline->tnl_mvp_valid = 0;
    pipeline->tnl_elements = 0;
    pipeline->tnl_call = 0ul;
    pipeline->tnl_element_next = 0ul;
    pipeline->tnl_taken = 0ul;
    pipeline->tnl_declined = 0ul;
    pipeline->tnl_culled = 0ul;
    pipeline->tnl_abandoned = 0ul;
    pipeline->tnl_cached = 0ul;
    pipeline->tnl_normals = 0;
    pipeline->lighting = 0;
}

void v9x_gl_pipeline_sink(V9X_GL_PIPELINE *pipeline, V9X_GL_SINK_FN sink,
                          void *user)
{
    pipeline->sink = sink;
    pipeline->sink_user = user;
}

void v9x_gl_pipeline_tnl(V9X_GL_PIPELINE *pipeline, V9X_GL_TNL_SINK_FN sink,
                         int allowed, V9X_R3D_ABI_VERTEX *vertices,
                         v9x_u16 *indices, V9X_GL_TNL_ELEMENT *elements,
                         GLfloat *normals)
{
    unsigned int i;

    pipeline->tnl_sink = sink;
    pipeline->tnl_vertices = vertices;
    pipeline->tnl_indices = indices;
    pipeline->tnl_elements = elements;
    pipeline->tnl_normals = normals;
    if (elements != 0) {
        for (i = 0u; i < V9X_GL_TNL_ELEMENTS; ++i) {
            elements[i].element = 0ul;
        }
    }
    pipeline->tnl_allowed = allowed != 0 && sink != 0 && vertices != 0 &&
                            indices != 0;
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

void v9x_gl_pipeline_units(V9X_GL_PIPELINE *pipeline, v9x_u32 units)
{
    pipeline->units = units > 1ul ? 2ul : 1ul;
}

void v9x_gl_prim_texcoord1(V9X_GL_PIPELINE *pipeline, GLfloat s, GLfloat t)
{
    pipeline->tex1[0] = s;
    pipeline->tex1[1] = t;
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

/*
 * A D3D blend factor with the destination's alpha taken as 1. No pixel
 * format the ICD offers has alpha bits, and GL reads the alpha of such a
 * framebuffer as 1.0 (GL 1.1 4.3.2): DESTALPHA is then ONE, INVDESTALPHA
 * ZERO, and SRC_ALPHA_SATURATE's min(As, 1 - Ad) is 0, which is ZERO for
 * the colour this target holds. Handing an engine the alpha factors asks
 * it for alpha the target does not store: the HD 4600 refused Quake 3's
 * GL_DST_COLOR / GL_ONE_MINUS_DST_ALPHA pass outright, and its floor patch
 * drew without it (GitHub issue 11, 2026-10-22).
 */
static v9x_u32 v9x_gl_prim_no_dest_alpha(v9x_u32 factor)
{
    if (factor == 7ul) {            /* DESTALPHA */
        return 2ul;                 /* ONE */
    }
    if (factor == 8ul || factor == 11ul) {  /* INVDESTALPHA, SATURATE */
        return 1ul;                 /* ZERO */
    }
    return factor;
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

void v9x_gl_prim_polygon_offset(V9X_GL_STATE *state,
                                V9X_GL_PIPELINE *pipeline,
                                GLfloat factor, GLfloat units)
{
    if (!v9x_gl_prim_allowed(state)) {
        return;
    }
    pipeline->offset_factor = factor;
    pipeline->offset_units = units;
}

void v9x_gl_prim_edge_flag(V9X_GL_PIPELINE *pipeline, GLboolean flag)
{
    pipeline->edge_flag = flag ? 1u : 0u;
}

void v9x_gl_prim_index(V9X_GL_PIPELINE *pipeline, GLfloat index)
{
    pipeline->index = index;
}

static GLfloat v9x_gl_prim_clamp(GLfloat value, GLfloat low, GLfloat high);

void v9x_gl_prim_fog(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                     GLenum pname, const GLfloat *params, int vector)
{
    GLenum mode;
    unsigned int i;

    if (!v9x_gl_prim_allowed(state)) {
        return;
    }
    switch (pname) {
    case V9X_GL_FOG_MODE:
        mode = (GLenum)v9x_gl_prim_to_long((double)params[0]);
        if (mode != V9X_GL_LINEAR && mode != V9X_GL_EXP &&
            mode != V9X_GL_EXP2) {
            v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
            return;
        }
        pipeline->fog_mode = mode;
        return;
    case V9X_GL_FOG_DENSITY:
        if (params[0] < 0.0f) {
            v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
            return;
        }
        pipeline->fog_density = params[0];
        return;
    case V9X_GL_FOG_START:
        pipeline->fog_start = params[0];
        return;
    case V9X_GL_FOG_END:
        pipeline->fog_end = params[0];
        return;
    case V9X_GL_FOG_INDEX:
        pipeline->fog_index = params[0];
        return;
    case V9X_GL_FOG_COLOR:
        if (!vector) {
            v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
            return;
        }
        for (i = 0u; i < 4u; ++i) {
            pipeline->fog_color[i] = v9x_gl_prim_clamp(params[i], 0.0f, 1.0f);
        }
        return;
    default:
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
}

/*
 * e^-x for x >= 0 without a C runtime: halve x to at most one half, sum
 * the Taylor series there - eight terms leave an error near 5e-9, which
 * the six squarings back grow to under 1e-6, far below the 1/255 a factor
 * is stored to - and square back. Past V9X_GL_PRIM_EXP_ZERO the result is
 * under 1e-9, which no factor byte tells from zero. The term divisor is
 * counted in a double: an unsigned-to-double conversion would call the
 * runtime helper the ICD does not link.
 */
#define V9X_GL_PRIM_EXP_ZERO 21.0

static double v9x_gl_prim_exp_negative(double x)
{
    double term = 1.0;
    double sum = 1.0;
    double divisor;
    unsigned int halvings = 0u;

    if (!(x > 0.0)) {
        return 1.0;
    }
    if (x > V9X_GL_PRIM_EXP_ZERO) {
        return 0.0;
    }
    while (x > 0.5) {
        x *= 0.5;
        ++halvings;
    }
    for (divisor = 1.0; divisor <= 8.0; divisor += 1.0) {
        term *= -x / divisor;
        sum += term;
    }
    while (halvings > 0u) {
        sum *= sum;
        --halvings;
    }
    return sum;
}

GLfloat v9x_gl_prim_fog_factor(const V9X_GL_PIPELINE *pipeline, GLfloat c)
{
    double f;
    double dc;

    if (pipeline->fog_mode == V9X_GL_LINEAR) {
        /* No range to fog across: the division is undefined, and an
         * unfogged fragment is what other implementations draw. */
        if (pipeline->fog_end == pipeline->fog_start) {
            return 1.0f;
        }
        f = ((double)pipeline->fog_end - (double)c) /
            ((double)pipeline->fog_end - (double)pipeline->fog_start);
    } else {
        dc = (double)pipeline->fog_density * (double)c;
        f = v9x_gl_prim_exp_negative(pipeline->fog_mode == V9X_GL_EXP2
                                         ? dc * dc : dc);
    }
    if (!(f > 0.0)) {
        return 0.0f;
    }
    return f >= 1.0 ? 1.0f : (GLfloat)f;
}

void v9x_gl_prim_rect(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                      GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
    /* Checked here, not left to glBegin: its error would still let the
     * four vertices join the primitive being assembled. */
    if (!v9x_gl_prim_allowed(state)) {
        return;
    }
    v9x_gl_prim_begin(state, pipeline, V9X_GL_POLYGON);
    v9x_gl_prim_vertex(state, pipeline, x1, y1, 0.0f, 1.0f);
    v9x_gl_prim_vertex(state, pipeline, x2, y1, 0.0f, 1.0f);
    v9x_gl_prim_vertex(state, pipeline, x2, y2, 0.0f, 1.0f);
    v9x_gl_prim_vertex(state, pipeline, x1, y2, 0.0f, 1.0f);
    v9x_gl_prim_end(state, pipeline);
}

void v9x_gl_prim_flush(V9X_GL_PIPELINE *pipeline)
{
    v9x_u32 vertices = pipeline->tnl_vertex_count;
    v9x_u32 indices = pipeline->tnl_index_count;

    /* The captured batch, then any screen batch; a primitive is one or the
     * other, and End flushes between them. The serial moves on, so no
     * later triangle names a vertex of this batch. A batch the cull left
     * empty has no indices, and is not sent: Half-Life sent a draw for
     * every primitive of a back-facing or off-screen model otherwise, 58%
     * of its captured triangles (the 4570, boot 85). */
    pipeline->tnl_vertex_count = 0ul;
    pipeline->tnl_index_count = 0ul;
    ++pipeline->tnl_serial;
    if (indices != 0ul) {
        if (pipeline->tnl_sink != 0 &&
            pipeline->tnl_sink(pipeline->sink_user, pipeline->tnl_vertices,
                               pipeline->tnl_normals, vertices,
                               pipeline->tnl_indices, indices)) {
            ++pipeline->tnl_taken;
        } else {
            ++pipeline->tnl_declined;
            ++pipeline->sink_failures;
        }
    }
    if (pipeline->batch_triangles == 0ul) {
        return;
    }
    if (pipeline->sink == 0 ||
        !pipeline->sink(pipeline->sink_user, pipeline->batch,
                        pipeline->units > 1ul ? pipeline->batch_tex1 : 0,
                        pipeline->batch_triangles)) {
        ++pipeline->sink_failures;
    }
    pipeline->batch_triangles = 0ul;
}

static int v9x_gl_prim_clip_edges(const V9X_GL_STATE *state,
                                  GLfloat *edge);
static int v9x_gl_prim_draw_rect(const V9X_GL_STATE *state, GLfloat *rect);

/*
 * Whether this Begin's setup inputs are the last setup's: the viewport, the
 * scissor box and its enable, the drawable's size and the depth range,
 * which are everything v9x_gl_prim_clip_edges, v9x_gl_prim_draw_rect and
 * the depth terms read. The setup is a pure function of them, so equal
 * inputs leave the kept results exact.
 */
static int v9x_gl_prim_begin_unchanged(const V9X_GL_STATE *state,
                                       const V9X_GL_PIPELINE *pipeline)
{
    unsigned int i;

    if (!pipeline->begin_valid ||
        pipeline->begin_scissor_on !=
            (v9x_gl_state_cap(state, V9X_GL_SCISSOR_TEST) ? 1 : 0) ||
        pipeline->begin_drawable[0] != state->drawable_width ||
        pipeline->begin_drawable[1] != state->drawable_height ||
        pipeline->begin_depth[0] != pipeline->depth_near ||
        pipeline->begin_depth[1] != pipeline->depth_far) {
        return 0;
    }
    for (i = 0u; i < 4u; ++i) {
        if (pipeline->begin_viewport[i] != state->viewport[i] ||
            pipeline->begin_scissor[i] != state->scissor[i]) {
            return 0;
        }
    }
    return 1;
}

static void v9x_gl_prim_begin_record(const V9X_GL_STATE *state,
                                     V9X_GL_PIPELINE *pipeline)
{
    unsigned int i;

    pipeline->begin_scissor_on =
        v9x_gl_state_cap(state, V9X_GL_SCISSOR_TEST) ? 1 : 0;
    pipeline->begin_drawable[0] = state->drawable_width;
    pipeline->begin_drawable[1] = state->drawable_height;
    pipeline->begin_depth[0] = pipeline->depth_near;
    pipeline->begin_depth[1] = pipeline->depth_far;
    for (i = 0u; i < 4u; ++i) {
        pipeline->begin_viewport[i] = state->viewport[i];
        pipeline->begin_scissor[i] = state->scissor[i];
    }
    pipeline->begin_valid = 1;
}

/*
 * The capture's clip transform, P x MV (step 3b), made again only when
 * either matrix differs from what it was made from: Half-Life begins a
 * primitive per polygon under one model-view, and the product is 64
 * multiplies against 32 compares.
 */
static void v9x_gl_prim_tnl_mvp(const V9X_GL_STATE *state,
                                V9X_GL_PIPELINE *pipeline)
{
    const V9X_GL_MATRIX *mv;
    const V9X_GL_MATRIX *p;
    unsigned int i;
    unsigned int row;
    unsigned int column;

    mv = v9x_gl_matrix_top(&state->matrices, V9X_GL_MODELVIEW);
    p = v9x_gl_matrix_top(&state->matrices, V9X_GL_PROJECTION);
    if (pipeline->tnl_mvp_valid) {
        for (i = 0u; i < 16u; ++i) {
            if (pipeline->tnl_mvp_from[i] != mv->m[i] ||
                pipeline->tnl_mvp_from[16u + i] != p->m[i]) {
                break;
            }
        }
        if (i == 16u) {
            return;
        }
    }
    /* Column-major, as the matrices are: element (row, column) is
     * m[column * 4 + row]. */
    for (column = 0u; column < 4u; ++column) {
        for (row = 0u; row < 4u; ++row) {
            GLfloat sum = 0.0f;

            for (i = 0u; i < 4u; ++i) {
                sum += p->m[i * 4u + row] * mv->m[column * 4u + i];
            }
            pipeline->tnl_mvp[column * 4u + row] = sum;
        }
    }
    for (i = 0u; i < 16u; ++i) {
        pipeline->tnl_mvp_from[i] = mv->m[i];
        pipeline->tnl_mvp_from[16u + i] = p->m[i];
    }
    pipeline->tnl_mvp_valid = 1;
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
    pipeline->offset_on =
        v9x_gl_state_cap(state, V9X_GL_POLYGON_OFFSET_FILL) ? 1 : 0;
    /* A 32-bit buffer steps as a 24-bit one: the vertex carries depth as a
     * float, whose 24-bit mantissa resolves no finer step near 1, so a
     * smaller r would move a triangle by nothing at all. */
    pipeline->offset_step = state->depth_bits >= 24ul ? V9X_GL_PRIM_STEP_24
                                                      : V9X_GL_PRIM_STEP_16;
    pipeline->fog_on = v9x_gl_state_cap(state, V9X_GL_FOG) ? 1 : 0;
    /* Hardware T&L: filled primitives whose every effect the generated
     * kernel reproduces (gl_prim.h, tnl_capture). Step 4 added fog and
     * lighting the key can express, lit ones only with somewhere to keep
     * their normals. A FRONT_AND_BACK cull draws nothing either way and is
     * left alone. */
    pipeline->tnl_capture =
        pipeline->tnl_allowed && mode >= V9X_GL_TRIANGLES &&
        !pipeline->offset_on &&
        pipeline->shade_model == V9X_GL_SMOOTH && pipeline->units <= 1ul &&
        pipeline->tex[3] == 1.0f &&
        !(v9x_gl_state_cap(state, V9X_GL_CULL_FACE) &&
          pipeline->cull_face == V9X_GL_FRONT_AND_BACK) &&
        (!v9x_gl_state_cap(state, V9X_GL_LIGHTING_CAP) ||
         (pipeline->tnl_normals != 0 &&
          (pipeline->lighting == 0 ||
           v9x_gl_lighting_expressible(state, pipeline->lighting))));
    if (pipeline->tnl_capture) {
        v9x_gl_prim_tnl_mvp(state, pipeline);
    }
    if (v9x_gl_prim_begin_unchanged(state, pipeline)) {
        return;
    }
    pipeline->clip_ready = v9x_gl_prim_clip_edges(state, pipeline->clip_edge);
    (void)v9x_gl_prim_draw_rect(state, pipeline->window_rect);
    pipeline->viewport_f[0] = (GLfloat)state->viewport[0];
    pipeline->viewport_f[1] = (GLfloat)state->viewport[1];
    pipeline->viewport_f[2] = (GLfloat)state->viewport[2];
    pipeline->viewport_f[3] = (GLfloat)state->viewport[3];
    pipeline->depth_scale = (pipeline->depth_far - pipeline->depth_near) * 0.5;
    pipeline->depth_bias = (pipeline->depth_near + pipeline->depth_far) * 0.5;
    v9x_gl_prim_begin_record(state, pipeline);
}

void v9x_gl_prim_end(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline)
{
    if (!state->in_begin) {
        v9x_gl_state_error(state, V9X_GL_INVALID_OPERATION);
        return;
    }
    state->in_begin = 0;
    pipeline->tnl_element_next = 0ul;
    /* A primitive the vertices did not complete draws nothing more; what
     * is batched goes now, so the next state change finds the batch
     * empty. */
    v9x_gl_prim_flush(pipeline);
}

/* ---- One triangle through clipping to the batch --------------------- */

/*
 * The rectangle a draw may write, in GL window coordinates (y up): the
 * drawable, cut to the scissor box when the scissor test is on. Zero when
 * it is empty. Pixels outside it are not written (4.1.2), so geometry is
 * clipped to it here rather than tested per pixel by the engine: the same
 * pixels, and an engine without a scissor (Gen3's builder emits none) can
 * draw it, as can one that refuses coordinates past the surface.
 */
static int v9x_gl_prim_draw_rect(const V9X_GL_STATE *state, GLfloat *rect)
{
    GLfloat left = 0.0f;
    GLfloat bottom = 0.0f;
    GLfloat right = (GLfloat)state->drawable_width;
    GLfloat top = (GLfloat)state->drawable_height;

    if (v9x_gl_state_cap(state, V9X_GL_SCISSOR_TEST)) {
        GLfloat sl = (GLfloat)state->scissor[0];
        GLfloat sb = (GLfloat)state->scissor[1];
        GLfloat sr = sl + (GLfloat)state->scissor[2];
        GLfloat st = sb + (GLfloat)state->scissor[3];

        left = sl > left ? sl : left;
        bottom = sb > bottom ? sb : bottom;
        right = sr < right ? sr : right;
        top = st < top ? st : top;
    }
    rect[0] = left;
    rect[1] = bottom;
    rect[2] = right;
    rect[3] = top;
    return right > left && top > bottom;
}

/*
 * The clip planes, as the signed distance of a clip-space vertex inside
 * when non-negative. 0 to 5 are the frustum's (2.11): w + x, w - x, w + y,
 * w - y, w + z, w - z. 6 to 9 are the draw rectangle's, carried into clip
 * space through the viewport: x_w >= left is x - a w >= 0 with
 * a = 2 (left - vx) / vw - 1, and likewise for the other three sides, so
 * attributes interpolate across them exactly as across the frustum's.
 */
static GLfloat v9x_gl_prim_plane(const V9X_GL_VERTEX *v, unsigned int plane,
                                 const GLfloat *edge)
{
    GLfloat w = v->clip[3];

    switch (plane) {
    case 0u: return w + v->clip[0];
    case 1u: return w - v->clip[0];
    case 2u: return w + v->clip[1];
    case 3u: return w - v->clip[1];
    case 4u: return w + v->clip[2];
    case 5u: return w - v->clip[2];
    case 6u: return v->clip[0] - edge[0] * w;
    case 7u: return edge[2] * w - v->clip[0];
    case 8u: return v->clip[1] - edge[1] * w;
    default: return edge[3] * w - v->clip[1];
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
    out->tex1[0] = a->tex1[0] + (b->tex1[0] - a->tex1[0]) * t;
    out->tex1[1] = a->tex1[1] + (b->tex1[1] - a->tex1[1]) * t;
    out->fog_c = a->fog_c + (b->fog_c - a->fog_c) * t;
}

/* A triangle clipped against the ten planes grows by at most one vertex
 * per plane. */
#define V9X_GL_PRIM_CLIP_PLANES 10u
#define V9X_GL_PRIM_CLIP_MAX 13u

/*
 * Sutherland-Hodgman in clip space, where colour and texture coordinates
 * interpolate linearly (2.11: the clipped attributes are the linear blend
 * of the edge's). Returns the vertex count, 0 when nothing is left.
 */
/* The draw rectangle's sides in normalised device coordinates, for planes
 * 6 to 9. Zero when the rectangle or the viewport has no area. */
static int v9x_gl_prim_clip_edges(const V9X_GL_STATE *state, GLfloat *edge)
{
    GLfloat rect[4];
    GLfloat vx = (GLfloat)state->viewport[0];
    GLfloat vy = (GLfloat)state->viewport[1];
    GLfloat vw = (GLfloat)state->viewport[2];
    GLfloat vh = (GLfloat)state->viewport[3];

    if (!v9x_gl_prim_draw_rect(state, rect) || !(vw > 0.0f) ||
        !(vh > 0.0f)) {
        return 0;
    }
    edge[0] = 2.0f * (rect[0] - vx) / vw - 1.0f;
    edge[1] = 2.0f * (rect[1] - vy) / vh - 1.0f;
    edge[2] = 2.0f * (rect[2] - vx) / vw - 1.0f;
    edge[3] = 2.0f * (rect[3] - vy) / vh - 1.0f;
    return 1;
}

/*
 * Sutherland-Hodgman over the ten planes, with the same arithmetic as the
 * plain form and less copying (2026-10-01, when clipping a quarter of
 * Quake 2's triangles cost more than transforming every vertex):
 *
 * - each plane's distances are taken once per vertex and kept. They are
 *   v9x_gl_prim_plane's values, which it rounds to float on return, so
 *   keeping them as floats changes nothing; the plain form took each twice.
 * - a plane every vertex of the current polygon is inside is skipped: the
 *   pass would return the polygon unchanged and in order. "Inside" is the
 *   same >= 0 test the pass makes, so a NaN still runs it.
 * - passes alternate between the caller's array and the scratch rather
 *   than copying the scratch back after each; one copy at the end if the
 *   result is in the scratch.
 *
 * test_pipeline_output_unchanged holds every emitted byte to the plain
 * form's.
 */
static unsigned int v9x_gl_prim_clip(const V9X_GL_STATE *state,
                                     V9X_GL_VERTEX *polygon,
                                     unsigned int count)
{
    V9X_GL_VERTEX scratch[V9X_GL_PRIM_CLIP_MAX];
    GLfloat distance[V9X_GL_PRIM_CLIP_MAX];
    GLfloat edge[4];
    V9X_GL_VERTEX *in = polygon;
    V9X_GL_VERTEX *out = scratch;
    unsigned int plane;
    unsigned int i;

    if (!v9x_gl_prim_clip_edges(state, edge)) {
        return 0u;
    }

    for (plane = 0u; plane < V9X_GL_PRIM_CLIP_PLANES && count != 0u;
         ++plane) {
        V9X_GL_VERTEX *swap;
        unsigned int written = 0u;
        int all_inside = 1;

        for (i = 0u; i < count; ++i) {
            distance[i] = v9x_gl_prim_plane(&in[i], plane, edge);
            if (!(distance[i] >= 0.0f)) {
                all_inside = 0;
            }
        }
        if (all_inside) {
            continue;
        }

        for (i = 0u; i < count; ++i) {
            unsigned int before = i == 0u ? count - 1u : i - 1u;
            GLfloat dc = distance[i];
            GLfloat dp = distance[before];

            if ((dc >= 0.0f) != (dp >= 0.0f) &&
                written < V9X_GL_PRIM_CLIP_MAX) {
                v9x_gl_prim_lerp(&out[written++], &in[before], &in[i],
                                 dp / (dp - dc));
            }
            if (dc >= 0.0f && written < V9X_GL_PRIM_CLIP_MAX) {
                out[written++] = in[i];
            }
        }
        swap = in;
        in = out;
        out = swap;
        count = written;
    }
    if (in != polygon) {
        for (i = 0u; i < count; ++i) {
            polygon[i] = in[i];
        }
    }
    return count;
}

/*
 * Whether a vertex is inside all ten planes - v9x_gl_prim_clip's test -
 * with the ten distances written out rather than ten calls through the
 * plane switch. The expressions are v9x_gl_prim_plane's, operand for
 * operand, and each is stored as a float before it is compared, as that
 * function's return rounds it.
 */
static int v9x_gl_prim_inside(const V9X_GL_VERTEX *v, const GLfloat *edge)
{
    GLfloat distance[V9X_GL_PRIM_CLIP_PLANES];
    GLfloat w = v->clip[3];
    unsigned int plane;

    distance[0] = w + v->clip[0];
    distance[1] = w - v->clip[0];
    distance[2] = w + v->clip[1];
    distance[3] = w - v->clip[1];
    distance[4] = w + v->clip[2];
    distance[5] = w - v->clip[2];
    distance[6] = v->clip[0] - edge[0] * w;
    distance[7] = edge[2] * w - v->clip[0];
    distance[8] = v->clip[1] - edge[1] * w;
    distance[9] = edge[3] * w - v->clip[1];
    for (plane = 0u; plane < V9X_GL_PRIM_CLIP_PLANES; ++plane) {
        if (!(distance[plane] >= 0.0f)) {
            return 0;
        }
    }
    return 1;
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
    const GLfloat *rect = pipeline->window_rect;
    GLfloat xd = v->clip[0] * rhw;
    GLfloat yd = v->clip[1] * rhw;
    GLfloat zd = v->clip[2] * rhw;

    (void)state;
    /* 2.10.1: xw = (px/2) xd + ox, zw = ((f-n)/2) zd + (n+f)/2, with the
     * viewport and the two depth terms taken at Begin. */
    out->x = pipeline->viewport_f[0] +
             (xd + 1.0f) * 0.5f * pipeline->viewport_f[2];
    out->y = pipeline->viewport_f[1] +
             (yd + 1.0f) * 0.5f * pipeline->viewport_f[3];
    out->z = (GLfloat)(pipeline->depth_scale * zd + pipeline->depth_bias);
    out->rhw = rhw;

    /*
     * A clipped vertex lies inside the view volume, so its window position
     * lies inside the viewport and its depth inside the depth range - in
     * exact arithmetic. In floats the clip and the divide can leave it a
     * rounding step outside, or at -0.0, and Gen3's stream builder refuses
     * both (a sign bit or a coordinate past the surface names a write
     * outside it). Clamping to the bounds the clip already guarantees -
     * the draw rectangle and the depth range - moves no correct vertex.
     * The low bounds are tested as
     * "not above", which also replaces -0.0 by the bound's +0.0.
     */
    out->x = v9x_gl_prim_clamp(out->x, rect[0], rect[2]);
    out->y = v9x_gl_prim_clamp(out->y, rect[1], rect[3]);
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

/* The packed colour, from the pipeline's last packing when the floats are
 * the same ones; a NaN never compares equal, so it is packed each time. */
static v9x_u32 v9x_gl_prim_argb_cached(V9X_GL_PIPELINE *pipeline,
                                       const GLfloat *color)
{
    if (pipeline->argb_valid &&
        pipeline->argb_from[0] == color[0] &&
        pipeline->argb_from[1] == color[1] &&
        pipeline->argb_from[2] == color[2] &&
        pipeline->argb_from[3] == color[3]) {
        return pipeline->argb;
    }
    pipeline->argb_from[0] = color[0];
    pipeline->argb_from[1] = color[1];
    pipeline->argb_from[2] = color[2];
    pipeline->argb_from[3] = color[3];
    pipeline->argb = v9x_gl_prim_argb(color);
    pipeline->argb_valid = 1;
    return pipeline->argb;
}

static void v9x_gl_prim_emit(const V9X_GL_STATE *state,
                             V9X_GL_PIPELINE *pipeline,
                             V9X_R3D_ABI_VERTEX *out,
                             const V9X_GL_VERTEX *v,
                             const V9X_GL_WINDOW *w)
{
    GLfloat q = v->tex[3] != 0.0f ? v->tex[3] : 1.0f;

    out->sx = w->x;
    out->sy = (GLfloat)state->drawable_height - w->y;
    out->sz = w->z;
    /* The texture divisor: 1/w, or q/w for a projective coordinate, with
     * tu/tv the divided s/q and t/q (the rasterizer contract). A q of one
     * divides nothing, and is what every non-projective call sends. */
    out->color = v9x_gl_prim_argb_cached(pipeline, v->color);
    /* Fog per vertex, interpolated by the engine (3.9 allows it). */
    out->specular = pipeline->fog_on
        ? v9x_gl_prim_byte(v9x_gl_prim_fog_factor(pipeline, v->fog_c)) << 24
        : 0xff000000ul;
    if (q == 1.0f) {
        out->rhw = w->rhw;
        out->tu = v->tex[0];
        out->tv = v->tex[1];
        return;
    }
    out->rhw = w->rhw * q;
    out->tu = v->tex[0] / q;
    out->tv = v->tex[1] / q;
}

/*
 * Polygon offset (3.5.5) on one emitted triangle: every depth moves by
 * factor * m + units * r and is clamped to [0, 1]. m is the larger of
 * |dz/dx| and |dz/dy| over the triangle's plane in window coordinates -
 * the specification's allowed approximation of the gradient's length - and
 * r is one step of the drawable's depth buffer (offset_step). A triangle
 * with no area has no plane, and is offset by the units alone. The low
 * clamp gives +0.0, never -0.0, which Gen3's stream builder refuses.
 */
static GLfloat v9x_gl_prim_abs(GLfloat value)
{
    return value < 0.0f ? -value : value;
}

static void v9x_gl_prim_offset(const V9X_GL_PIPELINE *pipeline,
                               V9X_R3D_ABI_VERTEX *out)
{
    GLfloat x1 = out[1].sx - out[0].sx;
    GLfloat y1 = out[1].sy - out[0].sy;
    GLfloat z1 = out[1].sz - out[0].sz;
    GLfloat x2 = out[2].sx - out[0].sx;
    GLfloat y2 = out[2].sy - out[0].sy;
    GLfloat z2 = out[2].sz - out[0].sz;
    GLfloat area = x1 * y2 - x2 * y1;
    GLfloat slope = 0.0f;
    GLfloat offset;
    unsigned int i;

    if (area != 0.0f) {
        GLfloat dzdx = v9x_gl_prim_abs((z1 * y2 - z2 * y1) / area);
        GLfloat dzdy = v9x_gl_prim_abs((x1 * z2 - x2 * z1) / area);

        slope = dzdx > dzdy ? dzdx : dzdy;
    }
    offset = pipeline->offset_factor * slope +
             pipeline->offset_units * pipeline->offset_step;
    for (i = 0u; i < 3u; ++i) {
        out[i].sz = v9x_gl_prim_clamp(out[i].sz + offset, 0.0f, 1.0f);
    }
}

/* Culled by its signed window area, positive counter-clockwise (2.13.1). */
static int v9x_gl_prim_culled(const V9X_GL_STATE *state,
                              const V9X_GL_PIPELINE *pipeline, GLfloat area)
{
    int front;

    if (!v9x_gl_state_cap(state, V9X_GL_CULL_FACE)) {
        return 0;
    }
    front = pipeline->front_face == V9X_GL_CCW ? area > 0.0f : area < 0.0f;
    return pipeline->cull_face == V9X_GL_FRONT_AND_BACK ||
           (pipeline->cull_face == V9X_GL_FRONT && front) ||
           (pipeline->cull_face == V9X_GL_BACK && !front);
}

/*
 * A captured triangle (step 3b): culled here only where the answer is
 * certain without clipping, and otherwise left whole to the engine, which
 * clips, culls and cuts to the scissor itself.
 *
 * - Every corner outside one frustum plane: the triangle is outside the
 *   half-space, and the view volume, whatever w's signs.
 * - Facing away under CULL_FACE, all three w positive: the window area's
 *   sign is then the sign of the determinant of the corners' (x, y, w)
 *   rows - the area of (x/w, y/w) is that determinant over w0 w1 w2, and
 *   the viewport scales x and y by positive amounts - which
 *   v9x_gl_prim_culled reads as it reads the CPU path's area. A corner
 *   with w not positive leaves the facing to the engine's clipper.
 *
 * A triangle left names its corners by index. A corner not yet in this
 * batch is sent now, once, and takes its index here; one cached from a
 * vertex array element records it in the cache too.
 */
static void v9x_gl_prim_tnl_triangle(V9X_GL_STATE *state,
                                     V9X_GL_PIPELINE *pipeline,
                                     V9X_GL_VERTEX *a,
                                     V9X_GL_VERTEX *b,
                                     V9X_GL_VERTEX *c)
{
    V9X_GL_VERTEX *corner[3];
    unsigned int i;

    if ((a->outcode & b->outcode & c->outcode) != 0ul ||
        !pipeline->clip_ready) {
        ++pipeline->tnl_culled;
        return;
    }
    if (v9x_gl_state_cap(state, V9X_GL_CULL_FACE) && a->clip[3] > 0.0f &&
        b->clip[3] > 0.0f && c->clip[3] > 0.0f) {
        double det =
            (double)a->clip[0] * ((double)b->clip[1] * c->clip[3] -
                                  (double)c->clip[1] * b->clip[3]) -
            (double)a->clip[1] * ((double)b->clip[0] * c->clip[3] -
                                  (double)c->clip[0] * b->clip[3]) +
            (double)a->clip[3] * ((double)b->clip[0] * c->clip[1] -
                                  (double)c->clip[0] * b->clip[1]);

        if (v9x_gl_prim_culled(state, pipeline,
                               det > 0.0 ? 1.0f : det < 0.0 ? -1.0f
                                                            : 0.0f)) {
            ++pipeline->tnl_culled;
            return;
        }
    }

    if (pipeline->tnl_index_count + 3u > V9X_GL_TNL_INDICES ||
        pipeline->tnl_vertex_count + 3u > V9X_GL_TNL_VERTICES) {
        v9x_gl_prim_flush(pipeline);
    }
    corner[0] = a;
    corner[1] = b;
    corner[2] = c;
    for (i = 0u; i < 3u; ++i) {
        v9x_u32 index = corner[i]->tnl_index;

        if (corner[i]->tnl_serial != pipeline->tnl_serial) {
            index = pipeline->tnl_vertex_count;
            pipeline->tnl_vertices[pipeline->tnl_vertex_count++] =
                corner[i]->object;
            if (pipeline->tnl_normals != 0) {
                GLfloat *normal = &pipeline->tnl_normals[index * 3ul];

                normal[0] = corner[i]->object_normal[0];
                normal[1] = corner[i]->object_normal[1];
                normal[2] = corner[i]->object_normal[2];
            }
            corner[i]->tnl_serial = pipeline->tnl_serial;
            corner[i]->tnl_index = index;
            if (corner[i]->tnl_element != 0ul) {
                V9X_GL_TNL_ELEMENT *entry =
                    &pipeline->tnl_elements[(corner[i]->tnl_element - 1ul) &
                                            (V9X_GL_TNL_ELEMENTS - 1u)];

                if (entry->element == corner[i]->tnl_element &&
                    entry->call == pipeline->tnl_call) {
                    entry->serial = pipeline->tnl_serial;
                    entry->index = index;
                }
            }
        }
        pipeline->tnl_indices[pipeline->tnl_index_count++] = (v9x_u16)index;
    }
}

/* The corners are the pipeline's own vertices (ring and first), not
 * const: a corner sent to a T&L batch takes its index there. */
static void v9x_gl_prim_triangle(V9X_GL_STATE *state,
                                 V9X_GL_PIPELINE *pipeline,
                                 V9X_GL_VERTEX *a,
                                 V9X_GL_VERTEX *b,
                                 V9X_GL_VERTEX *c,
                                 const V9X_GL_VERTEX *provoking)
{
    V9X_GL_VERTEX polygon[V9X_GL_PRIM_CLIP_MAX];
    V9X_GL_WINDOW window[V9X_GL_PRIM_CLIP_MAX];
    unsigned int count;
    unsigned int i;
    GLfloat area = 0.0f;

    if (pipeline->tnl_capture) {
        v9x_gl_prim_tnl_triangle(state, pipeline, a, b, c);
        return;
    }

    /* All three inside every plane: Sutherland-Hodgman would return them
     * as they are, in order, so the result below is the same without it. */
    if (a->inside && b->inside && c->inside) {
        const V9X_GL_VERTEX *corner[3];
        V9X_R3D_ABI_VERTEX *out;

        corner[0] = a;
        corner[1] = b;
        corner[2] = c;
        for (i = 0u; i < 3u; ++i) {
            unsigned int j = i + 1u == 3u ? 0u : i + 1u;

            area += corner[i]->window[0] * corner[j]->window[1] -
                    corner[j]->window[0] * corner[i]->window[1];
        }
        if (v9x_gl_prim_culled(state, pipeline, area)) {
            v9x_gl_prim_prof_count(pipeline, V9X_GL_PRIM_PROF_CULLED);
            return;
        }
        v9x_gl_prim_prof_count(pipeline, V9X_GL_PRIM_PROF_FAST);
        if (pipeline->batch_triangles >= V9X_R3D_ABI_BATCH_MAX) {
            v9x_gl_prim_flush(pipeline);
        }
        out = &pipeline->batch[pipeline->batch_triangles * 3ul];
        for (i = 0u; i < 3u; ++i) {
            out[i] = corner[i]->abi;
            if (pipeline->shade_model == V9X_GL_FLAT) {
                out[i].color = v9x_gl_prim_argb(provoking->color);
            }
        }
        if (pipeline->offset_on) {
            v9x_gl_prim_offset(pipeline, out);
        }
        if (pipeline->units > 1ul) {
            GLfloat *tex1 =
                &pipeline->batch_tex1[pipeline->batch_triangles * 6ul];

            for (i = 0u; i < 3u; ++i) {
                tex1[i * 2u] = corner[i]->tex1[0];
                tex1[i * 2u + 1u] = corner[i]->tex1[1];
            }
        }
        ++pipeline->batch_triangles;
        if (pipeline->batch_triangles >= V9X_R3D_ABI_BATCH_MAX) {
            v9x_gl_prim_flush(pipeline);
        }
        return;
    }

    v9x_gl_prim_prof_count(pipeline, V9X_GL_PRIM_PROF_CLIPPED);
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
    count = v9x_gl_prim_clip(state, polygon, 3u);
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
    if (v9x_gl_prim_culled(state, pipeline, area)) {
        return;
    }
    for (i = 1u; i + 1u < count; ++i) {
        V9X_R3D_ABI_VERTEX *out;

        if (pipeline->batch_triangles >= V9X_R3D_ABI_BATCH_MAX) {
            v9x_gl_prim_flush(pipeline);
        }
        out = &pipeline->batch[pipeline->batch_triangles * 3ul];
        v9x_gl_prim_emit(state, pipeline, &out[0], &polygon[0],
                         &window[0]);
        v9x_gl_prim_emit(state, pipeline, &out[1], &polygon[i],
                         &window[i]);
        v9x_gl_prim_emit(state, pipeline, &out[2], &polygon[i + 1u],
                         &window[i + 1u]);
        if (pipeline->offset_on) {
            v9x_gl_prim_offset(pipeline, out);
        }
        if (pipeline->units > 1ul) {
            GLfloat *tex1 =
                &pipeline->batch_tex1[pipeline->batch_triangles * 6ul];

            tex1[0] = polygon[0].tex1[0];
            tex1[1] = polygon[0].tex1[1];
            tex1[2] = polygon[i].tex1[0];
            tex1[3] = polygon[i].tex1[1];
            tex1[4] = polygon[i + 1u].tex1[0];
            tex1[5] = polygon[i + 1u].tex1[1];
        }
        ++pipeline->batch_triangles;
    }
    /* A full batch goes as soon as it fills, not at the next triangle. */
    if (pipeline->batch_triangles >= V9X_R3D_ABI_BATCH_MAX) {
        v9x_gl_prim_flush(pipeline);
    }
}

/* ---- Assembly ------------------------------------------------------- */

/*
 * The CPU path's vertex: object coordinates and attributes through both
 * matrices to clip space, then, inside every plane, to its window position
 * and emitted vertex (V9X_GL_VERTEX.inside).
 */
static void v9x_gl_prim_cpu_vertex(V9X_GL_STATE *state,
                                   V9X_GL_PIPELINE *pipeline,
                                   V9X_GL_VERTEX *vp, const GLfloat *object,
                                   const GLfloat *color, const GLfloat *tex,
                                   const GLfloat *tex1, v9x_u32 *mark)
{
    const V9X_GL_MATRIX *modelview;
    const V9X_GL_MATRIX *projection;
    GLfloat eye[4];
    unsigned int row;

    modelview = v9x_gl_matrix_top(&state->matrices, V9X_GL_MODELVIEW);
    projection = v9x_gl_matrix_top(&state->matrices, V9X_GL_PROJECTION);
    for (row = 0u; row < 4u; ++row) {
        eye[row] = modelview->m[row] * object[0] +
                   modelview->m[4u + row] * object[1] +
                   modelview->m[8u + row] * object[2] +
                   modelview->m[12u + row] * object[3];
    }
    for (row = 0u; row < 4u; ++row) {
        vp->clip[row] = projection->m[row] * eye[0] +
                      projection->m[4u + row] * eye[1] +
                      projection->m[8u + row] * eye[2] +
                      projection->m[12u + row] * eye[3];
        vp->color[row] = color[row];
        vp->tex[row] = tex[row];
    }
    vp->tex1[0] = tex1[0];
    vp->tex1[1] = tex1[1];
    vp->fog_c = v9x_gl_prim_abs(eye[2]);
    vp->tnl_serial = 0ul;
    vp->tnl_element = 0ul;
    /* Inside by the clipper's own test, plane by plane (v9x_gl_prim_clip):
     * then its window position and emitted vertex are what any triangle
     * it is a corner of would compute (V9X_GL_VERTEX.inside). */
    v9x_gl_prim_prof_mark(pipeline, V9X_GL_PRIM_PROF_TRANSFORM, mark);
    vp->inside = pipeline->clip_ready &&
                 v9x_gl_prim_inside(vp, pipeline->clip_edge);
    v9x_gl_prim_prof_mark(pipeline, V9X_GL_PRIM_PROF_INSIDE, mark);
    if (vp->inside) {
        V9X_GL_WINDOW w;

        v9x_gl_prim_window(state, pipeline, vp, &w);
        vp->window[0] = w.x;
        vp->window[1] = w.y;
        vp->window[2] = w.z;
        vp->window[3] = w.rhw;
        v9x_gl_prim_emit(state, pipeline, &vp->abi, vp, &w);
    }
    v9x_gl_prim_prof_mark(pipeline, V9X_GL_PRIM_PROF_WINDOW, mark);
}

/* The six frustum planes' outcode of a clip position: bit k set when
 * v9x_gl_prim_plane's plane k is negative, or not a number. */
static v9x_u32 v9x_gl_prim_outcode(const GLfloat *clip)
{
    GLfloat w = clip[3];
    v9x_u32 code = 0ul;

    if (!(w + clip[0] >= 0.0f)) {
        code |= 0x01ul;
    }
    if (!(w - clip[0] >= 0.0f)) {
        code |= 0x02ul;
    }
    if (!(w + clip[1] >= 0.0f)) {
        code |= 0x04ul;
    }
    if (!(w - clip[1] >= 0.0f)) {
        code |= 0x08ul;
    }
    if (!(w + clip[2] >= 0.0f)) {
        code |= 0x10ul;
    }
    if (!(w - clip[2] >= 0.0f)) {
        code |= 0x20ul;
    }
    return code;
}

/*
 * A captured vertex (step 3b): the engine's copy - object position, the
 * packed colour, an unfogged specular, s and t - and, for the cull, its
 * clip position through tnl_mvp and outcode. Nothing of the CPU path's
 * screen vertex is made. It goes in the batch only when a triangle that
 * survives the cull names it (v9x_gl_prim_tnl_triangle).
 */
static void v9x_gl_prim_tnl_vertex(V9X_GL_PIPELINE *pipeline,
                                   V9X_GL_VERTEX *vp, GLfloat x, GLfloat y,
                                   GLfloat z, GLfloat w)
{
    const GLfloat *m = pipeline->tnl_mvp;
    unsigned int row;

    for (row = 0u; row < 4u; ++row) {
        vp->clip[row] = m[row] * x + m[4u + row] * y + m[8u + row] * z +
                        m[12u + row] * w;
    }
    vp->outcode = v9x_gl_prim_outcode(vp->clip);
    vp->object.sx = x;
    vp->object.sy = y;
    vp->object.sz = z;
    vp->object.rhw = w;
    vp->object.color = v9x_gl_prim_argb_cached(pipeline, pipeline->color);
    vp->object.specular = 0xff000000ul;
    vp->object.tu = pipeline->tex[0];
    vp->object.tv = pipeline->tex[1];
    vp->object_normal[0] = pipeline->normal[0];
    vp->object_normal[1] = pipeline->normal[1];
    vp->object_normal[2] = pipeline->normal[2];
    vp->tnl_serial = 0ul;
    vp->tnl_element = 0ul;
}

/*
 * A q other than 1 inside a captured primitive: the kernel does not divide
 * by q, so the capture ends here. What is captured is flushed, and the
 * primitive's kept vertices - the first and the last three, as many as it
 * has - are made again as the CPU path makes them, from their object
 * copies: the packed colour unpacks to the bytes it packs back to, s and
 * t are as given, and a captured vertex's r is not drawn and its q was 1.
 */
static void v9x_gl_prim_tnl_abandon(V9X_GL_STATE *state,
                                    V9X_GL_PIPELINE *pipeline)
{
    V9X_GL_VERTEX *kept[4];
    unsigned int count = 0u;
    unsigned int i;
    v9x_u32 mark = 0ul;

    v9x_gl_prim_flush(pipeline);
    pipeline->tnl_capture = 0;
    ++pipeline->tnl_abandoned;
    if (pipeline->count >= 1ul) {
        kept[count++] = &pipeline->first;
        kept[count++] = &pipeline->ring[(pipeline->ring_head + 2u) & 3u];
    }
    if (pipeline->count >= 2ul) {
        kept[count++] = &pipeline->ring[(pipeline->ring_head + 1u) & 3u];
    }
    if (pipeline->count >= 3ul) {
        kept[count++] = &pipeline->ring[pipeline->ring_head & 3u];
    }
    for (i = 0u; i < count; ++i) {
        V9X_GL_VERTEX *v = kept[i];
        GLfloat object[4];
        GLfloat color[4];
        GLfloat tex[4];

        object[0] = v->object.sx;
        object[1] = v->object.sy;
        object[2] = v->object.sz;
        object[3] = v->object.rhw;
        color[0] = (GLfloat)((v->object.color >> 16) & 0xfful) / 255.0f;
        color[1] = (GLfloat)((v->object.color >> 8) & 0xfful) / 255.0f;
        color[2] = (GLfloat)(v->object.color & 0xfful) / 255.0f;
        color[3] = (GLfloat)((v->object.color >> 24) & 0xfful) / 255.0f;
        tex[0] = v->object.tu;
        tex[1] = v->object.tv;
        tex[2] = 0.0f;
        tex[3] = 1.0f;
        v9x_gl_prim_cpu_vertex(state, pipeline, v, object, color, tex,
                               pipeline->tex1, &mark);
    }
}

static void v9x_gl_prim_assemble(V9X_GL_STATE *state,
                                 V9X_GL_PIPELINE *pipeline, v9x_u32 *mark);

void v9x_gl_prim_vertex(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                        GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    V9X_GL_VERTEX *vp;
    GLfloat object[4];
    v9x_u32 mark = 0ul;

    if (!state->in_begin) {
        return;
    }
    /*
     * The last three vertices and this one live in a four-slot ring
     * (V9X_GL_PIPELINE.ring): previous[k] is slot head + k and this vertex
     * is built in slot head + 3, which the shift in v9x_gl_prim_assemble
     * makes previous[2]. The plain form copied three ~100-byte vertices
     * per call to do that.
     */
    vp = &pipeline->ring[(pipeline->ring_head + 3u) & 3u];
    if (pipeline->profile != 0) {
        mark = v9x_gl_prim_rdtsc_low();
    }
    if (pipeline->tnl_capture && pipeline->tex[3] != 1.0f) {
        v9x_gl_prim_tnl_abandon(state, pipeline);
    }
    if (pipeline->tnl_capture) {
        v9x_gl_prim_tnl_vertex(pipeline, vp, x, y, z, w);
        if (pipeline->tnl_element_next != 0ul &&
            pipeline->tnl_elements != 0) {
            V9X_GL_TNL_ELEMENT *entry =
                &pipeline->tnl_elements[(pipeline->tnl_element_next - 1ul) &
                                        (V9X_GL_TNL_ELEMENTS - 1u)];

            entry->element = pipeline->tnl_element_next;
            entry->call = pipeline->tnl_call;
            entry->serial = 0ul;
            entry->index = 0ul;
            entry->clip[0] = vp->clip[0];
            entry->clip[1] = vp->clip[1];
            entry->clip[2] = vp->clip[2];
            entry->clip[3] = vp->clip[3];
            entry->outcode = vp->outcode;
            entry->object = vp->object;
            entry->normal[0] = vp->object_normal[0];
            entry->normal[1] = vp->object_normal[1];
            entry->normal[2] = vp->object_normal[2];
            vp->tnl_element = pipeline->tnl_element_next;
        }
    } else {
        object[0] = x;
        object[1] = y;
        object[2] = z;
        object[3] = w;
        v9x_gl_prim_cpu_vertex(state, pipeline, vp, object, pipeline->color,
                               pipeline->tex, pipeline->tex1, &mark);
    }
    pipeline->tnl_element_next = 0ul;
    v9x_gl_prim_assemble(state, pipeline, &mark);
}

void v9x_gl_prim_tnl_call(V9X_GL_PIPELINE *pipeline)
{
    ++pipeline->tnl_call;
}

int v9x_gl_prim_element(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                        v9x_u32 element)
{
    const V9X_GL_TNL_ELEMENT *entry;
    V9X_GL_VERTEX *vp;
    v9x_u32 mark = 0ul;

    pipeline->tnl_element_next = 0ul;
    if (!state->in_begin || !pipeline->tnl_capture ||
        pipeline->tnl_elements == 0) {
        return 0;
    }
    entry = &pipeline->tnl_elements[element & (V9X_GL_TNL_ELEMENTS - 1u)];
    if (entry->element != element + 1ul ||
        entry->call != pipeline->tnl_call) {
        pipeline->tnl_element_next = element + 1ul;
        return 0;
    }
    vp = &pipeline->ring[(pipeline->ring_head + 3u) & 3u];
    vp->clip[0] = entry->clip[0];
    vp->clip[1] = entry->clip[1];
    vp->clip[2] = entry->clip[2];
    vp->clip[3] = entry->clip[3];
    vp->outcode = entry->outcode;
    vp->object = entry->object;
    vp->object_normal[0] = entry->normal[0];
    vp->object_normal[1] = entry->normal[1];
    vp->object_normal[2] = entry->normal[2];
    vp->tnl_serial = entry->serial;
    vp->tnl_index = entry->index;
    vp->tnl_element = element + 1ul;
    ++pipeline->tnl_cached;
    v9x_gl_prim_assemble(state, pipeline, &mark);
    return 1;
}

/* The vertex just built in the ring's fourth slot into the primitive: the
 * triangles it completes, then the ring's shift. */
static void v9x_gl_prim_assemble(V9X_GL_STATE *state,
                                 V9X_GL_PIPELINE *pipeline, v9x_u32 *mark)
{
    V9X_GL_VERTEX *p0 = &pipeline->ring[pipeline->ring_head & 3u];
    V9X_GL_VERTEX *p1 = &pipeline->ring[(pipeline->ring_head + 1u) & 3u];
    V9X_GL_VERTEX *p2 = &pipeline->ring[(pipeline->ring_head + 2u) & 3u];
    V9X_GL_VERTEX *vp = &pipeline->ring[(pipeline->ring_head + 3u) & 3u];
    v9x_u32 n;

    n = pipeline->count++;
    switch (pipeline->mode) {
    case V9X_GL_TRIANGLES:
        if (n % 3ul == 2ul) {
            v9x_gl_prim_triangle(state, pipeline, p1, p2, vp, vp);
        }
        break;
    case V9X_GL_TRIANGLE_STRIP:
        /* Triangle i is (i, i+1, i+2) for even i and (i+1, i, i+2) for odd
         * i, so every triangle of the strip faces the same way (2.6.1). */
        if (n >= 2ul) {
            if ((n & 1ul) == 0ul) {
                v9x_gl_prim_triangle(state, pipeline, p1, p2, vp, vp);
            } else {
                v9x_gl_prim_triangle(state, pipeline, p2, p1, vp, vp);
            }
        }
        break;
    case V9X_GL_TRIANGLE_FAN:
        if (n >= 2ul) {
            v9x_gl_prim_triangle(state, pipeline, &pipeline->first,
                                 p2, vp, vp);
        }
        break;
    case V9X_GL_QUADS:
        /* (v0, v1, v2, v3) as (v0, v1, v2) and (v0, v2, v3), flat-shaded
         * from v3. */
        if (n % 4ul == 3ul) {
            v9x_gl_prim_triangle(state, pipeline, p0, p1, p2, vp);
            v9x_gl_prim_triangle(state, pipeline, p0, p2, vp, vp);
        }
        break;
    case V9X_GL_QUAD_STRIP:
        /* Quad i is (v2i, v2i+1, v2i+3, v2i+2), flat-shaded from v2i+3. */
        if (n >= 3ul && (n & 1ul) == 1ul) {
            v9x_gl_prim_triangle(state, pipeline, p1, p2, vp, vp);
            v9x_gl_prim_triangle(state, pipeline, p1, vp, p2, vp);
        }
        break;
    case V9X_GL_POLYGON:
        /* A fan from the first vertex, flat-shaded from the first. */
        if (n >= 2ul) {
            v9x_gl_prim_triangle(state, pipeline, &pipeline->first,
                                 p2, vp,
                                 &pipeline->first);
        }
        break;
    default:
        /* POINTS and the LINES family: accepted, not drawn yet
         * (r3d_line.c's coverage comes with the plan's Phase 4 lines). */
        break;
    }
    v9x_gl_prim_prof_mark(pipeline, V9X_GL_PRIM_PROF_ASSEMBLE, mark);

    if (n == 0ul) {
        pipeline->first = *vp;
    }
    pipeline->ring_head = (pipeline->ring_head + 1u) & 3u;
    v9x_gl_prim_prof_mark(pipeline, V9X_GL_PRIM_PROF_HISTORY, mark);
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
    out->src_blend = v9x_gl_prim_no_dest_alpha(
        v9x_gl_prim_factor(pipeline->blend_src, 1));
    out->dst_blend = v9x_gl_prim_no_dest_alpha(
        v9x_gl_prim_factor(pipeline->blend_dst, 0));
    out->alpha_test_enable =
        v9x_gl_state_cap(state, V9X_GL_ALPHA_TEST) ? 1ul : 0ul;
    out->alpha_func = (v9x_u32)(pipeline->alpha_func - V9X_GL_NEVER) + 1ul;
    out->alpha_ref = v9x_gl_prim_byte(pipeline->alpha_ref);
    /* The vertices carry the factor (v9x_gl_prim_emit); the colour has no
     * alpha here, as fog leaves the fragment's alpha alone (3.9). */
    out->fog_enable = v9x_gl_state_cap(state, V9X_GL_FOG) ? 1ul : 0ul;
    out->fog_color = out->fog_enable == 0ul ? 0ul :
                     (v9x_gl_prim_byte(pipeline->fog_color[0]) << 16) |
                     (v9x_gl_prim_byte(pipeline->fog_color[1]) << 8) |
                     v9x_gl_prim_byte(pipeline->fog_color[2]);
    out->write_mask = (state->color_mask[0] ? V9X_R3D_ABI_WRITE_RED : 0ul) |
                      (state->color_mask[1] ? V9X_R3D_ABI_WRITE_GREEN : 0ul) |
                      (state->color_mask[2] ? V9X_R3D_ABI_WRITE_BLUE : 0ul);
    /* The whole drawable: the scissor box has already cut the geometry
     * (v9x_gl_prim_draw_rect), so the engine is not asked to test it. */
    out->scissor_left = 0ul;
    out->scissor_top = 0ul;
    out->scissor_right = state->drawable_width;
    out->scissor_bottom = state->drawable_height;
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

int v9x_gl_prim_same_draw(const V9X_R3D_ABI_TEXTURE *texture_a,
                          const V9X_R3D_ABI_STATE *state_a,
                          const V9X_R3D_ABI_TEXTURE *texture_b,
                          const V9X_R3D_ABI_STATE *state_b)
{
    const v9x_u8 *a = (const v9x_u8 *)state_a;
    const v9x_u8 *b = (const v9x_u8 *)state_b;
    const void *pixels_a;
    const void *pixels_b;
    unsigned int i;

    /* The fragment state is sixteen words with no padding. */
    for (i = 0u; i < sizeof(*state_a); ++i) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    if (texture_a->storage != texture_b->storage) {
        return 0;
    }
    if (texture_a->storage == V9X_R3D_ABI_TEXTURE_NONE) {
        return 1;
    }
    pixels_a = texture_a->levels != 0 && texture_a->level_count != 0ul
        ? texture_a->levels[0].pixels : 0;
    pixels_b = texture_b->levels != 0 && texture_b->level_count != 0ul
        ? texture_b->levels[0].pixels : 0;
    return texture_a->format == texture_b->format &&
           texture_a->surface.surface == texture_b->surface.surface &&
           texture_a->level_count == texture_b->level_count &&
           pixels_a == pixels_b &&
           texture_a->min_filter == texture_b->min_filter &&
           texture_a->mag_filter == texture_b->mag_filter &&
           texture_a->mip == texture_b->mip &&
           texture_a->address == texture_b->address &&
           texture_a->color_op == texture_b->color_op &&
           texture_a->alpha_op == texture_b->alpha_op &&
           texture_a->env_color == texture_b->env_color;
}

/* The interface's numbering (D3D's): SRCALPHA 5, INVSRCALPHA 6 and
 * SRCALPHASAT 11 read the source alpha. The destination ones read the
 * absent alpha plane, one, whatever the source. */
static int v9x_gl_prim_abi_factor_reads_alpha(v9x_u32 factor)
{
    return factor == 5ul || factor == 6ul || factor == 11ul;
}

int v9x_gl_prim_blend_reads_alpha(const V9X_R3D_ABI_STATE *state)
{
    return state->blend_enable != 0ul &&
           (v9x_gl_prim_abi_factor_reads_alpha(state->src_blend) ||
            v9x_gl_prim_abi_factor_reads_alpha(state->dst_blend));
}

/* The alpha test's comparison, GL's order and the interface's (1 NEVER
 * to 8 ALWAYS). */
static int v9x_gl_prim_alpha_compare(v9x_u32 func, v9x_u32 alpha,
                                     v9x_u32 reference)
{
    switch (func) {
    case 2ul:
        return alpha < reference;
    case 3ul:
        return alpha == reference;
    case 4ul:
        return alpha <= reference;
    case 5ul:
        return alpha > reference;
    case 6ul:
        return alpha != reference;
    case 7ul:
        return alpha >= reference;
    case 8ul:
        return 1;
    default:
        return 0;
    }
}

int v9x_gl_prim_alpha_test_passes(const V9X_R3D_ABI_STATE *state,
                                  const V9X_R3D_ABI_TEXTURE *texture,
                                  const V9X_R3D_ABI_VERTEX *vertices,
                                  v9x_u32 vertex_count)
{
    v9x_u32 low = 255ul;
    v9x_u32 high = 0ul;
    v9x_u32 i;

    if (state->alpha_test_enable == 0ul || vertex_count == 0ul) {
        return 0;
    }
    if (texture->storage != V9X_R3D_ABI_TEXTURE_NONE) {
        if (texture->format != V9X_R3D_ABI_FORMAT_RGB565) {
            return 0;
        }
        /* No texel alpha: REPLACE gives one, the rest the fragment's. */
        if (texture->alpha_op == V9X_R3D_ABI_ALPHAOP_REPLACE) {
            return v9x_gl_prim_alpha_compare(state->alpha_func, 255ul,
                                             state->alpha_ref);
        }
    }
    for (i = 0ul; i < vertex_count; ++i) {
        v9x_u32 alpha = vertices[i].color >> 24;

        if (alpha < low) {
            low = alpha;
        }
        if (alpha > high) {
            high = alpha;
        }
    }
    if (low == high) {
        return v9x_gl_prim_alpha_compare(state->alpha_func, low,
                                         state->alpha_ref);
    }
    if (state->alpha_func == 3ul || state->alpha_func == 6ul) {
        return 0;
    }
    /* The ordered tests hold between two alphas that pass, and a step
     * either side covers the interpolator's rounding. */
    return v9x_gl_prim_alpha_compare(state->alpha_func,
                                     low > 0ul ? low - 1ul : low,
                                     state->alpha_ref) &&
           v9x_gl_prim_alpha_compare(state->alpha_func,
                                     high < 255ul ? high + 1ul : high,
                                     state->alpha_ref);
}

/* The interface's (D3D's) numbers the split's passes use. */
#define V9X_GL_SPLIT_CMP_EQUAL       3ul
#define V9X_GL_SPLIT_BLEND_ZERO      1ul
#define V9X_GL_SPLIT_BLEND_ONE       2ul
#define V9X_GL_SPLIT_BLEND_INVSRCCOLOR 4ul
#define V9X_GL_SPLIT_BLEND_SRCALPHA  5ul
#define V9X_GL_SPLIT_BLEND_INVSRCALPHA 6ul
#define V9X_GL_SPLIT_BLEND_DESTCOLOR 9ul

/* One unit-1 pass: unit 1's texel, blended as given, over pass 0's
 * pixels only. */
static void v9x_gl_prim_split_unit1(const V9X_R3D_ABI_STATE *state,
                                    V9X_GL_SPLIT_PASS *pass,
                                    v9x_u32 color_op, v9x_u32 src_blend,
                                    v9x_u32 dst_blend)
{
    pass->unit = 1ul;
    pass->color_op = color_op;
    /* The interface's DECAL: the texel, its alpha with it, which the
     * DECALALPHA pass's blend reads and no other pass does. */
    pass->alpha_op = V9X_R3D_ABI_ALPHAOP_REPLACE;
    pass->env_colour = 0ul;
    pass->state = *state;
    pass->state.blend_enable =
        src_blend == V9X_GL_SPLIT_BLEND_ONE &&
        dst_blend == V9X_GL_SPLIT_BLEND_ZERO ? 0ul : 1ul;
    pass->state.src_blend = src_blend;
    pass->state.dst_blend = dst_blend;
    /* Pass 0 alone tests alpha; with depth writes what it kept is what
     * holds its depth, so EQUAL finds exactly those pixels. Without them
     * the buffer is as pass 0 found it, and its own test finds them. */
    pass->state.alpha_test_enable = 0ul;
    if (state->depth_enable != 0ul && state->depth_write != 0ul) {
        pass->state.depth_func = V9X_GL_SPLIT_CMP_EQUAL;
    }
    pass->state.depth_write = 0ul;
}

v9x_u32 v9x_gl_prim_split(const V9X_R3D_ABI_STATE *state,
                          const V9X_R3D_ABI_TEXTURE *texture1,
                          V9X_GL_SPLIT_PASS *passes)
{
    if (state == 0 || texture1 == 0 || passes == 0 ||
        texture1->storage == V9X_R3D_ABI_TEXTURE_NONE) {
        return 0ul;
    }
    /* The passes combine through the blender, so the application's own
     * blend cannot also be had. */
    if (state->blend_enable != 0ul) {
        return 0ul;
    }
    /* Fog applies to the units' combined colour, which no pass holds. */
    if (state->fog_enable != 0ul) {
        return 0ul;
    }
    /* A test that may discard leaves the later passes nothing to find
     * its survivors by but their depth, and it must test the alpha the
     * two units would have left: unit 1 must keep the fragment's. */
    if (state->alpha_test_enable != 0ul &&
        (state->depth_enable == 0ul || state->depth_write == 0ul ||
         texture1->alpha_op != V9X_R3D_ABI_ALPHAOP_FRAGMENT)) {
        return 0ul;
    }

    passes[0].unit = 0ul;
    passes[0].color_op = 0ul;
    passes[0].alpha_op = 0ul;
    passes[0].env_colour = 0ul;
    passes[0].state = *state;

    switch (texture1->color_op) {
    case V9X_R3D_ABI_COLOROP_MODULATE:
        v9x_gl_prim_split_unit1(state, &passes[1],
                                V9X_R3D_ABI_COLOROP_REPLACE,
                                V9X_GL_SPLIT_BLEND_DESTCOLOR,
                                V9X_GL_SPLIT_BLEND_ZERO);
        return 2ul;
    case V9X_R3D_ABI_COLOROP_REPLACE:
        v9x_gl_prim_split_unit1(state, &passes[1],
                                V9X_R3D_ABI_COLOROP_REPLACE,
                                V9X_GL_SPLIT_BLEND_ONE,
                                V9X_GL_SPLIT_BLEND_ZERO);
        return 2ul;
    case V9X_R3D_ABI_COLOROP_DECALALPHA:
        v9x_gl_prim_split_unit1(state, &passes[1],
                                V9X_R3D_ABI_COLOROP_REPLACE,
                                V9X_GL_SPLIT_BLEND_SRCALPHA,
                                V9X_GL_SPLIT_BLEND_INVSRCALPHA);
        return 2ul;
    case V9X_R3D_ABI_COLOROP_BLEND:
        v9x_gl_prim_split_unit1(state, &passes[1],
                                V9X_R3D_ABI_COLOROP_REPLACE,
                                V9X_GL_SPLIT_BLEND_ZERO,
                                V9X_GL_SPLIT_BLEND_INVSRCCOLOR);
        /* GL's default environment colour is black, and then Cc Ct adds
         * nothing: QuakeWorld's lightmaps are unit 1 BLEND with the colour
         * never set (id's QW gl_rsurf.c, R_DrawSequentialPoly). */
        if ((texture1->env_color & 0x00FFFFFFul) == 0ul) {
            return 2ul;
        }
        /* Cc Ct: the texel modulated by a vertex colour that is the
         * environment colour, added. */
        v9x_gl_prim_split_unit1(state, &passes[2],
                                V9X_R3D_ABI_COLOROP_MODULATE,
                                V9X_GL_SPLIT_BLEND_ONE,
                                V9X_GL_SPLIT_BLEND_ONE);
        passes[2].env_colour = 1ul;
        return 3ul;
    default:
        return 0ul;
    }
}

/* ---- Hardware T&L: what a captured batch is drawn under ------------- */

void v9x_gl_prim_tnl_constants(const V9X_GL_STATE *state,
                               const V9X_GL_PIPELINE *pipeline,
                               V9X_R3D_TNL_KEY *key,
                               V9X_R3D_TNL_CONSTANTS *constants)
{
    const V9X_GL_MATRIX *mv;
    const V9X_GL_MATRIX *p;
    unsigned char *bytes;
    unsigned int i;
    unsigned int row;
    unsigned int column;
    unsigned int unit;

    bytes = (unsigned char *)key;
    for (i = 0u; i < sizeof(*key); ++i) {
        bytes[i] = 0u;
    }
    bytes = (unsigned char *)constants;
    for (i = 0u; i < sizeof(*constants); ++i) {
        bytes[i] = 0u;
    }
    /* Unlit, unfogged, both colours in every vertex (step 3). */
    key->model = V9X_R3D_TNL_MODEL_GL11;
    key->inputs = V9X_R3D_TNL_IN_COLOR0 | V9X_R3D_TNL_IN_COLOR1;
    key->texture_count = pipeline->units;

    /* GL matrices are column-major: row r, column k is m[k * 4 + r]. The
     * constant block holds rows (r3d_tnl.h). */
    mv = v9x_gl_matrix_top(&state->matrices, V9X_GL_MODELVIEW);
    p = v9x_gl_matrix_top(&state->matrices, V9X_GL_PROJECTION);
    for (row = 0u; row < 4u; ++row) {
        for (column = 0u; column < 4u; ++column) {
            GLfloat sum = 0.0f;

            for (i = 0u; i < 4u; ++i) {
                sum += p->m[i * 4u + row] * mv->m[column * 4u + i];
            }
            constants->model_view_projection[row][column] = sum;
            constants->model_view[row][column] = mv->m[column * 4u + row];
        }
    }
    /* OpenGL keeps -w <= z <= w; the engine clips as Direct3D does,
     * 0 <= z' <= w. z' = (z + w) / 2 maps the one onto the other and
     * ndc z' = (ndc z + 1) / 2 onto [0, 1]. */
    for (column = 0u; column < 4u; ++column) {
        constants->model_view_projection[2][column] =
            0.5f * (constants->model_view_projection[2][column] +
                    constants->model_view_projection[3][column]);
    }
    /* The normal rows, read by no step 3 program: the model-view's upper
     * 3x3 until lighting (step 4) needs its inverse transpose. */
    for (row = 0u; row < 3u; ++row) {
        for (column = 0u; column < 3u; ++column) {
            constants->normal[row][column] = mv->m[column * 4u + row];
        }
    }
    for (unit = 0u; unit < V9X_R3D_TNL_MAX_TEXTURES; ++unit) {
        for (row = 0u; row < 4u; ++row) {
            constants->texture[unit][row][row] = 1.0f;
        }
    }
    constants->viewer[2] = 1.0f;
    for (i = 0u; i < 4u; ++i) {
        constants->default_color[0][i] = 1.0f;
    }

    /* Step 4: fog over |z_e|, the distance the CPU path's fog_c is. A
     * linear fog with start equal to end is one everywhere (3.9's
     * v9x_gl_prim_fog_factor), which no fog gives: the unlit colour 1's
     * alpha is the captured specular's, 1. */
    if (pipeline->fog_on) {
        if (pipeline->fog_mode == V9X_GL_LINEAR) {
            if (pipeline->fog_end != pipeline->fog_start) {
                key->fog_mode = V9X_R3D_TNL_FOG_LINEAR;
                constants->fog[3] =
                    1.0f / (pipeline->fog_end - pipeline->fog_start);
            }
        } else {
            key->fog_mode = pipeline->fog_mode == V9X_GL_EXP2
                                ? V9X_R3D_TNL_FOG_EXP2
                                : V9X_R3D_TNL_FOG_EXP;
        }
        key->fog_distance = V9X_R3D_TNL_FOG_DEPTH;
        constants->fog[0] = pipeline->fog_start;
        constants->fog[1] = pipeline->fog_end;
        constants->fog[2] = pipeline->fog_density;
    }

    /* Step 4: lighting, from the context's state or the initial one. */
    if (v9x_gl_state_cap(state, V9X_GL_LIGHTING_CAP)) {
        V9X_GL_LIGHTING initial;
        const V9X_GL_LIGHTING *lighting = pipeline->lighting;

        if (lighting == 0) {
            v9x_gl_lighting_init(&initial);
            lighting = &initial;
        }
        v9x_gl_lighting_tnl(state, lighting, key, constants);
    }
}

void v9x_gl_prim_tnl_viewport(const V9X_GL_STATE *state,
                              const V9X_GL_PIPELINE *pipeline,
                              V9X_R3D_ABI_TNL_DRAW *tnl)
{
    /* 2.10.1's viewport, its bottom edge moved to the target's rows: the
     * CPU path's sy is the drawable's height less the window y. */
    tnl->viewport_x = (GLfloat)state->viewport[0];
    tnl->viewport_y = (GLfloat)state->drawable_height -
                      (GLfloat)(state->viewport[1] + state->viewport[3]);
    tnl->viewport_width = (GLfloat)state->viewport[2];
    tnl->viewport_height = (GLfloat)state->viewport[3];
    tnl->depth_near = (GLfloat)pipeline->depth_near;
    tnl->depth_far = (GLfloat)pipeline->depth_far;
}

v9x_u32 v9x_gl_prim_tnl_cull(const V9X_GL_STATE *state,
                             const V9X_GL_PIPELINE *pipeline)
{
    int front_ccw = pipeline->front_face == V9X_GL_CCW;

    if (!v9x_gl_state_cap(state, V9X_GL_CULL_FACE)) {
        return V9X_R3D_ABI_CULL_NONE;
    }
    /* The winding seen is the same on the target (gl_prim.h). */
    if (pipeline->cull_face == V9X_GL_BACK) {
        return front_ccw ? V9X_R3D_ABI_CULL_CW : V9X_R3D_ABI_CULL_CCW;
    }
    if (pipeline->cull_face == V9X_GL_FRONT) {
        return front_ccw ? V9X_R3D_ABI_CULL_CCW : V9X_R3D_ABI_CULL_CW;
    }
    return V9X_R3D_ABI_CULL_NONE;
}

void v9x_gl_prim_tnl_scissor(const V9X_GL_STATE *state,
                             V9X_R3D_ABI_STATE *out)
{
    GLfloat rect[4];
    long height = (long)state->drawable_height;

    /* The draw rectangle's sides are whole pixels: the drawable's size and
     * the scissor box are integers. An empty one is an empty scissor. */
    if (!v9x_gl_prim_draw_rect(state, rect)) {
        out->scissor_left = 0ul;
        out->scissor_top = 0ul;
        out->scissor_right = 0ul;
        out->scissor_bottom = 0ul;
        return;
    }
    out->scissor_left = (v9x_u32)v9x_gl_prim_to_long(rect[0]);
    out->scissor_right = (v9x_u32)v9x_gl_prim_to_long(rect[2]);
    out->scissor_top = (v9x_u32)(height - v9x_gl_prim_to_long(rect[3]));
    out->scissor_bottom = (v9x_u32)(height - v9x_gl_prim_to_long(rect[1]));
}

/* ---- Hardware T&L: the CPU's draw of a refused batch ---------------- */

/* A corner as the fallback clips it: clip position in the engine's space
 * (z' = (z + w) / 2, kept 0 <= z' <= w), colour channels 0 to 1, s, t. */
typedef struct v9x_gl_tnl_corner {
    GLfloat clip[4];
    GLfloat color[4];
    GLfloat tex[2];
} V9X_GL_TNL_CORNER;

/*
 * The fallback's ten planes, as v9x_gl_prim_plane's are, in the engine's
 * clip space: 0 to 3 the frustum's sides, 4 and 5 z' >= 0 and z' <= w,
 * and 6 to 9 the scissor rectangle carried back through the viewport:
 * x_t >= left is x - a w >= 0 with a = 2 (left - vx) / vw - 1, and as the
 * target's y runs down, y_t >= top is c w - y >= 0 with
 * c = 1 - 2 (top - vy) / vh.
 */
static GLfloat v9x_gl_prim_tnl_plane(const V9X_GL_TNL_CORNER *v,
                                     unsigned int plane, const GLfloat *edge)
{
    GLfloat w = v->clip[3];

    switch (plane) {
    case 0u: return w + v->clip[0];
    case 1u: return w - v->clip[0];
    case 2u: return w + v->clip[1];
    case 3u: return w - v->clip[1];
    case 4u: return v->clip[2];
    case 5u: return w - v->clip[2];
    case 6u: return v->clip[0] - edge[0] * w;
    case 7u: return edge[2] * w - v->clip[0];
    case 8u: return edge[1] * w - v->clip[1];
    default: return v->clip[1] - edge[3] * w;
    }
}

static void v9x_gl_prim_tnl_lerp(V9X_GL_TNL_CORNER *out,
                                 const V9X_GL_TNL_CORNER *a,
                                 const V9X_GL_TNL_CORNER *b, GLfloat t)
{
    unsigned int i;

    for (i = 0u; i < 4u; ++i) {
        out->clip[i] = a->clip[i] + (b->clip[i] - a->clip[i]) * t;
        out->color[i] = a->color[i] + (b->color[i] - a->color[i]) * t;
    }
    out->tex[0] = a->tex[0] + (b->tex[0] - a->tex[0]) * t;
    out->tex[1] = a->tex[1] + (b->tex[1] - a->tex[1]) * t;
}

/* Sutherland-Hodgman over the ten planes, as v9x_gl_prim_clip does it. */
static unsigned int v9x_gl_prim_tnl_clip(V9X_GL_TNL_CORNER *polygon,
                                         unsigned int count,
                                         const GLfloat *edge)
{
    V9X_GL_TNL_CORNER scratch[V9X_GL_PRIM_CLIP_MAX];
    GLfloat distance[V9X_GL_PRIM_CLIP_MAX];
    V9X_GL_TNL_CORNER *in = polygon;
    V9X_GL_TNL_CORNER *out = scratch;
    unsigned int plane;
    unsigned int i;

    for (plane = 0u; plane < V9X_GL_PRIM_CLIP_PLANES && count != 0u;
         ++plane) {
        V9X_GL_TNL_CORNER *swap;
        unsigned int written = 0u;
        int all_inside = 1;

        for (i = 0u; i < count; ++i) {
            distance[i] = v9x_gl_prim_tnl_plane(&in[i], plane, edge);
            if (!(distance[i] >= 0.0f)) {
                all_inside = 0;
            }
        }
        if (all_inside) {
            continue;
        }
        for (i = 0u; i < count; ++i) {
            unsigned int before = i == 0u ? count - 1u : i - 1u;
            GLfloat dc = distance[i];
            GLfloat dp = distance[before];

            if ((dc >= 0.0f) != (dp >= 0.0f) &&
                written < V9X_GL_PRIM_CLIP_MAX) {
                v9x_gl_prim_tnl_lerp(&out[written++], &in[before], &in[i],
                                     dp / (dp - dc));
            }
            if (dc >= 0.0f && written < V9X_GL_PRIM_CLIP_MAX) {
                out[written++] = in[i];
            }
        }
        swap = in;
        in = out;
        out = swap;
        count = written;
    }
    if (in != polygon) {
        for (i = 0u; i < count; ++i) {
            polygon[i] = in[i];
        }
    }
    return count;
}

v9x_u32 v9x_gl_prim_tnl_screen(const V9X_R3D_TNL_CONSTANTS *constants,
                               const V9X_R3D_ABI_TNL_DRAW *frame,
                               const V9X_R3D_ABI_STATE *scissor,
                               const V9X_R3D_ABI_VERTEX *object,
                               const v9x_u16 *indices, v9x_u32 index_count,
                               v9x_u32 *next, V9X_R3D_ABI_VERTEX *out,
                               v9x_u32 room)
{
    V9X_GL_TNL_CORNER polygon[V9X_GL_PRIM_CLIP_MAX];
    V9X_R3D_ABI_VERTEX window[V9X_GL_PRIM_CLIP_MAX];
    GLfloat edge[4];
    GLfloat rect[4];
    GLfloat vx = frame->viewport_x;
    GLfloat vy = frame->viewport_y;
    GLfloat vw = frame->viewport_width;
    GLfloat vh = frame->viewport_height;
    GLfloat z_low = frame->depth_near;
    GLfloat z_high = frame->depth_far;
    v9x_u32 written = 0ul;

    rect[0] = (GLfloat)scissor->scissor_left;
    rect[1] = (GLfloat)scissor->scissor_top;
    rect[2] = (GLfloat)scissor->scissor_right;
    rect[3] = (GLfloat)scissor->scissor_bottom;
    if (!(vw > 0.0f) || !(vh > 0.0f) || !(rect[2] > rect[0]) ||
        !(rect[3] > rect[1])) {
        *next = index_count;
        return 0ul;
    }
    edge[0] = 2.0f * (rect[0] - vx) / vw - 1.0f;
    edge[1] = 1.0f - 2.0f * (rect[1] - vy) / vh;
    edge[2] = 2.0f * (rect[2] - vx) / vw - 1.0f;
    edge[3] = 1.0f - 2.0f * (rect[3] - vy) / vh;
    if (z_low > z_high) {
        z_low = frame->depth_far;
        z_high = frame->depth_near;
    }

    while (*next + 3ul <= index_count &&
           written + V9X_GL_PRIM_TNL_PIECES <= room) {
        unsigned int count;
        unsigned int i;
        unsigned int row;
        GLfloat area = 0.0f;

        for (i = 0u; i < 3u; ++i) {
            const V9X_R3D_ABI_VERTEX *v = &object[indices[*next + i]];
            V9X_GL_TNL_CORNER *c = &polygon[i];

            for (row = 0u; row < 4u; ++row) {
                c->clip[row] =
                    constants->model_view_projection[row][0] * v->sx +
                    constants->model_view_projection[row][1] * v->sy +
                    constants->model_view_projection[row][2] * v->sz +
                    constants->model_view_projection[row][3] * v->rhw;
            }
            c->color[0] = (GLfloat)((v->color >> 16) & 0xfful) / 255.0f;
            c->color[1] = (GLfloat)((v->color >> 8) & 0xfful) / 255.0f;
            c->color[2] = (GLfloat)(v->color & 0xfful) / 255.0f;
            c->color[3] = (GLfloat)((v->color >> 24) & 0xfful) / 255.0f;
            c->tex[0] = v->tu;
            c->tex[1] = v->tv;
        }
        *next += 3ul;

        count = v9x_gl_prim_tnl_clip(polygon, 3u, edge);
        if (count < 3u) {
            continue;
        }
        /* The render interface's viewport mapping (r3d_abi.h), each
         * position clamped to the bounds the clip already keeps it in, as
         * v9x_gl_prim_window clamps the CPU path's. */
        for (i = 0u; i < count; ++i) {
            const V9X_GL_TNL_CORNER *c = &polygon[i];
            GLfloat rhw = 1.0f / c->clip[3];

            window[i].sx = v9x_gl_prim_clamp(
                vx + (c->clip[0] * rhw + 1.0f) * 0.5f * vw, rect[0],
                rect[2]);
            window[i].sy = v9x_gl_prim_clamp(
                vy + (1.0f - c->clip[1] * rhw) * 0.5f * vh, rect[1],
                rect[3]);
            window[i].sz = v9x_gl_prim_clamp(
                frame->depth_near + c->clip[2] * rhw *
                    (frame->depth_far - frame->depth_near),
                z_low, z_high);
            window[i].rhw = rhw;
            window[i].color = v9x_gl_prim_argb(c->color);
            window[i].specular = 0xff000000ul;
            window[i].tu = c->tex[0];
            window[i].tv = c->tex[1];
        }
        /* The cull on the target, y down: a winding clockwise there has a
         * positive signed area. No area is culled either way. */
        for (i = 0u; i < count; ++i) {
            unsigned int j = i + 1u == count ? 0u : i + 1u;

            area += window[i].sx * window[j].sy - window[j].sx * window[i].sy;
        }
        if ((frame->cull == V9X_R3D_ABI_CULL_CW && !(area < 0.0f)) ||
            (frame->cull == V9X_R3D_ABI_CULL_CCW && !(area > 0.0f))) {
            continue;
        }
        for (i = 1u; i + 1u < count; ++i) {
            out[written * 3ul] = window[0];
            out[written * 3ul + 1ul] = window[i];
            out[written * 3ul + 2ul] = window[i + 1u];
            ++written;
        }
    }
    return written;
}
