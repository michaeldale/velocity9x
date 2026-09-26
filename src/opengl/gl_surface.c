/*
 * The ICD's DirectDraw side (gl_surface.h): one of the ICD's two platform
 * files (scripts\check-tree.ps1 lists it).
 *
 * DDSCL_NORMAL, one primary for the process and one clipper per window, as
 * any windowed DirectDraw application does; the back and depth buffers are
 * video-memory surfaces of the window's client size, so the HAL resolves
 * them exactly as it resolves a Direct3D render target and Z buffer.
 * SwapBuffers is a Blt through the clipper, which DirectDraw splits into one
 * HAL Blt per visible rectangle (measured, Phase 0.3).
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include "gl_surface.h"

#define V9X_GL_DRAWABLES_MAX 16u

typedef HRESULT (WINAPI *V9X_GL_DDRAW_CREATE)(GUID *, LPDIRECTDRAW *,
                                              IUnknown *);

struct v9x_gl_drawable {
    HWND window;
    LPDIRECTDRAWCLIPPER clipper;
    LPDIRECTDRAWSURFACE back;
    LPDIRECTDRAWSURFACE depth;
    /* Made on the first GL_FRONT draw, clear or read (v9x_gl_drawable_front). */
    LPDIRECTDRAWSURFACE front;
    DWORD width;
    DWORD height;
};

static HMODULE v9x_gl_ddraw_module;
static LPDIRECTDRAW v9x_gl_ddraw;
static LPDIRECTDRAWSURFACE v9x_gl_primary;
static HMODULE v9x_gl_hal_module;
static const V9X_R3D_INTERFACE *v9x_gl_interface;
static V9X_R3D_ABI_DESCRIBE v9x_gl_description;
static int v9x_gl_device_state;     /* 0 untried, 1 open, -1 failed */
static struct v9x_gl_drawable v9x_gl_drawables[V9X_GL_DRAWABLES_MAX];

static void v9x_gl_surface_zero(void *block, DWORD bytes)
{
    DWORD i;

    for (i = 0ul; i < bytes; ++i) {
        ((BYTE *)block)[i] = 0u;
    }
}

static int v9x_gl_describe_now(void)
{
    v9x_u32 result;

    v9x_gl_surface_zero(&v9x_gl_description, sizeof(v9x_gl_description));
    v9x_gl_description.struct_bytes = sizeof(v9x_gl_description);
    result = v9x_gl_interface->describe(&v9x_gl_description);
    v9x_gl_log3("describe result=%lu engine=%lu formats=%08lX", result,
                v9x_gl_description.engine,
                v9x_gl_description.target_formats);
    return result == V9X_R3D_RESULT_OK;
}

int v9x_gl_device_open(void)
{
    V9X_GL_DDRAW_CREATE create;
    V9X_R3D_ENTRY_FN entry;
    DDSURFACEDESC desc;
    HRESULT hr;

    if (v9x_gl_device_state != 0) {
        return v9x_gl_device_state > 0;
    }
    v9x_gl_device_state = -1;
    v9x_gl_ddraw_module = LoadLibraryA("DDRAW.DLL");
    create = v9x_gl_ddraw_module != 0
        ? (V9X_GL_DDRAW_CREATE)GetProcAddress(v9x_gl_ddraw_module,
                                              "DirectDrawCreate")
        : 0;
    if (create == 0 || create(0, &v9x_gl_ddraw, 0) != DD_OK) {
        v9x_gl_log("device: DirectDrawCreate failed");
        return 0;
    }
    hr = IDirectDraw_SetCooperativeLevel(v9x_gl_ddraw, 0, DDSCL_NORMAL);
    if (hr != DD_OK) {
        v9x_gl_log3("device: SetCooperativeLevel hr=%08lX", (DWORD)hr, 0ul,
                    0ul);
        return 0;
    }
    v9x_gl_surface_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    hr = IDirectDraw_CreateSurface(v9x_gl_ddraw, &desc, &v9x_gl_primary, 0);
    if (hr != DD_OK) {
        v9x_gl_log3("device: primary hr=%08lX", (DWORD)hr, 0ul, 0ul);
        return 0;
    }
    /* After DirectDrawCreate: DirectDraw's helper process is what loads the
     * HAL, and this process's own load returns the same shared instance
     * (docs\decisions\2026-09-26-phase3-render-interface-v1-draws-from-
     * another-process.md). Never freed while the ICD is loaded. */
    v9x_gl_hal_module = LoadLibraryA("V9XHAL.DLL");
    entry = v9x_gl_hal_module != 0
        ? (V9X_R3D_ENTRY_FN)GetProcAddress(v9x_gl_hal_module,
                                           "V9xRenderInterface")
        : 0;
    v9x_gl_interface = entry != 0
        ? entry(V9X_R3D_ABI_VERSION, sizeof(V9X_R3D_INTERFACE)) : 0;
    if (v9x_gl_interface == 0) {
        v9x_gl_log("device: no render interface");
        return 0;
    }
    if (!v9x_gl_describe_now()) {
        return 0;
    }
    v9x_gl_device_state = 1;
    return 1;
}

