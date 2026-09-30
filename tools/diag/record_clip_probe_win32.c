/* Direct DX5 record-stream probe. Bypass runtime preclipping, hold the
 * Win16 mutex exactly as DDRAW does, and compare every target pixel against
 * separate calls in original order. No mode switch or driver reinitialisation.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include "velocity9x/build.h"
#include "velocity9x/win9x_ddraw_abi.h"

#define V9X_RCP_PATH "C:\\V9XDIAG\\V9XRCLP.INI"
#define V9X_RCP_EDGE 64ul
#define V9X_RCP_PIXELS (V9X_RCP_EDGE * V9X_RCP_EDGE)
#define V9X_RCP_STREAM_BYTES 8192ul
#define V9X_RCP_LIST 4u
#define V9X_RCP_FAN 6u
#define V9X_RCP_TL 3u

typedef HRESULT (WINAPI *V9X_RCP_CREATE)(GUID *, LPDIRECTDRAW *, IUnknown *);
typedef void (WINAPI *V9X_RCP_GETLOCK)(void **);
typedef void (WINAPI *V9X_RCP_LEVEL)(void *);
typedef DWORD (WINAPI *V9X_RCP_CONTEXT)(V9X_D3DHAL_CONTEXTCREATEDATA *);
typedef DWORD (WINAPI *V9X_RCP_DESTROY)(V9X_D3DHAL_CONTEXTDESTROYDATA *);
typedef DWORD (WINAPI *V9X_RCP_INFO)(V9X_DDHAL_GETDRIVERINFODATA *);
typedef DWORD (WINAPI *V9X_RCP_DRAW)(V9X_D3DHAL_DRAWPRIMITIVESDATA *);
static int v9x_rcp_refusal;
static V9X_RCP_LEVEL v9x_rcp_enter, v9x_rcp_leave;
static void *v9x_rcp_lock;
static WORD v9x_rcp_reference[V9X_RCP_PIXELS];
static WORD v9x_rcp_pixels[V9X_RCP_PIXELS];
static BYTE v9x_rcp_stream[V9X_RCP_STREAM_BYTES];
static V9X_D3DTLVERTEX v9x_rcp_vertices[192];

static void v9x_rcp_zero(void *p, DWORD n)
{
    DWORD i;
    for (i = 0ul; i < n; ++i) { ((BYTE *)p)[i] = 0u; }
}
static void v9x_rcp_text(const char *key, const char *value)
{
    WritePrivateProfileStringA("RecordClipProbe", key, value, V9X_RCP_PATH);
}
static void v9x_rcp_uint(const char *key, DWORD value)
{
    char text[24];
    wsprintfA(text, "%lu", value);
    v9x_rcp_text(key, text);
}
/* Same ordinal walk used by the calibrated Win16-lock probe. */
static FARPROC v9x_rcp_ordinal(DWORD ordinal)
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
static void v9x_rcp_vertex(V9X_D3DTLVERTEX *v, float x, float y, DWORD color)
{
    v->sx = x; v->sy = y; v->sz = 0.5f; v->rhw = 1.0f;
    v->color = color; v->specular = 0xff000000ul;
    v->tu = 0.0f; v->tv = 0.0f;
}
/* Record 0 is an on-target survivor, 1 clips to a polygon, 2 is a fan
 * whose two triangles overhang both horizontal edges, 3 is a later survivor.
 * Opaque overlap makes their order visible without unsupported blend state.
 */
