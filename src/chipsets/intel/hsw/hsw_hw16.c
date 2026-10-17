#include "velocity9x/hw16.h"
#include "velocity9x/intel_hsw.h"
#include "velocity9x/engine_abi.h"

/*
 * Intel HD Graphics 4600 (Haswell GT2, 8086:0412): DirectDraw fills and
 * copies on the BCS ring. Phase B2 of docs\plans\intel-haswell-direct3d.md.
 *
 * Nothing in this file shares a path with the Gen3 chips beside it: no Gen3
 * verb, no arm token, no BAR3, no ring sandbox. The mini-VDD verbs are
 * HSW_ENGINE_OPEN and HSW_ENGINE_CLOSE (include\asm\V9XMAPI.INC), which run
 * Phase B1's measured sequence and its teardown.
 */

/* runtime.asm. PASCAL, so the first argument is the farthest on the stack. */
extern unsigned short __far __pascal V9xPciReadHswConfig(
    unsigned long __far *bar0, unsigned long __far *gmadr,
    unsigned long __far *bdsm);
extern unsigned short __far __pascal V9xMiniHswEngineOpen(
    unsigned long bar0, unsigned long gmadr, unsigned long bdsm,
    unsigned long __far *mmio, unsigned long __far *reserve,
    unsigned short __far *code);
extern unsigned short __far __pascal V9xMiniHswEngineClose(void);

/*
 * Why the last descriptor call did or did not open the engine, published as
 * EngineStatusEnable= by intel_hw16.c. Mechanical names, one per refusal
 * code of HSW_ENGINE_OPEN, so a capture says which stage stopped without
 * anyone inferring it.
 */
static const char *v9x_hsw_engine_status = "not-called";

static const char *v9x_hsw_refusal_text(unsigned short code)
{
    switch (code) {
    case 1u:
        return "open-refused-input";
    case 2u:
        return "open-refused-map";
    case 3u:
        return "open-refused-pte";
    case 4u:
        return "open-refused-cpu-path";
    case 5u:
        return "open-refused-forcewake-held";
    case 6u:
        return "open-refused-forcewake-timeout";
    case 7u:
        return "open-refused-ring-on";
    case 8u:
        return "open-refused-ring-readback";
    case 0xffffu:
        return "open-refused-no-api";
    default:
        return "open-refused-unknown";
    }
}

const char *v9x_hsw_engine_status_text(void)
{
    return v9x_hsw_engine_status;
}

/* Disable: give the ring and forcewake back. The next Enable reopens. */
void v9x_hsw_engine_close(void)
{
    if (V9xMiniHswEngineClose() != 0u) {
        v9x_hsw_engine_status = "closed";
    }
}

/*
 * The engine descriptor. Fresh config reads, then HSW_ENGINE_OPEN; only a
 * successful open publishes a type and caps. Called on Enable and on every
 * DirectDraw session, so the verb hands back a ring it already has open
 * rather than reprogramming it.
 */
static void v9x_hsw_fill_engine(unsigned long framebuffer_linear_base,
                                unsigned long *control_linear_base,
                                unsigned long *mapped_aperture_bytes,
                                unsigned long *engine_type,
                                unsigned long *engine_caps,
                                unsigned long *gtt_linear_base,
                                unsigned long *ring_linear_base,
                                unsigned long *ring_bytes)
{
    unsigned long bar0 = 0ul;
    unsigned long gmadr = 0ul;
    unsigned long bdsm = 0ul;
    unsigned long mmio = 0ul;
    unsigned long reserve = 0ul;
    unsigned short code = 0u;

    (void)framebuffer_linear_base;
    *control_linear_base = 0ul;
    *mapped_aperture_bytes = 0ul;
    *gtt_linear_base = 0ul;
    *ring_linear_base = 0ul;
    *ring_bytes = 0ul;
    *engine_type = V9X_DD_ENGINE_TYPE_NONE;
    *engine_caps = 0ul;

    v9x_hsw_engine_status = "config-unread";
    if (V9xPciReadHswConfig(&bar0, &gmadr, &bdsm) == 0u) {
        return;
    }
    if (V9xMiniHswEngineOpen(bar0, gmadr, bdsm, &mmio, &reserve, &code) ==
            0u ||
        mmio == 0ul || reserve == 0ul) {
        v9x_hsw_engine_status = v9x_hsw_refusal_text(code);
        return;
    }

    v9x_hsw_engine_status = code == 0x10u ? "open-reused" : "open";
    *control_linear_base = mmio;
    *mapped_aperture_bytes = 0x00200000ul;
    *ring_linear_base = reserve;
    *ring_bytes = V9X_INTEL_HSW_RING_BYTES;
    *engine_type = V9X_DD_ENGINE_TYPE_INTEL_GEN75;
    /* HSW_ENGINE_OPEN opens the render ring with the blitter's and refuses
     * both together, so an open engine has both (Phase F2). */
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY | V9X_DD_ENGINE_CAP_D3D;
}

const V9X_HW16_DEVICE v9x_hsw_hd4600_device = {
    0x8086u, 0x0412u,
    "Intel HD Graphics 4600 (Haswell)",
    "8086", "0412",
    "none",
    "vbe-lfb",
    "directdraw-fill-blt",
    "hardware-gen75",
    0,
    v9x_hsw_fill_engine,
    /* VBE reports the framebuffer in GMADR, BAR2 (offset 18h). */
    2u,
    /* The packed pitch the tier-0 desktop was measured at. */
    1u
};
