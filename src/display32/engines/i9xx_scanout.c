/*
 * Scanout controls for the Intel Gen3, and the dispatch that chooses them.
 *
 * The S3 controls in vga_scanout.c write CR69 and read the VGA input-status
 * port. On a 945GSE driving an LVDS panel the display pipe is programmed
 * through MMIO and the VGA plane may not be part of the picture at all, so
 * both are replaced: the plane base register moves the scanout and the
 * pipe's display-line register says where the beam is.
 *
 * Every number here is from Linux i915_reg.h and the read-only captures, and
 * is UNMEASURED as a write until a boot with IntelFlip=1 says otherwise:
 *
 *  - The panel is on pipe B, fed by plane B (intel56, intel60: PIPEB_CONF
 *    bit 31 set, DSPBCNTR enabled with pipe select 1, pipe A and plane A
 *    zero). Both are read rather than assumed: the live pipe by its enable
 *    bit, the plane by its own enable and pipe-select bits, because a
 *    plane can drive the other pipe on this generation.
 *  - The plane base (DSPBADDR 0x71184) reads 0 with the desktop at
 *    framebuffer offset 0, so a framebuffer byte offset IS the graphics
 *    address the plane wants; i915's gen3 path writes DSPADDR with the
 *    object's GTT offset plus the linear offset and reads it back to post.
 *  - VTOTAL bits 11:0 are vactive-1 (intel56 pipe B: 0x023F, 576 lines),
 *    bits 27:16 vtotal-1 (0x029F, 672). DSL in [vactive, vtotal) is the
 *    vertical blank; i915's scanline-based vblank test is this comparison.
 *
 * Nothing here runs unless the 16-bit side stamped V9X_DD_ENGINE_CAP_FLIP,
 * which it does unless INTELARM.TXT carries IntelFlip=0 (on by default from
 * 2026-09-18; before that it needed IntelFlip=1, and the boots that earned
 * the default are intel61 through intel66). A boot with it off dispatches to
 * the VGA controls exactly as before.
 */
#include "ddhal_internal.h"
#include "velocity9x/intel_gma.h"

