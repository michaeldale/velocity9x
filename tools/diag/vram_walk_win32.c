/*
 * Installed video memory, measured rather than decoded.
 *
 * A chip's memory-size register is only as good as the table that decodes it,
 * and on the S3D parts that table differs per chip (memory.c). This tool asks
 * the memory itself: through a lock of the DirectDraw primary, which hands back
 * the driver's own linear mapping of VRAM offset 0, it writes a distinct
 * signature every 512 KiB from 0 to 8 MiB and reads them all back.
 *
 * The signatures are written highest offset first. On a card whose VRAM
 * decode wraps at S bytes, every offset at or above S aliases one below it,
 * and the lower write lands last - so exactly the points below S keep their
 * own signature. The length of that leading run is the installed size. Memory
 * that floats instead of wrapping ends the run the same way.
 *
 * The originals are read before any write and put back afterwards, highest
 * offset first, so an aliased pair ends holding the lower one's original,
 * which is also the higher one's. Everything between Lock and Unlock touches
 * VRAM and nothing else: the lock holds the Win16 mutex, so the INI is written
 * after Unlock. Writes are fenced with a locked exchange because the driver
 * may have made the low aperture write-combining (enable16.c's MTRR policy).
 *
 * Writes C:\V9XDIAG\V9XVRAM.INI.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_VRAM_PATH     V9X_DIAG_VRAM_INI
#define V9X_VRAM_SECTION  "Velocity9xVram"
#define V9X_VRAM_STEP     0x00080000ul /* 512 KiB */
#define V9X_VRAM_POINTS   16u          /* 0 .. 8 MiB - 512 KiB */
/* Inside each step rather than at it, so the point at offset 0 is a pixel
 * on the first visible row and not the first byte any allocator hands out. */
#define V9X_VRAM_INNER    0x00000200ul
#define V9X_VRAM_SIGNATURE 0x5A3C0000ul

#define V9X_DDSD_CAPS              0x00000001ul
#define V9X_DDSCAPS_PRIMARYSURFACE 0x00000200ul
#define V9X_DDSCL_NORMAL           0x00000008ul
#define V9X_DDLOCK_WAIT            0x00000001ul

typedef struct v9x_ddcolorkey {
    DWORD low;
    DWORD high;
} V9X_DDCOLORKEY;

typedef struct v9x_ddpixelformat {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    DWORD dwRGBBitCount;
    DWORD dwRBitMask;
    DWORD dwGBitMask;
    DWORD dwBBitMask;
    DWORD dwRGBAlphaBitMask;
} V9X_DDPIXELFORMAT;

typedef struct v9x_ddsurfacedesc {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    LONG lPitch;
    DWORD dwBackBufferCount;
    DWORD dwMipMapCount;
    DWORD dwAlphaBitDepth;
    DWORD dwReserved;
    LPVOID lpSurface;
    V9X_DDCOLORKEY ddckCKDestOverlay;
    V9X_DDCOLORKEY ddckCKDestBlt;
    V9X_DDCOLORKEY ddckCKSrcOverlay;
    V9X_DDCOLORKEY ddckCKSrcBlt;
    V9X_DDPIXELFORMAT ddpfPixelFormat;
    DWORD dwCaps;
} V9X_DDSURFACEDESC;

struct v9x_dd;
struct v9x_dds;

/* IDirectDraw version 1, in vtable order; only the used slots are typed. */
typedef struct v9x_dd_vtbl {
    void *QueryInterface;
    void *AddRef;
    ULONG (__stdcall *Release)(struct v9x_dd *);
    void *Compact;
    void *CreateClipper;
    void *CreatePalette;
    HRESULT (__stdcall *CreateSurface)(struct v9x_dd *, V9X_DDSURFACEDESC *,
                                       struct v9x_dds **, void *);
    void *DuplicateSurface;
    void *EnumDisplayModes;
    void *EnumSurfaces;
    void *FlipToGDISurface;
    void *GetCaps;
    void *GetDisplayMode;
    void *GetFourCCCodes;
    void *GetGDISurface;
    void *GetMonitorFrequency;
    void *GetScanLine;
    void *GetVerticalBlankStatus;
    void *Initialize;
    void *RestoreDisplayMode;
    HRESULT (__stdcall *SetCooperativeLevel)(struct v9x_dd *, HWND, DWORD);
} V9X_DD_VTBL;

