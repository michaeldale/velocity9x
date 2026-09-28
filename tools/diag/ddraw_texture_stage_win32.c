#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

/*
 * V9XTXM.EXE: the DirectDraw surface traffic of V9XDDP's texture matrix,
 * without Direct3D, one flushed line per operation, for a machine that
 * hard-locked somewhere inside that matrix.
 *
 * It keeps a 256x256 off-screen surface alive as the probe keeps its render
 * target, then walks the matrix's cells: sizes 64, 128 and 256, ARGB1555
 * and ARGB4444, plain and gapped (a 64x64 filler, a half-size level
 * created separately, then AddAttachedSurface). Each cell creates its
 * surfaces, fills them by CPU through Lock, reads one texel back, fills the
 * scratch target the way the probe clears it, and releases everything.
 * The matrix runs /passes:N times (default 4) to catch anything cumulative.
 *
 * Every line is written through and flushed before the operation it names,
 * so after a hard lock the last line is the operation that did not return.
 */
#define TXM_TEXT_PATH "C:\\V9XDIAG\\V9XTXM.TXT"
#define TXM_TARGET_EDGE 256ul
#define TXM_DEFAULT_PASSES 4ul

#define DDSD_CAPS 0x00000001ul
#define DDSD_HEIGHT 0x00000002ul
#define DDSD_WIDTH 0x00000004ul
#define DDSD_PIXELFORMAT 0x00001000ul
#define DDSCAPS_OFFSCREENPLAIN 0x00000040ul
#define DDSCAPS_TEXTURE 0x00001000ul
#define DDSCAPS_VIDEOMEMORY 0x00004000ul
#define DDSCAPS_MIPMAP 0x00400000ul
#define DDSCL_NORMAL 0x00000008ul
#define DDLOCK_WAIT 0x00000001ul
#define DDPF_RGB_ALPHA 0x00000041ul

typedef struct txm_pixel_format {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    DWORD dwRGBBitCount;
    DWORD dwRBitMask;
    DWORD dwGBitMask;
    DWORD dwBBitMask;
    DWORD dwRGBAlphaBitMask;
} TXM_PIXEL_FORMAT;

typedef struct txm_surface_desc {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    LONG lPitch;
    DWORD dwBackBufferCount;
    DWORD dwMipMapCount;
    DWORD dwAlphaBitDepth;
    DWORD dwReserved;
    void *lpSurface;
    DWORD colorkeys[8];
    TXM_PIXEL_FORMAT ddpfPixelFormat;
    DWORD ddsCaps;
} TXM_SURFACE_DESC;

typedef char txm_surface_desc_is_108[
    sizeof(TXM_SURFACE_DESC) == 108 ? 1 : -1];

typedef struct txm_object { void **vtbl; } TXM_OBJECT;

#define DD_RELEASE 2
#define DD_CREATE_SURFACE 6
#define DD_SET_COOPERATIVE_LEVEL 20
#define DDS_ADD_ATTACHED_SURFACE 3
#define DDS_LOCK 25
#define DDS_UNLOCK 32

typedef HRESULT (WINAPI *txm_create_fn)(void *, TXM_OBJECT **, void *);
typedef ULONG (WINAPI *txm_release_fn)(TXM_OBJECT *);
typedef HRESULT (WINAPI *txm_create_surface_fn)(TXM_OBJECT *,
    TXM_SURFACE_DESC *, TXM_OBJECT **, void *);
typedef HRESULT (WINAPI *txm_coop_fn)(TXM_OBJECT *, HWND, DWORD);
typedef HRESULT (WINAPI *txm_attach_fn)(TXM_OBJECT *, TXM_OBJECT *);
typedef HRESULT (WINAPI *txm_lock_fn)(TXM_OBJECT *, RECT *,
    TXM_SURFACE_DESC *, DWORD, HANDLE);
typedef HRESULT (WINAPI *txm_unlock_fn)(TXM_OBJECT *, void *);

static HANDLE txm_file = INVALID_HANDLE_VALUE;
static DWORD txm_op = 0ul;

static void txm_append(char *text, int *at, const char *piece)
{
    while (*piece != '\0') {
        text[(*at)++] = *piece++;
    }
}

