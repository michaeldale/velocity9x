/*
 * Windowed clipped blit and surface-internals probe
 * (docs\plans\opengl-1.1-icd.md, Phases 0.3 and 0.4).
 *
 * Two questions the OpenGL ICD's SwapBuffers design rests on, asked from an
 * ordinary application the way the ICD will ask them:
 *
 *   0.4  Can an application follow a surface's COM pointer to the runtime's
 *        DDRAWI_DDRAWSURFACE_INT, its LCL and its GBL, and are the addresses
 *        it finds the ones the HAL sees - fpVidMem equal to what Lock hands
 *        back, dwProcessId equal to the caller's, everything above 2 GB?
 *        The HAL's own copies of these layouts (win9x_ddraw_abi.h) are the
 *        ones read here, so a disagreement is a disagreement with the HAL.
 *
 *   0.3  What does the runtime hand the HAL for a Blt into the primary
 *        through a clipper when another window covers part of the target?
 *        This tool makes the geometry: window A with a clipper, a video
 *        memory back buffer filled with one colour, and a topmost window B
 *        over A's lower-right quarter. It blits, then holds still for a few
 *        seconds so the agent can capture the screen. The HAL's clipped-blit
 *        counters (V9XTRACE: BltClipped*) say what arrived; the capture says
 *        whether window B kept its pixels.
 *
 * Writes C:\V9XDIAG\V9XSCLP.INI. Imports KERNEL32 and USER32 only.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"
#include "velocity9x/win9x_ddraw_abi.h"

#define V9X_SCLP_SECTION "SurfaceClipProbe"
#define V9X_SCLP_A_X      100
#define V9X_SCLP_A_Y      100
#define V9X_SCLP_A_W      400
#define V9X_SCLP_A_H      300
#define V9X_SCLP_B_X      300
#define V9X_SCLP_B_Y      250
#define V9X_SCLP_B_W      300
#define V9X_SCLP_B_H      300
#define V9X_SCLP_FILL_1   0xF81Ful   /* magenta in 565, also pink in 555 */
#define V9X_SCLP_FILL_2   0x07E0ul   /* green in 565 */
#define V9X_SCLP_HOLD_MS  6000ul

typedef HRESULT (WINAPI *V9X_SCLP_DDRAW_CREATE)(GUID *, LPDIRECTDRAW *, IUnknown *);

static void v9x_sclp_text(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_SCLP_SECTION, key, value, V9X_DIAG_SCLP_INI);
}

static void v9x_sclp_hex(const char *key, DWORD value)
{
    char text[16];

    wsprintfA(text, "0x%08lX", value);
    v9x_sclp_text(key, text);
}

static void v9x_sclp_uint(const char *key, DWORD value)
{
    char text[16];

    wsprintfA(text, "%lu", value);
    v9x_sclp_text(key, text);
}

static void v9x_sclp_zero(void *block, DWORD bytes)
{
    DWORD i;

    for (i = 0ul; i < bytes; ++i) {
        ((BYTE *)block)[i] = 0u;
    }
}

/* Both windows are painted by USER from the class brush; nothing here draws
 * with GDI, so window B's pixels are USER's and a blit that covers them is
 * unambiguous in the capture. */
static LRESULT CALLBACK v9x_sclp_window_proc(HWND window, UINT message,
                                             WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcA(window, message, wparam, lparam);
}

