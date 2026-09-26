/* CPU clear leaf for the neutral render core. */
#include "r3d.h"

static int v9x_r3d_clear_valid(const V9X_R3D_CLEAR *clear)
{
    v9x_u32 i;
    if (clear == 0 || clear->width == 0ul || clear->height == 0ul ||
        clear->width > 32767ul || clear->height > 32767ul) return 0;
    if (clear->clear_color != 0ul) {
        if (clear->color == 0 || clear->color_pitch < clear->width * 2ul ||
            (clear->format != V9X_R3D_FORMAT_RGB565 &&
             clear->format != V9X_R3D_FORMAT_XRGB1555)) return 0;
    }
    if (clear->clear_depth != 0ul &&
        (clear->depth == 0 || clear->depth_pitch < clear->width * 2ul))
        return 0;
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
    v9x_u16 packed = 0u, mask = 0u;
    if (!v9x_r3d_clear_valid(clear)) return 0;
    if (clear->format == V9X_R3D_FORMAT_RGB565) {
        packed = (v9x_u16)(((clear->color_value >> 8) & 0xf800ul) |
                           ((clear->color_value >> 5) & 0x07e0ul) |
                           ((clear->color_value >> 3) & 0x001ful));
        if (clear->write_red) mask |= 0xf800u;
        if (clear->write_green) mask |= 0x07e0u;
        if (clear->write_blue) mask |= 0x001fu;
    } else {
        packed = (v9x_u16)(((clear->color_value >> 9) & 0x7c00ul) |
                           ((clear->color_value >> 6) & 0x03e0ul) |
                           ((clear->color_value >> 3) & 0x001ful));
        if (clear->write_red) mask |= 0x7c00u;
        if (clear->write_green) mask |= 0x03e0u;
        if (clear->write_blue) mask |= 0x001fu;
    }
    for (i = 0ul; i < clear->rect_count; ++i) {
        const V9X_R3D_CLEAR_RECT *r = &clear->rects[i];
        v9x_u32 y;
        for (y = r->top; y < r->bottom; ++y) {
            v9x_u16 *colors = clear->clear_color ?
                (v9x_u16 *)((v9x_u8 *)clear->color + y * clear->color_pitch) : 0;
            v9x_u16 *depths = clear->clear_depth ?
                (v9x_u16 *)((v9x_u8 *)clear->depth + y * clear->depth_pitch) : 0;
            v9x_u32 x;
            for (x = r->left; x < r->right; ++x) {
                if (colors != 0) colors[x] =
                    (v9x_u16)((colors[x] & (v9x_u16)~mask) | (packed & mask));
                if (depths != 0 && clear->write_depth != 0ul)
                    depths[x] = (v9x_u16)clear->depth_value;
            }
        }
    }
    return 1;
}
