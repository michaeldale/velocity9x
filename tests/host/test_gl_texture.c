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
    GLuint bound = t->units[t->active].bound;
    GLuint i;

    if (bound == 0u) {
        return &t->default_object;
    }
    for (i = 0u; i < t->capacity; ++i) {
        if (t->objects[i].in_use && t->objects[i].name == bound) {
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
    TCHECK(t.units[0].bound == 0u &&
           t.units[0].env_mode == V9X_GL_MODULATE &&
           t.unpack_alignment == 4);
    v9x_gl_tex_gen(&s, &t, 3, names);
    TCHECK(names[0] != 0u && names[1] != names[0] && names[2] != names[1]);
    /* Generated but never bound is not yet a texture (3.8.10). */
    TCHECK(v9x_gl_tex_is(&s, &t, names[0]) == 0);
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, names[0]);
    TCHECK(t.units[0].bound == names[0] &&
           v9x_gl_tex_is(&s, &t, names[0]) == 1);
    /* A name never generated may be bound, and becomes a texture. */
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, 5000u);
    TCHECK(v9x_gl_tex_is(&s, &t, 5000u) == 1);
    /* Deleting the bound texture binds 0 again. */
    v9x_gl_tex_delete(&s, &t, 1, names + 0);
    v9x_gl_tex_delete(&s, &t, 1, names + 0);     /* twice is harmless */
    TCHECK(t.units[0].bound == 5000u);
    v9x_gl_tex_delete(&s, &t, 1, (const GLuint *)"\x88\x13\0\0");
    TCHECK(t.units[0].bound == 0u);
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

/* One complete level of edge x edge on the bound texture, LINEAR-minified
 * so that one level is all it needs. */
static void upload_one(V9X_GL_STATE *s, V9X_GL_TEXTURES *t, GLsizei edge)
{
    static GLubyte grey[4 * 8 * 8];

    v9x_gl_tex_image_2d(s, t, V9X_GL_TEXTURE_2D, 0, 4, edge, edge, 0,
                        V9X_GL_RGBA, V9X_GL_UNSIGNED_BYTE, grey);
    v9x_gl_tex_parameter(s, t, V9X_GL_TEXTURE_2D, V9X_GL_TEXTURE_MIN_FILTER,
                         (GLint)V9X_GL_LINEAR);
}

/* The width describe reports for the bound texture; 0 when it has none. */
static v9x_u32 described_width(V9X_GL_STATE *s, V9X_GL_TEXTURES *t)
{
    V9X_R3D_ABI_TEXTURE d;
    V9X_R3D_ABI_LEVEL levels[V9X_GL_TEXTURE_LEVELS];

    v9x_gl_tex_describe(s, t, &d, levels);
    return d.storage == V9X_R3D_ABI_TEXTURE_CPU ? levels[0].width : 0ul;
}

/*
 * Describe finds the bound object every time it is asked, which a draw
 * does once per polygon. Whatever makes that lookup fast must still find
 * the right object after its slot is freed and reused by another name, and
 * after the table grows and moves.
 */
static void test_lookup_after_reuse_and_growth(void)
{
    V9X_GL_STATE s;
    V9X_GL_TEXTURES t;
    GLuint name;

    fresh(&s, &t);
    v9x_gl_state_enable(&s, V9X_GL_TEXTURE_2D, 1);
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, 10u);
    upload_one(&s, &t, 8);
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, 11u);
    upload_one(&s, &t, 4);
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, 10u);
    TCHECK(described_width(&s, &t) == 8ul);
    TCHECK(described_width(&s, &t) == 8ul);
    /* 10's slot freed, then taken by 12: 12 is found, 10 is not. */
    name = 10u;
    v9x_gl_tex_delete(&s, &t, 1, &name);
    TCHECK(t.units[0].bound == 0u && described_width(&s, &t) == 0ul);
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, 12u);
    upload_one(&s, &t, 2);
    TCHECK(described_width(&s, &t) == 2ul);
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, 10u);
    TCHECK(described_width(&s, &t) == 0ul);
    /* Enough new objects to grow the table more than once (it grows by
     * 32, gl_texture.c). */
    for (name = 100u; name < 200u; ++name) {
        v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, name);
    }
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, 11u);
    TCHECK(described_width(&s, &t) == 4ul);
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, 12u);
    TCHECK(described_width(&s, &t) == 2ul);
    v9x_gl_textures_release(&t);
    TCHECK(outstanding == 0l);
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

/*
 * A texture past the device's largest edge (the Mach64's and the Rage
 * IIC's 256, against the 512 this implementation accepts): its own smaller
 * levels where it has them, else a box-filtered copy kept on the object.
 */
