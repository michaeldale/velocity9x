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
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
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
            v9x_glp_sgis_mip(client.right - client.left,
                             client.bottom - client.top);
            v9x_glp_polygon_offset(client.right - client.left,
                                   client.bottom - client.top);
            v9x_glp_fog(client.right - client.left,
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
