/* Scratch: draw filled shapes and lines straight to a window's screen DC
 * and hold the window, so the frame buffer (emulator capture) can be
 * compared with the shadow (GDI capture). Not part of the repository. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static void draw(HDC dc)
{
    POINT star[5];
    POINT zig[6];
    HBRUSH red = CreateSolidBrush(RGB(220, 20, 20));
    HBRUSH hatch = CreateHatchBrush(HS_DIAGCROSS, RGB(0, 120, 0));
    HPEN blue = CreatePen(PS_SOLID, 1, RGB(0, 0, 200));
    HPEN dash = CreatePen(PS_DASH, 1, RGB(0, 0, 0));
    HGDIOBJ old_brush = SelectObject(dc, red);
    HGDIOBJ old_pen = SelectObject(dc, blue);

    Ellipse(dc, 20, 20, 220, 160);
    SelectObject(dc, hatch);
    RoundRect(dc, 240, 20, 440, 160, 40, 40);
    Pie(dc, 460, 20, 640, 200, 640, 110, 460, 20);
    star[0].x = 120; star[0].y = 180;
    star[1].x = 180; star[1].y = 360;
    star[2].x = 30;  star[2].y = 250;
    star[3].x = 210; star[3].y = 250;
    star[4].x = 60;  star[4].y = 360;
    SetPolyFillMode(dc, WINDING);
    Polygon(dc, star, 5);
    SelectObject(dc, red);
    Chord(dc, 240, 190, 440, 360, 240, 275, 440, 275);
    SelectObject(dc, dash);
    zig[0].x = 460; zig[0].y = 220;
    zig[1].x = 520; zig[1].y = 360;
    zig[2].x = 560; zig[2].y = 230;
    zig[3].x = 600; zig[3].y = 350;
    zig[4].x = 630; zig[4].y = 240;
    zig[5].x = 470; zig[5].y = 300;
    Polyline(dc, zig, 6);
    MoveToEx(dc, 10, 380, 0);
    LineTo(dc, 640, 420);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(red);
    DeleteObject(hatch);
    DeleteObject(blue);
    DeleteObject(dash);
}

static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);

        draw(dc);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_TIMER) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, w, l);
}

void __stdcall ShapesEntry(void)
{
    WNDCLASSA wc;
    HWND window;
    MSG msg;
    HINSTANCE instance = GetModuleHandleA(0);

    wc.style = 0;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hIcon = 0;
    wc.lpszMenuName = 0;
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = "V9xShapes";
    wc.hCursor = LoadCursorA(0, IDC_ARROW);
    RegisterClassA(&wc);
    window = CreateWindowA("V9xShapes", "shapes", WS_POPUP | WS_VISIBLE,
                           100, 100, 660, 440, 0, 0, instance, 0);
    SetTimer(window, 1, 45000, 0);
    while (GetMessageA(&msg, 0, 0, 0)) {
        DispatchMessageA(&msg);
    }
    ExitProcess(0);
}
