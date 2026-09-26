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
#include "gl_prim.h"
#include "gl_texture.h"
#include "gl_get.h"
#include "gl_pixels.h"

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
    V9X_GL_PIPELINE pipeline;
    V9X_GL_TEXTURES textures;
    /* The levels a batch's texture names, valid for the draw call. */
    V9X_R3D_ABI_LEVEL levels[V9X_GL_TEXTURE_LEVELS];
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

/*
 * glReadPixels (4.3.2) from the back buffer. glEnd hands every batched
 * triangle to the interface and the call is an error inside glBegin, so
 * nothing is held here; finish makes the submitted work complete, and the
 * Lock's HAL side drains again before the CPU reads. READ_BUFFER FRONT
 * reads the back buffer too: after SwapBuffers the two hold the same
 * image, and the window's pixels on the primary may be covered.
 */
static void V9X_GL_API v9x_gl_read_pixels(GLint x, GLint y, GLsizei width,
                                          GLsizei height, GLenum format,
                                          GLenum type, GLvoid *pixels)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();
    const V9X_R3D_INTERFACE *iface = v9x_gl_device_interface();
    const V9X_R3D_ABI_DESCRIBE *description = v9x_gl_device_description();
    V9X_GL_DRAWABLE *drawable;
    V9X_GL_READ_PLAN plan;
    const void *surface;
    v9x_u32 pitch;
    v9x_u32 surface_width;
    v9x_u32 surface_height;
    v9x_u32 result;

    if (context == 0 || iface == 0) {
        return;
    }
    if (!v9x_gl_read_plan(&context->state, &context->textures, x, y, width,
                          height, format, type, &plan)) {
        return;
    }
    EnterCriticalSection(&v9x_gl_lock);
    drawable = v9x_gl_bind_window(context, v9x_gl_context_window(context));
    if (drawable == 0) {
        v9x_gl_state_error(&context->state, V9X_GL_OUT_OF_MEMORY);
        LeaveCriticalSection(&v9x_gl_lock);
        return;
    }
    result = iface->finish(description->generation);
    if (result != V9X_R3D_RESULT_OK) {
        v9x_gl_log3("glReadPixels finish result=%lu", result, 0ul, 0ul);
    }
    if (!v9x_gl_drawable_lock(drawable, &surface, &pitch)) {
        LeaveCriticalSection(&v9x_gl_lock);
        return;
    }
    v9x_gl_drawable_size(drawable, &surface_width, &surface_height);
    v9x_gl_read_convert(&plan, surface, pitch, surface_width, surface_height,
                        v9x_gl_device_format(), pixels);
    v9x_gl_drawable_unlock(drawable);
    LeaveCriticalSection(&v9x_gl_lock);
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

/* ---- Matrix commands (2.10.2), through gl_matrix.c ------------------ */

#define V9X_GL_WITH_CONTEXT(call) do { \
    V9X_GL_CONTEXT *context_ = v9x_gl_current(); \
    if (context_ != 0) { \
        call; \
    } \
} while (0)

static void V9X_GL_API v9x_gl_matrix_mode(GLenum mode)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_matrix_mode(&context_->state, mode));
}

static void V9X_GL_API v9x_gl_load_identity(void)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_load_identity(&context_->state));
}

static void V9X_GL_API v9x_gl_load_matrixf(const GLfloat *m)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_load_matrix(&context_->state, m));
}

static void V9X_GL_API v9x_gl_mult_matrixf(const GLfloat *m)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_mult_matrix(&context_->state, m));
}

/* The double forms convert on entry: the stacks are single precision, which
 * is what the specification's implementation latitude allows (2.10.2). */
static void v9x_gl_to_floats(const GLdouble *in, GLfloat *out)
{
    unsigned int index;

    for (index = 0u; index < 16u; ++index) {
        out[index] = (GLfloat)in[index];
    }
}

static void V9X_GL_API v9x_gl_load_matrixd(const GLdouble *m)
{
    GLfloat f[16];

    v9x_gl_to_floats(m, f);
    V9X_GL_WITH_CONTEXT(v9x_gl_state_load_matrix(&context_->state, f));
}

