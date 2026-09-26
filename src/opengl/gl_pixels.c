/*
 * glReadPixels (OpenGL 1.1 section 4.3.2) for the colour buffer in the
 * unsigned-byte formats. The plan validates and fixes the destination
 * layout from the pack parameters; the conversion flips window rows onto
 * surface rows, expands 565 or 555 to eight bits by bit replication and
 * writes the requested components.
 */
#include "gl_pixels.h"

#define GL_PIXELS_RED               0x1903u
#define GL_PIXELS_GREEN             0x1904u
#define GL_PIXELS_BLUE              0x1905u
#define GL_PIXELS_LUMINANCE_ALPHA   0x190Au

/* Indices into V9X_GL_TEXTURES.pack, pname - PACK_SWAP_BYTES (0x0D00). */
#define GL_PIXELS_PACK_ROW_LENGTH   2u
#define GL_PIXELS_PACK_SKIP_ROWS    3u
#define GL_PIXELS_PACK_SKIP_PIXELS  4u
#define GL_PIXELS_PACK_ALIGNMENT    5u

/* Components written per pixel for a format this slice reads, or zero.
 * COLOR_INDEX, STENCIL_INDEX and DEPTH_COMPONENT are legal GL but are not
 * converted yet, so they report zero with the unknown names. */
static unsigned int gl_pixels_components(GLenum format)
{
    if (format == GL_PIXELS_RED || format == GL_PIXELS_GREEN ||
        format == GL_PIXELS_BLUE || format == V9X_GL_ALPHA ||
        format == V9X_GL_LUMINANCE) {
        return 1u;
    }
    if (format == GL_PIXELS_LUMINANCE_ALPHA) {
        return 2u;
    }
    if (format == V9X_GL_RGB) {
        return 3u;
    }
    if (format == V9X_GL_RGBA) {
        return 4u;
    }
    return 0u;
}

int v9x_gl_read_plan(V9X_GL_STATE *state, const V9X_GL_TEXTURES *textures,
                     GLint x, GLint y, GLsizei width, GLsizei height,
                     GLenum format, GLenum type, V9X_GL_READ_PLAN *plan)
{
    unsigned int components;
    v9x_u32 row_pixels;
    v9x_u32 row_bytes;
    v9x_u32 alignment;

    if (state->in_begin) {
        v9x_gl_state_error(state, V9X_GL_INVALID_OPERATION);
        return 0;
    }
    if (width < 0 || height < 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return 0;
    }
    components = gl_pixels_components(format);
    if (components == 0u || type != V9X_GL_UNSIGNED_BYTE) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return 0;
    }
    if (width == 0 || height == 0) {
        return 0;
    }

    /* 4.3.2 packs as 3.6.3 unpacks: a row is ROW_LENGTH pixels when that
     * is positive, else the width, rounded up to the alignment. The
     * element here is one byte, which is always smaller than any
     * alignment above 1, so the rounding always applies. glPixelStore
     * has already limited the alignment to 1, 2, 4 or 8. */
    row_pixels = textures->pack[GL_PIXELS_PACK_ROW_LENGTH] > 0
        ? (v9x_u32)textures->pack[GL_PIXELS_PACK_ROW_LENGTH]
        : (v9x_u32)width;
    alignment = (v9x_u32)textures->pack[GL_PIXELS_PACK_ALIGNMENT];
    row_bytes = row_pixels * components;
    row_bytes = (row_bytes + alignment - 1ul) / alignment * alignment;

    plan->x = x;
    plan->y = y;
    plan->width = width;
    plan->height = height;
    plan->format = format;
    plan->components = components;
    plan->row_bytes = row_bytes;
    plan->skip_rows = (v9x_u32)textures->pack[GL_PIXELS_PACK_SKIP_ROWS];
    plan->skip_pixels = (v9x_u32)textures->pack[GL_PIXELS_PACK_SKIP_PIXELS];
    return 1;
}

