/*
 * AMD Radeon HD 5450, PCI 1002:68F9 (Evergreen "Cedar", PCIe).
 *
 * The physical target is the remote-help machine M-BXMFA8 since 2026-10-26:
 * subsystem 1028:010E, revision 00, HDMI audio at function 1 (1002:AA68),
 * 1024 MiB of VRAM on a 64-bit bus. Windows assigned a 256 MiB framebuffer
 * window at D0000000 (BAR0), a 128 KiB register window at FEAE0000 (BAR2)
 * and 256 I/O ports at D000 (BAR4); BAR0 and BAR2 are 64-bit
 * (docs\decisions\2026-10-26-cedar-register-probe.md).
 *
 * Evergreen shares nothing with the Mach64 or the Rage 128 below PCI/VGA,
 * and it has no fixed-function 2D engine. The VBE sets modes and reports
 * the framebuffer. The engine hook claims the async DMA engine for
 * DirectDraw fill and copy, whose packets ran byte-exact on this card
 * (docs\decisions\2026-10-26-cedar-dma-write-probe.md): the mini-VDD maps
 * the register window and a 64 KiB ring window at BAR0 + 240 MiB, past the
 * 16 MiB this family maps for the heap, and eng_eg.c starts the ring once
 * per mode. Without either window the chip stays tier-0.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/engine_abi.h"

/* runtime.asm, ati family only. */
extern unsigned short __far __pascal V9xPciReadEgBars(
    unsigned long __far *bar0, unsigned long __far *bar2);
extern unsigned short __far __pascal V9xMiniEgMap(
    unsigned long bar2, unsigned long bar0, unsigned long __far *mmio,
    unsigned long __far *ring, unsigned long __far *ring_bytes);

/* V9X_EG_MMIO_BYTES in eg_dma.h and the mini-VDD: the register BAR. */
#define V9X_CEDAR_MMIO_BYTES 0x00020000ul

/* How far the engine hook got, and the BARs it read, for
 * v9x_cedar_publish_diagnostics: neither refusal leaves any other trace,
 * and the first boot with the hook (M-BXMFA8 boot 4) claimed nothing. */
static const char *v9x_cedar_engine_state = "not-run";
static unsigned long v9x_cedar_bar0;
static unsigned long v9x_cedar_bar2;
static unsigned long v9x_cedar_map_reason;
static unsigned long v9x_cedar_map_value;

static void v9x_cedar_fill_engine(unsigned long framebuffer_linear_base,
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

    (void)framebuffer_linear_base;
    *control_linear_base = 0ul;
    *mapped_aperture_bytes = 0ul;
    *engine_type = V9X_DD_ENGINE_TYPE_NONE;
    *engine_caps = 0ul;
    *gtt_linear_base = 0ul;
    *ring_linear_base = 0ul;
    *ring_bytes = 0ul;

    if (V9xPciReadEgBars(&bar0, &bar2) == 0u) {
        v9x_cedar_engine_state = "bars-refused";
        return;
    }
    v9x_cedar_bar0 = bar0;
    v9x_cedar_bar2 = bar2;
    if (V9xMiniEgMap(bar2, bar0, &mmio, &ring, &bytes) == 0u ||
        mmio == 0ul || ring == 0ul || bytes == 0ul) {
        /* On a refusal the wrapper leaves the mini-VDD's reason in bytes
         * and the value it refused in ring. */
        v9x_cedar_engine_state = "map-refused";
        v9x_cedar_map_reason = bytes;
        v9x_cedar_map_value = ring;
        return;
    }
    v9x_cedar_engine_state = "claimed";
    *control_linear_base = mmio;
    *mapped_aperture_bytes = V9X_CEDAR_MMIO_BYTES;
    *engine_type = V9X_DD_ENGINE_TYPE_ATI_EVERGREEN;
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY;
    *ring_linear_base = ring;
    *ring_bytes = bytes;
}

static void v9x_cedar_hex(char *text, unsigned long value)
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
 * a family-internal symbol, not an export.
 */
void v9x_cedar_publish_diagnostics(v9x_hw16_write_fn write)
{
    char text[9];

    write("EvergreenEngine", v9x_cedar_engine_state);
    v9x_cedar_hex(text, v9x_cedar_bar0);
    write("EvergreenBar0", text);
    v9x_cedar_hex(text, v9x_cedar_bar2);
    write("EvergreenBar2", text);
    v9x_cedar_hex(text, v9x_cedar_map_reason);
    write("EvergreenMapReason", text);
    v9x_cedar_hex(text, v9x_cedar_map_value);
    write("EvergreenMapValue", text);
}

/*
 * Not static: the per-object audit resolves this symbol by name out of the
 * link map to prove this chip's module is actually in the family image.
 */
const V9X_HW16_DEVICE v9x_evergreen_cedar_device = {
    0x1002u, 0x68F9u,
    "AMD Radeon HD 5450 (Cedar)",
    "1002", "68F9",
    "ati-evergreen-unavailable-v1",
    "vbe-lfb",
    "directdraw-fill-copy",
    0,
    0,
    v9x_cedar_fill_engine
};
