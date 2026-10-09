/*
 * The attribute stacks (gl_attrib.h). A push copies everything; a pop
 * writes back the groups its mask named, straight into the state - each
 * value was legal when it was taken, so the commands' checks are not run
 * again.
 */
#include "gl_attrib.h"

static void v9x_gl_attrib_copy4(GLfloat *to, const GLfloat *from)
{
    to[0] = from[0];
    to[1] = from[1];
    to[2] = from[2];
    to[3] = from[3];
}

static void v9x_gl_attrib_copy4i(GLint *to, const GLint *from)
{
    to[0] = from[0];
    to[1] = from[1];
    to[2] = from[2];
    to[3] = from[3];
}

void v9x_gl_attrib_push(V9X_GL_STATE *state, const V9X_GL_PIPELINE *pipeline,
                        V9X_GL_TEXTURES *textures,
                        V9X_GL_ATTRIB_STACK *stack, GLbitfield mask)
{
    V9X_GL_ATTRIB_RECORD *r;
    unsigned int i;

    if (state->in_begin) {
        v9x_gl_state_error(state, V9X_GL_INVALID_OPERATION);
        return;
    }
    if (state->attrib_depth >= V9X_GL_ATTRIB_DEPTH) {
        v9x_gl_state_error(state, V9X_GL_STACK_OVERFLOW);
        return;
    }
    r = &stack->server[state->attrib_depth];
    r->mask = mask;
    for (i = 0u; i < V9X_GL_CAP_COUNT; ++i) {
        r->caps[i] = state->caps[i];
    }

    v9x_gl_attrib_copy4(r->color, pipeline->color);
    v9x_gl_attrib_copy4(r->tex, pipeline->tex);
    r->tex1[0] = pipeline->tex1[0];
    r->tex1[1] = pipeline->tex1[1];
    r->normal[0] = pipeline->normal[0];
    r->normal[1] = pipeline->normal[1];
    r->normal[2] = pipeline->normal[2];
    r->index = pipeline->index;
    r->edge_flag = pipeline->edge_flag;

    r->point_size = state->point_size;
    r->line_width = state->line_width;

    r->cull_face = pipeline->cull_face;
    r->front_face = pipeline->front_face;
    r->polygon_mode[0] = state->polygon_mode[0];
    r->polygon_mode[1] = state->polygon_mode[1];
    r->offset_factor = pipeline->offset_factor;
    r->offset_units = pipeline->offset_units;

    r->read_buffer = state->read_buffer;
    r->shade_model = pipeline->shade_model;

    r->fog_mode = pipeline->fog_mode;
    r->fog_density = pipeline->fog_density;
    r->fog_start = pipeline->fog_start;
    r->fog_end = pipeline->fog_end;
    v9x_gl_attrib_copy4(r->fog_color, pipeline->fog_color);
    r->fog_index = pipeline->fog_index;

    r->depth_func = pipeline->depth_func;
    r->clear_depth = state->clear_depth;
    r->depth_mask = state->depth_mask;
    r->clear_stencil = state->clear_stencil;

    v9x_gl_attrib_copy4i(r->viewport, state->viewport);
    r->depth_near = pipeline->depth_near;
    r->depth_far = pipeline->depth_far;
    r->matrix_mode = state->matrices.mode;

    r->alpha_func = pipeline->alpha_func;
    r->alpha_ref = pipeline->alpha_ref;
    r->blend_src = pipeline->blend_src;
    r->blend_dst = pipeline->blend_dst;
    r->draw_buffer = state->draw_buffer;
    r->index_mask = state->index_mask;
    for (i = 0u; i < 4u; ++i) {
        r->color_mask[i] = state->color_mask[i];
    }
    v9x_gl_attrib_copy4(r->clear_color, state->clear_color);
    r->clear_index = state->clear_index;

    for (i = 0u; i < 5u; ++i) {
        r->hints[i] = state->hints[i];
    }
    v9x_gl_tex_save_units(textures, r->units, &r->active_unit);
    v9x_gl_attrib_copy4i(r->scissor, state->scissor);
    ++state->attrib_depth;
}

