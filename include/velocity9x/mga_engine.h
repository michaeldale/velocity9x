/*
 * Matrox MGA-2064W drawing engine: register values for solid fill and
 * screen copy.
 *
 * Pure policy, no I/O. The engine module writes what these builders return,
 * every dword in order; the last write of a fill or copy goes to its
 * register's mirror at +100h, which starts the engine (MGA-1064SG Developer
 * Specification, the 2064W's 2D core, Table 3-4 note 6 and sections 5.5.5,
 * 5.5.6: "the last register you program must be accessed in the
 * 1D00h-1DFFh range"). Register meanings and citations:
 * docs\specifications\mga2064w-2d-engine.md.
 *
 * Offsets are from MGABASE1, the 16 KiB control aperture (BAR0 on this
 * chip).
 */
#ifndef VELOCITY9X_MGA_ENGINE_H
#define VELOCITY9X_MGA_ENGINE_H

#include "velocity9x/status.h"

#define V9X_MGA_DWGCTL      0x1c00ul
#define V9X_MGA_MACCESS     0x1c04ul
#define V9X_MGA_PLNWT       0x1c1cul
#define V9X_MGA_FCOL        0x1c24ul
#define V9X_MGA_SGN         0x1c58ul
#define V9X_MGA_AR0         0x1c60ul
#define V9X_MGA_AR3         0x1c6cul
#define V9X_MGA_AR5         0x1c74ul
#define V9X_MGA_CXBNDRY     0x1c80ul
#define V9X_MGA_FXBNDRY     0x1c84ul
#define V9X_MGA_YDSTLEN     0x1c88ul
#define V9X_MGA_PITCH       0x1c8cul
#define V9X_MGA_YDSTORG     0x1c94ul
#define V9X_MGA_YTOP        0x1c98ul
#define V9X_MGA_YBOT        0x1c9cul
#define V9X_MGA_FIFOSTATUS  0x1e10ul
#define V9X_MGA_STATUS      0x1e14ul

/* Writing a drawing register at this offset above itself starts the
 * engine (Table 3-4: 1D00h-1DFFh mirror 1C00h-1CFCh). */
#define V9X_MGA_GO          0x0100ul

/* FIFOSTATUS fifocount<5:0>: free slots in the 32-entry BFIFO (p.4-57,
 * section 5.1.1). STATUS dwgengsts<16>: the engine, its FIFO or the memory
 * controller still busy (p.4-74). */
#define V9X_MGA_FIFO_COUNT_MASK  0x0000003ful
#define V9X_MGA_FIFO_DEPTH       32ul
#define V9X_MGA_STATUS_BUSY      0x00010000ul

/* The most writes any builder emits. */
#define V9X_MGA_MAX_WRITES 10u

struct v9x_mga_writes {
    v9x_u32 offsets[V9X_MGA_MAX_WRITES];
    v9x_u32 values[V9X_MGA_MAX_WRITES];
    v9x_u32 count;
};

struct v9x_mga_fill {
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

struct v9x_mga_copy {
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

/* The per-mode state: pixel width, plane mask, a clip window wide open. */
v9x_status v9x_mga_build_setup(v9x_u32 bytes_per_pixel,
                               struct v9x_mga_writes *writes);
v9x_status v9x_mga_build_fill(const struct v9x_mga_fill *fill,
                              struct v9x_mga_writes *writes);
v9x_status v9x_mga_build_copy(const struct v9x_mga_copy *copy,
                              struct v9x_mga_writes *writes);
v9x_u32 v9x_mga_status_busy(v9x_u32 status);
v9x_u32 v9x_mga_fifo_free(v9x_u32 fifostatus);

#endif
