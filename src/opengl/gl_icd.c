/*
 * V9XGL.DLL, the OpenGL installable client driver
 * (docs\plans\opengl-1.1-icd.md, Phase 3).
 *
 * The Drv* exports OPENGL32 calls, the contexts they create, and the GL
 * commands that are implemented so far: glClear through the HAL's render
 * interface, glFinish and glFlush likewise, and the state commands whose
 * whole effect is in src\opengl\gl_state.c (clear colour and depth,
 * viewport, scissor, masks, enables, errors). Every other slot of the
 * generated 336-entry table is a typed stub that records GL_INVALID_OPERATION
 * on the current context: a development placeholder, tracked in
 * docs\plans\opengl-1.1-requirements.md, never a silent success.
 *
 * Contexts come from DrvCreateLayerContext(hdc, 0) on 98SE (measured,
 * Phase 0.9) and DrvCreateContext elsewhere. One context is current per
 * thread, through the ICD's own TLS slot; a context current on another
 * thread cannot be made current here. A critical section guards the context
 * table and the device; GL commands on the current context take it too,
 * which is the plan's lock order: the ICD's section, then the Win16 mutex
 * inside each render-interface call, never the reverse.
 *
 * One pixel format: the desktop's 16 bits as the HAL's describe offers it
 * (565 or 555), double-buffered, 16-bit depth, no alpha, stencil or
 * accumulation. When the engine offers none - the ViRGE on a 565 desktop -
 * the ICD lists no formats and OPENGL32 serves its generic ones.
 *
 * Every Drv* call and every first call of a stub is appended to
 * C:\V9XDIAG\V9XGL.LOG with its process id. Per process, not shared.
 * Imports KERNEL32 and USER32 only; DirectDraw is loaded at run time.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"
#include "gl_state.h"
#include "gl_surface.h"

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

#define V9X_GL_CONTEXTS_MAX 16u

typedef struct v9x_gl_context {
    int in_use;
    /* The thread it is current on, or zero. */
    DWORD owner;
    V9X_GL_DRAWABLE *drawable;
    int bound_once;
    V9X_GL_STATE state;
} V9X_GL_CONTEXT;

static const char v9x_gl_build_id[] = "V9XGL build=" V9X_BUILD_ID;
static const char *const v9x_gl_slot_names[V9X_GL_SLOT_COUNT] = V9X_GL_SLOT_NAMES;
static V9X_GLCLTPROCTABLE v9x_gl_table = { V9X_GL_SLOT_COUNT, V9X_GL_DISPATCH_INIT };
static DWORD v9x_gl_slot_calls[V9X_GL_SLOT_COUNT];
static DWORD v9x_gl_sequence;
static int v9x_gl_overrides_installed;
static CRITICAL_SECTION v9x_gl_lock;
static DWORD v9x_gl_tls = 0xfffffffful;
static V9X_GL_CONTEXT v9x_gl_contexts[V9X_GL_CONTEXTS_MAX];

/* One line, appended: sequence, process, text. Open/append/close each time
 * so a process that dies mid-run leaves everything it logged. */
void v9x_gl_log(const char *text)
{
    HANDLE file;
    char line[320];
    DWORD written;
    int length;

    file = CreateFileA(V9X_DIAG_GL_LOG, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    SetFilePointer(file, 0, 0, FILE_END);
    length = wsprintfA(line, "%lu pid=%08lX %s\r\n", ++v9x_gl_sequence,
                       GetCurrentProcessId(), text);
    WriteFile(file, line, (DWORD)length, &written, 0);
    CloseHandle(file);
}

void v9x_gl_log3(const char *format, v9x_u32 a, v9x_u32 b, v9x_u32 c)
{
    char text[256];

    wsprintfA(text, format, a, b, c);
    v9x_gl_log(text);
}

static V9X_GL_CONTEXT *v9x_gl_current(void)
{
    if (v9x_gl_tls == 0xfffffffful) {
        return 0;
    }
    return (V9X_GL_CONTEXT *)TlsGetValue(v9x_gl_tls);
}

static void v9x_gl_stub_called(unsigned int slot)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0) {
        v9x_gl_state_error(&context->state, V9X_GL_INVALID_OPERATION);
    }
    if (slot >= V9X_GL_SLOT_COUNT) {
        return;
    }
    if (v9x_gl_slot_calls[slot]++ == 0ul) {
        char text[128];

        wsprintfA(text, "stub first-call slot=%u %s", slot,
                  v9x_gl_slot_names[slot]);
        v9x_gl_log(text);
    }
}