/* IDirectDrawSurface version 1, in vtable order; only the used slots. */
typedef struct v9x_dds_vtbl {
    void *QueryInterface;
    void *AddRef;
    ULONG (__stdcall *Release)(struct v9x_dds *);
    void *AddAttachedSurface;
    void *AddOverlayDirtyRect;
    void *Blt;
    void *BltBatch;
    void *BltFast;
    void *DeleteAttachedSurface;
    void *EnumAttachedSurfaces;
    void *EnumOverlayZOrders;
    void *Flip;
    void *GetAttachedSurface;
    void *GetBltStatus;
    void *GetCaps;
    void *GetClipper;
    void *GetColorKey;
    void *GetDC;
    void *GetFlipStatus;
    void *GetOverlayPosition;
    void *GetPalette;
    void *GetPixelFormat;
    void *GetSurfaceDesc;
    void *Initialize;
    void *IsLost;
    HRESULT (__stdcall *Lock)(struct v9x_dds *, RECT *, V9X_DDSURFACEDESC *,
                              DWORD, HANDLE);
    void *ReleaseDC;
    void *Restore;
    void *SetClipper;
    void *SetColorKey;
    void *SetOverlayPosition;
    void *SetPalette;
    HRESULT (__stdcall *Unlock)(struct v9x_dds *, void *);
} V9X_DDS_VTBL;

struct v9x_dd {
    const V9X_DD_VTBL *vtbl;
};

struct v9x_dds {
    const V9X_DDS_VTBL *vtbl;
};

typedef HRESULT (__stdcall *V9X_DDCREATE)(void *, struct v9x_dd **, void *);

static DWORD v9x_original[V9X_VRAM_POINTS];
static DWORD v9x_readback[V9X_VRAM_POINTS];

/* No C runtime to lend memset, and ZeroMemory is a macro over it here. */
static void v9x_clear(void *target, unsigned int bytes)
{
    unsigned char *cursor = (unsigned char *)target;

    while (bytes != 0u) {
        *cursor++ = 0u;
        --bytes;
    }
}

static void v9x_write_hex(const char *key, DWORD value)
{
    char text[16];

    wsprintfA(text, "0x%08lX", value);
    WritePrivateProfileStringA(V9X_VRAM_SECTION, key, text, V9X_VRAM_PATH);
}

static void v9x_write_uint(const char *key, DWORD value)
{
    char text[16];

    wsprintfA(text, "%lu", value);
    WritePrivateProfileStringA(V9X_VRAM_SECTION, key, text, V9X_VRAM_PATH);
}

static void v9x_write_text(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_VRAM_SECTION, key, value, V9X_VRAM_PATH);
}

/* A locked exchange drains the write-combining buffers; a plain store
 * sequence followed by a read is not guaranteed to on WC memory. */
static void v9x_fence(void)
{
    LONG scratch = 0;

    InterlockedExchange(&scratch, 1);
}

static void v9x_walk(volatile DWORD *base)
{
    unsigned int index;

    for (index = 0u; index < V9X_VRAM_POINTS; ++index) {
        v9x_original[index] =
            base[(index * V9X_VRAM_STEP + V9X_VRAM_INNER) / 4ul];
    }

    /* Highest first: see the header for why the order is the measurement. */
    index = V9X_VRAM_POINTS;
    while (index != 0u) {
        --index;
        base[(index * V9X_VRAM_STEP + V9X_VRAM_INNER) / 4ul] =
            V9X_VRAM_SIGNATURE | (DWORD)index;
    }
    v9x_fence();

    for (index = 0u; index < V9X_VRAM_POINTS; ++index) {
        v9x_readback[index] =
            base[(index * V9X_VRAM_STEP + V9X_VRAM_INNER) / 4ul];
    }

    index = V9X_VRAM_POINTS;
    while (index != 0u) {
        --index;
        base[(index * V9X_VRAM_STEP + V9X_VRAM_INNER) / 4ul] =
            v9x_original[index];
    }
    v9x_fence();
}

