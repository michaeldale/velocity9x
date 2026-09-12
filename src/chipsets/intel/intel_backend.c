#include "velocity9x/intel_gma.h"

static void v9x_intel_clear_resources(struct v9x_backend_state *state)
{
    state->framebuffer.physical_base = 0ul;
    state->framebuffer.aperture_bytes = 0ul;
    state->framebuffer.vram_bytes = 0ul;
    state->framebuffer.override_active = V9X_FALSE;
    state->vram_bytes = 0ul;
    state->resources_bound = V9X_FALSE;
    state->capabilities = 0ul;
}

static v9x_status v9x_intel_enter_mode(struct v9x_backend_state *state,
                                       const struct v9x_mode_request *request)
{
    (void)state;
    (void)request;
    return V9X_STATUS_UNSUPPORTED;
}

static v9x_status v9x_intel_leave_mode(struct v9x_backend_state *state)
{
    (void)state;
    return V9X_STATUS_OK;
}

static v9x_status v9x_intel_wait_idle(struct v9x_backend_state *state,
                                      v9x_u32 timeout_ticks)
{
    (void)state;
    (void)timeout_ticks;
    return V9X_STATUS_UNSUPPORTED;
}

static v9x_status v9x_intel_recover(struct v9x_backend_state *state)
{
    if (state == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    state->capabilities = 0ul;
    return V9X_STATUS_UNSUPPORTED;
}

v9x_status v9x_intel_gma_probe(struct v9x_backend_state *state,
                               const struct v9x_pci_identity *pci)
{
    if (state == 0 || pci == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    state->initialized = V9X_FALSE;
    v9x_intel_clear_resources(state);
    state->pci.vendor_id = 0u;
    state->pci.device_id = 0u;
    state->pci.revision = 0u;

    if (pci->vendor_id != V9X_PCI_VENDOR_INTEL ||
        pci->device_id != V9X_PCI_DEVICE_GMA950_945GSE) {
        return V9X_STATUS_UNSUPPORTED;
    }
    state->pci = *pci;
    state->initialized = V9X_TRUE;
    return V9X_STATUS_OK;
}

v9x_status v9x_intel_gma_bind_framebuffer(
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
        v9x_intel_clear_resources(state);
        return V9X_STATUS_INVALID_STATE;
    }
    if (bar == 0) {
        v9x_intel_clear_resources(state);
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_framebuffer_validate_binding(bar, detected_vram_bytes,
                                               override_vram_bytes, &binding);
    if (status != V9X_STATUS_OK) {
        v9x_intel_clear_resources(state);
        return status;
    }
    state->framebuffer = binding;
    state->vram_bytes = binding.vram_bytes;
    state->resources_bound = V9X_TRUE;
    state->capabilities = 0ul;
    return V9X_STATUS_OK;
}

v9x_status v9x_intel_gma_validate_mode(
    struct v9x_backend_state *state,
    const struct v9x_mode_request *request,
    struct v9x_mode_layout *layout)
{
    struct v9x_mode_request bounded;

    if (state == 0 || request == 0 || layout == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (state->initialized == V9X_FALSE ||
        state->resources_bound == V9X_FALSE) {
        return V9X_STATUS_INVALID_STATE;
    }
    bounded = *request;
    bounded.framebuffer_bytes = state->vram_bytes;
    return v9x_mode_calculate(&bounded, layout);
}

static const struct v9x_backend_ops v9x_intel_ops = {
    v9x_intel_gma_probe,
    v9x_intel_gma_bind_framebuffer,
    v9x_intel_gma_validate_mode,
    v9x_intel_enter_mode,
    v9x_intel_leave_mode,
    v9x_intel_wait_idle,
    v9x_intel_recover
};

const struct v9x_backend_ops *v9x_intel_gma_backend(void)
{
    return &v9x_intel_ops;
}
