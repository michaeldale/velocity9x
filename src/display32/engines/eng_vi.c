/*
 * AMD Carrizo SDMA engine for V9XHAL.DLL: DirectDraw solid fill and screen
 * copy.
 *
 * Carrizo has no fixed-function 2D engine either. SDMA0 copies and fills
 * linear runs of the carve-out, so a rectangle is Evergreen's plan, one
 * packet a row (src\chipsets\ati\evergreen\eg_dma.c), written as SDMA
 * packets (src\chipsets\ati\vi\vi_sdma.c, both host-tested) on a ring in
 * the carve-out. Unlike Evergreen's DMA engine, SDMA runs AMD microcode;
 * it is written straight into the engine's ucode port, as Linux v4.9 did
 * with smc_load_fw=0. Every step here ran byte-exact on M-XP7053's R7E
 * (docs\decisions\2026-10-28-carrizo-sdma-write-probe.md).
 *
 * The 16-bit side had the mini-VDD map the 256 KiB register window (BAR5)
 * and a 4 MiB ring window at BAR0 + 240 MiB, past the 16 MiB heap
 * (carrizo_hw16.c). The carve-out is linear behind BAR0 from
 * MC_VM_FB_LOCATION's start, which is above 4 GiB (F4_00000000 on
 * M-XP7053), so every GPU address here is a high half, fixed for the
 * carve-out, and a low half: the start's, plus the desktop's offset into
 * BAR0, plus the offset. BAR0 is 256 MiB aligned (checked by the 16-bit
 * side), so the desktop's offset into it is its physical address's low
 * 28 bits.
 *
 * Once per mode, validation halts SDMA0, applies the Carrizo golden
 * settings, loads the microcode, starts the ring in sdma_v3_0_gfx_resume's
 * order with the read-pointer writeback in the window, unhalts it, and runs
 * a fence through the ring; the engine is claimed only if the fence lands.
 * The probe ran that three times in a row without a reset between, so a
 * mode switch restarts it the same way.
 *
 * Every submission ends with a fence carrying the next sequence number,
 * and idle is that number in the fence dword. The read pointer is not the
 * test: in the probe it stopped short of the write pointer, in the NOPs
 * that pad a submission, with the fence already written (run 1: 90h of
 * C0h). The CPU wrote the ring, and may have written a source surface,
 * through BAR0, so HDP is flushed before the write pointer moves; when the
 * engine goes idle having drawn, HDP is flushed and its read cache
 * invalidated before the CPU reads what it drew, as the probe did.
 *
 * There is no recovery path yet. A timeout raises idle_timeouts, gives the
 * engine up for the rest of the mode and declines the operation to the CPU.
 */
#include "ddhal_internal.h"
#include "velocity9x/diagpaths.h"
#include "velocity9x/eg_dma.h"
#include "velocity9x/vi_sdma.h"
#include "velocity9x/vi_sdma_microcode.h"

/* Bounded busy-wait. The probe's fences landed within 9 polls; this bound
 * only has to tell slow from stuck. */
#define V9X_VI_WAIT_SPINS 0x00200000ul

/* CANBLT's non-blocking poll. */
#define V9X_VI_QUICK_READS 16ul

/* BAR0 is 256 MiB aligned: a physical address's offset into it. */
#define V9X_VI_BAR0_OFFSET_MASK 0x0ffffffful

/* CONFIG_MEMSIZE in MiB: the 16-bit side refused anything outside this. */
#define V9X_VI_MEMSIZE_MIN_MIB 256ul
#define V9X_VI_MEMSIZE_MAX_MIB 4096ul

/* Packets staged before they go into the ring at once, with room for the
 * closing fence. */
#define V9X_VI_STAGE_DWORDS 256ul

/* A Z16's rows are padded to the DB's 8-row tile (d3d_vi_map.h). */
#define V9X_VI_DEPTH_TILE_ROWS 8ul

/* Fault-trace tag: "VI2D". */
#define V9X_VI_FAULT_TAG 0x56493244ul

