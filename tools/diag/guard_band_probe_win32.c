/* Gen3 guard-band probe (docs\plans\intel-gen3-guard-band.md, step 1).
 *
 * Measurement only. For each distance d it draws triangles reaching d
 * pixels past a drawing rectangle inset 32 pixels inside a 128x128 target,
 * once clipped by the core (the reference, the variant mask zero) and once
 * handed to the GMA unclipped (the GUARD and INSET variant), and reads back:
 *   - the 32-pixel frame between the rectangle and the surface's edge and
 *     the target's pitch padding: any change is a write the rectangle
 *     should have stopped;
 *   - two surfaces created either side of the target: any change is a
 *     write outside the target altogether;
 *   - the rectangle's interior against the reference;
 *   - the HAL's refusals, timeouts and resets.
 * It stops at the first d with any write outside the rectangle, refusal,
 * timeout or reset, and leaves the variant mask as it found it.
 *
 * Calls DrawPrimitives directly, as V9XRCLP does, so the runtime does not
 * clip first. Usage: V9XGBND [-max:<d>]. Writes C:\V9XDIAG\V9XGBND.INI and
 * a PPM of every unclipped draw.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include "velocity9x/build.h"
#include "velocity9x/win9x_ddraw_abi.h"

#define V9X_GB_PATH "C:\\V9XDIAG\\V9XGBND.INI"
#define V9X_GB_EDGE 128ul
#define V9X_GB_INSET 32ul
#define V9X_GB_PIXELS (V9X_GB_EDGE * V9X_GB_EDGE)
#define V9X_GB_STREAM_BYTES 4096ul
#define V9X_GB_SHAPES 6ul
#define V9X_GB_DISTANCES 5ul
/* The bytes of one surface read whole, padding included: a pitch of up to
 * four times the row. */
#define V9X_GB_SURFACE_BYTES (V9X_GB_EDGE * V9X_GB_EDGE * 8ul)
/* The fill colours: the target's guard, and one pattern per neighbour. */
#define V9X_GB_GUARD 0x528aul
#define V9X_GB_BEFORE 0x5555ul
#define V9X_GB_AFTER 0xaaaaul
/* The variant mask's bits, as d3d_i9xx.c reads them. */
#define V9X_GB_OMIT_GUARD 0x100ul
#define V9X_GB_OMIT_INSET 0x200ul
#define V9X_GB_LIST 4u
#define V9X_GB_TL 3u

typedef HRESULT (WINAPI *V9X_GB_CREATE)(GUID *, LPDIRECTDRAW *, IUnknown *);
typedef void (WINAPI *V9X_GB_GETLOCK)(void **);
typedef void (WINAPI *V9X_GB_LEVEL)(void *);
typedef DWORD (WINAPI *V9X_GB_CONTEXT)(V9X_D3DHAL_CONTEXTCREATEDATA *);
typedef DWORD (WINAPI *V9X_GB_DESTROY)(V9X_D3DHAL_CONTEXTDESTROYDATA *);
typedef DWORD (WINAPI *V9X_GB_INFO)(V9X_DDHAL_GETDRIVERINFODATA *);
typedef DWORD (WINAPI *V9X_GB_DRAW)(V9X_D3DHAL_DRAWPRIMITIVESDATA *);

static const DWORD v9x_gb_distances[V9X_GB_DISTANCES] = {
    64ul, 256ul, 1024ul, 2048ul, 3968ul
};

static V9X_GB_LEVEL v9x_gb_enter, v9x_gb_leave;
static void *v9x_gb_lock;
static V9X_DD_SHARED *v9x_gb_shared;
static DWORD v9x_gb_mask_found;
static WORD v9x_gb_reference[V9X_GB_PIXELS];
static WORD v9x_gb_pixels[V9X_GB_PIXELS];
static BYTE v9x_gb_target_before[V9X_GB_SURFACE_BYTES];
static BYTE v9x_gb_target_after[V9X_GB_SURFACE_BYTES];
static BYTE v9x_gb_side_before[2][V9X_GB_SURFACE_BYTES];
static BYTE v9x_gb_side_after[2][V9X_GB_SURFACE_BYTES];
static BYTE v9x_gb_stream[V9X_GB_STREAM_BYTES];
static V9X_D3DTLVERTEX v9x_gb_vertices[3];

