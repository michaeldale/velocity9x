/*
 * The Gen3 hardware cursor: the shape, the position and the registers'
 * values, as pure C (src\chipsets\intel\i9xx_cursor.c). The 16-bit
 * driver's cursor16.c writes what this computes.
 * docs\plans\gen3-hardware-cursor.md.
 *
 * Registers and modes are Linux's (drivers\gpu\drm\i915\display\
 * intel_cursor.c, intel_cursor_regs.h, i9xx_plane_regs.h): a 64x64 ARGB
 * cursor per pipe, CURxCNTR then CURxPOS then CURxBASE, the BASE write
 * arming the update.
 */
#ifndef VELOCITY9X_I9XX_CURSOR_H
#define VELOCITY9X_I9XX_CURSOR_H

#include "velocity9x/intel_gma.h"

/* The one mode used: 64x64, 32 bits a pixel, alpha blended
 * (MCURSOR_MODE_64_ARGB_AX). Square, as every pre-IVB cursor must be. */
#define V9X_I9XX_CURSOR_SIZE           64ul
#define V9X_I9XX_CURSOR_PITCH          (V9X_I9XX_CURSOR_SIZE * 4ul)
#define V9X_I9XX_CURSOR_PAGES          4ul
#define V9X_I9XX_CURSOR_MODE_64_ARGB   ((v9x_u32)0x00000027ul)
/* MCURSOR_PIPE_SEL, bits 29:28, used before G4X. */
#define V9X_I9XX_CURSOR_PIPE_SHIFT     28u

/* Cursor A and B: control, base, position (CURSOR_A_OFFSET 0x70080,
 * CURSOR_B_OFFSET 0x700c0). */
#define V9X_I9XX_REG_CURACNTR          ((v9x_u32)0x00070080ul)
#define V9X_I9XX_REG_CURABASE          ((v9x_u32)0x00070084ul)
#define V9X_I9XX_REG_CURAPOS           ((v9x_u32)0x00070088ul)
#define V9X_I9XX_REG_CURBCNTR          ((v9x_u32)0x000700c0ul)
#define V9X_I9XX_REG_CURBBASE          ((v9x_u32)0x000700c4ul)
#define V9X_I9XX_REG_CURBPOS           ((v9x_u32)0x000700c8ul)
/* The primary planes' control: DISP_ENABLE bit 31, DISP_PIPE_SEL 25:24. */
#define V9X_I9XX_REG_DSPACNTR          ((v9x_u32)0x00070180ul)
#define V9X_I9XX_REG_DSPBCNTR          ((v9x_u32)0x00071180ul)
#define V9X_I9XX_PLANE_ENABLE          ((v9x_u32)0x80000000ul)
#define V9X_I9XX_PLANE_PIPE_SHIFT      24u
#define V9X_I9XX_PLANE_PIPE_MASK       ((v9x_u32)0x00000003ul)

/* CURxPOS: sign and magnitude, x in 14:0 with its sign at 15, y in 30:16
 * with its sign at 31 (CURSOR_POS_*). */
#define V9X_I9XX_CURSOR_POS_X_SIGN     ((v9x_u32)0x00008000ul)
#define V9X_I9XX_CURSOR_POS_Y_SIGN     ((v9x_u32)0x80000000ul)
#define V9X_I9XX_CURSOR_POS_MASK       ((v9x_u32)0x00007ffful)

/* The largest AND or XOR mask the driver keeps a copy of: 64 rows of 8
 * bytes, a 64x64 monochrome cursor. Windows 98's are 32x32. */
#define V9X_I9XX_CURSOR_MASK_BYTES     512u

/* Pixels in the ARGB image. Transparent is zero in every channel, so the
 * value is the same premultiplied or not. */
#define V9X_I9XX_CURSOR_TRANSPARENT    ((v9x_u32)0x00000000ul)
#define V9X_I9XX_CURSOR_BLACK          ((v9x_u32)0xff000000ul)
#define V9X_I9XX_CURSOR_WHITE          ((v9x_u32)0xfffffffful)

