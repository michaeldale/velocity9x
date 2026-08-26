/*
 * GDI acceleration for V9XDISP.DRV: the BitBlt dispatcher, its acceptance
 * gates, the S3 primitives, the bounded waits, and the poison latch.
 *
 * Ordinal 1 used to be an unconditional `jmp DIB_BitBlt`. It is this file now,
 * and that has a consequence worth stating at the top: all four families link
 * the same display16 layer, and three of them - ati, vbe and matrox-m2 -
 * declare EngineType NONE on every chip. So for three quarters of the fleet
 * the decline path below is not scaffolding on the way to acceleration; it is
 * the shipping code, on every blit, for ever. Its cost is one WORD test
 * against v9x_gdi_enabled, which a family with no engine leaves at zero.
 *
 * The signature and the gate order are copied from first-party DDK code, not
 * from the documentation: 98DDK\src\display\mini\xga\BITBLT.ASM:57 declares the
 * eleven-argument entry point with its arguments named in push order, and
 * :66-77 is the gate sequence - test VRAM, test BUSY, test PALETTE_XLAT, then
 * give it a try. 98DDK\src\display\mini\s3v\S3BLT.ASM is the same driver family
 * as ours and does the same thing at :128-133.
 *
 * A far-Pascal frame that disagrees with the caller by one word corrupts the
 * stack on the first blit GDI issues, which on this path is during boot. That
 * is why the argument list below is transcribed rather than reconstructed.
 *
 * See docs\plans\gdi-acceleration.md for the design and
 * docs\plans\gdi-accel-000-and-harness.md for this build.
 */
/*
 * Ordinal 1 is named BitBlt, and so is the Win16 GDI API windows.h declares.
 * Rename the API out of the way for the duration of the include, the same way
 * ddi.c does for SetCursor: this file exports the driver-side entry point, and
 * it never calls the application-side one.
 */
#define BitBlt V9xUserBitBlt
#define SetCursor V9xUserSetCursor
#include <windows.h>
#undef SetCursor
#undef BitBlt

#include "velocity9x/engine_abi.h"
#include "velocity9x/hw16.h"
#include "velocity9x/s3_engine_regs.h"
#include "velocity9x/win9x_ddraw_abi.h"
#include "win9x_display_abi.h"
#include "gdi_accel.h"

/* ddi.c: the live screen PDEVICE, or NULL between Disable and Enable. This is
 * the driver's own liveness test and the identity an accelerated destination
 * has to match. */
extern V9X_DIB_ENGINE FAR *v9x_driver_pdevice;
extern void v9x_serial_write(const char FAR *message);
extern const V9X_HW16_DEVICE *v9x_hw16_active_device(void);

/* runtime.asm. */
extern DWORD FAR PASCAL V9xLinearBase(void);
extern WORD FAR PASCAL V9xEngineSelector(void);
extern DWORD FAR PASCAL V9xEngineRead(WORD offset);
extern void FAR PASCAL V9xEngineWrite(WORD offset, DWORD value);
extern WORD FAR PASCAL V9xDibBitBltCall(V9X_DIB_ENGINE FAR *destination_device,
                                        WORD destination_x,
                                        WORD destination_y,
                                        V9X_DIB_ENGINE FAR *source_device,
                                        WORD source_x, WORD source_y,
                                        WORD x_extent, WORD y_extent,
                                        DWORD rop,
                                        V9X_DIB_BRUSH FAR *brush,
                                        LPVOID draw_mode);

static WORD v9x_gdi_port_in_word(WORD port);
#pragma aux v9x_gdi_port_in_word = "in ax,dx" parm [dx] value [ax] \
    modify exact [ax]

static void v9x_gdi_port_out_word(WORD port, WORD value);
#pragma aux v9x_gdi_port_out_word = "out dx,ax" parm [dx] [ax] \
    modify exact []

static BYTE v9x_gdi_port_in(WORD port);
#pragma aux v9x_gdi_port_in = "in al,dx" parm [dx] value [al] \
    modify exact [al]

static void v9x_gdi_port_out(WORD port, BYTE value);
#pragma aux v9x_gdi_port_out = "out dx,al" parm [dx] [al] modify exact []

#define V9X_HARDWARE_INFO_PATH  "C:\\V9XHW.INI"
#define V9X_SYSTEM_INI          "SYSTEM.INI"
#define V9X_INI_SECTION         "Velocity9x"

/*
 * Compile-time primitive defaults, advanced per rollout build.
 *
 * Build 000 is all zeroes on purpose: every primitive below is compiled and
 * none of them is reachable, so the exit gate can ask whether adding this file
 * changed what the driver does rather than whether the new code is correct.
 * Build 001 turns fill on here, 002 copy, 003 overlap.
 */
#define V9X_GDI_DEFAULT_MASTER   1
#define V9X_GDI_DEFAULT_FILL     0
#define V9X_GDI_DEFAULT_COPY     0
#define V9X_GDI_DEFAULT_OVERLAP  0

/*
 * Smallest rectangle worth handing to the engine, in pixels.
 *
 * Below this the register programming and the cursor exclusion cost more than
 * the DIB Engine's inner loop, so a small fill accelerated is a small fill
 * made slower. 1024 is 32x32; INI-tunable so the crossover can be measured
 * rather than argued about.
 */