static void v9x_gb_zero(void *p, DWORD n)
{
    DWORD i;
    for (i = 0ul; i < n; ++i) { ((BYTE *)p)[i] = 0u; }
}
static void v9x_gb_text(const char *key, const char *value)
{
    WritePrivateProfileStringA("GuardBandProbe", key, value, V9X_GB_PATH);
}
static void v9x_gb_uint(const char *key, DWORD value)
{
    char text[24];
    wsprintfA(text, "%lu", value);
    v9x_gb_text(key, text);
}
static void v9x_gb_hex(const char *key, DWORD value)
{
    char text[24];
    wsprintfA(text, "0x%08lX", value);
    v9x_gb_text(key, text);
}
/* The same KERNEL32 ordinal walk V9XRCLP uses for the Win16 mutex. */
static FARPROC v9x_gb_ordinal(DWORD ordinal)
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
/* The variant mask, through the escape V9XTRACE -omit uses. */
static int v9x_gb_mask(DWORD mask)
{
    V9X_DCICMD command;
    HDC dc = GetDC(0);
    int result;
    v9x_gb_zero(&command, sizeof(command));
    command.dwCommand = V9X_DDFLUSHVARIANT;
    command.dwParam1 = mask;
    command.dwVersion = V9X_DD_VERSION;
    result = dc ? ExtEscape(dc, V9X_DCICOMMAND, sizeof(command),
                            (LPCSTR)&command, 0, 0) : 0;
    if (dc) { ReleaseDC(0, dc); }
    return result > 0 && v9x_gb_shared->hsw_flush_omit == mask;
}
/* Every exit puts the mask back as it was found. */
static void v9x_gb_exit(const char *result, UINT code)
{
    if (v9x_gb_shared != 0) { (void)v9x_gb_mask(v9x_gb_mask_found); }
    v9x_gb_text("Result", result);
    WritePrivateProfileStringA(0, 0, 0, V9X_GB_PATH);
    ExitProcess(code);
}
static void v9x_gb_vertex(V9X_D3DTLVERTEX *v, float x, float y, DWORD color)
{
    v->sx = x; v->sy = y; v->sz = 0.5f; v->rhw = 1.0f;
    v->color = color; v->specular = 0xff000000ul;
    v->tu = 0.0f; v->tv = 0.0f;
}
/* Each shape reaches d past one side of the rectangle [32, 96), or past
 * three; the last is Gouraud, so the interior compares interpolation too. */
static void v9x_gb_shape(DWORD shape, float d)
{
    const float lo = (float)V9X_GB_INSET;
    const float hi = (float)(V9X_GB_EDGE - V9X_GB_INSET);
    V9X_D3DTLVERTEX *v = v9x_gb_vertices;
    switch (shape) {
    case 0ul:
        v9x_gb_vertex(&v[0], lo - d, 40.0f, 0xffff0000ul);
        v9x_gb_vertex(&v[1], lo - d, 88.0f, 0xffff0000ul);
        v9x_gb_vertex(&v[2], 80.0f, 64.0f, 0xffff0000ul);
        break;
    case 1ul:
        v9x_gb_vertex(&v[0], hi + d, 40.0f, 0xff00ff00ul);
        v9x_gb_vertex(&v[1], hi + d, 88.0f, 0xff00ff00ul);
        v9x_gb_vertex(&v[2], 48.0f, 64.0f, 0xff00ff00ul);
        break;
    case 2ul:
        v9x_gb_vertex(&v[0], 40.0f, lo - d, 0xff0000fful);
        v9x_gb_vertex(&v[1], 88.0f, lo - d, 0xff0000fful);
        v9x_gb_vertex(&v[2], 64.0f, 80.0f, 0xff0000fful);
        break;
    case 3ul:
        v9x_gb_vertex(&v[0], 40.0f, hi + d, 0xffffff00ul);
        v9x_gb_vertex(&v[1], 88.0f, hi + d, 0xffffff00ul);
        v9x_gb_vertex(&v[2], 64.0f, 48.0f, 0xffffff00ul);
        break;
    case 4ul:
        v9x_gb_vertex(&v[0], lo - d, lo - d, 0xff00fffful);
        v9x_gb_vertex(&v[1], hi + d, lo - d, 0xff00fffful);
        v9x_gb_vertex(&v[2], lo - d, hi + d, 0xff00fffful);
        break;
    default:
        v9x_gb_vertex(&v[0], lo - d, hi + d, 0xffff0000ul);
        v9x_gb_vertex(&v[1], 64.0f, lo - d, 0xff00ff00ul);
        v9x_gb_vertex(&v[2], hi + d, hi + d, 0xff0000fful);
        break;
    }
}
/* One record, one triangle, with the states that keep it opaque and
 * uncullable, then the terminator. */
