/*
 * ATI Rage Mobility-M AGP (Mach64 "LM"), PCI 1002:4C4D.
 *
 * The physical target: a Gateway Solo 2150, subsystem 107B:2150, revision 0x64,
 * BIOS "ATI MACH64 SDRAM BIOS 4.216", part MACH64LMPCIMTSDU. Its display is a
 * fixed 1024x768 LG LP141XA panel with no EDID - the panel identity was decoded
 * out of the captured video BIOS rather than read from the monitor.
 *
 * Four values are known in advance and are worth asserting the first time this
 * chip is probed, because each proves a different register window is live:
 *
 *     CONFIG_CHIP_ID   low word  == 0x4C4D   ('LM')
 *     HORZ_PANEL_SIZE            == 127      (1024 = (127 + 1) * 8)
 *     VERT_PANEL_SIZE            == 767
 *     CFG_MEM_TYPE_T             == 6        (measured twice on 2026-09-27)
 *
 * Code 6 decodes as 32-bit SGRAM at 2:1, contradicting the generic "SDRAM"
 * BIOS wording.  Keep block write disabled and resolve that pre-flight
 * contradiction before either hook can issue hardware writes.
 *
 * The aperture hook is NULL: the VBE sets modes and reports the framebuffer.
 * If it is filled in, note that this part is >= 264VTB, so it decodes video
 * memory with the four-bit CTL_MEM_SIZEB table, not the three-bit CTL_MEM_SIZE
 * one its VT2 sibling uses - the two disagree for every code >= 2, and code 3
 * means 4 MiB on a VT and 2 MiB here.
 * See docs\decisions\2026-08-16-ati-mach64-hardware-audit.md.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/engine_abi.h"

/* runtime.asm, ati family only. */
extern unsigned short __far __pascal V9xPciReadAtiMmioBar(
    unsigned long __far *base);
extern unsigned short __far __pascal V9xMiniAtiMmioMap(
    unsigned long bar2, unsigned long __far *linear);

/* V9X_ATI_MMIO_BYTES in the mini-VDD: the register file eng_mach64.c and
 * d3d_mach64.c address, block 0 at +400h. */
#define V9X_MOBILITY_MMIO_BYTES 0x00001000ul

/*
 * The Mach64 engine: BAR2 mapped by the mini-VDD, which also checks that
 * CONFIG_CHIP_ID names a part it knows before handing the window over;
 * eng_mach64.c then checks it names a Rage Pro-class part.
 *
 * Claims D3D and nothing else. The 2D fill still reaches the engine by
 * type, because the HAL routes DirectDraw colour and depth fills by
 * engine_type; it is the Phase 2 fill/clear stream. Screen copy declines in
 * eng_mach64.c, so no copy claim is made.
 *
 * VT2 never gets this: it has no Rage setup engine, and its entry keeps a
 * NULL hook.
 */
static void v9x_mobility_fill_engine(unsigned long framebuffer_linear_base,
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
    if (V9xMiniAtiMmioMap(bar2, &linear) == 0u || linear == 0ul) {
        return;
    }
    *control_linear_base = linear;
    *mapped_aperture_bytes = V9X_MOBILITY_MMIO_BYTES;
    *engine_type = V9X_DD_ENGINE_TYPE_ATI_MACH64;
    *engine_caps = V9X_DD_ENGINE_CAP_D3D;
}

/*
 * The Rage XL PCI, A8U4I5 (2026-10-04): the Mobility-M's engine, and the
 * Rage IIC's flips through CRTC_OFF_PITCH and CRTC_VLINE (m64_scanout.c),
 * the same Mach64 CRTC registers in the same block-0 window, watched on
 * its CRT. Without them every flip was declined and DirectDraw copied the
 * back buffer to the front unsynced (FlipDeclined=61145 in 3DMark 99).
 * Not the Mobility-M: a panel CRTC, not measured.
 */
static void v9x_rage_xl_fill_engine(unsigned long framebuffer_linear_base,
                                    unsigned long *control_linear_base,
                                    unsigned long *mapped_aperture_bytes,
                                    unsigned long *engine_type,
                                    unsigned long *engine_caps,
                                    unsigned long *gtt_linear_base,
                                    unsigned long *ring_linear_base,
                                    unsigned long *ring_bytes)
{
    v9x_mobility_fill_engine(framebuffer_linear_base, control_linear_base,
                             mapped_aperture_bytes, engine_type, engine_caps,
                             gtt_linear_base, ring_linear_base, ring_bytes);
    if (*engine_type == V9X_DD_ENGINE_TYPE_ATI_MACH64) {
        *engine_caps |= V9X_DD_ENGINE_CAP_FLIP | V9X_DD_ENGINE_CAP_VBLANK;
    }
}

