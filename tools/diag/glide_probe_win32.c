/*
 * Glide probe (docs\plans\glide-2x-wrapper.md, Phase 2).
 *
 * Drives GLIDE2X.DLL - loaded from this program's own directory, the way a
 * game loads it - through what Phase 2 gives it, and reads every result
 * back through grLfbLock rather than a screenshot, which a page-flipping
 * application blinds:
 *
 *   - grSstWinOpen 640x480 with two colour buffers and a depth buffer, on
 *     no window of its own (the DLL makes one, as for NFS II SE's 0);
 *   - grBufferClear into the back buffer, then a flip: the cleared colour
 *     must be in the front buffer, and a second clear only in the back;
 *   - an untextured white triangle, inside and outside it;
 *   - the W-buffer: a farther triangle rejected, a nearer one accepted;
 *   - the clip window, a line, and the snap bias of 3 << 18.
 *
 * The pixels are 565 (white FFFF, red F800, green 07E0, blue 001F). Every
 * check is written with what it read to C:\V9XDIAG\V9XGLIDP.INI, and
 * Result is PASS only if all are exact. Imports KERNEL32 and USER32 only.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"

#define V9X_GLIDP_SECTION "GlideProbe"

/* Glide 2.4 values the probe passes (src\glide\glide_api.h has them all). */
#define GLIDP_RES_640X480     7ul
#define GLIDP_COLORFORMAT_ARGB 0ul
#define GLIDP_ORIGIN_UPPER    0ul
#define GLIDP_BUFFER_FRONT    0ul
#define GLIDP_BUFFER_BACK     1ul
#define GLIDP_LFB_READ_ONLY   0ul
#define GLIDP_LFBWRITE_ANY    0xFFul
#define GLIDP_DEPTH_DISABLE   0ul
#define GLIDP_DEPTH_WBUFFER   2ul
#define GLIDP_CMP_LEQUAL      3ul
#define GLIDP_CMP_ALWAYS      7ul
#define GLIDP_COMBINE_LOCAL   1ul
#define GLIDP_LOCAL_ITERATED  0ul
#define GLIDP_OTHER_CONSTANT  2ul
#define GLIDP_BLEND_ZERO      0ul
#define GLIDP_BLEND_ONE       4ul
#define GLIDP_SNAP            786432.0f

#define GLIDP_WHITE 0xFFFFul
#define GLIDP_RED   0xF800ul
#define GLIDP_GREEN 0x07E0ul
#define GLIDP_BLUE  0x001Ful
#define GLIDP_BLACK 0x0000ul

/* A GrVertex built for three TMUs: x y z r g b ooz a oow, then three of
 * sow tow oow. */
#define GLIDP_VERTEX_FLOATS 18

typedef void (__stdcall *GLIDP_V0)(void);
typedef void (__stdcall *GLIDP_V1)(DWORD);
typedef void (__stdcall *GLIDP_V2)(DWORD, DWORD);
typedef void (__stdcall *GLIDP_V3)(DWORD, DWORD, DWORD);
typedef void (__stdcall *GLIDP_V4)(DWORD, DWORD, DWORD, DWORD);
typedef void (__stdcall *GLIDP_V5)(DWORD, DWORD, DWORD, DWORD, DWORD);
typedef DWORD (__stdcall *GLIDP_R1)(void *);
typedef DWORD (__stdcall *GLIDP_R2)(DWORD, DWORD);
typedef DWORD (__stdcall *GLIDP_OPEN)(DWORD, DWORD, DWORD, DWORD, DWORD,
                                      DWORD, DWORD);
typedef DWORD (__stdcall *GLIDP_LOCK)(DWORD, DWORD, DWORD, DWORD, DWORD,
                                      DWORD *);
typedef void (__stdcall *GLIDP_TRI)(const float *, const float *,
                                    const float *);
