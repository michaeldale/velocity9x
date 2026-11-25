/* Cursor-square probe: what does GDI's cursor leave on a full-screen game?
 *
 * Issue 2026-11-13-evergreen-cursor-square-over-fullscreen-game.md: on an
 * HD 5570, Bug! (full screen, 320x240 and 640x480) showed a small
 * flickering square that followed the mouse, and NOSHADOW.TXT is the
 * leading suspect. This is a game that does what a game that never hides
 * the cursor does, and then looks at the frame buffer under the cursor:
 *
 *  - an exclusive, full-screen window whose class cursor is the arrow;
 *  - DirectDraw at 320x240x16 and 640x480x16 (ALLOWMODEX, so the smaller
 *    mode is offered at all);
 *  - per mode two styles, 90 frames each, the cursor moved every frame
 *    on a small circle around the centre with SetCursorPos:
 *      flip: a primary with one back buffer, the back buffer filled by
 *            the engine with one colour, then Flip(WAIT);
 *      blit: the primary alone, filled by the engine, then locked and a
 *            row written by the CPU, as a game that draws directly does;
 *  - after the frames, the cursor held still, every buffer of the chain
 *    read back through a Lock: the pixels that are not the fill colour,
 *    their box, and how much of the box they fill.
 *
 * The arrow is mostly transparent: drawn over the game it fills well under
 * half of its box. A copy of a cursor-sized rectangle of something else
 * (the desktop under the cursor, from the screen shadow) fills all of it.
 * So a case reads SQUARE when its differing pixels lie near the cursor and
 * fill at least V9X_CSQ_SQUARE_FILL of a box of at least
 * V9X_CSQ_SQUARE_AREA pixels; CURSOR when some differ near the cursor
 * otherwise; CLEAN when none differ; FAR when pixels away from the cursor
 * differ (not this issue, but recorded).
 *
 * Writes C:\V9XDIAG\V9XCURSQ.INI (one section a case, the shadow's event
 * log before and after) and, for each case with differing pixels, the box
 * eight times enlarged as C:\V9XDIAG\V9XCSQ<n>.PPM. 16 bpp only. Result is
 * PASS when no case reads SQUARE, FAIL when one does.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include "velocity9x/build.h"
#include "velocity9x/win9x_ddraw_abi.h"

#define V9X_CSQ_PATH "C:\\V9XDIAG\\V9XCURSQ.INI"
#define V9X_CSQ_FRAMES 90ul
/* The circle the cursor moves on, and how near a differing pixel must be
 * to the cursor's hot spot to count as the cursor's. */
#define V9X_CSQ_RADIUS 12l
#define V9X_CSQ_REACH 48l
#define V9X_CSQ_SQUARE_FILL_PERCENT 90ul
#define V9X_CSQ_SQUARE_AREA 64ul
#define V9X_CSQ_ZOOM 8l
/* 5:6:5 green: unlike the desktop, the arrow, and black. */
#define V9X_CSQ_FILL_565 0x07e0ul
#define V9X_CSQ_FILL_555 0x03e0ul
#define V9X_CSQ_MAX_WIDTH 640ul

typedef HRESULT (WINAPI *V9X_CSQ_CREATE)(GUID *, LPDIRECTDRAW *, IUnknown *);

static HWND v9x_csq_window;
static DWORD v9x_csq_ppm_number;
static int v9x_csq_square_seen;
static WORD v9x_csq_copy[V9X_CSQ_MAX_WIDTH * 480ul];

static void v9x_csq_zero(void *block, DWORD bytes)
{
    volatile BYTE *at = (volatile BYTE *)block;

    while (bytes-- != 0ul) {
        *at++ = 0u;
    }
}

static void v9x_csq_text(const char *section, const char *key,
                         const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_CSQ_PATH);
}

static void v9x_csq_uint(const char *section, const char *key, DWORD value)
{
    char text[16];

    wsprintfA(text, "%lu", value);
    v9x_csq_text(section, key, text);
}

static void v9x_csq_hr(const char *section, const char *key, HRESULT hr)
{
    char text[16];

    wsprintfA(text, "%08lX", (DWORD)hr);
    v9x_csq_text(section, key, text);
}