#define V9X_GDI_DEFAULT_THRESHOLD  1024ul

/*
 * Bounded waits, in iterations.
 *
 * These are NOT the 32-bit HAL's numbers, and must not be: this driver is
 * built without a -3, so wcc emits 8086 code, and one iteration here is a far
 * call into V9xEngineRead plus a DWORD countdown - call it 60 to 100 clocks
 * against the flat HAL's ten or so. Scaling the HAL's 0x00400000 by that
 * factor lands near 0x00010000, which on a 66 MHz part is roughly 100 ms: far
 * longer than any rectangle this driver accelerates takes to complete, and
 * short enough that the one operation that ever waits this long is not
 * mistaken for a hang. It is paid at most once per session, because the
 * expiry poisons the latch.
 *
 * Uncalibrated. Open item 3 of docs\plans\gdi-acceleration.md is the
 * measurement, on 86Box, through the serial log; these are the starting
 * points that measurement should replace, and the arithmetic above is what
 * they are derived from rather than a guess dressed as one.
 */
#define V9X_GDI_FIFO_SPIN_LIMIT   0x00004000ul
#define V9X_GDI_IDLE_SPIN_LIMIT   0x00010000ul

/* FIFO slots one accelerated operation needs before it starts writing. */
#define V9X_GDI_FIFO_SLOTS              8ul

/* ROP256 codes, the high word of a GDI Rop. */
#define V9X_ROP256_BLACKNESS          0x00u
#define V9X_ROP256_SRCCOPY            0xccu
#define V9X_ROP256_PATCOPY            0xf0u
#define V9X_ROP256_WHITENESS          0xffu

WORD v9x_gdi_engine_dirty;
WORD v9x_gdi_poisoned;

static V9X_GDI_STATS v9x_gdi;
static DWORD v9x_gdi_fault_inject;
static DWORD v9x_gdi_engine_type;
static DWORD v9x_gdi_engine_caps;
static WORD v9x_gdi_report_pending;

/*
 * What the engine is reachable through, resolved once per Enable.
 *
 * A ViRGE needs its MMIO selector; a Trio64 needs nothing but the fixed
 * 8514/A ports. Zero means "no primitive can run", which is the state every
 * engine-less family stays in for ever.
 */
static WORD v9x_gdi_engine_live;

static void v9x_gdi_write(DWORD offset, DWORD value)
{
    V9xEngineWrite((WORD)offset, value);
}

static DWORD v9x_gdi_read(DWORD offset)
{
    return V9xEngineRead((WORD)offset);
}

/*
 * Consume one armed fault injection.
 *
 * Mirrors the 32-bit injector (docs\decisions\2026-08-16-engine-fault-
 * injection.md): an armed count is spent by the production bounded waits
 * falling into their existing timeout tail, so the injector drives the
 * shipping recovery path rather than a parallel test one. The consequence
 * this build owns is that on a default 000 build no GDI bounded wait ever
 * runs, so an armed injection is never consumed and Poisoned stays 0 -
 * honestly. The harness gates its injection step on a primitive being enabled
 * for exactly that reason.
 */
static WORD v9x_gdi_fault_injected(void)
{
    if (v9x_gdi_fault_inject == 0ul) {
        return 0u;
    }
    --v9x_gdi_fault_inject;
    v9x_gdi.fault_inject = v9x_gdi_fault_inject;
    return 1u;
}

/*
 * CR66 bit 1 is the ViRGE/DX graphics-engine reset the Windows 98 S3 sample
 * uses, and it is touched only after a bounded wait has already expired. The
 * Trio64 has no recovery at all - the 32-bit side measured that a forced
 * timeout there raises idle_timeouts and leaves reset_count flat - so this is
 * deliberately ViRGE-only rather than a family-wide gesture.
 */
static void v9x_gdi_recover(void)
{
    BYTE cr66;

    if (v9x_gdi_engine_type != V9X_DD_ENGINE_TYPE_S3_VIRGE_DX) {
        return;
    }
    v9x_gdi_port_out(V9X_CRTC_INDEX, V9X_VIRGE_CRTC_CR66);
    cr66 = v9x_gdi_port_in(V9X_CRTC_DATA);
    v9x_gdi_port_out(V9X_CRTC_DATA,
                     (BYTE)(cr66 | V9X_VIRGE_CR66_ENGINE_RESET));
    v9x_gdi_port_out(V9X_CRTC_DATA, cr66);
    ++v9x_gdi.resets;
}

/*
 * Latch the session-long poison.
 *
 * This can run at interrupt time, on a software-cursor draw that took the
 * BeginAccess slow path, so it may touch nothing but DGROUP: no
 * WritePrivateProfileString, no serial port. The report is deferred to
 * whichever of the next BitBlt or Disable arrives first.
 */
static void v9x_gdi_poison(void)
{
    v9x_gdi_poisoned = 1u;
    v9x_gdi.poisoned = 1ul;
    v9x_gdi_engine_live = 0u;
    v9x_gdi.enabled = 0ul;
    v9x_gdi_report_pending = 1u;
    v9x_gdi_recover();
}

void v9x_gdi_accel_flush_report(void)
{
    if (v9x_gdi_report_pending == 0u) {
        return;
    }
    v9x_gdi_report_pending = 0u;
    v9x_serial_write("V9X-DRV gdi-poisoned\r\n");
    WritePrivateProfileString("Velocity9xHardware", "GdiAcceleration",
                              "gdi-poisoned", V9X_HARDWARE_INFO_PATH);
}