/* ---- GL commands --------------------------------------------------- */

static const GLubyte * V9X_GL_API v9x_gl_get_string(GLenum name)
{
    const V9X_R3D_ABI_DESCRIBE *description = v9x_gl_device_description();

    if (v9x_gl_current() == 0) {
        return 0;
    }
    switch (name) {
    case V9X_GL_VENDOR:
        return (const GLubyte *)"Velocity9x";
    case V9X_GL_RENDERER:
        return (const GLubyte *)(description != 0 ? description->renderer
                                                  : "Velocity9x");
    case V9X_GL_VERSION:
        return (const GLubyte *)"1.1.0";
    case V9X_GL_EXTENSIONS:
        return (const GLubyte *)"";
    default:
        v9x_gl_state_error(&v9x_gl_current()->state, V9X_GL_INVALID_ENUM);
        return 0;
    }
}

static GLenum V9X_GL_API v9x_gl_get_error(void)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    return context != 0 ? v9x_gl_state_get_error(&context->state)
                        : V9X_GL_NO_ERROR;
}

static void V9X_GL_API v9x_gl_clear_color(GLclampf red, GLclampf green,
                                          GLclampf blue, GLclampf alpha)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0) {
        v9x_gl_state_clear_color(&context->state, red, green, blue, alpha);
    }
}

static void V9X_GL_API v9x_gl_clear_depth(GLclampd depth)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0) {
        v9x_gl_state_clear_depth(&context->state, depth);
    }
}

static void V9X_GL_API v9x_gl_viewport(GLint x, GLint y, GLsizei width,
                                       GLsizei height)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0) {
        v9x_gl_state_viewport(&context->state, x, y, width, height);
    }
}

static void V9X_GL_API v9x_gl_scissor(GLint x, GLint y, GLsizei width,
                                      GLsizei height)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0) {
        v9x_gl_state_scissor(&context->state, x, y, width, height);
    }
}

static void V9X_GL_API v9x_gl_color_mask(GLboolean red, GLboolean green,
                                         GLboolean blue, GLboolean alpha)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0) {
        v9x_gl_state_color_mask(&context->state, red, green, blue, alpha);
    }
}

static void V9X_GL_API v9x_gl_depth_mask(GLboolean flag)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0) {
        v9x_gl_state_depth_mask(&context->state, flag);
    }
}

static void V9X_GL_API v9x_gl_enable(GLenum cap)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0) {
        v9x_gl_state_enable(&context->state, cap, 1);
    }
}

static void V9X_GL_API v9x_gl_disable(GLenum cap)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0) {
        v9x_gl_state_enable(&context->state, cap, 0);
    }
}

static GLboolean V9X_GL_API v9x_gl_is_enabled(GLenum cap)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    return context != 0 ? v9x_gl_state_is_enabled(&context->state, cap) : 0;
}

/*
 * Re-bind the current context's window: the client area may have changed
 * size, and the surfaces with it. The first bind of a context also sets its
 * viewport and scissor (gl_state.c).
 */