/*
 * A Windows cursor shape: the CURSORSHAPE header GDI hands the driver's
 * SetCursor (Win16 windows.h, tagCURSORSHAPE: xHotspot, yHotspot, cx, cy,
 * cbWidth, Planes, BitsPixel), and the AND mask then the XOR mask that
 * follow it, cbWidth bytes a row, top row first, most significant bit
 * leftmost.
 */
struct v9x_i9xx_cursor_shape {
    v9x_u16 hot_x;
    v9x_u16 hot_y;
    v9x_u16 width;
    v9x_u16 height;
    v9x_u16 width_bytes;
    v9x_u8 planes;
    v9x_u8 bits_pixel;
    const v9x_u8 *and_mask;
    const v9x_u8 *xor_mask;
};

/*
 * Whether the hardware can show this shape exactly. OK for a monochrome
 * shape up to 64x64 whose masks fit V9X_I9XX_CURSOR_MASK_BYTES.
 * UNSUPPORTED for a colour shape, and for one with any pixel whose AND and
 * XOR bits are both set: that pixel inverts the screen, which an ARGB
 * cursor cannot do (Windows' text I-beam is such a shape). INVALID_ARGUMENT
 * for an empty or malformed one. The driver leaves an unsupported shape to
 * the DIB Engine's software cursor.
 */
v9x_status v9x_i9xx_cursor_check(const struct v9x_i9xx_cursor_shape *shape);

/*
 * Row `row` of the 64x64 ARGB image, V9X_I9XX_CURSOR_SIZE pixels into
 * `pixels`. AND 1 XOR 0 is transparent, AND 0 XOR 0 black, AND 0 XOR 1
 * white; everything past the shape is transparent. The shape must have
 * passed v9x_i9xx_cursor_check.
 */
void v9x_i9xx_cursor_row(const struct v9x_i9xx_cursor_shape *shape,
                         v9x_u32 row, v9x_u32 *pixels);

/* CURxPOS for the image's top-left corner at (x, y), which may be off the
 * screen to the left or above it. */
v9x_u32 v9x_i9xx_cursor_position(v9x_s32 x, v9x_s32 y);

/* CURxCNTR showing the 64x64 ARGB image on `pipe` (0 or 1). Pipe gamma is
 * off: in an 8-bpp mode the pipe's palette would recolour the image. */
v9x_u32 v9x_i9xx_cursor_control(v9x_u32 pipe);

/*
 * Which pipes show the desktop: bit 0 for pipe A, bit 1 for pipe B, from
 * the two primary planes' control registers. Each enabled plane names its
 * pipe; the cursor goes on every pipe a plane scans out on, cursor A on
 * pipe A and cursor B on pipe B, as i915 pairs them. 0 when no plane is
 * enabled.
 */
v9x_u16 v9x_i9xx_cursor_pipes(v9x_u32 plane_a_control,
                              v9x_u32 plane_b_control);

/*
 * The value CURxBASE takes for an image at graphics address
 * `graphics_offset` on this device, given the image's four GTT entries.
 *
 * The 915G, 915GM, 945G and 945GM read the cursor from a PHYSICAL address
 * (i915's cursor_needs_physical); the image must then sit in four
 * physically contiguous, present pages below 4 GiB, and the base is the
 * first one's. Pineview and G33 take the graphics address. UNSUPPORTED for
 * a device this does not know, INVALID_ARGUMENT for an unaligned offset,
 * INSUFFICIENT_MEMORY for pages that are absent or not contiguous.
 */
v9x_status v9x_i9xx_cursor_base(v9x_u16 device_id, v9x_u32 graphics_offset,
                                const v9x_u32 *entries, v9x_u32 *base);

#endif /* VELOCITY9X_I9XX_CURSOR_H */
