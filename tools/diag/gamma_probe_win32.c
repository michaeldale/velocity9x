/*
 * V9XGAMMA.EXE: what a gamma ramp asks of the display driver.
 *
 * Writes C:\V9XDIAG\V9XGAMMA.INI: GDI's COLORMGMTCAPS (CM_GAMMA_RAMP is
 * C1_GAMMA_RAMP from the driver's GDIINFO), DirectDraw's
 * DDCAPS2_PRIMARYGAMMA, then GetDeviceGammaRamp, a brightened
 * SetDeviceGammaRamp held for a few seconds, the read-back, and the
 * original ramp restored. What the driver received is in its own
 * C:\V9XDIAG\V9XESC.INI (dd16.c). /keep leaves the bright ramp loaded, so
 * the DAC can be read while it holds; /identity loads the identity ramp.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_GAMMA_PATH       V9X_DIAG_PATH("V9XGAMMA.INI")
#define V9X_SECTION          "Velocity9xGamma"
#define V9X_COLORMGMTCAPS    121
#define V9X_CM_GAMMA_RAMP    0x00000002ul
#define V9X_DDCAPS2_PRIMARYGAMMA 0x00020000ul
/* DDCAPS as DirectX 3 sized it; every later DirectDraw accepts it. */
#define V9X_DDCAPS_DX3_BYTES 316ul
#define V9X_DDRAW_GETCAPS    11u
#define V9X_DDRAW_RELEASE    2u
#define V9X_HOLD_MS          3000ul

typedef HRESULT (WINAPI *v9x_ddcreate_fn)(void *, void **, void *);
typedef HRESULT (__stdcall *v9x_getcaps_fn)(void *, void *, void *);
typedef ULONG (__stdcall *v9x_release_fn)(void *);

static WORD v9x_ramp_before[3][256];
static WORD v9x_ramp_set[3][256];
static WORD v9x_ramp_after[3][256];
static DWORD v9x_caps[100];

static void v9x_hex(char *text, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    int index;

    text[0] = '0';
    text[1] = 'x';
    for (index = 0; index < 8; ++index) {
        text[2 + index] = digits[(value >> ((7 - index) * 4)) & 15ul];
    }
    text[10] = '\0';
}

static void v9x_key_hex(const char *key, DWORD value)
{
    char text[12];

    v9x_hex(text, value);
    WritePrivateProfileStringA(V9X_SECTION, key, text, V9X_GAMMA_PATH);
}

/* Entries 0, 64, 128 and 255 of each channel, as one key. */
static void v9x_key_ramp(const char *key, WORD ramp[3][256])
{
    static const int sample[4] = { 0, 64, 128, 255 };
    char line[160];
    char *at = line;
    int channel;
    int index;
    char text[12];

    for (channel = 0; channel < 3; ++channel) {
        for (index = 0; index < 4; ++index) {
            v9x_hex(text, ramp[channel][sample[index]]);
            lstrcpyA(at, text + 6);
            at += 4;
            *at++ = ' ';
        }
        *at++ = '|';
        *at++ = ' ';
    }
    *at = '\0';
    WritePrivateProfileStringA(V9X_SECTION, key, line, V9X_GAMMA_PATH);
}

static DWORD v9x_directdraw_caps2(void)
{
    HMODULE module = LoadLibraryA("DDRAW.DLL");
    v9x_ddcreate_fn create;
    void *ddraw = 0;
    void **vtbl;
    DWORD caps2 = 0xfffffffful;

    if (module == 0) {
        return caps2;
    }
    create = (v9x_ddcreate_fn)GetProcAddress(module, "DirectDrawCreate");
    if (create != 0 && create(0, &ddraw, 0) == 0 && ddraw != 0) {
        vtbl = *(void ***)ddraw;
        v9x_caps[0] = V9X_DDCAPS_DX3_BYTES;
        if (((v9x_getcaps_fn)vtbl[V9X_DDRAW_GETCAPS])(ddraw, v9x_caps, 0) ==
            0) {
            caps2 = v9x_caps[2];
        }
        ((v9x_release_fn)vtbl[V9X_DDRAW_RELEASE])(ddraw);
    }
    FreeLibrary(module);
    return caps2;
}

