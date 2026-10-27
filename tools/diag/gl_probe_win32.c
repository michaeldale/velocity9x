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
 * /format:N sets format N instead of the one ChoosePixelFormat picks, so a
 * format GLQuake's request does not reach - the ICD's second, deeper depth
 * buffer - can be measured too.
 *
 * Writes C:\V9XDIAG\V9XGLP.INI. Links OPENGL32 statically, as GLQuake does.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <GL/gl.h>
#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"
#include "probe_lock.h"

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

/* The N of a /format:N on the command line, 0 when there is none. The
 * probe links no C runtime, so the scan is by hand. */
static int v9x_glp_forced_format(void)
{
    static const char option[] = "/format:";
    const char *line = GetCommandLineA();
    unsigned int i;
    int value = 0;

    for (; line != 0 && *line != '\0'; ++line) {
        for (i = 0u; option[i] != '\0' && line[i] == option[i]; ++i) {
        }
        if (option[i] != '\0') {
            continue;
        }
        for (line += i; *line >= '0' && *line <= '9'; ++line) {
            value = value * 10 + (*line - '0');
        }
        return value;
    }
    return 0;
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

/*
 * Free video memory, from a DirectDraw object of the probe's own - loaded
 * at run time, so the probe still links OPENGL32 and nothing else new.
 * Zero when DirectDraw cannot say.
 */
/* IID_IDirectDraw2, {B3A6F3E0-2B43-11CF-A2DE-00AA00B93356} (ddraw.h), here
 * because no library the probe links defines it. */
static const GUID v9x_glp_iid_ddraw2 = {
    0xB3A6F3E0ul, 0x2B43u, 0x11CFu,
    { 0xA2u, 0xDEu, 0x00u, 0xAAu, 0x00u, 0xB9u, 0x33u, 0x56u }
};

typedef HRESULT (WINAPI *V9X_GLP_DDCREATE)(GUID *, LPDIRECTDRAW *,
                                           IUnknown *);

static DWORD v9x_glp_vram_free(void)
{
    HMODULE module = LoadLibraryA("DDRAW.DLL");
    V9X_GLP_DDCREATE create;
    LPDIRECTDRAW ddraw = 0;
    LPDIRECTDRAW2 ddraw2 = 0;
    DDSCAPS caps;
    DWORD total = 0ul;
    DWORD free_bytes = 0ul;

    if (module == 0) {
        return 0ul;
    }
    create = (V9X_GLP_DDCREATE)GetProcAddress(module, "DirectDrawCreate");
    if (create != 0 && create(0, &ddraw, 0) == DD_OK) {
        if (IDirectDraw_QueryInterface(ddraw, &v9x_glp_iid_ddraw2,
                                       (void **)&ddraw2) == DD_OK) {
            caps.dwCaps = DDSCAPS_VIDEOMEMORY;
            if (IDirectDraw2_GetAvailableVidMem(ddraw2, &caps, &total,
                                                &free_bytes) != DD_OK) {
                free_bytes = 0ul;
            }
            IDirectDraw2_Release(ddraw2);
        }
        IDirectDraw_Release(ddraw);
    }
    FreeLibrary(module);
    return free_bytes;
}

static void v9x_glp_zero(void *block, DWORD bytes)
{
    DWORD i;

    for (i = 0ul; i < bytes; ++i) {
        ((BYTE *)block)[i] = 0u;
    }
}

/*
 * One pixel of the window as video memory holds it, as 0xRRGGBB: the
 * primary locked through a DirectDraw object of the probe's own, at the
 * client point (x, y) in GDI's coordinates. GetPixel answers through GDI
 * and the display driver; this reads what scan-out reads. 0xFFFFFFFF when
 * the primary cannot be locked or its depth is not 16 or 32.
 */
static DWORD v9x_glp_vram_pixel(HWND window, LONG x, LONG y)
{
    HMODULE module = LoadLibraryA("DDRAW.DLL");
    V9X_GLP_DDCREATE create;
    LPDIRECTDRAW ddraw = 0;
    LPDIRECTDRAWSURFACE primary = 0;
    DDSURFACEDESC desc;
    POINT point;
    DWORD value = 0xFFFFFFFFul;

    if (module == 0) {
        return value;
    }
    point.x = x;
    point.y = y;
    ClientToScreen(window, &point);
    create = (V9X_GLP_DDCREATE)GetProcAddress(module, "DirectDrawCreate");
    if (create != 0 && create(0, &ddraw, 0) == DD_OK) {
        v9x_glp_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS;
        desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
        if (IDirectDraw_SetCooperativeLevel(ddraw, 0, DDSCL_NORMAL) == DD_OK &&
            IDirectDraw_CreateSurface(ddraw, &desc, &primary, 0) == DD_OK) {
            v9x_glp_zero(&desc, sizeof(desc));
            desc.dwSize = sizeof(desc);
            if (IDirectDrawSurface_Lock(primary, 0, &desc,
                                        DDLOCK_WAIT | DDLOCK_READONLY,
                                        0) == DD_OK) {
                const BYTE *row = (const BYTE *)desc.lpSurface +
                                  point.y * desc.lPitch;
                DWORD bits = desc.ddpfPixelFormat.dwRGBBitCount;

                if (bits == 32ul) {
                    value = ((const DWORD *)row)[point.x] & 0x00FFFFFFul;
                } else if (bits == 16ul) {
                    DWORD p = ((const WORD *)row)[point.x];

                    value = (((p >> 11) & 0x1Ful) << 19) |
                            (((p >> 5) & 0x3Ful) << 10) |
                            ((p & 0x1Ful) << 3);
                }
                IDirectDrawSurface_Unlock(primary, 0);
            }
            IDirectDrawSurface_Release(primary);
        }
        IDirectDraw_Release(ddraw);
    }
    FreeLibrary(module);
    return value;
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

/*
 * GL_SGIS_multitexture (docs\plans\gen3-sgis-multitexture.md): whether it
 * is offered with GLQuake's trailing space, its entry points, and what
 * each unit-1 environment draws over a known unit 0. Unit 0 REPLACEs a
 * uniform (200, 100, 50); unit 1 is a uniform (128, 255, 64), or with
 * alpha 128 for DECAL, BLEND toward (0, 0, 255). GL 1.1 table 3.18 gives
 *
 *   MODULATE  (100, 100, 13)    REPLACE  (128, 255, 64)
 *   BLEND     (100,   0, 101)   DECAL    (164, 178, 57)
 *
 * before the target's 565 or 555 quantisation; each is reported with an
 * Ok that allows 8 a channel for it. Then unit 1 at its own coordinates:
 * a texture black on the left and white on the right, MODULATE, s held at
 * 0.25 and then 0.75 while unit 0's s spans the quad - black, then unit
 * 0's colour. Then unit 1 disabled: unit 0's colour alone. Read from the
 * back buffer before any swap.
 */
typedef void (APIENTRY *V9X_GLP_SELECT_FN)(GLenum target);
typedef void (APIENTRY *V9X_GLP_MTEX_FN)(GLenum target, GLfloat s, GLfloat t);

#define V9X_GLP_TEXTURE0_SGIS 0x835Eu
#define V9X_GLP_TEXTURE1_SGIS 0x835Fu

static int v9x_glp_contains(const GLubyte *text, const char *word)
{
    unsigned int at;
    unsigned int i;

    if (text == 0) {
        return 0;
    }
    for (at = 0u; text[at] != 0u; ++at) {
        for (i = 0u; word[i] != '\0' && text[at + i] == (GLubyte)word[i]; ++i) {
        }
        if (word[i] == '\0') {
            return 1;
        }
    }
    return 0;
}

static int v9x_glp_near(DWORD rgb, DWORD red, DWORD green, DWORD blue)
{
    LONG dr = (LONG)((rgb >> 16) & 0xfful) - (LONG)red;
    LONG dg = (LONG)((rgb >> 8) & 0xfful) - (LONG)green;
    LONG db = (LONG)(rgb & 0xfful) - (LONG)blue;

    return dr <= 8l && dr >= -8l && dg <= 8l && dg >= -8l &&
           db <= 8l && db >= -8l;
}

static void v9x_glp_mtex_quad(V9X_GLP_MTEX_FN mtex, GLfloat width,
                              GLfloat height, GLfloat s1)
{
    glBegin(GL_QUADS);
    mtex(V9X_GLP_TEXTURE0_SGIS, 0.0f, 0.0f);
    mtex(V9X_GLP_TEXTURE1_SGIS, s1, 0.5f);
    glVertex2f(0.0f, 0.0f);
    mtex(V9X_GLP_TEXTURE0_SGIS, 1.0f, 0.0f);
    mtex(V9X_GLP_TEXTURE1_SGIS, s1, 0.5f);
    glVertex2f(width, 0.0f);
    mtex(V9X_GLP_TEXTURE0_SGIS, 1.0f, 1.0f);
    mtex(V9X_GLP_TEXTURE1_SGIS, s1, 0.5f);
    glVertex2f(width, height);
    mtex(V9X_GLP_TEXTURE0_SGIS, 0.0f, 1.0f);
    mtex(V9X_GLP_TEXTURE1_SGIS, s1, 0.5f);
    glVertex2f(0.0f, height);
    glEnd();
}

static void v9x_glp_sgis_case(V9X_GLP_MTEX_FN mtex, const char *key,
                              GLint width, GLint height, GLfloat s1,
                              DWORD red, DWORD green, DWORD blue)
{
    char name[48];
    DWORD rgb;

    glClear(GL_COLOR_BUFFER_BIT);
    v9x_glp_mtex_quad(mtex, (GLfloat)width, (GLfloat)height, s1);
    glFinish();
    rgb = v9x_glp_read(width / 2, height / 2);
    wsprintfA(name, "Sgis%s", key);
    v9x_glp_hex(name, rgb);
    wsprintfA(name, "Sgis%sOk", key);
    v9x_glp_uint(name, v9x_glp_near(rgb, red, green, blue) ? 1ul : 0ul);
}

static void v9x_glp_sgis(GLint width, GLint height)
{
    static GLubyte rgb0[4 * 3];
    static GLubyte rgb1[4 * 3];
    static GLubyte rgba1[4 * 4];
    static GLubyte halves[4 * 3];
    V9X_GLP_SELECT_FN select;
    V9X_GLP_MTEX_FN mtex;
    GLuint names[4];
    GLfloat blue_env[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    int i;

    v9x_glp_uint("SgisAdvertised",
                 v9x_glp_contains(glGetString(GL_EXTENSIONS),
                                  "GL_SGIS_multitexture ") ? 1ul : 0ul);
    select = (V9X_GLP_SELECT_FN)wglGetProcAddress("glSelectTextureSGIS");
    mtex = (V9X_GLP_MTEX_FN)wglGetProcAddress("glMTexCoord2fSGIS");
    v9x_glp_uint("SgisEntryPoints", (select != 0 ? 1ul : 0ul) +
                                    (mtex != 0 ? 1ul : 0ul));
    if (select == 0 || mtex == 0) {
        return;
    }

    for (i = 0; i < 4; ++i) {
        rgb0[i * 3 + 0] = 200u;
        rgb0[i * 3 + 1] = 100u;
        rgb0[i * 3 + 2] = 50u;
        rgb1[i * 3 + 0] = 128u;
        rgb1[i * 3 + 1] = 255u;
        rgb1[i * 3 + 2] = 64u;
        rgba1[i * 4 + 0] = 128u;
        rgba1[i * 4 + 1] = 255u;
        rgba1[i * 4 + 2] = 64u;
        rgba1[i * 4 + 3] = 128u;
        halves[i * 3 + 0] = (GLubyte)((i & 1) != 0 ? 255u : 0u);
        halves[i * 3 + 1] = halves[i * 3 + 0];
        halves[i * 3 + 2] = halves[i * 3 + 0];
    }

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)width, 0.0, (GLdouble)height, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(4, names);

    select(V9X_GLP_TEXTURE0_SGIS);
    glBindTexture(GL_TEXTURE_2D, names[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);

    select(V9X_GLP_TEXTURE1_SGIS);
    glBindTexture(GL_TEXTURE_2D, names[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, blue_env);
    glEnable(GL_TEXTURE_2D);
    v9x_glp_hex("SgisErrorAfterSetup", (DWORD)glGetError());

    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    v9x_glp_sgis_case(mtex, "Modulate", width, height, 0.5f, 100ul, 100ul,
                      13ul);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    v9x_glp_sgis_case(mtex, "Replace", width, height, 0.5f, 128ul, 255ul,
                      64ul);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
    v9x_glp_sgis_case(mtex, "Blend", width, height, 0.5f, 100ul, 0ul,
                      101ul);
    glBindTexture(GL_TEXTURE_2D, names[2]);
    glTexImage2D(GL_TEXTURE_2D, 0, 4, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    v9x_glp_sgis_case(mtex, "Decal", width, height, 0.5f, 164ul, 178ul,
                      57ul);

    glBindTexture(GL_TEXTURE_2D, names[3]);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 halves);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    v9x_glp_sgis_case(mtex, "CoordLeft", width, height, 0.25f, 0ul, 0ul,
                      0ul);
    v9x_glp_sgis_case(mtex, "CoordRight", width, height, 0.75f, 200ul, 100ul,
                      50ul);

    glDisable(GL_TEXTURE_2D);
    v9x_glp_sgis_case(mtex, "Unit1Off", width, height, 0.25f, 200ul, 100ul,
                      50ul);
    v9x_glp_hex("SgisErrorAfterDraws", (DWORD)glGetError());

    select(V9X_GLP_TEXTURE0_SGIS);
    glDeleteTextures(4, names);
    glDisable(GL_TEXTURE_2D);
}

/*
 * GL_ARB_multitexture (docs\plans\opengl-arb-multitexture.md, step 4): the
 * SGIS section's cases again through the ARB entry points, each drawn three
 * ways - immediate mode with glMultiTexCoord2fARB (Imm), and glDrawArrays
 * (Arrays) and glDrawElements (Elements) from a texture-coordinate array
 * per unit, chosen with glClientActiveTextureARB, which is how Quake 3
 * draws. The units, textures and expected colours are the SGIS section's;
 * keys are Arb<way><case> with an Ok beside each. First the queries:
 * MAX_TEXTURE_UNITS_ARB, and ACTIVE_TEXTURE_ARB and
 * CLIENT_ACTIVE_TEXTURE_ARB after each selects unit 1.
 */
#define V9X_GLP_TEXTURE0_ARB 0x84C0u
#define V9X_GLP_TEXTURE1_ARB 0x84C1u
#define V9X_GLP_ACTIVE_TEXTURE_ARB 0x84E0u
#define V9X_GLP_CLIENT_ACTIVE_TEXTURE_ARB 0x84E1u
#define V9X_GLP_MAX_TEXTURE_UNITS_ARB 0x84E2u
#define V9X_GLP_ARB_WAYS 3

static V9X_GLP_MTEX_FN v9x_glp_arb_coord;
static V9X_GLP_SELECT_FN v9x_glp_arb_client;

static void v9x_glp_arb_quad(int way, GLfloat width, GLfloat height,
                             GLfloat s1)
{
    static const GLubyte order[4] = { 0u, 1u, 2u, 3u };
    GLfloat xy[8];
    GLfloat st0[8];
    GLfloat st1[8];
    int i;

    xy[0] = 0.0f;   xy[1] = 0.0f;
    xy[2] = width;  xy[3] = 0.0f;
    xy[4] = width;  xy[5] = height;
    xy[6] = 0.0f;   xy[7] = height;
    st0[0] = 0.0f;  st0[1] = 0.0f;
    st0[2] = 1.0f;  st0[3] = 0.0f;
    st0[4] = 1.0f;  st0[5] = 1.0f;
    st0[6] = 0.0f;  st0[7] = 1.0f;
    for (i = 0; i < 4; ++i) {
        st1[i * 2] = s1;
        st1[i * 2 + 1] = 0.5f;
    }

    if (way == 0) {
        glBegin(GL_QUADS);
        for (i = 0; i < 4; ++i) {
            v9x_glp_arb_coord(V9X_GLP_TEXTURE0_ARB, st0[i * 2],
                              st0[i * 2 + 1]);
            v9x_glp_arb_coord(V9X_GLP_TEXTURE1_ARB, st1[i * 2],
                              st1[i * 2 + 1]);
            glVertex2f(xy[i * 2], xy[i * 2 + 1]);
        }
        glEnd();
        return;
    }

    glVertexPointer(2, GL_FLOAT, 0, xy);
    glEnableClientState(GL_VERTEX_ARRAY);
    v9x_glp_arb_client(V9X_GLP_TEXTURE0_ARB);
    glTexCoordPointer(2, GL_FLOAT, 0, st0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    v9x_glp_arb_client(V9X_GLP_TEXTURE1_ARB);
    glTexCoordPointer(2, GL_FLOAT, 0, st1);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    if (way == 1) {
        glDrawArrays(GL_QUADS, 0, 4);
    } else {
        glDrawElements(GL_QUADS, 4, GL_UNSIGNED_BYTE, order);
    }
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    v9x_glp_arb_client(V9X_GLP_TEXTURE0_ARB);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

/* One case drawn each of the three ways. */
static void v9x_glp_arb_case(const char *key, GLint width, GLint height,
                             GLfloat s1, DWORD red, DWORD green, DWORD blue)
{
    static const char *const ways[V9X_GLP_ARB_WAYS] = {
        "Imm", "Arrays", "Elements"
    };
    char name[48];
    DWORD rgb;
    int way;

    for (way = 0; way < V9X_GLP_ARB_WAYS; ++way) {
        glClear(GL_COLOR_BUFFER_BIT);
        v9x_glp_arb_quad(way, (GLfloat)width, (GLfloat)height, s1);
        glFinish();
        rgb = v9x_glp_read(width / 2, height / 2);
        wsprintfA(name, "Arb%s%s", ways[way], key);
        v9x_glp_hex(name, rgb);
        wsprintfA(name, "Arb%s%sOk", ways[way], key);
        v9x_glp_uint(name, v9x_glp_near(rgb, red, green, blue) ? 1ul : 0ul);
    }
}

static void v9x_glp_arb(GLint width, GLint height)
{
    static GLubyte rgb0[4 * 3];
    static GLubyte rgb1[4 * 3];
    static GLubyte rgba1[4 * 4];
    static GLubyte halves[4 * 3];
    V9X_GLP_SELECT_FN active;
    GLuint names[4];
    GLfloat blue_env[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    GLint units = 0;
    GLint active_unit = 0;
    GLint client_unit = 0;
    int i;

    v9x_glp_uint("ArbAdvertised",
                 v9x_glp_contains(glGetString(GL_EXTENSIONS),
                                  "GL_ARB_multitexture ") ? 1ul : 0ul);
    active = (V9X_GLP_SELECT_FN)wglGetProcAddress("glActiveTextureARB");
    v9x_glp_arb_client =
        (V9X_GLP_SELECT_FN)wglGetProcAddress("glClientActiveTextureARB");
    v9x_glp_arb_coord =
        (V9X_GLP_MTEX_FN)wglGetProcAddress("glMultiTexCoord2fARB");
    v9x_glp_uint("ArbEntryPoints", (active != 0 ? 1ul : 0ul) +
                                   (v9x_glp_arb_client != 0 ? 1ul : 0ul) +
                                   (v9x_glp_arb_coord != 0 ? 1ul : 0ul));
    if (active == 0 || v9x_glp_arb_client == 0 || v9x_glp_arb_coord == 0) {
        return;
    }

    glGetIntegerv(V9X_GLP_MAX_TEXTURE_UNITS_ARB, &units);
    active(V9X_GLP_TEXTURE1_ARB);
    glGetIntegerv(V9X_GLP_ACTIVE_TEXTURE_ARB, &active_unit);
    active(V9X_GLP_TEXTURE0_ARB);
    v9x_glp_arb_client(V9X_GLP_TEXTURE1_ARB);
    glGetIntegerv(V9X_GLP_CLIENT_ACTIVE_TEXTURE_ARB, &client_unit);
    v9x_glp_arb_client(V9X_GLP_TEXTURE0_ARB);
    v9x_glp_uint("ArbMaxUnits", (DWORD)units);
    v9x_glp_hex("ArbActiveTexture", (DWORD)active_unit);
    v9x_glp_hex("ArbClientActiveTexture", (DWORD)client_unit);
    v9x_glp_uint("ArbQueriesOk",
                 units == 2 && active_unit == (GLint)V9X_GLP_TEXTURE1_ARB &&
                 client_unit == (GLint)V9X_GLP_TEXTURE1_ARB ? 1ul : 0ul);

    for (i = 0; i < 4; ++i) {
        rgb0[i * 3 + 0] = 200u;
        rgb0[i * 3 + 1] = 100u;
        rgb0[i * 3 + 2] = 50u;
        rgb1[i * 3 + 0] = 128u;
        rgb1[i * 3 + 1] = 255u;
        rgb1[i * 3 + 2] = 64u;
        rgba1[i * 4 + 0] = 128u;
        rgba1[i * 4 + 1] = 255u;
        rgba1[i * 4 + 2] = 64u;
        rgba1[i * 4 + 3] = 128u;
        halves[i * 3 + 0] = (GLubyte)((i & 1) != 0 ? 255u : 0u);
        halves[i * 3 + 1] = halves[i * 3 + 0];
        halves[i * 3 + 2] = halves[i * 3 + 0];
    }

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)width, 0.0, (GLdouble)height, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(4, names);

    active(V9X_GLP_TEXTURE0_ARB);
    glBindTexture(GL_TEXTURE_2D, names[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);

    active(V9X_GLP_TEXTURE1_ARB);
    glBindTexture(GL_TEXTURE_2D, names[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, blue_env);
    glEnable(GL_TEXTURE_2D);
    v9x_glp_hex("ArbErrorAfterSetup", (DWORD)glGetError());

    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    v9x_glp_arb_case("Modulate", width, height, 0.5f, 100ul, 100ul, 13ul);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    v9x_glp_arb_case("Replace", width, height, 0.5f, 128ul, 255ul, 64ul);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
    v9x_glp_arb_case("Blend", width, height, 0.5f, 100ul, 0ul, 101ul);
    glBindTexture(GL_TEXTURE_2D, names[2]);
    glTexImage2D(GL_TEXTURE_2D, 0, 4, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    v9x_glp_arb_case("Decal", width, height, 0.5f, 164ul, 178ul, 57ul);

    glBindTexture(GL_TEXTURE_2D, names[3]);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 halves);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    v9x_glp_arb_case("CoordLeft", width, height, 0.25f, 0ul, 0ul, 0ul);
    v9x_glp_arb_case("CoordRight", width, height, 0.75f, 200ul, 100ul, 50ul);

    glDisable(GL_TEXTURE_2D);
    v9x_glp_arb_case("Unit1Off", width, height, 0.25f, 200ul, 100ul, 50ul);
    v9x_glp_hex("ArbErrorAfterDraws", (DWORD)glGetError());

    active(V9X_GLP_TEXTURE0_ARB);
    glDeleteTextures(4, names);
    glDisable(GL_TEXTURE_2D);
}

/*
 * Which mip level unit 0 samples, with unit 1 off and with it on
 * (2026-10-08, docs\decisions\2026-10-08-rage-xl-draw-cost.md): the Rage
 * Pro's composite drew Quake 2's walls from levels far too small. Unit 0
 * is a 64x64 chain whose seven levels are each one solid colour, drawn
 * NEAREST_MIPMAP_NEAREST over a square of 64, 32, 16, 8 and 4 pixels, so
 * the expected level is 0 to 4. Unit 1 is white, MODULATE, which leaves
 * the colour alone: a 2x2 and a 64x64 texture with coordinates 0..1, and
 * the 2x2 again with 0..8, so a level that follows unit 1's size or its
 * coordinates' rate shows as a different column. Each square's centre is
 * read and named by the level whose colour it is (SgisMip<case><edge>=L<n>,
 * or L? for none). Then the ways Quake 2's walls differ, one at a time,
 * with unit 1 off and with the 2x2: unit 0 LINEAR_MIPMAP_NEAREST with
 * LINEAR magnification (SgisMipLin...), unit 1 LINEAR both ways
 * (SgisMipU1Lin...), and the square in perspective at eye depth 2, so
 * every vertex's W is 2 (SgisMipPersp...).
 */
#define V9X_GLP_MIP_LEVELS 7

static const GLubyte v9x_glp_mip_colours[V9X_GLP_MIP_LEVELS][3] = {
    { 255u, 0u, 0u }, { 0u, 255u, 0u }, { 0u, 0u, 255u },
    { 255u, 255u, 0u }, { 255u, 0u, 255u }, { 0u, 255u, 255u },
    { 255u, 255u, 255u }
};

static void v9x_glp_mip_name(const char *key, DWORD rgb)
{
    char value[8];
    int level;
    int found = -1;

    for (level = 0; level < V9X_GLP_MIP_LEVELS; ++level) {
        if ((((rgb >> 16) & 0xfful) > 127ul) ==
                (v9x_glp_mip_colours[level][0] > 127u) &&
            (((rgb >> 8) & 0xfful) > 127ul) ==
                (v9x_glp_mip_colours[level][1] > 127u) &&
            ((rgb & 0xfful) > 127ul) ==
                (v9x_glp_mip_colours[level][2] > 127u)) {
            found = level;
            break;
        }
    }
    if (found < 0) {
        lstrcpyA(value, "L?");
    } else {
        wsprintfA(value, "L%d", found);
    }
    v9x_glp_text(key, value);
}

static void v9x_glp_mip_square(V9X_GLP_MTEX_FN mtex, GLfloat edge,
                               GLfloat s1, GLfloat depth)
{
    /* At eye depth `depth` under glFrustum(0, w, 0, h, 1, ...), a vertex
     * at (x * depth, y * depth) lands on pixel (x, y). */
    GLfloat z = -depth;
    GLfloat far_edge = edge * depth;

    glBegin(GL_QUADS);
    if (mtex != 0) {
        mtex(V9X_GLP_TEXTURE0_SGIS, 0.0f, 0.0f);
        mtex(V9X_GLP_TEXTURE1_SGIS, 0.0f, 0.0f);
    } else {
        glTexCoord2f(0.0f, 0.0f);
    }
    glVertex3f(0.0f, 0.0f, z);
    if (mtex != 0) {
        mtex(V9X_GLP_TEXTURE0_SGIS, 1.0f, 0.0f);
        mtex(V9X_GLP_TEXTURE1_SGIS, s1, 0.0f);
    } else {
        glTexCoord2f(1.0f, 0.0f);
    }
    glVertex3f(far_edge, 0.0f, z);
    if (mtex != 0) {
        mtex(V9X_GLP_TEXTURE0_SGIS, 1.0f, 1.0f);
        mtex(V9X_GLP_TEXTURE1_SGIS, s1, s1);
    } else {
        glTexCoord2f(1.0f, 1.0f);
    }
    glVertex3f(far_edge, far_edge, z);
    if (mtex != 0) {
        mtex(V9X_GLP_TEXTURE0_SGIS, 0.0f, 1.0f);
        mtex(V9X_GLP_TEXTURE1_SGIS, 0.0f, s1);
    } else {
        glTexCoord2f(0.0f, 1.0f);
    }
    glVertex3f(0.0f, far_edge, z);
    glEnd();
}

/* One sweep of square edges 64 down to 4 under the state already set. */
static void v9x_glp_mip_sweep(V9X_GLP_MTEX_FN mtex, const char *name,
                              GLfloat s1, GLfloat depth)
{
    GLint edge;

    for (edge = 64; edge >= 4; edge /= 2) {
        char key[40];

        glClear(GL_COLOR_BUFFER_BIT);
        v9x_glp_mip_square(mtex, (GLfloat)edge, s1, depth);
        glFinish();
        wsprintfA(key, "SgisMip%s%d", name, (int)edge);
        v9x_glp_mip_name(key, v9x_glp_read(edge / 2, edge / 2));
    }
}

static void v9x_glp_sgis_mip(GLint width, GLint height)
{
    static GLubyte level_texels[64 * 64 * 3];
    static GLubyte white[64 * 64 * 3];
    static const char *const cases[4] = { "Off", "W2", "W64", "W2x8" };
    static const char *const off_names[3] = {
        "LinOff", "U1LinOff", "PerspOff"
    };
    static const char *const on_names[3] = { "LinW2", "U1LinW2", "PerspW2" };
    V9X_GLP_SELECT_FN select;
    V9X_GLP_MTEX_FN mtex;
    GLuint names[3];
    int level;
    int variant;
    int i;
    int c;

    select = (V9X_GLP_SELECT_FN)wglGetProcAddress("glSelectTextureSGIS");
    mtex = (V9X_GLP_MTEX_FN)wglGetProcAddress("glMTexCoord2fSGIS");
    for (i = 0; i < 64 * 64 * 3; ++i) {
        white[i] = 255u;
    }

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)width, 0.0, (GLdouble)height, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(3, names);

    if (select != 0) {
        select(V9X_GLP_TEXTURE0_SGIS);
    }
    glBindTexture(GL_TEXTURE_2D, names[0]);
    for (level = 0; level < V9X_GLP_MIP_LEVELS; ++level) {
        GLint side = 64 >> level;

        for (i = 0; i < side * side; ++i) {
            level_texels[i * 3 + 0] = v9x_glp_mip_colours[level][0];
            level_texels[i * 3 + 1] = v9x_glp_mip_colours[level][1];
            level_texels[i * 3 + 2] = v9x_glp_mip_colours[level][2];
        }
        glTexImage2D(GL_TEXTURE_2D, level, 3, side, side, 0, GL_RGB,
                     GL_UNSIGNED_BYTE, level_texels);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);

    if (select != 0) {
        select(V9X_GLP_TEXTURE1_SGIS);
        glBindTexture(GL_TEXTURE_2D, names[1]);
        glTexImage2D(GL_TEXTURE_2D, 0, 3, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE,
                     white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, names[2]);
        glTexImage2D(GL_TEXTURE_2D, 0, 3, 64, 64, 0, GL_RGB,
                     GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    }

    for (c = 0; c < 4; ++c) {
        if (c > 0 && select == 0) {
            break;
        }
        if (select != 0) {
            select(V9X_GLP_TEXTURE1_SGIS);
            if (c == 0) {
                glDisable(GL_TEXTURE_2D);
            } else {
                glBindTexture(GL_TEXTURE_2D, names[c == 2 ? 2 : 1]);
                glEnable(GL_TEXTURE_2D);
            }
        }
        v9x_glp_mip_sweep(c == 0 ? 0 : mtex, cases[c],
                          c == 3 ? 8.0f : 1.0f, 1.0f);
    }

    for (variant = 0; variant < 3; ++variant) {
        GLfloat depth = variant == 2 ? 2.0f : 1.0f;

        if (select != 0) {
            select(V9X_GLP_TEXTURE0_SGIS);
        }
        glBindTexture(GL_TEXTURE_2D, names[0]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        variant == 0 ? GL_LINEAR_MIPMAP_NEAREST
                                     : GL_NEAREST_MIPMAP_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                        variant == 0 ? GL_LINEAR : GL_NEAREST);
        if (select != 0) {
            select(V9X_GLP_TEXTURE1_SGIS);
            glBindTexture(GL_TEXTURE_2D, names[1]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            variant == 1 ? GL_LINEAR : GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            variant == 1 ? GL_LINEAR : GL_NEAREST);
        }
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        if (variant == 2) {
            glFrustum(0.0, (GLdouble)width, 0.0, (GLdouble)height, 1.0,
                      10.0);
        } else {
            glOrtho(0.0, (GLdouble)width, 0.0, (GLdouble)height, -1.0, 1.0);
        }
        glMatrixMode(GL_MODELVIEW);
        if (select != 0) {
            select(V9X_GLP_TEXTURE1_SGIS);
            glDisable(GL_TEXTURE_2D);
        }
        v9x_glp_mip_sweep(0, off_names[variant], 1.0f, depth);
        if (select != 0) {
            glEnable(GL_TEXTURE_2D);
            v9x_glp_mip_sweep(mtex, on_names[variant], 1.0f, depth);
        }
    }
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)width, 0.0, (GLdouble)height, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    v9x_glp_hex("SgisMipError", (DWORD)glGetError());

    if (select != 0) {
        select(V9X_GLP_TEXTURE1_SGIS);
        glDisable(GL_TEXTURE_2D);
        select(V9X_GLP_TEXTURE0_SGIS);
    }
    glDeleteTextures(3, names);
    glDisable(GL_TEXTURE_2D);
}

/*
 * glPolygonOffset with POLYGON_OFFSET_FILL (GL 1.1 3.5.5), as Half-Life's
 * decals use it: under LESS, a second quad in the first one's plane loses
 * without an offset, wins with units -4 (four depth steps nearer) and
 * loses again with +4. Read from the back buffer before any swap.
 */
static void v9x_glp_offset_quad(GLint width, GLint height)
{
    glBegin(GL_QUADS);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f((GLfloat)width, 0.0f, 0.0f);
    glVertex3f((GLfloat)width, (GLfloat)height, 0.0f);
    glVertex3f(0.0f, (GLfloat)height, 0.0f);
    glEnd();
}

static void v9x_glp_polygon_offset(GLint width, GLint height)
{
    DWORD rgb;
    GLfloat value = 0.0f;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)width, 0.0, (GLdouble)height, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glColor3f(1.0f, 0.0f, 0.0f);
    v9x_glp_offset_quad(width, height);
    glColor3f(0.0f, 1.0f, 0.0f);
    v9x_glp_offset_quad(width, height);
    glFinish();
    rgb = v9x_glp_read(width / 2, height / 2);
    v9x_glp_hex("OffsetNone", rgb);
    v9x_glp_uint("OffsetNoneOk", v9x_glp_near(rgb, 255ul, 0ul, 0ul) ? 1ul : 0ul);

    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(0.0f, -4.0f);
    v9x_glp_offset_quad(width, height);
    glFinish();
    rgb = v9x_glp_read(width / 2, height / 2);
    v9x_glp_hex("OffsetNearer", rgb);
    v9x_glp_uint("OffsetNearerOk",
                 v9x_glp_near(rgb, 0ul, 255ul, 0ul) ? 1ul : 0ul);

    glPolygonOffset(0.0f, 4.0f);
    glColor3f(0.0f, 0.0f, 1.0f);
    v9x_glp_offset_quad(width, height);
    glFinish();
    rgb = v9x_glp_read(width / 2, height / 2);
    v9x_glp_hex("OffsetFarther", rgb);
    v9x_glp_uint("OffsetFartherOk",
                 v9x_glp_near(rgb, 0ul, 255ul, 0ul) ? 1ul : 0ul);

    glGetFloatv(GL_POLYGON_OFFSET_UNITS, &value);
    v9x_glp_uint("OffsetUnitsQueryOk", value == 4.0f ? 1ul : 0ul);
    v9x_glp_hex("ErrorAfterOffset", (DWORD)glGetError());
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(0.0f, 0.0f);
    glDisable(GL_DEPTH_TEST);
}

/*
 * glFog (GL 1.1 3.9) and the integer/double immediate-mode forms. A red
 * quad at eye distance 0.5 under blue fog: LINEAR 0..1 leaves f = 0.5,
 * so half red and half blue; EXP with density 2 leaves f = e^-1, red 94
 * and blue 161. Then the same with fog off, colours and corners given
 * through glColor3us, glColor4b, glRecti and glVertex2s, each read back
 * from the back buffer before any swap.
 */
static void v9x_glp_fog_quad(GLint width, GLint height, GLfloat z)
{
    glBegin(GL_QUADS);
    glVertex3f(0.0f, 0.0f, z);
    glVertex3f((GLfloat)width, 0.0f, z);
    glVertex3f((GLfloat)width, (GLfloat)height, z);
    glVertex3f(0.0f, (GLfloat)height, z);
    glEnd();
}

static void v9x_glp_fog(GLint width, GLint height)
{
    static const GLint blue[4] = { 0, 0, 2147483647, 2147483647 };
    GLint mode = 0;
    GLfloat colour[4];
    GLboolean edge = GL_TRUE;
    DWORD rgb;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)width, 0.0, (GLdouble)height, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 0.0f);
    glFogf(GL_FOG_END, 1.0f);
    glFogiv(GL_FOG_COLOR, blue);
    glColor3f(1.0f, 0.0f, 0.0f);
    v9x_glp_fog_quad(width, height, -0.5f);
    glFinish();
    rgb = v9x_glp_read(width / 2, height / 2);
    v9x_glp_hex("FogLinear", rgb);
    v9x_glp_uint("FogLinearOk", v9x_glp_near(rgb, 128ul, 0ul, 127ul) ? 1ul : 0ul);

    glFogi(GL_FOG_MODE, GL_EXP);
    glFogf(GL_FOG_DENSITY, 2.0f);
    v9x_glp_fog_quad(width, height, -0.5f);
    glFinish();
    rgb = v9x_glp_read(width / 2, height / 2);
    v9x_glp_hex("FogExp", rgb);
    v9x_glp_uint("FogExpOk", v9x_glp_near(rgb, 94ul, 0ul, 161ul) ? 1ul : 0ul);

    /* At the eye EXP leaves the fragment as it is. */
    v9x_glp_fog_quad(width, height, 0.0f);
    glFinish();
    rgb = v9x_glp_read(width / 2, height / 2);
    v9x_glp_hex("FogExpAtEye", rgb);
    v9x_glp_uint("FogExpAtEyeOk", v9x_glp_near(rgb, 255ul, 0ul, 0ul) ? 1ul : 0ul);

    glGetIntegerv(GL_FOG_MODE, &mode);
    glGetFloatv(GL_FOG_COLOR, colour);
    v9x_glp_uint("FogQueryOk", mode == GL_EXP && colour[2] > 0.99f &&
                               colour[0] < 0.01f ? 1ul : 0ul);
    glDisable(GL_FOG);
    v9x_glp_hex("ErrorAfterFog", (DWORD)glGetError());

    /* The added immediate-mode forms, unfogged. */
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3us(0u, 65535u, 0u);
    glRecti(0, 0, width / 2, height);
    glColor4b(127, 127, 0, 127);
    glBegin(GL_QUADS);
    glVertex2s((GLshort)(width / 2), 0);
    glVertex2s((GLshort)width, 0);
    glVertex2s((GLshort)width, (GLshort)height);
    glVertex2s((GLshort)(width / 2), (GLshort)height);
    glEnd();
    glFinish();
    rgb = v9x_glp_read(width / 4, height / 2);
    v9x_glp_hex("RectColor3us", rgb);
    v9x_glp_uint("RectColor3usOk", v9x_glp_near(rgb, 0ul, 255ul, 0ul) ? 1ul : 0ul);
    rgb = v9x_glp_read(width * 3 / 4, height / 2);
    v9x_glp_hex("Vertex2sColor4b", rgb);
    v9x_glp_uint("Vertex2sColor4bOk",
                 v9x_glp_near(rgb, 255ul, 255ul, 0ul) ? 1ul : 0ul);

    glEdgeFlag(GL_FALSE);
    glGetBooleanv(GL_EDGE_FLAG, &edge);
    glEdgeFlag(GL_TRUE);
    v9x_glp_uint("EdgeFlagQueryOk", edge == GL_FALSE ? 1ul : 0ul);
    v9x_glp_hex("ErrorAfterVariants", (DWORD)glGetError());
}

/*
 * GL_REPLACE on an RGB texture keeps the fragment's alpha (table 3.18):
 * Tux Racer's sky, blended and fogged, which every path refused on the
 * HD 4600 (report V9X-YP59WN). A green texture under a blue vertex of
 * alpha one half, SRC_ALPHA blended over black: green 128 unfogged, and
 * with LINEAR red fog at f = 0.5, red 64 and green 64. A blue cast says
 * the vertex colour leaked in; full green says the texel's alpha was used.
 */
static void v9x_glp_replace_rgb(GLint width, GLint height)
{
    GLubyte texels[4 * 4 * 3];
    GLuint name = 0u;
    unsigned int i;
    DWORD rgb;

    for (i = 0u; i < 4u * 4u; ++i) {
        texels[i * 3u] = 0u;
        texels[i * 3u + 1u] = 255u;
        texels[i * 3u + 2u] = 0u;
    }
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)width, 0.0, (GLdouble)height, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    glGenTextures(1, &name);
    glBindTexture(GL_TEXTURE_2D, name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 4, 4, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 texels);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    glClear(GL_COLOR_BUFFER_BIT);
    glColor4f(0.0f, 0.0f, 1.0f, 0.5f);
    v9x_glp_fog_quad(width, height, -0.5f);
    glFinish();
    rgb = v9x_glp_read(width / 2, height / 2);
    v9x_glp_hex("ReplaceRgbBlend", rgb);
    v9x_glp_uint("ReplaceRgbBlendOk",
                 v9x_glp_near(rgb, 0ul, 128ul, 0ul) ? 1ul : 0ul);

    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 0.0f);
    glFogf(GL_FOG_END, 1.0f);
    {
        static const GLfloat red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };

        glFogfv(GL_FOG_COLOR, red);
    }
    glClear(GL_COLOR_BUFFER_BIT);
    v9x_glp_fog_quad(width, height, -0.5f);
    glFinish();
    rgb = v9x_glp_read(width / 2, height / 2);
    v9x_glp_hex("ReplaceRgbFogBlend", rgb);
    v9x_glp_uint("ReplaceRgbFogBlendOk",
                 v9x_glp_near(rgb, 64ul, 64ul, 0ul) ? 1ul : 0ul);

    glDisable(GL_FOG);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glDeleteTextures(1, &name);
    v9x_glp_hex("ErrorAfterReplaceRgb", (DWORD)glGetError());
}

/*
 * Which texel each pixel of a 1:1 GL_NEAREST draw reads (2026-10-13), the
 * OpenGL twin of V9XDDP's TexPhase. Half-Life's OpenGL console font lost
 * single-pixel strokes on the Voodoo4 engine (A8U4I5 boot 442). A 16x16
 * texture whose texel (x, y) carries x * 17 in red and y * 17 in blue, so
 * each survives RGB565 to the nearest step, green full as a marker, is
 * drawn under glOrtho as a 16x16 quad at (16, 16) on whole pixels, s and
 * t 0 to 1, then at offsets of a quarter and a half pixel either way.
 * Row 24 and column 24 are read back: TexPhase<n>Cols holds the texel
 * column each of pixels 16..31 on row 24 took, as a hex digit, '.' for no
 * texel. An exact copy reads 0123456789ABCDEF in both.
 */
static void v9x_glp_texel_phase(GLint width, GLint height)
{
    static const char digits[] = "0123456789ABCDEF";
    static const GLfloat offset[5] = { 0.0f, 0.25f, 0.5f, -0.25f, -0.5f };
    static const char *cols_key[5] = {
        "TexPhase0Cols", "TexPhaseP25Cols", "TexPhaseP5Cols",
        "TexPhaseM25Cols", "TexPhaseM5Cols" };
    static const char *rows_key[5] = {
        "TexPhase0Rows", "TexPhaseP25Rows", "TexPhaseP5Rows",
        "TexPhaseM25Rows", "TexPhaseM5Rows" };
    GLubyte texels[16 * 16 * 3];
    char cols[17];
    char rows[17];
    GLuint name = 0u;
    DWORD index;
    DWORD at;

    for (index = 0ul; index < 256ul; ++index) {
        texels[index * 3ul] = (GLubyte)((index & 15ul) * 17ul);
        texels[index * 3ul + 1ul] = 255u;
        texels[index * 3ul + 2ul] = (GLubyte)((index >> 4) * 17ul);
    }
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)width, 0.0, (GLdouble)height, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glGenTextures(1, &name);
    glBindTexture(GL_TEXTURE_2D, name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 16, 16, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 texels);
    glEnable(GL_TEXTURE_2D);

    for (index = 0ul; index < 5ul; ++index) {
        GLfloat x0 = 16.0f + offset[index];
        GLfloat y0 = 16.0f + offset[index];

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f);
        glVertex2f(x0, y0);
        glTexCoord2f(1.0f, 0.0f);
        glVertex2f(x0 + 16.0f, y0);
        glTexCoord2f(1.0f, 1.0f);
        glVertex2f(x0 + 16.0f, y0 + 16.0f);
        glTexCoord2f(0.0f, 1.0f);
        glVertex2f(x0, y0 + 16.0f);
        glEnd();
        glFinish();
        for (at = 0ul; at < 16ul; ++at) {
            DWORD col = v9x_glp_read((GLint)(16ul + at), 24);
            DWORD row = v9x_glp_read(24, (GLint)(16ul + at));

            cols[at] = ((col >> 8) & 0xfful) >= 0xc0ul
                           ? digits[(((col >> 16) & 0xfful) + 8ul) / 17ul]
                           : '.';
            rows[at] = ((row >> 8) & 0xfful) >= 0xc0ul
                           ? digits[((row & 0xfful) + 8ul) / 17ul] : '.';
        }
        cols[16] = '\0';
        rows[16] = '\0';
        v9x_glp_text(cols_key[index], cols);
        v9x_glp_text(rows_key[index], rows);
    }
    glDisable(GL_TEXTURE_2D);
    glDeleteTextures(1, &name);
    v9x_glp_hex("ErrorAfterTexPhase", (DWORD)glGetError());
}

