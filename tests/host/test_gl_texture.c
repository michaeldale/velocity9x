/*
 * Tests for texture objects and images (src\opengl\gl_texture.c): names and
 * binding, the upload conversions (unpack alignment, each base format to
 * its 16-bit copy), the argument errors, completeness, the environment's
 * combine per base format (GL 1.1 table 3.18), and that deleted storage
 * goes back to the allocator.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../../src/opengl/gl_texture.h"

static unsigned int gl_texture_failures;
static long outstanding;

#define TCHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++gl_texture_failures; \
    } \
} while (0)

static void *test_alloc(v9x_u32 bytes)
{
    void *memory = malloc(bytes);

    if (memory != 0) {
        ++outstanding;
    }
    return memory;
}

static void test_free(void *memory)
{
    if (memory != 0) {
        --outstanding;
        free(memory);
    }
}

static void fresh(V9X_GL_STATE *s, V9X_GL_TEXTURES *t)
{
    v9x_gl_state_init(s);
    v9x_gl_textures_init(t, test_alloc, test_free);
}

static const V9X_GL_TEXOBJ *bound_object(V9X_GL_TEXTURES *t)
{
    GLuint i;

    if (t->bound == 0u) {
        return &t->default_object;
    }
    for (i = 0u; i < t->capacity; ++i) {
        if (t->objects[i].in_use && t->objects[i].name == t->bound) {
            return &t->objects[i];
        }
    }
    return 0;
}

static void test_names_and_binding(void)
{
    V9X_GL_STATE s;
    V9X_GL_TEXTURES t;
    GLuint names[3];

    fresh(&s, &t);
    TCHECK(t.bound == 0u && t.env_mode == V9X_GL_MODULATE &&
           t.unpack_alignment == 4);
    v9x_gl_tex_gen(&s, &t, 3, names);
    TCHECK(names[0] != 0u && names[1] != names[0] && names[2] != names[1]);
    /* Generated but never bound is not yet a texture (3.8.10). */
    TCHECK(v9x_gl_tex_is(&s, &t, names[0]) == 0);
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, names[0]);
    TCHECK(t.bound == names[0] && v9x_gl_tex_is(&s, &t, names[0]) == 1);
    /* A name never generated may be bound, and becomes a texture. */
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, 5000u);
    TCHECK(v9x_gl_tex_is(&s, &t, 5000u) == 1);
    /* Deleting the bound texture binds 0 again. */
    v9x_gl_tex_delete(&s, &t, 1, names + 0);
    v9x_gl_tex_delete(&s, &t, 1, names + 0);     /* twice is harmless */
    TCHECK(t.bound == 5000u);
    v9x_gl_tex_delete(&s, &t, 1, (const GLuint *)"\x88\x13\0\0");
    TCHECK(t.bound == 0u);
    TCHECK(v9x_gl_tex_is(&s, &t, 0u) == 0);
    v9x_gl_tex_bind(&s, &t, 0x0DE0u, 1u);        /* TEXTURE_1D: not yet */
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_tex_gen(&s, &t, -1, names);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    v9x_gl_textures_release(&t);
    TCHECK(outstanding == 0l);
}

