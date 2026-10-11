/*
 * V9XWATP.EXE: drives the HAL's process watch (src\display32\ddhal_watch.c)
 * through each way a program can end, so V9XTRACE.EXE's Watch* keys can be
 * checked against what the program actually did.
 *
 * Usage: V9XWATP <mode>, one of
 *   exit       ExitProcess(0x1234)
 *   terminate  TerminateProcess(GetCurrentProcess(), 0x2345)
 *   message    MessageBoxA, dismissed by a second thread after a second,
 *              then ExitProcess(0x3456)
 *   fault      SetUnhandledExceptionFilter(own), then a write to address 0;
 *              the probe's filter records that it ran and ends the process
 *   unload     release DirectDraw, FreeLibrary DDRAW.DLL, ExitProcess(0x5678)
 *
 * Each opens DirectDraw and creates a primary surface first; that
 * CreateSurface is where the HAL starts watching this process.
 * The probe writes C:\V9XDIAG\V9XWATP.INI: the mode, the addresses of its own
 * call sites' module, and whether its own fault filter ran (the HAL must
 * chain to it, not replace it).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_WATP_PATH    "C:\\V9XDIAG\\V9XWATP.INI"
#define V9X_WATP_SECTION "Velocity9xWatchProbe"
#define V9X_WATP_CAPTION "V9XWATP"
#define V9X_WATP_DISMISS_MS 1000ul

typedef HRESULT (WINAPI *V9X_WATP_CREATE)(GUID *guid, LPDIRECTDRAW *dd,
                                          IUnknown *outer);

static void v9x_watp_text(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_WATP_SECTION, key, value, V9X_WATP_PATH);
}

static void v9x_watp_hex(const char *key, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char text[11];
    int index;

    text[0] = '0';
    text[1] = 'x';
    for (index = 0; index < 8; ++index) {
        text[2 + index] = digits[(value >> ((7 - index) * 4)) & 0xful];
    }
    text[10] = '\0';
    v9x_watp_text(key, text);
}

/* Equal when the command line's first argument is mode. */
static int v9x_watp_is(const char *argument, const char *mode)
{
    while (*mode != '\0') {
        if ((*argument | 0x20) != *mode) {
            return 0;
        }
        ++argument;
        ++mode;
    }
    return *argument == '\0' || *argument == ' ';
}

/* The first argument: past the program name, quoted or not. */
static const char *v9x_watp_argument(void)
{
    const char *line = GetCommandLineA();

    if (*line == '"') {
        ++line;
        while (*line != '\0' && *line != '"') {
            ++line;
        }
        if (*line == '"') {
            ++line;
        }
    } else {
        while (*line != '\0' && *line != ' ') {
            ++line;
        }
    }
    while (*line == ' ') {
        ++line;
    }
    return line;
}

static LONG WINAPI v9x_watp_filter(struct _EXCEPTION_POINTERS *exception)
{
    v9x_watp_text("ProbeFilter", "ran");
    if (exception != 0 && exception->ExceptionRecord != 0) {
        v9x_watp_hex("ProbeFaultCode",
                     exception->ExceptionRecord->ExceptionCode);
        v9x_watp_hex("ProbeFaultAddress",
                     (DWORD)exception->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

/* Closes the probe's own message box, so the run needs no hand. */
static DWORD WINAPI v9x_watp_dismiss(LPVOID unused)
{
    HWND box;
    DWORD waited;

    (void)unused;
    for (waited = 0ul; waited < 10ul * V9X_WATP_DISMISS_MS; waited += 100ul) {
        Sleep(100ul);
        box = FindWindowA(0, V9X_WATP_CAPTION);
        if (box != 0) {
            Sleep(V9X_WATP_DISMISS_MS);
            PostMessageA(box, WM_COMMAND, IDOK, 0);
            return 0ul;
        }
    }
    return 0ul;
}

void __stdcall V9xWatchProbeEntry(void)
{
    const char *mode = v9x_watp_argument();
    HMODULE ddraw;
    V9X_WATP_CREATE create;
    LPDIRECTDRAW dd = 0;
    LPDIRECTDRAWSURFACE primary = 0;
    DWORD thread;

    CreateDirectoryA("C:\\V9XDIAG", 0);
    DeleteFileA(V9X_WATP_PATH);
    v9x_watp_text("Build", V9X_BUILD_ID);
    v9x_watp_text("Mode", mode);
    v9x_watp_hex("Pid", GetCurrentProcessId());
    v9x_watp_hex("Image", (DWORD)GetModuleHandleA(0));
    ddraw = LoadLibraryA("DDRAW.DLL");
    create = ddraw != 0 ? (V9X_WATP_CREATE)GetProcAddress(ddraw,
                                                          "DirectDrawCreate")
                        : 0;
    if (create == 0 || create(0, &dd, 0) != DD_OK) {
        v9x_watp_text("Result", "FAIL-DDRAW");
        ExitProcess(1u);
    }
    /* A primary surface: the HAL's CreateSurface is where it starts
     * watching a program that has not loaded it itself. */
    if (IDirectDraw_SetCooperativeLevel(dd, 0, DDSCL_NORMAL) != DD_OK) {
        v9x_watp_text("Result", "FAIL-COOPERATIVE");
        ExitProcess(1u);
    }
    {
        DDSURFACEDESC desc;
        BYTE *bytes = (BYTE *)&desc;
        DWORD index;

        for (index = 0ul; index < sizeof(desc); ++index) {
            bytes[index] = 0u;
        }
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS;
        desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
        if (IDirectDraw_CreateSurface(dd, &desc, &primary, 0) != DD_OK) {
            v9x_watp_text("Result", "FAIL-PRIMARY");
            ExitProcess(1u);
        }
    }
    v9x_watp_text("Result", "DDRAW-OPEN");

    if (v9x_watp_is(mode, "exit")) {
        ExitProcess(0x1234u);
    }
    if (v9x_watp_is(mode, "terminate")) {
        TerminateProcess(GetCurrentProcess(), 0x2345u);
    }
    if (v9x_watp_is(mode, "message")) {
        CreateThread(0, 0, v9x_watp_dismiss, 0, 0, &thread);
        MessageBoxA(0, "Velocity9x watch probe: this box closes itself.",
                    V9X_WATP_CAPTION, MB_OK);
        ExitProcess(0x3456u);
    }
    if (v9x_watp_is(mode, "fault")) {
        SetUnhandledExceptionFilter(v9x_watp_filter);
        *(volatile DWORD *)0 = 1ul;
    }
    if (v9x_watp_is(mode, "unload")) {
        IDirectDrawSurface_Release(primary);
        IDirectDraw_Release(dd);
        FreeLibrary(ddraw);
        v9x_watp_text("Result", "UNLOADED");
        ExitProcess(0x5678u);
    }
    v9x_watp_text("Result", "UNKNOWN-MODE");
    ExitProcess(2u);
}
