/*
 * V9XGL.DLL, the OpenGL installable client driver: the Phase 0.9 probe build
 * (docs\plans\opengl-1.1-icd.md).
 *
 * This build renders nothing. It exists to measure the contract between
 * Windows 98 SE's OPENGL32.DLL and an ICD before any of the plan's rendering
 * code is written: which Drv* exports the runtime calls, in what order and
 * with what, whether one 16-bit pixel format is accepted and reported as
 * accelerated, and whether a dispatch table of 336 typed stubs is installed
 * and called through. Every entry point and every first call of every slot
 * is appended to C:\V9XDIAG\V9XGL.LOG with its process id, so the log is
 * the evidence and its order is part of it.
 *
 * The table is generated from src\opengl\gl_entrypoints.psd1 by
 * scripts\lib\gl-dispatch.ps1: a typed stub per slot, so the stack is
 * cleaned by exactly the bytes the caller pushed. Two slots are replaced at
 * the first DrvSetContext with real answers - glGetString, so an application
 * reading the vendor does not read a null, and glGetError, so one asking
 * after every call is told nothing went wrong.
 *
 * Per process, not shared: OPENGL32 loads it into each OpenGL application.
 * Imports KERNEL32 and USER32 only; no runtime.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"

#define V9X_GL_API __stdcall
static void v9x_gl_stub_called(unsigned int slot);
#define V9X_GL_STUB_HOOK(slot) v9x_gl_stub_called(slot)
#define V9X_GL_DEFINE_STUBS
#include "gl_dispatch_gen.h"

/* The ICD-side names of the runtime's types (Mesa gldrv.h, ReactOS icd.h). */
typedef ULONG V9X_DHGLRC;
typedef struct v9x_glcltproctable {
    int cEntries;
    V9X_GL_PROC entries[V9X_GL_SLOT_COUNT];
} V9X_GLCLTPROCTABLE;
typedef void (__stdcall *V9X_PFN_SETPROCTABLE)(V9X_GLCLTPROCTABLE *table);

#define V9X_GL_VENDOR     0x1F00u
#define V9X_GL_RENDERER   0x1F01u
#define V9X_GL_VERSION    0x1F02u
#define V9X_GL_EXTENSIONS 0x1F03u

#define V9X_GL_FORMAT_COUNT 1

static const char v9x_gl_build_id[] = "V9XGL build=" V9X_BUILD_ID;
static const char *const v9x_gl_slot_names[V9X_GL_SLOT_COUNT] = V9X_GL_SLOT_NAMES;
static V9X_GLCLTPROCTABLE v9x_gl_table = { V9X_GL_SLOT_COUNT, V9X_GL_DISPATCH_INIT };
static DWORD v9x_gl_slot_calls[V9X_GL_SLOT_COUNT];
static DWORD v9x_gl_sequence;
static V9X_DHGLRC v9x_gl_next_context = 1ul;
static int v9x_gl_overrides_installed;

/* One line, appended: sequence, process, text. Open/append/close each time
 * so a process that dies mid-run leaves everything it logged. */
