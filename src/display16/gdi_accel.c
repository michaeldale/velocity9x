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

#include "velocity9x/diagpaths.h"
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
extern void FAR PASCAL V9xEngineImageRow(WORD source_selector,
                                         WORD source_offset, WORD bytes);
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

#define V9X_HARDWARE_INFO_PATH  V9X_DIAG_HW_INI
#define V9X_SYSTEM_INI          "SYSTEM.INI"
#define V9X_INI_SECTION         "Velocity9x"

/*
 * Compile-time primitive defaults, advanced per rollout build.
 *
 * Build 000 was all zeroes on purpose: every primitive compiled and none of
 * them reachable, so its exit gate could ask whether adding this file changed
 * what the driver does rather than whether the new code was correct.
 *
 * Build 001 turns fill on. It is on because it was measured, not because it
 * was written: the randomized comparison against a DIB Engine reference passes
 * with the engine executing the fills, and it did not pass on the first
 * attempt - the colour came from the wrong field of the realized brush, which
 * is recorded on V9X_DIB_BRUSH_SOLID and was worth an issue of its own.
 *
 * Build 002 turns copy on, non-overlapping only. Build 003 turns overlap on,
 * which is the last of the three: an overlapping same-surface copy is correct
 * only if the engine walks from the corner that keeps it correct, and the
 * direction logic below is what decides that.
 */
/*
 * Master switch. Forced to 0 on 2026-08-27 when the fill path corrupted real
 * Trio64 silicon; back to 1 later the same day with the divergence understood
 * and fixed. The cause was never in the fills: ADVFUNC_CNTL (4AE8H) bit 0 is
 * cleared by DOS-box/VDD activity on real silicon, after which the engine
 * executes commands and discards every write - and 86Box does not model that
 * gate, which is why 11/11 emulated modes passed while hardware failed.
 * v9x_gdi_trio_prepare() now re-asserts the bit per operation; verified on
 * BARRY at 640x480x16 and 800x600x16 with /probe exact (3072/3072) and
 * /accel PASS immediately after deliberate DOS-box poisoning
 * (docs/issues/2026-08-27-gdi-accel-corrupts-display-on-physical-trio64.md).
 */
#define V9X_GDI_DEFAULT_MASTER   1
#define V9X_GDI_DEFAULT_FILL     1
#define V9X_GDI_DEFAULT_COPY     1
#define V9X_GDI_DEFAULT_OVERLAP  1
/* Build 004: monochrome CPU-source expansion, compiled and OFF, which is what
 * the rollout table specifies for this build. */
#define V9X_GDI_DEFAULT_UPLOAD   0

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
 * Bounded waits, in iterations. The same numbers the 32-bit HAL uses, and that
 * is not laziness - it is the correction of a derivation that was wrong.
 *
 * These started 64 times shorter, scaled down from the HAL's on the grounds
 * that this driver is built without a -3, so one iteration here is a far call
 * into V9xEngineRead plus a DWORD countdown - 60 to 100 clocks against the flat
 * HAL's ten or so.
 *
 * That argument scaled the wrong quantity. What an iteration costs is not its
 * instructions, it is its bus access: on the Trio64 every spin is an `in ax,dx`
 * on a 9AE8h port, which is ISA-timed at roughly a microsecond and costs
 * exactly the same in 16-bit and in 32-bit code, and on the ViRGE it is an
 * uncached MMIO dword read. The loop overhead is noise beside either. So the
 * two bitnesses' iterations cost about the same, and their limits should be the
 * same order - not a 64th of each other.
 *
 * Measured, which is how the error was found: at 0x00010000 the Trio64 timed
 * out on real uninjected work in exactly its three largest modes -
 * 1024x768x16, 1280x1024x8 and 1280x1024x16 - and poisoned the session, while
 * every mode of 960 KB or less was fine. The ViRGE, whose MMIO read is cheaper
 * than a port cycle, passed all eleven. That is a limit too short for the
 * hardware, not hardware too slow for the limit.
 *
 * The cost of being generous is bounded and paid at most once per session,
 * because an expiry latches the poison: a genuinely hung engine stalls for a
 * few seconds and then acceleration is off for good, which is the trade the
 * 32-bit side already accepts on the same registers.
 *
 * This closes open item 3 of docs\plans\gdi-acceleration.md.
 */
