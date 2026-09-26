/*
 * The ICD's OpenGL context state (gl_state.h). Pure: no OS header, no C
 * runtime - the ICD links none - so no float-to-integer cast, which Open
 * Watcom lowers to a __CHP call; v9x_gl_round does the conversions.
 */
#include "gl_state.h"

/* Round to nearest; halves go up, which is what the host and the target
 * then agree on whatever the FPU's own tie rule is. The x87 store rounds to
 * nearest-even, and the correction below turns an even-rounded half down
 * into a half up. Arguments here are 0..65535. */
#ifdef __WATCOMC__
static long v9x_gl_to_long(double value);
#pragma aux v9x_gl_to_long = \
    "sub esp,4" \
    "fistp dword ptr [esp]" \
    "pop eax" \
    parm [8087] value [eax] modify exact [eax];
#else
static long v9x_gl_to_long(double value)
{
    return (long)value;     /* truncates; the correction below rounds */
}
#endif

static v9x_u32 v9x_gl_round(double value)
{
    long whole = v9x_gl_to_long(value);
    double difference = value - (double)whole;

    if (difference >= 0.5) {
        ++whole;
    } else if (difference < -0.5) {
        --whole;
    }
    return whole < 0l ? 0ul : (v9x_u32)whole;
}

/*
 * The capabilities glEnable takes, in one table so enable, disable and the
 * query index the same flag. DITHER is the one enabled by default.
 */
static const GLenum v9x_gl_caps[V9X_GL_CAP_COUNT] = {
    0x0BC0u,                                    /* ALPHA_TEST */
    0x0D80u,                                    /* AUTO_NORMAL */
    0x0BE2u,                                    /* BLEND */
    0x3000u, 0x3001u, 0x3002u, 0x3003u, 0x3004u, 0x3005u, /* CLIP_PLANE0-5 */
    0x0BF2u,                                    /* COLOR_LOGIC_OP */
    0x0B57u,                                    /* COLOR_MATERIAL */
    0x0B44u,                                    /* CULL_FACE */
    0x0B71u,                                    /* DEPTH_TEST */
    0x0BD0u,                                    /* DITHER */
    0x0B60u,                                    /* FOG */
    0x0BF1u,                                    /* INDEX_LOGIC_OP */
    0x4000u, 0x4001u, 0x4002u, 0x4003u,         /* LIGHT0-3 */
    0x4004u, 0x4005u, 0x4006u, 0x4007u,         /* LIGHT4-7 */
    0x0B50u,                                    /* LIGHTING */
    0x0B20u,                                    /* LINE_SMOOTH */
    0x0B24u,                                    /* LINE_STIPPLE */
    0x0D90u, 0x0D91u, 0x0D92u, 0x0D93u, 0x0D94u, /* MAP1_* */
    0x0D95u, 0x0D96u, 0x0D97u, 0x0D98u,
    0x0DB0u, 0x0DB1u, 0x0DB2u, 0x0DB3u, 0x0DB4u, /* MAP2_* */
    0x0DB5u, 0x0DB6u, 0x0DB7u, 0x0DB8u,
    0x0BA1u,                                    /* NORMALIZE */
    0x0B10u,                                    /* POINT_SMOOTH */
    0x8037u,                                    /* POLYGON_OFFSET_FILL */
    0x2A02u,                                    /* POLYGON_OFFSET_LINE */
    0x2A01u,                                    /* POLYGON_OFFSET_POINT */
    0x0B41u,                                    /* POLYGON_SMOOTH */
    0x0B42u,                                    /* POLYGON_STIPPLE */
    0x0C11u,                                    /* SCISSOR_TEST */
    0x0B90u,                                    /* STENCIL_TEST */
    0x0DE0u,                                    /* TEXTURE_1D */
    0x0DE1u,                                    /* TEXTURE_2D */
    0x0C63u, 0x0C62u, 0x0C60u, 0x0C61u          /* TEXTURE_GEN_Q,R,S,T */
};

static int v9x_gl_cap_index(GLenum cap)
{
    unsigned int index;

    for (index = 0u; index < V9X_GL_CAP_COUNT; ++index) {
        if (v9x_gl_caps[index] == cap) {
            return (int)index;
        }
    }
    return -1;
}

static GLfloat v9x_gl_clampf(GLfloat value)
{
    if (value < 0.0f) {
        return 0.0f;
    }
    return value > 1.0f ? 1.0f : value;
}

/* Most state commands are INVALID_OPERATION between Begin and End (2.6.3);
 * answers whether the command may go on. */
