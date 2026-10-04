/*
 * SiS 6326 2D engine: register values for solid fill and screen copy.
 *
 * Pure policy, no I/O. The engine module writes what these builders return:
 * every dword in order, then the 16-bit command word at 82AAh, which starts
 * the engine (datasheet 7.8.1: "Word-Writing to Command 1 and Command 0, it
 * will automatically initiate graphics engine"). Register meanings, with
 * datasheet citations: docs\specifications\sis6326-registers.md section 5.
 */
#ifndef VELOCITY9X_SIS6326_ENGINE_H
#define VELOCITY9X_SIS6326_ENGINE_H

#include "velocity9x/status.h"

/* General-function register block, MMIO offsets (datasheet 7.8.1). */
#define V9X_SIS_2D_SRC_ADDR     0x8280ul
#define V9X_SIS_2D_DST_ADDR     0x8284ul
#define V9X_SIS_2D_PITCH        0x8288ul
#define V9X_SIS_2D_SIZE         0x828cul
#define V9X_SIS_2D_FG           0x8290ul
#define V9X_SIS_2D_BG           0x8294ul
#define V9X_SIS_2D_CMD_STATUS   0x82a8ul
/* The command word, written 16 bits wide. */
#define V9X_SIS_2D_COMMAND      0x82aaul

/* Command 0 (82AAh), the low byte of the command word. */
#define V9X_SIS_CMD0_CLIP_ENABLE  0x0040u
#define V9X_SIS_CMD0_Y_INCREASE   0x0020u
#define V9X_SIS_CMD0_X_INCREASE   0x0010u
#define V9X_SIS_CMD0_PATTERN_FG   0x0004u
#define V9X_SIS_CMD0_SOURCE_BG    0x0000u
#define V9X_SIS_CMD0_SOURCE_VRAM  0x0002u
/* Command 1 (82ABh), the high byte: command type 00, BitBlt. */
#define V9X_SIS_CMD1_BITBLT       0x0000u

/* Command 1 D6 read through the dword at 82A8h: engine busy or hardware
 * queue not empty. D7: hardware queue empty. */
#define V9X_SIS_STATUS_BUSY       0x40000000ul
#define V9X_SIS_STATUS_QUEUE_EMPTY 0x80000000ul

/* SRCCOPY, in the ROP byte of the foreground register. */
#define V9X_SIS_ROP_SRCCOPY       0xccul

/* Field limits: 22-bit addresses (datasheet 7.8.1; Rev. Ax/Bx addresses at
 * most 4 MiB), 12-bit pitches, and 12-bit sizes holding n - 1. */
#define V9X_SIS_2D_ADDRESS_LIMIT  0x00400000ul
#define V9X_SIS_2D_PITCH_MAX      0x00000ffful
#define V9X_SIS_2D_EXTENT_MAX     0x00001000ul

/* Dwords a fill or copy writes before its command word. */
#define V9X_SIS_BLT_DWORDS        6u

struct v9x_sis_blt {
    v9x_u32 offsets[V9X_SIS_BLT_DWORDS];
    v9x_u32 values[V9X_SIS_BLT_DWORDS];
    v9x_u32 count;
    v9x_u16 command;
};

struct v9x_sis_fill {
    v9x_u32 vram_bytes;
    v9x_u32 target_offset;
    v9x_u32 pitch_bytes;
    v9x_u32 bytes_per_pixel;
    v9x_u32 left;
    v9x_u32 top;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 color;
};

struct v9x_sis_copy {
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

v9x_status v9x_sis_build_fill(const struct v9x_sis_fill *fill,
                              struct v9x_sis_blt *blt);
v9x_status v9x_sis_build_copy(const struct v9x_sis_copy *copy,
                              struct v9x_sis_blt *blt);
v9x_u32 v9x_sis_status_busy(v9x_u32 command_status);

#endif
