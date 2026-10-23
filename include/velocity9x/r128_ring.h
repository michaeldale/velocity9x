/*
 * The Rage 128's bus-master CCE ring: where AGP_BASE and PM4_BUFFER_OFFSET
 * put a ring in the AGP aperture, its size code, its free space, and the
 * bus and AGP control values it runs with. Pure arithmetic over register
 * values, host-tested (tests\host\test_r128_ring.c); the mini-VDD reserves
 * the aperture pages and src\display32\engines\r128_cce.c writes the
 * registers.
 *
 * All of it is measured on one card, A8U4I5's Rage Fury Pro on a 440BX
 * (docs\decisions\2026-10-23-rage128-cce-ring-agp-fetches-but-wrong-
 * data.md). The card maps its AGP window, sized by AGP_CNTL, from AGP_BASE;
 * PM4_BUFFER_OFFSET is the ring's distance into it (the register drops bit
 * 25, so Linux's AGP_APER_OFFSET-based form is the same value). A ring at
 * D9C00000 ran with AGP_BASE D8000000, the base of the 32 MiB window that
 * holds it, and fetched nothing with AGP_BASE D9C00000, the ring rounded to
 * the register's 4 MiB granularity. ATI's driver used DA000000 with offset
 * 0, also a 32 MiB boundary.
 */
#ifndef VELOCITY9X_R128_RING_H
#define VELOCITY9X_R128_RING_H

#include "velocity9x/types.h"
#include "velocity9x/status.h"

#define V9X_R128_RING_AGP_BASE         0x0170ul
#define V9X_R128_RING_AGP_CNTL         0x0174ul
#define V9X_R128_RING_BUS_CNTL         0x0030ul
/* PCI_GART_PAGE: 1 turns the card's own PCI GART off, as AGP mode needs
 * (RRG 3-207; X.Org's R128DRIAgpInit writes it). */
#define V9X_R128_RING_PCI_GART_PAGE    0x017cul
#define V9X_R128_RING_PCI_GART_OFF     0x00000001ul
/* AGP_COMMAND in the register file's copy of configuration space. */
#define V9X_R128_RING_AGP_COMMAND      0x0f58ul
#define V9X_R128_RING_BUFFER_OFFSET    0x0700ul
#define V9X_R128_RING_BUFFER_WM_CNTL   0x0708ul
#define V9X_R128_RING_DL_RPTR          0x0710ul
#define V9X_R128_RING_DL_WPTR          0x0714ul

/* The card's AGP window: AGP_CNTL size code 38h (RRG 3-186), the 32 MiB
 * ATI's driver programs and the ring ran with. */
#define V9X_R128_RING_WINDOW_BYTES     0x02000000ul
/* PM4_BUFFER_CNTL: 192BM (bits 31:28 = 2), the ring size in bits 5:0 as
 * log2 quadwords. DL_WPTR's DL_DONE (bit 31) ends a stream before a wait. */
#define V9X_R128_RING_MODE_192BM       0x20000000ul
/* 64BM_64VCBM_64INDBM (8 << 28): the mode Linux runs, with the vertex
 * channel 3D_RNDR_GEN_INDX_PRIM reads AGP vertices through. Its command
 * FIFO holds 64 entries, 192BM's 192. The probe ran the ring in both
 * (R128CCE /m8, boot 539). */
#define V9X_R128_RING_MODE_64BM_VCBM   0x80000000ul
#define V9X_R128_RING_MODE_64_FIFO     64ul
#define V9X_R128_RING_DL_DONE          0x80000000ul
/* PM4_BUFFER_WM_CNTL as ATI's driver and the /ati runs set it. */
#define V9X_R128_RING_WM_CNTL          0x40070707ul

/* Linux's R128_BROKEN_CCE: the CCE can read 32 dwords past the ring's end
 * before wrapping, so the ring's first 32 dwords are mirrored there. The
 * pages behind the ring must reach that far. */
#define V9X_R128_RING_MIRROR_DWORDS    32ul

#define V9X_R128_RING_MIN_BYTES        0x00002000ul
#define V9X_R128_RING_MAX_BYTES        0x00100000ul

/*
 * The rest of the reservation: the 64 KiB ring, its mirror page, then a
 * vertex area for indexed primitives, which the vertex walker reads from
 * AGP memory as the ring is (3D_RNDR_GEN_INDX_PRIM). The mini-VDD reserves
 * V9X_R128_RING_RESERVE_BYTES and reports that size, which the 16-bit side
 * publishes as engine.ring_bytes: the ring is the first 64 KiB of it. A
 * mini-VDD that reserved only the ring reports 64 KiB, and gets no vertex
 * area. loader.asm's page count must agree.
 */
