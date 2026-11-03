/*
 * NVIDIA RIVA 128 (NV3) 2D engine: the bring-up, solid fill and screen
 * copy, as register and method writes.
 *
 * Pure policy, no I/O. The engine module (engines\eng_nv3.c) writes what
 * these builders return, in order, through BAR0 and the RAMIN window.
 * Everything here was run on the card by the Phase 3 write probe
 * (tools\diag\nv3_2d_win32.c) and measured in
 * docs\decisions\2026-11-02-riva128-2d-engine-writes.md: the bring-up is
 * Linux rivafb's (riva_hw.c LoadStateExt, riva_tbl.h) at the byte offsets
 * its word indexes name; fills go through the GDI rectangle object and
 * copies through the blit object; PGRAPH surface 0 is the destination and
 * surface 1 the blit source.
 */
#ifndef VELOCITY9X_NV3_2D_H
#define VELOCITY9X_NV3_2D_H

#include "velocity9x/status.h"

/* BAR0 registers (riva_tbl.h word index x 4 plus the unit's base). */
#define V9X_NV3_PMC_BOOT_0          0x000000ul
#define V9X_NV3_PMC_INTR_EN         0x000140ul
#define V9X_NV3_PMC_ENABLE          0x000200ul
#define V9X_NV3_PFB_CONFIG_0        0x100200ul
#define V9X_NV3_PGRAPH_SURF_OFFSET  0x400630ul
#define V9X_NV3_PGRAPH_SURF_PITCH   0x400650ul
#define V9X_NV3_PGRAPH_SURF_CONFIG  0x4006a8ul
#define V9X_NV3_PGRAPH_STATUS       0x4006b0ul

/* PGRAPH STATUS bit 0, engine busy (riva_hw.c nv3Busy). */
#define V9X_NV3_STATUS_BUSY         0x00000001ul

/* The USER area: channel 0, subchannel s at 800000h + s * 2000h, its free
 * count at +10h in bytes (riva_hw.h RIVA_FIFO_FREE). */
#define V9X_NV3_USER                0x800000ul
#define V9X_NV3_SUBCHANNEL_STEP     0x2000ul
#define V9X_NV3_USER_FREE           0x800010ul
#define V9X_NV3_FREE_MASK           0x0000fffful
#define V9X_NV3_FREE_SHIFT          2u

/* rivafb's subchannels (riva_tbl.h RivaTableFIFO), without the D3D one. */
#define V9X_NV3_SUB_ROP             0ul
#define V9X_NV3_SUB_CLIP            1ul
#define V9X_NV3_SUB_PATTERN         2ul
#define V9X_NV3_SUB_PIXMAP          3ul
#define V9X_NV3_SUB_BLIT            4ul
#define V9X_NV3_SUB_BITMAP          5ul
#define V9X_NV3_SUB_LINE            6ul

/* Object methods (the riva_hw.h structs' offsets). */
#define V9X_NV3_M_ROP3              0x300ul
#define V9X_NV3_M_CLIP_POINT        0x300ul
#define V9X_NV3_M_CLIP_SIZE         0x304ul
#define V9X_NV3_M_PATTERN_COLOR0    0x310ul
#define V9X_NV3_M_PATTERN_COLOR1    0x314ul
#define V9X_NV3_M_PATTERN_MONO0     0x318ul
#define V9X_NV3_M_PATTERN_MONO1     0x31cul
#define V9X_NV3_M_BLIT_SRC          0x300ul
#define V9X_NV3_M_BLIT_DST          0x304ul
#define V9X_NV3_M_BLIT_SIZE         0x308ul
#define V9X_NV3_M_RECT_COLOR        0x3fcul
#define V9X_NV3_M_RECT_POINT        0x400ul
#define V9X_NV3_M_RECT_SIZE         0x404ul
#define V9X_NV3_ROP_SRCCOPY         0xccul

/*
 * RAMIN is the top 32 KiB of video memory, reversed in 16-byte units: RAMIN
 * offset r is video memory top - 10h - (r & ~0Fh) + (r & 0Fh), measured on
 * M-7V636C by writing the top of video memory and reading it back through
 * BAR1 + C00000h (docs\decisions\2026-11-02-riva128-register-probe.md).
 * The DirectDraw heap stops below it.
 */
#define V9X_NV3_RAMIN_BYTES         0x00008000ul
#define V9X_NV3_RAMIN_UNIT          0x10ul