static void test_uploads(void)
{
    V9X_GL_STATE s;
    V9X_GL_TEXTURES t;
    const V9X_GL_TEXOBJ *o;
    /* 2x2 RGB with the default unpack alignment of 4: each 6-byte row is
     * padded to 8. Red, green / blue, white. */
    static const GLubyte rgb[16] = {
        255, 0, 0,  0, 255, 0,  99, 99,
        0, 0, 255,  255, 255, 255,  99, 99
    };
    static const GLubyte rgba[16] = {
        255, 0, 0, 255,  0, 255, 0, 136,  0, 0, 255, 0,  255, 255, 255, 17
    };
    static const GLubyte alpha[4] = { 0, 85, 170, 255 };
    static const GLubyte luminance[4] = { 0, 255, 255, 0 };

    fresh(&s, &t);
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 3, 2, 2, 0,
                        V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, rgb);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    o = bound_object(&t);
    TCHECK(o->storage_format == V9X_R3D_ABI_FORMAT_RGB565 &&
           o->base_format == V9X_GL_RGB);
    TCHECK(o->levels[0].width == 2ul && o->levels[0].height == 2ul);
    TCHECK(o->levels[0].texels[0] == 0xF800u && o->levels[0].texels[1] == 0x07E0u);
    TCHECK(o->levels[0].texels[2] == 0x001Fu && o->levels[0].texels[3] == 0xFFFFu);

    /* Alignment 1: the same bytes read unpadded give a different second
     * row; the conversion honours it. */
    v9x_gl_pixel_store(&s, &t, V9X_GL_UNPACK_ALIGNMENT, 1);
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, V9X_GL_RGB, 2, 2, 0,
                        V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, rgb);
    o = bound_object(&t);
    TCHECK(o->levels[0].texels[2] == 0x6300u);      /* 99,99,0: 12, 24, 0 */
    v9x_gl_pixel_store(&s, &t, V9X_GL_UNPACK_ALIGNMENT, 3);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    v9x_gl_pixel_store(&s, &t, V9X_GL_UNPACK_ALIGNMENT, 4);

    /* RGBA to ARGB4444, alpha kept: 255 -> F, 136 -> 8, 17 -> 1. */
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 4, 2, 2, 0,
                        V9X_GL_RGBA, V9X_GL_UNSIGNED_BYTE, rgba);
    o = bound_object(&t);
    TCHECK(o->storage_format == V9X_R3D_ABI_FORMAT_ARGB4444);
    TCHECK(o->levels[0].texels[0] == 0xFF00u && o->levels[0].texels[1] == 0x80F0u);
    TCHECK(o->levels[0].texels[2] == 0x000Fu && o->levels[0].texels[3] == 0x1FFFu);

    /* ALPHA: white colour, the alpha in the top nibble. Alignment 4 with a
     * 2-byte row pads to 4, so a 2x2 ALPHA image reads alpha[0], alpha[1]
     * then alpha[4]... use alignment 1 for a packed 2x2. */
    v9x_gl_pixel_store(&s, &t, V9X_GL_UNPACK_ALIGNMENT, 1);
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, V9X_GL_ALPHA, 2, 2, 0,
                        V9X_GL_ALPHA, V9X_GL_UNSIGNED_BYTE, alpha);
    o = bound_object(&t);
    TCHECK(o->base_format == V9X_GL_ALPHA);
    TCHECK(o->levels[0].texels[0] == 0x0FFFu && o->levels[0].texels[3] == 0xFFFFu);
    TCHECK(o->levels[0].texels[1] == 0x5FFFu);
    /* LUMINANCE to grey 565. */
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 1, 2, 2, 0,
                        V9X_GL_LUMINANCE, V9X_GL_UNSIGNED_BYTE, luminance);
    o = bound_object(&t);
    TCHECK(o->base_format == V9X_GL_LUMINANCE &&
           o->storage_format == V9X_R3D_ABI_FORMAT_RGB565);
    TCHECK(o->levels[0].texels[0] == 0x0000u && o->levels[0].texels[1] == 0xFFFFu);

    /* Sub-image into the top-right texel: now red. */
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 3, 2, 2, 0,
                        V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, rgb + 0);
    v9x_gl_tex_sub_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 1, 0, 1, 1,
                            V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, rgb);
    o = bound_object(&t);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    TCHECK(o->levels[0].texels[1] == 0xF800u);
    v9x_gl_tex_sub_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 1, 1, 2, 1,
                            V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, rgb);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    v9x_gl_tex_sub_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 3, 0, 0, 1, 1,
                            V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, rgb);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_OPERATION);

    /* Argument errors, each with no effect. */
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 3, 3, 2, 0,
                        V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, rgb);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 3, 1024, 1, 0,
                        V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, rgb);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 10, 3, 1, 1, 0,
                        V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, rgb);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 5, 2, 2, 0,
                        V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, rgb);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 3, 2, 2, 0,
                        0x1234u, V9X_GL_UNSIGNED_BYTE, rgb);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_tex_image_2d(&s, &t, 0x0DE0u, 0, 3, 2, 2, 0,
                        V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, rgb);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    o = bound_object(&t);
    TCHECK(o->levels[0].texels[1] == 0xF800u);
    v9x_gl_textures_release(&t);
    TCHECK(outstanding == 0l);
}

