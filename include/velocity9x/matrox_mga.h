/*
 * The Matrox MGA pure-policy backend, for the matrox family.
 *
 * Two chips of one design, sharing the 2D core:
 *
 *   - the original Millennium's MGA-2064W, PCI 102B:0519, TI TVP3026
 *     RAMDAC. The card in A8U4I5 since 2026-10-09 is revision 01, subsystem
 *     0000, 8 MiB (docs\decisions\2026-10-09-mga2064w-tier0-first-boot.md);
 *   - the Millennium II's MGA-2164W, PCI 102B:051B. Its physical sample was
 *     subsystem 1200102B with 8 MiB WRAM
 *     (docs\specifications\matrox-millennium2-bringup.md).
 *
 * The one difference the driver acts on - which BAR holds the framebuffer
 * and which the control aperture - is hw16 data per chip, not policy here.
 *
 * The hardware-facing half lives in src\chipsets\matrox\. This half is
 * I/O-free and host-tested, as backend.h requires.
 */
#ifndef VELOCITY9X_MATROX_MGA_H
#define VELOCITY9X_MATROX_MGA_H

#include "velocity9x/backend.h"

#define V9X_PCI_VENDOR_MATROX_MGA   ((v9x_u16)0x102bu)
#define V9X_PCI_DEVICE_MGA2064W     ((v9x_u16)0x0519u)
#define V9X_PCI_DEVICE_MGA2164W     ((v9x_u16)0x051bu)

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