/*
 * glPushAttrib/glPopAttrib (GL 1.1 6.1.11), glPushClientAttrib, the
 * texture queries and glClearStencil. A red clear colour and blending are
 * pushed with COLOR_BUFFER_BIT, changed to green and off, and popped; the
 * clear after the pop must be red, from the back buffer. The depth test,
 * changed in between and not pushed, must stay as changed. Then a 4x2
 * texture's level size and environment through the integer queries.
 */
/*
 * Hardware T&L, step 3 (docs\plans\hardware-tnl.md): one scene drawn with
 * the ICD's T&L on, then with V9XGL_TNL=0, which leaves the vertices to the
 * CPU path, and the two frames compared pixel by pixel. The scene is what
 * the vertex stage must get right: a viewport offset into the window and a
 * narrowed depth range, a perspective cube under the depth test with back
 * faces culled, a triangle cut by the near plane, a quad far wider than the
 * viewport (the guard band, then the viewport's own edge), and a textured
 * quad. The window's border outside the viewport must stay the clear
 * colour in both. The probe reports; V9XGL.LOG's "tnl allowed=... batches="
 * lines say whether the first frame really went through the engine.
 */
#define V9X_GLP_TNL_TOLERANCE 3

static void v9x_glp_tnl_cube_face(const GLfloat *a, const GLfloat *b,
                                  const GLfloat *c, const GLfloat *d,
                                  GLfloat red, GLfloat green, GLfloat blue)
{
    glColor3f(red, green, blue);
    glVertex3fv(a);
    glColor3f(green, blue, red);
    glVertex3fv(b);
    glColor3f(blue, red, green);
    glVertex3fv(c);
    glColor3f(1.0f, 1.0f, 1.0f);
    glVertex3fv(d);
}

