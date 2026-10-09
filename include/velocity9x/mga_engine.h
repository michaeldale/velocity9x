/*
 * Matrox MGA-2064W drawing engine: register values for solid fill, screen
 * copy and monochrome expansion (text).
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
#define V9X_MGA_BCOL        0x1c20ul
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
#define V9X_MGA_OPMODE      0x1e54ul

/* DMAWIN, MGABASE1 + 0000h-1BFFh: the 7 KiB pseudo-DMA window an ILOAD's
 * data is written to (Table 3-4). Its addresses are not decoded during an
 * ILOAD (section 5.5.7), so a long transfer restarts at offset 0 every
 * window's worth, as FreeBE's PutMonoImage does. */
#define V9X_MGA_DMAWIN_BYTES 0x1c00ul

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
#define V9X_MGA_MAX_WRITES 12u

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

/*
 * A monochrome bitmap expanded onto the screen (ILOAD, BMONOWF): the
 * DIB Engine's string bitmap for text. The bitmap is `width` pixels by
 * `height` rows with no padding between rows, so the engine takes it as one
 * linear stream; a set bit draws the foreground, a clear bit the background
 * unless `transparent`. Only columns clip_left..clip_right (inclusive) are
 * drawn. That narrows CXBNDRY, which the caller must open again afterwards
 * by writing the setup, since the HAL writes it only once per mode.
 */
struct v9x_mga_expand {
    v9x_u32 vram_bytes;
    v9x_u32 target_offset;
    v9x_u32 pitch_bytes;
    v9x_u32 bytes_per_pixel;
    v9x_u32 left;
    v9x_u32 top;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 clip_left;
    v9x_u32 clip_right;
    v9x_u32 foreground;
    v9x_u32 background;
    v9x_u32 transparent;
};

/* The per-mode state: pixel width, plane mask, a clip window wide open. */
v9x_status v9x_mga_build_setup(v9x_u32 bytes_per_pixel,
                               struct v9x_mga_writes *writes);
v9x_status v9x_mga_build_fill(const struct v9x_mga_fill *fill,
                              struct v9x_mga_writes *writes);
v9x_status v9x_mga_build_copy(const struct v9x_mga_copy *copy,
                              struct v9x_mga_writes *writes);
v9x_status v9x_mga_build_expand(const struct v9x_mga_expand *expand,
                                struct v9x_mga_writes *writes);
/* The dwords an expansion's data occupies in DMAWIN, padded at the end:
 * exactly this many must follow the start, or the engine waits for ever
 * (fewer) or reads the rest as register writes (more). */
v9x_u32 v9x_mga_expand_dwords(const struct v9x_mga_expand *expand);
/* OPMODE with dmamod set to DMA BLIT write, the other fields kept. */
v9x_u32 v9x_mga_opmode_for_iload(v9x_u32 opmode);
/* Nonzero when the linearizer can address a surface with this pitch and
 * origin, the test every destination passes. */
int v9x_mga_surface_ok(v9x_u32 pitch_bytes, v9x_u32 offset,
                       v9x_u32 bytes_per_pixel);
/*
 * The display start for a page flip: the 20-bit startadd that puts
 * byte_offset at the top left of the screen. `crtc13` and `crtcext0` are
 * the live CRTC13 and CRTCEXT0, whose offset field says how many bytes one
 * offset unit is in this mode; startadd counts half that. OK, or
 * UNSUPPORTED when the mode's unit is not one this driver knows or the
 * offset cannot be expressed, which declines the flip rather than
 * rounding it.
 */
v9x_status v9x_mga_display_start(v9x_u32 byte_offset, v9x_u32 pitch_bytes,
                                 v9x_u32 crtc13, v9x_u32 crtcext0,
                                 v9x_u32 vram_bytes, v9x_u32 *start);
/* CRTCEXT0 with startadd<19:16> replaced, the offset and interlace bits
 * kept. */
v9x_u32 v9x_mga_crtcext0_with_start(v9x_u32 crtcext0, v9x_u32 start);
v9x_u32 v9x_mga_status_busy(v9x_u32 status);
v9x_u32 v9x_mga_fifo_free(v9x_u32 fifostatus);

#endif