static void upload_chain(V9X_GL_STATE *s, V9X_GL_TEXTURES *t,
                         unsigned int levels)
{
    static GLubyte white[4 * 8 * 8];
    unsigned int level;
    GLsizei edge = 8;
    unsigned int i;

    for (i = 0u; i < sizeof(white); ++i) {
        white[i] = 255u;
    }
    for (level = 0u; level < levels; ++level) {
        v9x_gl_tex_image_2d(s, t, V9X_GL_TEXTURE_2D, (GLint)level, 4, edge,
                            edge, 0, V9X_GL_RGBA, V9X_GL_UNSIGNED_BYTE,
                            white);
        edge = edge > 1 ? edge / 2 : 1;
    }
}

static void test_completeness_and_describe(void)
{
    V9X_GL_STATE s;
    V9X_GL_TEXTURES t;
    V9X_R3D_ABI_TEXTURE d;
    V9X_R3D_ABI_LEVEL levels[V9X_GL_TEXTURE_LEVELS];

    /* TEXTURE_2D off: none. */
    fresh(&s, &t);
    upload_chain(&s, &t, 1u);
    v9x_gl_tex_describe(&s, &t, &d, levels);
    TCHECK(d.storage == V9X_R3D_ABI_TEXTURE_NONE);
    /* On, but the default minification filter wants mipmaps and there is
     * one level: incomplete, so still none (3.8.9). */
    v9x_gl_state_enable(&s, V9X_GL_TEXTURE_2D, 1);
    v9x_gl_tex_describe(&s, &t, &d, levels);
    TCHECK(d.storage == V9X_R3D_ABI_TEXTURE_NONE);
    /* LINEAR minification: complete with one level. */
    v9x_gl_tex_parameter(&s, &t, V9X_GL_TEXTURE_2D, V9X_GL_TEXTURE_MIN_FILTER,
                         (GLint)V9X_GL_LINEAR);
    v9x_gl_tex_describe(&s, &t, &d, levels);
    TCHECK(d.storage == V9X_R3D_ABI_TEXTURE_CPU && d.level_count == 1ul);
    TCHECK(d.format == V9X_R3D_ABI_FORMAT_ARGB4444);
    TCHECK(d.mip == V9X_R3D_ABI_MIP_NONE &&
           d.min_filter == V9X_R3D_ABI_FILTER_LINEAR &&
           d.mag_filter == V9X_R3D_ABI_FILTER_LINEAR);
    TCHECK(d.address == V9X_R3D_ABI_ADDRESS_WRAP);
    TCHECK(levels[0].width == 8ul && levels[0].pitch == 16ul &&
           levels[0].bytes == 128ul && levels[0].pixels != 0);
    /* The full chain with LINEAR_MIPMAP_NEAREST (GLQuake's): four levels,
     * POINT level selection, linear texels. */
    upload_chain(&s, &t, 4u);
    v9x_gl_tex_parameter(&s, &t, V9X_GL_TEXTURE_2D, V9X_GL_TEXTURE_MIN_FILTER,
                         (GLint)V9X_GL_LINEAR_MIPMAP_NEAREST);
    v9x_gl_tex_parameter(&s, &t, V9X_GL_TEXTURE_2D, V9X_GL_TEXTURE_WRAP_S,
                         (GLint)V9X_GL_CLAMP);
    v9x_gl_tex_describe(&s, &t, &d, levels);
    TCHECK(d.storage == V9X_R3D_ABI_TEXTURE_CPU && d.level_count == 4ul);
    TCHECK(d.mip == V9X_R3D_ABI_MIP_POINT &&
           d.min_filter == V9X_R3D_ABI_FILTER_LINEAR);
    TCHECK(levels[3].width == 1ul && levels[3].bytes == 2ul);
    TCHECK(d.address == V9X_R3D_ABI_ADDRESS_CLAMP);
    v9x_gl_tex_parameter(&s, &t, V9X_GL_TEXTURE_2D, V9X_GL_TEXTURE_MIN_FILTER,
                         (GLint)V9X_GL_NEAREST_MIPMAP_LINEAR);
    v9x_gl_tex_describe(&s, &t, &d, levels);
    TCHECK(d.mip == V9X_R3D_ABI_MIP_LINEAR &&
           d.min_filter == V9X_R3D_ABI_FILTER_NEAREST);
    /* A missing level in the middle: incomplete. */
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 2, 4, 1, 1, 0,
                        V9X_GL_RGBA, V9X_GL_UNSIGNED_BYTE, "abcd");
    v9x_gl_tex_describe(&s, &t, &d, levels);
    TCHECK(d.storage == V9X_R3D_ABI_TEXTURE_NONE);
    /* Parameter errors. */
    v9x_gl_tex_parameter(&s, &t, V9X_GL_TEXTURE_2D, V9X_GL_TEXTURE_MAG_FILTER,
                         (GLint)V9X_GL_LINEAR_MIPMAP_LINEAR);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_tex_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0x1234u, 0);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_textures_release(&t);
    TCHECK(outstanding == 0l);
}

