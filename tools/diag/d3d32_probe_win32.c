#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

/*
 * V9XD32.EXE: Direct3D's HAL device at a 32 bpp display mode, every pixel it
 * draws read back (docs\plans\32bpp-acceleration-and-3d.md, Phase 2).
 *
 * V9XDDP sets 640x480x16 and reads 16-bit pixels throughout, so it cannot
 * answer whether a driver renders at 32 bpp. This sets 640x480x32 (or
 * 640x480x16 with /bpp16, the control: the same checks against a path
 * already measured) and, on the flip chain's back buffer with a 16-bit Z
 * buffer:
 *
 *   T1 a flat triangle: every pixel well inside is the vertex colour, every
 *      pixel well outside is the clear colour;
 *   T2 a Gouraud triangle: each corner's pixel dominated by its own channel;
 *   T3 depth: a quad at z 0.5, a triangle behind it that must not draw and
 *      one in front that must;
 *   T4 a flat triangle into a 64x64 offscreen render target of its own;
 *   T5 depth precision: a quad a tenth of a 16-bit step in front of
 *      another, which a 16-bit buffer cannot tell apart and a 24-bit one
 *      can;
 *   T6 an 8x8 ARGB1555 texture of four full-scale colours, DECAL and
 *      nearest, each quadrant's centre within 8 of its texel;
 *   T7 half-alpha red over opaque green, SRCALPHA / INVSRCALPHA, within 8
 *      of 0x807F00.
 *
 * /z24 makes the Z buffer 32 bits (24 of depth on Gen3) and clears it at
 * that full scale; T5 then expects the nearer quad, and without it the
 * farther one.
 *
 * At 32 bpp the comparisons are exact for flat colour (eight bits a channel
 * and nothing to round); at 16 bpp each channel is compared after expansion
 * to eight bits, within 8. The X byte of a 32-bit pixel must be zero.
 *
 * Each stage is appended to C:\V9XDIAG\V9XD32.TXT and flushed before it runs,
 * as V9XDDF does, so the last line after a hang names the step.
 */
#define D32_TEXT_PATH "C:\\V9XDIAG\\V9XD32.TXT"
#define D32_WIDTH 640ul
#define D32_HEIGHT 480ul

#define DDSD_CAPS 0x00000001ul
#define DDSD_HEIGHT 0x00000002ul
#define DDSD_WIDTH 0x00000004ul
#define DDSD_BACKBUFFERCOUNT 0x00000020ul
#define DDSD_ZBUFFERBITDEPTH 0x00000040ul
#define DDSCAPS_BACKBUFFER 0x00000004ul
#define DDSCAPS_COMPLEX 0x00000008ul
#define DDSCAPS_FLIP 0x00000010ul
#define DDSCAPS_OFFSCREENPLAIN 0x00000040ul
#define DDSCAPS_PRIMARYSURFACE 0x00000200ul
#define DDSCAPS_3DDEVICE 0x00002000ul
#define DDSCAPS_VIDEOMEMORY 0x00004000ul
#define DDSCAPS_ZBUFFER 0x00020000ul
#define DDSCL_FULLSCREEN 0x00000001ul
#define DDSCL_EXCLUSIVE 0x00000010ul
#define DDBLT_COLORFILL 0x00000400ul
#define DDBLT_WAIT 0x01000000ul
#define DDBLT_DEPTHFILL 0x02000000ul
#define DDLOCK_WAIT 0x00000001ul
#define DDBD_16 0x00000400ul
#define DDBD_32 0x00000100ul

#define D3DPT_TRIANGLELIST 4ul
#define D3DVT_TLVERTEX 3ul
#define D3DRENDERSTATE_ZENABLE 7ul
#define D3DRENDERSTATE_SHADEMODE 9ul
#define D3DRENDERSTATE_ZWRITEENABLE 14ul
#define D3DRENDERSTATE_CULLMODE 22ul
#define D3DRENDERSTATE_ZFUNC 23ul
#define D3DRENDERSTATE_DITHERENABLE 26ul
#define D3DRENDERSTATE_TEXTUREHANDLE 1ul
#define D3DRENDERSTATE_TEXTUREMAG 17ul
#define D3DRENDERSTATE_TEXTUREMIN 18ul
#define D3DRENDERSTATE_SRCBLEND 19ul
#define D3DRENDERSTATE_DESTBLEND 20ul
#define D3DRENDERSTATE_TEXTUREMAPBLEND 21ul
#define D3DRENDERSTATE_ALPHABLENDENABLE 27ul
#define D3DFILTER_NEAREST 1ul
#define D3DTBLEND_DECAL 1ul
#define D3DBLEND_SRCALPHA 5ul
#define D3DBLEND_INVSRCALPHA 6ul
#define DDSD_PIXELFORMAT 0x00001000ul
#define DDSCAPS_TEXTURE 0x00001000ul
#define DDPF_RGB 0x00000040ul
#define DDPF_ALPHAPIXELS 0x00000001ul
#define D3DTEX2_GET_HANDLE 3
#define D3DSHADE_GOURAUD 2ul
#define D3DCULL_NONE 1ul
#define D3DCMP_LESS 2ul

typedef struct d32_surface_desc {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    LONG lPitch;
    DWORD dwBackBufferCount;
    DWORD dwMipMapCount;        /* dwZBufferBitDepth in a Z request */
    DWORD dwAlphaBitDepth;
    DWORD dwReserved;
    void *lpSurface;
    DWORD colorkeys[8];
    DWORD ddpfPixelFormat[8];   /* size, flags, fourcc, bits, r, g, b, a */
    DWORD ddsCaps;
} D32_SURFACE_DESC;

typedef char d32_surface_desc_is_108[
    sizeof(D32_SURFACE_DESC) == 108 ? 1 : -1];

/* DDBLTFX: dwFillColor (and dwFillDepth, its union twin) is the 21st DWORD. */
typedef struct d32_bltfx {
    DWORD dwSize;
    DWORD before_fill[19];
    DWORD dwFillColor;
    DWORD colorkeys[4];
} D32_BLTFX;

typedef char d32_bltfx_is_100[sizeof(D32_BLTFX) == 100 ? 1 : -1];

