/*
 * AMD Mullins, PCI 1002:9854 rev 01 (Radeon R3E, GCN 2), the GPU in the
 * GX-412HC embedded APU. Linux binds 9850-985F as CHIP_MULLINS, a CIK
 * (Sea Islands) part.
 *
 * The physical target is the remote-help machine M-JJN1ZZ since
 * 2026-11-08. The VBE reports 16 MiB and its framebuffer at E0000000
 * (BAR0, 256 MiB, which covers the whole carve-out); the video memory is
 * a 256 MiB carve-out of system RAM at GPU F4_00000000, the registers a
 * 256 KiB window at F0C00000 (BAR5)
 * (docs\decisions\2026-11-08-mullins-register-probe.md).
 *
 * Mullins' SDMA engine is Carrizo's at the same register offsets, with
 * its own microcode and CIK's golden values, and it ran Carrizo's probe
 * byte-exact once those were changed
 * (docs\decisions\2026-11-08-mullins-sdma-write-probe.md). So this row
 * hands DirectDraw fill, copy and flips to Carrizo's engine
 * (engines\eng_vi.c, V9X_DD_ENGINE_TYPE_ATI_VI) with the Mullins identity
 * bit, through the same BAR read and mini-VDD map as carrizo_hw16.c. Its
 * display is DCE 8, whose first controller sits at Evergreen's offsets,
 * so eg_scanout.c's flips apply unchanged. Its graphics CP is GFX7:
 * vi_cp.c brings it up with Mullins' microcode, gpu_init, start of stream
 * and GCN 2 shaders when the identity bit is set, and d3d_vi.c draws
 * Carrizo's streams through it.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/engine_abi.h"

/* runtime.asm, ati family only. */
extern unsigned short __far __pascal V9xPciReadViBars(
    unsigned long __far *bar0, unsigned long __far *bar5);
extern unsigned short __far __pascal V9xMiniViMap(
    unsigned long bar5, unsigned long bar0, unsigned long __far *mmio,
    unsigned long __far *ring, unsigned long __far *ring_bytes);

/* V9X_VI_MMIO_BYTES in vi_sdma.h and the mini-VDD: the register BAR. */
#define V9X_MULLINS_MMIO_BYTES 0x00040000ul
/* V9X_VI_RING_WINDOW_BYTES in vi_sdma.h: the window SDMA's ring and the
 * graphics CP's ring, shaders and vertex arena share. */
#define V9X_MULLINS_RING_WINDOW_BYTES 0x00400000ul

/* How far the engine hook got, and the BARs it read, for
 * v9x_mullins_publish_diagnostics, as Carrizo's row records them. */
static const char *v9x_mullins_engine_state = "not-run";
static unsigned long v9x_mullins_bar0;
static unsigned long v9x_mullins_bar5;
static unsigned long v9x_mullins_map_reason;
static unsigned long v9x_mullins_map_value;

static void v9x_mullins_fill_engine(unsigned long framebuffer_linear_base,
                                    unsigned long *control_linear_base,
                                    unsigned long *mapped_aperture_bytes,
                                    unsigned long *engine_type,
                                    unsigned long *engine_caps,
                                    unsigned long *gtt_linear_base,
                                    unsigned long *ring_linear_base,
                                    unsigned long *ring_bytes)
{
    unsigned long bar0 = 0ul;
    unsigned long bar5 = 0ul;
    unsigned long mmio = 0ul;
    unsigned long ring = 0ul;
    unsigned long bytes = 0ul;

    (void)framebuffer_linear_base;
    *control_linear_base = 0ul;
    *mapped_aperture_bytes = 0ul;
    *engine_type = V9X_DD_ENGINE_TYPE_NONE;
    *engine_caps = 0ul;
    *gtt_linear_base = 0ul;
    *ring_linear_base = 0ul;
    *ring_bytes = 0ul;

    if (V9xPciReadViBars(&bar0, &bar5) == 0u) {
        v9x_mullins_engine_state = "bars-refused";
        return;
    }
    v9x_mullins_bar0 = bar0;
    v9x_mullins_bar5 = bar5;
    if (V9xMiniViMap(bar5, bar0, &mmio, &ring, &bytes) == 0u ||
        mmio == 0ul || ring == 0ul || bytes == 0ul) {
        /* On a refusal the wrapper leaves the mini-VDD's reason in bytes
         * and the value it refused in ring. */
        v9x_mullins_engine_state = "map-refused";
        v9x_mullins_map_reason = bytes;
        v9x_mullins_map_value = ring;
        return;
    }
    v9x_mullins_engine_state = "engine";
    *control_linear_base = mmio;
    *mapped_aperture_bytes = V9X_MULLINS_MMIO_BYTES;
    *engine_type = V9X_DD_ENGINE_TYPE_ATI_VI;
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY |
                   V9X_DD_ENGINE_CAP_FLIP |
                   V9X_DD_ENGINE_CAP_VI_MULLINS;
    /* Direct3D through the GFX7 CP, which engines\vi_cp.c brings up with
     * Mullins' microcode, gpu_init and start of stream and GCN 2 shaders
     * (docs\decisions\2026-11-08-mullins-first-3d-draws.md): only when
     * the mini-VDD mapped the whole window, as on Carrizo. */
    if (bytes >= V9X_MULLINS_RING_WINDOW_BYTES) {
        *engine_caps |= V9X_DD_ENGINE_CAP_D3D;
    }
    *ring_linear_base = ring;
    *ring_bytes = bytes;
}

static void v9x_mullins_hex(char *text, unsigned long value)
{
    static const char digits[] = "0123456789ABCDEF";
    unsigned short index;

    for (index = 0u; index < 8u; ++index) {
        text[index] = digits[(unsigned short)((value >>
                                               ((7u - index) * 4u)) & 0xful)];
    }
    text[8] = '\0';
}

/*
 * The engine hook's outcome, for the family's V9XHW.INI publisher, which
 * runs after the hook. External because ati_hw16.c calls it for this chip;
 * a family-internal symbol, not an export, as Carrizo's is.
 */
void v9x_mullins_publish_diagnostics(v9x_hw16_write_fn write)
{
    char text[9];

    write("MullinsEngine", v9x_mullins_engine_state);
    v9x_mullins_hex(text, v9x_mullins_bar0);
    write("MullinsBar0", text);
    v9x_mullins_hex(text, v9x_mullins_bar5);
    write("MullinsBar5", text);
    v9x_mullins_hex(text, v9x_mullins_map_reason);
    write("MullinsMapReason", text);
    v9x_mullins_hex(text, v9x_mullins_map_value);
    write("MullinsMapValue", text);
}

/*
 * Not static: the per-object audit resolves this symbol by name out of the
 * link map to prove this chip's module is actually in the family image.
 */
const V9X_HW16_DEVICE v9x_cik_mullins_device = {
    0x1002u, 0x9854u,
    "AMD Radeon R3E (Mullins)",
    "1002", "9854",
    "ati-cik-unavailable-v1",
    "vbe-lfb",
    "directdraw-fill-copy",
    "hardware-vi-cp",
    0,
    v9x_mullins_fill_engine,
    0u,                     /* framebuffer_bar: BAR0 */
    0u,                     /* packed_pitch */
    0ul,                    /* map_bytes: the family's 16 MiB */
    0,                      /* engine_reserve_offset */
    0u,                     /* write_combine: Stage A */
    0,                      /* write_combine_fallback */
    /* GDI has no engine here, and the R3E read its screen at 5 MB/s under
     * vbe until the shadow (docs\decisions\2026-11-12-ati-screen-shadow.md). */
    V9X_HW16_SCREEN_SHADOW
};
