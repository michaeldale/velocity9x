/*
 * Live mode-switch exerciser for the Velocity9x bring-up guest.
 *
 * Drives ChangeDisplaySettingsA the same way Display Properties does and
 * verifies the result with GetDeviceCaps plus a pixel write/readback.
 * Records machine-readable results in C:\V9XDIAG\V9XMSW.INI.
 *
 *   V9XMSW /set:800x600x8    switch to one mode and verify it
 *   V9XMSW /cycle:20         alternate 640x480 and 800x600 at the current
 *                            depth the requested number of times
 *   V9XMSW /depth:20         alternate 8 and 16 bpp at the current
 *                            resolution the requested number of times
 *   V9XMSW /cursor           add cursor agitation around every switch
 *   V9XMSW                   no arguments: default /cycle:2 run, so a bare
 *                            double-click on a physical machine still tests
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_RESULT_PATH V9X_DIAG_MSW_INI
#define V9X_SECTION     "Velocity9xModeSwitch"

/* /cursor: keep the pointer moving and redrawing across every switch. */
static UINT v9x_cursor_stress;

static void v9x_uint_text(char *text, DWORD value)
{
    char reverse[12];
    int count = 0;
    int index;

    do {
        reverse[count++] = (char)('0' + (value % 10ul));
        value /= 10ul;
    } while (value != 0ul);
    for (index = 0; index < count; ++index) {
        text[index] = reverse[count - index - 1];
    }
    text[count] = '\0';
}

static void v9x_int_text(char *text, LONG value)
{
    if (value < 0) {
        *text++ = '-';
        value = -value;
    }
    v9x_uint_text(text, (DWORD)value);
}

static void v9x_write_text(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_SECTION, key, value, V9X_RESULT_PATH);
}

/*
 * Flush the profile cache.
 *
 * Windows caches .INI writes, and a mode change immediately before process
 * exit discarded everything written after the last ChangeDisplaySettings -
 * the run reported nine of ten cycles and no verdict while still exiting
 * zero, because the tail of the file never reached disk. Passing a null
 * section is the documented way to commit it.
 */
static void v9x_flush_results(void)
{
    WritePrivateProfileStringA(0, 0, 0, V9X_RESULT_PATH);
}

static void v9x_write_uint(const char *key, DWORD value)
{
    char text[12];

    v9x_uint_text(text, value);
    v9x_write_text(key, text);
}

static void v9x_write_int(const char *key, LONG value)
{
    char text[13];

    v9x_int_text(text, value);
    v9x_write_text(key, text);
}

static void v9x_query_mode(UINT *width, UINT *height, UINT *bits)
{
    HDC display = GetDC(0);

    *width = (UINT)GetDeviceCaps(display, HORZRES);
    *height = (UINT)GetDeviceCaps(display, VERTRES);
    *bits = (UINT)(GetDeviceCaps(display, BITSPIXEL) *
                   GetDeviceCaps(display, PLANES));
    ReleaseDC(0, display);
}

static LONG v9x_request_mode(UINT width, UINT height, UINT bits)
{
    DEVMODEA mode;
    BYTE *bytes = (BYTE *)&mode;
    UINT index;

    for (index = 0u; index < sizeof(mode); ++index) {
        bytes[index] = 0u;
    }
    mode.dmSize = sizeof(mode);
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    mode.dmPelsWidth = width;
    mode.dmPelsHeight = height;
    mode.dmBitsPerPel = bits;
    return ChangeDisplaySettingsA(&mode, CDS_UPDATEREGISTRY);
}

/* Draw and read back one pixel away from the desktop icons; a mismatch
 * indicates a broken framebuffer mapping after the switch. */
static int v9x_pixel_check(void)
{
    HDC display = GetDC(0);
    COLORREF written;
    COLORREF read_back;
    int passed;

    SetPixelV(display, 30, 30, RGB(255, 0, 255));
    written = GetPixel(display, 30, 30);
    SetPixelV(display, 30, 30, RGB(0, 128, 0));
    read_back = GetPixel(display, 30, 30);
    ReleaseDC(0, display);
    passed = written != CLR_INVALID && read_back != CLR_INVALID &&
             written != read_back;
    return passed;
}