const V9X_R3D_INTERFACE *v9x_gl_device_interface(void)
{
    return v9x_gl_device_state > 0 ? v9x_gl_interface : 0;
}

const V9X_R3D_ABI_DESCRIBE *v9x_gl_device_description(void)
{
    return v9x_gl_device_state > 0 ? &v9x_gl_description : 0;
}

v9x_u32 v9x_gl_device_format(void)
{
    if (v9x_gl_device_state <= 0) {
        return 0ul;
    }
    if ((v9x_gl_description.target_formats &
         (1ul << V9X_R3D_ABI_FORMAT_RGB565)) != 0ul) {
        return V9X_R3D_ABI_FORMAT_RGB565;
    }
    if ((v9x_gl_description.target_formats &
         (1ul << V9X_R3D_ABI_FORMAT_XRGB1555)) != 0ul) {
        return V9X_R3D_ABI_FORMAT_XRGB1555;
    }
    return 0ul;
}

static void v9x_gl_drawable_release_surfaces(struct v9x_gl_drawable *drawable)
{
    if (drawable->front != 0) {
        IDirectDrawSurface_Release(drawable->front);
        drawable->front = 0;
    }
    if (drawable->depth != 0) {
        IDirectDrawSurface_Release(drawable->depth);
        drawable->depth = 0;
    }
    if (drawable->back != 0) {
        IDirectDrawSurface_Release(drawable->back);
        drawable->back = 0;
    }
    drawable->width = 0ul;
    drawable->height = 0ul;
}

int v9x_gl_device_redescribe(void)
{
    unsigned int index;

    if (v9x_gl_device_state <= 0) {
        return 0;
    }
    for (index = 0u; index < V9X_GL_DRAWABLES_MAX; ++index) {
        v9x_gl_drawable_release_surfaces(&v9x_gl_drawables[index]);
    }
    if (v9x_gl_primary != 0 &&
        IDirectDrawSurface_IsLost(v9x_gl_primary) == DDERR_SURFACELOST) {
        IDirectDrawSurface_Restore(v9x_gl_primary);
    }
    return v9x_gl_describe_now();
}

static LPDIRECTDRAWSURFACE v9x_gl_make_surface(DWORD caps, DWORD width,
                                               DWORD height, DWORD depth_bits)
{
    DDSURFACEDESC desc;
    LPDIRECTDRAWSURFACE surface = 0;
    HRESULT hr;

    v9x_gl_surface_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = width;
    desc.dwHeight = height;
    desc.ddsCaps.dwCaps = caps;
    if (depth_bits != 0ul) {
        desc.dwFlags |= DDSD_ZBUFFERBITDEPTH;
        desc.dwZBufferBitDepth = depth_bits;
    }
    hr = IDirectDraw_CreateSurface(v9x_gl_ddraw, &desc, &surface, 0);
    if (hr != DD_OK) {
        v9x_gl_log3("surface caps=%08lX %lux%lu failed", caps, width, height);
        v9x_gl_log3("  hr=%08lX", (DWORD)hr, 0ul, 0ul);
        return 0;
    }
    return surface;
}

V9X_GL_DRAWABLE *v9x_gl_drawable_find(void *window)
{
    unsigned int index;

    for (index = 0u; index < V9X_GL_DRAWABLES_MAX; ++index) {
        if (v9x_gl_drawables[index].window == (HWND)window) {
            return &v9x_gl_drawables[index];
        }
    }
    return 0;
}