typedef void (__stdcall *GLIDP_LINE)(const float *, const float *);
typedef void (__stdcall *GLIDP_TEX)(DWORD, DWORD, DWORD, const DWORD *);
typedef void (__stdcall *GLIDP_TABLE)(DWORD, DWORD, const void *);
typedef void (__stdcall *GLIDP_FOGTABLE)(const BYTE *);

/* Phase 3: GrTexInfo is small LOD, large LOD, aspect, format, data. An
 * 8x8 texture is LOD 5 (256 >> 5); aspect 3 is square. */
#define GLIDP_LOD_8           5ul
#define GLIDP_ASPECT_1X1      3ul
#define GLIDP_TEXFMT_P_8      5ul
#define GLIDP_TEXFMT_RGB_565  10ul
#define GLIDP_EVENODD_BOTH    3ul
#define GLIDP_TEXTABLE_PALETTE 2ul
#define GLIDP_COMBINE_SCALE_OTHER 3ul
#define GLIDP_FACTOR_LOCAL    1ul
#define GLIDP_OTHER_TEXTURE   1ul
#define GLIDP_TEXCOMBINE_DECAL 1ul
#define GLIDP_TEXTURE_POINT   0ul
#define GLIDP_TEXTURE_CLAMP   1ul
#define GLIDP_CHROMAKEY_ENABLE 1ul
#define GLIDP_BLEND_SRC_ALPHA 1ul
#define GLIDP_BLEND_ONE_MINUS_SRC_ALPHA 5ul
#define GLIDP_FOG_TABLE       2ul
#define GLIDP_TEXELS          64u

static HMODULE glidp_dll;
static unsigned int glidp_failures;

static void glidp_text(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_GLIDP_SECTION, key, value, V9X_DIAG_GLIDP_INI);
}

static FARPROC glidp_proc(const char *name)
{
    FARPROC proc = GetProcAddress(glidp_dll, name);

    if (proc == 0) {
        glidp_text("MissingExport", name);
        glidp_text("Result", "FAIL");
        ExitProcess(2u);
    }
    return proc;
}

static void glidp_vertex(float *v, float x, float y, float oow, float r,
                         float g, float b)
{
    int i;

    for (i = 0; i < GLIDP_VERTEX_FLOATS; ++i) {
        v[i] = 0.0f;
    }
    v[0] = x;
    v[1] = y;
    v[3] = r;
    v[4] = g;
    v[5] = b;
    v[7] = 255.0f;
    v[8] = oow;
}

/* One 565 pixel of a buffer, through the DLL's own frame-buffer lock. */
static DWORD glidp_read(DWORD buffer, DWORD x, DWORD y)
{
    GLIDP_LOCK lock = (GLIDP_LOCK)glidp_proc("_grLfbLock@24");
    GLIDP_R2 unlock = (GLIDP_R2)glidp_proc("_grLfbUnlock@8");
    DWORD info[5];
    DWORD value;

    info[0] = sizeof(info);
    info[1] = 0ul;
    info[2] = 0ul;
    info[3] = 0ul;
    info[4] = 0ul;
    if (!lock(GLIDP_LFB_READ_ONLY, buffer, GLIDP_LFBWRITE_ANY, GLIDP_ORIGIN_UPPER,
              0ul, info) || info[1] == 0ul) {
        return 0xFFFFFFFFul;
    }
    value = *(const WORD *)((const BYTE *)info[1] + y * info[2] + x * 2ul);
    unlock(GLIDP_LFB_READ_ONLY, buffer);
    return value;
}

static void glidp_check(const char *key, DWORD buffer, DWORD x, DWORD y,
                        DWORD expected)
{
    char text[64];
    DWORD value = glidp_read(buffer, x, y);

    if (value != expected) {
        ++glidp_failures;
    }
    wsprintfA(text, "%s read=%04lX expected=%04lX at=%lu,%lu",
              value == expected ? "PASS" : "FAIL", value, expected, x, y);
    glidp_text(key, text);
}

/* Blended pixels round differently per engine: each 565 channel within
 * one step of the expected. */
