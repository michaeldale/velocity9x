#ifndef VELOCITY9X_MATROX_MILLENNIUM2_H
#define VELOCITY9X_MATROX_MILLENNIUM2_H

#include "velocity9x/backend.h"

#define V9X_PCI_VENDOR_MATROX          ((v9x_u16)0x102bu)
#define V9X_PCI_DEVICE_MILLENNIUM_II   ((v9x_u16)0x051bu)
/*
 * The original Millennium, MGA-2064W. It takes the same policy as its
 * successor - a VBE mode set, a forced scan-line pitch, a framebuffer read
 * from PCI configuration space, no register writes - so it shares this
 * backend rather than getting one of its own. The one hardware difference
 * that matters to the 16-bit side, which BAR carries the framebuffer, is data
 * in the chip's hw16 object and not policy.
 */
#define V9X_PCI_DEVICE_MILLENNIUM      ((v9x_u16)0x0519u)

v9x_status v9x_matrox_millennium2_probe(
    struct v9x_backend_state *state,
    const struct v9x_pci_identity *pci);
v9x_status v9x_matrox_millennium2_bind_framebuffer(
    struct v9x_backend_state *state,
    const struct v9x_pci_bar_resource *bar,
    v9x_u32 detected_vram_bytes,
    v9x_u32 override_vram_bytes);
v9x_status v9x_matrox_millennium2_validate_mode(
    struct v9x_backend_state *state,
    const struct v9x_mode_request *request,
    struct v9x_mode_layout *layout);
const struct v9x_backend_ops *v9x_matrox_millennium2_backend(void);

#endif
