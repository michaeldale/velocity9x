/*
 * The tier-0 pure-policy backend.
 *
 * Its shape is the same as every other backend's, and that is what earns it a
 * place: the registry, the family matrix and the host tests all reason about
 * backends uniformly, so the generic one has to be a backend rather than an
 * absence of one.
 *
 * The hardware-facing half of tier-0 is elsewhere - src\chipsets\generic\vbe\
 * vbe_hw16.c and the NULL-hook default in src\display16\enable16.c. This half
 * is I/O-free and host-tested, like backend.h requires.
 */
#ifndef VELOCITY9X_VBE_GENERIC_H
#define VELOCITY9X_VBE_GENERIC_H

#include "velocity9x/backend.h"

/*
 * QEMU/Bochs std-vga. The allowlist is deliberately one entry: tier-0 works on
 * far more cards than this, but the INF may only claim what has been tested.
 *
 * Other cards are reached by choosing this package through Have-Disk. That
 * route is real rather than nominal only because the 16-bit family table sets
 * pci_match_optional, so the driver does not apply an allowlist of its own on
 * top of the INF's; see hw16.h and the vbe family manifest.
 */
#define V9X_PCI_VENDOR_QEMU_BOCHS ((v9x_u16)0x1234u)
#define V9X_PCI_DEVICE_STDVGA     ((v9x_u16)0x1111u)

v9x_status v9x_vbe_generic_probe(
    struct v9x_backend_state *state,
    const struct v9x_pci_identity *pci);
v9x_status v9x_vbe_generic_bind_framebuffer(
    struct v9x_backend_state *state,
    const struct v9x_pci_bar_resource *bar,
    v9x_u32 detected_vram_bytes,
    v9x_u32 override_vram_bytes);
v9x_status v9x_vbe_generic_validate_mode(
    struct v9x_backend_state *state,
    const struct v9x_mode_request *request,
    struct v9x_mode_layout *layout);
const struct v9x_backend_ops *v9x_vbe_generic_backend(void);

#endif
