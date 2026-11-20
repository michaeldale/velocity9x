#ifndef VELOCITY9X_RESOURCES_H
#define VELOCITY9X_RESOURCES_H

#include "velocity9x/status.h"
#include "velocity9x/leaf16.h"

#define V9X_PCI_BAR_MEMORY       ((v9x_u16)0x0001u)
#define V9X_PCI_BAR_IO           ((v9x_u16)0x0002u)
#define V9X_PCI_BAR_64BIT        ((v9x_u16)0x0004u)
#define V9X_PCI_BAR_PREFETCHABLE ((v9x_u16)0x0008u)
#define V9X_PCI_BAR_KNOWN_FLAGS  ((v9x_u16)0x000fu)

/* A decoded PCI BAR supplied by the OS-facing discovery layer. */
struct v9x_pci_bar_resource {
    v9x_u32 physical_base;
    v9x_u32 aperture_bytes;
    v9x_u16 flags;
};

/* A structurally validated framebuffer candidate. It is not yet mapped. */
struct v9x_framebuffer_binding {
    v9x_u32 physical_base;
    v9x_u32 aperture_bytes;
    v9x_u32 vram_bytes;
    v9x_u16 override_active;
};

v9x_status V9X_LEAF_FAR v9x_framebuffer_validate_binding(
    const struct v9x_pci_bar_resource *bar,
    v9x_u32 detected_vram_bytes,
    v9x_u32 override_vram_bytes,
    struct v9x_framebuffer_binding *binding);

#endif
