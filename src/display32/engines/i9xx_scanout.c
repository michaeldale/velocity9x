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
 *  - The panel is on pipe B (intel56, intel60: PIPEB_CONF bit 31 set, PIPEA
 *    zero). The live pipe is chosen by that bit rather than assumed, so a
 *    machine with the panel on A gets A.
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
 * The pipe with its enable bit set: B first, because that is where this
 * machine's panel is, then A. Returns the register offsets for that pipe
 * through the pointers; zero when neither pipe is on, in which case there is
 * no scanout to move and the caller declines.
 */
static int v9x_i9xx_scanout_pipe(DWORD *dsl, DWORD *vtotal, DWORD *base)
{
    if ((*v9x_i9xx_scanout_reg(V9X_I9XX_REG_PIPEB_CONF) &
         V9X_I9XX_PIPECONF_ENABLE) != 0ul) {
        *dsl = V9X_I9XX_REG_PIPEB_DSL;
        *vtotal = V9X_I9XX_REG_PIPEB_VTOTAL;
        *base = V9X_I9XX_REG_DSPB_ADDR;
        return 1;
    }
    if ((*v9x_i9xx_scanout_reg(V9X_I9XX_REG_PIPEA_CONF) &
         V9X_I9XX_PIPECONF_ENABLE) != 0ul) {
        *dsl = V9X_I9XX_REG_PIPEA_DSL;
        *vtotal = V9X_I9XX_REG_PIPEA_VTOTAL;
        *base = V9X_I9XX_REG_DSPA_ADDR;
        return 1;
    }
    return 0;
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