static void v9x_sclp_pump(void)
{
    MSG message;

    while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

/* Phase 0.4: the walk from the COM pointer, with every step guarded. */
static void v9x_sclp_internals(const char *name, LPDIRECTDRAWSURFACE surface,
                               DWORD locked_address)
{
    const V9X_DD_SURFACE_INT *wrapper = (const V9X_DD_SURFACE_INT *)surface;
    const V9X_DD_SURFACE_LCL *lcl;
    const V9X_DD_SURFACE_GBL *gbl;
    char key[48];

    wsprintfA(key, "%sInt", name);
    v9x_sclp_hex(key, (DWORD)wrapper);
    if (wrapper == 0 || IsBadReadPtr(wrapper, sizeof(*wrapper))) {
        wsprintfA(key, "%sWalk", name);
        v9x_sclp_text(key, "no-int");
        return;
    }
    lcl = wrapper->lpLcl;
    wsprintfA(key, "%sLcl", name);
    v9x_sclp_hex(key, (DWORD)lcl);
    if (lcl == 0 || IsBadReadPtr(lcl, sizeof(*lcl))) {
        wsprintfA(key, "%sWalk", name);
        v9x_sclp_text(key, "no-lcl");
        return;
    }
    gbl = lcl->lpGbl;
    wsprintfA(key, "%sGbl", name);
    v9x_sclp_hex(key, (DWORD)gbl);
    wsprintfA(key, "%sLclProcessId", name);
    v9x_sclp_hex(key, lcl->dwProcessId);
    wsprintfA(key, "%sLclFlags", name);
    v9x_sclp_hex(key, lcl->dwFlags);
    wsprintfA(key, "%sLclCaps", name);
    v9x_sclp_hex(key, lcl->ddsCaps);
    wsprintfA(key, "%sLclClipper", name);
    v9x_sclp_hex(key, lcl->lpDDClipper);
    if (gbl == 0 || IsBadReadPtr(gbl, 32u)) {
        wsprintfA(key, "%sWalk", name);
        v9x_sclp_text(key, "no-gbl");
        return;
    }
    wsprintfA(key, "%sGblVidMem", name);
    v9x_sclp_hex(key, gbl->fpVidMem);
    wsprintfA(key, "%sGblPitch", name);
    v9x_sclp_uint(key, (DWORD)gbl->lPitch);
    wsprintfA(key, "%sGblWidth", name);
    v9x_sclp_uint(key, gbl->wWidth);
    wsprintfA(key, "%sGblHeight", name);
    v9x_sclp_uint(key, gbl->wHeight);
    wsprintfA(key, "%sLockAddress", name);
    v9x_sclp_hex(key, locked_address);
    wsprintfA(key, "%sVidMemMatchesLock", name);
    v9x_sclp_uint(key, (locked_address != 0ul && locked_address == gbl->fpVidMem) ? 1ul : 0ul);
    wsprintfA(key, "%sProcessMatches", name);
    v9x_sclp_uint(key, lcl->dwProcessId == GetCurrentProcessId() ? 1ul : 0ul);
    wsprintfA(key, "%sAllShared", name);
    v9x_sclp_uint(key, ((DWORD)wrapper >= 0x80000000ul && (DWORD)lcl >= 0x80000000ul &&
                        (DWORD)gbl >= 0x80000000ul) ? 1ul : 0ul);
    wsprintfA(key, "%sWalk", name);
    v9x_sclp_text(key, "ok");
}

void __stdcall V9xSurfaceClipProbeEntry(void)
{
    WNDCLASSA window_class;
    HWND window_a;
    HWND window_b;
    HMODULE module;
    V9X_SCLP_DDRAW_CREATE create = 0;
    LPDIRECTDRAW dd = 0;
    LPDIRECTDRAWSURFACE primary = 0;
    LPDIRECTDRAWSURFACE back = 0;
    LPDIRECTDRAWCLIPPER clipper = 0;
    DDSURFACEDESC desc;
    DDBLTFX fx;
    RECT client;
    POINT origin;
    HRESULT hr;
    DWORD back_lock_address = 0ul;
    int ok = 0;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_DIAG_SCLP_INI);
    v9x_sclp_text("Build", V9X_BUILD_ID);
    v9x_sclp_text("Result", "RUNNING");
    v9x_sclp_uint("SchemaVersion", 1ul);
    v9x_sclp_hex("ProcessId", GetCurrentProcessId());

    v9x_sclp_zero(&window_class, sizeof(window_class));
    window_class.lpfnWndProc = v9x_sclp_window_proc;
    window_class.hInstance = GetModuleHandleA(0);
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class.lpszClassName = "Velocity9xSurfaceClipProbe";
    RegisterClassA(&window_class);
    window_a = CreateWindowExA(0ul, window_class.lpszClassName,
                               "Velocity9x clip probe A", WS_POPUP | WS_VISIBLE,
                               V9X_SCLP_A_X, V9X_SCLP_A_Y, V9X_SCLP_A_W, V9X_SCLP_A_H,
                               0, 0, window_class.hInstance, 0);
    if (window_a == 0) {
        v9x_sclp_text("Result", "FAIL-WINDOW");
        ExitProcess(1u);
    }
    v9x_sclp_pump();

    module = LoadLibraryA("DDRAW.DLL");
    create = module ? (V9X_SCLP_DDRAW_CREATE)GetProcAddress(module, "DirectDrawCreate") : 0;
    if (create == 0 || create(0, &dd, 0) != DD_OK) {
        v9x_sclp_text("Result", "FAIL-CREATE");
        ExitProcess(1u);
    }
    hr = IDirectDraw_SetCooperativeLevel(dd, window_a, DDSCL_NORMAL);
    v9x_sclp_hex("CoopHr", (DWORD)hr);

    v9x_sclp_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    hr = IDirectDraw_CreateSurface(dd, &desc, &primary, 0);
    v9x_sclp_hex("PrimaryCreateHr", (DWORD)hr);

    hr = IDirectDraw_CreateClipper(dd, 0ul, &clipper, 0);
    v9x_sclp_hex("ClipperCreateHr", (DWORD)hr);
    if (clipper != 0) {
        hr = IDirectDrawClipper_SetHWnd(clipper, 0ul, window_a);
        v9x_sclp_hex("ClipperSetHwndHr", (DWORD)hr);
        if (primary != 0) {
            hr = IDirectDrawSurface_SetClipper(primary, clipper);
            v9x_sclp_hex("PrimarySetClipperHr", (DWORD)hr);
        }
    }

    v9x_sclp_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = V9X_SCLP_A_W;
    desc.dwHeight = V9X_SCLP_A_H;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    hr = IDirectDraw_CreateSurface(dd, &desc, &back, 0);
    v9x_sclp_hex("BackCreateHr", (DWORD)hr);

    /* Phase 0.4 readings: the back buffer while locked, then the primary. */
    if (back != 0) {
        v9x_sclp_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        hr = IDirectDrawSurface_Lock(back, 0, &desc, DDLOCK_WAIT, 0);
        v9x_sclp_hex("BackLockHr", (DWORD)hr);
        if (hr == DD_OK) {
            back_lock_address = (DWORD)desc.lpSurface;
            v9x_sclp_uint("BackLockPitch", (DWORD)desc.lPitch);
            v9x_sclp_uint("BackLockBpp", desc.ddpfPixelFormat.dwRGBBitCount);
            v9x_sclp_hex("BackLockCaps", desc.ddsCaps.dwCaps);
            IDirectDrawSurface_Unlock(back, 0);
        }
        v9x_sclp_internals("Back", back, back_lock_address);
    }
    if (primary != 0) {
        v9x_sclp_internals("Primary", primary, 0ul);
    }

    /* Window B, topmost, over A's lower-right quarter. */
    window_b = CreateWindowExA(WS_EX_TOPMOST, window_class.lpszClassName,
                               "Velocity9x clip probe B", WS_POPUP | WS_VISIBLE,
                               V9X_SCLP_B_X, V9X_SCLP_B_Y, V9X_SCLP_B_W, V9X_SCLP_B_H,
                               0, 0, window_class.hInstance, 0);
    v9x_sclp_uint("WindowB", window_b != 0 ? 1ul : 0ul);
    v9x_sclp_pump();
    Sleep(500);
    v9x_sclp_pump();

    /* Phase 0.3: fill the back buffer, blit it through the clipper. */
    if (back != 0 && primary != 0) {
        v9x_sclp_zero(&fx, sizeof(fx));
        fx.dwSize = sizeof(fx);
        fx.dwFillColor = V9X_SCLP_FILL_1;
        hr = IDirectDrawSurface_Blt(back, 0, 0, 0, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
        v9x_sclp_hex("BackFillHr", (DWORD)hr);

        GetClientRect(window_a, &client);
        origin.x = 0;
        origin.y = 0;
        ClientToScreen(window_a, &origin);
        client.left += origin.x;
        client.right += origin.x;
        client.top += origin.y;
        client.bottom += origin.y;
        v9x_sclp_uint("DestLeft", (DWORD)client.left);
        v9x_sclp_uint("DestTop", (DWORD)client.top);
        v9x_sclp_uint("DestRight", (DWORD)client.right);
        v9x_sclp_uint("DestBottom", (DWORD)client.bottom);

        hr = IDirectDrawSurface_Blt(primary, &client, back, 0, DDBLT_WAIT, 0);
        v9x_sclp_hex("ClippedBltHr", (DWORD)hr);
        ok = hr == DD_OK;

        /* Hold for the capture, pumping so both windows stay painted. */
        {
            DWORD started = GetTickCount();

            v9x_sclp_text("Stage", "holding");
            while (GetTickCount() - started < V9X_SCLP_HOLD_MS) {
                v9x_sclp_pump();
                Sleep(50);
            }
        }

        /* A second colour and blit, so the trace counts two if it counts. */
        fx.dwFillColor = V9X_SCLP_FILL_2;
        hr = IDirectDrawSurface_Blt(back, 0, 0, 0, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
        hr = IDirectDrawSurface_Blt(primary, &client, back, 0, DDBLT_WAIT, 0);
        v9x_sclp_hex("ClippedBlt2Hr", (DWORD)hr);
    }

    v9x_sclp_text("Stage", "done");
    if (back != 0) {
        IDirectDrawSurface_Release(back);
    }
    if (clipper != 0) {
        IDirectDrawClipper_Release(clipper);
    }
    if (primary != 0) {
        IDirectDrawSurface_Release(primary);
    }
    if (dd != 0) {
        IDirectDraw_Release(dd);
    }
    if (module != 0) {
        FreeLibrary(module);
    }
    v9x_sclp_text("Result", ok ? "PASS" : "FAIL");
    WritePrivateProfileStringA(0, 0, 0, V9X_DIAG_SCLP_INI);
    ExitProcess(ok ? 0u : 1u);
}