static void V9X_GL_API v9x_gl_mult_matrixd(const GLdouble *m)
{
    GLfloat f[16];

    v9x_gl_to_floats(m, f);
    V9X_GL_WITH_CONTEXT(v9x_gl_state_mult_matrix(&context_->state, f));
}

static void V9X_GL_API v9x_gl_translatef(GLfloat x, GLfloat y, GLfloat z)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_translate(&context_->state, x, y, z));
}

static void V9X_GL_API v9x_gl_translated(GLdouble x, GLdouble y, GLdouble z)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_translate(&context_->state, (GLfloat)x,
                                               (GLfloat)y, (GLfloat)z));
}

static void V9X_GL_API v9x_gl_scalef(GLfloat x, GLfloat y, GLfloat z)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_scale(&context_->state, x, y, z));
}

static void V9X_GL_API v9x_gl_scaled(GLdouble x, GLdouble y, GLdouble z)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_scale(&context_->state, (GLfloat)x,
                                           (GLfloat)y, (GLfloat)z));
}

static void V9X_GL_API v9x_gl_rotatef(GLfloat angle, GLfloat x, GLfloat y,
                                      GLfloat z)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_rotate(&context_->state, angle, x, y,
                                            z));
}

static void V9X_GL_API v9x_gl_rotated(GLdouble angle, GLdouble x,
                                      GLdouble y, GLdouble z)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_rotate(&context_->state, (GLfloat)angle,
                                            (GLfloat)x, (GLfloat)y,
                                            (GLfloat)z));
}

static void V9X_GL_API v9x_gl_frustum(GLdouble left, GLdouble right,
                                      GLdouble bottom, GLdouble top,
                                      GLdouble near_plane, GLdouble far_plane)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_frustum(&context_->state, left, right,
                                             bottom, top, near_plane,
                                             far_plane));
}

static void V9X_GL_API v9x_gl_ortho(GLdouble left, GLdouble right,
                                    GLdouble bottom, GLdouble top,
                                    GLdouble near_plane, GLdouble far_plane)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_ortho(&context_->state, left, right,
                                           bottom, top, near_plane,
                                           far_plane));
}

static void V9X_GL_API v9x_gl_push_matrix(void)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_push_matrix(&context_->state));
}

static void V9X_GL_API v9x_gl_pop_matrix(void)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_pop_matrix(&context_->state));
}

/* ---- Textures (3.8), through gl_texture.c --------------------------- */

/* A float as a whole number, rounded to nearest, without the C runtime a
 * cast would call (__CHP): the float forms of the integer commands carry
 * enums and counts as whole numbers (2.3). */
static long v9x_gl_whole(GLfloat value);
#pragma aux v9x_gl_whole =     "sub esp,4"     "fistp dword ptr [esp]"     "pop eax"     parm [8087] value [eax] modify exact [eax];

/* The texture tables' storage: the process heap, which the ICD owns. */
static void *v9x_gl_heap_alloc(v9x_u32 bytes)
{
    return HeapAlloc(GetProcessHeap(), 0, bytes);
}

static void v9x_gl_heap_free(void *memory)
{
    HeapFree(GetProcessHeap(), 0, memory);
}

#define V9X_GL_WITH_TEXTURES(call) do { \
    V9X_GL_CONTEXT *context_ = v9x_gl_current(); \
    if (context_ != 0) { \
        call; \
    } \
} while (0)

static void V9X_GL_API v9x_gl_gen_textures(GLsizei n, GLuint *names)
{
    V9X_GL_WITH_TEXTURES(v9x_gl_tex_gen(&context_->state, &context_->textures,
                                        n, names));
}

static void V9X_GL_API v9x_gl_delete_textures(GLsizei n, const GLuint *names)
{
    V9X_GL_WITH_TEXTURES(v9x_gl_tex_delete(&context_->state,
                                           &context_->textures, n, names));
}

static GLboolean V9X_GL_API v9x_gl_is_texture(GLuint name)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    return context != 0 ? v9x_gl_tex_is(&context->state, &context->textures,
                                        name)
                        : 0;
}