/* The shadow's event log, found as V9XFBCMP finds it. */
static void v9x_csq_log(const char *section)
{
    static const char *const names[] = {
        "?", "begin", "end", "take", "overlap", "copy", "output", "cursor",
        "flush-all", "stale"
    };
    V9X_DCICMD command;
    V9X_DD32BITDRIVERDATA driver;
    const V9X_DD_SHARED *shared;
    HDC dc;
    int result;
    DWORD count;
    DWORD first;
    DWORD number;
    char key[16];
    char value[64];

    WritePrivateProfileStringA(section, 0, 0, V9X_CSQ_PATH);
    v9x_csq_zero(&command, sizeof(command));
    v9x_csq_zero(&driver, sizeof(driver));
    command.dwCommand = V9X_DDGET32BITDRIVERNAME;
    command.dwVersion = V9X_DD_VERSION;
    dc = GetDC(0);
    result = dc != 0 ? ExtEscape(dc, V9X_DCICOMMAND, sizeof(command),
                                 (LPCSTR)&command, sizeof(driver),
                                 (LPSTR)&driver) : 0;
    if (dc != 0) {
        ReleaseDC(0, dc);
    }
    shared = (const V9X_DD_SHARED *)driver.dwContext;
    if (result <= 0 || shared == 0 ||
        IsBadReadPtr(shared, sizeof(*shared)) ||
        shared->dwSize != sizeof(*shared) ||
        shared->abi != V9X_DD_SHARED_ABI) {
        v9x_csq_text(section, "Taken", "no");
        return;
    }
    count = shared->shadow_log_count;
    v9x_csq_uint(section, "Count", count);
    first = count > V9X_SHADOW_LOG_EVENTS ? count - V9X_SHADOW_LOG_EVENTS : 0ul;
    for (number = first; number < count; ++number) {
        const struct v9x_shadow_event *event =
            &shared->shadow_log[number & (V9X_SHADOW_LOG_EVENTS - 1u)];
        const char *name = event->kind < sizeof(names) / sizeof(names[0]) ?
                           names[event->kind] : "?";

        wsprintfA(key, "E%lu", number);
        wsprintfA(value, "%s %04X %d,%d,%d,%d", name,
                  (unsigned int)event->detail, (int)event->rect.left,
                  (int)event->rect.top, (int)event->rect.right,
                  (int)event->rect.bottom);
        v9x_csq_text(section, key, value);
    }
}

