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
    /* The PCI revision from the device's Enum key name (REV_xx), or
     * "Unavailable" for a card Windows did not enumerate on PCI. */
    char revision[16];
    char video_memory[48];
    char active_mode[48];
    /* The page's split of the mode: "800 x 600", "16-bit (65,536 colours)"
     * and the refresh as GDI reports it. */
    char resolution[24];
    char colour_depth[40];
    char refresh_rate[32];
    /* The mini-VDD's build, read out of V9XMINI.VXD's own marker, so a
     * driver and mini-VDD from different builds are visible. */
    char minivdd_build[48];
    /* 2D drawing: the engine primitives GDI uses, from GdiAcceleration=. */
    char gdi_rendering[64];
    /* Short forms for the page; the long ones stay in the report. */
    char driver_short[48];
    char gdi_short[48];
    char ddraw_test[64];
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
    /*
     * The 16-bit colour layout. `highcolor_request` is the raw [Velocity9x]
     * HighColor value - 15, 16, or 0 for absent/automatic - and
     * `colour_layout` is what the driver resolved this boot, from
     * V9XHW.INI's ColourLayout=, as a sentence for the page.
     */
    int highcolor_request;
    char colour_layout[64];
    /*
     * Vertical sync. `vsync_request` is the raw [Velocity9x] VSync value,
     * one of the V9X_VSYNC_REQUEST_* numbers, and `vsync` is what the driver
     * last stamped, from V9XHW.INI's VSync=, as a sentence for the report.
     */
    int vsync_request;
    char vsync[48];
    /*
     * The Direct3D driver interface. `ddi_request` is [Velocity9x]
     * Direct3DDdi as the page offers it: 5, 6, or 0 for absent or anything
     * else, which the driver reads as per program. `ddi` is what the driver
     * last stamped, from V9XHW.INI's Direct3DDdi=, as a sentence.
     */
    int ddi_request;
    char ddi[48];
    /* The runtime mode table's story, from C:\V9XDIAG\V9XMODES.INI: published and
     * hidden counts, or the static-list statement when no inventory exists. */
    char dynamic_modes[128];
    char report[2048];
} V9X_SETTINGS_STATUS;

unsigned long v9x_settings_string_length(const char *text);
void v9x_settings_collect(V9X_SETTINGS_STATUS *status,
                          const char *version,
                          const char *build_id);
int v9x_settings_copy_report(void *owner_window,
                             const char *caption,
                             const char *report);

#endif