/* ViRGE SUBSYS_STAT carries a FIFO free-slot count and an idle bit. */
static DWORD v9x_gdi_virge_status(void)
{
    return v9x_gdi_read(V9X_VIRGE_ENGINE_STATUS);
}

static DWORD v9x_gdi_virge_fifo_free(DWORD status)
{
    return (status & V9X_VIRGE_STATUS_FIFO_MASK) >>
           V9X_VIRGE_STATUS_FIFO_SHIFT;
}

static WORD v9x_gdi_wait_fifo(DWORD entries)
{
    DWORD spins;

    if (v9x_gdi_engine_live == 0u) {
        return 0u;
    }
    if (v9x_gdi_fault_injected() == 0u) {
        if (v9x_gdi_virge_fifo_free(v9x_gdi_virge_status()) >= entries) {
            return 1u;
        }
        spins = V9X_GDI_FIFO_SPIN_LIMIT;
        while (spins-- != 0ul) {
            if (v9x_gdi_virge_fifo_free(v9x_gdi_virge_status()) >= entries) {
                return 1u;
            }
        }
    }
    ++v9x_gdi.fifo_timeouts;
    v9x_gdi_poison();
    return 0u;
}

static WORD v9x_gdi_wait_idle(void)
{
    DWORD spins;

    if (v9x_gdi_engine_live == 0u) {
        return 0u;
    }
    if (v9x_gdi_fault_injected() == 0u) {
        spins = V9X_GDI_IDLE_SPIN_LIMIT;
        if (v9x_gdi_engine_type == V9X_DD_ENGINE_TYPE_S3_TRIO64) {
            do {
                if ((v9x_gdi_port_in_word(V9X_TRIO_CMD_STATUS) &
                     V9X_TRIO_STATUS_BUSY) == 0u) {
                    return 1u;
                }
            } while (spins-- != 0ul);
        } else {
            do {
                if ((v9x_gdi_virge_status() & V9X_VIRGE_STATUS_IDLE) != 0ul) {
                    return 1u;
                }
            } while (spins-- != 0ul);
        }
    }
    ++v9x_gdi.idle_timeouts;
    v9x_gdi_poison();
    return 0u;
}

/*
 * The BeginAccess slow path, reached from runtime.asm only when the dirty flag
 * is set.
 *
 * Interrupt-time constraints apply: MMIO, ports and DGROUP only. The drain
 * clears the flag whether or not the engine went idle, because a poisoned
 * engine will never go idle and leaving the flag set would take every later
 * CPU access down this path for nothing. The pixels stay correct either way -
 * the failed operation was completed by the DIB Engine.
 */
void __loadds FAR PASCAL V9xGdiBeginAccessSlow(void)
{
    ++v9x_gdi.drains;
    (void)v9x_gdi_wait_idle();
    v9x_gdi_engine_dirty = 0u;
}

/*
 * One accelerated operation, resolved from the DDI arguments before any
 * register is touched. Gathering it first is what lets the gates run
 * cheapest-first without the primitives re-deriving anything.
 */
typedef struct v9x_gdi_op {
    DWORD base;              /* destination byte offset into the aperture */
    DWORD pitch;
    DWORD color;             /* physical fill colour, already depth-sized */
    WORD bytes_per_pixel;
    WORD destination_x;
    WORD destination_y;
    WORD source_x;
    WORD source_y;
    WORD width;
    WORD height;
} V9X_GDI_OP;

static WORD v9x_gdi_virge_fill(const V9X_GDI_OP *op)
{
    DWORD command;

    if (v9x_gdi_wait_fifo(V9X_GDI_FIFO_SLOTS) == 0u) {
        return 0u;
    }
    command = V9X_VIRGE_CMD_ROP_PATCOPY | V9X_VIRGE_CMD_MONO_PATTERN |
              V9X_VIRGE_CMD_X_POSITIVE | V9X_VIRGE_CMD_Y_POSITIVE |
              V9X_VIRGE_CMD_DRAW_ENABLE |
              ((DWORD)(op->bytes_per_pixel - 1u) << 2);
    v9x_gdi_write(V9X_VIRGE_DEST_BASE, op->base);
    v9x_gdi_write(V9X_VIRGE_PATTERN_FG, op->color);
    v9x_gdi_write(V9X_VIRGE_DEST_SRC_STRIDE, op->pitch << 16);
    /* An all-ones mono pattern is what turns a pattern blit into a solid
     * fill, so no realized-brush pattern is ever parsed. */
    v9x_gdi_write(V9X_VIRGE_MONO_PAT_0, 0xfffffffful);
    v9x_gdi_write(V9X_VIRGE_MONO_PAT_1, 0xfffffffful);
    v9x_gdi_write(V9X_VIRGE_RECT_WH,
                  (((DWORD)op->width - 1ul) << 16) | (DWORD)op->height);
    v9x_gdi_write(V9X_VIRGE_RECT_DEST_XY,
                  ((DWORD)op->destination_x << 16) |
                  (DWORD)op->destination_y);
    /* Autoexecute is left clear, so this write is what starts the blit. */
    v9x_gdi_write(V9X_VIRGE_COMMAND, command);
    return 1u;
}

