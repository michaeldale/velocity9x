/*
 * NVIDIA GT218 (GeForce 210, NV50 family) 2D engine for V9XHAL.DLL:
 * DirectDraw solid fill and screen copy through class 502D.
 *
 * Registers through BAR0, which the 16-bit side had the mini-VDD map
 * (gt218_hw16.c). One PFIFO channel in IB mode, whose instance block, page
 * table, runlist, IB ring, pushbuffer and PGRAPH context image live in
 * the reserve at the top of the BAR1 mapping (engine.reserve_offset),
 * written through the framebuffer mapping. Everything written comes from
 * the host-tested builders in src\chipsets\nvidia\gt218\: nv50_chan.c
 * (the structures and pushes), nv50_grctx.c (the context program and
 * image, nouveau's) and nv50_init.c (the register bring-up). The probe
 * replayed the same list and pushes on M-BXMFA8, from a cold boot, at 8,
 * 16 and 32 bpp with overlapping copies
 * (docs\decisions\2026-11-14-gt218-502d-engine-measurements.md).
 *
 * What the measurements fix here:
 *
 * - USERD is written (IB PUT, the kick) and never read: reads of it lost
 *   the card (runs 3 and 6 of the channel probe). Progress comes from
 *   PFIFO's own registers.
 * - The channel's VM is the identity over the first 512 MiB and BAR1 is
 *   linear, so a surface's VM address is its offset in video memory.
 * - A VBE mode set leaves the channel alone (runs 37-39), so there is no
 *   rebuild per mode. Two registers are still checked before every
 *   operation, because a full-screen DOS box, standby and a driver
 *   restart were not measured.
 * - An overlapping copy is passed as it is; the engine handles it.
 *
 * One operation per kick. The pushbuffer is reused from its start each
 * time, which is safe because every operation first waits for the one
 * before to be fetched and for PGRAPH to go idle.
 *
 * There is no recovery path: a timeout, a PFIFO or PGRAPH interrupt
 * status or a pusher error raises idle_timeouts, gives the engine up for
 * the session and declines to the CPU. PMC_INTR_EN is left 0; nothing is
 * taken as an interrupt.
 */
#include "ddhal_internal.h"
#include "velocity9x/nv50_chan.h"
#include "velocity9x/nv50_init.h"

/* Bounded busy-wait. The probe's waits ended on the first or second
 * read for 64x64 operations; a full-screen copy is far longer, and this
 * bound only has to tell slow from stuck. */
#define V9X_NV50_WAIT_SPINS  0x00400000ul

/* BAR0 as the mini-VDD maps it. */
#define V9X_NV50_MMIO_BYTES  0x01000000ul

#define V9X_NV50_PMC_BOOT_0  0x000000ul
#define V9X_NV50_PBUS_UNITS  0x001540ul
#define V9X_NV50_BOOT_CHIPSET_SHIFT 20u
#define V9X_NV50_BOOT_CHIPSET_MASK  0x1fful

/* The one channel. Channel 0 is not usable on NV50 (fifo\nv50.c). */
#define V9X_NV50_ENGINE_CHID 1ul

/* A push never exceeds this: the copy is 43 words, the bind 4. */
#define V9X_NV50_PUSH_WORDS  64ul

/* Fault codes for the trace: 'N5BU' bring-up, 'N5ID' idle. */
#define V9X_NV50_FAULT_BRING_UP 0x4e354255ul
#define V9X_NV50_FAULT_IDLE     0x4e354944ul

/* V9XHAL's statics are shared by every process, which is what an
 * engine's state is. */
static struct v9x_nv50_chan_layout v9x_nv50_layout;
static struct v9x_nv50_init_op v9x_nv50_ops[V9X_NV50_INIT_OPS_MAX];
/* The ring index the next IB entry goes to, which is also what PFIFO's
 * IB get reads once everything issued has been fetched. */
static DWORD v9x_nv50_ib_index;

static int v9x_nv50_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_NV50 &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_NV50_MMIO_BYTES &&
        v9x_hal->engine.reserve_offset != 0ul &&
        v9x_hal->fb.linear_base != 0ul;
}

/* The 502D format for the current depth, 0 where the engine draws
 * nothing; ready() first. */
static DWORD v9x_nv50_format(void)
{
    return v9x_nv50_2d_format(v9x_hal->fb.bits_per_pixel / 8ul);
}