/* Not static: resolved by name in the link map by the per-object audit. */
const V9X_HW16_DEVICE v9x_rage_mobility_device = {
    0x1002u, 0x4c4du,
    "ATI Rage Mobility-M AGP",
    "1002", "4C4D",
    "ati-mach64-unavailable-v1",
    "vbe-lfb",
    "directdraw-fill",
    "hardware-mach64",
    0,
    v9x_mobility_fill_engine,
    0
};

/*
 * The Mobility-M's aliases: the rest of the Rage Pro class - Rage Pro, LT
 * Pro, XL, XC and the other Mobility parts - which share its setup engine,
 * register window and VTB+ FIFO, so this entry's hook drives them unchanged.
 * Bound at Michael's request (2026-10-03) from ATI's MACXW4 INF list; none
 * has run anywhere. eng_mach64.c drives the engine only once CONFIG_CHIP_ID
 * names the Rage Pro class (v9x_m64_chip_class).
 */
#define V9X_MOBILITY_ALIAS_TAIL \
    "ati-mach64-unavailable-v1", \
    "vbe-lfb", \
    "directdraw-fill", \
    "hardware-mach64", \
    0, \
    v9x_mobility_fill_engine, \
    0

const V9X_HW16_DEVICE v9x_ati_4742_device = {
    0x1002u, 0x4742u,
    "ATI 3D Rage Pro AGP 2X",
    "1002", "4742",
    V9X_MOBILITY_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_ati_4744_device = {
    0x1002u, 0x4744u,
    "ATI 3D Rage Pro AGP",
    "1002", "4744",
    V9X_MOBILITY_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_ati_4747_device = {
    0x1002u, 0x4747u,
    "ATI 3D Rage Pro",
    "1002", "4747",
    V9X_MOBILITY_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_ati_4749_device = {
    0x1002u, 0x4749u,
    "ATI 3D Rage Pro PCI",
    "1002", "4749",
    V9X_MOBILITY_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_ati_4750_device = {
    0x1002u, 0x4750u,
    "ATI 3D Rage Pro PCI",
    "1002", "4750",
    V9X_MOBILITY_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_ati_4751_device = {
    0x1002u, 0x4751u,
    "ATI 3D Rage Pro PCI",
    "1002", "4751",
    V9X_MOBILITY_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_ati_474c_device = {
    0x1002u, 0x474cu,
    "ATI 3D Rage XC PCI-66",
    "1002", "474C",
    V9X_MOBILITY_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_ati_474d_device = {
    0x1002u, 0x474du,
    "ATI 3D Rage XL AGP",
    "1002", "474D",
    V9X_MOBILITY_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_ati_474e_device = {
    0x1002u, 0x474eu,
    "ATI 3D Rage XC AGP",
    "1002", "474E",
    V9X_MOBILITY_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_ati_474f_device = {
    0x1002u, 0x474fu,
    "ATI 3D Rage XL PCI-66",
    "1002", "474F",
    V9X_MOBILITY_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_ati_4752_device = {
    0x1002u, 0x4752u,
    "ATI 3D Rage XL PCI",
    "1002", "4752",
    "ati-mach64-unavailable-v1",
    "vbe-lfb",
    "directdraw-fill",
    "hardware-mach64",
    0,
    v9x_rage_xl_fill_engine,
    0
};

const V9X_HW16_DEVICE v9x_ati_4753_device = {
    0x1002u, 0x4753u,
    "ATI 3D Rage XC PCI",
    "1002", "4753",
    V9X_MOBILITY_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_ati_4c42_device = {
    0x1002u, 0x4c42u,
    "ATI 3D Rage LT Pro AGP 2X",
    "1002", "4C42",
    V9X_MOBILITY_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_ati_4c44_device = {
    0x1002u, 0x4c44u,
    "ATI 3D Rage LT Pro AGP",
    "1002", "4C44",
    V9X_MOBILITY_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_ati_4c49_device = {
    0x1002u, 0x4c49u,
    "ATI 3D Rage LT Pro PCI",
    "1002", "4C49",
    V9X_MOBILITY_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_ati_4c50_device = {
    0x1002u, 0x4c50u,
    "ATI 3D Rage LT Pro PCI",
    "1002", "4C50",
    V9X_MOBILITY_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_ati_4c51_device = {
    0x1002u, 0x4c51u,
    "ATI 3D Rage LT Pro PCI",
    "1002", "4C51",
    V9X_MOBILITY_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_ati_4c4e_device = {
    0x1002u, 0x4c4eu,
    "ATI 3D Rage Mobility-L AGP",
    "1002", "4C4E",
    V9X_MOBILITY_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_ati_4c52_device = {
    0x1002u, 0x4c52u,
    "ATI 3D Rage Mobility P/M PCI",
    "1002", "4C52",
    V9X_MOBILITY_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_ati_4c53_device = {
    0x1002u, 0x4c53u,
    "ATI 3D Rage Mobility-L PCI",
    "1002", "4C53",
    V9X_MOBILITY_ALIAS_TAIL
};