static volatile DWORD *v9x_i9xx_scanout_reg(DWORD offset)
{
    return (volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

/* Non-zero when the Intel controls own the scanout on this boot. */
static int v9x_i9xx_scanout_active(void)
{
    if (v9x_hal == 0 ||
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) == 0ul ||
        v9x_hal->engine.engine_type != V9X_DD_ENGINE_TYPE_INTEL_GEN3 ||
        v9x_hal->engine.control_linear_base == 0ul) {
        return 0;
    }
    return (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_FLIP) != 0ul;
}

/*
 * The one pipe that is on, and the one plane feeding it.
 *
 * A plane is not tied to its namesake pipe on Gen3: DSPCNTR bits 25:24
 * select the pipe a plane drives, so plane A can feed pipe B. Choosing the
 * plane by the pipe's letter would write an inactive plane's base and
 * report a flip that moved nothing. So the plane is chosen the way the
 * fingerprint decoder (i9xx_mmio.c) chooses it: enabled, and routed to the
 * live pipe by its own select bits. Exactly one pipe on and exactly one such
 * plane, or the configuration is declined - two of either is not a state
 * this driver has seen and not one it should guess at. intel56: PIPEB_CONF
 * 0x80000000, DSPBCNTR 0x95000000 (enabled, pipe select 1), DSPACNTR 0.
 *
 * Returns the register offsets through the pointers; zero declines.
 */
static DWORD v9x_i9xx_scanout_plane = 0ul;   /* the last resolved plane */
static DWORD v9x_i9xx_scanout_stride_reg = 0ul;
/* The last flip: which base register, and the offset it was asked for, so
 * a later read can say whether the hardware has taken it. */
static DWORD v9x_i9xx_scanout_last_base_reg = 0ul;
static DWORD v9x_i9xx_scanout_last_offset = 0ul;
static int v9x_i9xx_scanout_flip_outstanding = 0;
/* The live pipe's frame-counter registers, and the count when the flip was
 * issued: a flip is taken once a retrace has passed, which the counter
 * measured in intel69 says by advancing. */
static DWORD v9x_i9xx_scanout_framehigh_reg = 0ul;
static DWORD v9x_i9xx_scanout_framepixel_reg = 0ul;
static DWORD v9x_i9xx_scanout_flip_frame = 0ul;

static DWORD v9x_i9xx_scanout_frame_now(void)
{
    if (v9x_i9xx_scanout_framehigh_reg == 0ul) {
        return 0ul;
    }
    return v9x_i9xx_frame_count(
        *v9x_i9xx_scanout_reg(v9x_i9xx_scanout_framehigh_reg),
        *v9x_i9xx_scanout_reg(v9x_i9xx_scanout_framepixel_reg));
}

static int v9x_i9xx_scanout_pipe(DWORD *dsl, DWORD *vtotal, DWORD *base)
{
    static const DWORD conf_reg[2] = {
        V9X_I9XX_REG_PIPEA_CONF, V9X_I9XX_REG_PIPEB_CONF
    };
    static const DWORD dsl_reg[2] = {
        V9X_I9XX_REG_PIPEA_DSL, V9X_I9XX_REG_PIPEB_DSL
    };
    static const DWORD vtotal_reg[2] = {
        V9X_I9XX_REG_PIPEA_VTOTAL, V9X_I9XX_REG_PIPEB_VTOTAL
    };
    static const DWORD cntr_reg[2] = {
        V9X_I9XX_REG_DSPA_CNTR, V9X_I9XX_REG_DSPB_CNTR
    };
    static const DWORD addr_reg[2] = {
        V9X_I9XX_REG_DSPA_ADDR, V9X_I9XX_REG_DSPB_ADDR
    };
    static const DWORD stride_reg[2] = {
        V9X_I9XX_REG_DSPA_STRIDE, V9X_I9XX_REG_DSPB_STRIDE
    };
    static const DWORD framehigh_reg[2] = {
        V9X_I9XX_REG_PIPEA_FRAMEHIGH, V9X_I9XX_REG_PIPEB_FRAMEHIGH
    };
    static const DWORD framepixel_reg[2] = {
        V9X_I9XX_REG_PIPEA_FRAMEPIXEL, V9X_I9XX_REG_PIPEB_FRAMEPIXEL
    };
    DWORD index;
    DWORD pipes = 0ul;
    DWORD planes = 0ul;
    DWORD pipe = 0ul;
    DWORD plane = 0ul;

    for (index = 0ul; index < 2ul; ++index) {
        if ((*v9x_i9xx_scanout_reg(conf_reg[index]) &
             V9X_I9XX_PIPECONF_ENABLE) != 0ul) {
            pipe = index;
            ++pipes;
        }
    }
    if (pipes != 1ul) {
        ++v9x_hal->d3d_diagnostics.scanout_unresolved;
        return 0;
    }
    for (index = 0ul; index < 2ul; ++index) {
        DWORD control = *v9x_i9xx_scanout_reg(cntr_reg[index]);

        if ((control & V9X_I9XX_DSPCNTR_ENABLE) != 0ul &&
            ((control & V9X_I9XX_DSPCNTR_PIPE_MASK) >> 24) == pipe) {
            plane = index;
            ++planes;
        }
    }
    if (planes != 1ul) {
        ++v9x_hal->d3d_diagnostics.scanout_unresolved;
        return 0;
    }
    *dsl = dsl_reg[pipe];
    *vtotal = vtotal_reg[pipe];
    *base = addr_reg[plane];
    v9x_i9xx_scanout_plane = plane;
    v9x_i9xx_scanout_stride_reg = stride_reg[plane];
    v9x_i9xx_scanout_framehigh_reg = framehigh_reg[pipe];
    v9x_i9xx_scanout_framepixel_reg = framepixel_reg[pipe];
    return 1;
}

/*
 * The ring flip is on when the 16-bit side stamped it AND there is a ring
 * to put it in. The second is checked here rather than trusted: the ring
 * belongs to runtime 3D, and a boot with that off has none.
 */
static int v9x_i9xx_ring_flip_active(void)
{
    return v9x_i9xx_scanout_active() &&
           (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_FLIP_RING) != 0ul &&
           v9x_hal->engine.ring_linear_base != 0ul;
}

/*
 * The flip as the hardware does it: MI_DISPLAY_FLIP in the ring, the pitch
 * read from the plane's own stride register, decoded before it is submitted
 * as every stream this engine sends is. docs\plans\intel-gen3-ring-flip.md.
 */
static int v9x_i9xx_ring_flip(DWORD plane, DWORD stride_reg, DWORD byte_offset)
{
    DWORD stream[V9X_I9XX_FLIP_STREAM_DWORDS];
    DWORD written = 0ul;
    DWORD rejected = 0ul;
    DWORD pitch = *v9x_i9xx_scanout_reg(stride_reg);
    DWORD frame_before = v9x_i9xx_scanout_frame_now();

    if (v9x_i9xx_build_flip_stream(plane, pitch, byte_offset,
                                   v9x_hal->fb.vram_bytes, stream,
                                   V9X_I9XX_FLIP_STREAM_DWORDS,
                                   &written) != V9X_STATUS_OK ||
        v9x_i9xx_decode_flip_stream(stream, written, plane, pitch,
                                    byte_offset, v9x_hal->fb.vram_bytes,
                                    &rejected) == V9X_FALSE ||
        !v9x_d3d_i9xx_ring_submit(stream, written)) {
        ++v9x_hal->d3d_diagnostics.flip_ring_refused;
        return 0;
    }
    ++v9x_hal->d3d_diagnostics.flip_ring_issued;
    /* The pending bit, read once directly after the submit and before any
     * poll; intel72 never saw i915's bits set on this part. */
    if ((*v9x_i9xx_scanout_reg(V9X_I9XX_REG_ISR) &
         v9x_i9xx_flip_pending_bit(plane)) != 0ul) {
        ++v9x_hal->d3d_diagnostics.flip_ring_pending_seen;
    }
    /* Did the retrace pass inside the submit's wait? If the streamer
     * stalls on MI_DISPLAY_FLIP until the flip is taken, it did. */
    if (v9x_i9xx_scanout_frame_now() != frame_before) {
        ++v9x_hal->d3d_diagnostics.flip_frames_in_submit;
    }
    return 1;
}

/*
 * Right after a flip was issued by either path: does the base register
 * already read the new offset? Immediate says the hardware applied it (or
 * the register is not double-buffered on read); deferred says the old base
 * is still what the register shows. Remembered for the read at done.
 */
static void v9x_i9xx_note_flip_issued(DWORD base_reg, DWORD byte_offset)
{
    v9x_i9xx_scanout_last_base_reg = base_reg;
    v9x_i9xx_scanout_last_offset = byte_offset;
    v9x_i9xx_scanout_flip_outstanding = 1;
    /* Every ISR bit seen right after a flip, for the empirical search. */
    v9x_hal->d3d_diagnostics.isr_after_flip_or |=
        *v9x_i9xx_scanout_reg(V9X_I9XX_REG_ISR);
    if (*v9x_i9xx_scanout_reg(base_reg) == byte_offset) {
        ++v9x_hal->d3d_diagnostics.flip_base_immediate;
    } else {
        ++v9x_hal->d3d_diagnostics.flip_base_deferred;
    }
}

void v9x_scanout_note_flip_done(void)
{
    if (!v9x_i9xx_scanout_active() || !v9x_i9xx_scanout_flip_outstanding ||
        v9x_i9xx_scanout_last_base_reg == 0ul) {
        return;
    }
    v9x_i9xx_scanout_flip_outstanding = 0;
    if (*v9x_i9xx_scanout_reg(v9x_i9xx_scanout_last_base_reg) ==
            v9x_i9xx_scanout_last_offset) {
        ++v9x_hal->d3d_diagnostics.flip_taken_at_done;
    } else {
        ++v9x_hal->d3d_diagnostics.flip_not_taken_at_done;
    }
}

static int v9x_i9xx_in_vblank(void)
{
    DWORD dsl;
    DWORD vtotal;
    DWORD base;
    DWORD line;
    DWORD active;

    if (!v9x_i9xx_scanout_pipe(&dsl, &vtotal, &base)) {
        /* No pipe is on. "Always in blank" would let a flip complete
         * against a scanout that does not exist; "never" makes the state
         * machine wait its bounded spins and report not done. The second
         * is the honest one. */
        return 0;
    }
    line = *v9x_i9xx_scanout_reg(dsl) & V9X_I9XX_DSL_LINE_MASK;
    active = (*v9x_i9xx_scanout_reg(vtotal) & 0x00000ffful) + 1ul;
    return line >= active;
}

static int v9x_i9xx_set_display_start(DWORD byte_offset)
{
    DWORD dsl;
    DWORD vtotal;
    DWORD base;

    /* The plane fetches dwords; an offset that is not one would shift the
     * picture by a pixel fraction, which is the same refusal the S3 path
     * makes for the same reason. */
    if ((byte_offset & 3ul) != 0ul) {
        return 0;
    }
    if (!v9x_i9xx_scanout_pipe(&dsl, &vtotal, &base)) {
        return 0;
    }
    /* ISR with no flip of ours outstanding, and the frame the flip is
     * issued in: the completion rule below waits for the counter to move. */
    v9x_hal->d3d_diagnostics.isr_before_flip_or |=
        *v9x_i9xx_scanout_reg(V9X_I9XX_REG_ISR);
    v9x_i9xx_scanout_flip_frame = v9x_i9xx_scanout_frame_now();
    if (v9x_i9xx_ring_flip_active()) {
        if (!v9x_i9xx_ring_flip(v9x_i9xx_scanout_plane,
                                v9x_i9xx_scanout_stride_reg, byte_offset)) {
            return 0;
        }
        v9x_i9xx_note_flip_issued(base, byte_offset);
        return 1;
    }
    *v9x_i9xx_scanout_reg(base) = byte_offset;
    /* The read back posts the write, as i915 does after every plane-base
     * write on this generation, and now also says what the register holds:
     * the new base at once, or the old one until some later moment. */
    v9x_i9xx_note_flip_issued(base, byte_offset);
    return 1;
}

/*
 * The two entry points the core calls. Intel when this boot armed the Intel
 * flip; the VGA controls otherwise, which is every S3 part and every Intel
 * boot without IntelFlip=1.
 */
int v9x_in_vblank(void)
{
    if (v9x_i9xx_scanout_active()) {
        return v9x_i9xx_in_vblank();
    }
    return v9x_vga_in_vblank();
}

int v9x_set_display_start(DWORD byte_offset)
{
    if (v9x_i9xx_scanout_active()) {
        return v9x_i9xx_set_display_start(byte_offset);
    }
    return v9x_vga_set_display_start(byte_offset);
}

/*
 * The plane base is not latched at the retrace on this part - or not by
 * this write sequence. intel65: every Flip handled, none declined, the
 * batch flushed, and the operator sees tearing confined to the lower half
 * of the frame. That is the shape of a base written mid-scan and applied
 * at once: the beam finishes the old frame from the new buffer. i915's
 * own gen3 page flip goes through MI_DISPLAY_FLIP in the ring, which the
 * display side applies at the retrace, for what looks like this reason.
 * So on this path the write waits for the blank, and the flip completes
 * when that blank ends. A hypothesis from a picture, with the counters
 * to say whether the wait happened (FlipStillDrawing rises per flip).
 */
#define V9X_I9XX_FLIP_WRITE_IN_BLANK 1
/*
 * How far into the blank a flip may still be issued, in lines. intel73
 * showed the base applies the moment it is written, by either path, and
 * intel66 showed that writing anywhere in the blank and releasing the
 * buffer at the blank's end was worse than writing anywhere at all. So
 * the write is confined to the FIRST lines of the blank - the beam has
 * finished the old frame and nothing has begun fetching the new one - and
 * the buffer is released only at the frame tick. 24 lines is about 0.6 ms
 * at this timing; DirectDraw polls Flip roughly every 0.3 ms, so the
 * window is caught within a frame.
 */
#define V9X_I9XX_FLIP_WINDOW_LINES 24ul

/*
 * The issue window: the first lines of the vertical blank, and nothing
 * else. Not "in the blank" - intel66 was that.
 */
int v9x_scanout_flip_window_open(void)
{
    DWORD dsl;
    DWORD vtotal;
    DWORD base;
    DWORD line;
    DWORD active;

    if (!v9x_i9xx_scanout_active()) {
        return v9x_vga_in_vblank();
    }
    if (!v9x_i9xx_scanout_pipe(&dsl, &vtotal, &base)) {
        return 0;
    }
    line = *v9x_i9xx_scanout_reg(dsl) & V9X_I9XX_DSL_LINE_MASK;
    active = (*v9x_i9xx_scanout_reg(vtotal) & 0x00000ffful) + 1ul;
    return line >= active && line < active + V9X_I9XX_FLIP_WINDOW_LINES;
}

/*
 * Every Intel flip is a hardware flip. i915 says a plain plane-base write
 * "will also generate a page-flip completion irq": it pends until the
 * retrace and sets the same ISR bit as MI_DISPLAY_FLIP. So both paths are
 * completed the same way, from that bit, and the line-register states are
 * the VGA path's alone. docs\decisions\2026-09-18-intel-gen3-page-flip-
 * audit.md.
 */
DWORD v9x_scanout_displayed_offset(void)
{
    DWORD dsl;
    DWORD vtotal;
    DWORD base;

    if (!v9x_i9xx_scanout_active() ||
        !v9x_i9xx_scanout_pipe(&dsl, &vtotal, &base)) {
        return 0xfffffffful;
    }
    return *v9x_i9xx_scanout_reg(base);
}

int v9x_scanout_hw_flip(void)
{
    return v9x_i9xx_scanout_active();
}

/*
 * Pending while the ISR flip-pending bit for the resolved plane is set.
 * The plane is the one the last set_display_start resolved; a flip is
 * issued and completed against one plane.
 */
/*
 * Pending until BOTH say taken: the ISR bit is clear AND the frame counter
 * has moved since the flip was issued.
 *
 * The second is i915's fallback made primary: a flip queued in frame N is
 * on screen from frame N+1, and the counter measured in intel69 ticks once
 * per frame at line 671. intel72 tore with the first condition alone,
 * because that bit read clear at once on this part - whatever the reason.
 * Waiting for the tick costs at most one frame of latency and cannot
 * declare done before a retrace has passed. A streamer that stalled on the
 * flip has already moved the counter by the time this is asked, so it
 * costs nothing there.
 */
int v9x_scanout_hw_flip_pending(void)
{
    DWORD bit;

    if (!v9x_i9xx_scanout_active()) {
        return 0;
    }
    bit = v9x_i9xx_flip_pending_bit(v9x_i9xx_scanout_plane);
    if ((*v9x_i9xx_scanout_reg(V9X_I9XX_REG_ISR) & bit) != 0ul) {
        return 1;
    }
    return v9x_i9xx_scanout_frame_now() == v9x_i9xx_scanout_flip_frame;
}

int v9x_scanout_writes_in_blank(void)
{
    /*
     * ON again from intel73, with two differences from intel66. intel69
     * measured that DSL >= vactive IS the blank and intel73 measured that
     * the base applies at once and nothing pends, so an immediate base has
     * to be written when the beam is between frames - and intel66's
     * "worse" is explained by its completing at the blank's END, when the
     * plane may already be fetching the next frame's first lines from the
     * buffer just released. Now: written in the first lines of the blank
     * (v9x_scanout_flip_window_open), released at the frame tick (WAIT_HW).
     */
    if (V9X_I9XX_FLIP_WRITE_IN_BLANK == 0) {
        return 0;
    }
    return v9x_i9xx_scanout_active();
}

/*
 * Whether v9x_in_vblank can ever say yes. The VGA status port always
 * answers; the Intel line register answers only for a resolved pipe, and a
 * flip that waited on an unresolved one would wait forever - intel63's
 * 54,688 WASSTILLDRAWING answers were a flip armed under one mode and polled
 * under another. The flip state machine asks this before arming and while
 * pending, so an unresolvable scanout is a flip not tracked rather than a
 * flip never finished.
 */
int v9x_scanout_vblank_available(void)
{
    DWORD dsl;
    DWORD vtotal;
    DWORD base;

    if (!v9x_i9xx_scanout_active()) {
        return 1;
    }
    return v9x_i9xx_scanout_pipe(&dsl, &vtotal, &base);
}
