/*
 * ATI3DCIF.DLL's DirectDraw side (cif_surface.h): one of the DLL's two
 * platform files (scripts\check-tree.ps1 lists it). DirectDraw and the HAL
 * are loaded at run time, so the DLL imports KERNEL32 and USER32 only, as
 * the Glide DLLs and the ICD do (glide_surface.c, whose texture surfaces
 * these follow).
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include "cif_surface.h"

typedef HRESULT (WINAPI *V9X_CIF_DDRAW_CREATE)(GUID *, LPDIRECTDRAW *,
                                               IUnknown *);

/* The display layouts a 3DCIF target can be: 16 bits, 5:6:5 or 5:5:5. */
#define V9X_CIF_DISPLAY_BITS 16ul

static HMODULE v9x_cif_ddraw_module;
static LPDIRECTDRAW v9x_cif_ddraw;
static HMODULE v9x_cif_hal_module;
static const V9X_R3D_INTERFACE *v9x_cif_interface;
static V9X_R3D_ABI_DESCRIBE v9x_cif_description;
static int v9x_cif_open;
static v9x_u32 v9x_cif_hwtex_failures;

static void v9x_cif_zero(void *block, DWORD bytes)
{
    DWORD i;

    for (i = 0ul; i < bytes; ++i) {
        ((BYTE *)block)[i] = 0u;
    }
}

static int v9x_cif_describe(void)
{
    v9x_u32 result;

    v9x_cif_zero(&v9x_cif_description, sizeof(v9x_cif_description));
    v9x_cif_description.struct_bytes = sizeof(v9x_cif_description);
    result = v9x_cif_interface->describe(&v9x_cif_description);
    v9x_cif_log3("device: describe result=%lu engine=%lu generation=%lu",
                 result, v9x_cif_description.engine,
                 v9x_cif_description.generation);
    v9x_cif_log3("device: target formats=%08lX texture formats=%08lX hw max=%lu",
                 v9x_cif_description.target_formats,
                 v9x_cif_description.texture_formats,
                 v9x_cif_description.hw_texture_size_max);
    return result == V9X_R3D_RESULT_OK;
}

/*
 * DirectDraw at the normal level, with no window: it creates only texture
 * surfaces and reads the mode, and the application's exclusive object
 * keeps the screen. The HAL is the one DirectDraw loaded; the DLL's
 * LoadLibrary finds that instance, as Glide's does.
 */
int v9x_cif_device_open(void)
{
    V9X_CIF_DDRAW_CREATE create;
    V9X_R3D_ENTRY_FN entry;
    HRESULT hr;

    if (v9x_cif_open) {
        return 1;
    }
    if (v9x_cif_ddraw_module == 0) {
        v9x_cif_ddraw_module = LoadLibraryA("DDRAW.DLL");
    }
    create = v9x_cif_ddraw_module != 0
        ? (V9X_CIF_DDRAW_CREATE)GetProcAddress(v9x_cif_ddraw_module,
                                               "DirectDrawCreate")
        : 0;
    if (create == 0 || create(0, &v9x_cif_ddraw, 0) != DD_OK) {
        v9x_cif_log_text("device: no DirectDraw");
        v9x_cif_ddraw = 0;
        return 0;
    }
    hr = IDirectDraw_SetCooperativeLevel(v9x_cif_ddraw, 0, DDSCL_NORMAL);
    if (hr != DD_OK) {
        v9x_cif_log3("device: cooperative level hr=%08lX", (DWORD)hr, 0ul,
                     0ul);
        v9x_cif_device_close();
        return 0;
    }
    if (v9x_cif_hal_module == 0) {
        v9x_cif_hal_module = LoadLibraryA("V9XHAL.DLL");
    }
    entry = v9x_cif_hal_module != 0
        ? (V9X_R3D_ENTRY_FN)GetProcAddress(v9x_cif_hal_module,
                                           "V9xRenderInterface")
        : 0;
    v9x_cif_interface = entry != 0
        ? entry(V9X_R3D_ABI_VERSION, sizeof(V9X_R3D_INTERFACE)) : 0;
    if (v9x_cif_interface == 0) {
        v9x_cif_log_text("device: no render interface");
        v9x_cif_device_close();
        return 0;
    }
    if (!v9x_cif_describe()) {
        v9x_cif_device_close();
        return 0;
    }
    v9x_cif_open = 1;
    return 1;
}