static V9X_GL_DRAWABLE *v9x_gl_bind_window(V9X_GL_CONTEXT *context,
                                           HWND window)
{
    V9X_GL_DRAWABLE *drawable;
    v9x_u32 width;
    v9x_u32 height;
    int resized;

    drawable = v9x_gl_drawable_bind(window, &resized);
    if (drawable == 0) {
        return 0;
    }
    v9x_gl_drawable_size(drawable, &width, &height);
    v9x_gl_state_drawable(&context->state, width, height,
                          v9x_gl_device_format(), !context->bound_once);
    context->bound_once = 1;
    context->drawable = drawable;
    return drawable;
}

/* The window a drawable belongs to is the one DrvSetContext was handed; it
 * is kept on the context so a command can re-bind after a resize. */
static HWND v9x_gl_context_window(const V9X_GL_CONTEXT *context);

static void V9X_GL_API v9x_gl_clear(GLbitfield mask)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();
    const V9X_R3D_INTERFACE *iface = v9x_gl_device_interface();
    const V9X_R3D_ABI_DESCRIBE *description = v9x_gl_device_description();
    V9X_GL_DRAWABLE *drawable;
    V9X_GL_CLEAR_PLAN plan;
    V9X_R3D_ABI_CLEAR clear;
    V9X_R3D_ABI_RECT rect;
    v9x_u32 result;

    if (context == 0 || iface == 0) {
        return;
    }
    EnterCriticalSection(&v9x_gl_lock);
    drawable = v9x_gl_bind_window(context, v9x_gl_context_window(context));
    if (drawable == 0) {
        v9x_gl_state_error(&context->state, V9X_GL_OUT_OF_MEMORY);
        LeaveCriticalSection(&v9x_gl_lock);
        return;
    }
    if (!v9x_gl_state_clear(&context->state, mask, 1, &plan)) {
        LeaveCriticalSection(&v9x_gl_lock);
        return;
    }
    rect.left = plan.rect_left;
    rect.top = plan.rect_top;
    rect.right = plan.rect_right;
    rect.bottom = plan.rect_bottom;
    clear.struct_bytes = sizeof(clear);
    clear.generation = description->generation;
    clear.target.surface = v9x_gl_drawable_back(drawable);
    clear.depth.surface = v9x_gl_drawable_depth(drawable);
    clear.clear_color = plan.clear_color;
    clear.clear_depth = plan.clear_depth;
    clear.color_value = plan.color_value;
    clear.depth_value = plan.depth_value;
    clear.write_mask = plan.write_mask;
    clear.write_depth = plan.clear_depth;
    clear.rects = &rect;
    clear.rect_count = 1ul;
    result = iface->clear(&clear);
    if (result == V9X_R3D_RESULT_STALE && v9x_gl_device_redescribe()) {
        /* A mode change: new generation, and the surfaces made again. */
        drawable = v9x_gl_bind_window(context, v9x_gl_context_window(context));
        if (drawable != 0) {
            clear.generation = description->generation;
            clear.target.surface = v9x_gl_drawable_back(drawable);
            clear.depth.surface = v9x_gl_drawable_depth(drawable);
            result = iface->clear(&clear);
        }
    }
    if (result != V9X_R3D_RESULT_OK) {
        v9x_gl_log3("glClear result=%lu mask=%08lX", result, (v9x_u32)mask,
                    0ul);
    }
    LeaveCriticalSection(&v9x_gl_lock);
}

static void V9X_GL_API v9x_gl_finish(void)
{
    const V9X_R3D_INTERFACE *iface = v9x_gl_device_interface();
    const V9X_R3D_ABI_DESCRIBE *description = v9x_gl_device_description();
    v9x_u32 result;

    if (v9x_gl_current() == 0 || iface == 0) {
        return;
    }
    result = iface->finish(description->generation);
    if (result != V9X_R3D_RESULT_OK) {
        v9x_gl_log3("glFinish result=%lu", result, 0ul, 0ul);
    }
}

