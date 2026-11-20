/*
 * ATI Radeon 9250 and 9200SE, PCI 1002:5960 and 1002:5964 (RV280), and
 * the 9200's 5961 and 5962, claimed unrun.
 *
 * The physical target is the remote-help machine M-HNBJNV since
 * 2026-11-15: a PowerColor Radeon 9250 (subsystem 148C:2094) on a VIA
 * PM133, BAR0 (frame buffer, 128 MiB) at D0000000 and BAR2 (registers,
 * 64 KiB) at E5000000. The 9200SE (ECS, 1019:4C43) was surveyed in the
 * same machine (docs\decisions\2026-11-13-rv280-radeon-9200se-9250-
 * survey.md).
 *
 * docs\plans\ati-rv280-radeon-9250.md. The VBE sets modes and reports the
 * frame buffer; GDI draws into the screen shadow, as the vbe package ran
 * this card on M-HNBJNV. The engine hook claims the legacy Radeon 2D engine
 * for DirectDraw fill and copy: the RS690's builder values were measured
 * byte-exact on this card at 8, 16 and 32 bpp with the CP's queues off, as
 * VBE leaves them (docs\decisions\2026-11-15-rv280-2d-write-probe.md).
 * eng_rv280.c writes the init list once per mode.
 *
 * BAR2 is read by V9xPciReadAtiMmioBar, the Rage 128's read, which has no
 * frame-buffer alignment rule; the RS690's map in the mini-VDD then requires
 * 64 KiB alignment and a CONFIG_MEMSIZE of at most 512 MiB through the
 * window (128 MiB here).
 *
 * The card's second PCI function (1002:5940 / 5D44, class 0380) is not
 * claimed: it is the second head, which this driver cannot drive.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/engine_abi.h"

/* runtime.asm, ati family only. */
extern unsigned short __far __pascal V9xPciReadAtiMmioBar(
    unsigned long __far *base);
extern unsigned short __far __pascal V9xMiniRs690MmioMap(
    unsigned long bar2, unsigned long __far *linear);

/* V9X_RS690_MMIO_BYTES in the mini-VDD: the register BAR. */
#define V9X_RV280_MMIO_BYTES 0x00010000ul

static void v9x_rv280_fill_engine(unsigned long framebuffer_linear_base,
                                  unsigned long *control_linear_base,
                                  unsigned long *mapped_aperture_bytes,
                                  unsigned long *engine_type,
                                  unsigned long *engine_caps,
                                  unsigned long *gtt_linear_base,
                                  unsigned long *ring_linear_base,
                                  unsigned long *ring_bytes)
{
    unsigned long bar2 = 0ul;
    unsigned long linear = 0ul;

    (void)framebuffer_linear_base;
    *control_linear_base = 0ul;
    *mapped_aperture_bytes = 0ul;
    *engine_type = V9X_DD_ENGINE_TYPE_NONE;
    *engine_caps = 0ul;
    *gtt_linear_base = 0ul;
    *ring_linear_base = 0ul;
    *ring_bytes = 0ul;

    if (V9xPciReadAtiMmioBar(&bar2) == 0u) {
        return;
    }
    if (V9xMiniRs690MmioMap(bar2, &linear) == 0u || linear == 0ul) {
        return;
    }
    *control_linear_base = linear;
    *mapped_aperture_bytes = V9X_RV280_MMIO_BYTES;
    *engine_type = V9X_DD_ENGINE_TYPE_ATI_RV280;
    /* FLIP: the legacy CRTC's CRTC_OFFSET, where the register probe read
     * the VBE desktop (rv280_scanout.c). D3D: the R200 3D engine through
     * the same window (d3d_rv280.c). */
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY |
                   V9X_DD_ENGINE_CAP_FLIP |
                   V9X_DD_ENGINE_CAP_D3D;
}

/* The row's tail, shared by the chip and its alias: everything after the
 * PCI identity. */
#define V9X_RV280_TAIL \
    "ati-rv280-unavailable-v1", \
    "vbe-lfb", \
    "directdraw-fill-copy", \
    "hardware-rv280-pio", \
    0,                      /* enable_aperture */ \
    v9x_rv280_fill_engine, \
    0u,                     /* framebuffer_bar: BAR0 */ \
    0u,                     /* packed_pitch */ \
    0ul,                    /* map_bytes: the VBE's size */ \
    0,                      /* engine_reserve_offset */ \
    V9X_HW16_WC_MAPPING, \
    0,                      /* write_combine_fallback */ \
    V9X_HW16_SCREEN_SHADOW

/*
 * Not static: the per-object audit resolves these symbols by name out of the
 * link map to prove this chip's module is actually in the family image.
 */
const V9X_HW16_DEVICE v9x_rv280_9250_device = {
    0x1002u, 0x5960u,
    "ATI Radeon 9250 (RV280)",
    "1002", "5960",
    V9X_RV280_TAIL
};

const V9X_HW16_DEVICE v9x_rv280_9200se_device = {
    0x1002u, 0x5964u,
    "ATI Radeon 9200SE (RV280)",
    "1002", "5964",
    V9X_RV280_TAIL
};

/*
 * The 9200's two ids, which Linux's drm_pciids.h binds as CHIP_RV280 with
 * the 5960's flags. RV280 attaches to AGP and PCI natively, with no bridge.
 * Not surveyed and not run
 * (docs\decisions\2026-11-20-bus-siblings-claimed-unrun.md).
 */
const V9X_HW16_DEVICE v9x_rv280_9200_5961_device = {
    0x1002u, 0x5961u,
    "ATI Radeon 9200 (RV280 5961)",
    "1002", "5961",
    V9X_RV280_TAIL
};

const V9X_HW16_DEVICE v9x_rv280_9200_5962_device = {
    0x1002u, 0x5962u,
    "ATI Radeon 9200 (RV280 5962)",
    "1002", "5962",
    V9X_RV280_TAIL
};
