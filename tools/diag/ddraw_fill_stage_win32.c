#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

/*
 * V9XDDF.EXE: the smallest DirectDraw exercise of a hardware 2D engine,
 * one step at a time, for a machine that may hang.
 *
 * Each stage is appended to C:\V9XDIAG\V9XDDF.TXT and flushed before the
 * stage runs, so after a hard hang the last line names the step that did
 * not return. The steps, in order of how much engine they touch:
 *
 *   1 DirectDrawCreate       no engine access
 *   2 SetCooperativeLevel    normal, no mode change
 *   3 CreateSurface          one 64x16 off-screen surface in video memory
 *   4 GetBltStatus(CANBLT)   the engine's validate and status read
 *   5 Blt COLORFILL          one engine fill
 *   6 Lock / read / Unlock   the drain and read-cache boundary
 *   7 Blt COLORFILL rect +   one rectangle fill and one non-overlapping
 *     Blt copy, same surface copy, read back against a CPU reference
 *
 * /nofill stops after step 4; /nocopy after step 6.
 *
 * The surface takes the desktop's depth (normal cooperative level, no mode
 * change), so a run after a mode switch exercises the engine at that depth:
 * 8, 16 or 32 bpp. The colours are chosen so every byte of a pixel differs
 * between them, which catches a wrong byte mask or a wrong depth field.
 */
#define DDF_TEXT_PATH "C:\\V9XDIAG\\V9XDDF.TXT"
#define DDF_WIDTH 64ul
#define DDF_HEIGHT 16ul
#define DDF_FILL_8 0x5aul
#define DDF_FILL_565 0x07e0ul
#define DDF_FILL_32 0x0012a5c3ul
#define DDF_MARK_8 0xa5ul
#define DDF_MARK_565 0xf81ful
#define DDF_MARK_32 0x00ed5a3cul
/* Step 7: columns [0,16) get the mark, then [0,32) is copied to [32,64),
 * so the reference row is mark, fill, mark, fill in 16-column quarters. */
#define DDF_MARK_COLUMNS 16ul
#define DDF_COPY_COLUMNS 32ul
/* DDPIXELFORMAT.dwRGBBitCount is its fourth DWORD. */
#define DDF_PF_RGB_BIT_COUNT 3

#define DDSD_CAPS 0x00000001ul
#define DDSD_HEIGHT 0x00000002ul
#define DDSD_WIDTH 0x00000004ul
#define DDSD_ZBUFFERBITDEPTH 0x00000040ul
#define DDSCAPS_PRIMARYSURFACE 0x00000200ul
#define DDSCAPS_ZBUFFER 0x00020000ul
#define DDSCAPS_OFFSCREENPLAIN 0x00000040ul
#define DDSCAPS_VIDEOMEMORY 0x00004000ul
#define DDSCL_NORMAL 0x00000008ul
#define DDBLT_COLORFILL 0x00000400ul
#define DDBLT_WAIT 0x01000000ul
#define DDGBS_CANBLT 0x00000001ul
#define DDLOCK_WAIT 0x00000001ul

typedef struct ddf_surface_desc {
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
    DWORD ddckCKDestOverlay[2];
    DWORD ddckCKDestBlt[2];
    DWORD ddckCKSrcOverlay[2];
    DWORD ddckCKSrcBlt[2];
    DWORD ddpfPixelFormat[8];
    DWORD ddsCaps;
} DDF_SURFACE_DESC;

typedef char ddf_surface_desc_is_108[
    sizeof(DDF_SURFACE_DESC) == 108 ? 1 : -1];

/* DDBLTFX: dwFillColor is the 21st DWORD. */
typedef struct ddf_bltfx {
    DWORD dwSize;
    DWORD before_fill[19];
    DWORD dwFillColor;
    DWORD colorkeys[4];
} DDF_BLTFX;

typedef char ddf_bltfx_is_100[sizeof(DDF_BLTFX) == 100 ? 1 : -1];

