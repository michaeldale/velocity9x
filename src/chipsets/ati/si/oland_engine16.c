/*
 * The Oland row's engine hook and diagnostics, and the FirePro rows that
 * share them (at the end), in LEAFCODE (leaf16.h).
 *
 * The ati family's _TEXT reached its 57,344-byte budget with the Oland
 * row in it (58,355 bytes, 2026-11-12), so the row's code lives here and
 * oland_hw16.c keeps only the device table and two thunks in _TEXT. The
 * device table's hook and the publisher's write function are near
 * pointers, which cannot be called across a segment, so neither is called
 * here: the hook's results come back through its far data pointers, and
 * the diagnostics come back as key and value strings for oland_hw16.c to
 * write. This module calls only runtime.asm's FAR entries. See
 * oland_hw16.c for what the hook does and what is unmeasured.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/engine_abi.h"
#include "velocity9x/leaf16.h"

/* runtime.asm, ati family only. */
extern unsigned short __far __pascal V9xPciReadSiBars(
    unsigned long __far *bar0, unsigned long __far *bar2);
extern unsigned short __far __pascal V9xMiniViMap(
    unsigned long bar5, unsigned long bar0, unsigned long __far *mmio,
    unsigned long __far *ring, unsigned long __far *ring_bytes);

/* V9X_SI_MMIO_BYTES in si_dma.h and the mini-VDD: the register BAR. */
#define V9X_OLAND_MMIO_BYTES 0x00040000ul
/* V9X_VI_RING_WINDOW_BYTES in vi_sdma.h: the whole window the HAL's CP
 * uses beside the DMA ring (vi_cp.c). */
#define V9X_OLAND_RING_WINDOW_BYTES 0x00400000ul

/* The diagnostics: how far the hook got, then four hex values. */
#define V9X_OLAND_DIAGNOSTIC_COUNT 5u
#define V9X_OLAND_HEX_DIGITS       8u

static const char *v9x_oland_engine_state = "not-run";
static unsigned long v9x_oland_values[V9X_OLAND_DIAGNOSTIC_COUNT - 1u];
static char v9x_oland_text[V9X_OLAND_HEX_DIGITS + 1u];

/* Indexes into v9x_oland_values. */
#define V9X_OLAND_BAR0          0u
#define V9X_OLAND_BAR2          1u
#define V9X_OLAND_MAP_REASON    2u
#define V9X_OLAND_MAP_VALUE     3u

void V9X_LEAF_FAR v9x_oland_fill_engine_leaf(
    unsigned long *control_linear_base,
    unsigned long *mapped_aperture_bytes,
    unsigned long *engine_type,
    unsigned long *engine_caps,
    unsigned long *gtt_linear_base,
    unsigned long *ring_linear_base,
    unsigned long *ring_bytes)
{
    unsigned long bar0 = 0ul;
    unsigned long bar2 = 0ul;
    unsigned long mmio = 0ul;
    unsigned long ring = 0ul;
    unsigned long bytes = 0ul;

    *control_linear_base = 0ul;
    *mapped_aperture_bytes = 0ul;
    *engine_type = V9X_DD_ENGINE_TYPE_NONE;
    *engine_caps = 0ul;
    *gtt_linear_base = 0ul;
    *ring_linear_base = 0ul;
    *ring_bytes = 0ul;

    if (V9xPciReadSiBars(&bar0, &bar2) == 0u) {
        v9x_oland_engine_state = "bars-refused";
        return;
    }
    v9x_oland_values[V9X_OLAND_BAR0] = bar0;
    v9x_oland_values[V9X_OLAND_BAR2] = bar2;
    if (V9xMiniViMap(bar2, bar0, &mmio, &ring, &bytes) == 0u ||
        mmio == 0ul || ring == 0ul || bytes == 0ul) {
        /* On a refusal the wrapper leaves the mini-VDD's reason in bytes
         * and the value it refused in ring. */
        v9x_oland_engine_state = "map-refused";
        v9x_oland_values[V9X_OLAND_MAP_REASON] = bytes;
        v9x_oland_values[V9X_OLAND_MAP_VALUE] = ring;
        return;
    }
    v9x_oland_engine_state = "engine";
    *control_linear_base = mmio;
    *mapped_aperture_bytes = V9X_OLAND_MMIO_BYTES;
    *engine_type = V9X_DD_ENGINE_TYPE_ATI_VI;
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY |
                   V9X_DD_ENGINE_CAP_FLIP |
                   V9X_DD_ENGINE_CAP_VI_OLAND;
    /* Direct3D through the GFX6 CP, which engines\vi_cp.c brings up as
     * V9XSICP did on W9QI0DAD, with SI's microcode, gpu_init, start of
     * stream and shaders (docs\decisions\2026-11-16-oland-texture-depth.md
     * and the records before it): only when the mini-VDD mapped the whole
     * window, as on Carrizo and Mullins. DDI 8 with it: pixel shaders on
     * the translator V9XSICP /ps11 drew right (step 5 of docs\plans\
     * ati-gcn-ps11.md). */
    if (bytes >= V9X_OLAND_RING_WINDOW_BYTES) {
        *engine_caps |= V9X_DD_ENGINE_CAP_D3D | V9X_DD_ENGINE_CAP_D3D_DDI8;
    }
    *ring_linear_base = ring;
    *ring_bytes = bytes;
}

