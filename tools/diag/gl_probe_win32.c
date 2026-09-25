/*
 * OpenGL client probe (docs\plans\opengl-1.1-icd.md, Phase 0.9 and, later,
 * the V9XGLP scenes).
 *
 * Does what a GL application does at startup, through the system
 * OPENGL32.DLL, and writes what it got: the pixel formats the DC offers with
 * their generic/accelerated flags, which one ChoosePixelFormat picks for
 * GLQuake's request (24 colour bits, 32 depth, double-buffered), whether
 * SetPixelFormat and wglCreateContext succeed, the vendor/renderer/version
 * strings, and what one clear and one SwapBuffers return. With the ICD
 * registered, an ICD format appears first and is picked; without it, only
 * generic formats appear. Either way the probe reports rather than judges.
 *
 * Writes C:\V9XDIAG\V9XGLP.INI. Links OPENGL32 statically, as GLQuake does.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"

#define V9X_GLP_SECTION "GlProbe"
#define V9X_GLP_FORMAT_MAX 32
#define V9X_GLP_HOLD_MS 3000ul

static void v9x_glp_text(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_GLP_SECTION, key, value, V9X_DIAG_GLP_INI);
}

static void v9x_glp_uint(const char *key, DWORD value)
{
    char text[16];

    wsprintfA(text, "%lu", value);
    v9x_glp_text(key, text);
}

static void v9x_glp_hex(const char *key, DWORD value)
{
    char text[16];

    wsprintfA(text, "0x%08lX", value);
    v9x_glp_text(key, text);
}

static void v9x_glp_zero(void *block, DWORD bytes)
{
    DWORD i;

    for (i = 0ul; i < bytes; ++i) {
        ((BYTE *)block)[i] = 0u;
    }
}

static void v9x_glp_pump(void)
{
    MSG message;

    while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

static LRESULT CALLBACK v9x_glp_window_proc(HWND window, UINT message,
                                            WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcA(window, message, wparam, lparam);
}

static void v9x_glp_format(const char *prefix, const PIXELFORMATDESCRIPTOR *pfd)
{
    char key[48];

    wsprintfA(key, "%sFlags", prefix);
    v9x_glp_hex(key, pfd->dwFlags);
    wsprintfA(key, "%sGeneric", prefix);
    v9x_glp_uint(key, (pfd->dwFlags & PFD_GENERIC_FORMAT) != 0ul ? 1ul : 0ul);
    wsprintfA(key, "%sGenericAccelerated", prefix);
    v9x_glp_uint(key, (pfd->dwFlags & PFD_GENERIC_ACCELERATED) != 0ul ? 1ul : 0ul);
    wsprintfA(key, "%sDoubleBuffer", prefix);
    v9x_glp_uint(key, (pfd->dwFlags & PFD_DOUBLEBUFFER) != 0ul ? 1ul : 0ul);
    wsprintfA(key, "%sPixelType", prefix);
    v9x_glp_uint(key, pfd->iPixelType);
    wsprintfA(key, "%sColorBits", prefix);
    v9x_glp_uint(key, pfd->cColorBits);
    wsprintfA(key, "%sDepthBits", prefix);
    v9x_glp_uint(key, pfd->cDepthBits);
    wsprintfA(key, "%sStencilBits", prefix);
    v9x_glp_uint(key, pfd->cStencilBits);
}

static void v9x_glp_string(const char *key, GLenum name)
{
    const GLubyte *value = glGetString(name);
    char text[256];
    unsigned int i;

    if (value == 0) {
        v9x_glp_text(key, "(null)");
        return;
    }
    for (i = 0u; i < sizeof(text) - 1u && value[i] != 0u; ++i) {
        text[i] = (char)value[i];
    }
    text[i] = '\0';
    v9x_glp_text(key, text);
}

void __stdcall V9xGlProbeEntry(void)
{
    WNDCLASSA window_class;
    HWND window;
    HDC hdc;
    PIXELFORMATDESCRIPTOR pfd;
    int count;
    int index;
    int chosen;
    HGLRC context = 0;
    int ok = 0;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_DIAG_GLP_INI);
    v9x_glp_text("Build", V9X_BUILD_ID);
    v9x_glp_text("Result", "RUNNING");
    v9x_glp_uint("SchemaVersion", 1ul);
    v9x_glp_hex("ProcessId", GetCurrentProcessId());

    v9x_glp_zero(&window_class, sizeof(window_class));
    window_class.lpfnWndProc = v9x_glp_window_proc;
    window_class.hInstance = GetModuleHandleA(0);
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class.lpszClassName = "Velocity9xGlProbe";
    RegisterClassA(&window_class);
    window = CreateWindowExA(0ul, window_class.lpszClassName, "Velocity9x GL probe",
                             WS_POPUP | WS_VISIBLE, 150, 150, 400, 300,
                             0, 0, window_class.hInstance, 0);
    if (window == 0) {
        v9x_glp_text("Result", "FAIL-WINDOW");
        ExitProcess(1u);
    }
    v9x_glp_pump();
    hdc = GetDC(window);
    v9x_glp_uint("DesktopBitsPerPixel", (DWORD)GetDeviceCaps(hdc, BITSPIXEL));

    /* Every format the DC offers, in the runtime's numbering. */
    v9x_glp_zero(&pfd, sizeof(pfd));
    count = DescribePixelFormat(hdc, 1, sizeof(pfd), 0);
    v9x_glp_uint("FormatCount", (DWORD)count);
    for (index = 1; index <= count && index <= V9X_GLP_FORMAT_MAX; ++index) {
        char prefix[16];

        v9x_glp_zero(&pfd, sizeof(pfd));
        if (DescribePixelFormat(hdc, index, sizeof(pfd), &pfd) == 0) {
            continue;
        }
        wsprintfA(prefix, "F%d", index);
        v9x_glp_format(prefix, &pfd);
    }

    /* GLQuake's request (gl_vidnt.c bSetupPixelFormat). */
    v9x_glp_zero(&pfd, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 32;
    pfd.iLayerType = PFD_MAIN_PLANE;
    chosen = ChoosePixelFormat(hdc, &pfd);
    v9x_glp_uint("ChosenFormat", (DWORD)chosen);
    v9x_glp_hex("ChooseLastError", chosen == 0 ? GetLastError() : 0ul);
    if (chosen != 0) {
        v9x_glp_zero(&pfd, sizeof(pfd));
        if (DescribePixelFormat(hdc, chosen, sizeof(pfd), &pfd) != 0) {
            v9x_glp_format("Chosen", &pfd);
        }
        v9x_glp_uint("SetPixelFormatOk", SetPixelFormat(hdc, chosen, &pfd) ? 1ul : 0ul);
        v9x_glp_hex("SetPixelFormatLastError", GetLastError());
        v9x_glp_uint("GetPixelFormat", (DWORD)GetPixelFormat(hdc));

        context = wglCreateContext(hdc);
        v9x_glp_hex("Context", (DWORD)context);
        v9x_glp_hex("CreateContextLastError", context == 0 ? GetLastError() : 0ul);
    }
    if (context != 0) {
        v9x_glp_uint("MakeCurrentOk", wglMakeCurrent(hdc, context) ? 1ul : 0ul);
        v9x_glp_hex("MakeCurrentLastError", GetLastError());
        v9x_glp_string("Vendor", GL_VENDOR);
        v9x_glp_string("Renderer", GL_RENDERER);
        v9x_glp_string("Version", GL_VERSION);
        v9x_glp_string("Extensions", GL_EXTENSIONS);
        glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        v9x_glp_hex("ErrorAfterClear", (DWORD)glGetError());
        v9x_glp_uint("SwapBuffersOk", SwapBuffers(hdc) ? 1ul : 0ul);
        v9x_glp_hex("SwapBuffersLastError", GetLastError());
        v9x_glp_hex("ErrorAfterSwap", (DWORD)glGetError());
        ok = 1;
        {
            DWORD started = GetTickCount();

            v9x_glp_text("Stage", "holding");
            while (GetTickCount() - started < V9X_GLP_HOLD_MS) {
                v9x_glp_pump();
                Sleep(50);
            }
        }
        wglMakeCurrent(0, 0);
        v9x_glp_uint("DeleteContextOk", wglDeleteContext(context) ? 1ul : 0ul);
    }
    ReleaseDC(window, hdc);
    v9x_glp_text("Stage", "done");
    v9x_glp_text("Result", ok ? "PASS" : "FAIL");
    WritePrivateProfileStringA(0, 0, 0, V9X_DIAG_GLP_INI);
    ExitProcess(ok ? 0u : 1u);
}
