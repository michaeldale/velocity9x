/*
 * 3dfx VSA-100 2D engine: register values for solid fill and screen copy.
 *
 * Pure policy, no I/O. The caller writes what these builders return, every
 * dword in order, to BAR0 + V9X_TDFX_2D_BASE + offset. The last write is
 * always the command register with the initiate bit, which starts the
 * operation (Napalm spec section 7.2.2: "1 = start on this write"). Register
 * meanings, with Napalm citations: docs\specifications\vsa100-registers.md,
 * section 6.
 */
#ifndef VELOCITY9X_TDFX_2D_H
#define VELOCITY9X_TDFX_2D_H

#include "velocity9x/status.h"

/* The 2D register block in BAR0 (Napalm section 4). */
#define V9X_TDFX_2D_BASE            0x100000ul

/* 2D registers, offsets inside the block (Napalm section 7.1). */
#define V9X_TDFX_2D_STATUS          0x00ul
#define V9X_TDFX_2D_CLIP0_MIN       0x08ul
#define V9X_TDFX_2D_CLIP0_MAX       0x0cul
#define V9X_TDFX_2D_DST_BASE        0x10ul
#define V9X_TDFX_2D_DST_FORMAT      0x14ul
#define V9X_TDFX_2D_SRC_BASE        0x34ul
#define V9X_TDFX_2D_COMMAND_EXTRA   0x38ul
#define V9X_TDFX_2D_SRC_FORMAT      0x54ul
#define V9X_TDFX_2D_SRC_XY          0x5cul
#define V9X_TDFX_2D_COLOR_FORE      0x64ul
#define V9X_TDFX_2D_DST_SIZE        0x68ul
#define V9X_TDFX_2D_DST_XY          0x6cul
#define V9X_TDFX_2D_COMMAND         0x70ul

/* command (section 7.2.2): operation in 3:0, initiate in 8, X right to left
 * in 14, Y bottom to top in 15, ROP0 in 31:24. */
#define V9X_TDFX_CMD_NOP            0x0ul
#define V9X_TDFX_CMD_BLT            0x1ul
#define V9X_TDFX_CMD_RECT_FILL      0x5ul
#define V9X_TDFX_CMD_INITIATE       0x00000100ul
#define V9X_TDFX_CMD_X_RIGHT_TO_LEFT 0x00004000ul
#define V9X_TDFX_CMD_Y_BOTTOM_TO_TOP 0x00008000ul
#define V9X_TDFX_CMD_ROP_SHIFT      24
#define V9X_TDFX_ROP_SRCCOPY        0xccul

/* dstFormat / srcFormat bits 18:16 (destination) and 19:16 (source): the
 * pixel format code; 1 = 8 bpp, 3 = 16, 4 = 24, 5 = 32 (section 7.2.9). */
#define V9X_TDFX_FORMAT_SHIFT       16
#define V9X_TDFX_STRIDE_MAX         0x3ffful

/* Coordinates and sizes are 13-bit fields: x and width 12:0, y and height
 * 28:16 (sections 7.2.7-7.2.8). Base addresses are 26-bit (25:0). */
#define V9X_TDFX_COORD_MAX          0x1ffful
#define V9X_TDFX_BASE_MAX           0x03fffffful

/* clip0: min inclusive, max exclusive, x 11:0 and y 27:16 (section 7.2.10).
 * The builders open it to the whole 4095x4095 range; bounds are checked
 * here instead. */
#define V9X_TDFX_CLIP_OPEN_MAX      0x0fff0ffful

/*
 * The research notes' small-blit erratum: an operation 16 pixels or smaller
 * in either dimension can hang the chip unless a NOP goes first. Not in the
 * Napalm spec and not measured; the builders honour it by emitting a NOP
 * command ahead of such an operation, which costs one register write.
 */
#define V9X_TDFX_SMALL_BLT_PIXELS   16ul

/* Status (section 3): bit 9 is the aggregate busy bit. */
#define V9X_TDFX_2D_STATUS_BUSY     0x00000200ul

/* NOP, clip0 min and max, base, format, colour or source pair, size, XY,
 * command: at most 11 writes. */
#define V9X_TDFX_BLT_WRITES         12u

struct v9x_tdfx_blt {
    v9x_u32 offsets[V9X_TDFX_BLT_WRITES];
    v9x_u32 values[V9X_TDFX_BLT_WRITES];
    v9x_u32 count;
};

struct v9x_tdfx_fill {
    v9x_u32 vram_bytes;
    v9x_u32 target_offset;
    v9x_u32 pitch_bytes;
    v9x_u32 bytes_per_pixel;
    v9x_u32 left;
    v9x_u32 top;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 color;
    v9x_u32 rop;
};

struct v9x_tdfx_copy {
    v9x_u32 vram_bytes;
    v9x_u32 source_offset;
    v9x_u32 source_pitch_bytes;
    v9x_u32 destination_offset;
    v9x_u32 destination_pitch_bytes;
    v9x_u32 bytes_per_pixel;
    v9x_u32 source_left;
    v9x_u32 source_top;
    v9x_u32 destination_left;
    v9x_u32 destination_top;
    v9x_u32 width;
    v9x_u32 height;
};

/* Non-zero when either dimension is at or below the small-blit limit. */
v9x_u32 v9x_tdfx_small_blt(v9x_u32 width, v9x_u32 height);

/* V9X_STATUS_OK with the writes in blt; INVALID_ARGUMENT for a rectangle
 * that does not fit the surface or memory; UNSUPPORTED for a pixel size,
 * pitch or coordinate the engine cannot express. */
v9x_status v9x_tdfx_build_fill(const struct v9x_tdfx_fill *fill,
                               struct v9x_tdfx_blt *blt);
v9x_status v9x_tdfx_build_copy(const struct v9x_tdfx_copy *copy,
                               struct v9x_tdfx_blt *blt);

/* Non-zero while status says the engine is busy. */
v9x_u32 v9x_tdfx_status_busy(v9x_u32 status);

#endif
