/*
 * AMD Evergreen (Radeon HD 5450, Cedar) async DMA engine: register offsets,
 * packets, and the rows of a DirectDraw fill or copy.
 *
 * Pure policy, no I/O. Evergreen has no fixed-function 2D engine; the DMA
 * engine copies and fills linear runs of VRAM, so a rectangle is one packet
 * per row, or one packet when the rows are contiguous. The caller writes the
 * packets into the ring, padded to V9X_EG_DMA_ALIGN_DWORDS with NOPs, flushes
 * HDP and moves the write pointer.
 *
 * Offsets, fields and packet formats are Linux radeon's evergreend.h and
 * r600d.h, the packet layouts its evergreen_cs.c DMA parser
 * (build\external\evergreen\linux-radeon\). Every packet kind used here ran
 * byte-exact on M-BXMFA8's HD 5450, five runs in a row
 * (docs\decisions\2026-10-26-cedar-dma-write-probe.md): dword-aligned copy,
 * byte-aligned copy, an overlapping dword copy (memmove semantics), and
 * constant fill. An overlapping byte copy was not measured, so it is refused.
 */
#ifndef VELOCITY9X_EG_DMA_H
#define VELOCITY9X_EG_DMA_H

#include "velocity9x/status.h"

/* Registers through BAR2, the 128 KiB register window. */
#define V9X_EG_MMIO_BYTES               0x00020000ul
#define V9X_EG_SRBM_STATUS2             0x0ec4ul
#define V9X_EG_SRBM_SOFT_RESET          0x0e60ul
#define V9X_EG_MC_VM_FB_LOCATION        0x2024ul
#define V9X_EG_CONFIG_MEMSIZE           0x5428ul
#define V9X_EG_HDP_MEM_COHERENCY_FLUSH  0x5480ul
#define V9X_EG_DMA_RB_CNTL              0xd000ul
#define V9X_EG_DMA_RB_BASE              0xd004ul
#define V9X_EG_DMA_RB_RPTR              0xd008ul
#define V9X_EG_DMA_RB_WPTR              0xd00cul
#define V9X_EG_DMA_RB_RPTR_ADDR_HI      0xd01cul
#define V9X_EG_DMA_RB_RPTR_ADDR_LO      0xd020ul
#define V9X_EG_DMA_IB_CNTL              0xd024ul
#define V9X_EG_DMA_CNTL                 0xd02cul
#define V9X_EG_DMA_SEM_INCOMPLETE       0xd044ul
#define V9X_EG_DMA_SEM_WAIT_FAIL        0xd048ul
#define V9X_EG_DMA_MODE                 0xd0bcul

/* Fields. SRBM_STATUS2 bit 5 DMA_BUSY; SRBM_SOFT_RESET bit 20 the DMA
 * block; DMA_RB_CNTL bit 0 enable and 5:1 log2 of the ring dwords;
 * DMA_IB_CNTL bit 0 enable; DMA_CNTL bit 28 the context-empty interrupt,
 * which nothing here services. MC_VM_FB_LOCATION 15:0 is the start of VRAM
 * in the GPU's address space, in 16 MiB units. */
#define V9X_EG_DMA_BUSY                 0x00000020ul
#define V9X_EG_SOFT_RESET_DMA           0x00100000ul
#define V9X_EG_DMA_RB_ENABLE            0x00000001ul
#define V9X_EG_DMA_RB_SIZE_SHIFT        1
#define V9X_EG_DMA_IB_ENABLE            0x00000001ul
#define V9X_EG_CTXEMPTY_INT_ENABLE      0x10000000ul
#define V9X_EG_FB_START_MASK            0x0000fffful
#define V9X_EG_FB_UNIT_SHIFT            24

/* The ring window the mini-VDD maps: BAR0 + 240 MiB, 64 KiB, past the
 * 16 MiB the driver maps for the heap (V9XMINI_FN_EG_MAP). The ring takes
 * the first 32 KiB; the fence dword sits behind it. */
#define V9X_EG_RING_WINDOW_OFFSET       0x0f000000ul
#define V9X_EG_RING_WINDOW_BYTES        0x00010000ul
#define V9X_EG_RING_LOG2_DWORDS         13ul
#define V9X_EG_RING_DWORDS              (1ul << V9X_EG_RING_LOG2_DWORDS)
#define V9X_EG_RING_FENCE_OFFSET        (V9X_EG_RING_DWORDS * 4ul)

/* Packets: evergreend.h DMA_PACKET, command 31:28, sub-command 27:20,
 * count 19:0. */
