/*
 * The SiS 6326 pure-policy backend.
 *
 * One chip, PCI 1039:6326, measured on two boards in A8U4I5 on 2026-10-04:
 * revision C3 (subsystem 63261039, EDO) and revision 0Bh (subsystem
 * 63261569, SGRAM), both 4 MiB behind a 4 MiB BAR0
 * (docs\decisions\2026-10-04-sis6326-first-survey.md). Both are this one
 * device id; the binding does not distinguish them.
 *
 * The family starts tier-0: the VBE BIOS sets modes and the CPU draws. The
 * hardware-facing half lives in src\chipsets\sis\. This half is I/O-free and
 * host-tested, as backend.h requires.
 */
#ifndef VELOCITY9X_SIS_6326_H
#define VELOCITY9X_SIS_6326_H

#include "velocity9x/backend.h"

#define V9X_PCI_VENDOR_SIS      ((v9x_u16)0x1039u)
#define V9X_PCI_DEVICE_SIS6326  ((v9x_u16)0x6326u)

v9x_status v9x_sis_6326_probe(
    struct v9x_backend_state *state,
    const struct v9x_pci_identity *pci);
v9x_status v9x_sis_6326_bind_framebuffer(
    struct v9x_backend_state *state,
    const struct v9x_pci_bar_resource *bar,
    v9x_u32 detected_vram_bytes,
    v9x_u32 override_vram_bytes);
v9x_status v9x_sis_6326_validate_mode(
    struct v9x_backend_state *state,
    const struct v9x_mode_request *request,
    struct v9x_mode_layout *layout);
const struct v9x_backend_ops *v9x_sis_6326_backend(void);

#endif
