/*
 * AMD Evergreen async DMA engine for V9XHAL.DLL: DirectDraw solid fill and
 * screen copy.
 *
 * Evergreen has no fixed-function 2D engine. The DMA engine copies and fills
 * linear runs of VRAM, so a rectangle is one packet a row
 * (src\chipsets\ati\evergreen\eg_dma.c, host-tested), queued on a ring in
 * VRAM and executed without microcode. Every packet kind used here ran
 * byte-exact on M-BXMFA8's HD 5450
 * (docs\decisions\2026-10-26-cedar-dma-write-probe.md).
 *
 * The 16-bit side had the mini-VDD map the 128 KiB register window and a
 * 64 KiB ring window at BAR0 + 240 MiB, past the 16 MiB heap (cedar_hw16.c).
 * VRAM is linear behind BAR0 from MC_VM_FB_LOCATION's start, so a heap
 * offset's GPU address is that start, plus the desktop's offset into BAR0,
 * plus the offset; the ring's is the start plus 240 MiB. BAR0 is 256 MiB
 * aligned (checked by the 16-bit side), so the desktop's offset into it is
 * its physical address's low 28 bits.
 *
 * Once per mode, validation stops the ring, pulses the DMA block's soft
 * reset and starts the ring as Linux r600_dma_resume does, without
 * read-pointer writeback or interrupts. The probe found that a ring stopped
 * the r600_dma_stop way leaves the engine unable to run the next one until
 * that reset. It then runs a fence through the ring and claims the engine
 * only if the fence lands.
 *
 * Work is queued, not waited for: a fill or copy returns once its packets
 * are in the ring. Idle is the read pointer at the write pointer with
 * SRBM_STATUS2's DMA_BUSY clear; DMA_STATUS_REG's bit 0 is not an idle flag
 * on this part (same record). The CPU wrote the ring, and may have written a
 * source surface, through BAR0, so HDP is flushed before the write pointer
 * moves; it is flushed again when the engine goes idle having drawn, before
 * the CPU reads what it drew, as the probe did.
 *
 * There is no recovery path yet. A timeout raises idle_timeouts, gives the
 * engine up for the rest of the mode and declines the operation to the CPU.
 */
#include "ddhal_internal.h"
#include "velocity9x/eg_dma.h"

/* Bounded busy-wait. The probe's fences landed within 3 polls; this bound
 * only has to tell slow from stuck. */
#define V9X_EG_WAIT_SPINS 0x00200000ul

/* CANBLT's non-blocking poll. */
#define V9X_EG_QUICK_READS 16ul

/* Register reads standing in for evergreen_gpu_soft_reset's udelay(50):
 * an uncached PCIe read takes on the order of a microsecond. */
#define V9X_EG_RESET_DELAY_READS 2000ul

/* BAR0 is 256 MiB aligned: a physical address's offset into it. */
#define V9X_EG_BAR0_OFFSET_MASK 0x0ffffffful

/* CONFIG_MEMSIZE in MiB: the 16-bit side refused anything outside this. */
#define V9X_EG_MEMSIZE_MIN_MIB 256ul
#define V9X_EG_MEMSIZE_MAX_MIB 4096ul

/* The validation fence's value. */
#define V9X_EG_FENCE_VALUE 0x45474453ul /* "EGDS" */

/* Packets staged before they go into the ring at once. */
#define V9X_EG_STAGE_DWORDS 256ul

/* Fault-trace tag: "EG2D". */
#define V9X_EG_FAULT_TAG 0x45473244ul

/* One engine, so one set of statics: V9XHAL's statics are shared by every
 * process, which is what this needs. The write pointer is in dwords. */
static DWORD v9x_eg_wptr;
static DWORD v9x_eg_heap_gpu;
static DWORD v9x_eg_ring_gpu;
static int v9x_eg_dirty;

static int v9x_eg_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_ATI_EVERGREEN &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_EG_MMIO_BYTES &&
        v9x_hal->engine.ring_linear_base != 0ul &&
        v9x_hal->engine.ring_bytes >= V9X_EG_RING_WINDOW_BYTES;
}