static int v9x_gl_outside_begin(V9X_GL_STATE *state)
{
    if (state->in_begin) {
        v9x_gl_state_error(state, V9X_GL_INVALID_OPERATION);
        return 0;
    }
    return 1;
}

void v9x_gl_state_init(V9X_GL_STATE *state)
{
    unsigned int index;

    state->error = V9X_GL_NO_ERROR;
    state->in_begin = 0;
    state->drawable_width = 0ul;
    state->drawable_height = 0ul;
    state->target_format = V9X_GL_TARGET_RGB565;
    for (index = 0u; index < 4u; ++index) {
        state->clear_color[index] = 0.0f;
        state->viewport[index] = 0;
        state->scissor[index] = 0;
        state->color_mask[index] = 1;
    }
    state->clear_depth = 1.0;
    state->depth_mask = 1;
    for (index = 0u; index < V9X_GL_CAP_COUNT; ++index) {
        state->caps[index] = v9x_gl_caps[index] == V9X_GL_DITHER ? 1 : 0;
    }
    v9x_gl_matrices_init(&state->matrices);
    for (index = 0u; index < 5u; ++index) {
        state->hints[index] = V9X_GL_DONT_CARE;
    }
    state->polygon_mode[0] = V9X_GL_FILL;
    state->polygon_mode[1] = V9X_GL_FILL;
    /* Double-buffered formats start drawing and reading the back buffer
     * (4.2.1, 4.3.2). */
    state->draw_buffer = V9X_GL_BACK_BUFFER;
    state->read_buffer = V9X_GL_BACK_BUFFER;
    state->line_width = 1.0f;
    state->point_size = 1.0f;
}

void v9x_gl_state_hint(V9X_GL_STATE *state, GLenum target, GLenum mode)
{
    if (!v9x_gl_outside_begin(state)) {
        return;
    }
    if (target < 0x0C50u || target > 0x0C54u ||
        (mode != V9X_GL_DONT_CARE && mode != V9X_GL_FASTEST &&
         mode != V9X_GL_NICEST)) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    state->hints[target - 0x0C50u] = mode;
}

void v9x_gl_state_polygon_mode(V9X_GL_STATE *state, GLenum face,
                               GLenum mode)
{
    if (!v9x_gl_outside_begin(state)) {
        return;
    }
    if ((face != 0x0404u && face != 0x0405u && face != 0x0408u) ||
        (mode != V9X_GL_POINT && mode != V9X_GL_LINE &&
         mode != V9X_GL_FILL)) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    if (face != 0x0405u) {
        state->polygon_mode[0] = mode;
    }
    if (face != 0x0404u) {
        state->polygon_mode[1] = mode;
    }
}

/* The colour buffers of a double-buffered, left-only format, or zero
 * for a name no format has (INVALID_ENUM) and one for a real buffer this
 * format lacks (INVALID_OPERATION). */
static int v9x_gl_buffer_class(GLenum buffer, int draw)
{
    if (buffer == 0x0404u || buffer == 0x0405u || buffer == 0x0406u ||
        buffer == 0x0400u || buffer == 0x0402u) {
        return 2;               /* FRONT, BACK, LEFT, FRONT_LEFT, BACK_LEFT */
    }
    if (draw && (buffer == 0x0408u || buffer == 0u)) {
        return 2;               /* FRONT_AND_BACK, NONE */
    }
    if (buffer == 0x0401u || buffer == 0x0403u || buffer == 0x0407u ||
        (buffer >= 0x0409u && buffer <= 0x040Cu) ||
        (!draw && buffer == 0x0408u)) {
        return 1;               /* RIGHT, AUX0-3: absent here */
    }
    return 0;
}

void v9x_gl_state_draw_buffer(V9X_GL_STATE *state, GLenum buffer)
{
    int kind;

    if (!v9x_gl_outside_begin(state)) {
        return;
    }
    kind = v9x_gl_buffer_class(buffer, 1);
    if (kind != 2) {
        v9x_gl_state_error(state, kind == 0 ? V9X_GL_INVALID_ENUM
                                            : V9X_GL_INVALID_OPERATION);
        return;
    }
    state->draw_buffer = buffer;
}

void v9x_gl_state_read_buffer(V9X_GL_STATE *state, GLenum buffer)
{
    int kind;

    if (!v9x_gl_outside_begin(state)) {
        return;
    }
    kind = v9x_gl_buffer_class(buffer, 0);
    if (kind != 2) {
        v9x_gl_state_error(state, kind == 0 ? V9X_GL_INVALID_ENUM
                                            : V9X_GL_INVALID_OPERATION);
        return;
    }
    state->read_buffer = buffer;
}

