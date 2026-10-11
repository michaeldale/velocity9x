/*
 * The 3dfx family pure-policy backend.
 *
 * One chip so far: the VSA-100 (Napalm), PCI 121A:0009, on every Voodoo4 and
 * Voodoo5 board. Measured on a retail Voodoo4 4500 AGP in A8U4I5 on
 * 2026-10-11 (docs\decisions\2026-10-11-voodoo4-stock-driver-tiles-its-
 * desktop-at-the-top-of-memory.md): one chip, 32 MiB, register BAR0 and
 * framebuffer BAR1 of 128 MiB each. The Voodoo3 (Avenger) is planned as a
 * second chip of this family (docs\plans\3dfx-voodoo3-family.md).
 *
 * C symbols use the tdfx prefix because an identifier cannot start with a
 * digit; the family id is 3dfx.
 *
 * The family starts tier-0: the VBE BIOS sets modes and the CPU draws. The
 * hardware-facing half lives in src\chipsets\tdfx\. This half is I/O-free and
 * host-tested, as backend.h requires.
 */
#ifndef VELOCITY9X_TDFX_H
#define VELOCITY9X_TDFX_H

#include "velocity9x/backend.h"

#define V9X_PCI_VENDOR_3DFX     ((v9x_u16)0x121au)
#define V9X_PCI_DEVICE_VSA100   ((v9x_u16)0x0009u)

v9x_status v9x_tdfx_probe(
    struct v9x_backend_state *state,
    const struct v9x_pci_identity *pci);
v9x_status v9x_tdfx_bind_framebuffer(
    struct v9x_backend_state *state,
    const struct v9x_pci_bar_resource *bar,
    v9x_u32 detected_vram_bytes,
    v9x_u32 override_vram_bytes);
v9x_status v9x_tdfx_validate_mode(
    struct v9x_backend_state *state,
    const struct v9x_mode_request *request,
    struct v9x_mode_layout *layout);
const struct v9x_backend_ops *v9x_tdfx_backend(void);

#endif
