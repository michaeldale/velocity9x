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
#include "velocity9x/nv50_clk.h"
#include "velocity9x/gt218_reserve.h"
#include "velocity9x/nv50_rblock.h"
#include "velocity9x/nv50_crtc.h"
#include "velocity9x/diagpaths.h"

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

/*
 * 502D addresses a surface from 256 bytes. An unaligned source is
 * addressed from below with x moved (nv50_2d.c); an unaligned destination
 * cannot be, since widening it past its pitch traps in PROP (3DMark
 * 2000's first blit, M-BXMFA8 boot 124), so fills and copies into one are
 * declined to the CPU. The HAL asks DirectDraw for surfaces on 256 bytes
 * (surface_align below), which makes that rare.
 */
#define V9X_NV50_SURFACE_ALIGN 0x100ul

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
/* The last kick, for the fault record (v9x_nv50_write_fault). */
static DWORD v9x_nv50_last_kick_kind;   /* 1 2D, 2 3D batch */
static DWORD v9x_nv50_last_kick_words;
/* The last 2D push itself: a 3DMark 2000 start trapped in PROP on a
 * blit, and its words are what say which. */
static DWORD v9x_nv50_last_2d[V9X_NV50_PUSH_WORDS];
static struct v9x_nv50_init_op v9x_nv50_ops[V9X_NV50_INIT_OPS_MAX];
/* There is no ring index here. The 16-bit driver kicks the same ring for
 * GDI (gdi_accel.c), so the index lives in the hardware: after a kick
 * each side waits for PFIFO's copy of PUT to show it, and the next index
 * is PFIFO's IB get once everything issued has been fetched. */

/* A kick shows in PFIFO's copy of PUT at once in every run measured; this
 * only has to end. */
#define V9X_NV50_PUT_SPINS   0x00010000ul

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

/* ddhal_internal.h. The memory configuration is the VBIOS's, fixed from
 * POST, so the unit is read once, from the registers nouveau's
 * nv50_fb_vram_rblock reads. */
