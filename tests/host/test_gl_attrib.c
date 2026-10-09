/*
 * Tests for the attribute stacks (src\opengl\gl_attrib.c): the depth limits
 * and their errors, that a pop restores the groups its push named and no
 * others, ENABLE_BIT's enables, TEXTURE_BIT's bindings, environments and
 * object parameters (a deleted name included), and the client stack's two
 * groups.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../../src/opengl/gl_attrib.h"

static unsigned int gl_attrib_failures;

#define ACHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++gl_attrib_failures; \
    } \
} while (0)

#define ALL_ATTRIB_BITS 0x000FFFFFu
#define ALL_CLIENT_BITS 0xFFFFFFFFu
#define GL_DEPTH_TEST_CAP 0x0B71u
#define GL_ALPHA_TEST_CAP 0x0BC0u

static void *attrib_alloc(v9x_u32 bytes) { return malloc(bytes); }
static void attrib_free(void *memory) { free(memory); }

static V9X_GL_STATE s;
static V9X_GL_PIPELINE p;
static V9X_GL_TEXTURES t;
static V9X_GL_ARRAYS a;
static V9X_GL_ATTRIB_STACK stack;

static void fresh(void)
{
    v9x_gl_state_init(&s);
    v9x_gl_state_drawable(&s, 320ul, 200ul, V9X_GL_TARGET_RGB565, 1);
    v9x_gl_pipeline_init(&p);
    v9x_gl_textures_init(&t, attrib_alloc, attrib_free);
    v9x_gl_arrays_init(&a);
}

static void test_depth_and_errors(void)
{
    unsigned int i;

    fresh();
    ACHECK(s.attrib_depth == 0ul && s.client_attrib_depth == 0ul);
    for (i = 0u; i < V9X_GL_ATTRIB_DEPTH; ++i) {
        v9x_gl_attrib_push(&s, &p, &t, &stack, 0u);
    }
    ACHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    ACHECK(s.attrib_depth == V9X_GL_ATTRIB_DEPTH);
    v9x_gl_attrib_push(&s, &p, &t, &stack, ALL_ATTRIB_BITS);
    ACHECK(v9x_gl_state_get_error(&s) == V9X_GL_STACK_OVERFLOW);
    ACHECK(s.attrib_depth == V9X_GL_ATTRIB_DEPTH);
    for (i = 0u; i < V9X_GL_ATTRIB_DEPTH; ++i) {
        v9x_gl_attrib_pop(&s, &p, &t, &stack);
    }
    ACHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    ACHECK(s.attrib_depth == 0ul);
    v9x_gl_attrib_pop(&s, &p, &t, &stack);
    ACHECK(v9x_gl_state_get_error(&s) == V9X_GL_STACK_UNDERFLOW);

    /* Inside Begin/End neither moves the stack. */
    s.in_begin = 1;
    v9x_gl_attrib_push(&s, &p, &t, &stack, ALL_ATTRIB_BITS);
    s.in_begin = 0;
    ACHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_OPERATION);
    ACHECK(s.attrib_depth == 0ul);

    /* The client stack, the same limits. */
    for (i = 0u; i < V9X_GL_CLIENT_ATTRIB_DEPTH; ++i) {
        v9x_gl_attrib_push_client(&s, &t, &a, &stack, ALL_CLIENT_BITS);
    }
    ACHECK(s.client_attrib_depth == V9X_GL_CLIENT_ATTRIB_DEPTH);
    v9x_gl_attrib_push_client(&s, &t, &a, &stack, ALL_CLIENT_BITS);
    ACHECK(v9x_gl_state_get_error(&s) == V9X_GL_STACK_OVERFLOW);
    for (i = 0u; i < V9X_GL_CLIENT_ATTRIB_DEPTH; ++i) {
        v9x_gl_attrib_pop_client(&s, &t, &a, &stack);
    }
    ACHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    v9x_gl_attrib_pop_client(&s, &t, &a, &stack);
    ACHECK(v9x_gl_state_get_error(&s) == V9X_GL_STACK_UNDERFLOW);
    ACHECK(s.client_attrib_depth == 0ul);
}

/* COLOR_BUFFER_BIT restores the blend, alpha test, clear colour, masks and
 * draw buffer, with their enables, and leaves the depth group alone. */