/*
 * Move the pointer across the screen and force it to be redrawn.
 *
 * A live mode switch rebuilds the display PDEVICE in place, and the DIB
 * Engine's cursor bookkeeping lives inside that PDEVICE. Hellbender faulted
 * inside DIBENG's cursor code during exactly that window, so the exerciser
 * has to be able to keep the cursor busy while the switch happens. Driving it
 * from inside this process is the only way to overlap the two: the remote
 * agent serialises its connections, so injected pointer input cannot arrive
 * while a mode switch is executing on that same agent.
 */
static void v9x_agitate_cursor(UINT width, UINT height, UINT steps)
{
    UINT step;

    for (step = 0u; step < steps; ++step) {
        UINT x = (width * ((step * 37u) % 100u)) / 100u;
        UINT y = (height * ((step * 61u) % 100u)) / 100u;

        SetCursorPos((int)x, (int)y);
        /* ShowCursor toggling makes the DIB Engine remove and redraw the
         * cursor, which is what touches the save-under buffer. */
        ShowCursor(FALSE);
        ShowCursor(TRUE);
    }
}

static int v9x_switch_and_verify(UINT width, UINT height, UINT bits,
                                 LONG *change_result)
{
    UINT now_width;
    UINT now_height;
    UINT now_bits;

    if (v9x_cursor_stress != 0u) {
        v9x_agitate_cursor(width, height, 24u);
    }
    *change_result = v9x_request_mode(width, height, bits);
    if (v9x_cursor_stress != 0u) {
        v9x_agitate_cursor(width, height, 24u);
    }
    if (*change_result != DISP_CHANGE_SUCCESSFUL) {
        return 0;
    }
    v9x_query_mode(&now_width, &now_height, &now_bits);
    if (now_width != width || now_height != height || now_bits != bits) {
        return 0;
    }
    return v9x_pixel_check();
}

static const char *v9x_find_switch(const char *command_line,
                                   const char *option)
{
    UINT offset;
    UINT index;

    for (offset = 0u; command_line[offset] != '\0'; ++offset) {
        for (index = 0u; option[index] != '\0'; ++index) {
            char left = command_line[offset + index];
            char right = option[index];

            if (left >= 'A' && left <= 'Z') {
                left = (char)(left + ('a' - 'A'));
            }
            if (left != right) {
                break;
            }
        }
        if (option[index] == '\0') {
            return command_line + offset + index;
        }
    }
    return 0;
}

static UINT v9x_parse_uint(const char **cursor)
{
    UINT value = 0u;

    while (**cursor >= '0' && **cursor <= '9') {
        value = value * 10u + (UINT)(**cursor - '0');
        ++*cursor;
    }
    return value;
}

