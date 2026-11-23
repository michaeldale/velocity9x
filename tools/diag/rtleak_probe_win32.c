/*
 * V9XRTLK.EXE: whether an offscreen render target's video memory comes back
 * when the surface is released.
 *
 * Several engines place an offscreen render target (OFFSCREENPLAIN |
 * 3DDEVICE) in a heap block of their own, because DirectDraw's placement
 * gives it a pitch or base the engine cannot draw to. Such a surface has no
 * heap, so DirectDraw does not free it, and v9x_d3d_place_release freed only
 * textures, mip levels and Z buffers: each render target leaked its block
 * (docs\issues\2026-11-15-placed-render-targets-never-freed.md, the 1.0
 * plan's B3). The ICD makes one such back buffer per drawable size.
 *
 * This creates and releases one render target at the desktop's format
 * Rounds times, then a Z buffer as a control, reading the free video memory
 * DirectDraw 2 reports before and after each. An engine that does not place
 * render targets passes either way; the probe says nothing about which kind
 * it ran on except through Pitch, read from a lock of the first one.
 *
 * Usage: V9XRTLK [rounds, default 20]
 * Writes C:\V9XDIAG\V9XRTLK.INI: Result=PASS when the free memory after the
 * loop equals the free memory before it, LEAK otherwise.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_RTLK_PATH    "C:\\V9XDIAG\\V9XRTLK.INI"
#define V9X_RTLK_SECTION "Velocity9xRenderTargetLeak"
#define V9X_RTLK_WIDTH   640ul
#define V9X_RTLK_HEIGHT  480ul
#define V9X_RTLK_ROUNDS  20ul
#define V9X_RTLK_DEPTH_BITS 16ul

typedef HRESULT (WINAPI *V9X_RTLK_CREATE)(GUID *guid, LPDIRECTDRAW *dd,
                                          IUnknown *outer);

/* IID_IDirectDraw2, {B3A6F3E0-2B43-11CF-A2DE-00AA00B93356} (DDRAW.H). */
static const GUID v9x_rtlk_iid_dd2 = {
    0xB3A6F3E0ul, 0x2B43u, 0x11CFu,
    { 0xA2u, 0xDEu, 0x00u, 0xAAu, 0x00u, 0xB9u, 0x33u, 0x56u }
};

static void v9x_rtlk_text(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_RTLK_SECTION, key, value, V9X_RTLK_PATH);
}

static void v9x_rtlk_uint(const char *key, DWORD value)
{
    char text[16];

    wsprintfA(text, "%lu", value);
    v9x_rtlk_text(key, text);
}

static void v9x_rtlk_hex(const char *key, DWORD value)
{
    char text[16];

    wsprintfA(text, "0x%08lX", value);
    v9x_rtlk_text(key, text);
}

static void v9x_rtlk_zero(void *memory, DWORD bytes)
{
    BYTE *at = (BYTE *)memory;

    while (bytes-- != 0ul) {
        *at++ = 0u;
    }
}

/* Free video memory for offscreen surfaces, as DirectDraw 2 reports it;
 * 0xFFFFFFFF when it does not answer. */
static DWORD v9x_rtlk_free(LPDIRECTDRAW2 dd2)
{
    DDSCAPS caps;
    DWORD total = 0ul;
    DWORD free = 0ul;

    caps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    if (IDirectDraw2_GetAvailableVidMem(dd2, &caps, &total, &free) != DD_OK) {
        return 0xFFFFFFFFul;
    }
    return free;
}

/* One render target (depth 0) or Z buffer (depth 1) in video memory. */
static HRESULT v9x_rtlk_make(LPDIRECTDRAW dd, int depth,
                             LPDIRECTDRAWSURFACE *surface)
{
    DDSURFACEDESC desc;

    v9x_rtlk_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = V9X_RTLK_WIDTH;
    desc.dwHeight = V9X_RTLK_HEIGHT;
    if (depth) {
        desc.dwFlags |= DDSD_ZBUFFERBITDEPTH;
        desc.dwZBufferBitDepth = V9X_RTLK_DEPTH_BITS;
        desc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_VIDEOMEMORY;
    } else {
        desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE |
                              DDSCAPS_VIDEOMEMORY;
    }
    *surface = 0;
    return IDirectDraw_CreateSurface(dd, &desc, surface, 0);
}

/* Pitch from a lock, or 0 when the lock is refused. */
static DWORD v9x_rtlk_pitch(LPDIRECTDRAWSURFACE surface)
{
    DDSURFACEDESC desc;
    DWORD pitch;

    v9x_rtlk_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    if (IDirectDrawSurface_Lock(surface, 0, &desc, DDLOCK_WAIT, 0) != DD_OK) {
        return 0ul;
    }
    pitch = (DWORD)desc.lPitch;
    IDirectDrawSurface_Unlock(surface, desc.lpSurface);
    return pitch;
}

/* The loop for one kind, its keys prefixed. Returns the bytes not given
 * back, 0xFFFFFFFF when a step failed. */