static void V9X_GL_API v9x_gl_flush(void)
{
    const V9X_R3D_INTERFACE *iface = v9x_gl_device_interface();
    const V9X_R3D_ABI_DESCRIBE *description = v9x_gl_device_description();

    if (v9x_gl_current() == 0 || iface == 0) {
        return;
    }
    (void)iface->flush(description->generation);
}

/* Each override checked against its slot's generated type: a signature
 * that does not match the slot cannot compile. */
static const V9X_GL_PFN_glGetString v9x_gl_check_get_string = v9x_gl_get_string;
static const V9X_GL_PFN_glGetError v9x_gl_check_get_error = v9x_gl_get_error;
static const V9X_GL_PFN_glClearColor v9x_gl_check_clear_color = v9x_gl_clear_color;
static const V9X_GL_PFN_glClearDepth v9x_gl_check_clear_depth = v9x_gl_clear_depth;
static const V9X_GL_PFN_glViewport v9x_gl_check_viewport = v9x_gl_viewport;
static const V9X_GL_PFN_glScissor v9x_gl_check_scissor = v9x_gl_scissor;
static const V9X_GL_PFN_glColorMask v9x_gl_check_color_mask = v9x_gl_color_mask;
static const V9X_GL_PFN_glDepthMask v9x_gl_check_depth_mask = v9x_gl_depth_mask;
static const V9X_GL_PFN_glEnable v9x_gl_check_enable = v9x_gl_enable;
static const V9X_GL_PFN_glDisable v9x_gl_check_disable = v9x_gl_disable;
static const V9X_GL_PFN_glIsEnabled v9x_gl_check_is_enabled = v9x_gl_is_enabled;
static const V9X_GL_PFN_glClear v9x_gl_check_clear = v9x_gl_clear;
static const V9X_GL_PFN_glFinish v9x_gl_check_finish = v9x_gl_finish;
static const V9X_GL_PFN_glFlush v9x_gl_check_flush = v9x_gl_flush;

typedef struct v9x_gl_override {
    const char *name;
    V9X_GL_PROC proc;
} V9X_GL_OVERRIDE;

static void v9x_gl_install_overrides(void)
{
    V9X_GL_OVERRIDE overrides[14];
    unsigned int slot;
    unsigned int index;

    if (v9x_gl_overrides_installed) {
        return;
    }
    v9x_gl_overrides_installed = 1;
    overrides[0].name = "glGetString";
    overrides[0].proc = (V9X_GL_PROC)v9x_gl_check_get_string;
    overrides[1].name = "glGetError";
    overrides[1].proc = (V9X_GL_PROC)v9x_gl_check_get_error;
    overrides[2].name = "glClearColor";
    overrides[2].proc = (V9X_GL_PROC)v9x_gl_check_clear_color;
    overrides[3].name = "glClearDepth";
    overrides[3].proc = (V9X_GL_PROC)v9x_gl_check_clear_depth;
    overrides[4].name = "glViewport";
    overrides[4].proc = (V9X_GL_PROC)v9x_gl_check_viewport;
    overrides[5].name = "glScissor";
    overrides[5].proc = (V9X_GL_PROC)v9x_gl_check_scissor;
    overrides[6].name = "glColorMask";
    overrides[6].proc = (V9X_GL_PROC)v9x_gl_check_color_mask;
    overrides[7].name = "glDepthMask";
    overrides[7].proc = (V9X_GL_PROC)v9x_gl_check_depth_mask;
    overrides[8].name = "glEnable";
    overrides[8].proc = (V9X_GL_PROC)v9x_gl_check_enable;
    overrides[9].name = "glDisable";
    overrides[9].proc = (V9X_GL_PROC)v9x_gl_check_disable;
    overrides[10].name = "glIsEnabled";
    overrides[10].proc = (V9X_GL_PROC)v9x_gl_check_is_enabled;
    overrides[11].name = "glClear";
    overrides[11].proc = (V9X_GL_PROC)v9x_gl_check_clear;
    overrides[12].name = "glFinish";
    overrides[12].proc = (V9X_GL_PROC)v9x_gl_check_finish;
    overrides[13].name = "glFlush";
    overrides[13].proc = (V9X_GL_PROC)v9x_gl_check_flush;
    for (index = 0u; index < 14u; ++index) {
        for (slot = 0u; slot < V9X_GL_SLOT_COUNT; ++slot) {
            if (lstrcmpA(v9x_gl_slot_names[slot], overrides[index].name) == 0) {
                v9x_gl_table.entries[slot] = overrides[index].proc;
                break;
            }
        }
        if (slot == V9X_GL_SLOT_COUNT) {
            v9x_gl_log(overrides[index].name);
        }
    }
}