#define V9X_GDI_FIFO_SPIN_LIMIT   0x00200000ul
#define V9X_GDI_IDLE_SPIN_LIMIT   0x00400000ul

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
 * Read one S3 CRTC register. Index then data, the same two ports the engine
 * reset below already uses, so this adds no new hardware surface.
 */
static BYTE v9x_gdi_crtc_read(BYTE index)
{
    v9x_gdi_port_out(V9X_CRTC_INDEX, index);
    return v9x_gdi_port_in(V9X_CRTC_DATA);
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
    {
        /* runtime.asm: the directory must exist before this write; on a boot
         * poisoned this early it may not yet. */
        extern void FAR PASCAL V9xEnsureDiagDir(void);
        V9xEnsureDiagDir();
    }
    WritePrivateProfileString("Velocity9xHardware", "GdiAcceleration",
                              "gdi-poisoned", V9X_DIAG_HW_INI);
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
    WORD rop256;             /* the GDI Rop's high byte, unchanged         */
    /* Monochrome CPU source, build 004. Zero selector means "not an upload". */
    WORD source_selector;
    DWORD source_bits_offset;   /* 32-bit: deBits is an fword, not a word   */
    WORD source_stride;
    WORD source_bit_offset;  /* the source x within its first byte, 0..7    */
    WORD upload_bytes;       /* whole source bytes to feed per row          */
    DWORD foreground;
    DWORD background;
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
    /*
     * The ROP goes into the command word rather than being normalised into a
     * colour. That is what the reference driver does - DoBltNoDSP puts the
     * ROP256 code straight into CMD_SET bits 24:17 and writes no colour at all
     * for BLACKNESS and WHITENESS - and it is better than normalising for a
     * concrete reason: with an all-ones mono pattern the engine generates the
     * pixel values itself from the ROP, so BLACKNESS and WHITENESS need no
     * opinion about what black and white are. Only PATCOPY reads the colour.
     */
    command = ((DWORD)op->rop256 << V9X_VIRGE_CMD_ROP_SHIFT) |
              V9X_VIRGE_CMD_MONO_PATTERN |
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

    /*
     * Scan direction for an overlapping same-surface copy. Build 003's whole
     * correctness rests on these five lines, so the reasoning is here rather
     * than in a decision record nobody reads while editing this function.
     *
     * The rule: flip Y when the copy moves down; otherwise, and only if the
     * copy stays on the same rows, flip X when it moves right.
     *
     * Why the "otherwise" is safe, which is the part worth checking. Once rows
     * are walked from the far end, the source row for any destination row lies
     * in the direction not yet written - moving down and walking bottom-up, the
     * source row is above and still intact - and the two rows are *different
     * rows*, so within-row order cannot alias anything. The X test is therefore
     * needed only when the copy stays on one row, which is exactly when it is
     * applied. Enumerated: down-any-x flips Y only; up-any-x flips nothing;
     * same-row-right flips X; same-row-left flips nothing. All four are safe.
     *
     * The reference driver is more conservative and flips X whenever the
     * destination x exceeds the source x, independent of the Y flip
     * (ScreenToScreenBlt in 98DDK\src\display\mini\s3v\S3BLT.ASM compares
     * the packed (x,y) dword for its second test). That is also correct, and
     * the difference is not a disagreement about the hardware - it is one
     * driver testing a condition the other has already made irrelevant.
     */
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
 * Monochrome CPU-source expansion on the ViRGE (build 004).
 *
 * The CPU writes one bit per pixel into the image-transfer window and the
 * engine expands it to the destination depth, which is eight times less
 * CPU-to-bus traffic at 8 bpp and sixteen times less at 16 - the whole reason
 * this primitive is worth having when a colour upload is not
 * (docs\decisions6-08-27-gdi-accel-004-design.md).
 *
 * Two things here are counter-intuitive and both are copied from the reference
 * driver's MonoSourceBlt rather than reasoned out:
 *
 * 1. THE COLOURS ARE SWAPPED. The ViRGE treats a set bit as *background* and a
 *    clear bit as *foreground*, the opposite of every other convention, so the
 *    reference writes DRAWMODE.TextColor to SRC_BG and DRAWMODE.bkColor to
 *    SRC_FG to get the conventional result. Its own comment says so: "the
 *    monochrome foreground/background bit designations are reversed from the
 *    typical designation ... The foreground/background color settings are
 *    reversed to convert bits back to the expected designations." Getting this
 *    wrong produces a correctly shaped blit in inverted colours - the same
 *    class of defect as the build 001 fill colour, and equally invisible to
 *    reasoning.
 *
 * 2. THE CLIP RECTANGLE IS LOAD BEARING. A mono source starts at an arbitrary
 *    bit within a byte, and the engine can only be fed whole bytes. So the
 *    transfer starts at the byte containing the first source pixel, the
 *    destination x is moved LEFT by that bit offset to line the pixels up, and
 *    the leading padding pixels then fall outside the true destination
 *    rectangle - where hardware clipping discards them. Without the clip they
 *    would be drawn.
 */
static WORD v9x_gdi_virge_upload(const V9X_GDI_OP *op)
{
    DWORD command;
    DWORD row;
    DWORD source_offset = op->source_bits_offset;
    /* Destination shifted left to absorb the source's bit offset, and the true
     * rectangle kept for the clip. Both are last-pixel-inclusive, as the
     * engine's clip registers expect. */
    WORD shifted_x = (WORD)(op->destination_x - op->source_bit_offset);

    if (v9x_gdi_wait_fifo(V9X_GDI_FIFO_SLOTS) == 0u) {
        return 0u;
    }
    command = ((DWORD)op->rop256 << V9X_VIRGE_CMD_ROP_SHIFT) |
              V9X_VIRGE_CMD_SRC_SYS | V9X_VIRGE_CMD_SRC_MONO |
              V9X_VIRGE_CMD_CPU_ALIGN_DWORD | V9X_VIRGE_CMD_CLIP_ENABLE |
              V9X_VIRGE_CMD_X_POSITIVE | V9X_VIRGE_CMD_Y_POSITIVE |
              V9X_VIRGE_CMD_DRAW_ENABLE |
              ((DWORD)(op->bytes_per_pixel - 1u) << 2);

    v9x_gdi_write(V9X_VIRGE_DEST_BASE, op->base);
    v9x_gdi_write(V9X_VIRGE_DEST_SRC_STRIDE, op->pitch << 16);
    v9x_gdi_write(V9X_VIRGE_CLIP_L_R,
                  ((DWORD)op->destination_x << 16) |
                  (DWORD)(op->destination_x + op->width - 1u));
    v9x_gdi_write(V9X_VIRGE_CLIP_T_B,
                  ((DWORD)op->destination_y << 16) |
                  (DWORD)(op->destination_y + op->height - 1u));
    /* Swapped, deliberately - see note 1 above. */
    v9x_gdi_write(V9X_VIRGE_SRC_BG_COLOR, op->background);
    v9x_gdi_write(V9X_VIRGE_SRC_FG_COLOR, op->foreground);
    v9x_gdi_write(V9X_VIRGE_RECT_WH,
                  (((DWORD)(op->upload_bytes * 8u) - 1ul) << 16) |
                  (DWORD)op->height);
    v9x_gdi_write(V9X_VIRGE_RECT_DEST_XY,
                  ((DWORD)shifted_x << 16) | (DWORD)op->destination_y);
    v9x_gdi_write(V9X_VIRGE_COMMAND, command);

    /*
     * Feed the rows. The window is a port rather than memory, so every row
     * restarts at offset 0, and there is no FIFO polling in this loop - the
     * engine throttles the bus itself and a pacing loop would be a slower way
     * to do nothing (the reference's ColorSourceBlt does the same).
     */
    for (row = 0ul; row < (DWORD)op->height; ++row) {
        /* The cast is safe by the gate's 64 KiB bound and is written out so
         * the narrowing is deliberate rather than incidental. */
        V9xEngineImageRow(op->source_selector, (WORD)source_offset,
                          op->upload_bytes);
        source_offset += (DWORD)op->source_stride;
    }
    return 1u;
}

/*
 * Establish the engine's latched global state.
 *
 * Everything here is write-only and undefined or inherited, and the databook
 * lists it as the setup every drawing operation depends on (DB014-B 13.4.2).
 * Before this existed the driver programmed only the per-operation registers -
 * colour, mix, coordinates, extent, command - and inherited clipping, colour
 * compare, the write mask and the destination base from whatever had used the
 * engine previously. In emulation that inheritance is a zeroed struct and
 * everything works; on real Trio64 silicon it is whatever the BIOS, the VDD or
 * a DirectDraw session left, and the fills went nowhere.
 *
 * Written before the per-operation registers, and written on every operation
 * rather than once at Enable: the 32-bit HAL drives the same engine through the
 * same registers with no lock shared with this side, so state established once
 * here could be changed underneath by a DirectDraw blit between two GDI calls.
 */
static void v9x_gdi_trio_prepare(void)
{
    /*
     * The one that was the actual hardware defect: ADVFUNC_CNTL bit 0. DOS-box
     * activity clears it on real silicon (0x008B measured dropping to 0x008A),
     * after which the engine executes every command and writes nothing - the
     * exact "busy toggles, pixels vanish" signature this file's fills showed
     * on BARRY. The register is Read/Write, so assert the bit and preserve
     * the rest; count the restores so a field failure can show whether the
     * environment is flipping it.
     */
    WORD advfunc = v9x_gdi_port_in_word(V9X_TRIO_ADVFUNC_CNTL);

    v9x_gdi.last_advfunc = (DWORD)advfunc;
    if ((advfunc & V9X_TRIO_ADVFUNC_ENABLE) == 0u) {
        v9x_gdi_port_out_word(V9X_TRIO_ADVFUNC_CNTL,
                              (WORD)(advfunc | V9X_TRIO_ADVFUNC_ENABLE));
        ++v9x_gdi.advfunc_restores;
    }
    /* Both bases in the first MByte of display memory, no external clipping,
     * no colour compare. */
    v9x_gdi_port_out_word(V9X_TRIO_MULTIFUNC_CNTL, V9X_TRIO_MULT_MISC2);
    v9x_gdi_port_out_word(V9X_TRIO_MULTIFUNC_CNTL, V9X_TRIO_MULT_MISC);
    /* All planes enabled for writing. */
    v9x_gdi_port_out_word(V9X_TRIO_WRT_MASK, V9X_TRIO_WRT_MASK_ALL);
    /*
     * The clip rectangle, opened to the engine's full 12-bit coordinate space.
     * The dispatcher's geometry gate has already bounded the operation to the
     * surface, so a second and narrower bound here would only be another thing
     * to get wrong - and with EXT CLIP now known to be clear, wide open means
     * wide open.
     */
    v9x_gdi_port_out_word(V9X_TRIO_MULTIFUNC_CNTL, V9X_TRIO_SCISSORS_T);
    v9x_gdi_port_out_word(V9X_TRIO_MULTIFUNC_CNTL, V9X_TRIO_SCISSORS_L);
    v9x_gdi_port_out_word(V9X_TRIO_MULTIFUNC_CNTL,
                          (WORD)(V9X_TRIO_SCISSORS_B | V9X_TRIO_SCISSORS_MAX));
    v9x_gdi_port_out_word(V9X_TRIO_MULTIFUNC_CNTL,
                          (WORD)(V9X_TRIO_SCISSORS_R | V9X_TRIO_SCISSORS_MAX));
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
    v9x_gdi_trio_prepare();
    v9x_gdi_port_out_word(V9X_TRIO_FRGD_MIX, V9X_TRIO_FRGD_MIX_NEW);
    v9x_gdi_port_out_word(V9X_TRIO_FRGD_COLOR, (WORD)op->color);
    v9x_gdi_port_out_word(V9X_TRIO_MULTIFUNC_CNTL,
                          V9X_TRIO_PIXEL_CNTL_FRGD_MIX);
    v9x_gdi_port_out_word(V9X_TRIO_CUR_X, op->destination_x);
    v9x_gdi_port_out_word(V9X_TRIO_CUR_Y, (WORD)y);
    v9x_gdi_port_out_word(V9X_TRIO_MAJ_AXIS_PCNT, (WORD)(op->width - 1u));
    v9x_gdi_port_out_word(V9X_TRIO_MULTIFUNC_CNTL, (WORD)(op->height - 1u));
    v9x_gdi.last_status_entry =
        (DWORD)v9x_gdi_port_in_word(V9X_TRIO_CMD_STATUS);
    v9x_gdi_port_out_word(V9X_TRIO_CMD_STATUS, V9X_TRIO_CMD_RECT_SOLID);
    v9x_gdi.last_status_issued =
        (DWORD)v9x_gdi_port_in_word(V9X_TRIO_CMD_STATUS);
    /* Where the engine's memory origin is being taken from. See the note on
     * last_cr6a: a non-zero bank would put this fill outside the displayed
     * part of video memory. */
    v9x_gdi.last_cr50 = (DWORD)v9x_gdi_crtc_read(0x50u);
    v9x_gdi.last_cr6a = (DWORD)v9x_gdi_crtc_read(0x6au);
    v9x_gdi.last_cr51 = (DWORD)v9x_gdi_crtc_read(0x51u);
    v9x_gdi.last_cr31 = (DWORD)v9x_gdi_crtc_read(0x31u);
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
    /* The copy path skipped the latched-state setup; the ADVFUNC guard it now
     * carries applies to every enhanced-engine operation equally. */
    v9x_gdi_trio_prepare();
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
                                   const V9X_GDI_OP *op, WORD is_copy)
{
    V9X_CURSOR_EXCLUDE_PROC exclude =
        (V9X_CURSOR_EXCLUDE_PROC)device->deBeginAccess;
    WORD left = op->destination_x;
    WORD top = op->destination_y;
    WORD right = op->destination_x;
    WORD bottom = op->destination_y;

    if (exclude == 0) {
        return;
    }
    /*
     * A fill excludes its destination; a screen-to-screen copy has to exclude
     * the union of source and destination, and the reference driver keeps two
     * separate routines for exactly that distinction - B_SWCursorExcludeRect
     * for the pattern paths and B_SWCursorExcludeUnion for DSP_ScreenBlt
     * (S3BLT.ASM).
     *
     * The reason is specific to a copy: the engine READS the source. A software
     * cursor sitting over the source rectangle is part of the framebuffer, so
     * the engine would copy the cursor's pixels into the destination and leave
     * a second cursor painted on the screen. Excluding only the destination
     * lifts the cursor from where the pixels land and not from where they come
     * from.
     *
     * The union is the bounding box of the two equal-sized rectangles, which is
     * what B_SWCursorExcludeUnion computes: min of the origins, and max of the
     * origins plus the extent, less one for the last pixel drawn.
     *
     * Note the harness cannot catch this. /accel parks the pointer in the far
     * corner so the readback is not polluted by it, which by construction makes
     * it blind to every cursor interaction. The window-drag soak is what covers
     * this, and it is the reason build 002's exit gate asks for one.
     */
    if (is_copy != 0u) {
        left = op->destination_x < op->source_x ? op->destination_x
                                               : op->source_x;
        top = op->destination_y < op->source_y ? op->destination_y
                                              : op->source_y;
        right = op->destination_x > op->source_x ? op->destination_x
                                                : op->source_x;
        bottom = op->destination_y > op->source_y ? op->destination_y
                                                  : op->source_y;
    }
    (void)exclude((void FAR *)device, left, top,
                  (WORD)(right + op->width - 1u),
                  (WORD)(bottom + op->height - 1u),
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
                         V9X_GDI_PRIM_OVERLAP | V9X_GDI_PRIM_UPLOAD;
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
        if (GetPrivateProfileInt(V9X_INI_SECTION, "GdiAccelUpload",
                                 V9X_GDI_DEFAULT_UPLOAD,
                                 V9X_SYSTEM_INI) != 0) {
            enabled |= V9X_GDI_PRIM_UPLOAD;
        }
    }
    /* The chip is the authority on capability, and an overlap-capable copy is
     * still a copy: overlap without copy would enable nothing. */
    if ((v9x_gdi_engine_caps & V9X_DD_ENGINE_CAP_SOLID_FILL) == 0ul) {
        enabled &= ~V9X_GDI_PRIM_FILL;
    }
    if ((v9x_gdi_engine_caps & V9X_DD_ENGINE_CAP_SCREEN_COPY) == 0ul) {
        enabled &= ~(V9X_GDI_PRIM_COPY | V9X_GDI_PRIM_OVERLAP |
                     V9X_GDI_PRIM_UPLOAD);
    }
    /* Only the ViRGE has an implemented upload path; see gate 8. */
    if (v9x_gdi_engine_type != V9X_DD_ENGINE_TYPE_S3_VIRGE_DX) {
        enabled &= ~V9X_GDI_PRIM_UPLOAD;
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
    if ((v9x_gdi.enabled & V9X_GDI_PRIM_UPLOAD) != 0ul) {
        return "gdi-fill-copy-overlap-upload";
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
    WORD upload = 0u;
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
     * BLACKNESS and WHITENESS reach the engine as ROPs wherever the hardware
     * has a ROP field, which is what the reference driver does and which means
     * neither chip needs an opinion about what black and white are: with an
     * all-ones mono pattern the engine generates the pixel values from the ROP
     * alone. The colour below is therefore ignored by the ViRGE for those two.
     *
     * The Trio64's 8514/A command set has no ROP field - FRGD_MIX selects a
     * mix, not a ternary operation - so on that chip the two are expressed as a
     * solid fill of 0 or of all ones, and the colour is how. All ones is the
     * right value there for the same reason it is what a ROP of 0xff produces.
     */
    rop256 = (WORD)((rop >> 16) & 0x00fful);
    op.color = 0ul;
    if (rop256 == V9X_ROP256_BLACKNESS) {
        op.color = 0ul;
    } else if (rop256 == V9X_ROP256_WHITENESS) {
        op.color = all_ones;
    } else if (rop256 == V9X_ROP256_PATCOPY) {
        /*
         * Style first, then the flag, then the colour - the order the
         * reference driver's PatternBlt uses. BS_HOLLOW draws nothing and the
         * other two carry a pattern this driver does not read, so only a
         * BS_SOLID brush with COLORSOLID set is a solid fill.
         *
         * The colour comes from the realized pattern, not from FgColor. See the
         * note on V9X_DIB_BRUSH_SOLID: FgColor holds the logical COLORREF, and
         * handing that to the engine painted every red-255 colour white.
         */
        if (brush == 0 ||
            brush->BrushStyle != V9X_BRUSH_STYLE_SOLID ||
            (brush->BrushFlags & V9X_BRUSH_COLORSOLID) == 0u) {
            ++v9x_gdi.decline_rop;
            goto decline;
        }
        op.color = ((const V9X_DIB_BRUSH_SOLID FAR *)brush)->Bits;
    } else if (rop256 != V9X_ROP256_SRCCOPY) {
        ++v9x_gdi.decline_rop;
        goto decline;
    }
    if (rop256 == V9X_ROP256_SRCCOPY && screen_to_screen == 0u) {
        /*
         * A memory source. Build 004: only a monochrome one is worth taking,
         * because a colour upload has the CPU move exactly the bytes the DIB
         * Engine would move anyway - see the design record. So a 1-bpp source
         * becomes an engine expansion and everything else declines here, which
         * the harness checks rather than assumes.
         */
        if ((v9x_gdi.enabled & V9X_GDI_PRIM_UPLOAD) == 0ul) {
            v9x_gdi.upload_reject_mask |= 1ul << 1;
            ++v9x_gdi.decline_upload;
            goto decline;
        }
        if (source_device == 0) {
            v9x_gdi.upload_reject_mask |= 1ul << 2;
            ++v9x_gdi.decline_upload;
            goto decline;
        }
        /*
         * deType decides which struct this is, and must be read before any
         * field past byte 10. A DIBENGINE has deFlags and can be VRAM; a plain
         * BITMAP has neither the field nor the possibility, so the VRAM test
         * only applies to the former - reading deFlags out of a BITMAP is a
         * read past the end of the object.
         */
        if (source_device->deType == V9X_TYPE_DIBENG) {
            if ((source_device->deFlags & V9X_DE_VRAM) != 0u) {
                v9x_gdi.upload_reject_mask |= 1ul << 3;
                ++v9x_gdi.decline_upload;
                goto decline;
            }
        } else if (source_device->deType != 0u) {
            v9x_gdi.upload_reject_mask |= 1ul << 10;
            v9x_gdi.upload_reject_detail = (DWORD)source_device->deType;
            ++v9x_gdi.decline_upload;
            goto decline;
        }
        if (source_device->deBitsPixel != 1u) {
            v9x_gdi.upload_reject_mask |= 1ul << 4;
            ++v9x_gdi.decline_upload;
            goto decline;
        }
        if (draw_mode == 0) {
            v9x_gdi.upload_reject_mask |= 1ul << 5;
            ++v9x_gdi.decline_upload;
            goto decline;
        }
        upload = 1u;
    } else if (rop256 == V9X_ROP256_SRCCOPY) {
        if ((v9x_gdi.enabled & V9X_GDI_PRIM_COPY) == 0ul) {
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
    v9x_gdi.last_base = op.base;
    v9x_gdi.last_pitch = op.pitch;
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
    if (upload != 0u) {
        /*
         * Mono source addressing. The transfer must begin at the byte holding
         * the first source pixel, so the row start is floor(source_x / 8) and
         * the leftover bits are handed to the primitive to absorb by shifting
         * the destination. The byte count covers the bit offset as well as the
         * width, rounded up.
         */
        DWORD first_byte = (DWORD)source_x >> 3;
        DWORD span_bits = (DWORD)(source_x & 7u) + (DWORD)op.width;
        const V9X_DRAWMODE FAR *mode = (const V9X_DRAWMODE FAR *)draw_mode;
        /*
         * Where the bits fword lives depends on which struct this is: offset 18
         * in a DIBENGINE, offset 10 in a plain BITMAP. Same six bytes, same
         * meaning, different place - and getting it wrong is what made the
         * first enabled run compute a half-gigabyte source address.
         */
        DWORD bits_offset;
        WORD bits_selector;
        DWORD start;

        if (source_device->deType == V9X_TYPE_DIBENG) {
            bits_offset = source_device->deBitsOffset;
            bits_selector = source_device->deBitsSelector;
        } else {
            const V9X_BITMAP16 FAR *bitmap =
                (const V9X_BITMAP16 FAR *)source_device;

            bits_offset = (DWORD)bitmap->bmBitsOffset;
            bits_selector = bitmap->bmBitsSelector;
        }
        start = bits_offset +
                (DWORD)source_y * (DWORD)source_device->deWidthBytes +
                first_byte;

        op.source_bit_offset = (WORD)(source_x & 7u);
        op.upload_bytes = (WORD)((span_bits + 7ul) >> 3);
        op.source_stride = source_device->deWidthBytes;
        op.source_selector = bits_selector;
        /*
         * Swapped on purpose: the ViRGE reads a set bit as background. See the
         * note on v9x_gdi_virge_upload.
         */
        op.foreground = mode->bkColor;
        op.background = mode->TextColor;
        v9x_gdi.last_color = op.foreground;
        /*
         * Everything must sit inside one selector. A Win16 memory bitmap larger
         * than 64 KiB needs selector stepping to walk, and inventing that here
         * for a first cut is how a display driver reads the wrong memory - so a
         * source whose last row would run past the selector declines instead.
         * Also refuse a destination the bit-offset shift would push negative.
         */
        if (source_device->deWidthBytes == 0u || op.upload_bytes == 0u) {
            v9x_gdi.upload_reject_mask |= 1ul << 6;
            v9x_gdi.upload_reject_detail =
                ((DWORD)source_device->deWidthBytes << 16) |
                (DWORD)op.upload_bytes;
            ++v9x_gdi.decline_geometry;
            goto decline;
        }
        if (op.source_bit_offset > destination_x) {
            v9x_gdi.upload_reject_mask |= 1ul << 7;
            v9x_gdi.upload_reject_detail =
                ((DWORD)op.source_bit_offset << 16) | (DWORD)destination_x;
            ++v9x_gdi.decline_geometry;
            goto decline;
        }
        /*
         * The transfer helper addresses the source with a 16-bit si, which
         * costs nothing for a plain BITMAP - bmBits is a 16:16 pointer, so its
         * offset is 16 bits by definition. A DIBENGINE source is the case worth
         * guarding: deBits is an fword (DIBENG.INC:64) and its offset really is
         * 32 bits, so one whose last row would pass 64 KiB declines here rather
         * than being silently truncated into the wrong memory.
         */
        if (start + (DWORD)op.height * (DWORD)source_device->deWidthBytes >
                0x00010000ul) {
            v9x_gdi.upload_reject_mask |= 1ul << 8;
            v9x_gdi.upload_reject_detail = start;
            ++v9x_gdi.decline_geometry;
            goto decline;
        }
        op.source_bits_offset = start;
        if ((DWORD)source_y + (DWORD)op.height >
                (DWORD)source_device->deHeight ||
            (DWORD)source_x + (DWORD)op.width >
                (DWORD)source_device->deWidth) {
            v9x_gdi.upload_reject_mask |= 1ul << 9;
            v9x_gdi.upload_reject_detail =
                ((DWORD)source_device->deWidth << 16) |
                (DWORD)source_device->deHeight;
            ++v9x_gdi.decline_geometry;
            goto decline;
        }
        v9x_gdi.upload_reject_mask |= 1ul;
        v9x_gdi.upload_reject_detail = start;
    } else if (rop256 == V9X_ROP256_SRCCOPY) {
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
                ++v9x_gdi.decline_overlap;
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
        /*
         * No monochrome upload on the Trio64, and this is a deliberate gap
         * rather than an oversight. Its 8514/A CPU-data path needs PIX_TRANS,
         * the background mix and pixel-control registers, and this project has
         * no first-party source for those values - the DDK's S3 sample is
         * ViRGE, its other samples are other vendors' chips, and the 32-bit HAL
         * has never driven this path either. Writing invented values to a 2D
         * engine's command register on real silicon is not a guess worth
         * making; build 001's fill colour showed how far wrong a plausible
         * assumption about these engines can be. Declines until somebody has
         * the databook section.
         */
        if (upload != 0u || (op.base % op.pitch) != 0ul) {
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
    op.rop256 = rop256;
    v9x_gdi.last_rop256 = (DWORD)rop256;
    v9x_gdi.last_color = op.color;
    v9x_gdi.last_brush_flags = brush != 0 ? (DWORD)brush->BrushFlags : 0ul;
    v9x_gdi.last_brush_bpp = brush != 0 ? (DWORD)brush->BrushBpp : 0ul;
    v9x_gdi.last_brush_style = brush != 0 ? (DWORD)brush->BrushStyle : 0ul;
    v9x_gdi.last_bpp = (DWORD)destination_device->deBitsPixel;
    /*
     * Destination-only for an upload: the engine reads the source from system
     * memory, not from the framebuffer, so there is no source rectangle on
     * screen for a cursor to be sitting over. The union form is a
     * screen-to-screen concern.
     */
    v9x_gdi_exclude_cursor(destination_device, &op,
                           (rop256 == V9X_ROP256_SRCCOPY && upload == 0u)
                               ? 1u : 0u);
    destination_device->deFlags |= V9X_DE_BUSY;
    if (v9x_gdi_engine_type == V9X_DD_ENGINE_TYPE_S3_VIRGE_DX) {
        if (upload != 0u) {
            issued = v9x_gdi_virge_upload(&op);
        } else {
            issued = rop256 == V9X_ROP256_SRCCOPY ? v9x_gdi_virge_copy(&op)
                                                  : v9x_gdi_virge_fill(&op);
        }
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
    if (upload != 0u) {
        ++v9x_gdi.uploads;
    } else if (rop256 == V9X_ROP256_SRCCOPY) {
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