static void test_one_group_only(void)
{
    fresh();
    v9x_gl_prim_blend_func(&s, &p, V9X_GL_SRC_ALPHA,
                           V9X_GL_ONE_MINUS_SRC_ALPHA);
    v9x_gl_state_enable(&s, V9X_GL_BLEND, 1);
    v9x_gl_attrib_push(&s, &p, &t, &stack, V9X_GL_COLOR_BUFFER_BIT);

    v9x_gl_prim_blend_func(&s, &p, V9X_GL_ONE, V9X_GL_ONE);
    v9x_gl_state_enable(&s, V9X_GL_BLEND, 0);
    v9x_gl_state_enable(&s, GL_ALPHA_TEST_CAP, 1);
    v9x_gl_prim_alpha_func(&s, &p, V9X_GL_LESS, 0.5f);
    v9x_gl_state_clear_color(&s, 1.0f, 1.0f, 0.0f, 1.0f);
    v9x_gl_state_color_mask(&s, 0u, 1u, 1u, 1u);
    v9x_gl_state_draw_buffer(&s, V9X_GL_FRONT_BUFFER);
    v9x_gl_prim_depth_func(&s, &p, V9X_GL_ALWAYS);
    v9x_gl_state_enable(&s, GL_DEPTH_TEST_CAP, 1);
    v9x_gl_state_depth_mask(&s, 0u);

    v9x_gl_attrib_pop(&s, &p, &t, &stack);
    ACHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    ACHECK(p.blend_src == V9X_GL_SRC_ALPHA &&
           p.blend_dst == V9X_GL_ONE_MINUS_SRC_ALPHA);
    ACHECK(v9x_gl_state_cap(&s, V9X_GL_BLEND) &&
           !v9x_gl_state_cap(&s, GL_ALPHA_TEST_CAP));
    ACHECK(p.alpha_func == V9X_GL_ALWAYS && p.alpha_ref == 0.0f);
    ACHECK(s.clear_color[0] == 0.0f && s.clear_color[1] == 0.0f);
    ACHECK(s.color_mask[0] == 1u);
    ACHECK(s.draw_buffer == V9X_GL_BACK_BUFFER);
    /* The depth group was not pushed. */
    ACHECK(p.depth_func == V9X_GL_ALWAYS);
    ACHECK(v9x_gl_state_cap(&s, GL_DEPTH_TEST_CAP));
    ACHECK(s.depth_mask == 0u);
}

/* ENABLE_BIT restores every enable and no value. */
static void test_enable_bit(void)
{
    fresh();
    v9x_gl_state_enable(&s, V9X_GL_CULL_FACE, 1);
    v9x_gl_attrib_push(&s, &p, &t, &stack, V9X_GL_ENABLE_BIT);
    v9x_gl_state_enable(&s, V9X_GL_CULL_FACE, 0);
    v9x_gl_state_enable(&s, V9X_GL_FOG, 1);
    v9x_gl_state_enable(&s, V9X_GL_DITHER, 0);
    v9x_gl_prim_cull_face(&s, &p, V9X_GL_FRONT);
    v9x_gl_attrib_pop(&s, &p, &t, &stack);
    ACHECK(v9x_gl_state_cap(&s, V9X_GL_CULL_FACE));
    ACHECK(!v9x_gl_state_cap(&s, V9X_GL_FOG));
    ACHECK(v9x_gl_state_cap(&s, V9X_GL_DITHER));
    ACHECK(p.cull_face == V9X_GL_FRONT);
}

