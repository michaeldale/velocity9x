/*
 * Shared read-only status collection for the Velocity9x settings surfaces.
 *
 * Both the standalone V9XSET.EXE panel and the Display Properties
 * property-sheet page render the same driver-published INI facts through
 * this module. It performs no hardware access and no registry writes.
 */
#ifndef V9X_SETTINGS_STATUS_H
#define V9X_SETTINGS_STATUS_H

typedef struct v9x_settings_status {
    char adapter_name[96];
    char pci_id[24];
    char video_memory[48];
    char active_mode[48];
    char core_clock[96];
    char memory_clock[64];
    char clock_detector[64];
    char driver_stage[80];
    char framebuffer_status[96];
    char gdi_status[160];
    char mode_switching[80];
    char rendering[64];
    char directdraw[80];
    char direct3d[64];
    int live_mode_switching;
    int hardware_acceleration;
    int live_depth_switching;
    /*
     * The Direct3D selector, which is the one setting these surfaces can
     * change rather than only report.
     *
     * `direct3d_capable` is the chip's answer, from the driver's own
     * Direct3D= key: false means no selector value can produce Direct3D and
     * the control is disabled. `direct3d_request` is the raw
     * [Velocity9x] Direct3D value out of SYSTEM.INI, one of the
     * V9X_D3D_REQUEST_* numbers, and is what a page writes back.
     *
     * They are separate from direct3d[] above, which is the resolved
     * sentence. A request of 1 on a card with no 3D engine and a request of 0
     * on the same card produce the same sentence and different requests, and
     * a page that offered to change the setting has to know which it is
     * looking at.
     */
    int direct3d_capable;
    int direct3d_request;
    /* The runtime mode table's story, from C:\V9XDIAG\V9XMODES.INI: published and
     * hidden counts, or the static-list statement when no inventory exists. */
    char dynamic_modes[128];
    char report[1536];
} V9X_SETTINGS_STATUS;

unsigned long v9x_settings_string_length(const char *text);
void v9x_settings_collect(V9X_SETTINGS_STATUS *status,
                          const char *version,
                          const char *build_id);
int v9x_settings_copy_report(void *owner_window,
                             const char *caption,
                             const char *report);

#endif
