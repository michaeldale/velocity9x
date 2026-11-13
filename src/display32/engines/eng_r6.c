/*
 * ATI R6xx async DMA engine for V9XHAL.DLL: DirectDraw solid fill and
 * screen copy on the Radeon HD 2600 XT (RV630).
 *
 * R6xx has no fixed-function 2D engine. The DMA engine copies linear runs
 * of VRAM, so a rectangle is one COPY a row (src\chipsets\ati\r600\
 * r6_dma.c, host-tested), queued on a ring in VRAM and executed without
 * microcode. eng_eg.c, Evergreen's, is the model: same ring window, same
 * once-per-mode reset and start. What differs is what M-NZHWBR's write
 * probe measured on this chip
 * (docs\decisions\2026-11-13-rv630-dma-write-probe.md):
 *
 *   - Inside one submission a copy did not reliably see an earlier
 *     packet's write. So every operation waits for the engine to finish
 *     the last one before it is queued, and the builder never has a packet
 *     read what another packet of the same operation wrote.
 *   - There is no constant fill. A fill's first row is written by the CPU
 *     through the heap mapping, drained and flushed through HDP before the
 *     write pointer moves, and every other row is copied from it.
 *   - The DMA soft reset is SRBM_SOFT_RESET bit 12; there is no DMA_MODE
 *     and no SRBM_STATUS2. Idle here is the read pointer at the write
 *     pointer, the last submission's sequence fence landed, and
 *     DMA_STATUS_REG's DMA_IDLE set (r600_gpu_check_soft_reset's test),
 *     which read set after every probe run.
 *
 * The 16-bit side had the mini-VDD map the register window and the ring
 * window at BAR0 + 240 MiB (cedar_hw16.c's R6xx row, EG_MAP). VRAM is linear behind
 * BAR0 from MC_VM_FB_LOCATION's start, E0000000 on M-NZHWBR, so a heap
 * offset's GPU address is that start plus the desktop's offset into BAR0
 * plus the offset. BAR0 is 256 MiB aligned (checked by the 16-bit side).
 *
 * There is no recovery path. A timeout raises idle_timeouts, writes
 * C:\V9XDIAG\V9XR6DMA.TXT once, gives the engine up for the rest of the
 * mode and declines the operation to the CPU.
 */
#include "ddhal_internal.h"
#include "velocity9x/diagpaths.h"
#include "velocity9x/r6_dma.h"

/* Bounded busy-wait. The probe's fences landed within 2 polls; this bound
 * only has to tell slow from stuck. */
#define V9X_R6_WAIT_SPINS 0x00200000ul

/* CANBLT's non-blocking poll. */
#define V9X_R6_QUICK_READS 16ul

/* Register reads standing in for r600_gpu_soft_reset's udelay(50): an
 * uncached PCIe read takes on the order of a microsecond. */
#define V9X_R6_RESET_DELAY_READS 2000ul

/* BAR0 is 256 MiB aligned: a physical address's offset into it. */
#define V9X_R6_BAR0_OFFSET_MASK 0x0ffffffful

/* Packets staged before they go into the ring at once; a fence follows
 * each operation's last stage. */
#define V9X_R6_STAGE_DWORDS 256ul

/* Fault-trace tag: "R62D". */
#define V9X_R6_FAULT_TAG 0x52363244ul

/* One engine, so one set of statics: V9XHAL's statics are shared by every
 * process, which is what this needs. The write pointer is in dwords; the
 * sequence is the value the last submitted fence writes. */
static DWORD v9x_r6_wptr;
static DWORD v9x_r6_gpu_high;
static DWORD v9x_r6_heap_gpu;
static DWORD v9x_r6_ring_gpu;
static DWORD v9x_r6_sequence;
static int v9x_r6_dirty;

static int v9x_r6_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_ATI_R600 &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_R6_MMIO_BYTES &&
        v9x_hal->engine.ring_linear_base != 0ul &&
        v9x_hal->engine.ring_bytes >= V9X_R6_RING_WINDOW_BYTES &&
        v9x_hal->fb.linear_base != 0ul;
}

