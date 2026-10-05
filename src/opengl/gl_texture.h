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

/* A half-open rectangle of texels; empty when right <= left. */
typedef struct v9x_gl_texrect {
    v9x_u32 left;
    v9x_u32 top;
    v9x_u32 right;
    v9x_u32 bottom;
} V9X_GL_TEXRECT;

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
    /*
     * What changed since revision `dirty_from`: per level, the union of the
     * rectangles glTexSubImage2D wrote. A glTexImage2D restarts it with
     * nothing dirty, so a copy older than that refills whole. Lets the
     * ICD's hardware copy refill only what an update touched
     * (v9x_gl_tex_dirty_rect); Quake 2 and Half-Life update a lightmap a
     * surface at a time under multitexture (2026-10-05).
     */
    v9x_u32 dirty_from;
    V9X_GL_TEXRECT dirty[V9X_GL_TEXTURE_LEVELS];
    /* The ICD's hardware copy of the images, opaque here; released through
     * V9X_GL_TEXTURES.hw_release when the object's storage goes. */
    void *hw;
    /* Level 0 box-filtered to fit a device's largest edge
     * (v9x_gl_tex_fit), made for `reduced_revision` and `reduced_limit`. */
    v9x_u16 *reduced;
    v9x_u32 reduced_revision;
    v9x_u32 reduced_limit;
    v9x_u32 reduced_width;
    v9x_u32 reduced_height;
} V9X_GL_TEXOBJ;

/*
 * GL_SGIS_multitexture's two units (docs\plans\gen3-sgis-multitexture.md),
 * named by the tokens GLQuake, Quake 2 and Half-Life use. The census found
 * them at 0x835E and 0x835F in all three
 * (docs\decisions\2026-09-26-opengl-icd-interface-research.md).
 */
#define V9X_GL_TEXTURE_UNITS    2u
#define V9X_GL_TEXTURE0_SGIS    0x835Eu
#define V9X_GL_TEXTURE1_SGIS    0x835Fu

/* What each unit has of its own: a binding and an environment, and unit
 * 1's enable. Unit 0's enable is the state's TEXTURE_2D capability, which
 * glIsEnabled and glPushAttrib already keep. */
typedef struct v9x_gl_texunit {
    GLuint bound;
    GLenum env_mode;
    GLfloat env_color[4];
    /* env_color as the interface's 0x00RRGGBB, packed when it is set
     * rather than at every describe (2026-10-01). */
    v9x_u32 env_color_packed;
    int enabled;
} V9X_GL_TEXUNIT;

