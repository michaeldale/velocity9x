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
 *
 * gmadr is BAR2, read fresh from PCI config just before the call: the
 * mini-VDD maps the ring through it. It was a constant there, the netbook's
 * D0000000, which on the 915GM survey is BAR0 - the register file.
 */
extern unsigned short __far __pascal V9xMiniI9xxRingOpen(
    unsigned long gmadr, unsigned long __far *base,
    unsigned long __far *bytes);

/* Fresh BAR2, BAR3, BSM and GGC reads into the globals below; non-zero when
 * every read validated (runtime.asm). Read-only config access. */
extern unsigned short __far __pascal V9xPciReadIntelGttConfig(void);
extern unsigned long v9x_i9xx_gmadr_bar2;
extern unsigned long v9x_vbe_vram_reported;

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
extern unsigned short v9x_intel_flip_allowed;
extern unsigned short v9x_intel_async_submit_allowed;

/*
 * Why the last descriptor call did or did not claim Direct3D.
 *
 * Added because a boot could not be read. On 2026-09-16 the netbook came up
 * with IntelRuntime3D=1 and Direct3DMode=none, and nothing on disk said which
 * of four things had happened: the mapping verb refused, it returned a half
 * mapping, the permission was clear, or the ring refused. Every one of those
 * produces the same absent capability and the same silent page.
 *
 * The strings are the chip module's, published by intel_hw16.c as
 * EngineStatus= beside the other per-chip words. They are deliberately
 * mechanical rather than descriptive: each names the step that stopped, so a
 * capture answers "which" without anyone having to infer it.
 */
static const char *v9x_gma950_engine_status = "not-called";

const char *v9x_gma950_engine_status_text(void)
{
    return v9x_gma950_engine_status;
}

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
 * D3D is claimed only with a ring (below). Caps published against an engine
 * that cannot submit is the exact failure V9X_D3D_ENGINE_OPS gained its
 * `ready` member to prevent: every call accepted, every HRESULT a success,
 * and nothing on the screen.
 *
 * The type is set even without the claim, which matters. Leaving it NONE would make
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

    v9x_gma950_engine_status = "map-refused";
    if (V9xMiniI9xxEngineMap(&bar0, &bar3) == 0u) {
        return;
    }
    v9x_gma950_engine_status = "map-incomplete";
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
    /*
     * The two refusals ahead of RingOpen are about WHERE the ring goes, and
     * both run before it because RingOpen writes RING_START.
     *
     * A reported size other than the one the ring offset was generated for
     * puts the ring inside the DirectDraw heap (V9X_I9XX_RUNTIME_RING_START).
     * Zero is let through: the earliest descriptor call, at enable-start,
     * runs before 4F00h is read, and refusing there would decide the netbook's
     * first answer on a figure nobody has yet.
     */
    if (v9x_intel_runtime3d_allowed == 0u) {
        v9x_gma950_engine_status = "runtime3d-not-permitted";
    } else if (v9x_vbe_vram_reported != 0ul &&
               v9x_gma950_reserve_video_memory(v9x_vbe_vram_reported, 0ul) !=
                   V9X_I9XX_RUNTIME_RING_START) {
        v9x_gma950_engine_status = "ring-layout-mismatch";
    } else if (V9xPciReadIntelGttConfig() == 0u ||
               v9x_i9xx_gmadr_bar2 == 0ul) {
        v9x_gma950_engine_status = "gmadr-unread";
    } else if (V9xMiniI9xxRingOpen(v9x_i9xx_gmadr_bar2, &ring,
                                   &ring_size) == 0u) {
        v9x_gma950_engine_status = "ring-refused";
    } else if (ring == 0ul || ring_size == 0ul) {
        v9x_gma950_engine_status = "ring-incomplete";
    } else {
        v9x_gma950_engine_status = "d3d-claimed";
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
     * No 2D bit is needed for DirectDraw fills and copies: the HAL routes
     * those by engine_type to engines\eng_i9xx.c, which falls back to the
     * CPU without a ring. FLIP is claimed below; the VBIOS keeps the mode.
     */
    if (v9x_intel_runtime3d_allowed != 0u && *ring_linear_base != 0ul) {
        *engine_caps = V9X_DD_ENGINE_CAP_D3D;
        if (v9x_intel_async_submit_allowed != 0u) {
            *engine_caps |= V9X_DD_ENGINE_CAP_ASYNC_SUBMIT;
        }
    }
    /*
     * FLIP, from 2026-09-17, unless this boot's IntelFlip reads 0 (on by
     * default from 2026-09-18).
     *
     * The HAL moves the scanout through the live pipe's plane base register
     * and reads the pipe's display line for the retrace (engines\
     * i9xx_scanout.c); both need only the control window, which is mapped
     * by this point. Without the bit the HAL declines every Flip and
     * DirectDraw copies each frame with the CPU, which is intel56's flicker.
     * Independent of the ring: a flip is a display write, not an engine one.
     *
     * Measured intel62 through intel66: the write moves the scanout and the
     * line register sweeps; what remains open is where the base takes
     * effect relative to the retrace (the issue record).
     */
    if (v9x_intel_flip_allowed != 0u) {
        *engine_caps |= V9X_DD_ENGINE_CAP_FLIP;
        /* Through the ring whenever there is a ring to put it in - the
         * default from 2026-09-18, no key; the HAL checks the ring again
         * at the flip and falls back to the register write without one. */
        if (*ring_linear_base != 0ul) {
            *engine_caps |= V9X_DD_ENGINE_CAP_FLIP_RING;
        }
    }
}