typedef struct d32_viewport2 {
    DWORD dwSize;
    DWORD dwX;
    DWORD dwY;
    DWORD dwWidth;
    DWORD dwHeight;
    float dvClipX;
    float dvClipY;
    float dvClipWidth;
    float dvClipHeight;
    float dvMinZ;
    float dvMaxZ;
} D32_VIEWPORT2;

typedef struct d32_tlvertex {
    float sx;
    float sy;
    float sz;
    float rhw;
    DWORD color;
    DWORD specular;
    float tu;
    float tv;
} D32_TLVERTEX;

typedef struct d32_object { void **vtbl; } D32_OBJECT;

/* vtable slots, in ddraw.h and d3d.h order. */
#define COM_QUERY_INTERFACE 0
#define COM_RELEASE 2
#define DD_CREATE_SURFACE 6
#define DD_GET_DISPLAY_MODE 12
#define DD_RESTORE_DISPLAY_MODE 19
#define DD_SET_COOPERATIVE_LEVEL 20
#define DD_SET_DISPLAY_MODE 21
#define DDS_ADD_ATTACHED_SURFACE 3
#define DDS_BLT 5
#define DDS_GET_ATTACHED_SURFACE 12
#define DDS_LOCK 25
#define DDS_UNLOCK 32
#define D3D2_ENUM_DEVICES 3
#define D3D2_CREATE_VIEWPORT 6
#define D3D2_CREATE_DEVICE 8
#define D3DDEV2_ADD_VIEWPORT 6
#define D3DDEV2_BEGIN_SCENE 10
#define D3DDEV2_END_SCENE 11
#define D3DDEV2_SET_CURRENT_VIEWPORT 13
#define D3DDEV2_SET_RENDER_STATE 23
#define D3DDEV2_DRAW_PRIMITIVE 29
#define D3DVP2_SET_VIEWPORT2 17

/* D3DDEVICEDESC as DWORDs: the render and Z depth masks are the 40th and
 * 41st, after the two 14-DWORD primitive caps. */
#define D3DDESC_RENDER_DEPTH 39
#define D3DDESC_Z_DEPTH 40

typedef HRESULT (WINAPI *d32_create_fn)(void *, D32_OBJECT **, void *);
typedef ULONG (WINAPI *d32_release_fn)(D32_OBJECT *);
typedef HRESULT (WINAPI *d32_query_fn)(D32_OBJECT *, const GUID *, void **);
typedef HRESULT (WINAPI *d32_create_surface_fn)(D32_OBJECT *,
    D32_SURFACE_DESC *, D32_OBJECT **, void *);
typedef HRESULT (WINAPI *d32_display_mode_fn)(D32_OBJECT *,
                                              D32_SURFACE_DESC *);
typedef HRESULT (WINAPI *d32_restore_fn)(D32_OBJECT *);
typedef HRESULT (WINAPI *d32_coop_fn)(D32_OBJECT *, HWND, DWORD);
typedef HRESULT (WINAPI *d32_set_mode_fn)(D32_OBJECT *, DWORD, DWORD,
                                          DWORD);
typedef HRESULT (WINAPI *d32_attach_fn)(D32_OBJECT *, D32_OBJECT *);
typedef HRESULT (WINAPI *d32_blt_fn)(D32_OBJECT *, RECT *, D32_OBJECT *,
    RECT *, DWORD, D32_BLTFX *);
typedef HRESULT (WINAPI *d32_get_attached_fn)(D32_OBJECT *, DWORD *,
                                              D32_OBJECT **);
typedef HRESULT (WINAPI *d32_lock_fn)(D32_OBJECT *, RECT *,
    D32_SURFACE_DESC *, DWORD, HANDLE);
typedef HRESULT (WINAPI *d32_unlock_fn)(D32_OBJECT *, void *);
typedef HRESULT (WINAPI *d32_enum_callback)(GUID *, char *, char *,
                                            DWORD *, DWORD *, void *);
typedef HRESULT (WINAPI *d32_enum_devices_fn)(D32_OBJECT *,
                                              d32_enum_callback, void *);
typedef HRESULT (WINAPI *d32_create_viewport_fn)(D32_OBJECT *,
                                                 D32_OBJECT **, void *);
typedef HRESULT (WINAPI *d32_create_device_fn)(D32_OBJECT *, const GUID *,
                                               D32_OBJECT *, D32_OBJECT **);
typedef HRESULT (WINAPI *d32_device_viewport_fn)(D32_OBJECT *,
                                                 D32_OBJECT *);
typedef HRESULT (WINAPI *d32_scene_fn)(D32_OBJECT *);
typedef HRESULT (WINAPI *d32_render_state_fn)(D32_OBJECT *, DWORD, DWORD);
typedef HRESULT (WINAPI *d32_draw_fn)(D32_OBJECT *, DWORD, DWORD, void *,
                                      DWORD, DWORD);
typedef HRESULT (WINAPI *d32_set_viewport2_fn)(D32_OBJECT *,
                                               D32_VIEWPORT2 *);

static const GUID d32_iid_d3d2 = {
    0x6aae1ec1ul, 0x662a, 0x11d0,
    { 0x88, 0x9d, 0x00, 0xaa, 0x00, 0xbb, 0xb7, 0x6a }
};

static const GUID d32_iid_d3d_texture2 = {
    0x93281502ul, 0x8cf8, 0x11d0,
    { 0x89, 0xab, 0x00, 0xa0, 0xc9, 0x05, 0x41, 0x29 }
};

typedef HRESULT (WINAPI *d32_texture_handle_fn)(D32_OBJECT *, D32_OBJECT *,
                                                DWORD *);

static const GUID d32_iid_d3d_hal = {
    0x84e63de0ul, 0x46aa, 0x11cf,
    { 0x81, 0x6f, 0x00, 0x00, 0xc0, 0x20, 0x15, 0x6e }
};

static HANDLE d32_file = INVALID_HANDLE_VALUE;

/* The mode's layout, read from the primary's pixel format. */
static DWORD d32_bytes;
static DWORD d32_masks[3];
static DWORD d32_tolerance;

static void d32_hex(char *text, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    int index;
    text[0] = '0';
    text[1] = 'x';
    for (index = 0; index < 8; ++index) {
        text[2 + index] = digits[(value >> ((7 - index) * 4)) & 15u];
    }
    text[10] = '\0';
}

