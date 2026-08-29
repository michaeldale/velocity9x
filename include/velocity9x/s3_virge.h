#ifndef VELOCITY9X_S3_VIRGE_H
#define VELOCITY9X_S3_VIRGE_H

#include "velocity9x/backend.h"

#define V9X_PCI_VENDOR_S3        ((v9x_u16)0x5333u)
#define V9X_PCI_DEVICE_VIRGE_DX  ((v9x_u16)0x8a01u)
#define V9X_PCI_DEVICE_TRIO64    ((v9x_u16)0x8811u)

/*
 * Aliases of V9X_PCI_DEVICE_TRIO64: further ids the Trio64 chip module drives
 * unchanged, bound but not validated. See the Aliases section of
 * docs\specifications\family-manifest.md, and
 * docs\decisions\2026-08-29-s3-device-id-survey.md for which of these were
 * measured off an option ROM and which are documentation.
 */
#define V9X_PCI_DEVICE_TRIO32     ((v9x_u16)0x8810u)
#define V9X_PCI_DEVICE_AURORA64   ((v9x_u16)0x8812u)
#define V9X_PCI_DEVICE_TRIO32_64  ((v9x_u16)0x8813u)
#define V9X_PCI_DEVICE_TRIO64UV   ((v9x_u16)0x8814u)
#define V9X_PCI_DEVICE_TRIO64V2   ((v9x_u16)0x8901u)

v9x_status v9x_s3_virge_probe(struct v9x_backend_state *state,
                              const struct v9x_pci_identity *pci);
v9x_status v9x_s3_virge_bind_framebuffer(
    struct v9x_backend_state *state,
    const struct v9x_pci_bar_resource *bar,
    v9x_u32 detected_vram_bytes,
    v9x_u32 override_vram_bytes);
v9x_status v9x_s3_virge_validate_mode(struct v9x_backend_state *state,
                                      const struct v9x_mode_request *request,
                                      struct v9x_mode_layout *layout);
const struct v9x_backend_ops *v9x_s3_virge_backend(void);
v9x_status v9x_s3_virge_decode_clock_pll(
    v9x_u8 sr10,
    v9x_u8 sr11,
    struct v9x_clock_info *clocks);
v9x_status v9x_s3_virge_decode_memory_size(v9x_u8 cr36, v9x_u32 *bytes);

#endif
