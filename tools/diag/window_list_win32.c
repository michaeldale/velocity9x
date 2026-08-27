/*
 * GDI-free top-level window inventory for fullscreen wedge diagnosis.
 * Writes C:\V9XDIAG\V9XWND.INI so the host can identify hidden dialogs
 * without invoking a screenshot path while the display driver is unstable.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_WINDOW_PATH V9X_DIAG_WND_INI
#define V9X_SECTION     "Velocity9xWindows"
#define V9X_MAX_WINDOWS 128ul

static DWORD v9x_window_count;

static char *v9x_append_text(char *cursor, char *end, const char *text)
{
    while (cursor < end && *text != '\0') {
        char value = *text++;

        if (value == '\r' || value == '\n' || value == ';') {
            value = ' ';
        }
        *cursor++ = value;
    }
    return cursor;
}

static char *v9x_append_uint(char *cursor, char *end, DWORD value)
{
    char reverse[12];
    unsigned count = 0u;

    do {
        reverse[count++] = (char)('0' + value % 10ul);
        value /= 10ul;
    } while (value != 0ul);
    while (count != 0u && cursor < end) {
        *cursor++ = reverse[--count];
    }
    return cursor;
}

static void v9x_uint_text(char *text, DWORD value)
{
    char *end = v9x_append_uint(text, text + 11, value);

    *end = '\0';
}

static BOOL CALLBACK v9x_enum_window(HWND window, LPARAM context)
{
    char key[16];
    char class_name[128];
    char title[256];
    char value[512];
    char *cursor = value;
    char *end = value + sizeof(value) - 1;
    DWORD process_id = 0ul;

    (void)context;
    if (v9x_window_count >= V9X_MAX_WINDOWS) {
        return FALSE;
    }
    class_name[0] = '\0';
    title[0] = '\0';
    GetClassNameA(window, class_name, sizeof(class_name));
    GetWindowTextA(window, title, sizeof(title));
    GetWindowThreadProcessId(window, &process_id);

    cursor = v9x_append_text(cursor, end, "Visible=");
    cursor = v9x_append_uint(cursor, end,
                             IsWindowVisible(window) ? 1ul : 0ul);
    cursor = v9x_append_text(cursor, end, ";Pid=");
    cursor = v9x_append_uint(cursor, end, process_id);
    cursor = v9x_append_text(cursor, end, ";Class=");
    cursor = v9x_append_text(cursor, end, class_name);
    cursor = v9x_append_text(cursor, end, ";Title=");
    cursor = v9x_append_text(cursor, end, title);
    *cursor = '\0';

    key[0] = 'W';
    key[1] = 'i';
    key[2] = 'n';
    key[3] = 'd';
    key[4] = 'o';
    key[5] = 'w';
    v9x_uint_text(key + 6, v9x_window_count);
    WritePrivateProfileStringA(V9X_SECTION, key, value, V9X_WINDOW_PATH);
    ++v9x_window_count;
    return TRUE;
}

void __stdcall V9xWindowListEntry(void)
{
    char count[12];

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    WritePrivateProfileStringA(V9X_SECTION, 0, 0, V9X_WINDOW_PATH);
    WritePrivateProfileStringA(V9X_SECTION, "Build", V9X_BUILD_ID,
                               V9X_WINDOW_PATH);
    WritePrivateProfileStringA(V9X_SECTION, "Result", "INCOMPLETE",
                               V9X_WINDOW_PATH);
    v9x_window_count = 0ul;
    EnumWindows(v9x_enum_window, 0l);
    v9x_uint_text(count, v9x_window_count);
    WritePrivateProfileStringA(V9X_SECTION, "Count", count,
                               V9X_WINDOW_PATH);
    WritePrivateProfileStringA(V9X_SECTION, "Result", "COMPLETE",
                               V9X_WINDOW_PATH);
    WritePrivateProfileStringA(0, 0, 0, V9X_WINDOW_PATH);
    ExitProcess(0u);
}
