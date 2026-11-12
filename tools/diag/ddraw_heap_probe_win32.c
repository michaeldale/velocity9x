#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

/*
 * V9XHEAP.EXE: where DirectDraw puts surfaces when the driver publishes a
 * second, texture-only heap. Phase 0 of
 * docs\plans\intel-gen3-system-memory-textures.md.
 *
 * The measurement DRV splits the top MiB of its heap off as heap 1, with
 * every surface type but textures excluded, and records both ranges in
 * C:\V9XDIAG\V9XDDH.INI [Velocity9xPoolProbe]. This program creates 64x64
 * surfaces of one kind in video memory until DirectDraw refuses one, locks
 * each to find its address, counts how many landed in heap 1, and releases
 * them; then the next kind. Off-screen plain and Z surfaces must never land
 * there; textures should, once the HAL's own placement in heap 0 runs out
 * and it hands the rest back to DirectDraw. GetAvailableVidMem is asked for
 * each kind too. Writes C:\V9XDIAG\V9XHEAP.INI.
 */
#define HEAP_INI       "C:\\V9XDIAG\\V9XHEAP.INI"
#define HEAP_DDH_INI   "C:\\V9XDIAG\\V9XDDH.INI"
#define HEAP_MAX       4096u
#define HEAP_EDGE      64ul

#define DDSD_CAPS 0x00000001ul
#define DDSD_HEIGHT 0x00000002ul
#define DDSD_WIDTH 0x00000004ul
#define DDSD_ZBUFFERBITDEPTH 0x00000040ul
#define DDSCAPS_OFFSCREENPLAIN 0x00000040ul
#define DDSCAPS_TEXTURE 0x00001000ul
#define DDSCAPS_VIDEOMEMORY 0x00004000ul
#define DDSCAPS_ZBUFFER 0x00020000ul
#define DDSCL_NORMAL 0x00000008ul
#define DDLOCK_WAIT 0x00000001ul

typedef struct heap_pixel_format {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    DWORD dwRGBBitCount;
    DWORD dwRBitMask;
    DWORD dwGBitMask;
    DWORD dwBBitMask;
    DWORD dwRGBAlphaBitMask;
} HEAP_PIXEL_FORMAT;

/* DDSURFACEDESC; dwZBufferBitDepth shares dwMipMapCount's slot. */
typedef struct heap_surface_desc {
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
    HEAP_PIXEL_FORMAT ddpfPixelFormat;
    DWORD ddsCaps;
} HEAP_SURFACE_DESC;

typedef char heap_surface_desc_is_108[
    sizeof(HEAP_SURFACE_DESC) == 108 ? 1 : -1];

typedef struct heap_object { void **vtbl; } HEAP_OBJECT;

/* IDirectDraw2 for GetAvailableVidMem; IDirectDraw for the rest. */
#define DD_QUERY_INTERFACE 0
#define DD_RELEASE 2
#define DD_CREATE_SURFACE 6
#define DD_SET_COOPERATIVE_LEVEL 20
#define DD2_GET_AVAILABLE_VID_MEM 23
#define DDS_LOCK 25
#define DDS_UNLOCK 32

typedef HRESULT (WINAPI *heap_create_fn)(void *, HEAP_OBJECT **, void *);
typedef HRESULT (WINAPI *heap_query_fn)(HEAP_OBJECT *, const GUID *,
                                        HEAP_OBJECT **);
typedef ULONG (WINAPI *heap_release_fn)(HEAP_OBJECT *);
typedef HRESULT (WINAPI *heap_create_surface_fn)(HEAP_OBJECT *,
    HEAP_SURFACE_DESC *, HEAP_OBJECT **, void *);
typedef HRESULT (WINAPI *heap_coop_fn)(HEAP_OBJECT *, HWND, DWORD);
typedef HRESULT (WINAPI *heap_avail_fn)(HEAP_OBJECT *, DWORD *, DWORD *,
                                        DWORD *);