void __stdcall V9xGammaProbeEntry(void)
{
    HDC screen;
    DWORD caps2;
    DWORD value;
    BOOL got;
    BOOL set;
    BOOL got_after;
    BOOL restored;
    int channel;
    int index;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    WritePrivateProfileStringA(V9X_SECTION, 0, 0, V9X_GAMMA_PATH);
    WritePrivateProfileStringA(V9X_SECTION, "Build", V9X_BUILD_ID,
                               V9X_GAMMA_PATH);
    WritePrivateProfileStringA(V9X_SECTION, "Result", "INCOMPLETE",
                               V9X_GAMMA_PATH);

    if (lstrlenA(GetCommandLineA()) >= 9 &&
        lstrcmpiA(GetCommandLineA() + lstrlenA(GetCommandLineA()) - 9,
                  "/identity") == 0) {
        screen = GetDC(0);
        for (channel = 0; channel < 3; ++channel) {
            for (index = 0; index < 256; ++index) {
                v9x_ramp_set[channel][index] = (WORD)(index << 8);
            }
        }
        set = screen != 0 && SetDeviceGammaRamp(screen, v9x_ramp_set);
        v9x_key_hex("SetIdentity", (DWORD)set);
        if (screen != 0) {
            ReleaseDC(0, screen);
        }
        WritePrivateProfileStringA(V9X_SECTION, "Result", "IDENTITY",
                                   V9X_GAMMA_PATH);
        ExitProcess(0u);
    }
    caps2 = v9x_directdraw_caps2();
    v9x_key_hex("DdCaps2", caps2);
    v9x_key_hex("DdPrimaryGamma",
                caps2 != 0xfffffffful &&
                (caps2 & V9X_DDCAPS2_PRIMARYGAMMA) != 0ul ? 1ul : 0ul);

    screen = GetDC(0);
    if (screen == 0) {
        WritePrivateProfileStringA(V9X_SECTION, "Result", "NO-DC",
                                   V9X_GAMMA_PATH);
        ExitProcess(1u);
    }
    value = (DWORD)GetDeviceCaps(screen, V9X_COLORMGMTCAPS);
    v9x_key_hex("ColorMgmtCaps", value);
    v9x_key_hex("GdiGammaRamp",
                (value & V9X_CM_GAMMA_RAMP) != 0ul ? 1ul : 0ul);

    got = GetDeviceGammaRamp(screen, v9x_ramp_before);
    v9x_key_hex("GetBefore", (DWORD)got);
    v9x_key_ramp("RampBefore", v9x_ramp_before);

    /* 1.5x brighter, clamped: visible on the monitor while held. */
    for (channel = 0; channel < 3; ++channel) {
        for (index = 0; index < 256; ++index) {
            value = (DWORD)index * 384ul;
            v9x_ramp_set[channel][index] =
                (WORD)(value > 65535ul ? 65535ul : value);
        }
    }
    set = SetDeviceGammaRamp(screen, v9x_ramp_set);
    v9x_key_hex("SetBright", (DWORD)set);
    v9x_key_hex("SetError", GetLastError());
    Sleep(V9X_HOLD_MS);
    got_after = GetDeviceGammaRamp(screen, v9x_ramp_after);
    v9x_key_hex("GetAfter", (DWORD)got_after);
    v9x_key_ramp("RampAfter", v9x_ramp_after);

    if (lstrcmpiA(GetCommandLineA() + lstrlenA(GetCommandLineA()) - 5,
                  "/keep") == 0) {
        restored = FALSE;
    } else {
        restored = got ? SetDeviceGammaRamp(screen, v9x_ramp_before) : FALSE;
    }
    v9x_key_hex("Restored", (DWORD)restored);
    ReleaseDC(0, screen);

    WritePrivateProfileStringA(V9X_SECTION, "Result", "COMPLETE",
                               V9X_GAMMA_PATH);
    WritePrivateProfileStringA(0, 0, 0, V9X_GAMMA_PATH);
    ExitProcess(0u);
}