typedef struct ddf_object { void **vtbl; } DDF_OBJECT;

/* IDirectDraw and IDirectDrawSurface vtable slots (ddraw.h order). */
#define DD_RELEASE 2
#define DD_CREATE_SURFACE 6
#define DD_SET_COOPERATIVE_LEVEL 20
#define DDS_BLT 5
#define DDS_GET_BLT_STATUS 13
#define DDS_LOCK 25
#define DDS_UNLOCK 32

typedef HRESULT (WINAPI *ddf_create_fn)(void *, DDF_OBJECT **, void *);
typedef ULONG (WINAPI *ddf_release_fn)(DDF_OBJECT *);
typedef HRESULT (WINAPI *ddf_create_surface_fn)(DDF_OBJECT *,
    DDF_SURFACE_DESC *, DDF_OBJECT **, void *);
typedef HRESULT (WINAPI *ddf_coop_fn)(DDF_OBJECT *, HWND, DWORD);
typedef HRESULT (WINAPI *ddf_blt_fn)(DDF_OBJECT *, RECT *, DDF_OBJECT *,
    RECT *, DWORD, DDF_BLTFX *);
typedef HRESULT (WINAPI *ddf_blt_status_fn)(DDF_OBJECT *, DWORD);
typedef HRESULT (WINAPI *ddf_lock_fn)(DDF_OBJECT *, RECT *,
    DDF_SURFACE_DESC *, DWORD, HANDLE);
typedef HRESULT (WINAPI *ddf_unlock_fn)(DDF_OBJECT *, void *);

static HANDLE ddf_file = INVALID_HANDLE_VALUE;

static void ddf_hex(char *text, DWORD value)
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
static void ddf_line(const char *key, const char *value)
{
    DWORD written;
    if (ddf_file == INVALID_HANDLE_VALUE) {
        return;
    }
    WriteFile(ddf_file, key, (DWORD)lstrlenA(key), &written, 0);
    WriteFile(ddf_file, "=", 1ul, &written, 0);
    WriteFile(ddf_file, value, (DWORD)lstrlenA(value), &written, 0);
    WriteFile(ddf_file, "\r\n", 2ul, &written, 0);
    FlushFileBuffers(ddf_file);
}

static void ddf_hx(const char *key, DWORD value)
{
    char text[11];
    ddf_hex(text, value);
    ddf_line(key, text);
}

static int ddf_has_switch(const char *option)
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

static void ddf_zero(void *block, unsigned length)
{
    unsigned char *bytes = (unsigned char *)block;
    while (length-- != 0u) {
        *bytes++ = 0u;
    }
}

/* One pixel of a locked row at 1, 2 or 4 bytes per pixel. */
static DWORD ddf_pixel(const BYTE *row, DWORD x, DWORD bytes_per_pixel)
{
    if (bytes_per_pixel == 1ul) {
        return (DWORD)row[x];
    }
    if (bytes_per_pixel == 2ul) {
        return (DWORD)((const WORD *)row)[x];
    }
    return ((const DWORD *)row)[x] & 0x00fffffful;
}

static void ddf_finish(const char *result, UINT code)
{
    ddf_line("Result", result);
    if (ddf_file != INVALID_HANDLE_VALUE) {
        CloseHandle(ddf_file);
    }
    ExitProcess(code);
}