void v9x_gl_state_line_width(V9X_GL_STATE *state, GLfloat width)
{
    if (!v9x_gl_outside_begin(state)) {
        return;
    }
    if (!(width > 0.0f)) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return;
    }
    state->line_width = width;
}

void v9x_gl_state_point_size(V9X_GL_STATE *state, GLfloat size)
{
    if (!v9x_gl_outside_begin(state)) {
        return;
    }
    if (!(size > 0.0f)) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return;
    }
    state->point_size = size;
}

void v9x_gl_state_drawable(V9X_GL_STATE *state, v9x_u32 width,
                           v9x_u32 height, v9x_u32 target_format,
                           int first_bind)
{
    state->drawable_width = width;
    state->drawable_height = height;
    state->target_format = target_format;
    if (first_bind) {
        state->viewport[0] = 0;
        state->viewport[1] = 0;
        state->viewport[2] = (GLint)width;
        state->viewport[3] = (GLint)height;
        state->scissor[0] = 0;
        state->scissor[1] = 0;
        state->scissor[2] = (GLint)width;
        state->scissor[3] = (GLint)height;
    }
}

void v9x_gl_state_error(V9X_GL_STATE *state, GLenum error)
{
    if (state->error == V9X_GL_NO_ERROR) {
        state->error = error;
    }
}

GLenum v9x_gl_state_get_error(V9X_GL_STATE *state)
{
    GLenum error = state->error;

    state->error = V9X_GL_NO_ERROR;
    return error;
}

void v9x_gl_state_clear_color(V9X_GL_STATE *state, GLclampf red,
                              GLclampf green, GLclampf blue, GLclampf alpha)
{
    if (!v9x_gl_outside_begin(state)) {
        return;
    }
    state->clear_color[0] = v9x_gl_clampf(red);
    state->clear_color[1] = v9x_gl_clampf(green);
    state->clear_color[2] = v9x_gl_clampf(blue);
    state->clear_color[3] = v9x_gl_clampf(alpha);
}

void v9x_gl_state_clear_depth(V9X_GL_STATE *state, GLclampd depth)
{
    if (!v9x_gl_outside_begin(state)) {
        return;
    }
    if (depth < 0.0) {
        depth = 0.0;
    } else if (depth > 1.0) {
        depth = 1.0;
    }
    state->clear_depth = depth;
}

void v9x_gl_state_viewport(V9X_GL_STATE *state, GLint x, GLint y,
                           GLsizei width, GLsizei height)
{
    if (!v9x_gl_outside_begin(state)) {
        return;
    }
    if (width < 0 || height < 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return;
    }
    state->viewport[0] = x;
    state->viewport[1] = y;
    state->viewport[2] = width;
    state->viewport[3] = height;
}

void v9x_gl_state_scissor(V9X_GL_STATE *state, GLint x, GLint y,
                          GLsizei width, GLsizei height)
{
    if (!v9x_gl_outside_begin(state)) {
        return;
    }
    if (width < 0 || height < 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return;
    }
    state->scissor[0] = x;
    state->scissor[1] = y;
    state->scissor[2] = width;
    state->scissor[3] = height;
}

void v9x_gl_state_color_mask(V9X_GL_STATE *state, GLboolean red,
                             GLboolean green, GLboolean blue,
                             GLboolean alpha)
{
    if (!v9x_gl_outside_begin(state)) {
        return;
    }
    state->color_mask[0] = red != 0 ? 1 : 0;
    state->color_mask[1] = green != 0 ? 1 : 0;
    state->color_mask[2] = blue != 0 ? 1 : 0;
    state->color_mask[3] = alpha != 0 ? 1 : 0;
}

void v9x_gl_state_depth_mask(V9X_GL_STATE *state, GLboolean flag)
{
    if (!v9x_gl_outside_begin(state)) {
        return;
    }
    state->depth_mask = flag != 0 ? 1 : 0;
}

