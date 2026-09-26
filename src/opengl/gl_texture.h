/*
 * OpenGL 1.1 texture objects and images (docs\plans\opengl-1.1-icd.md,
 * Phase 4): names, binding, glTexImage2D/glTexSubImage2D, parameters,
 * completeness and the texture environment, down to the render interface's
 * CPU texture description. Pure; storage comes from an allocator the ICD
 * supplies (HeapAlloc there, malloc in the host tests).
 *
 * Each level is kept as the sampler-ready 16-bit copy the plan calls for,
 * converted at upload: base formats without alpha to RGB565, with alpha to
 * ARGB4444, ALPHA with white colour so that the environment's
 * colour-from-fragment cases come out of the rasterizer's ops exactly. The
 * logical image (for glGetTexImage and restore) is a later slice; until
 * then the queries that need it are not offered.
 */
#ifndef VELOCITY9X_GL_TEXTURE_H
#define VELOCITY9X_GL_TEXTURE_H

#include "velocity9x/r3d_abi.h"
#include "gl_state.h"

#define V9X_GL_TEXTURE_2D          0x0DE1u
#define V9X_GL_TEXTURE_MAG_FILTER  0x2800u
#define V9X_GL_TEXTURE_MIN_FILTER  0x2801u
#define V9X_GL_TEXTURE_WRAP_S      0x2802u
#define V9X_GL_TEXTURE_WRAP_T      0x2803u
#define V9X_GL_NEAREST             0x2600u
#define V9X_GL_LINEAR              0x2601u
#define V9X_GL_NEAREST_MIPMAP_NEAREST 0x2700u
#define V9X_GL_LINEAR_MIPMAP_NEAREST  0x2701u
#define V9X_GL_NEAREST_MIPMAP_LINEAR  0x2702u
#define V9X_GL_LINEAR_MIPMAP_LINEAR   0x2703u
#define V9X_GL_CLAMP               0x2900u
#define V9X_GL_REPEAT              0x2901u

#define V9X_GL_TEXTURE_ENV         0x2300u
#define V9X_GL_TEXTURE_ENV_MODE    0x2200u
#define V9X_GL_TEXTURE_ENV_COLOR   0x2201u
#define V9X_GL_MODULATE            0x2100u
#define V9X_GL_DECAL               0x2101u
#define V9X_GL_REPLACE             0x1E01u
#define V9X_GL_BLEND_ENV           0x0BE2u   /* GL_BLEND, as an env mode */

#define V9X_GL_UNSIGNED_BYTE       0x1401u
#define V9X_GL_ALPHA               0x1906u
#define V9X_GL_RGB                 0x1907u
#define V9X_GL_RGBA                0x1908u
#define V9X_GL_LUMINANCE           0x1909u
#define V9X_GL_LUMINANCE_ALPHA     0x190Au
#define V9X_GL_INTENSITY           0x8049u
#define V9X_GL_UNPACK_ALIGNMENT    0x0CF5u

/* The largest level 0 edge this implementation accepts: the rasterizer's
 * 512, and the ten levels to 1x1 a 512 chain has. */
#define V9X_GL_TEXTURE_SIZE_MAX 512u
#define V9X_GL_TEXTURE_LEVELS   10u

typedef void *(*V9X_GL_ALLOC_FN)(v9x_u32 bytes);
typedef void (*V9X_GL_FREE_FN)(void *memory);

typedef struct v9x_gl_texlevel {
    v9x_u16 *texels;        /* width * height, tightly packed */
    v9x_u32 width;
    v9x_u32 height;
} V9X_GL_TEXLEVEL;

