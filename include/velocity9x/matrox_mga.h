/*
 * The Matrox MGA pure-policy backend, for the matrox family.
 *
 * One chip, the original Millennium's MGA-2064W, PCI 102B:0519, with a TI
 * TVP3026 RAMDAC. The card in A8U4I5 since 2026-10-09 is revision 01,
 * subsystem 0000, and runs Microsoft's inbox MGAPDX64 driver as the
 * baseline. The same part was characterised on the BringupKit host on
 * 2026-09-10/11 (docs\decisions\2026-09-11-the-2064w-aperture-opens-with-
 * mgamode.md): framebuffer in BAR1, 16 KiB control aperture in BAR0, 8 MiB
 * on that card.
 *
 * The family starts tier-0: the VBE BIOS sets modes and the CPU draws. The
 * hardware-facing half lives in src\chipsets\matrox\. This half is I/O-free
 * and host-tested, as backend.h requires.
 *
 * The Millennium II (051B) stays in the guarded matrox-m2 family, which has
 * its own backend; the registry binds each id to one family only.
 */
#ifndef VELOCITY9X_MATROX_MGA_H
#define VELOCITY9X_MATROX_MGA_H

#include "velocity9x/backend.h"

#define V9X_PCI_VENDOR_MATROX_MGA   ((v9x_u16)0x102bu)
#define V9X_PCI_DEVICE_MGA2064W     ((v9x_u16)0x0519u)

v9x_status v9x_matrox_mga_probe(
    struct v9x_backend_state *state,
    const struct v9x_pci_identity *pci);
v9x_status v9x_matrox_mga_bind_framebuffer(
    struct v9x_backend_state *state,
    const struct v9x_pci_bar_resource *bar,
    v9x_u32 detected_vram_bytes,
    v9x_u32 override_vram_bytes);
v9x_status v9x_matrox_mga_validate_mode(
    struct v9x_backend_state *state,
    const struct v9x_mode_request *request,
    struct v9x_mode_layout *layout);
const struct v9x_backend_ops *v9x_matrox_mga_backend(void);

#endif