static void txm_append_hex(char *text, int *at, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    int index;
    for (index = 0; index < 8; ++index) {
        text[(*at)++] = digits[(value >> ((7 - index) * 4)) & 15u];
    }
}

/* "op=NNNNNNNN <what> a=<value>", flushed before the caller proceeds. */
static void txm_log(const char *what, DWORD value)
{
    char line[128];
    int at = 0;
    DWORD written;
    if (txm_file == INVALID_HANDLE_VALUE) {
        return;
    }
    txm_append(line, &at, "op=");
    txm_append_hex(line, &at, txm_op++);
    txm_append(line, &at, " ");
    txm_append(line, &at, what);
    txm_append(line, &at, " v=");
    txm_append_hex(line, &at, value);
    txm_append(line, &at, "\r\n");
    WriteFile(txm_file, line, (DWORD)at, &written, 0);
    FlushFileBuffers(txm_file);
}

static void txm_zero(void *block, unsigned length)
{
    unsigned char *bytes = (unsigned char *)block;
    while (length-- != 0u) {
        *bytes++ = 0u;
    }
}

static DWORD txm_passes(void)
{
    const char *line = GetCommandLineA();
    const char *key = "/passes:";
    unsigned offset;
    unsigned index;
    DWORD value = 0ul;
    for (offset = 0u; line[offset] != '\0'; ++offset) {
        for (index = 0u; key[index] != '\0'; ++index) {
            if (line[offset + index] != key[index]) {
                break;
            }
        }
        if (key[index] == '\0') {
            offset += index;
            while (line[offset] >= '0' && line[offset] <= '9') {
                value = value * 10ul + (DWORD)(line[offset++] - '0');
            }
            return value != 0ul ? value : TXM_DEFAULT_PASSES;
        }
    }
    return TXM_DEFAULT_PASSES;
}

static TXM_OBJECT *txm_create(TXM_OBJECT *ddraw, DWORD edge, DWORD caps,
                              int alpha4444, int texture)
{
    TXM_SURFACE_DESC desc;
    TXM_OBJECT *surface = 0;
    HRESULT hr;

    txm_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = edge;
    desc.dwHeight = edge;
    desc.ddsCaps = caps;
    if (texture) {
        desc.dwFlags |= DDSD_PIXELFORMAT;
        desc.ddpfPixelFormat.dwSize = sizeof(TXM_PIXEL_FORMAT);
        desc.ddpfPixelFormat.dwFlags = DDPF_RGB_ALPHA;
        desc.ddpfPixelFormat.dwRGBBitCount = 16ul;
        if (alpha4444) {
            desc.ddpfPixelFormat.dwRBitMask = 0x0f00ul;
            desc.ddpfPixelFormat.dwGBitMask = 0x00f0ul;
            desc.ddpfPixelFormat.dwBBitMask = 0x000ful;
            desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0xf000ul;
        } else {
            desc.ddpfPixelFormat.dwRBitMask = 0x7c00ul;
            desc.ddpfPixelFormat.dwGBitMask = 0x03e0ul;
            desc.ddpfPixelFormat.dwBBitMask = 0x001ful;
            desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0x8000ul;
        }
    }
    txm_log("create-edge", edge);
    hr = ((txm_create_surface_fn)ddraw->vtbl[DD_CREATE_SURFACE])(
        ddraw, &desc, &surface, 0);
    txm_log("create-hr", (DWORD)hr);
    return hr == 0 ? surface : 0;
}

/* CPU fill through Lock, as V9XDDP's fill helpers do; returns the linear
 * address the lock handed out so an overrun or a top-of-VRAM surface shows
 * up in the log. */
static void txm_fill(TXM_OBJECT *surface, WORD texel)
{
    TXM_SURFACE_DESC desc;
    BYTE *row;
    DWORD y;
    DWORD x;
    HRESULT hr;

    txm_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    txm_log("lock", 0ul);
    hr = ((txm_lock_fn)surface->vtbl[DDS_LOCK])(surface, 0, &desc,
                                               DDLOCK_WAIT, 0);
    txm_log("lock-hr", (DWORD)hr);
    if (hr != 0 || desc.lpSurface == 0) {
        return;
    }
    txm_log("lock-address", (DWORD)desc.lpSurface);
    txm_log("lock-pitch", (DWORD)desc.lPitch);
    txm_log("lock-last-byte",
            (DWORD)desc.lpSurface + (DWORD)desc.lPitch * desc.dwHeight - 1ul);
    row = (BYTE *)desc.lpSurface;
    for (y = 0ul; y < desc.dwHeight; ++y) {
        WORD *line = (WORD *)row;
        for (x = 0ul; x < desc.dwWidth; ++x) {
            line[x] = texel;
        }
        row += desc.lPitch;
    }
    txm_log("fill-done", *(WORD *)desc.lpSurface);
    ((txm_unlock_fn)surface->vtbl[DDS_UNLOCK])(surface, desc.lpSurface);
    txm_log("unlock", 0ul);
}