void __stdcall V9xModeSwitchEntry(void)
{
    const char *command_line = GetCommandLineA();
    const char *argument;
    UINT start_width;
    UINT start_height;
    UINT start_bits;
    LONG change_result;
    DWORD exit_code = 1u;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    WritePrivateProfileStringA(V9X_SECTION, 0, 0, V9X_RESULT_PATH);
    v9x_write_text("Build", V9X_BUILD_ID);
    v9x_write_text("Result", "INCOMPLETE");
    v9x_query_mode(&start_width, &start_height, &start_bits);
    v9x_write_uint("StartW", start_width);
    v9x_write_uint("StartH", start_height);
    v9x_write_uint("StartBpp", start_bits);

    argument = v9x_find_switch(command_line, "/set:");
    if (argument != 0) {
        UINT width = v9x_parse_uint(&argument);
        UINT height;
        UINT bits;

        ++argument;
        height = v9x_parse_uint(&argument);
        ++argument;
        bits = v9x_parse_uint(&argument);
        v9x_write_uint("RequestW", width);
        v9x_write_uint("RequestH", height);
        v9x_write_uint("RequestBpp", bits);
        if (v9x_switch_and_verify(width, height, bits, &change_result)) {
            v9x_write_text("Result", "PASS");
            exit_code = 0u;
        } else {
            v9x_write_text("Result", "FAIL");
        }
        v9x_write_int("ChangeResult", change_result);
        ExitProcess(exit_code);
    }

    v9x_cursor_stress = v9x_find_switch(command_line, "/cursor") != 0 ? 1u : 0u;
    v9x_write_uint("CursorStress", v9x_cursor_stress);

    argument = v9x_find_switch(command_line, "/cycle:");
    if (argument != 0) {
        UINT rounds = v9x_parse_uint(&argument);
        UINT completed = 0u;
        UINT round;
        int at_alternate = 0;

        v9x_write_uint("RequestedCycles", rounds);
        for (round = 0u; round < rounds; ++round) {
            UINT width = at_alternate != 0 ? start_width : 640u;
            UINT height = at_alternate != 0 ? start_height : 480u;

            if (at_alternate == 0 && start_width == 640u &&
                start_height == 480u) {
                width = 800u;
                height = 600u;
            }
            if (!v9x_switch_and_verify(width, height, start_bits,
                                       &change_result)) {
                v9x_write_int("ChangeResult", change_result);
                break;
            }
            at_alternate = !at_alternate;
            ++completed;
            v9x_write_uint("CompletedCycles", completed);
        }
        if (at_alternate != 0) {
            (void)v9x_switch_and_verify(start_width, start_height,
                                        start_bits, &change_result);
        }
        v9x_write_uint("CompletedCycles", completed);
        if (completed == rounds) {
            v9x_write_text("Result", "PASS");
            exit_code = 0u;
        } else {
            v9x_write_text("Result", "FAIL");
        }
        v9x_flush_results();
        ExitProcess(exit_code);
    }

    argument = v9x_find_switch(command_line, "/depth:");
    if (argument != 0) {
        /* Alternate 8 and 16 bpp at the current resolution. Every switch has
         * to be honoured live and leave a readable framebuffer behind: the
         * driver now rebuilds the PDEVICE in place across a depth change
         * instead of demanding a restart. */
        UINT rounds = v9x_parse_uint(&argument);
        UINT completed = 0u;
        UINT round;

        v9x_write_uint("RequestedDepthCycles", rounds);
        for (round = 0u; round < rounds; ++round) {
            UINT bits = (round & 1u) != 0u ? start_bits
                                           : (start_bits == 8u ? 16u : 8u);

            if (!v9x_switch_and_verify(start_width, start_height, bits,
                                       &change_result)) {
                v9x_write_uint("FailedAtBpp", bits);
                v9x_write_int("ChangeResult", change_result);
                break;
            }
            ++completed;
            v9x_write_uint("CompletedDepthCycles", completed);
        }
        (void)v9x_request_mode(start_width, start_height, start_bits);
        v9x_write_uint("CompletedDepthCycles", completed);
        if (completed == rounds) {
            v9x_write_text("Result", "PASS");
            exit_code = 0u;
        } else {
            v9x_write_text("Result", "FAIL");
        }
        v9x_flush_results();
        ExitProcess(exit_code);
    }

    /* No arguments: run a default two-cycle resolution exercise instead of
     * writing NO-ARGUMENT and exiting. On the automated guests there is
     * always a command line, but on a physical machine reached by carrying a
     * USB stick to it, double-clicking the tool is the only way it runs -
     * and a diagnostic that no-ops when double-clicked wastes the trip. */
    {
        UINT completed = 0u;
        UINT round;
        int at_alternate = 0;

        v9x_write_text("DefaultRun", "cycle:2");
        v9x_write_uint("RequestedCycles", 2u);
        for (round = 0u; round < 2u; ++round) {
            UINT width = at_alternate != 0 ? start_width : 640u;
            UINT height = at_alternate != 0 ? start_height : 480u;

            if (at_alternate == 0 && start_width == 640u &&
                start_height == 480u) {
                width = 800u;
                height = 600u;
            }
            if (!v9x_switch_and_verify(width, height, start_bits,
                                       &change_result)) {
                v9x_write_int("ChangeResult", change_result);
                break;
            }
            at_alternate = !at_alternate;
            ++completed;
            v9x_write_uint("CompletedCycles", completed);
        }
        if (at_alternate != 0) {
            (void)v9x_switch_and_verify(start_width, start_height,
                                        start_bits, &change_result);
        }
        v9x_write_uint("CompletedCycles", completed);
        if (completed == 2u) {
            v9x_write_text("Result", "PASS");
            exit_code = 0u;
        } else {
            v9x_write_text("Result", "FAIL");
        }
        v9x_flush_results();
        ExitProcess(exit_code);
    }
}
