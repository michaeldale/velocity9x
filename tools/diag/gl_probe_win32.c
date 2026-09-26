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

/* One pixel through glReadPixels as 0xRRGGBB. GL rows count up from the
 * bottom, so (x, y) is in window coordinates, not GDI's. */
static DWORD v9x_glp_read(GLint x, GLint y)
{
    GLubyte rgb[4];

    rgb[0] = rgb[1] = rgb[2] = rgb[3] = 0xAAu;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    return ((DWORD)rgb[0] << 16) | ((DWORD)rgb[1] << 8) | (DWORD)rgb[2];
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
        /*
         * What reached the screen, read back through GDI from the window:
         * the magenta clear, then a scissored green clear over the lower-
         * left quarter, which must land there (window y is up) and not in
         * the upper right. And a slot not yet implemented must say so with
         * INVALID_OPERATION rather than succeed silently.
         */
        {
            RECT client;
            LONG width;
            LONG height;

            v9x_glp_pump();
            GetClientRect(window, &client);
            width = client.right - client.left;
            height = client.bottom - client.top;
            v9x_glp_hex("ClearPixel", (DWORD)GetPixel(hdc, 4, 4));
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, width / 2, height / 2);
            glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_SCISSOR_TEST);
            v9x_glp_hex("ErrorAfterScissorClear", (DWORD)glGetError());
            glFinish();
            SwapBuffers(hdc);
            v9x_glp_pump();
            v9x_glp_hex("ScissorInsidePixel",
                        (DWORD)GetPixel(hdc, 4, height - 5));
            v9x_glp_hex("ScissorOutsidePixel",
                        (DWORD)GetPixel(hdc, width - 5, 4));
            /* The same two pixels from the back buffer through
             * glReadPixels: green at the bottom left, magenta at the top
             * right, or the rows are flipped. */
            v9x_glp_hex("ReadScissorInside", v9x_glp_read(4, 4));
            v9x_glp_hex("ReadScissorOutside",
                        v9x_glp_read(width - 5, height - 5));
            v9x_glp_hex("ErrorAfterRead", (DWORD)glGetError());
            glBegin(GL_POINTS);
            glEnd();
            glPushAttrib(GL_ALL_ATTRIB_BITS);   /* Phase 6: still a stub */
            v9x_glp_hex("ErrorAfterStub", (DWORD)glGetError());

            /*
             * Geometry: an ortho projection onto the window, depth test
             * on. A green quad at window depth 0.5 over everything; a blue
             * one at 0.75 over the left half, which LESS must reject; a
             * red one at 0.25 over the right half, which must draw. Read
             * back from the screen after the swap.
             */
            glViewport(0, 0, width, height);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(0.0, (GLdouble)width, 0.0, (GLdouble)height, -1.0, 1.0);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClearDepth(1.0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glBegin(GL_QUADS);
            glColor3f(0.0f, 1.0f, 0.0f);
            glVertex3f(0.0f, 0.0f, 0.0f);
            glVertex3f((GLfloat)width, 0.0f, 0.0f);
            glVertex3f((GLfloat)width, (GLfloat)height, 0.0f);
            glVertex3f(0.0f, (GLfloat)height, 0.0f);
            glColor3f(0.0f, 0.0f, 1.0f);            /* ndc z 0.5: behind */
            glVertex3f(0.0f, 0.0f, -0.5f);
            glVertex3f((GLfloat)(width / 2), 0.0f, -0.5f);
            glVertex3f((GLfloat)(width / 2), (GLfloat)height, -0.5f);
            glVertex3f(0.0f, (GLfloat)height, -0.5f);
            glColor3f(1.0f, 0.0f, 0.0f);            /* ndc z -0.5: in front */
            glVertex3f((GLfloat)(width / 2), 0.0f, 0.5f);
            glVertex3f((GLfloat)width, 0.0f, 0.5f);
            glVertex3f((GLfloat)width, (GLfloat)height, 0.5f);
            glVertex3f((GLfloat)(width / 2), (GLfloat)height, 0.5f);
            glEnd();
            v9x_glp_hex("ErrorAfterGeometry", (DWORD)glGetError());
            glFinish();
            SwapBuffers(hdc);
            v9x_glp_pump();
            v9x_glp_hex("GeometryLeftPixel",
                        (DWORD)GetPixel(hdc, width / 4, height / 2));
            v9x_glp_hex("GeometryRightPixel",
                        (DWORD)GetPixel(hdc, 3 * width / 4, height / 2));
            v9x_glp_hex("ReadGeometryLeft",
                        v9x_glp_read(width / 4, height / 2));
            v9x_glp_hex("ReadGeometryRight",
                        v9x_glp_read(3 * width / 4, height / 2));

            /*
             * Texturing, perspective-correct. An 8x8 texture, red on the
             * left half and blue on the right, NEAREST, REPLACE, on a quad
             * whose left edge is at z = -1 and right edge at z = -3 under
             * glFrustum(-1,1,-1,1,1,10): the edges land at ndc -1 and 1/3,
             * the texture's s = 0.5 (object x = 0, z = -2) at ndc 0. Affine
             * interpolation would put s = 0.5 at ndc -1/3 instead. So the
             * pixel at ndc -1/6 (42% of the width) is red when correct and
             * blue when affine.
             */
            {
                static GLubyte texels[8 * 8 * 3];
                GLuint texture = 0u;
                int i;

                for (i = 0; i < 8 * 8; ++i) {
                    int left = (i % 8) < 4;

                    texels[i * 3 + 0] = (GLubyte)(left ? 255 : 0);
                    texels[i * 3 + 1] = 0;
                    texels[i * 3 + 2] = (GLubyte)(left ? 0 : 255);
                }
                glGenTextures(1, &texture);
                glBindTexture(GL_TEXTURE_2D, texture);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, 3, 8, 8, 0, GL_RGB,
                             GL_UNSIGNED_BYTE, texels);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                GL_NEAREST);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
                glEnable(GL_TEXTURE_2D);
                glDisable(GL_DEPTH_TEST);
                glMatrixMode(GL_PROJECTION);
                glLoadIdentity();
                glFrustum(-1.0, 1.0, -1.0, 1.0, 1.0, 10.0);
                glMatrixMode(GL_MODELVIEW);
                glLoadIdentity();
                glClear(GL_COLOR_BUFFER_BIT);
                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f);
                glVertex3f(-1.0f, -1.0f, -1.0f);
                glTexCoord2f(1.0f, 0.0f);
                glVertex3f(1.0f, -3.0f, -3.0f);
                glTexCoord2f(1.0f, 1.0f);
                glVertex3f(1.0f, 3.0f, -3.0f);
                glTexCoord2f(0.0f, 1.0f);
                glVertex3f(-1.0f, 1.0f, -1.0f);
                glEnd();
                v9x_glp_hex("ErrorAfterTexture", (DWORD)glGetError());
                glFinish();
                SwapBuffers(hdc);
                v9x_glp_pump();
                v9x_glp_hex("TextureFarLeftPixel",
                            (DWORD)GetPixel(hdc, width / 10, height / 2));
                v9x_glp_hex("TexturePerspectivePixel",
                            (DWORD)GetPixel(hdc, (width * 5) / 12,
                                            height / 2));
                v9x_glp_hex("TextureRightPixel",
                            (DWORD)GetPixel(hdc, (width * 3) / 5,
                                            height / 2));
                glDeleteTextures(1, &texture);
                v9x_glp_hex("ErrorAfterDelete", (DWORD)glGetError());
            }

            /* Queries, the way Quake 2 and GLQuake make them. */
            {
                GLint max_texture = 0;
                GLfloat matrix[16];

                glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
                glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture);
                glMatrixMode(GL_MODELVIEW);
                glLoadIdentity();
                glTranslatef(7.0f, 0.0f, 0.0f);
                glGetFloatv(GL_MODELVIEW_MATRIX, matrix);
                v9x_glp_uint("MaxTextureSize", (DWORD)max_texture);
                v9x_glp_uint("ModelviewTxIsSeven",
                             matrix[12] == 7.0f ? 1ul : 0ul);
                v9x_glp_hex("ErrorAfterQueries", (DWORD)glGetError());
            }

            /*
             * Vertex arrays, the way Quake 2 draws its world: separate
             * float vertex and ubyte colour arrays and glDrawElements
             * with unsigned-short indices - a yellow quad over the left
             * half - then an interleaved C4UB_V3F strip, cyan, over the
             * right half through glDrawArrays. Read back through
             * glReadPixels.
             */
            {
                GLfloat quad[4 * 3];
                static const GLubyte yellow[4 * 4] = {
                    255, 255, 0, 255,  255, 255, 0, 255,
                    255, 255, 0, 255,  255, 255, 0, 255
                };
                static const GLushort order[6] = { 0, 1, 2, 0, 2, 3 };
                struct {
                    GLubyte rgba[4];
                    GLfloat xyz[3];
                } strip[4];
                GLfloat half = (GLfloat)(width / 2);
                int i;

                quad[0] = 0.0f;     quad[1] = 0.0f;
                quad[3] = half;     quad[4] = 0.0f;
                quad[6] = half;     quad[7] = (GLfloat)height;
                quad[9] = 0.0f;     quad[10] = (GLfloat)height;
                for (i = 0; i < 4; ++i) {
                    quad[i * 3 + 2] = 0.0f;
                    strip[i].rgba[0] = 0;
                    strip[i].rgba[1] = 255;
                    strip[i].rgba[2] = 255;
                    strip[i].rgba[3] = 255;
                    strip[i].xyz[0] = (i & 1) ? (GLfloat)width : half;
                    strip[i].xyz[1] = (i & 2) ? (GLfloat)height : 0.0f;
                    strip[i].xyz[2] = 0.0f;
                }
                glDisable(GL_TEXTURE_2D);
                glDisable(GL_DEPTH_TEST);
                glMatrixMode(GL_PROJECTION);
                glLoadIdentity();
                glOrtho(0.0, (GLdouble)width, 0.0, (GLdouble)height,
                        -1.0, 1.0);
                glMatrixMode(GL_MODELVIEW);
                glLoadIdentity();
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                glVertexPointer(3, GL_FLOAT, 0, quad);
                glColorPointer(4, GL_UNSIGNED_BYTE, 0, yellow);
                glEnableClientState(GL_VERTEX_ARRAY);
                glEnableClientState(GL_COLOR_ARRAY);
                v9x_glp_uint("ColorArrayEnabled",
                             glIsEnabled(GL_COLOR_ARRAY) ? 1ul : 0ul);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, order);
                glInterleavedArrays(GL_C4UB_V3F, 0, strip);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                glDisableClientState(GL_VERTEX_ARRAY);
                glDisableClientState(GL_COLOR_ARRAY);
                v9x_glp_hex("ErrorAfterArrays", (DWORD)glGetError());
                glFinish();
                v9x_glp_hex("ReadArraysLeft",
                            v9x_glp_read(width / 4, height / 2));
                v9x_glp_hex("ReadArraysRight",
                            v9x_glp_read(3 * width / 4, height / 2));
                SwapBuffers(hdc);
                v9x_glp_pump();
                v9x_glp_hex("ArraysLeftPixel",
                            (DWORD)GetPixel(hdc, width / 4, height / 2));
                v9x_glp_hex("ArraysRightPixel",
                            (DWORD)GetPixel(hdc, 3 * width / 4, height / 2));
            }
        }
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
