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
 * which it does only for a boot that carries IntelFlip=1 in INTELARM.TXT. An
 * unarmed boot dispatches to the VGA controls exactly as before, so one
 * package serves the read-only boot and the write boot in turn.
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
    return 1;
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
    *v9x_i9xx_scanout_reg(base) = byte_offset;
    /* Read back to post the write, as i915 does after every plane-base
     * write on this generation. The value is not checked: the register may
     * latch at the next frame and read the old base until then. */
    (void)*v9x_i9xx_scanout_reg(base);
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
#define V9X_I9XX_FLIP_WRITE_IN_BLANK 0

int v9x_scanout_writes_in_blank(void)
{
    /*
     * OFF. intel66 ran with the write inside the blank and the operator
     * reports the flicker WORSE than intel65's write-anywhere: 710 flips,
     * each preceded by about 2,950 WASSTILLDRAWING answers, so the wait
     * happened and the write landed where DSL >= vactive. If that region
     * were the blank of an immediately-applied base, the tearing would have
     * gone. It did not, so at least one of "the base applies at once" and
     * "DSL >= vactive is the blank" is wrong, and this driver cannot say
     * which. The scanout watch now records the line at which the frame
     * counter ticks; that measurement decides, and until it is in a
     * capture the flip goes back to the less-bad behaviour.
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