static void expect_env(GLenum mode, GLenum base, v9x_u32 color_op,
                       v9x_u32 alpha_op, unsigned int line)
{
    V9X_GL_STATE s;
    V9X_GL_TEXTURES t;
    V9X_R3D_ABI_TEXTURE d;
    V9X_R3D_ABI_LEVEL levels[V9X_GL_TEXTURE_LEVELS];
    GLfloat value = (GLfloat)mode;
    static const GLubyte texel[4] = { 1, 2, 3, 4 };

    fresh(&s, &t);
    v9x_gl_pixel_store(&s, &t, V9X_GL_UNPACK_ALIGNMENT, 1);
    /* INTENSITY is an internal format only; its data arrives as
     * LUMINANCE. */
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, (GLint)base, 1, 1, 0,
                        base == V9X_GL_INTENSITY ? V9X_GL_LUMINANCE : base,
                        V9X_GL_UNSIGNED_BYTE, texel);
    v9x_gl_tex_parameter(&s, &t, V9X_GL_TEXTURE_2D, V9X_GL_TEXTURE_MIN_FILTER,
                         (GLint)V9X_GL_NEAREST);
    v9x_gl_tex_env(&s, &t, V9X_GL_TEXTURE_ENV, V9X_GL_TEXTURE_ENV_MODE, &value);
    v9x_gl_state_enable(&s, V9X_GL_TEXTURE_2D, 1);
    v9x_gl_tex_describe(&s, &t, &d, levels);
    if (d.color_op != color_op || d.alpha_op != alpha_op ||
        d.storage != V9X_R3D_ABI_TEXTURE_CPU) {
        printf("FAIL %s:%u: env %04X on base %04X gave %lu/%lu\n", __FILE__,
               line, mode, base, d.color_op, d.alpha_op);
        ++gl_texture_failures;
    }
    v9x_gl_textures_release(&t);
}