static DWORD v9x_r6_read32(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_r6_write32(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

static volatile DWORD *v9x_r6_ring(void)
{
    return (volatile DWORD *)v9x_hal->engine.ring_linear_base;
}

static DWORD v9x_r6_fence_value(void)
{
    return v9x_r6_ring()[V9X_R6_RING_FENCE_OFFSET / 4ul];
}

static void v9x_r6_delay(void)
{
    DWORD reads;

    for (reads = 0ul; reads < V9X_R6_RESET_DELAY_READS; ++reads) {
        (void)v9x_r6_read32(V9X_R6_SRBM_STATUS);
    }
}

static void v9x_r6_hdp_flush(void)
{
    v9x_r6_write32(V9X_R6_HDP_MEM_COHERENCY_FLUSH, 1ul);
}

/* Non-zero once the engine has read up to the write pointer, written the
 * last fence, and says it is idle, within the given reads. */
static int v9x_r6_poll_idle(DWORD reads)
{
    DWORD wptr_bytes = v9x_r6_wptr * 4ul;

    while (reads-- != 0ul) {
        if (v9x_r6_read32(V9X_R6_DMA_RB_RPTR) == wptr_bytes &&
            v9x_r6_fence_value() == v9x_r6_sequence &&
            (v9x_r6_read32(V9X_R6_DMA_STATUS_REG) & V9X_R6_DMA_IDLE) != 0ul) {
            return 1;
        }
    }
    return 0;
}

/* Moves the write pointer to the engine, after the CPU's writes are out of
 * the write-combining buffers and through HDP. */
static void v9x_r6_kick(void)
{
    v9x_wc_drain();
    v9x_r6_hdp_flush();
    v9x_r6_write32(V9X_R6_DMA_RB_WPTR, v9x_r6_wptr * 4ul);
}

/*
 * Puts count dwords into the ring, padded with NOPs to the 16-dword
 * boundary Linux keeps, and starts them. At the end of the ring the rest is
 * NOPs, the write pointer wraps to 0, and nothing more is written until the
 * engine has read up to it. Non-zero on success.
 */
static int v9x_r6_submit(const v9x_u32 *dwords, DWORD count)
{
    volatile DWORD *ring = v9x_r6_ring();
    DWORD index;

    if (count > V9X_R6_STAGE_DWORDS) {
        return 0;
    }
    if (v9x_r6_wptr + count + v9x_r6_dma_pad(count) > V9X_R6_RING_DWORDS) {
        while (v9x_r6_wptr < V9X_R6_RING_DWORDS) {
            ring[v9x_r6_wptr++] = V9X_R6_DMA_NOP;
        }
        v9x_r6_wptr = 0ul;
        v9x_r6_kick();
        if (!v9x_r6_poll_idle(V9X_R6_WAIT_SPINS)) {
            return 0;
        }
    }
    for (index = 0ul; index < count; ++index) {
        ring[v9x_r6_wptr + index] = dwords[index];
    }
    for (index = v9x_r6_wptr + count;
         (index % V9X_R6_DMA_ALIGN_DWORDS) != 0ul; ++index) {
        ring[index] = V9X_R6_DMA_NOP;
    }
    v9x_r6_wptr = v9x_r6_dma_ring_advance(v9x_r6_wptr, count);
    v9x_r6_kick();
    if (v9x_r6_wptr == 0ul && !v9x_r6_poll_idle(V9X_R6_WAIT_SPINS)) {
        return 0;
    }
    return 1;
}

static int v9x_r6_validated(void)
{
    return v9x_r6_ready() &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul;
}

/* r600_gpu_soft_reset for the DMA block alone: ring off, a wait, the reset
 * bit set and read back, a wait, cleared and read back, a wait. Linux also
 * stops the memory controller around it; the probe did not, nine runs, and
 * the display was unharmed. */
static void v9x_r6_reset(void)
{
    DWORD value;

    v9x_r6_write32(V9X_R6_DMA_RB_CNTL,
                   v9x_r6_read32(V9X_R6_DMA_RB_CNTL) & ~V9X_R6_DMA_RB_ENABLE);
    v9x_r6_delay();
    value = v9x_r6_read32(V9X_R6_SRBM_SOFT_RESET) | V9X_R6_SOFT_RESET_DMA;
    v9x_r6_write32(V9X_R6_SRBM_SOFT_RESET, value);
    (void)v9x_r6_read32(V9X_R6_SRBM_SOFT_RESET);
    v9x_r6_delay();
    v9x_r6_write32(V9X_R6_SRBM_SOFT_RESET, value & ~V9X_R6_SOFT_RESET_DMA);
    (void)v9x_r6_read32(V9X_R6_SRBM_SOFT_RESET);
    v9x_r6_delay();
}

/* A fence writing the next sequence value, as one submission. */
static int v9x_r6_submit_fence(void)
{
    v9x_u32 fence[V9X_R6_DMA_FENCE_DWORDS];

    ++v9x_r6_sequence;
    (void)v9x_r6_dma_fence(v9x_r6_gpu_high,
                           v9x_r6_ring_gpu + V9X_R6_RING_FENCE_OFFSET,
                           v9x_r6_sequence, fence);
    return v9x_r6_submit(fence, V9X_R6_DMA_FENCE_DWORDS);
}

/*
 * Once per mode: the window must be this card's and its address must not
 * carry out of its low half; the block is reset, the ring started as
 * r600_dma_resume starts it (no writeback, no interrupts, no DMA_MODE),
 * and a fence run through it. Any refusal leaves the engine unclaimed and
 * DirectDraw on the CPU.
 */
static int v9x_r6_validate(void)
{
    DWORD memsize;
    v9x_u32 fb_high;
    v9x_u32 fb_low;

    if (!v9x_r6_ready()) {
        return 0;
    }
    if ((v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul) {
        return 1;
    }
    memsize = v9x_r6_read32(V9X_R6_CONFIG_MEMSIZE);
    if (memsize < V9X_R6_MEMSIZE_MIN || memsize > V9X_R6_MEMSIZE_MAX) {
        return 0;
    }
    if (v9x_r6_fb_start(v9x_r6_read32(V9X_R6_MC_VM_FB_LOCATION), &fb_high,
                        &fb_low) != V9X_STATUS_OK) {
        return 0;
    }
    v9x_r6_gpu_high = fb_high;
    v9x_r6_heap_gpu = fb_low +
        (v9x_hal->fb.physical_base & V9X_R6_BAR0_OFFSET_MASK);
    v9x_r6_ring_gpu = fb_low + V9X_R6_RING_WINDOW_OFFSET;

    v9x_r6_reset();

    /* DMA_CNTL's context-empty interrupt, which the BIOS leaves set, is
     * cleared because nothing here services it. */
    v9x_r6_write32(V9X_R6_DMA_SEM_INCOMPLETE, 0ul);
    v9x_r6_write32(V9X_R6_DMA_SEM_WAIT_FAIL, 0ul);
    v9x_r6_write32(V9X_R6_DMA_RB_CNTL,
                   V9X_R6_RING_LOG2_DWORDS << V9X_R6_DMA_RB_SIZE_SHIFT);
    v9x_r6_write32(V9X_R6_DMA_RB_RPTR, 0ul);
    v9x_r6_write32(V9X_R6_DMA_RB_WPTR, 0ul);
    v9x_r6_write32(V9X_R6_DMA_RB_RPTR_ADDR_HI, 0ul);
    v9x_r6_write32(V9X_R6_DMA_RB_RPTR_ADDR_LO, 0ul);
    v9x_r6_write32(V9X_R6_DMA_RB_BASE,
                   v9x_r6_dma_ring_base(v9x_r6_gpu_high, v9x_r6_ring_gpu));
    v9x_r6_write32(V9X_R6_DMA_IB_CNTL, V9X_R6_DMA_IB_ENABLE);
    v9x_r6_write32(V9X_R6_DMA_CNTL,
                   v9x_r6_read32(V9X_R6_DMA_CNTL) &
                   ~V9X_R6_CTXEMPTY_INT_ENABLE);
    v9x_r6_wptr = 0ul;
    v9x_r6_sequence = 0ul;

    v9x_r6_ring()[V9X_R6_RING_FENCE_OFFSET / 4ul] = 0ul;
    v9x_r6_hdp_flush();
    v9x_r6_write32(V9X_R6_DMA_RB_CNTL,
                   (V9X_R6_RING_LOG2_DWORDS << V9X_R6_DMA_RB_SIZE_SHIFT) |
                   V9X_R6_DMA_RB_ENABLE);

    if (!v9x_r6_submit_fence() || !v9x_r6_poll_idle(V9X_R6_WAIT_SPINS)) {
        v9x_r6_reset();
        return 0;
    }
    v9x_r6_dirty = 0;
    v9x_hal->engine.flags |= V9X_DD_ENGINE_STATUS_VALIDATED;
    return 1;
}

/*
 * The first timeout's state, beside the fault trace, which names the
 * timeout but not why the engine stopped: the DMA block's registers, the
 * sequence the fence should hold, and the whole ring.
 */
#define V9X_R6_DMA_LOG_PATH V9X_DIAG_PATH("V9XR6DMA.TXT")

static void v9x_r6_log_text(HANDLE file, const char *text)
{
    DWORD written;
    DWORD length = 0ul;

    while (text[length] != '\0') {
        ++length;
    }
    (void)WriteFile(file, text, length, &written, 0);
}

static void v9x_r6_log_hex(HANDLE file, const char *key, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char text[12];
    DWORD index;

    v9x_r6_log_text(file, key);
    for (index = 0ul; index < 8ul; ++index) {
        text[index] = digits[(value >> ((7ul - index) * 4ul)) & 0xful];
    }
    text[8] = '\r';
    text[9] = '\n';
    text[10] = '\0';
    v9x_r6_log_text(file, text);
}

static void v9x_r6_write_record(void)
{
    volatile DWORD *ring = v9x_r6_ring();
    HANDLE file;
    DWORD index;

    file = CreateFileA(V9X_R6_DMA_LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ,
                       0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    v9x_r6_log_text(file, "[R600Dma]\r\n");
    v9x_r6_log_hex(file, "WptrBytes=", v9x_r6_wptr * 4ul);
    v9x_r6_log_hex(file, "Sequence=", v9x_r6_sequence);
    v9x_r6_log_hex(file, "Fence=", v9x_r6_fence_value());
    v9x_r6_log_hex(file, "DMA_RB_RPTR=", v9x_r6_read32(V9X_R6_DMA_RB_RPTR));
    v9x_r6_log_hex(file, "DMA_RB_WPTR=", v9x_r6_read32(V9X_R6_DMA_RB_WPTR));
    v9x_r6_log_hex(file, "DMA_RB_CNTL=", v9x_r6_read32(V9X_R6_DMA_RB_CNTL));
    v9x_r6_log_hex(file, "DMA_RB_BASE=", v9x_r6_read32(V9X_R6_DMA_RB_BASE));
    v9x_r6_log_hex(file, "DMA_CNTL=", v9x_r6_read32(V9X_R6_DMA_CNTL));
    v9x_r6_log_hex(file, "DMA_STATUS_REG=",
                   v9x_r6_read32(V9X_R6_DMA_STATUS_REG));
    v9x_r6_log_hex(file, "SRBM_STATUS=", v9x_r6_read32(V9X_R6_SRBM_STATUS));
    v9x_r6_log_hex(file, "GRBM_STATUS=", v9x_r6_read32(V9X_R6_GRBM_STATUS));
    v9x_r6_log_hex(file, "GpuHigh=", v9x_r6_gpu_high);
    v9x_r6_log_hex(file, "HeapGpu=", v9x_r6_heap_gpu);
    v9x_r6_log_hex(file, "RingGpu=", v9x_r6_ring_gpu);
    v9x_r6_log_text(file, "[Ring]\r\n");
    for (index = 0ul; index < V9X_R6_RING_DWORDS; ++index) {
        v9x_r6_log_hex(file, "", ring[index]);
    }
    CloseHandle(file);
}

/* A timeout or a ring that never drained gives up on the engine for the
 * rest of the mode, as Evergreen's does: the descriptor is invalidated, so
 * v9x_engine32() answers none, blits and fills fall to the CPU, and Lock's
 * drain completes. The record is written on the first only. */
static void v9x_r6_give_up(void)
{
    if (v9x_hal->engine.idle_timeouts++ == 0ul) {
        v9x_r6_write_record();
        v9x_trace_flush_fault(V9X_R6_FAULT_TAG, V9X_R6_DMA_RB_RPTR);
    }
    v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
}

static int v9x_r6_dma_idle(int wait)
{
    if (!v9x_r6_validate()) {
        return 0;
    }
    if (wait && v9x_fault_injected()) {
        ++v9x_hal->engine.idle_timeouts;
        v9x_trace_flush_fault(V9X_R6_FAULT_TAG, V9X_R6_DMA_RB_RPTR);
        return 0;
    }
    if (v9x_r6_poll_idle(wait ? V9X_R6_WAIT_SPINS : V9X_R6_QUICK_READS)) {
        if (v9x_r6_dirty) {
            v9x_r6_hdp_flush();
            v9x_r6_dirty = 0;
        }
        return 1;
    }
    if (!wait) {
        return 0;
    }
    v9x_r6_give_up();
    return 0;
}

/* Idle for whatever needs the drawing: the DMA engine's work, and the
 * CP's (r6_cp.c), which shares the same VRAM. */
static int v9x_r6_wait_idle(int wait)
{
    return v9x_r6_dma_idle(wait) && v9x_r6_cp_drain(wait);
}

int v9x_r6_dma_settle(void)
{
    if (!v9x_r6_validated() || !v9x_r6_dirty) {
        return 1;
    }
    return v9x_r6_dma_idle(1);
}

/* The heap is linear from its GPU address, and R6xx's D1GRPH surface
 * address is that 32-bit address. */
int v9x_r6_flip_address(DWORD byte_offset, DWORD *address)
{
    if (address == 0 || !v9x_r6_validate() ||
        byte_offset >= v9x_hal->fb.vram_bytes ||
        v9x_r6_heap_gpu + byte_offset < v9x_r6_heap_gpu) {
        return 0;
    }
    *address = v9x_r6_heap_gpu + byte_offset;
    return 1;
}

/*
 * A Z buffer the Direct3D engine draws is 1D tiled (d3d_r6.c), so a linear
 * rectangle of it is the wrong pixels. A constant over all of it is the
 * same value in every pixel whatever the tiling, so the whole surface is
 * filled, its rows padded to the tile as d3d_r6.c placed them; a fill of
 * less than that is drawn on the CP instead (v9x_d3d_r6_depth_fill), as
 * eng_eg.c does for the same reason.
 */
#define V9X_R6_DEPTH_TILE_ROWS 8ul

static int v9x_r6_tiled_depth_fill(const V9X_DDHAL_BLTDATA *data,
                                   struct v9x_r6_dma_fill *fill)
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
    fill->height = (fill->height + V9X_R6_DEPTH_TILE_ROWS - 1ul) &
                   ~(V9X_R6_DEPTH_TILE_ROWS - 1ul);
    return 1;
}

/* The CPU's seed row, through the heap mapping: the engine is idle, so
 * nothing it queued can land on the row after this. */
static void v9x_r6_write_seed(const struct v9x_r6_dma_plan *plan)
{
    volatile DWORD *row;
    DWORD index;

    row = (volatile DWORD *)(v9x_hal->fb.linear_base + plan->seed_offset);
    for (index = 0ul; index < plan->seed_bytes / 4ul; ++index) {
        row[index] = plan->value;
    }
}

/* An operation: the previous one finished, the seed written, every row
 * into the ring a stage at a time, then a fence. */
static int v9x_r6_run(const struct v9x_r6_dma_plan *plan)
{
    v9x_u32 stage[V9X_R6_STAGE_DWORDS];
    DWORD used;
    DWORD row;

    /* The CP's drawing finished first: the DMA engine must not read a
     * surface the CP is still writing, or write one it still reads. A CP
     * that times out is given up by r6_cp.c and its work dropped, so the
     * operation goes ahead either way. */
    (void)v9x_r6_cp_drain(1);
    if (!v9x_r6_poll_idle(V9X_R6_WAIT_SPINS)) {
        v9x_r6_give_up();
        return V9X_BLT_BUSY;
    }
    v9x_r6_write_seed(plan);
    used = 0ul;
    for (row = 0ul; row < plan->rows; ++row) {
        if (used + V9X_R6_DMA_PACKET_DWORDS_MAX > V9X_R6_STAGE_DWORDS) {
            if (!v9x_r6_submit(stage, used)) {
                v9x_r6_give_up();
                return V9X_BLT_BUSY;
            }
            used = 0ul;
        }
        used += v9x_r6_dma_plan_row(plan, row, v9x_r6_gpu_high,
                                    stage + used);
    }
    if (used != 0ul && !v9x_r6_submit(stage, used)) {
        v9x_r6_give_up();
        return V9X_BLT_BUSY;
    }
    /* The seed alone still needs its drain and flush before the CPU or the
     * display is told the fill is done: the fence's submission kicks both. */
    if (!v9x_r6_submit_fence()) {
        v9x_r6_give_up();
        return V9X_BLT_BUSY;
    }
    v9x_r6_dirty = 1;
    v9x_present_note_submission();
    return V9X_BLT_DONE;
}

static int v9x_r6_fill(V9X_DDHAL_BLTDATA *data, DWORD offset,
                       DWORD bytes_per_pixel, int wait)
{
    struct v9x_r6_dma_fill fill;
    struct v9x_r6_dma_plan plan;

    (void)wait;
    if (!v9x_r6_validate() || data == 0 || data->lpDDDestSurface == 0 ||
        data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        data->rDest[2] <= data->rDest[0] ||
        data->rDest[3] <= data->rDest[1] ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    fill.gpu_base = v9x_r6_heap_gpu;
    fill.vram_bytes = v9x_hal->fb.vram_bytes;
    fill.target_offset = offset;
    fill.pitch_bytes = (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    fill.bytes_per_pixel = bytes_per_pixel;
    fill.left = (DWORD)data->rDest[0];
    fill.top = (DWORD)data->rDest[1];
    fill.width = (DWORD)(data->rDest[2] - data->rDest[0]);
    fill.height = (DWORD)(data->rDest[3] - data->rDest[1]);
    fill.color = data->bltFX.dwFillColor;
    if (!v9x_r6_tiled_depth_fill(data, &fill)) {
        return v9x_d3d_r6_depth_fill(data->lpDDDestSurface, data->rDest,
                                     data->bltFX.dwFillColor, wait);
    }
    if (v9x_r6_dma_plan_fill(&fill, &plan) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    return v9x_r6_run(&plan);
}

static int v9x_r6_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                       DWORD destination_offset, DWORD bytes_per_pixel,
                       int wait)
{
    struct v9x_r6_dma_copy copy;
    struct v9x_r6_dma_plan plan;

    (void)wait;
    if (!v9x_r6_validate() || data == 0 ||
        data->lpDDSrcSurface == 0 || data->lpDDSrcSurface->lpGbl == 0 ||
        data->lpDDDestSurface == 0 || data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDSrcSurface->lpGbl->lPitch <= 0l ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        data->rSrc[2] <= data->rSrc[0] || data->rSrc[3] <= data->rSrc[1] ||
        data->rSrc[0] < 0l || data->rSrc[1] < 0l ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    copy.gpu_base = v9x_r6_heap_gpu;
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
    if (v9x_r6_dma_plan_copy(&copy, &plan) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    return v9x_r6_run(&plan);
}

/* CANBLT is the non-blocking idle poll, as on Evergreen. */
static int v9x_r6_can_blt(void)
{
    return v9x_r6_wait_idle(0);
}

const V9X_ENGINE32_OPS v9x_engine32_r6 = {
    v9x_r6_ready,
    v9x_r6_validate,
    v9x_r6_validated,
    v9x_r6_can_blt,
    v9x_r6_wait_idle,
    v9x_r6_fill,
    v9x_r6_copy
};