static void glidp_check_near(const char *key, DWORD buffer, DWORD x, DWORD y,
                             DWORD expected)
{
    char text[64];
    DWORD value = glidp_read(buffer, x, y);
    long dr = (long)((value >> 11) & 31ul) - (long)((expected >> 11) & 31ul);
    long dg = (long)((value >> 5) & 63ul) - (long)((expected >> 5) & 63ul);
    long db = (long)(value & 31ul) - (long)(expected & 31ul);
    int ok = value != 0xFFFFFFFFul && dr >= -1l && dr <= 1l && dg >= -1l &&
             dg <= 1l && db >= -1l && db <= 1l;

    if (!ok) {
        ++glidp_failures;
    }
    wsprintfA(text, "%s read=%04lX expected=%04lX+-1 at=%lu,%lu",
              ok ? "PASS" : "FAIL", value, expected, x, y);
    glidp_text(key, text);
}

/* A textured square, s and t over the whole texture, white vertices. */
static void glidp_quad(GLIDP_TRI triangle, float x, float y, float edge,
                       float oow, float alpha)
{
    float a[GLIDP_VERTEX_FLOATS];
    float b[GLIDP_VERTEX_FLOATS];
    float c[GLIDP_VERTEX_FLOATS];
    float d[GLIDP_VERTEX_FLOATS];

    glidp_vertex(a, x, y, oow, 255.0f, 255.0f, 255.0f);
    glidp_vertex(b, x + edge, y, oow, 255.0f, 255.0f, 255.0f);
    glidp_vertex(c, x, y + edge, oow, 255.0f, 255.0f, 255.0f);
    glidp_vertex(d, x + edge, y + edge, oow, 255.0f, 255.0f, 255.0f);
    a[7] = alpha; b[7] = alpha; c[7] = alpha; d[7] = alpha;
    /* sow and tow are s/w and t/w; Glide's s and t span 0..256. */
    b[9] = 256.0f * oow;
    c[10] = 256.0f * oow;
    d[9] = 256.0f * oow;
    d[10] = 256.0f * oow;
    triangle(a, b, c);
    triangle(b, d, c);
}

