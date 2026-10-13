/*
 * V9XGL3P.EXE: one Glide 3 session against a named GLIDE3X.DLL.
 *
 * docs\plans\3dfx-vsa100-glide-ext-hwc.md, step 1. Loads the DLL given on the
 * command line (default C:\WINDOWS\SYSTEM\GLIDE3X.DLL, 3dfx's own on a Voodoo
 * card), installs an error callback, and runs grGlideInit, grSstSelect(0),
 * grSstWinOpen at 640x480, one clear and swap, grSstWinClose and
 * grGlideShutdown. Each step is written to C:\V9XDIAG\V9XGL3P.INI as it is
 * reached, so a hang or fault leaves the last step on disk. A Glide error
 * records its message and ends the process: Glide's own default would show a
 * message box, which nothing on a remote machine can dismiss.
 *
 * Glide 3's exports are __stdcall with decorated names (_grGlideInit@0),
 * checked against 3dfx's 3.10.00.0658 DLL.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_GL3P_SECTION "Velocity9xGlide3Session"
#define V9X_GL3P_DEFAULT_DLL "C:\\WINDOWS\\SYSTEM\\GLIDE3X.DLL"

/* Glide 3 constants (glide.h, 3.x): 640x480, refresh 60 Hz, ARGB colour,
 * origin upper left, the colour-buffer clear mask. */
#define V9X_GR_RESOLUTION_640x480 0x7ul
#define V9X_GR_REFRESH_60Hz 0x0ul
#define V9X_GR_COLORFORMAT_ARGB 0x0ul
#define V9X_GR_ORIGIN_UPPER_LEFT 0x0ul
#define V9X_GR_HARDWARE 0xa1ul

typedef void (__stdcall *v9x_gr_void0)(void);
typedef void (__stdcall *v9x_gr_select)(DWORD);
typedef DWORD (__stdcall *v9x_gr_open)(DWORD, DWORD, DWORD, DWORD, DWORD,
                                       int, int);
typedef DWORD (__stdcall *v9x_gr_close)(DWORD);
typedef void (__stdcall *v9x_gr_clear)(DWORD, DWORD, DWORD);
typedef void (__stdcall *v9x_gr_swap)(DWORD);
typedef const char *(__stdcall *v9x_gr_string)(DWORD);
typedef void (__stdcall *v9x_gr_error_cb)(const char *, int);
typedef void (__stdcall *v9x_gr_set_error)(v9x_gr_error_cb);

static char v9x_gl3p_path[MAX_PATH];

static void v9x_gl3p_write(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_GL3P_SECTION, key, value,
                               V9X_DIAG_GL3P_INI);
    /* Commit now, so a fault in the next call leaves this on disk. */
    WritePrivateProfileStringA(0, 0, 0, V9X_DIAG_GL3P_INI);
}

static void v9x_gl3p_hex(const char *key, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char text[11];
    int index;

    text[0] = '0';
    text[1] = 'x';
    for (index = 0; index < 8; ++index) {
        text[2 + index] = digits[(value >> ((7 - index) * 4)) & 15u];
    }
    text[10] = '\0';
    v9x_gl3p_write(key, text);
}

static void __stdcall v9x_gl3p_error(const char *message, int fatal)
{
    v9x_gl3p_write("ErrorMessage", message != 0 ? message : "(null)");
    v9x_gl3p_write("ErrorFatal", fatal ? "1" : "0");
    v9x_gl3p_write("Result", "GLIDE-ERROR");
    ExitProcess(9u);
}

static FARPROC v9x_gl3p_proc(HMODULE module, const char *name)
{
    FARPROC proc = GetProcAddress(module, name);

    if (proc == 0) {
        v9x_gl3p_write("MissingExport", name);
        v9x_gl3p_write("Result", "EXPORT-MISSING");
        ExitProcess(3u);
    }
    return proc;
}

/* The command line's first argument after the program name, or the
 * default. */
static const char *v9x_gl3p_dll(void)
{
    const char *line = GetCommandLineA();
    int quoted = 0;
    int at = 0;

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
    if (*line == '\0') {
        return V9X_GL3P_DEFAULT_DLL;
    }
    if (*line == '"') {
        quoted = 1;
        ++line;
    }
    while (*line != '\0' && at < MAX_PATH - 1 &&
           (quoted ? *line != '"' : *line != ' ')) {
        v9x_gl3p_path[at++] = *line++;
    }
    v9x_gl3p_path[at] = '\0';
    return v9x_gl3p_path;
}

