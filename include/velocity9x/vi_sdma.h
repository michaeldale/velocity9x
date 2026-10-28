/*
 * AMD Carrizo (GCN 3, "Volcanic Islands" in Linux) SDMA engine: register
 * offsets, and Evergreen's DirectDraw fill and copy plans as SDMA packets.
 *
 * Pure policy, no I/O. The rows of a fill or copy are planned by
 * eg_dma.c's v9x_eg_dma_plan_fill/_copy, whose limits are the measured
 * ones on both engines (dword-aligned fill rows, no overlapping byte
 * copies); this module only writes each row as SDMA packets, which carry a
 * 64-bit address: Carrizo's carve-out sits at GPU F4_00000000 on M-XP7053,
 * past 4 GiB, so the plan's 32-bit addresses are the low halves and every
 * packet takes the high half from the caller. The caller writes the
 * packets into the ring, padded to V9X_VI_SDMA_ALIGN_DWORDS with NOPs,
 * flushes HDP and moves the write pointer.
 *
 * Offsets and fields are Linux amdgpu's oss_3_0_d.h/_sh_mask.h and
 * bif_5_0_d.h, packet layouts tonga_sdma_pkt_open.h and v4.9 sdma_v3_0.c
 * (build\external\carrizo\). Every packet kind used here ran byte-exact on
 * M-XP7053's R7E, three runs in a row
 * (docs\decisions\2026-10-28-carrizo-sdma-write-probe.md): byte-count
 * linear copy, an overlapping copy (memmove semantics), dword constant fill
 * and fence.
 */
#ifndef VELOCITY9X_VI_SDMA_H
#define VELOCITY9X_VI_SDMA_H

#include "velocity9x/status.h"
#include "velocity9x/eg_dma.h"

/* Registers through BAR5, the 256 KiB register window. */
#define V9X_VI_MMIO_BYTES               0x00040000ul
#define V9X_VI_SRBM_STATUS2             0x0e4cul
#define V9X_VI_SRBM_SOFT_RESET          0x0e60ul
#define V9X_VI_MC_VM_FB_LOCATION        0x2024ul
#define V9X_VI_HDP_DEBUG0               0x2f30ul
#define V9X_VI_CONFIG_MEMSIZE           0x5428ul
#define V9X_VI_HDP_MEM_COHERENCY_FLUSH  0x5480ul
#define V9X_VI_SDMA0_UCODE_ADDR         0xd000ul
#define V9X_VI_SDMA0_UCODE_DATA         0xd004ul
#define V9X_VI_SDMA0_POWER_CNTL         0xd008ul
#define V9X_VI_SDMA0_CLK_CTRL           0xd00cul
#define V9X_VI_SDMA0_CNTL               0xd010ul
#define V9X_VI_SDMA0_CHICKEN_BITS       0xd014ul
#define V9X_VI_SDMA0_TILING_CONFIG      0xd018ul
#define V9X_VI_SDMA0_SEM_WAIT_FAIL      0xd024ul
#define V9X_VI_SDMA0_STATUS_REG         0xd034ul
#define V9X_VI_SDMA0_F32_CNTL           0xd048ul
#define V9X_VI_SDMA0_RB_CNTL            0xd200ul
#define V9X_VI_SDMA0_RB_BASE            0xd204ul
#define V9X_VI_SDMA0_RB_BASE_HI         0xd208ul
#define V9X_VI_SDMA0_RB_RPTR            0xd20cul
#define V9X_VI_SDMA0_RB_WPTR            0xd210ul
#define V9X_VI_SDMA0_RPTR_ADDR_HI       0xd220ul
#define V9X_VI_SDMA0_RPTR_ADDR_LO       0xd224ul
#define V9X_VI_SDMA0_IB_CNTL            0xd228ul
#define V9X_VI_SDMA0_IB_RPTR            0xd22cul
#define V9X_VI_SDMA0_IB_OFFSET          0xd230ul
#define V9X_VI_SDMA0_DOORBELL           0xd248ul
#define V9X_VI_SDMA0_RLC0_IB_CNTL       0xd428ul
#define V9X_VI_SDMA0_RLC1_IB_CNTL       0xd628ul

/* Fields. SRBM_SOFT_RESET bit 20 SDMA0; F32_CNTL bit 0 halt; RB_CNTL bit 0
 * enable, 5:1 log2 of the ring dwords, bit 12 read-pointer writeback;
 * IB_CNTL bit 0 enable; CNTL bit 18 automatic context switch; DOORBELL
 * bit 28 enable; STATUS_REG bit 0 idle. MC_VM_FB_LOCATION 15:0 is the start
 * of the carve-out in the GPU's address space, in 16 MiB units of a 40-bit
 * address. */
#define V9X_VI_SOFT_RESET_SDMA0         0x00100000ul
#define V9X_VI_F32_HALT                 0x00000001ul
#define V9X_VI_RB_ENABLE                0x00000001ul
#define V9X_VI_RB_SIZE_MASK             0x0000003eul
#define V9X_VI_RB_SIZE_SHIFT            1
#define V9X_VI_RPTR_WRITEBACK           0x00001000ul
#define V9X_VI_IB_ENABLE                0x00000001ul
#define V9X_VI_AUTO_CTXSW               0x00040000ul
#define V9X_VI_DOORBELL_ENABLE          0x10000000ul
#define V9X_VI_SDMA_IDLE                0x00000001ul
#define V9X_VI_FB_START_MASK            0x0000fffful

