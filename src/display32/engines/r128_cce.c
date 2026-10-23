/*
 * The Rage 128's CCE in PIO mode, for V9XHAL.DLL: the microcode loaded
 * once, the engine started when a Direct3D batch arrives, and left running
 * between batches until something needs what it drew.
 *
 * d3d_r128.c submits; v9x_r128_cce_drain stops it, and is called by every
 * path that needs the drawing finished or the engine back in NONPM4:
 * eng_r128.c's wait_idle, which Flip, Lock, GetBltStatus and the render
 * drain reach, and its fill and copy, whose direct register writes must
 * not meet a running microengine. Before this the CCE was started and
 * stopped around every batch, so the CPU waited for each to finish and the
 * GPU waited for the CPU to build the next: 3DMark 2000 scored 993 with
 * flips declined and 995 with them taken (A8U4I5, boots 527 and 529).
 *
 * Draining is: the CCE's FIFO empty and the engine not busy (as
 * r128_do_cce_idle), the GUI pixel cache the stream's last packet flushed
 * reported idle, the microengine stopped and PM4 back to NONPM4, and the
 * non-GUI pixel cache flushed, so the CPU reads what was drawn. Never
 * leaving the microengine running with nothing to drain it is what the
 * boot-522 hang taught: a probe that exited with it free-running locked
 * A8U4I5 once our 2D engine next wrote its registers
 * (docs\decisions\2026-10-22-rage128-cce-pio-runs-packets-and-a-triangle.md).
 *
 * With a ring published (engine.ring_linear_base, and its bus address in
 * engine.reserve_offset, from the mini-VDD's GART reservation) and the card
 * in AGP mode, the CCE runs bus-master instead: the batch is copied into
 * the ring and DL_WPTR moved, so the CPU no longer feeds the FIFO a pair at
 * a time. AGP_BASE is the 32 MiB window holding the ring; the probe that
 * found it is in docs\decisions\2026-10-23-rage128-cce-ring-agp-fetches-
 * but-wrong-data.md, the arithmetic in include\velocity9x\r128_ring.h.
 * Without a ring, or with one the card cannot reach, the PIO path stays.
 */
#include "ddhal_internal.h"
#include "velocity9x/r128_2d.h"
#include "velocity9x/r128_3d.h"
#include "velocity9x/r128_ring.h"
#include "velocity9x/r128_crtc.h"

/* CCE registers (r128_reg.h). */
#define V9X_R128_CCE_PM4_BUFFER_CNTL    0x0704ul
#define V9X_R128_CCE_PM4_STAT           0x07b8ul
#define V9X_R128_CCE_MICROCODE_ADDR     0x07d4ul
#define V9X_R128_CCE_MICROCODE_RADDR    0x07d8ul
#define V9X_R128_CCE_MICROCODE_DATAH    0x07dcul
#define V9X_R128_CCE_MICROCODE_DATAL    0x07e0ul
#define V9X_R128_CCE_PM4_BUFFER_ADDR    0x07f0ul
#define V9X_R128_CCE_PM4_MICRO_CNTL     0x07fcul
#define V9X_R128_CCE_FIFO_EVEN          0x1000ul
#define V9X_R128_CCE_FIFO_ODD           0x1004ul
/* The engine reset's registers (r128_reg.h): the PLL index (a byte) and
 * data, MCLK_CNTL's FORCE_GCP | FORCE_PIPE3D_CP, and SOFT_RESET_GUI. */
#define V9X_R128_CCE_CLOCK_CNTL_INDEX   0x0008ul
#define V9X_R128_CCE_CLOCK_CNTL_DATA    0x000cul
#define V9X_R128_CCE_PLL_WR_EN          0x80u
#define V9X_R128_CCE_PLL_MCLK_CNTL      0x0fu
#define V9X_R128_CCE_MCLK_FORCE         0x00030000ul
#define V9X_R128_CCE_GEN_RESET_CNTL     0x00f0ul
#define V9X_R128_CCE_SOFT_RESET_GUI     0x00000001ul

