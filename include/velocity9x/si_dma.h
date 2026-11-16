/*
 * AMD Southern Islands (Oland, Radeon R7 240, GCN 1) async DMA engine:
 * register offsets, and Evergreen's DirectDraw fill and copy plans as SI
 * DMA packets.
 *
 * Pure policy, no I/O. The rows of a fill or copy are planned by
 * eg_dma.c's v9x_eg_dma_plan_fill/_copy; this module only checks a plan
 * against what Linux does with SI's engine and writes each row's packets.
 * The caller writes them into the ring, padded to V9X_SI_DMA_ALIGN_DWORDS
 * with NOPs, flushes HDP and moves the write pointer.
 *
 * SI's engine is Evergreen's and Cayman's at the same offsets, with no
 * microcode, and its packets are Evergreen's header (radeon sid.h
 * DMA_PACKET: command 31:28, bit 26, count 19:0). Linux differs from its
 * Evergreen code in two places, both kept here:
 * - every copy is DMA_PACKET(COPY, 1, 0, 0, bytes): bit 26 set and a byte
 *   count (radeon si_dma.c si_copy_dma, amdgpu si_dma.c
 *   si_dma_emit_copy_buffer), which is Evergreen's byte sub-command 40h.
 *   Linux never sends SI a dword-count copy, so neither does this.
 * - a packet carries at most copy_max_bytes = fill_max_bytes = FFFF8h
 *   (amdgpu si_dma.c si_dma_buffer_funcs), so a longer row is split.
 * Evergreen's byte copy was not measured overlapping itself, and SI's has
 * not run at all, so a copy whose source and destination spans overlap is
 * refused (the CPU draws it) until a write probe measures one.
 *
 * Offsets and fields are amdgpu's oss_1_0_d.h/_sh_mask.h, gmc_6_0_d.h and
 * radeon's sid.h (build\external\carrizo\linux\). Nothing here has run on
 * an Oland (docs\plans\ati-oland-gfx6.md).
 */
#ifndef VELOCITY9X_SI_DMA_H
#define VELOCITY9X_SI_DMA_H

#include "velocity9x/status.h"
#include "velocity9x/eg_dma.h"

/* Registers through BAR2, the 256 KiB register window. */
#define V9X_SI_MMIO_BYTES               0x00040000ul
#define V9X_SI_SRBM_STATUS2             0x0e4cul
#define V9X_SI_SRBM_SOFT_RESET          0x0e60ul
#define V9X_SI_MC_VM_FB_LOCATION        0x2024ul
#define V9X_SI_HDP_DEBUG0               0x2f30ul
#define V9X_SI_CONFIG_MEMSIZE           0x5428ul
#define V9X_SI_HDP_MEM_COHERENCY_FLUSH  0x5480ul
#define V9X_SI_DMA_RB_CNTL              0xd000ul
#define V9X_SI_DMA_RB_BASE              0xd004ul
#define V9X_SI_DMA_RB_RPTR              0xd008ul
#define V9X_SI_DMA_RB_WPTR              0xd00cul
#define V9X_SI_DMA_RB_RPTR_ADDR_HI      0xd01cul
#define V9X_SI_DMA_RB_RPTR_ADDR_LO      0xd020ul
#define V9X_SI_DMA_IB_CNTL              0xd024ul
#define V9X_SI_DMA_IB_RPTR              0xd028ul
#define V9X_SI_DMA_CNTL                 0xd02cul
#define V9X_SI_DMA_STATUS_REG           0xd034ul
#define V9X_SI_DMA_SEM_INCOMPLETE_TIMER 0xd044ul
#define V9X_SI_DMA_SEM_WAIT_FAIL_TIMER  0xd048ul

/* Fields. SRBM_STATUS2 bit 5 DMA_BUSY; DMA_RB_CNTL bit 0 enable, 5:1
 * log2 of the ring dwords, bit 12 read-pointer writeback; DMA_IB_CNTL
 * bit 0 enable and bit 31 CMD_VMID_FORCE, which si_dma_start sets;
 * DMA_CNTL bit 28 the context-empty interrupt, which nothing here
 * services; DMA_STATUS_REG bit 0 idle. */
#define V9X_SI_DMA_BUSY                 0x00000020ul
#define V9X_SI_DMA_RB_ENABLE            0x00000001ul
#define V9X_SI_DMA_RB_SIZE_MASK         0x0000003eul
#define V9X_SI_DMA_RB_SIZE_SHIFT        1
#define V9X_SI_DMA_RPTR_WRITEBACK       0x00001000ul
#define V9X_SI_DMA_IB_ENABLE            0x00000001ul
#define V9X_SI_DMA_IB_VMID_FORCE        0x80000000ul
#define V9X_SI_DMA_CTXEMPTY_INT_ENABLE  0x10000000ul
#define V9X_SI_DMA_IDLE                 0x00000001ul

/* Packets: sid.h DMA_PACKET. Bit 26 is the "b" argument Linux passes as 1
 * for every copy. NOP, fence and fill are Evergreen's words
 * (V9X_EG_DMA_*). */
#define V9X_SI_DMA_COPY_BYTES_BIT       0x04000000ul

/* amdgpu's copy_max_bytes and fill_max_bytes: the most one packet is
 * given. A dword multiple. */
#define V9X_SI_DMA_CHUNK_BYTES_MAX      0x000ffff8ul

/* amdgpu si_dma's ring align_mask is 0xf: submissions padded to 16
 * dwords, as Evergreen's. */
#define V9X_SI_DMA_ALIGN_DWORDS         16ul

/* Dwords one packet takes: a copy 5, a fill 4, a fence 4 (Evergreen's).
 * An Evergreen plan's row is at most FFFFFh dwords (3FFFFCh bytes), so at
 * most five chunks. */
#define V9X_SI_DMA_COPY_DWORDS          V9X_EG_DMA_COPY_DWORDS
#define V9X_SI_DMA_FILL_DWORDS          V9X_EG_DMA_FILL_DWORDS
#define V9X_SI_DMA_FENCE_DWORDS         V9X_EG_DMA_FENCE_DWORDS
#define V9X_SI_DMA_ROW_PACKETS_MAX      5ul
#define V9X_SI_DMA_ROW_DWORDS_MAX       \
    (V9X_SI_DMA_ROW_PACKETS_MAX * V9X_SI_DMA_COPY_DWORDS)

/* V9X_STATUS_OK when every row of the plan fits this emitter: at most
 * V9X_SI_DMA_ROW_PACKETS_MAX packets a row, and for a copy, source and
 * destination spans that do not overlap. UNSUPPORTED otherwise (the
 * operation goes to the CPU); INVALID_ARGUMENT for no plan. */
v9x_status v9x_si_dma_plan_check(const struct v9x_eg_dma_plan *plan);

/* Writes row index's packets to out (at least V9X_SI_DMA_ROW_DWORDS_MAX
 * dwords), address_high being bits 39:32 of every address, and returns
 * their dword count; 0 for an index past the plan or a plan
 * v9x_si_dma_plan_check refuses. */
v9x_u32 v9x_si_dma_plan_row(const struct v9x_eg_dma_plan *plan,
                            v9x_u32 index, v9x_u32 address_high,
                            v9x_u32 *out);

#endif