static void txm_release(TXM_OBJECT *surface)
{
    if (surface != 0) {
        txm_log("release", 0ul);
        ((txm_release_fn)surface->vtbl[DD_RELEASE])(surface);
    }
}

void WINAPI V9xTextureStageEntry(void)
{
    static const DWORD sizes[3] = { 64ul, 128ul, 256ul };
    HMODULE ddraw_module;
    txm_create_fn create;
    TXM_OBJECT *ddraw = 0;
    TXM_OBJECT *target;
    TXM_OBJECT *top;
    TXM_OBJECT *filler;
    TXM_OBJECT *level;
    HRESULT hr;
    DWORD passes;
    DWORD pass;
    DWORD si;
    DWORD fi;
    DWORD li;
    DWORD written;

    CreateDirectoryA("C:\\V9XDIAG", 0);
    txm_file = CreateFileA(TXM_TEXT_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                           CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, 0);
    WriteFile(txm_file, "Build=" V9X_BUILD_ID "\r\n",
              (DWORD)lstrlenA("Build=" V9X_BUILD_ID "\r\n"), &written, 0);
    passes = txm_passes();
    txm_log("passes", passes);

    txm_log("directdrawcreate", 0ul);
    ddraw_module = LoadLibraryA("DDRAW.DLL");
    create = ddraw_module != 0
        ? (txm_create_fn)GetProcAddress(ddraw_module, "DirectDrawCreate") : 0;
    if (create == 0 || create(0, &ddraw, 0) != 0 || ddraw == 0) {
        txm_log("result-no-ddraw", 0ul);
        ExitProcess(2u);
    }
    hr = ((txm_coop_fn)ddraw->vtbl[DD_SET_COOPERATIVE_LEVEL])(
        ddraw, GetDesktopWindow(), DDSCL_NORMAL);
    txm_log("cooperative-hr", (DWORD)hr);

    target = txm_create(ddraw, TXM_TARGET_EDGE,
                        DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY, 0, 0);

    for (pass = 0ul; pass < passes; ++pass)
    for (si = 0ul; si < 3ul; ++si)
    for (fi = 0ul; fi < 2ul; ++fi)
    for (li = 0ul; li < 2ul; ++li) {
        txm_log("cell", (pass << 24) | (si << 16) | (fi << 8) | li);
        filler = 0;
        level = 0;
        top = txm_create(ddraw, sizes[si],
                         DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY |
                         (li != 0ul ? DDSCAPS_MIPMAP : 0ul),
                         (int)fi, 1);
        if (top != 0 && li != 0ul) {
            filler = txm_create(ddraw, 64ul,
                                DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY,
                                (int)fi, 1);
            level = txm_create(ddraw, sizes[si] / 2ul,
                               DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY |
                               DDSCAPS_MIPMAP, (int)fi, 1);
            if (level != 0) {
                txm_log("attach", 0ul);
                hr = ((txm_attach_fn)top->vtbl[DDS_ADD_ATTACHED_SURFACE])(
                    top, level);
                txm_log("attach-hr", (DWORD)hr);
            }
        }
        if (top != 0) {
            txm_fill(top, 0x83e0u);
        }
        if (level != 0) {
            txm_fill(level, 0xfc1fu);
        }
        if (target != 0) {
            txm_fill(target, 0u);
        }
        txm_release(level);
        txm_release(filler);
        txm_release(top);
    }

    txm_release(target);
    txm_log("release-ddraw", 0ul);
    ((txm_release_fn)ddraw->vtbl[DD_RELEASE])(ddraw);
    txm_log("result-pass", 0ul);
    CloseHandle(txm_file);
    ExitProcess(0u);
}
