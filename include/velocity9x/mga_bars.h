/*
 * Which BAR holds a Matrox chip's control aperture: the decision half.
 *
 * MGABASE1, the 16 KiB control aperture, and MGABASE2, the framebuffer, sit in
 * BAR0 and BAR1, in an order that is not a property of the device id. The
 * MGA-2064W has control in BAR0 (measured,
 * docs\decisions\2026-09-09-millennium-2064w-bar-ordering.md), the MGA-2164W
 * in BAR1 (its specification), and 102B:051A is two chips: the MGA-1064SG,
 * revision 00h, with control in BAR0 (MGA-1064SG specification 4-3, 4-11,
 * 4-12), and the MGA-1164SG, revision 03h, with control in BAR1 (86Box's
 * model, and a VOGONS survey,
 * docs\probe\references\matrox-mystique-pci-vogons-vgasurv-2026-10-17-d746481.ini).
 *
 * What does not vary is the prefetchable bit. Every source above marks the
 * framebuffer prefetchable and the control aperture not, because a read of
 * STATUS or FIFOSTATUS is an event and a speculative or merged read of it is
 * wrong. The bit is wired by the chip, so no BIOS moves it. So the rule is
 * one for every chip: the control aperture is the one of BAR0 and BAR1 that
 * is not prefetchable (docs\plans\matrox-mystique.md).
 *
 * Except under emulation. 86Box returns 00h for the low byte of both BARs on
 * every Millennium and Mystique (vid_mga.c, PCI config read), so neither is
 * prefetchable there, and on 2026-10-18 the rule alone left the
 * Win98SE-Millennium guest with no engine. When neither BAR is prefetchable,
 * and only then, the caller's per-chip fallback names the control BAR. Both
 * prefetchable is refused whatever the fallback: no source shows it.
 *
 * Pure policy, no I/O: runtime.asm reads the two raw dwords and interprets
 * nothing, the way src\common\mtrr.c takes what the mini-VDD read.
 */
#ifndef VELOCITY9X_MGA_BARS_H
#define VELOCITY9X_MGA_BARS_H

#include "velocity9x/types.h"

/* PCI BAR low bits (PCI 2.1 6.2.5.1): bit 0 I/O space, bits 2:1 the memory
 * type (00 anywhere in 32-bit space), bit 3 prefetchable. */
#define V9X_MGA_BAR_IO_SPACE     ((v9x_u32)0x00000001ul)
#define V9X_MGA_BAR_TYPE_MASK    ((v9x_u32)0x00000006ul)
#define V9X_MGA_BAR_PREFETCHABLE ((v9x_u32)0x00000008ul)
#define V9X_MGA_BAR_ADDRESS_MASK ((v9x_u32)0xFFFFFFF0ul)

/* MGABASE1 decodes 16 KiB and is aligned to it (mgabase1 <31:14>, 4-11). */
#define V9X_MGA_CONTROL_APERTURE_BYTES ((v9x_u32)0x00004000ul)

/* fallback_control_index when the chip has no stated order to fall back on. */
#define V9X_MGA_NO_FALLBACK_BAR ((v9x_u16)0xFFFFu)

/*
 * Given the raw BAR0 and BAR1 dwords, returns V9X_TRUE and sets
 * *control_index (0 or 1) and *control_base when both are 32-bit memory
 * BARs and the control one - the non-prefetchable one, or
 * fallback_control_index when neither is prefetchable - has a base that is a
 * plausible control aperture. Anything else - an I/O BAR, a 64-bit BAR, both
 * prefetchable, neither with no fallback, an unassigned or misaligned base -
 * returns V9X_FALSE with both outputs zero, and the chip stays on the CPU
 * path.
 */
v9x_u16 v9x_matrox_mga_select_control_bar(v9x_u32 bar0, v9x_u32 bar1,
                                          v9x_u16 fallback_control_index,
                                          v9x_u16 *control_index,
                                          v9x_u32 *control_base);

#endif