static void test_fit_to_size_max(void)
{
    static GLubyte stripes[4 * 8 * 8];
    V9X_GL_STATE s;
    V9X_GL_TEXTURES t;
    V9X_R3D_ABI_TEXTURE d;
    V9X_R3D_ABI_LEVEL levels[V9X_GL_TEXTURE_LEVELS];
    const void *reduced;
    long held;
    unsigned int i;

    /* A chain: the top level goes, the rest stays complete. */
    fresh(&s, &t);
    v9x_gl_state_enable(&s, V9X_GL_TEXTURE_2D, 1);
    upload_chain(&s, &t, 4u);
    v9x_gl_tex_parameter(&s, &t, V9X_GL_TEXTURE_2D, V9X_GL_TEXTURE_MIN_FILTER,
                         (GLint)V9X_GL_LINEAR_MIPMAP_NEAREST);
    v9x_gl_tex_describe(&s, &t, &d, levels);
    held = outstanding;
    v9x_gl_tex_fit(&t, 0ul, &d, levels, 4ul);
    TCHECK(d.level_count == 3ul && d.levels == levels);
    TCHECK(levels[0].width == 4ul && levels[2].width == 1ul);
    TCHECK(d.mip == V9X_R3D_ABI_MIP_POINT && outstanding == held);
    /* Within the limit, or no limit: untouched. */
    v9x_gl_tex_describe(&s, &t, &d, levels);
    v9x_gl_tex_fit(&t, 0ul, &d, levels, 8ul);
    TCHECK(d.level_count == 4ul && levels[0].width == 8ul);
    v9x_gl_tex_fit(&t, 0ul, &d, levels, 0ul);
    TCHECK(d.level_count == 4ul);
    v9x_gl_textures_release(&t);
    TCHECK(outstanding == 0l);

    /* One level: red in alternate columns, so each 2x2 box averages two
     * texels of 15 and two of 0 to 7 (4444, truncated). */
    for (i = 0u; i < 8u * 8u; ++i) {
        stripes[i * 4u + 0u] = (i & 1u) != 0u ? 255u : 0u;
        stripes[i * 4u + 1u] = 0u;
        stripes[i * 4u + 2u] = 0u;
        stripes[i * 4u + 3u] = 255u;
    }
    fresh(&s, &t);
    v9x_gl_state_enable(&s, V9X_GL_TEXTURE_2D, 1);
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 4, 8, 8, 0,
                        V9X_GL_RGBA, V9X_GL_UNSIGNED_BYTE, stripes);
    v9x_gl_tex_parameter(&s, &t, V9X_GL_TEXTURE_2D, V9X_GL_TEXTURE_MIN_FILTER,
                         (GLint)V9X_GL_LINEAR);
    v9x_gl_tex_describe(&s, &t, &d, levels);
    v9x_gl_tex_fit(&t, 0ul, &d, levels, 4ul);
    TCHECK(d.level_count == 1ul && d.storage == V9X_R3D_ABI_TEXTURE_CPU);
    TCHECK(levels[0].width == 4ul && levels[0].height == 4ul &&
           levels[0].pitch == 8ul && levels[0].bytes == 32ul);
    TCHECK(levels[0].pixels != 0 &&
           ((const v9x_u16 *)levels[0].pixels)[0] == 0xf700u);
    TCHECK(((const v9x_u16 *)levels[0].pixels)[15] == 0xf700u);
    /* The copy is kept: a second draw allocates nothing. */
    reduced = levels[0].pixels;
    held = outstanding;
    v9x_gl_tex_describe(&s, &t, &d, levels);
    v9x_gl_tex_fit(&t, 0ul, &d, levels, 4ul);
    TCHECK(levels[0].pixels == reduced && outstanding == held);
    /* A new image is a new copy. */
    for (i = 0u; i < 8u * 8u; ++i) {
        stripes[i * 4u + 0u] = 255u;
    }
    v9x_gl_tex_sub_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 0, 0, 8, 8,
                            V9X_GL_RGBA, V9X_GL_UNSIGNED_BYTE, stripes);
    v9x_gl_tex_describe(&s, &t, &d, levels);
    v9x_gl_tex_fit(&t, 0ul, &d, levels, 4ul);
    TCHECK(((const v9x_u16 *)levels[0].pixels)[0] == 0xff00u);
    /* Two halvings at once: 8 to 2. */
    v9x_gl_tex_describe(&s, &t, &d, levels);
    v9x_gl_tex_fit(&t, 0ul, &d, levels, 2ul);
    TCHECK(levels[0].width == 2ul && levels[0].bytes == 8ul &&
           ((const v9x_u16 *)levels[0].pixels)[3] == 0xff00u);
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