static void v9x_glp_tnl_draw(GLint width, GLint height, GLuint texture,
                             GLuint texture2)
{
    int quad;
    static const GLfloat v[8][3] = {
        { -1.0f, -1.0f, -1.0f }, { 1.0f, -1.0f, -1.0f },
        { 1.0f, 1.0f, -1.0f }, { -1.0f, 1.0f, -1.0f },
        { -1.0f, -1.0f, 1.0f }, { 1.0f, -1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f }, { -1.0f, 1.0f, 1.0f }
    };

    glViewport(width / 8, height / 8, width * 3 / 4, height * 3 / 4);
    glDepthRange(0.1, 0.9);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glShadeModel(GL_SMOOTH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -0.75, 0.75, 1.0, 20.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* A quad far wider than the viewport, behind everything. */
    glBegin(GL_QUADS);
    glColor3f(0.2f, 0.2f, 0.6f);
    glVertex3f(-60.0f, -3.0f, -15.0f);
    glColor3f(0.6f, 0.2f, 0.2f);
    glVertex3f(60.0f, -3.0f, -15.0f);
    glColor3f(0.2f, 0.6f, 0.2f);
    glVertex3f(60.0f, 3.0f, -15.0f);
    glColor3f(0.6f, 0.6f, 0.2f);
    glVertex3f(-60.0f, 3.0f, -15.0f);
    glEnd();

    /* A triangle from in front of the eye to behind the far side of the
     * cube: the near plane cuts it. */
    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 0.5f, 0.0f);
    glVertex3f(-2.0f, -1.5f, 0.5f);
    glColor3f(0.0f, 0.5f, 1.0f);
    glVertex3f(2.5f, -1.0f, -9.0f);
    glColor3f(0.5f, 1.0f, 0.5f);
    glVertex3f(-1.5f, 2.0f, -9.0f);
    glEnd();

    /* The cube, back faces culled. */
    glPushMatrix();
    glTranslatef(0.3f, 0.0f, -5.0f);
    glRotatef(25.0f, 1.0f, 1.0f, 0.0f);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glBegin(GL_QUADS);
    v9x_glp_tnl_cube_face(v[0], v[3], v[2], v[1], 1.0f, 0.0f, 0.0f);
    v9x_glp_tnl_cube_face(v[4], v[5], v[6], v[7], 0.0f, 1.0f, 0.0f);
    v9x_glp_tnl_cube_face(v[0], v[1], v[5], v[4], 0.0f, 0.0f, 1.0f);
    v9x_glp_tnl_cube_face(v[3], v[7], v[6], v[2], 1.0f, 1.0f, 0.0f);
    v9x_glp_tnl_cube_face(v[0], v[4], v[7], v[3], 0.0f, 1.0f, 1.0f);
    v9x_glp_tnl_cube_face(v[1], v[2], v[6], v[5], 1.0f, 0.0f, 1.0f);
    glEnd();
    glDisable(GL_CULL_FACE);
    glPopMatrix();

    /* A textured quad, upper left, in front. */
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBegin(GL_QUADS);
    glColor3f(1.0f, 1.0f, 1.0f);
    glTexCoord2f(0.0f, 0.0f);
    glVertex3f(-2.6f, 0.6f, -3.5f);
    glTexCoord2f(2.0f, 0.0f);
    glVertex3f(-1.2f, 0.6f, -3.0f);
    glTexCoord2f(2.0f, 2.0f);
    glVertex3f(-1.2f, 1.9f, -3.0f);
    glTexCoord2f(0.0f, 2.0f);
    glVertex3f(-2.6f, 1.9f, -3.5f);
    glEnd();

    /* Six small quads along the bottom, alternating two textures, each
     * under its own translation and colour, with nothing between them
     * that changes the frame: the ICD holds them as one T&L batch of six
     * segments (step 3b), which the engine draws in one submission. */
    for (quad = 0; quad < 6; ++quad) {
        glPushMatrix();
        glTranslatef(-2.2f + 0.8f * (GLfloat)quad, -2.1f, -4.0f);
        glBindTexture(GL_TEXTURE_2D, (quad & 1) != 0 ? texture2 : texture);
        glBegin(GL_QUADS);
        glColor3f(1.0f, 0.5f + 0.1f * (GLfloat)quad, 1.0f);
        glTexCoord2f(0.0f, 0.0f);
        glVertex3f(-0.3f, -0.3f, 0.0f);
        glTexCoord2f(1.0f, 0.0f);
        glVertex3f(0.3f, -0.3f, 0.0f);
        glTexCoord2f(1.0f, 1.0f);
        glVertex3f(0.3f, 0.3f, 0.0f);
        glTexCoord2f(0.0f, 1.0f);
        glVertex3f(-0.3f, 0.3f, 0.0f);
        glEnd();
        glPopMatrix();
    }
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDepthRange(0.0, 1.0);
    glFinish();
}