static int v9x_gb_draw(V9X_GB_DRAW callback, DWORD context)
{
    V9X_D3DHAL_DRAWPRIMITIVESDATA data;
    BYTE *cursor = (BYTE *)(((DWORD)v9x_gb_stream + 31ul) & ~31ul);
    V9X_D3DHAL_DRAWPRIMCOUNTS *counts = (V9X_D3DHAL_DRAWPRIMCOUNTS *)cursor;
    DWORD *pairs;
    DWORD i;
    int ok;
    v9x_gb_zero(&data, sizeof(data));
    data.dwhContext = context;
    data.lpvData = cursor;
    counts->wNumStateChanges = 4u;
    counts->wPrimitiveType = V9X_GB_LIST;
    counts->wVertexType = V9X_GB_TL;
    counts->wNumVertices = 3u;
    cursor += sizeof(*counts);
    pairs = (DWORD *)cursor;
    pairs[0] = 22ul; pairs[1] = 1ul;  /* CULLMODE NONE */
    pairs[2] = 7ul; pairs[3] = 0ul;   /* ZENABLE off */
    pairs[4] = 14ul; pairs[5] = 0ul;  /* ZWRITEENABLE off */
    pairs[6] = 27ul; pairs[7] = 0ul;  /* ALPHABLENDENABLE off */
    cursor += 32ul;
    cursor = (BYTE *)(((DWORD)cursor + 31ul) & ~31ul);
    for (i = 0ul; i < 3ul; ++i) {
        ((V9X_D3DTLVERTEX *)cursor)[i] = v9x_gb_vertices[i];
    }
    cursor += 3ul * sizeof(V9X_D3DTLVERTEX);
    v9x_gb_zero(cursor, sizeof(V9X_D3DHAL_DRAWPRIMCOUNTS));
    v9x_gb_enter(v9x_gb_lock);
    data.ddrval = 0xfffffffful;
    ok = callback(&data) == V9X_DDHAL_DRIVER_HANDLED && data.ddrval == 0ul;
    v9x_gb_leave(v9x_gb_lock);
    return ok;
}
static int v9x_gb_fill(LPDIRECTDRAWSURFACE surface, DWORD color)
{
    DDBLTFX fx;
    v9x_gb_zero(&fx, sizeof(fx)); fx.dwSize = sizeof(fx);
    fx.dwFillColor = color;
    return IDirectDrawSurface_Blt(surface, 0, 0, 0,
                                  DDBLT_COLORFILL | DDBLT_WAIT, &fx) == DD_OK;
}
/* The surface's bytes, every row at its full pitch; its pitch and linear
 * address returned for the record. Lock waits for the engine. */