/* GL_SGIS_multitexture's units: each its own binding, images, environment
 * and enable, selected by the SGIS tokens, and described apart. */
static void test_sgis_units(void)
{
    V9X_GL_STATE s;
    V9X_GL_TEXTURES t;
    V9X_R3D_ABI_TEXTURE d0;
    V9X_R3D_ABI_TEXTURE d1;
    V9X_R3D_ABI_LEVEL levels0[V9X_GL_TEXTURE_LEVELS];
    V9X_R3D_ABI_LEVEL levels1[V9X_GL_TEXTURE_LEVELS];
    GLfloat replace = (GLfloat)V9X_GL_REPLACE;
    GLfloat blend = (GLfloat)V9X_GL_BLEND_ENV;
    GLfloat colour[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    GLboolean enabled = 0;
    const V9X_GL_TEXOBJ *first;
    const V9X_GL_TEXOBJ *second;
    GLuint name;

    fresh(&s, &t);
    v9x_gl_pixel_store(&s, &t, V9X_GL_UNPACK_ALIGNMENT, 1);

    /* Unit 0: texture 1, RGB, REPLACE. */
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, 1u);
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 3, 1, 1, 0, V9X_GL_RGB,
                        V9X_GL_UNSIGNED_BYTE, "abc");
    v9x_gl_tex_parameter(&s, &t, V9X_GL_TEXTURE_2D, V9X_GL_TEXTURE_MIN_FILTER,
                         (GLint)V9X_GL_NEAREST);
    v9x_gl_tex_env(&s, &t, V9X_GL_TEXTURE_ENV, V9X_GL_TEXTURE_ENV_MODE,
                   &replace);
    first = bound_object(&t);

    /* Unit 1: texture 2, a luminance lightmap, BLEND toward green. */
    v9x_gl_tex_select(&s, &t, V9X_GL_TEXTURE1_SGIS);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR && t.active == 1ul);
    TCHECK(t.units[1].bound == 0u && t.units[1].env_mode == V9X_GL_MODULATE);
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, 2u);
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 1, 1, 1, 0,
                        V9X_GL_LUMINANCE, V9X_GL_UNSIGNED_BYTE, "z");
    v9x_gl_tex_parameter(&s, &t, V9X_GL_TEXTURE_2D, V9X_GL_TEXTURE_MIN_FILTER,
                         (GLint)V9X_GL_NEAREST);
    v9x_gl_tex_env(&s, &t, V9X_GL_TEXTURE_ENV, V9X_GL_TEXTURE_ENV_MODE, &blend);
    v9x_gl_tex_env(&s, &t, V9X_GL_TEXTURE_ENV, V9X_GL_TEXTURE_ENV_COLOR,
                   colour);
    second = bound_object(&t);
    TCHECK(first != second && t.units[0].bound == 1u &&
           t.units[1].bound == 2u &&
           t.units[0].env_mode == V9X_GL_REPLACE &&
           t.units[0].env_color_packed == 0ul);

    /* Unit 1's enable is its own, not the state's TEXTURE_2D. */
    TCHECK(v9x_gl_tex_enable_selected(&t, V9X_GL_TEXTURE_2D, 1));
    TCHECK(!v9x_gl_state_cap(&s, V9X_GL_TEXTURE_2D));
    TCHECK(v9x_gl_tex_is_enabled_selected(&t, V9X_GL_TEXTURE_2D, &enabled) &&
           enabled == 1);
    TCHECK(!v9x_gl_tex_enable_selected(&t, 0x0B71u, 1));    /* DEPTH_TEST */

    /* Unit 0 off, unit 1 on: only unit 1 describes. */
    v9x_gl_tex_describe_unit(&s, &t, 0ul, &d0, levels0);
    v9x_gl_tex_describe_unit(&s, &t, 1ul, &d1, levels1);
    TCHECK(d0.storage == V9X_R3D_ABI_TEXTURE_NONE);
    TCHECK(d1.storage == V9X_R3D_ABI_TEXTURE_CPU &&
           d1.color_op == V9X_R3D_ABI_COLOROP_BLEND &&
           d1.alpha_op == V9X_R3D_ABI_ALPHAOP_FRAGMENT &&
           d1.env_color == 0x0000ff00ul && d1.levels == levels1 &&
           levels1[0].pixels == second->levels[0].texels);

    /* With unit 0 selected, TEXTURE_2D is the state's again. */
    v9x_gl_tex_select(&s, &t, V9X_GL_TEXTURE0_SGIS);
    TCHECK(t.active == 0ul);
    TCHECK(!v9x_gl_tex_enable_selected(&t, V9X_GL_TEXTURE_2D, 1));
    TCHECK(!v9x_gl_tex_is_enabled_selected(&t, V9X_GL_TEXTURE_2D, &enabled));
    v9x_gl_state_enable(&s, V9X_GL_TEXTURE_2D, 1);
    v9x_gl_tex_describe_unit(&s, &t, 0ul, &d0, levels0);
    TCHECK(d0.storage == V9X_R3D_ABI_TEXTURE_CPU &&
           d0.color_op == V9X_R3D_ABI_COLOROP_REPLACE &&
           levels0[0].pixels == first->levels[0].texels);
    v9x_gl_tex_describe(&s, &t, &d1, levels1);
    TCHECK(d1.storage == d0.storage && levels1[0].pixels ==
           levels0[0].pixels);
    TCHECK(t.units[1].enabled == 1);

    /* Any other target is INVALID_ENUM and selects nothing; a unit past
     * the last describes nothing. */
    v9x_gl_tex_select(&s, &t, V9X_GL_TEXTURE1_SGIS + 1u);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM &&
           t.active == 0ul);
    v9x_gl_tex_describe_unit(&s, &t, V9X_GL_TEXTURE_UNITS, &d0, levels0);
    TCHECK(d0.storage == V9X_R3D_ABI_TEXTURE_NONE);

    /* Deleting a texture unbinds it from whichever unit holds it. */
    name = 2u;
    v9x_gl_tex_delete(&s, &t, 1, &name);
    TCHECK(t.units[1].bound == 0u && t.units[0].bound == 1u);

    v9x_gl_textures_release(&t);
    TCHECK(outstanding == 0l);
}

