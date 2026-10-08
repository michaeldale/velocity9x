/*
 * GLIDE2X.DLL's DirectDraw side (glide_surface.h): one of the DLL's two
 * platform files (scripts\check-tree.ps1 lists it). DirectDraw and the HAL
 * are loaded at run time, so the DLL imports KERNEL32 and USER32 only, as
 * the ICD does (gl_surface.c, whose open order this follows).
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include "glide_surface.h"

typedef HRESULT (WINAPI *V9X_GLIDE_DDRAW_CREATE)(GUID *, LPDIRECTDRAW *,
                                                 IUnknown *);

/* The depth surface's bits: the Voodoo's aux buffer is 16 bits, and 16 is
 * what every engine's Direct3D Z buffer takes. */
#define V9X_GLIDE_DEPTH_BITS 16ul
#define V9X_GLIDE_DISPLAY_BITS 16ul

static const char v9x_glide_window_class[] = "Velocity9xGlide";

static HMODULE v9x_glide_ddraw_module;
static LPDIRECTDRAW v9x_glide_ddraw;
static LPDIRECTDRAWSURFACE v9x_glide_primary;
static LPDIRECTDRAWSURFACE v9x_glide_back;
static LPDIRECTDRAWSURFACE v9x_glide_depth;
static HMODULE v9x_glide_hal_module;
static const V9X_R3D_INTERFACE *v9x_glide_interface;
static V9X_R3D_ABI_DESCRIBE v9x_glide_description;
static HWND v9x_glide_window;
static int v9x_glide_window_ours;
static int v9x_glide_open;

static void v9x_glide_zero(void *block, DWORD bytes)
{
    DWORD i;

    for (i = 0ul; i < bytes; ++i) {
        ((BYTE *)block)[i] = 0u;
    }
}

static int v9x_glide_describe(void)
{
    v9x_u32 result;

    v9x_glide_zero(&v9x_glide_description, sizeof(v9x_glide_description));
    v9x_glide_description.struct_bytes = sizeof(v9x_glide_description);
    result = v9x_glide_interface->describe(&v9x_glide_description);
    v9x_glide_log3("device: describe result=%lu engine=%lu generation=%lu", result,
                   v9x_glide_description.engine,
                   v9x_glide_description.generation);
    v9x_glide_log3("device: target formats=%08lX texture formats=%08lX hw max=%lu",
                   v9x_glide_description.target_formats,
                   v9x_glide_description.texture_formats,
                   v9x_glide_description.hw_texture_size_max);
    return result == V9X_R3D_RESULT_OK;
}

static LRESULT CALLBACK v9x_glide_window_proc(HWND window, UINT message,
                                              WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcA(window, message, wparam, lparam);
}

/*
 * Exclusive mode needs a top-level window. A game that passes none - NFS II
 * SE passed 0 (census) - is given its own active window, then any visible
 * top-level window of this thread, and only then one made here.
 */
static HWND v9x_glide_find_window(void *window)
{
    WNDCLASSA wc;
    HWND found;

    v9x_glide_window_ours = 0;
    if (window != 0) {
        return (HWND)window;
    }
    found = GetActiveWindow();
    if (found != 0) {
        return found;
    }
    found = GetForegroundWindow();
    if (found != 0 &&
        GetWindowThreadProcessId(found, 0) == GetCurrentThreadId()) {
        return found;
    }
    v9x_glide_zero(&wc, sizeof(wc));
    wc.lpfnWndProc = v9x_glide_window_proc;
    wc.hInstance = GetModuleHandleA(0);
    wc.lpszClassName = v9x_glide_window_class;
    RegisterClassA(&wc);
    found = CreateWindowExA(WS_EX_TOPMOST, v9x_glide_window_class, "Glide",
                            WS_POPUP | WS_VISIBLE, 0, 0,
                            GetSystemMetrics(SM_CXSCREEN),
                            GetSystemMetrics(SM_CYSCREEN), 0, 0,
                            GetModuleHandleA(0), 0);
    if (found != 0) {
        v9x_glide_window_ours = 1;
    }
    return found;
}

