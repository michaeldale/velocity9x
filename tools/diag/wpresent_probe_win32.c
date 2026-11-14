/* Windowed-present probe: presents into a window, timed, then read back
 * through GDI.
 *
 * Written for the release checks after 0.47.0's screen shadow. On a chip
 * whose GDI draws into a copy of the desktop, every DirectDraw blit and every
 * OpenGL SwapBuffers into a window writes the frame buffer behind that copy,
 * and two things can go wrong without any other probe noticing: the present
 * becomes slow (eager read-backs made windowed Quake 3 7.5 times slower on
 * Carrizo), or GDI reads or redraws stale pixels over what was presented.
 * Both were found with a windowed Quake 3 timedemo, which this replaces
 * (docs\decisions\2026-11-12-ati-screen-shadow.md).
 *
 * In a 512x384 top-most window:
 *
 *  1. DirectDraw: 40 colour-filled frames from an off-screen surface blitted
 *     to the window through a clipper, each timed, the cursor moved across
 *     the window every tenth. GDI then reads five points of the window and
 *     must see the last frame.
 *  2. GDI over it: the window's left half filled black through GDI. The left
 *     points must read black and the right points still the last frame:
 *     drawing next to presented pixels must not put stale ones back.
 *  3. OpenGL: 40 cleared frames presented with SwapBuffers, timed, and the
 *     last one read back the same way. NO-GL when no context can be made.
 *
 * The times are recorded, not judged: the release-check record sets them
 * beside the previous release's. Writes C:\V9XDIAG\V9XWPRS.INI. Result=PASS
 * when every read-back matches; 8 bpp desktops are skipped.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <GL/gl.h>
#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"
#include "probe_lock.h"

#define V9X_WPR_PATH "C:\\V9XDIAG\\V9XWPRS.INI"
#define V9X_WPR_SECTION "WindowedPresent"
#define V9X_WPR_LEFT 64
#define V9X_WPR_TOP 64
#define V9X_WPR_WIDTH 512
#define V9X_WPR_HEIGHT 384
#define V9X_WPR_FRAMES 40
/* A read-back channel may differ by this much: a 5:6:5 or 5:5:5 desktop
 * stores red and blue in five bits. */
#define V9X_WPR_TOLERANCE 8
#define V9X_WPR_POINTS 5

typedef HRESULT (WINAPI *V9X_WPR_CREATE)(GUID *, LPDIRECTDRAW *, IUnknown *);

static HWND v9x_wpr_window;
static DWORD v9x_wpr_frequency;

static void v9x_wpr_text(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_WPR_SECTION, key, value, V9X_WPR_PATH);
}

static void v9x_wpr_uint(const char *key, DWORD value)
{
    char text[16];

    wsprintfA(text, "%lu", value);
    v9x_wpr_text(key, text);
}

static void v9x_wpr_hex(const char *key, DWORD value)
{
    char text[16];

    wsprintfA(text, "0x%08lX", value);
    v9x_wpr_text(key, text);
}

/* ZeroMemory is memset, and this probe links no C runtime. */
static void v9x_wpr_zero(void *block, DWORD bytes)
{
    volatile BYTE *at = (volatile BYTE *)block;

    while (bytes-- != 0ul) {
        *at++ = 0u;
    }
}

static LRESULT CALLBACK v9x_wpr_window_proc(HWND window, UINT message,
                                            WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcA(window, message, wparam, lparam);
}

