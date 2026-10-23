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
#define V9X_R128_RING_DL_DONE          0x80000000ul
/* PM4_BUFFER_WM_CNTL as ATI's driver and the /ati runs set it. */
#define V9X_R128_RING_WM_CNTL          0x40070707ul

/* Linux's R128_BROKEN_CCE: the CCE can read 32 dwords past the ring's end
 * before wrapping, so the ring's first 32 dwords are mirrored there. The
 * pages behind the ring must reach that far. */
#define V9X_R128_RING_MIRROR_DWORDS    32ul

#define V9X_R128_RING_MIN_BYTES        0x00002000ul
#define V9X_R128_RING_MAX_BYTES        0x00100000ul

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