static LPDIRECTDRAWSURFACE v9x_glide_make_depth(DWORD width, DWORD height)
{
    DDSURFACEDESC desc;
    LPDIRECTDRAWSURFACE surface = 0;
    HRESULT hr;

    v9x_glide_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_ZBUFFERBITDEPTH;
    desc.dwWidth = width;
    desc.dwHeight = height;
    desc.dwZBufferBitDepth = V9X_GLIDE_DEPTH_BITS;
    desc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_VIDEOMEMORY;
    hr = IDirectDraw_CreateSurface(v9x_glide_ddraw, &desc, &surface, 0);
    if (hr != DD_OK) {
        v9x_glide_log3("device: depth %lux%lu hr=%08lX", width, height, (DWORD)hr);
        return 0;
    }
    return surface;
}

int v9x_glide_device_open(void *window, v9x_u32 width, v9x_u32 height,
                          v9x_u32 color_buffers, v9x_u32 aux_buffers)
{
    V9X_GLIDE_DDRAW_CREATE create;
    V9X_R3D_ENTRY_FN entry;
    DDSURFACEDESC desc;
    DDSCAPS caps;
    HRESULT hr;

    if (v9x_glide_open) {
        v9x_glide_device_close();
    }
    v9x_glide_window = v9x_glide_find_window(window);
    if (v9x_glide_window == 0) {
        v9x_glide_log("device: no window");
        return 0;
    }
    v9x_glide_log3("device: window=%08lX ours=%lu", (DWORD)v9x_glide_window,
                   (DWORD)v9x_glide_window_ours, 0ul);

    if (v9x_glide_ddraw_module == 0) {
        v9x_glide_ddraw_module = LoadLibraryA("DDRAW.DLL");
    }
    create = v9x_glide_ddraw_module != 0
        ? (V9X_GLIDE_DDRAW_CREATE)GetProcAddress(v9x_glide_ddraw_module,
                                                 "DirectDrawCreate")
        : 0;
    if (create == 0 || create(0, &v9x_glide_ddraw, 0) != DD_OK) {
        v9x_glide_log("device: DirectDrawCreate failed");
        v9x_glide_ddraw = 0;
        v9x_glide_device_close();
        return 0;
    }
    hr = IDirectDraw_SetCooperativeLevel(v9x_glide_ddraw, v9x_glide_window,
                                         DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
    if (hr != DD_OK) {
        v9x_glide_log3("device: SetCooperativeLevel hr=%08lX", (DWORD)hr, 0ul, 0ul);
        v9x_glide_device_close();
        return 0;
    }
    hr = IDirectDraw_SetDisplayMode(v9x_glide_ddraw, width, height,
                                    V9X_GLIDE_DISPLAY_BITS);
    if (hr != DD_OK) {
        v9x_glide_log3("device: SetDisplayMode %lux%lu hr=%08lX", width, height,
                       (DWORD)hr);
        v9x_glide_device_close();
        return 0;
    }

    /* The colour buffers as one flip chain: the primary is always the
     * front, its attached back buffer always the back. */
    v9x_glide_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP |
                          DDSCAPS_COMPLEX | DDSCAPS_3DDEVICE;
    desc.dwBackBufferCount = color_buffers > 1ul ? color_buffers - 1ul : 1ul;
    hr = IDirectDraw_CreateSurface(v9x_glide_ddraw, &desc, &v9x_glide_primary, 0);
    if (hr != DD_OK) {
        v9x_glide_log3("device: flip chain of %lu hr=%08lX", desc.dwBackBufferCount,
                       (DWORD)hr, 0ul);
        v9x_glide_primary = 0;
        v9x_glide_device_close();
        return 0;
    }
    v9x_glide_zero(&caps, sizeof(caps));
    caps.dwCaps = DDSCAPS_BACKBUFFER;
    hr = IDirectDrawSurface_GetAttachedSurface(v9x_glide_primary, &caps,
                                               &v9x_glide_back);
    if (hr != DD_OK) {
        v9x_glide_log3("device: back buffer hr=%08lX", (DWORD)hr, 0ul, 0ul);
        v9x_glide_back = 0;
        v9x_glide_device_close();
        return 0;
    }
    if (aux_buffers != 0ul) {
        v9x_glide_depth = v9x_glide_make_depth(width, height);
        if (v9x_glide_depth == 0) {
            v9x_glide_device_close();
            return 0;
        }
    }

    /* After the mode set: SetDisplayMode runs DriverInit, which starts a
     * new generation, and the describe must be of that one. */
    if (v9x_glide_hal_module == 0) {
        v9x_glide_hal_module = LoadLibraryA("V9XHAL.DLL");
    }
    entry = v9x_glide_hal_module != 0
        ? (V9X_R3D_ENTRY_FN)GetProcAddress(v9x_glide_hal_module,
                                           "V9xRenderInterface")
        : 0;
    v9x_glide_interface = entry != 0
        ? entry(V9X_R3D_ABI_VERSION, sizeof(V9X_R3D_INTERFACE)) : 0;
    if (v9x_glide_interface == 0) {
        v9x_glide_log("device: no render interface");
        v9x_glide_device_close();
        return 0;
    }
    if (!v9x_glide_describe()) {
        v9x_glide_device_close();
        return 0;
    }
    if ((v9x_glide_description.target_formats &
         (1ul << V9X_R3D_ABI_FORMAT_RGB565)) == 0ul) {
        v9x_glide_log("device: the engine offers no RGB565 target");
        v9x_glide_device_close();
        return 0;
    }
    v9x_glide_open = 1;
    v9x_glide_log3("device: open %lux%lu buffers=%lu", width, height,
                   desc.dwBackBufferCount + 1ul);
    return 1;
}

void v9x_glide_device_close(void)
{
    v9x_glide_open = 0;
    v9x_glide_interface = 0;
    if (v9x_glide_depth != 0) {
        IDirectDrawSurface_Release(v9x_glide_depth);
        v9x_glide_depth = 0;
    }
    if (v9x_glide_back != 0) {
        IDirectDrawSurface_Release(v9x_glide_back);
        v9x_glide_back = 0;
    }
    if (v9x_glide_primary != 0) {
        IDirectDrawSurface_Release(v9x_glide_primary);
        v9x_glide_primary = 0;
    }
    if (v9x_glide_ddraw != 0) {
        IDirectDraw_RestoreDisplayMode(v9x_glide_ddraw);
        IDirectDraw_SetCooperativeLevel(v9x_glide_ddraw, v9x_glide_window,
                                        DDSCL_NORMAL);
        IDirectDraw_Release(v9x_glide_ddraw);
        v9x_glide_ddraw = 0;
        v9x_glide_log("device: closed");
    }
    if (v9x_glide_window_ours && v9x_glide_window != 0) {
        DestroyWindow(v9x_glide_window);
    }
    v9x_glide_window = 0;
    v9x_glide_window_ours = 0;
}

int v9x_glide_device_is_open(void)
{
    return v9x_glide_open;
}

const V9X_R3D_INTERFACE *v9x_glide_device_interface(void)
{
    return v9x_glide_open ? v9x_glide_interface : 0;
}

const V9X_R3D_ABI_DESCRIBE *v9x_glide_device_description(void)
{
    return v9x_glide_open ? &v9x_glide_description : 0;
}

v9x_u32 v9x_glide_device_generation(void)
{
    return v9x_glide_description.generation;
}

int v9x_glide_device_redescribe(void)
{
    if (!v9x_glide_open) {
        return 0;
    }
    return v9x_glide_describe();
}

void *v9x_glide_device_buffer(v9x_u32 buffer)
{
    if (!v9x_glide_open) {
        return 0;
    }
    if (buffer == V9X_GLIDE_BUFFER_FRONT) {
        return v9x_glide_primary;
    }
    if (buffer == V9X_GLIDE_BUFFER_BACK) {
        return v9x_glide_back;
    }
    if (buffer == V9X_GLIDE_BUFFER_AUX) {
        return v9x_glide_depth;
    }
    return 0;
}

/* A lost chain (another application took the screen) comes back with
 * Restore on the primary, which restores its attached back buffer too. */
static void v9x_glide_restore(void)
{
    if (v9x_glide_primary != 0 &&
        IDirectDrawSurface_IsLost(v9x_glide_primary) == DDERR_SURFACELOST) {
        IDirectDrawSurface_Restore(v9x_glide_primary);
    }
    if (v9x_glide_depth != 0 &&
        IDirectDrawSurface_IsLost(v9x_glide_depth) == DDERR_SURFACELOST) {
        IDirectDrawSurface_Restore(v9x_glide_depth);
    }
}

int v9x_glide_device_swap(v9x_u32 interval)
{
    HRESULT hr;
    v9x_u32 extra;

    if (!v9x_glide_open) {
        return 0;
    }
    (void)v9x_glide_interface->flush(v9x_glide_description.generation);
    /* A flip waits for one retrace; an interval of n waits n. Interval 0
     * (no wait) has no flag before DirectX 6 and is taken as 1. */
    for (extra = 1ul; extra < interval; ++extra) {
        IDirectDraw_WaitForVerticalBlank(v9x_glide_ddraw, DDWAITVB_BLOCKBEGIN, 0);
    }
    hr = IDirectDrawSurface_Flip(v9x_glide_primary, 0, DDFLIP_WAIT);
    if (hr == DDERR_SURFACELOST) {
        v9x_glide_restore();
        hr = IDirectDrawSurface_Flip(v9x_glide_primary, 0, DDFLIP_WAIT);
    }
    if (hr != DD_OK) {
        v9x_glide_log3("device: flip hr=%08lX", (DWORD)hr, 0ul, 0ul);
        return 0;
    }
    return 1;
}

int v9x_glide_device_lock(v9x_u32 buffer, int read_only, void **pixels,
                          v9x_u32 *pitch)
{
    LPDIRECTDRAWSURFACE surface = (LPDIRECTDRAWSURFACE)v9x_glide_device_buffer(buffer);
    DDSURFACEDESC desc;
    HRESULT hr;

    if (surface == 0) {
        return 0;
    }
    v9x_glide_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = IDirectDrawSurface_Lock(surface, 0, &desc,
                                 DDLOCK_WAIT | (read_only ? DDLOCK_READONLY : 0ul),
                                 0);
    if (hr == DDERR_SURFACELOST) {
        v9x_glide_restore();
        hr = IDirectDrawSurface_Lock(surface, 0, &desc,
                                     DDLOCK_WAIT |
                                     (read_only ? DDLOCK_READONLY : 0ul), 0);
    }
    if (hr != DD_OK) {
        v9x_glide_log3("device: lock buffer=%lu hr=%08lX", buffer, (DWORD)hr, 0ul);
        return 0;
    }
    *pixels = desc.lpSurface;
    *pitch = (v9x_u32)desc.lPitch;
    return 1;
}

void v9x_glide_device_unlock(v9x_u32 buffer)
{
    LPDIRECTDRAWSURFACE surface = (LPDIRECTDRAWSURFACE)v9x_glide_device_buffer(buffer);

    if (surface != 0) {
        IDirectDrawSurface_Unlock(surface, 0);
    }
}

/* The DirectDraw pixel format of a V9X_R3D_ABI_FORMAT_* layout, as
 * gl_surface.c's v9x_gl_hwtex_pixel_format gives it. */
static int v9x_glide_hwtex_pixel_format(v9x_u32 format, DDPIXELFORMAT *out)
{
    v9x_glide_zero(out, sizeof(*out));
    out->dwSize = sizeof(*out);
    out->dwFlags = DDPF_RGB;
    out->dwRGBBitCount = 16ul;
    if (format == V9X_R3D_ABI_FORMAT_RGB565) {
        out->dwRBitMask = 0xF800ul;
        out->dwGBitMask = 0x07E0ul;
        out->dwBBitMask = 0x001Ful;
        return 1;
    }
    out->dwFlags |= DDPF_ALPHAPIXELS;
    if (format == V9X_R3D_ABI_FORMAT_ARGB1555) {
        out->dwRBitMask = 0x7C00ul;
        out->dwGBitMask = 0x03E0ul;
        out->dwBBitMask = 0x001Ful;
        out->dwRGBAlphaBitMask = 0x8000ul;
        return 1;
    }
    if (format == V9X_R3D_ABI_FORMAT_ARGB4444) {
        out->dwRBitMask = 0x0F00ul;
        out->dwGBitMask = 0x00F0ul;
        out->dwBBitMask = 0x000Ful;
        out->dwRGBAlphaBitMask = 0xF000ul;
        return 1;
    }
    return 0;
}

void *v9x_glide_hwtex_create(v9x_u32 width, v9x_u32 height, v9x_u32 format)
{
    DDSURFACEDESC desc;
    LPDIRECTDRAWSURFACE surface = 0;
    HRESULT hr;

    if (!v9x_glide_open) {
        return 0;
    }
    v9x_glide_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = width;
    desc.dwHeight = height;
    if (!v9x_glide_hwtex_pixel_format(format, &desc.ddpfPixelFormat)) {
        return 0;
    }
    /* Video memory, where the HAL's placement puts what the sampler reads;
     * Gen3's bind refuses a system-memory texture (gl_surface.c). */
    desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY;
    hr = IDirectDraw_CreateSurface(v9x_glide_ddraw, &desc, &surface, 0);
    if (hr != DD_OK) {
        v9x_glide_log3("hwtex create %08lX format=%lu hr=%08lX",
                       (width << 16) | height, format, (DWORD)hr);
        return 0;
    }
    return surface;
}

int v9x_glide_hwtex_upload(void *surface, const v9x_u16 *texels,
                           v9x_u32 width, v9x_u32 height)
{
    LPDIRECTDRAWSURFACE target = (LPDIRECTDRAWSURFACE)surface;
    DDSURFACEDESC desc;
    const BYTE *source = (const BYTE *)texels;
    BYTE *row;
    DWORD y;
    DWORD i;
    HRESULT hr;

    v9x_glide_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = IDirectDrawSurface_Lock(target, 0, &desc,
                                 DDLOCK_WAIT | DDLOCK_WRITEONLY, 0);
    if (hr == DDERR_SURFACELOST) {
        IDirectDrawSurface_Restore(target);
        hr = IDirectDrawSurface_Lock(target, 0, &desc,
                                     DDLOCK_WAIT | DDLOCK_WRITEONLY, 0);
    }
    if (hr != DD_OK) {
        v9x_glide_log3("hwtex lock hr=%08lX", (DWORD)hr, 0ul, 0ul);
        return 0;
    }
    if (desc.dwWidth != width || desc.dwHeight != height) {
        IDirectDrawSurface_Unlock(target, 0);
        return 0;
    }
    row = (BYTE *)desc.lpSurface;
    for (y = 0ul; y < height; ++y) {
        for (i = 0ul; i < width * 2ul; ++i) {
            row[i] = source[i];
        }
        source += width * 2ul;
        row += desc.lPitch;
    }
    IDirectDrawSurface_Unlock(target, 0);
    return 1;
}

void v9x_glide_hwtex_release(void *surface)
{
    if (surface != 0) {
        IDirectDrawSurface_Release((LPDIRECTDRAWSURFACE)surface);
    }
}
