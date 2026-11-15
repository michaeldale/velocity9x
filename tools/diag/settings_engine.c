/*
 * V9xEngineStart: have the HAL build the GT218's engine channel at logon, so
 * GDI's copies run on the engine from the start of the session rather than
 * from the first DirectDraw program.
 *
 * GDI draws on the NV50 engine only once the HAL's channel exists
 * (gdi_accel.c, option A of docs\plans\nvidia-gt218-gdi-engine.md), and the
 * HAL builds it lazily, on its first engine operation (eng_nv50.c,
 * v9x_nv50_validate). Until something uses DirectDraw, screen-to-screen
 * copies are CPU reads through BAR1 at about 2,000 cycles a pixel; with the
 * channel up they cost about 6
 * (docs\decisions\2026-11-15-gt218-write-combining.md). Michael chose this
 * route, option B, over a bring-up in the 16-bit driver (2026-11-15).
 *
 * The INF runs it at every logon, through Run beside V9xSyncModes:
 *
 *     rundll32.exe v9xsetp.dll,V9xEngineStart
 *
 * It asks the display driver first (V9X_GDIGETSTATS) and does nothing unless
 * the engine is the NV50 and GDI has its copy primitive enabled, so on any
 * other chip, or with GDI acceleration off or poisoned, DirectDraw is never
 * loaded. Otherwise it creates a DirectDraw object at the normal cooperative
 * level, colour-fills one small offscreen surface in video memory - an
 * engine operation, so the HAL brings the channel up with the code that has
 * run on the card - and releases everything. The channel outlives the
 * process (measured, V9XN5CH /check, 2026-11-14). Nothing is shown: the
 * outcome goes to C:\V9XDIAG\V9XENG.INI, because boot-time UI holds Windows
 * at its first boot after an install.
 *
 * DDRAW.DLL is loaded at run time, not imported, so the property sheet this
 * DLL also serves never loads DirectDraw, and the import audit in
 * build-settings-page.ps1 keeps its list of system DLLs.
 */

#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/engine_abi.h"
#include "velocity9x/win9x_ddraw_abi.h"

/* DirectDraw constants this file uses (DDRAW.H values). */
#define V9X_ENG_DDSCL_NORMAL             0x00000008ul
#define V9X_ENG_DDSD_CAPS                0x00000001ul
#define V9X_ENG_DDSD_HEIGHT              0x00000002ul
#define V9X_ENG_DDSD_WIDTH               0x00000004ul
#define V9X_ENG_DDSCAPS_OFFSCREENPLAIN   0x00000040ul
#define V9X_ENG_DDSCAPS_VIDEOMEMORY      0x00004000ul
#define V9X_ENG_DDBLT_COLORFILL          0x00000400ul
#define V9X_ENG_DDBLT_WAIT               0x01000000ul

/* Big enough that the HAL hands the fill to the engine, small enough to fit
 * any heap: 64x64 at 4 bytes a pixel is 16 KiB. */
#define V9X_ENG_SURFACE_SIDE             64ul

/*
 * The DirectDraw version 1 layouts this file touches, in DDRAW.H order.
 * ddraw_probe_win32.c declares the full set privately; only the slots called
 * here are typed, the rest are placeholders that keep the vtable offsets.
 */
typedef struct v9x_eng_colorkey {
    DWORD low;
    DWORD high;
} V9X_ENG_COLORKEY;

typedef struct v9x_eng_pixelformat {
    DWORD fields[8];
} V9X_ENG_PIXELFORMAT;

typedef struct v9x_eng_surfacedesc {
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
    V9X_ENG_COLORKEY keys[4];
    V9X_ENG_PIXELFORMAT ddpfPixelFormat;
    DWORD dwCaps;
} V9X_ENG_SURFACEDESC;

/* DDBLTFX: dwSize, nineteen fields this file does not set, dwFillColor,
 * then two colour keys. */
typedef struct v9x_eng_bltfx {
    DWORD dwSize;
    DWORD unused[19];
    DWORD dwFillColor;
    V9X_ENG_COLORKEY keys[2];
} V9X_ENG_BLTFX;

struct v9x_eng_dd;
struct v9x_eng_dds;

typedef struct v9x_eng_dd_vtbl {
    void *QueryInterface;
    void *AddRef;
    ULONG (__stdcall *Release)(struct v9x_eng_dd *);
    void *Compact;
    void *CreateClipper;
    void *CreatePalette;
    HRESULT (__stdcall *CreateSurface)(struct v9x_eng_dd *,
                                       V9X_ENG_SURFACEDESC *,
                                       struct v9x_eng_dds **, void *);
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
    HRESULT (__stdcall *SetCooperativeLevel)(struct v9x_eng_dd *, HWND,
                                             DWORD);
} V9X_ENG_DD_VTBL;

typedef struct v9x_eng_dd {
    V9X_ENG_DD_VTBL *vtbl;
} V9X_ENG_DD;

typedef struct v9x_eng_dds_vtbl {
    void *QueryInterface;
    void *AddRef;
    ULONG (__stdcall *Release)(struct v9x_eng_dds *);
    void *AddAttachedSurface;
    void *AddOverlayDirtyRect;
    HRESULT (__stdcall *Blt)(struct v9x_eng_dds *, RECT *,
                             struct v9x_eng_dds *, RECT *, DWORD, void *);
} V9X_ENG_DDS_VTBL;

typedef struct v9x_eng_dds {
    V9X_ENG_DDS_VTBL *vtbl;
} V9X_ENG_DDS;

typedef HRESULT (WINAPI *V9X_ENG_CREATE)(GUID *, V9X_ENG_DD **, void *);

