/*
 * AMD Carrizo, PCI 1002:9874 rev 88 (Radeon R7E, GCN 3), the GPU in the
 * GX-420GI APU.
 *
 * The physical target is the remote-help machine M-XP7053 since 2026-10-28:
 * an HP board, subsystem 103C:8158. The VBE reports 48 MiB and its
 * framebuffer at E0000000 (BAR0); the video memory is a 512 MiB carve-out
 * of system RAM, the registers a 256 KiB window at FEB00000 (BAR5)
 * (docs\decisions\2026-10-28-carrizo-register-probe.md).
 *
 * Carrizo shares nothing with Evergreen below PCI/VGA except the display
 * controller's register layout, and every engine it has runs AMD microcode
 * before it does anything (docs\plans\ati-carrizo-gfx8.md). The VBE sets
 * modes and reports the framebuffer. The engine hook claims SDMA0 for
 * DirectDraw fill and copy, whose microcode and packets ran byte-exact on
 * this chip (docs\decisions\2026-10-28-carrizo-sdma-write-probe.md): the
 * mini-VDD maps the register window and a 4 MiB window at BAR0 + 240 MiB,
 * past the 16 MiB the family maps for the heap, and eng_vi.c loads the
 * microcode and starts the ring once per mode. Without either window the
 * chip stays tier-0. The BIOS lists no 8 bpp mode; the runtime scan hides
 * the family's 8 bpp baseline rows.
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
#define V9X_CARRIZO_MMIO_BYTES 0x00040000ul

/* How far the engine hook got, and the BARs it read, for
 * v9x_carrizo_publish_diagnostics: neither refusal leaves any other trace,
 * which is what Cedar's first boot with its hook taught (M-BXMFA8 boot 4). */
static const char *v9x_carrizo_engine_state = "not-run";
static unsigned long v9x_carrizo_bar0;
static unsigned long v9x_carrizo_bar5;
static unsigned long v9x_carrizo_map_reason;
static unsigned long v9x_carrizo_map_value;

static void v9x_carrizo_fill_engine(unsigned long framebuffer_linear_base,
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
        v9x_carrizo_engine_state = "bars-refused";
        return;
    }
    v9x_carrizo_bar0 = bar0;
    v9x_carrizo_bar5 = bar5;
    if (V9xMiniViMap(bar5, bar0, &mmio, &ring, &bytes) == 0u ||
        mmio == 0ul || ring == 0ul || bytes == 0ul) {
        /* On a refusal the wrapper leaves the mini-VDD's reason in bytes
         * and the value it refused in ring. */
        v9x_carrizo_engine_state = "map-refused";
        v9x_carrizo_map_reason = bytes;
        v9x_carrizo_map_value = ring;
        return;
    }
    v9x_carrizo_engine_state = "claimed";
    *control_linear_base = mmio;
    *mapped_aperture_bytes = V9X_CARRIZO_MMIO_BYTES;
    *engine_type = V9X_DD_ENGINE_TYPE_ATI_VI;
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY;
    *ring_linear_base = ring;
    *ring_bytes = bytes;
}

static void v9x_carrizo_hex(char *text, unsigned long value)
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
 * a family-internal symbol, not an export, as Cedar's is.
 */
void v9x_carrizo_publish_diagnostics(v9x_hw16_write_fn write)
{
    char text[9];

    write("CarrizoEngine", v9x_carrizo_engine_state);
    v9x_carrizo_hex(text, v9x_carrizo_bar0);
    write("CarrizoBar0", text);
    v9x_carrizo_hex(text, v9x_carrizo_bar5);
    write("CarrizoBar5", text);
    v9x_carrizo_hex(text, v9x_carrizo_map_reason);
    write("CarrizoMapReason", text);
    v9x_carrizo_hex(text, v9x_carrizo_map_value);
    write("CarrizoMapValue", text);
}

/*
 * Not static: the per-object audit resolves this symbol by name out of the
 * link map to prove this chip's module is actually in the family image.
 */
const V9X_HW16_DEVICE v9x_vi_carrizo_device = {
    0x1002u, 0x9874u,
    "AMD Radeon R7E (Carrizo)",
    "1002", "9874",
    "ati-vi-unavailable-v1",
    "vbe-lfb",
    "directdraw-fill-copy",
    "not-advertised",
    0,
    v9x_carrizo_fill_engine
};