typedef struct v9x_gl_textures {
    V9X_GL_ALLOC_FN alloc;
    V9X_GL_FREE_FN release;
    /* Releases an object's `hw`; null when the ICD keeps none. */
    V9X_GL_FREE_FN hw_release;
    /* Object 0 is the default texture; the rest grow on demand. */
    V9X_GL_TEXOBJ default_object;
    V9X_GL_TEXOBJ *objects;
    v9x_u32 capacity;
    /* The slot the last lookup found, tried first by the next: a draw
     * describes the bound texture once per polygon, and a table scan per
     * polygon cost Half-Life about a tenth of its frame (2026-10-01). It
     * is an index, so the table moving when it grows does not stale it,
     * and a hit is checked against in_use and the name every time. */
    v9x_u32 find_hint;
    GLuint next_name;
    /* The units, and the one glSelectTextureSGIS chose: binding, texture
     * images and parameters, the environment and TEXTURE_2D's enable are
     * all the selected unit's. */
    V9X_GL_TEXUNIT units[V9X_GL_TEXTURE_UNITS];
    v9x_u32 active;
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

/* glSelectTextureSGIS: TEXTURE0_SGIS or TEXTURE1_SGIS, INVALID_ENUM for
 * anything else. */
void v9x_gl_tex_select(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                       GLenum target);
/*
 * glEnable/glDisable/glIsEnabled of TEXTURE_2D with unit 1 selected, which
 * is unit 1's enable and not the state's capability. Non-zero when `cap`
 * was that and is done; zero leaves the call to the state.
 */
int v9x_gl_tex_enable_selected(V9X_GL_TEXTURES *textures, GLenum cap,
                               int enable);
int v9x_gl_tex_is_enabled_selected(const V9X_GL_TEXTURES *textures,
                                   GLenum cap, GLboolean *enabled);

/*
 * The render interface's texture for a draw from `unit`: NONE unless its
 * TEXTURE_2D is enabled and its bound object is complete (3.8.9);
 * otherwise CPU storage naming `levels`, which must have room for
 * V9X_GL_TEXTURE_LEVELS and stays valid until the next texture command.
 * v9x_gl_tex_describe is unit 0's.
 */
void v9x_gl_tex_describe_unit(const V9X_GL_STATE *state,
                              V9X_GL_TEXTURES *textures, v9x_u32 unit,
                              V9X_R3D_ABI_TEXTURE *out,
                              V9X_R3D_ABI_LEVEL *levels);
void v9x_gl_tex_describe(const V9X_GL_STATE *state,
                         V9X_GL_TEXTURES *textures,
                         V9X_R3D_ABI_TEXTURE *out,
                         V9X_R3D_ABI_LEVEL *levels);

/*
 * A described texture made to fit a device whose largest edge is
 * `size_max` (zero: no limit). Levels past it are dropped from the top,
 * which leaves a mipmapped chain complete; a single level past it is
 * replaced by a box-filtered copy kept on the bound object until its next
 * image. The Mach64 and the Rage IIC sample to 256 and have no software
 * fallback, so a 512 texture's draw was refused as invalid and not drawn.
 * `levels` is the array `texture` names; `unit` the unit it was described
 * from, whose bound object keeps the copy.
 */
void v9x_gl_tex_fit(V9X_GL_TEXTURES *textures, v9x_u32 unit,
                    V9X_R3D_ABI_TEXTURE *texture,
                    V9X_R3D_ABI_LEVEL *levels, v9x_u32 size_max);

/*
 * For a copy of `object` made at revision `copy_revision`: 1 with `rect`
 * the part of `level` to refill (empty for none), or 0 when the copy cannot
 * be brought up to date by rectangles and must refill whole - it predates
 * dirty_from, or `level` is past the chain. v9x_gl_tex_dirty_reset starts
 * the rectangles again from the current revision, once the one copy that
 * reads them is current.
 */
int v9x_gl_tex_dirty_rect(const V9X_GL_TEXOBJ *object, v9x_u32 copy_revision,
                          v9x_u32 level, V9X_GL_TEXRECT *rect);
void v9x_gl_tex_dirty_reset(V9X_GL_TEXOBJ *object);

/* The selected unit's bound object (never null: name 0 is the default
 * texture). */
V9X_GL_TEXOBJ *v9x_gl_tex_bound_object(V9X_GL_TEXTURES *textures);
/* The object named `name` (0 the default), or null when there is none. */
V9X_GL_TEXOBJ *v9x_gl_tex_object(V9X_GL_TEXTURES *textures, GLuint name);
/* Every object's hardware copy released through the hook and forgotten:
 * a mode change has made them all lost. */
void v9x_gl_textures_drop_hw(V9X_GL_TEXTURES *textures);
/*
 * When nothing reads the result's alpha, the alpha op may be whichever
 * the hardware has: REPLACE on a texture without alpha takes the texel's
 * one instead of the fragment's (Direct3D's DECAL), and REPLACE or
 * MODULATE on a texture with alpha take the texel's (DECAL, MODULATE)
 * instead of the product. The pixels written are identical.
 */
void v9x_gl_tex_fragment_alpha_unused(V9X_R3D_ABI_TEXTURE *texture);

/* An RGB565 texel as ARGB1555 with alpha one: red and blue keep their five
 * bits, green drops its lowest. For an engine that samples no 565 (the
 * ViRGE's S3D takes 1555 and 4444). */
v9x_u16 v9x_gl_tex_565_to_1555(v9x_u16 texel);

/*
 * The square copy for a sampler that takes only squares of side_min to
 * side_max (the Mach64's 8 to 256): side max(width, height, side_min), or 0
 * when that is over side_max. Level n of the copy is side >> n square,
 * filled from the image's level n (or its last, 1x1, once the image's chain
 * is shorter) by v9x_gl_tex_square_fill; s and t are then drawn scaled by
 * width / side and height / side.
 *
 * Wrap repeats the image across the square: the scaled coordinates reach
 * the same texel at every repeat, and a filter's neighbours across a repeat
 * are the image's own. Clamp repeats its last row and column instead, which
 * is what the clamped image gives past its edge. Both are exact at level 0;
 * a padded clamp level below it can average the edge a little wider.
 */
v9x_u32 v9x_gl_tex_square_side(v9x_u32 width, v9x_u32 height,
                               v9x_u32 side_min, v9x_u32 side_max);
void v9x_gl_tex_square_fill(const v9x_u16 *source, v9x_u32 source_pitch,
                            v9x_u32 width, v9x_u32 height, v9x_u16 *square,
                            v9x_u32 side, int clamp);
/*
 * A description of an RGB565 texture retargeted to the same images stored
 * as ARGB1555 with alpha one. The colour op is unchanged; the alpha op
 * becomes the one that gives the same result with a texel alpha of one:
 * REPLACE when nothing reads the fragment's alpha (`alpha_used` zero, so
 * any alpha is as good), MODULATE for the fragment's (one times it), and
 * REPLACE stays REPLACE (one either way).
 */
void v9x_gl_tex_as_1555(V9X_R3D_ABI_TEXTURE *texture, int alpha_used);

#endif /* VELOCITY9X_GL_TEXTURE_H */