/*
 * What a hardware copy at a revision must refill: nothing when current, the
 * union of glTexSubImage2D's rectangles per level since the last reset, or
 * the whole object when it is older than that (an image was specified, or
 * the copy predates the rectangles).
 */
static void test_dirty_rectangles(void)
{
    V9X_GL_STATE s;
    V9X_GL_TEXTURES t;
    V9X_GL_TEXOBJ *object;
    V9X_GL_TEXRECT rect;
    static v9x_u8 image[16 * 16 * 3];
    v9x_u32 copy;

    fresh(&s, &t);
    v9x_gl_pixel_store(&s, &t, V9X_GL_UNPACK_ALIGNMENT, 1);
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 3, 16, 16, 0,
                        V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, image);
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 1, 3, 8, 8, 0,
                        V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, image);
    object = v9x_gl_tex_bound_object(&t);
    copy = object->revision;

    /* A copy made now is current: nothing to refill on any level. */
    TCHECK(v9x_gl_tex_dirty_rect(object, copy, 0u, &rect) == 1 &&
           rect.right <= rect.left);
    TCHECK(v9x_gl_tex_dirty_rect(object, copy, 1u, &rect) == 1 &&
           rect.right <= rect.left);

    /* Two sub-images on level 0: their union, level 1 untouched. */
    v9x_gl_tex_sub_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 2, 3, 4, 2,
                            V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, image);
    v9x_gl_tex_sub_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 10, 1, 3, 3,
                            V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, image);
    TCHECK(object->revision == copy + 2ul);
    TCHECK(v9x_gl_tex_dirty_rect(object, copy, 0u, &rect) == 1 &&
           rect.left == 2ul && rect.top == 1ul && rect.right == 13ul &&
           rect.bottom == 5ul);
    TCHECK(v9x_gl_tex_dirty_rect(object, copy, 1u, &rect) == 1 &&
           rect.right <= rect.left);

    /* After the copy is refilled and the rectangles reset, a later
     * sub-image is all there is. */
    v9x_gl_tex_dirty_reset(object);
    copy = object->revision;
    v9x_gl_tex_sub_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 1, 0, 0, 1, 1,
                            V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, image);
    TCHECK(v9x_gl_tex_dirty_rect(object, copy, 0u, &rect) == 1 &&
           rect.right <= rect.left);
    TCHECK(v9x_gl_tex_dirty_rect(object, copy, 1u, &rect) == 1 &&
           rect.left == 0ul && rect.right == 1ul && rect.bottom == 1ul);

    /* A copy older than the reset cannot be brought up by rectangles. */
    TCHECK(v9x_gl_tex_dirty_rect(object, copy - 1ul, 0u, &rect) == 0);
    /* Nor one older than an image specification, which restarts them. */
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 1, 3, 8, 8, 0,
                        V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, image);
    TCHECK(v9x_gl_tex_dirty_rect(object, copy, 0u, &rect) == 0);
    TCHECK(v9x_gl_tex_dirty_rect(object, object->revision, 1u, &rect) ==
               1 &&
           rect.right <= rect.left);
    /* A level past the chain is refused. */
    TCHECK(v9x_gl_tex_dirty_rect(object, object->revision,
                                 V9X_GL_TEXTURE_LEVELS, &rect) == 0);
    v9x_gl_textures_release(&t);
    TCHECK(outstanding == 0l);
}