/* ---- Pixel formats ------------------------------------------------- */

/* The formats offered: one, when the engine can write the desktop's layout,
 * else none. Opening the device here is deliberate: DescribePixelFormat is
 * the runtime's first question, and the answer depends on the engine. */
static int v9x_gl_format_count(void)
{
    int count;

    EnterCriticalSection(&v9x_gl_lock);
    count = v9x_gl_device_open() && v9x_gl_device_format() != 0ul ? 1 : 0;
    LeaveCriticalSection(&v9x_gl_lock);
    return count;
}

static void v9x_gl_describe(PIXELFORMATDESCRIPTOR *pfd)
{
    unsigned int i;
    int is_565 = v9x_gl_device_format() == V9X_R3D_ABI_FORMAT_RGB565;

    for (i = 0u; i < sizeof(*pfd); ++i) {
        ((BYTE *)pfd)[i] = 0u;
    }
    pfd->nSize = sizeof(*pfd);
    pfd->nVersion = 1;
    /* Neither PFD_GENERIC_ flag: that is what marks the format as the
     * ICD's. */
    pfd->dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER |
                   PFD_SWAP_COPY;
    pfd->iPixelType = PFD_TYPE_RGBA;
    pfd->cColorBits = 16;
    pfd->cRedBits = 5;
    pfd->cRedShift = (BYTE)(is_565 ? 11 : 10);
    pfd->cGreenBits = (BYTE)(is_565 ? 6 : 5);
    pfd->cGreenShift = 5;
    pfd->cBlueBits = 5;
    pfd->cBlueShift = 0;
    pfd->cDepthBits = 16;
    pfd->iLayerType = PFD_MAIN_PLANE;
}

/* ---- Contexts ------------------------------------------------------ */

static HWND v9x_gl_windows[V9X_GL_CONTEXTS_MAX];

static HWND v9x_gl_context_window(const V9X_GL_CONTEXT *context)
{
    return v9x_gl_windows[context - v9x_gl_contexts];
}

static V9X_GL_CONTEXT *v9x_gl_context_of(V9X_DHGLRC handle)
{
    if (handle == 0ul || handle > V9X_GL_CONTEXTS_MAX ||
        !v9x_gl_contexts[handle - 1ul].in_use) {
        return 0;
    }
    return &v9x_gl_contexts[handle - 1ul];
}

static V9X_DHGLRC v9x_gl_context_create(HDC hdc)
{
    unsigned int index;
    V9X_DHGLRC handle = 0ul;

    EnterCriticalSection(&v9x_gl_lock);
    if (v9x_gl_device_open() && v9x_gl_device_format() != 0ul) {
        for (index = 0u; index < V9X_GL_CONTEXTS_MAX; ++index) {
            if (!v9x_gl_contexts[index].in_use) {
                V9X_GL_CONTEXT *context = &v9x_gl_contexts[index];

                context->in_use = 1;
                context->owner = 0ul;
                context->drawable = 0;
                context->bound_once = 0;
                v9x_gl_state_init(&context->state);
                v9x_gl_windows[index] = WindowFromDC(hdc);
                handle = (V9X_DHGLRC)(index + 1u);
                break;
            }
        }
    }
    LeaveCriticalSection(&v9x_gl_lock);
    return handle;
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
    int count = v9x_gl_format_count();

    v9x_gl_log3("DrvDescribePixelFormat index=%ld bytes=%lu -> count=%ld",
                (DWORD)index, bytes, (DWORD)count);
    (void)hdc;
    if (pfd != 0 && count != 0 && index == 1 && bytes >= sizeof(*pfd)) {
        v9x_gl_describe(pfd);
    }
    return count;
}