static DWORD v9x_rtlk_loop(LPDIRECTDRAW dd, LPDIRECTDRAW2 dd2, int depth,
                           DWORD rounds, const char *prefix)
{
    LPDIRECTDRAWSURFACE surface;
    DWORD before;
    DWORD after;
    DWORD round;
    HRESULT hr;
    char key[48];

    before = v9x_rtlk_free(dd2);
    wsprintfA(key, "%sFreeBefore", prefix);
    v9x_rtlk_uint(key, before);
    for (round = 0ul; round < rounds; ++round) {
        hr = v9x_rtlk_make(dd, depth, &surface);
        if (hr != DD_OK) {
            wsprintfA(key, "%sCreateFailed", prefix);
            v9x_rtlk_hex(key, (DWORD)hr);
            wsprintfA(key, "%sCreateFailedRound", prefix);
            v9x_rtlk_uint(key, round);
            return 0xFFFFFFFFul;
        }
        if (round == 0ul) {
            wsprintfA(key, "%sPitch", prefix);
            v9x_rtlk_uint(key, v9x_rtlk_pitch(surface));
            wsprintfA(key, "%sFreeWithOne", prefix);
            v9x_rtlk_uint(key, v9x_rtlk_free(dd2));
        }
        IDirectDrawSurface_Release(surface);
        if (round == 0ul) {
            wsprintfA(key, "%sFreeAfterOne", prefix);
            v9x_rtlk_uint(key, v9x_rtlk_free(dd2));
        }
    }
    after = v9x_rtlk_free(dd2);
    wsprintfA(key, "%sFreeAfter", prefix);
    v9x_rtlk_uint(key, after);
    if (before == 0xFFFFFFFFul || after == 0xFFFFFFFFul) {
        return 0xFFFFFFFFul;
    }
    wsprintfA(key, "%sLost", prefix);
    v9x_rtlk_uint(key, before > after ? before - after : 0ul);
    return before > after ? before - after : 0ul;
}

/* A leading number on the command line: the rounds. */
static DWORD v9x_rtlk_rounds(void)
{
    const char *line = GetCommandLineA();
    DWORD rounds = 0ul;

    if (*line == '"') {
        ++line;
        while (*line != '\0' && *line != '"') {
            ++line;
        }
        if (*line == '"') {
            ++line;
        }
    } else {
        while (*line != '\0' && *line != ' ') {
            ++line;
        }
    }
    while (*line == ' ') {
        ++line;
    }
    while (*line >= '0' && *line <= '9') {
        rounds = rounds * 10ul + (DWORD)(*line - '0');
        ++line;
    }
    return rounds == 0ul ? V9X_RTLK_ROUNDS : rounds;
}

void __stdcall V9xRenderTargetLeakEntry(void)
{
    HMODULE ddraw;
    V9X_RTLK_CREATE create;
    LPDIRECTDRAW dd = 0;
    LPDIRECTDRAW2 dd2 = 0;
    DWORD rounds = v9x_rtlk_rounds();
    DWORD target_lost;
    DWORD depth_lost;

    CreateDirectoryA("C:\\V9XDIAG", 0);
    DeleteFileA(V9X_RTLK_PATH);
    v9x_rtlk_text("Build", V9X_BUILD_ID);
    v9x_rtlk_text("Result", "RUNNING");
    v9x_rtlk_uint("Rounds", rounds);
    v9x_rtlk_uint("Width", V9X_RTLK_WIDTH);
    v9x_rtlk_uint("Height", V9X_RTLK_HEIGHT);

    ddraw = LoadLibraryA("DDRAW.DLL");
    create = ddraw != 0
        ? (V9X_RTLK_CREATE)GetProcAddress(ddraw, "DirectDrawCreate") : 0;
    if (create == 0 || create(0, &dd, 0) != DD_OK) {
        v9x_rtlk_text("Result", "FAIL-DDRAW");
        ExitProcess(1u);
    }
    if (IDirectDraw_SetCooperativeLevel(dd, 0, DDSCL_NORMAL) != DD_OK ||
        IDirectDraw_QueryInterface(dd, &v9x_rtlk_iid_dd2,
                                   (void **)&dd2) != DD_OK) {
        v9x_rtlk_text("Result", "FAIL-DDRAW2");
        IDirectDraw_Release(dd);
        ExitProcess(1u);
    }

    target_lost = v9x_rtlk_loop(dd, dd2, 0, rounds, "Target");
    depth_lost = v9x_rtlk_loop(dd, dd2, 1, rounds, "Depth");

    if (target_lost == 0xFFFFFFFFul) {
        v9x_rtlk_text("Result", "FAIL-TARGET");
    } else if (target_lost != 0ul) {
        v9x_rtlk_text("Result", "LEAK");
    } else {
        v9x_rtlk_text("Result", "PASS");
    }
    v9x_rtlk_text("DepthResult", depth_lost == 0xFFFFFFFFul ? "FAIL" :
                  depth_lost != 0ul ? "LEAK" : "PASS");

    IDirectDraw2_Release(dd2);
    IDirectDraw_Release(dd);
    ExitProcess(target_lost == 0ul ? 0u : 2u);
}