static void v9x_csq_pump(void)
{
    MSG message;

    while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

static LRESULT CALLBACK v9x_csq_proc(HWND window, UINT message, WPARAM w,
                                     LPARAM l)
{
    return DefWindowProcA(window, message, w, l);
}

/* The cursor's place on frame n: a circle of 8 points around the centre. */
static void v9x_csq_cursor_at(DWORD width, DWORD height, DWORD frame,
                              LONG *x, LONG *y)
{
    static const signed char dx[8] = { 12, 8, 0, -8, -12, -8, 0, 8 };
    static const signed char dy[8] = { 0, 8, 12, 8, 0, -8, -12, -8 };

    *x = (LONG)(width / 2ul) + dx[frame & 7ul];
    *y = (LONG)(height / 2ul) + dy[frame & 7ul];
}

static void v9x_csq_fill(LPDIRECTDRAWSURFACE surface, DWORD colour)
{
    DDBLTFX fx;

    v9x_csq_zero(&fx, sizeof(fx));
    fx.dwSize = sizeof(fx);
    fx.dwFillColor = colour;
    IDirectDrawSurface_Blt(surface, 0, 0, 0, DDBLT_COLORFILL | DDBLT_WAIT,
                           &fx);
}

/* The box of a copy as a binary PPM, 5:6:5 or 5:5:5. */
static void v9x_csq_ppm(DWORD width, int is555, LONG left, LONG top,
                        LONG right, LONG bottom)
{
    HANDLE file;
    char path[32];
    char header[48];
    BYTE line[3 * 128 * 8];
    DWORD written;
    LONG x;
    LONG y;
    LONG repeat;
    LONG box_width = right - left;
    int length;

    if (box_width > 128l) {
        box_width = 128l;
    }
    if (bottom - top > 128l) {
        bottom = top + 128l;
    }
    wsprintfA(path, "C:\\V9XDIAG\\V9XCSQ%lu.PPM", v9x_csq_ppm_number);
    file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    length = wsprintfA(header, "P6\n%ld %ld\n255\n", box_width * V9X_CSQ_ZOOM,
                       (bottom - top) * V9X_CSQ_ZOOM);
    WriteFile(file, header, (DWORD)length, &written, 0);
    for (y = top; y < bottom; ++y) {
        for (x = 0l; x < box_width; ++x) {
            WORD pixel = v9x_csq_copy[width * (DWORD)y + (DWORD)(left + x)];
            BYTE rgb[3];

            if (is555) {
                rgb[0] = (BYTE)((pixel >> 7) & 0xf8u);
                rgb[1] = (BYTE)((pixel >> 2) & 0xf8u);
            } else {
                rgb[0] = (BYTE)((pixel >> 8) & 0xf8u);
                rgb[1] = (BYTE)((pixel >> 3) & 0xfcu);
            }
            rgb[2] = (BYTE)((pixel << 3) & 0xf8u);
            for (repeat = 0l; repeat < V9X_CSQ_ZOOM; ++repeat) {
                BYTE *to = line + ((x * V9X_CSQ_ZOOM) + repeat) * 3l;

                to[0] = rgb[0];
                to[1] = rgb[1];
                to[2] = rgb[2];
            }
        }
        for (repeat = 0l; repeat < V9X_CSQ_ZOOM; ++repeat) {
            WriteFile(file, line, (DWORD)(box_width * V9X_CSQ_ZOOM * 3l),
                      &written, 0);
        }
    }
    CloseHandle(file);
}

/*
 * Read one buffer back and judge it. Writes <prefix>Differ, Near, Far,
 * Box, FillPercent, Colours (the first three non-fill colours met near the
 * cursor) and Verdict into the case's section.
 */
static void v9x_csq_judge(const char *section, const char *prefix,
                          LPDIRECTDRAWSURFACE surface, DWORD width,
                          DWORD height, DWORD fill, int is555,
                          LONG cursor_x, LONG cursor_y)
{
    DDSURFACEDESC desc;
    HRESULT hr;
    DWORD x;
    DWORD y;
    DWORD near_count = 0ul;
    DWORD far_count = 0ul;
    DWORD area;
    DWORD fill_percent = 0ul;
    DWORD colours[3];
    DWORD colour_count = 0ul;
    LONG left = 0x7fffffffl;
    LONG top = 0x7fffffffl;
    LONG right = -1l;
    LONG bottom = -1l;
    const char *verdict;
    char key[32];
    char text[64];

    v9x_csq_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = IDirectDrawSurface_Lock(surface, 0, &desc,
                                 DDLOCK_WAIT | DDLOCK_READONLY, 0);
    wsprintfA(key, "%sLockHr", prefix);
    v9x_csq_hr(section, key, hr);
    if (hr != DD_OK) {
        return;
    }
    for (y = 0ul; y < height; ++y) {
        const WORD *row = (const WORD *)((const BYTE *)desc.lpSurface +
                                         (DWORD)desc.lPitch * y);

        for (x = 0ul; x < width; ++x) {
            v9x_csq_copy[width * y + x] = row[x];
        }
    }
    IDirectDrawSurface_Unlock(surface, 0);

    for (y = 0ul; y < height; ++y) {
        for (x = 0ul; x < width; ++x) {
            DWORD pixel = v9x_csq_copy[width * y + x];
            LONG dx = (LONG)x - cursor_x;
            LONG dy = (LONG)y - cursor_y;

            if (pixel == fill) {
                continue;
            }
            if (dx < -V9X_CSQ_REACH || dx > V9X_CSQ_REACH ||
                dy < -V9X_CSQ_REACH || dy > V9X_CSQ_REACH) {
                ++far_count;
                continue;
            }
            ++near_count;
            if (colour_count < 3ul &&
                (colour_count == 0ul || colours[0] != pixel) &&
                (colour_count < 2ul || colours[1] != pixel)) {
                colours[colour_count++] = pixel;
            }
            if ((LONG)x < left) {
                left = (LONG)x;
            }
            if ((LONG)y < top) {
                top = (LONG)y;
            }
            if ((LONG)x > right) {
                right = (LONG)x;
            }
            if ((LONG)y > bottom) {
                bottom = (LONG)y;
            }
        }
    }
    wsprintfA(key, "%sNear", prefix);
    v9x_csq_uint(section, key, near_count);
    wsprintfA(key, "%sFar", prefix);
    v9x_csq_uint(section, key, far_count);
    if (near_count != 0ul) {
        area = (DWORD)(right - left + 1l) * (DWORD)(bottom - top + 1l);
        fill_percent = near_count * 100ul / area;
        wsprintfA(key, "%sBox", prefix);
        wsprintfA(text, "%ld,%ld,%ld,%ld", left, top, right + 1l,
                  bottom + 1l);
        v9x_csq_text(section, key, text);
        wsprintfA(key, "%sFillPercent", prefix);
        v9x_csq_uint(section, key, fill_percent);
        wsprintfA(key, "%sColours", prefix);
        wsprintfA(text, "%04lX %04lX %04lX", colours[0],
                  colour_count > 1ul ? colours[1] : 0ul,
                  colour_count > 2ul ? colours[2] : 0ul);
        v9x_csq_text(section, key, text);
        ++v9x_csq_ppm_number;
        wsprintfA(key, "%sImage", prefix);
        wsprintfA(text, "V9XCSQ%lu.PPM", v9x_csq_ppm_number);
        v9x_csq_text(section, key, text);
        v9x_csq_ppm(width, is555, left, top, right + 1l, bottom + 1l);
        if (fill_percent >= V9X_CSQ_SQUARE_FILL_PERCENT &&
            area >= V9X_CSQ_SQUARE_AREA) {
            verdict = "SQUARE";
            v9x_csq_square_seen = 1;
        } else {
            verdict = "CURSOR";
        }
    } else {
        verdict = far_count != 0ul ? "FAR" : "CLEAN";
    }
    if (near_count == 0ul && far_count != 0ul) {
        verdict = "FAR";
    }
    wsprintfA(key, "%sVerdict", prefix);
    v9x_csq_text(section, key, verdict);
}

/* One mode, one style. */
static void v9x_csq_case(LPDIRECTDRAW dd, const char *section, DWORD width,
                         DWORD height, int flip)
{
    LPDIRECTDRAWSURFACE primary = 0;
    LPDIRECTDRAWSURFACE back = 0;
    DDSURFACEDESC desc;
    DDSCAPS caps;
    DDPIXELFORMAT format;
    HRESULT hr;
    DWORD frame;
    DWORD fill;
    int is555;
    LONG x;
    LONG y;

    WritePrivateProfileStringA(section, 0, 0, V9X_CSQ_PATH);
    hr = IDirectDraw_SetDisplayMode(dd, width, height, 16ul);
    v9x_csq_hr(section, "SetModeHr", hr);
    if (hr != DD_OK) {
        v9x_csq_text(section, "Verdict", "NO-MODE");
        return;
    }
    v9x_csq_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    if (flip) {
        desc.dwFlags |= DDSD_BACKBUFFERCOUNT;
        desc.ddsCaps.dwCaps |= DDSCAPS_FLIP | DDSCAPS_COMPLEX;
        desc.dwBackBufferCount = 1ul;
    }
    hr = IDirectDraw_CreateSurface(dd, &desc, &primary, 0);
    v9x_csq_hr(section, "CreateHr", hr);
    if (hr != DD_OK) {
        v9x_csq_text(section, "Verdict", "NO-SURFACE");
        return;
    }
    if (flip) {
        v9x_csq_zero(&caps, sizeof(caps));
        caps.dwCaps = DDSCAPS_BACKBUFFER;
        hr = IDirectDrawSurface_GetAttachedSurface(primary, &caps, &back);
        v9x_csq_hr(section, "BackHr", hr);
        if (hr != DD_OK) {
            IDirectDrawSurface_Release(primary);
            v9x_csq_text(section, "Verdict", "NO-SURFACE");
            return;
        }
    }
    v9x_csq_zero(&format, sizeof(format));
    format.dwSize = sizeof(format);
    IDirectDrawSurface_GetPixelFormat(primary, &format);
    is555 = format.dwGBitMask == 0x03e0ul;
    fill = is555 ? V9X_CSQ_FILL_555 : V9X_CSQ_FILL_565;
    v9x_csq_uint(section, "Green", format.dwGBitMask);

    for (frame = 0ul; frame < V9X_CSQ_FRAMES; ++frame) {
        v9x_csq_pump();
        v9x_csq_cursor_at(width, height, frame, &x, &y);
        SetCursorPos((int)x, (int)y);
        if (flip) {
            v9x_csq_fill(back, fill);
            do {
                hr = IDirectDrawSurface_Flip(primary, 0, DDFLIP_WAIT);
            } while (hr == DDERR_WASSTILLDRAWING);
        } else {
            v9x_csq_fill(primary, fill);
            v9x_csq_zero(&desc, sizeof(desc));
            desc.dwSize = sizeof(desc);
            /* A row at the bottom written by the CPU, as a game that draws
             * into the primary does; same colour, so nothing to see. */
            hr = IDirectDrawSurface_Lock(primary, 0, &desc, DDLOCK_WAIT, 0);
            if (hr == DD_OK) {
                WORD *row = (WORD *)((BYTE *)desc.lpSurface +
                                     (DWORD)desc.lPitch * (height - 1ul));
                DWORD column;

                for (column = 0ul; column < width; ++column) {
                    row[column] = (WORD)fill;
                }
                IDirectDrawSurface_Unlock(primary, 0);
            }
        }
        Sleep(16ul);
    }
    v9x_csq_hr(section, "LastHr", hr);

    /* Hold the cursor still a moment, then read every buffer. */
    v9x_csq_cursor_at(width, height, 0ul, &x, &y);
    SetCursorPos((int)x, (int)y);
    v9x_csq_pump();
    Sleep(250ul);
    v9x_csq_uint(section, "CursorX", (DWORD)x);
    v9x_csq_uint(section, "CursorY", (DWORD)y);
    v9x_csq_judge(section, "Front", primary, width, height, fill, is555, x,
                  y);
    if (flip) {
        do {
            hr = IDirectDrawSurface_Flip(primary, 0, DDFLIP_WAIT);
        } while (hr == DDERR_WASSTILLDRAWING);
        v9x_csq_pump();
        Sleep(250ul);
        v9x_csq_judge(section, "Other", primary, width, height, fill, is555,
                      x, y);
        IDirectDrawSurface_Release(back);
    }
    IDirectDrawSurface_Release(primary);
}

void __stdcall V9xCursorSquareProbeEntry(void)
{
    V9X_CSQ_CREATE create;
    LPDIRECTDRAW dd = 0;
    WNDCLASSA window_class;
    HRESULT hr;

    CreateDirectoryA("C:\\V9XDIAG", 0);
    WritePrivateProfileStringA("CursorSquare", 0, 0, V9X_CSQ_PATH);
    v9x_csq_text("CursorSquare", "Build", V9X_BUILD_ID);
    v9x_csq_log("ShadowLogBefore");

    v9x_csq_zero(&window_class, sizeof(window_class));
    window_class.lpfnWndProc = v9x_csq_proc;
    window_class.hInstance = GetModuleHandleA(0);
    window_class.hCursor = LoadCursorA(0, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    window_class.lpszClassName = "V9xCursorSquare";
    RegisterClassA(&window_class);
    v9x_csq_window = CreateWindowExA(WS_EX_TOPMOST, "V9xCursorSquare",
                                     "V9XCURSQ", WS_POPUP | WS_VISIBLE, 0, 0,
                                     GetSystemMetrics(SM_CXSCREEN),
                                     GetSystemMetrics(SM_CYSCREEN), 0, 0,
                                     window_class.hInstance, 0);
    if (v9x_csq_window == 0) {
        v9x_csq_text("CursorSquare", "Result", "FAIL-WINDOW");
        ExitProcess(1u);
    }
    SetForegroundWindow(v9x_csq_window);
    v9x_csq_pump();

    create = (V9X_CSQ_CREATE)GetProcAddress(LoadLibraryA("DDRAW.DLL"),
                                            "DirectDrawCreate");
    if (create == 0 || create(0, &dd, 0) != DD_OK) {
        v9x_csq_text("CursorSquare", "Result", "FAIL-DDRAW");
        ExitProcess(1u);
    }
    hr = IDirectDraw_SetCooperativeLevel(dd, v9x_csq_window,
                                         DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN |
                                         DDSCL_ALLOWMODEX);
    v9x_csq_hr("CursorSquare", "CooperativeHr", hr);
    if (hr != DD_OK) {
        IDirectDraw_Release(dd);
        v9x_csq_text("CursorSquare", "Result", "FAIL-COOPERATIVE");
        ExitProcess(1u);
    }

    v9x_csq_case(dd, "Case320Flip", 320ul, 240ul, 1);
    v9x_csq_case(dd, "Case320Blit", 320ul, 240ul, 0);
    v9x_csq_case(dd, "Case640Flip", 640ul, 480ul, 1);
    v9x_csq_case(dd, "Case640Blit", 640ul, 480ul, 0);

    IDirectDraw_RestoreDisplayMode(dd);
    IDirectDraw_SetCooperativeLevel(dd, v9x_csq_window, DDSCL_NORMAL);
    IDirectDraw_Release(dd);
    DestroyWindow(v9x_csq_window);
    v9x_csq_log("ShadowLogAfter");
    v9x_csq_text("CursorSquare", "Result",
                 v9x_csq_square_seen ? "FAIL" : "PASS");
    ExitProcess(v9x_csq_square_seen ? 2u : 0u);
}