#define V9X_R128_CCE_192PIO             0x10000000ul
#define V9X_R128_CCE_NONPM4             0x00000000ul
#define V9X_R128_CCE_NOUPDATE           0x08000000ul
#define V9X_R128_CCE_FREERUN            0x40000000ul
/* PM4_STAT: free CCE FIFO entries 11:0 (192 in 192PIO), PM4_BUSY 16,
 * GUI_ACTIVE 31. */
#define V9X_R128_CCE_FIFO_MASK          0x00000ffful
#define V9X_R128_CCE_FIFO_ENTRIES       192ul
#define V9X_R128_CCE_BUSY_BITS          0x80010000ul

/* Bounded waits, matching eng_r128.c's; a non-blocking drain looks a few
 * times and answers busy. */
#define V9X_R128_CCE_SPINS       0x00200000ul
#define V9X_R128_CCE_QUICK_READS 16ul

/* Fault-trace tag: "R1CC". */
#define V9X_R128_CCE_FAULT_TAG 0x52314343ul

/* One engine, so one of each: V9XHAL's statics are shared by every
 * process, which is what these need. The microcode stays in the chip's RAM
 * until power is removed; a HAL that loads again loads it again. */
static int v9x_r128_cce_microcode_loaded = 0;
static int v9x_r128_cce_running = 0;

/* The transport, decided at the first start: undecided, PIO, or the ring.
 * The ring's writer position is a dword index; the CCE's is DL_RPTR. */
#define V9X_R128_CCE_UNDECIDED 0
#define V9X_R128_CCE_PIO       1
#define V9X_R128_CCE_RING      2
static int v9x_r128_cce_transport = V9X_R128_CCE_UNDECIDED;

/*
 * A flip queued in the ring. WAIT_UNTIL's EVENT_CRTC_OFFSET (r128_3d.h)
 * goes ahead of each CRTC_OFFSET write, as in Linux's
 * r128_cce_dispatch_flip. The flip number goes to GUI_SCRATCH_REG1 behind
 * the write, so the CPU can tell a flip the CCE has not reached from one
 * the CRTC has not taken.
 */
#define V9X_R128_CCE_GUI_SCRATCH_REG1   0x15e4ul
#define V9X_R128_CCE_FLIP_DWORDS        8ul
static DWORD v9x_r128_cce_flip_seq = 0ul;
static int v9x_r128_cce_flip_queued = 0;

static void v9x_r128_cce_engine_reset(void);

/* `lock or dword ptr [esp], 0`: a locked instruction drains the
 * write-combining buffers on every P6 (Intel SDM vol. 3, 11.3.1), where
 * SFENCE exists only from the Pentium III; the probe's write-combined fill
 * ended with the same. */
static void v9x_r128_cce_wc_drain(void);
#pragma aux v9x_r128_cce_wc_drain = 0xf0 0x83 0x0c 0x24 0x00 \
    modify exact [];
static v9x_r128_ring_plan v9x_r128_cce_plan;
static DWORD v9x_r128_cce_wptr = 0ul;