/*
 * Diagnostic index's key and value, or 0 past the last. A hex value is
 * formatted into one static buffer, which the caller writes before it asks
 * for the next.
 */
const char * V9X_LEAF_FAR v9x_oland_diagnostic(unsigned short index,
                                               const char **key)
{
    static const char * const keys[V9X_OLAND_DIAGNOSTIC_COUNT] = {
        "OlandEngine", "OlandBar0", "OlandBar2", "OlandMapReason",
        "OlandMapValue"
    };
    static const char digits[] = "0123456789ABCDEF";
    unsigned long value;
    unsigned short digit;

    if (index >= V9X_OLAND_DIAGNOSTIC_COUNT) {
        return 0;
    }
    *key = keys[index];
    if (index == 0u) {
        return v9x_oland_engine_state;
    }
    value = v9x_oland_values[index - 1u];
    for (digit = 0u; digit < V9X_OLAND_HEX_DIGITS; ++digit) {
        v9x_oland_text[digit] =
            digits[(unsigned short)((value >> ((7u - digit) * 4u)) & 0xful)];
    }
    v9x_oland_text[V9X_OLAND_HEX_DIGITS] = '\0';
    return v9x_oland_text;
}

/*
 * Three more GCN 1 chips on the Oland's path, claimed unrun from DOS
 * surveys of their owner's cards (GitHub issue 15): FirePro W4100 (Cape
 * Verde, 1002:682C, 2 GiB), W7000 (Pitcairn, 1002:6808, 4 GiB) and W9000
 * (Tahiti, 1002:6780, 6 GiB). Each reads as the Oland's BAR layout: BAR0
 * a 64-bit prefetchable window at D0000000h or E0000000h, 256 MiB
 * aligned, BAR2 the 64-bit register window. The hook is the Oland's, so
 * they carry its engine identity bit; the HAL tells them apart by the PCI
 * device id and brings each up with its own microcode and a gpu_init list
 * built from what the card reads, two shader engines on Pitcairn and
 * Tahiti (si_gpu_init.c). Their BAR0 sizes, VRAM placement and fused-off
 * units were not read: docs\decisions\
 * 2026-11-28-si-cape-verde-pitcairn-tahiti-from-surveys.md.
 *
 * Here, not in oland_hw16.c, because rows are data and the family's _TEXT
 * had no room for three more (121 bytes over with them there): the PCI
 * scan reaches a row through a far pointer, so it may live in LEAFCODE.
 * The hook a row names is a near pointer to oland_hw16.c's thunk in
 * _TEXT, which enable16.c, also in _TEXT, calls near; nothing here calls
 * it.
 */
extern void v9x_oland_fill_engine(unsigned long framebuffer_linear_base,
                                  unsigned long *control_linear_base,
                                  unsigned long *mapped_aperture_bytes,
                                  unsigned long *engine_type,
                                  unsigned long *engine_caps,
                                  unsigned long *gtt_linear_base,
                                  unsigned long *ring_linear_base,
                                  unsigned long *ring_bytes);

/* The Oland row's 128 MiB of BAR0 (oland_hw16.c says why), which assumes
 * the same 256 MiB BAR0 these surveys did not size. */
#define V9X_SI_FIREPRO_MAP_BYTES 0x08000000ul

#define V9X_SI_FIREPRO_TAIL \
    "ati-si-unavailable-v1", \
    "vbe-lfb", \
    "directdraw-fill-copy", \
    "not-advertised", \
    0, \
    v9x_oland_fill_engine, \
    0u, \
    0u, \
    V9X_SI_FIREPRO_MAP_BYTES, \
    0, \
    V9X_HW16_WC_BAR256, \
    0, \
    V9X_HW16_SCREEN_SHADOW

const V9X_HW16_DEVICE v9x_si_verde_w4100_device = {
    0x1002u, 0x682cu,
    "AMD FirePro W4100 (Cape Verde)",
    "1002", "682C",
    V9X_SI_FIREPRO_TAIL
};

const V9X_HW16_DEVICE v9x_si_pitcairn_w7000_device = {
    0x1002u, 0x6808u,
    "AMD FirePro W7000 (Pitcairn)",
    "1002", "6808",
    V9X_SI_FIREPRO_TAIL
};

const V9X_HW16_DEVICE v9x_si_tahiti_w9000_device = {
    0x1002u, 0x6780u,
    "AMD FirePro W9000 (Tahiti)",
    "1002", "6780",
    V9X_SI_FIREPRO_TAIL
};
