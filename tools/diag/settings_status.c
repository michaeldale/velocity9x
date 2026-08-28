/*
 * Shared read-only status collection for the Velocity9x settings surfaces.
 *
 * The values are read from the driver-published INI files in C:\V9XDIAG
 * (include\velocity9x\diagpaths.h):
 *   V9XHW.INI    hardware identity and clock diagnostics
 *   V9XBOOT.INI  boot-trace stage
 *   V9XGDI.INI   last GDI framebuffer test result
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"

#include "settings_status.h"

/*
 * The family's mode list, supplied at build time from the manifest's
 * Package.ModesSummary by build-settings-page.ps1. It cannot be derived here:
 * one source serves every family and the families no longer offer the same
 * depths. The fallback is for a standalone build with no family to ask, and
 * says so rather than naming modes that may not exist.
 */
#ifndef V9X_MODES_SUMMARY
#define V9X_MODES_SUMMARY "see Display Properties, Settings"
#endif

unsigned long v9x_settings_string_length(const char *text)
{
    unsigned long length = 0ul;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static void v9x_append(char *destination, DWORD capacity, const char *text)
{
    DWORD offset = v9x_settings_string_length(destination);
    DWORD index = 0ul;
    while (text[index] != '\0' && offset + index + 1ul < capacity) {
        destination[offset + index] = text[index];
        ++index;
    }
    destination[offset + index] = '\0';
}

static void v9x_append_uint(char *destination, DWORD capacity, UINT value)
{
    char reverse[12];
    char number[12];
    int count = 0;
    int index;
    do {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    for (index = 0; index < count; ++index) {
        number[index] = reverse[count - index - 1];
    }
    number[count] = '\0';
    v9x_append(destination, capacity, number);
}

static BOOL v9x_parse_u32(const char *text, DWORD *value)
{
    DWORD result = 0ul;
    DWORD index = 0ul;

    if (text == 0 || value == 0 || text[0] == '\0') {
        return FALSE;
    }
    while (text[index] != '\0') {
        DWORD digit;
        if (text[index] < '0' || text[index] > '9') {
            return FALSE;
        }
        digit = (DWORD)(text[index] - '0');
        if (result > 429496729ul ||
            (result == 429496729ul && digit > 5ul)) {
            return FALSE;
        }
        result = result * 10ul + digit;
        ++index;
    }
    *value = result;
    return TRUE;
}

static void v9x_format_clock(char *destination,
                             DWORD capacity,
                             const char *khz_text,
                             BOOL shared_memory)
{
    DWORD khz;
    DWORD fraction;
    char fraction_text[4];

    destination[0] = '\0';
    if (!v9x_parse_u32(khz_text, &khz) || khz == 0ul) {
        v9x_append(destination, capacity, "Unavailable");
        return;
    }
    v9x_append_uint(destination, capacity, (UINT)(khz / 1000ul));
    v9x_append(destination, capacity, ".");
    fraction = khz % 1000ul;
    fraction_text[0] = (char)('0' + fraction / 100ul);
    fraction_text[1] = (char)('0' + (fraction / 10ul) % 10ul);
    fraction_text[2] = (char)('0' + fraction % 10ul);
    fraction_text[3] = '\0';
    v9x_append(destination, capacity, fraction_text);
    v9x_append(destination, capacity, " MHz");
    if (shared_memory) {
        v9x_append(destination, capacity, " (shared with memory)");
    }
}

void v9x_settings_collect(V9X_SETTINGS_STATUS *status,
                          const char *version,
                          const char *build_id)
{
    HDC display = GetDC(0);
    UINT width = (UINT)GetDeviceCaps(display, HORZRES);
    UINT height = (UINT)GetDeviceCaps(display, VERTRES);
    UINT bits = (UINT)(GetDeviceCaps(display, BITSPIXEL) *
                       GetDeviceCaps(display, PLANES));
    char result[16];
    char test_build[80];
    char test_width[12];
    char test_height[12];
    char test_bits[12];
    char clock_status[24];
    char core_clock_khz[16];
    char memory_clock_khz[16];
    char core_relation[32];
    ReleaseDC(0, display);

    status->active_mode[0] = '\0';
    v9x_append_uint(status->active_mode, sizeof(status->active_mode), width);
    v9x_append(status->active_mode, sizeof(status->active_mode), " x ");
    v9x_append_uint(status->active_mode, sizeof(status->active_mode), height);
    v9x_append(status->active_mode, sizeof(status->active_mode), " x ");
    v9x_append_uint(status->active_mode, sizeof(status->active_mode), bits);
    v9x_append(status->active_mode, sizeof(status->active_mode), " bpp");

    GetPrivateProfileStringA("Velocity9xHardware", "Adapter",
        "Unknown VGA adapter", status->adapter_name,
        sizeof(status->adapter_name), V9X_DIAG_HW_INI);
    GetPrivateProfileStringA("Velocity9xHardware", "ClockStatus",
        "unavailable", clock_status, sizeof(clock_status),
        V9X_DIAG_HW_INI);
    GetPrivateProfileStringA("Velocity9xHardware", "CoreClockKHz", "",
        core_clock_khz, sizeof(core_clock_khz), V9X_DIAG_HW_INI);
    GetPrivateProfileStringA("Velocity9xHardware", "MemoryClockKHz", "",
        memory_clock_khz, sizeof(memory_clock_khz), V9X_DIAG_HW_INI);
    GetPrivateProfileStringA("Velocity9xHardware", "CoreClockRelation", "",
        core_relation, sizeof(core_relation), V9X_DIAG_HW_INI);
    GetPrivateProfileStringA("Velocity9xHardware", "ClockDetector",
        "none", status->clock_detector, sizeof(status->clock_detector),
        V9X_DIAG_HW_INI);
    if (lstrcmpiA(clock_status, "valid") == 0) {
        v9x_format_clock(status->core_clock, sizeof(status->core_clock),
            core_clock_khz,
            lstrcmpiA(core_relation, "shared-memory-clock") == 0);
        v9x_format_clock(status->memory_clock, sizeof(status->memory_clock),
                         memory_clock_khz, FALSE);
    } else {
        v9x_format_clock(status->core_clock, sizeof(status->core_clock),
                         "", FALSE);
        v9x_format_clock(status->memory_clock, sizeof(status->memory_clock),
                         "", FALSE);
    }

    {
        char vendor[16];
        char device[16];

        GetPrivateProfileStringA("Velocity9xHardware", "VendorId", "",
                                 vendor, sizeof(vendor), V9X_DIAG_HW_INI);
        GetPrivateProfileStringA("Velocity9xHardware", "DeviceId", "",
                                 device, sizeof(device), V9X_DIAG_HW_INI);
        status->pci_id[0] = '\0';
        if (vendor[0] != '\0' && device[0] != '\0') {
            v9x_append(status->pci_id, sizeof(status->pci_id), vendor);
            v9x_append(status->pci_id, sizeof(status->pci_id), ":");
            v9x_append(status->pci_id, sizeof(status->pci_id), device);
        } else {
            v9x_append(status->pci_id, sizeof(status->pci_id), "Unavailable");
        }
    }

    /*
     * Installed video memory, reported in whole MB when it divides evenly.
     * The driver decodes this from the chip and reports nothing when the
     * encoding is one it does not recognise, so an unknown card shows
     * "Unavailable" rather than a fabricated size.
     */
    {
        char memory_status[16];
        char memory_bytes_text[16];
        DWORD memory_bytes = 0ul;

        GetPrivateProfileStringA("Velocity9xHardware", "VideoMemoryStatus",
                                 "unavailable", memory_status,
                                 sizeof(memory_status), V9X_DIAG_HW_INI);
        GetPrivateProfileStringA("Velocity9xHardware", "VideoMemoryBytes",
                                 "0", memory_bytes_text,
                                 sizeof(memory_bytes_text), V9X_DIAG_HW_INI);
        if (!v9x_parse_u32(memory_bytes_text, &memory_bytes)) {
            memory_bytes = 0ul;
        }
        status->video_memory[0] = '\0';
        if (lstrcmpiA(memory_status, "valid") == 0 && memory_bytes != 0ul) {
            if ((memory_bytes % (1024ul * 1024ul)) == 0ul) {
                v9x_append_uint(status->video_memory,
                                sizeof(status->video_memory),
                                (UINT)(memory_bytes / (1024ul * 1024ul)));
                v9x_append(status->video_memory,
                           sizeof(status->video_memory), " MB");
            } else {
                v9x_append_uint(status->video_memory,
                                sizeof(status->video_memory),
                                (UINT)(memory_bytes / 1024ul));
                v9x_append(status->video_memory,
                           sizeof(status->video_memory), " KB");
            }
        } else {
            v9x_append(status->video_memory, sizeof(status->video_memory),
                       "Unavailable");
        }
    }

    {
        char switching[32];
        char acceleration[40];
        char direct3d[32];

        GetPrivateProfileStringA("Velocity9xHardware", "ModeSwitching",
                                 "reboot-selected", switching,
                                 sizeof(switching), V9X_DIAG_HW_INI);
        status->live_mode_switching =
            lstrcmpiA(switching, "live-any-depth") == 0 ||
            lstrcmpiA(switching, "live-same-depth") == 0;
        status->live_depth_switching =
            lstrcmpiA(switching, "live-any-depth") == 0;
        status->mode_switching[0] = '\0';
        v9x_append(status->mode_switching, sizeof(status->mode_switching),
                   status->live_depth_switching
                       ? "Live, including color depth"
                       : (status->live_mode_switching
                          ? "Live (same color depth); depth change requires"
                            " restart"
                          : "Selected at boot"));
        GetPrivateProfileStringA("Velocity9xHardware", "Acceleration",
                                 "disabled", acceleration,
                                 sizeof(acceleration), V9X_DIAG_HW_INI);
        status->hardware_acceleration =
            lstrcmpiA(acceleration, "directdraw-fill-blt") == 0 ||
            lstrcmpiA(acceleration, "directdraw-solid-fill") == 0;

        /* Describe what the DirectDraw HAL runs on the engine. Anything the
         * driver does not claim is reported as software so the page never
         * overstates the hardware path. */
        status->directdraw[0] = '\0';
        if (lstrcmpiA(acceleration, "directdraw-fill-blt") == 0) {
            v9x_append(status->directdraw, sizeof(status->directdraw),
                       "Surfaces, page flip, vblank, fill, blit");
        } else if (lstrcmpiA(acceleration, "directdraw-solid-fill") == 0) {
            v9x_append(status->directdraw, sizeof(status->directdraw),
                       "Surfaces, page flip, vblank, fill");
        } else {
            v9x_append(status->directdraw, sizeof(status->directdraw),
                       "Software emulation only");
        }

        GetPrivateProfileStringA("Velocity9xHardware", "Direct3D",
                                 "not-advertised", direct3d,
                                 sizeof(direct3d), V9X_DIAG_HW_INI);
        status->direct3d[0] = '\0';
        if (lstrcmpiA(direct3d, "hardware-s3d") == 0) {
            v9x_append(status->direct3d, sizeof(status->direct3d),
                       "Hardware (S3D triangle engine)");
        } else {
            v9x_append(status->direct3d, sizeof(status->direct3d),
                       "Not advertised on this chip");
        }

        status->rendering[0] = '\0';
        v9x_append(status->rendering, sizeof(status->rendering),
                   "Windows DIB Engine (software GDI)");
    }

    /*
     * The runtime mode table, from the validated inventory the driver writes
     * after a successful enable. An absent or torn inventory reads as the
     * static list rather than as partial numbers: Complete=1 is the same
     * sentinel the registry synchronizer requires.
     */
    {
        char table_line[96];
        DWORD rows = 0ul;
        DWORD published = 0ul;

        status->dynamic_modes[0] = '\0';
        if (GetPrivateProfileIntA("Velocity9xModes", "Complete", 0,
                                  V9X_DIAG_MODES_INI) == 1 &&
            GetPrivateProfileStringA("Velocity9xModes", "Table", "",
                                     table_line, sizeof(table_line),
                                     V9X_DIAG_MODES_INI) != 0ul) {
            /* "rows=N published=N first=N dropped=N": two bounded fields. */
            const char *at = table_line;
            DWORD out = 0ul;
            int field = 0;

            while (*at != '\0' && field < 2) {
                if (at[0] == '=' && at > table_line) {
                    const char *cursor = at + 1;

                    out = 0ul;
                    while (*cursor >= '0' && *cursor <= '9') {
                        out = out * 10ul + (DWORD)(*cursor - '0');
                        ++cursor;
                    }
                    if (field == 0) {
                        rows = out;
                    } else {
                        published = out;
                    }
                    ++field;
                    at = cursor;
                    continue;
                }
                ++at;
            }
        }
        if (rows != 0ul && published != 0ul && published <= rows) {
            v9x_append_uint(status->dynamic_modes,
                            sizeof(status->dynamic_modes), (UINT)published);
            v9x_append(status->dynamic_modes, sizeof(status->dynamic_modes),
                       " published");
            if (published < rows) {
                v9x_append(status->dynamic_modes,
                           sizeof(status->dynamic_modes), ", ");
                v9x_append_uint(status->dynamic_modes,
                                sizeof(status->dynamic_modes),
                                (UINT)(rows - published));
                v9x_append(status->dynamic_modes,
                           sizeof(status->dynamic_modes),
                           " hidden (scan/EDID-contradicted)");
            }
        } else {
            v9x_append(status->dynamic_modes, sizeof(status->dynamic_modes),
                       "Static mode list (no runtime inventory)");
        }
    }

    GetPrivateProfileStringA("Velocity9x", "Stage", "not recorded",
                             status->driver_stage,
                             sizeof(status->driver_stage),
                             V9X_DIAG_BOOT_INI);
    status->framebuffer_status[0] = '\0';
    if (lstrcmpiA(status->driver_stage, "enable-ok") == 0) {
        v9x_append(status->framebuffer_status,
                   sizeof(status->framebuffer_status),
                   "Active - linear aperture mapped");
    } else {
        v9x_append(status->framebuffer_status,
                   sizeof(status->framebuffer_status),
                   "Not confirmed - stage: ");
        v9x_append(status->framebuffer_status,
                   sizeof(status->framebuffer_status),
                   status->driver_stage);
    }

    GetPrivateProfileStringA("Velocity9xGDI", "Result", "not run", result,
                             sizeof(result), V9X_DIAG_GDI_INI);
    GetPrivateProfileStringA("Velocity9xGDI", "Build", "unknown", test_build,
                             sizeof(test_build), V9X_DIAG_GDI_INI);
    GetPrivateProfileStringA("Velocity9xGDI", "Width", "?", test_width,
                             sizeof(test_width), V9X_DIAG_GDI_INI);
    GetPrivateProfileStringA("Velocity9xGDI", "Height", "?", test_height,
                             sizeof(test_height), V9X_DIAG_GDI_INI);
    GetPrivateProfileStringA("Velocity9xGDI", "BitsPerPixel", "?", test_bits,
                             sizeof(test_bits), V9X_DIAG_GDI_INI);
    status->gdi_status[0] = '\0';
    v9x_append(status->gdi_status, sizeof(status->gdi_status), result);
    if (lstrcmpiA(result, "not run") != 0) {
        v9x_append(status->gdi_status, sizeof(status->gdi_status), " - ");
        v9x_append(status->gdi_status, sizeof(status->gdi_status), test_width);
        v9x_append(status->gdi_status, sizeof(status->gdi_status), "x");
        v9x_append(status->gdi_status, sizeof(status->gdi_status), test_height);
        v9x_append(status->gdi_status, sizeof(status->gdi_status), "x");
        v9x_append(status->gdi_status, sizeof(status->gdi_status), test_bits);
        v9x_append(status->gdi_status, sizeof(status->gdi_status), " (");
        v9x_append(status->gdi_status, sizeof(status->gdi_status), test_build);
        v9x_append(status->gdi_status, sizeof(status->gdi_status), ")");
    }

    status->report[0] = '\0';
    v9x_append(status->report, sizeof(status->report),
               "Velocity9x settings report\r\nVersion: ");
    v9x_append(status->report, sizeof(status->report), version);
    v9x_append(status->report, sizeof(status->report), "\r\nBuild: ");
    v9x_append(status->report, sizeof(status->report), build_id);
    v9x_append(status->report, sizeof(status->report), "\r\nAdapter: ");
    v9x_append(status->report, sizeof(status->report), status->adapter_name);
    v9x_append(status->report, sizeof(status->report), "\r\nPCI ID: ");
    v9x_append(status->report, sizeof(status->report), status->pci_id);
    v9x_append(status->report, sizeof(status->report), "\r\nVideo memory: ");
    v9x_append(status->report, sizeof(status->report), status->video_memory);
    v9x_append(status->report, sizeof(status->report), "\r\nActive mode: ");
    v9x_append(status->report, sizeof(status->report), status->active_mode);
    v9x_append(status->report, sizeof(status->report),
               "\r\nCore / engine clock: ");
    v9x_append(status->report, sizeof(status->report), status->core_clock);
    v9x_append(status->report, sizeof(status->report), "\r\nMemory clock: ");
    v9x_append(status->report, sizeof(status->report), status->memory_clock);
    v9x_append(status->report, sizeof(status->report), "\r\nClock detector: ");
    v9x_append(status->report, sizeof(status->report), status->clock_detector);
    v9x_append(status->report, sizeof(status->report), "\r\nDriver stage: ");
    v9x_append(status->report, sizeof(status->report), status->driver_stage);
    v9x_append(status->report, sizeof(status->report), "\r\nFramebuffer: ");
    v9x_append(status->report, sizeof(status->report),
               status->framebuffer_status);
    v9x_append(status->report, sizeof(status->report), "\r\nLast GDI test: ");
    v9x_append(status->report, sizeof(status->report), status->gdi_status);
    v9x_append(status->report, sizeof(status->report),
               "\r\nMode switching: ");
    v9x_append(status->report, sizeof(status->report),
               status->mode_switching);
    v9x_append(status->report, sizeof(status->report),
        "\r\nBaseline modes: " V9X_MODES_SUMMARY
        "\r\nRuntime modes: ");
    v9x_append(status->report, sizeof(status->report), status->dynamic_modes);

    /* The rest of the runtime-table story, verbatim from the inventory and
     * the synchronizer report: scan state and counts, per-reason drop tally,
     * the panel's EDID recommendation, and the generation the registry was
     * last synchronized against - so a stale Settings page is identifiable
     * rather than mysterious. */
    {
        char line[112];

        GetPrivateProfileStringA("Velocity9xModes", "Scan", "not collected",
                                 line, sizeof(line), V9X_DIAG_MODES_INI);
        v9x_append(status->report, sizeof(status->report), "\r\nMode scan: ");
        v9x_append(status->report, sizeof(status->report), line);
        GetPrivateProfileStringA("Velocity9xModes", "Reasons", "none",
                                 line, sizeof(line), V9X_DIAG_MODES_INI);
        v9x_append(status->report, sizeof(status->report),
                   "\r\nDrop reasons: ");
        v9x_append(status->report, sizeof(status->report), line);
        GetPrivateProfileStringA("Velocity9xModes", "Recommendation", "none",
                                 line, sizeof(line), V9X_DIAG_MODES_INI);
        v9x_append(status->report, sizeof(status->report),
                   "\r\nEDID recommendation: ");
        v9x_append(status->report, sizeof(status->report), line);
        GetPrivateProfileStringA("Velocity9xModes", "Generation", "0",
                                 line, sizeof(line), V9X_DIAG_MODES_INI);
        v9x_append(status->report, sizeof(status->report),
                   "\r\nInventory generation: ");
        v9x_append(status->report, sizeof(status->report), line);
        GetPrivateProfileStringA("Velocity9xSync", "Status", "never ran",
                                 line, sizeof(line), V9X_DIAG_SYNC_INI);
        v9x_append(status->report, sizeof(status->report),
                   "\r\nRegistry sync: ");
        v9x_append(status->report, sizeof(status->report), line);
        GetPrivateProfileStringA("Velocity9xSync", "Generation", "",
                                 line, sizeof(line), V9X_DIAG_SYNC_INI);
        if (line[0] != '\0') {
            v9x_append(status->report, sizeof(status->report),
                       " (generation ");
            v9x_append(status->report, sizeof(status->report), line);
            v9x_append(status->report, sizeof(status->report), ")");
        }
    }

    v9x_append(status->report, sizeof(status->report),
        "\r\nRendering: ");
    v9x_append(status->report, sizeof(status->report), status->rendering);
    v9x_append(status->report, sizeof(status->report), "\r\nDirectDraw: ");
    v9x_append(status->report, sizeof(status->report), status->directdraw);
    v9x_append(status->report, sizeof(status->report), "\r\nDirect3D: ");
    v9x_append(status->report, sizeof(status->report), status->direct3d);
    /*
     * A static statement of what the mini-VDD installs, not a query: the
     * dispatch table is registered once at Device_Init and the build gate in
     * scripts\build-minivdd-skeleton.ps1 asserts the set, so the two cannot
     * drift. This said "master VDD defaults" until 2026-08-28, by which time
     * it had been wrong since the power callbacks landed - and it was read as
     * evidence of what the mini-VDD hooked, which it never was.
     */
    v9x_append(status->report, sizeof(status->report),
               "\r\nMini-VDD callbacks: VESA, monitor power\r\n");
}

int v9x_settings_copy_report(void *owner_window,
                             const char *caption,
                             const char *report)
{
    HWND window = (HWND)owner_window;
    DWORD length = v9x_settings_string_length(report) + 1ul;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, length);
    char *destination;
    DWORD index;

    if (memory == 0) {
        MessageBoxA(window, "Could not allocate the report buffer.",
                    caption, MB_OK | MB_ICONERROR);
        return 0;
    }
    destination = (char *)GlobalLock(memory);
    if (destination == 0) {
        GlobalFree(memory);
        return 0;
    }
    for (index = 0ul; index < length; ++index) {
        destination[index] = report[index];
    }
    GlobalUnlock(memory);

    if (!OpenClipboard(window)) {
        GlobalFree(memory);
        MessageBoxA(window, "Could not open the clipboard.", caption,
                    MB_OK | MB_ICONERROR);
        return 0;
    }
    EmptyClipboard();
    if (SetClipboardData(CF_TEXT, memory) == 0) {
        CloseClipboard();
        GlobalFree(memory);
        MessageBoxA(window, "Could not copy the report.", caption,
                    MB_OK | MB_ICONERROR);
        return 0;
    }
    CloseClipboard();
    MessageBoxA(window, "The diagnostic report is on the clipboard.",
                caption, MB_OK | MB_ICONINFORMATION);
    return 1;
}