static DWORD v9x_r128_cce_read32(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_r128_cce_write32(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

/* Stop the CCE back to direct register access, whatever state it is in. */
static void v9x_r128_cce_stop(void)
{
    v9x_r128_cce_write32(V9X_R128_CCE_PM4_MICRO_CNTL, 0ul);
    v9x_r128_cce_write32(V9X_R128_CCE_PM4_BUFFER_CNTL,
                         V9X_R128_CCE_NONPM4 | V9X_R128_CCE_NOUPDATE);
    v9x_r128_cce_write32(V9X_R128_SCALE_3D_CNTL, 0ul);
    v9x_r128_cce_running = 0;
}

/* A timeout: the CCE stopped and the engine given up until the next boot,
 * V9X_DD_ENGINE_VALID with it, so 2D and Direct3D fall back to the CPU. */
static void v9x_r128_cce_quarantine(DWORD status)
{
    /* A ring the CCE is stuck on is reset before the microengine stops,
     * as every probe stall was; the probe never stopped one mid-fetch. */
    if (v9x_r128_cce_transport == V9X_R128_CCE_RING) {
        v9x_r128_cce_write32(V9X_R128_CCE_PM4_MICRO_CNTL, 0ul);
        v9x_r128_cce_engine_reset();
    }
    /* A flip the CCE never reached is never coming. */
    v9x_r128_cce_flip_queued = 0;
    v9x_r128_cce_stop();
    if (v9x_hal->engine.idle_timeouts++ == 0ul) {
        v9x_trace_flush_fault(V9X_R128_CCE_FAULT_TAG, status);
    }
    v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
}

/* The 2D engine idle: 64 free FIFO slots, not active (eng_r128.c's). */
static int v9x_r128_cce_wait_gui_idle(void)
{
    DWORD spins;
    DWORD status = 0ul;

    for (spins = 0ul; spins < V9X_R128_CCE_SPINS; ++spins) {
        status = v9x_r128_cce_read32(V9X_R128_GUI_STAT);
        if (status == 0xfffffffful) {
            break;
        }
        if (v9x_r128_gui_idle(status) != 0ul) {
            return 1;
        }
    }
    v9x_r128_cce_quarantine(status);
    return 0;
}

/*
 * The microcode, once: microengine stopped, ADDR 0, the pairs, then the
 * first word read back through RADDR. A mismatch quarantines the engine
 * rather than run a CCE whose program is not the one measured.
 */
static int v9x_r128_cce_load_microcode(void)
{
    DWORD index;

    v9x_r128_cce_write32(V9X_R128_CCE_PM4_MICRO_CNTL, 0ul);
    v9x_r128_cce_write32(V9X_R128_CCE_MICROCODE_ADDR, 0ul);
    for (index = 0ul; index < V9X_R128_CCE_MICROCODE_DWORDS; index += 2ul) {
        v9x_r128_cce_write32(V9X_R128_CCE_MICROCODE_DATAH,
                             v9x_r128_cce_microcode[index]);
        v9x_r128_cce_write32(V9X_R128_CCE_MICROCODE_DATAL,
                             v9x_r128_cce_microcode[index + 1ul]);
    }
    v9x_r128_cce_write32(V9X_R128_CCE_MICROCODE_RADDR, 0ul);
    if (v9x_r128_cce_read32(V9X_R128_CCE_MICROCODE_DATAH) !=
            v9x_r128_cce_microcode[0] ||
        v9x_r128_cce_read32(V9X_R128_CCE_MICROCODE_DATAL) !=
            v9x_r128_cce_microcode[1]) {
        v9x_r128_cce_quarantine(v9x_r128_cce_microcode[1]);
        return 0;
    }
    v9x_r128_cce_microcode_loaded = 1;
    return 1;
}

/*
 * The ring or PIO, once. The ring needs: pages published by the 16-bit
 * side, the card in AGP mode (a PCI card's bus-master reads do not use the
 * AGP window), a placement r128_ring.c accepts, and pages this process may
 * write - the GART service maps them in the shared arena, and a mapping it
 * kept supervisor-only would fault on the first batch, so it is asked
 * here instead.
 */
static void v9x_r128_cce_choose_transport(void)
{
    DWORD linear = v9x_hal->engine.ring_linear_base;
    DWORD bytes = v9x_hal->engine.ring_bytes;

    v9x_r128_cce_transport = V9X_R128_CCE_PIO;
    if (linear == 0ul || bytes == 0ul ||
        v9x_hal->engine.reserve_offset == 0ul) {
        return;
    }
    if (!v9x_r128_ring_agp_enabled(
            v9x_r128_cce_read32(V9X_R128_RING_AGP_COMMAND))) {
        return;
    }
    if (v9x_r128_ring_plan_make(v9x_hal->engine.reserve_offset, bytes,
                                &v9x_r128_cce_plan) != V9X_STATUS_OK) {
        return;
    }
    if (IsBadWritePtr((LPVOID)linear,
                      bytes + V9X_R128_RING_MIRROR_DWORDS * 4ul)) {
        return;
    }
    v9x_r128_cce_transport = V9X_R128_CCE_RING;
}

/*
 * r128_do_engine_reset, as the probe runs it: the non-GUI pixel cache
 * flushed, MCLK's GUI and 3D clocks forced on through the PLL, a
 * SOFT_RESET_GUI pulse with read-backs, then the clocks and
 * GEN_RESET_CNTL as found. The 2D engine's per-mode registers may not
 * survive it, so eng_r128.c is told to write them again.
 */
static void v9x_r128_cce_engine_reset(void)
{
    volatile BYTE *index_port =
        (volatile BYTE *)(v9x_hal->engine.control_linear_base +
                          V9X_R128_CCE_CLOCK_CNTL_INDEX);
    DWORD spins;
    DWORD mclk;
    DWORD gen;
    BYTE index;

    v9x_r128_cce_write32(V9X_R128_PC_NGUI_CTLSTAT,
                         v9x_r128_cce_read32(V9X_R128_PC_NGUI_CTLSTAT) |
                         V9X_R128_PC_FLUSH_ALL);
    for (spins = 0ul; spins < V9X_R128_CCE_SPINS; ++spins) {
        if ((v9x_r128_cce_read32(V9X_R128_PC_NGUI_CTLSTAT) &
             V9X_R128_PC_BUSY) == 0ul) {
            break;
        }
    }

    index = *index_port;
    *index_port = V9X_R128_CCE_PLL_MCLK_CNTL;
    mclk = v9x_r128_cce_read32(V9X_R128_CCE_CLOCK_CNTL_DATA);
    *index_port = V9X_R128_CCE_PLL_MCLK_CNTL | V9X_R128_CCE_PLL_WR_EN;
    v9x_r128_cce_write32(V9X_R128_CCE_CLOCK_CNTL_DATA,
                         mclk | V9X_R128_CCE_MCLK_FORCE);

    gen = v9x_r128_cce_read32(V9X_R128_CCE_GEN_RESET_CNTL);
    v9x_r128_cce_write32(V9X_R128_CCE_GEN_RESET_CNTL,
                         gen | V9X_R128_CCE_SOFT_RESET_GUI);
    (void)v9x_r128_cce_read32(V9X_R128_CCE_GEN_RESET_CNTL);
    v9x_r128_cce_write32(V9X_R128_CCE_GEN_RESET_CNTL,
                         gen & ~V9X_R128_CCE_SOFT_RESET_GUI);
    (void)v9x_r128_cce_read32(V9X_R128_CCE_GEN_RESET_CNTL);

    *index_port = V9X_R128_CCE_PLL_MCLK_CNTL | V9X_R128_CCE_PLL_WR_EN;
    v9x_r128_cce_write32(V9X_R128_CCE_CLOCK_CNTL_DATA, mclk);
    *index_port = index;
    v9x_r128_cce_write32(V9X_R128_CCE_GEN_RESET_CNTL, gen);

    v9x_hal->engine.flags &= ~V9X_DD_ENGINE_STATUS_VALIDATED;
}

/*
 * The ring's registers, written at every start since a mode set or a DOS
 * box may have been through them. Each start resets the engine and puts
 * both pointers at 0, as the probe does: started again from NONPM4 without
 * the reset, with the write pointer set to the read pointer found, the CCE
 * moved DL_RPTR, executed nothing and stayed busy (R128CCE /noreset, boot
 * 542) - and the first driver build, which started that way, locked
 * A8U4I5 at a Lock (boot 540). DL_RPTR takes a write only after the reset.
 * Then bus mastering and the AGP window as the probe ran them, the ring's
 * place and its watermarks.
 */
static int v9x_r128_cce_ring_registers(void)
{
    v9x_r128_cce_write32(V9X_R128_CCE_PM4_MICRO_CNTL, 0ul);
    v9x_r128_cce_engine_reset();

    v9x_r128_cce_write32(V9X_R128_RING_BUS_CNTL,
                         v9x_r128_ring_bus_cntl(
                             v9x_r128_cce_read32(V9X_R128_RING_BUS_CNTL)));
    v9x_r128_cce_write32(V9X_R128_RING_AGP_CNTL,
                         v9x_r128_ring_agp_cntl(
                             v9x_r128_cce_read32(V9X_R128_RING_AGP_CNTL)));
    v9x_r128_cce_write32(V9X_R128_RING_AGP_BASE, v9x_r128_cce_plan.agp_base);
    v9x_r128_cce_write32(V9X_R128_RING_PCI_GART_PAGE,
                         V9X_R128_RING_PCI_GART_OFF);
    v9x_r128_cce_write32(V9X_R128_RING_BUFFER_OFFSET,
                         v9x_r128_cce_plan.buffer_offset);
    v9x_r128_cce_write32(V9X_R128_RING_BUFFER_WM_CNTL,
                         V9X_R128_RING_WM_CNTL);
    v9x_r128_cce_write32(V9X_R128_RING_DL_WPTR, 0ul);
    v9x_r128_cce_write32(V9X_R128_RING_DL_RPTR, 0ul);
    v9x_r128_cce_wptr = 0ul;
    /* A pointer that did not take means the reset did not either. */
    return v9x_r128_cce_read32(V9X_R128_RING_DL_RPTR) == 0ul;
}

/* r128_do_cce_start: mode, the read "as per the sample code", then the
 * microengine. The 2D engine is idle first, so a blit that wrote this
 * batch's texture or target has finished. */
static int v9x_r128_cce_start(void)
{
    DWORD mode = V9X_R128_CCE_192PIO;

    if (!v9x_r128_cce_wait_gui_idle()) {
        return 0;
    }
    if (!v9x_r128_cce_microcode_loaded && !v9x_r128_cce_load_microcode()) {
        return 0;
    }
    if (v9x_r128_cce_transport == V9X_R128_CCE_UNDECIDED) {
        v9x_r128_cce_choose_transport();
    }
    if (v9x_r128_cce_transport == V9X_R128_CCE_RING) {
        if (v9x_r128_cce_ring_registers()) {
            mode = V9X_R128_RING_MODE_192BM | v9x_r128_cce_plan.size_l2qw;
        } else {
            v9x_r128_cce_transport = V9X_R128_CCE_PIO;
        }
    }
    v9x_r128_cce_write32(V9X_R128_CCE_PM4_BUFFER_CNTL,
                         mode | V9X_R128_CCE_NOUPDATE);
    (void)v9x_r128_cce_read32(V9X_R128_CCE_PM4_BUFFER_ADDR);
    v9x_r128_cce_write32(V9X_R128_CCE_PM4_MICRO_CNTL, V9X_R128_CCE_FREERUN);
    v9x_r128_cce_running = 1;
    return 1;
}

/*
 * Into the ring, as much as the CCE has room for each time, then DL_WPTR.
 * The pages are write-combined, so the CPU's combining buffers are drained
 * by a locked instruction before the pointer write that publishes them:
 * the card's AGP reads do not snoop, and a dword still in a buffer is not
 * one the CCE can fetch. DL_DONE goes with every publish: the probe's
 * kicks all carried it and each continued from the last, and without it
 * the CCE may hold a short tail until more arrives. The ring's first 32
 * dwords are mirrored past its end (Linux's R128_BROKEN_CCE).
 */
static int v9x_r128_cce_submit_ring(const v9x_u32 *dwords, DWORD count)
{
    volatile DWORD *ring = (volatile DWORD *)v9x_hal->engine.ring_linear_base;
    DWORD mask = v9x_r128_cce_plan.dword_mask;
    DWORD at = 0ul;
    DWORD spins = 0ul;
    DWORD rptr = 0ul;
    DWORD started = V9X_TIME_BEGIN();
    DWORD waited = 0ul;

    while (at < count) {
        DWORD room;

        rptr = v9x_r128_cce_read32(V9X_R128_RING_DL_RPTR) & mask;
        room = v9x_r128_ring_free(rptr, v9x_r128_cce_wptr, mask);
        if (room == 0ul) {
            if (spins == 0ul) {
                waited = V9X_TIME_BEGIN();
            }
            if (++spins >= V9X_R128_CCE_SPINS) {
                v9x_r128_cce_quarantine(rptr);
                return 0;
            }
            continue;
        }
        if (spins != 0ul) {
            V9X_TIME_END(V9X_TIME_RING_SPACE_WAIT, waited);
        }
        spins = 0ul;
        if (room > count - at) {
            room = count - at;
        }
        while (room != 0ul) {
            ring[v9x_r128_cce_wptr] = dwords[at];
            if (v9x_r128_cce_wptr < V9X_R128_RING_MIRROR_DWORDS) {
                ring[mask + 1ul + v9x_r128_cce_wptr] = dwords[at];
            }
            v9x_r128_cce_wptr = (v9x_r128_cce_wptr + 1ul) & mask;
            ++at;
            --room;
        }
        v9x_r128_cce_wc_drain();
        v9x_r128_cce_write32(V9X_R128_RING_DL_WPTR,
                             v9x_r128_cce_wptr | V9X_R128_RING_DL_DONE);
    }
    /* The whole submit, space waits included; RING_SPACE_WAIT is the
     * waiting part of it. */
    V9X_TIME_END(V9X_TIME_RING_WRITE, started);
    return 1;
}

int v9x_r128_cce_submit(const v9x_u32 *dwords, DWORD count)
{
    DWORD at = 0ul;
    DWORD spins = 0ul;
    DWORD status = 0ul;

    if (dwords == 0 || (count & 1ul) != 0ul) {
        return 0;
    }
    if (!v9x_r128_cce_running && !v9x_r128_cce_start()) {
        return 0;
    }
    if (v9x_r128_cce_transport == V9X_R128_CCE_RING) {
        return v9x_r128_cce_submit_ring(dwords, count);
    }
    /* In EVEN/ODD pairs, as many as PM4_STAT says fit each time. */
    while (at < count) {
        DWORD room;

        status = v9x_r128_cce_read32(V9X_R128_CCE_PM4_STAT);
        room = (status & V9X_R128_CCE_FIFO_MASK) & ~1ul;
        if (room == 0ul) {
            if (++spins >= V9X_R128_CCE_SPINS) {
                v9x_r128_cce_quarantine(status);
                return 0;
            }
            continue;
        }
        spins = 0ul;
        if (room > count - at) {
            room = count - at;
        }
        while (room != 0ul) {
            v9x_r128_cce_write32(V9X_R128_CCE_FIFO_EVEN, dwords[at]);
            v9x_r128_cce_write32(V9X_R128_CCE_FIFO_ODD, dwords[at + 1ul]);
            at += 2ul;
            room -= 2ul;
        }
    }
    return 1;
}

/* Poll until the condition holds or the reads run out: the CCE drained
 * (on the ring, everything published fetched; FIFO empty, not busy), or a
 * pixel cache's busy bit clear. */
static int v9x_r128_cce_poll_drained(DWORD reads, DWORD *status)
{
    DWORD read;

    for (read = 0ul; read < reads; ++read) {
        if (v9x_r128_cce_transport == V9X_R128_CCE_RING &&
            (v9x_r128_cce_read32(V9X_R128_RING_DL_RPTR) &
             v9x_r128_cce_plan.dword_mask) != v9x_r128_cce_wptr) {
            *status = v9x_r128_cce_read32(V9X_R128_RING_DL_RPTR);
            continue;
        }
        *status = v9x_r128_cce_read32(V9X_R128_CCE_PM4_STAT);
        if ((*status & V9X_R128_CCE_FIFO_MASK) >=
                V9X_R128_CCE_FIFO_ENTRIES &&
            (*status & V9X_R128_CCE_BUSY_BITS) == 0ul) {
            return 1;
        }
    }
    return 0;
}

static int v9x_r128_cce_poll_cache(DWORD offset, DWORD reads, DWORD *status)
{
    DWORD read;

    for (read = 0ul; read < reads; ++read) {
        *status = v9x_r128_cce_read32(offset);
        if ((*status & V9X_R128_PC_BUSY) == 0ul) {
            return 1;
        }
    }
    return 0;
}

int v9x_r128_cce_drain(int wait)
{
    DWORD reads = wait ? V9X_R128_CCE_SPINS : V9X_R128_CCE_QUICK_READS;
    DWORD status = 0ul;
    DWORD started;

    if (!v9x_r128_cce_running || v9x_hal == 0 ||
        v9x_hal->engine.control_linear_base == 0ul) {
        return 1;
    }
    /* Still drawing: a non-blocking caller hears so and asks again.
     * RENDER_DRAIN is the CPU's wait for the GPU here, blocking or not. */
    started = V9X_TIME_BEGIN();
    if (!v9x_r128_cce_poll_drained(reads, &status)) {
        V9X_TIME_END(V9X_TIME_RENDER_DRAIN, started);
        if (!wait) {
            return 0;
        }
        v9x_r128_cce_quarantine(status);
        return 0;
    }
    V9X_TIME_END(V9X_TIME_RENDER_DRAIN, started);
    if (!v9x_r128_cce_poll_cache(V9X_R128_PC_GUI_CTLSTAT,
                                 V9X_R128_CCE_SPINS, &status)) {
        v9x_r128_cce_quarantine(status);
        return 0;
    }
    /* Drained: a queued flip has been executed, whatever the scratch
     * register says after the next start's reset. */
    v9x_r128_cce_flip_queued = 0;
    v9x_r128_cce_stop();
    v9x_r128_cce_write32(V9X_R128_PC_NGUI_CTLSTAT,
                         v9x_r128_cce_read32(V9X_R128_PC_NGUI_CTLSTAT) |
                         V9X_R128_PC_FLUSH_ALL);
    if (!v9x_r128_cce_poll_cache(V9X_R128_PC_NGUI_CTLSTAT,
                                 V9X_R128_CCE_SPINS, &status)) {
        v9x_r128_cce_quarantine(status);
        return 0;
    }
    return 1;
}

/* Only on the ring, once this boot has chosen it, and while the engine is
 * still trusted: PIO flips keep the drain and the direct write. */
int v9x_r128_cce_flip_queues(void)
{
    if (v9x_hal == 0 ||
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) == 0ul) {
        return 0;
    }
    return v9x_r128_cce_transport == V9X_R128_CCE_RING;
}

/*
 * The flip, behind everything submitted: the GUI pixel cache flushed, so
 * the frame's last pixels are in memory before the CRTC can fetch them;
 * the previous flip taken (the core has already seen it complete, so this
 * normally passes at once); the new CRTC_OFFSET; then the flip's number.
 * The 3D engine may still be finishing the last primitive when the CCE
 * writes CRTC_OFFSET - WAIT_UNTIL has no engine-idle trigger on this chip
 * - but the CRTC takes the offset only at the next blank, as with
 * Linux's flip, which does the same.
 */
int v9x_r128_cce_queue_flip(DWORD crtc_offset)
{
    v9x_u32 stream[V9X_R128_CCE_FLIP_DWORDS];

    stream[0] = V9X_R128_PACKET0(V9X_R128_PC_GUI_CTLSTAT, 0u);
    stream[1] = V9X_R128_PC_FLUSH_GUI;
    stream[2] = V9X_R128_PACKET0(V9X_R128_WAIT_UNTIL, 0u);
    stream[3] = V9X_R128_EVENT_CRTC_OFFSET;
    stream[4] = V9X_R128_PACKET0(V9X_R128_CRTC_OFFSET, 0u);
    stream[5] = crtc_offset;
    stream[6] = V9X_R128_PACKET0(V9X_R128_CCE_GUI_SCRATCH_REG1, 0u);
    stream[7] = v9x_r128_cce_flip_seq + 1ul;
    if (!v9x_r128_cce_submit(stream, V9X_R128_CCE_FLIP_DWORDS)) {
        return 0;
    }
    ++v9x_r128_cce_flip_seq;
    v9x_r128_cce_flip_queued = 1;
    return 1;
}

/* Until GUI_SCRATCH_REG1 holds the last flip's number, the CCE has not
 * written that flip's CRTC_OFFSET. */
int v9x_r128_cce_flip_in_flight(void)
{
    if (!v9x_r128_cce_flip_queued) {
        return 0;
    }
    if (v9x_r128_cce_read32(V9X_R128_CCE_GUI_SCRATCH_REG1) ==
        v9x_r128_cce_flip_seq) {
        v9x_r128_cce_flip_queued = 0;
        return 0;
    }
    return 1;
}
