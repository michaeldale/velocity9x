#include "velocity9x/vbe_generic.h"

static void v9x_vbe_clear_resources(struct v9x_backend_state *state)
{
    state->framebuffer.physical_base = 0ul;
    state->framebuffer.aperture_bytes = 0ul;
    state->framebuffer.vram_bytes = 0ul;
    state->framebuffer.override_active = V9X_FALSE;
    state->vram_bytes = 0ul;
    state->resources_bound = V9X_FALSE;
    state->capabilities = 0ul;
}

/*
 * Tier-0 sets modes through the BIOS, from the 16-bit driver. There is no
 * register sequence for this layer to describe, and inventing one would be
 * claiming knowledge the tier is defined by not having.
 */
static v9x_status v9x_vbe_enter_mode(struct v9x_backend_state *state,
                                     const struct v9x_mode_request *request)
{
    (void)state;
    (void)request;
    return V9X_STATUS_UNSUPPORTED;
}

static v9x_status v9x_vbe_leave_mode(struct v9x_backend_state *state)
{
    (void)state;
    return V9X_STATUS_OK;
}

/* No engine, so nothing is ever outstanding to wait for. */
static v9x_status v9x_vbe_wait_idle(struct v9x_backend_state *state,
                                    v9x_u32 timeout_ticks)
{
    (void)state;
    (void)timeout_ticks;
    return V9X_STATUS_UNSUPPORTED;
}

static v9x_status v9x_vbe_recover(struct v9x_backend_state *state)
{
    if (state == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    state->capabilities = 0ul;
    return V9X_STATUS_UNSUPPORTED;
}

v9x_status v9x_vbe_generic_probe(
    struct v9x_backend_state *state,
    const struct v9x_pci_identity *pci)
{
    if (state == 0 || pci == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    state->initialized = V9X_FALSE;
    v9x_vbe_clear_resources(state);
    state->pci.vendor_id = 0u;
    state->pci.device_id = 0u;
    state->pci.revision = 0u;

    /*
     * Exact match, not "anything unrecognised". A backend that accepted every
     * PCI id would make the registry's refusal path unreachable and would bind
     * itself to cards nobody has run it on; the driver reaches new hardware by
     * a Have-Disk install, which is a decision a person makes.
     */
    if (pci->vendor_id != V9X_PCI_VENDOR_QEMU_BOCHS ||
        pci->device_id != V9X_PCI_DEVICE_STDVGA) {
        return V9X_STATUS_UNSUPPORTED;
    }

    state->pci = *pci;
    state->initialized = V9X_TRUE;
    return V9X_STATUS_OK;
}

v9x_status v9x_vbe_generic_bind_framebuffer(
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
        v9x_vbe_clear_resources(state);
        return V9X_STATUS_INVALID_STATE;
    }
    if (bar == 0) {
        v9x_vbe_clear_resources(state);
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    status = v9x_framebuffer_validate_binding(bar, detected_vram_bytes,
                                              override_vram_bytes, &binding);
    if (status != V9X_STATUS_OK) {
        v9x_vbe_clear_resources(state);
        return status;
    }

    state->framebuffer = binding;
    state->vram_bytes = binding.vram_bytes;
    state->resources_bound = V9X_TRUE;
    /* Lock/Unlock and CPU blits only; no engine capability is ever claimed. */
    state->capabilities = 0ul;
    return V9X_STATUS_OK;
}

v9x_status v9x_vbe_generic_validate_mode(
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

    /* Trusted state, not the caller's number: the request's own
     * framebuffer_bytes is untrusted input everywhere else too. */
    bounded_request = *request;
    bounded_request.framebuffer_bytes = state->vram_bytes;
    return v9x_mode_calculate(&bounded_request, layout);
}

static const struct v9x_backend_ops v9x_vbe_ops = {
    v9x_vbe_generic_probe,
    v9x_vbe_generic_bind_framebuffer,
    v9x_vbe_generic_validate_mode,
    v9x_vbe_enter_mode,
    v9x_vbe_leave_mode,
    v9x_vbe_wait_idle,
    v9x_vbe_recover
};

const struct v9x_backend_ops *v9x_vbe_generic_backend(void)
{
    return &v9x_vbe_ops;
}