static void V9X_GL_API v9x_gl_bind_texture(GLenum target, GLuint name)
{
    V9X_GL_WITH_TEXTURES(v9x_gl_tex_bind(&context_->state,
                                         &context_->textures, target, name));
}

static void V9X_GL_API v9x_gl_tex_parameteri(GLenum target, GLenum pname,
                                             GLint value)
{
    V9X_GL_WITH_TEXTURES(v9x_gl_tex_parameter(&context_->state,
                                              &context_->textures, target,
                                              pname, value));
}

/* The float and vector forms carry enums as whole numbers (2.3). */
static void V9X_GL_API v9x_gl_tex_parameterf(GLenum target, GLenum pname,
                                             GLfloat value)
{
    v9x_gl_tex_parameteri(target, pname, (GLint)v9x_gl_whole(value));
}

static void V9X_GL_API v9x_gl_tex_parameteriv(GLenum target, GLenum pname,
                                              const GLint *values)
{
    v9x_gl_tex_parameteri(target, pname, values[0]);
}

static void V9X_GL_API v9x_gl_tex_parameterfv(GLenum target, GLenum pname,
                                              const GLfloat *values)
{
    v9x_gl_tex_parameteri(target, pname, (GLint)v9x_gl_whole(values[0]));
}

static void V9X_GL_API v9x_gl_tex_envfv(GLenum target, GLenum pname,
                                        const GLfloat *values)
{
    V9X_GL_WITH_TEXTURES(v9x_gl_tex_env(&context_->state,
                                        &context_->textures, target, pname,
                                        values));
}

static void V9X_GL_API v9x_gl_tex_envf(GLenum target, GLenum pname,
                                       GLfloat value)
{
    GLfloat values[4];

    values[0] = value;
    values[1] = 0.0f;
    values[2] = 0.0f;
    values[3] = 0.0f;
    v9x_gl_tex_envfv(target, pname, values);
}

static void V9X_GL_API v9x_gl_tex_envi(GLenum target, GLenum pname,
                                       GLint value)
{
    v9x_gl_tex_envf(target, pname, (GLfloat)value);
}

/* Integer colours map to [0,1] linearly from the full range (2.3); an
 * integer mode is the enum itself. */
static void V9X_GL_API v9x_gl_tex_enviv(GLenum target, GLenum pname,
                                        const GLint *values)
{
    GLfloat converted[4];
    unsigned int i;

    for (i = 0u; i < 4u; ++i) {
        converted[i] = pname == V9X_GL_TEXTURE_ENV_COLOR
            ? (GLfloat)((double)values[i] / 2147483647.0)
            : (GLfloat)values[i];
        if (pname != V9X_GL_TEXTURE_ENV_COLOR) {
            break;
        }
    }
    v9x_gl_tex_envfv(target, pname, converted);
}

static void V9X_GL_API v9x_gl_pixel_storei(GLenum pname, GLint value)
{
    V9X_GL_WITH_TEXTURES(v9x_gl_pixel_store(&context_->state,
                                            &context_->textures, pname,
                                            value));
}

static void V9X_GL_API v9x_gl_pixel_storef(GLenum pname, GLfloat value)
{
    v9x_gl_pixel_storei(pname, (GLint)v9x_gl_whole(value));
}

static void V9X_GL_API v9x_gl_api_tex_image_2d(GLenum target, GLint level,
                                           GLint internal_format,
                                           GLsizei width, GLsizei height,
                                           GLint border, GLenum format,
                                           GLenum type, const GLvoid *pixels)
{
    V9X_GL_WITH_TEXTURES(v9x_gl_tex_image_2d(&context_->state,
                                             &context_->textures, target,
                                             level, internal_format, width,
                                             height, border, format, type,
                                             pixels));
}

static void V9X_GL_API v9x_gl_api_tex_sub_image_2d(GLenum target, GLint level,
                                               GLint xoffset, GLint yoffset,
                                               GLsizei width, GLsizei height,
                                               GLenum format, GLenum type,
                                               const GLvoid *pixels)
{
    V9X_GL_WITH_TEXTURES(v9x_gl_tex_sub_image_2d(&context_->state,
                                                 &context_->textures, target,
                                                 level, xoffset, yoffset,
                                                 width, height, format, type,
                                                 pixels));
}

