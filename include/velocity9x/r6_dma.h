/*
 * ATI R6xx (Radeon HD 2600 XT, RV630) async DMA engine: register offsets,
 * packets, and the rows of a DirectDraw fill or copy.
 *
 * Pure policy, no I/O. R6xx has no fixed-function 2D engine; its DMA engine
 * copies linear runs of VRAM, so a rectangle is one packet a row. Offsets,
 * fields and packets are Linux radeon's r600d.h and r600_dma.c
 * (build\external\evergreen\linux-radeon\). What the engine does with them
 * was measured on M-NZHWBR's HD 2600 XT
 * (docs\decisions\2026-11-13-rv630-dma-write-probe.md), and that record
 * sets three rules this builder keeps:
 *
 *   - There is no constant fill (r600d.h: "7xx only"). A fill is a seed row
 *     the CPU writes, then every other row copied from that row, so no copy
 *     reads what another packet of the same submission wrote: inside one
 *     submission a copy did not see an earlier packet's write.
 *   - A one-dword COPY did nothing. Rows of one dword are declined.
 *   - COPY is dword-sized only: no byte copy. Anything not on dwords is
 *     declined to the CPU.
 */
#ifndef VELOCITY9X_R6_DMA_H
#define VELOCITY9X_R6_DMA_H

#include "velocity9x/status.h"

/* Registers through BAR2, the 64 KiB register window. */
#define V9X_R6_MMIO_BYTES               0x00010000ul
#define V9X_R6_SRBM_STATUS              0x0e50ul
#define V9X_R6_SRBM_SOFT_RESET          0x0e60ul
#define V9X_R6_MC_VM_FB_LOCATION        0x2180ul
#define V9X_R6_CONFIG_MEMSIZE           0x5428ul
#define V9X_R6_HDP_MEM_COHERENCY_FLUSH  0x5480ul
#define V9X_R6_GRBM_STATUS              0x8010ul
#define V9X_R6_DMA_RB_CNTL              0xd000ul
#define V9X_R6_DMA_RB_BASE              0xd004ul
#define V9X_R6_DMA_RB_RPTR              0xd008ul
#define V9X_R6_DMA_RB_WPTR              0xd00cul
#define V9X_R6_DMA_RB_RPTR_ADDR_HI      0xd01cul
#define V9X_R6_DMA_RB_RPTR_ADDR_LO      0xd020ul
#define V9X_R6_DMA_IB_CNTL              0xd024ul
#define V9X_R6_DMA_CNTL                 0xd02cul
#define V9X_R6_DMA_STATUS_REG           0xd034ul
#define V9X_R6_DMA_SEM_INCOMPLETE       0xd044ul
#define V9X_R6_DMA_SEM_WAIT_FAIL        0xd048ul

/* Fields. SRBM_SOFT_RESET bit 12 the DMA block (RV770 moved it to bit 20);
 * DMA_RB_CNTL bit 0 enable and 5:1 log2 of the ring dwords; DMA_IB_CNTL
 * bit 0 enable; DMA_CNTL bit 28 the context-empty interrupt, which nothing
 * here services; DMA_STATUS_REG bit 0 DMA_IDLE, the busy test
 * r600_gpu_check_soft_reset uses. CONFIG_MEMSIZE is in bytes on R6xx.
 * MC_VM_FB_LOCATION 15:0 is the start of VRAM in the GPU's address space,
 * in 16 MiB units. */
#define V9X_R6_SOFT_RESET_DMA           0x00001000ul
#define V9X_R6_DMA_RB_ENABLE            0x00000001ul
#define V9X_R6_DMA_RB_SIZE_SHIFT        1
#define V9X_R6_DMA_IB_ENABLE            0x00000001ul
#define V9X_R6_CTXEMPTY_INT_ENABLE      0x10000000ul
#define V9X_R6_DMA_IDLE                 0x00000001ul
#define V9X_R6_MEMSIZE_MIN              0x10000000ul
#define V9X_R6_MEMSIZE_MAX              0x40000000ul
#define V9X_R6_FB_START_MASK            0x0000fffful
#define V9X_R6_FB_UNIT_SHIFT            24

/* The ring window the mini-VDD maps (V9XMINI_FN_EG_MAP, shared with
 * Evergreen): BAR0 + 240 MiB, past the heap. The ring takes the first
 * 32 KiB; the fence dword sits behind it. */
#define V9X_R6_RING_WINDOW_OFFSET       0x0f000000ul
#define V9X_R6_RING_WINDOW_BYTES        0x00010000ul
#define V9X_R6_RING_LOG2_DWORDS         13ul
#define V9X_R6_RING_DWORDS              (1ul << V9X_R6_RING_LOG2_DWORDS)
#define V9X_R6_RING_FENCE_OFFSET        (V9X_R6_RING_DWORDS * 4ul)