typedef HRESULT (WINAPI *heap_lock_fn)(HEAP_OBJECT *, RECT *,
    HEAP_SURFACE_DESC *, DWORD, HANDLE);
typedef HRESULT (WINAPI *heap_unlock_fn)(HEAP_OBJECT *, void *);

/* {B3A6F3E0-2B43-11CF-A2DE-00AA00B93356} */
static const GUID heap_iid_ddraw2 = {
    0xb3a6f3e0ul, 0x2b43u, 0x11cfu,
    { 0xa2u, 0xdeu, 0x00u, 0xaau, 0x00u, 0xb9u, 0x33u, 0x56u }
};

static HEAP_OBJECT *heap_surfaces[HEAP_MAX];
static DWORD heap1_start;
static DWORD heap1_end;

static void heap_zero(void *block, unsigned length)
{
    unsigned char *bytes = (unsigned char *)block;

    while (length-- != 0u) {
        *bytes++ = 0u;
    }
}

static void heap_hex(char *text, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < 8; ++index) {
        text[index] = digits[(value >> ((7 - index) * 4)) & 15u];
    }
    text[8] = '\0';
}

static void heap_key(const char *section, const char *key, DWORD value)
{
    char text[12];

    heap_hex(text, value);
    WritePrivateProfileStringA(section, key, text, HEAP_INI);
}

static DWORD heap_read_hex(const char *key)
{
    char text[16];
    DWORD value = 0ul;
    int index;

    GetPrivateProfileStringA("Velocity9xPoolProbe", key, "", text,
                             sizeof(text), HEAP_DDH_INI);
    for (index = 0; text[index] != '\0'; ++index) {
        char c = text[index];

        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= (DWORD)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            value |= (DWORD)(c - 'A' + 10);
        }
    }
    return value;
}

static void heap_available(HEAP_OBJECT *ddraw2, const char *section,
                           DWORD caps)
{
    DWORD total = 0ul;
    DWORD free_bytes = 0ul;
    HRESULT hr;

    if (ddraw2 == 0) {
        return;
    }
    hr = ((heap_avail_fn)ddraw2->vtbl[DD2_GET_AVAILABLE_VID_MEM])(
        ddraw2, &caps, &total, &free_bytes);
    heap_key(section, "AvailHr", (DWORD)hr);
    heap_key(section, "AvailTotal", total);
    heap_key(section, "AvailFree", free_bytes);
}

/* Create surfaces of one kind until refused; count where they landed. */
static void heap_fill(HEAP_OBJECT *ddraw, HEAP_OBJECT *ddraw2,
                      const char *section, DWORD caps, DWORD z_depth)
{
    HEAP_SURFACE_DESC desc;
    DWORD count = 0ul;
    DWORD in_heap1 = 0ul;
    DWORD lowest = 0xfffffffful;
    DWORD highest = 0ul;
    DWORD unlocked = 0ul;
    HRESULT hr = 0;
    DWORD index;

    heap_available(ddraw2, section, caps);
    while (count < HEAP_MAX) {
        HEAP_OBJECT *surface = 0;

        heap_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
        desc.dwWidth = HEAP_EDGE;
        desc.dwHeight = HEAP_EDGE;
        desc.ddsCaps = caps | DDSCAPS_VIDEOMEMORY;
        if (z_depth != 0ul) {
            desc.dwFlags |= DDSD_ZBUFFERBITDEPTH;
            desc.dwMipMapCount = z_depth;
        }
        hr = ((heap_create_surface_fn)ddraw->vtbl[DD_CREATE_SURFACE])(
            ddraw, &desc, &surface, 0);
        if (hr != 0 || surface == 0) {
            break;
        }
        heap_surfaces[count++] = surface;

        heap_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        if (((heap_lock_fn)surface->vtbl[DDS_LOCK])(
                surface, 0, &desc, DDLOCK_WAIT, 0) != 0 ||
            desc.lpSurface == 0) {
            ++unlocked;
            continue;
        }
        {
            DWORD address = (DWORD)desc.lpSurface;

            ((heap_unlock_fn)surface->vtbl[DDS_UNLOCK])(surface, 0);
            if (address < lowest) {
                lowest = address;
            }
            if (address > highest) {
                highest = address;
            }
            if (address >= heap1_start && address <= heap1_end) {
                ++in_heap1;
            }
        }
    }
    heap_key(section, "Created", count);
    heap_key(section, "StopHr", (DWORD)hr);
    heap_key(section, "InHeap1", in_heap1);
    heap_key(section, "Unlocked", unlocked);
    heap_key(section, "Lowest", lowest);
    heap_key(section, "Highest", highest);
    for (index = count; index > 0ul; --index) {
        ((heap_release_fn)heap_surfaces[index - 1ul]->vtbl[DD_RELEASE])(
            heap_surfaces[index - 1ul]);
    }
}

