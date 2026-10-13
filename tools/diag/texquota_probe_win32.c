/*
 * V9XTXQ.EXE: how many 64x64 textures fit in video memory beside a Glide-
 * shaped device, and where each one lands.
 *
 * Carmageddon II's intro (V9X-N1A23B, GMA 900; and the netbook, 945GSE)
 * draws 15 paletted 64x64 tiles a frame through GLIDE2X.DLL, and texture
 * creation fails with DDERR_OUTOFVIDEOMEMORY after about fourteen: evictions
 * then match uploads one for one. On paper a 640x480 triple-buffered chain
 * and a Z buffer leave about 5 MB of an 8 MB part, and 64x64 at 16 bits is
 * 8 KB. This repeats the DLL's setup - exclusive 640x480x16, the same flip
 * chain and Z buffer (glide_surface.c) - then creates textures the way
 * v9x_glide_hwtex_create does until one fails, recording each one's address
 * and pitch from a lock and the free texture memory DirectDraw reports.
 *
 * Usage: V9XTXQ [back buffers, default 2] [nodepth]
 * Writes C:\V9XDIAG\V9XTXQ.INI.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_TXQ_PATH     "C:\\V9XDIAG\\V9XTXQ.INI"
#define V9X_TXQ_SECTION  "Velocity9xTextureQuota"
#define V9X_TXQ_MAX      512
#define V9X_TXQ_DETAIL   48
#define V9X_TXQ_EDGE     64ul

typedef HRESULT (WINAPI *V9X_TXQ_CREATE)(GUID *guid, LPDIRECTDRAW *dd,
                                         IUnknown *outer);

/* IID_IDirectDraw2, {B3A6F3E0-2B43-11CF-A2DE-00AA00B93356} (DDRAW.H). */
static const GUID v9x_txq_iid_dd2 = {
    0xB3A6F3E0ul, 0x2B43u, 0x11CFu,
    { 0xA2u, 0xDEu, 0x00u, 0xAAu, 0x00u, 0xB9u, 0x33u, 0x56u }
};

static LPDIRECTDRAWSURFACE v9x_txq_textures[V9X_TXQ_MAX];

static void v9x_txq_text(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_TXQ_SECTION, key, value, V9X_TXQ_PATH);
}

static void v9x_txq_hex(const char *key, DWORD value)
{
    char text[16];

    wsprintfA(text, "0x%08lX", value);
    v9x_txq_text(key, text);
}

static void v9x_txq_uint(const char *key, DWORD value)
{
    char text[16];

    wsprintfA(text, "%lu", value);
    v9x_txq_text(key, text);
}

static void v9x_txq_zero(void *memory, DWORD bytes)
{
    BYTE *at = (BYTE *)memory;

    while (bytes-- != 0ul) {
        *at++ = 0u;
    }
}

/* Free and total texture memory as DirectDraw 2 reports it. */
static void v9x_txq_free(LPDIRECTDRAW2 dd2, const char *prefix)
{
    DDSCAPS caps;
    DWORD total = 0ul;
    DWORD free = 0ul;
    char key[48];
    HRESULT hr;

    if (dd2 == 0) {
        return;
    }
    caps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY;
    hr = IDirectDraw2_GetAvailableVidMem(dd2, &caps, &total, &free);
    wsprintfA(key, "%sFree", prefix);
    v9x_txq_uint(key, hr == DD_OK ? free : 0xFFFFFFFFul);
    wsprintfA(key, "%sTotal", prefix);
    v9x_txq_uint(key, hr == DD_OK ? total : 0xFFFFFFFFul);
}

/* Where a surface lies, from a lock: its address and pitch. */
static void v9x_txq_where(LPDIRECTDRAWSURFACE surface, const char *prefix)
{
    DDSURFACEDESC desc;
    char key[48];

    v9x_txq_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    if (IDirectDrawSurface_Lock(surface, 0, &desc, DDLOCK_WAIT, 0) != DD_OK) {
        wsprintfA(key, "%sLock", prefix);
        v9x_txq_text(key, "refused");
        return;
    }
    wsprintfA(key, "%sAddress", prefix);
    v9x_txq_hex(key, (DWORD)desc.lpSurface);
    wsprintfA(key, "%sPitch", prefix);
    v9x_txq_uint(key, (DWORD)desc.lPitch);
    IDirectDrawSurface_Unlock(surface, desc.lpSurface);
}

static LRESULT CALLBACK v9x_txq_window(HWND window, UINT message,
                                       WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcA(window, message, wparam, lparam);
}

/* The arguments: a leading number of back buffers, and "nodepth". */
static void v9x_txq_arguments(DWORD *back, int *depth)
{
    const char *line = GetCommandLineA();

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
    if (*line >= '0' && *line <= '9') {
        *back = 0ul;
        while (*line >= '0' && *line <= '9') {
            *back = *back * 10ul + (DWORD)(*line - '0');
            ++line;
        }
    }
    while (*line == ' ') {
        ++line;
    }
    if ((line[0] | 0x20) == 'n' && (line[1] | 0x20) == 'o') {
        *depth = 0;
    }
}