BOOL __stdcall DrvSetPixelFormat(HDC hdc, LONG index)
{
    v9x_gl_log3("DrvSetPixelFormat hdc=%08lX index=%ld", (DWORD)hdc,
                (DWORD)index, 0ul);
    return index == 1 && v9x_gl_format_count() == 1 ? TRUE : FALSE;
}

V9X_DHGLRC __stdcall DrvCreateContext(HDC hdc)
{
    V9X_DHGLRC context = v9x_gl_context_create(hdc);

    v9x_gl_log3("DrvCreateContext hdc=%08lX -> %lu", (DWORD)hdc, context, 0ul);
    return context;
}

/*
 * 98SE's OPENGL32 creates every context through this entry with layer 0 and
 * never calls DrvCreateContext (run1, 2026-09-26). Layer 0 is the main
 * plane, so it is the same context DrvCreateContext would make; other
 * planes are not offered.
 */
V9X_DHGLRC __stdcall DrvCreateLayerContext(HDC hdc, INT layer)
{
    V9X_DHGLRC context = layer == 0 ? v9x_gl_context_create(hdc) : 0ul;

    v9x_gl_log3("DrvCreateLayerContext hdc=%08lX layer=%ld -> %lu",
                (DWORD)hdc, (DWORD)layer, context);
    return context;
}

BOOL __stdcall DrvDeleteContext(V9X_DHGLRC handle)
{
    V9X_GL_CONTEXT *context;
    BOOL ok = FALSE;

    EnterCriticalSection(&v9x_gl_lock);
    context = v9x_gl_context_of(handle);
    /* A context current on another thread is not this thread's to delete;
     * one current here is released first. */
    if (context != 0 &&
        (context->owner == 0ul || context->owner == GetCurrentThreadId())) {
        if (v9x_gl_current() == context) {
            TlsSetValue(v9x_gl_tls, 0);
        }
        context->in_use = 0;
        context->owner = 0ul;
        context->drawable = 0;
        ok = TRUE;
    }
    LeaveCriticalSection(&v9x_gl_lock);
    v9x_gl_log3("DrvDeleteContext context=%lu -> %lu", handle, (DWORD)ok, 0ul);
    return ok;
}

V9X_GLCLTPROCTABLE * __stdcall DrvSetContext(HDC hdc, V9X_DHGLRC handle,
                                             V9X_PFN_SETPROCTABLE set_table)
{
    V9X_GL_CONTEXT *context;
    V9X_GL_CONTEXT *previous = v9x_gl_current();
    V9X_GLCLTPROCTABLE *table = 0;
    DWORD thread = GetCurrentThreadId();
    HWND window = WindowFromDC(hdc);

    (void)set_table;
    EnterCriticalSection(&v9x_gl_lock);
    context = v9x_gl_context_of(handle);
    if (context != 0 && (context->owner == 0ul || context->owner == thread) &&
        window != 0) {
        v9x_gl_windows[context - v9x_gl_contexts] = window;
        if (v9x_gl_bind_window(context, window) != 0) {
            if (previous != 0 && previous != context) {
                previous->owner = 0ul;
            }
            context->owner = thread;
            TlsSetValue(v9x_gl_tls, context);
            v9x_gl_install_overrides();
            table = &v9x_gl_table;
        }
    }
    LeaveCriticalSection(&v9x_gl_lock);
    v9x_gl_log3("DrvSetContext hdc=%08lX context=%lu -> %lu", (DWORD)hdc,
                handle, (DWORD)(table != 0));
    return table;
}

