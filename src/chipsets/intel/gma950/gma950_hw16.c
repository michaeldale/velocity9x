#include "velocity9x/hw16.h"
#include "velocity9x/intel_gma.h"
#include "velocity9x/engine_abi.h"

/*
 * The two linear windows the mini-VDD has already mapped: BAR0 and BAR3.
 *
 * Declared here rather than in a shared header because this is the only
 * caller. Returns non-zero with both written, or zero with both zeroed.
 */
extern unsigned short __far __pascal V9xMiniI9xxEngineMap(
    unsigned long __far *bar0, unsigned long __far *bar3);

unsigned long v9x_gma950_reserve_video_memory(
    unsigned long usable_bytes, unsigned long visible_bytes)
{
    struct v9x_i9xx_sandbox_layout layout;

    /* Physical BSM is irrelevant to the heap boundary, so zero is the honest
     * base for this calculation.  The first-write path independently checks
     * the measured BSM before it may use the physical fields. */
    if (v9x_i9xx_sandbox_calculate(usable_bytes, 0ul, &layout) !=
            V9X_STATUS_OK ||
        layout.heap_bytes < visible_bytes) {
        return usable_bytes;
    }
    return layout.heap_bytes;
}

/*
 * The Gen3 engine descriptor.
 *
 * Unlike every chip before it, this one cannot derive its control window from
 * the framebuffer base. The ViRGE's is the framebuffer BAR plus 16 MiB; Gen3's
 * registers are in BAR0 and its page table in BAR3, two independent regions
 * that this project has measured to move between DOS and Windows on the same
 * machine. So both come from the mini-VDD, which mapped them during the
 * capture and GTT paths, and neither is computed here.
 *
 * WHAT THIS DOES NOT DO: claim D3D.
 *
 * engine_caps carries no V9X_DD_ENGINE_CAP_D3D, and that is deliberate rather
 * than pending. The engine behind V9X_DD_ENGINE_TYPE_INTEL_GEN3 reports itself
 * not ready - it has no submission path yet - and caps published against an
 * engine that refuses every draw is the exact failure V9X_D3D_ENGINE_OPS
 * gained its `ready` member to prevent: every call accepted, every HRESULT a
 * success, and nothing on the screen.
 *
 * The type IS set, which matters. Leaving it NONE would make
 * v9x_d3d_publish_engine fall through to its default - the binary's one
 * hardware engine, which is the ViRGE - so a Gen3 part would resolve an S3
 * engine at the point in DriverInit where engine_type is normally unreadable.
 * Naming the type is what keeps that from happening.
 *
 * Nothing is advertised on a failed mapping. A descriptor carrying a type and
 * no aperture would be a chip claiming an engine it cannot reach.
 */
static void v9x_gma950_fill_engine(unsigned long framebuffer_linear_base,
                                   unsigned long *control_linear_base,
                                   unsigned long *mapped_aperture_bytes,
                                   unsigned long *engine_type,
                                   unsigned long *engine_caps,
                                   unsigned long *gtt_linear_base)
{
    unsigned long bar0 = 0ul;
    unsigned long bar3 = 0ul;

    (void)framebuffer_linear_base;
    *control_linear_base = 0ul;
    *mapped_aperture_bytes = 0ul;
    *gtt_linear_base = 0ul;
    *engine_type = V9X_DD_ENGINE_TYPE_NONE;
    *engine_caps = 0ul;

    if (V9xMiniI9xxEngineMap(&bar0, &bar3) == 0u) {
        return;
    }
    if (bar0 == 0ul || bar3 == 0ul) {
        /* The verb promises both or neither, and this is the caller not
         * taking that on trust: a half-mapped engine is the one state where
         * submitting would reach a window nobody mapped. */
        return;
    }

    *control_linear_base = bar0;
    *mapped_aperture_bytes = V9X_I9XX_MMIO_BYTES;
    *gtt_linear_base = bar3;
    *engine_type = V9X_DD_ENGINE_TYPE_INTEL_GEN3;
    /*
     * 2D capabilities are absent too. Engine-only ownership means the VBIOS
     * keeps the display, and nothing here has ever driven a blit outside the
     * armed diagnostic - so solid fill, screen copy, flip and vblank are all
     * unclaimed for the same reason D3D is.
     */
}

/* Exact physical target. The aperture hook remains absent: this family takes
 * the engine only, and the VBIOS keeps the display. */
const V9X_HW16_DEVICE v9x_gma950_device = {
    0x8086u, 0x27aeu,
    "Intel GMA 950 (945GSE)",
    "8086", "27AE",
    "intel-gen3-mmio-fingerprint-v1",
    "vbe-lfb",
    0,
    0,
    0,
    v9x_gma950_fill_engine,
    /* VBE reports the framebuffer in GMADR BAR2. */
    2u
};