/* Surface pixel to 8-bit channels. Bit replication maps full scale to
 * 255 and zero to zero, which a shift alone would not. There is no alpha
 * plane in any Velocity9x pixel format, so alpha reads as one (255). */
static void gl_pixels_expand(v9x_u16 pixel, v9x_u32 target_format,
                             unsigned int *red, unsigned int *green,
                             unsigned int *blue)
{
    unsigned int r;
    unsigned int g;
    unsigned int b;

    if (target_format == V9X_GL_TARGET_XRGB1555) {
        r = (pixel >> 10) & 0x1Fu;
        g = (pixel >> 5) & 0x1Fu;
        b = pixel & 0x1Fu;
        *red = (r << 3) | (r >> 2);
        *green = (g << 3) | (g >> 2);
        *blue = (b << 3) | (b >> 2);
        return;
    }
    r = (pixel >> 11) & 0x1Fu;
    g = (pixel >> 5) & 0x3Fu;
    b = pixel & 0x1Fu;
    *red = (r << 3) | (r >> 2);
    *green = (g << 2) | (g >> 4);
    *blue = (b << 3) | (b >> 2);
}

void v9x_gl_read_convert(const V9X_GL_READ_PLAN *plan, const void *surface,
                         v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
                         v9x_u32 target_format, void *out)
{
    const v9x_u8 *rows;
    v9x_u8 *dest_row;
    v9x_u8 *dest;
    const v9x_u16 *source;
    GLint row;
    GLint column;
    GLint window_x;
    GLint window_y;
    unsigned int red;
    unsigned int green;
    unsigned int blue;
    unsigned int luminance;

    rows = (const v9x_u8 *)surface;

    /* A caller's buffer for a valid request holds (skip_rows + height)
     * rows of row_bytes, so these offsets cannot wrap for one. */
    dest_row = (v9x_u8 *)out + plan->skip_rows * plan->row_bytes +
               plan->skip_pixels * plan->components;
    for (row = 0; row < plan->height; ++row, dest_row += plan->row_bytes) {
        window_y = plan->y + row;
        if (window_y < 0 || (v9x_u32)window_y >= height) {
            continue;
        }

        /* Window y counts up from the bottom; surface rows count down. */
        source = (const v9x_u16 *)(rows +
                 (height - 1ul - (v9x_u32)window_y) * pitch);
        for (column = 0; column < plan->width; ++column) {
            window_x = plan->x + column;
            if (window_x < 0 || (v9x_u32)window_x >= width) {
                continue;
            }
            gl_pixels_expand(source[window_x], target_format,
                             &red, &green, &blue);
            dest = dest_row + (v9x_u32)column * plan->components;

            if (plan->format == V9X_GL_RGBA) {
                dest[0] = (v9x_u8)red;
                dest[1] = (v9x_u8)green;
                dest[2] = (v9x_u8)blue;
                dest[3] = 255u;
            } else if (plan->format == V9X_GL_RGB) {
                dest[0] = (v9x_u8)red;
                dest[1] = (v9x_u8)green;
                dest[2] = (v9x_u8)blue;
            } else if (plan->format == GL_PIXELS_RED) {
                dest[0] = (v9x_u8)red;
            } else if (plan->format == GL_PIXELS_GREEN) {
                dest[0] = (v9x_u8)green;
            } else if (plan->format == GL_PIXELS_BLUE) {
                dest[0] = (v9x_u8)blue;
            } else if (plan->format == V9X_GL_ALPHA) {
                dest[0] = 255u;
            } else {
                /* 4.3.2: luminance is R + G + B, clamped to one. */
                luminance = red + green + blue;
                if (luminance > 255u) {
                    luminance = 255u;
                }
                dest[0] = (v9x_u8)luminance;
                if (plan->format == GL_PIXELS_LUMINANCE_ALPHA) {
                    dest[1] = 255u;
                }
            }
        }
    }
}
