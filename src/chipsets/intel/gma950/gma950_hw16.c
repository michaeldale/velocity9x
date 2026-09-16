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

/*
 * Enable the ring and report where it is.
 *
 * Asked for HERE rather than by the 32-bit side, which has no way to reach
 * the mini-VDD at all: the HAL talks to this driver only through the shared
 * block. It also must not derive the address - it did once, from the already
 * reduced fb.vram_bytes, and landed inside the DirectDraw heap.
 */
extern unsigned short __far __pascal V9xMiniI9xxRingOpen(
    unsigned long __far *base, unsigned long __far *bytes);

/*
 * Whether this boot may touch the ring at all. Data, not a call, so no
 * segment crossing: it is set once by v9x_intel_boot_arm_prepare at
 * DriverInit, which is strictly before DDRAW asks for this descriptor.
 *
 * Declared here alongside the two verbs it governs rather than in intel16.h,
 * which exists for the CALLS that cross into I9XXCODE - the compact model
 * addresses all data far already. intel_exec16.c and intel_ring16.c extern
 * v9x_intel_boot_arm_latch the same way.
 */
extern unsigned short v9x_intel_runtime3d_allowed;

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
                                   unsigned long *gtt_linear_base,
                                   unsigned long *ring_linear_base,
                                   unsigned long *ring_bytes)
{
    unsigned long bar0 = 0ul;
    unsigned long bar3 = 0ul;
    unsigned long ring = 0ul;
    unsigned long ring_size = 0ul;

    (void)framebuffer_linear_base;
    *control_linear_base = 0ul;
    *mapped_aperture_bytes = 0ul;
    *gtt_linear_base = 0ul;
    *ring_linear_base = 0ul;
    *ring_bytes = 0ul;
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

    /*
     * The ring, brought up once here rather than on a draw - and ONLY with
     * this boot's permission.
     *
     * RingOpen is a card-state write: it sets RING_START and RING_CTL on a
     * boot that carries no arm token, which is a thing nothing in this driver
     * had ever done before 2026-09-16. Reaching it merely because the BARs
     * mapped would have put the one register sequence that can start a GPU
     * fetching behind no permission at all, on every DirectDraw session, with
     * no way to stop it from DOS after a hang. That last part is what makes it
     * a defect rather than a preference.
     *
     * Its absence is not fatal to the descriptor - the windows are still worth
     * publishing, and the engine reports itself not ready without a ring
     * rather than submitting to one that is not there.
     */
    if (v9x_intel_runtime3d_allowed != 0u &&
        V9xMiniI9xxRingOpen(&ring, &ring_size) != 0u &&
        ring != 0ul && ring_size != 0ul) {
        *ring_linear_base = ring;
        *ring_bytes = ring_size;
    }

    *control_linear_base = bar0;
    *mapped_aperture_bytes = V9X_I9XX_MMIO_BYTES;
    *gtt_linear_base = bar3;
    *engine_type = V9X_DD_ENGINE_TYPE_INTEL_GEN3;
    /*
     * DIRECT3D IS CLAIMED, from 2026-09-16.
     *
     * Authorised by the sustained-3D amendment to the errata gate, which was
     * asked for explicitly and which records what it gives up: no draw bound,
     * no combined-CRC gate, no one-shot token, and a hang that names a frame
     * nobody can reconstruct. The decoder's allowlist is what stands in their
     * place, and it runs on every stream this engine builds.
     *
     * Only claimed when the mapping AND the ring came up, which in turn needs
     * this boot's IntelRuntime3D permission. An engine advertised without a
     * ring would accept every call and draw nothing, which is the failure the
     * ops table's `ready` member exists to prevent - and a capability is
     * checked before `ready` is ever consulted.
     *
     * The permission is tested again here rather than inferred from the ring
     * address being non-zero. The two are the same fact today only because the
     * gate above is the only writer of that address; a capability that says
     * "an application may drive this engine" should not rest on that staying
     * true.
     *
     * NOT YET RUN. No guest has executed one of these streams. The first boot
     * with this bit set is the experiment the amendment describes, not a
     * driver release.
     *
     * 2D capabilities stay absent: engine-only ownership means the VBIOS keeps
     * the display, and nothing here has driven a blit outside the armed
     * diagnostic, so solid fill, screen copy, flip and vblank are unclaimed.
     */
    if (v9x_intel_runtime3d_allowed != 0u && *ring_linear_base != 0ul) {
        *engine_caps = V9X_DD_ENGINE_CAP_D3D;
    }
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
