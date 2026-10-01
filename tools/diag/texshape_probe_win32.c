/* Texture-shape probe: non-square textures through the HAL's DrawPrimitives,
 * read back pixel by pixel. Like the record clip probe, it calls the HAL
 * directly, holding the Win16 mutex exactly as DDRAW does, with no mode
 * switch.
 *
 * Two questions, for the Mach64 first (2026-10-01):
 *
 * 1. Single level. A wide (w > h) and a tall (h > w) texture, each filled
 *    with a pattern that names its texel, drawn nearest and COPY onto a
 *    64x64 quad with u and v 0 to 1. Every edge is at most 64 and divides
 *    64, so each pixel centre lies inside one texel whatever the sampler's
 *    sub-texel convention, and the whole image can be compared. A wide
 *    texture reads the same under a row pitch of its width or of its larger
 *    edge; a tall one does not, so the pair says which the engine uses.
 *
 * 2. Mip chains. A 128x32 and a 32x128 chain, every level filled with the
 *    same kind of pattern plus a level tag, drawn MIPNEAREST at each
 *    level's own size from 64x16 down. Each draw is compared against every
 *    level's expected image; the best match names the level the engine
 *    chose and its mismatch count says whether that level's layout is
 *    right.
 *
 * 3. Large textures, Gen3 only (2026-10-02): single levels from 256 to
 *    1024 and full 512 and 1024 chains, minified onto the target, to say
 *    whether the sampler reads past the 256 the engine's limit was set at
 *    from one measured 32x32 map. Their texel pattern carries x and y mod
 *    32 exactly, so an error of one texel shows.
 *
 * Writes C:\V9XDIAG\V9XTSHP.INI and a PPM per draw. Result=PASS only when
 * every single-level image is exact, every chain draw matches some level
 * exactly, and the engine counted no refusal, timeout or reset.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include "velocity9x/build.h"
#include "velocity9x/win9x_ddraw_abi.h"

#define V9X_TSP_PATH "C:\\V9XDIAG\\V9XTSHP.INI"
#define V9X_TSP_EDGE 64ul
#define V9X_TSP_PIXELS (V9X_TSP_EDGE * V9X_TSP_EDGE)
#define V9X_TSP_STREAM_BYTES 4096ul
#define V9X_TSP_LIST 4u
#define V9X_TSP_TL 3u
#define V9X_TSP_LEVELS 12u

typedef HRESULT (WINAPI *V9X_TSP_CREATE)(GUID *, LPDIRECTDRAW *, IUnknown *);
typedef void (WINAPI *V9X_TSP_GETLOCK)(void **);
typedef void (WINAPI *V9X_TSP_LEVEL)(void *);
typedef DWORD (WINAPI *V9X_TSP_CONTEXT)(V9X_D3DHAL_CONTEXTCREATEDATA *);
typedef DWORD (WINAPI *V9X_TSP_DESTROY)(V9X_D3DHAL_CONTEXTDESTROYDATA *);
typedef DWORD (WINAPI *V9X_TSP_TEXCREATE)(V9X_D3DHAL_TEXTURECREATEDATA *);
typedef DWORD (WINAPI *V9X_TSP_TEXDESTROY)(V9X_D3DHAL_TEXTUREDESTROYDATA *);
typedef DWORD (WINAPI *V9X_TSP_INFO)(V9X_DDHAL_GETDRIVERINFODATA *);
typedef DWORD (WINAPI *V9X_TSP_DRAW)(V9X_D3DHAL_DRAWPRIMITIVESDATA *);

static V9X_TSP_LEVEL v9x_tsp_enter, v9x_tsp_leave;
static void *v9x_tsp_lock;
static WORD v9x_tsp_pixels[V9X_TSP_PIXELS];
static WORD v9x_tsp_expect[V9X_TSP_PIXELS];
static BYTE v9x_tsp_stream[V9X_TSP_STREAM_BYTES];

static void v9x_tsp_zero(void *p, DWORD n)
{
    DWORD i;
    for (i = 0ul; i < n; ++i) { ((BYTE *)p)[i] = 0u; }
}
static void v9x_tsp_text(const char *key, const char *value)
{
    WritePrivateProfileStringA("TexShapeProbe", key, value, V9X_TSP_PATH);
}
static void v9x_tsp_uint(const char *key, DWORD value)
{
    char text[24];
    wsprintfA(text, "%lu", value);
    v9x_tsp_text(key, text);
}
static void v9x_tsp_hex(const char *key, DWORD value)
{
    char text[24];
    wsprintfA(text, "0x%08lX", value);
    v9x_tsp_text(key, text);
}
/* KERNEL32's Win16-mutex ordinals, by the same walk the other probes use. */
static FARPROC v9x_tsp_ordinal(DWORD ordinal)
{
    const BYTE *image = (const BYTE *)GetModuleHandleA("KERNEL32.DLL");
    DWORD pe, directory, bytes, functions, rva;
    const DWORD *exports;
    if (image == 0 || IsBadReadPtr(image, 64u)) { return 0; }
    pe = *(const DWORD *)(image + 60ul);
    if (IsBadReadPtr(image + pe, 248u) ||
        *(const DWORD *)(image + pe) != 0x00004550ul) { return 0; }
    directory = *(const DWORD *)(image + pe + 120ul);
    bytes = *(const DWORD *)(image + pe + 124ul);
    if (directory == 0ul || IsBadReadPtr(image + directory, 40u)) { return 0; }
    exports = (const DWORD *)(image + directory);
    if (ordinal < exports[4] || ordinal - exports[4] >= exports[5] ||
        exports[5] > 65536ul) { return 0; }
    functions = exports[7];
    if (IsBadReadPtr(image + functions, exports[5] * 4ul)) { return 0; }
    rva = ((const DWORD *)(image + functions))[ordinal - exports[4]];
    if (rva == 0ul || (rva >= directory && rva - directory < bytes)) { return 0; }
    return (FARPROC)(image + rva);
}

