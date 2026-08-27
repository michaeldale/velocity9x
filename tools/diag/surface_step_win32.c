#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_RESULT V9X_DIAG_SURF_INI

static int v9x_near(BYTE actual, BYTE expected)
{
    int difference = (int)actual - (int)expected;
    if (difference < 0) difference = -difference;
    return difference <= 8;
}

static int v9x_color_near(COLORREF actual, COLORREF expected)
{
    return actual != CLR_INVALID &&
           v9x_near(GetRValue(actual), GetRValue(expected)) &&
           v9x_near(GetGValue(actual), GetGValue(expected)) &&
           v9x_near(GetBValue(actual), GetBValue(expected));
}

static void v9x_stage(const char *stage)
{
    WritePrivateProfileStringA("Velocity9xSurface", "Stage", stage,
                               V9X_RESULT);
}

static void v9x_hex_color(char *text, COLORREF color)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;
    for (shift = 20; shift >= 0; shift -= 4) {
        text[(20 - shift) / 4] = digits[(color >> shift) & 15u];
    }
    text[6] = '\0';
}

void WINAPI V9xSurfaceStepEntry(void)
{
    HINSTANCE instance = GetModuleHandleA(0);
    HWND window;
    HDC display;
    HBRUSH brush;
    HDC memory;
    HBITMAP bitmap;
    HBITMAP previous_bitmap;
    RECT area;
    COLORREF pixel;
    COLORREF set_result;
    char actual[7];
    char set_actual[7];

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    WritePrivateProfileStringA("Velocity9xSurface", 0, 0, V9X_RESULT);
    WritePrivateProfileStringA("Velocity9xSurface", "Build", V9X_BUILD_ID,
                               V9X_RESULT);
    v9x_stage("create-window");
    window = CreateWindowExA(WS_EX_DLGMODALFRAME, "STATIC",
        "Velocity9x surface step", WS_OVERLAPPED | WS_CAPTION,
        16, 16, 128, 112, 0, 0, instance, 0);
    if (window == 0) ExitProcess(2ul);
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);

    v9x_stage("get-dc");
    display = GetDC(window);
    if (display == 0) ExitProcess(3ul);
    area.left = 8;
    area.top = 8;
    area.right = 72;
    area.bottom = 56;
    brush = CreateSolidBrush(RGB(16, 112, 208));
    if (brush == 0) ExitProcess(4ul);

    v9x_stage("fill-rect");
    FillRect(display, &area, brush);
    v9x_stage("fill-rect-complete");
    memory = CreateCompatibleDC(display);
    bitmap = CreateCompatibleBitmap(display, 32, 24);
    if (memory == 0 || bitmap == 0) ExitProcess(6ul);
    previous_bitmap = (HBITMAP)SelectObject(memory, bitmap);
    area.left = 0;
    area.top = 0;
    area.right = 32;
    area.bottom = 24;
    FillRect(memory, &area, brush);
    v9x_stage("bitblt");
    BitBlt(display, 80, 8, 32, 24, memory, 0, 0, SRCCOPY);
    v9x_stage("bitblt-complete");
    v9x_stage("stretchblt");
    StretchBlt(display, 80, 40, 40, 30, memory, 0, 0, 32, 24, SRCCOPY);
    v9x_stage("stretchblt-complete");
    SelectObject(memory, previous_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    DeleteObject(brush);
    v9x_stage("set-pixel");
    set_result = SetPixel(display, 32, 32, RGB(240, 32, 48));
    v9x_hex_color(set_actual, set_result);
    WritePrivateProfileStringA("Velocity9xSurface", "SetPixelResult",
                               set_actual, V9X_RESULT);
    GdiFlush();
    v9x_stage("get-pixel");
    pixel = GetPixel(display, 32, 32);
    v9x_hex_color(actual, pixel);
    WritePrivateProfileStringA("Velocity9xSurface", "ReadbackColorRef",
                               actual, V9X_RESULT);
    WritePrivateProfileStringA("Velocity9xSurface", "Readback",
        v9x_color_near(pixel, RGB(240, 32, 48)) ? "match" : "mismatch",
        V9X_RESULT);
    WritePrivateProfileStringA("Velocity9xSurface", "PixelApi",
        (set_result == CLR_INVALID || pixel == CLR_INVALID) ?
            "unsupported" : "available", V9X_RESULT);
    v9x_stage("PASS-completed");
    ReleaseDC(window, display);
    DestroyWindow(window);
    ExitProcess(0ul);
}