static void v9x_eng_report(const char *result, HRESULT hr)
{
    static const char digits[] = "0123456789ABCDEF";
    char text[11];
    DWORD value = (DWORD)hr;
    int index;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    WritePrivateProfileStringA("Velocity9xEngine", 0, 0,
                               V9X_DIAG_ENGINE_INI);
    WritePrivateProfileStringA("Velocity9xEngine", "Result", result,
                               V9X_DIAG_ENGINE_INI);
    text[0] = '0';
    text[1] = 'x';
    for (index = 0; index < 8; ++index) {
        text[2 + index] = digits[(value >> (28 - 4 * index)) & 15ul];
    }
    text[10] = '\0';
    WritePrivateProfileStringA("Velocity9xEngine", "Hr", text,
                               V9X_DIAG_ENGINE_INI);
    WritePrivateProfileStringA("Velocity9xEngine", "Build", V9X_BUILD_ID,
                               V9X_DIAG_ENGINE_INI);
    /* Flush the profile cache, so a reader sees the file at once. */
    WritePrivateProfileStringA(0, 0, 0, V9X_DIAG_ENGINE_INI);
}

/* The display driver's GDI counters, or 0 when it does not answer. */
static int v9x_eng_read_stats(V9X_GDI_STATS *stats)
{
    V9X_DCICMD command;
    unsigned char *bytes = (unsigned char *)stats;
    unsigned index;
    HDC screen;
    int answered;

    for (index = 0u; index < sizeof(V9X_GDI_STATS); ++index) {
        bytes[index] = 0u;
    }
    screen = GetDC(0);
    if (screen == 0) {
        return 0;
    }
    command.dwCommand = V9X_GDIGETSTATS;
    command.dwParam1 = 0ul;
    command.dwParam2 = 0ul;
    command.dwVersion = V9X_DD_VERSION;
    command.dwReserved = 0ul;
    answered = ExtEscape(screen, V9X_DCICOMMAND, sizeof(command),
                         (LPCSTR)&command, sizeof(V9X_GDI_STATS),
                         (LPSTR)stats) > 0 &&
               stats->dwSize == sizeof(V9X_GDI_STATS);
    ReleaseDC(0, screen);
    return answered;
}

/* One small video-memory colour fill through DirectDraw. */
static void v9x_eng_fill_once(V9X_ENG_CREATE create)
{
    V9X_ENG_DD *dd = 0;
    V9X_ENG_DDS *surface = 0;
    V9X_ENG_SURFACEDESC desc;
    V9X_ENG_BLTFX fx;
    unsigned char *bytes;
    unsigned index;
    HRESULT hr;

    hr = create(0, &dd, 0);
    if (hr != 0 || dd == 0) {
        v9x_eng_report("create-failed", hr);
        return;
    }
    hr = dd->vtbl->SetCooperativeLevel(dd, 0, V9X_ENG_DDSCL_NORMAL);
    if (hr != 0) {
        v9x_eng_report("cooperative-level-failed", hr);
        dd->vtbl->Release(dd);
        return;
    }

    bytes = (unsigned char *)&desc;
    for (index = 0u; index < sizeof(desc); ++index) {
        bytes[index] = 0u;
    }
    desc.dwSize = sizeof(desc);
    desc.dwFlags = V9X_ENG_DDSD_CAPS | V9X_ENG_DDSD_HEIGHT |
                   V9X_ENG_DDSD_WIDTH;
    desc.dwWidth = V9X_ENG_SURFACE_SIDE;
    desc.dwHeight = V9X_ENG_SURFACE_SIDE;
    desc.dwCaps = V9X_ENG_DDSCAPS_OFFSCREENPLAIN |
                  V9X_ENG_DDSCAPS_VIDEOMEMORY;
    hr = dd->vtbl->CreateSurface(dd, &desc, &surface, 0);
    if (hr != 0 || surface == 0) {
        v9x_eng_report("surface-failed", hr);
        dd->vtbl->Release(dd);
        return;
    }

    bytes = (unsigned char *)&fx;
    for (index = 0u; index < sizeof(fx); ++index) {
        bytes[index] = 0u;
    }
    fx.dwSize = sizeof(fx);
    hr = surface->vtbl->Blt(surface, 0, 0, 0,
                            V9X_ENG_DDBLT_COLORFILL | V9X_ENG_DDBLT_WAIT,
                            &fx);
    surface->vtbl->Release(surface);
    dd->vtbl->Release(dd);
    v9x_eng_report(hr == 0 ? "filled" : "fill-failed", hr);
}

void CALLBACK V9xEngineStart(HWND owner, HINSTANCE instance, LPSTR command,
                             int show)
{
    V9X_GDI_STATS stats;
    HMODULE ddraw;
    V9X_ENG_CREATE create;

    (void)owner;
    (void)instance;
    (void)command;
    (void)show;

    if (!v9x_eng_read_stats(&stats)) {
        v9x_eng_report("skipped-no-driver-answer", 0);
        return;
    }
    if (stats.engine_type != V9X_DD_ENGINE_TYPE_NV50) {
        v9x_eng_report("skipped-not-nv50", 0);
        return;
    }
    if ((stats.enabled & V9X_GDI_PRIM_COPY) == 0ul || stats.poisoned != 0ul) {
        v9x_eng_report("skipped-gdi-copy-off", 0);
        return;
    }

    ddraw = LoadLibraryA("DDRAW.DLL");
    if (ddraw == 0) {
        v9x_eng_report("no-ddraw", 0);
        return;
    }
    create = (V9X_ENG_CREATE)GetProcAddress(ddraw, "DirectDrawCreate");
    if (create == 0) {
        v9x_eng_report("no-directdrawcreate", 0);
    } else {
        v9x_eng_fill_once(create);
    }
    FreeLibrary(ddraw);
}
