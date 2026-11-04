/*
 * The NVIDIA family pure-policy backend.
 *
 * One chip so far: the RIVA 128ZX (NV3T), PCI 12D2:0018, measured as an STB
 * Velocity 128 ZXV (subsystem 10B4:222A, revision 22, 8 MiB, AGP) on
 * M-7V636C on 2026-11-02 (docs\decisions\2026-11-02-m-7v636c-riva128-first-
 * look.md). 12D2 is the NVIDIA/SGS-Thomson joint vendor id the RIVA 128
 * carries; the TNT and later use 10DE and are a different generation.
 *
 * The family starts tier-0: the VBE BIOS sets modes and the CPU draws. The
 * hardware-facing half lives in src\chipsets\nvidia\. This half is I/O-free
 * and host-tested, as backend.h requires.
 */
#ifndef VELOCITY9X_NVIDIA_H
#define VELOCITY9X_NVIDIA_H

#include "velocity9x/backend.h"

#define V9X_PCI_VENDOR_NVIDIA_SGS   ((v9x_u16)0x12d2u)
#define V9X_PCI_DEVICE_RIVA128      ((v9x_u16)0x0018u)

v9x_status v9x_nvidia_probe(
    struct v9x_backend_state *state,
    const struct v9x_pci_identity *pci);
v9x_status v9x_nvidia_bind_framebuffer(
    struct v9x_backend_state *state,
    const struct v9x_pci_bar_resource *bar,
    v9x_u32 detected_vram_bytes,
    v9x_u32 override_vram_bytes);
v9x_status v9x_nvidia_validate_mode(
    struct v9x_backend_state *state,
    const struct v9x_mode_request *request,
    struct v9x_mode_layout *layout);
const struct v9x_backend_ops *v9x_nvidia_backend(void);

#endif