/* Everything this ICD keeps comes back from ALL_ATTRIB_BITS. */
static void test_all_attrib_bits(void)
{
    GLfloat fog[4];

    fresh();
    v9x_gl_attrib_push(&s, &p, &t, &stack, ALL_ATTRIB_BITS);
    v9x_gl_prim_color(&p, 0.5f, 0.25f, 0.0f, 1.0f);
    v9x_gl_prim_texcoord(&p, 2.0f, 3.0f, 0.0f, 1.0f);
    v9x_gl_prim_normal(&p, 1.0f, 0.0f, 0.0f);
    v9x_gl_prim_edge_flag(&p, 0u);
    v9x_gl_state_point_size(&s, 4.0f);
    v9x_gl_state_line_width(&s, 3.0f);
    v9x_gl_prim_front_face(&s, &p, V9X_GL_CW);
    v9x_gl_state_polygon_mode(&s, V9X_GL_FRONT_AND_BACK, V9X_GL_LINE);
    v9x_gl_prim_polygon_offset(&s, &p, 1.0f, 2.0f);
    v9x_gl_state_read_buffer(&s, V9X_GL_FRONT_BUFFER);
    v9x_gl_prim_shade_model(&s, &p, V9X_GL_FLAT);
    fog[0] = (GLfloat)V9X_GL_LINEAR;
    v9x_gl_prim_fog(&s, &p, V9X_GL_FOG_MODE, fog, 0);
    fog[0] = 1.0f;
    fog[1] = 1.0f;
    fog[2] = 1.0f;
    fog[3] = 1.0f;
    v9x_gl_prim_fog(&s, &p, V9X_GL_FOG_COLOR, fog, 1);
    v9x_gl_state_clear_depth(&s, 0.5);
    v9x_gl_state_clear_stencil(&s, 7);
    v9x_gl_state_viewport(&s, 10, 20, 30, 40);
    v9x_gl_prim_depth_range(&s, &p, 0.25, 0.75);
    v9x_gl_state_matrix_mode(&s, V9X_GL_PROJECTION);
    v9x_gl_state_hint(&s, 0x0C50u, V9X_GL_NICEST);
    v9x_gl_state_scissor(&s, 1, 2, 3, 4);
    v9x_gl_state_enable(&s, V9X_GL_SCISSOR_TEST, 1);
    v9x_gl_state_clear_index(&s, 3.0f);
    v9x_gl_state_index_mask(&s, 1u);

    v9x_gl_attrib_pop(&s, &p, &t, &stack);
    ACHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    ACHECK(p.color[0] == 1.0f && p.color[1] == 1.0f && p.tex[0] == 0.0f);
    ACHECK(p.normal[0] == 0.0f && p.normal[2] == 1.0f && p.edge_flag == 1u);
    ACHECK(s.point_size == 1.0f && s.line_width == 1.0f);
    ACHECK(p.front_face == V9X_GL_CCW && s.polygon_mode[0] == V9X_GL_FILL &&
           s.polygon_mode[1] == V9X_GL_FILL);
    ACHECK(p.offset_factor == 0.0f && p.offset_units == 0.0f);
    ACHECK(s.read_buffer == V9X_GL_BACK_BUFFER);
    ACHECK(p.shade_model == V9X_GL_SMOOTH);
    ACHECK(p.fog_mode == V9X_GL_EXP && p.fog_color[0] == 0.0f);
    ACHECK(s.clear_depth == 1.0 && s.clear_stencil == 0);
    ACHECK(s.viewport[0] == 0 && s.viewport[2] == 320 &&
           s.viewport[3] == 200);
    ACHECK(p.depth_near == 0.0 && p.depth_far == 1.0);
    ACHECK(s.matrices.mode == V9X_GL_MODELVIEW);
    ACHECK(s.hints[0] == V9X_GL_DONT_CARE);
    ACHECK(s.scissor[2] == 320 &&
           !v9x_gl_state_cap(&s, V9X_GL_SCISSOR_TEST));
    ACHECK(s.clear_index == 0.0f && s.index_mask == 0xFFFFFFFFul);

    /* A pop does not touch the matrices themselves. */
    fresh();
    v9x_gl_attrib_push(&s, &p, &t, &stack, ALL_ATTRIB_BITS);
    v9x_gl_state_translate(&s, 5.0f, 0.0f, 0.0f);
    v9x_gl_attrib_pop(&s, &p, &t, &stack);
    ACHECK(v9x_gl_matrix_top(&s.matrices, V9X_GL_MODELVIEW)->m[12] == 5.0f);
}

/* TEXTURE_BIT: bindings, environments, enables and the bound objects'
 * parameters; a name deleted in between comes back. */