/*
 * Screen-to-screen SRCCOPY on the ViRGE 2D engine.
 *
 * Overlap is handled by direction rather than by row order: the engine walks
 * from whichever corner keeps a same-surface copy correct, which is why the
 * scan direction is derived from the rectangles. Mirrors v9x_virge_copy in
 * src\display32\engines\eng_s3_virge.c, whose measured result is
 * docs\decisions\2026-08-14-virge-blitter.md.
 */
static WORD v9x_gdi_virge_copy(const V9X_GDI_OP *op)
{
    DWORD command;
    DWORD source_x = (DWORD)op->source_x;
    DWORD source_y = (DWORD)op->source_y;
    DWORD destination_x = (DWORD)op->destination_x;
    DWORD destination_y = (DWORD)op->destination_y;
    WORD x_positive = 1u;
    WORD y_positive = 1u;

    if (destination_y > source_y) {
        y_positive = 0u;
        source_y += (DWORD)op->height - 1ul;
        destination_y += (DWORD)op->height - 1ul;
    } else if (destination_y == source_y && destination_x > source_x) {
        x_positive = 0u;
        source_x += (DWORD)op->width - 1ul;
        destination_x += (DWORD)op->width - 1ul;
    }

    if (v9x_gdi_wait_fifo(V9X_GDI_FIFO_SLOTS) == 0u) {
        return 0u;
    }
    command = V9X_VIRGE_CMD_ROP_SRCCOPY | V9X_VIRGE_CMD_DRAW_ENABLE |
              ((DWORD)(op->bytes_per_pixel - 1u) << 2) |
              (x_positive != 0u ? V9X_VIRGE_CMD_X_POSITIVE : 0ul) |
              (y_positive != 0u ? V9X_VIRGE_CMD_Y_POSITIVE : 0ul);
    v9x_gdi_write(V9X_VIRGE_SRC_BASE, op->base);
    v9x_gdi_write(V9X_VIRGE_DEST_BASE, op->base);
    v9x_gdi_write(V9X_VIRGE_DEST_SRC_STRIDE,
                  (op->pitch << 16) | op->pitch);
    v9x_gdi_write(V9X_VIRGE_RECT_WH,
                  (((DWORD)op->width - 1ul) << 16) | (DWORD)op->height);
    v9x_gdi_write(V9X_VIRGE_RECT_SRC_XY, (source_x << 16) | source_y);
    v9x_gdi_write(V9X_VIRGE_RECT_DEST_XY,
                  (destination_x << 16) | destination_y);
    v9x_gdi_write(V9X_VIRGE_COMMAND, command);
    return 1u;
}

/*
 * Solid rectangle fill on the Trio32/64 enhanced 8514/A engine, per the
 * databook section 13.3.3 sequence the 32-bit side already drives.
 *
 * This engine has no per-surface base or stride: it walks display memory as
 * one surface at the display pitch, so the destination's byte offset has to be
 * folded into the y coordinate, which the gates have already required to be a
 * whole number of scan lines.
 */
static WORD v9x_gdi_trio_fill(const V9X_GDI_OP *op)
{
    DWORD y = op->base / op->pitch + (DWORD)op->destination_y;

    if (v9x_gdi_wait_idle() == 0u) {
        return 0u;
    }
    v9x_gdi_port_out_word(V9X_TRIO_FRGD_MIX, V9X_TRIO_FRGD_MIX_NEW);
    v9x_gdi_port_out_word(V9X_TRIO_FRGD_COLOR, (WORD)op->color);
    v9x_gdi_port_out_word(V9X_TRIO_MULTIFUNC_CNTL,
                          V9X_TRIO_PIXEL_CNTL_FRGD_MIX);
    v9x_gdi_port_out_word(V9X_TRIO_CUR_X, op->destination_x);
    v9x_gdi_port_out_word(V9X_TRIO_CUR_Y, (WORD)y);
    v9x_gdi_port_out_word(V9X_TRIO_MAJ_AXIS_PCNT, (WORD)(op->width - 1u));
    v9x_gdi_port_out_word(V9X_TRIO_MULTIFUNC_CNTL, (WORD)(op->height - 1u));
    v9x_gdi_port_out_word(V9X_TRIO_CMD_STATUS, V9X_TRIO_CMD_RECT_SOLID);
    return 1u;
}

