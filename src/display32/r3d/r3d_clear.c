/* CPU clear leaf for the neutral render core. */
#include "r3d.h"

static int v9x_r3d_clear_valid(const V9X_R3D_CLEAR *clear)
{
    v9x_u32 i;
    if (clear == 0 || clear->width == 0ul || clear->height == 0ul ||
        clear->width > 32767ul || clear->height > 32767ul) return 0;
    if (clear->clear_color != 0ul) {
        v9x_u32 bytes = clear->format == V9X_R3D_FORMAT_XRGB8888 ? 4ul : 2ul;
        if (clear->color == 0 || clear->color_pitch < clear->width * bytes ||
            (clear->format != V9X_R3D_FORMAT_RGB565 &&
             clear->format != V9X_R3D_FORMAT_XRGB1555 &&
             clear->format != V9X_R3D_FORMAT_XRGB8888)) return 0;
    }
    if (clear->clear_depth != 0ul) {
        v9x_u32 bytes = clear->depth_bytes == 4ul ? 4ul : 2ul;
        if (clear->depth == 0 || clear->depth_pitch < clear->width * bytes ||
            (clear->depth_bytes != 0ul && clear->depth_bytes != 2ul &&
             clear->depth_bytes != 4ul)) return 0;
    }
    if (clear->rect_count != 0ul && clear->rects == 0) return 0;
    for (i = 0ul; i < clear->rect_count; ++i) {
        const V9X_R3D_CLEAR_RECT *r = &clear->rects[i];
        if (r->left > r->right || r->top > r->bottom ||
            r->right > clear->width || r->bottom > clear->height) return 0;
    }
    return 1;
}

int v9x_r3d_clear(const V9X_R3D_CLEAR *clear)
{
    v9x_u32 i;
    v9x_u32 packed = 0ul, mask = 0ul;
    if (!v9x_r3d_clear_valid(clear)) return 0;
    if (clear->format == V9X_R3D_FORMAT_XRGB8888) {
        /* D3DCOLOR's own layout. All three channels written clears the X
         * byte too, as the rasterizer's full store does; a mask keeps it. */
        packed = clear->color_value & 0x00fffffful;
        if (clear->write_red) mask |= 0x00ff0000ul;
        if (clear->write_green) mask |= 0x0000ff00ul;
        if (clear->write_blue) mask |= 0x000000fful;
        if (mask == 0x00fffffful) mask = 0xfffffffful;
    } else if (clear->format == V9X_R3D_FORMAT_RGB565) {
        packed = ((clear->color_value >> 8) & 0xf800ul) |
                 ((clear->color_value >> 5) & 0x07e0ul) |
                 ((clear->color_value >> 3) & 0x001ful);
        if (clear->write_red) mask |= 0xf800ul;
        if (clear->write_green) mask |= 0x07e0ul;
        if (clear->write_blue) mask |= 0x001ful;
    } else {
        packed = ((clear->color_value >> 9) & 0x7c00ul) |
                 ((clear->color_value >> 6) & 0x03e0ul) |
                 ((clear->color_value >> 3) & 0x001ful);
        if (clear->write_red) mask |= 0x7c00ul;
        if (clear->write_green) mask |= 0x03e0ul;
        if (clear->write_blue) mask |= 0x001ful;
    }
    for (i = 0ul; i < clear->rect_count; ++i) {
        const V9X_R3D_CLEAR_RECT *r = &clear->rects[i];
        v9x_u32 y;
        for (y = r->top; y < r->bottom; ++y) {
            v9x_u8 *row = clear->clear_color ?
                (v9x_u8 *)clear->color + y * clear->color_pitch : 0;
            v9x_u16 *colors = row != 0 &&
                clear->format != V9X_R3D_FORMAT_XRGB8888 ? (v9x_u16 *)row : 0;
            v9x_u32 *colors32 = row != 0 &&
                clear->format == V9X_R3D_FORMAT_XRGB8888 ? (v9x_u32 *)row : 0;
            v9x_u8 *zrow = clear->clear_depth ?
                (v9x_u8 *)clear->depth + y * clear->depth_pitch : 0;
            v9x_u16 *depths = zrow != 0 && clear->depth_bytes != 4ul
                ? (v9x_u16 *)zrow : 0;
            v9x_u32 *depths32 = zrow != 0 && clear->depth_bytes == 4ul
                ? (v9x_u32 *)zrow : 0;
            v9x_u32 x;
            for (x = r->left; x < r->right; ++x) {
                if (colors != 0) colors[x] =
                    (v9x_u16)((colors[x] & ~mask) | (packed & mask));
                if (colors32 != 0) colors32[x] =
                    (colors32[x] & ~mask) | (packed & mask);
                if (depths != 0 && clear->write_depth != 0ul)
                    depths[x] = (v9x_u16)clear->depth_value;
                if (depths32 != 0 && clear->write_depth != 0ul)
                    depths32[x] = clear->depth_value;
            }
        }
    }
    return 1;
}