V9X_GL_DRAWABLE *v9x_gl_drawable_bind(void *window, int *resized)
{
    struct v9x_gl_drawable *drawable;
    RECT client;
    DWORD width;
    DWORD height;
    unsigned int index;

    *resized = 0;
    if (v9x_gl_device_state <= 0 || window == 0 ||
        !GetClientRect((HWND)window, &client)) {
        return 0;
    }
    width = client.right > client.left ? (DWORD)(client.right - client.left)
                                       : 1ul;
    height = client.bottom > client.top ? (DWORD)(client.bottom - client.top)
                                        : 1ul;
    drawable = v9x_gl_drawable_find(window);
    if (drawable == 0) {
        for (index = 0u; index < V9X_GL_DRAWABLES_MAX; ++index) {
            if (v9x_gl_drawables[index].window == 0) {
                drawable = &v9x_gl_drawables[index];
                break;
            }
        }
        if (drawable == 0) {
            v9x_gl_log("drawable: table full");
            return 0;
        }
        v9x_gl_surface_zero(drawable, sizeof(*drawable));
        drawable->window = (HWND)window;
        if (IDirectDraw_CreateClipper(v9x_gl_ddraw, 0ul, &drawable->clipper,
                                      0) != DD_OK ||
            IDirectDrawClipper_SetHWnd(drawable->clipper, 0ul,
                                       (HWND)window) != DD_OK) {
            v9x_gl_log("drawable: clipper failed");
            drawable->window = 0;
            return 0;
        }
    }
    if (drawable->back != 0 && drawable->width == width &&
        drawable->height == height &&
        IDirectDrawSurface_IsLost(drawable->back) != DDERR_SURFACELOST) {
        return drawable;
    }
    v9x_gl_drawable_release_surfaces(drawable);
    drawable->back = v9x_gl_make_surface(DDSCAPS_OFFSCREENPLAIN |
                                         DDSCAPS_VIDEOMEMORY |
                                         DDSCAPS_3DDEVICE,
                                         width, height, 0ul);
    drawable->depth = v9x_gl_make_surface(DDSCAPS_ZBUFFER |
                                          DDSCAPS_VIDEOMEMORY,
                                          width, height, 16ul);
    if (drawable->back == 0 || drawable->depth == 0) {
        v9x_gl_drawable_release_surfaces(drawable);
        return 0;
    }
    drawable->width = width;
    drawable->height = height;
    *resized = 1;
    v9x_gl_log3("drawable window=%08lX %lux%lu", (DWORD)window, width, height);
    return drawable;
}

void v9x_gl_drawable_size(const V9X_GL_DRAWABLE *drawable,
                          v9x_u32 *width, v9x_u32 *height)
{
    *width = drawable->width;
    *height = drawable->height;
}

void *v9x_gl_drawable_back(const V9X_GL_DRAWABLE *drawable)
{
    return drawable->back;
}

void *v9x_gl_drawable_depth(const V9X_GL_DRAWABLE *drawable)
{
    return drawable->depth;
}