static WORD v9x_gdi_trio_copy(const V9X_GDI_OP *op)
{
    DWORD rows = op->base / op->pitch;
    DWORD source_y = rows + (DWORD)op->source_y;
    DWORD destination_y = rows + (DWORD)op->destination_y;
    DWORD source_x = (DWORD)op->source_x;
    DWORD destination_x = (DWORD)op->destination_x;
    WORD command = (WORD)(V9X_TRIO_CMD_BITBLT | V9X_TRIO_CMD_INC_X |
                          V9X_TRIO_CMD_INC_Y);

    if (destination_y > source_y) {
        command = (WORD)(command & ~V9X_TRIO_CMD_INC_Y);
        source_y += (DWORD)op->height - 1ul;
        destination_y += (DWORD)op->height - 1ul;
    } else if (destination_y == source_y && destination_x > source_x) {
        command = (WORD)(command & ~V9X_TRIO_CMD_INC_X);
        source_x += (DWORD)op->width - 1ul;
        destination_x += (DWORD)op->width - 1ul;
    }

    if (v9x_gdi_wait_idle() == 0u) {
        return 0u;
    }
    v9x_gdi_port_out_word(V9X_TRIO_FRGD_MIX, V9X_TRIO_FRGD_MIX_COPY);
    v9x_gdi_port_out_word(V9X_TRIO_MULTIFUNC_CNTL,
                          V9X_TRIO_PIXEL_CNTL_FRGD_MIX);
    v9x_gdi_port_out_word(V9X_TRIO_CUR_X, (WORD)source_x);
    v9x_gdi_port_out_word(V9X_TRIO_CUR_Y, (WORD)source_y);
    v9x_gdi_port_out_word(V9X_TRIO_DESTX_DIASTP, (WORD)destination_x);
    v9x_gdi_port_out_word(V9X_TRIO_DESTY_AXSTP, (WORD)destination_y);
    v9x_gdi_port_out_word(V9X_TRIO_MAJ_AXIS_PCNT, (WORD)(op->width - 1u));
    v9x_gdi_port_out_word(V9X_TRIO_MULTIFUNC_CNTL, (WORD)(op->height - 1u));
    v9x_gdi_port_out_word(V9X_TRIO_CMD_STATUS, command);
    return 1u;
}

/*
 * Lift a software cursor out of the rectangle about to be drawn.
 *
 * Called through the PDEVICE, exactly as the reference blitter does
 * (98DDK\src\display\mini\s3v\S3BLT.ASM:1778 calls deCursorExclude, which
 * DIBENG.INC:45 defines as an alias for deBeginAccess, with the blit rectangle
 * and CURSOREXCLUDE). The right and bottom coordinates it wants are the last
 * pixel drawn, not one past it.
 *
 * This also drains any pending engine work, because our own dirty-check stub
 * sits in front of DIB_BeginAccess - which is the correct order: the cursor
 * exclusion is a CPU write to the framebuffer, and it must not overtake an
 * engine command already in flight.
 */
static void v9x_gdi_exclude_cursor(V9X_DIB_ENGINE FAR *device,
                                   const V9X_GDI_OP *op)
{
    V9X_CURSOR_EXCLUDE_PROC exclude =
        (V9X_CURSOR_EXCLUDE_PROC)device->deBeginAccess;

    if (exclude == 0) {
        return;
    }
    (void)exclude((void FAR *)device, op->destination_x, op->destination_y,
                  (WORD)(op->destination_x + op->width - 1u),
                  (WORD)(op->destination_y + op->height - 1u),
                  V9X_CURSOREXCLUDE);
}

/*
 * Resolve this chip's engine and decide what may run.
 *
 * The capability mask comes from the chip module's own engine descriptor, the
 * same one dd16.c hands to DirectDraw, so a chip that does not claim
 * SOLID_FILL cannot have a GDI fill enabled on its behalf. An engine-less
 * family leaves v9x_gdi_engine_live at zero and every gate below short-circuits
 * on it.
 */
void v9x_gdi_accel_configure(void)
{
    const V9X_HW16_DEVICE *device = v9x_hw16_active_device();
    DWORD control_base = 0ul;
    DWORD aperture_bytes = 0ul;
    DWORD enabled = 0ul;
    WORD master;

    v9x_gdi.dwSize = sizeof(V9X_GDI_STATS);
    v9x_gdi.abi = V9X_DD_SHARED_ABI;
    /* Every primitive below is compiled into every family's binary, so what is
     * advertised is a property of the source and not of the chip. What the
     * chip will actually run is `enabled`. */
    v9x_gdi.advertised = V9X_GDI_PRIM_FILL | V9X_GDI_PRIM_COPY |
                         V9X_GDI_PRIM_OVERLAP;
    v9x_gdi_engine_type = V9X_DD_ENGINE_TYPE_NONE;
    v9x_gdi_engine_caps = 0ul;
    v9x_gdi_engine_live = 0u;

    if (device != 0 && device->fill_engine_descriptor != 0) {
        device->fill_engine_descriptor(V9xLinearBase(), &control_base,
                                       &aperture_bytes, &v9x_gdi_engine_type,
                                       &v9x_gdi_engine_caps);
    }
    v9x_gdi.engine_type = v9x_gdi_engine_type;

    master = (WORD)GetPrivateProfileInt(V9X_INI_SECTION, "GdiAccel",
                                        V9X_GDI_DEFAULT_MASTER,
                                        V9X_SYSTEM_INI);
    v9x_gdi.threshold = (DWORD)GetPrivateProfileInt(
        V9X_INI_SECTION, "GdiAccelThreshold",
        (int)V9X_GDI_DEFAULT_THRESHOLD, V9X_SYSTEM_INI);
    if (v9x_gdi.threshold == 0ul) {
        v9x_gdi.threshold = V9X_GDI_DEFAULT_THRESHOLD;
    }
    if (master != 0u) {
        if (GetPrivateProfileInt(V9X_INI_SECTION, "GdiAccelFill",
                                 V9X_GDI_DEFAULT_FILL, V9X_SYSTEM_INI) != 0) {
            enabled |= V9X_GDI_PRIM_FILL;
        }
        if (GetPrivateProfileInt(V9X_INI_SECTION, "GdiAccelCopy",
                                 V9X_GDI_DEFAULT_COPY, V9X_SYSTEM_INI) != 0) {
            enabled |= V9X_GDI_PRIM_COPY;
        }
        if (GetPrivateProfileInt(V9X_INI_SECTION, "GdiAccelOverlap",
                                 V9X_GDI_DEFAULT_OVERLAP,
                                 V9X_SYSTEM_INI) != 0) {
            enabled |= V9X_GDI_PRIM_OVERLAP;
        }
    }
    /* The chip is the authority on capability, and an overlap-capable copy is
     * still a copy: overlap without copy would enable nothing. */
    if ((v9x_gdi_engine_caps & V9X_DD_ENGINE_CAP_SOLID_FILL) == 0ul) {
        enabled &= ~V9X_GDI_PRIM_FILL;
    }
    if ((v9x_gdi_engine_caps & V9X_DD_ENGINE_CAP_SCREEN_COPY) == 0ul) {
        enabled &= ~(V9X_GDI_PRIM_COPY | V9X_GDI_PRIM_OVERLAP);
    }
    if ((enabled & V9X_GDI_PRIM_COPY) == 0ul) {
        enabled &= ~V9X_GDI_PRIM_OVERLAP;
    }
    /* A latched poison outlives a mode switch, so a ReEnable must not
     * re-enable what a timeout turned off. */
    if (v9x_gdi_poisoned != 0u) {
        enabled = 0ul;
    }
    v9x_gdi.enabled = enabled;

    if (enabled == 0ul) {
        return;
    }
    if (v9x_gdi_engine_type == V9X_DD_ENGINE_TYPE_S3_TRIO64) {
        /* Port I/O only: nothing to map, so the engine is reachable as soon as
         * the chip module says it is there. */
        v9x_gdi_engine_live = 1u;
    } else if (v9x_gdi_engine_type == V9X_DD_ENGINE_TYPE_S3_VIRGE_DX &&
               control_base != 0ul &&
               aperture_bytes > V9X_VIRGE_RECT_DEST_XY + 4ul) {
        v9x_gdi_engine_live = V9xEngineSelector() != 0u ? 1u : 0u;
    }
    if (v9x_gdi_engine_live == 0u) {
        v9x_gdi.enabled = 0ul;
    }
}