/* One engine, so one set of statics: V9XHAL's statics are shared by every
 * process, which is what this needs. The write pointer is in dwords. */
static DWORD v9x_vi_wptr;
static DWORD v9x_vi_sequence;
static DWORD v9x_vi_gpu_high;
static DWORD v9x_vi_heap_low;
static DWORD v9x_vi_ring_low;
static int v9x_vi_dirty;

static int v9x_vi_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_ATI_VI &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_VI_MMIO_BYTES &&
        v9x_hal->engine.ring_linear_base != 0ul &&
        v9x_hal->engine.ring_bytes >= V9X_VI_RING_WINDOW_BYTES;
}

static DWORD v9x_vi_read32(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_vi_write32(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

/* amdgpu_program_register_sequence's step, and the read-modify-writes of
 * sdma_v3_0.c: clear one mask, set another. */
static void v9x_vi_modify32(DWORD offset, DWORD clear, DWORD set)
{
    v9x_vi_write32(offset, (v9x_vi_read32(offset) & ~clear) | set);
}

static volatile DWORD *v9x_vi_ring(void)
{
    return (volatile DWORD *)v9x_hal->engine.ring_linear_base;
}

static DWORD v9x_vi_fence_dword(void)
{
    return v9x_vi_ring()[V9X_VI_SDMA_FENCE_OFFSET / 4ul];
}

static void v9x_vi_hdp_flush(void)
{
    v9x_vi_write32(V9X_VI_HDP_MEM_COHERENCY_FLUSH, 1ul);
}

/* Non-zero once the fence dword holds the last sequence number sent,
 * within the given reads. */
static int v9x_vi_poll_idle(DWORD reads)
{
    while (reads-- != 0ul) {
        if (v9x_vi_fence_dword() == v9x_vi_sequence) {
            return 1;
        }
    }
    return 0;
}

/* Moves the write pointer to the engine, in bytes, after the CPU's writes
 * are out of the write-combining buffers and through HDP. */
static void v9x_vi_kick(void)
{
    v9x_wc_drain();
    v9x_vi_hdp_flush();
    v9x_vi_write32(V9X_VI_SDMA0_RB_WPTR, v9x_vi_wptr * 4ul);
}

/*
 * Puts count dwords into the ring, then a fence with the next sequence
 * number, padded with NOPs to the 16-dword boundary, and starts them. At
 * the end of the ring the rest is NOPs, the write pointer wraps to 0, and
 * nothing more is written until the engine has passed the last fence.
 * Non-zero on success.
 */
static int v9x_vi_submit(const v9x_u32 *dwords, DWORD count)
{
    volatile DWORD *ring = v9x_vi_ring();
    v9x_u32 fence[V9X_VI_SDMA_FENCE_DWORDS];
    DWORD total;
    DWORD index;

    if (count > V9X_VI_STAGE_DWORDS) {
        return 0;
    }
    total = count + V9X_VI_SDMA_FENCE_DWORDS;
    /* The write pointer is always on the 16-dword boundary, and a stage
     * fits the ring many times, so after a wrap there is room. */
    if (v9x_vi_wptr + total + v9x_vi_sdma_pad(total) >
        V9X_VI_SDMA_RING_DWORDS) {
        if (!v9x_vi_poll_idle(V9X_VI_WAIT_SPINS)) {
            return 0;
        }
        while (v9x_vi_wptr < V9X_VI_SDMA_RING_DWORDS) {
            ring[v9x_vi_wptr++] = V9X_VI_SDMA_NOP;
        }
        v9x_vi_wptr = 0ul;
        v9x_vi_kick();
    }
    for (index = 0ul; index < count; ++index) {
        ring[v9x_vi_wptr + index] = dwords[index];
    }
    (void)v9x_vi_sdma_fence(v9x_vi_ring_low + V9X_VI_SDMA_FENCE_OFFSET,
                            v9x_vi_gpu_high, v9x_vi_sequence + 1ul, fence);
    for (index = 0ul; index < V9X_VI_SDMA_FENCE_DWORDS; ++index) {
        ring[v9x_vi_wptr + count + index] = fence[index];
    }
    for (index = v9x_vi_wptr + total;
         (index % V9X_VI_SDMA_ALIGN_DWORDS) != 0ul; ++index) {
        ring[index] = V9X_VI_SDMA_NOP;
    }
    ++v9x_vi_sequence;
    v9x_vi_wptr = v9x_vi_sdma_ring_advance(v9x_vi_wptr, total);
    v9x_vi_kick();
    /* A submission ending exactly at the ring's end leaves the position at
     * 0: the engine must pass its fence before the next one writes over
     * the ring's start, and nothing else checks space. */
    if (v9x_vi_wptr == 0ul && !v9x_vi_poll_idle(V9X_VI_WAIT_SPINS)) {
        return 0;
    }
    return 1;
}

static int v9x_vi_validated(void)
{
    return v9x_vi_ready() &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul;
}

/* sdma_v3_0_enable(false) and gfx_stop for SDMA0: ring and IBs off, then
 * halted. */
static void v9x_vi_halt(void)
{
    v9x_vi_modify32(V9X_VI_SDMA0_RB_CNTL, V9X_VI_RB_ENABLE, 0ul);
    v9x_vi_modify32(V9X_VI_SDMA0_IB_CNTL, V9X_VI_IB_ENABLE, 0ul);
    v9x_vi_modify32(V9X_VI_SDMA0_F32_CNTL, 0ul, V9X_VI_F32_HALT);
}

/* sdma_v3_0_start for SDMA0 with the microcode loaded directly: the golden
 * settings, the image through SDMA0_UCODE_DATA, then gfx_resume's ring
 * registers and the unhalt. TILING_CONFIG is GB_ADDR_CONFIG & 70h, and
 * GB_ADDR_CONFIG reads Linux's Carrizo golden 22010001h
 * (docs\decisions\2026-10-28-carrizo-register-probe.md), so 0. */
static void v9x_vi_start(void)
{
    DWORD index;

    v9x_vi_halt();
    v9x_vi_modify32(V9X_VI_SDMA0_CNTL, V9X_VI_AUTO_CTXSW, 0ul);
    v9x_vi_modify32(V9X_VI_SDMA0_CHICKEN_BITS, V9X_VI_CHICKEN_CLEAR,
                    V9X_VI_CHICKEN_SET);
    v9x_vi_modify32(V9X_VI_SDMA0_CLK_CTRL, V9X_VI_CLK_CLEAR, 0ul);
    v9x_vi_modify32(V9X_VI_SDMA0_IB_CNTL, 0ul, V9X_VI_IB_CNTL_GOLDEN);
    v9x_vi_modify32(V9X_VI_SDMA0_POWER_CNTL, V9X_VI_POWER_CLEAR,
                    V9X_VI_POWER_SET);
    v9x_vi_modify32(V9X_VI_SDMA0_RLC0_IB_CNTL, 0ul, V9X_VI_IB_CNTL_GOLDEN);
    v9x_vi_modify32(V9X_VI_SDMA0_RLC1_IB_CNTL, 0ul, V9X_VI_IB_CNTL_GOLDEN);

    v9x_vi_write32(V9X_VI_SDMA0_UCODE_ADDR, 0ul);
    for (index = 0ul; index < V9X_VI_SDMA_MICROCODE_DWORDS; ++index) {
        v9x_vi_write32(V9X_VI_SDMA0_UCODE_DATA, v9x_vi_sdma_microcode[index]);
    }
    v9x_vi_write32(V9X_VI_SDMA0_UCODE_ADDR, v9x_vi_sdma_microcode_version);

    v9x_vi_write32(V9X_VI_SDMA0_TILING_CONFIG, 0ul);
    v9x_vi_write32(V9X_VI_SDMA0_SEM_WAIT_FAIL, 0ul);
    v9x_vi_modify32(V9X_VI_SDMA0_RB_CNTL,
                    V9X_VI_RB_SIZE_MASK | V9X_VI_RB_ENABLE,
                    V9X_VI_SDMA_RING_LOG2_DWORDS << V9X_VI_RB_SIZE_SHIFT);
    v9x_vi_write32(V9X_VI_SDMA0_RB_RPTR, 0ul);
    v9x_vi_write32(V9X_VI_SDMA0_RB_WPTR, 0ul);
    v9x_vi_write32(V9X_VI_SDMA0_IB_RPTR, 0ul);
    v9x_vi_write32(V9X_VI_SDMA0_IB_OFFSET, 0ul);
    v9x_vi_write32(V9X_VI_SDMA0_RPTR_ADDR_HI, v9x_vi_gpu_high);
    v9x_vi_write32(V9X_VI_SDMA0_RPTR_ADDR_LO,
                   (v9x_vi_ring_low + V9X_VI_SDMA_WRITEBACK_OFFSET) & ~3ul);
    v9x_vi_modify32(V9X_VI_SDMA0_RB_CNTL, 0ul, V9X_VI_RPTR_WRITEBACK);
    /* RB_BASE is the ring's 40-bit address >> 8; it is 256-byte aligned
     * and its bits 39:32 land in 31:24. */
    v9x_vi_write32(V9X_VI_SDMA0_RB_BASE,
                   (v9x_vi_ring_low >> 8) | (v9x_vi_gpu_high << 24));
    v9x_vi_write32(V9X_VI_SDMA0_RB_BASE_HI, 0ul);
    v9x_vi_write32(V9X_VI_SDMA0_RB_WPTR, 0ul);
    v9x_vi_modify32(V9X_VI_SDMA0_DOORBELL, V9X_VI_DOORBELL_ENABLE, 0ul);
    v9x_vi_hdp_flush();
    v9x_vi_modify32(V9X_VI_SDMA0_RB_CNTL, 0ul, V9X_VI_RB_ENABLE);
    v9x_vi_modify32(V9X_VI_SDMA0_IB_CNTL, 0ul, V9X_VI_IB_ENABLE);
    v9x_vi_modify32(V9X_VI_SDMA0_F32_CNTL, V9X_VI_F32_HALT, 0ul);
}

/*
 * Once per mode: the window must be this chip's, the engine started, and
 * a fence run through the ring. Any refusal leaves the engine unclaimed
 * and DirectDraw on the CPU.
 */
static int v9x_vi_validate(void)
{
    DWORD memsize;
    DWORD high;
    DWORD low;

    if (!v9x_vi_ready()) {
        return 0;
    }
    if ((v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul) {
        return 1;
    }
    memsize = v9x_vi_read32(V9X_VI_CONFIG_MEMSIZE);
    if (memsize < V9X_VI_MEMSIZE_MIN_MIB || memsize > V9X_VI_MEMSIZE_MAX_MIB) {
        return 0;
    }
    if (v9x_vi_fb_start(v9x_vi_read32(V9X_VI_MC_VM_FB_LOCATION), &high,
                        &low) != V9X_STATUS_OK) {
        return 0;
    }
    v9x_vi_gpu_high = high;
    v9x_vi_heap_low = low +
        (v9x_hal->fb.physical_base & V9X_VI_BAR0_OFFSET_MASK);
    v9x_vi_ring_low = low + V9X_VI_RING_WINDOW_OFFSET;

    /* The fence dword starts at the sequence number, so a fence the engine
     * never writes cannot read as landed. */
    v9x_vi_wptr = 0ul;
    v9x_vi_sequence = 0ul;
    v9x_vi_ring()[V9X_VI_SDMA_FENCE_OFFSET / 4ul] = 0xfffffffful;
    v9x_vi_start();

    if (!v9x_vi_submit(0, 0ul) || !v9x_vi_poll_idle(V9X_VI_WAIT_SPINS)) {
        v9x_vi_halt();
        return 0;
    }
    v9x_vi_dirty = 0;
    v9x_hal->engine.flags |= V9X_DD_ENGINE_STATUS_VALIDATED;
    return 1;
}

/*
 * The first timeout's state, beside the fault trace, which names the
 * timeout but not why the engine stopped: SDMA0's registers and the whole
 * ring.
 */
#define V9X_VI_DMA_LOG_PATH      V9X_DIAG_PATH("V9XVIDMA.TXT")

static void v9x_vi_log_text(HANDLE file, const char *text)
{
    DWORD written;
    DWORD length = 0ul;

    while (text[length] != '\0') {
        ++length;
    }
    (void)WriteFile(file, text, length, &written, 0);
}

static void v9x_vi_log_hex(HANDLE file, const char *key, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char text[12];
    DWORD index;

    v9x_vi_log_text(file, key);
    for (index = 0ul; index < 8ul; ++index) {
        text[index] = digits[(value >> ((7ul - index) * 4ul)) & 0xful];
    }
    text[8] = '\r';
    text[9] = '\n';
    text[10] = '\0';
    v9x_vi_log_text(file, text);
}

static void v9x_vi_write_record(void)
{
    volatile DWORD *ring = v9x_vi_ring();
    HANDLE file;
    DWORD index;

    file = CreateFileA(V9X_VI_DMA_LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ,
                       0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    v9x_vi_log_text(file, "[CarrizoSdma]\r\n");
    v9x_vi_log_hex(file, "WptrBytes=", v9x_vi_wptr * 4ul);
    v9x_vi_log_hex(file, "Sequence=", v9x_vi_sequence);
    v9x_vi_log_hex(file, "FenceDword=", v9x_vi_fence_dword());
    v9x_vi_log_hex(file, "WritebackRptr=",
                   ring[V9X_VI_SDMA_WRITEBACK_OFFSET / 4ul]);
    v9x_vi_log_hex(file, "SDMA0_GFX_RB_RPTR=",
                   v9x_vi_read32(V9X_VI_SDMA0_RB_RPTR));
    v9x_vi_log_hex(file, "SDMA0_GFX_RB_WPTR=",
                   v9x_vi_read32(V9X_VI_SDMA0_RB_WPTR));
    v9x_vi_log_hex(file, "SDMA0_GFX_RB_CNTL=",
                   v9x_vi_read32(V9X_VI_SDMA0_RB_CNTL));
    v9x_vi_log_hex(file, "SDMA0_GFX_RB_BASE=",
                   v9x_vi_read32(V9X_VI_SDMA0_RB_BASE));
    v9x_vi_log_hex(file, "SDMA0_F32_CNTL=",
                   v9x_vi_read32(V9X_VI_SDMA0_F32_CNTL));
    v9x_vi_log_hex(file, "SDMA0_STATUS_REG=",
                   v9x_vi_read32(V9X_VI_SDMA0_STATUS_REG));
    v9x_vi_log_hex(file, "SDMA0_CNTL=", v9x_vi_read32(V9X_VI_SDMA0_CNTL));
    v9x_vi_log_hex(file, "SRBM_STATUS2=", v9x_vi_read32(V9X_VI_SRBM_STATUS2));
    v9x_vi_log_hex(file, "GpuHigh=", v9x_vi_gpu_high);
    v9x_vi_log_hex(file, "HeapLow=", v9x_vi_heap_low);
    v9x_vi_log_hex(file, "RingLow=", v9x_vi_ring_low);
    v9x_vi_log_text(file, "[Ring]\r\n");
    for (index = 0ul; index < V9X_VI_SDMA_RING_DWORDS; ++index) {
        v9x_vi_log_hex(file, "", ring[index]);
    }
    CloseHandle(file);
}

static void v9x_vi_give_up(void)
{
    if (v9x_hal->engine.idle_timeouts++ == 0ul) {
        v9x_vi_write_record();
        v9x_trace_flush_fault(V9X_VI_FAULT_TAG, V9X_VI_SDMA0_RB_RPTR);
    }
    v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
}

/*
 * A real timeout gives up on the engine for the rest of the mode, as the
 * Evergreen and Rage 128 ones do: the descriptor is invalidated, so
 * v9x_engine32() answers none, blits and fills fall to the CPU, and Lock's
 * drain completes rather than answering WASSTILLDRAWING for ever.
 */
static int v9x_vi_dma_idle(int wait)
{
    DWORD reads;

    if (!v9x_vi_validate()) {
        return 0;
    }
    if (wait && v9x_fault_injected()) {
        ++v9x_hal->engine.idle_timeouts;
        v9x_trace_flush_fault(V9X_VI_FAULT_TAG, V9X_VI_SDMA0_RB_RPTR);
        return 0;
    }
    reads = wait ? V9X_VI_WAIT_SPINS : V9X_VI_QUICK_READS;
    if (v9x_vi_poll_idle(reads)) {
        if (v9x_vi_dirty) {
            v9x_vi_hdp_flush();
            v9x_vi_write32(V9X_VI_HDP_DEBUG0, 1ul);
            v9x_vi_dirty = 0;
        }
        return 1;
    }
    if (!wait) {
        return 0;
    }
    v9x_vi_give_up();
    return 0;
}

/* Idle for whatever needs the drawing: SDMA's work, and the graphics CP's
 * (vi_cp.c), which shares the same VRAM. */
static int v9x_vi_wait_idle(int wait)
{
    return v9x_vi_dma_idle(wait) && v9x_vi_cp_drain(wait);
}

/* Before a flip or a 3D batch: the last fill or copy finished, so the
 * frame shown is the frame drawn and a batch never reads what a fill has
 * yet to write. SDMA alone: the CP's batches follow one another in its
 * ring. */
int v9x_vi_dma_settle(void)
{
    if (!v9x_vi_validated() || !v9x_vi_dirty) {
        return 1;
    }
    return v9x_vi_dma_idle(1);
}

/* The heap's GPU address, once the engine is validated, for the scanout
 * controller (eg_scanout.c): the carve-out's high half and the heap's low
 * half. */
int v9x_vi_heap_address(DWORD *high, DWORD *low)
{
    if (!v9x_vi_validate()) {
        return 0;
    }
    *high = v9x_vi_gpu_high;
    *low = v9x_vi_heap_low;
    return 1;
}

/* The 4 MiB window's GPU address, for the graphics CP (vi_cp.c), whose
 * ring, shaders and vertices share it. */
int v9x_vi_window_address(DWORD *high, DWORD *low)
{
    if (!v9x_vi_validate()) {
        return 0;
    }
    *high = v9x_vi_gpu_high;
    *low = v9x_vi_ring_low;
    return 1;
}

/* Before a fill or copy, the CP's drawing finished: SDMA must not read a
 * surface the CP is still writing, or write one it still reads. A CP that
 * times out here is given up by vi_cp.c and its work dropped, so the SDMA
 * operation goes ahead either way. */
static void v9x_vi_after_cp(void)
{
    (void)v9x_vi_cp_drain(1);
}

/* Every row of a plan into the ring, a stage at a time. */
static int v9x_vi_emit(const struct v9x_eg_dma_plan *plan)
{
    v9x_u32 stage[V9X_VI_STAGE_DWORDS];
    DWORD used;
    DWORD row;

    used = 0ul;
    for (row = 0ul; row < plan->rows; ++row) {
        if (used + V9X_VI_SDMA_ROW_DWORDS_MAX > V9X_VI_STAGE_DWORDS) {
            if (!v9x_vi_submit(stage, used)) {
                return 0;
            }
            used = 0ul;
        }
        used += v9x_vi_sdma_plan_row(plan, row, v9x_vi_gpu_high,
                                     stage + used);
    }
    if (used != 0ul && !v9x_vi_submit(stage, used)) {
        return 0;
    }
    v9x_vi_dirty = 1;
    v9x_present_note_submission();
    return 1;
}

/* A failed submission is a ring that never drained: the engine is given up
 * as a timed-out wait would. */
static int v9x_vi_emit_or_give_up(const struct v9x_eg_dma_plan *plan)
{
    if (v9x_vi_emit(plan)) {
        return V9X_BLT_DONE;
    }
    v9x_vi_give_up();
    return V9X_BLT_BUSY;
}

static int v9x_vi_fill(V9X_DDHAL_BLTDATA *data, DWORD offset,
                       DWORD bytes_per_pixel, int wait)
{
    struct v9x_eg_dma_fill fill;
    struct v9x_eg_dma_plan plan;

    (void)wait;
    if (!v9x_vi_validate() || data == 0 || data->lpDDDestSurface == 0 ||
        data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        data->rDest[2] <= data->rDest[0] ||
        data->rDest[3] <= data->rDest[1] ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    fill.gpu_base = v9x_vi_heap_low;
    fill.vram_bytes = v9x_hal->fb.vram_bytes;
    fill.target_offset = offset;
    fill.pitch_bytes = (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    fill.bytes_per_pixel = bytes_per_pixel;
    fill.left = (DWORD)data->rDest[0];
    fill.top = (DWORD)data->rDest[1];
    fill.width = (DWORD)(data->rDest[2] - data->rDest[0]);
    fill.height = (DWORD)(data->rDest[3] - data->rDest[1]);
    fill.color = data->bltFX.dwFillColor;
    /* A Z16 is 1D tiled (d3d_vi.c places it, rows padded to the 8-row
     * tile): a fill of all of it is a fill of its bytes, padding rows
     * included, but one of part of it would land in the wrong samples, and
     * so would the CPU's. d3d_vi.c draws those on the CP, as Cedar's
     * d3d_eg.c does. */
    if ((data->lpDDDestSurface->ddsCaps & V9X_DDSCAPS_ZBUFFER) != 0ul) {
        if (fill.left != 0ul || fill.top != 0ul ||
            fill.width != (DWORD)data->lpDDDestSurface->lpGbl->wWidth ||
            fill.height != (DWORD)data->lpDDDestSurface->lpGbl->wHeight) {
            return v9x_d3d_vi_depth_fill(data->lpDDDestSurface, data->rDest,
                                         data->bltFX.dwFillColor, wait);
        }
        fill.height = (fill.height + V9X_VI_DEPTH_TILE_ROWS - 1ul) &
                      ~(V9X_VI_DEPTH_TILE_ROWS - 1ul);
    }
    if (v9x_eg_dma_plan_fill(&fill, &plan) != V9X_STATUS_OK ||
        v9x_vi_sdma_plan_check(&plan) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    v9x_vi_after_cp();
    return v9x_vi_emit_or_give_up(&plan);
}

static int v9x_vi_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                       DWORD destination_offset, DWORD bytes_per_pixel,
                       int wait)
{
    struct v9x_eg_dma_copy copy;
    struct v9x_eg_dma_plan plan;

    (void)wait;
    if (!v9x_vi_validate() || data == 0 ||
        data->lpDDSrcSurface == 0 || data->lpDDSrcSurface->lpGbl == 0 ||
        data->lpDDDestSurface == 0 || data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDSrcSurface->lpGbl->lPitch <= 0l ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        data->rSrc[2] <= data->rSrc[0] || data->rSrc[3] <= data->rSrc[1] ||
        data->rSrc[0] < 0l || data->rSrc[1] < 0l ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    copy.gpu_base = v9x_vi_heap_low;
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
    if (v9x_eg_dma_plan_copy(&copy, &plan) != V9X_STATUS_OK ||
        v9x_vi_sdma_plan_check(&plan) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    v9x_vi_after_cp();
    return v9x_vi_emit_or_give_up(&plan);
}

/* CANBLT is the non-blocking idle poll, as on Evergreen. */
static int v9x_vi_can_blt(void)
{
    return v9x_vi_wait_idle(0);
}

const V9X_ENGINE32_OPS v9x_engine32_vi = {
    v9x_vi_ready,
    v9x_vi_validate,
    v9x_vi_validated,
    v9x_vi_can_blt,
    v9x_vi_wait_idle,
    v9x_vi_fill,
    v9x_vi_copy
};