#define V9X_R128_RING_BYTES            0x00010000ul
#define V9X_R128_RING_VERTEX_OFFSET    (V9X_R128_RING_BYTES + 0x1000ul)
#define V9X_R128_RING_VERTEX_BYTES     0x00080000ul
#define V9X_R128_RING_RESERVE_BYTES \
    (V9X_R128_RING_VERTEX_OFFSET + V9X_R128_RING_VERTEX_BYTES)

/* The vertex area in segments. A segment is closed with an age the CCE
 * writes to GUI_SCRATCH_REG2 behind the last packet that read it, and
 * written again only once that age has come back. */
#define V9X_R128_VTX_SEGMENTS          8ul
#define V9X_R128_VTX_SEGMENT_BYTES \
    (V9X_R128_RING_VERTEX_BYTES / V9X_R128_VTX_SEGMENTS)
#define V9X_R128_RING_GUI_SCRATCH_REG2 0x15e8ul

typedef struct v9x_r128_vtx_area {
    v9x_u32 segment;                        /* the one being filled */
    v9x_u32 used;                           /* its bytes handed out */
    v9x_u32 age[V9X_R128_VTX_SEGMENTS];     /* 0: free */
    v9x_u32 last_age;
} v9x_r128_vtx_area;

typedef struct v9x_r128_vtx_grant {
    v9x_u32 offset;     /* in the vertex area */
    v9x_u32 close_age;  /* non-zero: write it behind what is submitted */
    v9x_u32 wait_age;   /* non-zero: the CCE must reach it first */
} v9x_r128_vtx_grant;

/* Empty, nothing outstanding. */
void v9x_r128_vtx_reset(v9x_r128_vtx_area *area);
/* The CCE has drained: every segment is free again. */
void v9x_r128_vtx_all_done(v9x_r128_vtx_area *area);
/* Room for `bytes` in one segment, or zero when it cannot fit in one. A
 * piece that does not fit in the current segment closes it and moves on;
 * the grant says what to write and what to wait for. */
int v9x_r128_vtx_reserve(v9x_r128_vtx_area *area, v9x_u32 bytes,
                         v9x_r128_vtx_grant *grant);
/* Non-zero when the age read back covers `wanted`. */
int v9x_r128_vtx_age_reached(v9x_u32 read_back, v9x_u32 wanted);

typedef struct v9x_r128_ring_plan {
    v9x_u32 agp_base;       /* AGP_BASE */
    v9x_u32 buffer_offset;  /* PM4_BUFFER_OFFSET */
    v9x_u32 size_l2qw;      /* PM4_BUFFER_CNTL bits 5:0 */
    v9x_u32 dword_mask;     /* ring dwords - 1: the pointers' wrap */
} v9x_r128_ring_plan;

/* The registers for a ring of ring_bytes at bus_address in the aperture.
 * V9X_STATUS_INVALID_ARGUMENT for no address, one not page aligned, a size
 * that is not a power of two between the limits above, or a ring whose
 * mirror runs past its 32 MiB window. */
v9x_status v9x_r128_ring_plan_make(v9x_u32 bus_address, v9x_u32 ring_bytes,
                                   v9x_r128_ring_plan *plan);

/* Dwords the writer may add with the CCE at rptr and the writer at wptr,
 * keeping one empty so a full ring is not read as an empty one. */
v9x_u32 v9x_r128_ring_free(v9x_u32 rptr, v9x_u32 wptr, v9x_u32 dword_mask);

/* BUS_CNTL with bus mastering enabled (BUS_MASTER_DIS, bit 6, clear) and
 * bit 29 clear, as ATI's driver runs it. */
v9x_u32 v9x_r128_ring_bus_cntl(v9x_u32 current);

/* AGP_CNTL with the 32 MiB window and the FIFO sensing bits 22:20 clear,
 * as ATI's driver runs it; the other bits kept. */
v9x_u32 v9x_r128_ring_agp_cntl(v9x_u32 current);

/* Non-zero when AGP_COMMAND has AGP_EN (bit 8): the card is in AGP mode,
 * where its bus-master reads use the AGP window. All ones (no decode) is
 * not. */
int v9x_r128_ring_agp_enabled(v9x_u32 agp_command);

#endif