/* ---- Geometry (2.6-2.11), through gl_prim.c ------------------------- */

/*
 * The pipeline's sink: one batch of up to 64 screen-space triangles to the
 * render interface, drawn into the context's back and depth buffers with
 * its fragment state. A batch only ever holds triangles from one
 * glBegin/glEnd, and no state command is legal between those, so the state
 * read here is the state the triangles were made with.
 */
static int v9x_gl_draw_batch(void *user, const V9X_R3D_ABI_VERTEX *vertices,
                             v9x_u32 triangle_count)
{
    V9X_GL_CONTEXT *context = (V9X_GL_CONTEXT *)user;
    const V9X_R3D_INTERFACE *iface = v9x_gl_device_interface();
    const V9X_R3D_ABI_DESCRIBE *description = v9x_gl_device_description();
    V9X_GL_DRAWABLE *drawable;
    V9X_R3D_ABI_DRAW draw;
    V9X_R3D_ABI_OUTCOME outcome;
    v9x_u32 result;
    unsigned int i;

    if (iface == 0) {
        return 0;
    }
    EnterCriticalSection(&v9x_gl_lock);
    drawable = v9x_gl_bind_window(context, v9x_gl_context_window(context));
    if (drawable == 0) {
        LeaveCriticalSection(&v9x_gl_lock);
        return 0;
    }
    for (i = 0u; i < sizeof(draw); ++i) {
        ((BYTE *)&draw)[i] = 0u;
    }
    draw.struct_bytes = sizeof(draw);
    draw.generation = description->generation;
    draw.target.surface = v9x_gl_drawable_back(drawable);
    draw.depth.surface = v9x_gl_drawable_depth(drawable);
    v9x_gl_tex_describe(&context->state, &context->textures, &draw.texture,
                        context->levels);
    v9x_gl_prim_abi_state(&context->state, &context->pipeline, &draw.state);
    draw.vertices = vertices;
    draw.triangle_count = triangle_count;
    result = iface->draw(&draw, &outcome);
    if (result == V9X_R3D_RESULT_STALE && v9x_gl_device_redescribe()) {
        drawable = v9x_gl_bind_window(context,
                                      v9x_gl_context_window(context));
        if (drawable != 0) {
            draw.generation = description->generation;
            draw.target.surface = v9x_gl_drawable_back(drawable);
            draw.depth.surface = v9x_gl_drawable_depth(drawable);
            result = iface->draw(&draw, &outcome);
        }
    }
    LeaveCriticalSection(&v9x_gl_lock);
    if (result != V9X_R3D_RESULT_OK) {
        v9x_gl_log3("draw result=%lu triangles=%lu submitted=%lu", result,
                    triangle_count, outcome.submitted);
        return 0;
    }
    return 1;
}

#define V9X_GL_WITH_PIPELINE(call) do { \
    V9X_GL_CONTEXT *context_ = v9x_gl_current(); \
    if (context_ != 0) { \
        call; \
    } \
} while (0)

static void V9X_GL_API v9x_gl_begin(GLenum mode)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_begin(&context_->state,
                                           &context_->pipeline, mode));
}

static void V9X_GL_API v9x_gl_end(void)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_end(&context_->state,
                                         &context_->pipeline));
}

static void v9x_gl_vertex4(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_vertex(&context_->state,
                                            &context_->pipeline, x, y, z,
                                            w));
}

static void V9X_GL_API v9x_gl_vertex2f(GLfloat x, GLfloat y)
{
    v9x_gl_vertex4(x, y, 0.0f, 1.0f);
}

static void V9X_GL_API v9x_gl_vertex2fv(const GLfloat *v)
{
    v9x_gl_vertex4(v[0], v[1], 0.0f, 1.0f);
}

static void V9X_GL_API v9x_gl_vertex2i(GLint x, GLint y)
{
    v9x_gl_vertex4((GLfloat)x, (GLfloat)y, 0.0f, 1.0f);
}