static v9x_u32 hw_released;
static void *hw_last;

static void test_hw_release(void *hw)
{
    ++hw_released;
    hw_last = hw;
}

/* The hardware-copy bookkeeping: revisions, the release hook on delete,
 * release and drop, and the alpha normalisation. */
static void test_hardware_copy_bookkeeping(void)
{
    V9X_GL_STATE s;
    V9X_GL_TEXTURES t;
    V9X_GL_TEXOBJ *object;
    V9X_R3D_ABI_TEXTURE d;
    GLuint name = 0u;
    int token_a = 1;
    int token_b = 2;
    int token_c = 3;
    v9x_u32 before;

    v9x_gl_state_init(&s);
    v9x_gl_textures_init(&t, test_alloc, test_free);
    t.hw_release = test_hw_release;
    hw_released = 0ul;
    hw_last = 0;

    v9x_gl_tex_gen(&s, &t, 1, &name);
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, name);
    object = v9x_gl_tex_bound_object(&t);
    TCHECK(object != &t.default_object && object->name == name);
    before = object->revision;
    v9x_gl_pixel_store(&s, &t, V9X_GL_UNPACK_ALIGNMENT, 1);
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 3, 2, 2, 0, V9X_GL_RGB,
                        V9X_GL_UNSIGNED_BYTE, "abcdefghijkl");
    TCHECK(object->revision != before);
    before = object->revision;
    v9x_gl_tex_sub_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 1, 1, 1, 1,
                            V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, "xyz");
    TCHECK(object->revision != before);
    before = object->revision;
    /* A parameter is not image content. */
    v9x_gl_tex_parameter(&s, &t, V9X_GL_TEXTURE_2D, V9X_GL_TEXTURE_MIN_FILTER,
                         (GLint)V9X_GL_NEAREST);
    TCHECK(object->revision == before);

    /* Deleting the object releases its copy through the hook. */
    object->hw = &token_a;
    v9x_gl_tex_delete(&s, &t, 1, &name);
    TCHECK(hw_released == 1ul && hw_last == (void *)&token_a);

    /* So do the context's release and a mode change's drop. */
    object = v9x_gl_tex_bound_object(&t);
    TCHECK(object == &t.default_object);
    object->hw = &token_b;
    v9x_gl_textures_drop_hw(&t);
    TCHECK(hw_released == 2ul && hw_last == (void *)&token_b &&
           object->hw == 0);
    v9x_gl_textures_drop_hw(&t);
    TCHECK(hw_released == 2ul);
    object->hw = &token_c;
    v9x_gl_textures_release(&t);
    TCHECK(hw_released == 3ul && hw_last == (void *)&token_c);
    TCHECK(outstanding == 0l);

    /* The normalisation: REPLACE on RGB565 with the fragment's alpha
     * becomes REPLACE/REPLACE; alpha-bearing textures and MODULATE are
     * left as they are. */
    d.format = V9X_R3D_ABI_FORMAT_RGB565;
    d.color_op = V9X_R3D_ABI_COLOROP_REPLACE;
    d.alpha_op = V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    v9x_gl_tex_fragment_alpha_unused(&d);
    TCHECK(d.alpha_op == V9X_R3D_ABI_ALPHAOP_REPLACE);
    d.color_op = V9X_R3D_ABI_COLOROP_MODULATE;
    d.alpha_op = V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    v9x_gl_tex_fragment_alpha_unused(&d);
    TCHECK(d.alpha_op == V9X_R3D_ABI_ALPHAOP_FRAGMENT);
    d.format = V9X_R3D_ABI_FORMAT_ARGB4444;
    d.color_op = V9X_R3D_ABI_COLOROP_DECALALPHA;
    d.alpha_op = V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    v9x_gl_tex_fragment_alpha_unused(&d);
    TCHECK(d.alpha_op == V9X_R3D_ABI_ALPHAOP_FRAGMENT);
    /* An alpha-bearing texture under REPLACE or MODULATE: with no reader
     * of the result's alpha, the texel's serves (so an engine that has
     * MODULATE but not MODULATEALPHA draws it). */
    d.format = V9X_R3D_ABI_FORMAT_ARGB4444;
    d.color_op = V9X_R3D_ABI_COLOROP_MODULATE;
    d.alpha_op = V9X_R3D_ABI_ALPHAOP_MODULATE;
    v9x_gl_tex_fragment_alpha_unused(&d);
    TCHECK(d.alpha_op == V9X_R3D_ABI_ALPHAOP_REPLACE);
    d.color_op = V9X_R3D_ABI_COLOROP_REPLACE;
    d.alpha_op = V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    v9x_gl_tex_fragment_alpha_unused(&d);
    TCHECK(d.alpha_op == V9X_R3D_ABI_ALPHAOP_REPLACE);
}