/*
 * A frame as a 24-bit bottom-up BMP, rows as glReadPixels returns them
 * (bottom first), so the file needs no flip. `scale` multiplies every
 * channel, clamped, for the difference image.
 */
static void v9x_glp_tnl_bmp(const char *path, const GLubyte *rgba,
                            GLint width, GLint height, int scale)
{
    BITMAPFILEHEADER file;
    BITMAPINFOHEADER info;
    HANDLE out;
    DWORD row_bytes = ((DWORD)width * 3ul + 3ul) & ~3ul;
    DWORD written;
    GLubyte row[4096 * 3];
    GLint x;
    GLint y;

    if (width > 4096) {
        return;
    }
    out = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                      FILE_ATTRIBUTE_NORMAL, 0);
    if (out == INVALID_HANDLE_VALUE) {
        return;
    }
    v9x_glp_zero(&file, sizeof(file));
    v9x_glp_zero(&info, sizeof(info));
    file.bfType = 0x4d42u;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + row_bytes * (DWORD)height;
    info.biSize = sizeof(info);
    info.biWidth = width;
    info.biHeight = height;
    info.biPlanes = 1u;
    info.biBitCount = 24u;
    WriteFile(out, &file, sizeof(file), &written, 0);
    WriteFile(out, &info, sizeof(info), &written, 0);
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            const GLubyte *p = &rgba[((DWORD)y * (DWORD)width + (DWORD)x) * 4ul];
            int channel;

            for (channel = 0; channel < 3; ++channel) {
                int value = (int)p[2 - channel] * scale;

                row[x * 3 + channel] = (GLubyte)(value > 255 ? 255 : value);
            }
        }
        for (x = width * 3; (DWORD)x < row_bytes; ++x) {
            row[x] = 0u;
        }
        WriteFile(out, row, row_bytes, &written, 0);
    }
    CloseHandle(out);
}

/* Make the context current again, so the ICD reads V9XGL_TNL anew. */
static void v9x_glp_tnl_switch(HDC hdc, HGLRC context, const char *value)
{
    SetEnvironmentVariableA("V9XGL_TNL", value);
    wglMakeCurrent(0, 0);
    wglMakeCurrent(hdc, context);
}

static void v9x_glp_tnl(HDC hdc, HGLRC context, GLint width, GLint height)
{
    static GLubyte texels[8 * 8 * 3];
    HANDLE heap = GetProcessHeap();
    GLubyte *frame[2];
    DWORD bytes;
    DWORD index;
    DWORD differ = 0ul;
    DWORD over[3] = { 0ul, 0ul, 0ul };
    DWORD drawn = 0ul;
    DWORD border = 0ul;
    DWORD first = 0xfffffffful;
    int worst = 0;
    int pass;
    GLuint texture = 0u;
    GLuint texture2 = 0u;
    GLint x;
    GLint y;

    if (width < 16 || height < 16) {
        v9x_glp_text("TnlResult", "skipped: small window");
        return;
    }
    bytes = (DWORD)width * (DWORD)height * 4ul;
    frame[0] = (GLubyte *)HeapAlloc(heap, 0, bytes);
    frame[1] = (GLubyte *)HeapAlloc(heap, 0, bytes);
    if (frame[0] == 0 || frame[1] == 0) {
        v9x_glp_text("TnlResult", "skipped: no memory");
        return;
    }
    /* An 8x8 checker of two colours. */
    for (y = 0; y < 8; ++y) {
        for (x = 0; x < 8; ++x) {
            GLubyte *t = &texels[(y * 8 + x) * 3];
            int on = ((x ^ y) & 1) != 0;

            t[0] = on ? 255u : 40u;
            t[1] = on ? 200u : 40u;
            t[2] = on ? 0u : 160u;
        }
    }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, texels);
    /* The second: the same checker, red on green. */
    for (index = 0ul; index < 8ul * 8ul; ++index) {
        GLubyte *t = &texels[index * 3ul];
        int on = (((index % 8ul) ^ (index / 8ul)) & 1ul) != 0ul;

        t[0] = on ? 220u : 20u;
        t[1] = on ? 30u : 180u;
        t[2] = 30u;
    }
    glGenTextures(1, &texture2);
    glBindTexture(GL_TEXTURE_2D, texture2);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, texels);

    /* Pass 0 with T&L (the variable unset), pass 1 without it. */
    for (pass = 0; pass < 2; ++pass) {
        v9x_glp_tnl_switch(hdc, context, pass == 0 ? 0 : "0");
        v9x_glp_tnl_draw(width, height, texture, texture2);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                     frame[pass]);
    }
    v9x_glp_tnl_switch(hdc, context, 0);
    glDeleteTextures(1, &texture);
    glDeleteTextures(1, &texture2);

    for (index = 0ul; index < (DWORD)width * (DWORD)height; ++index) {
        const GLubyte *a = &frame[0][index * 4ul];
        const GLubyte *b = &frame[1][index * 4ul];
        int channel;
        int largest = 0;

        for (channel = 0; channel < 3; ++channel) {
            int d = (int)a[channel] - (int)b[channel];

            if (d < 0) {
                d = -d;
            }
            if (d > largest) {
                largest = d;
            }
        }
        if (largest > worst) {
            worst = largest;
        }
        /* Past one 565 step (8), two, and four. */
        if (largest > 8) {
            ++over[0];
        }
        if (largest > 16) {
            ++over[1];
        }
        if (largest > 32) {
            ++over[2];
        }
        if (largest > V9X_GLP_TNL_TOLERANCE) {
            ++differ;
            if (first == 0xfffffffful) {
                first = index;
            }
        }
        if ((a[0] | a[1] | a[2]) != 0u) {
            ++drawn;
            x = (GLint)(index % (DWORD)width);
            y = (GLint)(index / (DWORD)width);
            /* Outside the viewport: nothing may be drawn there. */
            if (x < width / 8 || y < height / 8 ||
                x >= width / 8 + width * 3 / 4 ||
                y >= height / 8 + height * 3 / 4) {
                ++border;
            }
        }
    }
    v9x_glp_uint("TnlPixels", (DWORD)width * (DWORD)height);
    v9x_glp_uint("TnlDrawnPixels", drawn);
    v9x_glp_uint("TnlDifferPixels", differ);
    v9x_glp_uint("TnlWorstChannel", (DWORD)worst);
    v9x_glp_uint("TnlDifferOver8", over[0]);
    v9x_glp_uint("TnlDifferOver16", over[1]);
    v9x_glp_uint("TnlDifferOver32", over[2]);
    /* Both frames and their difference, x16, for the eye. */
    v9x_glp_tnl_bmp(V9X_DIAG_PATH("V9XTNLA.BMP"), frame[0], width, height, 1);
    v9x_glp_tnl_bmp(V9X_DIAG_PATH("V9XTNLB.BMP"), frame[1], width, height, 1);
    for (index = 0ul; index < (DWORD)width * (DWORD)height * 4ul; ++index) {
        int d = (int)frame[0][index] - (int)frame[1][index];

        frame[0][index] = (GLubyte)(d < 0 ? -d : d);
    }
    v9x_glp_tnl_bmp(V9X_DIAG_PATH("V9XTNLD.BMP"), frame[0], width, height,
                    16);
    v9x_glp_uint("TnlOutsideViewport", border);
    if (first != 0xfffffffful) {
        v9x_glp_uint("TnlFirstDifferX", first % (DWORD)width);
        v9x_glp_uint("TnlFirstDifferY", first / (DWORD)width);
    }
    v9x_glp_text("TnlResult",
                 differ == 0ul && border == 0ul && drawn != 0ul ? "match"
                                                                : "differ");
    HeapFree(heap, 0, frame[0]);
    HeapFree(heap, 0, frame[1]);
}

/*
 * OpenGL lighting (docs\plans\hardware-tnl.md, step 4's gate). Each scene
 * sets lights and a material through the ordinary calls, then draws samples:
 * 4x4-pixel quads, each with one normal (and colour), whose centre pixel is
 * read back and compared with GL 1.1's equation (2.13.1) evaluated here, in
 * code of the probe's own - not the ICD's gl_light.c nor the reference
 * evaluator. Eye coordinates are window coordinates (orthographic, identity
 * model-view), so a positional light's distance is in pixels. Only an engine
 * that runs T&L lights at all; elsewhere every sample shows unlit white and
 * LitResult says so.
 */
#ifdef __WATCOMC__
static double v9x_glp_sqrt(double value);
#pragma aux v9x_glp_sqrt = "fsqrt" parm [8087] value [8087];
/* Float to long by fistp, rounding to nearest: a cast would call Watcom's
 * __CHP, from the C runtime the probe does not link. */
static long v9x_glp_to_long(float value);
#pragma aux v9x_glp_to_long = \
    "sub esp,4" \
    "fistp dword ptr [esp]" \
    "pop eax" \
    parm [8087] value [eax] modify exact [eax];
#endif

#define V9X_GLP_LIT_SAMPLES 8u
#define V9X_GLP_LIT_TOLERANCE 10l

typedef struct v9x_glp_lit_light {
    int on;
    GLfloat ambient[4];
    GLfloat diffuse[4];
    GLfloat specular[4];
    GLfloat position[4];
    GLfloat direction[3];
    GLfloat exponent;
    GLfloat cutoff_cos;     /* -2 for no spot */
    GLfloat cutoff_degrees;
    GLfloat attenuation[3];
} V9X_GLP_LIT_LIGHT;

typedef struct v9x_glp_lit_scene {
    const char *name;
    V9X_GLP_LIT_LIGHT light[4];
    GLfloat material_ambient[4];
    GLfloat material_diffuse[4];
    GLfloat material_specular[4];
    GLfloat material_emission[4];
    GLfloat shininess;
    GLfloat model_ambient[4];
    int local_viewer;
    int normalize;
    int color_material;     /* AMBIENT_AND_DIFFUSE, front and back */
    GLfloat x[V9X_GLP_LIT_SAMPLES];
    GLfloat y[V9X_GLP_LIT_SAMPLES];
    GLfloat normal[V9X_GLP_LIT_SAMPLES][3];
    GLfloat colour[V9X_GLP_LIT_SAMPLES][4];
} V9X_GLP_LIT_SCENE;

