#include "velocity9x/matrox_millennium2.h"

static void v9x_mga2_clear_resources(struct v9x_backend_state *state)
{
    state->framebuffer.physical_base = 0ul;
    state->framebuffer.aperture_bytes = 0ul;
    state->framebuffer.vram_bytes = 0ul;
    state->framebuffer.override_active = V9X_FALSE;
    state->vram_bytes = 0ul;
    state->resources_bound = V9X_FALSE;
    state->capabilities = 0ul;
}

static v9x_status v9x_mga2_enter_mode(struct v9x_backend_state *state,
                                      const struct v9x_mode_request *request)
{
    (void)state;
    (void)request;
    return V9X_STATUS_UNSUPPORTED;
}

static v9x_status v9x_mga2_leave_mode(struct v9x_backend_state *state)
{
    (void)state;
    return V9X_STATUS_OK;
}

static v9x_status v9x_mga2_wait_idle(struct v9x_backend_state *state,
                                     v9x_u32 timeout_ticks)
{
    (void)state;
    (void)timeout_ticks;
    return V9X_STATUS_UNSUPPORTED;
}

static v9x_status v9x_mga2_recover(struct v9x_backend_state *state)
{
    if (state == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    state->capabilities = 0ul;
    return V9X_STATUS_UNSUPPORTED;
}

v9x_status v9x_matrox_millennium2_probe(
    struct v9x_backend_state *state,
    const struct v9x_pci_identity *pci)
{
    if (state == 0 || pci == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    state->initialized = V9X_FALSE;
    v9x_mga2_clear_resources(state);
    state->pci.vendor_id = 0u;
    state->pci.device_id = 0u;
    state->pci.revision = 0u;

    if (pci->vendor_id != V9X_PCI_VENDOR_MATROX) {
        return V9X_STATUS_UNSUPPORTED;
    }
    /* Both Millennium generations, because the policy is the same for both
     * and the only difference the driver acts on - the framebuffer's BAR
     * index - is hw16 data rather than anything this file decides. */
    if (pci->device_id != V9X_PCI_DEVICE_MILLENNIUM_II &&
        pci->device_id != V9X_PCI_DEVICE_MILLENNIUM) {
        return V9X_STATUS_UNSUPPORTED;
    }

    state->pci = *pci;
    state->initialized = V9X_TRUE;
    return V9X_STATUS_OK;
}

v9x_status v9x_matrox_millennium2_bind_framebuffer(
    struct v9x_backend_state *state,
    const struct v9x_pci_bar_resource *bar,
    v9x_u32 detected_vram_bytes,
    v9x_u32 override_vram_bytes)
{
    struct v9x_framebuffer_binding binding;
    v9x_status status;

    if (state == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (state->initialized == V9X_FALSE) {
        v9x_mga2_clear_resources(state);
        return V9X_STATUS_INVALID_STATE;
    }
    if (bar == 0) {
        v9x_mga2_clear_resources(state);
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    status = v9x_framebuffer_validate_binding(bar, detected_vram_bytes,
                                               override_vram_bytes, &binding);
    if (status != V9X_STATUS_OK) {
        v9x_mga2_clear_resources(state);
        return status;
    }

    state->framebuffer = binding;
    state->vram_bytes = binding.vram_bytes;
    state->resources_bound = V9X_TRUE;
    /* Mapping and register access have not yet been proven on hardware. */
    state->capabilities = 0ul;
    return V9X_STATUS_OK;
}

v9x_status v9x_matrox_millennium2_validate_mode(
    struct v9x_backend_state *state,
    const struct v9x_mode_request *request,
    struct v9x_mode_layout *layout)
{
    struct v9x_mode_request bounded_request;

    if (state == 0 || request == 0 || layout == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (state->initialized == V9X_FALSE ||
        state->resources_bound == V9X_FALSE) {
        return V9X_STATUS_INVALID_STATE;
    }

    bounded_request = *request;
    bounded_request.framebuffer_bytes = state->vram_bytes;
    return v9x_mode_calculate(&bounded_request, layout);
}

static const struct v9x_backend_ops v9x_mga2_ops = {
    v9x_matrox_millennium2_probe,
    v9x_matrox_millennium2_bind_framebuffer,
    v9x_matrox_millennium2_validate_mode,
    v9x_mga2_enter_mode,
    v9x_mga2_leave_mode,
    v9x_mga2_wait_idle,
    v9x_mga2_recover
};

const struct v9x_backend_ops *v9x_matrox_millennium2_backend(void)
{
    return &v9x_mga2_ops;
}