/* The DirectDraw pixel format of a V9X_R3D_ABI_FORMAT_* texture layout. */
static int v9x_gl_hwtex_pixel_format(v9x_u32 format, DDPIXELFORMAT *out)
{
    v9x_gl_surface_zero(out, sizeof(*out));
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

void *v9x_gl_hwtex_create(v9x_u32 edge, v9x_u32 levels, v9x_u32 format)
{
    DDSURFACEDESC desc;
    LPDIRECTDRAWSURFACE surface = 0;
    HRESULT hr;

    if (v9x_gl_ddraw == 0 || levels == 0ul) {
        return 0;
    }
    v9x_gl_surface_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = edge;
    desc.dwHeight = edge;
    if (!v9x_gl_hwtex_pixel_format(format, &desc.ddpfPixelFormat)) {
        return 0;
    }
    /* Video memory, so the HAL's placement puts it where the sampler
     * reads (a chain as one mip tree); a system-memory texture would be a
     * surface Gen3's bind refuses. */
    desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY;
    if (levels > 1ul) {
        desc.dwFlags |= DDSD_MIPMAPCOUNT;
        desc.dwMipMapCount = levels;
        desc.ddsCaps.dwCaps |= DDSCAPS_MIPMAP | DDSCAPS_COMPLEX;
    }
    hr = IDirectDraw_CreateSurface(v9x_gl_ddraw, &desc, &surface, 0);
    if (hr != DD_OK) {
        v9x_gl_log3("hwtex create edge=%lu levels=%lu hr=%08lX", edge, levels,
                    (DWORD)hr);
        return 0;
    }
    return surface;
}

/* One level's rows into a locked surface. */
static int v9x_gl_hwtex_fill(LPDIRECTDRAWSURFACE surface,
                             const V9X_R3D_ABI_LEVEL *level)
{
    DDSURFACEDESC desc;
    const BYTE *source;
    BYTE *target;
    DWORD row;
    DWORD i;
    HRESULT hr;

    v9x_gl_surface_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = IDirectDrawSurface_Lock(surface, 0, &desc,
                                 DDLOCK_WAIT | DDLOCK_WRITEONLY, 0);
    if (hr == DDERR_SURFACELOST) {
        /* Video memory given back after another application's mode or
         * exclusive use: memory again, and this upload refills it. */
        IDirectDrawSurface_Restore(surface);
        hr = IDirectDrawSurface_Lock(surface, 0, &desc,
                                     DDLOCK_WAIT | DDLOCK_WRITEONLY, 0);
    }
    if (hr != DD_OK) {
        v9x_gl_log3("hwtex lock hr=%08lX", (DWORD)hr, 0ul, 0ul);
        return 0;
    }
    if (desc.dwWidth != level->width || desc.dwHeight != level->height) {
        IDirectDrawSurface_Unlock(surface, 0);
        v9x_gl_log3("hwtex level %lux%lu expected width %lu", desc.dwWidth,
                    desc.dwHeight, level->width);
        return 0;
    }
    source = (const BYTE *)level->pixels;
    target = (BYTE *)desc.lpSurface;
    for (row = 0ul; row < level->height; ++row) {
        for (i = 0ul; i < level->width * 2ul; ++i) {
            target[i] = source[i];
        }
        source += level->pitch;
        target += desc.lPitch;
    }
    IDirectDrawSurface_Unlock(surface, 0);
    return 1;
}

int v9x_gl_hwtex_upload(void *surface, v9x_u32 levels,
                        const V9X_R3D_ABI_LEVEL *source)
{
    LPDIRECTDRAWSURFACE level = (LPDIRECTDRAWSURFACE)surface;
    LPDIRECTDRAWSURFACE next;
    DDSCAPS caps;
    v9x_u32 index;
    int ok = 1;

    /* The top level, then each attached level in turn. GetAttachedSurface
     * adds a reference to what it returns; every one but the top is
     * released once filled. */
    for (index = 0ul; index < levels && ok; ++index) {
        ok = v9x_gl_hwtex_fill(level, &source[index]);
        next = 0;
        if (ok && index + 1ul < levels) {
            caps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_MIPMAP;
            if (IDirectDrawSurface_GetAttachedSurface(level, &caps, &next) !=
                    DD_OK) {
                next = 0;
                ok = 0;
            }
        }
        if (index != 0ul) {
            IDirectDrawSurface_Release(level);
        }
        level = next;
    }
    return ok;
}

void v9x_gl_hwtex_release(void *surface)
{
    if (surface != 0) {
        IDirectDrawSurface_Release((LPDIRECTDRAWSURFACE)surface);
    }
}

int v9x_gl_drawable_lock(V9X_GL_DRAWABLE *drawable, int front,
                         const void **pixels, v9x_u32 *pitch)
{
    DDSURFACEDESC description;
    LPDIRECTDRAWSURFACE surface;
    HRESULT hr;

    if (drawable == 0) {
        return 0;
    }
    surface = front ? drawable->front : drawable->back;
    if (surface == 0) {
        return 0;
    }
    v9x_gl_surface_zero(&description, sizeof(description));
    description.dwSize = sizeof(description);

    /* A lost back buffer has no contents to read: Restore gives it memory
     * again but not its pixels, so the read fails rather than returning
     * whatever the restored memory holds. */
    hr = IDirectDrawSurface_Lock(surface, 0, &description,
                                 DDLOCK_WAIT | DDLOCK_READONLY, 0);
    if (hr != DD_OK) {
        v9x_gl_log3("read lock hr=%08lX", (DWORD)hr, 0ul, 0ul);
        return 0;
    }
    *pixels = description.lpSurface;
    *pitch = (v9x_u32)description.lPitch;
    return 1;
}

void v9x_gl_drawable_unlock(V9X_GL_DRAWABLE *drawable, int front)
{
    IDirectDrawSurface_Unlock(front ? drawable->front : drawable->back, 0);
}

/* The window's client rectangle in screen coordinates, as the primary
 * surface addresses it. */
static void v9x_gl_drawable_screen_rect(const struct v9x_gl_drawable *drawable,
                                        RECT *rect)
{
    POINT origin;

    origin.x = 0;
    origin.y = 0;
    ClientToScreen(drawable->window, &origin);
    rect->left = origin.x;
    rect->top = origin.y;
    rect->right = origin.x + (LONG)drawable->width;
    rect->bottom = origin.y + (LONG)drawable->height;
}

void *v9x_gl_drawable_front(V9X_GL_DRAWABLE *drawable)
{
    RECT screen;
    HRESULT hr;

    if (drawable == 0 || drawable->back == 0) {
        return 0;
    }
    if (drawable->front != 0) {
        return drawable->front;
    }
    drawable->front = v9x_gl_make_surface(DDSCAPS_OFFSCREENPLAIN |
                                          DDSCAPS_VIDEOMEMORY |
                                          DDSCAPS_3DDEVICE,
                                          drawable->width, drawable->height,
                                          0ul);
    if (drawable->front == 0) {
        v9x_gl_log("front: surface refused");
        return 0;
    }
    /* What the window shows now is what GL's front buffer holds (pixels
     * of other windows over it are ones this context does not own, whose
     * values GL leaves undefined). */
    v9x_gl_drawable_screen_rect(drawable, &screen);
    hr = IDirectDrawSurface_Blt(drawable->front, 0, v9x_gl_primary, &screen,
                                DDBLT_WAIT, 0);
    if (hr != DD_OK) {
        v9x_gl_log3("front: copy from screen hr=%08lX", (DWORD)hr, 0ul, 0ul);
    }
    return drawable->front;
}

void *v9x_gl_drawable_front_existing(const V9X_GL_DRAWABLE *drawable)
{
    return drawable != 0 ? drawable->front : 0;
}

int v9x_gl_drawable_show_front(V9X_GL_DRAWABLE *drawable)
{
    RECT target;
    HRESULT hr;

    if (drawable == 0 || drawable->front == 0 || v9x_gl_primary == 0) {
        return 0;
    }
    v9x_gl_drawable_screen_rect(drawable, &target);
    IDirectDrawSurface_SetClipper(v9x_gl_primary, drawable->clipper);
    hr = IDirectDrawSurface_Blt(v9x_gl_primary, &target, drawable->front, 0,
                                DDBLT_WAIT, 0);
    if (hr != DD_OK) {
        v9x_gl_log3("show front hr=%08lX", (DWORD)hr, 0ul, 0ul);
        return 0;
    }
    return 1;
}

int v9x_gl_drawable_present(V9X_GL_DRAWABLE *drawable)
{
    RECT target;
    POINT origin;
    HRESULT hr;

    if (drawable == 0 || drawable->back == 0 || v9x_gl_primary == 0) {
        return 0;
    }
    origin.x = 0;
    origin.y = 0;
    ClientToScreen(drawable->window, &origin);
    target.left = origin.x;
    target.top = origin.y;
    target.right = origin.x + (LONG)drawable->width;
    target.bottom = origin.y + (LONG)drawable->height;
    /* One primary, one clipper per window: attach this window's before the
     * Blt so DirectDraw cuts to its visible region. */
    IDirectDrawSurface_SetClipper(v9x_gl_primary, drawable->clipper);
    hr = IDirectDrawSurface_Blt(v9x_gl_primary, &target, drawable->back, 0,
                                DDBLT_WAIT, 0);
    if (hr == DDERR_SURFACELOST) {
        IDirectDrawSurface_Restore(v9x_gl_primary);
        IDirectDrawSurface_Restore(drawable->back);
        hr = IDirectDrawSurface_Blt(v9x_gl_primary, &target, drawable->back, 0,
                                    DDBLT_WAIT, 0);
    }
    if (hr != DD_OK) {
        v9x_gl_log3("present hr=%08lX", (DWORD)hr, 0ul, 0ul);
        return 0;
    }
    /* SwapBuffers makes the back buffer's image the front's (4.2.1). The
     * copy exists only once something has used the front buffer. */
    if (drawable->front != 0) {
        IDirectDrawSurface_Blt(drawable->front, 0, drawable->back, 0,
                               DDBLT_WAIT, 0);
    }
    return 1;
}