static GLfloat v9x_glp_dot3(const GLfloat *a, const GLfloat *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void v9x_glp_unit3(GLfloat *v)
{
    double length = v9x_glp_sqrt((double)v9x_glp_dot3(v, v));

    if (length > 0.0) {
        v[0] = (GLfloat)(v[0] / length);
        v[1] = (GLfloat)(v[1] / length);
        v[2] = (GLfloat)(v[2] / length);
    }
}

/* x^e for the small whole exponents the scenes use. */
static GLfloat v9x_glp_power(GLfloat x, GLfloat e)
{
    GLfloat result = 1.0f;
    long n = v9x_glp_to_long(e);

    while (n-- > 0) {
        result *= x;
    }
    return result;
}

static LONG v9x_glp_byte(GLfloat value)
{
    if (value <= 0.0f) {
        return 0l;
    }
    if (value >= 1.0f) {
        return 255l;
    }
    return (LONG)v9x_glp_to_long(value * 255.0f);
}

/* GL 1.1's front-face colour for sample s (2.13.1), as 0xRRGGBB. */
static DWORD v9x_glp_lit_expected(const V9X_GLP_LIT_SCENE *scene,
                                  unsigned int s)
{
    const GLfloat *a_cm = scene->color_material ? scene->colour[s]
                                                : scene->material_ambient;
    const GLfloat *d_cm = scene->color_material ? scene->colour[s]
                                                : scene->material_diffuse;
    GLfloat vertex[3];
    GLfloat n[3];
    GLfloat c[3];
    unsigned int i;
    unsigned int k;

    vertex[0] = scene->x[s] + 2.0f;
    vertex[1] = scene->y[s] + 2.0f;
    vertex[2] = 0.0f;
    n[0] = scene->normal[s][0];
    n[1] = scene->normal[s][1];
    n[2] = scene->normal[s][2];
    if (scene->normalize) {
        v9x_glp_unit3(n);
    }
    for (k = 0u; k < 3u; ++k) {
        c[k] = scene->material_emission[k] + a_cm[k] * scene->model_ambient[k];
    }
    for (i = 0u; i < 4u; ++i) {
        const V9X_GLP_LIT_LIGHT *l = &scene->light[i];
        GLfloat vp[3];
        GLfloat h[3];
        GLfloat factor = 1.0f;
        GLfloat n_dot_l;

        if (!l->on) {
            continue;
        }
        if (l->position[3] == 0.0f) {
            vp[0] = l->position[0];
            vp[1] = l->position[1];
            vp[2] = l->position[2];
            v9x_glp_unit3(vp);
        } else {
            GLfloat d;

            vp[0] = l->position[0] - vertex[0];
            vp[1] = l->position[1] - vertex[1];
            vp[2] = l->position[2] - vertex[2];
            d = (GLfloat)v9x_glp_sqrt((double)v9x_glp_dot3(vp, vp));
            v9x_glp_unit3(vp);
            factor = 1.0f / (l->attenuation[0] + l->attenuation[1] * d +
                             l->attenuation[2] * d * d);
            if (l->cutoff_cos > -1.5f) {
                GLfloat rho = -v9x_glp_dot3(vp, l->direction);

                factor *= rho < l->cutoff_cos ? 0.0f
                                              : v9x_glp_power(rho, l->exponent);
            }
        }
        n_dot_l = v9x_glp_dot3(n, vp);
        for (k = 0u; k < 3u; ++k) {
            c[k] += factor * a_cm[k] * l->ambient[k];
        }
        if (n_dot_l <= 0.0f) {
            continue;
        }
        if (scene->local_viewer) {
            GLfloat eye[3];

            eye[0] = -vertex[0];
            eye[1] = -vertex[1];
            eye[2] = -vertex[2];
            v9x_glp_unit3(eye);
            h[0] = vp[0] + eye[0];
            h[1] = vp[1] + eye[1];
            h[2] = vp[2] + eye[2];
        } else {
            h[0] = vp[0];
            h[1] = vp[1];
            h[2] = vp[2] + 1.0f;
        }
        v9x_glp_unit3(h);
        for (k = 0u; k < 3u; ++k) {
            GLfloat n_dot_h = v9x_glp_dot3(n, h);

            c[k] += factor * n_dot_l * d_cm[k] * l->diffuse[k];
            if (n_dot_h > 0.0f) {
                c[k] += factor * v9x_glp_power(n_dot_h, scene->shininess) *
                        scene->material_specular[k] * l->specular[k];
            }
        }
    }
    return ((DWORD)v9x_glp_byte(c[0]) << 16) |
           ((DWORD)v9x_glp_byte(c[1]) << 8) | (DWORD)v9x_glp_byte(c[2]);
}

static int v9x_glp_lit_close(DWORD a, DWORD b)
{
    LONG d[3];
    unsigned int k;

    d[0] = (LONG)((a >> 16) & 0xfful) - (LONG)((b >> 16) & 0xfful);
    d[1] = (LONG)((a >> 8) & 0xfful) - (LONG)((b >> 8) & 0xfful);
    d[2] = (LONG)(a & 0xfful) - (LONG)(b & 0xfful);
    for (k = 0u; k < 3u; ++k) {
        if (d[k] > V9X_GLP_LIT_TOLERANCE || d[k] < -V9X_GLP_LIT_TOLERANCE) {
            return 0;
        }
    }
    return 1;
}

/* One scene's state through the GL calls, its samples drawn, read back and
 * judged; the samples that matched. */
static unsigned int v9x_glp_lit_scene(const V9X_GLP_LIT_SCENE *scene,
                                      unsigned int index)
{
    static const GLenum lights[4] = {
        GL_LIGHT0, GL_LIGHT1, GL_LIGHT2, GL_LIGHT3
    };
    unsigned int matched = 0u;
    unsigned int i;
    unsigned int s;
    char key[32];
    char value[48];

    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_LIGHTING);
    for (i = 0u; i < 4u; ++i) {
        const V9X_GLP_LIT_LIGHT *l = &scene->light[i];

        if (!l->on) {
            glDisable(lights[i]);
            continue;
        }
        glEnable(lights[i]);
        glLightfv(lights[i], GL_AMBIENT, l->ambient);
        glLightfv(lights[i], GL_DIFFUSE, l->diffuse);
        glLightfv(lights[i], GL_SPECULAR, l->specular);
        glLightfv(lights[i], GL_POSITION, l->position);
        glLightfv(lights[i], GL_SPOT_DIRECTION, l->direction);
        glLightf(lights[i], GL_SPOT_EXPONENT, l->exponent);
        glLightf(lights[i], GL_SPOT_CUTOFF, l->cutoff_degrees);
        glLightf(lights[i], GL_CONSTANT_ATTENUATION, l->attenuation[0]);
        glLightf(lights[i], GL_LINEAR_ATTENUATION, l->attenuation[1]);
        glLightf(lights[i], GL_QUADRATIC_ATTENUATION, l->attenuation[2]);
    }
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, scene->material_ambient);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, scene->material_diffuse);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, scene->material_specular);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, scene->material_emission);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, scene->shininess);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, scene->model_ambient);
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, scene->local_viewer);
    if (scene->normalize) {
        glEnable(GL_NORMALIZE);
    } else {
        glDisable(GL_NORMALIZE);
    }
    if (scene->color_material) {
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
        glEnable(GL_COLOR_MATERIAL);
    } else {
        glDisable(GL_COLOR_MATERIAL);
    }

    glBegin(GL_QUADS);
    for (s = 0u; s < V9X_GLP_LIT_SAMPLES; ++s) {
        glNormal3fv(scene->normal[s]);
        glColor4fv(scene->colour[s]);
        glVertex2f(scene->x[s], scene->y[s]);
        glVertex2f(scene->x[s] + 4.0f, scene->y[s]);
        glVertex2f(scene->x[s] + 4.0f, scene->y[s] + 4.0f);
        glVertex2f(scene->x[s], scene->y[s] + 4.0f);
    }
    glEnd();
    glFinish();

    for (s = 0u; s < V9X_GLP_LIT_SAMPLES; ++s) {
        DWORD got = v9x_glp_read((GLint)v9x_glp_to_long(scene->x[s]) + 2,
                                 (GLint)v9x_glp_to_long(scene->y[s]) + 2);
        DWORD want = v9x_glp_lit_expected(scene, s);
        int close = v9x_glp_lit_close(got, want);

        wsprintfA(key, "Lit%u%s%u", index, scene->name, s);
        wsprintfA(value, "%06lX want %06lX %s", got, want,
                  close ? "ok" : "off");
        v9x_glp_text(key, value);
        if (close) {
            ++matched;
        }
    }
    glDisable(GL_LIGHTING);
    glDisable(GL_COLOR_MATERIAL);
    glDisable(GL_NORMALIZE);
    for (i = 0u; i < 4u; ++i) {
        glDisable(lights[i]);
    }
    return matched;
}

static void v9x_glp_lit_light(V9X_GLP_LIT_LIGHT *l, GLfloat r, GLfloat g,
                              GLfloat b)
{
    unsigned int k;

    v9x_glp_zero(l, sizeof(*l));
    l->on = 1;
    l->ambient[3] = 1.0f;
    l->diffuse[0] = r;
    l->diffuse[1] = g;
    l->diffuse[2] = b;
    l->diffuse[3] = 1.0f;
    for (k = 0u; k < 4u; ++k) {
        l->specular[k] = l->diffuse[k];
    }
    l->position[2] = 1.0f;
    l->direction[2] = -1.0f;
    l->cutoff_cos = -2.0f;
    l->cutoff_degrees = 180.0f;
    l->attenuation[0] = 1.0f;
}

static void v9x_glp_lit_base(V9X_GLP_LIT_SCENE *scene, const char *name,
                             GLint width, GLint height)
{
    unsigned int s;

    v9x_glp_zero(scene, sizeof(*scene));
    scene->name = name;
    scene->material_ambient[0] = scene->material_ambient[1] =
        scene->material_ambient[2] = 0.2f;
    scene->material_diffuse[0] = scene->material_diffuse[1] =
        scene->material_diffuse[2] = 0.8f;
    scene->material_ambient[3] = scene->material_diffuse[3] = 1.0f;
    scene->material_specular[3] = scene->material_emission[3] = 1.0f;
    scene->model_ambient[0] = scene->model_ambient[1] =
        scene->model_ambient[2] = 0.2f;
    scene->model_ambient[3] = 1.0f;
    /* Samples on a 4 x 2 grid across the window. */
    for (s = 0u; s < V9X_GLP_LIT_SAMPLES; ++s) {
        scene->x[s] = (GLfloat)((GLint)(s % 4u) * width / 4 + width / 8);
        scene->y[s] = (GLfloat)((GLint)(s / 4u) * height / 2 + height / 4);
        scene->normal[s][2] = 1.0f;
        scene->colour[s][0] = scene->colour[s][1] = scene->colour[s][2] =
            scene->colour[s][3] = 1.0f;
    }
}

static void v9x_glp_lit(GLint width, GLint height)
{
    static const GLfloat tilt[V9X_GLP_LIT_SAMPLES][3] = {
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.6f, 0.8f }, { 0.8f, 0.0f, 0.6f },
        { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f }, { -0.6f, 0.0f, 0.8f },
        { 0.48f, 0.6f, 0.64f }, { 0.0f, -0.8f, 0.6f }
    };
    static V9X_GLP_LIT_SCENE scene;
    GLfloat translate_position[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    GLfloat got[4];
    GLfloat cx = (GLfloat)(width / 2);
    GLfloat cy = (GLfloat)(height / 2);
    unsigned int matched = 0u;
    unsigned int total = 0u;
    unsigned int s;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)width, 0.0, (GLdouble)height, -400.0, 400.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FOG);
    glShadeModel(GL_SMOOTH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    /* 1: the initial state, LIGHT0 white from +z. */
    v9x_glp_lit_base(&scene, "Default", width, height);
    v9x_glp_lit_light(&scene.light[0], 1.0f, 1.0f, 1.0f);
    for (s = 0u; s < V9X_GLP_LIT_SAMPLES; ++s) {
        scene.normal[s][0] = tilt[s][0];
        scene.normal[s][1] = tilt[s][1];
        scene.normal[s][2] = tilt[s][2];
    }
    matched += v9x_glp_lit_scene(&scene, 1u);
    total += V9X_GLP_LIT_SAMPLES;

    /* 2: a coloured directional light with specular, shininess 8. */
    scene.name = "Specular";
    v9x_glp_lit_light(&scene.light[0], 1.0f, 0.5f, 0.25f);
    scene.light[0].position[0] = 0.0f;
    scene.light[0].position[1] = 0.6f;
    scene.light[0].position[2] = 0.8f;
    scene.light[0].ambient[0] = 0.1f;
    scene.material_specular[0] = scene.material_specular[1] =
        scene.material_specular[2] = 0.5f;
    scene.shininess = 8.0f;
    matched += v9x_glp_lit_scene(&scene, 2u);
    total += V9X_GLP_LIT_SAMPLES;

    /* 3: a green point light 120 pixels in front of the window's centre,
     * attenuated; normals face the viewer. */
    v9x_glp_lit_base(&scene, "Point", width, height);
    v9x_glp_lit_light(&scene.light[1], 0.0f, 1.0f, 0.0f);
    scene.light[1].position[0] = cx;
    scene.light[1].position[1] = cy;
    scene.light[1].position[2] = 120.0f;
    scene.light[1].position[3] = 1.0f;
    scene.light[1].attenuation[0] = 0.5f;
    scene.light[1].attenuation[1] = 0.004f;
    scene.light[1].attenuation[2] = 0.00002f;
    scene.model_ambient[0] = 0.0f;
    scene.model_ambient[1] = 0.0f;
    scene.model_ambient[2] = 0.3f;
    matched += v9x_glp_lit_scene(&scene, 3u);
    total += V9X_GLP_LIT_SAMPLES;

    /* 4: a yellow spot 200 pixels in front, pointing at the window, a 30
     * degree cutoff with exponent 4: the cone meets z = 0 115 pixels from
     * the centre, so some samples are outside it. */
    v9x_glp_lit_base(&scene, "Spot", width, height);
    v9x_glp_lit_light(&scene.light[2], 1.0f, 1.0f, 0.0f);
    scene.light[2].position[0] = cx;
    scene.light[2].position[1] = cy;
    scene.light[2].position[2] = 200.0f;
    scene.light[2].position[3] = 1.0f;
    scene.light[2].exponent = 4.0f;
    scene.light[2].cutoff_degrees = 30.0f;
    scene.light[2].cutoff_cos = 0.8660254f;
    matched += v9x_glp_lit_scene(&scene, 4u);
    total += V9X_GLP_LIT_SAMPLES;

    /* 5: two lights, colour material from each sample's colour, a local
     * viewer, specular, and normals of length 3 renormalised. */
    v9x_glp_lit_base(&scene, "ColourMaterial", width, height);
    v9x_glp_lit_light(&scene.light[0], 0.6f, 0.6f, 0.6f);
    v9x_glp_lit_light(&scene.light[3], 0.3f, 0.2f, 0.5f);
    scene.light[3].position[0] = 0.57735f;
    scene.light[3].position[1] = 0.57735f;
    scene.light[3].position[2] = 0.57735f;
    scene.material_specular[0] = scene.material_specular[1] =
        scene.material_specular[2] = 0.4f;
    scene.shininess = 20.0f;
    scene.local_viewer = 1;
    scene.normalize = 1;
    scene.color_material = 1;
    for (s = 0u; s < V9X_GLP_LIT_SAMPLES; ++s) {
        scene.normal[s][0] = tilt[s][0] * 3.0f;
        scene.normal[s][1] = tilt[s][1] * 3.0f;
        scene.normal[s][2] = tilt[s][2] * 3.0f;
        scene.colour[s][0] = (GLfloat)(s & 1u) * 0.5f + 0.25f;
        scene.colour[s][1] = (GLfloat)((s >> 1) & 1u) * 0.5f + 0.25f;
        scene.colour[s][2] = (GLfloat)((s >> 2) & 1u) * 0.5f + 0.25f;
    }
    matched += v9x_glp_lit_scene(&scene, 5u);
    total += V9X_GLP_LIT_SAMPLES;

    /* glLight's POSITION goes through the model-view current at the call. */
    glTranslatef(10.0f, 20.0f, 30.0f);
    glLightfv(GL_LIGHT4, GL_POSITION, translate_position);
    glLoadIdentity();
    glGetLightfv(GL_LIGHT4, GL_POSITION, got);
    v9x_glp_uint("LitPositionQueryOk",
                 got[0] == 11.0f && got[1] == 21.0f && got[2] == 31.0f &&
                         got[3] == 1.0f
                     ? 1ul : 0ul);

    v9x_glp_uint("LitSamples", total);
    v9x_glp_uint("LitMatched", matched);
    v9x_glp_text("LitResult", matched == total ? "match" : "differ");
    v9x_glp_hex("ErrorAfterLit", (DWORD)glGetError());
}