static DWORD v9x_eg_read32(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_eg_write32(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

static volatile DWORD *v9x_eg_ring(void)
{
    return (volatile DWORD *)v9x_hal->engine.ring_linear_base;
}

static void v9x_eg_delay(void)
{
    DWORD reads;

    for (reads = 0ul; reads < V9X_EG_RESET_DELAY_READS; ++reads) {
        (void)v9x_eg_read32(V9X_EG_SRBM_STATUS2);
    }
}

static void v9x_eg_hdp_flush(void)
{
    v9x_eg_write32(V9X_EG_HDP_MEM_COHERENCY_FLUSH, 1ul);
}

/* Non-zero once the read pointer has reached the write pointer and the
 * block says it is not busy, within the given reads. */
static int v9x_eg_poll_idle(DWORD reads)
{
    DWORD wptr_bytes = v9x_eg_wptr * 4ul;

    while (reads-- != 0ul) {
        if (v9x_eg_read32(V9X_EG_DMA_RB_RPTR) == wptr_bytes &&
            (v9x_eg_read32(V9X_EG_SRBM_STATUS2) & V9X_EG_DMA_BUSY) == 0ul) {
            return 1;
        }
    }
    return 0;
}

/* Moves the write pointer to the engine, after the CPU's writes are out of
 * the write-combining buffers and through HDP. */
static void v9x_eg_kick(void)
{
    v9x_wc_drain();
    v9x_eg_hdp_flush();
    v9x_eg_write32(V9X_EG_DMA_RB_WPTR, v9x_eg_wptr * 4ul);
}

/*
 * Puts count dwords into the ring, padded with NOPs to the 16-dword
 * boundary Linux keeps, and starts them. At the end of the ring the rest is
 * NOPs, the write pointer wraps to 0, and nothing more is written until the
 * engine has read up to it. Non-zero on success.
 */
static int v9x_eg_submit(const v9x_u32 *dwords, DWORD count)
{
    volatile DWORD *ring = v9x_eg_ring();
    DWORD index;

    if (count > V9X_EG_STAGE_DWORDS) {
        return 0;
    }
    /* The write pointer is always on the 16-dword boundary, and a stage
     * fits the ring many times, so after a wrap there is room. */
    if (v9x_eg_wptr + count + v9x_eg_dma_pad(count) > V9X_EG_RING_DWORDS) {
        while (v9x_eg_wptr < V9X_EG_RING_DWORDS) {
            ring[v9x_eg_wptr++] = V9X_EG_DMA_NOP;
        }
        v9x_eg_wptr = 0ul;
        v9x_eg_kick();
        if (!v9x_eg_poll_idle(V9X_EG_WAIT_SPINS)) {
            return 0;
        }
    }
    for (index = 0ul; index < count; ++index) {
        ring[v9x_eg_wptr++] = dwords[index];
    }
    while ((v9x_eg_wptr % V9X_EG_DMA_ALIGN_DWORDS) != 0ul) {
        ring[v9x_eg_wptr++] = V9X_EG_DMA_NOP;
    }
    v9x_eg_kick();
    return 1;
}

static int v9x_eg_validated(void)
{
    return v9x_eg_ready() &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul;
}

/* evergreen_gpu_soft_reset for the DMA block alone: ring off, a wait, the
 * reset bit set and read back, a wait, cleared and read back, a wait.
 * Linux also stops the memory controller around it; the probe did not and
 * the display was unharmed. */
static void v9x_eg_reset(void)
{
    DWORD value;

    v9x_eg_write32(V9X_EG_DMA_RB_CNTL,
                   v9x_eg_read32(V9X_EG_DMA_RB_CNTL) & ~V9X_EG_DMA_RB_ENABLE);
    v9x_eg_delay();
    value = v9x_eg_read32(V9X_EG_SRBM_SOFT_RESET) | V9X_EG_SOFT_RESET_DMA;
    v9x_eg_write32(V9X_EG_SRBM_SOFT_RESET, value);
    (void)v9x_eg_read32(V9X_EG_SRBM_SOFT_RESET);
    v9x_eg_delay();
    v9x_eg_write32(V9X_EG_SRBM_SOFT_RESET, value & ~V9X_EG_SOFT_RESET_DMA);
    (void)v9x_eg_read32(V9X_EG_SRBM_SOFT_RESET);
    v9x_eg_delay();
}

/*
 * Once per mode: the window must be this card's and its VRAM below 4 GiB;
 * the block is reset, the ring started, and a fence run through it. Any
 * refusal leaves the engine unclaimed and DirectDraw on the CPU.
 */
static int v9x_eg_validate(void)
{
    volatile DWORD *ring;
    v9x_u32 fence[V9X_EG_DMA_FENCE_DWORDS];
    DWORD memsize;
    DWORD fb_start;
    DWORD ok;
    DWORD spins;

    if (!v9x_eg_ready()) {
        return 0;
    }
    if ((v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul) {
        return 1;
    }
    memsize = v9x_eg_read32(V9X_EG_CONFIG_MEMSIZE);
    if (memsize < V9X_EG_MEMSIZE_MIN_MIB || memsize > V9X_EG_MEMSIZE_MAX_MIB) {
        return 0;
    }
    fb_start = v9x_eg_fb_start(v9x_eg_read32(V9X_EG_MC_VM_FB_LOCATION), &ok);
    if (ok == 0ul ||
        fb_start > 0xfffffffful - V9X_EG_RING_WINDOW_OFFSET -
                   V9X_EG_RING_WINDOW_BYTES) {
        return 0;
    }
    v9x_eg_heap_gpu = fb_start +
        (v9x_hal->fb.physical_base & V9X_EG_BAR0_OFFSET_MASK);
    v9x_eg_ring_gpu = fb_start + V9X_EG_RING_WINDOW_OFFSET;

    v9x_eg_reset();

    /* r600_dma_resume's order, without writeback or interrupts: DMA_CNTL's
     * context-empty interrupt, which the BIOS leaves set, is cleared because
     * nothing here services it. */
    v9x_eg_write32(V9X_EG_DMA_SEM_INCOMPLETE, 0ul);
    v9x_eg_write32(V9X_EG_DMA_SEM_WAIT_FAIL, 0ul);
    v9x_eg_write32(V9X_EG_DMA_RB_CNTL,
                   V9X_EG_RING_LOG2_DWORDS << V9X_EG_DMA_RB_SIZE_SHIFT);
    v9x_eg_write32(V9X_EG_DMA_RB_RPTR, 0ul);
    v9x_eg_write32(V9X_EG_DMA_RB_WPTR, 0ul);
    v9x_eg_write32(V9X_EG_DMA_RB_RPTR_ADDR_HI, 0ul);
    v9x_eg_write32(V9X_EG_DMA_RB_RPTR_ADDR_LO, 0ul);
    v9x_eg_write32(V9X_EG_DMA_RB_BASE, v9x_eg_ring_gpu >> 8);
    v9x_eg_write32(V9X_EG_DMA_IB_CNTL, V9X_EG_DMA_IB_ENABLE);
    v9x_eg_write32(V9X_EG_DMA_CNTL,
                   v9x_eg_read32(V9X_EG_DMA_CNTL) &
                   ~V9X_EG_CTXEMPTY_INT_ENABLE);
    v9x_eg_write32(V9X_EG_DMA_MODE, 1ul);
    v9x_eg_wptr = 0ul;

    ring = v9x_eg_ring();
    ring[V9X_EG_RING_FENCE_OFFSET / 4ul] = 0ul;
    v9x_eg_hdp_flush();
    v9x_eg_write32(V9X_EG_DMA_RB_CNTL,
                   (V9X_EG_RING_LOG2_DWORDS << V9X_EG_DMA_RB_SIZE_SHIFT) |
                   V9X_EG_DMA_RB_ENABLE);

    (void)v9x_eg_dma_fence(v9x_eg_ring_gpu + V9X_EG_RING_FENCE_OFFSET,
                           V9X_EG_FENCE_VALUE, fence);
    if (!v9x_eg_submit(fence, V9X_EG_DMA_FENCE_DWORDS)) {
        return 0;
    }
    for (spins = 0ul; spins < V9X_EG_WAIT_SPINS; ++spins) {
        if (ring[V9X_EG_RING_FENCE_OFFSET / 4ul] == V9X_EG_FENCE_VALUE) {
            break;
        }
    }
    if (spins == V9X_EG_WAIT_SPINS || !v9x_eg_poll_idle(V9X_EG_WAIT_SPINS)) {
        v9x_eg_reset();
        return 0;
    }
    v9x_eg_dirty = 0;
    v9x_hal->engine.flags |= V9X_DD_ENGINE_STATUS_VALIDATED;
    return 1;
}

/*
 * A real timeout gives up on the engine for the rest of the mode, as the
 * Rage 128's does: the descriptor is invalidated, so v9x_engine32() answers
 * none, blits and fills fall to the CPU, and Lock's drain completes rather
 * than answering WASSTILLDRAWING for ever. The fault trace is written on the
 * first timeout only.
 */
static int v9x_eg_dma_idle(int wait)
{
    DWORD reads;

    if (!v9x_eg_validate()) {
        return 0;
    }
    if (wait && v9x_fault_injected()) {
        ++v9x_hal->engine.idle_timeouts;
        v9x_trace_flush_fault(V9X_EG_FAULT_TAG, V9X_EG_DMA_RB_RPTR);
        return 0;
    }
    reads = wait ? V9X_EG_WAIT_SPINS : V9X_EG_QUICK_READS;
    if (v9x_eg_poll_idle(reads)) {
        if (v9x_eg_dirty) {
            v9x_eg_hdp_flush();
            v9x_eg_dirty = 0;
        }
        return 1;
    }
    if (!wait) {
        return 0;
    }
    if (v9x_hal->engine.idle_timeouts++ == 0ul) {
        v9x_trace_flush_fault(V9X_EG_FAULT_TAG, V9X_EG_DMA_RB_RPTR);
    }
    v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
    return 0;
}

/* Idle for whatever needs the drawing: the DMA engine's work, and the
 * CP's (eg_cp.c), which shares the same VRAM. */
static int v9x_eg_wait_idle(int wait)
{
    return v9x_eg_dma_idle(wait) && v9x_eg_cp_drain(wait);
}

int v9x_eg_dma_settle(void)
{
    if (!v9x_eg_validated() || !v9x_eg_dirty) {
        return 1;
    }
    return v9x_eg_dma_idle(1);
}

/* Before a fill or copy, the CP's drawing finished: the DMA engine must
 * not read a surface the CP is still writing, or write one it still reads.
 * A CP that times out here is given up by eg_cp.c and its work dropped, so
 * the DMA operation goes ahead either way. */
static void v9x_eg_after_cp(void)
{
    (void)v9x_eg_cp_drain(1);
}

/* Every row of a plan into the ring, a stage at a time. */
static int v9x_eg_emit(const struct v9x_eg_dma_plan *plan)
{
    v9x_u32 stage[V9X_EG_STAGE_DWORDS];
    DWORD used;
    DWORD row;

    used = 0ul;
    for (row = 0ul; row < plan->rows; ++row) {
        if (used + V9X_EG_DMA_PACKET_DWORDS_MAX > V9X_EG_STAGE_DWORDS) {
            if (!v9x_eg_submit(stage, used)) {
                return 0;
            }
            used = 0ul;
        }
        used += v9x_eg_dma_plan_row(plan, row, stage + used);
    }
    if (used != 0ul && !v9x_eg_submit(stage, used)) {
        return 0;
    }
    v9x_eg_dirty = 1;
    v9x_present_note_submission();
    return 1;
}

/* A failed submission is a ring that never drained: the engine is given up
 * as a timed-out wait would. */
static int v9x_eg_emit_or_give_up(const struct v9x_eg_dma_plan *plan)
{
    if (v9x_eg_emit(plan)) {
        return V9X_BLT_DONE;
    }
    if (v9x_hal->engine.idle_timeouts++ == 0ul) {
        v9x_trace_flush_fault(V9X_EG_FAULT_TAG, V9X_EG_DMA_RB_RPTR);
    }
    v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
    return V9X_BLT_BUSY;
}

/*
 * A Z buffer the Direct3D engine draws is 1D tiled (d3d_eg.c), so a linear
 * rectangle of it is the wrong pixels. A constant over all of it is the
 * same value in every pixel whatever the tiling, so the whole surface is
 * filled - its rows padded to the tile as d3d_eg.c placed them - and only
 * a fill of the whole surface is taken; the core fails a declined one
 * rather than fill linearly (v9x_depthfill_body). Zero to decline.
 */
#define V9X_EG_DEPTH_TILE_ROWS 8ul

static int v9x_eg_tiled_depth_fill(const V9X_DDHAL_BLTDATA *data,
                                   struct v9x_eg_dma_fill *fill)
{
    const V9X_DD_SURFACE_LCL *surface = data->lpDDDestSurface;

    if ((surface->ddsCaps & V9X_DDSCAPS_ZBUFFER) == 0ul ||
        !v9x_d3d_depth_tiled()) {
        return 1;
    }
    if (fill->left != 0ul || fill->top != 0ul ||
        fill->width != (DWORD)surface->lpGbl->wWidth ||
        fill->height != (DWORD)surface->lpGbl->wHeight) {
        return 0;
    }
    fill->width = fill->pitch_bytes / fill->bytes_per_pixel;
    fill->height = (fill->height + V9X_EG_DEPTH_TILE_ROWS - 1ul) &
                   ~(V9X_EG_DEPTH_TILE_ROWS - 1ul);
    return 1;
}

static int v9x_eg_fill(V9X_DDHAL_BLTDATA *data, DWORD offset,
                       DWORD bytes_per_pixel, int wait)
{
    struct v9x_eg_dma_fill fill;
    struct v9x_eg_dma_plan plan;

    (void)wait;
    if (!v9x_eg_validate() || data == 0 || data->lpDDDestSurface == 0 ||
        data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        data->rDest[2] <= data->rDest[0] ||
        data->rDest[3] <= data->rDest[1] ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    fill.gpu_base = v9x_eg_heap_gpu;
    fill.vram_bytes = v9x_hal->fb.vram_bytes;
    fill.target_offset = offset;
    fill.pitch_bytes = (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    fill.bytes_per_pixel = bytes_per_pixel;
    fill.left = (DWORD)data->rDest[0];
    fill.top = (DWORD)data->rDest[1];
    fill.width = (DWORD)(data->rDest[2] - data->rDest[0]);
    fill.height = (DWORD)(data->rDest[3] - data->rDest[1]);
    fill.color = data->bltFX.dwFillColor;
    if (!v9x_eg_tiled_depth_fill(data, &fill)) {
        return V9X_BLT_DECLINED;
    }
    if (v9x_eg_dma_plan_fill(&fill, &plan) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    v9x_eg_after_cp();
    return v9x_eg_emit_or_give_up(&plan);
}

static int v9x_eg_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                       DWORD destination_offset, DWORD bytes_per_pixel,
                       int wait)
{
    struct v9x_eg_dma_copy copy;
    struct v9x_eg_dma_plan plan;

    (void)wait;
    if (!v9x_eg_validate() || data == 0 ||
        data->lpDDSrcSurface == 0 || data->lpDDSrcSurface->lpGbl == 0 ||
        data->lpDDDestSurface == 0 || data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDSrcSurface->lpGbl->lPitch <= 0l ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        data->rSrc[2] <= data->rSrc[0] || data->rSrc[3] <= data->rSrc[1] ||
        data->rSrc[0] < 0l || data->rSrc[1] < 0l ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    copy.gpu_base = v9x_eg_heap_gpu;
    copy.vram_bytes = v9x_hal->fb.vram_bytes;
    copy.source_offset = source_offset;
    copy.source_pitch_bytes = (DWORD)data->lpDDSrcSurface->lpGbl->lPitch;
    copy.destination_offset = destination_offset;
    copy.destination_pitch_bytes =
        (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    copy.bytes_per_pixel = bytes_per_pixel;
    copy.source_left = (DWORD)data->rSrc[0];
    copy.source_top = (DWORD)data->rSrc[1];
    copy.destination_left = (DWORD)data->rDest[0];
    copy.destination_top = (DWORD)data->rDest[1];
    copy.width = (DWORD)(data->rSrc[2] - data->rSrc[0]);
    copy.height = (DWORD)(data->rSrc[3] - data->rSrc[1]);
    if (v9x_eg_dma_plan_copy(&copy, &plan) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    v9x_eg_after_cp();
    return v9x_eg_emit_or_give_up(&plan);
}

/* CANBLT is the non-blocking idle poll, as on the Rage 128. */
static int v9x_eg_can_blt(void)
{
    return v9x_eg_wait_idle(0);
}

const V9X_ENGINE32_OPS v9x_engine32_eg = {
    v9x_eg_ready,
    v9x_eg_validate,
    v9x_eg_validated,
    v9x_eg_can_blt,
    v9x_eg_wait_idle,
    v9x_eg_fill,
    v9x_eg_copy
};