static void v9x_wpr_pump(void)
{
    MSG message;

    while (PeekMessageA(&message, 0, 0u, 0u, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

/* The performance counter's low half: on Windows 9x it counts at 1.19 MHz,
 * so a run's deltas stay far inside 32 bits. */
static DWORD v9x_wpr_now(void)
{
    LARGE_INTEGER count;

    QueryPerformanceCounter(&count);
    return count.u.LowPart;
}

/* Microseconds between two counts, with MulDiv's 64-bit intermediate: this
 * probe links no runtime for a 64-bit divide. */
static DWORD v9x_wpr_us(DWORD start, DWORD end)
{
    if (v9x_wpr_frequency == 0ul) {
        return 0ul;
    }
    return (DWORD)MulDiv((int)(end - start), 1000000, (int)v9x_wpr_frequency);
}

/* The five points read back, in window coordinates: the centre and a point
 * near each corner. Points 1 and 3 lie in the left half, 2 and 4 in the
 * right; the centre is on the boundary and checked only before the fill. */
static const int v9x_wpr_points[V9X_WPR_POINTS][2] = {
    { V9X_WPR_WIDTH / 2, V9X_WPR_HEIGHT / 2 },
    { 32, 32 },
    { V9X_WPR_WIDTH - 33, 32 },
    { 32, V9X_WPR_HEIGHT - 33 },
    { V9X_WPR_WIDTH - 33, V9X_WPR_HEIGHT - 33 }
};

static int v9x_wpr_near(int a, int b)
{
    int difference = a - b;

    return difference <= V9X_WPR_TOLERANCE && difference >= -V9X_WPR_TOLERANCE;
}

/* Whether point `index` of the window reads as (red, green, blue) through
 * GDI, which reads the screen shadow where there is one. */
static int v9x_wpr_point_is(int index, int red, int green, int blue,
                            const char *key_prefix)
{
    HDC screen = GetDC(0);
    COLORREF pixel;
    char key[32];

    pixel = GetPixel(screen, V9X_WPR_LEFT + v9x_wpr_points[index][0],
                     V9X_WPR_TOP + v9x_wpr_points[index][1]);
    ReleaseDC(0, screen);
    wsprintfA(key, "%sPoint%d", key_prefix, index);
    v9x_wpr_hex(key, (DWORD)pixel);
    return v9x_wpr_near(GetRValue(pixel), red) &&
           v9x_wpr_near(GetGValue(pixel), green) &&
           v9x_wpr_near(GetBValue(pixel), blue);
}

/* Phase 1 and 2. Returns 1 when every read-back matched. */
static int v9x_wpr_directdraw(void)
{
    V9X_WPR_CREATE create;
    LPDIRECTDRAW dd = 0;
    LPDIRECTDRAWSURFACE primary = 0;
    LPDIRECTDRAWSURFACE frame = 0;
    LPDIRECTDRAWCLIPPER clipper = 0;
    DDSURFACEDESC desc;
    DDBLTFX fx;
    RECT target;
    RECT half;
    DWORD red_value;
    DWORD blue_value;
    DWORD total_us = 0ul;
    DWORD max_us = 0ul;
    DWORD start;
    DWORD us;
    HDC dc;
    int index;
    int ok = 1;

    create = (V9X_WPR_CREATE)GetProcAddress(LoadLibraryA("DDRAW.DLL"),
                                            "DirectDrawCreate");
    if (create == 0 || create(0, &dd, 0) != DD_OK ||
        IDirectDraw_SetCooperativeLevel(dd, v9x_wpr_window, DDSCL_NORMAL) !=
            DD_OK) {
        v9x_wpr_text("DDrawResult", "FAIL-SETUP");
        return 0;
    }
    v9x_wpr_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    if (IDirectDraw_CreateSurface(dd, &desc, &primary, 0) != DD_OK ||
        IDirectDraw_CreateClipper(dd, 0ul, &clipper, 0) != DD_OK ||
        IDirectDrawClipper_SetHWnd(clipper, 0ul, v9x_wpr_window) != DD_OK ||
        IDirectDrawSurface_SetClipper(primary, clipper) != DD_OK) {
        v9x_wpr_text("DDrawResult", "FAIL-PRIMARY");
        IDirectDraw_Release(dd);
        return 0;
    }
    v9x_wpr_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    IDirectDrawSurface_GetSurfaceDesc(primary, &desc);
    red_value = desc.ddpfPixelFormat.dwRBitMask;
    blue_value = desc.ddpfPixelFormat.dwBBitMask;

    /* The frame in video memory, as a windowed game's back buffer is; system
     * memory only if the heap refuses, and the report says which. */
    v9x_wpr_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    desc.dwWidth = V9X_WPR_WIDTH;
    desc.dwHeight = V9X_WPR_HEIGHT;
    if (IDirectDraw_CreateSurface(dd, &desc, &frame, 0) == DD_OK) {
        v9x_wpr_text("DDrawFrameMemory", "video");
    } else {
        desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
        if (IDirectDraw_CreateSurface(dd, &desc, &frame, 0) != DD_OK) {
            v9x_wpr_text("DDrawResult", "FAIL-FRAME");
            IDirectDraw_Release(dd);
            return 0;
        }
        v9x_wpr_text("DDrawFrameMemory", "system");
    }

    target.left = V9X_WPR_LEFT;
    target.top = V9X_WPR_TOP;
    target.right = V9X_WPR_LEFT + V9X_WPR_WIDTH;
    target.bottom = V9X_WPR_TOP + V9X_WPR_HEIGHT;
    for (index = 0; index < V9X_WPR_FRAMES; ++index) {
        /* Red and blue in turn: the last frame, index 39, is blue. */
        v9x_wpr_zero(&fx, sizeof(fx));
        fx.dwSize = sizeof(fx);
        fx.dwFillColor = (index & 1) != 0 ? blue_value : red_value;
        IDirectDrawSurface_Blt(frame, 0, 0, 0, DDBLT_COLORFILL | DDBLT_WAIT,
                               &fx);
        if (index % 10 == 5) {
            /* The cursor over the window between presents: its save and
             * restore read what the presents left behind. */
            SetCursorPos(V9X_WPR_LEFT + 40 + index * 8,
                         V9X_WPR_TOP + 40 + index * 6);
            v9x_wpr_pump();
        }
        start = v9x_wpr_now();
        if (IDirectDrawSurface_Blt(primary, &target, frame, 0, DDBLT_WAIT,
                                   0) != DD_OK) {
            v9x_wpr_text("DDrawResult", "FAIL-PRESENT");
            ok = 0;
            break;
        }
        us = v9x_wpr_us(start, v9x_wpr_now());
        total_us += us;
        if (us > max_us) {
            max_us = us;
        }
    }
    SetCursorPos(GetSystemMetrics(SM_CXSCREEN) - 1, 0);
    v9x_wpr_pump();
    v9x_wpr_uint("DDrawPresents", (DWORD)index);
    v9x_wpr_uint("DDrawPresentAvgUs", index > 0 ? total_us / (DWORD)index : 0ul);
    v9x_wpr_uint("DDrawPresentMaxUs", max_us);

    if (ok) {
        int read_ok = 1;

        for (index = 0; index < V9X_WPR_POINTS; ++index) {
            read_ok &= v9x_wpr_point_is(index, 0, 0, 255, "DDraw");
        }
        v9x_wpr_text("DDrawReadResult", read_ok ? "PASS" : "FAIL");
        ok &= read_ok;
    }

    if (ok) {
        int gdi_ok = 1;

        half.left = 0;
        half.top = 0;
        half.right = V9X_WPR_WIDTH / 2 - 8;
        half.bottom = V9X_WPR_HEIGHT;
        dc = GetDC(v9x_wpr_window);
        FillRect(dc, &half, (HBRUSH)GetStockObject(BLACK_BRUSH));
        ReleaseDC(v9x_wpr_window, dc);
        GdiFlush();
        gdi_ok &= v9x_wpr_point_is(1, 0, 0, 0, "Gdi");
        gdi_ok &= v9x_wpr_point_is(3, 0, 0, 0, "Gdi");
        gdi_ok &= v9x_wpr_point_is(2, 0, 0, 255, "Gdi");
        gdi_ok &= v9x_wpr_point_is(4, 0, 0, 255, "Gdi");
        v9x_wpr_text("GdiOverResult", gdi_ok ? "PASS" : "FAIL");
        ok &= gdi_ok;
    }

    IDirectDrawSurface_Release(frame);
    IDirectDrawSurface_Release(primary);
    IDirectDrawClipper_Release(clipper);
    IDirectDraw_Release(dd);
    return ok;
}

/* Phase 3. Returns 1 when the read-back matched or there is no OpenGL. */
static int v9x_wpr_opengl(void)
{
    PIXELFORMATDESCRIPTOR pfd;
    HDC dc = GetDC(v9x_wpr_window);
    HGLRC context;
    DWORD total_us = 0ul;
    DWORD max_us = 0ul;
    DWORD start;
    DWORD us;
    int format;
    int index;
    int ok = 1;

    v9x_wpr_zero(&pfd, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 16;
    pfd.cDepthBits = 16;
    pfd.iLayerType = PFD_MAIN_PLANE;
    format = ChoosePixelFormat(dc, &pfd);
    if (format == 0 || !SetPixelFormat(dc, format, &pfd)) {
        v9x_wpr_text("GlResult", "NO-GL format");
        ReleaseDC(v9x_wpr_window, dc);
        return 1;
    }
    context = wglCreateContext(dc);
    if (context == 0 || !wglMakeCurrent(dc, context)) {
        v9x_wpr_text("GlResult", "NO-GL context");
        ReleaseDC(v9x_wpr_window, dc);
        return 1;
    }
    v9x_wpr_text("GlRenderer", (const char *)glGetString(GL_RENDERER));
    glViewport(0, 0, V9X_WPR_WIDTH, V9X_WPR_HEIGHT);
    for (index = 0; index < V9X_WPR_FRAMES; ++index) {
        /* Red and green in turn: the last frame is green. */
        if ((index & 1) != 0) {
            glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
        } else {
            glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
        }
        glClear(GL_COLOR_BUFFER_BIT);
        start = v9x_wpr_now();
        SwapBuffers(dc);
        us = v9x_wpr_us(start, v9x_wpr_now());
        total_us += us;
        if (us > max_us) {
            max_us = us;
        }
    }
    glFinish();
    v9x_wpr_uint("GlSwaps", (DWORD)V9X_WPR_FRAMES);
    v9x_wpr_uint("GlSwapAvgUs", total_us / (DWORD)V9X_WPR_FRAMES);
    v9x_wpr_uint("GlSwapMaxUs", max_us);
    for (index = 0; index < V9X_WPR_POINTS; ++index) {
        ok &= v9x_wpr_point_is(index, 0, 255, 0, "Gl");
    }
    v9x_wpr_text("GlResult", ok ? "PASS" : "FAIL");
    wglMakeCurrent(0, 0);
    wglDeleteContext(context);
    ReleaseDC(v9x_wpr_window, dc);
    return ok;
}

void __stdcall V9xWindowedPresentProbeEntry(void)
{
    WNDCLASSA window_class;
    LARGE_INTEGER frequency;
    POINT cursor;
    HDC screen;
    DWORD bpp;
    int ok;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_WPR_PATH);
    v9x_wpr_text("Build", V9X_BUILD_ID);
    if (!v9x_probe_lock_take()) {
        v9x_wpr_text("Result", "BUSY");
        ExitProcess(3u);
    }
    v9x_wpr_text("Result", "RUNNING");
    QueryPerformanceFrequency(&frequency);
    v9x_wpr_frequency = frequency.u.LowPart;
    v9x_wpr_uint("CounterHz", v9x_wpr_frequency);

    screen = GetDC(0);
    bpp = (DWORD)GetDeviceCaps(screen, BITSPIXEL);
    v9x_wpr_uint("Width", (DWORD)GetDeviceCaps(screen, HORZRES));
    v9x_wpr_uint("Height", (DWORD)GetDeviceCaps(screen, VERTRES));
    v9x_wpr_uint("BitsPerPixel", bpp);
    ReleaseDC(0, screen);
    if (bpp < 16ul) {
        v9x_wpr_text("Result", "SKIP-8BPP");
        ExitProcess(0u);
    }
    GetCursorPos(&cursor);

    v9x_wpr_zero(&window_class, sizeof(window_class));
    window_class.lpfnWndProc = v9x_wpr_window_proc;
    window_class.hInstance = GetModuleHandleA(0);
    window_class.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    window_class.lpszClassName = "Velocity9xWindowedPresentProbe";
    RegisterClassA(&window_class);
    v9x_wpr_window = CreateWindowExA(WS_EX_TOPMOST,
                                     window_class.lpszClassName,
                                     "Velocity9x windowed present probe",
                                     WS_POPUP | WS_VISIBLE, V9X_WPR_LEFT,
                                     V9X_WPR_TOP, V9X_WPR_WIDTH,
                                     V9X_WPR_HEIGHT, 0, 0,
                                     window_class.hInstance, 0);
    if (v9x_wpr_window == 0) {
        v9x_wpr_text("Result", "FAIL-WINDOW");
        ExitProcess(1u);
    }
    v9x_wpr_pump();

    ok = v9x_wpr_directdraw();
    ok &= v9x_wpr_opengl();

    DestroyWindow(v9x_wpr_window);
    v9x_wpr_pump();
    SetCursorPos(cursor.x, cursor.y);
    v9x_wpr_text("Result", ok ? "PASS" : "FAIL");
    ExitProcess(ok ? 0u : 2u);
}