static void v9x_glp_attrib(GLint width, GLint height)
{
    static const GLubyte texels[4 * 2 * 3] = { 0 };
    GLint depth = -1;
    GLint value = 0;
    GLint size[2];
    GLint alignment = 0;
    GLfloat clear[4];
    GLuint name = 0u;
    DWORD rgb;

    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_BLEND);
    glPushAttrib(GL_COLOR_BUFFER_BIT);
    glGetIntegerv(GL_ATTRIB_STACK_DEPTH, &depth);
    v9x_glp_uint("AttribDepthOk", depth == 1 ? 1ul : 0ul);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glPopAttrib();
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    v9x_glp_uint("AttribPopOk", clear[0] == 1.0f && clear[1] == 0.0f &&
                                glIsEnabled(GL_BLEND) &&
                                glIsEnabled(GL_DEPTH_TEST) ? 1ul : 0ul);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    rgb = v9x_glp_read(width / 2, height / 2);
    v9x_glp_hex("AttribClear", rgb);
    v9x_glp_uint("AttribClearOk", v9x_glp_near(rgb, 255ul, 0ul, 0ul) ? 1ul : 0ul);
    glPopAttrib();
    v9x_glp_uint("AttribUnderflowOk",
                 glGetError() == GL_STACK_UNDERFLOW ? 1ul : 0ul);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPopClientAttrib();
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    v9x_glp_uint("ClientAttribOk", alignment == 4 ? 1ul : 0ul);

    glGenTextures(1, &name);
    glBindTexture(GL_TEXTURE_2D, name);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 4, 2, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 texels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &size[0]);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &size[1]);
    v9x_glp_uint("TexLevelQueryOk", size[0] == 4 && size[1] == 2 ? 1ul : 0ul);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &value);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    v9x_glp_uint("TexEnvQueryOk", value == GL_REPLACE ? 1ul : 0ul);
    glClearStencil(3);
    value = 0;
    glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &value);
    v9x_glp_uint("ClearStencilQueryOk", value == 3 ? 1ul : 0ul);
    glClearStencil(0);
    glDeleteTextures(1, &name);
    v9x_glp_hex("ErrorAfterAttrib", (DWORD)glGetError());
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
    if (!v9x_probe_lock_take()) {
        v9x_glp_text("Result", "BUSY");
        WritePrivateProfileStringA(0, 0, 0, V9X_DIAG_GLP_INI);
        ExitProcess(3u);
    }
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
    if (v9x_glp_forced_format() > 0) {
        chosen = v9x_glp_forced_format();
        v9x_glp_uint("ForcedFormat", (DWORD)chosen);
    }
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
            glAccum(GL_ACCUM, 1.0f);            /* Phase 6: still a stub */
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
            /* How long GDI takes to see the swap: GetPixel polled until it
             * reads red, up to 500 ms. A blit's length points at the copy;
             * a display refresh's at something tied to the retrace. */
            {
                DWORD started = GetTickCount();
                DWORD polls = 0ul;
                DWORD seen = 0ul;

                while (GetTickCount() - started < 500ul) {
                    ++polls;
                    seen = (DWORD)GetPixel(hdc, 3 * width / 4, height / 2);
                    if (seen == 0x000000FFul) {
                        break;
                    }
                }
                v9x_glp_uint("GeometryRightSeenMs", GetTickCount() - started);
                v9x_glp_uint("GeometryRightSeenPolls", polls);
            }
            v9x_glp_hex("GeometryRightPixelLate",
                        (DWORD)GetPixel(hdc, 3 * width / 4, height / 2));

            /*
             * The swap again, read without a message pump: two GetPixels
             * back to back, timed from SwapBuffers' return in
             * performance-counter ticks. Then the swap once more, read
             * first through a DirectDraw lock (which waits for the
             * engine) and only then through GetPixel.
             */
            {
                LARGE_INTEGER frequency;
                LARGE_INTEGER t0;
                LARGE_INTEGER t1;
                LARGE_INTEGER t2;
                DWORD first;
                DWORD second;

                QueryPerformanceFrequency(&frequency);
                v9x_glp_uint("SeqTicksPerSecondLow", frequency.LowPart);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                SwapBuffers(hdc);
                v9x_glp_pump();
                glBegin(GL_QUADS);
                glColor3f(1.0f, 0.0f, 0.0f);
                glVertex3f(0.0f, 0.0f, 0.5f);
                glVertex3f((GLfloat)width, 0.0f, 0.5f);
                glVertex3f((GLfloat)width, (GLfloat)height, 0.5f);
                glVertex3f(0.0f, (GLfloat)height, 0.5f);
                glEnd();
                glFinish();
                SwapBuffers(hdc);
                QueryPerformanceCounter(&t0);
                first = (DWORD)GetPixel(hdc, 3 * width / 4, height / 2);
                QueryPerformanceCounter(&t1);
                second = (DWORD)GetPixel(hdc, 3 * width / 4, height / 2);
                QueryPerformanceCounter(&t2);
                v9x_glp_hex("SeqFirstPixel", first);
                v9x_glp_hex("SeqSecondPixel", second);
                v9x_glp_uint("SeqFirstTicks", t1.LowPart - t0.LowPart);
                v9x_glp_uint("SeqSecondTicks", t2.LowPart - t1.LowPart);

                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                SwapBuffers(hdc);
                v9x_glp_pump();
                glBegin(GL_QUADS);
                glColor3f(1.0f, 0.0f, 0.0f);
                glVertex3f(0.0f, 0.0f, 0.5f);
                glVertex3f((GLfloat)width, 0.0f, 0.5f);
                glVertex3f((GLfloat)width, (GLfloat)height, 0.5f);
                glVertex3f(0.0f, (GLfloat)height, 0.5f);
                glEnd();
                glFinish();
                SwapBuffers(hdc);
                v9x_glp_hex("SeqVramFirst", v9x_glp_vram_pixel(
                    window, 3 * width / 4, height / 2));
                v9x_glp_hex("SeqPixelAfterVram",
                            (DWORD)GetPixel(hdc, 3 * width / 4, height / 2));
            }
            /* The same frame in video memory, at eighths of the width
             * along the middle row: left of centre green, right red. */
            {
                char key[24];
                LONG eighth;

                for (eighth = 0; eighth < 8; ++eighth) {
                    wsprintfA(key, "VramGeometryX%ld", eighth);
                    v9x_glp_hex(key, v9x_glp_vram_pixel(
                        window, (width * eighth) / 8 + width / 16,
                        height / 2));
                }
            }

            /*
             * Depth precision: green at window depth 0.25 + 2^-20, then
             * red at 0.25 under LESS. The two are 16 steps apart in a
             * 24-bit buffer and 0.06 of a step in a 16-bit one, so red
             * draws only where the buffer resolves them: DepthFineRead
             * red (FF0000) means more than 16 bits, green 16. Window
             * depth is (1 - z) / 2 under this ortho.
             */
            {
                GLint depth_bits = -1;
                const GLfloat near_z = 0.5f;
                const GLfloat far_z = 0.5f - 2.0f / 1048576.0f;

                glGetIntegerv(GL_DEPTH_BITS, &depth_bits);
                v9x_glp_uint("DepthBits", (DWORD)depth_bits);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glBegin(GL_QUADS);
                glColor3f(0.0f, 1.0f, 0.0f);
                glVertex3f(0.0f, 0.0f, far_z);
                glVertex3f((GLfloat)width, 0.0f, far_z);
                glVertex3f((GLfloat)width, (GLfloat)height, far_z);
                glVertex3f(0.0f, (GLfloat)height, far_z);
                glColor3f(1.0f, 0.0f, 0.0f);
                glVertex3f(0.0f, 0.0f, near_z);
                glVertex3f((GLfloat)width, 0.0f, near_z);
                glVertex3f((GLfloat)width, (GLfloat)height, near_z);
                glVertex3f(0.0f, (GLfloat)height, near_z);
                glEnd();
                glFinish();
                v9x_glp_hex("ErrorAfterDepthFine", (DWORD)glGetError());
                v9x_glp_hex("DepthFineRead",
                            v9x_glp_read(width / 2, height / 2));
                v9x_glp_uint("DepthFineResolved",
                             v9x_glp_read(width / 2, height / 2) ==
                                     0x00FF0000ul
                                 ? 1ul
                                 : 0ul);
            }

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

            /*
             * Mip levels: a 64x64 texture whose seven levels are each one
             * solid colour, NEAREST_MIPMAP_NEAREST and REPLACE, on quads of
             * 64, 16 and 4 pixels - texel-to-pixel ratios 1, 4 and 16, so
             * levels 0, 2 and 4. Each quad's centre pixel names the level
             * the sampler read. Hardware and the CPU must agree; a wrong
             * colour is a level uploaded or laid out wrongly.
             */
            {
                static const GLubyte level_rgb[7][3] = {
                    { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 },
                    { 255, 255, 0 }, { 0, 255, 255 }, { 255, 0, 255 },
                    { 255, 255, 255 }
                };
                static GLubyte image[64 * 64 * 3];
                static const int quad_edge[3] = { 64, 16, 4 };
                static const char *const quad_key[3] = {
                    "MipQuad64", "MipQuad16", "MipQuad4"
                };
                GLuint texture = 0u;
                int level;
                int edge;
                int i;
                int q;

                glGenTextures(1, &texture);
                glBindTexture(GL_TEXTURE_2D, texture);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                for (level = 0, edge = 64; edge >= 1; ++level, edge /= 2) {
                    for (i = 0; i < edge * edge; ++i) {
                        image[i * 3 + 0] = level_rgb[level][0];
                        image[i * 3 + 1] = level_rgb[level][1];
                        image[i * 3 + 2] = level_rgb[level][2];
                    }
                    glTexImage2D(GL_TEXTURE_2D, level, 3, edge, edge, 0,
                                 GL_RGB, GL_UNSIGNED_BYTE, image);
                }
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                GL_NEAREST_MIPMAP_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                GL_NEAREST);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
                glEnable(GL_TEXTURE_2D);
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_BLEND);
                glMatrixMode(GL_PROJECTION);
                glLoadIdentity();
                glOrtho(0.0, (GLdouble)width, 0.0, (GLdouble)height,
                        -1.0, 1.0);
                glMatrixMode(GL_MODELVIEW);
                glLoadIdentity();
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                for (q = 0; q < 3; ++q) {
                    GLfloat x0 = (GLfloat)(20 + q * 100);
                    GLfloat y0 = 40.0f;
                    GLfloat e = (GLfloat)quad_edge[q];

                    glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 0.0f);
                    glVertex2f(x0, y0);
                    glTexCoord2f(1.0f, 0.0f);
                    glVertex2f(x0 + e, y0);
                    glTexCoord2f(1.0f, 1.0f);
                    glVertex2f(x0 + e, y0 + e);
                    glTexCoord2f(0.0f, 1.0f);
                    glVertex2f(x0, y0 + e);
                    glEnd();
                }
                v9x_glp_hex("ErrorAfterMips", (DWORD)glGetError());
                glFinish();
                for (q = 0; q < 3; ++q) {
                    v9x_glp_hex(quad_key[q],
                                v9x_glp_read(20 + q * 100 + quad_edge[q] / 2,
                                             40 + quad_edge[q] / 2));
                }
                glDisable(GL_TEXTURE_2D);
                glDeleteTextures(1, &texture);
            }

            /*
             * Quake 2's sky, reduced: a 256x256 RGB texture of one grey-beige
             * (160, 140, 100), one level, LINEAR both ways, CLAMP, REPLACE,
             * drawn while the current colour is orange (1, 0.5, 0) - which
             * REPLACE must ignore. Then the same texture MODULATE with white,
             * and REPLACE with NEAREST and REPEAT, to separate the filter and
             * the address mode from the environment.
             */
            {
                static GLubyte sky[256 * 256 * 3];
                static const char *const sky_key[3] = {
                    "SkyReplaceLinearClamp", "SkyModulateWhite",
                    "SkyReplaceNearestRepeat"
                };
                GLuint texture = 0u;
                int i;
                int v;

                for (i = 0; i < 256 * 256; ++i) {
                    sky[i * 3 + 0] = 160;
                    sky[i * 3 + 1] = 140;
                    sky[i * 3 + 2] = 100;
                }
                glGenTextures(1, &texture);
                glBindTexture(GL_TEXTURE_2D, texture);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, 3, 256, 256, 0, GL_RGB,
                             GL_UNSIGNED_BYTE, sky);
                glEnable(GL_TEXTURE_2D);
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_BLEND);
                glMatrixMode(GL_PROJECTION);
                glLoadIdentity();
                glOrtho(0.0, (GLdouble)width, 0.0, (GLdouble)height,
                        -1.0, 1.0);
                glMatrixMode(GL_MODELVIEW);
                glLoadIdentity();
                for (v = 0; v < 3; ++v) {
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                    v == 2 ? GL_NEAREST : GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                    v == 2 ? GL_NEAREST : GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                    v == 2 ? GL_REPEAT : GL_CLAMP);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                    v == 2 ? GL_REPEAT : GL_CLAMP);
                    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
                              v == 1 ? GL_MODULATE : GL_REPLACE);
                    if (v == 1) {
                        glColor3f(1.0f, 1.0f, 1.0f);
                    } else {
                        glColor3f(1.0f, 0.5f, 0.0f);
                    }
                    glClear(GL_COLOR_BUFFER_BIT);
                    glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 0.0f);
                    glVertex2f(0.0f, 0.0f);
                    glTexCoord2f(1.0f, 0.0f);
                    glVertex2f((GLfloat)width, 0.0f);
                    glTexCoord2f(1.0f, 1.0f);
                    glVertex2f((GLfloat)width, (GLfloat)height);
                    glTexCoord2f(0.0f, 1.0f);
                    glVertex2f(0.0f, (GLfloat)height);
                    glEnd();
                    glFinish();
                    v9x_glp_hex(sky_key[v],
                                v9x_glp_read(width / 2, height / 2));
                }
                v9x_glp_hex("ErrorAfterSky", (DWORD)glGetError());
                glDisable(GL_TEXTURE_2D);
                glDeleteTextures(1, &texture);
            }

            /*
             * One texture rewritten between draws, as Quake 2 rewrites its
             * dynamic lightmap several times a frame: colour A by
             * glTexImage2D, a quad; B by glTexSubImage2D, a second quad; C,
             * a third. Correct is A, B, C. A later colour on an earlier quad
             * is a write that overtook queued work; an earlier colour on a
             * later quad is a sampler reading stale texels.
             */
            {
                static GLubyte block[64 * 64 * 3];
                static const GLubyte colour[3][3] = {
                    { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }
                };
                static const char *const sub_key[3] = {
                    "SubImageQuadA", "SubImageQuadB", "SubImageQuadC"
                };
                GLuint texture = 0u;
                int i;
                int q;

                glGenTextures(1, &texture);
                glBindTexture(GL_TEXTURE_2D, texture);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                GL_NEAREST);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
                glEnable(GL_TEXTURE_2D);
                glMatrixMode(GL_PROJECTION);
                glLoadIdentity();
                glOrtho(0.0, (GLdouble)width, 0.0, (GLdouble)height,
                        -1.0, 1.0);
                glMatrixMode(GL_MODELVIEW);
                glLoadIdentity();
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                for (q = 0; q < 3; ++q) {
                    GLfloat x0 = (GLfloat)(20 + q * 100);

                    for (i = 0; i < 64 * 64; ++i) {
                        block[i * 3 + 0] = colour[q][0];
                        block[i * 3 + 1] = colour[q][1];
                        block[i * 3 + 2] = colour[q][2];
                    }
                    if (q == 0) {
                        glTexImage2D(GL_TEXTURE_2D, 0, 3, 64, 64, 0, GL_RGB,
                                     GL_UNSIGNED_BYTE, block);
                    } else {
                        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 64, 64,
                                        GL_RGB, GL_UNSIGNED_BYTE, block);
                    }
                    glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 0.0f);
                    glVertex2f(x0, 40.0f);
                    glTexCoord2f(1.0f, 0.0f);
                    glVertex2f(x0 + 64.0f, 40.0f);
                    glTexCoord2f(1.0f, 1.0f);
                    glVertex2f(x0 + 64.0f, 104.0f);
                    glTexCoord2f(0.0f, 1.0f);
                    glVertex2f(x0, 104.0f);
                    glEnd();
                }
                glFinish();
                for (q = 0; q < 3; ++q) {
                    v9x_glp_hex(sub_key[q], v9x_glp_read(52 + q * 100, 72));
                }
                v9x_glp_hex("ErrorAfterSubImage", (DWORD)glGetError());
                glDisable(GL_TEXTURE_2D);
                glDeleteTextures(1, &texture);
            }

            /*
             * Quake 2's translucent surfaces and its lightmap pass, each over
             * a known background, on an RGB texture of (200, 100, 40):
             *   TransModulate: MODULATE, colour (1, 1, 1, 0.33), SRC_ALPHA /
             *     ONE_MINUS_SRC_ALPHA over grey (128) - 0.33 tex + 0.67 grey.
             *   LightmapBlend: REPLACE, ZERO / SRC_COLOR over grey - grey
             *     times the texel, as the lightmap multiply.
             *   LightmapModulate: the same blend under MODULATE with white.
             * Background and expected values are in the record; both engines
             * must agree with the arithmetic.
             */
            {
                static GLubyte tex[64 * 64 * 3];
                static const char *const trans_key[3] = {
                    "TransModulate", "LightmapBlend", "LightmapModulate"
                };
                GLuint texture = 0u;
                int i;
                int v;

                for (i = 0; i < 64 * 64; ++i) {
                    tex[i * 3 + 0] = 200;
                    tex[i * 3 + 1] = 100;
                    tex[i * 3 + 2] = 40;
                }
                glGenTextures(1, &texture);
                glBindTexture(GL_TEXTURE_2D, texture);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, 3, 64, 64, 0, GL_RGB,
                             GL_UNSIGNED_BYTE, tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                GL_LINEAR);
                glMatrixMode(GL_PROJECTION);
                glLoadIdentity();
                glOrtho(0.0, (GLdouble)width, 0.0, (GLdouble)height,
                        -1.0, 1.0);
                glMatrixMode(GL_MODELVIEW);
                glLoadIdentity();
                glDisable(GL_DEPTH_TEST);
                for (v = 0; v < 3; ++v) {
                    glDisable(GL_BLEND);
                    glDisable(GL_TEXTURE_2D);
                    glClearColor(128.0f / 255.0f, 128.0f / 255.0f,
                                 128.0f / 255.0f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT);
                    glEnable(GL_TEXTURE_2D);
                    glEnable(GL_BLEND);
                    if (v == 0) {
                        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
                                  GL_MODULATE);
                        glColor4f(1.0f, 1.0f, 1.0f, 0.33f);
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    } else {
                        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
                                  v == 1 ? GL_REPLACE : GL_MODULATE);
                        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                        glBlendFunc(GL_ZERO, GL_SRC_COLOR);
                    }
                    glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 0.0f);
                    glVertex2f(0.0f, 0.0f);
                    glTexCoord2f(1.0f, 0.0f);
                    glVertex2f((GLfloat)width, 0.0f);
                    glTexCoord2f(1.0f, 1.0f);
                    glVertex2f((GLfloat)width, (GLfloat)height);
                    glTexCoord2f(0.0f, 1.0f);
                    glVertex2f(0.0f, (GLfloat)height);
                    glEnd();
                    glFinish();
                    v9x_glp_hex(trans_key[v],
                                v9x_glp_read(width / 2, height / 2));
                }
                glDisable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ZERO);
                glDisable(GL_TEXTURE_2D);
                v9x_glp_hex("ErrorAfterTrans", (DWORD)glGetError());
                glDeleteTextures(1, &texture);
            }

            /*
             * Quake 2's sky box, reduced: glFrustum with near 4 and far 4096
             * (Quake 2's perspective), depth test LEQUAL against a cleared
             * depth of 1, and a textured quad facing the viewer at z = -2300
             * whose corners reach z = -3900 - so w runs to the thousands and
             * rhw to a few ten-thousandths. The texture is one colour
             * (40, 200, 120); the clear is black. FarQuadCentre and
             * FarQuadEdge read the middle and near a corner.
             */
            {
                static GLubyte tex[64 * 64 * 3];
                GLuint texture = 0u;
                int i;

                for (i = 0; i < 64 * 64; ++i) {
                    tex[i * 3 + 0] = 40;
                    tex[i * 3 + 1] = 200;
                    tex[i * 3 + 2] = 120;
                }
                glGenTextures(1, &texture);
                glBindTexture(GL_TEXTURE_2D, texture);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, 3, 64, 64, 0, GL_RGB,
                             GL_UNSIGNED_BYTE, tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                GL_LINEAR);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
                glEnable(GL_TEXTURE_2D);
                glDisable(GL_BLEND);
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_TRUE);
                glDepthRange(0.0, 1.0);
                glMatrixMode(GL_PROJECTION);
                glLoadIdentity();
                glFrustum(-4.0, 4.0, -3.0, 3.0, 4.0, 4096.0);
                glMatrixMode(GL_MODELVIEW);
                glLoadIdentity();
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClearDepth(1.0);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f);
                glVertex3f(-3000.0f, -2400.0f, -3900.0f);
                glTexCoord2f(1.0f, 0.0f);
                glVertex3f(3000.0f, -2400.0f, -3900.0f);
                glTexCoord2f(1.0f, 1.0f);
                glVertex3f(1800.0f, 1400.0f, -2300.0f);
                glTexCoord2f(0.0f, 1.0f);
                glVertex3f(-1800.0f, 1400.0f, -2300.0f);
                glEnd();
                glFinish();
                v9x_glp_hex("FarQuadCentre", v9x_glp_read(width / 2,
                                                          height / 2));
                v9x_glp_hex("FarQuadEdge", v9x_glp_read(width / 8,
                                                        height / 8));
                v9x_glp_hex("ErrorAfterFar", (DWORD)glGetError());

                /*
                 * A ceiling the camera stands under, clipped at the near
                 * plane as Quake 2's sky box is: the plane y = 100 from
                 * z = +4000 (behind) to -4000, x -4000 to 4000, s and t
                 * running 0 to 1 across it. The texture is a 4x4 grid,
                 * cell (row, column) coloured red 40 + 60 row, green
                 * 40 + 60 column, blue 200. A pixel at ndc (xn, yn) sees
                 * the plane at distance 100 / (0.75 yn), so the cell it
                 * must show is computable: CeilY90, Y50, Y10 are row 1 and
                 * CeilY05 row 0, all column 2; CeilX50Y50 column 2.
                 */
                {
                    static GLubyte grid[64 * 64 * 3];
                    static const int probe_y[4] = { 90, 50, 10, 5 };
                    static const char *const ceil_key[4] = {
                        "CeilY90", "CeilY50", "CeilY10", "CeilY05"
                    };
                    int gx;
                    int gy;
                    int k;

                    for (gy = 0; gy < 64; ++gy) {
                        for (gx = 0; gx < 64; ++gx) {
                            GLubyte *texel = &grid[(gy * 64 + gx) * 3];

                            texel[0] = (GLubyte)(40 + 60 * (gy / 16));
                            texel[1] = (GLubyte)(40 + 60 * (gx / 16));
                            texel[2] = 200;
                        }
                    }
                    glTexImage2D(GL_TEXTURE_2D, 0, 3, 64, 64, 0, GL_RGB,
                                 GL_UNSIGNED_BYTE, grid);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                    GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                    GL_NEAREST);
                    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
                              GL_REPLACE);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 1.0f);
                    glVertex3f(-4000.0f, 100.0f, 4000.0f);
                    glTexCoord2f(1.0f, 1.0f);
                    glVertex3f(4000.0f, 100.0f, 4000.0f);
                    glTexCoord2f(1.0f, 0.0f);
                    glVertex3f(4000.0f, 100.0f, -4000.0f);
                    glTexCoord2f(0.0f, 0.0f);
                    glVertex3f(-4000.0f, 100.0f, -4000.0f);
                    glEnd();
                    glFinish();
                    for (k = 0; k < 4; ++k) {
                        v9x_glp_hex(ceil_key[k],
                                    v9x_glp_read(width / 2,
                                                 height / 2 +
                                                     (height / 2) *
                                                         probe_y[k] / 100));
                    }
                    v9x_glp_hex("CeilX50Y50",
                                v9x_glp_read(width / 2 + width / 4,
                                             height / 2 + height / 4));
                }

                /* The same far quad with an RGBA texture (stored ARGB4444)
                 * of (160, 140, 100, 255): REPLACE while the colour is
                 * orange, then MODULATE with white. */
                {
                    static GLubyte rgba[64 * 64 * 4];
                    int k;
                    int v;

                    for (k = 0; k < 64 * 64; ++k) {
                        rgba[k * 4 + 0] = 160;
                        rgba[k * 4 + 1] = 140;
                        rgba[k * 4 + 2] = 100;
                        rgba[k * 4 + 3] = 255;
                    }
                    glTexImage2D(GL_TEXTURE_2D, 0, 4, 64, 64, 0, GL_RGBA,
                                 GL_UNSIGNED_BYTE, rgba);
                    for (v = 0; v < 2; ++v) {
                        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
                                  v == 0 ? GL_REPLACE : GL_MODULATE);
                        if (v == 0) {
                            glColor3f(1.0f, 0.5f, 0.0f);
                        } else {
                            glColor3f(1.0f, 1.0f, 1.0f);
                        }
                        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                        glBegin(GL_QUADS);
                        glTexCoord2f(0.0f, 0.0f);
                        glVertex3f(-3000.0f, -2400.0f, -3900.0f);
                        glTexCoord2f(1.0f, 0.0f);
                        glVertex3f(3000.0f, -2400.0f, -3900.0f);
                        glTexCoord2f(1.0f, 1.0f);
                        glVertex3f(1800.0f, 1400.0f, -2300.0f);
                        glTexCoord2f(0.0f, 1.0f);
                        glVertex3f(-1800.0f, 1400.0f, -2300.0f);
                        glEnd();
                        glFinish();
                        v9x_glp_hex(v == 0 ? "FarRgbaReplace"
                                           : "FarRgbaModulate",
                                    v9x_glp_read(width / 2, height / 2));
                    }
                    /* MODULATE with orange: the vertex colour must scale
                     * the texel, (160,140,100) x (1, 0.5, 0). */
                    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
                              GL_MODULATE);
                    glColor3f(1.0f, 0.5f, 0.0f);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 0.0f);
                    glVertex3f(-3000.0f, -2400.0f, -3900.0f);
                    glTexCoord2f(1.0f, 0.0f);
                    glVertex3f(3000.0f, -2400.0f, -3900.0f);
                    glTexCoord2f(1.0f, 1.0f);
                    glVertex3f(1800.0f, 1400.0f, -2300.0f);
                    glTexCoord2f(0.0f, 1.0f);
                    glVertex3f(-1800.0f, 1400.0f, -2300.0f);
                    glEnd();
                    glFinish();
                    v9x_glp_hex("FarRgbaModulateOrange",
                                v9x_glp_read(width / 2, height / 2));
                    /* And an RGB texture (RGB565), MODULATE with orange. */
                    glTexImage2D(GL_TEXTURE_2D, 0, 3, 64, 64, 0, GL_RGBA,
                                 GL_UNSIGNED_BYTE, rgba);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 0.0f);
                    glVertex3f(-3000.0f, -2400.0f, -3900.0f);
                    glTexCoord2f(1.0f, 0.0f);
                    glVertex3f(3000.0f, -2400.0f, -3900.0f);
                    glTexCoord2f(1.0f, 1.0f);
                    glVertex3f(1800.0f, 1400.0f, -2300.0f);
                    glTexCoord2f(0.0f, 1.0f);
                    glVertex3f(-1800.0f, 1400.0f, -2300.0f);
                    glEnd();
                    glFinish();
                    v9x_glp_hex("FarRgbModulateOrange",
                                v9x_glp_read(width / 2, height / 2));
                    glColor3f(1.0f, 1.0f, 1.0f);
                    v9x_glp_hex("ErrorAfterFarRgba", (DWORD)glGetError());
                }
                glDisable(GL_TEXTURE_2D);
                glDisable(GL_DEPTH_TEST);
                glDepthFunc(GL_LESS);
                glDeleteTextures(1, &texture);
            }
        }
        /*
         * Non-square textures. A 64x16 map of four column bands by two row
         * halves, cell (band b, half h) coloured red 40 + 60 b, green
         * 60 + 120 h, blue 200, NEAREST on a 256x64 quad: each cell's
         * centre names the texel column and row the sampler used
         * (NonSq64x16B<b>H<h>). The transpose at 16x64 on a 64x256 quad
         * (NonSq16x64...). Then a 64x16 chain, each level one colour
         * (level L: red 30 L, green 255 - 30 L, blue 0), on quads of 64x16
         * and 16x4 pixels: levels 0 and 2 (NonSqMipL0, NonSqMipL2).
         */
        if (context != 0) {
            static GLubyte map[64 * 16 * 3];
            static const char *const cell_key[2][8] = {
                { "NonSq64x16B0H0", "NonSq64x16B1H0", "NonSq64x16B2H0",
                  "NonSq64x16B3H0", "NonSq64x16B0H1", "NonSq64x16B1H1",
                  "NonSq64x16B2H1", "NonSq64x16B3H1" },
                { "NonSq16x64B0H0", "NonSq16x64B1H0", "NonSq16x64B2H0",
                  "NonSq16x64B3H0", "NonSq16x64B0H1", "NonSq16x64B1H1",
                  "NonSq16x64B2H1", "NonSq16x64B3H1" }
            };
            RECT client;
            LONG width;
            LONG height;
            GLuint texture = 0u;
            int pass;
            int x;
            int y;
            int level;
            int w;
            int h;

            GetClientRect(window, &client);
            width = client.right - client.left;
            height = client.bottom - client.top;
            glViewport(0, 0, width, height);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(0.0, (GLdouble)width, 0.0, (GLdouble)height, -1.0, 1.0);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glDrawBuffer(GL_BACK);
            glReadBuffer(GL_BACK);
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
            glEnable(GL_TEXTURE_2D);
            for (pass = 0; pass < 2; ++pass) {
                /* Pass 0: 64 wide, 16 tall, bands along s. Pass 1: 16 wide,
                 * 64 tall, bands along t. The half is along the other axis. */
                int tw = pass == 0 ? 64 : 16;
                int th = pass == 0 ? 16 : 64;
                GLfloat qw = (GLfloat)(pass == 0 ? 256 : 64);
                GLfloat qh = (GLfloat)(pass == 0 ? 64 : 256);
                int b;
                int half;

                for (y = 0; y < th; ++y) {
                    for (x = 0; x < tw; ++x) {
                        GLubyte *texel = &map[(y * tw + x) * 3];

                        b = pass == 0 ? x / 16 : y / 16;
                        half = pass == 0 ? y / 8 : x / 8;
                        texel[0] = (GLubyte)(40 + 60 * b);
                        texel[1] = (GLubyte)(60 + 120 * half);
                        texel[2] = 200;
                    }
                }
                glTexImage2D(GL_TEXTURE_2D, 0, 3, tw, th, 0, GL_RGB,
                             GL_UNSIGNED_BYTE, map);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                GL_NEAREST);
                glClear(GL_COLOR_BUFFER_BIT);
                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f);
                glVertex2f(0.0f, 0.0f);
                glTexCoord2f(1.0f, 0.0f);
                glVertex2f(qw, 0.0f);
                glTexCoord2f(1.0f, 1.0f);
                glVertex2f(qw, qh);
                glTexCoord2f(0.0f, 1.0f);
                glVertex2f(0.0f, qh);
                glEnd();
                glFinish();
                for (half = 0; half < 2; ++half) {
                    for (b = 0; b < 4; ++b) {
                        /* Band centre along the banded axis, half centre
                         * along the other; window y up matches t up. */
                        GLint rx = pass == 0 ? 32 + b * 64 : 16 + half * 32;
                        GLint ry = pass == 0 ? 16 + half * 32 : 32 + b * 64;

                        v9x_glp_hex(cell_key[pass][half * 4 + b],
                                    v9x_glp_read(rx, ry));
                    }
                }
            }
            for (level = 0, w = 64, h = 16; w >= 1 || h >= 1; ++level) {
                for (y = 0; y < w * h; ++y) {
                    map[y * 3 + 0] = (GLubyte)(30 * level);
                    map[y * 3 + 1] = (GLubyte)(255 - 30 * level);
                    map[y * 3 + 2] = 0;
                }
                glTexImage2D(GL_TEXTURE_2D, level, 3, w, h, 0, GL_RGB,
                             GL_UNSIGNED_BYTE, map);
                if (w == 1 && h == 1) {
                    break;
                }
                w = w > 1 ? w / 2 : 1;
                h = h > 1 ? h / 2 : 1;
            }
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            GL_NEAREST_MIPMAP_NEAREST);
            glClear(GL_COLOR_BUFFER_BIT);
            for (pass = 0; pass < 2; ++pass) {
                GLfloat x0 = (GLfloat)(20 + pass * 100);
                GLfloat qw = pass == 0 ? 64.0f : 16.0f;
                GLfloat qh = pass == 0 ? 16.0f : 4.0f;

                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f);
                glVertex2f(x0, 40.0f);
                glTexCoord2f(1.0f, 0.0f);
                glVertex2f(x0 + qw, 40.0f);
                glTexCoord2f(1.0f, 1.0f);
                glVertex2f(x0 + qw, 40.0f + qh);
                glTexCoord2f(0.0f, 1.0f);
                glVertex2f(x0, 40.0f + qh);
                glEnd();
            }
            glFinish();
            v9x_glp_hex("NonSqMipL0", v9x_glp_read(20 + 32, 40 + 8));
            v9x_glp_hex("NonSqMipL2", v9x_glp_read(120 + 8, 40 + 2));
            v9x_glp_hex("ErrorAfterNonSquare", (DWORD)glGetError());
            glDisable(GL_TEXTURE_2D);
            glDeleteTextures(1, &texture);

            /*
             * An alpha-tested texture with a lightmap pass over it, as
             * Half-Life OpenGL draws its fences (2026-10-19: drawn solid
             * on the MGA-2164W, cut out under Direct3D). A 16x16 RGBA
             * map, red, alpha 0 in the left half and 255 in the right,
             * GL_GREATER 0.666 over a green clear on a 64x64 quad with
             * depth on: AlphaHole (left, expect green) and AlphaSolid
             * (right, expect red). Then a grey 128 map over the same quad,
             * ZERO/SRC_COLOR with LEQUAL and no depth write, the lightmap
             * pass: AlphaLitHole (expect green halved, 008000h) and
             * AlphaLitSolid (red halved, 800000h). Each with the pixel to
             * its right (...Next).
             */
            {
                static GLubyte rgba[16 * 16 * 4];
                GLuint maps[2];

                for (y = 0; y < 16; ++y) {
                    for (x = 0; x < 16; ++x) {
                        GLubyte *texel = &rgba[(y * 16 + x) * 4];

                        texel[0] = 255;
                        texel[1] = 0;
                        texel[2] = 0;
                        texel[3] = (GLubyte)(x < 8 ? 0 : 255);
                    }
                }
                glGenTextures(2, maps);
                glBindTexture(GL_TEXTURE_2D, maps[0]);
                glTexImage2D(GL_TEXTURE_2D, 0, 4, 16, 16, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, rgba);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                GL_NEAREST);
                for (y = 0; y < 16 * 16; ++y) {
                    rgba[y * 4 + 0] = 128;
                    rgba[y * 4 + 1] = 128;
                    rgba[y * 4 + 2] = 128;
                    rgba[y * 4 + 3] = 255;
                }
                glBindTexture(GL_TEXTURE_2D, maps[1]);
                glTexImage2D(GL_TEXTURE_2D, 0, 3, 16, 16, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, rgba);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                GL_NEAREST);

                glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
                glClearDepth(1.0);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_TRUE);
                glEnable(GL_TEXTURE_2D);
                glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                for (pass = 0; pass < 2; ++pass) {
                    glBindTexture(GL_TEXTURE_2D, maps[pass]);
                    if (pass == 0) {
                        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
                                  GL_MODULATE);
                        glEnable(GL_ALPHA_TEST);
                        glAlphaFunc(GL_GREATER, 0.666f);
                    } else {
                        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
                                  GL_REPLACE);
                        glDisable(GL_ALPHA_TEST);
                        glDepthMask(GL_FALSE);
                        glEnable(GL_BLEND);
                        glBlendFunc(GL_ZERO, GL_SRC_COLOR);
                    }
                    glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 0.0f);
                    glVertex3f(200.0f, 20.0f, 0.0f);
                    glTexCoord2f(1.0f, 0.0f);
                    glVertex3f(264.0f, 20.0f, 0.0f);
                    glTexCoord2f(1.0f, 1.0f);
                    glVertex3f(264.0f, 84.0f, 0.0f);
                    glTexCoord2f(0.0f, 1.0f);
                    glVertex3f(200.0f, 84.0f, 0.0f);
                    glEnd();
                    glFinish();
                    v9x_glp_hex(pass == 0 ? "AlphaHole" : "AlphaLitHole",
                                v9x_glp_read(216, 52));
                    v9x_glp_hex(pass == 0 ? "AlphaSolid" : "AlphaLitSolid",
                                v9x_glp_read(248, 52));
                    /* The neighbours, for an engine that draws the multiply
                     * as a half stipple. */
                    v9x_glp_hex(pass == 0 ? "AlphaHoleNext"
                                          : "AlphaLitHoleNext",
                                v9x_glp_read(217, 52));
                    v9x_glp_hex(pass == 0 ? "AlphaSolidNext"
                                          : "AlphaLitSolidNext",
                                v9x_glp_read(249, 52));
                }
                v9x_glp_hex("ErrorAfterAlpha", (DWORD)glGetError());
                glDisable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ZERO);
                glDepthMask(GL_TRUE);
                glDepthFunc(GL_LESS);
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_TEXTURE_2D);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glDeleteTextures(2, maps);
            }

            /*
             * Small textures, which Serious Sam draws with (1x1 layers):
             * 1x1 red, 2x2 green, 4x4 blue, 4x1 yellow, 1x4 cyan, each
             * one solid colour, NEAREST, MODULATE with white, on a 16x16
             * quad. The centre must be the texture's colour (Small1x1,
             * Small2x2, Small4x4, Small4x1, Small1x4).
             */
            {
                static const int small_w[5] = { 1, 2, 4, 4, 1 };
                static const int small_h[5] = { 1, 2, 4, 1, 4 };
                static const GLubyte small_rgb[5][3] = {
                    { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 },
                    { 255, 255, 0 }, { 0, 255, 255 }
                };
                static const char *const small_key[5] = {
                    "Small1x1", "Small2x2", "Small4x4", "Small4x1", "Small1x4"
                };
                GLuint small_tex[5];
                int k;
                int t;

                glGenTextures(5, small_tex);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                glColor3f(1.0f, 1.0f, 1.0f);
                glEnable(GL_TEXTURE_2D);
                glClear(GL_COLOR_BUFFER_BIT);
                for (k = 0; k < 5; ++k) {
                    GLfloat x0 = (GLfloat)(20 + k * 30);

                    for (t = 0; t < small_w[k] * small_h[k]; ++t) {
                        map[t * 3 + 0] = small_rgb[k][0];
                        map[t * 3 + 1] = small_rgb[k][1];
                        map[t * 3 + 2] = small_rgb[k][2];
                    }
                    glBindTexture(GL_TEXTURE_2D, small_tex[k]);
                    glTexImage2D(GL_TEXTURE_2D, 0, 3, small_w[k], small_h[k],
                                 0, GL_RGB, GL_UNSIGNED_BYTE, map);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                    GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                    GL_NEAREST);
                    glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 0.0f);
                    glVertex2f(x0, 40.0f);
                    glTexCoord2f(1.0f, 0.0f);
                    glVertex2f(x0 + 16.0f, 40.0f);
                    glTexCoord2f(1.0f, 1.0f);
                    glVertex2f(x0 + 16.0f, 56.0f);
                    glTexCoord2f(0.0f, 1.0f);
                    glVertex2f(x0, 56.0f);
                    glEnd();
                }
                glFinish();
                for (k = 0; k < 5; ++k) {
                    v9x_glp_hex(small_key[k], v9x_glp_read(28 + k * 30, 48));
                }
                v9x_glp_hex("ErrorAfterSmall", (DWORD)glGetError());
                glDisable(GL_TEXTURE_2D);
                glDeleteTextures(5, small_tex);
            }

            /*
             * The scissor as geometry: a scissor of x 40..80, y 30..70 and
             * a quad over the whole window, red over a black clear. Inside
             * red, just outside black (ScissorGeomIn, ScissorGeomLeft,
             * ScissorGeomRight, ScissorGeomBelow, ScissorGeomAbove).
             */
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_SCISSOR_TEST);
            glScissor(40, 30, 40, 40);
            glColor3f(1.0f, 0.0f, 0.0f);
            glBegin(GL_QUADS);
            glVertex2f(0.0f, 0.0f);
            glVertex2f((GLfloat)width, 0.0f);
            glVertex2f((GLfloat)width, (GLfloat)height);
            glVertex2f(0.0f, (GLfloat)height);
            glEnd();
            glDisable(GL_SCISSOR_TEST);
            glFinish();
            v9x_glp_hex("ScissorGeomIn", v9x_glp_read(60, 50));
            v9x_glp_hex("ScissorGeomInCorner", v9x_glp_read(40, 30));
            v9x_glp_hex("ScissorGeomInFar", v9x_glp_read(79, 69));
            v9x_glp_hex("ScissorGeomLeft", v9x_glp_read(39, 50));
            v9x_glp_hex("ScissorGeomRight", v9x_glp_read(80, 50));
            v9x_glp_hex("ScissorGeomBelow", v9x_glp_read(60, 29));
            v9x_glp_hex("ScissorGeomAbove", v9x_glp_read(60, 70));
            glColor3f(1.0f, 1.0f, 1.0f);
        }
        /*
         * Front-buffer drawing, as GLQuake draws its loading disc: a black
         * frame swapped to the window, then with glDrawBuffer(GL_FRONT) a
         * yellow quad over the lower-left quarter and a glFlush, no swap.
         * The screen must show it at once (GDI), the front read it
         * (glReadBuffer(GL_FRONT)), and the back still hold black. A quad
         * drawn with GL_NONE must change neither.
         */
        if (context != 0) {
            RECT client;
            LONG width;
            LONG height;

            GetClientRect(window, &client);
            width = client.right - client.left;
            height = client.bottom - client.top;
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glViewport(0, 0, width, height);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(0.0, (GLdouble)width, 0.0, (GLdouble)height, -1.0, 1.0);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glDrawBuffer(GL_BACK);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glFinish();
            SwapBuffers(hdc);
            v9x_glp_pump();
            glDrawBuffer(GL_FRONT);
            glColor3f(1.0f, 1.0f, 0.0f);
            glBegin(GL_QUADS);
            glVertex2f(0.0f, 0.0f);
            glVertex2f((GLfloat)(width / 2), 0.0f);
            glVertex2f((GLfloat)(width / 2), (GLfloat)(height / 2));
            glVertex2f(0.0f, (GLfloat)(height / 2));
            glEnd();
            glFlush();
            v9x_glp_pump();
            v9x_glp_hex("FrontDrawScreen",
                        (DWORD)GetPixel(hdc, width / 4, height - height / 4));
            v9x_glp_hex("FrontDrawScreenOutside",
                        (DWORD)GetPixel(hdc, 3 * width / 4, height / 4));
            glReadBuffer(GL_FRONT);
            v9x_glp_hex("FrontReadFront",
                        v9x_glp_read(width / 4, height / 4));
            v9x_glp_hex("FrontReadFrontAgain",
                        v9x_glp_read(width / 4, height / 4));
            v9x_glp_hex("FrontVramScreen", v9x_glp_vram_pixel(
                window, width / 4, height - height / 4));
            glReadBuffer(GL_BACK);
            v9x_glp_hex("FrontReadBack", v9x_glp_read(width / 4, height / 4));
            glDrawBuffer(GL_NONE);
            glColor3f(1.0f, 0.0f, 1.0f);
            glBegin(GL_QUADS);
            glVertex2f(0.0f, 0.0f);
            glVertex2f((GLfloat)width, 0.0f);
            glVertex2f((GLfloat)width, (GLfloat)height);
            glVertex2f(0.0f, (GLfloat)height);
            glEnd();
            glFinish();
            glReadBuffer(GL_BACK);
            v9x_glp_hex("NoneReadBack",
                        v9x_glp_read(3 * width / 4, 3 * height / 4));
            glReadBuffer(GL_FRONT);
            v9x_glp_hex("NoneReadFront",
                        v9x_glp_read(3 * width / 4, 3 * height / 4));
            glDrawBuffer(GL_BACK);
            glReadBuffer(GL_BACK);
            v9x_glp_hex("ErrorAfterFront", (DWORD)glGetError());

            /* The front draw again with 200 ms between the swap and the
             * draw. Yellow here and black above means the swap's copy
             * into the front buffer landed after the draw. */
            glClear(GL_COLOR_BUFFER_BIT);
            glFinish();
            SwapBuffers(hdc);
            v9x_glp_pump();
            Sleep(200);
            glDrawBuffer(GL_FRONT);
            glColor3f(1.0f, 1.0f, 0.0f);
            glBegin(GL_QUADS);
            glVertex2f(0.0f, 0.0f);
            glVertex2f((GLfloat)(width / 2), 0.0f);
            glVertex2f((GLfloat)(width / 2), (GLfloat)(height / 2));
            glVertex2f(0.0f, (GLfloat)(height / 2));
            glEnd();
            glFlush();
            glReadBuffer(GL_FRONT);
            v9x_glp_hex("FrontReadFrontSettled",
                        v9x_glp_read(width / 4, height / 4));
            glDrawBuffer(GL_BACK);
            glReadBuffer(GL_BACK);
        }
        /*
         * Lifetime: 50 cycles of wglCreateContext, wglMakeCurrent, a 64x64
         * mipmapped texture, a textured clear-and-draw, a swap, then
         * wglMakeCurrent(0) and wglDeleteContext - with the window resized
         * at cycle 25, so the drawable's surfaces are remade once. Free
         * video memory after the first cycle and after the last must
         * agree: every texture chain and context goes back.
         */
        if (context != 0) {
            static GLubyte image[64 * 64 * 3];
            DWORD free_first = 0ul;
            DWORD free_last = 0ul;
            DWORD failures = 0ul;
            int cycle;
            int i;

            for (i = 0; i < 64 * 64 * 3; ++i) {
                image[i] = (GLubyte)(i * 7);
            }
            wglMakeCurrent(0, 0);
            for (cycle = 0; cycle < 50; ++cycle) {
                HGLRC cycle_context;
                GLuint texture = 0u;
                int level;
                int edge;

                if (cycle == 25) {
                    SetWindowPos(window, 0, 0, 0, 360, 280,
                                 SWP_NOMOVE | SWP_NOZORDER);
                    v9x_glp_pump();
                }
                cycle_context = wglCreateContext(hdc);
                if (cycle_context == 0 || !wglMakeCurrent(hdc, cycle_context)) {
                    ++failures;
                    if (cycle_context != 0) {
                        wglDeleteContext(cycle_context);
                    }
                    continue;
                }
                glGenTextures(1, &texture);
                glBindTexture(GL_TEXTURE_2D, texture);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                for (level = 0, edge = 64; edge >= 1; ++level, edge /= 2) {
                    glTexImage2D(GL_TEXTURE_2D, level, 3, edge, edge, 0,
                                 GL_RGB, GL_UNSIGNED_BYTE, image);
                }
                glEnable(GL_TEXTURE_2D);
                glClear(GL_COLOR_BUFFER_BIT);
                glBegin(GL_TRIANGLES);
                glTexCoord2f(0.0f, 0.0f);
                glVertex2f(-1.0f, -1.0f);
                glTexCoord2f(1.0f, 0.0f);
                glVertex2f(1.0f, -1.0f);
                glTexCoord2f(0.0f, 1.0f);
                glVertex2f(-1.0f, 1.0f);
                glEnd();
                glFinish();
                SwapBuffers(hdc);
                if (glGetError() != GL_NO_ERROR) {
                    ++failures;
                }
                wglMakeCurrent(0, 0);
                if (!wglDeleteContext(cycle_context)) {
                    ++failures;
                }
                v9x_glp_pump();
                if (cycle == 26) {
                    free_first = v9x_glp_vram_free();
                }
            }
            free_last = v9x_glp_vram_free();
            v9x_glp_uint("LifetimeCycles", 50ul);
            v9x_glp_uint("LifetimeFailures", failures);
            v9x_glp_uint("LifetimeVramFreeFirst", free_first);
            v9x_glp_uint("LifetimeVramFreeLast", free_last);
            wglMakeCurrent(hdc, context);
        }
        {
            RECT client;

            v9x_glp_pump();
            GetClientRect(window, &client);
            v9x_glp_sgis(client.right - client.left,
                         client.bottom - client.top);
            v9x_glp_arb(client.right - client.left,
                        client.bottom - client.top);
            v9x_glp_sgis_mip(client.right - client.left,
                             client.bottom - client.top);
            v9x_glp_polygon_offset(client.right - client.left,
                                   client.bottom - client.top);
            v9x_glp_fog(client.right - client.left,
                        client.bottom - client.top);
            v9x_glp_lit(client.right - client.left,
                        client.bottom - client.top);
            v9x_glp_replace_rgb(client.right - client.left,
                                client.bottom - client.top);
            v9x_glp_texel_phase(client.right - client.left,
                                client.bottom - client.top);
            v9x_glp_attrib(client.right - client.left,
                           client.bottom - client.top);
            v9x_glp_tnl(hdc, context, client.right - client.left,
                        client.bottom - client.top);
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