/* One line, written through and flushed before the caller goes on. */
static void d32_line(const char *key, const char *value)
{
    DWORD written;
    if (d32_file == INVALID_HANDLE_VALUE) {
        return;
    }
    WriteFile(d32_file, key, (DWORD)lstrlenA(key), &written, 0);
    WriteFile(d32_file, "=", 1ul, &written, 0);
    WriteFile(d32_file, value, (DWORD)lstrlenA(value), &written, 0);
    WriteFile(d32_file, "\r\n", 2ul, &written, 0);
    FlushFileBuffers(d32_file);
}

static void d32_hx(const char *key, DWORD value)
{
    char text[11];
    d32_hex(text, value);
    d32_line(key, text);
}

static int d32_has_switch(const char *option)
{
    const char *line = GetCommandLineA();
    unsigned offset;
    unsigned index;
    for (offset = 0u; line[offset] != '\0'; ++offset) {
        for (index = 0u; option[index] != '\0'; ++index) {
            char c = line[offset + index];
            if (c >= 'A' && c <= 'Z') {
                c = (char)(c + ('a' - 'A'));
            }
            if (c != option[index]) {
                break;
            }
        }
        if (option[index] == '\0') {
            return 1;
        }
    }
    return 0;
}

static void d32_zero(void *block, unsigned length)
{
    unsigned char *bytes = (unsigned char *)block;
    while (length-- != 0u) {
        *bytes++ = 0u;
    }
}

static int d32_guid_equal(const GUID *left, const GUID *right)
{
    const BYTE *a = (const BYTE *)left;
    const BYTE *b = (const BYTE *)right;
    unsigned index;
    for (index = 0u; index < sizeof(GUID); ++index) {
        if (a[index] != b[index]) {
            return 0;
        }
    }
    return 1;
}

static LRESULT CALLBACK d32_window_proc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcA(window, message, wparam, lparam);
}

typedef struct d32_enum_result {
    DWORD hal_found;
    DWORD render_depth;
    DWORD z_depth;
} D32_ENUM_RESULT;

static HRESULT WINAPI d32_enum_device(GUID *guid, char *description,
                                      char *name, DWORD *hardware,
                                      DWORD *software, void *context)
{
    D32_ENUM_RESULT *result = (D32_ENUM_RESULT *)context;

    (void)description;
    (void)name;
    (void)software;
    if (guid != 0 && hardware != 0 &&
        d32_guid_equal(guid, &d32_iid_d3d_hal)) {
        result->hal_found = 1ul;
        result->render_depth = hardware[D3DDESC_RENDER_DEPTH];
        result->z_depth = hardware[D3DDESC_Z_DEPTH];
    }
    return 1;   /* D3DENUMRET_OK */
}

/* A channel of a pixel, expanded to eight bits by its mask. */
static DWORD d32_channel(DWORD pixel, DWORD mask)
{
    DWORD shift = 0ul;
    DWORD width = 0ul;
    DWORD value;

    if (mask == 0ul) {
        return 0ul;
    }
    while ((mask & 1ul) == 0ul) {
        mask >>= 1;
        ++shift;
    }
    while ((mask & 1ul) != 0ul) {
        mask >>= 1;
        ++width;
    }
    value = (pixel >> shift) & ((1ul << width) - 1ul);
    if (width >= 8ul) {
        return value >> (width - 8ul);
    }
    /* Replicate the high bits into the low ones, so full scale is 255. */
    value <<= 8ul - width;
    return value | (value >> width);
}

static DWORD d32_difference(DWORD a, DWORD b)
{
    return a > b ? a - b : b - a;
}

/* Whether a pixel is the colour 0x00RRGGBB, within the mode's tolerance,
 * with the X byte zero at 32 bpp. */
static int d32_is_colour(DWORD pixel, DWORD rgb)
{
    if (d32_bytes == 4ul && (pixel & 0xff000000ul) != 0ul) {
        return 0;
    }
    return d32_difference(d32_channel(pixel, d32_masks[0]),
                          (rgb >> 16) & 0xfful) <= d32_tolerance &&
           d32_difference(d32_channel(pixel, d32_masks[1]),
                          (rgb >> 8) & 0xfful) <= d32_tolerance &&
           d32_difference(d32_channel(pixel, d32_masks[2]),
                          rgb & 0xfful) <= d32_tolerance;
}

static DWORD d32_pixel(const D32_SURFACE_DESC *desc, DWORD x, DWORD y)
{
    const BYTE *row = (const BYTE *)desc->lpSurface + y * (DWORD)desc->lPitch;
    if (d32_bytes == 4ul) {
        return ((const DWORD *)row)[x];
    }
    return (DWORD)((const WORD *)row)[x];
}

static void d32_finish(const char *result, UINT code)
{
    d32_line("Result", result);
    if (d32_file != INVALID_HANDLE_VALUE) {
        CloseHandle(d32_file);
    }
    ExitProcess(code);
}

static void d32_vertex(D32_TLVERTEX *vertex, float x, float y, float z,
                       DWORD colour)
{
    vertex->sx = x;
    vertex->sy = y;
    vertex->sz = z;
    vertex->rhw = 1.0f;
    vertex->color = colour;
    vertex->specular = 0ul;
    vertex->tu = 0.0f;
    vertex->tv = 0.0f;
}

static HRESULT d32_draw(D32_OBJECT *device, D32_TLVERTEX *vertices,
                        DWORD count)
{
    HRESULT hr;
    HRESULT draw_hr;

    hr = ((d32_scene_fn)device->vtbl[D3DDEV2_BEGIN_SCENE])(device);
    if (hr != 0) {
        return hr;
    }
    draw_hr = ((d32_draw_fn)device->vtbl[D3DDEV2_DRAW_PRIMITIVE])(
        device, D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, vertices, count, 0ul);
    hr = ((d32_scene_fn)device->vtbl[D3DDEV2_END_SCENE])(device);
    return draw_hr != 0 ? draw_hr : hr;
}

static void d32_state(D32_OBJECT *device, DWORD state, DWORD value)
{
    ((d32_render_state_fn)device->vtbl[D3DDEV2_SET_RENDER_STATE])(
        device, state, value);
}

static HRESULT d32_fill(D32_OBJECT *surface, DWORD flags, DWORD value)
{
    D32_BLTFX fx;

    d32_zero(&fx, sizeof(fx));
    fx.dwSize = sizeof(fx);
    fx.dwFillColor = value;
    return ((d32_blt_fn)surface->vtbl[DDS_BLT])(surface, 0, 0, 0,
                                                flags | DDBLT_WAIT, &fx);
}