static void V9X_GL_API v9x_gl_vertex2d(GLdouble x, GLdouble y)
{
    v9x_gl_vertex4((GLfloat)x, (GLfloat)y, 0.0f, 1.0f);
}

static void V9X_GL_API v9x_gl_vertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    v9x_gl_vertex4(x, y, z, 1.0f);
}

static void V9X_GL_API v9x_gl_vertex3fv(const GLfloat *v)
{
    v9x_gl_vertex4(v[0], v[1], v[2], 1.0f);
}

static void V9X_GL_API v9x_gl_vertex3i(GLint x, GLint y, GLint z)
{
    v9x_gl_vertex4((GLfloat)x, (GLfloat)y, (GLfloat)z, 1.0f);
}

static void V9X_GL_API v9x_gl_vertex3d(GLdouble x, GLdouble y, GLdouble z)
{
    v9x_gl_vertex4((GLfloat)x, (GLfloat)y, (GLfloat)z, 1.0f);
}

static void V9X_GL_API v9x_gl_vertex4f(GLfloat x, GLfloat y, GLfloat z,
                                       GLfloat w)
{
    v9x_gl_vertex4(x, y, z, w);
}

static void V9X_GL_API v9x_gl_vertex4fv(const GLfloat *v)
{
    v9x_gl_vertex4(v[0], v[1], v[2], v[3]);
}

static void v9x_gl_color4(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_color(&context_->pipeline, r, g, b, a));
}

/* Unsigned bytes map c / 255 (table 2.6). */
#define V9X_GL_UB(value) ((GLfloat)(value) * (1.0f / 255.0f))

static void V9X_GL_API v9x_gl_color3f(GLfloat r, GLfloat g, GLfloat b)
{
    v9x_gl_color4(r, g, b, 1.0f);
}

static void V9X_GL_API v9x_gl_color3fv(const GLfloat *v)
{
    v9x_gl_color4(v[0], v[1], v[2], 1.0f);
}

static void V9X_GL_API v9x_gl_color3d(GLdouble r, GLdouble g, GLdouble b)
{
    v9x_gl_color4((GLfloat)r, (GLfloat)g, (GLfloat)b, 1.0f);
}

static void V9X_GL_API v9x_gl_color4f(GLfloat r, GLfloat g, GLfloat b,
                                      GLfloat a)
{
    v9x_gl_color4(r, g, b, a);
}

static void V9X_GL_API v9x_gl_color4fv(const GLfloat *v)
{
    v9x_gl_color4(v[0], v[1], v[2], v[3]);
}

static void V9X_GL_API v9x_gl_color3ub(GLubyte r, GLubyte g, GLubyte b)
{
    v9x_gl_color4(V9X_GL_UB(r), V9X_GL_UB(g), V9X_GL_UB(b), 1.0f);
}

static void V9X_GL_API v9x_gl_color3ubv(const GLubyte *v)
{
    v9x_gl_color4(V9X_GL_UB(v[0]), V9X_GL_UB(v[1]), V9X_GL_UB(v[2]), 1.0f);
}

static void V9X_GL_API v9x_gl_color4ub(GLubyte r, GLubyte g, GLubyte b,
                                       GLubyte a)
{
    v9x_gl_color4(V9X_GL_UB(r), V9X_GL_UB(g), V9X_GL_UB(b), V9X_GL_UB(a));
}

static void V9X_GL_API v9x_gl_color4ubv(const GLubyte *v)
{
    v9x_gl_color4(V9X_GL_UB(v[0]), V9X_GL_UB(v[1]), V9X_GL_UB(v[2]),
                  V9X_GL_UB(v[3]));
}

static void V9X_GL_API v9x_gl_texcoord2f(GLfloat s, GLfloat t)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_texcoord(&context_->pipeline, s, t,
                                              0.0f, 1.0f));
}

static void V9X_GL_API v9x_gl_texcoord2fv(const GLfloat *v)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_texcoord(&context_->pipeline, v[0], v[1],
                                              0.0f, 1.0f));
}

static void V9X_GL_API v9x_gl_shade_model(GLenum mode)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_shade_model(&context_->state,
                                                 &context_->pipeline, mode));
}