void __stdcall V9xGlideProbeEntry(void)
{
    GLIDP_V0 init;
    GLIDP_V0 shutdown;
    GLIDP_V0 close;
    GLIDP_R1 query;
    GLIDP_V1 select;
    GLIDP_OPEN open;
    GLIDP_V3 clear;
    GLIDP_V1 swap;
    GLIDP_V1 render_buffer;
    GLIDP_V4 clip;
    GLIDP_V5 color_combine;
    GLIDP_V5 alpha_combine;
    GLIDP_V4 blend;
    GLIDP_V1 alpha_test;
    GLIDP_V1 cull;
    GLIDP_V1 depth_mode;
    GLIDP_V1 depth_function;
    GLIDP_V1 depth_mask;
    GLIDP_TRI triangle;
    GLIDP_LINE line;
    GLIDP_TEX download;
    GLIDP_TEX source;
    GLIDP_TABLE table;
    GLIDP_V2 tex_function;
    GLIDP_V3 tex_filter;
    GLIDP_V3 tex_clamp;
    GLIDP_V1 chroma_mode;
    GLIDP_V1 chroma_value;
    GLIDP_V1 fog_mode;
    GLIDP_V1 fog_color;
    GLIDP_FOGTABLE fog_table;
    WORD texels565[GLIDP_TEXELS];
    WORD green565[GLIDP_TEXELS];
    BYTE texels8[GLIDP_TEXELS];
    DWORD palette[256];
    BYTE fog[64];
    DWORD info[5];
    unsigned int i;
    DWORD hardware[64];
    float a[GLIDP_VERTEX_FLOATS];
    float b[GLIDP_VERTEX_FLOATS];
    float c[GLIDP_VERTEX_FLOATS];
    char text[96];

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    WritePrivateProfileStringA(V9X_GLIDP_SECTION, 0, 0, V9X_DIAG_GLIDP_INI);
    glidp_text("Build", V9X_BUILD_ID);
    glidp_text("Result", "INCOMPLETE");
    glidp_dll = LoadLibraryA("GLIDE2X.DLL");
    if (glidp_dll == 0) {
        glidp_text("Result", "FAIL no GLIDE2X.DLL");
        ExitProcess(1u);
    }
    init = (GLIDP_V0)glidp_proc("_grGlideInit@0");
    shutdown = (GLIDP_V0)glidp_proc("_grGlideShutdown@0");
    close = (GLIDP_V0)glidp_proc("_grSstWinClose@0");
    query = (GLIDP_R1)glidp_proc("_grSstQueryHardware@4");
    select = (GLIDP_V1)glidp_proc("_grSstSelect@4");
    open = (GLIDP_OPEN)glidp_proc("_grSstWinOpen@28");
    clear = (GLIDP_V3)glidp_proc("_grBufferClear@12");
    swap = (GLIDP_V1)glidp_proc("_grBufferSwap@4");
    render_buffer = (GLIDP_V1)glidp_proc("_grRenderBuffer@4");
    clip = (GLIDP_V4)glidp_proc("_grClipWindow@16");
    color_combine = (GLIDP_V5)glidp_proc("_grColorCombine@20");
    alpha_combine = (GLIDP_V5)glidp_proc("_grAlphaCombine@20");
    blend = (GLIDP_V4)glidp_proc("_grAlphaBlendFunction@16");
    alpha_test = (GLIDP_V1)glidp_proc("_grAlphaTestFunction@4");
    cull = (GLIDP_V1)glidp_proc("_grCullMode@4");
    depth_mode = (GLIDP_V1)glidp_proc("_grDepthBufferMode@4");
    depth_function = (GLIDP_V1)glidp_proc("_grDepthBufferFunction@4");
    depth_mask = (GLIDP_V1)glidp_proc("_grDepthMask@4");
    triangle = (GLIDP_TRI)glidp_proc("_grDrawTriangle@12");
    line = (GLIDP_LINE)glidp_proc("_grDrawLine@8");
    download = (GLIDP_TEX)glidp_proc("_grTexDownloadMipMap@16");
    source = (GLIDP_TEX)glidp_proc("_grTexSource@16");
    table = (GLIDP_TABLE)glidp_proc("_grTexDownloadTable@12");
    tex_function = (GLIDP_V2)glidp_proc("_grTexCombineFunction@8");
    tex_filter = (GLIDP_V3)glidp_proc("_grTexFilterMode@12");
    tex_clamp = (GLIDP_V3)glidp_proc("_grTexClampMode@12");
    chroma_mode = (GLIDP_V1)glidp_proc("_grChromakeyMode@4");
    chroma_value = (GLIDP_V1)glidp_proc("_grChromakeyValue@4");
    fog_mode = (GLIDP_V1)glidp_proc("_grFogMode@4");
    fog_color = (GLIDP_V1)glidp_proc("_grFogColorValue@4");
    fog_table = (GLIDP_FOGTABLE)glidp_proc("_grFogTable@4");

    init();
    query(hardware);
    select(0ul);
    if (!open(0ul, GLIDP_RES_640X480, 0ul, GLIDP_COLORFORMAT_ARGB,
              GLIDP_ORIGIN_UPPER, 2ul, 1ul)) {
        glidp_text("Open", "FAIL");
        glidp_text("Result", "FAIL open");
        shutdown();
        ExitProcess(1u);
    }
    glidp_text("Open", "PASS 640x480 two buffers and depth");

    /* Clears and the flip. */
    depth_mode(GLIDP_DEPTH_DISABLE);
    render_buffer(GLIDP_BUFFER_BACK);
    clip(0ul, 0ul, 640ul, 480ul);
    clear(0x00FF0000ul, 0ul, 0xFFFFul);
    glidp_check("ClearBack", GLIDP_BUFFER_BACK, 10ul, 10ul, GLIDP_RED);
    swap(1ul);
    glidp_check("FlipFront", GLIDP_BUFFER_FRONT, 10ul, 10ul, GLIDP_RED);
    clear(0x0000FF00ul, 0ul, 0xFFFFul);
    glidp_check("ClearBackAgain", GLIDP_BUFFER_BACK, 10ul, 10ul, GLIDP_GREEN);
    glidp_check("FrontKept", GLIDP_BUFFER_FRONT, 10ul, 10ul, GLIDP_RED);

    /* An untextured Gouraud triangle: colour LOCAL on iterated, no blend. */
    color_combine(GLIDP_COMBINE_LOCAL, 0ul, GLIDP_LOCAL_ITERATED,
                  GLIDP_OTHER_CONSTANT, 0ul);
    alpha_combine(GLIDP_COMBINE_LOCAL, 0ul, GLIDP_LOCAL_ITERATED,
                  GLIDP_OTHER_CONSTANT, 0ul);
    blend(GLIDP_BLEND_ONE, GLIDP_BLEND_ZERO, GLIDP_BLEND_ONE, GLIDP_BLEND_ZERO);
    alpha_test(GLIDP_CMP_ALWAYS);
    cull(0ul);
    glidp_vertex(a, 100.0f, 100.0f, 1.0f, 255.0f, 255.0f, 255.0f);
    glidp_vertex(b, 300.0f, 100.0f, 1.0f, 255.0f, 255.0f, 255.0f);
    glidp_vertex(c, 100.0f, 300.0f, 1.0f, 255.0f, 255.0f, 255.0f);
    triangle(a, b, c);
    glidp_check("Triangle", GLIDP_BUFFER_BACK, 120ul, 120ul, GLIDP_WHITE);
    glidp_check("TriangleOutside", GLIDP_BUFFER_BACK, 290ul, 290ul, GLIDP_GREEN);

    /* The W-buffer: blue at W 2, then red at W 4 (farther, rejected by
     * LEQUAL), then red at W 1 (nearer, accepted). */
    depth_mode(GLIDP_DEPTH_WBUFFER);
    depth_function(GLIDP_CMP_LEQUAL);
    depth_mask(1ul);
    clear(0x00000000ul, 0ul, 0xFFFFul);
    glidp_vertex(a, 300.0f, 100.0f, 0.5f, 0.0f, 0.0f, 255.0f);
    glidp_vertex(b, 500.0f, 100.0f, 0.5f, 0.0f, 0.0f, 255.0f);
    glidp_vertex(c, 300.0f, 300.0f, 0.5f, 0.0f, 0.0f, 255.0f);
    triangle(a, b, c);
    glidp_check("DepthFirst", GLIDP_BUFFER_BACK, 320ul, 120ul, GLIDP_BLUE);
    glidp_vertex(a, 300.0f, 100.0f, 0.25f, 255.0f, 0.0f, 0.0f);
    glidp_vertex(b, 500.0f, 100.0f, 0.25f, 255.0f, 0.0f, 0.0f);
    glidp_vertex(c, 300.0f, 300.0f, 0.25f, 255.0f, 0.0f, 0.0f);
    triangle(a, b, c);
    glidp_check("DepthReject", GLIDP_BUFFER_BACK, 320ul, 120ul, GLIDP_BLUE);
    glidp_vertex(a, 300.0f, 100.0f, 1.0f, 255.0f, 0.0f, 0.0f);
    glidp_vertex(b, 500.0f, 100.0f, 1.0f, 255.0f, 0.0f, 0.0f);
    glidp_vertex(c, 300.0f, 300.0f, 1.0f, 255.0f, 0.0f, 0.0f);
    triangle(a, b, c);
    glidp_check("DepthAccept", GLIDP_BUFFER_BACK, 320ul, 120ul, GLIDP_RED);

    /* The clip window cuts a screen-sized triangle to 50x50. */
    depth_mode(GLIDP_DEPTH_DISABLE);
    clip(0ul, 0ul, 50ul, 50ul);
    glidp_vertex(a, 0.0f, 0.0f, 1.0f, 255.0f, 255.0f, 255.0f);
    glidp_vertex(b, 639.0f, 0.0f, 1.0f, 255.0f, 255.0f, 255.0f);
    glidp_vertex(c, 0.0f, 479.0f, 1.0f, 255.0f, 255.0f, 255.0f);
    triangle(a, b, c);
    glidp_check("ClipInside", GLIDP_BUFFER_BACK, 10ul, 10ul, GLIDP_WHITE);
    glidp_check("ClipOutside", GLIDP_BUFFER_BACK, 60ul, 10ul, GLIDP_BLACK);
    clip(0ul, 0ul, 640ul, 480ul);

    /* A horizontal HUD-style line, and a triangle carrying the snap bias. */
    glidp_vertex(a, 10.0f, 400.0f, 1.0f, 255.0f, 255.0f, 255.0f);
    glidp_vertex(b, 200.0f, 400.0f, 1.0f, 255.0f, 255.0f, 255.0f);
    line(a, b);
    glidp_check("Line", GLIDP_BUFFER_BACK, 100ul, 400ul, GLIDP_WHITE);
    glidp_vertex(a, GLIDP_SNAP + 400.0f, GLIDP_SNAP + 350.0f, 1.0f, 255.0f, 255.0f, 255.0f);
    glidp_vertex(b, GLIDP_SNAP + 500.0f, GLIDP_SNAP + 350.0f, 1.0f, 255.0f, 255.0f, 255.0f);
    glidp_vertex(c, GLIDP_SNAP + 400.0f, GLIDP_SNAP + 450.0f, 1.0f, 255.0f, 255.0f, 255.0f);
    triangle(a, b, c);
    glidp_check("Snap", GLIDP_BUFFER_BACK, 410ul, 360ul, GLIDP_WHITE);

    /* Phase 3. Textures, texture times white Gouraud (MODULATE), point
     * sampled and clamped so every texel read is exact. */
    color_combine(GLIDP_COMBINE_SCALE_OTHER, GLIDP_FACTOR_LOCAL,
                  GLIDP_LOCAL_ITERATED, GLIDP_OTHER_TEXTURE, 0ul);
    alpha_combine(GLIDP_COMBINE_SCALE_OTHER, GLIDP_FACTOR_LOCAL,
                  GLIDP_LOCAL_ITERATED, GLIDP_OTHER_TEXTURE, 0ul);
    tex_function(0ul, GLIDP_TEXCOMBINE_DECAL);
    tex_filter(0ul, GLIDP_TEXTURE_POINT, GLIDP_TEXTURE_POINT);
    tex_clamp(0ul, GLIDP_TEXTURE_CLAMP, GLIDP_TEXTURE_CLAMP);
    info[0] = GLIDP_LOD_8;
    info[1] = GLIDP_LOD_8;
    info[2] = GLIDP_ASPECT_1X1;

    /* An RGB565 texture, all red. */
    for (i = 0u; i < GLIDP_TEXELS; ++i) {
        texels565[i] = (WORD)GLIDP_RED;
        green565[i] = (WORD)GLIDP_GREEN;
        texels8[i] = 1u;
    }
    info[3] = GLIDP_TEXFMT_RGB_565;
    info[4] = (DWORD)texels565;
    download(0ul, 0x0ul, GLIDP_EVENODD_BOTH, info);
    source(0ul, 0x0ul, GLIDP_EVENODD_BOTH, info);
    glidp_quad(triangle, 100.0f, 100.0f, 64.0f, 1.0f, 255.0f);
    glidp_check("Texture565", GLIDP_BUFFER_BACK, 120ul, 120ul, GLIDP_RED);

    /* P_8 through the palette: entry 1 green, then blue. */
    for (i = 0u; i < 256u; ++i) {
        palette[i] = 0xFF000000ul;
    }
    palette[1] = 0xFF00FF00ul;
    table(0ul, GLIDP_TEXTABLE_PALETTE, palette);
    info[3] = GLIDP_TEXFMT_P_8;
    info[4] = (DWORD)texels8;
    download(0ul, 0x1000ul, GLIDP_EVENODD_BOTH, info);
    source(0ul, 0x1000ul, GLIDP_EVENODD_BOTH, info);
    glidp_quad(triangle, 200.0f, 100.0f, 64.0f, 1.0f, 255.0f);
    glidp_check("TexturePalette", GLIDP_BUFFER_BACK, 220ul, 120ul, GLIDP_GREEN);
    palette[1] = 0xFF0000FFul;
    table(0ul, GLIDP_TEXTABLE_PALETTE, palette);
    glidp_quad(triangle, 200.0f, 100.0f, 64.0f, 1.0f, 255.0f);
    glidp_check("TexturePaletteChange", GLIDP_BUFFER_BACK, 220ul, 120ul,
                GLIDP_BLUE);

    /* The chroma key: a green texture keyed on green leaves the red under
     * it; with the key off it covers it. */
    info[3] = GLIDP_TEXFMT_RGB_565;
    info[4] = (DWORD)green565;
    download(0ul, 0x2000ul, GLIDP_EVENODD_BOTH, info);
    source(0ul, 0x2000ul, GLIDP_EVENODD_BOTH, info);
    chroma_value(0x0000FF00ul);
    chroma_mode(GLIDP_CHROMAKEY_ENABLE);
    glidp_quad(triangle, 100.0f, 100.0f, 64.0f, 1.0f, 255.0f);
    glidp_check("ChromaKeyed", GLIDP_BUFFER_BACK, 120ul, 120ul, GLIDP_RED);
    chroma_mode(0ul);
    glidp_quad(triangle, 100.0f, 100.0f, 64.0f, 1.0f, 255.0f);
    glidp_check("ChromaOff", GLIDP_BUFFER_BACK, 120ul, 120ul, GLIDP_GREEN);

    /* Untextured from here: white at alpha 128 blended over black. */
    color_combine(GLIDP_COMBINE_LOCAL, 0ul, GLIDP_LOCAL_ITERATED,
                  GLIDP_OTHER_CONSTANT, 0ul);
    alpha_combine(GLIDP_COMBINE_LOCAL, 0ul, GLIDP_LOCAL_ITERATED,
                  GLIDP_OTHER_CONSTANT, 0ul);
    blend(GLIDP_BLEND_SRC_ALPHA, GLIDP_BLEND_ONE_MINUS_SRC_ALPHA,
          GLIDP_BLEND_ZERO, GLIDP_BLEND_ZERO);
    glidp_quad(triangle, 550.0f, 300.0f, 64.0f, 1.0f, 128.0f);
    glidp_check_near("BlendHalf", GLIDP_BUFFER_BACK, 570ul, 320ul, 0x8410ul);

    /* Table fog at full strength everywhere, blue: the fog colour must
     * replace the white the triangle carries. */
    blend(GLIDP_BLEND_ONE, GLIDP_BLEND_ZERO, GLIDP_BLEND_ONE, GLIDP_BLEND_ZERO);
    for (i = 0u; i < 64u; ++i) {
        fog[i] = 255u;
    }
    fog_table(fog);
    fog_color(0x000000FFul);
    fog_mode(GLIDP_FOG_TABLE);
    glidp_quad(triangle, 550.0f, 380.0f, 64.0f, 0.5f, 255.0f);
    glidp_check_near("FogTable", GLIDP_BUFFER_BACK, 570ul, 400ul, GLIDP_BLUE);
    fog_mode(0ul);

    /* Show the result for a moment, then give the desktop back. */
    swap(1ul);
    Sleep(2000ul);
    close();
    shutdown();
    wsprintfA(text, "%s failures=%u", glidp_failures == 0u ? "PASS" : "FAIL",
              glidp_failures);
    glidp_text("Result", text);
    ExitProcess(glidp_failures == 0u ? 0u : 1u);
}