static DWORD v9x_nv50_read32(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_nv50_write32(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

/* The region, where the framebuffer mapping shows it. */
static volatile DWORD *v9x_nv50_region(DWORD byte_offset)
{
    return (volatile DWORD *)(v9x_hal->fb.linear_base +
                              v9x_hal->engine.reserve_offset + byte_offset);
}

/* An error PFIFO or PGRAPH has latched: the engine polls what nouveau
 * takes as interrupts. */
static int v9x_nv50_faulted(void)
{
    return v9x_nv50_read32(V9X_NV50_PFIFO_INTR) != 0ul ||
           v9x_nv50_read32(V9X_NV50_PGRAPH_INTR) != 0ul ||
           (v9x_nv50_read32(V9X_NV50_PFIFO_DMA_STATE) >>
            V9X_NV50_PFIFO_DMA_ERROR_SHIFT) != 0ul;
}

/* Everything issued has been fetched and executed: PFIFO's IB get is
 * where the engine's own index stands, the pusher has consumed the
 * pushbuffer, and PGRAPH is idle. The index is compared with ours, not
 * with PFIFO's copy of PUT, which trails the kick. */
static int v9x_nv50_idle_now(void)
{
    return v9x_nv50_read32(V9X_NV50_PFIFO_IB_GET) == v9x_nv50_ib_index &&
           v9x_nv50_read32(V9X_NV50_PFIFO_DMA_GET) ==
               v9x_nv50_read32(V9X_NV50_PFIFO_DMA_PUT) &&
           v9x_nv50_read32(V9X_NV50_PGRAPH_STATUS) == 0ul;
}

static int v9x_nv50_wait_drained(void)
{
    DWORD spins = V9X_NV50_WAIT_SPINS;

    while (spins-- != 0ul) {
        if (v9x_nv50_idle_now()) {
            return !v9x_nv50_faulted();
        }
    }
    return 0;
}

/* One pushbuffer into the region, an IB entry naming it, and the kick.
 * The caller has waited for the engine to drain. */
static void v9x_nv50_submit(const struct v9x_nv50_push *push)
{
    volatile DWORD *target = v9x_nv50_region(V9X_NV50_PUSH);
    volatile DWORD *ring = v9x_nv50_region(V9X_NV50_IB);
    DWORD entry[2];
    DWORD index;

    for (index = 0ul; index < push->count; ++index) {
        target[index] = push->words[index];
    }
    v9x_nv50_ib_entry(v9x_nv50_layout.push_addr, push->count, entry);
    ring[v9x_nv50_ib_index * 2ul] = entry[0];
    ring[v9x_nv50_ib_index * 2ul + 1ul] = entry[1];
    /* The pushbuffer, the IB entry and any surface the CPU just wrote go
     * through a write-combined mapping; the chip must see them before
     * the kick. */
    v9x_wc_drain();
    v9x_nv50_ib_index = v9x_nv50_ib_next(v9x_nv50_ib_index);
    v9x_nv50_write32(V9X_NV50_USERD(V9X_NV50_ENGINE_CHID) +
                     V9X_NV50_USERD_IB_PUT, v9x_nv50_ib_index);
}

/* Replay nv50_init.c's list, waits bounded. */
static int v9x_nv50_replay(const struct v9x_nv50_init_list *list)
{
    DWORD at;
    DWORD spins;

    for (at = 0ul; at < list->count; ++at) {
        if (v9x_nv50_ops[at].kind == V9X_NV50_INIT_WRITE) {
            v9x_nv50_write32(v9x_nv50_ops[at].reg, v9x_nv50_ops[at].value);
            continue;
        }
        spins = V9X_NV50_WAIT_SPINS;
        while ((v9x_nv50_read32(v9x_nv50_ops[at].reg) &
                v9x_nv50_ops[at].mask) != v9x_nv50_ops[at].value) {
            if (spins-- == 0ul) {
                return 0;
            }
        }
    }
    return 1;
}

/*
 * The channel's structures into the reserve, the register bring-up, then
 * the 502D object bound to its subchannel, which is also PGRAPH's first
 * context load. The DirectDraw heap ends below the reserve
 * (nvidia_hw16.c), so nothing else lives where this writes.
 */
static int v9x_nv50_bring_up(void)
{
    struct v9x_nv50_grctx_input grctx;
    struct v9x_nv50_init_input input;
    struct v9x_nv50_init_list list;
    struct v9x_nv50_push push;
    DWORD words[V9X_NV50_PUSH_WORDS];
    DWORD boot;

    boot = v9x_nv50_read32(V9X_NV50_PMC_BOOT_0);
    grctx.chipset = (boot >> V9X_NV50_BOOT_CHIPSET_SHIFT) &
                    V9X_NV50_BOOT_CHIPSET_MASK;
    grctx.units = v9x_nv50_read32(V9X_NV50_PBUS_UNITS);
    /* DDR2 or DDR3 on the measured card (PFB 100714h type 1); the one
     * default GDDR5 changes is not known to matter to 2D. */
    grctx.gddr5 = 0ul;

    /* Built straight into video memory: the region is 704 KiB, too much
     * to hold twice. The builder zeroes it first. */
    if (v9x_nv50_chan_build(v9x_hal->engine.reserve_offset,
                            V9X_NV50_ENGINE_CHID, &grctx,
                            (v9x_u32 *)v9x_nv50_region(0ul),
                            V9X_NV50_REGION_WORDS, &v9x_nv50_layout) !=
        V9X_STATUS_OK) {
        return 0;
    }
    v9x_wc_drain();

    input.chipset = grctx.chipset;
    input.units = grctx.units;
    input.pmc_enable = v9x_nv50_read32(V9X_NV50_PMC_ENABLE);
    if (v9x_nv50_init_build(&input, &v9x_nv50_layout, v9x_nv50_ops,
                            V9X_NV50_INIT_OPS_MAX, &list) !=
        V9X_STATUS_OK) {
        return 0;
    }
    if (!v9x_nv50_replay(&list)) {
        return 0;
    }

    v9x_nv50_ib_index = 0ul;
    v9x_nv50_push_init(&push, words, V9X_NV50_PUSH_WORDS);
    v9x_nv50_push_bind_2d(&push);
    if (push.overflow != 0ul) {
        return 0;
    }
    v9x_nv50_submit(&push);
    return v9x_nv50_wait_drained();
}

/* What the bring-up left, still there: the channel's table entry, and
 * PGRAPH holding this channel's context. */
static int v9x_nv50_state_holds(void)
{
    return v9x_nv50_read32(V9X_NV50_PFIFO_CHAN(V9X_NV50_ENGINE_CHID)) ==
               v9x_nv50_layout.chan_entry &&
           v9x_nv50_read32(V9X_NV50_PGRAPH_CTX) ==
               (V9X_NV50_PGRAPH_CTX_VALID |
                (v9x_nv50_layout.inst_addr >> 12));
}

static void v9x_nv50_give_up(DWORD code, DWORD address)
{
    if (v9x_hal->engine.idle_timeouts++ == 0ul) {
        v9x_trace_flush_fault(code, address);
    }
    v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
}

static int v9x_nv50_validate(void)
{
    if (!v9x_nv50_ready()) {
        return 0;
    }
    /* Nothing is issued at this depth, so there is nothing to bring up
     * and nothing for a CPU access to wait on (eng_nv3.c says what
     * answering "invalid" here cost). */
    if (v9x_nv50_format() == 0ul) {
        return 1;
    }
    if ((v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul) {
        if (v9x_nv50_state_holds()) {
            return 1;
        }
        /* The state went with something unmeasured. Not rebuilt: what
         * took it may still own the card. */
        v9x_hal->engine.flags &= ~V9X_DD_ENGINE_STATUS_VALIDATED;
        v9x_nv50_give_up(V9X_NV50_FAULT_BRING_UP, V9X_NV50_PGRAPH_CTX);
        return 0;
    }
    if (!v9x_nv50_bring_up()) {
        v9x_nv50_give_up(V9X_NV50_FAULT_BRING_UP, V9X_NV50_PFIFO_DMA_STATE);
        return 0;
    }
    ++v9x_hal->engine.reset_count;
    v9x_hal->engine.flags |= V9X_DD_ENGINE_STATUS_VALIDATED;
    return 1;
}

static int v9x_nv50_validated(void)
{
    return v9x_nv50_ready() &&
        (v9x_nv50_format() == 0ul ||
         (v9x_hal->engine.flags & V9X_DD_ENGINE_STATUS_VALIDATED) != 0ul);
}

static int v9x_nv50_wait_idle(int wait)
{
    DWORD spins;

    if (!v9x_nv50_validate()) {
        return 0;
    }
    if (v9x_nv50_format() == 0ul) {
        return 1;
    }
    if (!(wait && v9x_fault_injected())) {
        if (v9x_nv50_idle_now()) {
            if (v9x_nv50_faulted()) {
                v9x_nv50_give_up(V9X_NV50_FAULT_IDLE,
                                 V9X_NV50_PFIFO_DMA_STATE);
                return 0;
            }
            return 1;
        }
        if (!wait) {
            return 0;
        }
        spins = V9X_NV50_WAIT_SPINS;
        while (spins-- != 0ul) {
            if (v9x_nv50_idle_now()) {
                if (v9x_nv50_faulted()) {
                    break;
                }
                return 1;
            }
        }
        v9x_nv50_give_up(V9X_NV50_FAULT_IDLE, V9X_NV50_PGRAPH_STATUS);
        return 0;
    }
    ++v9x_hal->engine.idle_timeouts;
    v9x_trace_flush_fault(V9X_NV50_FAULT_IDLE, V9X_NV50_PGRAPH_STATUS);
    return 0;
}

static int v9x_nv50_fill(V9X_DDHAL_BLTDATA *data, DWORD offset,
                         DWORD bytes_per_pixel, int wait)
{
    struct v9x_nv50_push push;
    DWORD words[V9X_NV50_PUSH_WORDS];
    DWORD format;

    if (!v9x_nv50_validate() || data == 0 ||
        data->lpDDDestSurface == 0 ||
        data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        data->rDest[2] <= data->rDest[0] ||
        data->rDest[3] <= data->rDest[1] ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    format = v9x_nv50_2d_format(bytes_per_pixel);
    if (format == 0ul || v9x_nv50_format() != format) {
        return V9X_BLT_DECLINED;
    }
    v9x_nv50_push_init(&push, words, V9X_NV50_PUSH_WORDS);
    v9x_nv50_push_fill(&push, offset,
                       (DWORD)data->lpDDDestSurface->lpGbl->lPitch,
                       (DWORD)data->lpDDDestSurface->lpGbl->wWidth,
                       (DWORD)data->lpDDDestSurface->lpGbl->wHeight,
                       format, data->bltFX.dwFillColor,
                       (DWORD)data->rDest[0], (DWORD)data->rDest[1],
                       (DWORD)(data->rDest[2] - data->rDest[0]),
                       (DWORD)(data->rDest[3] - data->rDest[1]));
    if (push.overflow != 0ul) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_nv50_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_nv50_submit(&push);
    v9x_present_note_submission();
    return V9X_BLT_DONE;
}

static int v9x_nv50_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                         DWORD destination_offset, DWORD bytes_per_pixel,
                         int wait)
{
    struct v9x_nv50_push push;
    DWORD words[V9X_NV50_PUSH_WORDS];
    DWORD format;

    if (!v9x_nv50_validate() || data == 0 ||
        data->lpDDSrcSurface == 0 || data->lpDDSrcSurface->lpGbl == 0 ||
        data->lpDDDestSurface == 0 || data->lpDDDestSurface->lpGbl == 0 ||
        data->lpDDSrcSurface->lpGbl->lPitch <= 0l ||
        data->lpDDDestSurface->lpGbl->lPitch <= 0l ||
        data->rSrc[2] <= data->rSrc[0] || data->rSrc[3] <= data->rSrc[1] ||
        data->rSrc[0] < 0l || data->rSrc[1] < 0l ||
        data->rDest[0] < 0l || data->rDest[1] < 0l) {
        return V9X_BLT_DECLINED;
    }
    format = v9x_nv50_2d_format(bytes_per_pixel);
    if (format == 0ul || v9x_nv50_format() != format) {
        return V9X_BLT_DECLINED;
    }
    v9x_nv50_push_init(&push, words, V9X_NV50_PUSH_WORDS);
    v9x_nv50_push_blit(&push, source_offset,
                       (DWORD)data->lpDDSrcSurface->lpGbl->lPitch,
                       (DWORD)data->lpDDSrcSurface->lpGbl->wWidth,
                       (DWORD)data->lpDDSrcSurface->lpGbl->wHeight,
                       destination_offset,
                       (DWORD)data->lpDDDestSurface->lpGbl->lPitch,
                       (DWORD)data->lpDDDestSurface->lpGbl->wWidth,
                       (DWORD)data->lpDDDestSurface->lpGbl->wHeight,
                       format,
                       (DWORD)data->rSrc[0], (DWORD)data->rSrc[1],
                       (DWORD)data->rDest[0], (DWORD)data->rDest[1],
                       (DWORD)(data->rSrc[2] - data->rSrc[0]),
                       (DWORD)(data->rSrc[3] - data->rSrc[1]));
    if (push.overflow != 0ul) {
        return V9X_BLT_DECLINED;
    }
    if (!v9x_nv50_wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    v9x_nv50_submit(&push);
    v9x_present_note_submission();
    return V9X_BLT_DONE;
}

/* CANBLT is the non-blocking idle poll, as on the Trio. */
static int v9x_nv50_can_blt(void)
{
    return v9x_nv50_wait_idle(0);
}

const V9X_ENGINE32_OPS v9x_engine32_nv50 = {
    v9x_nv50_ready,
    v9x_nv50_validate,
    v9x_nv50_validated,
    v9x_nv50_can_blt,
    v9x_nv50_wait_idle,
    v9x_nv50_fill,
    v9x_nv50_copy
};