static void v9x_gl_log(const char *text)
{
    HANDLE file;
    DWORD written;
    char line[320];

    file = CreateFileA(V9X_DIAG_GL_LOG, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    SetFilePointer(file, 0, 0, FILE_END);
    wsprintfA(line, "%lu pid=%08lX %s\r\n", ++v9x_gl_sequence,
              GetCurrentProcessId(), text);
    WriteFile(file, line, lstrlenA(line), &written, 0);
    CloseHandle(file);
}

static void v9x_gl_log3(const char *format, DWORD a, DWORD b, DWORD c)
{
    char text[256];

    wsprintfA(text, format, a, b, c);
    v9x_gl_log(text);
}

static void v9x_gl_stub_called(unsigned int slot)
{
    if (slot >= V9X_GL_SLOT_COUNT) {
        return;
    }
    if (v9x_gl_slot_calls[slot]++ == 0ul) {
        char text[128];

        wsprintfA(text, "stub first-call slot=%u %s", slot, v9x_gl_slot_names[slot]);
        v9x_gl_log(text);
    }
}

static const GLubyte * V9X_GL_API v9x_gl_get_string(GLenum name)
{
    v9x_gl_log3("glGetString name=0x%lX", (DWORD)name, 0ul, 0ul);
    switch (name) {
    case V9X_GL_VENDOR:     return (const GLubyte *)"Velocity9x";
    case V9X_GL_RENDERER:   return (const GLubyte *)"Velocity9x probe ICD";
    case V9X_GL_VERSION:    return (const GLubyte *)"1.1 Velocity9x-probe";
    case V9X_GL_EXTENSIONS: return (const GLubyte *)"";
    default:                return 0;
    }
}

static GLenum V9X_GL_API v9x_gl_get_error(void)
{
    return 0u;
}

static void v9x_gl_install_overrides(void)
{
    unsigned int slot;

    if (v9x_gl_overrides_installed) {
        return;
    }
    v9x_gl_overrides_installed = 1;
    for (slot = 0u; slot < V9X_GL_SLOT_COUNT; ++slot) {
        if (lstrcmpA(v9x_gl_slot_names[slot], "glGetString") == 0) {
            v9x_gl_table.entries[slot] = (V9X_GL_PROC)v9x_gl_get_string;
        } else if (lstrcmpA(v9x_gl_slot_names[slot], "glGetError") == 0) {
            v9x_gl_table.entries[slot] = (V9X_GL_PROC)v9x_gl_get_error;
        }
    }
}

/* The one pixel format this probe offers: the desktop's 16 bits, 565, a
 * back buffer, a 16-bit depth buffer, nothing else. Neither PFD_GENERIC_
 * flag: that is what marks it as the ICD's. */
static void v9x_gl_describe(PIXELFORMATDESCRIPTOR *pfd)
{
    unsigned int i;

    for (i = 0u; i < sizeof(*pfd); ++i) {
        ((BYTE *)pfd)[i] = 0u;
    }
    pfd->nSize = sizeof(*pfd);
    pfd->nVersion = 1;
    pfd->dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER |
                   PFD_SWAP_COPY;
    pfd->iPixelType = PFD_TYPE_RGBA;
    pfd->cColorBits = 16;
    pfd->cRedBits = 5;
    pfd->cRedShift = 11;
    pfd->cGreenBits = 6;
    pfd->cGreenShift = 5;
    pfd->cBlueBits = 5;
    pfd->cBlueShift = 0;
    pfd->cDepthBits = 16;
    pfd->iLayerType = PFD_MAIN_PLANE;
}

BOOL __stdcall DrvValidateVersion(ULONG version)
{
    v9x_gl_log3("DrvValidateVersion version=%lu", version, 0ul, 0ul);
    return TRUE;
}

void __stdcall DrvSetCallbackProcs(INT count, PROC *procs)
{
    v9x_gl_log3("DrvSetCallbackProcs count=%ld procs=%08lX", (DWORD)count,
                (DWORD)procs, 0ul);
}

LONG __stdcall DrvDescribePixelFormat(HDC hdc, INT index, ULONG bytes,
                                      PIXELFORMATDESCRIPTOR *pfd)
{
    v9x_gl_log3("DrvDescribePixelFormat index=%ld bytes=%lu pfd=%08lX",
                (DWORD)index, bytes, (DWORD)pfd);
    (void)hdc;
    if (pfd != 0 && index == 1 && bytes >= sizeof(*pfd)) {
        v9x_gl_describe(pfd);
    }
    return V9X_GL_FORMAT_COUNT;
}

BOOL __stdcall DrvSetPixelFormat(HDC hdc, LONG index)
{
    v9x_gl_log3("DrvSetPixelFormat hdc=%08lX index=%ld", (DWORD)hdc,
                (DWORD)index, 0ul);
    return index == 1 ? TRUE : FALSE;
}

V9X_DHGLRC __stdcall DrvCreateContext(HDC hdc)
{
    V9X_DHGLRC context = v9x_gl_next_context++;

    v9x_gl_log3("DrvCreateContext hdc=%08lX -> %lu", (DWORD)hdc, context, 0ul);
    return context;
}

/*
 * 98SE's OPENGL32 creates every context through this entry with layer 0 and
 * never calls DrvCreateContext (run1, 2026-09-26: wglCreateContext produced
 * one DrvCreateLayerContext(hdc, 0) and the probe's zero answer failed the
 * application's context). Layer 0 is the main plane, so it is the same
 * context DrvCreateContext would make; other planes are not offered.
 */
V9X_DHGLRC __stdcall DrvCreateLayerContext(HDC hdc, INT layer)
{
    V9X_DHGLRC context;

    if (layer != 0) {
        v9x_gl_log3("DrvCreateLayerContext hdc=%08lX layer=%ld -> 0", (DWORD)hdc,
                    (DWORD)layer, 0ul);
        return 0ul;
    }
    context = v9x_gl_next_context++;
    v9x_gl_log3("DrvCreateLayerContext hdc=%08lX layer=0 -> %lu", (DWORD)hdc,
                context, 0ul);
    return context;
}

BOOL __stdcall DrvDeleteContext(V9X_DHGLRC context)
{
    v9x_gl_log3("DrvDeleteContext context=%lu", context, 0ul, 0ul);
    return TRUE;
}

V9X_GLCLTPROCTABLE * __stdcall DrvSetContext(HDC hdc, V9X_DHGLRC context,
                                             V9X_PFN_SETPROCTABLE set_table)
{
    v9x_gl_log3("DrvSetContext hdc=%08lX context=%lu setproc=%08lX",
                (DWORD)hdc, context, (DWORD)set_table);
    v9x_gl_install_overrides();
    return &v9x_gl_table;
}

BOOL __stdcall DrvReleaseContext(V9X_DHGLRC context)
{
    v9x_gl_log3("DrvReleaseContext context=%lu", context, 0ul, 0ul);
    return TRUE;
}

BOOL __stdcall DrvCopyContext(V9X_DHGLRC source, V9X_DHGLRC target, UINT mask)
{
    v9x_gl_log3("DrvCopyContext source=%lu target=%lu mask=%08lX", source,
                target, (DWORD)mask);
    return FALSE;
}

BOOL __stdcall DrvShareLists(V9X_DHGLRC first, V9X_DHGLRC second)
{
    v9x_gl_log3("DrvShareLists first=%lu second=%lu", first, second, 0ul);
    return FALSE;
}

BOOL __stdcall DrvSwapBuffers(HDC hdc)
{
    v9x_gl_log3("DrvSwapBuffers hdc=%08lX", (DWORD)hdc, 0ul, 0ul);
    return TRUE;
}

BOOL __stdcall DrvSwapLayerBuffers(HDC hdc, UINT planes)
{
    v9x_gl_log3("DrvSwapLayerBuffers hdc=%08lX planes=%08lX", (DWORD)hdc,
                (DWORD)planes, 0ul);
    return FALSE;
}

BOOL __stdcall DrvDescribeLayerPlane(HDC hdc, INT index, INT layer,
                                     UINT bytes, LAYERPLANEDESCRIPTOR *descriptor)
{
    v9x_gl_log3("DrvDescribeLayerPlane index=%ld layer=%ld bytes=%lu",
                (DWORD)index, (DWORD)layer, (DWORD)bytes);
    (void)hdc;
    (void)descriptor;
    return FALSE;
}

INT __stdcall DrvSetLayerPaletteEntries(HDC hdc, INT layer, INT start,
                                        INT count, const COLORREF *entries)
{
    v9x_gl_log3("DrvSetLayerPaletteEntries layer=%ld start=%ld count=%ld",
                (DWORD)layer, (DWORD)start, (DWORD)count);
    (void)hdc;
    (void)entries;
    return 0;
}

INT __stdcall DrvGetLayerPaletteEntries(HDC hdc, INT layer, INT start,
                                        INT count, COLORREF *entries)
{
    v9x_gl_log3("DrvGetLayerPaletteEntries layer=%ld start=%ld count=%ld",
                (DWORD)layer, (DWORD)start, (DWORD)count);
    (void)hdc;
    (void)entries;
    return 0;
}

BOOL __stdcall DrvRealizeLayerPalette(HDC hdc, INT layer, BOOL realize)
{
    v9x_gl_log3("DrvRealizeLayerPalette layer=%ld realize=%lu", (DWORD)layer,
                (DWORD)realize, 0ul);
    (void)hdc;
    return FALSE;
}

PROC __stdcall DrvGetProcAddress(LPCSTR name)
{
    char text[200];

    wsprintfA(text, "DrvGetProcAddress name=%s -> NULL",
              name != 0 ? name : "(null)");
    v9x_gl_log(text);
    return 0;
}

BOOL __stdcall V9xGlEntry(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        CreateDirectoryA(V9X_DIAG_DIR, 0);
        v9x_gl_log3("attach instance=%08lX build-marker=%lu", (DWORD)instance,
                    (DWORD)(v9x_gl_build_id[0] != '\0'), 0ul);
    } else if (reason == DLL_PROCESS_DETACH) {
        DWORD slot;
        DWORD distinct = 0ul;
        DWORD total = 0ul;

        for (slot = 0ul; slot < V9X_GL_SLOT_COUNT; ++slot) {
            if (v9x_gl_slot_calls[slot] != 0ul) {
                ++distinct;
                total += v9x_gl_slot_calls[slot];
            }
        }
        v9x_gl_log3("detach slots-called=%lu stub-calls=%lu", distinct, total, 0ul);
    }
    return TRUE;
}