static void V9X_GL_API v9x_gl_cull_face(GLenum mode)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_cull_face(&context_->state,
                                               &context_->pipeline, mode));
}

static void V9X_GL_API v9x_gl_front_face(GLenum mode)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_front_face(&context_->state,
                                                &context_->pipeline, mode));
}

static void V9X_GL_API v9x_gl_depth_func(GLenum func)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_depth_func(&context_->state,
                                                &context_->pipeline, func));
}

static void V9X_GL_API v9x_gl_blend_func(GLenum src, GLenum dst)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_blend_func(&context_->state,
                                                &context_->pipeline, src,
                                                dst));
}

static void V9X_GL_API v9x_gl_alpha_func(GLenum func, GLclampf ref)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_alpha_func(&context_->state,
                                                &context_->pipeline, func,
                                                ref));
}

static void V9X_GL_API v9x_gl_depth_range(GLclampd near_value,
                                          GLclampd far_value)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_depth_range(&context_->state,
                                                 &context_->pipeline,
                                                 near_value, far_value));
}

/* ---- Queries and held state (6.1, 5.6, 3.5.4, 4.2.1), gl_get.c ------- */

static void v9x_gl_get_any(GLenum pname, int kind, void *out)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0) {
        v9x_gl_get(&context->state, &context->pipeline, &context->textures,
                   pname, kind, out);
    }
}

static void V9X_GL_API v9x_gl_get_booleanv(GLenum pname, GLboolean *out)
{
    v9x_gl_get_any(pname, V9X_GL_GET_BOOLEAN, out);
}

static void V9X_GL_API v9x_gl_get_integerv(GLenum pname, GLint *out)
{
    v9x_gl_get_any(pname, V9X_GL_GET_INTEGER, out);
}

static void V9X_GL_API v9x_gl_get_floatv(GLenum pname, GLfloat *out)
{
    v9x_gl_get_any(pname, V9X_GL_GET_FLOAT, out);
}

static void V9X_GL_API v9x_gl_get_doublev(GLenum pname, GLdouble *out)
{
    v9x_gl_get_any(pname, V9X_GL_GET_DOUBLE, out);
}

static void V9X_GL_API v9x_gl_hint(GLenum target, GLenum mode)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_hint(&context_->state, target, mode));
}

static void V9X_GL_API v9x_gl_polygon_mode(GLenum face, GLenum mode)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_polygon_mode(&context_->state, face,
                                                  mode));
}

static void V9X_GL_API v9x_gl_draw_buffer(GLenum buffer)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_draw_buffer(&context_->state, buffer));
}

static void V9X_GL_API v9x_gl_read_buffer(GLenum buffer)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_read_buffer(&context_->state, buffer));
}

static void V9X_GL_API v9x_gl_line_width(GLfloat width)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_line_width(&context_->state, width));
}

static void V9X_GL_API v9x_gl_point_size(GLfloat size)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_point_size(&context_->state, size));
}

/* ---- The table ----------------------------------------------------- */

static void v9x_gl_set_slot(const char *name, V9X_GL_PROC proc)
{
    unsigned int slot;

    for (slot = 0u; slot < V9X_GL_SLOT_COUNT; ++slot) {
        if (lstrcmpA(v9x_gl_slot_names[slot], name) == 0) {
            v9x_gl_table.entries[slot] = proc;
            return;
        }
    }
    v9x_gl_log(name);
}

/* Each override is assigned to a local of its slot's generated type before
 * it is stored, so a signature that does not match the slot - the stack
 * cleanup a caller relies on - cannot compile. */
#define V9X_GL_OVERRIDE(slot_name, function) do { \
    V9X_GL_PFN_##slot_name typed_ = function; \
    v9x_gl_set_slot(#slot_name, (V9X_GL_PROC)typed_); \
} while (0)