/* The Carrizo golden settings for SDMA0, v4.9 sdma_v3_0.c
 * cz_golden_settings_a11 (clear the first mask, set the second);
 * cz_mgcg_cgcg_init's CLK_CTRL entry is undone by the golden one after
 * it, so only the golden one is kept. */
#define V9X_VI_CHICKEN_CLEAR            0xfc910007ul
#define V9X_VI_CHICKEN_SET              0x00810007ul
#define V9X_VI_CLK_CLEAR                0xff000ffful
#define V9X_VI_IB_CNTL_GOLDEN           0x00000100ul
#define V9X_VI_POWER_CLEAR              0x00000800ul
#define V9X_VI_POWER_SET                0x0003c800ul

/* The ring window the mini-VDD maps: BAR0 + 240 MiB, 4 MiB, past the
 * 16 MiB the driver maps for the heap (V9XMINI_FN_VI_MAP), where the write
 * probe ran. The ring takes the first 32 KiB, the read-pointer writeback
 * and the fence dword sit behind it. */
#define V9X_VI_RING_WINDOW_OFFSET       0x0f000000ul
#define V9X_VI_RING_WINDOW_BYTES        0x00400000ul
#define V9X_VI_SDMA_RING_LOG2_DWORDS    13ul
#define V9X_VI_SDMA_RING_DWORDS         (1ul << V9X_VI_SDMA_RING_LOG2_DWORDS)
#define V9X_VI_SDMA_WRITEBACK_OFFSET    (V9X_VI_SDMA_RING_DWORDS * 4ul)
#define V9X_VI_SDMA_FENCE_OFFSET        (V9X_VI_SDMA_WRITEBACK_OFFSET + 0x100ul)

/* Packets: tonga_sdma_pkt_open.h, header op 7:0 and sub-op 15:8;
 * CONSTANT_FILL's fill size in header 31:30, where the probe measured 0 a
 * byte and 2 a dword (1 is not a 16-bit fill). */
#define V9X_VI_SDMA_OP_NOP              0ul
#define V9X_VI_SDMA_OP_COPY             1ul
#define V9X_VI_SDMA_OP_FENCE            5ul
#define V9X_VI_SDMA_OP_CONST_FILL       11ul
#define V9X_VI_SDMA_SUB_LINEAR          0ul
#define V9X_VI_SDMA_FILL_SIZE_SHIFT     30
#define V9X_VI_SDMA_FILL_SIZE_DWORD     2ul
#define V9X_VI_SDMA_NOP                 0x00000000ul

/* Linux master's copy_max_bytes, "not 0x3fffff due to HW limitation": the
 * most one copy or fill packet is given here. A dword multiple. */
#define V9X_VI_SDMA_CHUNK_BYTES_MAX     0x003fffe0ul

/* Submissions are padded to 16 dwords, as the probe's were. */
#define V9X_VI_SDMA_ALIGN_DWORDS        16ul

/* Dwords one packet takes: a copy 7, a fill 5, a fence 4. A row is at most
 * two fill packets (an Evergreen fill row is at most 3FFFFCh bytes) or one
 * copy. */
#define V9X_VI_SDMA_COPY_DWORDS         7ul
#define V9X_VI_SDMA_FILL_DWORDS         5ul
#define V9X_VI_SDMA_FENCE_DWORDS        4ul
#define V9X_VI_SDMA_ROW_DWORDS_MAX      (2ul * V9X_VI_SDMA_FILL_DWORDS)

/* V9X_STATUS_OK when every row of the plan fits this emitter: a copy row
 * in one packet, a fill row in two. UNSUPPORTED otherwise (the operation
 * goes to the CPU); INVALID_ARGUMENT for no plan. */
v9x_status v9x_vi_sdma_plan_check(const struct v9x_eg_dma_plan *plan);

/* Writes row index's packets to out (at least V9X_VI_SDMA_ROW_DWORDS_MAX
 * dwords), addresses high-halved with address_high, and returns their
 * dword count; 0 for an index past the plan. */
v9x_u32 v9x_vi_sdma_plan_row(const struct v9x_eg_dma_plan *plan,
                             v9x_u32 index, v9x_u32 address_high,
                             v9x_u32 *out);

/* A fence: writes value to the dword at the GPU address when the engine
 * reaches it. Returns V9X_VI_SDMA_FENCE_DWORDS. */
v9x_u32 v9x_vi_sdma_fence(v9x_u32 address_low, v9x_u32 address_high,
                          v9x_u32 value, v9x_u32 *out);

/* NOP dwords that take a write position, in dwords, to the next
 * V9X_VI_SDMA_ALIGN_DWORDS boundary. */
v9x_u32 v9x_vi_sdma_pad(v9x_u32 position);

/* The write position, in dwords, after count dwords are put at wptr and
 * padded, the caller having already wrapped to 0 a submission that would
 * pass the ring's end. A submission that ends exactly at the end leaves
 * the position at 0. */
v9x_u32 v9x_vi_sdma_ring_advance(v9x_u32 wptr, v9x_u32 count);

/* The carve-out's first byte in the GPU's address space, from
 * MC_VM_FB_LOCATION, as bits 39:32 and 31:0. UNSUPPORTED when the low half
 * is above F0000000h, so that the ring window (240 MiB plus 4 MiB) could
 * carry into the high half. */
v9x_status v9x_vi_fb_start(v9x_u32 fb_location, v9x_u32 *high,
                           v9x_u32 *low);

#endif
