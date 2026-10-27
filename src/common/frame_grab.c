/*
 * Frame grabs, the arithmetic. See include\velocity9x\frame_grab.h.
 */
#include "velocity9x/frame_grab.h"

v9x_u16 v9x_frame_grab_plan(v9x_u32 offset, v9x_u32 width, v9x_u32 height,
                            v9x_u32 pitch, v9x_u32 bits_per_pixel,
                            v9x_u32 vram_bytes, v9x_u32 red_mask,
                            v9x_u32 green_mask, v9x_u32 blue_mask,
                            struct v9x_frame_grab_plan *plan)
{
    v9x_u32 bytes_per_pixel;
    v9x_u32 row_used;
    v9x_u32 row_bytes;
    v9x_u32 last_row;

    if (plan == 0 || width == 0ul || height == 0ul || pitch == 0ul ||
        width > V9X_FRAME_GRAB_WIDTH_MAX || red_mask == 0ul ||
        green_mask == 0ul || blue_mask == 0ul) {
        return V9X_FALSE;
    }
    if (bits_per_pixel == 16ul) {
        bytes_per_pixel = 2ul;
    } else if (bits_per_pixel == 32ul) {
        bytes_per_pixel = 4ul;
    } else {
        return V9X_FALSE;
    }
    /* width is at most 2048, so this cannot overflow. */
    row_used = width * bytes_per_pixel;
    if (row_used > pitch) {
        return V9X_FALSE;
    }
    /* The last row starts at offset + (height - 1) * pitch. Each step is
     * checked against vram_bytes before it is formed. */
    if (offset > vram_bytes || row_used > vram_bytes - offset) {
        return V9X_FALSE;
    }
    if (height - 1ul > (vram_bytes - offset - row_used) / pitch) {
        return V9X_FALSE;
    }
    last_row = offset + (height - 1ul) * pitch;
    if (last_row + row_used > vram_bytes) {
        return V9X_FALSE;
    }
    row_bytes = (width * 3ul + 3ul) & ~3ul;
    /* A BMP's size is a DWORD; 2048 wide by this height must fit. */
    if (height > (0xfffffffful - V9X_FRAME_GRAB_BMP_HEADER_BYTES) / row_bytes) {
        return V9X_FALSE;
    }

    plan->offset = offset;
    plan->width = width;
    plan->height = height;
    plan->pitch = pitch;
    plan->bytes_per_pixel = bytes_per_pixel;
    plan->red_mask = red_mask;
    plan->green_mask = green_mask;
    plan->blue_mask = blue_mask;
    plan->row_bytes = row_bytes;
    plan->file_bytes = V9X_FRAME_GRAB_BMP_HEADER_BYTES + height * row_bytes;
    return V9X_TRUE;
}

v9x_u8 v9x_frame_grab_channel(v9x_u32 pixel, v9x_u32 mask)
{
    v9x_u32 value;
    v9x_u32 bits = 0ul;
    v9x_u32 widened;

    if (mask == 0ul) {
        return 0u;
    }
    while ((mask & 1ul) == 0ul) {
        mask >>= 1;
        pixel >>= 1;
    }
    value = pixel & mask;
    while ((mask >> bits) != 0ul) {
        ++bits;
    }
    if (bits >= 8ul) {
        return (v9x_u8)(value >> (bits - 8ul));
    }
    /* Replicate the top bits into the low ones: 5 bits abcde become
     * abcdeabc. */
    widened = value << (8ul - bits);
    while (bits < 8ul) {
        widened |= widened >> bits;
        bits *= 2ul;
    }
    return (v9x_u8)(widened & 0xfful);
}

static void v9x_frame_grab_put32(v9x_u8 *out, v9x_u32 value)
{
    out[0] = (v9x_u8)(value & 0xfful);
    out[1] = (v9x_u8)((value >> 8) & 0xfful);
    out[2] = (v9x_u8)((value >> 16) & 0xfful);
    out[3] = (v9x_u8)((value >> 24) & 0xfful);
}

void v9x_frame_grab_bmp_header(const struct v9x_frame_grab_plan *plan,
                               v9x_u8 *out)
{
    v9x_u32 index;

    for (index = 0ul; index < V9X_FRAME_GRAB_BMP_HEADER_BYTES; ++index) {
        out[index] = 0u;
    }
    out[0] = (v9x_u8)'B';
    out[1] = (v9x_u8)'M';
    v9x_frame_grab_put32(out + 2, plan->file_bytes);
    v9x_frame_grab_put32(out + 10, V9X_FRAME_GRAB_BMP_HEADER_BYTES);
    v9x_frame_grab_put32(out + 14, 40ul);           /* biSize          */
    v9x_frame_grab_put32(out + 18, plan->width);
    v9x_frame_grab_put32(out + 22, plan->height);   /* positive: bottom-up */
    out[26] = 1u;                                   /* biPlanes        */
    out[28] = 24u;                                  /* biBitCount      */
    v9x_frame_grab_put32(out + 34, plan->height * plan->row_bytes);
}

void v9x_frame_grab_row(const struct v9x_frame_grab_plan *plan,
                        const v9x_u8 *line, v9x_u8 *out)
{
    v9x_u32 x;
    v9x_u32 at = 0ul;

    for (x = 0ul; x < plan->width; ++x) {
        const v9x_u8 *p = line + x * plan->bytes_per_pixel;
        v9x_u32 pixel = (v9x_u32)p[0] | ((v9x_u32)p[1] << 8);

        if (plan->bytes_per_pixel == 4ul) {
            pixel |= ((v9x_u32)p[2] << 16) | ((v9x_u32)p[3] << 24);
        }
        out[at++] = v9x_frame_grab_channel(pixel, plan->blue_mask);
        out[at++] = v9x_frame_grab_channel(pixel, plan->green_mask);
        out[at++] = v9x_frame_grab_channel(pixel, plan->red_mask);
    }
    while (at < plan->row_bytes) {
        out[at++] = 0u;
    }
}

v9x_u16 v9x_frame_grab_due(v9x_u32 *countdown, v9x_u32 interval,
                           v9x_u32 remaining)
{
    if (countdown == 0 || remaining == 0ul) {
        return V9X_FALSE;
    }
    if (interval == 0ul) {
        interval = 1ul;
    }
    if (*countdown > 1ul) {
        --*countdown;
        return V9X_FALSE;
    }
    *countdown = interval;
    return V9X_TRUE;
}