/*
 * The 915GM: the 945's descriptor, plus the one difference the HAL has to
 * know about. MAP_STATE carries no layout bit Mesa ever sets, and with it
 * clear a 945 samples levels in the i945 arrangement while a 915 samples them
 * stacked (gallium i915_resource_texture.c, i915_texture_layout_2d against
 * i945_texture_layout_2d, chosen by is_i945). The bit says which to place.
 */
static void v9x_gma900_fill_engine(unsigned long framebuffer_linear_base,
                                   unsigned long *control_linear_base,
                                   unsigned long *mapped_aperture_bytes,
                                   unsigned long *engine_type,
                                   unsigned long *engine_caps,
                                   unsigned long *gtt_linear_base,
                                   unsigned long *ring_linear_base,
                                   unsigned long *ring_bytes)
{
    v9x_gma950_fill_engine(framebuffer_linear_base, control_linear_base,
                           mapped_aperture_bytes, engine_type, engine_caps,
                           gtt_linear_base, ring_linear_base, ring_bytes);
    if (*engine_type == V9X_DD_ENGINE_TYPE_INTEL_GEN3) {
        *engine_caps |= V9X_DD_ENGINE_CAP_I9XX_MIP_STACKED;
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
    /*
     * DirectDraw fills and copies on the Gen3 blitter through the ring
     * (engines\eng_i9xx.c, since 2026-09-25), and page flips through the
     * ring or the plane base. It was null - "no 2D acceleration" - for a
     * week after the blits landed, so the settings page called DirectDraw
     * software emulation on the netbook (2026-10-04). The S3 word, because
     * it means the same thing there: fill and copy on the engine, flip and
     * vblank. GDI stays the DIB engine's; this word is DirectDraw only.
     * Like the Direct3D word below it says what the chip has: a boot with
     * no ring falls back to CPU blits and still publishes it.
     */
    "directdraw-fill-blt",
    /*
     * The chip's word for the engine it carries, and it was null until
     * 2026-09-16 - one line below a capability claiming Direct3D.
     *
     * This string is STATIC and says what the silicon has, not what this boot
     * permits. The two are separate on purpose: what the settings page offers
     * is a property of the card, while whether the engine may run is
     * IntelRuntime3D and the ring, and the resolved answer reaches the page
     * through Direct3DMode= instead. Conflating them would have made the
     * Hardware entry appear and disappear between boots.
     *
     * Left null, the property page offered Software and Disabled only: the
     * chip whose engine this driver had just spent three boots measuring was
     * the one card that could not be asked to use it. Photographed on the
     * netbook 2026-09-16, build 9655778.
     */
    "hardware-gen3",
    0,
    v9x_gma950_fill_engine,
    /* VBE reports the framebuffer in GMADR BAR2. */
    2u
};

/*
 * The 915GM's GMA 900. Its BARs, stolen-memory decode, BSM and VBE size match
 * the 945GSE's in the one survey there is
 * (docs\probe\references\lenovo-3000-c100-915gm-vgasurv-2026-10-07.ini);
 * nothing here has run on one.
 */
const V9X_HW16_DEVICE v9x_gma900_device = {
    0x8086u, 0x2592u,
    "Intel GMA 900 (915GM)",
    "8086", "2592",
    "intel-gen3-mmio-fingerprint-v1",
    "vbe-lfb",
    "directdraw-fill-blt",
    "hardware-gen3",
    0,
    v9x_gma900_fill_engine,
    2u
};