/* A RAMHT entry the bring-up writes, read back to tell whether RAMIN still
 * holds the bring-up: a VBE mode set clears it (measured, the record above). */
#define V9X_NV3_RAMIN_MARK_OFFSET   0x0800ul
#define V9X_NV3_RAMIN_MARK_VALUE    0x80000000ul

/*
 * Surface limits: PGRAPH's surface offset holds bits 21:4 and its pitch
 * bits 12:4 (nv3_pgraph.xml's masks, 003FFFF0h and 1FF0h). The offset mask
 * is a comment in rnndb, not measured; nothing past 4 MiB is asked of the
 * engine until it is. Coordinates and sizes are 16-bit method fields.
 */
#define V9X_NV3_SURFACE_ALIGN       0x10ul
#define V9X_NV3_SURFACE_LIMIT       0x00400000ul
#define V9X_NV3_PITCH_MAX           0x00001ff0ul
#define V9X_NV3_EXTENT_MAX          0x0000fffful

/* rivafb CalcStateExt's PFB_CONFIG_0: width / 32 rounded up in 5:0, bytes
 * per pixel (at most 3) in 9:8, tiling disabled. */
#define V9X_NV3_PFB_WIDTH_UNIT      32ul
#define V9X_NV3_PFB_DEPTH_SHIFT     8u
#define V9X_NV3_PFB_TILING_OFF      0x00001000ul

struct v9x_nv3_write {
    v9x_u32 offset;
    v9x_u32 value;
};

#define V9X_NV3_BEFORE_MAX   40u
#define V9X_NV3_RAMIN_MAX    32u
#define V9X_NV3_AFTER_MAX    48u
#define V9X_NV3_BIND_MAX     8u
#define V9X_NV3_DEFAULTS_MAX 8u

/*
 * The bring-up for one mode, as rivafb orders it: BAR0 writes before RAMIN
 * (PMC, PTIMER, PFB, PFIFO), RAMIN writes by RAMIN offset, BAR0 writes after
 * (PGRAPH, SURF_CONFIG, the four surfaces at the screen), the channel's
 * subchannel bindings, then the defaults rivafb's riva_setup_accel sends
 * (clip, solid pattern, SRCCOPY). The engine module waits for the engine to
 * go idle after the bindings and latches the free count before the
 * defaults.
 */
struct v9x_nv3_setup {
    struct v9x_nv3_write before[V9X_NV3_BEFORE_MAX];
    struct v9x_nv3_write ramin[V9X_NV3_RAMIN_MAX];
    struct v9x_nv3_write after[V9X_NV3_AFTER_MAX];
    struct v9x_nv3_write bind[V9X_NV3_BIND_MAX];
    struct v9x_nv3_write defaults[V9X_NV3_DEFAULTS_MAX];
    v9x_u32 before_count;
    v9x_u32 ramin_count;
    v9x_u32 after_count;
    v9x_u32 bind_count;
    v9x_u32 defaults_count;
};

/*
 * A fill or copy: PGRAPH surface registers written directly while the
 * engine is idle, then methods into the channel. method_count entries of
 * free count are needed before the methods go.
 */
#define V9X_NV3_SURFACE_WRITES_MAX 4u
#define V9X_NV3_METHODS_MAX        6u

struct v9x_nv3_op {
    struct v9x_nv3_write surface[V9X_NV3_SURFACE_WRITES_MAX];
    struct v9x_nv3_write methods[V9X_NV3_METHODS_MAX];
    v9x_u32 surface_count;
    v9x_u32 method_count;
};

struct v9x_nv3_fill {
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

struct v9x_nv3_copy {
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

v9x_status v9x_nv3_build_setup(v9x_u32 bytes_per_pixel, v9x_u32 width,
                               v9x_u32 height, v9x_u32 pitch_bytes,
                               struct v9x_nv3_setup *setup);
v9x_u32 v9x_nv3_pfb_config(v9x_u32 bytes_per_pixel, v9x_u32 width);
v9x_u32 v9x_nv3_ramin_vram_offset(v9x_u32 vram_top, v9x_u32 ramin_offset);
v9x_status v9x_nv3_build_fill(const struct v9x_nv3_fill *fill,
                              struct v9x_nv3_op *op);
v9x_status v9x_nv3_build_copy(const struct v9x_nv3_copy *copy,
                              struct v9x_nv3_op *op);
v9x_u16 v9x_nv3_depth_drawn(v9x_u32 bytes_per_pixel);

#endif