DWORD v9x_nv50_rblock_unit_now(void)
{
    static DWORD unit = 0ul;

    if (unit == 0ul && v9x_nv50_ready()) {
        unit = v9x_nv50_rblock_unit(v9x_nv50_rblock_bytes(
            v9x_nv50_read32(V9X_NV50_RBLOCK_UNITS_REG),
            v9x_nv50_read32(V9X_NV50_RBLOCK_CFG1_REG),
            v9x_nv50_read32(V9X_NV50_RBLOCK_TRIPLE_REG)));
    }
    return unit;
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

/* Everything issued has been fetched and executed: PFIFO's IB get has
 * reached its PUT, the pusher has consumed the pushbuffer, and PGRAPH is
 * idle. PFIFO's PUT can be trusted here because whoever kicked last
 * waited for it to show the kick (v9x_nv50_submit, and gdi_accel.c's). */
static int v9x_nv50_idle_now(void)
{
    return v9x_nv50_read32(V9X_NV50_PFIFO_IB_GET) ==
               v9x_nv50_read32(V9X_NV50_PFIFO_IB_PUT) &&
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

/* One pushbuffer into the region, an IB entry naming it at ring index
 * at, and the kick. The caller has waited for the engine to drain.
 * Returns 0 if PFIFO never showed the kick. */
static int v9x_nv50_submit_at(const struct v9x_nv50_push *push, DWORD at)
{
    volatile DWORD *target = v9x_nv50_region(V9X_NV50_PUSH);
    volatile DWORD *ring = v9x_nv50_region(V9X_NV50_IB);
    DWORD entry[2];
    DWORD index;
    DWORD next;
    DWORD spins;

    if (at >= V9X_NV50_IB_ENTRIES) {
        return 0;
    }
    v9x_nv50_last_kick_kind = 1ul;
    v9x_nv50_last_kick_words = push->count;
    for (index = 0ul; index < push->count; ++index) {
        target[index] = push->words[index];
        if (index < V9X_NV50_PUSH_WORDS) {
            v9x_nv50_last_2d[index] = push->words[index];
        }
    }
    v9x_nv50_ib_entry(v9x_nv50_live_push(v9x_hal->engine.reserve_offset),
                      push->count, entry);
    ring[at * 2ul] = entry[0];
    ring[at * 2ul + 1ul] = entry[1];
    /* The pushbuffer, the IB entry and any surface the CPU just wrote go
     * through a write-combined mapping; the chip must see them before
     * the kick. */
    v9x_wc_drain();
    next = v9x_nv50_ib_next(at);
    v9x_nv50_write32(V9X_NV50_USERD(V9X_NV50_ENGINE_CHID) +
                     V9X_NV50_USERD_IB_PUT, next);
    /* Until PFIFO's own PUT shows the kick, an idle test that compares
     * get with it would pass on the state before the kick. */
    spins = V9X_NV50_PUT_SPINS;
    while (v9x_nv50_read32(V9X_NV50_PFIFO_IB_PUT) != next) {
        if (spins-- == 0ul) {
            return 0;
        }
    }
    return 1;
}

/* The same at the ring's current position: PFIFO's IB get, which is where
 * the next entry goes once the engine has drained. */
static int v9x_nv50_submit(const struct v9x_nv50_push *push)
{
    return v9x_nv50_submit_at(push,
                              v9x_nv50_read32(V9X_NV50_PFIFO_IB_GET));
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
 * The G96's core and shader clocks from POST's 400/800 MHz to the VBIOS's
 * level 3, 550/1400 (nv50_clk.h, docs\decisions\
 * 2026-11-28-g96-clocks-measured.md). Run before the channel exists, so
 * nothing is on PFIFO while the script blocks it. Any other chip or clock
 * state gets an empty list. Returns 0 only when the sequencer never
 * finished, which gives the bring-up up.
 */
static void v9x_nv50_write_clock(const struct v9x_nv50_clk_input *before,
                                 const char *result);

/* The G96's chipset, the one the clock record is written for. */
#define V9X_NV50_CHIPSET_G96 0x96ul

static int v9x_nv50_reclock(DWORD chipset)
{
    struct v9x_nv50_clk_input input;
    struct v9x_nv50_init_list list;
    v9x_u32 count = 0ul;
    int ran;

    input.chipset = chipset;
    input.hwsq_ctrl = v9x_nv50_read32(V9X_NV50_CLK_BUS_HWSQ_CTRL);
    input.source = v9x_nv50_read32(V9X_NV50_CLK_SOURCE);
    input.ref_select = v9x_nv50_read32(V9X_NV50_CLK_REF_SELECT);
    input.spll_ctrl = v9x_nv50_read32(V9X_NV50_CLK_SPLL_CTRL);
    input.spll_coef = v9x_nv50_read32(V9X_NV50_CLK_SPLL_COEF);
    input.nvpll_ctrl = v9x_nv50_read32(V9X_NV50_CLK_NVPLL_CTRL);
    input.nvpll_coef = v9x_nv50_read32(V9X_NV50_CLK_NVPLL_COEF);
    input.gpio_0_7 = v9x_nv50_read32(V9X_NV50_CLK_GPIO_0_7);
    if (v9x_nv50_clk_build(&input, v9x_nv50_ops, V9X_NV50_INIT_OPS_MAX,
                           &count) != V9X_STATUS_OK || count == 0ul) {
        if (chipset == V9X_NV50_CHIPSET_G96) {
            v9x_nv50_write_clock(&input, "kept");
        }
        return 1;
    }
    list.count = count;
    list.fifo_end = count;
    list.gr_end = count;
    ran = v9x_nv50_replay(&list);
    v9x_nv50_write_clock(&input, ran ? "raised" : "sequencer-timeout");
    return ran;
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

    if (!v9x_nv50_reclock(grctx.chipset)) {
        return 0;
    }

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

    /* A fresh RAMFC starts its ring at 0; nothing is resident to read an
     * index from until this first kick. */
    v9x_nv50_push_init(&push, words, V9X_NV50_PUSH_WORDS);
    v9x_nv50_push_bind_2d(&push);
    if (push.overflow != 0ul) {
        return 0;
    }
    if (!v9x_nv50_submit_at(&push, 0ul)) {
        return 0;
    }
    return v9x_nv50_wait_drained();
}

/* A channel at the reserve is live: its table entry is there, and PGRAPH
 * holds its context. The values come from the reserve's address alone
 * (nv50_2d.c), so this also recognises a channel brought up before the
 * last Enable, or by an earlier load of this DLL. */
static int v9x_nv50_state_holds(void)
{
    return v9x_nv50_read32(V9X_NV50_PFIFO_CHAN(V9X_NV50_ENGINE_CHID)) ==
               v9x_nv50_live_chan_entry(v9x_hal->engine.reserve_offset) &&
           v9x_nv50_read32(V9X_NV50_PGRAPH_CTX) ==
               v9x_nv50_live_context(v9x_hal->engine.reserve_offset);
}

/*
 * The engine's state at the first give-up, in C:\V9XDIAG\V9XN50F.TXT: the
 * interrupt and status words, PGRAPH's trap words and the PROP and MP
 * trap words of TP0 (nouveau nv50_gr_trap_handler, as the channel probe
 * reads them), and the last kick. 3DMark 2000's first start on step 5b
 * gave the engine up inside a Lock with nothing but N5ID at 400700h to
 * go on (M-BXMFA8, boot 122).
 */
#define V9X_NV50_FAULT_PATH V9X_DIAG_PATH("V9XN50F.TXT")

static void v9x_nv50_fault_text(HANDLE file, const char *text)
{
    DWORD written;
    DWORD length = 0ul;

    while (text[length] != '\0') {
        ++length;
    }
    (void)WriteFile(file, text, length, &written, 0);
}

static void v9x_nv50_fault_hex(HANDLE file, const char *key, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char text[12];
    DWORD index;

    v9x_nv50_fault_text(file, key);
    for (index = 0ul; index < 8ul; ++index) {
        text[index] = digits[(value >> ((7ul - index) * 4ul)) & 0xful];
    }
    text[8] = '\r';
    text[9] = '\n';
    text[10] = '\0';
    v9x_nv50_fault_text(file, text);
}

/*
 * The G96's clocks at bring-up, in C:\V9XDIAG\V9XN5CLK.INI, which V9XTRACE
 * copies into its report: what the PLLs held before, what they hold now,
 * both decoded on the PCIe reference (nv50_clk.h), and whether the HAL
 * raised them (raised), left them (kept: not the state the script is
 * built for) or saw the sequencer never finish (sequencer-timeout). The
 * 16-bit driver writes V9XHW.INI before this runs, so the clocks are the
 * HAL's to report (docs\plans\nvidia-g96-reclock.md).
 */
#define V9X_NV50_CLOCK_PATH V9X_DIAG_PATH("V9XN5CLK.INI")

static void v9x_nv50_clock_khz(HANDLE file, const char *key, DWORD value)
{
    char digits[12];
    char text[14];
    DWORD count = 0ul;
    DWORD at = 0ul;

    v9x_nv50_fault_text(file, key);
    do {
        digits[count++] = (char)('0' + (value % 10ul));
        value /= 10ul;
    } while (value != 0ul && count < sizeof(digits));
    while (count != 0ul) {
        text[at++] = digits[--count];
    }
    text[at++] = '\r';
    text[at++] = '\n';
    text[at] = '\0';
    v9x_nv50_fault_text(file, text);
}

static void v9x_nv50_write_clock(const struct v9x_nv50_clk_input *before,
                                 const char *result)
{
    HANDLE file;
    DWORD spll_ctrl;
    DWORD spll_coef;
    DWORD nvpll_ctrl;
    DWORD nvpll_coef;

    spll_ctrl = v9x_nv50_read32(V9X_NV50_CLK_SPLL_CTRL);
    spll_coef = v9x_nv50_read32(V9X_NV50_CLK_SPLL_COEF);
    nvpll_ctrl = v9x_nv50_read32(V9X_NV50_CLK_NVPLL_CTRL);
    nvpll_coef = v9x_nv50_read32(V9X_NV50_CLK_NVPLL_COEF);

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    file = CreateFileA(V9X_NV50_CLOCK_PATH, GENERIC_WRITE, 0, 0,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    v9x_nv50_fault_text(file, "[Nv50Clock]\r\nResult=");
    v9x_nv50_fault_text(file, result);
    v9x_nv50_fault_text(file, "\r\n");
    v9x_nv50_clock_khz(file, "CoreKHzBefore=",
                       v9x_nv50_clk_pll_khz(before->nvpll_ctrl,
                                            before->nvpll_coef));
    v9x_nv50_clock_khz(file, "ShaderKHzBefore=",
                       v9x_nv50_clk_pll_khz(before->spll_ctrl,
                                            before->spll_coef));
    v9x_nv50_clock_khz(file, "CoreKHz=",
                       v9x_nv50_clk_pll_khz(nvpll_ctrl, nvpll_coef));
    v9x_nv50_clock_khz(file, "ShaderKHz=",
                       v9x_nv50_clk_pll_khz(spll_ctrl, spll_coef));
    v9x_nv50_fault_hex(file, "Source=", before->source);
    v9x_nv50_fault_hex(file, "RefSelect=", before->ref_select);
    v9x_nv50_fault_hex(file, "Gpio0to7=", before->gpio_0_7);
    v9x_nv50_fault_hex(file, "NvpllCtrlBefore=", before->nvpll_ctrl);
    v9x_nv50_fault_hex(file, "NvpllCoefBefore=", before->nvpll_coef);
    v9x_nv50_fault_hex(file, "SpllCtrlBefore=", before->spll_ctrl);
    v9x_nv50_fault_hex(file, "SpllCoefBefore=", before->spll_coef);
    v9x_nv50_fault_hex(file, "NvpllCtrl=", nvpll_ctrl);
    v9x_nv50_fault_hex(file, "NvpllCoef=", nvpll_coef);
    v9x_nv50_fault_hex(file, "SpllCtrl=", spll_ctrl);
    v9x_nv50_fault_hex(file, "SpllCoef=", spll_coef);
    v9x_nv50_fault_hex(file, "SourceAfter=",
                       v9x_nv50_read32(V9X_NV50_CLK_SOURCE));
    CloseHandle(file);
}

static void v9x_nv50_write_fault(DWORD code, DWORD address)
{
    static const struct {
        const char *key;
        DWORD reg;
    } regs[] = {
        { "PfifoIntr=", V9X_NV50_PFIFO_INTR },
        { "PfifoDmaState=", V9X_NV50_PFIFO_DMA_STATE },
        { "PfifoIbGet=", V9X_NV50_PFIFO_IB_GET },
        { "PfifoIbPut=", V9X_NV50_PFIFO_IB_PUT },
        { "PfifoDmaGet=", V9X_NV50_PFIFO_DMA_GET },
        { "PfifoDmaPut=", V9X_NV50_PFIFO_DMA_PUT },
        { "PgraphIntr=", V9X_NV50_PGRAPH_INTR },
        { "PgraphTrap=", 0x400108ul },
        { "PgraphStatus=", V9X_NV50_PGRAPH_STATUS },
        { "PgraphTrapStatus=", 0x400804ul },
        { "PgraphTrapAddr=", 0x400808ul },
        { "PgraphTrapData=", 0x40080cul },
        { "PgraphTrapDataHigh=", 0x400810ul },
        { "PgraphTrapClass=", 0x400814ul },
        { "PropTp0Status=", 0x408708ul },
        { "PropTp0E0c=", 0x40870cul },
        { "PropTp0AddressLow=", 0x408710ul },
        { "PropTp0AddressHigh=", 0x408714ul },
        { "PropTp0E18=", 0x408718ul },
        { "PropTp0E1c=", 0x40871cul },
        { "PropTp0E20=", 0x408720ul },
        { "PropTp0E24=", 0x408724ul },
        { "MpTp0Status=", 0x40831cul },
        { "Mp0Status=", 0x408114ul },
        { "Mp0Pc=", 0x408124ul },
        { "Mp1Status=", 0x408194ul },
        { "Mp1Pc=", 0x4081a4ul },
        { "VmTrap=", 0x100c90ul }
    };
    HANDLE file;
    DWORD index;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    file = CreateFileA(V9X_NV50_FAULT_PATH, GENERIC_WRITE, FILE_SHARE_READ,
                       0, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    v9x_nv50_fault_text(file, "[Nv50Fault]\r\n");
    v9x_nv50_fault_hex(file, "Code=", code);
    v9x_nv50_fault_hex(file, "Address=", address);
    v9x_nv50_fault_hex(file, "LastKickKind=", v9x_nv50_last_kick_kind);
    v9x_nv50_fault_hex(file, "LastKickWords=", v9x_nv50_last_kick_words);
    for (index = 0ul; index < sizeof(regs) / sizeof(regs[0]); ++index) {
        v9x_nv50_fault_hex(file, regs[index].key,
                           v9x_nv50_read32(regs[index].reg));
    }
    for (index = 0ul; index < V9X_NV50_PUSH_WORDS; ++index) {
        v9x_nv50_fault_hex(file, "Last2d=", v9x_nv50_last_2d[index]);
    }
    CloseHandle(file);
}

static void v9x_nv50_give_up(DWORD code, DWORD address)
{
    if (v9x_hal->engine.idle_timeouts++ == 0ul) {
        v9x_nv50_write_fault(code, address);
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
    /* An Enable clears the validated flag (dd16.c republishes the
     * record), but a VBE mode set leaves the channel as it was (runs
     * 37-39 on M-BXMFA8). A channel that is still live is kept, not
     * rebuilt: GDI may be drawing on it, and the first-run session
     * rebuilt it three times for nothing. reset_count counts builds. */
    if (v9x_nv50_state_holds()) {
        v9x_hal->engine.flags |= V9X_DD_ENGINE_STATUS_VALIDATED;
        return 1;
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

static int v9x_nv50_drain(int wait)
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

/*
 * The wait every DirectDraw path takes - Flip, Lock and Blt through
 * v9x_render_drain, and this file's fill and copy - with Direct3D's
 * shadow targets copied back to their linear targets first
 * (d3d_nv50.c), so nothing reads or writes a target the shadow has
 * newer drawing for. Direct3D's own batches drain without it.
 */
static int v9x_nv50_wait_idle(int wait)
{
    if (v9x_nv50_validate() && v9x_nv50_format() != 0ul) {
        (void)v9x_d3d_nv50_resolve();
    }
    return v9x_nv50_drain(wait);
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
    /* A Z buffer the 3D engine placed is tiled: its fills are draws. */
    if ((data->lpDDDestSurface->ddsCaps & V9X_DDSCAPS_ZBUFFER) != 0ul) {
        if (!v9x_nv50_wait_idle(wait)) {
            return V9X_BLT_BUSY;
        }
        return v9x_d3d_nv50_depth_fill(data->lpDDDestSurface, data->rDest,
                                       data->bltFX.dwFillColor, wait);
    }
    format = v9x_nv50_2d_format(bytes_per_pixel);
    if (format == 0ul || v9x_nv50_format() != format ||
        (offset & (V9X_NV50_SURFACE_ALIGN - 1ul)) != 0ul) {
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
    if (!v9x_nv50_submit(&push)) {
        /* PFIFO never showed the kick: the ring's state is unknown. */
        v9x_nv50_give_up(V9X_NV50_FAULT_IDLE, V9X_NV50_PFIFO_IB_PUT);
        return V9X_BLT_DECLINED;
    }
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
    if (format == 0ul || v9x_nv50_format() != format ||
        (destination_offset & (V9X_NV50_SURFACE_ALIGN - 1ul)) != 0ul) {
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
    if (!v9x_nv50_submit(&push)) {
        /* PFIFO never showed the kick: the ring's state is unknown. */
        v9x_nv50_give_up(V9X_NV50_FAULT_IDLE, V9X_NV50_PFIFO_IB_PUT);
        return V9X_BLT_DECLINED;
    }
    v9x_present_note_submission();
    return V9X_BLT_DONE;
}

/*
 * Direct3D's batches (d3d_nv50.c, ddhal_internal.h). A batch waits for
 * everything before it as the 2D operations do, so the push area and the
 * scratch are never written while the GPU reads them. The area is the
 * reserve's (gt218_reserve.h), addressed through the identity VM.
 */
int v9x_nv50_3d_begin(volatile DWORD **push, DWORD *push_words,
                      volatile DWORD **scratch, DWORD *scratch_gpu,
                      DWORD *generation)
{
    if (!v9x_nv50_validate() || v9x_nv50_format() == 0ul ||
        !v9x_nv50_drain(1)) {
        return 0;
    }
    *push = v9x_nv50_region(V9X_GT218_RESERVE_3D_PUSH);
    *push_words = V9X_GT218_RESERVE_3D_PUSH_BYTES / 4ul;
    *scratch = v9x_nv50_region(V9X_GT218_RESERVE_3D_SCRATCH);
    *scratch_gpu = v9x_hal->engine.reserve_offset +
                   V9X_GT218_RESERVE_3D_SCRATCH;
    *generation = v9x_hal->engine.reset_count;
    return 1;
}

int v9x_nv50_3d_submit(DWORD count)
{
    volatile DWORD *ring = v9x_nv50_region(V9X_NV50_IB);
    DWORD entry[2];
    DWORD at;
    DWORD next;
    DWORD spins;

    if (count == 0ul || count > V9X_GT218_RESERVE_3D_PUSH_BYTES / 4ul) {
        return 0;
    }
    at = v9x_nv50_read32(V9X_NV50_PFIFO_IB_GET);
    if (at >= V9X_NV50_IB_ENTRIES) {
        return 0;
    }
    v9x_nv50_last_kick_kind = 2ul;
    v9x_nv50_last_kick_words = count;
    v9x_nv50_ib_entry(v9x_hal->engine.reserve_offset +
                          V9X_GT218_RESERVE_3D_PUSH,
                      count, entry);
    ring[at * 2ul] = entry[0];
    ring[at * 2ul + 1ul] = entry[1];
    /* The batch, its tables and the IB entry are all write-combined. */
    v9x_wc_drain();
    next = v9x_nv50_ib_next(at);
    v9x_nv50_write32(V9X_NV50_USERD(V9X_NV50_ENGINE_CHID) +
                     V9X_NV50_USERD_IB_PUT, next);
    spins = V9X_NV50_PUT_SPINS;
    while (v9x_nv50_read32(V9X_NV50_PFIFO_IB_PUT) != next) {
        if (spins-- == 0ul) {
            v9x_nv50_give_up(V9X_NV50_FAULT_IDLE, V9X_NV50_PFIFO_IB_PUT);
            return 0;
        }
    }
    v9x_present_note_submission();
    return 1;
}

/* CANBLT is the non-blocking idle poll, as on the Trio. */
static int v9x_nv50_can_blt(void)
{
    return v9x_nv50_wait_idle(0);
}

/*
 * The display start, for page flips. In the modes the VBE sets the head
 * scans out through the VGA CRTC emulation, not an EVO channel, so a flip
 * is a CRTC write (nv50_crtc.h has the measurement and the VBIOS routine
 * the layout comes from). Until this, every Flip on this row was declined
 * and DirectDraw's fallback left 3DMark 2001 SE's clears on screen
 * (docs\issues\2026-11-16-gt218-3dmark2001se-car-chase-cleared-frames-
 * on-screen.md).
 *
 * The extended registers are opened with CR3F = 57h and CR3F is written
 * back as it read, as the VBIOS's int 10h entry does. Low byte first, as
 * the RIVA path writes it. The retrace is the VGA status port's (3DAh
 * bit 3, 60 Hz on W9QI0DAD), which the core's default timing uses; whether
 * the emulation latches the start at the retrace, as VGA does, or applies
 * it at once is not measured, and under the second this tears.
 */
int v9x_nv50_scanout_active(void)
{
    if (v9x_hal == 0 ||
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) == 0ul ||
        v9x_hal->engine.engine_type != V9X_DD_ENGINE_TYPE_NV50) {
        return 0;
    }
    return (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_FLIP) != 0ul;
}

int v9x_nv50_set_display_start(DWORD byte_offset)
{
    struct v9x_nv50_crtc_start start;
    unsigned char lock;

    if ((v9x_hal->fb.flags & V9X_DD_FB_VALID) == 0ul ||
        v9x_nv50_build_crtc_start(byte_offset, v9x_hal->fb.vram_bytes,
                                  &start) != V9X_STATUS_OK) {
        return 0;
    }
    lock = v9x_read_crtc(V9X_NV50_CR_LOCK);
    v9x_write_crtc(V9X_NV50_CR_LOCK, V9X_NV50_CR_UNLOCK);
    v9x_write_crtc(V9X_NV50_CR_START_LOW, start.cr0d);
    v9x_write_crtc(V9X_NV50_CR_START_HIGH, start.cr0c);
    v9x_write_crtc(V9X_NV50_CR_START_MID, start.cr35);
    v9x_write_crtc(V9X_NV50_CR_START_TOP, start.cr34);
    v9x_write_crtc(V9X_NV50_CR_LOCK, lock);
    return 1;
}

const V9X_ENGINE32_OPS v9x_engine32_nv50 = {
    v9x_nv50_ready,
    v9x_nv50_validate,
    v9x_nv50_validated,
    v9x_nv50_can_blt,
    v9x_nv50_wait_idle,
    v9x_nv50_fill,
    v9x_nv50_copy,
    V9X_NV50_SURFACE_ALIGN
};
