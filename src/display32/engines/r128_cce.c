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
 */
#include "ddhal_internal.h"
#include "velocity9x/r128_2d.h"
#include "velocity9x/r128_3d.h"

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

/* r128_do_cce_start: mode, the read "as per the sample code", then the
 * microengine. The 2D engine is idle first, so a blit that wrote this
 * batch's texture or target has finished. */
static int v9x_r128_cce_start(void)
{
    if (!v9x_r128_cce_wait_gui_idle()) {
        return 0;
    }
    if (!v9x_r128_cce_microcode_loaded && !v9x_r128_cce_load_microcode()) {
        return 0;
    }
    v9x_r128_cce_write32(V9X_R128_CCE_PM4_BUFFER_CNTL,
                         V9X_R128_CCE_192PIO | V9X_R128_CCE_NOUPDATE);
    (void)v9x_r128_cce_read32(V9X_R128_CCE_PM4_BUFFER_ADDR);
    v9x_r128_cce_write32(V9X_R128_CCE_PM4_MICRO_CNTL, V9X_R128_CCE_FREERUN);
    v9x_r128_cce_running = 1;
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
 * (FIFO empty, not busy), or a pixel cache's busy bit clear. */
static int v9x_r128_cce_poll_drained(DWORD reads, DWORD *status)
{
    DWORD read;

    for (read = 0ul; read < reads; ++read) {
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

    if (!v9x_r128_cce_running || v9x_hal == 0 ||
        v9x_hal->engine.control_linear_base == 0ul) {
        return 1;
    }
    /* Still drawing: a non-blocking caller hears so and asks again. */
    if (!v9x_r128_cce_poll_drained(reads, &status)) {
        if (!wait) {
            return 0;
        }
        v9x_r128_cce_quarantine(status);
        return 0;
    }
    if (!v9x_r128_cce_poll_cache(V9X_R128_PC_GUI_CTLSTAT,
                                 V9X_R128_CCE_SPINS, &status)) {
        v9x_r128_cce_quarantine(status);
        return 0;
    }
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