void v9x_gl_attrib_pop(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                       V9X_GL_TEXTURES *textures, V9X_GL_ATTRIB_STACK *stack)
{
    const V9X_GL_ATTRIB_RECORD *r;
    GLbitfield mask;
    unsigned int i;

    if (state->in_begin) {
        v9x_gl_state_error(state, V9X_GL_INVALID_OPERATION);
        return;
    }
    if (state->attrib_depth == 0ul) {
        v9x_gl_state_error(state, V9X_GL_STACK_UNDERFLOW);
        return;
    }
    --state->attrib_depth;
    r = &stack->server[state->attrib_depth];
    mask = r->mask;
    v9x_gl_state_restore_caps(state, r->caps, mask);

    if ((mask & V9X_GL_CURRENT_BIT) != 0u) {
        v9x_gl_attrib_copy4(pipeline->color, r->color);
        v9x_gl_attrib_copy4(pipeline->tex, r->tex);
        pipeline->tex1[0] = r->tex1[0];
        pipeline->tex1[1] = r->tex1[1];
        pipeline->normal[0] = r->normal[0];
        pipeline->normal[1] = r->normal[1];
        pipeline->normal[2] = r->normal[2];
        pipeline->index = r->index;
        pipeline->edge_flag = r->edge_flag;
    }
    if ((mask & V9X_GL_POINT_BIT) != 0u) {
        state->point_size = r->point_size;
    }
    if ((mask & V9X_GL_LINE_BIT) != 0u) {
        state->line_width = r->line_width;
    }
    if ((mask & V9X_GL_POLYGON_BIT) != 0u) {
        pipeline->cull_face = r->cull_face;
        pipeline->front_face = r->front_face;
        state->polygon_mode[0] = r->polygon_mode[0];
        state->polygon_mode[1] = r->polygon_mode[1];
        pipeline->offset_factor = r->offset_factor;
        pipeline->offset_units = r->offset_units;
    }
    if ((mask & V9X_GL_PIXEL_MODE_BIT) != 0u) {
        state->read_buffer = r->read_buffer;
    }
    if ((mask & V9X_GL_LIGHTING_BIT) != 0u) {
        pipeline->shade_model = r->shade_model;
    }
    if ((mask & V9X_GL_FOG_BIT) != 0u) {
        pipeline->fog_mode = r->fog_mode;
        pipeline->fog_density = r->fog_density;
        pipeline->fog_start = r->fog_start;
        pipeline->fog_end = r->fog_end;
        v9x_gl_attrib_copy4(pipeline->fog_color, r->fog_color);
        pipeline->fog_index = r->fog_index;
    }
    if ((mask & V9X_GL_DEPTH_BUFFER_BIT) != 0u) {
        pipeline->depth_func = r->depth_func;
        state->clear_depth = r->clear_depth;
        state->depth_mask = r->depth_mask;
    }
    if ((mask & V9X_GL_STENCIL_BUFFER_BIT) != 0u) {
        state->clear_stencil = r->clear_stencil;
    }
    if ((mask & V9X_GL_VIEWPORT_BIT) != 0u) {
        v9x_gl_attrib_copy4i(state->viewport, r->viewport);
        pipeline->depth_near = r->depth_near;
        pipeline->depth_far = r->depth_far;
    }
    if ((mask & V9X_GL_TRANSFORM_BIT) != 0u) {
        state->matrices.mode = r->matrix_mode;
    }
    if ((mask & V9X_GL_COLOR_BUFFER_BIT) != 0u) {
        pipeline->alpha_func = r->alpha_func;
        pipeline->alpha_ref = r->alpha_ref;
        pipeline->blend_src = r->blend_src;
        pipeline->blend_dst = r->blend_dst;
        state->draw_buffer = r->draw_buffer;
        state->index_mask = r->index_mask;
        for (i = 0u; i < 4u; ++i) {
            state->color_mask[i] = r->color_mask[i];
        }
        v9x_gl_attrib_copy4(state->clear_color, r->clear_color);
        state->clear_index = r->clear_index;
    }
    if ((mask & V9X_GL_HINT_BIT) != 0u) {
        for (i = 0u; i < 5u; ++i) {
            state->hints[i] = r->hints[i];
        }
    }
    if ((mask & V9X_GL_TEXTURE_BIT) != 0u &&
        !v9x_gl_tex_restore_units(textures, r->units, r->active_unit)) {
        v9x_gl_state_error(state, V9X_GL_OUT_OF_MEMORY);
    }
    if ((mask & V9X_GL_SCISSOR_BIT) != 0u) {
        v9x_gl_attrib_copy4i(state->scissor, r->scissor);
    }
}

void v9x_gl_attrib_push_client(V9X_GL_STATE *state,
                               V9X_GL_TEXTURES *textures,
                               const V9X_GL_ARRAYS *arrays,
                               V9X_GL_ATTRIB_STACK *stack, GLbitfield mask)
{
    V9X_GL_CLIENT_RECORD *r;
    unsigned int i;

    if (state->client_attrib_depth >= V9X_GL_CLIENT_ATTRIB_DEPTH) {
        v9x_gl_state_error(state, V9X_GL_STACK_OVERFLOW);
        return;
    }
    r = &stack->client[state->client_attrib_depth];
    r->mask = mask;
    r->unpack_alignment = textures->unpack_alignment;
    r->unpack_row_length = textures->unpack_row_length;
    r->unpack_skip_rows = textures->unpack_skip_rows;
    r->unpack_skip_pixels = textures->unpack_skip_pixels;
    for (i = 0u; i < 6u; ++i) {
        r->pack[i] = textures->pack[i];
    }
    r->arrays = *arrays;
    ++state->client_attrib_depth;
}

void v9x_gl_attrib_pop_client(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                              V9X_GL_ARRAYS *arrays,
                              V9X_GL_ATTRIB_STACK *stack)
{
    const V9X_GL_CLIENT_RECORD *r;
    unsigned int i;

    if (state->client_attrib_depth == 0ul) {
        v9x_gl_state_error(state, V9X_GL_STACK_UNDERFLOW);
        return;
    }
    --state->client_attrib_depth;
    r = &stack->client[state->client_attrib_depth];
    if ((r->mask & V9X_GL_CLIENT_PIXEL_STORE_BIT) != 0u) {
        textures->unpack_alignment = r->unpack_alignment;
        textures->unpack_row_length = r->unpack_row_length;
        textures->unpack_skip_rows = r->unpack_skip_rows;
        textures->unpack_skip_pixels = r->unpack_skip_pixels;
        for (i = 0u; i < 6u; ++i) {
            textures->pack[i] = r->pack[i];
        }
    }
    if ((r->mask & V9X_GL_CLIENT_VERTEX_ARRAY_BIT) != 0u) {
        *arrays = r->arrays;
    }
}
