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
extern unsigned short __far __pascal V9xPciReadHswGgc(
    unsigned short __far *ggc);
extern unsigned short __far __pascal V9xMiniHswStolenReserved(
    unsigned long bar0, unsigned long __far *reg);
extern unsigned short __far __pascal V9xMiniHswEngineOpen(
    unsigned long bar0, unsigned long gmadr, unsigned long bdsm,
    unsigned long offset, unsigned long chip, unsigned long __far *mmio,
    unsigned long __far *reserve, unsigned short __far *code);

/* HSW_ENGINE_OPEN's chip argument (EDI, V9XMAPI.INC): which forcewake the
 * mini-VDD takes. */
#define V9X_HSW_OPEN_CHIP_HASWELL 0ul
#define V9X_HSW_OPEN_CHIP_IVYBRIDGE 1ul
extern unsigned short __far __pascal V9xMiniHswEngineClose(void);

/*
 * 64 MiB of GMADR: above the 32 and 64 MiB of stolen memory the two boards
 * on record carry, and what every non-Intel family maps. The heap never
 * reaches past the protected base, and the GTT past stolen memory holds junk
 * with the valid bit set (docs\decisions\2026-10-20-haswell-stolen-
 * layout.md), so nothing may touch the mapping above the reserve.
 */
#define V9X_HSW_MAP_BYTES 0x04000000ul

/* The reserve does not depend on the mode, so the layout is planned against
 * one page of desktop; the heap hook checks the real mode against it. */
#define V9X_HSW_LAYOUT_VISIBLE 0x00001000ul

/*
 * Where the heap ends and the engine reserve sits, decided once a boot
 * (docs\plans\intel-haswell-heap-beyond-16-mib.md). Every step that cannot
 * be read or is not understood leaves the fallback, the 16 MiB heap and
 * 1800000h reserve every release before it ran, and says which step in
 * source.
 */
static unsigned short v9x_hsw_layout_decided;
static const char *v9x_hsw_layout_source = "undecided";
static unsigned long v9x_hsw_stolen_bytes;
static unsigned long v9x_hsw_protected_bytes;
static unsigned long v9x_hsw_reserved_reg;
static unsigned long v9x_hsw_reserve_at = V9X_INTEL_HSW_FALLBACK_RESERVE_OFFSET;
static unsigned long v9x_hsw_heap_at = V9X_INTEL_HSW_FALLBACK_HEAP_END;

static void v9x_hsw_decide_layout(void)
{
    struct v9x_intel_hsw_ggc ggc;
    struct v9x_intel_hsw_heap_inputs in;
    struct v9x_intel_hsw_heap_plan plan;
    unsigned long bar0 = 0ul;
    unsigned long gmadr = 0ul;
    unsigned long bdsm = 0ul;
    unsigned long reg = 0ul;
    unsigned long protected_bytes = 0ul;
    unsigned short ggc_word = 0u;

    if (v9x_hsw_layout_decided != 0u) {
        return;
    }
    v9x_hsw_layout_decided = 1u;

    if (V9xPciReadHswConfig(&bar0, &gmadr, &bdsm) == 0u ||
        V9xPciReadHswGgc(&ggc_word) == 0u) {
        v9x_hsw_layout_source = "fallback-config-unread";
        return;
    }
    if (v9x_intel_hsw_decode_ggc(ggc_word, &ggc) != V9X_STATUS_OK) {
        v9x_hsw_layout_source = "fallback-ggc-undecoded";
        return;
    }
    v9x_hsw_stolen_bytes = ggc.stolen_bytes;
    if (V9xMiniHswStolenReserved(bar0, &reg) != 1u) {
        v9x_hsw_layout_source = "fallback-register-unread";
        return;
    }
    v9x_hsw_reserved_reg = reg;
    if (v9x_intel_hsw_protected_bytes(bdsm, ggc.stolen_bytes, reg,
                                      &protected_bytes) != V9X_STATUS_OK) {
        v9x_hsw_layout_source = "fallback-protected-not-understood";
        return;
    }
    v9x_hsw_protected_bytes = protected_bytes;

    in.stolen_bytes = ggc.stolen_bytes;
    in.protected_bytes = protected_bytes;
    in.mapped_bytes = V9X_HSW_MAP_BYTES;
    in.vbe_bytes = 0ul;
    in.visible_bytes = V9X_HSW_LAYOUT_VISIBLE;
    if (v9x_intel_hsw_plan_heap(&in, &plan) != V9X_STATUS_OK) {
        v9x_hsw_layout_source = "fallback-no-room";
        return;
    }
    v9x_hsw_reserve_at = plan.reserve_offset;
    v9x_hsw_heap_at = plan.heap_end;
    v9x_hsw_layout_source = "chip";
}

/* The heap's end for intel_hw16.c's reserve hook, before the mode and
 * VBE's figure clamp it. */
unsigned long v9x_hsw_heap_end(void)
{
    v9x_hsw_decide_layout();
    return v9x_hsw_heap_at;
}

static unsigned long v9x_hsw_reserve_offset(void)
{
    v9x_hsw_decide_layout();
    return v9x_hsw_reserve_at;
}

static void v9x_hsw_format_hex(char *text, unsigned long value)
{
    static const char digits[] = "0123456789ABCDEF";
    unsigned short at;

    for (at = 0u; at < 8u; ++at) {
        text[at] = digits[(unsigned short)((value >> (28u - at * 4u)) & 0xful)];
    }
    text[8] = '\0';
}