const char *v9x_gdi_accel_state_text(void)
{
    if (v9x_gdi_poisoned != 0u) {
        return "gdi-poisoned";
    }
    if (v9x_gdi.enabled == 0ul) {
        return "none";
    }
    if ((v9x_gdi.enabled & V9X_GDI_PRIM_OVERLAP) != 0ul) {
        return "gdi-fill-copy-overlap";
    }
    if ((v9x_gdi.enabled & V9X_GDI_PRIM_COPY) != 0ul) {
        return (v9x_gdi.enabled & V9X_GDI_PRIM_FILL) != 0ul
                   ? "gdi-fill-copy" : "gdi-copy";
    }
    return "gdi-fill";
}

WORD v9x_gdi_accel_stats(void FAR *output)
{
    BYTE FAR *destination = (BYTE FAR *)output;
    const BYTE FAR *source = (const BYTE FAR *)&v9x_gdi;
    WORD index;

    if (output == 0) {
        return 0u;
    }
    v9x_gdi.fault_inject = v9x_gdi_fault_inject;
    v9x_gdi.poisoned = v9x_gdi_poisoned != 0u ? 1ul : 0ul;
    for (index = 0u; index < sizeof(V9X_GDI_STATS); ++index) {
        destination[index] = source[index];
    }
    return 1u;
}

WORD v9x_gdi_accel_fault_inject(DWORD count)
{
    v9x_gdi_fault_inject = count;
    v9x_gdi.fault_inject = count;
    return 1u;
}

/*
 * Ordinal 1: the GDI BitBlt dispatcher.
 *
 * The argument list is transcribed from 98DDK\src\display\mini\xga\BITBLT.ASM:
 * 57-67, which declares it in first-party code with the arguments named in
 * push order. Do not reorder or retype anything here.
 */