static void test_environment_table(void)
{
    V9X_GL_STATE s;
    V9X_GL_TEXTURES t;
    V9X_R3D_ABI_TEXTURE d;
    V9X_R3D_ABI_LEVEL levels[V9X_GL_TEXTURE_LEVELS];
    GLfloat colour[4] = { 1.0f, 0.5f, 0.0f, 1.0f };
    GLfloat bad = 1.0f;

    /* GL 1.1 table 3.18, as the interface's two ops. ALPHA textures are
     * stored white, so "colour from the fragment" is MODULATE. */
    expect_env(V9X_GL_MODULATE, V9X_GL_RGB, V9X_R3D_ABI_COLOROP_MODULATE,
               V9X_R3D_ABI_ALPHAOP_FRAGMENT, __LINE__);
    expect_env(V9X_GL_MODULATE, V9X_GL_RGBA, V9X_R3D_ABI_COLOROP_MODULATE,
               V9X_R3D_ABI_ALPHAOP_MODULATE, __LINE__);
    expect_env(V9X_GL_MODULATE, V9X_GL_ALPHA, V9X_R3D_ABI_COLOROP_MODULATE,
               V9X_R3D_ABI_ALPHAOP_MODULATE, __LINE__);
    expect_env(V9X_GL_MODULATE, V9X_GL_LUMINANCE, V9X_R3D_ABI_COLOROP_MODULATE,
               V9X_R3D_ABI_ALPHAOP_FRAGMENT, __LINE__);
    expect_env(V9X_GL_MODULATE, V9X_GL_INTENSITY, V9X_R3D_ABI_COLOROP_MODULATE,
               V9X_R3D_ABI_ALPHAOP_MODULATE, __LINE__);
    expect_env(V9X_GL_REPLACE, V9X_GL_RGB, V9X_R3D_ABI_COLOROP_REPLACE,
               V9X_R3D_ABI_ALPHAOP_FRAGMENT, __LINE__);
    expect_env(V9X_GL_REPLACE, V9X_GL_RGBA, V9X_R3D_ABI_COLOROP_REPLACE,
               V9X_R3D_ABI_ALPHAOP_REPLACE, __LINE__);
    expect_env(V9X_GL_REPLACE, V9X_GL_ALPHA, V9X_R3D_ABI_COLOROP_MODULATE,
               V9X_R3D_ABI_ALPHAOP_REPLACE, __LINE__);
    expect_env(V9X_GL_REPLACE, V9X_GL_LUMINANCE_ALPHA,
               V9X_R3D_ABI_COLOROP_REPLACE, V9X_R3D_ABI_ALPHAOP_REPLACE,
               __LINE__);
    expect_env(V9X_GL_DECAL, V9X_GL_RGB, V9X_R3D_ABI_COLOROP_REPLACE,
               V9X_R3D_ABI_ALPHAOP_FRAGMENT, __LINE__);
    expect_env(V9X_GL_DECAL, V9X_GL_RGBA, V9X_R3D_ABI_COLOROP_DECALALPHA,
               V9X_R3D_ABI_ALPHAOP_FRAGMENT, __LINE__);
    expect_env(V9X_GL_BLEND_ENV, V9X_GL_RGB, V9X_R3D_ABI_COLOROP_BLEND,
               V9X_R3D_ABI_ALPHAOP_FRAGMENT, __LINE__);
    expect_env(V9X_GL_BLEND_ENV, V9X_GL_RGBA, V9X_R3D_ABI_COLOROP_BLEND,
               V9X_R3D_ABI_ALPHAOP_MODULATE, __LINE__);

    /* The environment colour, packed; a bad mode is INVALID_ENUM. */
    fresh(&s, &t);
    v9x_gl_tex_env(&s, &t, V9X_GL_TEXTURE_ENV, V9X_GL_TEXTURE_ENV_COLOR,
                   colour);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    v9x_gl_pixel_store(&s, &t, V9X_GL_UNPACK_ALIGNMENT, 1);
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 3, 1, 1, 0, V9X_GL_RGB,
                        V9X_GL_UNSIGNED_BYTE, "abc");
    v9x_gl_tex_parameter(&s, &t, V9X_GL_TEXTURE_2D, V9X_GL_TEXTURE_MIN_FILTER,
                         (GLint)V9X_GL_NEAREST);
    v9x_gl_state_enable(&s, V9X_GL_TEXTURE_2D, 1);
    v9x_gl_tex_describe(&s, &t, &d, levels);
    TCHECK(d.env_color == 0x00ff8000ul);
    v9x_gl_tex_env(&s, &t, V9X_GL_TEXTURE_ENV, V9X_GL_TEXTURE_ENV_MODE, &bad);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_textures_release(&t);
    TCHECK(outstanding == 0l);
}

unsigned int v9x_run_gl_texture_tests(void)
{
    gl_texture_failures = 0u;
    outstanding = 0l;
    test_names_and_binding();
    test_uploads();
    test_completeness_and_describe();
    test_environment_table();
    if (gl_texture_failures == 0u) {
        printf("PASS: OpenGL texture objects and images\n");
    }
    return gl_texture_failures;
}