/* Packets: r600d.h DMA_PACKET, command 31:28, count 15:0. Linux splits a
 * copy at 0FFFEh dwords (r600_copy_dma), so this does too. */
#define V9X_R6_DMA_CMD_COPY             0x3ul
#define V9X_R6_DMA_CMD_FENCE            0x6ul
#define V9X_R6_DMA_CMD_NOP              0xful
#define V9X_R6_DMA_COUNT_MAX            0x0000fffeul
#define V9X_R6_DMA_NOP                  0xf0000000ul

/* Linux pads every DMA submission to 16 dwords. */
#define V9X_R6_DMA_ALIGN_DWORDS         16ul

/* A copy packet and a fence are four dwords each. */
#define V9X_R6_DMA_COPY_DWORDS          4ul
#define V9X_R6_DMA_FENCE_DWORDS         4ul
#define V9X_R6_DMA_PACKET_DWORDS_MAX    4ul

/*
 * A planned operation. First the CPU writes seed_bytes of value at heap
 * offset seed_offset (a fill's first row; nothing for a copy), and the
 * caller makes that write visible to the engine. Then rows COPY packets,
 * row i from source + i * source_step to destination + i * destination_step
 * (steps negative when a copy runs bottom-up), row_dwords each. Addresses
 * are bits 31:0 of the GPU address; bits 39:32 go to v9x_r6_dma_plan_row.
 */
struct v9x_r6_dma_plan {
    v9x_u32 rows;
    v9x_u32 row_dwords;
    v9x_u32 destination;
    v9x_u32 source;
    v9x_u32 destination_step;   /* two's complement */
    v9x_u32 source_step;
    v9x_u32 seed_offset;        /* heap offset */
    v9x_u32 seed_bytes;         /* 0 for a copy */
    v9x_u32 value;              /* the fill dword, replicated */
};

struct v9x_r6_dma_fill {
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

struct v9x_r6_dma_copy {
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
 * measured doing right: anything off dwords, 24 bpp, a row of one dword,
 * a row past one packet, or an overlapping copy between two pitches. */
v9x_status v9x_r6_dma_plan_fill(const struct v9x_r6_dma_fill *fill,
                                struct v9x_r6_dma_plan *plan);
v9x_status v9x_r6_dma_plan_copy(const struct v9x_r6_dma_copy *copy,
                                struct v9x_r6_dma_plan *plan);

/* Writes row index's COPY packet to out (at least
 * V9X_R6_DMA_PACKET_DWORDS_MAX dwords) and returns its dword count; 0 for
 * an index past the plan. address_high is bits 39:32 of both sides' GPU
 * addresses: dword 3 carries the destination's in 23:16 and the source's
 * in 7:0 (r600_copy_dma). */
v9x_u32 v9x_r6_dma_plan_row(const struct v9x_r6_dma_plan *plan,
                            v9x_u32 index, v9x_u32 address_high,
                            v9x_u32 *out);

/* A fence: writes value to the GPU address (bits 39:32 in address_high,
 * dword 2) when the engine reaches it (r600_dma_fence_ring_emit, without
 * the TRAP: nothing here takes the interrupt). Returns
 * V9X_R6_DMA_FENCE_DWORDS. */
v9x_u32 v9x_r6_dma_fence(v9x_u32 address_high, v9x_u32 gpu_address,
                         v9x_u32 value, v9x_u32 *out);

/* DMA_RB_BASE: the ring's GPU address shifted right by 8
 * (r600_dma_resume). */
v9x_u32 v9x_r6_dma_ring_base(v9x_u32 address_high, v9x_u32 address_low);

/* NOP dwords that take a write position to the next 16-dword boundary. */
v9x_u32 v9x_r6_dma_pad(v9x_u32 position);

/* The write position after count dwords are put at wptr and padded, 0 when
 * that ends exactly at the ring's end (DMA_RB_WPTR holds offsets inside the
 * ring only). The caller has already wrapped a submission that would pass
 * the end. */
v9x_u32 v9x_r6_dma_ring_advance(v9x_u32 wptr, v9x_u32 count);

/* The GPU address of VRAM's first byte, from MC_VM_FB_LOCATION: bits 39:32
 * in *high and 31:0 in *low. INVALID_ARGUMENT when the low half is above
 * V9X_R6_FB_LOW_MAX, where the window at 240 MiB could carry into the high
 * half. M-NZHWBR reads 00EF00E0: VRAM at E0000000. */
#define V9X_R6_FB_LOW_MAX               0xef000000ul
v9x_status v9x_r6_fb_start(v9x_u32 fb_location, v9x_u32 *high,
                           v9x_u32 *low);

#endif