/* The texel at (x, y) of a w x h level tagged `tag`, in RGB565: red and
 * blue spread x and y over their ranges, green carries the tag and the low
 * bits of x and y so neighbouring texels always differ. */
static WORD v9x_tsp_texel(DWORD x, DWORD y, DWORD w, DWORD h, DWORD tag)
{
    DWORD r = w > 1ul ? (x * 31ul) / (w - 1ul) : 0ul;
    DWORD b = h > 1ul ? (y * 31ul) / (h - 1ul) : 0ul;
    DWORD g = ((tag & 7ul) << 3) | ((x & 1ul) << 2) | ((y & 1ul) << 1) |
              ((x ^ y) & 1ul);

    /* Past 64 the spread would repeat over runs of texels: red and blue are
     * x and y mod 32, and green the tag and the 32-texel blocks. */
    if (w > V9X_TSP_EDGE || h > V9X_TSP_EDGE) {
        r = x & 31ul;
        b = y & 31ul;
        g = ((tag & 7ul) << 3) | (((x >> 5) + (y >> 5) * 3ul) & 7ul);
    }
    return (WORD)((r << 11) | (g << 5) | b);
}

static int v9x_tsp_fill(LPDIRECTDRAWSURFACE surface, DWORD tag)
{
    DDSURFACEDESC desc;
    DWORD x, y;
    v9x_tsp_zero(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
    if (IDirectDrawSurface_Lock(surface, 0, &desc, DDLOCK_WAIT, 0) != DD_OK) { return 0; }
    for (y = 0ul; y < desc.dwHeight; ++y) {
        WORD *row = (WORD *)((BYTE *)desc.lpSurface + y * (DWORD)desc.lPitch);
        for (x = 0ul; x < desc.dwWidth; ++x) {
            row[x] = v9x_tsp_texel(x, y, desc.dwWidth, desc.dwHeight, tag);
        }
    }
    return IDirectDrawSurface_Unlock(surface, 0) == DD_OK;
}

/* What a nearest sampler reads into a dw x dh region from a w x h level:
 * texel floor((p + 0.5) * w / dw). Pixels outside the region stay 0. */
static void v9x_tsp_expected(DWORD w, DWORD h, DWORD dw, DWORD dh, DWORD tag)
{
    DWORD x, y;
    v9x_tsp_zero(v9x_tsp_expect, sizeof(v9x_tsp_expect));
    for (y = 0ul; y < dh; ++y) {
        for (x = 0ul; x < dw; ++x) {
            DWORD tx = ((2ul * x + 1ul) * w) / (2ul * dw);
            DWORD ty = ((2ul * y + 1ul) * h) / (2ul * dh);
            v9x_tsp_expect[y * V9X_TSP_EDGE + x] = v9x_tsp_texel(tx, ty, w, h, tag);
        }
    }
}
static DWORD v9x_tsp_mismatch(void)
{
    DWORD i, count = 0ul;
    for (i = 0ul; i < V9X_TSP_PIXELS; ++i) {
        if (v9x_tsp_pixels[i] != v9x_tsp_expect[i]) { ++count; }
    }
    return count;
}

static void v9x_tsp_vertex(V9X_D3DTLVERTEX *v, float x, float y, float u, float t)
{
    v->sx = x; v->sy = y; v->sz = 0.5f; v->rhw = 1.0f;
    v->color = 0xfffffffful; v->specular = 0xff000000ul;
    v->tu = u; v->tv = t;
}

/* One record: the state pairs, then a dw x dh quad as two triangles. */
static int v9x_tsp_draw(V9X_TSP_DRAW callback, DWORD context, DWORD handle,
                        DWORD min_filter, DWORD dw, DWORD dh)
{
    V9X_D3DHAL_DRAWPRIMITIVESDATA data;
    V9X_D3DHAL_DRAWPRIMCOUNTS *counts;
    BYTE *begin = (BYTE *)(((DWORD)v9x_tsp_stream + 31ul) & ~31ul);
    BYTE *cursor = begin;
    DWORD *pairs;
    V9X_D3DTLVERTEX *v;
    float x = (float)dw;
    float y = (float)dh;
    int ok;

    v9x_tsp_zero(v9x_tsp_stream, sizeof(v9x_tsp_stream));
    counts = (V9X_D3DHAL_DRAWPRIMCOUNTS *)cursor;
    counts->wNumStateChanges = 13u;
    counts->wPrimitiveType = V9X_TSP_LIST;
    counts->wVertexType = V9X_TSP_TL;
    counts->wNumVertices = 6u;
    cursor += sizeof(*counts);
    pairs = (DWORD *)cursor;
    pairs[0] = V9X_D3DRENDERSTATE_TEXTUREHANDLE; pairs[1] = handle;
    pairs[2] = V9X_D3DRENDERSTATE_TEXTUREMAPBLEND; pairs[3] = 7ul;   /* COPY */
    pairs[4] = V9X_D3DRENDERSTATE_TEXTUREMAG; pairs[5] = 1ul;        /* NEAREST */
    pairs[6] = V9X_D3DRENDERSTATE_TEXTUREMIN; pairs[7] = min_filter;
    /* WRAP, the default the passing halves scene uses: pixel centres
     * never reach u or v of 1, so it samples as CLAMP would. */
    pairs[8] = V9X_D3DRENDERSTATE_TEXTUREADDRESS; pairs[9] = 1ul;    /* WRAP */
    pairs[10] = V9X_D3DRENDERSTATE_CULLMODE; pairs[11] = 1ul;        /* NONE */
    pairs[12] = V9X_D3DRENDERSTATE_ZENABLE; pairs[13] = 0ul;
    pairs[14] = V9X_D3DRENDERSTATE_ZWRITEENABLE; pairs[15] = 0ul;
    pairs[16] = V9X_D3DRENDERSTATE_ALPHABLENDENABLE; pairs[17] = 0ul;
    pairs[18] = V9X_D3DRENDERSTATE_SPECULARENABLE; pairs[19] = 0ul;
    pairs[20] = V9X_D3DRENDERSTATE_FOGENABLE; pairs[21] = 0ul;
    pairs[22] = 5ul; pairs[23] = 0ul;                               /* WRAPU */
    pairs[24] = 6ul; pairs[25] = 0ul;                               /* WRAPV */
    cursor += 13ul * 8ul;
    cursor = (BYTE *)(((DWORD)cursor + 31ul) & ~31ul);
    v = (V9X_D3DTLVERTEX *)cursor;
    v9x_tsp_vertex(&v[0], 0.0f, 0.0f, 0.0f, 0.0f);
    v9x_tsp_vertex(&v[1], x, 0.0f, 1.0f, 0.0f);
    v9x_tsp_vertex(&v[2], 0.0f, y, 0.0f, 1.0f);
    v9x_tsp_vertex(&v[3], x, 0.0f, 1.0f, 0.0f);
    v9x_tsp_vertex(&v[4], x, y, 1.0f, 1.0f);
    v9x_tsp_vertex(&v[5], 0.0f, y, 0.0f, 1.0f);
    cursor += 6ul * sizeof(V9X_D3DTLVERTEX);
    v9x_tsp_zero(cursor, sizeof(V9X_D3DHAL_DRAWPRIMCOUNTS));
    v9x_tsp_zero(&data, sizeof(data));
    data.dwhContext = context;
    data.lpvData = begin;
    data.ddrval = 0xfffffffful;
    v9x_tsp_enter(v9x_tsp_lock);
    ok = callback(&data) == V9X_DDHAL_DRIVER_HANDLED && data.ddrval == 0ul;
    v9x_tsp_leave(v9x_tsp_lock);
    return ok;
}

static int v9x_tsp_clear(LPDIRECTDRAWSURFACE target)
{
    DDBLTFX fx;
    v9x_tsp_zero(&fx, sizeof(fx)); fx.dwSize = sizeof(fx);
    return IDirectDrawSurface_Blt(target, 0, 0, 0, DDBLT_COLORFILL | DDBLT_WAIT, &fx) == DD_OK;
}
static int v9x_tsp_capture(LPDIRECTDRAWSURFACE target)
{
    DDSURFACEDESC desc;
    DWORD x, y;
    v9x_tsp_zero(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
    if (IDirectDrawSurface_Lock(target, 0, &desc, DDLOCK_WAIT, 0) != DD_OK) { return 0; }
    for (y = 0ul; y < V9X_TSP_EDGE; ++y) {
        const WORD *row = (const WORD *)((const BYTE *)desc.lpSurface + y * (DWORD)desc.lPitch);
        for (x = 0ul; x < V9X_TSP_EDGE; ++x) { v9x_tsp_pixels[y * V9X_TSP_EDGE + x] = row[x]; }
    }
    return IDirectDrawSurface_Unlock(target, 0) == DD_OK;
}
static void v9x_tsp_image(const char *name)
{
    char path[80];
    const char header[] = "P6\n64 64\n255\n";
    static BYTE rgb[V9X_TSP_PIXELS * 3ul];
    HANDLE file;
    DWORD i, wrote;
    for (i = 0ul; i < V9X_TSP_PIXELS; ++i) {
        DWORD value = v9x_tsp_pixels[i];
        rgb[i * 3ul] = (BYTE)(((value >> 11) & 31ul) * 255ul / 31ul);
        rgb[i * 3ul + 1ul] = (BYTE)(((value >> 5) & 63ul) * 255ul / 63ul);
        rgb[i * 3ul + 2ul] = (BYTE)((value & 31ul) * 255ul / 31ul);
    }
    wsprintfA(path, "C:\\V9XDIAG\\%s.PPM", name);
    file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) { return; }
    (void)WriteFile(file, header, sizeof(header) - 1u, &wrote, 0);
    (void)WriteFile(file, rgb, sizeof(rgb), &wrote, 0);
    CloseHandle(file);
}

/* An RGB565 texture in video memory, with a mip chain when levels > 1. */
static LPDIRECTDRAWSURFACE v9x_tsp_texture(LPDIRECTDRAW dd, DWORD w, DWORD h,
                                           DWORD levels, HRESULT *hr_out)
{
    DDSURFACEDESC desc;
    LPDIRECTDRAWSURFACE surface = 0;
    v9x_tsp_zero(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = w; desc.dwHeight = h;
    desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY;
    if (levels > 1ul) {
        desc.dwFlags |= DDSD_MIPMAPCOUNT;
        desc.dwMipMapCount = levels;
        desc.ddsCaps.dwCaps |= DDSCAPS_MIPMAP | DDSCAPS_COMPLEX;
    }
    desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount = 16ul;
    desc.ddpfPixelFormat.dwRBitMask = 0xf800ul;
    desc.ddpfPixelFormat.dwGBitMask = 0x07e0ul;
    desc.ddpfPixelFormat.dwBBitMask = 0x001ful;
    *hr_out = IDirectDraw_CreateSurface(dd, &desc, &surface, 0);
    return *hr_out == DD_OK ? surface : 0;
}

/* Each level of a chain filled with its own pattern, tagged by level. */
static DWORD v9x_tsp_fill_chain(LPDIRECTDRAWSURFACE top, DWORD *pitch_out)
{
    LPDIRECTDRAWSURFACE level = top;
    DDSCAPS caps;
    DWORD count = 0ul;
    while (level != 0 && count < V9X_TSP_LEVELS) {
        DDSURFACEDESC desc;
        LPDIRECTDRAWSURFACE next = 0;
        v9x_tsp_zero(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
        if (IDirectDrawSurface_GetSurfaceDesc(level, &desc) == DD_OK) {
            pitch_out[count] = ((DWORD)desc.lPitch << 16) | (desc.dwWidth << 8) | desc.dwHeight;
        }
        if (!v9x_tsp_fill(level, count + 1ul)) { break; }
        ++count;
        caps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_MIPMAP;
        if (IDirectDrawSurface_GetAttachedSurface(level, &caps, &next) != DD_OK) { next = 0; }
        if (level != top) { IDirectDrawSurface_Release(level); }
        level = next;
    }
    if (level != 0 && level != top) { IDirectDrawSurface_Release(level); }
    return count;
}

void __stdcall V9xTexShapeProbeEntry(void)
{
    static const BYTE guid[16] = {0xe1u,0x84u,0xa5u,0x0bu,0xb6u,0x70u,0xd0u,0x11u,0x88u,0x9du,0u,0xaau,0u,0xbbu,0xb7u,0x6au};
    /* Single-level shapes: wide and tall pairs, and a square control. */
    static const DWORD single_w[9] = { 32ul, 64ul, 8ul, 64ul, 16ul, 64ul, 32ul, 32ul, 8ul };
    static const DWORD single_h[9] = { 32ul, 8ul, 64ul, 16ul, 64ul, 32ul, 64ul, 8ul, 32ul };
    static const DWORD chain_w[2] = { 128ul, 32ul };
    static const DWORD chain_h[2] = { 32ul, 128ul };
    LPDIRECTDRAW dd = 0;
    LPDIRECTDRAWSURFACE target = 0;
    V9X_TSP_CREATE create;
    V9X_TSP_GETLOCK getlock;
    V9X_TSP_CONTEXT context_create;
    V9X_TSP_DESTROY context_destroy;
    V9X_TSP_TEXCREATE texture_create;
    V9X_TSP_TEXDESTROY texture_destroy;
    V9X_TSP_INFO info;
    V9X_TSP_DRAW draw;
    V9X_DD_SHARED *shared;
    V9X_DD32BITDRIVERDATA driver;
    V9X_DCICMD command;
    V9X_DDHAL_GETDRIVERINFODATA request;
    V9X_D3DHAL_CALLBACKS2 callbacks;
    V9X_D3DHAL_CONTEXTCREATEDATA context;
    V9X_D3DHAL_CONTEXTDESTROYDATA destroy;
    DDSURFACEDESC desc;
    HDC dc;
    DWORD i, test, level, mismatch, before_refused, before_draws;
    DWORD baseline_refused, baseline_fifo, baseline_idle, baseline_resets;
    char key[48], name[16];
    int result, ok = 1;

    CreateDirectoryA("C:\\V9XDIAG", 0);
    DeleteFileA(V9X_TSP_PATH);
    v9x_tsp_text("Build", V9X_BUILD_ID); v9x_tsp_text("Result", "RUNNING");
    create = (V9X_TSP_CREATE)GetProcAddress(LoadLibraryA("DDRAW.DLL"), "DirectDrawCreate");
    if (create == 0 || create(0, &dd, 0) != DD_OK) { v9x_tsp_text("Result", "FAIL-DDRAW"); ExitProcess(1u); }
    if (IDirectDraw_SetCooperativeLevel(dd, 0, DDSCL_NORMAL) != DD_OK) { ExitProcess(1u); }
    v9x_tsp_zero(&driver, sizeof(driver)); v9x_tsp_zero(&command, sizeof(command));
    command.dwCommand = V9X_DDGET32BITDRIVERNAME; command.dwVersion = V9X_DD_VERSION;
    dc = GetDC(0);
    result = dc ? ExtEscape(dc, V9X_DCICOMMAND, sizeof(command), (LPCSTR)&command, sizeof(driver), (LPSTR)&driver) : 0;
    if (dc) { ReleaseDC(0, dc); }
    shared = (V9X_DD_SHARED *)driver.dwContext;
    if (result <= 0 || shared == 0 || IsBadReadPtr(shared, sizeof(*shared)) ||
        shared->dwSize != sizeof(*shared) || shared->abi != V9X_DD_SHARED_ABI) {
        v9x_tsp_text("Result", "FAIL-SHARED"); ExitProcess(1u);
    }
    v9x_tsp_uint("EngineType", shared->engine.engine_type);
    getlock = (V9X_TSP_GETLOCK)v9x_tsp_ordinal(93ul);
    v9x_tsp_enter = (V9X_TSP_LEVEL)v9x_tsp_ordinal(97ul);
    v9x_tsp_leave = (V9X_TSP_LEVEL)v9x_tsp_ordinal(98ul);
    if (!getlock || !v9x_tsp_enter || !v9x_tsp_leave) { v9x_tsp_text("Result", "FAIL-MUTEX"); ExitProcess(1u); }
    getlock(&v9x_tsp_lock);
    if (!v9x_tsp_lock) { ExitProcess(1u); }
    info = (V9X_TSP_INFO)shared->info.GetDriverInfo;
    context_create = (V9X_TSP_CONTEXT)shared->d3d_callbacks.ContextCreate;
    context_destroy = (V9X_TSP_DESTROY)shared->d3d_callbacks.ContextDestroy;
    texture_create = (V9X_TSP_TEXCREATE)shared->d3d_callbacks.TextureCreate;
    texture_destroy = (V9X_TSP_TEXDESTROY)shared->d3d_callbacks.TextureDestroy;
    if (!info || !context_create || !context_destroy || !texture_create || !texture_destroy) {
        v9x_tsp_text("Result", "FAIL-CALLBACKS"); ExitProcess(1u);
    }
    v9x_tsp_zero(&request, sizeof(request)); v9x_tsp_zero(&callbacks, sizeof(callbacks));
    request.dwSize = sizeof(request); request.dwExpectedSize = sizeof(callbacks); request.lpvData = &callbacks;
    for (i = 0ul; i < sizeof(guid); ++i) { request.guidInfo[i] = guid[i]; }
    v9x_tsp_enter(v9x_tsp_lock); info(&request); v9x_tsp_leave(v9x_tsp_lock);
    if (request.ddRVal != 0ul || !callbacks.DrawPrimitives) { v9x_tsp_text("Result", "FAIL-CALLBACKS"); ExitProcess(1u); }
    draw = (V9X_TSP_DRAW)callbacks.DrawPrimitives;
    v9x_tsp_zero(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = V9X_TSP_EDGE; desc.dwHeight = V9X_TSP_EDGE;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY | DDSCAPS_3DDEVICE;
    if (IDirectDraw_CreateSurface(dd, &desc, &target, 0) != DD_OK) { v9x_tsp_text("Result", "FAIL-TARGET"); ExitProcess(1u); }
    v9x_tsp_zero(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
    if (IDirectDrawSurface_GetSurfaceDesc(target, &desc) != DD_OK) { ExitProcess(1u); }
    v9x_tsp_hex("TargetGreenMask", desc.ddpfPixelFormat.dwGBitMask);
    if (desc.ddpfPixelFormat.dwGBitMask != 0x07e0ul) {
        /* The patterns are compared as RGB565, as the textures are. */
        v9x_tsp_text("Result", "FAIL-TARGET-NOT-565"); ExitProcess(1u);
    }
    v9x_tsp_zero(&context, sizeof(context)); context.lpDDS = target; context.dwPID = GetCurrentProcessId();
    v9x_tsp_enter(v9x_tsp_lock); context_create(&context); v9x_tsp_leave(v9x_tsp_lock);
    if (context.ddrval != 0ul || context.dwhContext == 0ul) { v9x_tsp_text("Result", "FAIL-CONTEXT"); ExitProcess(1u); }
    baseline_refused = shared->d3d_diagnostics.batches_engine_refused;
    baseline_fifo = shared->engine.fifo_timeouts; baseline_idle = shared->engine.idle_timeouts;
    baseline_resets = shared->engine.reset_count;

    /* 1. Single levels. */
    for (test = 0ul; test < 9ul; ++test) {
        LPDIRECTDRAWSURFACE texture;
        V9X_D3DHAL_TEXTURECREATEDATA tc;
        V9X_D3DHAL_TEXTUREDESTROYDATA td;
        HRESULT hr;
        DWORD w = single_w[test], h = single_h[test];

        texture = v9x_tsp_texture(dd, w, h, 1ul, &hr);
        wsprintfA(key, "S%lux%luCreateHr", w, h); v9x_tsp_hex(key, (DWORD)hr);
        if (texture == 0) { ok = 0; continue; }
        v9x_tsp_zero(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
        if (IDirectDrawSurface_GetSurfaceDesc(texture, &desc) == DD_OK) {
            wsprintfA(key, "S%lux%luPitch", w, h); v9x_tsp_uint(key, (DWORD)desc.lPitch);
        }
        if (!v9x_tsp_fill(texture, 0ul)) { ok = 0; }
        v9x_tsp_zero(&tc, sizeof(tc)); tc.dwhContext = context.dwhContext; tc.lpDDS = texture;
        v9x_tsp_enter(v9x_tsp_lock); texture_create(&tc); v9x_tsp_leave(v9x_tsp_lock);
        before_refused = shared->d3d_diagnostics.batches_engine_refused;
        before_draws = shared->d3d_diagnostics.m64_draws;
        if (tc.ddrval != 0ul || tc.dwHandle == 0ul || !v9x_tsp_clear(target) ||
            !v9x_tsp_draw(draw, context.dwhContext, tc.dwHandle, 1ul, V9X_TSP_EDGE, V9X_TSP_EDGE) ||
            !v9x_tsp_capture(target)) {
            ok = 0;
        } else {
            v9x_tsp_expected(w, h, V9X_TSP_EDGE, V9X_TSP_EDGE, 0ul);
            mismatch = v9x_tsp_mismatch();
            wsprintfA(key, "S%lux%luMismatch", w, h); v9x_tsp_uint(key, mismatch);
            wsprintfA(key, "S%lux%luRefused", w, h);
            v9x_tsp_uint(key, shared->d3d_diagnostics.batches_engine_refused - before_refused);
            wsprintfA(key, "S%lux%luEngineDraws", w, h);
            v9x_tsp_uint(key, shared->d3d_diagnostics.m64_draws - before_draws);
            wsprintfA(key, "S%lux%luPixel33", w, h); v9x_tsp_hex(key, v9x_tsp_pixels[33ul * V9X_TSP_EDGE + 33ul]);
            wsprintfA(name, "TS%lu", test); v9x_tsp_image(name);
            if (mismatch != 0ul) { ok = 0; }
        }
        if (tc.dwHandle != 0ul) {
            td.dwhContext = context.dwhContext; td.dwHandle = tc.dwHandle; td.ddrval = 0ul;
            v9x_tsp_enter(v9x_tsp_lock); texture_destroy(&td); v9x_tsp_leave(v9x_tsp_lock);
        }
        IDirectDrawSurface_Release(texture);
    }

    /* 2. Chains, drawn at each level's own size, MIPNEAREST (D3D 3). */
    for (test = 0ul; test < 2ul; ++test) {
        LPDIRECTDRAWSURFACE texture;
        V9X_D3DHAL_TEXTURECREATEDATA tc;
        V9X_D3DHAL_TEXTUREDESTROYDATA td;
        HRESULT hr;
        DWORD pitches[V9X_TSP_LEVELS];
        DWORD w = chain_w[test], h = chain_h[test];
        DWORD filled;

        v9x_tsp_zero(pitches, sizeof(pitches));
        texture = v9x_tsp_texture(dd, w, h, 6ul, &hr);
        wsprintfA(key, "C%lux%luCreateHr", w, h); v9x_tsp_hex(key, (DWORD)hr);
        if (texture == 0) { ok = 0; continue; }
        filled = v9x_tsp_fill_chain(texture, pitches);
        wsprintfA(key, "C%lux%luLevelsFilled", w, h); v9x_tsp_uint(key, filled);
        for (level = 0ul; level < filled; ++level) {
            wsprintfA(key, "C%lux%luL%luPitchWH", w, h, level); v9x_tsp_hex(key, pitches[level]);
        }
        v9x_tsp_zero(&tc, sizeof(tc)); tc.dwhContext = context.dwhContext; tc.lpDDS = texture;
        v9x_tsp_enter(v9x_tsp_lock); texture_create(&tc); v9x_tsp_leave(v9x_tsp_lock);
        if (tc.ddrval != 0ul || tc.dwHandle == 0ul) { ok = 0; IDirectDrawSurface_Release(texture); continue; }
        before_draws = shared->d3d_diagnostics.mip_chain_levels;
        /* Levels 1 to 4: 64x16 (or 16x64) down to 8x2 (or 2x8); every
         * level drawn must fit the 64x64 target. */
        for (level = 1ul; level <= 4ul && level < filled; ++level) {
            DWORD dw = w >> level, dh = h >> level;
            DWORD best = 0xfffffffful, best_level = 0xfffffffful, candidate;
            if (dw > V9X_TSP_EDGE || dh > V9X_TSP_EDGE) { continue; }
            before_refused = shared->d3d_diagnostics.batches_engine_refused;
            if (!v9x_tsp_clear(target) ||
                !v9x_tsp_draw(draw, context.dwhContext, tc.dwHandle, 3ul, dw, dh) ||
                !v9x_tsp_capture(target)) { ok = 0; break; }
            for (candidate = 0ul; candidate < filled; ++candidate) {
                DWORD cw = w >> candidate, ch = h >> candidate;
                if (cw == 0ul) { cw = 1ul; }
                if (ch == 0ul) { ch = 1ul; }
                v9x_tsp_expected(cw, ch, dw, dh, candidate + 1ul);
                mismatch = v9x_tsp_mismatch();
                if (mismatch < best) { best = mismatch; best_level = candidate; }
            }
            wsprintfA(key, "C%lux%luAt%lux%luBestLevel", w, h, dw, dh); v9x_tsp_uint(key, best_level);
            wsprintfA(key, "C%lux%luAt%lux%luMismatch", w, h, dw, dh); v9x_tsp_uint(key, best);
            wsprintfA(key, "C%lux%luAt%lux%luRefused", w, h, dw, dh);
            v9x_tsp_uint(key, shared->d3d_diagnostics.batches_engine_refused - before_refused);
            wsprintfA(name, "TC%lu%lu", test, level); v9x_tsp_image(name);
            if (best != 0ul) { ok = 0; }
        }
        wsprintfA(key, "C%lux%luChainLevelsSeen", w, h);
        v9x_tsp_uint(key, shared->d3d_diagnostics.mip_chain_levels);
        td.dwhContext = context.dwhContext; td.dwHandle = tc.dwHandle; td.ddrval = 0ul;
        v9x_tsp_enter(v9x_tsp_lock); texture_destroy(&td); v9x_tsp_leave(v9x_tsp_lock);
        IDirectDrawSurface_Release(texture);
    }

    /* 3. Large textures, on Gen3 only. */
    if (shared->engine.engine_type == V9X_DD_ENGINE_TYPE_INTEL_GEN3) {
        static const DWORD large_w[6] = { 256ul, 512ul, 512ul, 256ul, 1024ul, 1024ul };
        static const DWORD large_h[6] = { 256ul, 512ul, 256ul, 512ul, 1024ul, 256ul };
        static const DWORD big_chain[2] = { 512ul, 1024ul };

        for (test = 0ul; test < 6ul; ++test) {
            LPDIRECTDRAWSURFACE texture;
            V9X_D3DHAL_TEXTURECREATEDATA tc;
            V9X_D3DHAL_TEXTUREDESTROYDATA td;
            HRESULT hr;
            DWORD w = large_w[test], h = large_h[test];

            texture = v9x_tsp_texture(dd, w, h, 1ul, &hr);
            wsprintfA(key, "L%lux%luCreateHr", w, h); v9x_tsp_hex(key, (DWORD)hr);
            if (texture == 0) { ok = 0; continue; }
            if (!v9x_tsp_fill(texture, 0ul)) { ok = 0; }
            v9x_tsp_zero(&tc, sizeof(tc)); tc.dwhContext = context.dwhContext; tc.lpDDS = texture;
            v9x_tsp_enter(v9x_tsp_lock); texture_create(&tc); v9x_tsp_leave(v9x_tsp_lock);
            before_refused = shared->d3d_diagnostics.batches_engine_refused;
            if (tc.ddrval != 0ul || tc.dwHandle == 0ul || !v9x_tsp_clear(target) ||
                !v9x_tsp_draw(draw, context.dwhContext, tc.dwHandle, 1ul, V9X_TSP_EDGE, V9X_TSP_EDGE) ||
                !v9x_tsp_capture(target)) {
                ok = 0;
            } else {
                v9x_tsp_expected(w, h, V9X_TSP_EDGE, V9X_TSP_EDGE, 0ul);
                mismatch = v9x_tsp_mismatch();
                wsprintfA(key, "L%lux%luMismatch", w, h); v9x_tsp_uint(key, mismatch);
                wsprintfA(key, "L%lux%luRefused", w, h);
                v9x_tsp_uint(key, shared->d3d_diagnostics.batches_engine_refused - before_refused);
                wsprintfA(name, "TL%lu", test); v9x_tsp_image(name);
                if (mismatch != 0ul) { ok = 0; }
            }
            if (tc.dwHandle != 0ul) {
                td.dwhContext = context.dwhContext; td.dwHandle = tc.dwHandle; td.ddrval = 0ul;
                v9x_tsp_enter(v9x_tsp_lock); texture_destroy(&td); v9x_tsp_leave(v9x_tsp_lock);
            }
            IDirectDrawSurface_Release(texture);
        }

        /* Full chains, drawn MIPNEAREST at 64 and 32: each draw against
         * every level, the best match named. */
        for (test = 0ul; test < 2ul; ++test) {
            LPDIRECTDRAWSURFACE texture;
            V9X_D3DHAL_TEXTURECREATEDATA tc;
            V9X_D3DHAL_TEXTUREDESTROYDATA td;
            HRESULT hr;
            DWORD pitches[V9X_TSP_LEVELS];
            DWORD w = big_chain[test], h = big_chain[test];
            DWORD levels = 1ul, filled, size;

            while ((w >> (levels - 1ul)) > 1ul) { ++levels; }
            v9x_tsp_zero(pitches, sizeof(pitches));
            texture = v9x_tsp_texture(dd, w, h, levels, &hr);
            wsprintfA(key, "B%luCreateHr", w); v9x_tsp_hex(key, (DWORD)hr);
            if (texture == 0) { ok = 0; continue; }
            filled = v9x_tsp_fill_chain(texture, pitches);
            wsprintfA(key, "B%luLevelsFilled", w); v9x_tsp_uint(key, filled);
            v9x_tsp_zero(&tc, sizeof(tc)); tc.dwhContext = context.dwhContext; tc.lpDDS = texture;
            v9x_tsp_enter(v9x_tsp_lock); texture_create(&tc); v9x_tsp_leave(v9x_tsp_lock);
            if (tc.ddrval != 0ul || tc.dwHandle == 0ul) { ok = 0; IDirectDrawSurface_Release(texture); continue; }
            for (size = V9X_TSP_EDGE; size >= 32ul; size /= 2ul) {
                DWORD best = 0xfffffffful, best_level = 0xfffffffful, candidate;
                before_refused = shared->d3d_diagnostics.batches_engine_refused;
                if (!v9x_tsp_clear(target) ||
                    !v9x_tsp_draw(draw, context.dwhContext, tc.dwHandle, 3ul, size, size) ||
                    !v9x_tsp_capture(target)) { ok = 0; break; }
                for (candidate = 0ul; candidate < filled; ++candidate) {
                    DWORD cw = w >> candidate;
                    if (cw == 0ul) { cw = 1ul; }
                    v9x_tsp_expected(cw, cw, size, size, candidate + 1ul);
                    mismatch = v9x_tsp_mismatch();
                    if (mismatch < best) { best = mismatch; best_level = candidate; }
                }
                wsprintfA(key, "B%luAt%luBestLevel", w, size); v9x_tsp_uint(key, best_level);
                wsprintfA(key, "B%luAt%luMismatch", w, size); v9x_tsp_uint(key, best);
                wsprintfA(key, "B%luAt%luRefused", w, size);
                v9x_tsp_uint(key, shared->d3d_diagnostics.batches_engine_refused - before_refused);
                wsprintfA(name, "TB%lu%lu", test, size); v9x_tsp_image(name);
                if (best != 0ul) { ok = 0; }
            }
            td.dwhContext = context.dwhContext; td.dwHandle = tc.dwHandle; td.ddrval = 0ul;
            v9x_tsp_enter(v9x_tsp_lock); texture_destroy(&td); v9x_tsp_leave(v9x_tsp_lock);
            IDirectDrawSurface_Release(texture);
        }
    }

    v9x_tsp_uint("NewRefusals", shared->d3d_diagnostics.batches_engine_refused - baseline_refused);
    v9x_tsp_uint("NewFifoTimeouts", shared->engine.fifo_timeouts - baseline_fifo);
    v9x_tsp_uint("NewIdleTimeouts", shared->engine.idle_timeouts - baseline_idle);
    v9x_tsp_uint("NewResets", shared->engine.reset_count - baseline_resets);
    v9x_tsp_uint("M64PolicyLast", shared->d3d_diagnostics.m64_policy_last);
    if (shared->d3d_diagnostics.batches_engine_refused != baseline_refused ||
        shared->engine.fifo_timeouts != baseline_fifo ||
        shared->engine.idle_timeouts != baseline_idle ||
        shared->engine.reset_count != baseline_resets) { ok = 0; }
    destroy.dwhContext = context.dwhContext; destroy.ddrval = 0xfffffffful;
    v9x_tsp_enter(v9x_tsp_lock); context_destroy(&destroy); v9x_tsp_leave(v9x_tsp_lock);
    IDirectDrawSurface_Release(target); IDirectDraw_Release(dd);
    v9x_tsp_text("Result", ok ? "PASS" : "FAIL");
    WritePrivateProfileStringA(0, 0, 0, V9X_TSP_PATH);
    ExitProcess(ok ? 0u : 1u);
}