static void v9x_gl_install_overrides(void)
{
    if (v9x_gl_overrides_installed) {
        return;
    }
    v9x_gl_overrides_installed = 1;
    V9X_GL_OVERRIDE(glGetString, v9x_gl_get_string);
    V9X_GL_OVERRIDE(glGetError, v9x_gl_get_error);
    V9X_GL_OVERRIDE(glClearColor, v9x_gl_clear_color);
    V9X_GL_OVERRIDE(glClearDepth, v9x_gl_clear_depth);
    V9X_GL_OVERRIDE(glViewport, v9x_gl_viewport);
    V9X_GL_OVERRIDE(glScissor, v9x_gl_scissor);
    V9X_GL_OVERRIDE(glColorMask, v9x_gl_color_mask);
    V9X_GL_OVERRIDE(glDepthMask, v9x_gl_depth_mask);
    V9X_GL_OVERRIDE(glEnable, v9x_gl_enable);
    V9X_GL_OVERRIDE(glDisable, v9x_gl_disable);
    V9X_GL_OVERRIDE(glIsEnabled, v9x_gl_is_enabled);
    V9X_GL_OVERRIDE(glClear, v9x_gl_clear);
    V9X_GL_OVERRIDE(glFinish, v9x_gl_finish);
    V9X_GL_OVERRIDE(glFlush, v9x_gl_flush);
    V9X_GL_OVERRIDE(glReadPixels, v9x_gl_read_pixels);
    V9X_GL_OVERRIDE(glMatrixMode, v9x_gl_matrix_mode);
    V9X_GL_OVERRIDE(glLoadIdentity, v9x_gl_load_identity);
    V9X_GL_OVERRIDE(glLoadMatrixf, v9x_gl_load_matrixf);
    V9X_GL_OVERRIDE(glLoadMatrixd, v9x_gl_load_matrixd);
    V9X_GL_OVERRIDE(glMultMatrixf, v9x_gl_mult_matrixf);
    V9X_GL_OVERRIDE(glMultMatrixd, v9x_gl_mult_matrixd);
    V9X_GL_OVERRIDE(glTranslatef, v9x_gl_translatef);
    V9X_GL_OVERRIDE(glTranslated, v9x_gl_translated);
    V9X_GL_OVERRIDE(glScalef, v9x_gl_scalef);
    V9X_GL_OVERRIDE(glScaled, v9x_gl_scaled);
    V9X_GL_OVERRIDE(glRotatef, v9x_gl_rotatef);
    V9X_GL_OVERRIDE(glRotated, v9x_gl_rotated);
    V9X_GL_OVERRIDE(glFrustum, v9x_gl_frustum);
    V9X_GL_OVERRIDE(glOrtho, v9x_gl_ortho);
    V9X_GL_OVERRIDE(glPushMatrix, v9x_gl_push_matrix);
    V9X_GL_OVERRIDE(glPopMatrix, v9x_gl_pop_matrix);
    V9X_GL_OVERRIDE(glBegin, v9x_gl_begin);
    V9X_GL_OVERRIDE(glEnd, v9x_gl_end);
    V9X_GL_OVERRIDE(glVertex2f, v9x_gl_vertex2f);
    V9X_GL_OVERRIDE(glVertex2fv, v9x_gl_vertex2fv);
    V9X_GL_OVERRIDE(glVertex2i, v9x_gl_vertex2i);
    V9X_GL_OVERRIDE(glVertex2d, v9x_gl_vertex2d);
    V9X_GL_OVERRIDE(glVertex3f, v9x_gl_vertex3f);
    V9X_GL_OVERRIDE(glVertex3fv, v9x_gl_vertex3fv);
    V9X_GL_OVERRIDE(glVertex3i, v9x_gl_vertex3i);
    V9X_GL_OVERRIDE(glVertex3d, v9x_gl_vertex3d);
    V9X_GL_OVERRIDE(glVertex4f, v9x_gl_vertex4f);
    V9X_GL_OVERRIDE(glVertex4fv, v9x_gl_vertex4fv);
    V9X_GL_OVERRIDE(glColor3f, v9x_gl_color3f);
    V9X_GL_OVERRIDE(glColor3fv, v9x_gl_color3fv);
    V9X_GL_OVERRIDE(glColor3d, v9x_gl_color3d);
    V9X_GL_OVERRIDE(glColor4f, v9x_gl_color4f);
    V9X_GL_OVERRIDE(glColor4fv, v9x_gl_color4fv);
    V9X_GL_OVERRIDE(glColor3ub, v9x_gl_color3ub);
    V9X_GL_OVERRIDE(glColor3ubv, v9x_gl_color3ubv);
    V9X_GL_OVERRIDE(glColor4ub, v9x_gl_color4ub);
    V9X_GL_OVERRIDE(glColor4ubv, v9x_gl_color4ubv);
    V9X_GL_OVERRIDE(glTexCoord2f, v9x_gl_texcoord2f);
    V9X_GL_OVERRIDE(glTexCoord2fv, v9x_gl_texcoord2fv);
    V9X_GL_OVERRIDE(glShadeModel, v9x_gl_shade_model);
    V9X_GL_OVERRIDE(glCullFace, v9x_gl_cull_face);
    V9X_GL_OVERRIDE(glFrontFace, v9x_gl_front_face);
    V9X_GL_OVERRIDE(glDepthFunc, v9x_gl_depth_func);
    V9X_GL_OVERRIDE(glBlendFunc, v9x_gl_blend_func);
    V9X_GL_OVERRIDE(glAlphaFunc, v9x_gl_alpha_func);
    V9X_GL_OVERRIDE(glDepthRange, v9x_gl_depth_range);
    V9X_GL_OVERRIDE(glGenTextures, v9x_gl_gen_textures);
    V9X_GL_OVERRIDE(glDeleteTextures, v9x_gl_delete_textures);
    V9X_GL_OVERRIDE(glIsTexture, v9x_gl_is_texture);
    V9X_GL_OVERRIDE(glBindTexture, v9x_gl_bind_texture);
    V9X_GL_OVERRIDE(glTexParameteri, v9x_gl_tex_parameteri);
    V9X_GL_OVERRIDE(glTexParameterf, v9x_gl_tex_parameterf);
    V9X_GL_OVERRIDE(glTexParameteriv, v9x_gl_tex_parameteriv);
    V9X_GL_OVERRIDE(glTexParameterfv, v9x_gl_tex_parameterfv);
    V9X_GL_OVERRIDE(glTexEnvf, v9x_gl_tex_envf);
    V9X_GL_OVERRIDE(glTexEnvi, v9x_gl_tex_envi);
    V9X_GL_OVERRIDE(glTexEnvfv, v9x_gl_tex_envfv);
    V9X_GL_OVERRIDE(glTexEnviv, v9x_gl_tex_enviv);
    V9X_GL_OVERRIDE(glPixelStorei, v9x_gl_pixel_storei);
    V9X_GL_OVERRIDE(glPixelStoref, v9x_gl_pixel_storef);
    V9X_GL_OVERRIDE(glTexImage2D, v9x_gl_api_tex_image_2d);
    V9X_GL_OVERRIDE(glTexSubImage2D, v9x_gl_api_tex_sub_image_2d);
    V9X_GL_OVERRIDE(glGetBooleanv, v9x_gl_get_booleanv);
    V9X_GL_OVERRIDE(glGetIntegerv, v9x_gl_get_integerv);
    V9X_GL_OVERRIDE(glGetFloatv, v9x_gl_get_floatv);
    V9X_GL_OVERRIDE(glGetDoublev, v9x_gl_get_doublev);
    V9X_GL_OVERRIDE(glHint, v9x_gl_hint);
    V9X_GL_OVERRIDE(glPolygonMode, v9x_gl_polygon_mode);
    V9X_GL_OVERRIDE(glDrawBuffer, v9x_gl_draw_buffer);
    V9X_GL_OVERRIDE(glReadBuffer, v9x_gl_read_buffer);
    V9X_GL_OVERRIDE(glLineWidth, v9x_gl_line_width);
    V9X_GL_OVERRIDE(glPointSize, v9x_gl_point_size);
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
                v9x_gl_pipeline_init(&context->pipeline);
                v9x_gl_textures_init(&context->textures, v9x_gl_heap_alloc,
                                     v9x_gl_heap_free);
                v9x_gl_pipeline_sink(&context->pipeline, v9x_gl_draw_batch,
                                     context);
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
        v9x_gl_textures_release(&context->textures);
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