static void test_texture_bit(void)
{
    GLuint names[2];
    GLfloat mode;

    fresh();
    v9x_gl_tex_gen(&s, &t, 2, names);
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, names[0]);
    v9x_gl_tex_parameter(&s, &t, V9X_GL_TEXTURE_2D, V9X_GL_TEXTURE_MIN_FILTER,
                         (GLint)V9X_GL_NEAREST);
    mode = (GLfloat)V9X_GL_REPLACE;
    v9x_gl_tex_env(&s, &t, V9X_GL_TEXTURE_ENV, V9X_GL_TEXTURE_ENV_MODE, &mode);
    v9x_gl_state_enable(&s, V9X_GL_TEXTURE_2D, 1);
    v9x_gl_attrib_push(&s, &p, &t, &stack, V9X_GL_TEXTURE_BIT);

    v9x_gl_tex_parameter(&s, &t, V9X_GL_TEXTURE_2D, V9X_GL_TEXTURE_MIN_FILTER,
                         (GLint)V9X_GL_LINEAR);
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, names[1]);
    mode = (GLfloat)V9X_GL_MODULATE;
    v9x_gl_tex_env(&s, &t, V9X_GL_TEXTURE_ENV, V9X_GL_TEXTURE_ENV_MODE, &mode);
    v9x_gl_state_enable(&s, V9X_GL_TEXTURE_2D, 0);
    v9x_gl_attrib_pop(&s, &p, &t, &stack);

    ACHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    ACHECK(t.units[0].bound == names[0]);
    ACHECK(t.units[0].env_mode == V9X_GL_REPLACE);
    ACHECK(v9x_gl_state_cap(&s, V9X_GL_TEXTURE_2D));
    ACHECK(v9x_gl_tex_bound_object(&t)->min_filter == V9X_GL_NEAREST);

    /* Deleted between push and pop: bound again, an object again, with the
     * parameters it had. */
    v9x_gl_attrib_push(&s, &p, &t, &stack, V9X_GL_TEXTURE_BIT);
    v9x_gl_tex_delete(&s, &t, 1, &names[0]);
    ACHECK(t.units[0].bound == 0u);
    v9x_gl_attrib_pop(&s, &p, &t, &stack);
    ACHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    ACHECK(t.units[0].bound == names[0]);
    ACHECK(v9x_gl_tex_is(&s, &t, names[0]));
    ACHECK(v9x_gl_tex_bound_object(&t)->min_filter == V9X_GL_NEAREST);

    /* Unit 1's enable and the selected unit (GL_SGIS_multitexture). */
    v9x_gl_tex_select(&s, &t, V9X_GL_TEXTURE1_SGIS);
    v9x_gl_tex_enable_selected(&t, V9X_GL_TEXTURE_2D, 1);
    v9x_gl_attrib_push(&s, &p, &t, &stack, V9X_GL_TEXTURE_BIT);
    v9x_gl_tex_enable_selected(&t, V9X_GL_TEXTURE_2D, 0);
    v9x_gl_tex_select(&s, &t, V9X_GL_TEXTURE0_SGIS);
    v9x_gl_attrib_pop(&s, &p, &t, &stack);
    ACHECK(t.active == 1ul && t.units[1].enabled);

    /* Not pushed: the binding stays as changed. */
    v9x_gl_tex_select(&s, &t, V9X_GL_TEXTURE0_SGIS);
    v9x_gl_attrib_push(&s, &p, &t, &stack, V9X_GL_COLOR_BUFFER_BIT);
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, names[1]);
    v9x_gl_attrib_pop(&s, &p, &t, &stack);
    ACHECK(t.units[0].bound == names[1]);
    v9x_gl_textures_release(&t);
}

static void test_client_bits(void)
{
    static const GLfloat vertices[6] = { 0 };

    fresh();
    v9x_gl_attrib_push_client(&s, &t, &a, &stack,
                              V9X_GL_CLIENT_VERTEX_ARRAY_BIT);
    v9x_gl_arrays_pointer(&s, &a, V9X_GL_ARRAY_VERTEX, 2, V9X_GL_FLOAT, 0,
                          vertices);
    v9x_gl_arrays_client_state(&s, &a, V9X_GL_VERTEX_ARRAY, 1);
    v9x_gl_pixel_store(&s, &t, V9X_GL_UNPACK_ALIGNMENT, 1);
    v9x_gl_attrib_pop_client(&s, &t, &a, &stack);
    ACHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    ACHECK(!a.array[V9X_GL_ARRAY_VERTEX].enabled &&
           a.array[V9X_GL_ARRAY_VERTEX].pointer == 0 &&
           a.array[V9X_GL_ARRAY_VERTEX].size == 4);
    /* Pixel store was not pushed. */
    ACHECK(t.unpack_alignment == 1);

    v9x_gl_attrib_push_client(&s, &t, &a, &stack, ALL_CLIENT_BITS);
    v9x_gl_pixel_store(&s, &t, V9X_GL_UNPACK_ALIGNMENT, 8);
    v9x_gl_pixel_store(&s, &t, 0x0D02u, 5);        /* PACK_ROW_LENGTH */
    v9x_gl_arrays_client_state(&s, &a, V9X_GL_COLOR_ARRAY, 1);
    v9x_gl_attrib_pop_client(&s, &t, &a, &stack);
    ACHECK(t.unpack_alignment == 1 && t.pack[2] == 0);
    ACHECK(!a.array[V9X_GL_ARRAY_COLOR].enabled);
    v9x_gl_textures_release(&t);
}

unsigned int v9x_run_gl_attrib_tests(void)
{
    gl_attrib_failures = 0u;
    test_depth_and_errors();
    test_one_group_only();
    test_enable_bit();
    test_all_attrib_bits();
    test_texture_bit();
    test_client_bits();
    if (gl_attrib_failures == 0u) {
        printf("PASS: OpenGL attribute stacks\n");
    }
    return gl_attrib_failures;
}
