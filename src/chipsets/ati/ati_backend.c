#include "velocity9x/ati_mach64.h"

static void v9x_ati_clear_resources(struct v9x_backend_state *state)
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
 * The family starts on tier-0, so mode setting is the BIOS's job through the
 * 16-bit driver and there is no register sequence for this layer to describe.
 * A native Mach64 mode set is a later step; until it exists, saying so is more
 * honest than a stub that pretends.
 */
static v9x_status v9x_ati_enter_mode(struct v9x_backend_state *state,
                                     const struct v9x_mode_request *request)
{
    (void)state;
    (void)request;
    return V9X_STATUS_UNSUPPORTED;
}

static v9x_status v9x_ati_leave_mode(struct v9x_backend_state *state)
{
    (void)state;
    return V9X_STATUS_OK;
}

/*
 * No engine is claimed yet, so nothing is ever outstanding.
 *
 * When eng_mach64.c lands this becomes a real wait, and it will not be the
 * Trio64's block-on-idle: the Mach64's idiom is to wait for N free command
 * FIFO slots and then issue N writes without polling again. On the Mobility
 * that count comes from GUI_STAT bits [25:16]; on a plain VT it has to be
 * population-counted out of FIFO_STAT. The two targets genuinely differ.
 */
static v9x_status v9x_ati_wait_idle(struct v9x_backend_state *state,
                                    v9x_u32 timeout_ticks)
{
    (void)state;
    (void)timeout_ticks;
    return V9X_STATUS_UNSUPPORTED;
}

/*
 * Unlike the Trio64, this chip does have a documented recovery: pulse
 * GEN_GUI_RESETB in GEN_TEST_CNTL and replay the engine state. It is not
 * implemented here because there is no engine to recover yet, and because it
 * cannot be exercised on the emulated target at all - 86Box's engine can never
 * be observed busy, so wait, timeout and reset logic must be written by
 * inspection and first tested on the laptop.
 */
static v9x_status v9x_ati_recover(struct v9x_backend_state *state)
{
    if (state == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    state->capabilities = 0ul;
    return V9X_STATUS_UNSUPPORTED;
}

v9x_status v9x_ati_mach64_probe(
    struct v9x_backend_state *state,
    const struct v9x_pci_identity *pci)
{
    if (state == 0 || pci == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    state->initialized = V9X_FALSE;
    v9x_ati_clear_resources(state);
    state->pci.vendor_id = 0u;
    state->pci.device_id = 0u;
    state->pci.revision = 0u;

    /*
     * Exact ids, never a vendor-wide match. ATI shipped a great many Mach64
     * variants and this family has been run on two of them; the rest reach the
     * driver through a Have-Disk install, which is a decision a person makes.
     */
    if (pci->vendor_id != V9X_PCI_VENDOR_ATI) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (pci->device_id != V9X_PCI_DEVICE_MACH64_VT2 &&
        pci->device_id != V9X_PCI_DEVICE_RAGE_MOBILITY_M) {
        return V9X_STATUS_UNSUPPORTED;
    }

    state->pci = *pci;
    state->initialized = V9X_TRUE;
    return V9X_STATUS_OK;
}

v9x_status v9x_ati_mach64_bind_framebuffer(
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
        v9x_ati_clear_resources(state);
        return V9X_STATUS_INVALID_STATE;
    }
    if (bar == 0) {
        v9x_ati_clear_resources(state);
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    status = v9x_framebuffer_validate_binding(bar, detected_vram_bytes,
                                              override_vram_bytes, &binding);
    if (status != V9X_STATUS_OK) {
        v9x_ati_clear_resources(state);
        return status;
    }

    state->framebuffer = binding;
    state->vram_bytes = binding.vram_bytes;
    state->resources_bound = V9X_TRUE;
    /* Lock/Unlock and CPU blits only until an engine is implemented. */
    state->capabilities = 0ul;
    return V9X_STATUS_OK;
}

v9x_status v9x_ati_mach64_validate_mode(
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

static const struct v9x_backend_ops v9x_ati_ops = {
    v9x_ati_mach64_probe,
    v9x_ati_mach64_bind_framebuffer,
    v9x_ati_mach64_validate_mode,
    v9x_ati_enter_mode,
    v9x_ati_leave_mode,
    v9x_ati_wait_idle,
    v9x_ati_recover
};

const struct v9x_backend_ops *v9x_ati_mach64_backend(void)
{
    return &v9x_ati_ops;
}