/* RGB565 retargeted to ARGB1555 for an engine without 565. */
static void test_retarget_to_1555(void)
{
    V9X_R3D_ABI_TEXTURE d;
    unsigned int i;

    /* White, black, pure red, pure green (six bits to five), pure blue,
     * and a mixed value: F800 red, 07E0 green, 001F blue. */
    TCHECK(v9x_gl_tex_565_to_1555(0xFFFFu) == 0xFFFFu);
    TCHECK(v9x_gl_tex_565_to_1555(0x0000u) == 0x8000u);
    TCHECK(v9x_gl_tex_565_to_1555(0xF800u) == 0xFC00u);
    TCHECK(v9x_gl_tex_565_to_1555(0x07E0u) == 0x83E0u);
    TCHECK(v9x_gl_tex_565_to_1555(0x001Fu) == 0x801Fu);
    TCHECK(v9x_gl_tex_565_to_1555(0x0020u) == 0x8000u);  /* green LSB drops */
    TCHECK(v9x_gl_tex_565_to_1555(0x0040u) == 0x8020u);
    TCHECK(v9x_gl_tex_565_to_1555(0x8410u) == 0xC210u);

    for (i = 0u; i < sizeof(d); ++i) {
        ((v9x_u8 *)&d)[i] = 0u;
    }
    d.format = V9X_R3D_ABI_FORMAT_RGB565;
    d.color_op = V9X_R3D_ABI_COLOROP_MODULATE;
    d.alpha_op = V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    v9x_gl_tex_as_1555(&d, 1);
    TCHECK(d.format == V9X_R3D_ABI_FORMAT_ARGB1555 &&
           d.color_op == V9X_R3D_ABI_COLOROP_MODULATE &&
           d.alpha_op == V9X_R3D_ABI_ALPHAOP_MODULATE);
    d.format = V9X_R3D_ABI_FORMAT_RGB565;
    d.alpha_op = V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    v9x_gl_tex_as_1555(&d, 0);
    TCHECK(d.alpha_op == V9X_R3D_ABI_ALPHAOP_REPLACE);
    d.format = V9X_R3D_ABI_FORMAT_RGB565;
    d.color_op = V9X_R3D_ABI_COLOROP_REPLACE;
    d.alpha_op = V9X_R3D_ABI_ALPHAOP_REPLACE;
    v9x_gl_tex_as_1555(&d, 1);
    TCHECK(d.color_op == V9X_R3D_ABI_COLOROP_REPLACE &&
           d.alpha_op == V9X_R3D_ABI_ALPHAOP_REPLACE);
}

/*
 * A square-only sampler of at least side_min (the Mach64: 8 to 256) takes
 * a square copy. A wrapped image repeats across it, so with s and t scaled
 * by width/side and height/side it samples the same texels, repeats
 * included; a clamped one keeps its last row and column out to the edge.
 */
static void test_square_copy(void)
{
    static const v9x_u16 image[2][4] = {
        { 0x0001u, 0x0002u, 0x0003u, 0x0004u },
        { 0x0011u, 0x0012u, 0x0013u, 0x0014u } };
    v9x_u16 square[8 * 8];
    v9x_u32 x;
    v9x_u32 y;
    int wrap_ok = 1;
    int clamp_ok = 1;

    TCHECK(v9x_gl_tex_square_side(4ul, 2ul, 1ul, 256ul) == 4ul);
    TCHECK(v9x_gl_tex_square_side(64ul, 32ul, 8ul, 256ul) == 64ul);
    TCHECK(v9x_gl_tex_square_side(2ul, 4ul, 8ul, 256ul) == 8ul);
    TCHECK(v9x_gl_tex_square_side(1ul, 1ul, 8ul, 256ul) == 8ul);
    TCHECK(v9x_gl_tex_square_side(512ul, 16ul, 8ul, 256ul) == 0ul);

    v9x_gl_tex_square_fill(&image[0][0], 8ul, 4ul, 2ul, square, 4ul, 0);
    for (y = 0ul; y < 4ul; ++y) {
        for (x = 0ul; x < 4ul; ++x) {
            if (square[y * 4ul + x] != image[y % 2ul][x]) {
                wrap_ok = 0;
            }
        }
    }
    TCHECK(wrap_ok);

    v9x_gl_tex_square_fill(&image[0][0], 8ul, 4ul, 2ul, square, 8ul, 1);
    for (y = 0ul; y < 8ul; ++y) {
        for (x = 0ul; x < 8ul; ++x) {
            if (square[y * 8ul + x] !=
                image[y < 2ul ? y : 1ul][x < 4ul ? x : 3ul]) {
                clamp_ok = 0;
            }
        }
    }
    TCHECK(clamp_ok);

    /* A 1x1 level (the chain's end) fills the whole square. */
    v9x_gl_tex_square_fill(&image[0][0], 8ul, 1ul, 1ul, square, 2ul, 0);
    TCHECK(square[0] == 0x0001u && square[1] == 0x0001u &&
           square[2] == 0x0001u && square[3] == 0x0001u);
}

