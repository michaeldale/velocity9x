/*
 * The ATI Mach64 / Rage pure-policy backend.
 *
 * Two chips, one family, dispatched at run time by PCI id - the shape the S3
 * family proved at phase 8. They are three years apart deliberately: the
 * Mach64 VT2 is what 86Box can emulate, and the Rage Mobility-M is the
 * physical target. One binary serves both because the Mach64 GUI register set
 * is common across GX, CT, VT and Rage, so what the emulated part validates
 * about the command stream is true of the real one.
 *
 * What it does NOT validate is anything timing- or panel-related; see
 * docs\decisions\2026-08-16-ati-mach64-hardware-audit.md.
 *
 * The hardware-facing half lives in src\chipsets\ati\. This half is I/O-free
 * and host-tested, as backend.h requires.
 */
#ifndef VELOCITY9X_ATI_MACH64_H
#define VELOCITY9X_ATI_MACH64_H

#include "velocity9x/backend.h"

#define V9X_PCI_VENDOR_ATI ((v9x_u16)0x1002u)

/*
 * Device ids are an ASCII pair, which is also what CONFIG_CHIP_ID's low word
 * returns - 'VT' and 'LM'. That makes the chip id register a free cross-check
 * that the driver is talking to the register window it thinks it is.
 *
 * 0x4750 (Rage Pro, 'GP') is deliberately absent. The restructure plan named
 * it, but nobody here has one to test, and claiming an untested id is exactly
 * what the tier-0 family's Have-Disk reasoning forbids.
 */
#define V9X_PCI_DEVICE_MACH64_VT2       ((v9x_u16)0x5654u)
#define V9X_PCI_DEVICE_RAGE_MOBILITY_M  ((v9x_u16)0x4c4du)

v9x_status v9x_ati_mach64_probe(
    struct v9x_backend_state *state,
    const struct v9x_pci_identity *pci);
v9x_status v9x_ati_mach64_bind_framebuffer(
    struct v9x_backend_state *state,
    const struct v9x_pci_bar_resource *bar,
    v9x_u32 detected_vram_bytes,
    v9x_u32 override_vram_bytes);
v9x_status v9x_ati_mach64_validate_mode(
    struct v9x_backend_state *state,
    const struct v9x_mode_request *request,
    struct v9x_mode_layout *layout);
const struct v9x_backend_ops *v9x_ati_mach64_backend(void);

#endif