void __stdcall V9xTexQuotaProbeEntry(void)
{
    HMODULE ddraw;
    V9X_TXQ_CREATE create;
    LPDIRECTDRAW dd = 0;
    LPDIRECTDRAW2 dd2 = 0;
    LPDIRECTDRAWSURFACE primary = 0;
    LPDIRECTDRAWSURFACE depth_surface = 0;
    DDSURFACEDESC desc;
    WNDCLASSA class_info;
    HWND window;
    HRESULT hr;
    DWORD back = 2ul;
    DWORD count;
    int depth = 1;
    char key[48];

    CreateDirectoryA("C:\\V9XDIAG", 0);
    DeleteFileA(V9X_TXQ_PATH);
    v9x_txq_text("Build", V9X_BUILD_ID);
    v9x_txq_text("Result", "RUNNING");
    v9x_txq_arguments(&back, &depth);
    v9x_txq_uint("BackBuffers", back);
    v9x_txq_uint("Depth", (DWORD)depth);

    v9x_txq_zero(&class_info, sizeof(class_info));
    class_info.lpfnWndProc = v9x_txq_window;
    class_info.hInstance = GetModuleHandleA(0);
    class_info.lpszClassName = "V9XTXQ";
    RegisterClassA(&class_info);
    window = CreateWindowExA(WS_EX_TOPMOST, "V9XTXQ", "V9XTXQ",
                             WS_POPUP | WS_VISIBLE, 0, 0, 640, 480, 0, 0,
                             class_info.hInstance, 0);

    ddraw = LoadLibraryA("DDRAW.DLL");
    create = ddraw != 0
        ? (V9X_TXQ_CREATE)GetProcAddress(ddraw, "DirectDrawCreate") : 0;
    if (window == 0 || create == 0 || create(0, &dd, 0) != DD_OK) {
        v9x_txq_text("Result", "FAIL-DDRAW");
        ExitProcess(1u);
    }
    if (IDirectDraw_QueryInterface(dd, &v9x_txq_iid_dd2,
                                   (void **)&dd2) != DD_OK) {
        dd2 = 0;
    }
    v9x_txq_free(dd2, "Desktop");
    hr = IDirectDraw_SetCooperativeLevel(dd, window,
                                         DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
    if (hr == DD_OK) {
        hr = IDirectDraw_SetDisplayMode(dd, 640ul, 480ul, 16ul);
    }
    if (hr != DD_OK) {
        v9x_txq_hex("ModeHr", (DWORD)hr);
        v9x_txq_text("Result", "FAIL-MODE");
        ExitProcess(1u);
    }
    v9x_txq_free(dd2, "Mode");

    /* The chain glide_surface.c makes. */
    v9x_txq_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP |
                          DDSCAPS_COMPLEX | DDSCAPS_3DDEVICE;
    desc.dwBackBufferCount = back;
    hr = IDirectDraw_CreateSurface(dd, &desc, &primary, 0);
    v9x_txq_hex("ChainHr", (DWORD)hr);
    if (hr != DD_OK) {
        v9x_txq_text("Result", "FAIL-CHAIN");
        IDirectDraw_RestoreDisplayMode(dd);
        ExitProcess(1u);
    }
    v9x_txq_where(primary, "Primary");
    v9x_txq_free(dd2, "Chain");

    if (depth) {
        v9x_txq_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT |
                       DDSD_ZBUFFERBITDEPTH;
        desc.dwWidth = 640ul;
        desc.dwHeight = 480ul;
        desc.dwZBufferBitDepth = 16ul;
        desc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_VIDEOMEMORY;
        hr = IDirectDraw_CreateSurface(dd, &desc, &depth_surface, 0);
        v9x_txq_hex("DepthHr", (DWORD)hr);
        if (hr == DD_OK) {
            v9x_txq_where(depth_surface, "DepthSurface");
        }
        v9x_txq_free(dd2, "Zbuffer");
    }

    /* Textures as v9x_glide_hwtex_create makes them, ARGB1555 (the DLL's
     * format 3, V9X_R3D_ABI_FORMAT_ARGB1555, for the intro's tiles), until
     * one fails. */
    for (count = 0ul; count < V9X_TXQ_MAX; ++count) {
        v9x_txq_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT |
                       DDSD_PIXELFORMAT;
        desc.dwWidth = V9X_TXQ_EDGE;
        desc.dwHeight = V9X_TXQ_EDGE;
        desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        desc.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
        desc.ddpfPixelFormat.dwRGBBitCount = 16ul;
        desc.ddpfPixelFormat.dwRBitMask = 0x7C00ul;
        desc.ddpfPixelFormat.dwGBitMask = 0x03E0ul;
        desc.ddpfPixelFormat.dwBBitMask = 0x001Ful;
        desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0x8000ul;
        desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY;
        hr = IDirectDraw_CreateSurface(dd, &desc, &v9x_txq_textures[count], 0);
        if (hr != DD_OK) {
            v9x_txq_textures[count] = 0;
            v9x_txq_hex("TextureFailHr", (DWORD)hr);
            break;
        }
        if (count < V9X_TXQ_DETAIL) {
            wsprintfA(key, "Tex%02lu", count);
            v9x_txq_where(v9x_txq_textures[count], key);
            v9x_txq_free(dd2, key);
        }
    }
    v9x_txq_uint("Textures", count);
    v9x_txq_free(dd2, "End");

    while (count != 0ul) {
        --count;
        IDirectDrawSurface_Release(v9x_txq_textures[count]);
    }
    if (depth_surface != 0) {
        IDirectDrawSurface_Release(depth_surface);
    }
    IDirectDrawSurface_Release(primary);
    IDirectDraw_RestoreDisplayMode(dd);
    IDirectDraw_SetCooperativeLevel(dd, window, DDSCL_NORMAL);
    if (dd2 != 0) {
        IDirectDraw2_Release(dd2);
    }
    IDirectDraw_Release(dd);
    v9x_txq_text("Result", "COMPLETE");
    ExitProcess(0u);
}
