/*
 * OpenGL 1.1 context state the ICD keeps per context, and the pure
 * operations on it (docs\plans\opengl-1.1-icd.md, Phases 3 and 4).
 *
 * No OS header and no render-interface call: every function here reads and
 * writes a V9X_GL_STATE and, where a GL command produces work, fills a
 * project-owned description of it that the platform file (gl_icd.c) hands
 * to the HAL. So each command's errors, defaults and arithmetic are host
 * tests (tests\host\test_gl_state.c).
 *
 * Errors follow the specification's model (2.5): a command that fails sets
 * the error flag only if it is clear, and has no other effect; glGetError
 * returns the flag and clears it.
 */
#ifndef VELOCITY9X_GL_STATE_H
#define VELOCITY9X_GL_STATE_H

#include "velocity9x/types.h"
#include "velocity9x/gl_types.h"

/* The values this file names, as <GL/gl.h> numbers them; V9X_ so the ICD
 * can include both. */
#define V9X_GL_NO_ERROR           0x0000u
#define V9X_GL_INVALID_ENUM       0x0500u
#define V9X_GL_INVALID_VALUE      0x0501u
#define V9X_GL_INVALID_OPERATION  0x0502u
#define V9X_GL_OUT_OF_MEMORY      0x0505u

#define V9X_GL_DEPTH_BUFFER_BIT   0x00000100u
#define V9X_GL_ACCUM_BUFFER_BIT   0x00000200u
#define V9X_GL_STENCIL_BUFFER_BIT 0x00000400u
#define V9X_GL_COLOR_BUFFER_BIT   0x00004000u

#define V9X_GL_DITHER             0x0BD0u
#define V9X_GL_DEPTH_TEST         0x0B71u
#define V9X_GL_SCISSOR_TEST       0x0C11u

/* The target layouts a context can be made current on, numbered as the
 * render interface numbers them. */
#define V9X_GL_TARGET_RGB565   1ul
#define V9X_GL_TARGET_XRGB1555 2ul

/*
 * Every capability glEnable accepts in OpenGL 1.1 (table 6.x and the
 * command descriptions): 60 of them, one flag each - the client arrays go
 * through glEnableClientState, not here. Kept as an index into
 * a table rather than a bitfield per name, so glIsEnabled and push/pop can
 * treat them uniformly when those arrive.
 */
#define V9X_GL_CAP_COUNT 60u

typedef struct v9x_gl_state {
    GLenum error;
    /* Non-zero between glBegin and glEnd; most commands are then errors. */
    int in_begin;
    /* The drawable the context is current on: its size in pixels and the
     * layout of its colour buffer. Set by the platform file on every
     * DrvSetContext and resize. */
    v9x_u32 drawable_width;
    v9x_u32 drawable_height;
    v9x_u32 target_format;
    GLfloat clear_color[4];
    GLdouble clear_depth;
    GLint viewport[4];
    GLint scissor[4];
    GLboolean color_mask[4];
    GLboolean depth_mask;
    GLboolean caps[V9X_GL_CAP_COUNT];
} V9X_GL_STATE;

/*
 * A clear the HAL can execute: rectangles already in surface rows (top
 * down), colour packed so that the render interface's truncation to the
 * target's bits lands exactly on the GL conversion (2.13.9: round
 * c * (2^m - 1)), and depth as the 16-bit word.
 */
#define V9X_GL_CLEAR_RED   1ul
#define V9X_GL_CLEAR_GREEN 2ul
#define V9X_GL_CLEAR_BLUE  4ul

typedef struct v9x_gl_clear_plan {
    v9x_u32 clear_color;
    v9x_u32 clear_depth;
    v9x_u32 color_value;        /* 0x00RRGGBB */
    v9x_u32 depth_value;        /* 0..65535 */
    v9x_u32 write_mask;         /* V9X_GL_CLEAR_* */
    v9x_u32 rect_left;
    v9x_u32 rect_top;
    v9x_u32 rect_right;
    v9x_u32 rect_bottom;
} V9X_GL_CLEAR_PLAN;

/* The initial state of a new context (2.x and the state tables): clear
 * colour 0,0,0,0, clear depth 1, masks all true, DITHER the only enabled
 * capability, viewport and scissor the drawable once it is known. */
void v9x_gl_state_init(V9X_GL_STATE *state);

/* The drawable changed (first bind, or a resize). The first bind also sets
 * the viewport and scissor boxes to it, as 2.10.1 and 4.1.2 say. */
void v9x_gl_state_drawable(V9X_GL_STATE *state, v9x_u32 width,
                           v9x_u32 height, v9x_u32 target_format,
                           int first_bind);

/* Record an error, keeping an earlier one (2.5). */
void v9x_gl_state_error(V9X_GL_STATE *state, GLenum error);
/* glGetError. */
GLenum v9x_gl_state_get_error(V9X_GL_STATE *state);

void v9x_gl_state_clear_color(V9X_GL_STATE *state, GLclampf red,
                              GLclampf green, GLclampf blue, GLclampf alpha);
void v9x_gl_state_clear_depth(V9X_GL_STATE *state, GLclampd depth);
void v9x_gl_state_viewport(V9X_GL_STATE *state, GLint x, GLint y,
                           GLsizei width, GLsizei height);
void v9x_gl_state_scissor(V9X_GL_STATE *state, GLint x, GLint y,
                          GLsizei width, GLsizei height);
void v9x_gl_state_color_mask(V9X_GL_STATE *state, GLboolean red,
                             GLboolean green, GLboolean blue,
                             GLboolean alpha);
void v9x_gl_state_depth_mask(V9X_GL_STATE *state, GLboolean flag);
void v9x_gl_state_enable(V9X_GL_STATE *state, GLenum cap, int enable);
GLboolean v9x_gl_state_is_enabled(V9X_GL_STATE *state, GLenum cap);

/*
 * glClear(mask): non-zero when there is work, with `plan` describing it;
 * zero when the command was an error (recorded) or has nothing to write.
 * The stencil and accumulation bits are legal and do nothing: no pixel
 * format this ICD offers has either buffer (4.2.3).
 */
int v9x_gl_state_clear(V9X_GL_STATE *state, GLbitfield mask,
                       int has_depth, V9X_GL_CLEAR_PLAN *plan);

#endif /* VELOCITY9X_GL_STATE_H */
