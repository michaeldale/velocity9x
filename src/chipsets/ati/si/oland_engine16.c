/*
 * The Oland row's engine hook and diagnostics, in LEAFCODE (leaf16.h).
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
