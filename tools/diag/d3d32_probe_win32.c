#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/win9x_ddraw_abi.h"
#include "probe_lock.h"

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
 *   T7 half-alpha red over opaque green, SRCALPHA / INVSRCALPHA, an 8x8
 *      average within 8 of 0x807F00 (a half stipple averages the same);
 *   T8 sloped depth: two quads whose z runs in opposite directions across
 *      the same pixels, once along x and once along y, must cross at the
 *      centre.
 *   T11 culling by every submit form the DX5 device has - a triangle list,
 *      an indexed list, a strip (its odd triangle's swap), a fan, an
 *      indexed strip and an indexed list over 260 vertices - each under
 *      no culling, CW and CCW, with a clockwise and a counter-clockwise
 *      quad: present where Direct3D keeps it, absent where it culls.
 *   T12 two texture stages through a DirectX 6 device: a quadrant texture
 *      on stage 0 and a lightmap on stage 1 with its own coordinates,
 *      MODULATE and ADD, each quadrant against its expected colour; skipped
 *      when the device's caps offer one stage.
 *   T13 volume: a list, an indexed list and a strip at DirectX 5's 1024
 *      vertices a call, 48 times each, and T12's two stages over 340
 *      triangles a call: the centres drawn and nothing stray in the band.
 *   T14 a second copy of this program (/child) that draws, flips and exits
 *      inside a scene without releasing anything; then this one sets the
 *      mode again and draws T1's triangle.
 *   T15 a pattern in process memory, a shared mapping and a file read
 *      back through the disk cache, laid before the first draw and checked
 *      after the last; the memory is scanned again at every stage line,
 *      and each bad word is written out with its address and value.
 *      /t15early lays it before DirectDraw loads; /t15only draws nothing
 *      and watches it for 30 s instead; /t15bare does that without loading
 *      DirectDraw; /t15small lays 4 MiB and 1 MiB instead of 32 and 8.
 *      Each page's page-table entry is kept, to tell a page the pager
 *      wrote out and read back from one that stayed.
 *
 * Every run also fails if the engine gave up on a wait (RunIdleTimeouts,
 * RunFifoTimeouts): a driver that times out draws the rest on the CPU, and
 * the pixels come out right.
 *
 * T9 and each T11 form also record which path drew them, from the HAL's
 * own counters read through the V9X_DDGETTRACE escape before and after:
 * DrawPrimitives2 calls (DDI 6), the engine's whole indexed path and the
 * core's triangle list. A check is only as good as the path it reached:
 * T9 passed for weeks through the core's culling while the Rage 128's
 * hardware cull was inverted (2026-10-24), and nothing said which path
 * had answered.
 *
 * /ddi5, /ddi6 and /ddi7 set this program's [Velocity9x.Direct3DDdi] entry
 * in SYSTEM.INI for the run and restore it after, so one probe covers each
 * interface; without any the driver's own choice stands.
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
#define D3DPT_TRIANGLESTRIP 5ul
#define D3DPT_TRIANGLEFAN 6ul
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
#define D3DCULL_CW 2ul
#define D3DCULL_CCW 3ul
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
#define D3DDEV2_DRAW_INDEXED_PRIMITIVE 30
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
typedef HRESULT (WINAPI *d32_draw_indexed_fn)(D32_OBJECT *, DWORD, DWORD,
                                              void *, DWORD, WORD *, DWORD,
                                              DWORD);
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

static void d32_sentinel_check(const char *stage);

/* One line, written through and flushed before the caller goes on. A
 * stage line first scans T15's pattern (d32_sentinel_check). */
static void d32_line(const char *key, const char *value)
{
    DWORD written;
    if (d32_file == INVALID_HANDLE_VALUE) {
        return;
    }
    if (lstrcmpA(key, "Stage") == 0) {
        d32_sentinel_check(value);
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

/*
 * A switch is written /name or -name, in any case. The dash form used to
 * match nothing and say nothing: A8U4I5's 0.32.0 checks ran
 * "-t14nomode", and T14 quietly ran its default child
 * (docs\decisions\2026-10-27-0.32.0-release-checks-ati-ironlake.md).
 * Switches the probe does not know are listed in the report
 * (d32_report_switches).
 */
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
            if (index == 0u && c == '-' && option[0] == '/') {
                continue;
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

/* Every switch the probe reads; a name ending in '=' takes a value. */
static const char *const d32_known_switches[] = {
    "/bpp16", "/z24", "/ddi5", "/ddi6", "/ddi7", "/child",
    "/clean", "/endscene", "/nomode", "/release3d",
    "/t14clean", "/t14endscene", "/t14nomode", "/t14release3d", "/t14wait",
    "/t15bare", "/t15early", "/t15frames", "/t15fresh", "/t15ones",
    "/t15only", "/t15small", "/t15mb=", "/t15frame=", "/t15hunt=",
    "/t15watch="
};

/* Whether one command-line word, of length bytes and starting with / or -,
 * is a switch the probe reads. */
static int d32_switch_known(const char *word, unsigned length)
{
    unsigned entry;
    unsigned index;

    for (entry = 0u;
         entry < sizeof(d32_known_switches) / sizeof(d32_known_switches[0]);
         ++entry) {
        const char *name = d32_known_switches[entry];

        for (index = 1u; name[index] != '\0' && index < length; ++index) {
            char c = word[index];

            if (c >= 'A' && c <= 'Z') {
                c = (char)(c + ('a' - 'A'));
            }
            if (c != name[index]) {
                break;
            }
        }
        if (name[index] == '\0' &&
            (index == length || name[index - 1u] == '=')) {
            return 1;
        }
    }
    return 0;
}

/*
 * The command line as given, and each word after the program's name that
 * starts with / or - and names no switch the probe reads: those changed
 * nothing, and a run that looks like it tested something it did not
 * should say so in its own report.
 */
static void d32_report_switches(void)
{
    const char *line = GetCommandLineA();
    char word[64];
    unsigned at = 0u;
    unsigned length;
    unsigned unknown = 0u;
    int quoted = 0;

    d32_line("CommandLine", line);
    /* Past the program's name, quoted or not. */
    while (line[at] != '\0' && (quoted || line[at] != ' ')) {
        if (line[at] == '"') {
            quoted = !quoted;
        }
        ++at;
    }
    while (line[at] != '\0') {
        while (line[at] == ' ') {
            ++at;
        }
        length = 0u;
        while (line[at + length] != '\0' && line[at + length] != ' ') {
            ++length;
        }
        if (length != 0u && (line[at] == '/' || line[at] == '-') &&
            !d32_switch_known(line + at, length)) {
            unsigned copy = length < sizeof(word) - 1u
                ? length : (unsigned)sizeof(word) - 1u;
            unsigned index;

            for (index = 0u; index < copy; ++index) {
                word[index] = line[at + index];
            }
            word[copy] = '\0';
            d32_line("UnknownSwitch", word);
            ++unknown;
        }
        at += length;
    }
    d32_hx("UnknownSwitches", (DWORD)unknown);
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

static void d32_ddi_restore(void);

static void d32_finish(const char *result, UINT code)
{
    d32_ddi_restore();
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

static HRESULT d32_draw_indexed(D32_OBJECT *device, D32_TLVERTEX *vertices,
                                DWORD vertex_count, WORD *indices,
                                DWORD index_count)
{
    HRESULT hr;
    HRESULT draw_hr;

    hr = ((d32_scene_fn)device->vtbl[D3DDEV2_BEGIN_SCENE])(device);
    if (hr != 0) {
        return hr;
    }
    draw_hr = ((d32_draw_indexed_fn)
               device->vtbl[D3DDEV2_DRAW_INDEXED_PRIMITIVE])(
        device, D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, vertices, vertex_count,
        indices, index_count, 0ul);
    hr = ((d32_scene_fn)device->vtbl[D3DDEV2_END_SCENE])(device);
    return draw_hr != 0 ? draw_hr : hr;
}

static void d32_state(D32_OBJECT *device, DWORD state, DWORD value)
{
    ((d32_render_state_fn)device->vtbl[D3DDEV2_SET_RENDER_STATE])(
        device, state, value);
}

/* One primitive of any type, in its own scene. */
static HRESULT d32_draw_type(D32_OBJECT *device, DWORD type,
                             D32_TLVERTEX *vertices, DWORD count)
{
    HRESULT hr;
    HRESULT draw_hr;

    hr = ((d32_scene_fn)device->vtbl[D3DDEV2_BEGIN_SCENE])(device);
    if (hr != 0) {
        return hr;
    }
    draw_hr = ((d32_draw_fn)device->vtbl[D3DDEV2_DRAW_PRIMITIVE])(
        device, type, D3DVT_TLVERTEX, vertices, count, 0ul);
    hr = ((d32_scene_fn)device->vtbl[D3DDEV2_END_SCENE])(device);
    return draw_hr != 0 ? draw_hr : hr;
}

static HRESULT d32_draw_indexed_type(D32_OBJECT *device, DWORD type,
                                     D32_TLVERTEX *vertices,
                                     DWORD vertex_count, WORD *indices,
                                     DWORD index_count)
{
    HRESULT hr;
    HRESULT draw_hr;

    hr = ((d32_scene_fn)device->vtbl[D3DDEV2_BEGIN_SCENE])(device);
    if (hr != 0) {
        return hr;
    }
    draw_hr = ((d32_draw_indexed_fn)
               device->vtbl[D3DDEV2_DRAW_INDEXED_PRIMITIVE])(
        device, type, D3DVT_TLVERTEX, vertices, vertex_count, indices,
        index_count, 0ul);
    hr = ((d32_scene_fn)device->vtbl[D3DDEV2_END_SCENE])(device);
    return draw_hr != 0 ? draw_hr : hr;
}

/*
 * /ddi5 and /ddi6: this program's [Velocity9x.Direct3DDdi] entry, which
 * the HAL reads when the runtime negotiates (d3d_core.c,
 * v9x_d3d_dp2_for_process), set before DirectDraw is created. Whatever was
 * there is put back by d32_finish, on every way out.
 */
#define D32_DDI_SECTION "Velocity9x.Direct3DDdi"
#define D32_DDI_KEY "V9XD32.EXE"

static int d32_ddi_set;
static char d32_ddi_previous[16];

static void d32_ddi_apply(const char *value)
{
    GetPrivateProfileStringA(D32_DDI_SECTION, D32_DDI_KEY, "",
                             d32_ddi_previous, sizeof(d32_ddi_previous),
                             "SYSTEM.INI");
    d32_ddi_set = 1;
    WritePrivateProfileStringA(D32_DDI_SECTION, D32_DDI_KEY, value,
                               "SYSTEM.INI");
    d32_line("DdiRequested", value);
    d32_line("DdiPrevious", d32_ddi_previous[0] != '\0'
                                ? d32_ddi_previous : "(none)");
}

static void d32_ddi_restore(void)
{
    if (!d32_ddi_set) {
        return;
    }
    d32_ddi_set = 0;
    WritePrivateProfileStringA(D32_DDI_SECTION, D32_DDI_KEY,
                               d32_ddi_previous[0] != '\0'
                                   ? d32_ddi_previous : 0,
                               "SYSTEM.INI");
}

/*
 * Which path drew: the HAL's Direct3D counters through V9X_DDGETTRACE, as
 * V9XTRACE reads them. GDI32 is reached through GetProcAddress, so the
 * probe still imports only KERNEL32 and USER32. The 16-bit escape copies
 * the snapshot without a length check, so the buffer carries slack (an
 * undersized one crashed V9XCPU, a19476c9).
 */
typedef int (WINAPI *d32_ext_escape_fn)(HDC, int, int, LPCSTR, int, LPSTR);

typedef struct d32_path {
    DWORD ok;
    DWORD dp2_calls;
    DWORD indexed_engine;
    DWORD core_list;
    DWORD core_culled;
    DWORD two_unit;
    /* The engine's own: a wait it gave up on, and whether it is still
     * trusted. */
    DWORD idle_timeouts;
    DWORD fifo_timeouts;
    DWORD engine_flags;
} D32_PATH;

static union {
    V9X_DD_TRACE_SNAPSHOT snapshot;
    BYTE slack[sizeof(V9X_DD_TRACE_SNAPSHOT) + 65536u];
} d32_trace;

static void d32_path_read(D32_PATH *path)
{
    static d32_ext_escape_fn escape;
    V9X_DCICMD command;
    HDC screen;
    int result;

    d32_zero(path, sizeof(*path));
    if (escape == 0) {
        HMODULE gdi = LoadLibraryA("GDI32.DLL");

        escape = gdi != 0 ? (d32_ext_escape_fn)GetProcAddress(gdi,
                                                              "ExtEscape")
                          : 0;
        if (escape == 0) {
            return;
        }
    }
    screen = GetDC(0);
    if (screen == 0) {
        return;
    }
    d32_zero(&d32_trace, sizeof(d32_trace));
    command.dwCommand = V9X_DDGETTRACE;
    command.dwParam1 = 0ul;
    command.dwParam2 = 0ul;
    command.dwVersion = V9X_DD_VERSION;
    command.dwReserved = 0ul;
    result = escape(screen, V9X_DCICOMMAND, sizeof(command),
                    (LPCSTR)&command, sizeof(d32_trace.snapshot),
                    (LPSTR)&d32_trace.snapshot);
    ReleaseDC(0, screen);
    if (result <= 0 ||
        d32_trace.snapshot.dwSize != sizeof(V9X_DD_TRACE_SNAPSHOT) ||
        d32_trace.snapshot.abi != V9X_DD_SHARED_ABI) {
        return;
    }
    path->ok = 1ul;
    path->dp2_calls = d32_trace.snapshot.d3d.dp2_calls;
    path->indexed_engine = d32_trace.snapshot.d3d.indexed_engine_calls;
    path->core_list = d32_trace.snapshot.d3d.r3d_list_triangles_in;
    path->core_culled = d32_trace.snapshot.d3d.r3d_list_culled;
    path->two_unit = d32_trace.snapshot.d3d.two_unit_batches;
    path->idle_timeouts = d32_trace.snapshot.engine.idle_timeouts;
    path->fifo_timeouts = d32_trace.snapshot.engine.fifo_timeouts;
    path->engine_flags = d32_trace.snapshot.engine.flags;
}

/*
 * What happened between two reads, as <prefix>Path=: "engine-two-unit"
 * when batches went on two texture units, "engine-indexed"
 * when the engine took indexed triangles whole, "core-list" when the core's
 * list builder had them, "none" when neither moved, "unknown" without the
 * escape; and the counts, with Dp2 saying the interface was DDI 6.
 */
static void d32_path_write(const char *prefix, const D32_PATH *before,
                           const D32_PATH *after)
{
    char key[48];
    const char *path;

    if (!before->ok || !after->ok) {
        path = "unknown";
    } else if (after->two_unit != before->two_unit) {
        path = "engine-two-unit";
    } else if (after->indexed_engine != before->indexed_engine) {
        path = "engine-indexed";
    } else if (after->core_list != before->core_list) {
        path = "core-list";
    } else {
        path = "none";
    }
    wsprintfA(key, "%sPath", prefix);
    d32_line(key, path);
    wsprintfA(key, "%sDp2Calls", prefix);
    d32_hx(key, after->dp2_calls - before->dp2_calls);
    wsprintfA(key, "%sEngineIndexed", prefix);
    d32_hx(key, after->indexed_engine - before->indexed_engine);
    wsprintfA(key, "%sCoreTriangles", prefix);
    d32_hx(key, after->core_list - before->core_list);
    wsprintfA(key, "%sCoreCulled", prefix);
    d32_hx(key, after->core_culled - before->core_culled);
    wsprintfA(key, "%sTwoUnit", prefix);
    d32_hx(key, after->two_unit - before->two_unit);
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
/*
 * How much of the Z buffer holds what, recorded and not judged: the share
 * of cells at the far plane in either form a 32-bit buffer may store it
 * (00FFFFFF with a zero pad byte, FFFFFFFF with a set one), at the 16-bit
 * far plane, and anything else, with the first cell and the surface's
 * address. A clear that reached only part of the buffer shows here.
 */
static void d32_zstats(D32_OBJECT *zbuffer, DWORD zbits, const char *prefix)
{
    D32_SURFACE_DESC desc;
    HRESULT hr;
    DWORD counts[4];
    DWORD y;
    DWORD x;
    DWORD cell = zbits == 32ul ? 4ul : 2ul;
    char key[40];

    if (zbuffer == 0) {
        return;
    }
    d32_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = ((d32_lock_fn)zbuffer->vtbl[DDS_LOCK])(zbuffer, 0, &desc,
                                                DDLOCK_WAIT, 0);
    wsprintfA(key, "%sLockHr", prefix);
    d32_hx(key, (DWORD)hr);
    if (hr != 0 || desc.lpSurface == 0) {
        return;
    }
    counts[0] = 0ul;
    counts[1] = 0ul;
    counts[2] = 0ul;
    counts[3] = 0ul;
    for (y = 0ul; y < desc.dwHeight; ++y) {
        const BYTE *row = (const BYTE *)desc.lpSurface + y * (DWORD)desc.lPitch;

        for (x = 0ul; x < desc.dwWidth; ++x) {
            DWORD value = cell == 4ul ? ((const DWORD *)row)[x]
                                      : (DWORD)((const WORD *)row)[x];

            if (value == 0x00fffffful) {
                ++counts[0];
            } else if (value == 0xfffffffful) {
                ++counts[1];
            } else if (value == 0x0000fffful) {
                ++counts[2];
            } else {
                ++counts[3];
            }
        }
    }
    wsprintfA(key, "%sAddress", prefix);
    d32_hx(key, (DWORD)desc.lpSurface);
    wsprintfA(key, "%sFirst", prefix);
    d32_hx(key, cell == 4ul ? *(const DWORD *)desc.lpSurface
                            : (DWORD)*(const WORD *)desc.lpSurface);
    wsprintfA(key, "%sFar24", prefix);
    d32_hx(key, counts[0]);
    wsprintfA(key, "%sFar32", prefix);
    d32_hx(key, counts[1]);
    wsprintfA(key, "%sFar16", prefix);
    d32_hx(key, counts[2]);
    wsprintfA(key, "%sOther", prefix);
    d32_hx(key, counts[3]);
    ((d32_unlock_fn)zbuffer->vtbl[DDS_UNLOCK])(zbuffer, desc.lpSurface);
}

/* An 8x8 ARGB1555 texture in video memory, its four 4x4 quadrants the
 * given texels (top left, top right, bottom left, bottom right); null when
 * it could not be made. <prefix>CreateHr and LockHr are recorded. */
static D32_OBJECT *d32_texture_surface(D32_OBJECT *ddraw,
                                       const WORD quadrant[4],
                                       const char *prefix)
{
    D32_SURFACE_DESC desc;
    D32_OBJECT *surface = 0;
    DWORD x;
    DWORD y;
    HRESULT hr;
    char key[32];

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
    wsprintfA(key, "%sCreateHr", prefix);
    d32_hx(key, (DWORD)hr);
    if (hr != 0 || surface == 0) {
        return 0;
    }
    d32_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = ((d32_lock_fn)surface->vtbl[DDS_LOCK])(surface, 0, &desc,
                                                DDLOCK_WAIT, 0);
    wsprintfA(key, "%sLockHr", prefix);
    d32_hx(key, (DWORD)hr);
    if (hr != 0 || desc.lpSurface == 0) {
        ((d32_release_fn)surface->vtbl[COM_RELEASE])(surface);
        return 0;
    }
    for (y = 0ul; y < 8ul; ++y) {
        WORD *row = (WORD *)((BYTE *)desc.lpSurface +
                             y * (DWORD)desc.lPitch);
        for (x = 0ul; x < 8ul; ++x) {
            row[x] = quadrant[(y >= 4ul ? 2u : 0u) + (x >= 4ul ? 1u : 0u)];
        }
    }
    ((d32_unlock_fn)surface->vtbl[DDS_UNLOCK])(surface, desc.lpSurface);
    return surface;
}

/* T6's texture: red, green, blue and white quadrants, and its DX5 handle
 * for device. */
static const WORD d32_quadrants[4] = { 0xfc00u, 0x83e0u, 0x801fu, 0xffffu };

static DWORD d32_texture(D32_OBJECT *ddraw, D32_OBJECT *device,
                         D32_OBJECT **surface_out)
{
    D32_OBJECT *surface;
    D32_OBJECT *texture = 0;
    DWORD handle = 0ul;
    HRESULT hr;

    surface = d32_texture_surface(ddraw, d32_quadrants, "T6");
    *surface_out = surface;
    if (surface == 0) {
        return 0ul;
    }
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

/*
 * T11, culling by submit form. Each form draws a 24x24 quad clockwise on
 * the screen (y down) or counter-clockwise, as two triangles of that
 * winding however the form builds them: a strip's second triangle is the
 * odd one Direct3D swaps back, a fan's share the first vertex, and the
 * wide form indexes a 260-vertex pool, past the ranges an engine may copy
 * whole (the Rage 128's DrawPrimitives2 path declines past 192 converted
 * vertices). Every unreferenced vertex of the pool is a copy of the
 * quad's first, so all of them are valid.
 */
#define D32_FORMS 6ul
#define D32_QUAD 24.0f
#define D32_WIDE_POOL 260ul

static const char *const d32_form_names[D32_FORMS] = {
    "List", "Indexed", "Strip", "Fan", "IndexedStrip", "IndexedWide"
};

static D32_TLVERTEX d32_wide[D32_WIDE_POOL];

static HRESULT d32_cull_quad(D32_OBJECT *device, DWORD form, float x,
                             float y, int clockwise)
{
    D32_TLVERTEX v[6];
    WORD indices[6];
    float r = x + D32_QUAD;
    float b = y + D32_QUAD;
    DWORD i;

    switch (form) {
    case 0ul:
        /* (TL TR BL) (TR BR BL) clockwise; their mirror otherwise. */
        d32_vertex(&v[0], x, y, 0.5f, 0xff00ff00ul);
        d32_vertex(&v[1], clockwise ? r : x, clockwise ? y : b, 0.5f,
                   0xff00ff00ul);
        d32_vertex(&v[2], clockwise ? x : r, clockwise ? b : y, 0.5f,
                   0xff00ff00ul);
        d32_vertex(&v[3], r, y, 0.5f, 0xff00ff00ul);
        d32_vertex(&v[4], clockwise ? r : x, b, 0.5f, 0xff00ff00ul);
        d32_vertex(&v[5], clockwise ? x : r, b, 0.5f, 0xff00ff00ul);
        return d32_draw_type(device, D3DPT_TRIANGLELIST, v, 6ul);
    case 1ul:
    case 4ul:
        /* TL TR BL BR, indexed as a list or a strip. */
        d32_vertex(&v[0], x, y, 0.5f, 0xff00ff00ul);
        d32_vertex(&v[1], r, y, 0.5f, 0xff00ff00ul);
        d32_vertex(&v[2], x, b, 0.5f, 0xff00ff00ul);
        d32_vertex(&v[3], r, b, 0.5f, 0xff00ff00ul);
        if (form == 4ul) {
            indices[0] = 0u;
            indices[1] = (WORD)(clockwise ? 1u : 2u);
            indices[2] = (WORD)(clockwise ? 2u : 1u);
            indices[3] = 3u;
            return d32_draw_indexed_type(device, D3DPT_TRIANGLESTRIP, v, 4ul,
                                         indices, 4ul);
        }
        indices[0] = 0u;
        indices[1] = (WORD)(clockwise ? 1u : 2u);
        indices[2] = (WORD)(clockwise ? 2u : 1u);
        indices[3] = 1u;
        indices[4] = (WORD)(clockwise ? 3u : 2u);
        indices[5] = (WORD)(clockwise ? 2u : 3u);
        return d32_draw_indexed_type(device, D3DPT_TRIANGLELIST, v, 4ul,
                                     indices, 6ul);
    case 2ul:
        /* TL TR BL BR clockwise, TL BL TR BR not. */
        d32_vertex(&v[0], x, y, 0.5f, 0xff00ff00ul);
        d32_vertex(&v[1], clockwise ? r : x, clockwise ? y : b, 0.5f,
                   0xff00ff00ul);
        d32_vertex(&v[2], clockwise ? x : r, clockwise ? b : y, 0.5f,
                   0xff00ff00ul);
        d32_vertex(&v[3], r, b, 0.5f, 0xff00ff00ul);
        return d32_draw_type(device, D3DPT_TRIANGLESTRIP, v, 4ul);
    case 3ul:
        /* Around the quad from TL: TR BR BL clockwise, BL BR TR not. */
        d32_vertex(&v[0], x, y, 0.5f, 0xff00ff00ul);
        d32_vertex(&v[1], clockwise ? r : x, clockwise ? y : b, 0.5f,
                   0xff00ff00ul);
        d32_vertex(&v[2], r, b, 0.5f, 0xff00ff00ul);
        d32_vertex(&v[3], clockwise ? x : r, clockwise ? b : y, 0.5f,
                   0xff00ff00ul);
        return d32_draw_type(device, D3DPT_TRIANGLEFAN, v, 4ul);
    default:
        /* TL at 0, TR at 255, BL at 129, BR at 259 of the pool. */
        for (i = 0ul; i < D32_WIDE_POOL; ++i) {
            d32_vertex(&d32_wide[i], x, y, 0.5f, 0xff00ff00ul);
        }
        d32_vertex(&d32_wide[255], r, y, 0.5f, 0xff00ff00ul);
        d32_vertex(&d32_wide[129], x, b, 0.5f, 0xff00ff00ul);
        d32_vertex(&d32_wide[259], r, b, 0.5f, 0xff00ff00ul);
        indices[0] = 0u;
        indices[1] = (WORD)(clockwise ? 255u : 129u);
        indices[2] = (WORD)(clockwise ? 129u : 255u);
        indices[3] = 255u;
        indices[4] = (WORD)(clockwise ? 259u : 129u);
        indices[5] = (WORD)(clockwise ? 129u : 259u);
        return d32_draw_indexed_type(device, D3DPT_TRIANGLELIST, d32_wide,
                                     D32_WIDE_POOL, indices, 6ul);
    }
}

/*
 * The whole matrix on a cleared target: row by form, then six cells - no
 * culling, CW, CCW, each with the clockwise quad and then the other - each
 * judged at four points, one in each corner, so both triangles count
 * whichever diagonal splits the quad. Direct3D's CW culls what runs
 * clockwise on the screen and CCW what runs counter-clockwise. Returns the
 * failures.
 */
static DWORD d32_cull_matrix(D32_OBJECT *device, D32_OBJECT *back)
{
    static const DWORD culls[3] = { D3DCULL_NONE, D3DCULL_CW, D3DCULL_CCW };
    static const DWORD offsets[4][2] = {
        { 5ul, 5ul }, { 19ul, 5ul }, { 5ul, 19ul }, { 19ul, 19ul }
    };
    D32_SURFACE_DESC desc;
    D32_PATH before;
    D32_PATH after;
    DWORD failures = 0ul;
    DWORD form;
    DWORD cell;
    DWORD point;
    HRESULT hr;
    HRESULT first_hr;
    char key[48];

    d32_line("Stage", "10a-t11-cull-matrix");
    d32_hx("T11ClearHr", (DWORD)d32_fill(back, DDBLT_COLORFILL, 0ul));
    for (form = 0ul; form < D32_FORMS; ++form) {
        d32_path_read(&before);
        first_hr = 0;
        for (cell = 0ul; cell < 6ul; ++cell) {
            d32_state(device, D3DRENDERSTATE_CULLMODE, culls[cell / 2ul]);
            hr = d32_cull_quad(device, form, 16.0f + (float)(cell * 40ul),
                               16.0f + (float)(form * 40ul),
                               (cell & 1ul) == 0ul);
            if (first_hr == 0) {
                first_hr = hr;
            }
        }
        d32_state(device, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
        d32_path_read(&after);
        wsprintfA(key, "T11%sHr", d32_form_names[form]);
        d32_hx(key, (DWORD)first_hr);
        wsprintfA(key, "T11%s", d32_form_names[form]);
        d32_path_write(key, &before, &after);
    }

    d32_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = ((d32_lock_fn)back->vtbl[DDS_LOCK])(back, 0, &desc, DDLOCK_WAIT, 0);
    d32_hx("T11LockHr", (DWORD)hr);
    if (hr != 0 || desc.lpSurface == 0) {
        return 1ul;
    }
    for (form = 0ul; form < D32_FORMS; ++form) {
        DWORD present = 0ul;
        DWORD ok = 0ul;

        for (cell = 0ul; cell < 6ul; ++cell) {
            DWORD cull = culls[cell / 2ul];
            int clockwise = (cell & 1ul) == 0ul;
            int kept = cull == D3DCULL_NONE ||
                       (cull == D3DCULL_CW && !clockwise) ||
                       (cull == D3DCULL_CCW && clockwise);
            DWORD seen = 0ul;

            for (point = 0ul; point < 4ul; ++point) {
                if (d32_is_colour(
                        d32_pixel(&desc,
                                  16ul + cell * 40ul + offsets[point][0],
                                  16ul + form * 40ul + offsets[point][1]),
                        0x0000ff00ul)) {
                    ++seen;
                }
            }
            present |= seen << (cell * 4ul);
            if ((kept && seen == 4ul) || (!kept && seen == 0ul)) {
                ok |= 1ul << cell;
            }
        }
        /* Four bits a cell, cell 0 lowest: how many points were drawn. */
        wsprintfA(key, "T11%sPresent", d32_form_names[form]);
        d32_hx(key, present);
        wsprintfA(key, "T11%sOk", d32_form_names[form]);
        d32_hx(key, ok);
        failures += ok != 0x3ful ? 1ul : 0ul;
    }
    ((d32_unlock_fn)back->vtbl[DDS_UNLOCK])(back, desc.lpSurface);
    return failures;
}

/*
 * T13, volume: every test above draws a handful of triangles, and an
 * engine splits its work into packets far larger than that. Under PIO the
 * Rage 128 hard-locked A8U4I5 on 3DMark 2000's two-unit batches while
 * T12's two triangles passed (2026-10-25,
 * docs\issues\2026-10-25-rage128-32mib-map-corrupts.md). So each submit
 * form is drawn at DirectX 5's limit of 1024 vertices a call - 340
 * triangles, many packets on any engine - and again D32_VOLUME_REPS
 * times: a list of 170 quads, the same quads indexed, and one strip of
 * 1018 triangles. Each has a band of its own; the quads' centres must be
 * its colour, and no pixel in the band may be anything but its colour or
 * the clear, which is what a malformed packet leaves.
 */
#define D32_VOLUME_QUADS 170ul
#define D32_VOLUME_VERTICES 1020ul
#define D32_VOLUME_REPS 48ul
#define D32_VOLUME_COLUMNS 34ul
#define D32_VOLUME_LIST_TOP 0ul
#define D32_VOLUME_INDEXED_TOP 96ul
#define D32_VOLUME_STRIP_TOP 192ul
#define D32_VOLUME_STRIP_HEIGHT 24ul
#define D32_VOLUME_ROWS_HEIGHT 80ul

static D32_TLVERTEX d32_volume_vertices[D32_VOLUME_VERTICES];
static WORD d32_volume_indices[D32_VOLUME_VERTICES];

/* Quad n of the grid: 12x12 on a 16-pixel pitch, 34 to a row. */
static void d32_volume_corner(DWORD quad, DWORD top, float *x, float *y)
{
    *x = (float)((quad % D32_VOLUME_COLUMNS) * 16ul + 2ul);
    *y = (float)(top + (quad / D32_VOLUME_COLUMNS) * 16ul + 2ul);
}

/* The six corners of every quad, as a list. */
static void d32_volume_list(DWORD top, DWORD colour)
{
    DWORD quad;
    float x;
    float y;
    D32_TLVERTEX *v;

    for (quad = 0ul; quad < D32_VOLUME_QUADS; ++quad) {
        d32_volume_corner(quad, top, &x, &y);
        v = &d32_volume_vertices[quad * 6ul];
        d32_vertex(&v[0], x, y, 0.5f, colour);
        d32_vertex(&v[1], x + 12.0f, y, 0.5f, colour);
        d32_vertex(&v[2], x, y + 12.0f, 0.5f, colour);
        d32_vertex(&v[3], x + 12.0f, y, 0.5f, colour);
        d32_vertex(&v[4], x + 12.0f, y + 12.0f, 0.5f, colour);
        d32_vertex(&v[5], x, y + 12.0f, 0.5f, colour);
    }
}

/* Four corners a quad and six indices into them: 680 vertices. */
static void d32_volume_indexed(DWORD top, DWORD colour)
{
    DWORD quad;
    float x;
    float y;
    D32_TLVERTEX *v;
    WORD *index;
    WORD base;

    for (quad = 0ul; quad < D32_VOLUME_QUADS; ++quad) {
        d32_volume_corner(quad, top, &x, &y);
        v = &d32_volume_vertices[quad * 4ul];
        d32_vertex(&v[0], x, y, 0.5f, colour);
        d32_vertex(&v[1], x + 12.0f, y, 0.5f, colour);
        d32_vertex(&v[2], x, y + 12.0f, 0.5f, colour);
        d32_vertex(&v[3], x + 12.0f, y + 12.0f, 0.5f, colour);
        base = (WORD)(quad * 4ul);
        index = &d32_volume_indices[quad * 6ul];
        index[0] = base;
        index[1] = (WORD)(base + 1u);
        index[2] = (WORD)(base + 2u);
        index[3] = (WORD)(base + 1u);
        index[4] = (WORD)(base + 3u);
        index[5] = (WORD)(base + 2u);
    }
}

/* One strip zigzagging along a band from x 2 to 600. */
static void d32_volume_strip(DWORD top, DWORD colour)
{
    DWORD i;
    float step = 598.0f / (float)(D32_VOLUME_VERTICES / 2ul - 1ul);

    for (i = 0ul; i < D32_VOLUME_VERTICES; ++i) {
        d32_vertex(&d32_volume_vertices[i], 2.0f + (float)(i / 2ul) * step,
                   (float)(top + ((i & 1ul) != 0ul
                                      ? D32_VOLUME_STRIP_HEIGHT : 0ul)),
                   0.5f, colour);
    }
}

/* Pixels in rows top to bottom that are neither colour nor the clear. */
static DWORD d32_stray(const D32_SURFACE_DESC *desc, DWORD top,
                       DWORD bottom, DWORD rgb, DWORD tolerance)
{
    DWORD x;
    DWORD y;
    DWORD pixel;
    DWORD stray = 0ul;

    for (y = top; y < bottom; ++y) {
        for (x = 0ul; x < D32_WIDTH; ++x) {
            pixel = d32_pixel(desc, x, y);
            if (pixel != 0ul && !d32_near(pixel, rgb, tolerance)) {
                ++stray;
            }
        }
    }
    return stray;
}

/* Centres of the first, a middle and the last quad of a grid band. */
static DWORD d32_volume_centres(const D32_SURFACE_DESC *desc, DWORD top,
                                DWORD rgb, DWORD tolerance)
{
    static const DWORD quads[4] = { 0ul, 33ul, 85ul, 169ul };
    DWORD point;
    DWORD ok = 0ul;
    DWORD x;
    DWORD y;

    /* d32_volume_corner's arithmetic in integers: no float conversion in
     * this freestanding build. */
    for (point = 0ul; point < 4ul; ++point) {
        x = (quads[point] % D32_VOLUME_COLUMNS) * 16ul + 2ul;
        y = top + (quads[point] / D32_VOLUME_COLUMNS) * 16ul + 2ul;
        if (d32_near(d32_pixel(desc, x + 6ul, y + 6ul), rgb, tolerance)) {
            ok |= 1ul << point;
        }
    }
    return ok;
}

static DWORD d32_volume(D32_OBJECT *device, D32_OBJECT *back)
{
    static const DWORD list_rgb = 0x0000ff00ul;
    static const DWORD indexed_rgb = 0x000000fful;
    static const DWORD strip_rgb = 0x00ff0000ul;
    D32_SURFACE_DESC desc;
    D32_PATH before;
    D32_PATH after;
    DWORD failures = 0ul;
    DWORD rep;
    DWORD stray;
    DWORD ok;
    HRESULT hr;
    HRESULT first_hr = 0;

    d32_line("Stage", "10c-t13-volume");
    d32_state(device, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    d32_state(device, D3DRENDERSTATE_ZENABLE, 0ul);
    d32_hx("T13ClearHr", (DWORD)d32_fill(back, DDBLT_COLORFILL, 0ul));

    d32_volume_list(D32_VOLUME_LIST_TOP, 0xff000000ul | list_rgb);
    d32_path_read(&before);
    for (rep = 0ul; rep < D32_VOLUME_REPS; ++rep) {
        hr = d32_draw(device, d32_volume_vertices, D32_VOLUME_VERTICES);
        if (hr != 0 && first_hr == 0) {
            first_hr = hr;
        }
    }
    d32_path_read(&after);
    d32_hx("T13ListHr", (DWORD)first_hr);
    d32_path_write("T13List", &before, &after);

    d32_line("Stage", "10d-t13-volume-indexed");
    first_hr = 0;
    d32_volume_indexed(D32_VOLUME_INDEXED_TOP, 0xff000000ul | indexed_rgb);
    d32_path_read(&before);
    for (rep = 0ul; rep < D32_VOLUME_REPS; ++rep) {
        hr = d32_draw_indexed(device, d32_volume_vertices,
                              D32_VOLUME_QUADS * 4ul, d32_volume_indices,
                              D32_VOLUME_VERTICES);
        if (hr != 0 && first_hr == 0) {
            first_hr = hr;
        }
    }
    d32_path_read(&after);
    d32_hx("T13IndexedHr", (DWORD)first_hr);
    d32_path_write("T13Indexed", &before, &after);

    d32_line("Stage", "10e-t13-volume-strip");
    first_hr = 0;
    d32_volume_strip(D32_VOLUME_STRIP_TOP, 0xff000000ul | strip_rgb);
    d32_path_read(&before);
    for (rep = 0ul; rep < D32_VOLUME_REPS; ++rep) {
        hr = d32_draw_type(device, D3DPT_TRIANGLESTRIP, d32_volume_vertices,
                           D32_VOLUME_VERTICES);
        if (hr != 0 && first_hr == 0) {
            first_hr = hr;
        }
    }
    d32_path_read(&after);
    d32_hx("T13StripHr", (DWORD)first_hr);
    d32_path_write("T13Strip", &before, &after);

    d32_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = ((d32_lock_fn)back->vtbl[DDS_LOCK])(back, 0, &desc, DDLOCK_WAIT, 0);
    d32_hx("T13LockHr", (DWORD)hr);
    if (hr != 0 || desc.lpSurface == 0) {
        return failures + 1ul;
    }
    ok = d32_volume_centres(&desc, D32_VOLUME_LIST_TOP, list_rgb,
                            d32_tolerance);
    d32_hx("T13ListCentres", ok);
    failures += ok != 15ul ? 1ul : 0ul;
    stray = d32_stray(&desc, D32_VOLUME_LIST_TOP,
                      D32_VOLUME_LIST_TOP + D32_VOLUME_ROWS_HEIGHT, list_rgb,
                      d32_tolerance);
    d32_hx("T13ListStray", stray);
    failures += stray != 0ul ? 1ul : 0ul;

    ok = d32_volume_centres(&desc, D32_VOLUME_INDEXED_TOP, indexed_rgb,
                            d32_tolerance);
    d32_hx("T13IndexedCentres", ok);
    failures += ok != 15ul ? 1ul : 0ul;
    stray = d32_stray(&desc, D32_VOLUME_INDEXED_TOP,
                      D32_VOLUME_INDEXED_TOP + D32_VOLUME_ROWS_HEIGHT,
                      indexed_rgb, d32_tolerance);
    d32_hx("T13IndexedStray", stray);
    failures += stray != 0ul ? 1ul : 0ul;

    ok = (d32_near(d32_pixel(&desc, 8ul, D32_VOLUME_STRIP_TOP + 12ul),
                   strip_rgb, d32_tolerance) ? 1ul : 0ul) |
         (d32_near(d32_pixel(&desc, 300ul, D32_VOLUME_STRIP_TOP + 12ul),
                   strip_rgb, d32_tolerance) ? 2ul : 0ul) |
         (d32_near(d32_pixel(&desc, 594ul, D32_VOLUME_STRIP_TOP + 12ul),
                   strip_rgb, d32_tolerance) ? 4ul : 0ul);
    d32_hx("T13StripSamples", ok);
    failures += ok != 7ul ? 1ul : 0ul;
    stray = d32_stray(&desc, D32_VOLUME_STRIP_TOP,
                      D32_VOLUME_STRIP_TOP + D32_VOLUME_STRIP_HEIGHT + 8ul,
                      strip_rgb, d32_tolerance);
    d32_hx("T13StripStray", stray);
    failures += stray != 0ul ? 1ul : 0ul;
    ((d32_unlock_fn)back->vtbl[DDS_UNLOCK])(back, desc.lpSurface);
    d32_line("T13Result", failures == 0ul ? "PASS" : "FAIL");
    return failures;
}

/*
 * T12, two texture stages through a DirectX 6 device (IDirect3D3,
 * IDirect3DDevice3; slots from d3d.h's declaration order), on the same
 * back buffer through its DirectDrawSurface4 interface. T6's quadrant
 * texture on stage 0, SELECTARG1; on stage 1 a lightmap - dark grey on
 * the left, mid grey on the right - with its own coordinate set, MODULATE
 * over one 64x64 quad and ADD over another. ValidateDevice is recorded;
 * a device that does not take two stages in one pass is recorded as
 * skipped, not failed, since the application then draws its own passes.
 * One that does, and whose HAL counters say no batch went on two units,
 * fails as not exercised: the colours could have come from anywhere.
 */
#define D3D3_CREATE_VIEWPORT 6
#define D3D3_CREATE_DEVICE 8
#define D3DDEV3_GET_CAPS 3
/* DirectX 6's D3DDEVICEDESC is 252 bytes; its last DWORD holds
 * wMaxTextureBlendStages (low) and wMaxSimultaneousTextures (high). */
#define D3DDESC_DX6_DWORDS 63
#define D3DDESC_STAGES 62
#define D3DDEV3_ADD_VIEWPORT 5
#define D3DDEV3_BEGIN_SCENE 9
#define D3DDEV3_END_SCENE 10
#define D3DDEV3_SET_CURRENT_VIEWPORT 12
#define D3DDEV3_SET_RENDER_STATE 22
#define D3DDEV3_DRAW_PRIMITIVE 28
#define D3DDEV3_SET_TEXTURE 38
#define D3DDEV3_SET_TEXTURE_STAGE_STATE 40
#define D3DDEV3_VALIDATE_DEVICE 41
/* XYZRHW | DIFFUSE | SPECULAR | TEX2. */
#define D32_FVF_TL2 0x000002c4ul
#define D32_TSS_COLOROP 1ul
#define D32_TSS_COLORARG1 2ul
#define D32_TSS_COLORARG2 3ul
#define D32_TSS_ALPHAOP 4ul
#define D32_TSS_ALPHAARG1 5ul
#define D32_TSS_ALPHAARG2 6ul
#define D32_TSS_TEXCOORDINDEX 11ul
#define D32_TSS_MAGFILTER 16ul
#define D32_TSS_MINFILTER 17ul
#define D32_TSS_MIPFILTER 18ul
#define D32_TOP_DISABLE 1ul
#define D32_TOP_SELECTARG1 2ul
#define D32_TOP_SELECTARG2 3ul
#define D32_TOP_MODULATE 4ul
#define D32_TOP_ADD 7ul
#define D32_TA_CURRENT 1ul
#define D32_TA_TEXTURE 2ul
#define D32_TFG_POINT 1ul
#define D32_TFP_NONE 1ul

typedef struct d32_tl2vertex {
    float sx;
    float sy;
    float sz;
    float rhw;
    DWORD color;
    DWORD specular;
    float tu0;
    float tv0;
    float tu1;
    float tv1;
} D32_TL2VERTEX;

typedef HRESULT (WINAPI *d32_create_device3_fn)(D32_OBJECT *, const GUID *,
                                                D32_OBJECT *, D32_OBJECT **,
                                                void *);
typedef HRESULT (WINAPI *d32_set_texture_fn)(D32_OBJECT *, DWORD,
                                             D32_OBJECT *);
typedef HRESULT (WINAPI *d32_stage_state_fn)(D32_OBJECT *, DWORD, DWORD,
                                             DWORD);
typedef HRESULT (WINAPI *d32_validate_fn)(D32_OBJECT *, DWORD *);
typedef HRESULT (WINAPI *d32_caps3_fn)(D32_OBJECT *, DWORD *, DWORD *);

static const GUID d32_iid_d3d3 = {
    0xbb223240ul, 0xe72b, 0x11d0,
    { 0xa9, 0xb4, 0x00, 0xaa, 0x00, 0xc0, 0x99, 0x3e }
};

static const GUID d32_iid_surface4 = {
    0x0b2b8630ul, 0xad35, 0x11d0,
    { 0x8e, 0xa6, 0x00, 0x60, 0x97, 0x97, 0xea, 0x5b }
};

/* The lightmap: 8 of 31 on the left, 16 of 31 on the right, opaque. */
static const WORD d32_lightmap[4] = { 0xa108u, 0xc210u, 0xa108u, 0xc210u };

static void d32_tss(D32_OBJECT *device, DWORD stage, DWORD state,
                    DWORD value)
{
    ((d32_stage_state_fn)device->vtbl[D3DDEV3_SET_TEXTURE_STAGE_STATE])(
        device, stage, state, value);
}

/* A 64x64 quad at (x, y), both coordinate sets 0 to 1 across it. */
static HRESULT d32_draw_two_set_quad(D32_OBJECT *device, float x, float y)
{
    static const float corners[6][2] = {
        { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f },
        { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }
    };
    D32_TL2VERTEX v[6];
    DWORD i;
    HRESULT hr;
    HRESULT draw_hr;

    for (i = 0ul; i < 6ul; ++i) {
        v[i].sx = x + corners[i][0] * 64.0f;
        v[i].sy = y + corners[i][1] * 64.0f;
        v[i].sz = 0.5f;
        v[i].rhw = 1.0f;
        v[i].color = 0xfffffffful;
        v[i].specular = 0xff000000ul;
        v[i].tu0 = corners[i][0];
        v[i].tv0 = corners[i][1];
        v[i].tu1 = corners[i][0];
        v[i].tv1 = corners[i][1];
    }
    hr = ((d32_scene_fn)device->vtbl[D3DDEV3_BEGIN_SCENE])(device);
    if (hr != 0) {
        return hr;
    }
    draw_hr = ((d32_draw_fn)device->vtbl[D3DDEV3_DRAW_PRIMITIVE])(
        device, D3DPT_TRIANGLELIST, D32_FVF_TL2, v, 6ul, 0ul);
    hr = ((d32_scene_fn)device->vtbl[D3DDEV3_END_SCENE])(device);
    return draw_hr != 0 ? draw_hr : hr;
}

/* Each quadrant's centre of the quad at (x, y) against rgb[4], within
 * tolerance: a bit a quadrant. */
static DWORD d32_quadrants_near(const D32_SURFACE_DESC *desc, DWORD x,
                                DWORD y, const DWORD rgb[4],
                                const char *prefix)
{
    static const DWORD offsets[4][2] = {
        { 16ul, 16ul }, { 48ul, 16ul }, { 16ul, 48ul }, { 48ul, 48ul }
    };
    DWORD point;
    DWORD ok = 0ul;
    char key[48];

    for (point = 0ul; point < 4ul; ++point) {
        DWORD pixel = d32_pixel(desc, x + offsets[point][0],
                                y + offsets[point][1]);

        wsprintfA(key, "%sPixel%lu", prefix, point);
        d32_hx(key, pixel);
        if (d32_near(pixel, rgb[point], 16ul)) {
            ok |= 1ul << point;
        }
    }
    return ok;
}

/*
 * T13's two-unit form, on T12's device with T12's textures bound: the 170
 * quads as one list of 1020 two-coordinate vertices, D32_VOLUME_REPS
 * times, MODULATE of the red quadrant by the dark lightmap - 0x420000 -
 * in rows 300 to 380. Two-unit vertices sent inline through PIO are what
 * locked the Rage 128; T12's quads never filled a packet.
 */
static D32_TL2VERTEX d32_volume2_vertices[D32_VOLUME_VERTICES];

static DWORD d32_two_stage_volume(D32_OBJECT *device3, D32_OBJECT *back)
{
    static const float corners[6][2] = {
        { 0.0f, 0.0f }, { 12.0f, 0.0f }, { 0.0f, 12.0f },
        { 12.0f, 0.0f }, { 12.0f, 12.0f }, { 0.0f, 12.0f }
    };
    D32_SURFACE_DESC desc;
    D32_PATH before;
    D32_PATH after;
    D32_TL2VERTEX *v;
    DWORD quad;
    DWORD corner;
    DWORD rep;
    DWORD ok;
    DWORD stray;
    DWORD failures = 0ul;
    float x;
    float y;
    HRESULT hr;
    HRESULT first_hr = 0;

    d32_line("Stage", "10f-t13-volume-two-unit");
    d32_tss(device3, 1ul, D32_TSS_COLOROP, D32_TOP_MODULATE);
    for (quad = 0ul; quad < D32_VOLUME_QUADS; ++quad) {
        d32_volume_corner(quad, 300ul, &x, &y);
        for (corner = 0ul; corner < 6ul; ++corner) {
            v = &d32_volume2_vertices[quad * 6ul + corner];
            v->sx = x + corners[corner][0];
            v->sy = y + corners[corner][1];
            v->sz = 0.5f;
            v->rhw = 1.0f;
            v->color = 0xfffffffful;
            v->specular = 0xff000000ul;
            /* The red quadrant and the lightmap's dark left half. */
            v->tu0 = 0.25f;
            v->tv0 = 0.25f;
            v->tu1 = 0.25f;
            v->tv1 = 0.25f;
        }
    }
    d32_path_read(&before);
    for (rep = 0ul; rep < D32_VOLUME_REPS; ++rep) {
        hr = ((d32_scene_fn)device3->vtbl[D3DDEV3_BEGIN_SCENE])(device3);
        if (hr == 0) {
            hr = ((d32_draw_fn)device3->vtbl[D3DDEV3_DRAW_PRIMITIVE])(
                device3, D3DPT_TRIANGLELIST, D32_FVF_TL2,
                d32_volume2_vertices, D32_VOLUME_VERTICES, 0ul);
            ((d32_scene_fn)device3->vtbl[D3DDEV3_END_SCENE])(device3);
        }
        if (hr != 0 && first_hr == 0) {
            first_hr = hr;
        }
    }
    d32_path_read(&after);
    d32_hx("T13TwoUnitHr", (DWORD)first_hr);
    d32_path_write("T13TwoUnit", &before, &after);

    d32_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = ((d32_lock_fn)back->vtbl[DDS_LOCK])(back, 0, &desc, DDLOCK_WAIT, 0);
    if (hr != 0 || desc.lpSurface == 0) {
        d32_hx("T13TwoUnitLockHr", (DWORD)hr);
        return 1ul;
    }
    ok = d32_volume_centres(&desc, 300ul, 0x00420000ul, 16ul);
    d32_hx("T13TwoUnitCentres", ok);
    failures += ok != 15ul ? 1ul : 0ul;
    stray = d32_stray(&desc, 300ul, 380ul, 0x00420000ul, 16ul);
    d32_hx("T13TwoUnitStray", stray);
    failures += stray != 0ul ? 1ul : 0ul;
    ((d32_unlock_fn)back->vtbl[DDS_UNLOCK])(back, desc.lpSurface);
    d32_line("T13TwoUnitResult", failures == 0ul ? "PASS" : "FAIL");
    return failures;
}

/*
 * T16, a DirectX 7 device (IDirectDraw7, IDirect3D7, IDirect3DDevice7;
 * slots from ddraw.h and d3d.h's declaration order) on an off-screen
 * target of its own - what the DirectX 7 driver interface (DDI
 * 7, docs\plans\hardware-tnl.md step 5) has to carry, and at DDI 6 what
 * the runtime does for a driver without it:
 *   a. the HAL device's caps: DRAWPRIMITIVES2EX and HWTRANSFORMANDLIGHT;
 *   b. a clear through the device, to dark blue (DDI 7: a CLEAR record);
 *   c. T6's quadrants in a managed texture made through IDirectDraw7
 *      (DDI 7: the texture manager's TEXBLT), SELECTARG1 over a 64x64 TL
 *      quad at (16, 120);
 *   d. an untransformed yellow quad, XYZ | DIFFUSE, lighting off, every
 *      matrix the identity, NDC x -0.5 to -0.3 and y 0.5 to 0.3: pixels
 *      160 to 224 by 120 to 168, transformed by the runtime;
 *   e. a state block made and applied (DDI 7: STATESET).
 * The pixels are read back; the driver's own account of what arrived is
 * C:\V9XDIAG\V9XD7.LOG.
 */
#define DD7_CREATE_SURFACE 6
#define DD7_SET_COOPERATIVE_LEVEL 20
#define D32_DDSCL_NORMAL 0x00000008ul
#define D3D7_CREATE_DEVICE 4
#define D3DDEV7_GET_CAPS 3
#define D3DDEV7_BEGIN_SCENE 5
#define D3DDEV7_END_SCENE 6
#define D3DDEV7_CLEAR 10
#define D3DDEV7_SET_TRANSFORM 11
#define D3DDEV7_SET_VIEWPORT 13
#define D3DDEV7_SET_RENDER_STATE 20
#define D3DDEV7_BEGIN_STATE_BLOCK 22
#define D3DDEV7_END_STATE_BLOCK 23
#define D3DDEV7_DRAW_PRIMITIVE 25
#define D3DDEV7_SET_TEXTURE 35
#define D3DDEV7_SET_TEXTURE_STAGE_STATE 37
#define D3DDEV7_APPLY_STATE_BLOCK 39
#define D3DDEV7_DELETE_STATE_BLOCK 41
/* DDSURFACEDESC2 is 124 bytes: the pixel format at DWORD 18, ddsCaps at
 * 26 and its dwCaps2 at 27. */
#define D32_DESC2_DWORDS 31
#define D32_DESC2_FORMAT 18
#define D32_DESC2_CAPS 26
#define D32_DESC2_CAPS2 27
#define D32_DDSCAPS2_TEXTUREMANAGE 0x00000010ul
#define D32_D3DDEVCAPS_DRAWPRIMITIVES2EX 0x00008000ul
#define D32_D3DDEVCAPS_HWTRANSFORMANDLIGHT 0x00010000ul
#define D32_D3DCLEAR_TARGET 0x00000001ul
#define D32_D3DRENDERSTATE_LIGHTING 137ul
#define D32_D3DTS_WORLD 1ul
#define D32_D3DTS_VIEW 2ul
#define D32_D3DTS_PROJECTION 3ul
/* D3DFVF_XYZ | DIFFUSE, D3DFVF_TLVERTEX. */
#define D32_FVF_XYZ_DIFFUSE 0x00000042ul
#define D32_FVF_TLVERTEX 0x000001c4ul
#define D32_D3DSBT_ALL 1ul

typedef struct d32_xyz_diffuse {
    float x;
    float y;
    float z;
    DWORD color;
} D32_XYZ_DIFFUSE;

typedef struct d32_viewport7 {
    DWORD dwX;
    DWORD dwY;
    DWORD dwWidth;
    DWORD dwHeight;
    float dvMinZ;
    float dvMaxZ;
} D32_VIEWPORT7;

typedef HRESULT (WINAPI *d32_create_surface7_fn)(D32_OBJECT *, DWORD *,
                                                 D32_OBJECT **, void *);
typedef HRESULT (WINAPI *d32_create_device7_fn)(D32_OBJECT *, const GUID *,
                                                D32_OBJECT *, D32_OBJECT **);
typedef HRESULT (WINAPI *d32_caps7_fn)(D32_OBJECT *, DWORD *);
typedef HRESULT (WINAPI *d32_clear7_fn)(D32_OBJECT *, DWORD, void *, DWORD,
                                        DWORD, float, DWORD);
typedef HRESULT (WINAPI *d32_transform7_fn)(D32_OBJECT *, DWORD, float *);
typedef HRESULT (WINAPI *d32_viewport7_fn)(D32_OBJECT *, D32_VIEWPORT7 *);
typedef HRESULT (WINAPI *d32_draw7_fn)(D32_OBJECT *, DWORD, DWORD, void *,
                                       DWORD, DWORD);
typedef HRESULT (WINAPI *d32_end_block7_fn)(D32_OBJECT *, DWORD *);
typedef HRESULT (WINAPI *d32_block7_fn)(D32_OBJECT *, DWORD);
typedef HRESULT (WINAPI *d32_lock7_fn)(D32_OBJECT *, RECT *, DWORD *, DWORD,
                                       void *);
typedef HRESULT (WINAPI *d32_unlock7_fn)(D32_OBJECT *, RECT *);
typedef HRESULT (WINAPI *d32_create_ex_fn)(GUID *, void **, const GUID *,
                                           void *);
typedef HRESULT (WINAPI *d32_cooperate_fn)(D32_OBJECT *, HWND, DWORD);

static const GUID d32_iid_ddraw7 = {
    0x15e65ec0ul, 0x3b9c, 0x11d2,
    { 0xb9, 0x2f, 0x00, 0x60, 0x97, 0x97, 0xea, 0x5b }
};

static const GUID d32_iid_d3d7 = {
    0xf5049e77ul, 0x4861, 0x11d2,
    { 0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8 }
};

static void d32_render_state7(D32_OBJECT *device, DWORD state, DWORD value)
{
    ((d32_render_state_fn)device->vtbl[D3DDEV7_SET_RENDER_STATE])(
        device, state, value);
}

/* T6's quadrants in an 8x8 1555 texture, made through IDirectDraw7 with
 * the texture manager's caps when `managed`. */
static D32_OBJECT *d32_texture7(D32_OBJECT *ddraw7, int managed)
{
    DWORD desc[D32_DESC2_DWORDS];
    D32_OBJECT *surface = 0;
    DWORD x;
    DWORD y;
    HRESULT hr;

    d32_zero(desc, sizeof(desc));
    desc[0] = sizeof(desc);
    desc[1] = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc[2] = 8ul;
    desc[3] = 8ul;
    desc[D32_DESC2_FORMAT] = 32ul;
    desc[D32_DESC2_FORMAT + 1] = DDPF_RGB | DDPF_ALPHAPIXELS;
    desc[D32_DESC2_FORMAT + 3] = 16ul;
    desc[D32_DESC2_FORMAT + 4] = 0x00007c00ul;
    desc[D32_DESC2_FORMAT + 5] = 0x000003e0ul;
    desc[D32_DESC2_FORMAT + 6] = 0x0000001ful;
    desc[D32_DESC2_FORMAT + 7] = 0x00008000ul;
    desc[D32_DESC2_CAPS] = DDSCAPS_TEXTURE |
                           (managed ? 0ul : DDSCAPS_VIDEOMEMORY);
    desc[D32_DESC2_CAPS2] = managed ? D32_DDSCAPS2_TEXTUREMANAGE : 0ul;
    hr = ((d32_create_surface7_fn)ddraw7->vtbl[DD7_CREATE_SURFACE])(
        ddraw7, desc, &surface, 0);
    d32_hx(managed ? "T16ManagedCreateHr" : "T16VideoCreateHr", (DWORD)hr);
    if (hr != 0 || surface == 0) {
        return 0;
    }
    d32_zero(desc, sizeof(desc));
    desc[0] = sizeof(desc);
    hr = ((d32_lock7_fn)surface->vtbl[DDS_LOCK])(surface, 0, desc,
                                                 DDLOCK_WAIT, 0);
    if (hr != 0 || desc[9] == 0ul) {
        d32_hx("T16TextureLockHr", (DWORD)hr);
        ((d32_release_fn)surface->vtbl[COM_RELEASE])(surface);
        return 0;
    }
    /* lPitch is DWORD 4, lpSurface DWORD 9. */
    for (y = 0ul; y < 8ul; ++y) {
        WORD *row = (WORD *)((BYTE *)desc[9] + y * desc[4]);

        for (x = 0ul; x < 8ul; ++x) {
            row[x] = d32_quadrants[(y >= 4ul ? 2u : 0u) +
                                   (x >= 4ul ? 1u : 0u)];
        }
    }
    ((d32_unlock7_fn)surface->vtbl[DDS_UNLOCK])(surface, 0);
    return surface;
}

static DWORD d32_dx7(D32_OBJECT *ddraw, D32_OBJECT *back)
{
    static const DWORD quadrants[4] = {
        0x00ff0000ul, 0x0000ff00ul, 0x000000fful, 0x00fffffful
    };
    static const float identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f
    };
    static const float corners[6][2] = {
        { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f },
        { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }
    };
    D32_OBJECT *ddraw7 = 0;
    D32_OBJECT *d3d7 = 0;
    D32_OBJECT *back7 = 0;
    D32_OBJECT *device7 = 0;
    D32_OBJECT *texture = 0;
    D32_SURFACE_DESC desc;
    DWORD desc2[D32_DESC2_DWORDS];
    d32_create_ex_fn create_ex;
    D32_VIEWPORT7 view;
    D32_TLVERTEX tl[6];
    D32_XYZ_DIFFUSE xyz[6];
    DWORD caps[64];
    DWORD block = 0ul;
    DWORD failures = 0ul;
    DWORD ok;
    DWORD i;
    HRESULT hr;
    float copy[16];

    /*
     * Direct3D 7 comes only from a DirectDraw object made by
     * DirectDrawCreateEx - a DirectDraw 1 object queried for
     * IDirectDraw7 answers E_NOINTERFACE for IDirect3D7 (the 4570, boot
     * 89) - so T16 has its own, at the normal cooperative level beside
     * the session's exclusive one, and draws into an off-screen target in
     * the display's format, 640x480, rather than the session's back
     * buffer.
     */
    (void)ddraw;
    (void)back;
    d32_line("Stage", "10g-t16-dx7");
    create_ex = (d32_create_ex_fn)GetProcAddress(
        GetModuleHandleA("DDRAW.DLL"), "DirectDrawCreateEx");
    hr = create_ex != 0 ? create_ex(0, (void **)&ddraw7, &d32_iid_ddraw7, 0)
                        : (HRESULT)0x80004002ul;
    d32_hx("T16DDraw7Hr", (DWORD)hr);
    if (hr == 0) {
        hr = ((d32_cooperate_fn)ddraw7->vtbl[DD7_SET_COOPERATIVE_LEVEL])(
            ddraw7, 0, D32_DDSCL_NORMAL);
        d32_hx("T16CooperateHr", (DWORD)hr);
    }
    if (hr == 0) {
        hr = ((d32_query_fn)ddraw7->vtbl[COM_QUERY_INTERFACE])(
            ddraw7, &d32_iid_d3d7, (void **)&d3d7);
        d32_hx("T16D3D7Hr", (DWORD)hr);
    }
    if (hr != 0) {
        d32_line("T16Result", "SKIPPED-NO-D3D7");
        goto release;
    }
    d32_zero(desc2, sizeof(desc2));
    desc2[0] = sizeof(desc2);
    desc2[1] = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc2[2] = D32_HEIGHT;
    desc2[3] = D32_WIDTH;
    desc2[D32_DESC2_CAPS] = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE |
                            DDSCAPS_VIDEOMEMORY;
    hr = ((d32_create_surface7_fn)ddraw7->vtbl[DD7_CREATE_SURFACE])(
        ddraw7, desc2, &back7, 0);
    d32_hx("T16TargetHr", (DWORD)hr);
    if (hr == 0) {
        hr = ((d32_create_device7_fn)d3d7->vtbl[D3D7_CREATE_DEVICE])(
            d3d7, &d32_iid_d3d_hal, back7, &device7);
        d32_hx("T16CreateDeviceHr", (DWORD)hr);
    }
    if (hr != 0 || device7 == 0) {
        d32_line("T16Result", "NO-DEVICE7");
        ++failures;
        goto release;
    }

    /* a. */
    d32_zero(caps, sizeof(caps));
    hr = ((d32_caps7_fn)device7->vtbl[D3DDEV7_GET_CAPS])(device7, caps);
    d32_hx("T16CapsHr", (DWORD)hr);
    d32_hx("T16DevCaps", caps[0]);
    d32_hx("T16DrawPrimitives2Ex",
           (caps[0] & D32_D3DDEVCAPS_DRAWPRIMITIVES2EX) != 0ul);
    d32_hx("T16HwTransformAndLight",
           (caps[0] & D32_D3DDEVCAPS_HWTRANSFORMANDLIGHT) != 0ul);

    /* b. */
    hr = ((d32_clear7_fn)device7->vtbl[D3DDEV7_CLEAR])(
        device7, 0ul, 0, D32_D3DCLEAR_TARGET, 0xff000040ul, 1.0f, 0ul);
    d32_hx("T16ClearHr", (DWORD)hr);

    view.dwX = 0ul;
    view.dwY = 0ul;
    view.dwWidth = D32_WIDTH;
    view.dwHeight = D32_HEIGHT;
    view.dvMinZ = 0.0f;
    view.dvMaxZ = 1.0f;
    d32_hx("T16ViewportHr",
           (DWORD)((d32_viewport7_fn)device7->vtbl[D3DDEV7_SET_VIEWPORT])(
               device7, &view));
    d32_render_state7(device7, D3DRENDERSTATE_ZENABLE, 0ul);
    d32_render_state7(device7, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    d32_render_state7(device7, D3DRENDERSTATE_DITHERENABLE, 0ul);
    d32_render_state7(device7, D32_D3DRENDERSTATE_LIGHTING, 0ul);

    /* c. */
    texture = d32_texture7(ddraw7, 1);
    if (texture == 0) {
        texture = d32_texture7(ddraw7, 0);
    }
    d32_hx("T16SetTextureHr",
           (DWORD)((d32_set_texture_fn)device7->vtbl[D3DDEV7_SET_TEXTURE])(
               device7, 0ul, texture));
    ((d32_stage_state_fn)device7->vtbl[D3DDEV7_SET_TEXTURE_STAGE_STATE])(
        device7, 0ul, D32_TSS_COLOROP, D32_TOP_SELECTARG1);
    ((d32_stage_state_fn)device7->vtbl[D3DDEV7_SET_TEXTURE_STAGE_STATE])(
        device7, 0ul, D32_TSS_COLORARG1, D32_TA_TEXTURE);
    ((d32_stage_state_fn)device7->vtbl[D3DDEV7_SET_TEXTURE_STAGE_STATE])(
        device7, 0ul, D32_TSS_MAGFILTER, D32_TFG_POINT);
    ((d32_stage_state_fn)device7->vtbl[D3DDEV7_SET_TEXTURE_STAGE_STATE])(
        device7, 0ul, D32_TSS_MINFILTER, D32_TFG_POINT);
    for (i = 0ul; i < 6ul; ++i) {
        tl[i].sx = 16.0f + corners[i][0] * 64.0f;
        tl[i].sy = 120.0f + corners[i][1] * 64.0f;
        tl[i].sz = 0.5f;
        tl[i].rhw = 1.0f;
        tl[i].color = 0xfffffffful;
        tl[i].specular = 0xff000000ul;
        tl[i].tu = corners[i][0];
        tl[i].tv = corners[i][1];
    }
    hr = ((d32_scene_fn)device7->vtbl[D3DDEV7_BEGIN_SCENE])(device7);
    if (hr == 0) {
        hr = ((d32_draw7_fn)device7->vtbl[D3DDEV7_DRAW_PRIMITIVE])(
            device7, D3DPT_TRIANGLELIST, D32_FVF_TLVERTEX, tl, 6ul, 0ul);
        ((d32_scene_fn)device7->vtbl[D3DDEV7_END_SCENE])(device7);
    }
    d32_hx("T16TexturedHr", (DWORD)hr);

    /* d. */
    ((d32_set_texture_fn)device7->vtbl[D3DDEV7_SET_TEXTURE])(device7, 0ul,
                                                             0);
    for (i = 0ul; i < 16ul; ++i) {
        copy[i] = identity[i];
    }
    ((d32_transform7_fn)device7->vtbl[D3DDEV7_SET_TRANSFORM])(
        device7, D32_D3DTS_WORLD, copy);
    ((d32_transform7_fn)device7->vtbl[D3DDEV7_SET_TRANSFORM])(
        device7, D32_D3DTS_VIEW, copy);
    hr = ((d32_transform7_fn)device7->vtbl[D3DDEV7_SET_TRANSFORM])(
        device7, D32_D3DTS_PROJECTION, copy);
    d32_hx("T16TransformHr", (DWORD)hr);
    for (i = 0ul; i < 6ul; ++i) {
        xyz[i].x = -0.5f + corners[i][0] * 0.2f;
        xyz[i].y = 0.5f - corners[i][1] * 0.2f;
        xyz[i].z = 0.5f;
        xyz[i].color = 0xffffff00ul;
    }
    hr = ((d32_scene_fn)device7->vtbl[D3DDEV7_BEGIN_SCENE])(device7);
    if (hr == 0) {
        hr = ((d32_draw7_fn)device7->vtbl[D3DDEV7_DRAW_PRIMITIVE])(
            device7, D3DPT_TRIANGLELIST, D32_FVF_XYZ_DIFFUSE, xyz, 6ul, 0ul);
        ((d32_scene_fn)device7->vtbl[D3DDEV7_END_SCENE])(device7);
    }
    d32_hx("T16UntransformedHr", (DWORD)hr);

    /* e. */
    hr = ((d32_scene_fn)device7->vtbl[D3DDEV7_BEGIN_STATE_BLOCK])(device7);
    if (hr == 0) {
        d32_render_state7(device7, D3DRENDERSTATE_DITHERENABLE, 0ul);
        hr = ((d32_end_block7_fn)device7->vtbl[D3DDEV7_END_STATE_BLOCK])(
            device7, &block);
    }
    if (hr == 0) {
        hr = ((d32_block7_fn)device7->vtbl[D3DDEV7_APPLY_STATE_BLOCK])(
            device7, block);
        ((d32_block7_fn)device7->vtbl[D3DDEV7_DELETE_STATE_BLOCK])(device7,
                                                                  block);
    }
    d32_hx("T16StateBlockHr", (DWORD)hr);

    /* The target through DDSURFACEDESC2: lPitch is DWORD 4, lpSurface 9. */
    d32_zero(desc2, sizeof(desc2));
    desc2[0] = sizeof(desc2);
    hr = ((d32_lock7_fn)back7->vtbl[DDS_LOCK])(back7, 0, desc2, DDLOCK_WAIT,
                                               0);
    d32_hx("T16LockHr", (DWORD)hr);
    if (hr != 0 || desc2[9] == 0ul) {
        ++failures;
        goto release;
    }
    d32_zero(&desc, sizeof(desc));
    desc.lPitch = (LONG)desc2[4];
    desc.lpSurface = (void *)desc2[9];
    ok = d32_quadrants_near(&desc, 16ul, 120ul, quadrants, "T16Texture");
    d32_hx("T16TextureOk", ok);
    failures += ok != 0xful ? 1ul : 0ul;
    i = d32_pixel(&desc, 192ul, 144ul);
    d32_hx("T16UntransformedPixel", i);
    failures += d32_near(i, 0x00ffff00ul, 16ul) ? 0ul : 1ul;
    i = d32_pixel(&desc, 400ul, 400ul);
    d32_hx("T16ClearPixel", i);
    failures += d32_near(i, 0x00000040ul, 16ul) ? 0ul : 1ul;
    ((d32_unlock7_fn)back7->vtbl[DDS_UNLOCK])(back7, 0);
    d32_line("T16Result", failures == 0ul ? "PASS" : "FAIL");

release:
    if (texture != 0) {
        ((d32_release_fn)texture->vtbl[COM_RELEASE])(texture);
    }
    if (device7 != 0) {
        ((d32_release_fn)device7->vtbl[COM_RELEASE])(device7);
    }
    if (back7 != 0) {
        ((d32_release_fn)back7->vtbl[COM_RELEASE])(back7);
    }
    if (d3d7 != 0) {
        ((d32_release_fn)d3d7->vtbl[COM_RELEASE])(d3d7);
    }
    if (ddraw7 != 0) {
        ((d32_release_fn)ddraw7->vtbl[COM_RELEASE])(ddraw7);
    }
    return failures;
}

/*
 * T18, the T&L device (docs\plans\hardware-tnl.md step 6's gate): the same
 * lit scenes drawn twice, once on the HAL device - which the runtime
 * transforms and lights itself, sending pre-transformed vertices - and
 * once on the T&L HAL device, which sends them untransformed for this
 * driver's vertex kernels; each into its own 640x480 target, the centres
 * of twelve 16x16 quads read back from both and compared.
 *   1. a coloured directional light with specular, an infinite viewer;
 *   2. an attenuated point light and a spot, a local viewer, specular;
 *   3. vertex diffuse as the diffuse and ambient source, two-length
 *      normals renormalised, linear vertex fog to blue.
 * Every matrix is the identity, so object, eye and clip space are one and
 * the quads sit at z 0.5. Skipped when there is no T&L device.
 */
#define D3DDEV7_SET_MATERIAL 16
#define D3DDEV7_SET_LIGHT 18
#define D3DDEV7_LIGHT_ENABLE 44
#define D32_T18_QUADS 12ul
#define D32_T18_TOLERANCE 12ul
#define D32_RS_SPECULARENABLE 29ul
#define D32_RS_FOGENABLE 28ul
#define D32_RS_FOGCOLOR 34ul
#define D32_RS_FOGSTART 36ul
#define D32_RS_FOGEND 37ul
#define D32_RS_AMBIENT 139ul
#define D32_RS_FOGVERTEXMODE 140ul
#define D32_RS_LOCALVIEWER 142ul
#define D32_RS_NORMALIZENORMALS 143ul
#define D32_RS_DIFFUSEMATERIALSOURCE 145ul
#define D32_RS_AMBIENTMATERIALSOURCE 147ul
/* D3DFVF_XYZ | NORMAL and | DIFFUSE. */
#define D32_FVF_XYZ_NORMAL 0x00000012ul
#define D32_FVF_XYZ_NORMAL_DIFFUSE 0x00000052ul

typedef struct d32_light7 {
    DWORD type;
    float diffuse[4];
    float specular[4];
    float ambient[4];
    float position[3];
    float direction[3];
    float range;
    float falloff;
    float attenuation[3];
    float theta;
    float phi;
} D32_LIGHT7;

typedef struct d32_material7 {
    float diffuse[4];
    float ambient[4];
    float specular[4];
    float emissive[4];
    float power;
} D32_MATERIAL7;

typedef struct d32_lit_vertex {
    float x;
    float y;
    float z;
    float nx;
    float ny;
    float nz;
    DWORD color;
} D32_LIT_VERTEX;

typedef HRESULT (WINAPI *d32_material7_fn)(D32_OBJECT *, D32_MATERIAL7 *);
typedef HRESULT (WINAPI *d32_light7_fn)(D32_OBJECT *, DWORD, D32_LIGHT7 *);
typedef HRESULT (WINAPI *d32_light_enable7_fn)(D32_OBJECT *, DWORD, BOOL);

static const GUID d32_iid_d3d_tnl_hal = {
    0xf5049e78ul, 0x4861, 0x11d2,
    { 0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8 }
};

static void d32_set4(float *out, float x, float y, float z, float w)
{
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = w;
}

static DWORD d32_float_bits(float value)
{
    union {
        float value;
        DWORD bits;
    } u;

    u.value = value;
    return u.bits;
}

/* Quad q's screen centre: four columns of three rows. */
static DWORD d32_t18_x(DWORD q)
{
    return 80ul + 160ul * (q % 4ul);
}

static DWORD d32_t18_y(DWORD q)
{
    return 80ul + 160ul * (q / 4ul);
}

/* One scene's state and quads on a device. */
static HRESULT d32_t18_scene(D32_OBJECT *device, DWORD scene)
{
    static const float normals[D32_T18_QUADS][3] = {
        { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.6f, -0.8f }, { 0.6f, 0.0f, -0.8f },
        { -0.6f, 0.0f, -0.8f }, { 0.0f, -0.6f, -0.8f }, { 0.48f, 0.6f, -0.64f },
        { -0.48f, -0.6f, -0.64f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f },
        { 0.36f, -0.48f, -0.8f }, { -0.8f, 0.0f, -0.6f }, { 0.0f, 0.8f, -0.6f }
    };
    static const float identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f
    };
    static const float corners[6][2] = {
        { -1.0f, -1.0f }, { 1.0f, -1.0f }, { -1.0f, 1.0f },
        { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f }
    };
    D32_LIT_VERTEX v[D32_T18_QUADS * 6ul];
    D32_MATERIAL7 material;
    D32_LIGHT7 light;
    D32_VIEWPORT7 view;
    float copy[16];
    DWORD q;
    DWORD i;
    float scale = scene == 3ul ? 2.0f : 1.0f;
    HRESULT hr;

    view.dwX = 0ul;
    view.dwY = 0ul;
    view.dwWidth = D32_WIDTH;
    view.dwHeight = D32_HEIGHT;
    view.dvMinZ = 0.0f;
    view.dvMaxZ = 1.0f;
    ((d32_viewport7_fn)device->vtbl[D3DDEV7_SET_VIEWPORT])(device, &view);
    for (i = 0ul; i < 16ul; ++i) {
        copy[i] = identity[i];
    }
    ((d32_transform7_fn)device->vtbl[D3DDEV7_SET_TRANSFORM])(
        device, D32_D3DTS_WORLD, copy);
    ((d32_transform7_fn)device->vtbl[D3DDEV7_SET_TRANSFORM])(
        device, D32_D3DTS_VIEW, copy);
    ((d32_transform7_fn)device->vtbl[D3DDEV7_SET_TRANSFORM])(
        device, D32_D3DTS_PROJECTION, copy);
    d32_render_state7(device, D3DRENDERSTATE_ZENABLE, 0ul);
    d32_render_state7(device, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    d32_render_state7(device, D3DRENDERSTATE_DITHERENABLE, 0ul);
    d32_render_state7(device, D32_D3DRENDERSTATE_LIGHTING, 1ul);
    d32_render_state7(device, D32_RS_SPECULARENABLE, scene != 3ul);
    d32_render_state7(device, D32_RS_LOCALVIEWER, scene == 2ul);
    d32_render_state7(device, D32_RS_NORMALIZENORMALS, scene == 3ul);
    d32_render_state7(device, D32_RS_AMBIENT, 0x00202020ul);
    d32_render_state7(device, D32_RS_DIFFUSEMATERIALSOURCE,
                      scene == 3ul ? 1ul : 0ul);
    d32_render_state7(device, D32_RS_AMBIENTMATERIALSOURCE,
                      scene == 3ul ? 1ul : 0ul);
    d32_render_state7(device, D32_RS_FOGENABLE, scene == 3ul);
    d32_render_state7(device, D32_RS_FOGCOLOR, 0x000000fful);
    d32_render_state7(device, D32_RS_FOGVERTEXMODE, scene == 3ul ? 3ul : 0ul);
    d32_render_state7(device, D32_RS_FOGSTART, d32_float_bits(0.0f));
    d32_render_state7(device, D32_RS_FOGEND, d32_float_bits(1.5f));
    ((d32_set_texture_fn)device->vtbl[D3DDEV7_SET_TEXTURE])(device, 0ul, 0);

    d32_zero(&material, sizeof(material));
    d32_set4(material.diffuse, 0.8f, 0.8f, 0.8f, 1.0f);
    d32_set4(material.ambient, 1.0f, 1.0f, 1.0f, 1.0f);
    d32_set4(material.specular, 0.6f, 0.6f, 0.6f, 1.0f);
    material.power = scene == 2ul ? 16.0f : 8.0f;
    ((d32_material7_fn)device->vtbl[D3DDEV7_SET_MATERIAL])(device, &material);

    d32_zero(&light, sizeof(light));
    light.type = 3ul;                       /* directional */
    d32_set4(light.diffuse, 1.0f, 0.5f, 0.25f, 1.0f);
    d32_set4(light.specular, 1.0f, 1.0f, 1.0f, 1.0f);
    d32_set4(light.ambient, 0.1f, 0.1f, 0.1f, 1.0f);
    light.direction[2] = 1.0f;
    if (scene == 3ul) {
        light.direction[0] = 0.3f;
        light.direction[1] = -0.3f;
    }
    light.range = 1000.0f;
    light.attenuation[0] = 1.0f;
    ((d32_light7_fn)device->vtbl[D3DDEV7_SET_LIGHT])(device, 0ul, &light);
    ((d32_light_enable7_fn)device->vtbl[D3DDEV7_LIGHT_ENABLE])(
        device, 0ul, scene != 2ul);
    if (scene == 2ul) {
        light.type = 1ul;                   /* point at the eye */
        d32_set4(light.diffuse, 0.0f, 1.0f, 0.5f, 1.0f);
        light.attenuation[0] = 0.3f;
        light.attenuation[1] = 0.5f;
        light.attenuation[2] = 0.2f;
        light.range = 100.0f;
        ((d32_light7_fn)device->vtbl[D3DDEV7_SET_LIGHT])(device, 1ul, &light);
        light.type = 2ul;                   /* a spot down +z */
        d32_set4(light.diffuse, 1.0f, 1.0f, 0.0f, 1.0f);
        light.position[0] = 0.3f;
        light.position[1] = 0.2f;
        light.attenuation[0] = 1.0f;
        light.attenuation[1] = 0.0f;
        light.attenuation[2] = 0.0f;
        light.theta = 0.6f;
        light.phi = 1.2f;
        light.falloff = 1.0f;
        ((d32_light7_fn)device->vtbl[D3DDEV7_SET_LIGHT])(device, 2ul, &light);
    }
    ((d32_light_enable7_fn)device->vtbl[D3DDEV7_LIGHT_ENABLE])(
        device, 1ul, scene == 2ul);
    ((d32_light_enable7_fn)device->vtbl[D3DDEV7_LIGHT_ENABLE])(
        device, 2ul, scene == 2ul);

    for (q = 0ul; q < D32_T18_QUADS; ++q) {
        float cx = (float)(long)d32_t18_x(q) / 320.0f - 1.0f;
        float cy = 1.0f - (float)(long)d32_t18_y(q) / 240.0f;

        for (i = 0ul; i < 6ul; ++i) {
            D32_LIT_VERTEX *p = &v[q * 6ul + i];

            p->x = cx + corners[i][0] * (8.0f / 320.0f);
            p->y = cy + corners[i][1] * (8.0f / 240.0f);
            p->z = 0.5f;
            p->nx = normals[q][0] * scale;
            p->ny = normals[q][1] * scale;
            p->nz = normals[q][2] * scale;
            p->color = 0xff000000ul | ((q * 0x15ul & 0xfful) << 16) |
                       ((0xfful - q * 0x11ul) << 8) | (0x40ul + q * 0x0cul);
        }
    }
    hr = ((d32_scene_fn)device->vtbl[D3DDEV7_BEGIN_SCENE])(device);
    if (hr == 0) {
        /* The same 28-byte vertices for both formats: without DIFFUSE the
         * colour is past the stride's end, so they are drawn as two
         * formats of two strides. */
        if (scene == 3ul) {
            hr = ((d32_draw7_fn)device->vtbl[D3DDEV7_DRAW_PRIMITIVE])(
                device, D3DPT_TRIANGLELIST, D32_FVF_XYZ_NORMAL_DIFFUSE, v,
                D32_T18_QUADS * 6ul, 0ul);
        } else {
            static float plain[D32_T18_QUADS * 6ul * 6ul];

            for (i = 0ul; i < D32_T18_QUADS * 6ul; ++i) {
                plain[i * 6ul] = v[i].x;
                plain[i * 6ul + 1ul] = v[i].y;
                plain[i * 6ul + 2ul] = v[i].z;
                plain[i * 6ul + 3ul] = v[i].nx;
                plain[i * 6ul + 4ul] = v[i].ny;
                plain[i * 6ul + 5ul] = v[i].nz;
            }
            hr = ((d32_draw7_fn)device->vtbl[D3DDEV7_DRAW_PRIMITIVE])(
                device, D3DPT_TRIANGLELIST, D32_FVF_XYZ_NORMAL, plain,
                D32_T18_QUADS * 6ul, 0ul);
        }
        ((d32_scene_fn)device->vtbl[D3DDEV7_END_SCENE])(device);
    }
    return hr;
}

static DWORD d32_t18_channel_difference(DWORD a, DWORD b)
{
    DWORD worst = 0ul;
    DWORD k;

    for (k = 0ul; k < 3ul; ++k) {
        DWORD d = d32_difference(d32_channel(a, d32_masks[k]),
                                 d32_channel(b, d32_masks[k]));

        if (d > worst) {
            worst = d;
        }
    }
    return worst;
}

static DWORD d32_tnl(void)
{
    static const char *const names[4] = { "", "Dir", "Pos", "ColourFog" };
    D32_OBJECT *ddraw7 = 0;
    D32_OBJECT *d3d7 = 0;
    D32_OBJECT *target[2] = { 0, 0 };
    D32_OBJECT *device[2] = { 0, 0 };
    DWORD desc2[D32_DESC2_DWORDS];
    DWORD pixels[2][D32_T18_QUADS];
    d32_create_ex_fn create_ex;
    DWORD caps[64];
    DWORD failures = 0ul;
    DWORD worst = 0ul;
    DWORD scene;
    DWORD d;
    DWORD q;
    HRESULT hr;
    char key[32];
    char value[64];

    d32_line("Stage", "10h-t18-tnl");
    create_ex = (d32_create_ex_fn)GetProcAddress(
        GetModuleHandleA("DDRAW.DLL"), "DirectDrawCreateEx");
    hr = create_ex != 0 ? create_ex(0, (void **)&ddraw7, &d32_iid_ddraw7, 0)
                        : (HRESULT)0x80004002ul;
    if (hr == 0) {
        hr = ((d32_cooperate_fn)ddraw7->vtbl[DD7_SET_COOPERATIVE_LEVEL])(
            ddraw7, 0, D32_DDSCL_NORMAL);
    }
    if (hr == 0) {
        hr = ((d32_query_fn)ddraw7->vtbl[COM_QUERY_INTERFACE])(
            ddraw7, &d32_iid_d3d7, (void **)&d3d7);
    }
    if (hr != 0) {
        d32_line("T18Result", "SKIPPED-NO-D3D7");
        goto release;
    }
    for (d = 0ul; d < 2ul; ++d) {
        d32_zero(desc2, sizeof(desc2));
        desc2[0] = sizeof(desc2);
        desc2[1] = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
        desc2[2] = D32_HEIGHT;
        desc2[3] = D32_WIDTH;
        desc2[D32_DESC2_CAPS] = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE |
                                DDSCAPS_VIDEOMEMORY;
        hr = ((d32_create_surface7_fn)ddraw7->vtbl[DD7_CREATE_SURFACE])(
            ddraw7, desc2, &target[d], 0);
        if (hr == 0) {
            hr = ((d32_create_device7_fn)d3d7->vtbl[D3D7_CREATE_DEVICE])(
                d3d7, d == 0ul ? &d32_iid_d3d_hal : &d32_iid_d3d_tnl_hal,
                target[d], &device[d]);
        }
        d32_hx(d == 0ul ? "T18HalDeviceHr" : "T18TnlDeviceHr", (DWORD)hr);
        if (hr != 0 || device[d] == 0) {
            d32_line("T18Result", d == 0ul ? "NO-HAL-DEVICE" : "NO-TNL-DEVICE");
            if (d == 0ul) {
                ++failures;
            }
            goto release;
        }
    }
    d32_zero(caps, sizeof(caps));
    ((d32_caps7_fn)device[1]->vtbl[D3DDEV7_GET_CAPS])(device[1], caps);
    d32_hx("T18TnlDevCaps", caps[0]);

    for (scene = 1ul; scene <= 3ul; ++scene) {
        for (d = 0ul; d < 2ul; ++d) {
            ((d32_clear7_fn)device[d]->vtbl[D3DDEV7_CLEAR])(
                device[d], 0ul, 0, D32_D3DCLEAR_TARGET, 0xff000000ul, 1.0f,
                0ul);
            hr = d32_t18_scene(device[d], scene);
            wsprintfA(key, "T18%s%sHr", names[scene],
                      d == 0ul ? "Hal" : "Tnl");
            d32_hx(key, (DWORD)hr);
            d32_zero(desc2, sizeof(desc2));
            desc2[0] = sizeof(desc2);
            hr = ((d32_lock7_fn)target[d]->vtbl[DDS_LOCK])(
                target[d], 0, desc2, DDLOCK_WAIT, 0);
            if (hr != 0 || desc2[9] == 0ul) {
                d32_hx("T18LockHr", (DWORD)hr);
                ++failures;
                goto release;
            }
            {
                D32_SURFACE_DESC desc;

                d32_zero(&desc, sizeof(desc));
                desc.lPitch = (LONG)desc2[4];
                desc.lpSurface = (void *)desc2[9];
                for (q = 0ul; q < D32_T18_QUADS; ++q) {
                    pixels[d][q] = d32_pixel(&desc, d32_t18_x(q),
                                             d32_t18_y(q));
                }
            }
            ((d32_unlock7_fn)target[d]->vtbl[DDS_UNLOCK])(target[d], 0);
        }
        for (q = 0ul; q < D32_T18_QUADS; ++q) {
            DWORD diff = d32_t18_channel_difference(pixels[0][q],
                                                    pixels[1][q]);

            if (diff > worst) {
                worst = diff;
            }
            wsprintfA(key, "T18%s%lu", names[scene], q);
            wsprintfA(value, "tnl %06lX hal %06lX diff %lu %s",
                      pixels[1][q], pixels[0][q], diff,
                      diff <= D32_T18_TOLERANCE ? "ok" : "off");
            d32_line(key, value);
            if (diff > D32_T18_TOLERANCE) {
                ++failures;
            }
        }
    }
    d32_hx("T18WorstChannel", worst);
    d32_line("T18Result", failures == 0ul ? "PASS" : "FAIL");

release:
    for (d = 0ul; d < 2ul; ++d) {
        if (device[d] != 0) {
            ((d32_release_fn)device[d]->vtbl[COM_RELEASE])(device[d]);
        }
        if (target[d] != 0) {
            ((d32_release_fn)target[d]->vtbl[COM_RELEASE])(target[d]);
        }
    }
    if (d3d7 != 0) {
        ((d32_release_fn)d3d7->vtbl[COM_RELEASE])(d3d7);
    }
    if (ddraw7 != 0) {
        ((d32_release_fn)ddraw7->vtbl[COM_RELEASE])(ddraw7);
    }
    return failures;
}

/*
 * T17, recorded and not judged: whether the Direct3D 8 and 9 runtimes see
 * a hardware device on this driver at this DDI level (the plan's step 5
 * asks it of DDI 7). Each runtime is loaded, made (SDK versions 220 and
 * 32) and asked for the HAL device's caps on adapter 0; the answer and
 * DevCaps (the eighth DWORD of D3DCAPS8 and D3DCAPS9) are written.
 * IDirect3D8::GetDeviceCaps is slot 13; IDirect3D9 inserts
 * CheckDeviceFormatConversion before it, slot 14.
 */
#define D32_D3DDEVTYPE_HAL 1ul

typedef void *(WINAPI *d32_create_d3dx_fn)(UINT);
typedef HRESULT (WINAPI *d32_device_caps_fn)(D32_OBJECT *, UINT, DWORD,
                                             DWORD *);

static void d32_d3dx_caps(const char *dll, const char *entry, UINT sdk,
                          DWORD slot, const char *prefix)
{
    static DWORD caps[512];
    HMODULE module = LoadLibraryA(dll);
    d32_create_d3dx_fn create = module != 0
        ? (d32_create_d3dx_fn)GetProcAddress(module, entry) : 0;
    D32_OBJECT *d3d = create != 0 ? (D32_OBJECT *)create(sdk) : 0;
    HRESULT hr;
    char key[48];

    wsprintfA(key, "%sCreated", prefix);
    d32_hx(key, d3d != 0);
    if (d3d == 0) {
        return;
    }
    d32_zero(caps, sizeof(caps));
    hr = ((d32_device_caps_fn)d3d->vtbl[slot])(d3d, 0u, D32_D3DDEVTYPE_HAL,
                                              caps);
    wsprintfA(key, "%sHalCapsHr", prefix);
    d32_hx(key, (DWORD)hr);
    wsprintfA(key, "%sHalDevCaps", prefix);
    d32_hx(key, caps[7]);
    ((d32_release_fn)d3d->vtbl[COM_RELEASE])(d3d);
}

static void d32_d3d89(void)
{
    d32_line("Stage", "10h-t17-d3d8-d3d9");
    d32_d3dx_caps("D3D8.DLL", "Direct3DCreate8", 220u, 13ul, "T17D3D8");
    d32_d3dx_caps("D3D9.DLL", "Direct3DCreate9", 32u, 14ul, "T17D3D9");
}

static DWORD d32_two_stage(D32_OBJECT *ddraw, D32_OBJECT *back,
                           D32_OBJECT *base_surface)
{
    /* SELECTARG1 of the quadrants, then the lightmap: 0x42 on the left
     * and 0x84 on the right, multiplied or added. */
    static const DWORD modulated[4] = {
        0x00420000ul, 0x00008400ul, 0x00000042ul, 0x00848484ul
    };
    static const DWORD added[4] = {
        0x00ff4242ul, 0x0084ff84ul, 0x004242fful, 0x00fffffful
    };
    D32_OBJECT *d3d3 = 0;
    D32_OBJECT *back4 = 0;
    D32_OBJECT *device3 = 0;
    D32_OBJECT *viewport3 = 0;
    D32_OBJECT *lightmap = 0;
    D32_OBJECT *texture0 = 0;
    D32_OBJECT *texture1 = 0;
    D32_VIEWPORT2 view;
    D32_SURFACE_DESC desc;
    D32_PATH before;
    D32_PATH after;
    DWORD hal_caps[D3DDESC_DX6_DWORDS];
    DWORD hel_caps[D3DDESC_DX6_DWORDS];
    DWORD passes = 0ul;
    DWORD failures = 0ul;
    DWORD ok;
    HRESULT hr;

    d32_line("Stage", "10b-t12-two-stages");
    lightmap = d32_texture_surface(ddraw, d32_lightmap, "T12Lightmap");
    hr = ((d32_query_fn)ddraw->vtbl[COM_QUERY_INTERFACE])(
        ddraw, &d32_iid_d3d3, (void **)&d3d3);
    d32_hx("T12D3D3Hr", (DWORD)hr);
    if (hr == 0) {
        hr = ((d32_query_fn)back->vtbl[COM_QUERY_INTERFACE])(
            back, &d32_iid_surface4, (void **)&back4);
        d32_hx("T12Surface4Hr", (DWORD)hr);
    }
    if (hr == 0) {
        hr = ((d32_create_device3_fn)d3d3->vtbl[D3D3_CREATE_DEVICE])(
            d3d3, &d32_iid_d3d_hal, back4, &device3, 0);
        d32_hx("T12CreateDeviceHr", (DWORD)hr);
    }
    if (hr == 0) {
        hr = ((d32_create_viewport_fn)d3d3->vtbl[D3D3_CREATE_VIEWPORT])(
            d3d3, &viewport3, 0);
    }
    if (hr == 0) {
        hr = ((d32_device_viewport_fn)device3->vtbl[D3DDEV3_ADD_VIEWPORT])(
            device3, viewport3);
    }
    if (hr == 0) {
        d32_zero(&view, sizeof(view));
        view.dwSize = sizeof(view);
        view.dwWidth = D32_WIDTH;
        view.dwHeight = D32_HEIGHT;
        view.dvClipX = -1.0f;
        view.dvClipY = 1.0f;
        view.dvClipWidth = 2.0f;
        view.dvClipHeight = 2.0f;
        view.dvMinZ = 0.0f;
        view.dvMaxZ = 1.0f;
        hr = ((d32_set_viewport2_fn)viewport3->vtbl[D3DVP2_SET_VIEWPORT2])(
            viewport3, &view);
    }
    if (hr == 0) {
        hr = ((d32_device_viewport_fn)
                  device3->vtbl[D3DDEV3_SET_CURRENT_VIEWPORT])(device3,
                                                               viewport3);
    }
    d32_hx("T12ViewportHr", (DWORD)hr);
    if (hr == 0 && base_surface != 0 && lightmap != 0) {
        hr = ((d32_query_fn)base_surface->vtbl[COM_QUERY_INTERFACE])(
            base_surface, &d32_iid_d3d_texture2, (void **)&texture0);
        if (hr == 0) {
            hr = ((d32_query_fn)lightmap->vtbl[COM_QUERY_INTERFACE])(
                lightmap, &d32_iid_d3d_texture2, (void **)&texture1);
        }
        d32_hx("T12TextureHr", (DWORD)hr);
    } else if (hr == 0) {
        hr = (HRESULT)0x80004005ul;
    }
    if (hr != 0) {
        d32_line("T12Result", "NO-DEVICE3");
        ++failures;
        goto release;
    }

    ((d32_render_state_fn)device3->vtbl[D3DDEV3_SET_RENDER_STATE])(
        device3, D3DRENDERSTATE_ZENABLE, 0ul);
    ((d32_render_state_fn)device3->vtbl[D3DDEV3_SET_RENDER_STATE])(
        device3, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    ((d32_render_state_fn)device3->vtbl[D3DDEV3_SET_RENDER_STATE])(
        device3, D3DRENDERSTATE_DITHERENABLE, 0ul);
    d32_hx("T12SetTexture0Hr",
           (DWORD)((d32_set_texture_fn)device3->vtbl[D3DDEV3_SET_TEXTURE])(
               device3, 0ul, texture0));
    d32_hx("T12SetTexture1Hr",
           (DWORD)((d32_set_texture_fn)device3->vtbl[D3DDEV3_SET_TEXTURE])(
               device3, 1ul, texture1));
    d32_tss(device3, 0ul, D32_TSS_COLOROP, D32_TOP_SELECTARG1);
    d32_tss(device3, 0ul, D32_TSS_COLORARG1, D32_TA_TEXTURE);
    d32_tss(device3, 0ul, D32_TSS_ALPHAOP, D32_TOP_SELECTARG1);
    d32_tss(device3, 0ul, D32_TSS_ALPHAARG1, D32_TA_TEXTURE);
    d32_tss(device3, 0ul, D32_TSS_TEXCOORDINDEX, 0ul);
    d32_tss(device3, 0ul, D32_TSS_MAGFILTER, D32_TFG_POINT);
    d32_tss(device3, 0ul, D32_TSS_MINFILTER, D32_TFG_POINT);
    d32_tss(device3, 0ul, D32_TSS_MIPFILTER, D32_TFP_NONE);
    d32_tss(device3, 1ul, D32_TSS_COLOROP, D32_TOP_MODULATE);
    d32_tss(device3, 1ul, D32_TSS_COLORARG1, D32_TA_TEXTURE);
    d32_tss(device3, 1ul, D32_TSS_COLORARG2, D32_TA_CURRENT);
    d32_tss(device3, 1ul, D32_TSS_ALPHAOP, D32_TOP_SELECTARG2);
    d32_tss(device3, 1ul, D32_TSS_ALPHAARG2, D32_TA_CURRENT);
    d32_tss(device3, 1ul, D32_TSS_TEXCOORDINDEX, 1ul);
    d32_tss(device3, 1ul, D32_TSS_MAGFILTER, D32_TFG_POINT);
    d32_tss(device3, 1ul, D32_TSS_MINFILTER, D32_TFG_POINT);
    d32_tss(device3, 1ul, D32_TSS_MIPFILTER, D32_TFP_NONE);
    d32_tss(device3, 2ul, D32_TSS_COLOROP, D32_TOP_DISABLE);
    hr = ((d32_validate_fn)device3->vtbl[D3DDEV3_VALIDATE_DEVICE])(device3,
                                                                  &passes);
    d32_hx("T12ValidateHr", (DWORD)hr);
    d32_hx("T12ValidatePasses", passes);
    /*
     * The device's own stage counts decide, not ValidateDevice alone: at
     * DDI 5 the DX6 runtime answered one pass for these two stages and
     * then drew stage 0 only (A8U4I5, 2026-10-24), since a DX5 driver is
     * never told of stage 1. An application that reads the caps sees one
     * stage there and draws its own passes.
     */
    d32_zero(hal_caps, sizeof(hal_caps));
    d32_zero(hel_caps, sizeof(hel_caps));
    hal_caps[0] = sizeof(hal_caps);
    hel_caps[0] = sizeof(hel_caps);
    d32_hx("T12CapsHr",
           (DWORD)((d32_caps3_fn)device3->vtbl[D3DDEV3_GET_CAPS])(
               device3, hal_caps, hel_caps));
    d32_hx("T12BlendStages", hal_caps[D3DDESC_STAGES] & 0xfffful);
    d32_hx("T12SimultaneousTextures", hal_caps[D3DDESC_STAGES] >> 16);
    if ((hal_caps[D3DDESC_STAGES] & 0xfffful) < 2ul ||
        (hal_caps[D3DDESC_STAGES] >> 16) < 2ul) {
        d32_line("T12Result", "SKIPPED-ONE-STAGE");
        goto release;
    }
    if (hr != 0 || passes != 1ul) {
        d32_line("T12Result", "SKIPPED-NOT-ONE-PASS");
        goto release;
    }

    d32_hx("T12ClearHr", (DWORD)d32_fill(back, DDBLT_COLORFILL, 0ul));
    d32_path_read(&before);
    d32_hx("T12ModulateHr", (DWORD)d32_draw_two_set_quad(device3, 16.0f,
                                                         16.0f));
    d32_tss(device3, 1ul, D32_TSS_COLOROP, D32_TOP_ADD);
    d32_hx("T12AddHr", (DWORD)d32_draw_two_set_quad(device3, 96.0f, 16.0f));
    d32_path_read(&after);
    d32_path_write("T12", &before, &after);

    d32_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = ((d32_lock_fn)back->vtbl[DDS_LOCK])(back, 0, &desc, DDLOCK_WAIT, 0);
    d32_hx("T12LockHr", (DWORD)hr);
    if (hr != 0 || desc.lpSurface == 0) {
        ++failures;
        goto release;
    }
    ok = d32_quadrants_near(&desc, 16ul, 16ul, modulated, "T12Modulate") |
         (d32_quadrants_near(&desc, 96ul, 16ul, added, "T12Add") << 4);
    ((d32_unlock_fn)back->vtbl[DDS_UNLOCK])(back, desc.lpSurface);
    d32_hx("T12Ok", ok);
    failures += ok != 0xfful ? 1ul : 0ul;
    /* One pass promised and the trace readable: a pass that never touched
     * the second unit proves nothing about it. */
    if (before.ok && after.ok && after.two_unit == before.two_unit) {
        d32_line("T12Result", "NOT-EXERCISED");
        ++failures;
    } else {
        d32_line("T12Result", ok == 0xfful ? "PASS" : "FAIL");
    }
    failures += d32_two_stage_volume(device3, back);

release:
    if (texture1 != 0) {
        ((d32_release_fn)texture1->vtbl[COM_RELEASE])(texture1);
    }
    if (texture0 != 0) {
        ((d32_release_fn)texture0->vtbl[COM_RELEASE])(texture0);
    }
    if (viewport3 != 0) {
        ((d32_release_fn)viewport3->vtbl[COM_RELEASE])(viewport3);
    }
    if (device3 != 0) {
        ((d32_release_fn)device3->vtbl[COM_RELEASE])(device3);
    }
    if (back4 != 0) {
        ((d32_release_fn)back4->vtbl[COM_RELEASE])(back4);
    }
    if (d3d3 != 0) {
        ((d32_release_fn)d3d3->vtbl[COM_RELEASE])(d3d3);
    }
    if (lightmap != 0) {
        ((d32_release_fn)lightmap->vtbl[COM_RELEASE])(lightmap);
    }
    return failures;
}

/*
 * T15, memory outside the driver: a pattern in this process's own memory,
 * in a shared mapping (the shared arena, where Windows 9x also maps AGP
 * pages and file views) and in a file read back through the disk cache,
 * laid down before the first draw and checked after the last, the child's
 * included. With the Rage 128's AGP ring, 3DMark 2001 SE read its own data
 * files back corrupt within a minute while every pixel test passed
 * (2026-10-25); nothing here looked at memory the driver does not own.
 */
#define D32_SENTINEL_HEAP_BYTES 0x02000000ul
#define D32_SENTINEL_MAP_BYTES 0x00800000ul
#define D32_SENTINEL_FILE_BYTES 0x00400000ul
#define D32_SENTINEL_CHUNK_BYTES 0x00010000ul
#define D32_SENTINEL_PATH "C:\\V9XDIAG\\V9XD32.SEN"
#define D32_SENTINEL_BARE_PATH "C:\\V9XDIAG\\V9XD32W.SEN"
#define D32_SENTINEL_HEAP_SEED 0xa5a5a5a5ul
#define D32_SENTINEL_MAP_SEED 0x5a5a5a5aul
#define D32_SENTINEL_FILE_SEED 0x3c3c3c3cul
/* /t15only's watch: about as long as a full run takes on A8U4I5. */
#define D32_SENTINEL_WATCH_MS 30000ul
#define D32_SENTINEL_WATCH_STEP_MS 1000ul

static const char *d32_sentinel_path = D32_SENTINEL_PATH;
static DWORD *d32_sentinel_heap;
static DWORD *d32_sentinel_map;
/* /t15small: 4 MiB and 1 MiB, which a 64 MiB machine keeps resident. */
#define D32_SENTINEL_SMALL_HEAP_BYTES 0x00400000ul
#define D32_SENTINEL_SMALL_MAP_BYTES 0x00100000ul
static DWORD d32_sentinel_heap_bytes = D32_SENTINEL_HEAP_BYTES;
static DWORD d32_sentinel_map_bytes = D32_SENTINEL_MAP_BYTES;

/*
 * The page-table entry behind each pattern page, as laid and as found,
 * so a bad word is named by its physical frame and not only its linear
 * address. Ring 3 cannot read Windows 98's page tables (a guarded read at
 * FF800000h failed on A8U4I5), so PFRM.VXD copies them with
 * _CopyPageTable (tools\diag\page_frames.asm); without it every entry
 * reads FFFFFFFFh. A frame that changed, or an entry not present, is a
 * page the pager wrote out: T15 commits 40 MiB, and A8U4I5 has 64 MiB.
 */
#define D32_PFRM_VXD "\\\\.\\PFRM.VXD"
#define D32_PFRM_COPY 1ul
#define D32_PFRM_PAGES_MAX 8192ul
#define D32_PAGE_PRESENT 0x00000001ul
#define D32_PAGE_FRAME 0xfffff000ul
#define D32_SENTINEL_HEAP_PAGES (0x02000000ul / 4096ul)
#define D32_SENTINEL_MAP_PAGES (0x00800000ul / 4096ul)

static DWORD d32_sentinel_heap_pte[D32_SENTINEL_HEAP_PAGES];
static DWORD d32_sentinel_map_pte[D32_SENTINEL_MAP_PAGES];
static DWORD d32_pte_now[D32_SENTINEL_HEAP_PAGES];
static HANDLE d32_pfrm = INVALID_HANDLE_VALUE;
static int d32_pfrm_tried;

/* Entries for pages pages from linear, each FFFFFFFFh when they cannot
 * be read. */
static void d32_pte_copy(const void *linear, DWORD pages, DWORD *out)
{
    DWORD request[2];
    DWORD returned = 0ul;
    DWORD page;

    if (!d32_pfrm_tried) {
        d32_pfrm_tried = 1;
        d32_pfrm = CreateFileA(D32_PFRM_VXD, 0, 0, 0, CREATE_NEW,
                               FILE_FLAG_DELETE_ON_CLOSE, 0);
    }
    /* PFRM.VXD copies at most D32_PFRM_PAGES_MAX entries a call. */
    for (page = 0ul; page < pages; page += request[1]) {
        request[0] = ((DWORD)linear >> 12) + page;
        request[1] = pages - page < D32_PFRM_PAGES_MAX ? pages - page
                                                       : D32_PFRM_PAGES_MAX;
        returned = 0ul;
        if (d32_pfrm == INVALID_HANDLE_VALUE ||
            !DeviceIoControl(d32_pfrm, D32_PFRM_COPY, request,
                             sizeof(request), out + page, request[1] * 4ul,
                             &returned, 0) ||
            returned != request[1] * 4ul) {
            for (returned = 0ul; returned < request[1]; ++returned) {
                out[page + returned] = 0xfffffffful;
            }
        }
    }
}

static DWORD d32_pte(const void *linear)
{
    DWORD entry;

    d32_pte_copy(linear, 1ul, &entry);
    return entry;
}

/* The entry as laid, for a word in the heap or the mapping. */
static DWORD d32_pte_laid(const DWORD *word)
{
    DWORD at = (DWORD)word;

    if (d32_sentinel_heap != 0 && at >= (DWORD)d32_sentinel_heap &&
        at - (DWORD)d32_sentinel_heap < d32_sentinel_heap_bytes) {
        return d32_sentinel_heap_pte[(at - (DWORD)d32_sentinel_heap) >> 12];
    }
    if (d32_sentinel_map != 0 && at >= (DWORD)d32_sentinel_map &&
        at - (DWORD)d32_sentinel_map < d32_sentinel_map_bytes) {
        return d32_sentinel_map_pte[(at - (DWORD)d32_sentinel_map) >> 12];
    }
    return 0xfffffffful;
}

/* Pages whose entry is no longer the one laid: not present now, or
 * present in another frame. */
static DWORD d32_pte_moved(const DWORD *words, DWORD pages,
                           const DWORD *ptes, DWORD *absent)
{
    DWORD page;
    DWORD now;
    DWORD moved = 0ul;

    *absent = 0ul;
    d32_pte_copy(words, pages, d32_pte_now);
    for (page = 0ul; page < pages; ++page) {
        now = d32_pte_now[page];
        if ((now & D32_PAGE_PRESENT) == 0ul || now == 0xfffffffful) {
            ++*absent;
            ++moved;
        } else if ((now & D32_PAGE_FRAME) != (ptes[page] & D32_PAGE_FRAME)) {
            ++moved;
        }
    }
    return moved;
}
static int d32_sentinel_file;
static DWORD d32_sentinel_chunk[D32_SENTINEL_CHUNK_BYTES / 4ul];

/* /t15ones lays every bit set, so a writer that only clears bits shows
 * wherever it lands; the bad words found so far each had one bit cleared. */
static int d32_sentinel_ones = -1;

static DWORD d32_sentinel_word(DWORD index, DWORD seed)
{
    if (d32_sentinel_ones < 0) {
        d32_sentinel_ones = d32_has_switch("/t15ones");
    }
    if (d32_sentinel_ones) {
        return 0xfffffffful;
    }
    return (index * 0x9e3779b1ul) ^ seed;
}

static void d32_sentinel_pattern(DWORD *words, DWORD first, DWORD count,
                                 DWORD seed)
{
    DWORD i;

    for (i = 0ul; i < count; ++i) {
        words[i] = d32_sentinel_word(first + i, seed);
    }
}

/*
 * /t15frame=XXXXXXXX names a physical frame (hex); the fill says whether
 * the pattern holds it, where, and which word of it the pattern puts at
 * page offset 5C0h. A bad word that keeps its physical address across
 * runs is either one cell or one writer at a fixed address.
 */
static DWORD d32_switch_number(const char *option, DWORD base)
{
    const char *line = GetCommandLineA();
    DWORD value = 0ul;
    DWORD digit;
    unsigned length = (unsigned)lstrlenA(option);
    unsigned offset;
    unsigned index;
    char c;

    for (offset = 0u; line[offset] != '\0'; ++offset) {
        /* /name= or -name=, as d32_has_switch takes them. */
        if (option[0] == '/' && line[offset] == '-') {
            if (length < 2u ||
                CompareStringA(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE,
                               line + offset + 1, (int)(length - 1u),
                               option + 1, (int)(length - 1u)) !=
                    CSTR_EQUAL) {
                continue;
            }
        } else if (CompareStringA(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE,
                                  line + offset, (int)length, option,
                                  (int)length) != CSTR_EQUAL) {
            continue;
        }
        for (index = offset + length; line[index] != '\0'; ++index) {
            c = line[index];
            if (c >= '0' && c <= '9') {
                digit = (DWORD)(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = (DWORD)(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                digit = (DWORD)(c - 'A' + 10);
            } else {
                break;
            }
            if (digit >= base) {
                break;
            }
            value = value * base + digit;
        }
        return value;
    }
    return 0ul;
}

/*
 * /t15bare's watch with /t15watch=<seconds>: every D32_SENTINEL_HIT_STEP_MS
 * the heap and the mapping are scanned, each bad word is written out with
 * the time, its linear address, its page-table entry and its value, and
 * put back, so the next hit on the same word shows too. How often the
 * writer comes, and whether its physical target stays put, is what this
 * measures.
 */
#define D32_SENTINEL_HIT_STEP_MS 250ul
#define D32_SENTINEL_HIT_LINES 200ul
static DWORD d32_sentinel_hits;

static void d32_sentinel_repair(DWORD *words, DWORD count, DWORD seed,
                                const char *name)
{
    char text[96];
    DWORD i;
    DWORD expected;

    for (i = 0ul; i < count; ++i) {
        expected = d32_sentinel_word(i, seed);
        if (words[i] == expected) {
            continue;
        }
        if (d32_sentinel_hits < D32_SENTINEL_HIT_LINES) {
            wsprintfA(text, "%lu %s %08lX %08lX pte %08lX %08lX",
                      GetTickCount(), name, (DWORD)&words[i],
                      d32_pte(&words[i]), words[i], expected);
            d32_line("T15Hit", text);
        }
        ++d32_sentinel_hits;
        words[i] = expected;
    }
}

/*
 * /t15bare /t15hunt=<cycles> [/t15mb=<MiB>]: the address map of the bad
 * bits, apart from anything else T15 does. One block (96 MiB unless
 * /t15mb says otherwise) is committed; each cycle writes it all ones or
 * all zeros by turns, waits D32_HUNT_WAIT_MS and reads it back, and every
 * word that changed is written out with its physical address and the bits
 * that flipped. Then the block's frames are summarised, so a rate per
 * address class can be worked out: every hit through boot 621 was at a
 * physical address of 15C0h modulo 8000h, bit 23, 1 to 0.
 */
#define D32_HUNT_MB_DEFAULT 96ul
#define D32_HUNT_MB_MAX 128ul
#define D32_HUNT_WAIT_MS 2000ul
#define D32_HUNT_LINES 400ul
#define D32_HUNT_CLASS_STRIDE 0x8000ul
#define D32_HUNT_CLASS_OFFSET 0x15c0ul

static DWORD d32_hunt_pte[D32_HUNT_MB_MAX * 256ul];

static void d32_hunt(DWORD cycles)
{
    char text[96];
    DWORD mb = d32_switch_number("/t15mb=", 10ul);
    DWORD pages;
    DWORD words;
    DWORD *block;
    DWORD cycle;
    DWORD i;
    DWORD fill;
    DWORD frame;
    DWORD lines = 0ul;
    DWORD hits = 0ul;
    DWORD class_pages = 0ul;
    DWORD frame_low = 0xfffffffful;
    DWORD frame_high = 0ul;
    int fresh = d32_has_switch("/t15fresh");

    if (mb == 0ul || mb > D32_HUNT_MB_MAX) {
        mb = D32_HUNT_MB_DEFAULT;
    }
    pages = mb * 256ul;
    words = pages * 1024ul;
    block = (DWORD *)VirtualAlloc(0, pages * 4096ul, MEM_COMMIT,
                                  PAGE_READWRITE);
    d32_hx("HuntBase", (DWORD)block);
    d32_hx("HuntPages", pages);
    if (block == 0) {
        return;
    }
    for (cycle = 0ul; cycle < cycles; ++cycle) {
        fill = (cycle & 1ul) == 0ul ? 0xfffffffful : 0ul;
        /* /t15fresh: every cycle on newly committed pages, all ones; the
         * boot-621 hunt failed almost only on its first cycle. */
        if (fresh) {
            fill = 0xfffffffful;
            if (cycle != 0ul) {
                VirtualFree(block, 0ul, MEM_RELEASE);
                block = (DWORD *)VirtualAlloc(0, pages * 4096ul, MEM_COMMIT,
                                              PAGE_READWRITE);
                if (block == 0) {
                    d32_line("HuntRealloc", "failed");
                    break;
                }
            }
        }
        for (i = 0ul; i < words; ++i) {
            block[i] = fill;
        }
        if (cycle == 0ul || fresh) {
            d32_pte_copy(block, pages, d32_hunt_pte);
        }
        Sleep(D32_HUNT_WAIT_MS);
        for (i = 0ul; i < words; ++i) {
            if (block[i] == fill) {
                continue;
            }
            ++hits;
            if (lines < D32_HUNT_LINES) {
                frame = d32_hunt_pte[i >> 10] & D32_PAGE_FRAME;
                wsprintfA(text, "%lu %08lX %08lX %08lX", cycle,
                          (DWORD)&block[i], frame + (i & 1023ul) * 4ul,
                          block[i] ^ fill);
                d32_line("HuntHit", text);
                ++lines;
            }
        }
    }
    /* The frames the block had, and how many sit in the suspect class. */
    for (i = 0ul; i < pages; ++i) {
        frame = d32_hunt_pte[i] & D32_PAGE_FRAME;
        if ((d32_hunt_pte[i] & D32_PAGE_PRESENT) == 0ul) {
            continue;
        }
        if (frame < frame_low) {
            frame_low = frame;
        }
        if (frame > frame_high) {
            frame_high = frame;
        }
        if (((frame + 0x5c0ul) & (D32_HUNT_CLASS_STRIDE - 1ul)) ==
            D32_HUNT_CLASS_OFFSET) {
            ++class_pages;
        }
    }
    d32_hx("HuntFrameLow", frame_low);
    d32_hx("HuntFrameHigh", frame_high);
    d32_hx("HuntClassPages", class_pages);
    d32_hx("HuntHits", hits);
    /* Every frame, eight to a line, for working out which rows were held. */
    for (i = 0ul; i < pages && d32_has_switch("/t15frames"); i += 8ul) {
        wsprintfA(text, "%05lX %05lX %05lX %05lX %05lX %05lX %05lX %05lX",
                  d32_hunt_pte[i] >> 12, d32_hunt_pte[i + 1ul] >> 12,
                  d32_hunt_pte[i + 2ul] >> 12, d32_hunt_pte[i + 3ul] >> 12,
                  d32_hunt_pte[i + 4ul] >> 12, d32_hunt_pte[i + 5ul] >> 12,
                  d32_hunt_pte[i + 6ul] >> 12, d32_hunt_pte[i + 7ul] >> 12);
        d32_line("HuntFrames", text);
    }
}

static void d32_sentinel_watch_hits(DWORD seconds)
{
    DWORD started = GetTickCount();

    while (GetTickCount() - started < seconds * 1000ul) {
        Sleep(D32_SENTINEL_HIT_STEP_MS);
        d32_sentinel_repair(d32_sentinel_heap, d32_sentinel_heap_bytes / 4ul,
                            D32_SENTINEL_HEAP_SEED, "heap");
        d32_sentinel_repair(d32_sentinel_map, d32_sentinel_map_bytes / 4ul,
                            D32_SENTINEL_MAP_SEED, "map");
    }
    d32_hx("T15Hits", d32_sentinel_hits);
}

static void d32_sentinel_find_frame(void)
{
    char text[64];
    DWORD frame = d32_switch_number("/t15frame=", 16ul) & D32_PAGE_FRAME;
    DWORD page;

    if (frame == 0ul) {
        return;
    }
    for (page = 0ul; page < (d32_sentinel_heap_bytes >> 12); ++page) {
        if ((d32_sentinel_heap_pte[page] & D32_PAGE_FRAME) == frame) {
            wsprintfA(text, "heap %08lX %08lX", page << 12,
                      d32_sentinel_heap[page * 1024ul + 0x5c0ul / 4ul]);
            d32_line("T15FrameAt", text);
            return;
        }
    }
    for (page = 0ul; page < (d32_sentinel_map_bytes >> 12); ++page) {
        if ((d32_sentinel_map_pte[page] & D32_PAGE_FRAME) == frame) {
            wsprintfA(text, "map %08lX %08lX", page << 12,
                      d32_sentinel_map[page * 1024ul + 0x5c0ul / 4ul]);
            d32_line("T15FrameAt", text);
            return;
        }
    }
    /* The file's read buffer, where the boot-619 file word went bad. */
    for (page = 0ul; page <= D32_SENTINEL_CHUNK_BYTES >> 12; ++page) {
        DWORD linear = ((DWORD)d32_sentinel_chunk & D32_PAGE_FRAME) +
                       (page << 12);

        if ((d32_pte((const void *)linear) & D32_PAGE_FRAME) == frame) {
            wsprintfA(text, "chunk %08lX", linear);
            d32_line("T15FrameAt", text);
            return;
        }
    }
    d32_line("T15FrameAt", "none");
}

static void d32_sentinel_fill(void)
{
    HANDLE mapping;
    HANDLE file;
    DWORD at;
    DWORD written;

    d32_line("Stage", "3b-t15-sentinel-fill");
    if (d32_has_switch("/t15small")) {
        d32_sentinel_heap_bytes = D32_SENTINEL_SMALL_HEAP_BYTES;
        d32_sentinel_map_bytes = D32_SENTINEL_SMALL_MAP_BYTES;
    }
    d32_sentinel_heap = (DWORD *)VirtualAlloc(0, d32_sentinel_heap_bytes,
                                              MEM_COMMIT, PAGE_READWRITE);
    /* A page at a time, its entry read while it is surely resident. */
    for (at = 0ul; d32_sentinel_heap != 0 && at < d32_sentinel_heap_bytes;
         at += 4096ul) {
        d32_sentinel_pattern(d32_sentinel_heap + at / 4ul, at / 4ul, 1024ul,
                             D32_SENTINEL_HEAP_SEED);
        d32_sentinel_heap_pte[at >> 12] =
            d32_pte(d32_sentinel_heap + at / 4ul);
    }
    mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0ul,
                                 d32_sentinel_map_bytes, 0);
    if (mapping != 0) {
        d32_sentinel_map = (DWORD *)MapViewOfFile(mapping, FILE_MAP_WRITE,
                                                  0ul, 0ul, 0ul);
    }
    for (at = 0ul; d32_sentinel_map != 0 && at < d32_sentinel_map_bytes;
         at += 4096ul) {
        d32_sentinel_pattern(d32_sentinel_map + at / 4ul, at / 4ul, 1024ul,
                             D32_SENTINEL_MAP_SEED);
        d32_sentinel_map_pte[at >> 12] = d32_pte(d32_sentinel_map + at / 4ul);
    }
    /* Scanned again before and after the file, the fill's slowest part:
     * the hits so far all came before the next stage. */
    d32_sentinel_check("3c-t15-before-file");
    file = CreateFileA(d32_sentinel_path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, 0);
    if (file != INVALID_HANDLE_VALUE) {
        d32_sentinel_file = 1;
        for (at = 0ul; at < D32_SENTINEL_FILE_BYTES;
             at += D32_SENTINEL_CHUNK_BYTES) {
            d32_sentinel_pattern(d32_sentinel_chunk, at / 4ul,
                                 D32_SENTINEL_CHUNK_BYTES / 4ul,
                                 D32_SENTINEL_FILE_SEED);
            if (!WriteFile(file, d32_sentinel_chunk,
                           D32_SENTINEL_CHUNK_BYTES, &written, 0) ||
                written != D32_SENTINEL_CHUNK_BYTES) {
                d32_sentinel_file = 0;
                break;
            }
        }
        CloseHandle(file);
    }
    d32_sentinel_check("3d-t15-after-file");
    d32_hx("T15HeapReady", d32_sentinel_heap != 0 ? 1ul : 0ul);
    d32_hx("T15HeapBase", (DWORD)d32_sentinel_heap);
    d32_hx("T15MapReady", d32_sentinel_map != 0 ? 1ul : 0ul);
    d32_hx("T15MapBase", (DWORD)d32_sentinel_map);
    d32_hx("T15HeapBytes", d32_sentinel_heap_bytes);
    d32_hx("T15FramesReadable", d32_pfrm != INVALID_HANDLE_VALUE ? 1ul : 0ul);
    d32_sentinel_find_frame();
    d32_hx("T15FillTick", GetTickCount());
    d32_hx("T15FileReady", (DWORD)d32_sentinel_file);
}

/* Words that differ from the pattern; the first one's byte offset goes to
 * first_bad unless an earlier call already set it. With a name, the first
 * D32_SENTINEL_LISTED are written out as "offset linear found expected",
 * and the spread as the last offset: one writer at one page offset reads
 * differently from a run of words or a whole page. */
#define D32_SENTINEL_LISTED 8ul

static DWORD d32_sentinel_compare(const DWORD *words, DWORD first,
                                  DWORD count, DWORD seed, DWORD *first_bad,
                                  const char *name)
{
    char key[32];
    char text[80];
    DWORD i;
    DWORD expected;
    DWORD bad = 0ul;
    DWORD last = 0ul;

    for (i = 0ul; i < count; ++i) {
        expected = d32_sentinel_word(first + i, seed);
        if (words[i] == expected) {
            continue;
        }
        if (bad == 0ul && *first_bad == 0xfffffffful) {
            *first_bad = (first + i) * 4ul;
        }
        if (name != 0 && bad < D32_SENTINEL_LISTED) {
            wsprintfA(key, "%sWord%lu", name, bad);
            wsprintfA(text, "%08lX %08lX %08lX %08lX pte %08lX %08lX",
                      (first + i) * 4ul, (DWORD)&words[i], words[i],
                      expected, d32_pte_laid(&words[i]), d32_pte(&words[i]));
            d32_line(key, text);
        }
        last = (first + i) * 4ul;
        ++bad;
    }
    if (name != 0 && bad != 0ul) {
        wsprintfA(key, "%sLast", name);
        d32_hx(key, last);
    }
    return bad;
}

/*
 * The heap and the mapping scanned again at every stage line, so a bad
 * word is placed between two stages, not only somewhere in the run. A
 * line is written only when a count changes. The file is left to the end:
 * reading it back goes through the disk cache the run is also using.
 */
static DWORD d32_sentinel_seen_heap;
static DWORD d32_sentinel_seen_map;
static int d32_sentinel_scanning;

static void d32_sentinel_check(const char *stage)
{
    char text[64];
    DWORD first_bad = 0xfffffffful;
    DWORD heap = 0ul;
    DWORD map = 0ul;

    if (d32_sentinel_scanning) {
        return;
    }
    /* The page tables first, since the scan pages everything back in. */
    if (d32_sentinel_heap != 0 && d32_sentinel_map != 0) {
        DWORD heap_absent;
        DWORD map_absent;
        DWORD heap_moved = d32_pte_moved(d32_sentinel_heap,
                                         d32_sentinel_heap_bytes >> 12,
                                         d32_sentinel_heap_pte, &heap_absent);
        DWORD map_moved = d32_pte_moved(d32_sentinel_map,
                                        d32_sentinel_map_bytes >> 12,
                                        d32_sentinel_map_pte, &map_absent);

        wsprintfA(text, "%s heap %lu/%lu map %lu/%lu", stage, heap_moved,
                  heap_absent, map_moved, map_absent);
        d32_sentinel_scanning = 1;
        d32_line("T15Paged", text);
        d32_sentinel_scanning = 0;
    }
    if (d32_sentinel_heap != 0) {
        heap = d32_sentinel_compare(d32_sentinel_heap, 0ul,
                                    d32_sentinel_heap_bytes / 4ul,
                                    D32_SENTINEL_HEAP_SEED, &first_bad, 0);
    }
    if (d32_sentinel_map != 0) {
        map = d32_sentinel_compare(d32_sentinel_map, 0ul,
                                   d32_sentinel_map_bytes / 4ul,
                                   D32_SENTINEL_MAP_SEED, &first_bad, 0);
    }
    if (heap == d32_sentinel_seen_heap && map == d32_sentinel_seen_map) {
        return;
    }
    wsprintfA(text, "%s heap %lu map %lu tick %lu", stage, heap, map,
              GetTickCount());
    d32_sentinel_scanning = 1;
    d32_line("T15Seen", text);
    /* The words themselves, the first time a region's count moves: the
     * run may not live to the verify. */
    if (heap != d32_sentinel_seen_heap) {
        first_bad = 0xfffffffful;
        d32_sentinel_compare(d32_sentinel_heap, 0ul,
                             d32_sentinel_heap_bytes / 4ul,
                             D32_SENTINEL_HEAP_SEED, &first_bad,
                             "T15SeenHeap");
    }
    if (map != d32_sentinel_seen_map) {
        first_bad = 0xfffffffful;
        d32_sentinel_compare(d32_sentinel_map, 0ul,
                             d32_sentinel_map_bytes / 4ul,
                             D32_SENTINEL_MAP_SEED, &first_bad, "T15SeenMap");
    }
    d32_sentinel_scanning = 0;
    d32_sentinel_seen_heap = heap;
    d32_sentinel_seen_map = map;
}

static DWORD d32_sentinel_verify(void)
{
    HANDLE file;
    DWORD at;
    DWORD got;
    DWORD bad;
    DWORD first_bad;
    DWORD failures = 0ul;

    d32_line("Stage", "13-t15-sentinel-verify");
    d32_sentinel_scanning = 1;
    if (d32_sentinel_heap != 0) {
        first_bad = 0xfffffffful;
        bad = d32_sentinel_compare(d32_sentinel_heap, 0ul,
                                   d32_sentinel_heap_bytes / 4ul,
                                   D32_SENTINEL_HEAP_SEED, &first_bad,
                                   "T15Heap");
        d32_hx("T15HeapBad", bad);
        d32_hx("T15HeapFirst", first_bad);
        failures += bad != 0ul ? 1ul : 0ul;
    }
    if (d32_sentinel_map != 0) {
        first_bad = 0xfffffffful;
        bad = d32_sentinel_compare(d32_sentinel_map, 0ul,
                                   d32_sentinel_map_bytes / 4ul,
                                   D32_SENTINEL_MAP_SEED, &first_bad,
                                   "T15Map");
        d32_hx("T15MapBad", bad);
        d32_hx("T15MapFirst", first_bad);
        failures += bad != 0ul ? 1ul : 0ul;
    }
    if (d32_sentinel_file) {
        file = CreateFileA(d32_sentinel_path, GENERIC_READ, FILE_SHARE_READ,
                           0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
        bad = file == INVALID_HANDLE_VALUE ? 1ul : 0ul;
        first_bad = 0xfffffffful;
        for (at = 0ul; file != INVALID_HANDLE_VALUE &&
                       at < D32_SENTINEL_FILE_BYTES;
             at += D32_SENTINEL_CHUNK_BYTES) {
            if (!ReadFile(file, d32_sentinel_chunk, D32_SENTINEL_CHUNK_BYTES,
                          &got, 0) || got != D32_SENTINEL_CHUNK_BYTES) {
                ++bad;
                break;
            }
            bad += d32_sentinel_compare(d32_sentinel_chunk, at / 4ul,
                                        D32_SENTINEL_CHUNK_BYTES / 4ul,
                                        D32_SENTINEL_FILE_SEED, &first_bad,
                                        bad == 0ul ? "T15File" : 0);
        }
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
        d32_hx("T15FileBad", bad);
        d32_hx("T15FileFirst", first_bad);
        failures += bad != 0ul ? 1ul : 0ul;
    }
    d32_line("T15Result", failures == 0ul ? "PASS" : "FAIL");
    return failures;
}

/*
 * T14, a program that ends without cleaning up, then the next one. The
 * Rage 128's command engine was left running after a program's last
 * batch, into the mode change at its exit, and the next 3D program
 * crashed, drew wrongly or hung the machine (2026-10-25); V9XD32 always
 * released everything in order and never saw it. /child is this program
 * again: it sets the mode, draws T13's list, flips, opens a scene and
 * exits there without releasing anything. Then this process sets the mode
 * itself, draws T1's triangle and reads it back. A child that does not
 * end within D32_CHILD_WAIT_MS is a failure, and is left running.
 */
#define D32_CHILD_TEXT_PATH "C:\\V9XDIAG\\V9XD32C.TXT"
#define D32_BARE_TEXT_PATH "C:\\V9XDIAG\\V9XD32W.TXT"
#define D32_CHILD_WAIT_MS 90000ul
#define D32_CHILD_SETTLE_MS 5000ul
#define D32_CHILD_EXIT_CODE 7u
#define D32_CHILD_FLIPS 4ul
#define DDS_FLIP 11
#define DDFLIP_WAIT 0x00000001ul

typedef HRESULT (WINAPI *d32_flip_fn)(D32_OBJECT *, D32_OBJECT *, DWORD);

/* DirectDraw, exclusive at 640x480 and bpp, a flip chain and a DX5 device
 * on its back buffer; zero where a step fails, which is recorded. */
typedef struct d32_session {
    D32_OBJECT *ddraw;
    D32_OBJECT *primary;
    D32_OBJECT *back;
    D32_OBJECT *d3d;
    D32_OBJECT *device;
} D32_SESSION;

static int d32_session_open(D32_SESSION *session, HWND window, DWORD bpp,
                            const char *prefix)
{
    d32_create_fn create;
    D32_SURFACE_DESC desc;
    DWORD caps;
    HRESULT hr;
    char key[48];
    char step[48];

    /* Each call is named before it is made: the reopen after a child that
     * died mid-scene is where a process has been seen to die. */
    wsprintfA(step, "%sStep", prefix);
    d32_zero(session, sizeof(*session));
    d32_line(step, "create");
    create = (d32_create_fn)GetProcAddress(LoadLibraryA("DDRAW.DLL"),
                                           "DirectDrawCreate");
    if (create == 0 || create(0, &session->ddraw, 0) != 0) {
        return 0;
    }
    d32_line(step, "cooperative");
    ((d32_coop_fn)session->ddraw->vtbl[DD_SET_COOPERATIVE_LEVEL])(
        session->ddraw, window, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
    d32_line(step, "set-mode");
    hr = ((d32_set_mode_fn)session->ddraw->vtbl[DD_SET_DISPLAY_MODE])(
        session->ddraw, D32_WIDTH, D32_HEIGHT, bpp);
    wsprintfA(key, "%sSetModeHr", prefix);
    d32_hx(key, (DWORD)hr);
    if (hr != 0) {
        return 0;
    }
    d32_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    desc.dwBackBufferCount = 1ul;
    desc.ddsCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX |
                   DDSCAPS_3DDEVICE;
    d32_line(step, "create-surface");
    hr = ((d32_create_surface_fn)session->ddraw->vtbl[DD_CREATE_SURFACE])(
        session->ddraw, &desc, &session->primary, 0);
    if (hr == 0) {
        d32_line(step, "get-attached");
        caps = DDSCAPS_BACKBUFFER;
        hr = ((d32_get_attached_fn)
                  session->primary->vtbl[DDS_GET_ATTACHED_SURFACE])(
            session->primary, &caps, &session->back);
    }
    if (hr == 0) {
        d32_line(step, "query-d3d");
        hr = ((d32_query_fn)session->ddraw->vtbl[COM_QUERY_INTERFACE])(
            session->ddraw, &d32_iid_d3d2, (void **)&session->d3d);
    }
    wsprintfA(key, "%sSurfacesHr", prefix);
    d32_hx(key, (DWORD)hr);
    if (hr != 0) {
        return 0;
    }
    d32_line(step, "device");
    session->device = d32_device(session->d3d, session->back, D32_WIDTH,
                                 D32_HEIGHT, prefix);
    return session->device != 0;
}

static void d32_session_close(D32_SESSION *session)
{
    if (session->device != 0) {
        ((d32_release_fn)session->device->vtbl[COM_RELEASE])(session->device);
    }
    if (session->d3d != 0) {
        ((d32_release_fn)session->d3d->vtbl[COM_RELEASE])(session->d3d);
    }
    if (session->primary != 0) {
        ((d32_release_fn)session->primary->vtbl[COM_RELEASE])(
            session->primary);
    }
    if (session->ddraw != 0) {
        ((d32_restore_fn)session->ddraw->vtbl[DD_RESTORE_DISPLAY_MODE])(
            session->ddraw);
        ((d32_release_fn)session->ddraw->vtbl[COM_RELEASE])(session->ddraw);
    }
}

/* The child's whole run; never returns. */
static void d32_child(HWND window, DWORD bpp)
{
    D32_SESSION session;
    DWORD flip;
    DWORD rep;

    d32_line("Stage", "child-open");
    if (!d32_session_open(&session, window, bpp, "Child")) {
        d32_finish("CHILD-NO-DEVICE", 2u);
    }
    d32_volume_list(D32_VOLUME_LIST_TOP, 0xff00ff00ul);
    d32_line("Stage", "child-draw-and-flip");
    for (flip = 0ul; flip < D32_CHILD_FLIPS; ++flip) {
        d32_fill(session.back, DDBLT_COLORFILL, 0ul);
        for (rep = 0ul; rep < 8ul; ++rep) {
            d32_draw(session.device, d32_volume_vertices,
                     D32_VOLUME_VERTICES);
        }
        d32_hx("ChildFlipHr",
               (DWORD)((d32_flip_fn)session.primary->vtbl[DDS_FLIP])(
                   session.primary, 0, DDFLIP_WAIT));
    }
    /* /clean, the control: everything released and the mode restored. The
     * others leave less behind than the default does, to find which part
     * of what a dead program leaves breaks the next one: /endscene closes
     * its scene first, /release3d releases the device and IDirect3D2 too,
     * /nomode everything but the display mode. */
    if (d32_has_switch("/clean")) {
        d32_line("Stage", "child-exit-clean");
        d32_session_close(&session);
        d32_line("Result", "CHILD-EXITED-CLEAN");
        ExitProcess(D32_CHILD_EXIT_CODE);
    }
    if (d32_has_switch("/nomode")) {
        d32_line("Stage", "child-exit-no-mode-restore");
        ((d32_release_fn)session.device->vtbl[COM_RELEASE])(session.device);
        ((d32_release_fn)session.d3d->vtbl[COM_RELEASE])(session.d3d);
        ((d32_release_fn)session.primary->vtbl[COM_RELEASE])(
            session.primary);
        ((d32_release_fn)session.ddraw->vtbl[COM_RELEASE])(session.ddraw);
        d32_line("Result", "CHILD-EXITED-NO-MODE");
        ExitProcess(D32_CHILD_EXIT_CODE);
    }
    if (d32_has_switch("/release3d")) {
        d32_line("Stage", "child-exit-surfaces-left");
        ((d32_release_fn)session.device->vtbl[COM_RELEASE])(session.device);
        ((d32_release_fn)session.d3d->vtbl[COM_RELEASE])(session.d3d);
        d32_line("Result", "CHILD-EXITED-SURFACES-LEFT");
        ExitProcess(D32_CHILD_EXIT_CODE);
    }
    if (d32_has_switch("/endscene")) {
        d32_line("Stage", "child-exit-after-scene");
        d32_draw(session.device, d32_volume_vertices, D32_VOLUME_VERTICES);
        d32_line("Result", "CHILD-EXITED-AFTER-SCENE");
        ExitProcess(D32_CHILD_EXIT_CODE);
    }
    /* A scene left open, a batch in it, and out: no EndScene, no Release,
     * no mode restored - what a crashed or killed program leaves. */
    d32_line("Stage", "child-exit-mid-scene");
    ((d32_scene_fn)session.device->vtbl[D3DDEV2_BEGIN_SCENE])(session.device);
    ((d32_draw_fn)session.device->vtbl[D3DDEV2_DRAW_PRIMITIVE])(
        session.device, D3DPT_TRIANGLELIST, D3DVT_TLVERTEX,
        d32_volume_vertices, D32_VOLUME_VERTICES, 0ul);
    d32_ddi_restore();
    d32_line("Result", "CHILD-EXITED");
    ExitProcess(D32_CHILD_EXIT_CODE);
}

static DWORD d32_child_and_after(HWND window, DWORD bpp)
{
    char path[MAX_PATH];
    char command[MAX_PATH + 32];
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    D32_SESSION session;
    D32_SURFACE_DESC desc;
    D32_TLVERTEX v[3];
    DWORD wait;
    DWORD code = 0xfffffffful;
    DWORD judged = 0ul;
    DWORD mismatches;
    HRESULT hr;

    d32_line("Stage", "12-t14-child");
    if (GetModuleFileNameA(0, path, sizeof(path)) == 0ul) {
        d32_line("T14Result", "NO-PATH");
        return 1ul;
    }
    path[sizeof(path) - 1u] = '\0';
    /* /t14clean has the child exit cleanly, and /t14wait waits
     * D32_CHILD_SETTLE_MS before the reopen: the controls that tell a
     * driver fault from DirectDraw still cleaning up after the child. */
    wsprintfA(command, "\"%s\" /child%s%s", path,
              bpp == 16ul ? " /bpp16" : "",
              d32_has_switch("/t14clean") ? " /clean"
              : d32_has_switch("/t14nomode") ? " /nomode"
              : d32_has_switch("/t14release3d") ? " /release3d"
              : d32_has_switch("/t14endscene") ? " /endscene" : "");
    d32_zero(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    if (!CreateProcessA(0, command, 0, 0, FALSE, 0ul, 0, 0, &startup,
                        &process)) {
        d32_hx("T14CreateError", GetLastError());
        d32_line("T14Result", "NO-CHILD");
        return 1ul;
    }
    wait = WaitForSingleObject(process.hProcess, D32_CHILD_WAIT_MS);
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    d32_hx("T14ChildWait", wait);
    d32_hx("T14ChildExit", code);
    if (wait != WAIT_OBJECT_0) {
        d32_line("T14Result", "CHILD-HUNG");
        return 1ul;
    }

    if (d32_has_switch("/t14wait")) {
        Sleep(D32_CHILD_SETTLE_MS);
    }
    /* What the HAL's process watch saw while DirectDraw cleaned up after
     * the child, before this process's own calls can overwrite it: the
     * cleanup runs in DDHELP, which has been seen to exit there. */
    {
        D32_PATH watch_read;

        d32_path_read(&watch_read);
        if (watch_read.ok) {
            const V9X_WATCH *watch = &d32_trace.snapshot.d3d.watch;

            d32_hx("T14WatchFaultPid", watch->fault_pid);
            d32_hx("T14WatchFaultCode", watch->fault_code);
            d32_hx("T14WatchFaultAddress", watch->fault_address);
            d32_hx("T14WatchFaultModule", watch->fault_module);
            d32_hx("T14WatchExitPid", watch->exit_pid);
            d32_hx("T14WatchExitCode", watch->exit_code);
            d32_hx("T14WatchExitCaller", watch->exit_caller);
            d32_hx("T14WatchExitCallerModule", watch->exit_caller_module);
        }
    }
    d32_line("Stage", "12b-t14-after-child");
    if (!d32_session_open(&session, window, bpp, "T14")) {
        d32_session_close(&session);
        d32_line("T14Result", "NO-DEVICE-AFTER");
        return 1ul;
    }
    d32_hx("T14ClearHr", (DWORD)d32_fill(session.back, DDBLT_COLORFILL, 0ul));
    d32_vertex(&v[0], 8.0f, 8.0f, 0.5f, 0xff13579bul);
    d32_vertex(&v[1], 56.0f, 8.0f, 0.5f, 0xff13579bul);
    d32_vertex(&v[2], 8.0f, 56.0f, 0.5f, 0xff13579bul);
    d32_hx("T14DrawHr", (DWORD)d32_draw(session.device, v, 3ul));
    d32_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = ((d32_lock_fn)session.back->vtbl[DDS_LOCK])(session.back, 0, &desc,
                                                     DDLOCK_WAIT, 0);
    d32_hx("T14LockHr", (DWORD)hr);
    mismatches = 1ul;
    if (hr == 0 && desc.lpSurface != 0) {
        mismatches = d32_flat_mismatches(&desc, 8ul, 8ul, 0x0013579bul,
                                         &judged);
        d32_hx("T14Pixel", d32_pixel(&desc, 16ul, 16ul));
        ((d32_unlock_fn)session.back->vtbl[DDS_UNLOCK])(session.back,
                                                        desc.lpSurface);
    }
    d32_hx("T14Judged", judged);
    d32_hx("T14Mismatches", mismatches);
    d32_session_close(&session);
    d32_line("T14Result", mismatches == 0ul ? "PASS" : "FAIL");
    return mismatches != 0ul ? 1ul : 0ul;
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
    WORD indices[6];
    WNDCLASSA window_class;
    HWND window;
    HRESULT hr;
    DWORD bpp;
    DWORD zbits;
    DWORD caps;
    DWORD judged;
    DWORD failures = 0ul;
    DWORD mismatches;
    D32_PATH run_before;
    D32_PATH run_after;
    D32_PATH t9_before;
    D32_PATH t9_after;

    CreateDirectoryA("C:\\V9XDIAG", 0);
    /* /t15bare has files of its own, so it can watch beside a full run. */
    if (d32_has_switch("/t15bare")) {
        d32_sentinel_path = D32_SENTINEL_BARE_PATH;
    }
    d32_file = CreateFileA(d32_has_switch("/child") ? D32_CHILD_TEXT_PATH
                           : d32_has_switch("/t15bare") ? D32_BARE_TEXT_PATH
                                                        : D32_TEXT_PATH,
                           GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, 0);
    d32_line("[V9xD3d32]Build", V9X_BUILD_ID);
    d32_report_switches();
    /* T14's child runs under its parent's lock, so it does not take one. */
    if (!d32_has_switch("/child") && !v9x_probe_lock_take()) {
        d32_finish("BUSY", 3u);
    }
    bpp = d32_has_switch("/bpp16") ? 16ul : 32ul;
    zbits = d32_has_switch("/z24") ? 32ul : 16ul;
    d32_hx("RequestBpp", bpp);
    d32_hx("RequestZBits", zbits);
    if (d32_has_switch("/ddi5")) {
        d32_ddi_apply("5");
    } else if (d32_has_switch("/ddi6")) {
        d32_ddi_apply("6");
    } else if (d32_has_switch("/ddi7")) {
        d32_ddi_apply("7");
    }
    d32_path_read(&run_before);
    d32_hx("TraceReadable", run_before.ok);
    /* /t15early lays T15's pattern before DirectDraw is loaded, so the
     * mode set and the enumeration are inside it too. */
    if (d32_has_switch("/t15early") && !d32_has_switch("/child")) {
        d32_sentinel_fill();
    }
    /* /t15bare, the control below /t15only: no DirectDraw at all, the
     * pattern laid, watched and checked. */
    if (d32_has_switch("/t15bare")) {
        DWORD started;

        if (d32_switch_number("/t15hunt=", 10ul) != 0ul) {
            d32_hunt(d32_switch_number("/t15hunt=", 10ul));
            d32_finish("HUNTED", 0u);
        }
        d32_sentinel_fill();
        if (d32_switch_number("/t15watch=", 10ul) != 0ul) {
            d32_sentinel_watch_hits(d32_switch_number("/t15watch=", 10ul));
        }
        started = GetTickCount();
        while (GetTickCount() - started < D32_SENTINEL_WATCH_MS) {
            Sleep(D32_SENTINEL_WATCH_STEP_MS);
            d32_line("Stage", "t15-bare-watch");
        }
        failures = d32_sentinel_verify();
        d32_hx("Failures", failures);
        d32_finish(failures == 0ul ? "PASS" : "FAIL",
                   failures == 0ul ? 0u : 1u);
    }

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
    if (d32_has_switch("/child")) {
        d32_child(window, bpp);
    }
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

    if (!d32_has_switch("/t15early")) {
        d32_sentinel_fill();
    }
    /* /t15only, T15's control: the mode set and Direct3D enumerated as
     * always, then no surface and no draw, the pattern watched for
     * D32_SENTINEL_WATCH_MS and checked. A writer that still lands is not
     * reached through anything this program draws. */
    if (d32_has_switch("/t15only")) {
        DWORD started = GetTickCount();

        while (GetTickCount() - started < D32_SENTINEL_WATCH_MS) {
            Sleep(D32_SENTINEL_WATCH_STEP_MS);
            d32_sentinel_check("t15-watch");
        }
        failures = d32_sentinel_verify();
        ((d32_release_fn)d3d->vtbl[COM_RELEASE])(d3d);
        ((d32_restore_fn)ddraw->vtbl[DD_RESTORE_DISPLAY_MODE])(ddraw);
        ((d32_release_fn)ddraw->vtbl[COM_RELEASE])(ddraw);
        DestroyWindow(window);
        d32_hx("Failures", failures);
        d32_finish(failures == 0ul ? "PASS" : "FAIL",
                   failures == 0ul ? 0u : 1u);
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
    d32_zstats(zbuffer, zbits, "ZClear");

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

    /*
     * T8, sloped depth: T3 and T5 hold z constant over each quad, which
     * checks only the constant term of the depth plane. Here a green quad
     * runs from z 0.2 to 0.8 and a red one from 0.8 to 0.2 over the same
     * pixels, so with LESS each colour wins on the side where it is nearer
     * and they cross at the centre. At (560, 8) the slope is along x; at
     * (560, 80) it is along y. Ironlake drew 3DMark 2000's helicopter
     * through a hill (2026-10-22); the depth slopes are the suspect.
     */
    d32_line("Stage", "8e-t8-sloped-depth");
    d32_vertex(&v[0], 560.0f, 8.0f, 0.2f, 0xff00ff00ul);
    d32_vertex(&v[1], 624.0f, 8.0f, 0.8f, 0xff00ff00ul);
    d32_vertex(&v[2], 560.0f, 72.0f, 0.2f, 0xff00ff00ul);
    d32_vertex(&v[3], 624.0f, 8.0f, 0.8f, 0xff00ff00ul);
    d32_vertex(&v[4], 624.0f, 72.0f, 0.8f, 0xff00ff00ul);
    d32_vertex(&v[5], 560.0f, 72.0f, 0.2f, 0xff00ff00ul);
    d32_hx("T8XGreenHr", (DWORD)d32_draw(device, v, 6ul));
    for (judged = 0ul; judged < 6ul; ++judged) {
        v[judged].sz = 1.0f - v[judged].sz;
        v[judged].color = 0xffff0000ul;
    }
    d32_hx("T8XRedHr", (DWORD)d32_draw(device, v, 6ul));
    d32_vertex(&v[0], 560.0f, 80.0f, 0.2f, 0xff00ff00ul);
    d32_vertex(&v[1], 624.0f, 80.0f, 0.2f, 0xff00ff00ul);
    d32_vertex(&v[2], 560.0f, 144.0f, 0.8f, 0xff00ff00ul);
    d32_vertex(&v[3], 624.0f, 80.0f, 0.2f, 0xff00ff00ul);
    d32_vertex(&v[4], 624.0f, 144.0f, 0.8f, 0xff00ff00ul);
    d32_vertex(&v[5], 560.0f, 144.0f, 0.8f, 0xff00ff00ul);
    d32_hx("T8YGreenHr", (DWORD)d32_draw(device, v, 6ul));
    for (judged = 0ul; judged < 6ul; ++judged) {
        v[judged].sz = 1.0f - v[judged].sz;
        v[judged].color = 0xffff0000ul;
    }
    d32_hx("T8YRedHr", (DWORD)d32_draw(device, v, 6ul));
    /* The x pair again at (560, 152) with rhw 1.0 on the left and 0.2 on
     * the right, as a perspective scene sends: screen z is still linear,
     * so the crossing must not move. */
    d32_vertex(&v[0], 560.0f, 152.0f, 0.2f, 0xff00ff00ul);
    d32_vertex(&v[1], 624.0f, 152.0f, 0.8f, 0xff00ff00ul);
    d32_vertex(&v[2], 560.0f, 216.0f, 0.2f, 0xff00ff00ul);
    d32_vertex(&v[3], 624.0f, 152.0f, 0.8f, 0xff00ff00ul);
    d32_vertex(&v[4], 624.0f, 216.0f, 0.8f, 0xff00ff00ul);
    d32_vertex(&v[5], 560.0f, 216.0f, 0.2f, 0xff00ff00ul);
    v[1].rhw = 0.2f;
    v[3].rhw = 0.2f;
    v[4].rhw = 0.2f;
    d32_hx("T8WGreenHr", (DWORD)d32_draw(device, v, 6ul));
    for (judged = 0ul; judged < 6ul; ++judged) {
        v[judged].sz = 1.0f - v[judged].sz;
        v[judged].color = 0xffff0000ul;
    }
    d32_hx("T8WRedHr", (DWORD)d32_draw(device, v, 6ul));
    d32_state(device, D3DRENDERSTATE_ZENABLE, 0ul);
    d32_zstats(zbuffer, zbits, "ZDepth");

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

    /* T9: DrawIndexedPrimitive under Direct3D's default culling (CCW).
     * With screen y down, a triangle whose (v1-v0) x (v2-v0) is positive
     * runs clockwise on the screen and is kept; a negative one is culled.
     * Two single triangles, one of each, then a quad of two triangles
     * sharing an edge through indices: the culling and the index handling,
     * whichever of the core or the hardware does them. */
    d32_line("Stage", "8f-t9-indexed-cull");
    d32_path_read(&t9_before);
    d32_state(device, D3DRENDERSTATE_CULLMODE, D3DCULL_CCW);
    d32_vertex(&v[0], 8.0f, 300.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[1], 72.0f, 300.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[2], 8.0f, 364.0f, 0.5f, 0xff00ff00ul);
    indices[0] = 0u;
    indices[1] = 1u;
    indices[2] = 2u;
    d32_hx("T9KeptHr", (DWORD)d32_draw_indexed(device, v, 3ul, indices, 3ul));
    d32_vertex(&v[0], 100.0f, 300.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[1], 100.0f, 364.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[2], 164.0f, 300.0f, 0.5f, 0xff00ff00ul);
    d32_hx("T9CulledHr",
           (DWORD)d32_draw_indexed(device, v, 3ul, indices, 3ul));
    d32_vertex(&v[0], 200.0f, 300.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[1], 264.0f, 300.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[2], 200.0f, 364.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[3], 264.0f, 364.0f, 0.5f, 0xff00ff00ul);
    indices[3] = 2u;
    indices[4] = 1u;
    indices[5] = 3u;
    d32_hx("T9QuadHr", (DWORD)d32_draw_indexed(device, v, 4ul, indices, 6ul));
    d32_state(device, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    d32_path_read(&t9_after);
    d32_path_write("T9", &t9_before, &t9_after);

    /* T10: triangles reaching off the target, inside a 2048 guard band.
     * Whether the core clips them or the engine draws them under its
     * scissor, the visible pixels must be the same: a band y 400-439
     * filled whole by one huge flat triangle, a Gouraud triangle crossing
     * three edges into y 440-479, and nothing above the band, nor in the
     * next row where a triangle past the right edge would wrap. */
    d32_line("Stage", "8g-t10-guard-band");
    d32_vertex(&v[0], -1500.0f, 400.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[1], 2000.0f, 400.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[2], 320.0f, 1800.0f, 0.5f, 0xff00ff00ul);
    d32_hx("T10FlatHr", (DWORD)d32_draw(device, v, 3ul));
    d32_vertex(&v[0], -800.0f, 440.0f, 0.5f, 0xffff0000ul);
    d32_vertex(&v[1], 1400.0f, 440.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[2], 300.0f, 1600.0f, 0.5f, 0xff0000fful);
    d32_hx("T10GouraudHr", (DWORD)d32_draw(device, v, 3ul));
    /* Off the right edge only, rows 380-388, the row above the band. */
    d32_vertex(&v[0], 600.0f, 380.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[1], 760.0f, 380.0f, 0.5f, 0xff00ff00ul);
    d32_vertex(&v[2], 600.0f, 388.0f, 0.5f, 0xff00ff00ul);
    d32_hx("T10RightHr", (DWORD)d32_draw(device, v, 3ul));

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

        /* T9: the kept triangle drawn, the culled one not, both halves of
         * the indexed quad drawn. */
        d32_hx("T9Kept", d32_pixel(&desc, 16ul, 308ul));
        d32_hx("T9Culled", d32_pixel(&desc, 108ul, 308ul));
        d32_hx("T9QuadUpper", d32_pixel(&desc, 210ul, 310ul));
        d32_hx("T9QuadLower", d32_pixel(&desc, 254ul, 354ul));
        caps = (d32_is_colour(d32_pixel(&desc, 16ul, 308ul), 0x0000ff00ul)
                    ? 1ul : 0ul) |
               (!d32_is_colour(d32_pixel(&desc, 108ul, 308ul), 0x0000ff00ul)
                    ? 2ul : 0ul) |
               (d32_is_colour(d32_pixel(&desc, 210ul, 310ul), 0x0000ff00ul)
                    ? 4ul : 0ul) |
               (d32_is_colour(d32_pixel(&desc, 254ul, 354ul), 0x0000ff00ul)
                    ? 8ul : 0ul);
        d32_hx("T9Ok", caps);
        failures += caps != 15ul ? 1ul : 0ul;

        /* T10: the band whole, its edges; the Gouraud samples, recorded so
         * two builds can be compared pixel for pixel; the right-edge piece
         * drawn, and nothing wrapped into the left of its rows or drawn
         * between it and the band. */
        d32_hx("T10BandLeft", d32_pixel(&desc, 0ul, 402ul));
        d32_hx("T10BandMiddle", d32_pixel(&desc, 320ul, 420ul));
        d32_hx("T10BandRight", d32_pixel(&desc, 639ul, 438ul));
        d32_hx("T10AboveBand", d32_pixel(&desc, 320ul, 396ul));
        d32_hx("T10Gouraud0", d32_pixel(&desc, 0ul, 445ul));
        d32_hx("T10Gouraud1", d32_pixel(&desc, 320ul, 460ul));
        d32_hx("T10Gouraud2", d32_pixel(&desc, 639ul, 478ul));
        d32_hx("T10RightPiece", d32_pixel(&desc, 630ul, 381ul));
        d32_hx("T10WrapRow", d32_pixel(&desc, 2ul, 382ul));
        caps = (d32_is_colour(d32_pixel(&desc, 0ul, 402ul), 0x0000ff00ul)
                    ? 1ul : 0ul) |
               (d32_is_colour(d32_pixel(&desc, 320ul, 420ul), 0x0000ff00ul)
                    ? 2ul : 0ul) |
               (d32_is_colour(d32_pixel(&desc, 639ul, 438ul), 0x0000ff00ul)
                    ? 4ul : 0ul) |
               (!d32_is_colour(d32_pixel(&desc, 320ul, 396ul), 0x0000ff00ul)
                    ? 8ul : 0ul) |
               (d32_is_colour(d32_pixel(&desc, 630ul, 381ul), 0x0000ff00ul)
                    ? 16ul : 0ul) |
               (!d32_is_colour(d32_pixel(&desc, 2ul, 382ul), 0x0000ff00ul)
                    ? 32ul : 0ul);
        d32_hx("T10Ok", caps);
        failures += caps != 63ul ? 1ul : 0ul;

        d32_hx("T5Pixel", d32_pixel(&desc, 332ul, 40ul));
        caps = d32_is_colour(d32_pixel(&desc, 332ul, 40ul),
                             zbits == 32ul ? 0x00ff0000ul : 0x0000ff00ul)
                   ? 1ul : 0ul;
        d32_hx("T5Ok", caps);
        failures += caps != 1ul ? 1ul : 0ul;

        /* T8: green on the near side of each centre, red past it, and the
         * first red pixel along the slope - 592 and 112 when the planes
         * cross where their vertices say they do. */
        {
            DWORD at;
            DWORD cross;

            cross = 0ul;
            for (at = 560ul; at < 624ul; ++at) {
                if (d32_is_colour(d32_pixel(&desc, at, 40ul), 0x00ff0000ul)) {
                    cross = at;
                    break;
                }
            }
            d32_hx("T8XCross", cross);
            cross = 0ul;
            for (at = 80ul; at < 144ul; ++at) {
                if (d32_is_colour(d32_pixel(&desc, 592ul, at), 0x00ff0000ul)) {
                    cross = at;
                    break;
                }
            }
            d32_hx("T8YCross", cross);
            cross = 0ul;
            for (at = 560ul; at < 624ul; ++at) {
                if (d32_is_colour(d32_pixel(&desc, at, 184ul),
                                  0x00ff0000ul)) {
                    cross = at;
                    break;
                }
            }
            d32_hx("T8WCross", cross);
        }
        d32_hx("T8XNear", d32_pixel(&desc, 568ul, 40ul));
        d32_hx("T8XFar", d32_pixel(&desc, 616ul, 40ul));
        d32_hx("T8YNear", d32_pixel(&desc, 592ul, 88ul));
        d32_hx("T8YFar", d32_pixel(&desc, 592ul, 136ul));
        caps = (d32_is_colour(d32_pixel(&desc, 568ul, 40ul), 0x0000ff00ul)
                    ? 1ul : 0ul) |
               (d32_is_colour(d32_pixel(&desc, 616ul, 40ul), 0x00ff0000ul)
                    ? 2ul : 0ul) |
               (d32_is_colour(d32_pixel(&desc, 592ul, 88ul), 0x0000ff00ul)
                    ? 4ul : 0ul) |
               (d32_is_colour(d32_pixel(&desc, 592ul, 136ul), 0x00ff0000ul)
                    ? 8ul : 0ul);
        d32_hx("T8Ok", caps);
        failures += caps != 15ul ? 1ul : 0ul;

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

        /*
         * T7: half red over green, about 0x80 and 0x7F - averaged over an
         * 8x8 block, because an engine with no blender (the MGA-2164W)
         * draws source alpha as a half stipple, which is every other pixel
         * red and averages to the same colour a blend gives.
         */
        d32_hx("T7Pixel", d32_pixel(&desc, 512ul, 40ul));
        {
            DWORD sum[3];
            DWORD x;
            DWORD y;
            DWORD pixel;

            sum[0] = 0ul;
            sum[1] = 0ul;
            sum[2] = 0ul;
            for (y = 36ul; y < 44ul; ++y) {
                for (x = 508ul; x < 516ul; ++x) {
                    pixel = d32_pixel(&desc, x, y);
                    sum[0] += d32_channel(pixel, d32_masks[0]);
                    sum[1] += d32_channel(pixel, d32_masks[1]);
                    sum[2] += d32_channel(pixel, d32_masks[2]);
                }
            }
            /* Eight bits a channel already, so compared as they are and
             * not through the mode's masks. */
            pixel = ((sum[0] / 64ul) << 16) | ((sum[1] / 64ul) << 8) |
                    (sum[2] / 64ul);
            d32_hx("T7Average", pixel);
            caps = d32_difference(sum[0] / 64ul, 0x80ul) <= 8ul &&
                           d32_difference(sum[1] / 64ul, 0x7ful) <= 8ul &&
                           sum[2] / 64ul <= 8ul
                       ? 1ul : 0ul;
        }
        d32_hx("T7Ok", caps);
        failures += caps != 1ul ? 1ul : 0ul;
        ((d32_unlock_fn)back->vtbl[DDS_UNLOCK])(back, desc.lpSurface);
    } else {
        ++failures;
    }

    /*
     * The Z buffer's own contents, recorded and not judged: whether the clear
     * and the depth tests wrote what they should can be told apart there. The
     * cell at each sample point is read twice - at the linear address, and
     * at the Y-tiled one (tiles of 128 bytes by 32 rows, sixteen-byte columns
     * of 32 rows inside) - because an engine may place its Z buffer either
     * way and this probe cannot ask which.
     */
    d32_line("Stage", "9b-z-read-back");
    d32_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = ((d32_lock_fn)zbuffer->vtbl[DDS_LOCK])(zbuffer, 0, &desc,
                                                DDLOCK_WAIT, 0);
    d32_hx("ZLockHr", (DWORD)hr);
    if (hr == 0 && desc.lpSurface != 0) {
        static const char *const names[5] = {
            "Origin", "T3Front", "T3Kept", "T5", "Untouched" };
        static const DWORD xs[5] = { 0ul, 204ul, 250ul, 332ul, 600ul };
        static const DWORD ys[5] = { 0ul, 20ul, 20ul, 40ul, 400ul };
        DWORD cell = zbits == 32ul ? 4ul : 2ul;
        DWORD pitch = (DWORD)desc.lPitch;
        DWORD point;

        d32_hx("ZPitch", pitch);
        for (point = 0ul; point < 5ul; ++point) {
            DWORD xb = xs[point] * cell;
            DWORD y = ys[point];
            DWORD linear = y * pitch + xb;
            DWORD tiled = (y / 32ul) * pitch * 32ul + (xb / 128ul) * 4096ul +
                          ((xb % 128ul) / 16ul) * 512ul + (y % 32ul) * 16ul +
                          (xb % 16ul);
            const BYTE *base = (const BYTE *)desc.lpSurface;
            char key[32];
            DWORD value;

            value = cell == 4ul ? *(const DWORD *)(base + linear)
                                : (DWORD)*(const WORD *)(base + linear);
            wsprintfA(key, "ZLinear%s", names[point]);
            d32_hx(key, value);
            value = cell == 4ul ? *(const DWORD *)(base + tiled)
                                : (DWORD)*(const WORD *)(base + tiled);
            wsprintfA(key, "ZTiled%s", names[point]);
            d32_hx(key, value);
        }
        ((d32_unlock_fn)zbuffer->vtbl[DDS_UNLOCK])(zbuffer, desc.lpSurface);
    }

    failures += d32_cull_matrix(device, back);
    failures += d32_volume(device, back);

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
    /* T12 after the DX5 device is gone, on the same back buffer. */
    failures += d32_two_stage(ddraw, back, texture_surface);
    failures += d32_dx7(ddraw, back);
    failures += d32_tnl();
    d32_d3d89();
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
    failures += d32_child_and_after(window, bpp);
    failures += d32_sentinel_verify();
    DestroyWindow(window);
    /* The run's interface, by what it did: DrawPrimitives2 calls are DDI 6. */
    d32_path_read(&run_after);
    d32_hx("RunDp2Calls", run_after.dp2_calls - run_before.dp2_calls);
    d32_line("RunDdi", !run_before.ok || !run_after.ok ? "unknown"
                       : run_after.dp2_calls != run_before.dp2_calls ? "6"
                                                                     : "5");
    /* A wait the engine gave up on is a failure whatever the pixels say:
     * the driver drew the rest on the CPU and hid it. */
    if (run_before.ok && run_after.ok) {
        d32_hx("RunIdleTimeouts",
               run_after.idle_timeouts - run_before.idle_timeouts);
        d32_hx("RunFifoTimeouts",
               run_after.fifo_timeouts - run_before.fifo_timeouts);
        d32_hx("RunEngineFlags", run_after.engine_flags);
        failures += run_after.idle_timeouts != run_before.idle_timeouts ||
                    run_after.fifo_timeouts != run_before.fifo_timeouts
                        ? 1ul : 0ul;
    }
    d32_hx("Failures", failures);
    d32_finish(failures == 0ul ? "PASS" : "FAIL", failures == 0ul ? 0u : 1u);
}
