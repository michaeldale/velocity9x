#include "velocity9x/tdfx.h"

static void v9x_tdfx_clear_resources(struct v9x_backend_state *state)
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
 * Tier-0: the VBE BIOS sets modes through the 16-bit driver, so there is no
 * register sequence for this layer to describe.
 */
static v9x_status v9x_tdfx_enter_mode(struct v9x_backend_state *state,
                                      const struct v9x_mode_request *request)
{
    (void)state;
    (void)request;
    return V9X_STATUS_UNSUPPORTED;
}

static v9x_status v9x_tdfx_leave_mode(struct v9x_backend_state *state)
{
    (void)state;
    return V9X_STATUS_OK;
}

/*
 * No engine is claimed yet, so nothing is ever outstanding. When the 2D
 * engine lands this polls status bit 9 (aggregate busy), bounded by time and
 * believed idle only after three consecutive clear reads
 * (docs\specifications\vsa100-registers.md).
 */
static v9x_status v9x_tdfx_wait_idle(struct v9x_backend_state *state,
                                     v9x_u32 timeout_ticks)
{
    (void)state;
    (void)timeout_ticks;
    return V9X_STATUS_UNSUPPORTED;
}

static v9x_status v9x_tdfx_recover(struct v9x_backend_state *state)
{
    if (state == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    state->capabilities = 0ul;
    return V9X_STATUS_UNSUPPORTED;
}

v9x_status v9x_tdfx_probe(
    struct v9x_backend_state *state,
    const struct v9x_pci_identity *pci)
{
    if (state == 0 || pci == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    state->initialized = V9X_FALSE;
    v9x_tdfx_clear_resources(state);
    state->pci.vendor_id = 0u;
    state->pci.device_id = 0u;
    state->pci.revision = 0u;

    /*
     * The exact id, never a vendor-wide match: the Banshee (0003) and
     * Voodoo3 (0005) share the VSA-100's 2D core but not its memory and
     * aperture layout, and neither has run here. Revision is not checked;
     * both measured boards are revision 01.
     */
    if (pci->vendor_id != V9X_PCI_VENDOR_3DFX ||
        pci->device_id != V9X_PCI_DEVICE_VSA100) {
        return V9X_STATUS_UNSUPPORTED;
    }

    state->pci = *pci;
    state->initialized = V9X_TRUE;
    return V9X_STATUS_OK;
}

v9x_status v9x_tdfx_bind_framebuffer(
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
        v9x_tdfx_clear_resources(state);
        return V9X_STATUS_INVALID_STATE;
    }
    if (bar == 0) {
        v9x_tdfx_clear_resources(state);
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    status = v9x_framebuffer_validate_binding(bar, detected_vram_bytes,
                                              override_vram_bytes, &binding);
    if (status != V9X_STATUS_OK) {
        v9x_tdfx_clear_resources(state);
        return status;
    }

    state->framebuffer = binding;
    state->vram_bytes = binding.vram_bytes;
    state->resources_bound = V9X_TRUE;
    /* Lock/Unlock and CPU blits only until an engine is implemented. */
    state->capabilities = 0ul;
    return V9X_STATUS_OK;
}

v9x_status v9x_tdfx_validate_mode(
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

    /* Trusted state, not the caller's number. */
    bounded_request = *request;
    bounded_request.framebuffer_bytes = state->vram_bytes;
    return v9x_mode_calculate(&bounded_request, layout);
}

static const struct v9x_backend_ops v9x_tdfx_ops = {
    v9x_tdfx_probe,
    v9x_tdfx_bind_framebuffer,
    v9x_tdfx_validate_mode,
    v9x_tdfx_enter_mode,
    v9x_tdfx_leave_mode,
    v9x_tdfx_wait_idle,
    v9x_tdfx_recover
};

const struct v9x_backend_ops *v9x_tdfx_backend(void)
{
    return &v9x_tdfx_ops;
}
