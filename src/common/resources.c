#include "velocity9x/resources.h"

#define V9X_U32_MAX ((v9x_u32)0xfffffffful)

static void v9x_framebuffer_clear_binding(
    struct v9x_framebuffer_binding *binding)
{
    binding->physical_base = 0ul;
    binding->aperture_bytes = 0ul;
    binding->vram_bytes = 0ul;
    binding->override_active = V9X_FALSE;
}

static v9x_u16 v9x_u32_is_power_of_two(v9x_u32 value)
{
    if (value == 0ul) {
        return V9X_FALSE;
    }
    return ((value & (value - 1ul)) == 0ul) ? V9X_TRUE : V9X_FALSE;
}

v9x_status V9X_LEAF_FAR v9x_framebuffer_validate_binding(
    const struct v9x_pci_bar_resource *bar,
    v9x_u32 detected_vram_bytes,
    v9x_u32 override_vram_bytes,
    struct v9x_framebuffer_binding *binding)
{
    v9x_u32 selected_vram;

    if (binding == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_framebuffer_clear_binding(binding);
    if (bar == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    if ((bar->flags & (v9x_u16)~V9X_PCI_BAR_KNOWN_FLAGS) != 0u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if ((bar->flags & V9X_PCI_BAR_MEMORY) == 0u ||
        (bar->flags & V9X_PCI_BAR_IO) != 0u ||
        (bar->flags & V9X_PCI_BAR_64BIT) != 0u) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (bar->physical_base == 0ul ||
        v9x_u32_is_power_of_two(bar->aperture_bytes) == V9X_FALSE) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if ((bar->physical_base & (bar->aperture_bytes - 1ul)) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if ((bar->aperture_bytes - 1ul) > V9X_U32_MAX - bar->physical_base) {
        return V9X_STATUS_INTEGER_OVERFLOW;
    }

    selected_vram = override_vram_bytes != 0ul
        ? override_vram_bytes
        : detected_vram_bytes;
    if (selected_vram == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (selected_vram > bar->aperture_bytes) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }

    binding->physical_base = bar->physical_base;
    binding->aperture_bytes = bar->aperture_bytes;
    binding->vram_bytes = selected_vram;
    binding->override_active = override_vram_bytes != 0ul
        ? V9X_TRUE
        : V9X_FALSE;
    return V9X_STATUS_OK;
}