static int v9x_gb_read(LPDIRECTDRAWSURFACE surface, BYTE *bytes,
                       DWORD *pitch, DWORD *address)
{
    DDSURFACEDESC desc;
    DWORD i, n;
    v9x_gb_zero(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
    if (IDirectDrawSurface_Lock(surface, 0, &desc, DDLOCK_WAIT, 0) != DD_OK) {
        return 0;
    }
    *pitch = (DWORD)desc.lPitch;
    *address = (DWORD)desc.lpSurface;
    n = *pitch * V9X_GB_EDGE;
    if (n > V9X_GB_SURFACE_BYTES) { n = V9X_GB_SURFACE_BYTES; }
    for (i = 0ul; i < n; ++i) { bytes[i] = ((const BYTE *)desc.lpSurface)[i]; }
    return IDirectDrawSurface_Unlock(surface, 0) == DD_OK;
}
static void v9x_gb_pixels_from(const BYTE *bytes, DWORD pitch, WORD *pixels)
{
    DWORD x, y;
    for (y = 0ul; y < V9X_GB_EDGE; ++y) {
        const WORD *row = (const WORD *)(bytes + y * pitch);
        for (x = 0ul; x < V9X_GB_EDGE; ++x) {
            pixels[y * V9X_GB_EDGE + x] = row[x];
        }
    }
}
static DWORD v9x_gb_bytes_changed(const BYTE *a, const BYTE *b, DWORD n)
{
    DWORD i, count = 0ul;
    for (i = 0ul; i < n; ++i) { if (a[i] != b[i]) { ++count; } }
    return count;
}
/* Bytes past the row's last pixel, inside the pitch, that changed. */
static DWORD v9x_gb_padding_changed(DWORD pitch)
{
    DWORD x, y, count = 0ul;
    for (y = 0ul; y < V9X_GB_EDGE; ++y) {
        for (x = V9X_GB_EDGE * 2ul; x < pitch; ++x) {
            if (v9x_gb_target_before[y * pitch + x] !=
                v9x_gb_target_after[y * pitch + x]) { ++count; }
        }
    }
    return count;
}
static int v9x_gb_in_rect(DWORD x, DWORD y)
{
    return x >= V9X_GB_INSET && x < V9X_GB_EDGE - V9X_GB_INSET &&
           y >= V9X_GB_INSET && y < V9X_GB_EDGE - V9X_GB_INSET;
}
static DWORD v9x_gb_channel_diff(DWORD a, DWORD b, DWORD green_mask)
{
    DWORD shifts[3], widths[3], i, worst = 0ul;
    shifts[0] = green_mask == 0x7e0ul ? 11ul : 10ul; widths[0] = 31ul;
    shifts[1] = 5ul; widths[1] = green_mask == 0x7e0ul ? 63ul : 31ul;
    shifts[2] = 0ul; widths[2] = 31ul;
    for (i = 0ul; i < 3ul; ++i) {
        DWORD ca = (a >> shifts[i]) & widths[i];
        DWORD cb = (b >> shifts[i]) & widths[i];
        DWORD d = ca > cb ? ca - cb : cb - ca;
        if (d > worst) { worst = d; }
    }
    return worst;
}
static int v9x_gb_image(const char *name, const WORD *pixels, DWORD green_mask)
{
    char path[80];
    const char header[] = "P6\n128 128\n255\n";
    static BYTE rgb[V9X_GB_PIXELS * 3ul];
    HANDLE file;
    DWORD i, wrote;
    int ok;
    for (i = 0ul; i < V9X_GB_PIXELS; ++i) {
        DWORD value = pixels[i];
        DWORD gmax = green_mask == 0x7e0ul ? 63ul : 31ul;
        rgb[i*3ul] = (BYTE)(((green_mask == 0x7e0ul ? value >> 11 : value >> 10) & 31ul) * 255ul / 31ul);
        rgb[i*3ul+1ul] = (BYTE)(((value >> 5) & gmax) * 255ul / gmax);
        rgb[i*3ul+2ul] = (BYTE)((value & 31ul) * 255ul / 31ul);
    }
    wsprintfA(path, "C:\\V9XDIAG\\%s.PPM", name);
    file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) { return 0; }
    ok = WriteFile(file, header, sizeof(header) - 1u, &wrote, 0) && wrote == sizeof(header) - 1u;
    if (!WriteFile(file, rgb, sizeof(rgb), &wrote, 0) || wrote != sizeof(rgb)) { ok = 0; }
    CloseHandle(file);
    return ok;
}
static DWORD v9x_gb_max_argument(void)
{
    const char *arg = GetCommandLineA();
    DWORD value = 0ul;
    for (; *arg != '\0'; ++arg) {
        if (arg[0] == '-' && arg[1] == 'm' && arg[2] == 'a' && arg[3] == 'x' &&
            arg[4] == ':') {
            for (arg += 5; *arg >= '0' && *arg <= '9'; ++arg) {
                value = value * 10ul + (DWORD)(*arg - '0');
            }
            return value;
        }
    }
    return 0xfffffffful;
}