typedef struct v9x_gl_texobj {
    GLuint name;
    int in_use;
    /* The base internal format of level 0 (GL_ALPHA, _RGB, _RGBA,
     * _LUMINANCE, _LUMINANCE_ALPHA, _INTENSITY), and the 16-bit layout
     * every level is stored in (V9X_R3D_ABI_FORMAT_*). */
    GLenum base_format;
    v9x_u32 storage_format;
    V9X_GL_TEXLEVEL levels[V9X_GL_TEXTURE_LEVELS];
    GLenum min_filter;
    GLenum mag_filter;
    GLenum wrap_s;
    GLenum wrap_t;
    /* Bumped by every glTexImage2D and glTexSubImage2D on any level, so a
     * copy made from the images can tell it is stale. */
    v9x_u32 revision;
    /* The ICD's hardware copy of the images, opaque here; released through
     * V9X_GL_TEXTURES.hw_release when the object's storage goes. */
    void *hw;
} V9X_GL_TEXOBJ;

typedef struct v9x_gl_textures {
    V9X_GL_ALLOC_FN alloc;
    V9X_GL_FREE_FN release;
    /* Releases an object's `hw`; null when the ICD keeps none. */
    V9X_GL_FREE_FN hw_release;
    /* Object 0 is the default texture; the rest grow on demand. */
    V9X_GL_TEXOBJ default_object;
    V9X_GL_TEXOBJ *objects;
    v9x_u32 capacity;
    GLuint next_name;
    GLuint bound;
    GLenum env_mode;
    GLfloat env_color[4];
    GLint unpack_alignment;
    GLint unpack_row_length;
    GLint unpack_skip_rows;
    GLint unpack_skip_pixels;
    /* The pack parameters, indexed by pname - PACK_SWAP_BYTES, which
     * glReadPixels reads (gl_pixels.c). */
    GLint pack[6];
} V9X_GL_TEXTURES;

void v9x_gl_textures_init(V9X_GL_TEXTURES *textures, V9X_GL_ALLOC_FN alloc,
                          V9X_GL_FREE_FN release);
/* Every object's storage back to the allocator (context deletion). */
void v9x_gl_textures_release(V9X_GL_TEXTURES *textures);

void v9x_gl_tex_gen(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                    GLsizei n, GLuint *names);
void v9x_gl_tex_delete(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                       GLsizei n, const GLuint *names);
GLboolean v9x_gl_tex_is(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                        GLuint name);
void v9x_gl_tex_bind(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                     GLenum target, GLuint name);
void v9x_gl_tex_parameter(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                          GLenum target, GLenum pname, GLint value);
void v9x_gl_tex_env(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                    GLenum target, GLenum pname, const GLfloat *values);
void v9x_gl_pixel_store(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                        GLenum pname, GLint value);
void v9x_gl_tex_image_2d(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                         GLenum target, GLint level, GLint internal_format,
                         GLsizei width, GLsizei height, GLint border,
                         GLenum format, GLenum type, const void *pixels);
void v9x_gl_tex_sub_image_2d(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                             GLenum target, GLint level, GLint xoffset,
                             GLint yoffset, GLsizei width, GLsizei height,
                             GLenum format, GLenum type, const void *pixels);

/*
 * The render interface's texture for a draw: NONE unless TEXTURE_2D is
 * enabled and the bound object is complete (3.8.9); otherwise CPU storage
 * naming `levels`, which must have room for V9X_GL_TEXTURE_LEVELS and
 * stays valid until the next texture command.
 */
void v9x_gl_tex_describe(const V9X_GL_STATE *state,
                         V9X_GL_TEXTURES *textures,
                         V9X_R3D_ABI_TEXTURE *out,
                         V9X_R3D_ABI_LEVEL *levels);

/* The bound object (never null: name 0 is the default texture). */
V9X_GL_TEXOBJ *v9x_gl_tex_bound_object(V9X_GL_TEXTURES *textures);
/* Every object's hardware copy released through the hook and forgotten:
 * a mode change has made them all lost. */
void v9x_gl_textures_drop_hw(V9X_GL_TEXTURES *textures);
/*
 * When nothing reads the fragment's alpha, REPLACE (and DECAL) on a
 * texture without alpha may take the texel's alpha of one instead of the
 * fragment's: the pixels written are identical, and the combined form is
 * the one hardware texture stages have (Direct3D's DECAL).
 */
void v9x_gl_tex_fragment_alpha_unused(V9X_R3D_ABI_TEXTURE *texture);

#endif /* VELOCITY9X_GL_TEXTURE_H */
