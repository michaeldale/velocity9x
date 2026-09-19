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
#include "velocity9x/i9xx_wm.h"

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
/* The display line when set_display_start was entered, against which the
 * line at issue gives the cost of the write path in scanlines. */
static DWORD v9x_i9xx_scanout_entry_line = 0ul;
/* Whether the PIPESTAT baseline has been taken and the underrun status
 * cleared to open the measurement boundary. Once per session. */
static int v9x_i9xx_pipestat_baselined = 0;

/*
 * Tries at a consistent pair of counter reads. The high word and the low
 * byte live in two registers; a carry between the two reads composes a
 * count off by 256, which the masked subtraction in hw_flip_pending reads
 * as hundreds of frames elapsed and completes a flip at once. i915 v4.4
 * reads high, low, high and retries while the highs differ
 * (i915_get_vblank_counter). Once a frame at most, so two tries settle it;
 * four is the bound so a register that never agrees cannot spin.
 */
#define V9X_I9XX_FRAME_READ_TRIES 4ul

/* Whether the last frame_now composed a consistent pair. A read that ran
 * out of tries is not a frame count, and a flip is kept pending on it
 * rather than completed against a number that may be off by 256. */
static int v9x_i9xx_scanout_frame_valid = 0;

static DWORD v9x_i9xx_scanout_frame_now(void)
{
    DWORD high_first;
    DWORD high_second;
    DWORD low;
    DWORD tries;

    v9x_i9xx_scanout_frame_valid = 0;
    if (v9x_i9xx_scanout_framehigh_reg == 0ul) {
        return 0ul;
    }
    high_first = *v9x_i9xx_scanout_reg(v9x_i9xx_scanout_framehigh_reg);
    low = *v9x_i9xx_scanout_reg(v9x_i9xx_scanout_framepixel_reg);
    high_second = *v9x_i9xx_scanout_reg(v9x_i9xx_scanout_framehigh_reg);
    for (tries = 1ul;
         tries < V9X_I9XX_FRAME_READ_TRIES &&
         (high_first & V9X_I9XX_FRAME_HIGH_MASK) !=
         (high_second & V9X_I9XX_FRAME_HIGH_MASK);
         ++tries) {
        high_first = high_second;
        low = *v9x_i9xx_scanout_reg(v9x_i9xx_scanout_framepixel_reg);
        high_second = *v9x_i9xx_scanout_reg(v9x_i9xx_scanout_framehigh_reg);
    }
    v9x_i9xx_scanout_frame_valid =
        (high_first & V9X_I9XX_FRAME_HIGH_MASK) ==
        (high_second & V9X_I9XX_FRAME_HIGH_MASK);
    return v9x_i9xx_frame_count(high_first, low);
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
/*
 * The scanout layout as it stands when a flip is issued, for the snapshot:
 * the plane's stride register, its control register (format, pipe select)
 * and the pipe's source size. The buffers are 0x96000 apart, 1280 x 480
 * exactly; a stride the mode set left at the desktop's 2048 would fetch 480
 * rows across 0xF0000 bytes and run 0x5A000 into the next buffer, showing
 * that buffer's construction at the bottom of the frame with both base
 * addresses different - which DrawsToFront=0 cannot see. The desktop
 * capture cannot answer this; only the value during the game can.
 */
static void v9x_i9xx_note_layout(DWORD byte_offset)
{
    DWORD cntr_reg = v9x_i9xx_scanout_plane == 0ul ? V9X_I9XX_REG_DSPA_CNTR
                                                   : V9X_I9XX_REG_DSPB_CNTR;
    DWORD cntr = *v9x_i9xx_scanout_reg(cntr_reg);
    DWORD src_reg = (cntr & V9X_I9XX_DSPCNTR_PIPE_MASK) == 0ul
                        ? V9X_I9XX_REG_PIPEA_SRC : V9X_I9XX_REG_PIPEB_SRC;

    v9x_hal->d3d_diagnostics.flip_stride_last =
        *v9x_i9xx_scanout_reg(v9x_i9xx_scanout_stride_reg);
    v9x_hal->d3d_diagnostics.flip_dspcntr_last = cntr;
    v9x_hal->d3d_diagnostics.flip_pipesrc_last = *v9x_i9xx_scanout_reg(src_reg);

    /* The whole layout, both planes and both pipes, the fitter and the
     * VGA and LVDS controls, at this same instant (review H4). */
    {
        static const DWORD regs[V9X_I9XX_SCAN_REG_COUNT] = {
            V9X_I9XX_REG_PIPEA_CONF, V9X_I9XX_REG_PIPEA_HTOTAL,
            V9X_I9XX_REG_PIPEA_VTOTAL, V9X_I9XX_REG_PIPEA_SRC,
            V9X_I9XX_REG_DSPA_CNTR, V9X_I9XX_REG_DSPA_ADDR,
            V9X_I9XX_REG_DSPA_STRIDE, V9X_I9XX_REG_DSPA_POS,
            V9X_I9XX_REG_DSPA_SIZE,
            V9X_I9XX_REG_PIPEB_CONF, V9X_I9XX_REG_PIPEB_HTOTAL,
            V9X_I9XX_REG_PIPEB_VTOTAL, V9X_I9XX_REG_PIPEB_SRC,
            V9X_I9XX_REG_DSPB_CNTR, V9X_I9XX_REG_DSPB_ADDR,
            V9X_I9XX_REG_DSPB_STRIDE, V9X_I9XX_REG_DSPB_POS,
            V9X_I9XX_REG_DSPB_SIZE,
            V9X_I9XX_REG_PFIT_CONTROL, V9X_I9XX_REG_PFIT_PGM_RATIOS,
            V9X_I9XX_REG_LVDS, V9X_I9XX_REG_VGACNTRL,
            V9X_I9XX_REG_PIPEB_DSL, V9X_I9XX_REG_PIPEB_FRAMEHIGH
        };
        DWORD index;

        for (index = 0ul; index < V9X_I9XX_SCAN_REG_COUNT; ++index) {
            v9x_hal->d3d_diagnostics.scan_reg_offset[index] = regs[index];
            v9x_hal->d3d_diagnostics.scan_reg_value[index] =
                *v9x_i9xx_scanout_reg(regs[index]);
        }
        v9x_hal->d3d_diagnostics.scan_sample_offset = byte_offset;
        v9x_hal->d3d_diagnostics.scan_sample_frame =
            v9x_i9xx_scanout_frame_now();
        ++v9x_hal->d3d_diagnostics.scan_layout_samples;
    }
}

/*
 * One watermark log entry per MODE, triggered by the pipe source changing
 * rather than by a session boundary.
 *
 * intel92 logged at the session boundary and came back with two entries
 * both at 1024x576, the panel's own mode, and none at the game's 640x480 -
 * so a mode change does not reliably produce a DriverInit and the boundary
 * is the wrong trigger. The pipe source IS the mode, read from the
 * hardware, so a change in it is the event worth an entry.
 *
 * DSPARB goes in beside it: it partitions the FIFO between the planes and
 * is the one input the watermark arithmetic needs that intel92 did not
 * capture. And the computed watermark goes in beside what is actually
 * programmed, so the comparison needs no arithmetic done by hand off a
 * capture - the formula is v9x_i9xx_wm_plane, host-tested against this
 * machine's timing in tests\host\test_i9xx_wm.c.
 *
 * Nothing here writes a watermark. This says what the difference is; what
 * to do about it is a later change, made once the computation has been
 * seen to agree with the hardware on a real mode.
 */
static void v9x_i9xx_note_watermarks(void)
{
    DWORD src_reg = v9x_i9xx_scanout_plane == 0ul
                    ? V9X_I9XX_REG_PIPEA_SRC : V9X_I9XX_REG_PIPEB_SRC;
    DWORD htotal_reg = v9x_i9xx_scanout_plane == 0ul
                       ? V9X_I9XX_REG_PIPEA_HTOTAL
                       : V9X_I9XX_REG_PIPEB_HTOTAL;
    DWORD vtotal_reg = v9x_i9xx_scanout_plane == 0ul
                       ? V9X_I9XX_REG_PIPEA_VTOTAL
                       : V9X_I9XX_REG_PIPEB_VTOTAL;
    DWORD pipesrc = *v9x_i9xx_scanout_reg(src_reg);
    DWORD count = v9x_hal->d3d_diagnostics.wm_log_count;
    DWORD slot;
    DWORD dsparb;
    DWORD fifo_a = 0ul;
    DWORD fifo_b = 0ul;
    DWORD rate_khz;
    DWORD cpp;
    DWORD want = 0ul;

    /* Only when the mode changes, and only while there is room. */
    if (count != 0ul &&
        v9x_hal->d3d_diagnostics.wm_log_pipesrc[count - 1ul] == pipesrc) {
        return;
    }
    if (count >= (DWORD)V9X_D3D_WM_LOG) {
        return;
    }
    slot = count;
    dsparb = *v9x_i9xx_scanout_reg(V9X_I9XX_REG_DSPARB);

    v9x_hal->d3d_diagnostics.wm_log_pipesrc[slot] = pipesrc;
    v9x_hal->d3d_diagnostics.wm_log_dsparb[slot] = dsparb;
    v9x_hal->d3d_diagnostics.wm_log_fw_blc[slot] =
        *v9x_i9xx_scanout_reg(V9X_I9XX_REG_FW_BLC);
    v9x_hal->d3d_diagnostics.wm_log_fw_blc2[slot] =
        *v9x_i9xx_scanout_reg(V9X_I9XX_REG_FW_BLC2);
    v9x_hal->d3d_diagnostics.wm_log_fw_blc_self[slot] =
        *v9x_i9xx_scanout_reg(V9X_I9XX_REG_FW_BLC_SELF);

    /*
     * The pixel rate from the pipe's own totals at the 60 Hz this driver
     * reports, and the bytes per pixel from the mode. Both totals are
     * stored less one, as vactive is elsewhere in this file.
     */
    rate_khz = (((*v9x_i9xx_scanout_reg(htotal_reg) >> 16) & 0x0ffful) + 1ul) *
               (((*v9x_i9xx_scanout_reg(vtotal_reg) >> 16) & 0x0ffful) + 1ul);
    rate_khz = rate_khz / 1000ul * 60ul;
    cpp = (v9x_hal->fb.bits_per_pixel + 7ul) / 8ul;

    if (v9x_i9xx_wm_fifo_split(dsparb, &fifo_a, &fifo_b) != V9X_FALSE) {
        /*
         * Only the plane that is actually driving gets the active
         * arithmetic. v9x_i9xx_scanout_pipe resolves exactly one live plane
         * and declines anything else, so the other is idle by construction,
         * and i915 gives an idle plane its whole FIFO less the guard rather
         * than a watermark computed from a pixel rate it is not consuming.
         *
         * The netbook runs plane B. Computing plane A as though it were
         * live gave 17 where the idle answer is 26, and programming that
         * would have starved a plane for a fetch it never makes (review of
         * ed3217a).
         */
        DWORD rate_a = v9x_i9xx_scanout_plane == 0ul ? rate_khz : 0ul;
        DWORD rate_b = v9x_i9xx_scanout_plane == 0ul ? 0ul : rate_khz;

        want = v9x_i9xx_wm_fw_blc_merge(
                   v9x_hal->d3d_diagnostics.wm_log_fw_blc[slot],
                   v9x_i9xx_wm_plane(rate_a, cpp, fifo_a,
                                     V9X_I9XX_WM_LATENCY_NS),
                   v9x_i9xx_wm_plane(rate_b, cpp, fifo_b,
                                     V9X_I9XX_WM_LATENCY_NS));
        v9x_hal->d3d_diagnostics.wm_log_computed[slot] = want;
    }
    v9x_hal->d3d_diagnostics.wm_log_rate_khz[slot] = rate_khz;
    ++v9x_hal->d3d_diagnostics.wm_log_count;

    /*
     * And program it.
     *
     * intel93 read FW_BLC as 6 and 6 at both the panel's mode and the
     * game's. That is NOT proof the BIOS never reprograms them: both modes
     * share a pixel rate, a pixel format and a FIFO partition, so a correct
     * per-mode calculation would land on the same number twice. What the
     * captures establish is the VALUE, not its provenance.
     *
     * The value is the point. Plane B is the one driving the panel, and for
     * it the arithmetic i915 uses gives 20 against the 6 programmed - a
     * third of the margin, on a pipe intel90 and intel91 both measured
     * underrunning. Plane A is idle here and gets its FIFO less the guard,
     * 26, which is what i915 gives a plane that is not fetching.
     *
     * Only the managed fields are replaced; bit 25 and anything else the
     * BIOS left is kept, because overwriting a bit whose meaning is not
     * established is not a thing to do to a machine reached by carrying a
     * USB stick to it.
     *
     * Self-refresh stays as found. i915 disables CxSR around a watermark
     * change and re-enables it after; here FW_BLC_SELF's enable is already
     * clear (intel91), so there is nothing to disable and enabling it would
     * be a second change rolled into this one.
     *
     * UNCONFIRMED as a fix for the flicker. What is measured is the
     * underrun and the shortfall; that the underrun is what the camera
     * caught is inference from the symptom's shape and its concentration in
     * heavy scenes.
     */
    if (want != 0ul && want != v9x_hal->d3d_diagnostics.wm_log_fw_blc[slot]) {
        *v9x_i9xx_scanout_reg(V9X_I9XX_REG_FW_BLC) = want;
        /* Posting read, and the record of what the register now holds. */
        v9x_hal->d3d_diagnostics.wm_written =
            *v9x_i9xx_scanout_reg(V9X_I9XX_REG_FW_BLC);
        ++v9x_hal->d3d_diagnostics.wm_writes;
    }
}

static void v9x_i9xx_note_flip_issued(DWORD base_reg, DWORD byte_offset)
{
    v9x_i9xx_scanout_last_base_reg = base_reg;
    v9x_i9xx_scanout_last_offset = byte_offset;
    v9x_i9xx_scanout_flip_outstanding = 1;
    /* Only a flip to a buffer other than offset zero is the game's: the
     * desktop restoration flips to zero, and would otherwise overwrite the
     * in-game sample on the way out (review of 2347f59). */
    if (byte_offset != 0ul) {
        v9x_i9xx_note_layout(byte_offset);
    }
    /* Every ISR bit seen right after a flip, for the empirical search. */
    v9x_hal->d3d_diagnostics.isr_after_flip_or |=
        *v9x_i9xx_scanout_reg(V9X_I9XX_REG_ISR);
    /*
     * PIPESTAT, whose bit 31 is the display FIFO underrun. Sticky, so the
     * first flip of a session takes the baseline and then opens a
     * measurement boundary by clearing it; everything ORed after that is a
     * FRESH underrun. See the register's note in intel_gma.h for the clear,
     * which is i915's and preserves the interrupt enables.
     */
    if (!v9x_i9xx_pipestat_baselined) {
        DWORD first_a = *v9x_i9xx_scanout_reg(V9X_I9XX_REG_PIPEA_STAT);
        DWORD first_b = *v9x_i9xx_scanout_reg(V9X_I9XX_REG_PIPEB_STAT);

        v9x_hal->d3d_diagnostics.pipestat_a_first = first_a;
        v9x_hal->d3d_diagnostics.pipestat_b_first = first_b;
        *v9x_i9xx_scanout_reg(V9X_I9XX_REG_PIPEA_STAT) =
            (first_a & V9X_I9XX_PIPESTAT_ENABLE_MASK) |
            V9X_I9XX_PIPESTAT_FIFO_UNDERRUN;
        *v9x_i9xx_scanout_reg(V9X_I9XX_REG_PIPEB_STAT) =
            (first_b & V9X_I9XX_PIPESTAT_ENABLE_MASK) |
            V9X_I9XX_PIPESTAT_FIFO_UNDERRUN;
        /* Posting reads, and they are the first post-boundary samples. */
        v9x_hal->d3d_diagnostics.pipestat_a_or |=
            *v9x_i9xx_scanout_reg(V9X_I9XX_REG_PIPEA_STAT);
        v9x_hal->d3d_diagnostics.pipestat_b_or |=
            *v9x_i9xx_scanout_reg(V9X_I9XX_REG_PIPEB_STAT);
        /* And the watermarks, once, at the same boundary: this is what the
         * BIOS left for the mode the game is actually in. Never written. */
        v9x_hal->d3d_diagnostics.fw_blc =
            *v9x_i9xx_scanout_reg(V9X_I9XX_REG_FW_BLC);
        v9x_hal->d3d_diagnostics.fw_blc2 =
            *v9x_i9xx_scanout_reg(V9X_I9XX_REG_FW_BLC2);
        v9x_hal->d3d_diagnostics.fw_blc_self =
            *v9x_i9xx_scanout_reg(V9X_I9XX_REG_FW_BLC_SELF);
        v9x_hal->d3d_diagnostics.pipestat_cleared = 1ul;
        v9x_i9xx_pipestat_baselined = 1;
    } else {
        v9x_hal->d3d_diagnostics.pipestat_a_or |=
            *v9x_i9xx_scanout_reg(V9X_I9XX_REG_PIPEA_STAT);
        v9x_hal->d3d_diagnostics.pipestat_b_or |=
            *v9x_i9xx_scanout_reg(V9X_I9XX_REG_PIPEB_STAT);
    }
    v9x_i9xx_note_watermarks();
    /*
     * The scanline at the moment the base is read back, which settles what
     * that readback IS.
     *
     * i915's page-flip stall check asks whether the plane address register
     * has reached the expected offset and warns if it has not, which only
     * works if the register reads the ACTIVE value once latched. The model
     * in this file says the opposite - that it returns the PENDING value -
     * and that was inferred from a video, never measured. The two cannot
     * both hold, and flip_base_immediate reading 605 of 605 means opposite
     * things under them.
     *
     * One reading decides it. A register holding the ACTIVE value cannot
     * report a new offset while the beam is in the middle of the frame,
     * because the latch has not happened yet. So: the DSL line at which
     * the readback was taken, kept as last, min and max across the run. If
     * the base reads new at lines scattered through active video, the
     * readback is the pending value and every conclusion drawn from it -
     * intel73's "applies at once" among them - is about a register that
     * was never reporting the scanout.
     */
    /*
     * Only a flip that went through the window test, which is the game's -
     * FlipToGDISurface writes the display start directly on the way back to
     * the desktop and takes no test, so its issue line says nothing about
     * the guard and mixing the two put line 610 in a statistic the guard was
     * then sized from (intel90). Offset zero is the desktop restore, the
     * same test the layout sample above uses.
     */
    if (byte_offset != 0ul) {
        DWORD dsl;
        DWORD vtotal;
        DWORD base;

        if (v9x_i9xx_scanout_pipe(&dsl, &vtotal, &base)) {
            DWORD line = *v9x_i9xx_scanout_reg(dsl) & V9X_I9XX_DSL_LINE_MASK;

            v9x_hal->d3d_diagnostics.flip_issue_line_last = line;
            if (v9x_hal->d3d_diagnostics.flip_issue_line_max < line) {
                v9x_hal->d3d_diagnostics.flip_issue_line_max = line;
            }
            if (v9x_hal->d3d_diagnostics.flip_issue_line_min == 0ul ||
                v9x_hal->d3d_diagnostics.flip_issue_line_min > line) {
                v9x_hal->d3d_diagnostics.flip_issue_line_min = line;
            }
            v9x_hal->d3d_diagnostics.flip_issue_vactive =
                (*v9x_i9xx_scanout_reg(vtotal) & 0x00000ffful) + 1ul;
            /* Lines consumed between entering the write path and the flip
             * being issued. Wraps are discarded rather than guessed at:
             * a flip that spans a frame boundary is not what the guard is
             * being sized against. */
            if (line >= v9x_i9xx_scanout_entry_line) {
                DWORD delta = line - v9x_i9xx_scanout_entry_line;

                v9x_hal->d3d_diagnostics.flip_issue_delta_last = delta;
                if (v9x_hal->d3d_diagnostics.flip_issue_delta_max < delta) {
                    v9x_hal->d3d_diagnostics.flip_issue_delta_max = delta;
                }
            }
        }
    }
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
    /*
     * The line on the way IN, so the distance to the line at issue is the
     * cost of everything between - which intel89 measured indirectly and
     * badly: the window test requires line < 568 and the issue line
     * reached 660, so something between them was eating up to ninety-odd
     * lines. This measures it directly instead of inferring it from the
     * two ends.
     */
    v9x_i9xx_scanout_entry_line =
        *v9x_i9xx_scanout_reg(dsl) & V9X_I9XX_DSL_LINE_MASK;
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
/*
 * A new session, from DriverInit.
 *
 * The PIPESTAT baseline is per SESSION, not per DLL lifetime. Taking it
 * once and never again would mean the second benchmark run on one boot -
 * or any run after a mode change - skips the clearing boundary entirely,
 * and the mode change's own underrun lands in the accumulator as though it
 * were gameplay. That is precisely the contamination the boundary exists
 * to remove, so the flag and everything derived from it are cleared here
 * (review of 7d11b59).
 *
 * DriverInit runs on a new session AND on a mode change, and the game's
 * mode set precedes its first flip, so the boundary still lands after the
 * event it is meant to exclude.
 */
void v9x_scanout_reset(void)
{
    v9x_i9xx_pipestat_baselined = 0;
    v9x_i9xx_scanout_entry_line = 0ul;
    v9x_i9xx_scanout_flip_outstanding = 0;
    if (v9x_hal == 0) {
        return;
    }
    v9x_hal->d3d_diagnostics.pipestat_a_first = 0ul;
    v9x_hal->d3d_diagnostics.pipestat_b_first = 0ul;
    v9x_hal->d3d_diagnostics.pipestat_a_or = 0ul;
    v9x_hal->d3d_diagnostics.pipestat_b_or = 0ul;
    v9x_hal->d3d_diagnostics.pipestat_cleared = 0ul;
    v9x_hal->d3d_diagnostics.fw_blc = 0ul;
    v9x_hal->d3d_diagnostics.fw_blc2 = 0ul;
    v9x_hal->d3d_diagnostics.fw_blc_self = 0ul;
    /* The issue-line readings are session-scoped too: a min and max
     * carried across a mode change describe two different timings. */
    v9x_hal->d3d_diagnostics.flip_issue_line_last = 0ul;
    v9x_hal->d3d_diagnostics.flip_issue_line_min = 0ul;
    v9x_hal->d3d_diagnostics.flip_issue_line_max = 0ul;
    v9x_hal->d3d_diagnostics.flip_issue_vactive = 0ul;
    v9x_hal->d3d_diagnostics.flip_issue_delta_last = 0ul;
    v9x_hal->d3d_diagnostics.flip_issue_delta_max = 0ul;
}

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
 * to say whether the wait happened (FlipWindowClosed rises per flip from
 * 2026091901; before that it was folded into FlipStillDrawing, which is
 * why intel86 could not separate them).
 */
#define V9X_I9XX_FLIP_WRITE_IN_BLANK 1
/*
 * The plane base is double-buffered and latched at the START of the
 * vertical blank - the first blank line, 576 here - not applied as it is
 * written and not latched at the frame tick (671). (A model from the
 * video record; the reviews of 2026-09-18 note it is not established by
 * a register, and the i915 citation below supports a guard around the
 * vblank start, not a specific write window.)
 *
 * The evidence is the operator's video of intel7x (docs\decisions\
 * 2026-09-18-intel-plane-base-latches-at-vblank-start.md): once per game
 * frame the panel shows the buffer the game is DRAWING - the clear, then
 * the sky, then the ground filling in - for about one display frame,
 * and then the finished picture. DrawsToFront=0 in the same runs says
 * every batch went to the buffer the base REGISTER did not name; so the
 * register does not say what the panel shows for one frame after it is
 * written. That is a latch, and the writes of intel66/74/75/76/77 all
 * landed in the blank, AFTER the latch point, and were released at the
 * tick of the same blank - one frame before the panel switched. intel65,
 * written anywhere and released at a bit that reads zero, released before
 * the latch too. The readback that made intel73 say "applies at once"
 * returns the pending value, which is what a double-buffered register
 * reads back.
 *
 * i915's intel_pipe_update_start (intel_sprite.c) keeps its plane register
 * writes OUT of the interval just before vblank start
 * (VBLANK_EVASION_TIME_US, 100 us), so a write is never racing the latch;
 * that supports a guard before the blank, which is what this window is,
 * not a preferred moment inside those 100 us. Gen2/3 page flips go through
 * MI_DISPLAY_FLIP, applied by the display side at the vblank.
 *
 * So the write is issued while the beam is in ACTIVE video, with a guard
 * of lines before the latch so a write racing the latch point cannot land
 * on the wrong side of it, and the buffer is released at the frame tick,
 * which follows the latch in the same blank. Any active line serves; the
 * window is the whole active frame less the guard, so a Flip retried every
 * 0.3 ms finds it at once.
 *
 * MEASURED, and not a fix: intel86 (2026-09-19, netbook, Final Reality
 * robot benchmark) ran this window for 585 presents and the operator saw
 * the same flicker. So the write window is not what makes the panel show
 * the buffer under construction, and neither are the two models this run
 * also closed - DrawsFlipWaited=0 says no batch ever arrived while a flip
 * was pending (intel78's exposure), and RenderDrainWaits=0 against a
 * working completion channel says no flip ever presented drawing the GPU
 * had not finished (the whole point of the breadcrumb). The latch model
 * above remains unread from a register; what is now known is that moving
 * the write inside it does not change the picture.
 * docs\decisions\2026-09-19-intel86-the-completion-channel-works-and-the-
 * flicker-is-not-unfinished-drawing.md
 */
/*
 * Eight lines, and eight is right.
 *
 * intel89 appeared to show the guard leaking - the window requires the beam
 * below line 568 and the line at issue reached 660 - and this was widened to
 * ninety-six to cover the difference. That inference was wrong. intel90
 * measured the write path directly: flip_issue_delta_max is ONE scanline,
 * so a window-tested flip cannot be issued more than a line past where it
 * was tested, and there is no gap to guard against.
 *
 * The high issue lines came from flips that never took the test.
 * FlipToGDISurface calls set_display_start directly on the way back to the
 * desktop, twenty-two times in that run, and those land wherever the beam
 * happens to be. FlipRingIssued 578 against FlipHandled 554 is the same
 * twenty-odd. The line statistics now exclude them, so the number the
 * guard is sized from describes only the flips the guard governs.
 *
 * The widening cost what widening costs: FlipWindowClosed went from 45,625
 * to 169,972 with presents unchanged at 554 against 558. Pure refusal
 * churn for a gap that was not there.
 */
#define V9X_I9XX_FLIP_LATCH_GUARD_LINES 8ul

/*
 * The issue window: active video, short of the latch at the first blank
 * line. Not the blank - intel66 and intel74 to intel77 were that.
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
    if (active <= V9X_I9XX_FLIP_LATCH_GUARD_LINES) {
        return 0;
    }
    return line < active - V9X_I9XX_FLIP_LATCH_GUARD_LINES;
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
/*
 * How many frame ticks after the write a flip is called complete.
 *
 * intel79: the write in active video, the runtime holding every Lock and
 * Blt on GetFlipStatus (43 asks a frame), and NOT ONE batch arriving with
 * a flip pending (DrawsFlipWaited=0) - so the clear and every draw came
 * after the first tick said "done" - and the panel still showed the
 * cleared buffer with the sky on it once a frame. Whatever the latch is,
 * the panel switches AFTER the tick that follows the write, not before
 * it. Two ticks is one more frame than the panel could need under any
 * latch that is at most a frame late; it costs one frame of latency and
 * cannot be early. If the picture still shows construction at two, the
 * latch is not what the frame counter is counting, and the readback
 * probe is the only instrument left. Measured on intel80.
 */
#define V9X_I9XX_FLIP_TICKS_TO_COMPLETE 2ul

int v9x_scanout_hw_flip_pending(void)
{
    DWORD bit;
    DWORD ticks;

    if (!v9x_i9xx_scanout_active()) {
        return 0;
    }
    bit = v9x_i9xx_flip_pending_bit(v9x_i9xx_scanout_plane);
    if ((*v9x_i9xx_scanout_reg(V9X_I9XX_REG_ISR) & bit) != 0ul) {
        return 1;
    }
    ticks = (v9x_i9xx_scanout_frame_now() - v9x_i9xx_scanout_flip_frame) &
            V9X_I9XX_FRAME_COUNT_MASK;
    if (!v9x_i9xx_scanout_frame_valid) {
        return 1;
    }
    return ticks < V9X_I9XX_FLIP_TICKS_TO_COMPLETE;
}

int v9x_scanout_writes_in_blank(void)
{
    /*
     * The name is historical: the write is timed against the beam, and
     * from the video record it is timed to land in ACTIVE video, before
     * the latch at the first blank line (v9x_scanout_flip_window_open),
     * with the buffer released at the frame tick (WAIT_HW) that follows
     * the latch. intel66 and intel74 to intel77 wrote inside the blank,
     * after the latch, and released a frame early.
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
