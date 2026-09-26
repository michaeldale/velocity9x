/*
 * glReadPixels (4.3.2) as pure arithmetic: validate a request against the
 * GL state, then convert rows of a 16-bit colour buffer into the caller's
 * memory under the pack parameters. The ICD supplies the buffer, locked
 * after the render interface's finish.
 *
 * This slice reads the colour buffer in the unsigned-byte formats: RGBA,
 * RGB, RED, GREEN, BLUE, ALPHA, LUMINANCE and LUMINANCE_ALPHA. Depth,
 * stencil and colour-index reads and the other transfer types are legal
 * GL and are refused with INVALID_ENUM until they are converted.
 */
#ifndef VELOCITY9X_GL_PIXELS_H
#define VELOCITY9X_GL_PIXELS_H

#include "gl_texture.h"

typedef struct v9x_gl_read_plan {
    GLint x;
    GLint y;
    GLsizei width;
    GLsizei height;
    GLenum format;
    unsigned int components;
    v9x_u32 row_bytes;          /* the destination stride, aligned */
    v9x_u32 skip_rows;
    v9x_u32 skip_pixels;
} V9X_GL_READ_PLAN;

/* Non-zero when there is something to read; zero for an error (recorded)
 * or an empty rectangle. */
int v9x_gl_read_plan(V9X_GL_STATE *state, const V9X_GL_TEXTURES *textures,
                     GLint x, GLint y, GLsizei width, GLsizei height,
                     GLenum format, GLenum type, V9X_GL_READ_PLAN *plan);

/*
 * Convert. `surface` is the colour buffer's first row (surface row 0, the
 * top), `pitch` its stride in bytes, `width`/`height` its extent and
 * `target_format` V9X_GL_TARGET_RGB565 or _XRGB1555. Window row y is
 * surface row height - 1 - y. A pixel outside the buffer leaves its
 * destination bytes as they were (the specification leaves its value
 * undefined).
 */
void v9x_gl_read_convert(const V9X_GL_READ_PLAN *plan, const void *surface,
                         v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
                         v9x_u32 target_format, void *out);

#endif /* VELOCITY9X_GL_PIXELS_H */
