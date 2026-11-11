/* Scratch: time scrolling in each direction on the screen. An 800x600
 * window filled with a pattern; for each direction, 200 overlapping BitBlts
 * of the window by 16 pixels and 200 ScrollDC calls, timed with the
 * performance counter after one untimed warm-up. Microseconds per call in
 * C:\V9XDIAG\SCROLLT.TXT. Not part of the repository. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define ST_W 800
#define ST_H 600
#define ST_STEP 16
#define ST_CALLS 20

static HANDLE st_file = INVALID_HANDLE_VALUE;

static void st_line(const char *key, DWORD value)
{
    char text[80];
    DWORD written;
    int n = wsprintfA(text, "%s=%lu\r\n", key, value);
    WriteFile(st_file, text, (DWORD)n, &written, 0);
}

static void st_fill(HDC dc)
{
    int y;
    for (y = 0; y < ST_H; y += 8) {
        RECT r;
        HBRUSH b = CreateSolidBrush(RGB((y * 3) & 255, (y * 7) & 255, 128));
        r.left = 0;
        r.top = y;
        r.right = ST_W;
        r.bottom = y + 8;
        FillRect(dc, &r, b);
        DeleteObject(b);
    }
}

static DWORD st_time(HDC dc, int kind, int dx, int dy)
{
    LARGE_INTEGER f;
    LARGE_INTEGER t0;
    LARGE_INTEGER t1;
    RECT all;
    int i;

    all.left = 0;
    all.top = 0;
    all.right = ST_W;
    all.bottom = ST_H;
    QueryPerformanceFrequency(&f);
    for (i = -1; i < ST_CALLS; ++i) {
        if (i == 0) {
            GdiFlush();
            QueryPerformanceCounter(&t0);
        }
        if (kind == 0) {
            BitBlt(dc, dx > 0 ? dx : 0, dy > 0 ? dy : 0,
                   ST_W - (dx > 0 ? dx : -dx), ST_H - (dy > 0 ? dy : -dy),
                   dc, dx < 0 ? -dx : 0, dy < 0 ? -dy : 0, SRCCOPY);
        } else {
            ScrollDC(dc, dx, dy, &all, &all, 0, 0);
        }
    }
    GdiFlush();
    QueryPerformanceCounter(&t1);
    /* 32-bit only (no runtime): ticks per call, then microseconds. */
    return ((t1.LowPart - t0.LowPart) / ST_CALLS) * 1000ul /
           (f.LowPart / 1000ul);
}

void __stdcall ScrolltEntry(void)
{
    WNDCLASSA wc;
    HWND window;
    HDC dc;
    HINSTANCE instance = GetModuleHandleA(0);
    static const char *names[2] = { "Blt", "ScrollDC" };
    static const int dirs[4][2] = {
        { 0, -ST_STEP }, { 0, ST_STEP }, { -ST_STEP, 0 }, { ST_STEP, 0 }
    };
    static const char *dir_names[4] = { "Up", "Down", "Left", "Right" };
    int kind;
    int d;
    char key[32];

    st_file = CreateFileA("C:\\V9XDIAG\\SCROLLT.TXT", GENERIC_WRITE, 0, 0,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    wc.style = CS_OWNDC;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hIcon = 0;
    wc.lpszMenuName = 0;
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = instance;
    wc.hbrBackground = 0;
    wc.lpszClassName = "V9xScrollt";
    wc.hCursor = LoadCursorA(0, IDC_ARROW);
    RegisterClassA(&wc);
    window = CreateWindowA("V9xScrollt", "scrollt", WS_POPUP | WS_VISIBLE,
                           0, 0, ST_W, ST_H, 0, 0, instance, 0);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    dc = GetDC(window);
    for (kind = 0; kind < 2; ++kind) {
        for (d = 0; d < 4; ++d) {
            st_fill(dc);
            wsprintfA(key, "%s%sUs", names[kind], dir_names[d]);
            st_line(key, st_time(dc, kind, dirs[d][0], dirs[d][1]));
        }
    }
    st_line("Done", 1ul);
    CloseHandle(st_file);
    ReleaseDC(window, dc);
    DestroyWindow(window);
    ExitProcess(0);
}