void __stdcall V9xGuardBandProbeEntry(void)
{
    static const BYTE guid[16] = {0xe1u,0x84u,0xa5u,0x0bu,0xb6u,0x70u,0xd0u,0x11u,0x88u,0x9du,0u,0xaau,0u,0xbbu,0xb7u,0x6au};
    LPDIRECTDRAW dd = 0;
    LPDIRECTDRAWSURFACE side[2] = { 0, 0 };
    LPDIRECTDRAWSURFACE target = 0;
    V9X_GB_CREATE create;
    V9X_GB_GETLOCK getlock;
    V9X_GB_CONTEXT context_create;
    V9X_GB_DESTROY context_destroy;
    V9X_GB_INFO info;
    V9X_GB_DRAW draw;
    V9X_DD32BITDRIVERDATA driver;
    V9X_DCICMD command;
    V9X_DDHAL_GETDRIVERINFODATA request;
    V9X_D3DHAL_CALLBACKS2 callbacks;
    V9X_D3DHAL_CONTEXTCREATEDATA context;
    V9X_D3DHAL_CONTEXTDESTROYDATA destroy;
    DDSURFACEDESC desc;
    HDC dc;
    DWORD i, k, s, x, y, pitch, side_pitch[2], address, side_address[2];
    DWORD green_mask, max_d, passed_d = 0ul;
    DWORD frame, padding, sides, interior, worst, clipped;
    DWORD refused0, crumb0, reset0, fifo0, idle0, clipped0;
    char key[48], name[16];
    int result, ok = 1, safe;

    CreateDirectoryA("C:\\V9XDIAG", 0);
    DeleteFileA(V9X_GB_PATH);
    v9x_gb_text("Build", V9X_BUILD_ID);
    v9x_gb_text("Result", "RUNNING");
    max_d = v9x_gb_max_argument();
    v9x_gb_hex("MaxDistance", max_d);
    create = (V9X_GB_CREATE)GetProcAddress(LoadLibraryA("DDRAW.DLL"), "DirectDrawCreate");
    if (create == 0 || create(0, &dd, 0) != DD_OK) { v9x_gb_exit("FAIL-DDRAW", 1u); }
    if (IDirectDraw_SetCooperativeLevel(dd, 0, DDSCL_NORMAL) != DD_OK) { v9x_gb_exit("FAIL-COOP", 1u); }
    v9x_gb_zero(&driver, sizeof(driver)); v9x_gb_zero(&command, sizeof(command));
    command.dwCommand = V9X_DDGET32BITDRIVERNAME; command.dwVersion = V9X_DD_VERSION;
    dc = GetDC(0);
    result = dc ? ExtEscape(dc, V9X_DCICOMMAND, sizeof(command), (LPCSTR)&command, sizeof(driver), (LPSTR)&driver) : 0;
    if (dc) { ReleaseDC(0, dc); }
    {
        V9X_DD_SHARED *shared = (V9X_DD_SHARED *)driver.dwContext;
        if (result <= 0 || shared == 0 || IsBadReadPtr(shared, sizeof(*shared)) ||
            shared->dwSize != sizeof(*shared) || shared->abi != V9X_DD_SHARED_ABI ||
            shared->engine.engine_type != V9X_DD_ENGINE_TYPE_INTEL_GEN3) {
            v9x_gb_exit("FAIL-SHARED-OR-ENGINE", 1u);
        }
        v9x_gb_shared = shared;
    }
    v9x_gb_mask_found = v9x_gb_shared->hsw_flush_omit;
    v9x_gb_hex("MaskFound", v9x_gb_mask_found);
    getlock = (V9X_GB_GETLOCK)v9x_gb_ordinal(93ul);
    v9x_gb_enter = (V9X_GB_LEVEL)v9x_gb_ordinal(97ul);
    v9x_gb_leave = (V9X_GB_LEVEL)v9x_gb_ordinal(98ul);
    if (!getlock || !v9x_gb_enter || !v9x_gb_leave) { v9x_gb_exit("FAIL-MUTEX", 1u); }
    getlock(&v9x_gb_lock);
    if (!v9x_gb_lock) { v9x_gb_exit("FAIL-MUTEX", 1u); }
    info = (V9X_GB_INFO)v9x_gb_shared->info.GetDriverInfo;
    context_create = (V9X_GB_CONTEXT)v9x_gb_shared->d3d_callbacks.ContextCreate;
    context_destroy = (V9X_GB_DESTROY)v9x_gb_shared->d3d_callbacks.ContextDestroy;
    if (!info || !context_create || !context_destroy) { v9x_gb_exit("FAIL-CALLBACKS", 1u); }
    v9x_gb_zero(&request, sizeof(request)); v9x_gb_zero(&callbacks, sizeof(callbacks));
    request.dwSize = sizeof(request); request.dwExpectedSize = sizeof(callbacks); request.lpvData = &callbacks;
    for (i = 0ul; i < sizeof(guid); ++i) { request.guidInfo[i] = guid[i]; }
    v9x_gb_enter(v9x_gb_lock); info(&request); v9x_gb_leave(v9x_gb_lock);
    if (request.ddRVal != 0ul || !callbacks.DrawPrimitives) { v9x_gb_exit("FAIL-CALLBACKS", 1u); }
    draw = (V9X_GB_DRAW)callbacks.DrawPrimitives;

    /* Before, target, after: in that order, so the heap is likeliest to
     * place them adjacent. Their addresses are recorded either way. */
    for (i = 0ul; i < 3ul; ++i) {
        LPDIRECTDRAWSURFACE *slot = i == 0ul ? &side[0] : (i == 1ul ? &target : &side[1]);
        v9x_gb_zero(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
        desc.dwWidth = V9X_GB_EDGE; desc.dwHeight = V9X_GB_EDGE;
        desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY | DDSCAPS_3DDEVICE;
        if (IDirectDraw_CreateSurface(dd, &desc, slot, 0) != DD_OK) { v9x_gb_exit("FAIL-SURFACE", 1u); }
    }
    v9x_gb_zero(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
    if (IDirectDrawSurface_GetSurfaceDesc(target, &desc) != DD_OK ||
        desc.ddpfPixelFormat.dwRGBBitCount != 16ul) { v9x_gb_exit("FAIL-FORMAT", 1u); }
    green_mask = desc.ddpfPixelFormat.dwGBitMask;
    v9x_gb_zero(&context, sizeof(context)); context.lpDDS = target; context.dwPID = GetCurrentProcessId();
    v9x_gb_enter(v9x_gb_lock); context_create(&context); v9x_gb_leave(v9x_gb_lock);
    if (context.ddrval != 0ul || context.dwhContext == 0ul) { v9x_gb_exit("FAIL-CONTEXT", 1u); }

    for (k = 0ul; k < V9X_GB_DISTANCES && ok; ++k) {
        DWORD d = v9x_gb_distances[k];
        if (d > max_d) { break; }
        for (s = 0ul; s < V9X_GB_SHAPES; ++s) {
            /* The reference: the core clips, as shipped. */
            v9x_gb_shape(s, (float)d);
            if (!v9x_gb_mask(0ul) || !v9x_gb_fill(target, V9X_GB_GUARD) ||
                !v9x_gb_draw(draw, context.dwhContext) ||
                !v9x_gb_read(target, v9x_gb_target_after, &pitch, &address)) {
                v9x_gb_exit("FAIL-REFERENCE", 1u);
            }
            v9x_gb_pixels_from(v9x_gb_target_after, pitch, v9x_gb_reference);

            /* The same triangle unclipped, the rectangle inset. */
            if (!v9x_gb_fill(target, V9X_GB_GUARD) ||
                !v9x_gb_fill(side[0], V9X_GB_BEFORE) ||
                !v9x_gb_fill(side[1], V9X_GB_AFTER) ||
                !v9x_gb_read(target, v9x_gb_target_before, &pitch, &address) ||
                !v9x_gb_read(side[0], v9x_gb_side_before[0], &side_pitch[0], &side_address[0]) ||
                !v9x_gb_read(side[1], v9x_gb_side_before[1], &side_pitch[1], &side_address[1])) {
                v9x_gb_exit("FAIL-FILL", 1u);
            }
            refused0 = v9x_gb_shared->d3d_diagnostics.batches_engine_refused;
            crumb0 = v9x_gb_shared->d3d_diagnostics.breadcrumb_timeouts;
            clipped0 = v9x_gb_shared->d3d_diagnostics.r3d_list_clipped;
            reset0 = v9x_gb_shared->engine.reset_count;
            fifo0 = v9x_gb_shared->engine.fifo_timeouts;
            idle0 = v9x_gb_shared->engine.idle_timeouts;
            if (!v9x_gb_mask(V9X_GB_OMIT_GUARD | V9X_GB_OMIT_INSET | (d << 16))) {
                v9x_gb_exit("FAIL-MASK", 1u);
            }
            result = v9x_gb_draw(draw, context.dwhContext);
            (void)v9x_gb_mask(0ul);
            if (!v9x_gb_read(target, v9x_gb_target_after, &pitch, &address) ||
                !v9x_gb_read(side[0], v9x_gb_side_after[0], &side_pitch[0], &side_address[0]) ||
                !v9x_gb_read(side[1], v9x_gb_side_after[1], &side_pitch[1], &side_address[1])) {
                v9x_gb_exit("FAIL-READ", 1u);
            }
            v9x_gb_pixels_from(v9x_gb_target_after, pitch, v9x_gb_pixels);

            frame = 0ul; interior = 0ul; worst = 0ul;
            for (y = 0ul; y < V9X_GB_EDGE; ++y) {
                for (x = 0ul; x < V9X_GB_EDGE; ++x) {
                    DWORD at = y * V9X_GB_EDGE + x;
                    if (!v9x_gb_in_rect(x, y)) {
                        if (v9x_gb_pixels[at] != (WORD)V9X_GB_GUARD) { ++frame; }
                    } else if (v9x_gb_pixels[at] != v9x_gb_reference[at]) {
                        DWORD diff = v9x_gb_channel_diff(v9x_gb_pixels[at], v9x_gb_reference[at], green_mask);
                        ++interior;
                        if (diff > worst) { worst = diff; }
                    }
                }
            }
            padding = v9x_gb_padding_changed(pitch);
            sides = v9x_gb_bytes_changed(v9x_gb_side_before[0], v9x_gb_side_after[0], side_pitch[0] * V9X_GB_EDGE) +
                    v9x_gb_bytes_changed(v9x_gb_side_before[1], v9x_gb_side_after[1], side_pitch[1] * V9X_GB_EDGE);
            clipped = v9x_gb_shared->d3d_diagnostics.r3d_list_clipped - clipped0;
            safe = result && frame == 0ul && padding == 0ul && sides == 0ul &&
                   v9x_gb_shared->d3d_diagnostics.batches_engine_refused == refused0 &&
                   v9x_gb_shared->d3d_diagnostics.breadcrumb_timeouts == crumb0 &&
                   v9x_gb_shared->engine.reset_count == reset0 &&
                   v9x_gb_shared->engine.fifo_timeouts == fifo0 &&
                   v9x_gb_shared->engine.idle_timeouts == idle0;

            wsprintfA(key, "D%luS%lu", d, s);
            {
                char line[160];
                wsprintfA(line, "drawn=%d frame=%lu padding=%lu sides=%lu interior=%lu worst=%lu clipped=%lu refused=%lu crumb=%lu resets=%lu fifo=%lu idle=%lu",
                          result, frame, padding, sides, interior, worst, clipped,
                          v9x_gb_shared->d3d_diagnostics.batches_engine_refused - refused0,
                          v9x_gb_shared->d3d_diagnostics.breadcrumb_timeouts - crumb0,
                          v9x_gb_shared->engine.reset_count - reset0,
                          v9x_gb_shared->engine.fifo_timeouts - fifo0,
                          v9x_gb_shared->engine.idle_timeouts - idle0);
                v9x_gb_text(key, line);
            }
            wsprintfA(name, "GB%luS%lu", d, s);
            (void)v9x_gb_image(name, v9x_gb_pixels, green_mask);
            if (!safe) { ok = 0; }
        }
        if (ok) { passed_d = d; }
    }
    v9x_gb_hex("TargetAddress", address);
    v9x_gb_hex("TargetPitch", pitch);
    v9x_gb_hex("BeforeAddress", side_address[0]);
    v9x_gb_hex("AfterAddress", side_address[1]);
    v9x_gb_uint("LargestSafeDistance", passed_d);
    destroy.dwhContext = context.dwhContext; destroy.ddrval = 0xfffffffful;
    v9x_gb_enter(v9x_gb_lock); context_destroy(&destroy); v9x_gb_leave(v9x_gb_lock);
    IDirectDrawSurface_Release(side[1]); IDirectDrawSurface_Release(target);
    IDirectDrawSurface_Release(side[0]); IDirectDraw_Release(dd);
    v9x_gb_exit(ok ? "PASS" : "FAIL", ok ? 0u : 1u);
}