static DWORD v9x_rcp_geometry(DWORD record, DWORD repeated)
{
    DWORD i;
    if (record == 0ul) {
        for (i = 0ul; i < repeated; ++i) {
            v9x_rcp_vertex(&v9x_rcp_vertices[i*3ul], 8.0f, 8.0f, 0xffff0000ul);
            v9x_rcp_vertex(&v9x_rcp_vertices[i*3ul+1ul], 56.0f, 8.0f, 0xffff0000ul);
            v9x_rcp_vertex(&v9x_rcp_vertices[i*3ul+2ul], 8.0f, 56.0f, 0xffff0000ul);
        }
        return repeated * 3ul;
    }
    if (v9x_rcp_refusal && (record == 1ul || record == 2ul)) {
        v9x_rcp_vertex(&v9x_rcp_vertices[0], 4.0f, 4.0f, 0xff00ff00ul);
        v9x_rcp_vertex(&v9x_rcp_vertices[1], 48.0f, 4.0f, 0xff00ff00ul);
        v9x_rcp_vertex(&v9x_rcp_vertices[2], 16.0f, 60.0f, 0xff00ff00ul);
        if (record == 1ul) {
            for (i = 0ul; i < 3ul; ++i) {
                ((DWORD *)&v9x_rcp_vertices[i].sz)[0] = 0x3f800001ul;
            }
            /* This valid triangle belongs to the refused record too. A
             * triangle-wise retry would incorrectly make it visible. */
            v9x_rcp_vertex(&v9x_rcp_vertices[3], 52.0f, 2.0f, 0xff00fffful);
            v9x_rcp_vertex(&v9x_rcp_vertices[4], 62.0f, 2.0f, 0xff00fffful);
            v9x_rcp_vertex(&v9x_rcp_vertices[5], 62.0f, 10.0f, 0xff00fffful);
            return 6ul;
        }
        v9x_rcp_vertex(&v9x_rcp_vertices[3], 4.0f, 60.0f, 0xff0000fful);
        for (i = 0ul; i < 3ul; ++i) { v9x_rcp_vertices[i].color = 0xff0000fful; }
        return 4ul;
    }
    if (record == 1ul) {
        v9x_rcp_vertex(&v9x_rcp_vertices[0], -16.0f, 4.0f, 0xff00ff00ul);
        v9x_rcp_vertex(&v9x_rcp_vertices[1], 48.0f, 4.0f, 0xff00ff00ul);
        v9x_rcp_vertex(&v9x_rcp_vertices[2], 16.0f, 72.0f, 0xff00ff00ul);
        return 3ul;
    }
    if (record == 2ul) {
        v9x_rcp_vertex(&v9x_rcp_vertices[0], -12.0f, 12.0f, 0xff0000fful);
        v9x_rcp_vertex(&v9x_rcp_vertices[1], 72.0f, 12.0f, 0xff0000fful);
        v9x_rcp_vertex(&v9x_rcp_vertices[2], 72.0f, 52.0f, 0xff0000fful);
        v9x_rcp_vertex(&v9x_rcp_vertices[3], -12.0f, 52.0f, 0xff0000fful);
        return 4ul;
    }
    v9x_rcp_vertex(&v9x_rcp_vertices[0], 20.0f, 20.0f, 0xffffff00ul);
    v9x_rcp_vertex(&v9x_rcp_vertices[1], 52.0f, 20.0f, 0xffffff00ul);
    v9x_rcp_vertex(&v9x_rcp_vertices[2], 20.0f, 52.0f, 0xffffff00ul);
    return 3ul;
}
static BYTE *v9x_rcp_record(BYTE *cursor, DWORD record, DWORD repeated, int states)
{
    V9X_D3DHAL_DRAWPRIMCOUNTS *counts = (V9X_D3DHAL_DRAWPRIMCOUNTS *)cursor;
    DWORD n = v9x_rcp_geometry(record, repeated);
    DWORD i;
    DWORD *pairs;
    counts->wNumStateChanges = states ? 4u : 0u;
    counts->wPrimitiveType = record == 2ul ? V9X_RCP_FAN : V9X_RCP_LIST;
    counts->wVertexType = V9X_RCP_TL;
    counts->wNumVertices = (WORD)n;
    cursor += sizeof(*counts);
    if (states) {
        pairs = (DWORD *)cursor;
        pairs[0] = 22ul; pairs[1] = 1ul; /* CULLMODE NONE */
        pairs[2] = 7ul; pairs[3] = 0ul;  /* ZENABLE off */
        pairs[4] = 14ul; pairs[5] = 0ul; /* ZWRITEENABLE off */
        pairs[6] = 27ul; pairs[7] = 0ul; /* ALPHABLENDENABLE off */
        cursor += 32ul;
    }
    cursor = (BYTE *)(((DWORD)cursor + 31ul) & ~31ul);
    for (i = 0ul; i < n; ++i) { ((V9X_D3DTLVERTEX *)cursor)[i] = v9x_rcp_vertices[i]; }
    return cursor + n * sizeof(V9X_D3DTLVERTEX);
}
static int v9x_rcp_submit(V9X_RCP_DRAW callback, DWORD context,
                           DWORD repeated, int serial, int redundant, int reverse)
{
    V9X_D3DHAL_DRAWPRIMITIVESDATA data;
    BYTE *begin = (BYTE *)(((DWORD)v9x_rcp_stream + 31ul) & ~31ul);
    BYTE *cursor = begin;
    DWORD i, record;
    int ok = 1;
    v9x_rcp_zero(&data, sizeof(data));
    data.dwhContext = context;
    data.lpvData = begin;
    for (i = 0ul; i < 4ul; ++i) {
        record = reverse ? 3ul-i : i;
        cursor = v9x_rcp_record(cursor, record, repeated, redundant || i == 0ul);
        if (serial) {
            v9x_rcp_zero(cursor, sizeof(V9X_D3DHAL_DRAWPRIMCOUNTS));
            v9x_rcp_enter(v9x_rcp_lock);
            data.ddrval = 0xfffffffful;
            if (callback(&data) != V9X_DDHAL_DRIVER_HANDLED || data.ddrval != 0ul) { ok = 0; }
            v9x_rcp_leave(v9x_rcp_lock);
            cursor = begin;
        }
    }
    if (!serial) {
        v9x_rcp_zero(cursor, sizeof(V9X_D3DHAL_DRAWPRIMCOUNTS));
        v9x_rcp_enter(v9x_rcp_lock);
        data.ddrval = 0xfffffffful;
        if (callback(&data) != V9X_DDHAL_DRIVER_HANDLED || data.ddrval != 0ul) { ok = 0; }
        v9x_rcp_leave(v9x_rcp_lock);
    }
    return ok;
}
static int v9x_rcp_clear(LPDIRECTDRAWSURFACE target)
{
    DDBLTFX fx;
    v9x_rcp_zero(&fx, sizeof(fx)); fx.dwSize = sizeof(fx);
    return IDirectDrawSurface_Blt(target, 0, 0, 0, DDBLT_COLORFILL | DDBLT_WAIT, &fx) == DD_OK;
}
static int v9x_rcp_capture(LPDIRECTDRAWSURFACE target, WORD *pixels)
{
    DDSURFACEDESC desc;
    DWORD x, y;
    v9x_rcp_zero(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
    if (IDirectDrawSurface_Lock(target, 0, &desc, DDLOCK_WAIT, 0) != DD_OK) { return 0; }
    for (y = 0ul; y < V9X_RCP_EDGE; ++y) {
        const WORD *row = (const WORD *)((const BYTE *)desc.lpSurface + y * (DWORD)desc.lPitch);
        for (x = 0ul; x < V9X_RCP_EDGE; ++x) { pixels[y*V9X_RCP_EDGE+x] = row[x]; }
    }
    return IDirectDrawSurface_Unlock(target, 0) == DD_OK;
}
static DWORD v9x_rcp_mismatch(void)
{
    DWORD i, count = 0ul;
    for (i = 0ul; i < V9X_RCP_PIXELS; ++i) {
        if (v9x_rcp_pixels[i] != v9x_rcp_reference[i]) { ++count; }
    }
    return count;
}
static int v9x_rcp_image(const char *name, const WORD *pixels, DWORD green_mask)
{
    char path[80];
    const char header[] = "P6\n64 64\n255\n";
    BYTE rgb[V9X_RCP_PIXELS*3ul];
    HANDLE file;
    DWORD i, wrote;
    int ok;
    for (i = 0ul; i < V9X_RCP_PIXELS; ++i) {
        DWORD value = pixels[i];
        rgb[i*3ul] = (BYTE)((green_mask == 0x7e0ul ? value >> 11 : value >> 10) & 31ul) * 255ul / 31ul;
        rgb[i*3ul+1ul] = (BYTE)((value >> 5) & (green_mask == 0x7e0ul ? 63ul : 31ul)) * 255ul / (green_mask == 0x7e0ul ? 63ul : 31ul);
        rgb[i*3ul+2ul] = (BYTE)(value & 31ul) * 255ul / 31ul;
    }
    wsprintfA(path, "C:\\V9XDIAG\\%s.PPM", name);
    file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) { return 0; }
    ok = WriteFile(file, header, sizeof(header)-1u, &wrote, 0) && wrote == sizeof(header)-1u;
    if (!WriteFile(file, rgb, sizeof(rgb), &wrote, 0) || wrote != sizeof(rgb)) { ok = 0; }
    CloseHandle(file);
    return ok;
}
void __stdcall V9xRecordClipProbeEntry(void)
{
    static const BYTE guid[16] = {0xe1u,0x84u,0xa5u,0x0bu,0xb6u,0x70u,0xd0u,0x11u,0x88u,0x9du,0u,0xaau,0u,0xbbu,0xb7u,0x6au};
    LPDIRECTDRAW dd = 0;
    LPDIRECTDRAWSURFACE target = 0;
    V9X_RCP_CREATE create;
    V9X_RCP_GETLOCK getlock;
    V9X_RCP_CONTEXT context_create;
    V9X_RCP_DESTROY context_destroy;
    V9X_RCP_INFO info;
    V9X_RCP_DRAW draw;
    V9X_DD_SHARED *shared;
    V9X_DD32BITDRIVERDATA driver;
    V9X_DCICMD command;
    V9X_DDHAL_GETDRIVERINFODATA request;
    V9X_D3DHAL_CALLBACKS2 callbacks;
    V9X_D3DHAL_CONTEXTCREATEDATA context;
    V9X_D3DHAL_CONTEXTDESTROYDATA destroy;
    DDSURFACEDESC desc;
    HDC dc;
    DWORD i, test, before_calls, before_clips, before_batches, mismatch;
    DWORD baseline_refused, baseline_fifo, baseline_idle, baseline_resets;
    DWORD green_mask;
    char key[48], name[16];
    int result, ok = 1;
    {
        const char *arg = GetCommandLineA();
        if (*arg == '"') {
            ++arg;
            while (*arg && *arg != '"') { ++arg; }
            if (*arg) { ++arg; }
        } else {
            while (*arg && *arg != ' ' && *arg != '\t') { ++arg; }
        }
        while (*arg) {
            while (*arg == ' ' || *arg == '\t') { ++arg; }
            if (arg[0] == '-' && arg[1] == 'r' && arg[2] == 'e' &&
                arg[3] == 'f' && arg[4] == 'u' && arg[5] == 's' &&
                arg[6] == 'a' && arg[7] == 'l' &&
                (arg[8] == 0 || arg[8] == ' ' || arg[8] == '\t')) {
                v9x_rcp_refusal = 1; break;
            }
            while (*arg && *arg != ' ' && *arg != '\t') { ++arg; }
        }
    }
    CreateDirectoryA("C:\\V9XDIAG", 0);
    DeleteFileA(V9X_RCP_PATH);
    v9x_rcp_text("Build", V9X_BUILD_ID); v9x_rcp_text("Result", "RUNNING");
    create = (V9X_RCP_CREATE)GetProcAddress(LoadLibraryA("DDRAW.DLL"), "DirectDrawCreate");
    if (create == 0 || create(0, &dd, 0) != DD_OK) { v9x_rcp_text("Result", "FAIL-DDRAW"); ExitProcess(1u); }
    if (IDirectDraw_SetCooperativeLevel(dd, 0, DDSCL_NORMAL) != DD_OK) { ExitProcess(1u); }
    v9x_rcp_zero(&driver, sizeof(driver)); v9x_rcp_zero(&command, sizeof(command));
    command.dwCommand = V9X_DDGET32BITDRIVERNAME; command.dwVersion = V9X_DD_VERSION;
    dc = GetDC(0);
    result = dc ? ExtEscape(dc, V9X_DCICOMMAND, sizeof(command), (LPCSTR)&command, sizeof(driver), (LPSTR)&driver) : 0;
    if (dc) { ReleaseDC(0, dc); }
    shared = (V9X_DD_SHARED *)driver.dwContext;
    if (result <= 0 || shared == 0 || IsBadReadPtr(shared, sizeof(*shared)) ||
        shared->dwSize != sizeof(*shared) || shared->abi != V9X_DD_SHARED_ABI ||
        shared->engine.engine_type != (v9x_rcp_refusal ?
            V9X_DD_ENGINE_TYPE_INTEL_GEN3 : V9X_DD_ENGINE_TYPE_ATI_MACH64)) {
        v9x_rcp_text("Result", "FAIL-SHARED-OR-ENGINE"); ExitProcess(1u);
    }
    getlock = (V9X_RCP_GETLOCK)v9x_rcp_ordinal(93ul);
    v9x_rcp_enter = (V9X_RCP_LEVEL)v9x_rcp_ordinal(97ul);
    v9x_rcp_leave = (V9X_RCP_LEVEL)v9x_rcp_ordinal(98ul);
    if (!getlock || !v9x_rcp_enter || !v9x_rcp_leave) { v9x_rcp_text("Result", "FAIL-MUTEX"); ExitProcess(1u); }
    getlock(&v9x_rcp_lock);
    if (!v9x_rcp_lock) { ExitProcess(1u); }
    info = (V9X_RCP_INFO)shared->info.GetDriverInfo;
    context_create = (V9X_RCP_CONTEXT)shared->d3d_callbacks.ContextCreate;
    context_destroy = (V9X_RCP_DESTROY)shared->d3d_callbacks.ContextDestroy;
    if (!info || !context_create || !context_destroy) { ExitProcess(1u); }
    v9x_rcp_zero(&request, sizeof(request)); v9x_rcp_zero(&callbacks, sizeof(callbacks));
    request.dwSize = sizeof(request); request.dwExpectedSize = sizeof(callbacks); request.lpvData = &callbacks;
    for (i = 0ul; i < sizeof(guid); ++i) { request.guidInfo[i] = guid[i]; }
    v9x_rcp_enter(v9x_rcp_lock); info(&request); v9x_rcp_leave(v9x_rcp_lock);
    if (request.ddRVal != 0ul || !callbacks.DrawPrimitives) { v9x_rcp_text("Result", "FAIL-CALLBACKS"); ExitProcess(1u); }
    draw = (V9X_RCP_DRAW)callbacks.DrawPrimitives;
    v9x_rcp_zero(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = V9X_RCP_EDGE; desc.dwHeight = V9X_RCP_EDGE;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY | DDSCAPS_3DDEVICE;
    if (IDirectDraw_CreateSurface(dd, &desc, &target, 0) != DD_OK) { v9x_rcp_text("Result", "FAIL-TARGET"); ExitProcess(1u); }
    v9x_rcp_zero(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
    if (IDirectDrawSurface_GetSurfaceDesc(target, &desc) != DD_OK) { ExitProcess(1u); }
    green_mask = desc.ddpfPixelFormat.dwGBitMask;
    v9x_rcp_zero(&context, sizeof(context)); context.lpDDS = target; context.dwPID = GetCurrentProcessId();
    v9x_rcp_enter(v9x_rcp_lock); context_create(&context); v9x_rcp_leave(v9x_rcp_lock);
    if (context.ddrval != 0ul || context.dwhContext == 0ul) { v9x_rcp_text("Result", "FAIL-CONTEXT"); ExitProcess(1u); }
    v9x_rcp_uint("RefusalMode", (DWORD)v9x_rcp_refusal);
    baseline_refused = shared->d3d_diagnostics.batches_engine_refused;
    baseline_fifo = shared->engine.fifo_timeouts; baseline_idle = shared->engine.idle_timeouts;
    baseline_resets = shared->engine.reset_count;
    for (test = 0ul; test < 3ul; ++test) {
        DWORD repeated = test == 2ul ? 62ul : 1ul;
        if (!v9x_rcp_clear(target) || !v9x_rcp_submit(draw, context.dwhContext, repeated, 1, test == 1ul, 0) ||
            !v9x_rcp_capture(target, v9x_rcp_reference)) { ok = 0; break; }
        wsprintfA(name, "RCP%luREF", test);
        if (!v9x_rcp_image(name, v9x_rcp_reference, green_mask)) { ok = 0; }
        if (!v9x_rcp_clear(target)) { ok = 0; break; }
        before_calls = shared->d3d_diagnostics.r3d_list_calls;
        before_clips = shared->d3d_diagnostics.r3d_list_clipped;
        before_batches = shared->d3d_diagnostics.r3d_list_sink_batches;
        if (!v9x_rcp_submit(draw, context.dwhContext, repeated, 0, test == 1ul, 0) ||
            !v9x_rcp_capture(target, v9x_rcp_pixels)) { ok = 0; break; }
        mismatch = v9x_rcp_mismatch();
        wsprintfA(key, "Case%luMismatch", test); v9x_rcp_uint(key, mismatch);
        wsprintfA(key, "Case%luListCalls", test); v9x_rcp_uint(key, shared->d3d_diagnostics.r3d_list_calls-before_calls);
        wsprintfA(key, "Case%luClipped", test); v9x_rcp_uint(key, shared->d3d_diagnostics.r3d_list_clipped-before_clips);
        wsprintfA(key, "Case%luSinkBatches", test); v9x_rcp_uint(key, shared->d3d_diagnostics.r3d_list_sink_batches-before_batches);
        wsprintfA(name, "RCP%luRUN", test);
        if (!v9x_rcp_image(name, v9x_rcp_pixels, green_mask) || mismatch != 0ul ||
            (!v9x_rcp_refusal && shared->d3d_diagnostics.r3d_list_clipped == before_clips)) { ok = 0; }
        /* Prove the comparator detects order, rather than matching blank targets. */
        if (test == 0ul && !v9x_rcp_refusal) {
            if (!v9x_rcp_clear(target) || !v9x_rcp_submit(draw, context.dwhContext, 1ul, 0, 1, 1) ||
                !v9x_rcp_capture(target, v9x_rcp_pixels)) { ok = 0; break; }
            mismatch = v9x_rcp_mismatch(); v9x_rcp_uint("ReversedOrderMismatch", mismatch);
            if (mismatch == 0ul) { ok = 0; }
        }
    }
    v9x_rcp_uint("NewRefusals", shared->d3d_diagnostics.batches_engine_refused-baseline_refused);
    v9x_rcp_uint("NewFifoTimeouts", shared->engine.fifo_timeouts-baseline_fifo);
    v9x_rcp_uint("NewIdleTimeouts", shared->engine.idle_timeouts-baseline_idle);
    v9x_rcp_uint("NewResets", shared->engine.reset_count-baseline_resets);
    if ((!v9x_rcp_refusal &&
         shared->d3d_diagnostics.batches_engine_refused != baseline_refused) ||
        (v9x_rcp_refusal &&
         shared->d3d_diagnostics.batches_engine_refused-baseline_refused != 6ul &&
         shared->d3d_diagnostics.batches_engine_refused-baseline_refused != 9ul) ||
        shared->engine.fifo_timeouts != baseline_fifo || shared->engine.idle_timeouts != baseline_idle ||
        shared->engine.reset_count != baseline_resets) { ok = 0; }
    destroy.dwhContext = context.dwhContext; destroy.ddrval = 0xfffffffful;
    v9x_rcp_enter(v9x_rcp_lock); context_destroy(&destroy); v9x_rcp_leave(v9x_rcp_lock);
    IDirectDrawSurface_Release(target); IDirectDraw_Release(dd);
    v9x_rcp_text("Result", ok ? "PASS" : "FAIL");
    WritePrivateProfileStringA(0, 0, 0, V9X_RCP_PATH);
    ExitProcess(ok ? 0u : 1u);
}