/* The device, a viewport over width x height, and the states every test
 * starts from: no culling, no dithering, Gouraud, depth off. */
static D32_OBJECT *d32_device(D32_OBJECT *d3d, D32_OBJECT *target,
                              DWORD width, DWORD height, const char *prefix)
{
    D32_OBJECT *device = 0;
    D32_OBJECT *viewport = 0;
    D32_VIEWPORT2 view;
    HRESULT hr;
    char key[32];

    hr = ((d32_create_device_fn)d3d->vtbl[D3D2_CREATE_DEVICE])(
        d3d, &d32_iid_d3d_hal, target, &device);
    lstrcpyA(key, prefix);
    lstrcatA(key, "CreateDeviceHr");
    d32_hx(key, (DWORD)hr);
    if (hr != 0 || device == 0) {
        return 0;
    }
    hr = ((d32_create_viewport_fn)d3d->vtbl[D3D2_CREATE_VIEWPORT])(
        d3d, &viewport, 0);
    if (hr == 0 && viewport != 0) {
        hr = ((d32_device_viewport_fn)device->vtbl[D3DDEV2_ADD_VIEWPORT])(
            device, viewport);
    }
    if (hr == 0) {
        d32_zero(&view, sizeof(view));
        view.dwSize = sizeof(view);
        view.dwWidth = width;
        view.dwHeight = height;
        view.dvClipX = -1.0f;
        view.dvClipY = 1.0f;
        view.dvClipWidth = 2.0f;
        view.dvClipHeight = 2.0f;
        view.dvMinZ = 0.0f;
        view.dvMaxZ = 1.0f;
        hr = ((d32_set_viewport2_fn)viewport->vtbl[D3DVP2_SET_VIEWPORT2])(
            viewport, &view);
    }
    if (hr == 0) {
        hr = ((d32_device_viewport_fn)
                  device->vtbl[D3DDEV2_SET_CURRENT_VIEWPORT])(device,
                                                              viewport);
    }
    lstrcpyA(key, prefix);
    lstrcatA(key, "ViewportHr");
    d32_hx(key, (DWORD)hr);
    if (hr != 0) {
        ((d32_release_fn)device->vtbl[COM_RELEASE])(device);
        return 0;
    }
    d32_state(device, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    d32_state(device, D3DRENDERSTATE_DITHERENABLE, 0ul);
    d32_state(device, D3DRENDERSTATE_SHADEMODE, D3DSHADE_GOURAUD);
    d32_state(device, D3DRENDERSTATE_ZENABLE, 0ul);
    return device;
}

/*
 * A flat triangle at (left, top), legs of 48 pixels, over a target cleared
 * to zero. Pixels whose centre is at least one pixel inside every edge must
 * be the colour, and those at least one pixel outside must be zero; the
 * pixels on the edges belong to the coverage rule and are not judged here.
 */
static DWORD d32_flat_mismatches(const D32_SURFACE_DESC *desc, DWORD left,
                                 DWORD top, DWORD rgb, DWORD *judged)
{
    DWORD mismatches = 0ul;
    DWORD x;
    DWORD y;

    *judged = 0ul;
    for (y = 0ul; y < 64ul; ++y) {
        for (x = 0ul; x < 64ul; ++x) {
            DWORD pixel = d32_pixel(desc, left + x, top + y);
            /* Centre (x + 0.5, y + 0.5) against x >= 0, y >= 0 and
             * x + y <= 48, in triangle-local pixels. */
            int inside = x >= 1ul && y >= 1ul && x + y + 1ul <= 46ul;
            int outside = x + y + 1ul >= 50ul;

            if (inside) {
                ++*judged;
                if (!d32_is_colour(pixel, rgb)) {
                    ++mismatches;
                }
            } else if (outside) {
                ++*judged;
                if (pixel != 0ul) {
                    ++mismatches;
                }
            }
        }
    }
    return mismatches;
}

/* Within `tolerance` of 0x00RRGGBB on every channel, X byte zero at
 * 32 bpp: for a sampled or blended colour, whose expansion and rounding
 * are the engine's. */
static int d32_near(DWORD pixel, DWORD rgb, DWORD tolerance)
{
    DWORD saved = d32_tolerance;
    int result;

    d32_tolerance = tolerance;
    result = d32_is_colour(pixel, rgb);
    d32_tolerance = saved;
    return result;
}

/*
 * The T6 texture: 8x8 ARGB1555, every texel opaque, in four 4x4 quadrants
 * red, green, blue and white - each channel at full scale or zero, so the
 * expansion to eight bits cannot be mistaken for a wrong texel. ARGB1555
 * because both the Gen3 and the software engine sample it. Returns the
 * handle, or zero.
 */
static DWORD d32_texture(D32_OBJECT *ddraw, D32_OBJECT *device,
                         D32_OBJECT **surface_out)
{
    static const WORD quadrant[4] = { 0xfc00u, 0x83e0u, 0x801fu, 0xffffu };
    D32_SURFACE_DESC desc;
    D32_OBJECT *surface = 0;
    D32_OBJECT *texture = 0;
    DWORD handle = 0ul;
    DWORD x;
    DWORD y;
    HRESULT hr;

    *surface_out = 0;
    d32_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = 8ul;
    desc.dwHeight = 8ul;
    desc.ddpfPixelFormat[0] = 32ul;
    desc.ddpfPixelFormat[1] = DDPF_RGB | DDPF_ALPHAPIXELS;
    desc.ddpfPixelFormat[3] = 16ul;
    desc.ddpfPixelFormat[4] = 0x00007c00ul;
    desc.ddpfPixelFormat[5] = 0x000003e0ul;
    desc.ddpfPixelFormat[6] = 0x0000001ful;
    desc.ddpfPixelFormat[7] = 0x00008000ul;
    desc.ddsCaps = DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY;
    hr = ((d32_create_surface_fn)ddraw->vtbl[DD_CREATE_SURFACE])(
        ddraw, &desc, &surface, 0);
    d32_hx("T6CreateHr", (DWORD)hr);
    if (hr != 0 || surface == 0) {
        return 0ul;
    }
    *surface_out = surface;
    d32_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = ((d32_lock_fn)surface->vtbl[DDS_LOCK])(surface, 0, &desc,
                                                DDLOCK_WAIT, 0);
    d32_hx("T6LockHr", (DWORD)hr);
    if (hr != 0 || desc.lpSurface == 0) {
        return 0ul;
    }
    for (y = 0ul; y < 8ul; ++y) {
        WORD *row = (WORD *)((BYTE *)desc.lpSurface +
                             y * (DWORD)desc.lPitch);
        for (x = 0ul; x < 8ul; ++x) {
            row[x] = quadrant[(y >= 4ul ? 2u : 0u) + (x >= 4ul ? 1u : 0u)];
        }
    }
    ((d32_unlock_fn)surface->vtbl[DDS_UNLOCK])(surface, desc.lpSurface);
    hr = ((d32_query_fn)surface->vtbl[COM_QUERY_INTERFACE])(
        surface, &d32_iid_d3d_texture2, (void **)&texture);
    if (hr == 0 && texture != 0) {
        hr = ((d32_texture_handle_fn)texture->vtbl[D3DTEX2_GET_HANDLE])(
            texture, device, &handle);
        ((d32_release_fn)texture->vtbl[COM_RELEASE])(texture);
    }
    d32_hx("T6HandleHr", (DWORD)hr);
    d32_hx("T6Handle", handle);
    return hr == 0 ? handle : 0ul;
}

static int d32_dominant(DWORD pixel, int channel)
{
    DWORD values[3];
    int other;

    if (d32_bytes == 4ul && (pixel & 0xff000000ul) != 0ul) {
        return 0;
    }
    values[0] = d32_channel(pixel, d32_masks[0]);
    values[1] = d32_channel(pixel, d32_masks[1]);
    values[2] = d32_channel(pixel, d32_masks[2]);
    if (values[channel] < 200ul) {
        return 0;
    }
    for (other = 0; other < 3; ++other) {
        if (other != channel && values[other] > 60ul) {
            return 0;
        }
    }
    return 1;
}

void WINAPI V9xD3d32ProbeEntry(void)
{
    HMODULE ddraw_module;
    d32_create_fn create;
    D32_OBJECT *ddraw = 0;
    D32_OBJECT *primary = 0;
    D32_OBJECT *back = 0;
    D32_OBJECT *zbuffer = 0;
    D32_OBJECT *offscreen = 0;
    D32_OBJECT *d3d = 0;
    D32_OBJECT *device = 0;
    D32_OBJECT *device2 = 0;
    D32_OBJECT *texture_surface = 0;
    DWORD handle = 0ul;
    D32_SURFACE_DESC desc;
    D32_ENUM_RESULT enumerated;
    D32_TLVERTEX v[6];
    WNDCLASSA window_class;
    HWND window;
    HRESULT hr;
    DWORD bpp;
    DWORD zbits;
    DWORD caps;
    DWORD judged;
    DWORD failures = 0ul;
    DWORD mismatches;

    CreateDirectoryA("C:\\V9XDIAG", 0);
    d32_file = CreateFileA(D32_TEXT_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                           CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, 0);
    d32_line("[V9xD3d32]Build", V9X_BUILD_ID);
    bpp = d32_has_switch("/bpp16") ? 16ul : 32ul;
    zbits = d32_has_switch("/z24") ? 32ul : 16ul;
    d32_hx("RequestBpp", bpp);
    d32_hx("RequestZBits", zbits);

    d32_line("Stage", "1-directdrawcreate");
    ddraw_module = LoadLibraryA("DDRAW.DLL");
    create = ddraw_module != 0
        ? (d32_create_fn)GetProcAddress(ddraw_module, "DirectDrawCreate") : 0;
    if (create == 0) {
        d32_finish("NO-DDRAW", 2u);
    }
    d32_zero(&window_class, sizeof(window_class));
    window_class.lpfnWndProc = d32_window_proc;
    window_class.hInstance = GetModuleHandleA(0);
    window_class.lpszClassName = "Velocity9xD3d32ProbeWindow";
    RegisterClassA(&window_class);
    window = CreateWindowExA(0ul, window_class.lpszClassName,
                             "Velocity9x 32 bpp Direct3D probe", WS_POPUP,
                             0, 0, 64, 64, 0, 0, window_class.hInstance, 0);
    if (window == 0) {
        d32_finish("NO-WINDOW", 2u);
    }
    ShowWindow(window, SW_SHOWNORMAL);
    SetForegroundWindow(window);
    hr = create(0, &ddraw, 0);
    d32_hx("DirectDrawCreateHr", (DWORD)hr);
    if (hr != 0 || ddraw == 0) {
        d32_finish("REVIEW", 1u);
    }

    d32_line("Stage", "2-exclusive-setmode");
    hr = ((d32_coop_fn)ddraw->vtbl[DD_SET_COOPERATIVE_LEVEL])(
        ddraw, window, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
    d32_hx("CooperativeHr", (DWORD)hr);
    hr = ((d32_set_mode_fn)ddraw->vtbl[DD_SET_DISPLAY_MODE])(
        ddraw, D32_WIDTH, D32_HEIGHT, bpp);
    d32_hx("SetModeHr", (DWORD)hr);
    if (hr != 0) {
        d32_finish("NO-MODE", 1u);
    }
    d32_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    ((d32_display_mode_fn)ddraw->vtbl[DD_GET_DISPLAY_MODE])(ddraw, &desc);
    d32_hx("ModeBpp", desc.ddpfPixelFormat[3]);
    d32_hx("ModeRMask", desc.ddpfPixelFormat[4]);
    d32_hx("ModeGMask", desc.ddpfPixelFormat[5]);
    d32_hx("ModeBMask", desc.ddpfPixelFormat[6]);
    d32_bytes = desc.ddpfPixelFormat[3] / 8ul;
    d32_masks[0] = desc.ddpfPixelFormat[4];
    d32_masks[1] = desc.ddpfPixelFormat[5];
    d32_masks[2] = desc.ddpfPixelFormat[6];
    d32_tolerance = d32_bytes == 4ul ? 0ul : 8ul;
    if (d32_bytes != bpp / 8ul) {
        d32_finish("WRONG-MODE", 1u);
    }

    d32_line("Stage", "3-enumerate");
    hr = ((d32_query_fn)ddraw->vtbl[COM_QUERY_INTERFACE])(
        ddraw, &d32_iid_d3d2, (void **)&d3d);
    d32_hx("D3DQueryHr", (DWORD)hr);
    if (hr != 0 || d3d == 0) {
        d32_finish("NO-D3D", 1u);
    }
    d32_zero(&enumerated, sizeof(enumerated));
    hr = ((d32_enum_devices_fn)d3d->vtbl[D3D2_ENUM_DEVICES])(
        d3d, d32_enum_device, &enumerated);
    d32_hx("EnumHr", (DWORD)hr);
    d32_hx("HalFound", enumerated.hal_found);
    d32_hx("HalRenderDepth", enumerated.render_depth);
    d32_hx("HalZDepth", enumerated.z_depth);
    if (enumerated.hal_found == 0ul ||
        (enumerated.render_depth & (bpp == 32ul ? DDBD_32 : DDBD_16)) ==
            0ul) {
        d32_finish("NO-HAL-AT-DEPTH", 1u);
    }
    if ((enumerated.z_depth & (zbits == 32ul ? DDBD_32 : DDBD_16)) == 0ul) {
        d32_finish("NO-Z-AT-DEPTH", 1u);
    }

    d32_line("Stage", "4-surfaces");
    d32_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    desc.dwBackBufferCount = 1ul;
    desc.ddsCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX |
                   DDSCAPS_3DDEVICE;
    hr = ((d32_create_surface_fn)ddraw->vtbl[DD_CREATE_SURFACE])(
        ddraw, &desc, &primary, 0);
    d32_hx("PrimaryHr", (DWORD)hr);
    if (hr == 0 && primary != 0) {
        caps = DDSCAPS_BACKBUFFER;
        hr = ((d32_get_attached_fn)primary->vtbl[DDS_GET_ATTACHED_SURFACE])(
            primary, &caps, &back);
        d32_hx("BackHr", (DWORD)hr);
    }
    if (hr == 0 && back != 0) {
        d32_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT |
                       DDSD_ZBUFFERBITDEPTH;
        desc.dwWidth = D32_WIDTH;
        desc.dwHeight = D32_HEIGHT;
        desc.dwMipMapCount = zbits;
        desc.ddsCaps = DDSCAPS_ZBUFFER | DDSCAPS_VIDEOMEMORY;
        hr = ((d32_create_surface_fn)ddraw->vtbl[DD_CREATE_SURFACE])(
            ddraw, &desc, &zbuffer, 0);
        d32_hx("ZCreateHr", (DWORD)hr);
        if (hr == 0 && zbuffer != 0) {
            hr = ((d32_attach_fn)back->vtbl[DDS_ADD_ATTACHED_SURFACE])(
                back, zbuffer);
            d32_hx("ZAttachHr", (DWORD)hr);
        }
    }
    if (hr != 0 || back == 0) {
        d32_finish("NO-SURFACES", 1u);
    }

    d32_line("Stage", "5-device");
    device = d32_device(d3d, back, D32_WIDTH, D32_HEIGHT, "Back");
    if (device == 0) {
        d32_finish("NO-DEVICE", 1u);
    }
    d32_hx("ClearHr", (DWORD)d32_fill(back, DDBLT_COLORFILL, 0ul));
    /* The far plane at the buffer's full scale, as Direct3D's own clear
     * sends it: 0xFFFF for 16 bits, 0xFFFFFFFF for a 32-bit buffer. */
    d32_hx("ZClearHr", (DWORD)d32_fill(zbuffer, DDBLT_DEPTHFILL,
                                       zbits == 32ul ? 0xfffffffful
                                                     : 0xfffful));

    /* T1, flat. */
    d32_line("Stage", "6-t1-flat");
    d32_vertex(&v[0], 8.0f, 8.0f, 0.5f, 0xff13579bul);
    d32_vertex(&v[1], 56.0f, 8.0f, 0.5f, 0xff13579bul);
    d32_vertex(&v[2], 8.0f, 56.0f, 0.5f, 0xff13579bul);
    d32_hx("T1DrawHr", (DWORD)d32_draw(device, v, 3ul));

    /* T2, Gouraud: red, green and blue corners. */
    d32_line("Stage", "7-t2-gouraud");
    d32_vertex(&v[0], 108.0f, 8.0f, 0.5f, 0xffff0000ul);
    d32_vertex(&v[1], 156.0f, 8.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[2], 108.0f, 56.0f, 0.5f, 0xff0000fful);
    d32_hx("T2DrawHr", (DWORD)d32_draw(device, v, 3ul));

    /* T3, depth: a green quad at 0.5, red behind it, blue in front of it
     * over the left half. */
    d32_line("Stage", "8-t3-depth");
    d32_state(device, D3DRENDERSTATE_ZENABLE, 1ul);
    d32_state(device, D3DRENDERSTATE_ZWRITEENABLE, 1ul);
    d32_state(device, D3DRENDERSTATE_ZFUNC, D3DCMP_LESS);
    d32_vertex(&v[0], 200.0f, 8.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[1], 264.0f, 8.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[2], 200.0f, 72.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[3], 264.0f, 8.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[4], 264.0f, 72.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[5], 200.0f, 72.0f, 0.5f, 0xff00ff00ul);
    d32_hx("T3QuadHr", (DWORD)d32_draw(device, v, 6ul));
    d32_vertex(&v[0], 196.0f, 4.0f, 0.75f, 0xffff0000ul);
    d32_vertex(&v[1], 300.0f, 4.0f, 0.75f, 0xffff0000ul);
    d32_vertex(&v[2], 196.0f, 108.0f, 0.75f, 0xffff0000ul);
    d32_hx("T3BehindHr", (DWORD)d32_draw(device, v, 3ul));
    d32_vertex(&v[0], 196.0f, 4.0f, 0.25f, 0xff0000fful);
    d32_vertex(&v[1], 232.0f, 4.0f, 0.25f, 0xff0000fful);
    d32_vertex(&v[2], 196.0f, 108.0f, 0.25f, 0xff0000fful);
    d32_hx("T3FrontHr", (DWORD)d32_draw(device, v, 3ul));

    /*
     * T5, depth precision: a green quad at z = 16000.25 / 65535, then a red
     * one a tenth of a 16-bit step nearer (16000.15 / 65535). A 16-bit
     * buffer stores the green as step 16000 whether it rounds or truncates,
     * and the red fragment, still above 16000, fails LESS against it; a
     * 24-bit buffer stores them about 25 steps apart and draws the red.
     * Green with a 16-bit buffer and red with a 32-bit one is the test
     * discriminating.
     *
     * The first form put the green at 0.25, three quarters through a
     * 16-bit step: Gen3 drew the red at 16 bits too (netbook boot 211),
     * which says the stored value rounds up and the fragment is compared
     * at more than 16 bits, so a step's lower quarter is the place to test.
     */
    d32_line("Stage", "8b-t5-precision");
    d32_vertex(&v[0], 300.0f, 8.0f, 0.244148165f, 0xff00ff00ul);
    d32_vertex(&v[1], 364.0f, 8.0f, 0.244148165f, 0xff00ff00ul);
    d32_vertex(&v[2], 300.0f, 72.0f, 0.244148165f, 0xff00ff00ul);
    d32_vertex(&v[3], 364.0f, 8.0f, 0.244148165f, 0xff00ff00ul);
    d32_vertex(&v[4], 364.0f, 72.0f, 0.244148165f, 0xff00ff00ul);
    d32_vertex(&v[5], 300.0f, 72.0f, 0.244148165f, 0xff00ff00ul);
    d32_hx("T5BackHr", (DWORD)d32_draw(device, v, 6ul));
    d32_vertex(&v[0], 300.0f, 8.0f, 0.244146645f, 0xffff0000ul);
    d32_vertex(&v[1], 364.0f, 8.0f, 0.244146645f, 0xffff0000ul);
    d32_vertex(&v[2], 300.0f, 72.0f, 0.244146645f, 0xffff0000ul);
    d32_vertex(&v[3], 364.0f, 8.0f, 0.244146645f, 0xffff0000ul);
    d32_vertex(&v[4], 364.0f, 72.0f, 0.244146645f, 0xffff0000ul);
    d32_vertex(&v[5], 300.0f, 72.0f, 0.244146645f, 0xffff0000ul);
    d32_hx("T5FrontHr", (DWORD)d32_draw(device, v, 6ul));
    d32_state(device, D3DRENDERSTATE_ZENABLE, 0ul);

    /* T6, a texture: the 8x8 quadrant map over a 64x64 quad at (400, 8),
     * DECAL and nearest, so each quadrant's centre is its texel. */
    d32_line("Stage", "8c-t6-texture");
    handle = d32_texture(ddraw, device, &texture_surface);
    if (handle != 0ul) {
        d32_state(device, D3DRENDERSTATE_TEXTUREHANDLE, handle);
        d32_state(device, D3DRENDERSTATE_TEXTUREMAG, D3DFILTER_NEAREST);
        d32_state(device, D3DRENDERSTATE_TEXTUREMIN, D3DFILTER_NEAREST);
        d32_state(device, D3DRENDERSTATE_TEXTUREMAPBLEND, D3DTBLEND_DECAL);
        d32_vertex(&v[0], 400.0f, 8.0f, 0.5f, 0xfffffffful);
        d32_vertex(&v[1], 464.0f, 8.0f, 0.5f, 0xfffffffful);
        d32_vertex(&v[2], 400.0f, 72.0f, 0.5f, 0xfffffffful);
        v[1].tu = 1.0f;
        v[2].tv = 1.0f;
        d32_vertex(&v[3], 464.0f, 8.0f, 0.5f, 0xfffffffful);
        d32_vertex(&v[4], 464.0f, 72.0f, 0.5f, 0xfffffffful);
        d32_vertex(&v[5], 400.0f, 72.0f, 0.5f, 0xfffffffful);
        v[3].tu = 1.0f;
        v[4].tu = 1.0f;
        v[4].tv = 1.0f;
        v[5].tv = 1.0f;
        d32_hx("T6DrawHr", (DWORD)d32_draw(device, v, 6ul));
        d32_state(device, D3DRENDERSTATE_TEXTUREHANDLE, 0ul);
    }

    /* T7, blending: opaque green, then red at alpha 0x80 with SRCALPHA
     * and INVSRCALPHA over it, at (480, 8). */
    d32_line("Stage", "8d-t7-blend");
    d32_vertex(&v[0], 480.0f, 8.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[1], 544.0f, 8.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[2], 480.0f, 72.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[3], 544.0f, 8.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[4], 544.0f, 72.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[5], 480.0f, 72.0f, 0.5f, 0xff00ff00ul);
    d32_hx("T7BackHr", (DWORD)d32_draw(device, v, 6ul));
    d32_state(device, D3DRENDERSTATE_ALPHABLENDENABLE, 1ul);
    d32_state(device, D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    d32_state(device, D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
    for (judged = 0ul; judged < 6ul; ++judged) {
        v[judged].color = 0x80ff0000ul;
    }
    d32_hx("T7BlendHr", (DWORD)d32_draw(device, v, 6ul));
    d32_state(device, D3DRENDERSTATE_ALPHABLENDENABLE, 0ul);

    d32_line("Stage", "9-read-back");
    d32_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = ((d32_lock_fn)back->vtbl[DDS_LOCK])(back, 0, &desc, DDLOCK_WAIT, 0);
    d32_hx("BackLockHr", (DWORD)hr);
    if (hr == 0 && desc.lpSurface != 0) {
        d32_hx("BackPitch", (DWORD)desc.lPitch);
        mismatches = d32_flat_mismatches(&desc, 8ul, 8ul, 0x0013579bul,
                                         &judged);
        d32_hx("T1Pixel", d32_pixel(&desc, 16ul, 16ul));
        d32_hx("T1Judged", judged);
        d32_hx("T1Mismatches", mismatches);
        failures += mismatches != 0ul ? 1ul : 0ul;

        d32_hx("T2RedCorner", d32_pixel(&desc, 110ul, 10ul));
        d32_hx("T2GreenCorner", d32_pixel(&desc, 150ul, 10ul));
        d32_hx("T2BlueCorner", d32_pixel(&desc, 110ul, 50ul));
        d32_hx("T2Centre", d32_pixel(&desc, 124ul, 24ul));
        caps = (d32_dominant(d32_pixel(&desc, 110ul, 10ul), 0) ? 1ul : 0ul) |
               (d32_dominant(d32_pixel(&desc, 150ul, 10ul), 1) ? 2ul : 0ul) |
               (d32_dominant(d32_pixel(&desc, 110ul, 50ul), 2) ? 4ul : 0ul);
        d32_hx("T2CornersOk", caps);
        failures += caps != 7ul ? 1ul : 0ul;

        d32_hx("T3Front", d32_pixel(&desc, 204ul, 20ul));
        d32_hx("T3Kept", d32_pixel(&desc, 250ul, 20ul));
        caps = (d32_is_colour(d32_pixel(&desc, 204ul, 20ul), 0x000000fful)
                    ? 1ul : 0ul) |
               (d32_is_colour(d32_pixel(&desc, 250ul, 20ul), 0x0000ff00ul)
                    ? 2ul : 0ul) |
               (d32_is_colour(d32_pixel(&desc, 250ul, 66ul), 0x0000ff00ul)
                    ? 4ul : 0ul);
        d32_hx("T3Ok", caps);
        failures += caps != 7ul ? 1ul : 0ul;

        d32_hx("T5Pixel", d32_pixel(&desc, 332ul, 40ul));
        caps = d32_is_colour(d32_pixel(&desc, 332ul, 40ul),
                             zbits == 32ul ? 0x00ff0000ul : 0x0000ff00ul)
                   ? 1ul : 0ul;
        d32_hx("T5Ok", caps);
        failures += caps != 1ul ? 1ul : 0ul;

        /* T6: each quadrant's centre is its texel, within the expansion
         * of a 5-bit channel to the target's. */
        if (handle != 0ul) {
            d32_hx("T6Red", d32_pixel(&desc, 416ul, 24ul));
            d32_hx("T6Green", d32_pixel(&desc, 448ul, 24ul));
            d32_hx("T6Blue", d32_pixel(&desc, 416ul, 56ul));
            d32_hx("T6White", d32_pixel(&desc, 448ul, 56ul));
            caps = (d32_near(d32_pixel(&desc, 416ul, 24ul), 0x00ff0000ul,
                             8ul) ? 1ul : 0ul) |
                   (d32_near(d32_pixel(&desc, 448ul, 24ul), 0x0000ff00ul,
                             8ul) ? 2ul : 0ul) |
                   (d32_near(d32_pixel(&desc, 416ul, 56ul), 0x000000fful,
                             8ul) ? 4ul : 0ul) |
                   (d32_near(d32_pixel(&desc, 448ul, 56ul), 0x00fffffful,
                             8ul) ? 8ul : 0ul);
        } else {
            caps = 0ul;
        }
        d32_hx("T6Ok", caps);
        failures += caps != 15ul ? 1ul : 0ul;

        /* T7: half red over green, about 0x80 and 0x7F. */
        d32_hx("T7Pixel", d32_pixel(&desc, 512ul, 40ul));
        caps = d32_near(d32_pixel(&desc, 512ul, 40ul), 0x00807f00ul, 8ul)
                   ? 1ul : 0ul;
        d32_hx("T7Ok", caps);
        failures += caps != 1ul ? 1ul : 0ul;
        ((d32_unlock_fn)back->vtbl[DDS_UNLOCK])(back, desc.lpSurface);
    } else {
        ++failures;
    }

    /* T4, an offscreen render target with a device of its own. */
    d32_line("Stage", "10-t4-offscreen");
    d32_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = 64ul;
    desc.dwHeight = 64ul;
    desc.ddsCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE |
                   DDSCAPS_VIDEOMEMORY;
    hr = ((d32_create_surface_fn)ddraw->vtbl[DD_CREATE_SURFACE])(
        ddraw, &desc, &offscreen, 0);
    d32_hx("T4CreateHr", (DWORD)hr);
    if (hr == 0 && offscreen != 0) {
        device2 = d32_device(d3d, offscreen, 64ul, 64ul, "T4");
    }
    if (device2 != 0) {
        d32_hx("T4ClearHr", (DWORD)d32_fill(offscreen, DDBLT_COLORFILL, 0ul));
        d32_vertex(&v[0], 0.0f, 0.0f, 0.5f, 0xff2468acul);
        d32_vertex(&v[1], 48.0f, 0.0f, 0.5f, 0xff2468acul);
        d32_vertex(&v[2], 0.0f, 48.0f, 0.5f, 0xff2468acul);
        d32_hx("T4DrawHr", (DWORD)d32_draw(device2, v, 3ul));
        d32_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        hr = ((d32_lock_fn)offscreen->vtbl[DDS_LOCK])(offscreen, 0, &desc,
                                                      DDLOCK_WAIT, 0);
        d32_hx("T4LockHr", (DWORD)hr);
        if (hr == 0 && desc.lpSurface != 0) {
            mismatches = d32_flat_mismatches(&desc, 0ul, 0ul, 0x002468acul,
                                             &judged);
            d32_hx("T4Pixel", d32_pixel(&desc, 8ul, 8ul));
            d32_hx("T4Judged", judged);
            d32_hx("T4Mismatches", mismatches);
            failures += mismatches != 0ul ? 1ul : 0ul;
            ((d32_unlock_fn)offscreen->vtbl[DDS_UNLOCK])(offscreen,
                                                         desc.lpSurface);
        } else {
            ++failures;
        }
        ((d32_release_fn)device2->vtbl[COM_RELEASE])(device2);
    } else {
        ++failures;
    }

    d32_line("Stage", "11-release");
    ((d32_release_fn)device->vtbl[COM_RELEASE])(device);
    if (offscreen != 0) {
        ((d32_release_fn)offscreen->vtbl[COM_RELEASE])(offscreen);
    }
    if (texture_surface != 0) {
        ((d32_release_fn)texture_surface->vtbl[COM_RELEASE])(
            texture_surface);
    }
    if (zbuffer != 0) {
        ((d32_release_fn)zbuffer->vtbl[COM_RELEASE])(zbuffer);
    }
    ((d32_release_fn)primary->vtbl[COM_RELEASE])(primary);
    ((d32_release_fn)d3d->vtbl[COM_RELEASE])(d3d);
    ((d32_restore_fn)ddraw->vtbl[DD_RESTORE_DISPLAY_MODE])(ddraw);
    ((d32_release_fn)ddraw->vtbl[COM_RELEASE])(ddraw);
    DestroyWindow(window);
    d32_hx("Failures", failures);
    d32_finish(failures == 0ul ? "PASS" : "FAIL", failures == 0ul ? 0u : 1u);
}