/* The layout's keys for V9XHW.INI, so a field report says which heap a
 * machine got and why. */
void v9x_hsw_publish_layout(v9x_hw16_write_fn write)
{
    char number[9];

    v9x_hsw_decide_layout();
    write("HeapLayout", v9x_hsw_layout_source);
    v9x_hsw_format_hex(number, v9x_hsw_stolen_bytes);
    write("StolenBytes", number);
    v9x_hsw_format_hex(number, v9x_hsw_reserved_reg);
    write("Reg1082C0", number);
    v9x_hsw_format_hex(number, v9x_hsw_protected_bytes);
    write("ProtectedBytes", number);
    v9x_hsw_format_hex(number, v9x_hsw_reserve_at);
    write("ReserveOffset", number);
    v9x_hsw_format_hex(number, v9x_hsw_heap_at);
    write("HeapEnd", number);
}

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

/* Disable: give the ring and forcewake back. The next Enable reopens. The
 * close waits, bounded, for the draw the HAL may have left running; 2 says
 * it closed without seeing that draw finish. */
void v9x_hsw_engine_close(void)
{
    unsigned short closed = V9xMiniHswEngineClose();

    if (closed == 2u) {
        v9x_hsw_engine_status = "closed-render-busy";
        return;
    }
    if (closed != 0u) {
        v9x_hsw_engine_status = "closed";
    }
}

/*
 * The engine descriptor. Fresh config reads, then HSW_ENGINE_OPEN; only a
 * successful open publishes a type and caps. Called on Enable and on every
 * DirectDraw session, so the verb hands back a ring it already has open
 * rather than reprogramming it.
 *
 * Ivy Bridge runs the same open with its own forcewake (chip 1) and claims
 * the blitter alone: its render engine and plane differ from Haswell's
 * (docs\plans\intel-ivybridge-gen7.md, code audit), and neither has run.
 */
static void v9x_hsw_fill_engine_for(unsigned long chip,
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
    if (V9xMiniHswEngineOpen(bar0, gmadr, bdsm, v9x_hsw_reserve_offset(),
                             chip, &mmio, &reserve, &code) == 0u ||
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
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY;
    if (chip != V9X_HSW_OPEN_CHIP_HASWELL) {
        /* Ivy Bridge: flips through plane A's base and PRI_LINOFF, which
         * the HAL picks by the identity bit (engines\hsw_scanout.c). */
        *engine_caps |= V9X_DD_ENGINE_CAP_FLIP | V9X_DD_ENGINE_CAP_GEN7_IVB;
        return;
    }
    /* HSW_ENGINE_OPEN opens the render ring with the blitter's and refuses
     * both together, so an open engine has both (Phase F2). FLIP: plane A's
     * base, which the HAL writes only while pipe A and plane A are live
     * (Phase F3, engines\hsw_scanout.c). */
    *engine_caps |= V9X_DD_ENGINE_CAP_D3D | V9X_DD_ENGINE_CAP_FLIP;
}

static void v9x_hsw_fill_engine(unsigned long framebuffer_linear_base,
                                unsigned long *control_linear_base,
                                unsigned long *mapped_aperture_bytes,
                                unsigned long *engine_type,
                                unsigned long *engine_caps,
                                unsigned long *gtt_linear_base,
                                unsigned long *ring_linear_base,
                                unsigned long *ring_bytes)
{
    (void)framebuffer_linear_base;
    v9x_hsw_fill_engine_for(V9X_HSW_OPEN_CHIP_HASWELL, control_linear_base,
                            mapped_aperture_bytes, engine_type, engine_caps,
                            gtt_linear_base, ring_linear_base, ring_bytes);
}

static void v9x_ivb_fill_engine(unsigned long framebuffer_linear_base,
                                unsigned long *control_linear_base,
                                unsigned long *mapped_aperture_bytes,
                                unsigned long *engine_type,
                                unsigned long *engine_caps,
                                unsigned long *gtt_linear_base,
                                unsigned long *ring_linear_base,
                                unsigned long *ring_bytes)
{
    (void)framebuffer_linear_base;
    v9x_hsw_fill_engine_for(V9X_HSW_OPEN_CHIP_IVYBRIDGE, control_linear_base,
                            mapped_aperture_bytes, engine_type, engine_caps,
                            gtt_linear_base, ring_linear_base, ring_bytes);
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
    1u,
    V9X_HSW_MAP_BYTES,
    v9x_hsw_reserve_offset
};

/*
 * Ivy Bridge GT2 desktop on the same path, for 2D. The heap layout is
 * Haswell's: i915 decodes 1082C0h the same way on Gen7 and Gen7.5
 * (gen7_get_stolen_reserved). No Direct3D word: the descriptor claims no
 * render engine (v9x_ivb_fill_engine).
 */
const V9X_HW16_DEVICE v9x_ivb_hd4000_device = {
    0x8086u, 0x0162u,
    "Intel HD Graphics 4000 (Ivy Bridge)",
    "8086", "0162",
    "none",
    "vbe-lfb",
    "directdraw-fill-blt",
    0,
    0,
    v9x_ivb_fill_engine,
    /* VBE reports the framebuffer in GMADR, BAR2: E0000000 on W9QI4E14. */
    2u,
    /* Packed pitch: 640 at 640x480x8, measured at tier 0 there. */
    1u,
    V9X_HSW_MAP_BYTES,
    v9x_hsw_reserve_offset
};
