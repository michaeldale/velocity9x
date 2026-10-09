/*
 * The attribute stacks (OpenGL 1.1 6.1.11, table 6.30): glPushAttrib,
 * glPopAttrib, glPushClientAttrib and glPopClientAttrib over the state the
 * ICD keeps in V9X_GL_STATE, V9X_GL_PIPELINE, V9X_GL_TEXTURES and
 * V9X_GL_ARRAYS. Pure, like the modules it reads.
 *
 * A push copies every group's state into the record, whatever the mask, and
 * the pop writes back only the groups the mask named. Copying all of it is
 * a few hundred bytes and keeps one code path; restoring by group is what
 * the specification defines.
 *
 * Groups whose state the ICD does not keep yet save nothing beyond their
 * enables: polygon stipple, pixel transfer and zoom, lighting parameters,
 * the accumulation clear value, stencil function and masks, clip plane
 * equations, evaluator state, the display-list base and texture
 * generation. Each joins its group's record when its commands are
 * implemented.
 */
#ifndef VELOCITY9X_GL_ATTRIB_H
#define VELOCITY9X_GL_ATTRIB_H

#include "gl_prim.h"
#include "gl_texture.h"
#include "gl_varray.h"

/* Table 6.20's minimums, which are this implementation's (gl_get.c). */
#define V9X_GL_ATTRIB_DEPTH        16u
#define V9X_GL_CLIENT_ATTRIB_DEPTH 16u

#define V9X_GL_STACK_OVERFLOW      0x0503u
#define V9X_GL_STACK_UNDERFLOW     0x0504u

#define V9X_GL_CLIENT_PIXEL_STORE_BIT  0x00000001u
#define V9X_GL_CLIENT_VERTEX_ARRAY_BIT 0x00000002u

/* One glPushAttrib: the mask, and the state of every group. */
typedef struct v9x_gl_attrib_record {
    GLbitfield mask;
    /* Every capability, restored by group (v9x_gl_state_restore_caps). */
    GLboolean caps[V9X_GL_CAP_COUNT];
    /* CURRENT_BIT */
    GLfloat color[4];
    GLfloat tex[4];
    GLfloat tex1[2];
    GLfloat normal[3];
    GLfloat index;
    GLboolean edge_flag;
    /* POINT_BIT, LINE_BIT */
    GLfloat point_size;
    GLfloat line_width;
    /* POLYGON_BIT */
    GLenum cull_face;
    GLenum front_face;
    GLenum polygon_mode[2];
    GLfloat offset_factor;
    GLfloat offset_units;
    /* PIXEL_MODE_BIT */
    GLenum read_buffer;
    /* LIGHTING_BIT */
    GLenum shade_model;
    /* FOG_BIT */
    GLenum fog_mode;
    GLfloat fog_density;
    GLfloat fog_start;
    GLfloat fog_end;
    GLfloat fog_color[4];
    GLfloat fog_index;
    /* DEPTH_BUFFER_BIT */
    GLenum depth_func;
    GLdouble clear_depth;
    GLboolean depth_mask;
    /* STENCIL_BUFFER_BIT */
    GLint clear_stencil;
    /* VIEWPORT_BIT */
    GLint viewport[4];
    GLdouble depth_near;
    GLdouble depth_far;
    /* TRANSFORM_BIT */
    GLenum matrix_mode;
    /* COLOR_BUFFER_BIT */
    GLenum alpha_func;
    GLfloat alpha_ref;
    GLenum blend_src;
    GLenum blend_dst;
    GLenum draw_buffer;
    GLuint index_mask;
    GLboolean color_mask[4];
    GLfloat clear_color[4];
    GLfloat clear_index;
    /* HINT_BIT */
    GLenum hints[5];
    /* TEXTURE_BIT: each unit's binding, environment and enable, and the
     * bound objects' parameters (v9x_gl_tex_save_units). */
    V9X_GL_TEXUNIT_SAVED units[V9X_GL_TEXTURE_UNITS];
    v9x_u32 active_unit;
    /* SCISSOR_BIT */
    GLint scissor[4];
} V9X_GL_ATTRIB_RECORD;

/* One glPushClientAttrib. */
typedef struct v9x_gl_client_record {
    GLbitfield mask;
    /* CLIENT_PIXEL_STORE_BIT */
    GLint unpack_alignment;
    GLint unpack_row_length;
    GLint unpack_skip_rows;
    GLint unpack_skip_pixels;
    GLint pack[6];
    /* CLIENT_VERTEX_ARRAY_BIT */
    V9X_GL_ARRAYS arrays;
} V9X_GL_CLIENT_RECORD;

typedef struct v9x_gl_attrib_stack {
    V9X_GL_ATTRIB_RECORD server[V9X_GL_ATTRIB_DEPTH];
    V9X_GL_CLIENT_RECORD client[V9X_GL_CLIENT_ATTRIB_DEPTH];
} V9X_GL_ATTRIB_STACK;

/* The stacks' depths are V9X_GL_STATE's attrib_depth and
 * client_attrib_depth, so the queries can read them; both start at 0. The
 * pops read `stack` only below a depth above 0, so a caller that makes the
 * stacks at its first push may pass none until then. */

/* glPushAttrib. STACK_OVERFLOW when full, INVALID_OPERATION inside
 * Begin/End; either way nothing changes. */
void v9x_gl_attrib_push(V9X_GL_STATE *state, const V9X_GL_PIPELINE *pipeline,
                        V9X_GL_TEXTURES *textures,
                        V9X_GL_ATTRIB_STACK *stack, GLbitfield mask);
/* glPopAttrib. STACK_UNDERFLOW when empty, INVALID_OPERATION inside
 * Begin/End. A texture name deleted since the push is bound again, which
 * makes a default object of it as glBindTexture would; OUT_OF_MEMORY if
 * that cannot be made, with the rest restored. */
void v9x_gl_attrib_pop(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                       V9X_GL_TEXTURES *textures, V9X_GL_ATTRIB_STACK *stack);

/* glPushClientAttrib and glPopClientAttrib: STACK_OVERFLOW and
 * STACK_UNDERFLOW. Client state is legal inside Begin/End. */
void v9x_gl_attrib_push_client(V9X_GL_STATE *state,
                               V9X_GL_TEXTURES *textures,
                               const V9X_GL_ARRAYS *arrays,
                               V9X_GL_ATTRIB_STACK *stack, GLbitfield mask);
void v9x_gl_attrib_pop_client(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                              V9X_GL_ARRAYS *arrays,
                              V9X_GL_ATTRIB_STACK *stack);

#endif /* VELOCITY9X_GL_ATTRIB_H */