void v9x_cif_device_close(void)
{
    v9x_cif_open = 0;
    v9x_cif_interface = 0;
    if (v9x_cif_ddraw != 0) {
        IDirectDraw_Release(v9x_cif_ddraw);
        v9x_cif_ddraw = 0;
        v9x_cif_log_text("device: closed");
    }
}

const V9X_R3D_INTERFACE *v9x_cif_device_interface(void)
{
    return v9x_cif_open ? v9x_cif_interface : 0;
}

const V9X_R3D_ABI_DESCRIBE *v9x_cif_device_description(void)
{
    return v9x_cif_open ? &v9x_cif_description : 0;
}

int v9x_cif_device_redescribe(void)
{
    if (!v9x_cif_open) {
        return 0;
    }
    return v9x_cif_describe();
}

int v9x_cif_device_display(v9x_u32 *width, v9x_u32 *height, v9x_u32 *pitch,
                           v9x_u32 *format)
{
    DDSURFACEDESC desc;

    if (!v9x_cif_open) {
        return 0;
    }
    v9x_cif_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    if (IDirectDraw_GetDisplayMode(v9x_cif_ddraw, &desc) != DD_OK) {
        return 0;
    }
    *width = desc.dwWidth;
    *height = desc.dwHeight;
    *pitch = (v9x_u32)desc.lPitch;
    *format = 0ul;
    if (desc.ddpfPixelFormat.dwRGBBitCount == V9X_CIF_DISPLAY_BITS) {
        if (desc.ddpfPixelFormat.dwGBitMask == 0x07e0ul) {
            *format = V9X_R3D_ABI_FORMAT_RGB565;
        } else if (desc.ddpfPixelFormat.dwGBitMask == 0x03e0ul) {
            *format = V9X_R3D_ABI_FORMAT_XRGB1555;
        }
    }
    return 1;
}

/* The DirectDraw pixel format of a V9X_R3D_ABI_FORMAT_* layout, as
 * glide_surface.c's gives it. */
static int v9x_cif_hwtex_pixel_format(v9x_u32 format, DDPIXELFORMAT *out)
{
    v9x_cif_zero(out, sizeof(*out));
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

void *v9x_cif_hwtex_create(v9x_u32 width, v9x_u32 height, v9x_u32 format)
{
    DDSURFACEDESC desc;
    LPDIRECTDRAWSURFACE surface = 0;
    HRESULT hr;

    if (!v9x_cif_open) {
        return 0;
    }
    v9x_cif_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = width;
    desc.dwHeight = height;
    if (!v9x_cif_hwtex_pixel_format(format, &desc.ddpfPixelFormat)) {
        return 0;
    }
    /* Video memory: the Rage and Mach64 engines sample nothing else. */
    desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY;
    hr = IDirectDraw_CreateSurface(v9x_cif_ddraw, &desc, &surface, 0);
    if (hr != DD_OK) {
        if (v9x_cif_hwtex_failures++ < 8ul) {
            v9x_cif_log3("hwtex create %08lX format=%lu hr=%08lX",
                         (width << 16) | height, format, (DWORD)hr);
        }
        return 0;
    }
    return surface;
}

int v9x_cif_hwtex_upload(void *surface, const v9x_u16 *texels,
                         v9x_u32 width, v9x_u32 height)
{
    LPDIRECTDRAWSURFACE target = (LPDIRECTDRAWSURFACE)surface;
    DDSURFACEDESC desc;
    const BYTE *source = (const BYTE *)texels;
    BYTE *row;
    DWORD y;
    DWORD i;
    HRESULT hr;

    v9x_cif_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = IDirectDrawSurface_Lock(target, 0, &desc,
                                 DDLOCK_WAIT | DDLOCK_WRITEONLY, 0);
    if (hr == DDERR_SURFACELOST) {
        IDirectDrawSurface_Restore(target);
        hr = IDirectDrawSurface_Lock(target, 0, &desc,
                                     DDLOCK_WAIT | DDLOCK_WRITEONLY, 0);
    }
    if (hr != DD_OK) {
        v9x_cif_log3("hwtex lock hr=%08lX", (DWORD)hr, 0ul, 0ul);
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

void v9x_cif_hwtex_release(void *surface)
{
    if (surface != 0) {
        IDirectDrawSurface_Release((LPDIRECTDRAWSURFACE)surface);
    }
}