void v9x_gl_state_enable(V9X_GL_STATE *state, GLenum cap, int enable)
{
    int index;

    if (!v9x_gl_outside_begin(state)) {
        return;
    }
    index = v9x_gl_cap_index(cap);
    if (index < 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    state->caps[index] = enable ? 1 : 0;
}

GLboolean v9x_gl_state_cap(const V9X_GL_STATE *state, GLenum cap)
{
    int index = v9x_gl_cap_index(cap);

    return index < 0 ? 0 : state->caps[index];
}

GLboolean v9x_gl_state_is_enabled(V9X_GL_STATE *state, GLenum cap)
{
    int index;

    if (!v9x_gl_outside_begin(state)) {
        return 0;
    }
    index = v9x_gl_cap_index(cap);
    if (index < 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return 0;
    }
    return state->caps[index];
}

/*
 * One channel as the 8-bit value whose top `bits` are round(c * (2^bits -
 * 1)), the low bits filled with its own top bits, so the interface's
 * truncation to the target's field is exact and an 8-bit reader of the
 * value still sees roughly c.
 */
static v9x_u32 v9x_gl_channel(GLfloat value, unsigned int bits)
{
    v9x_u32 maximum = (1ul << bits) - 1ul;
    v9x_u32 field = v9x_gl_round((double)value * (double)maximum);

    if (field > maximum) {
        field = maximum;
    }
    return ((field << (8u - bits)) | (field >> (2u * bits - 8u))) & 0xfful;
}

/* [a0, a1) intersected with [b0, b1), empty when they miss. */
static void v9x_gl_intersect(long *a0, long *a1, long b0, long b1)
{
    if (*a0 < b0) {
        *a0 = b0;
    }
    if (*a1 > b1) {
        *a1 = b1;
    }
    if (*a1 < *a0) {
        *a1 = *a0;
    }
}

int v9x_gl_state_clear(V9X_GL_STATE *state, GLbitfield mask,
                       int has_depth, V9X_GL_CLEAR_PLAN *plan)
{
    const GLbitfield legal = V9X_GL_COLOR_BUFFER_BIT |
                             V9X_GL_DEPTH_BUFFER_BIT |
                             V9X_GL_STENCIL_BUFFER_BIT |
                             V9X_GL_ACCUM_BUFFER_BIT;
    long left = 0l;
    long right = (long)state->drawable_width;
    long bottom = 0l;           /* window coordinates, y up */
    long top = (long)state->drawable_height;
    unsigned int green_bits;

    if (!v9x_gl_outside_begin(state)) {
        return 0;
    }
    if ((mask & ~legal) != 0u) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return 0;
    }

    plan->write_mask = 0ul;
    if (state->color_mask[0]) {
        plan->write_mask |= V9X_GL_CLEAR_RED;
    }
    if (state->color_mask[1]) {
        plan->write_mask |= V9X_GL_CLEAR_GREEN;
    }
    if (state->color_mask[2]) {
        plan->write_mask |= V9X_GL_CLEAR_BLUE;
    }
    /* No pixel format this ICD offers has alpha, stencil or accumulation
     * planes: those bits, and colour with every RGB channel masked, write
     * nothing. */
    plan->clear_color = (mask & V9X_GL_COLOR_BUFFER_BIT) != 0u &&
                        plan->write_mask != 0ul ? 1ul : 0ul;
    plan->clear_depth = (mask & V9X_GL_DEPTH_BUFFER_BIT) != 0u &&
                        has_depth && state->depth_mask ? 1ul : 0ul;
    if (plan->clear_color == 0ul && plan->clear_depth == 0ul) {
        return 0;
    }

    green_bits = state->target_format == V9X_GL_TARGET_RGB565 ? 6u : 5u;
    plan->color_value = (v9x_gl_channel(state->clear_color[0], 5u) << 16) |
                        (v9x_gl_channel(state->clear_color[1], green_bits)
                            << 8) |
                        v9x_gl_channel(state->clear_color[2], 5u);
    plan->depth_value = v9x_gl_round(state->clear_depth * 65535.0);

    /* The scissor box, clipped to the drawable, then flipped to surface
     * rows: window row y is surface row height - 1 - y, so the box's
     * [bottom, top) becomes [height - top, height - bottom). */
    if (v9x_gl_state_is_enabled(state, V9X_GL_SCISSOR_TEST)) {
        long box_left = (long)state->scissor[0];
        long box_right = box_left + (long)state->scissor[2];
        long box_bottom = (long)state->scissor[1];
        long box_top = box_bottom + (long)state->scissor[3];

        v9x_gl_intersect(&box_left, &box_right, left, right);
        v9x_gl_intersect(&box_bottom, &box_top, bottom, top);
        left = box_left;
        right = box_right;
        bottom = box_bottom;
        top = box_top;
    }
    if (left >= right || bottom >= top) {
        return 0;
    }
    plan->rect_left = (v9x_u32)left;
    plan->rect_right = (v9x_u32)right;
    plan->rect_top = state->drawable_height - (v9x_u32)top;
    plan->rect_bottom = state->drawable_height - (v9x_u32)bottom;
    return 1;
}