#define V9X_EG_DMA_CMD_COPY             0x3ul
#define V9X_EG_DMA_CMD_FENCE            0x6ul
#define V9X_EG_DMA_CMD_CONSTANT_FILL    0xdul
#define V9X_EG_DMA_CMD_NOP              0xful
#define V9X_EG_DMA_COPY_DWORD           0x00ul
#define V9X_EG_DMA_COPY_BYTE            0x40ul
#define V9X_EG_DMA_COUNT_MAX            0x000ffffful
#define V9X_EG_DMA_NOP                  0xf0000000ul

/* Linux pads every DMA submission to 16 dwords (the ring's align_mask). */
#define V9X_EG_DMA_ALIGN_DWORDS         16ul

/* Dwords one row's packet takes: a copy 5, a fill 4, a fence 4. */
#define V9X_EG_DMA_COPY_DWORDS          5ul
#define V9X_EG_DMA_FILL_DWORDS          4ul
#define V9X_EG_DMA_FENCE_DWORDS         4ul
#define V9X_EG_DMA_PACKET_DWORDS_MAX    5ul

/*
 * A planned operation: rows packets, row i at first + i * step on each
 * side (step negative when the copy runs bottom-up). One row when the rows
 * are contiguous. GPU addresses below 4 GiB only; the packets' high bytes
 * are zero.
 */
struct v9x_eg_dma_plan {
    v9x_u32 kind;               /* V9X_EG_DMA_PLAN_FILL or _COPY */
    v9x_u32 rows;
    v9x_u32 row_bytes;
    v9x_u32 destination;        /* GPU address of the first row emitted */
    v9x_u32 source;
    v9x_u32 destination_step;   /* two's complement */
    v9x_u32 source_step;
    v9x_u32 value;              /* the fill dword, replicated */
    v9x_u32 byte_copy;          /* non-zero: sub-command 40h */
};

#define V9X_EG_DMA_PLAN_FILL            1ul
#define V9X_EG_DMA_PLAN_COPY            2ul

struct v9x_eg_dma_fill {
    v9x_u32 gpu_base;           /* GPU address of heap offset 0 */
    v9x_u32 vram_bytes;         /* the heap: offsets must stay below this */
    v9x_u32 target_offset;
    v9x_u32 pitch_bytes;
    v9x_u32 bytes_per_pixel;
    v9x_u32 left;
    v9x_u32 top;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 color;
};

struct v9x_eg_dma_copy {
    v9x_u32 gpu_base;
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

/* V9X_STATUS_OK with the plan; INVALID_ARGUMENT for a rectangle outside
 * memory or a 4 GiB address space; UNSUPPORTED for what the engine was not
 * measured doing: a fill row that does not start and end on a dword, 24 bpp,
 * or an overlapping copy that would need byte mode. */
v9x_status v9x_eg_dma_plan_fill(const struct v9x_eg_dma_fill *fill,
                                struct v9x_eg_dma_plan *plan);
v9x_status v9x_eg_dma_plan_copy(const struct v9x_eg_dma_copy *copy,
                                struct v9x_eg_dma_plan *plan);

/* Writes row index's packet to out (at least V9X_EG_DMA_PACKET_DWORDS_MAX
 * dwords) and returns its dword count; 0 for an index past the plan. */
v9x_u32 v9x_eg_dma_plan_row(const struct v9x_eg_dma_plan *plan,
                            v9x_u32 index, v9x_u32 *out);

/* A fence: writes value to the GPU address when the engine reaches it.
 * Returns V9X_EG_DMA_FENCE_DWORDS. */
v9x_u32 v9x_eg_dma_fence(v9x_u32 gpu_address, v9x_u32 value, v9x_u32 *out);

/* NOP dwords that take a write position, in dwords, to the next
 * V9X_EG_DMA_ALIGN_DWORDS boundary. */
v9x_u32 v9x_eg_dma_pad(v9x_u32 position);

/* The write position, in dwords, after count dwords are put at wptr and
 * padded to the boundary, the caller having already wrapped to 0 a
 * submission that would pass the ring's end. DMA_RB_WPTR takes offsets
 * inside the ring only, so a submission that ends exactly at the end
 * leaves the position at 0. */
v9x_u32 v9x_eg_dma_ring_advance(v9x_u32 wptr, v9x_u32 count);

/* The GPU address of VRAM's first byte, from MC_VM_FB_LOCATION; 0 with
 * *ok clear when it lies at or above 4 GiB. */
v9x_u32 v9x_eg_fb_start(v9x_u32 fb_location, v9x_u32 *ok);

#endif