void WINAPI V9xDdFillStageEntry(void)
{
    HMODULE ddraw_module;
    ddf_create_fn create;
    DDF_OBJECT *ddraw = 0;
    DDF_OBJECT *surface = 0;
    DDF_SURFACE_DESC desc;
    DDF_BLTFX fx;
    HRESULT hr;
    HRESULT blt_hr;
    const BYTE *row;
    DWORD mismatches = 0ul;
    DWORD copy_mismatches = 0ul;
    DWORD y;
    DWORD x;
    DWORD bits;
    DWORD bytes_per_pixel;
    DWORD fill;
    DWORD mark;
    DWORD expect;
    HDC screen;
    RECT mark_rect;
    RECT source_rect;
    RECT dest_rect;

    CreateDirectoryA("C:\\V9XDIAG", 0);
    ddf_file = CreateFileA(DDF_TEXT_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                           CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, 0);
    ddf_line("[V9xDdFillStage]Build", V9X_BUILD_ID);

    screen = GetDC(0);
    bits = (DWORD)GetDeviceCaps(screen, BITSPIXEL);
    ReleaseDC(0, screen);
    ddf_hx("DesktopBpp", bits);
    if (bits == 8ul) {
        bytes_per_pixel = 1ul;
        fill = DDF_FILL_8;
        mark = DDF_MARK_8;
    } else if (bits == 32ul) {
        bytes_per_pixel = 4ul;
        fill = DDF_FILL_32;
        mark = DDF_MARK_32;
    } else {
        bytes_per_pixel = 2ul;
        fill = DDF_FILL_565;
        mark = DDF_MARK_565;
    }

    ddf_line("Stage", "1-directdrawcreate");
    ddraw_module = LoadLibraryA("DDRAW.DLL");
    create = ddraw_module != 0
        ? (ddf_create_fn)GetProcAddress(ddraw_module, "DirectDrawCreate") : 0;
    if (create == 0) {
        ddf_finish("NO-DDRAW", 2u);
    }
    hr = create(0, &ddraw, 0);
    ddf_hx("DirectDrawCreateHr", (DWORD)hr);
    if (hr != 0 || ddraw == 0) {
        ddf_finish("REVIEW", 1u);
    }

    ddf_line("Stage", "2-cooperative-normal");
    hr = ((ddf_coop_fn)ddraw->vtbl[DD_SET_COOPERATIVE_LEVEL])(
        ddraw, GetDesktopWindow(), DDSCL_NORMAL);
    ddf_hx("CooperativeHr", (DWORD)hr);

    /*
     * /zaddr: where DirectDraw places a 64x64 Z buffer, relative to the
     * primary. Lock returns a pointer and reads nothing, so this touches no
     * video memory: it answers whether the Z surface sits at the top of the
     * 4 MiB heap before anything reads it (the Gateway hard-locked reading a
     * Z buffer back, 2026-09-29).
     */
    if (ddf_has_switch("/zaddr")) {
        DDF_OBJECT *primary = 0;
        DDF_OBJECT *zbuffer = 0;
        DWORD primary_address = 0ul;

        ddf_line("Stage", "z1-create-primary");
        ddf_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS;
        desc.ddsCaps = DDSCAPS_PRIMARYSURFACE;
        hr = ((ddf_create_surface_fn)ddraw->vtbl[DD_CREATE_SURFACE])(
            ddraw, &desc, &primary, 0);
        ddf_hx("PrimaryHr", (DWORD)hr);
        if (hr == 0 && primary != 0) {
            ddf_zero(&desc, sizeof(desc));
            desc.dwSize = sizeof(desc);
            hr = ((ddf_lock_fn)primary->vtbl[DDS_LOCK])(primary, 0, &desc,
                                                        DDLOCK_WAIT, 0);
            ddf_hx("PrimaryLockHr", (DWORD)hr);
            if (hr == 0) {
                primary_address = (DWORD)desc.lpSurface;
                ((ddf_unlock_fn)primary->vtbl[DDS_UNLOCK])(primary,
                                                           desc.lpSurface);
            }
            ddf_hx("PrimaryAddress", primary_address);
        }

        ddf_line("Stage", "z2-create-zbuffer");
        ddf_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT |
                       DDSD_ZBUFFERBITDEPTH;
        desc.dwWidth = 64ul;
        desc.dwHeight = 64ul;
        desc.dwMipMapCount = 16ul;          /* dwZBufferBitDepth */
        desc.ddsCaps = DDSCAPS_ZBUFFER | DDSCAPS_VIDEOMEMORY;
        hr = ((ddf_create_surface_fn)ddraw->vtbl[DD_CREATE_SURFACE])(
            ddraw, &desc, &zbuffer, 0);
        ddf_hx("ZCreateHr", (DWORD)hr);
        if (hr == 0 && zbuffer != 0) {
            ddf_zero(&desc, sizeof(desc));
            desc.dwSize = sizeof(desc);
            hr = ((ddf_lock_fn)zbuffer->vtbl[DDS_LOCK])(zbuffer, 0, &desc,
                                                        DDLOCK_WAIT, 0);
            ddf_hx("ZLockHr", (DWORD)hr);
            if (hr == 0) {
                ddf_hx("ZAddress", (DWORD)desc.lpSurface);
                ddf_hx("ZPitch", (DWORD)desc.lPitch);
                ddf_hx("ZOffset", (DWORD)desc.lpSurface - primary_address);
                ddf_hx("ZEndOffset", (DWORD)desc.lpSurface - primary_address +
                                     (DWORD)desc.lPitch * 64ul);
                ((ddf_unlock_fn)zbuffer->vtbl[DDS_UNLOCK])(zbuffer,
                                                           desc.lpSurface);
            }
            ((ddf_release_fn)zbuffer->vtbl[DD_RELEASE])(zbuffer);
        }
        if (primary != 0) {
            ((ddf_release_fn)primary->vtbl[DD_RELEASE])(primary);
        }
        ((ddf_release_fn)ddraw->vtbl[DD_RELEASE])(ddraw);
        ddf_finish("ZADDR", 0u);
    }

    ddf_line("Stage", "3-create-offscreen");
    ddf_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = DDF_WIDTH;
    desc.dwHeight = DDF_HEIGHT;
    desc.ddsCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    hr = ((ddf_create_surface_fn)ddraw->vtbl[DD_CREATE_SURFACE])(
        ddraw, &desc, &surface, 0);
    ddf_hx("CreateSurfaceHr", (DWORD)hr);
    if (hr != 0 || surface == 0) {
        ((ddf_release_fn)ddraw->vtbl[DD_RELEASE])(ddraw);
        ddf_finish("REVIEW", 1u);
    }

    ddf_line("Stage", "4-getbltstatus-canblt");
    hr = ((ddf_blt_status_fn)surface->vtbl[DDS_GET_BLT_STATUS])(
        surface, DDGBS_CANBLT);
    ddf_hx("CanBltHr", (DWORD)hr);
    if (ddf_has_switch("/nofill")) {
        ((ddf_release_fn)surface->vtbl[DD_RELEASE])(surface);
        ((ddf_release_fn)ddraw->vtbl[DD_RELEASE])(ddraw);
        ddf_finish("STATUS-ONLY", 0u);
    }

    ddf_line("Stage", "5-blt-colorfill");
    ddf_zero(&fx, sizeof(fx));
    fx.dwSize = sizeof(fx);
    fx.dwFillColor = fill;
    hr = ((ddf_blt_fn)surface->vtbl[DDS_BLT])(
        surface, 0, 0, 0, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
    ddf_hx("FillHr", (DWORD)hr);

    ddf_line("Stage", "6-lock-read");
    ddf_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = ((ddf_lock_fn)surface->vtbl[DDS_LOCK])(surface, 0, &desc,
                                                DDLOCK_WAIT, 0);
    ddf_hx("LockHr", (DWORD)hr);
    if (hr == 0 && desc.lpSurface != 0) {
        ddf_hx("Pitch", (DWORD)desc.lPitch);
        ddf_hx("SurfaceBpp", desc.ddpfPixelFormat[DDF_PF_RGB_BIT_COUNT]);
        for (y = 0ul; y < DDF_HEIGHT; ++y) {
            row = (const BYTE *)desc.lpSurface + y * (DWORD)desc.lPitch;
            for (x = 0ul; x < DDF_WIDTH; ++x) {
                if (ddf_pixel(row, x, bytes_per_pixel) != fill) {
                    ++mismatches;
                }
            }
        }
        ddf_hx("FirstPixel", bytes_per_pixel == 4ul
                   ? *(const DWORD *)desc.lpSurface
                   : ddf_pixel((const BYTE *)desc.lpSurface, 0ul,
                               bytes_per_pixel));
        ((ddf_unlock_fn)surface->vtbl[DDS_UNLOCK])(surface, desc.lpSurface);
    }
    ddf_hx("PixelMismatches", mismatches);
    if (hr != 0 || mismatches != 0ul || ddf_has_switch("/nocopy")) {
        ((ddf_release_fn)surface->vtbl[DD_RELEASE])(surface);
        ((ddf_release_fn)ddraw->vtbl[DD_RELEASE])(ddraw);
        ddf_finish(hr == 0 && mismatches == 0ul ? "PASS" : "REVIEW",
                   hr == 0 && mismatches == 0ul ? 0u : 1u);
    }

    /*
     * Step 7. The copy's rectangles do not overlap, so a driver that sends
     * overlapping same-surface copies to the CPU still gives this one to
     * its engine.
     */
    ddf_line("Stage", "7-rect-fill-and-copy");
    mark_rect.left = 0;
    mark_rect.top = 0;
    mark_rect.right = (LONG)DDF_MARK_COLUMNS;
    mark_rect.bottom = (LONG)DDF_HEIGHT;
    fx.dwFillColor = mark;
    hr = ((ddf_blt_fn)surface->vtbl[DDS_BLT])(
        surface, &mark_rect, 0, 0, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
    ddf_hx("MarkFillHr", (DWORD)hr);
    blt_hr = hr;
    source_rect.left = 0;
    source_rect.top = 0;
    source_rect.right = (LONG)DDF_COPY_COLUMNS;
    source_rect.bottom = (LONG)DDF_HEIGHT;
    dest_rect.left = (LONG)DDF_COPY_COLUMNS;
    dest_rect.top = 0;
    dest_rect.right = (LONG)(2ul * DDF_COPY_COLUMNS);
    dest_rect.bottom = (LONG)DDF_HEIGHT;
    hr = ((ddf_blt_fn)surface->vtbl[DDS_BLT])(
        surface, &dest_rect, surface, &source_rect, DDBLT_WAIT, 0);
    ddf_hx("CopyHr", (DWORD)hr);
    if (blt_hr == 0) {
        blt_hr = hr;
    }

    ddf_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = ((ddf_lock_fn)surface->vtbl[DDS_LOCK])(surface, 0, &desc,
                                                DDLOCK_WAIT, 0);
    ddf_hx("CopyLockHr", (DWORD)hr);
    if (hr == 0 && desc.lpSurface != 0) {
        for (y = 0ul; y < DDF_HEIGHT; ++y) {
            row = (const BYTE *)desc.lpSurface + y * (DWORD)desc.lPitch;
            for (x = 0ul; x < DDF_WIDTH; ++x) {
                expect = ((x / DDF_MARK_COLUMNS) & 1ul) == 0ul ? mark : fill;
                if (ddf_pixel(row, x, bytes_per_pixel) != expect) {
                    ++copy_mismatches;
                }
            }
        }
        ddf_hx("CopyPixel32", ddf_pixel((const BYTE *)desc.lpSurface,
                                        DDF_COPY_COLUMNS, bytes_per_pixel));
        ((ddf_unlock_fn)surface->vtbl[DDS_UNLOCK])(surface, desc.lpSurface);
    }
    ddf_hx("CopyMismatches", copy_mismatches);

    ((ddf_release_fn)surface->vtbl[DD_RELEASE])(surface);
    ((ddf_release_fn)ddraw->vtbl[DD_RELEASE])(ddraw);
    if (hr == 0) {
        hr = blt_hr;
    }
    ddf_finish(hr == 0 && copy_mismatches == 0ul ? "PASS" : "REVIEW",
               hr == 0 && copy_mismatches == 0ul ? 0u : 1u);
}