/* glGetTexParameter, glGetTexLevelParameter and glGetTexEnv. */
static void test_texture_queries(void)
{
    static const GLubyte rgb[4 * 2 * 3] = { 0 };
    V9X_GL_STATE s;
    V9X_GL_TEXTURES t;
    GLfloat v[4];
    GLfloat colour[4];
    GLuint name;
    int is_colour = -1;

    fresh(&s, &t);
    v9x_gl_tex_gen(&s, &t, 1, &name);
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, name);

    /* Parameters: the initial filters and wraps, then set ones; border
     * colour 0,0,0,0 as a colour; priority 1; resident. */
    TCHECK(v9x_gl_tex_get_parameter(&s, &t, V9X_GL_TEXTURE_2D,
                                    V9X_GL_TEXTURE_MIN_FILTER, v,
                                    &is_colour) == 1u);
    TCHECK(v[0] == (GLfloat)V9X_GL_NEAREST_MIPMAP_LINEAR && !is_colour);
    TCHECK(v9x_gl_tex_get_parameter(&s, &t, V9X_GL_TEXTURE_2D,
                                    V9X_GL_TEXTURE_WRAP_T, v,
                                    &is_colour) == 1u &&
           v[0] == (GLfloat)V9X_GL_REPEAT);
    v9x_gl_tex_parameter(&s, &t, V9X_GL_TEXTURE_2D, V9X_GL_TEXTURE_MAG_FILTER,
                         (GLint)V9X_GL_NEAREST);
    v9x_gl_tex_parameter(&s, &t, V9X_GL_TEXTURE_2D, V9X_GL_TEXTURE_WRAP_S,
                         (GLint)V9X_GL_CLAMP);
    TCHECK(v9x_gl_tex_get_parameter(&s, &t, V9X_GL_TEXTURE_2D,
                                    V9X_GL_TEXTURE_MAG_FILTER, v,
                                    &is_colour) == 1u &&
           v[0] == (GLfloat)V9X_GL_NEAREST);
    TCHECK(v9x_gl_tex_get_parameter(&s, &t, V9X_GL_TEXTURE_2D,
                                    V9X_GL_TEXTURE_WRAP_S, v,
                                    &is_colour) == 1u &&
           v[0] == (GLfloat)V9X_GL_CLAMP);
    TCHECK(v9x_gl_tex_get_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0x1004u, v,
                                    &is_colour) == 4u);
    TCHECK(is_colour && v[0] == 0.0f && v[3] == 0.0f);
    TCHECK(v9x_gl_tex_get_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0x8066u, v,
                                    &is_colour) == 1u && v[0] == 1.0f);
    TCHECK(v9x_gl_tex_get_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0x8067u, v,
                                    &is_colour) == 1u && v[0] == 1.0f);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);

    /* An empty level: 0 by 0, internal format 1, no bits. */
    TCHECK(v9x_gl_tex_get_level_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0,
                                          0x1000u, v) == 1u && v[0] == 0.0f);
    TCHECK(v9x_gl_tex_get_level_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0,
                                          0x1003u, v) == 1u && v[0] == 1.0f);
    TCHECK(v9x_gl_tex_get_level_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0,
                                          0x805Cu, v) == 1u && v[0] == 0.0f);

    /* A 4x2 RGB image: its size, its base format, stored 5-6-5. */
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, 3, 4, 2, 0,
                        V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, rgb);
    TCHECK(v9x_gl_tex_get_level_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0,
                                          0x1000u, v) == 1u && v[0] == 4.0f);
    TCHECK(v9x_gl_tex_get_level_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0,
                                          0x1001u, v) == 1u && v[0] == 2.0f);
    TCHECK(v9x_gl_tex_get_level_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0,
                                          0x1003u, v) == 1u &&
           v[0] == (GLfloat)V9X_GL_RGB);
    TCHECK(v9x_gl_tex_get_level_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0,
                                          0x1005u, v) == 1u && v[0] == 0.0f);
    TCHECK(v9x_gl_tex_get_level_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0,
                                          0x805Cu, v) == 1u && v[0] == 5.0f);
    TCHECK(v9x_gl_tex_get_level_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0,
                                          0x805Du, v) == 1u && v[0] == 6.0f);
    TCHECK(v9x_gl_tex_get_level_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0,
                                          0x805Fu, v) == 1u && v[0] == 0.0f);
    /* RGBA is stored 4-4-4-4; LUMINANCE as 5-6-5 grey, so luminance 5. */
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, V9X_GL_RGBA, 2, 2, 0,
                        V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, rgb);
    TCHECK(v9x_gl_tex_get_level_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0,
                                          0x805Fu, v) == 1u && v[0] == 4.0f);
    TCHECK(v9x_gl_tex_get_level_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0,
                                          0x805Cu, v) == 1u && v[0] == 4.0f);
    v9x_gl_tex_image_2d(&s, &t, V9X_GL_TEXTURE_2D, 0, V9X_GL_LUMINANCE, 2, 2,
                        0, V9X_GL_RGB, V9X_GL_UNSIGNED_BYTE, rgb);
    TCHECK(v9x_gl_tex_get_level_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0,
                                          0x8060u, v) == 1u && v[0] == 5.0f);
    TCHECK(v9x_gl_tex_get_level_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0,
                                          0x805Cu, v) == 1u && v[0] == 0.0f);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);

    /* The environment: MODULATE and black, then what was set. */
    TCHECK(v9x_gl_tex_get_env(&s, &t, V9X_GL_TEXTURE_ENV,
                              V9X_GL_TEXTURE_ENV_MODE, v, &is_colour) == 1u);
    TCHECK(v[0] == (GLfloat)V9X_GL_MODULATE && !is_colour);
    colour[0] = 0.25f;
    colour[1] = 0.5f;
    colour[2] = 0.75f;
    colour[3] = 1.0f;
    v9x_gl_tex_env(&s, &t, V9X_GL_TEXTURE_ENV, V9X_GL_TEXTURE_ENV_COLOR,
                   colour);
    TCHECK(v9x_gl_tex_get_env(&s, &t, V9X_GL_TEXTURE_ENV,
                              V9X_GL_TEXTURE_ENV_COLOR, v, &is_colour) == 4u);
    TCHECK(is_colour && v[0] == 0.25f && v[2] == 0.75f && v[3] == 1.0f);

    /* Errors, each recorded and answering nothing. */
    TCHECK(v9x_gl_tex_get_parameter(&s, &t, 0x0DE0u, V9X_GL_TEXTURE_WRAP_S,
                                    v, &is_colour) == 0u);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    TCHECK(v9x_gl_tex_get_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0x1234u, v,
                                    &is_colour) == 0u);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    TCHECK(v9x_gl_tex_get_level_parameter(&s, &t, V9X_GL_TEXTURE_2D, -1,
                                          0x1000u, v) == 0u);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    TCHECK(v9x_gl_tex_get_level_parameter(&s, &t, V9X_GL_TEXTURE_2D,
                                          (GLint)V9X_GL_TEXTURE_LEVELS,
                                          0x1000u, v) == 0u);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    TCHECK(v9x_gl_tex_get_level_parameter(&s, &t, 0x8064u, 0, 0x1000u,
                                          v) == 0u);   /* PROXY_TEXTURE_2D */
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    TCHECK(v9x_gl_tex_get_level_parameter(&s, &t, V9X_GL_TEXTURE_2D, 0,
                                          0x1234u, v) == 0u);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    TCHECK(v9x_gl_tex_get_env(&s, &t, V9X_GL_TEXTURE_2D,
                              V9X_GL_TEXTURE_ENV_MODE, v, &is_colour) == 0u);
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    s.in_begin = 1;
    TCHECK(v9x_gl_tex_get_env(&s, &t, V9X_GL_TEXTURE_ENV,
                              V9X_GL_TEXTURE_ENV_MODE, v, &is_colour) == 0u);
    s.in_begin = 0;
    TCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_OPERATION);
    v9x_gl_textures_release(&t);
}

unsigned int v9x_run_gl_texture_tests(void)
{
    gl_texture_failures = 0u;
    outstanding = 0l;
    test_texture_queries();
    test_names_and_binding();
    test_uploads();
    test_completeness_and_describe();
    test_fit_to_size_max();
    test_lookup_after_reuse_and_growth();
    test_environment_table();
    test_sgis_units();
    test_dirty_rectangles();
    test_hardware_copy_bookkeeping();
    test_retarget_to_1555();
    test_square_copy();
    if (gl_texture_failures == 0u) {
        printf("PASS: OpenGL texture objects and images\n");
    }
    return gl_texture_failures;
}