static LRESULT CALLBACK v9x_gl3p_wndproc(HWND window, UINT message,
                                         WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcA(window, message, wparam, lparam);
}

void WINAPI V9xGlide3SessionEntry(void)
{
    static const char class_name[] = "V9xGlide3Session";
    const char *dll = v9x_gl3p_dll();
    WNDCLASSA window_class;
    HMODULE module;
    HWND window;
    v9x_gr_set_error set_error;
    v9x_gr_void0 glide_init;
    v9x_gr_void0 glide_shutdown;
    v9x_gr_select sst_select;
    v9x_gr_open win_open;
    v9x_gr_close win_close;
    v9x_gr_clear buffer_clear;
    v9x_gr_swap buffer_swap;
    v9x_gr_string get_string;
    DWORD context;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_DIAG_GL3P_INI);
    v9x_gl3p_write("Build", V9X_BUILD_ID);
    v9x_gl3p_write("Dll", dll);
    v9x_gl3p_write("Result", "INCOMPLETE");

    v9x_gl3p_write("Step", "load");
    module = LoadLibraryA(dll);
    if (module == 0) {
        v9x_gl3p_hex("LoadError", GetLastError());
        v9x_gl3p_write("Result", "LOAD-FAILED");
        ExitProcess(2u);
    }
    set_error = (v9x_gr_set_error)v9x_gl3p_proc(module, "_grErrorSetCallback@4");
    glide_init = (v9x_gr_void0)v9x_gl3p_proc(module, "_grGlideInit@0");
    glide_shutdown = (v9x_gr_void0)v9x_gl3p_proc(module, "_grGlideShutdown@0");
    sst_select = (v9x_gr_select)v9x_gl3p_proc(module, "_grSstSelect@4");
    win_open = (v9x_gr_open)v9x_gl3p_proc(module, "_grSstWinOpen@28");
    win_close = (v9x_gr_close)v9x_gl3p_proc(module, "_grSstWinClose@4");
    buffer_clear = (v9x_gr_clear)v9x_gl3p_proc(module, "_grBufferClear@12");
    buffer_swap = (v9x_gr_swap)v9x_gl3p_proc(module, "_grBufferSwap@4");
    get_string = (v9x_gr_string)v9x_gl3p_proc(module, "_grGetString@4");

    set_error(v9x_gl3p_error);

    v9x_gl3p_write("Step", "grGlideInit");
    glide_init();
    v9x_gl3p_write("Step", "grSstSelect");
    sst_select(0ul);
    v9x_gl3p_write("Hardware", get_string(V9X_GR_HARDWARE));

    window_class.style = 0u;
    window_class.lpfnWndProc = v9x_gl3p_wndproc;
    window_class.cbClsExtra = 0;
    window_class.cbWndExtra = 0;
    window_class.hInstance = GetModuleHandleA(0);
    window_class.hIcon = 0;
    window_class.hCursor = 0;
    window_class.hbrBackground = 0;
    window_class.lpszMenuName = 0;
    window_class.lpszClassName = class_name;
    RegisterClassA(&window_class);
    window = CreateWindowA(class_name, "V9XGL3P", WS_POPUP | WS_VISIBLE,
                           0, 0, 640, 480, 0, 0, window_class.hInstance, 0);

    v9x_gl3p_write("Step", "grSstWinOpen");
    context = win_open((DWORD)window, V9X_GR_RESOLUTION_640x480,
                       V9X_GR_REFRESH_60Hz, V9X_GR_COLORFORMAT_ARGB,
                       V9X_GR_ORIGIN_UPPER_LEFT, 2, 1);
    v9x_gl3p_hex("Context", context);
    if (context != 0ul) {
        v9x_gl3p_write("Step", "grBufferClear");
        buffer_clear(0x000000fful, 0ul, 0xfffful);
        v9x_gl3p_write("Step", "grBufferSwap");
        buffer_swap(1ul);
        v9x_gl3p_write("Step", "grSstWinClose");
        v9x_gl3p_hex("WinClose", win_close(context));
    }
    v9x_gl3p_write("Step", "grGlideShutdown");
    glide_shutdown();
    if (window != 0) {
        DestroyWindow(window);
    }
    v9x_gl3p_write("Step", "done");
    v9x_gl3p_write("Result", context != 0ul ? "SESSION" : "NO-CONTEXT");
    ExitProcess(context != 0ul ? 0u : 1u);
}