WORD __loadds FAR PASCAL BitBlt(V9X_DIB_ENGINE FAR *destination_device,
                                WORD destination_x,
                                WORD destination_y,
                                V9X_DIB_ENGINE FAR *source_device,
                                WORD source_x,
                                WORD source_y,
                                WORD x_extent,
                                WORD y_extent,
                                DWORD rop,
                                V9X_DIB_BRUSH FAR *brush,
                                LPVOID draw_mode)
{
    V9X_GDI_OP op;
    DWORD all_ones;
    WORD flags;
    WORD rop256;
    WORD screen_to_screen;
    WORD issued;

    ++v9x_gdi.calls;
    /* Deferred from a bounded wait that expired at interrupt time, where
     * writing a file or a serial byte was not allowed. */
    if (v9x_gdi_report_pending != 0u) {
        v9x_gdi_accel_flush_report();
    }

    /*
     * Gate 1: is anything on at all?
     *
     * This is the whole cost of this file to ati, vbe and matrox-m2, whose
     * chips declare no engine and therefore never leave v9x_gdi.enabled
     * non-zero. It is also where a poisoned session declines for ever, since
     * the latch clears the mask.
     */
    if (v9x_gdi.enabled == 0ul || v9x_gdi_engine_live == 0u) {
        ++v9x_gdi.decline_disabled;
        goto decline;
    }
    if (v9x_gdi_poisoned != 0u) {
        ++v9x_gdi.decline_poisoned;
        goto decline;
    }
    /* Gate 2: the destination must be this driver's live screen surface. */
    if (destination_device == 0 ||
        destination_device != v9x_driver_pdevice ||
        v9x_driver_pdevice == 0) {
        ++v9x_gdi.decline_not_screen;
        goto decline;
    }

    /*
     * Gate 3: the DIB Engine's own flags, in the reference driver's order -
     * VRAM, then BUSY, then PALETTE_XLAT (xga\BITBLT.ASM:66-77).
     */
    flags = destination_device->deFlags;
    if ((flags & V9X_DE_VRAM) == 0u) {
        ++v9x_gdi.decline_not_screen;
        goto decline;
    }
    if ((flags & V9X_DE_BUSY) != 0u) {
        ++v9x_gdi.decline_busy;
        goto decline;
    }
    screen_to_screen = source_device == destination_device ? 1u : 0u;
    /*
     * PALETTE_XLAT means the blit needs a background palette translation the
     * engine cannot do. Copy the rule exactly, not approximately: xga's
     * BB_JumpToDibEngineX (:44-50) re-checks lpSrcDev == lpDestDev and still
     * accelerates a screen-to-screen blit, because a copy that never leaves
     * VRAM moves pixel values untranslated. Fills arrive with lpSrcDev not
     * equal to the screen PDEVICE and so decline.
     *
     * The distinction is load bearing from build 002 onward: an 8-bpp desktop
     * with an active palette translate is precisely the desktop whose window
     * scrolls and moves the copy primitive exists to accelerate, and a blanket
     * decline would silently turn the feature off exactly there.
     *
     * The s3v sample is stricter - S3BLT.ASM:130 declines on PALETTE_XLAT with
     * no exemption at all. xga's rule is the one taken here because it is the
     * one that keeps 002 useful, and it is sound for the same reason s3v's is
     * safe: neither ever translates.
     */
    if ((flags & V9X_DE_PALETTE_XLAT) != 0u && screen_to_screen == 0u) {
        ++v9x_gdi.decline_palette_xlat;
        goto decline;
    }

    /* Gate 4: depth. 8 and 16 bpp only, as on the 32-bit side - no 24- or
     * 32-bpp S3 blit has ever been run on this hardware. */
    if (destination_device->deBitsPixel == 8u) {
        op.bytes_per_pixel = 1u;
        all_ones = 0x000000fful;
    } else if (destination_device->deBitsPixel == 16u) {
        op.bytes_per_pixel = 2u;
        all_ones = 0x0000fffful;
    } else {
        ++v9x_gdi.decline_depth;
        goto decline;
    }

    /*
     * Gate 5: the operation.
     *
     * The ROP256 code is the high word of the Rop DWORD, which is what the
     * reference driver reads (`mov di,wptr ss:[Rop+2]`, S3BLT.ASM:167) and
     * what the ViRGE command word's bits 24:17 take unchanged. The four codes
     * below are the ones s3v's own BltTypeTable classifies as needing no
     * source operand (0x00 and 0xff), a pattern only (0xf0), or a source only
     * (0xcc).
     *
     * BLACKNESS and WHITENESS are normalised into a solid fill here rather
     * than passed to the engine as ROPs, so that one definition of the fill
     * colour serves both chips and the two cannot disagree about what
     * whiteness means. That definition is the all-ones bit pattern, which is
     * what both DDK reference blitters produce; GDI documents the two in terms
     * of physical palette indices 0 and 1 instead. On a palettized desktop
     * whose realized palette does not put white at 0xff those are different
     * answers, and the /accel comparison against a reference DC is what
     * settles it - which is why it must land before build 001 turns fill on.
     */
    rop256 = (WORD)((rop >> 16) & 0x00fful);
    op.color = 0ul;
    if (rop256 == V9X_ROP256_BLACKNESS) {
        op.color = 0ul;
    } else if (rop256 == V9X_ROP256_WHITENESS) {
        op.color = all_ones;
    } else if (rop256 == V9X_ROP256_PATCOPY) {
        if (brush == 0 ||
            (brush->BrushFlags & V9X_BRUSH_COLORSOLID) == 0u) {
            ++v9x_gdi.decline_rop;
            goto decline;
        }
        op.color = brush->FgColor;
    } else if (rop256 != V9X_ROP256_SRCCOPY) {
        ++v9x_gdi.decline_rop;
        goto decline;
    }
    if (rop256 == V9X_ROP256_SRCCOPY) {
        if ((v9x_gdi.enabled & V9X_GDI_PRIM_COPY) == 0ul ||
            screen_to_screen == 0u) {
            ++v9x_gdi.decline_rop;
            goto decline;
        }
    } else if ((v9x_gdi.enabled & V9X_GDI_PRIM_FILL) == 0ul) {
        ++v9x_gdi.decline_rop;
        goto decline;
    }

    /* Gate 6: geometry. In bounds, non-empty, and inside the engine's
     * 11-bit coordinate space. */
    op.width = x_extent;
    op.height = y_extent;
    op.destination_x = destination_x;
    op.destination_y = destination_y;
    op.source_x = source_x;
    op.source_y = source_y;
    op.pitch = (DWORD)destination_device->deWidthBytes;
    op.base = destination_device->deBitsOffset;
    if (op.width == 0u || op.height == 0u || op.pitch == 0ul ||
        (DWORD)destination_x + (DWORD)op.width >
            (DWORD)destination_device->deWidth ||
        (DWORD)destination_y + (DWORD)op.height >
            (DWORD)destination_device->deHeight ||
        (DWORD)destination_x + (DWORD)op.width > V9X_VIRGE_COORD_MAX ||
        (DWORD)destination_y + (DWORD)op.height > V9X_VIRGE_COORD_MAX) {
        ++v9x_gdi.decline_geometry;
        goto decline;
    }
    if (rop256 == V9X_ROP256_SRCCOPY) {
        if ((DWORD)source_x + (DWORD)op.width >
                (DWORD)destination_device->deWidth ||
            (DWORD)source_y + (DWORD)op.height >
                (DWORD)destination_device->deHeight) {
            ++v9x_gdi.decline_geometry;
            goto decline;
        }
        /*
         * Overlap declines until build 003 turns it on. Two rectangles on one
         * surface overlap when they intersect on both axes; anything else is
         * a plain forward copy.
         */
        if ((v9x_gdi.enabled & V9X_GDI_PRIM_OVERLAP) == 0ul) {
            WORD left = destination_x < source_x ? source_x : destination_x;
            WORD top = destination_y < source_y ? source_y : destination_y;
            WORD right = (WORD)(destination_x < source_x
                                    ? destination_x + op.width
                                    : source_x + op.width);
            WORD bottom = (WORD)(destination_y < source_y
                                     ? destination_y + op.height
                                     : source_y + op.height);

            if (left < right && top < bottom) {
                ++v9x_gdi.decline_geometry;
                goto decline;
            }
        }
    }

    /* Gate 7: is the rectangle big enough to be worth it? */
    if ((DWORD)op.width * (DWORD)op.height < v9x_gdi.threshold) {
        ++v9x_gdi.decline_threshold;
        goto decline;
    }

    /*
     * Gate 8: chip-specific surface constraints. The ViRGE masks strides to
     * 0xff8 and surface bases to an 8-byte boundary; the Trio64 has no
     * per-surface base or stride at all and can only express a rectangle whose
     * surface starts on a display-pitch scan line.
     */
    if (v9x_gdi_engine_type == V9X_DD_ENGINE_TYPE_S3_VIRGE_DX) {
        if ((op.pitch & ~V9X_VIRGE_STRIDE_MASK) != 0ul ||
            (op.base & 7ul) != 0ul) {
            ++v9x_gdi.decline_engine;
            goto decline;
        }
    } else if (v9x_gdi_engine_type == V9X_DD_ENGINE_TYPE_S3_TRIO64) {
        if ((op.base % op.pitch) != 0ul) {
            ++v9x_gdi.decline_engine;
            goto decline;
        }
    } else {
        ++v9x_gdi.decline_engine;
        goto decline;
    }

    /*
     * Every gate has passed, so from here the operation is ours.
     *
     * The cursor exclusion is issued before the primitive and not around it:
     * the engine command is asynchronous, so there is no "after" in which to
     * end an exclusion. Lifting the software cursor first and letting DIBENG
     * redraw it on its next check is what the reference blitter does
     * (S3BLT.ASM:1778).
     *
     * BUSY is set across the register programming for the same reason the
     * reference sets it (S3BLT.ASM:132): it keeps the DIB Engine out of a
     * surface whose registers are half written. It is not what protects the
     * pending engine work after we return - that is v9x_gdi_engine_dirty and
     * the BeginAccess drain.
     */
    v9x_gdi.last_rop256 = (DWORD)rop256;
    v9x_gdi.last_color = op.color;
    v9x_gdi.last_brush_flags = brush != 0 ? (DWORD)brush->BrushFlags : 0ul;
    v9x_gdi.last_bpp = (DWORD)destination_device->deBitsPixel;
    v9x_gdi_exclude_cursor(destination_device, &op);
    destination_device->deFlags |= V9X_DE_BUSY;
    if (v9x_gdi_engine_type == V9X_DD_ENGINE_TYPE_S3_VIRGE_DX) {
        issued = rop256 == V9X_ROP256_SRCCOPY ? v9x_gdi_virge_copy(&op)
                                              : v9x_gdi_virge_fill(&op);
    } else {
        issued = rop256 == V9X_ROP256_SRCCOPY ? v9x_gdi_trio_copy(&op)
                                              : v9x_gdi_trio_fill(&op);
    }
    destination_device->deFlags &= (WORD)~V9X_DE_BUSY;
    if (issued == 0u) {
        /* A bounded wait expired and poisoned the session. The operation has
         * not been performed, so the DIB Engine still has to perform it -
         * which is what keeps the pixels correct through a timeout. */
        goto decline;
    }
    v9x_gdi_engine_dirty = 1u;
    if (rop256 == V9X_ROP256_SRCCOPY) {
        ++v9x_gdi.copies;
    } else {
        ++v9x_gdi.fills;
    }
    return 1u;

decline:
    ++v9x_gdi.declines;
    return V9xDibBitBltCall(destination_device, destination_x, destination_y,
                            source_device, source_x, source_y, x_extent,
                            y_extent, rop, brush, draw_mode);
}