void WINAPI V9xHeapProbeEntry(void)
{
    HMODULE ddraw_module;
    heap_create_fn create;
    HEAP_OBJECT *ddraw = 0;
    HEAP_OBJECT *ddraw2 = 0;
    HRESULT hr;

    CreateDirectoryA("C:\\V9XDIAG", 0);
    DeleteFileA(HEAP_INI);
    WritePrivateProfileStringA("Report", "Build", V9X_BUILD_ID, HEAP_INI);

    ddraw_module = LoadLibraryA("DDRAW.DLL");
    create = ddraw_module != 0
        ? (heap_create_fn)GetProcAddress(ddraw_module, "DirectDrawCreate") : 0;
    if (create == 0 || create(0, &ddraw, 0) != 0 || ddraw == 0) {
        WritePrivateProfileStringA("Report", "Result", "NO-DDRAW", HEAP_INI);
        ExitProcess(2u);
    }
    /* After DirectDrawCreate: the DRV publishes the split when the driver
     * object is made, not at boot. */
    heap1_start = heap_read_hex("Heap1Start");
    heap1_end = heap_read_hex("Heap1End");
    heap_key("Report", "Split", heap_read_hex("Split"));
    heap_key("Report", "Heap1Start", heap1_start);
    heap_key("Report", "Heap1End", heap1_end);
    hr = ((heap_coop_fn)ddraw->vtbl[DD_SET_COOPERATIVE_LEVEL])(
        ddraw, GetDesktopWindow(), DDSCL_NORMAL);
    heap_key("Report", "CooperativeHr", (DWORD)hr);
    if (((heap_query_fn)ddraw->vtbl[DD_QUERY_INTERFACE])(
            ddraw, &heap_iid_ddraw2, &ddraw2) != 0) {
        ddraw2 = 0;
    }

    heap_fill(ddraw, ddraw2, "Offscreen", DDSCAPS_OFFSCREENPLAIN, 0ul);
    heap_fill(ddraw, ddraw2, "ZBuffer", DDSCAPS_ZBUFFER, 16ul);
    heap_fill(ddraw, ddraw2, "Texture", DDSCAPS_TEXTURE, 0ul);
    /* Off-screen again after the textures: a heap 1 the runtime marked
     * used must still refuse them. */
    heap_fill(ddraw, ddraw2, "Offscreen2", DDSCAPS_OFFSCREENPLAIN, 0ul);

    if (ddraw2 != 0) {
        ((heap_release_fn)ddraw2->vtbl[DD_RELEASE])(ddraw2);
    }
    ((heap_release_fn)ddraw->vtbl[DD_RELEASE])(ddraw);
    WritePrivateProfileStringA("Report", "Result", "COMPLETE", HEAP_INI);
    WritePrivateProfileStringA(0, 0, 0, HEAP_INI);
    ExitProcess(0u);
}
