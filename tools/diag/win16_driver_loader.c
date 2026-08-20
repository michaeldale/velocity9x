#include <windows.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

static const char v9x_title[] = "Velocity9x Win16 probe " V9X_BUILD_ID;
static BYTE v9x_gdi_info[128];

typedef WORD (FAR PASCAL *V9X_ENABLE_PROC)(LPVOID, WORD, LPSTR, LPSTR, LPVOID);
typedef WORD (FAR PASCAL *V9X_VALIDATE_PROC)(LPVOID);

typedef struct v9x_probe_mode {
    WORD size;
    WORD bits_per_pixel;
    short width;
    short height;
} V9X_PROBE_MODE;

static int v9x_supported_mode(WORD width, WORD height, WORD bits_per_pixel)
{
#ifdef V9X_TARGET_MATROX_MILLENNIUM2
#ifdef V9X_MATROX_16BPP
    return ((width == 640u && height == 480u) ||
            (width == 800u && height == 600u) ||
            (width == 1024u && height == 768u)) && bits_per_pixel == 16u;
#else
    return width == 640u && height == 480u && bits_per_pixel == 8u;
#endif
#else
    int supported_resolution =
        (width == 640u && height == 480u) ||
        (width == 800u && height == 600u) ||
        (width == 1024u && height == 768u);
    return supported_resolution &&
           (bits_per_pixel == 8u || bits_per_pixel == 16u);
#endif
}

static int v9x_is_quiet(const char FAR *command_line)
{
    if (command_line == 0) {
        return 0;
    }
    while (*command_line == ' ' || *command_line == '\t') {
        ++command_line;
    }
    return (command_line[0] == '/' || command_line[0] == '-') &&
           (command_line[1] == 'q' || command_line[1] == 'Q');
}

#pragma off (unreferenced)
int PASCAL WinMain(HINSTANCE instance,
                   HINSTANCE previous_instance,
                   LPSTR command_line,
                   int show_command)
#pragma on (unreferenced)
{
    HINSTANCE driver;
    V9X_ENABLE_PROC enable_proc;
    V9X_VALIDATE_PROC validate_proc;
    V9X_PROBE_MODE mode;
    WORD FAR *gdi_words = (WORD FAR *)v9x_gdi_info;
#if !defined(V9X_TARGET_MATROX_MILLENNIUM2) || defined(V9X_MATROX_16BPP)
    static const WORD widths[] = { 640u, 800u, 1024u };
    static const WORD heights[] = { 480u, 600u, 768u };
    WORD index;
#ifndef V9X_TARGET_MATROX_MILLENNIUM2
    WORD depth;
#endif
#endif
    int quiet = v9x_is_quiet(command_line);

    driver = LoadLibrary("V9XDISP.DRV");
    if ((UINT)driver < 32u) {
        if (!quiet) {
            MessageBox(0,
                       "Loading V9XDISP.DRV as an inactive library failed. "
                       "Keep V9X16LD.EXE and V9XDISP.DRV together.",
                       v9x_title,
                       MB_OK | MB_ICONHAND);
        }
        return 1;
    }

    enable_proc = (V9X_ENABLE_PROC)GetProcAddress(driver, "Enable");
    validate_proc = (V9X_VALIDATE_PROC)GetProcAddress(driver, "ValidateMode");
    if (enable_proc == 0 || validate_proc == 0) {
        FreeLibrary(driver);
        if (!quiet) {
            MessageBox(0, "Required display entry points were not found.",
                       v9x_title, MB_OK | MB_ICONHAND);
        }
        return 2;
    }

    if (enable_proc(v9x_gdi_info, 1u, 0, 0, 0) != 110u ||
        gdi_words[0] != 0x0400u ||
        !v9x_supported_mode(gdi_words[4], gdi_words[5], gdi_words[6]) ||
        gdi_words[7] != 1u || gdi_words[13] <= 48u) {
        FreeLibrary(driver);
        if (!quiet) {
            MessageBox(0, "The DIB Engine/GDIINFO inquiry failed.",
                       v9x_title, MB_OK | MB_ICONHAND);
        }
        return 3;
    }

#ifdef V9X_TARGET_MATROX_MILLENNIUM2
#ifdef V9X_MATROX_16BPP
    for (index = 0u; index < 3u; ++index) {
        mode.size = sizeof(mode);
        mode.bits_per_pixel = 16u;
        mode.width = (short)widths[index];
        mode.height = (short)heights[index];
        if (validate_proc(&mode) != 0u) {
            FreeLibrary(driver);
            return 4;
        }
    }
#else
    mode.size = sizeof(mode);
    mode.bits_per_pixel = 8u;
    mode.width = 640;
    mode.height = 480;
    if (validate_proc(&mode) != 0u) {
        FreeLibrary(driver);
        if (!quiet) {
            MessageBox(0, "The guarded 640x480 Matrox mode was rejected.",
                       v9x_title, MB_OK | MB_ICONHAND);
        }
        return 4;
    }
#endif
#else
    for (depth = 8u; depth <= 16u; depth += 8u) {
        for (index = 0u; index < 3u; ++index) {
            mode.size = sizeof(mode);
            mode.bits_per_pixel = depth;
            mode.width = (short)widths[index];
            mode.height = (short)heights[index];
            if (validate_proc(&mode) != 0u) {
                FreeLibrary(driver);
                if (!quiet) {
                    MessageBox(0, "A supported mode was rejected.",
                               v9x_title, MB_OK | MB_ICONHAND);
                }
                return 4;
            }
        }
    }
#endif
    /*
     * A geometry no family table has or plausibly will, to prove ValidateMode
     * is still a whitelist rather than an accept-all.
     *
     * This asked about 1280x1024x8 until the S3 families gained that mode, at
     * which point the probe started failing on a correct driver - and because
     * update-associated-driver.ps1 runs it as its preflight, that blocked every
     * deploy. Pick something outside the range any card here can scan out.
     */
    mode.size = sizeof(mode);
    mode.bits_per_pixel = 8u;
    mode.width = 2048;
    mode.height = 1536;
    if (validate_proc(&mode) == 0u) {
        FreeLibrary(driver);
        if (!quiet) {
            MessageBox(0, "An unsupported mode was incorrectly accepted.",
                       v9x_title, MB_OK | MB_ICONHAND);
        }
        return 5;
    }
    mode.bits_per_pixel = 24u;
    mode.width = 640;
    mode.height = 480;
    if (validate_proc(&mode) == 0u) {
        FreeLibrary(driver);
        if (!quiet) {
            MessageBox(0, "An unsupported colour depth was incorrectly accepted.",
                       v9x_title, MB_OK | MB_ICONHAND);
        }
        return 6;
    }

    if (!quiet) {
        MessageBox(0,
#ifdef V9X_TARGET_MATROX_MILLENNIUM2
                   "V9XDISP.DRV passed its DIB Engine inquiry and guarded "
                   "640x480 validation without enabling the display. "
                   "Click OK to unload it.",
#else
                   "V9XDISP.DRV passed its DIB Engine inquiry and all six "
                   "mode validations without enabling the display. Click "
                   "OK to unload it.",
#endif
                   v9x_title,
                   MB_OK | MB_ICONINFORMATION);
    }
    FreeLibrary(driver);
    if (!quiet) {
        MessageBox(0,
                   "V9XDISP.DRV unloaded successfully.",
                   v9x_title,
                   MB_OK | MB_ICONINFORMATION);
    }
    return 0;
}