BOOL __stdcall DrvReleaseContext(V9X_DHGLRC handle)
{
    V9X_GL_CONTEXT *context;
    BOOL ok = FALSE;

    EnterCriticalSection(&v9x_gl_lock);
    context = v9x_gl_context_of(handle);
    if (context != 0 && context->owner == GetCurrentThreadId()) {
        context->owner = 0ul;
        TlsSetValue(v9x_gl_tls, 0);
        ok = TRUE;
    }
    LeaveCriticalSection(&v9x_gl_lock);
    v9x_gl_log3("DrvReleaseContext context=%lu -> %lu", handle, (DWORD)ok,
                0ul);
    return ok;
}

/* Not yet: state copies and shared lists arrive with the state and texture
 * objects they would copy and share (plan, Phases 4 and 6). Declined, not
 * falsely accepted. */
BOOL __stdcall DrvCopyContext(V9X_DHGLRC source, V9X_DHGLRC target, UINT mask)
{
    v9x_gl_log3("DrvCopyContext source=%lu target=%lu mask=%08lX -> 0",
                source, target, (DWORD)mask);
    return FALSE;
}

BOOL __stdcall DrvShareLists(V9X_DHGLRC first, V9X_DHGLRC second)
{
    v9x_gl_log3("DrvShareLists first=%lu second=%lu -> 0", first, second, 0ul);
    return FALSE;
}

BOOL __stdcall DrvSwapBuffers(HDC hdc)
{
    V9X_GL_DRAWABLE *drawable;
    BOOL ok = FALSE;

    EnterCriticalSection(&v9x_gl_lock);
    drawable = v9x_gl_drawable_find(WindowFromDC(hdc));
    if (drawable != 0) {
        /* OPENGL32 has already called glFinish (Phase 0.9), so everything
         * drawn is complete before the Blt reads it. */
        ok = v9x_gl_drawable_present(drawable) ? TRUE : FALSE;
    }
    LeaveCriticalSection(&v9x_gl_lock);
    if (!ok) {
        v9x_gl_log3("DrvSwapBuffers hdc=%08lX -> 0", (DWORD)hdc, 0ul, 0ul);
    }
    return ok;
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
    (void)hdc;
    (void)index;
    (void)layer;
    (void)bytes;
    (void)descriptor;
    return FALSE;
}

INT __stdcall DrvSetLayerPaletteEntries(HDC hdc, INT layer, INT start,
                                        INT count, const COLORREF *entries)
{
    (void)hdc;
    (void)layer;
    (void)start;
    (void)count;
    (void)entries;
    return 0;
}

INT __stdcall DrvGetLayerPaletteEntries(HDC hdc, INT layer, INT start,
                                        INT count, COLORREF *entries)
{
    (void)hdc;
    (void)layer;
    (void)start;
    (void)count;
    (void)entries;
    return 0;
}

BOOL __stdcall DrvRealizeLayerPalette(HDC hdc, INT layer, BOOL realize)
{
    (void)hdc;
    (void)layer;
    (void)realize;
    return FALSE;
}

/* No extension is offered yet, so no extension entry point exists. */
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
        InitializeCriticalSection(&v9x_gl_lock);
        v9x_gl_tls = TlsAlloc();
        v9x_gl_log3("attach instance=%08lX tls=%lu build-marker=%lu",
                    (DWORD)instance, v9x_gl_tls,
                    (DWORD)(v9x_gl_build_id[0] != '\0'));
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
        v9x_gl_log3("detach slots-called=%lu stub-calls=%lu", distinct, total,
                    0ul);
        if (v9x_gl_tls != 0xfffffffful) {
            TlsFree(v9x_gl_tls);
        }
        DeleteCriticalSection(&v9x_gl_lock);
    }
    return TRUE;
}
