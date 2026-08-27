#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_PALETTE_COUNT 256u
#define V9X_TEST_INDEX     20u

static const char v9x_title[] = "Velocity9x 8-bit palette test";
static const char v9x_result_path[] = V9X_DIAG_PAL_INI;

static int v9x_ascii_equal_ci(char left, char right)
{
    if (left >= 'A' && left <= 'Z') left += (char)('a' - 'A');
    if (right >= 'A' && right <= 'Z') right += (char)('a' - 'A');
    return left == right;
}

static int v9x_has_auto_switch(const char *command_line)
{
    static const char option[] = "/auto";
    int offset;
    int index;
    for (offset = 0; command_line[offset] != '\0'; ++offset) {
        for (index = 0; option[index] != '\0'; ++index) {
            if (command_line[offset + index] == '\0' ||
                !v9x_ascii_equal_ci(command_line[offset + index],
                                    option[index])) break;
        }
        if (option[index] == '\0') return 1;
    }
    return 0;
}

static void v9x_uint_text(char *text, UINT value)
{
    char reverse[12];
    int count = 0;
    int index;
    do {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    for (index = 0; index < count; ++index)
        text[index] = reverse[count - index - 1];
    text[count] = '\0';
}

static void v9x_write_result(HDC display,
                             const char *result,
                             const char *reason)
{
    char number[12];
    WritePrivateProfileStringA("Velocity9xPalette", 0, 0, v9x_result_path);
    WritePrivateProfileStringA("Velocity9xPalette", "Result", result,
                               v9x_result_path);
    WritePrivateProfileStringA("Velocity9xPalette", "Reason", reason,
                               v9x_result_path);
    WritePrivateProfileStringA("Velocity9xPalette", "Build", V9X_BUILD_ID,
                               v9x_result_path);
    v9x_uint_text(number, (UINT)GetDeviceCaps(display, HORZRES));
    WritePrivateProfileStringA("Velocity9xPalette", "Width", number,
                               v9x_result_path);
    v9x_uint_text(number, (UINT)GetDeviceCaps(display, VERTRES));
    WritePrivateProfileStringA("Velocity9xPalette", "Height", number,
                               v9x_result_path);
    v9x_uint_text(number, (UINT)(GetDeviceCaps(display, BITSPIXEL) *
                                GetDeviceCaps(display, PLANES)));
    WritePrivateProfileStringA("Velocity9xPalette", "BitsPerPixel", number,
                               v9x_result_path);
}

static int v9x_channel_near(BYTE actual, BYTE expected)
{
    int difference = (int)actual - (int)expected;
    if (difference < 0) difference = -difference;
    return difference <= 8;
}

static int v9x_color_near(COLORREF actual, PALETTEENTRY expected)
{
    return actual != CLR_INVALID &&
           v9x_channel_near(GetRValue(actual), expected.peRed) &&
           v9x_channel_near(GetGValue(actual), expected.peGreen) &&
           v9x_channel_near(GetBValue(actual), expected.peBlue);
}

void WINAPI V9xPaletteSmokeEntry(void)
{
    const int auto_mode = v9x_has_auto_switch(GetCommandLineA());
    HINSTANCE instance = GetModuleHandleA(0);
    HWND window;
    HDC display;
    LOGPALETTE *logical;
    HPALETTE palette;
    HPALETTE previous;
    PALETTEENTRY before;
    PALETTEENTRY after;
    PALETTEENTRY readback;
    COLORREF before_pixel;
    COLORREF after_pixel;
    DWORD bytes;
    UINT index;
    int passed;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    window = CreateWindowExA(WS_EX_DLGMODALFRAME, "STATIC", v9x_title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        (GetSystemMetrics(SM_CXSCREEN) - 360) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - 180) / 2,
        360, 180, 0, 0, instance, 0);
    if (window == 0) ExitProcess(2ul);
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    display = GetDC(window);
    if ((UINT)(GetDeviceCaps(display, BITSPIXEL) *
               GetDeviceCaps(display, PLANES)) != 8u) {
        v9x_write_result(display, "SKIP", "requires-8-bpp");
        ReleaseDC(window, display);
        DestroyWindow(window);
        ExitProcess(0ul);
    }

    bytes = sizeof(LOGPALETTE) +
            (V9X_PALETTE_COUNT - 1u) * sizeof(PALETTEENTRY);
    logical = (LOGPALETTE *)GlobalAlloc(GPTR, bytes);
    if (logical == 0) ExitProcess(3ul);
    logical->palVersion = 0x0300u;
    logical->palNumEntries = V9X_PALETTE_COUNT;
    for (index = 0u; index < V9X_PALETTE_COUNT; ++index) {
        logical->palPalEntry[index].peRed = (BYTE)index;
        logical->palPalEntry[index].peGreen = (BYTE)index;
        logical->palPalEntry[index].peBlue = (BYTE)index;
        logical->palPalEntry[index].peFlags = PC_NOCOLLAPSE;
    }
    before.peRed = 24u;
    before.peGreen = 72u;
    before.peBlue = 156u;
    before.peFlags = PC_RESERVED;
    logical->palPalEntry[V9X_TEST_INDEX] = before;
    palette = CreatePalette(logical);
    GlobalFree(logical);
    if (palette == 0) ExitProcess(4ul);

    previous = SelectPalette(display, palette, FALSE);
    RealizePalette(display);
    SetPixel(display, 80, 70, PALETTEINDEX(V9X_TEST_INDEX));
    GdiFlush();
    before_pixel = GetPixel(display, 80, 70);

    after.peRed = 204u;
    after.peGreen = 44u;
    after.peBlue = 212u;
    after.peFlags = PC_RESERVED;
    passed = AnimatePalette(palette, V9X_TEST_INDEX, 1u, &after) &&
             GetPaletteEntries(palette, V9X_TEST_INDEX, 1u, &readback) == 1u;
    GdiFlush();
    after_pixel = GetPixel(display, 80, 70);
    passed = passed && v9x_color_near(before_pixel, before) &&
             v9x_color_near(after_pixel, after) &&
             v9x_color_near(RGB(readback.peRed, readback.peGreen,
                                readback.peBlue), after) &&
             before_pixel != after_pixel;
    v9x_write_result(display, passed ? "PASS" : "FAIL",
                     passed ? "palette-animation-and-screen-readback" :
                              "palette-readback-mismatch");

    SelectPalette(display, previous, FALSE);
    RealizePalette(display);
    DeleteObject(palette);
    ReleaseDC(window, display);
    if (!auto_mode) {
        MessageBoxA(window,
            passed ? "PASS: palette animation and screen readback agree." :
                     "FAIL: palette animation did not reach the framebuffer.",
            v9x_title, MB_OK | (passed ? MB_ICONINFORMATION : MB_ICONERROR));
    }
    DestroyWindow(window);
    ExitProcess(passed ? 0ul : 5ul);
}