static DWORD v9x_report(HRESULT hr, const char *stage)
{
    v9x_write_text("Result", "FAIL");
    v9x_write_text("Stage", stage);
    v9x_write_hex("Hr", (DWORD)hr);
    return 1ul;
}

static DWORD v9x_run(void)
{
    HMODULE ddraw_module;
    V9X_DDCREATE create;
    struct v9x_dd *dd = 0;
    struct v9x_dds *primary = 0;
    V9X_DDSURFACEDESC desc;
    HRESULT hr;
    unsigned int index;
    DWORD held = 0ul;
    char key[24];

    ddraw_module = LoadLibraryA("DDRAW.DLL");
    if (ddraw_module == 0) {
        return v9x_report(0, "load-ddraw");
    }
    create = (V9X_DDCREATE)GetProcAddress(ddraw_module, "DirectDrawCreate");
    if (create == 0) {
        return v9x_report(0, "find-create");
    }
    hr = create(0, &dd, 0);
    if (hr != 0) {
        return v9x_report(hr, "create");
    }
    hr = dd->vtbl->SetCooperativeLevel(dd, 0, V9X_DDSCL_NORMAL);
    if (hr != 0) {
        dd->vtbl->Release(dd);
        return v9x_report(hr, "cooperative-level");
    }

    v9x_clear(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = V9X_DDSD_CAPS;
    desc.dwCaps = V9X_DDSCAPS_PRIMARYSURFACE;
    hr = dd->vtbl->CreateSurface(dd, &desc, &primary, 0);
    if (hr != 0) {
        dd->vtbl->Release(dd);
        return v9x_report(hr, "create-primary");
    }

    v9x_clear(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = primary->vtbl->Lock(primary, 0, &desc, V9X_DDLOCK_WAIT, 0);
    if (hr != 0) {
        primary->vtbl->Release(primary);
        dd->vtbl->Release(dd);
        return v9x_report(hr, "lock");
    }
    v9x_walk((volatile DWORD *)desc.lpSurface);
    primary->vtbl->Unlock(primary, desc.lpSurface);
    primary->vtbl->Release(primary);
    dd->vtbl->Release(dd);

    v9x_write_hex("Base", (DWORD)desc.lpSurface);
    v9x_write_uint("Width", desc.dwWidth);
    v9x_write_uint("Height", desc.dwHeight);
    v9x_write_uint("Pitch", (DWORD)desc.lPitch);
    for (index = 0u; index < V9X_VRAM_POINTS; ++index) {
        wsprintfA(key, "Point%02u", index);
        v9x_write_hex(key, v9x_readback[index]);
    }
    while (held < V9X_VRAM_POINTS &&
           v9x_readback[held] == (V9X_VRAM_SIGNATURE | (DWORD)held)) {
        ++held;
    }
    v9x_write_uint("LeadingPointsHeld", held);
    v9x_write_uint("MeasuredBytes", held * V9X_VRAM_STEP);
    v9x_write_text("Result", held == V9X_VRAM_POINTS ? "AT-LEAST-8MIB" : "MEASURED");
    return 0ul;
}

void __stdcall V9xVramWalkEntry(void)
{
    CreateDirectoryA(V9X_DIAG_DIR, 0);
    WritePrivateProfileStringA(V9X_VRAM_SECTION, 0, 0, V9X_VRAM_PATH);
    v9x_write_text("Build", V9X_BUILD_ID);
    ExitProcess(v9x_run());
}
