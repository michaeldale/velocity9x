/*
 * ATI3DCIF.DLL, census build (docs\plans\ati-3dcif-wrapper.md, Phase 1).
 *
 * Every ATI3DCIF entry point ATI's DLL exports, each logging to
 * C:\V9XDIAG\V9XCIF.LOG and drawing nothing. A demo or title run against
 * it says which calls, vertex types, states, texture formats and surface
 * pointers it really uses, so the real DLL is aimed at that list - the
 * method of the Glide 2 and Glide 3 census builds.
 *
 * Unlike Glide, 3DCIF does not own the screen: the application creates
 * its DirectDraw surfaces on our HAL and hands this DLL raw pointers and
 * pitches (C3D_ERS_SURF_DRAW_PTR and friends, C3D_TMAP.apvLevels). The
 * census logs those pointers so they can be set against the HAL's surface
 * list, and checksums each registered texture's top level again when it
 * is selected, to see whether a title rewrites a texture in place.
 *
 * Each entry point is exported twice, as ATI's are: the __stdcall function
 * and ATI3DCIF_<name>_lib, a variable holding a pointer to it. The demos
 * find only the second, through GetProcAddress, and call through it.
 *
 * Constants and layouts are ATI3DCIF.H (SDK 4.1, $Revision 2.41) and the
 * SDK-C02700 Rev 1.30 guide, read for facts under the licence rule in the
 * plan; nothing is copied.
 *
 * Runtime-free like the other Velocity9x DLLs: KERNEL32 and USER32 only.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"

/* ---- ATI3DCIF.H facts ------------------------------------------------ */

/* C3D_EC */
#define V9X_C3D_EC_OK              0ul
#define V9X_C3D_EC_GENFAIL         1ul
#define V9X_C3D_EC_MEMALLOCFAIL    2ul
#define V9X_C3D_EC_BADPARAM        3ul
#define V9X_C3D_EC_NOTIMPYET       6ul

/* C3D_3DCIFINFO is twelve 32-bit fields. The guide: u32Size must be the
 * structure's size or GetInfo returns BADPARAM. Build 17 and earlier had
 * no caps words, so a client of that era passes the first seven. */
#define V9X_C3D_INFO_BYTES         48ul
#define V9X_C3D_INFO_BUILD17_BYTES 28ul

/* u32CIFCaps1 and u32CIFCaps2 flags. */
#define V9X_C3D_CAPS1_FOG          0x00000001ul
#define V9X_C3D_CAPS1_POINT        0x00000002ul
#define V9X_C3D_CAPS1_RECT         0x00000004ul
#define V9X_C3D_CAPS1_Z_BUFFER     0x00000008ul
#define V9X_C3D_CAPS1_CI4_TMAP     0x00000010ul
#define V9X_C3D_CAPS1_CI8_TMAP     0x00000020ul
#define V9X_C3D_CAPS1_DITHER_EN    0x00000080ul
#define V9X_C3D_CAPS1_ENH_PERSP    0x00000100ul
#define V9X_C3D_CAPS1_SCISSOR      0x00000200ul
#define V9X_C3D_CAPS2_COMPOSITE    0x00000001ul
#define V9X_C3D_CAPS2_CLAMP        0x00000002ul
#define V9X_C3D_CAPS2_DST_ALPHA    0x00000004ul

/* C3D_EVERTEX and the two live vertex sizes: C3D_VTCF is ten floats,
 * C3D_TLVERTEX eight 32-bit fields plus the three of its composite. */
#define V9X_C3D_EV_VTCF            3ul
#define V9X_C3D_EV_TLVERTEX        4ul
#define V9X_C3D_VTCF_BYTES         40u
#define V9X_C3D_TLVERTEX_BYTES     44u

/* C3D_ERSID: 37 states. The two rectangles are four C3D_INT32 each; every
 * other state is one 32-bit value. */
#define V9X_C3D_ERS_VERTEX_TYPE    1u
#define V9X_C3D_ERS_PRIM_TYPE      2u
#define V9X_C3D_ERS_TMAP_SELECT    6u
#define V9X_C3D_ERS_SURF_DRAW_PTR  13u
#define V9X_C3D_ERS_SURF_DRAW_PITCH 14u
#define V9X_C3D_ERS_SURF_DRAW_PF   15u
#define V9X_C3D_ERS_SURF_VPORT     16u
#define V9X_C3D_ERS_SURF_Z_PTR     21u
#define V9X_C3D_ERS_SURF_Z_PITCH   22u
#define V9X_C3D_ERS_SURF_SCISSOR   23u
#define V9X_C3D_ERS_NUM            37u

/* The defaults ATI3DCIF.H gives each state: vertex type VTCF, triangles,
 * smooth shading, perspective THREE, MINPNT_MAG2BY2, source ONE,
 * destination ZERO, dither on, Z compare ALWAYS, composite function MAX,
 * composite factor 8, composite filter MIN2BY2_MAG2BY2, destination-alpha
 * test ALWAYS. The surface defaults ("Desktop Surface") stay zero here:
 * a census that sees a draw with them unset logs it as such. */
static const v9x_u32 v9x_cif_state_default[V9X_C3D_ERS_NUM] = {
    0ul, 3ul, 1ul, 0ul, 3ul, 0ul, 0ul, 0ul, 3ul, 1ul,
    0ul, 1ul, 0ul, 0ul, 0ul, 0ul, 0ul, 0ul, 1ul, 7ul,
    0ul, 0ul, 0ul, 0ul, 0ul, 0ul, 3ul, 8ul, 2ul, 0ul,
    0ul, 0ul, 7ul, 0ul, 0ul, 0ul, 0ul
};

static const char *const v9x_cif_state_names[V9X_C3D_ERS_NUM] = {
    "FG_CLR", "VERTEX_TYPE", "PRIM_TYPE", "SOLID_CLR", "SHADE_MODE",
    "TMAP_EN", "TMAP_SELECT", "TMAP_LIGHT", "TMAP_PERSP_COR", "TMAP_FILTER",
    "TMAP_TEXOP", "ALPHA_SRC", "ALPHA_DST", "SURF_DRAW_PTR",
    "SURF_DRAW_PITCH", "SURF_DRAW_PF", "SURF_VPORT", "FOG_EN", "DITHER_EN",
    "Z_CMP_FNC", "Z_MODE", "SURF_Z_PTR", "SURF_Z_PITCH", "SURF_SCISSOR",
    "COMPOSITE_EN", "COMPOSITE_SELECT", "COMPOSITE_FNC", "COMPOSITE_FACTOR",
    "COMPOSITE_FILTER", "COMPOSITE_FACTOR_ALPHA", "LOD_BIAS_LEVEL",
    "ALPHA_DST_TEST_ENABLE", "ALPHA_DST_TEST_FNC", "ALPHA_DST_WRITE_SELECT",
    "ALPHA_DST_REFERENCE", "SPECULAR_EN", "ENHANCED_COLOR_RANGE_EN"
};

/* C3D_TMAP byte offsets. 72 bytes end at htxpalTexPalette; the Rage PRO
 * header adds bClampS, bClampT, bAlphaBlend and eTexTiling (88). */
#define V9X_C3D_TMAP_MIPMAP        4u
#define V9X_C3D_TMAP_LEVELS        8u
#define V9X_C3D_TMAP_MAX_LEVELS    11u
#define V9X_C3D_TMAP_XLG2          52u
#define V9X_C3D_TMAP_YLG2          56u
#define V9X_C3D_TMAP_FORMAT        60u
#define V9X_C3D_TMAP_CHROMA        64u
#define V9X_C3D_TMAP_PALETTE       68u
#define V9X_C3D_TMAP_BASE_BYTES    72u
#define V9X_C3D_TMAP_CLAMP_S       72u
#define V9X_C3D_TMAP_CLAMP_T       76u
#define V9X_C3D_TMAP_ALPHA_BLEND   80u
#define V9X_C3D_TMAP_TILING        84u
#define V9X_C3D_TMAP_RAGE3_BYTES   88u

/* C3D_ETEXFMT values and their texel sizes in bits. VQ is one byte of
 * codebook index per 2x2 block: two bits a texel. */
#define V9X_C3D_ETF_CI4            1ul
#define V9X_C3D_ETF_CI8            2ul
#define V9X_C3D_ETF_RGB1555        3ul
#define V9X_C3D_ETF_RGB565         4ul
#define V9X_C3D_ETF_RGB8888        6ul
#define V9X_C3D_ETF_RGB332         7ul
#define V9X_C3D_ETF_Y8             8ul
#define V9X_C3D_ETF_YUV422         11ul
#define V9X_C3D_ETF_RGB4444        15ul
#define V9X_C3D_ETF_VQ             20ul

/* C3D_ECI_TMAP_TYPE: palette entry counts for the log. */
#define V9X_C3D_ECI_4BIT_HI        1ul
#define V9X_C3D_ECI_4BIT_LOW       2ul

/* ---- the device the census describes --------------------------------- */

/*
 * The guide gives no ASIC id values. 0x401 is chosen, not measured: it is
 * the lowest u32ASICID the SDK 4.1 Knight demos accept as a Rage PRO
 * (AtiDemo.cpp refuses below it), so they too run to their drawing calls.
 * The caps are the Rage PRO set less LOAD_OBJECT (bus-master loading the
 * census cannot do) and texture tiling. The frame buffer and heap
 * pointers are zero, as GLCIF reports for Tomb Raider: a title that
 * derives its surface pointers from them shows it in the log. The real
 * DLL's identity is the plan's open Identity decision.
 */
#define V9X_CIF_ASIC_ID           0x401ul
#define V9X_CIF_ASIC_REVISION     0ul
#define V9X_CIF_TOTAL_RAM         (4ul * 1024ul * 1024ul)
#define V9X_CIF_OFFSCREEN_SIZE    (2ul * 1024ul * 1024ul)
#define V9X_CIF_CAPS1 (V9X_C3D_CAPS1_FOG | V9X_C3D_CAPS1_POINT | \
                       V9X_C3D_CAPS1_RECT | V9X_C3D_CAPS1_Z_BUFFER | \
                       V9X_C3D_CAPS1_CI4_TMAP | V9X_C3D_CAPS1_CI8_TMAP | \
                       V9X_C3D_CAPS1_DITHER_EN | V9X_C3D_CAPS1_ENH_PERSP | \
                       V9X_C3D_CAPS1_SCISSOR)
#define V9X_CIF_CAPS2 (V9X_C3D_CAPS2_COMPOSITE | V9X_C3D_CAPS2_CLAMP | \
                       V9X_C3D_CAPS2_DST_ALPHA)

/* ---- log volume ------------------------------------------------------ */

/*
 * The Glide census rules: whole frames (the first few and every Nth, a
 * frame being a RenderBegin to RenderEnd), each export's first calls, and
 * each distinct key once. A draw's key leaves out its vertex pointer,
 * which differs on every call. Counts go out every V9X_CIF_SUMMARY_MS
 * and at Term and detach.
 */
#define V9X_CIF_EXPORTS          19u
#define V9X_CIF_CAPTURE_FIRST    3ul
#define V9X_CIF_CAPTURE_EVERY    600ul
#define V9X_CIF_CAPTURE_LINES    2000ul
#define V9X_CIF_SAMPLE_FIRST     16ul
#define V9X_CIF_SEEN_SLOTS       8192u
#define V9X_CIF_SUMMARY_MS       15000ul
/* Vertices dumped from a captured draw, and draws per frame dumped. */
#define V9X_CIF_DUMP_VERTICES    3u
#define V9X_CIF_DUMP_DRAWS       12ul

/* Fixed pools: ATI's early DLLs allowed one context at a time. */
#define V9X_CIF_CONTEXTS         8u
#define V9X_CIF_TEXTURES         2048u
#define V9X_CIF_PALETTES         256u
/* A texture is re-checksummed on its first selections and then every Nth,
 * bounding the cost of reading it back out of video memory. */
#define V9X_CIF_RECHECK_FIRST    4ul
#define V9X_CIF_RECHECK_EVERY    256ul
/* The largest top level checksummed: 1024x1024 at 32 bits. */
#define V9X_CIF_CHECKSUM_MAX     (4ul * 1024ul * 1024ul)

#define V9X_CIF_IX_Init                  0u
#define V9X_CIF_IX_Term                  1u
#define V9X_CIF_IX_GetInfo               2u
#define V9X_CIF_IX_TextureReg            3u
#define V9X_CIF_IX_TextureUnreg          4u
#define V9X_CIF_IX_TexturePaletteCreate  5u
#define V9X_CIF_IX_TexturePaletteDestroy 6u
#define V9X_CIF_IX_TexturePaletteAnimate 7u
#define V9X_CIF_IX_LoadObject            8u
#define V9X_CIF_IX_ContextCreate         9u
#define V9X_CIF_IX_ContextDestroy        10u
#define V9X_CIF_IX_ContextSetState       11u
#define V9X_CIF_IX_RenderBegin           12u
#define V9X_CIF_IX_RenderEnd             13u
#define V9X_CIF_IX_RenderSwitch          14u
#define V9X_CIF_IX_RenderPrimStrip       15u
#define V9X_CIF_IX_RenderPrimList        16u
#define V9X_CIF_IX_RenderPrimMesh        17u
#define V9X_CIF_IX_RenderIndexedPrim     18u

static const char *const v9x_cif_names[V9X_CIF_EXPORTS] = {
    "Init", "Term", "GetInfo", "TextureReg", "TextureUnreg",
    "TexturePaletteCreate", "TexturePaletteDestroy", "TexturePaletteAnimate",
    "LoadObject", "ContextCreate", "ContextDestroy", "ContextSetState",
    "RenderBegin", "RenderEnd", "RenderSwitch", "RenderPrimStrip",
    "RenderPrimList", "RenderPrimMesh", "RenderIndexedPrim"
};

struct v9x_cif_context {
    v9x_u32 live;
    v9x_u32 state[V9X_C3D_ERS_NUM];
    v9x_u32 rect[2][4];   /* SURF_VPORT, SURF_SCISSOR */
    v9x_u32 set_once[2];  /* bit per state id: set at least once */
};

struct v9x_cif_texture {
    v9x_u32 live;
    v9x_u32 level0;
    v9x_u32 bytes;
    v9x_u32 checksum;
    v9x_u32 selections;
    v9x_u32 rewrites;
};

static struct v9x_cif_context v9x_cif_contexts[V9X_CIF_CONTEXTS];
static struct v9x_cif_texture v9x_cif_textures[V9X_CIF_TEXTURES];
static v9x_u32 v9x_cif_palettes[V9X_CIF_PALETTES];
static struct v9x_cif_context *v9x_cif_current;

static v9x_u32 v9x_cif_sequence;
static v9x_u32 v9x_cif_calls[V9X_CIF_EXPORTS];
static v9x_u32 v9x_cif_seen[V9X_CIF_SEEN_SLOTS];
static v9x_u32 v9x_cif_seen_full;
static v9x_u32 v9x_cif_capturing = 1ul;
static v9x_u32 v9x_cif_capture_lines;
static v9x_u32 v9x_cif_frames;
static v9x_u32 v9x_cif_frame_draws;
static v9x_u32 v9x_cif_vertices;
static v9x_u32 v9x_cif_summary_tick;
static CRITICAL_SECTION v9x_cif_lock;

/* ---- the log --------------------------------------------------------- */

static void v9x_cif_log(const char *text)
{
    HANDLE file;
    char line[480];
    DWORD written;
    int length;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    file = CreateFileA(V9X_DIAG_CIF_LOG, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    SetFilePointer(file, 0, 0, FILE_END);
    length = wsprintfA(line, "%lu t=%lu %s\r\n", ++v9x_cif_sequence,
                       GetTickCount(), text);
    WriteFile(file, line, (DWORD)length, &written, 0);
    CloseHandle(file);
}

/* Whether this key has been logged before: a fixed open-addressed set of
 * hashes; once full, nothing new is added. */
static int v9x_cif_seen_before(unsigned int ix, const v9x_u32 *key,
                               unsigned int count)
{
    v9x_u32 hash = 2166136261ul ^ (v9x_u32)ix;
    unsigned int index;
    unsigned int probe;

    for (index = 0u; index < count; ++index) {
        hash = (hash ^ key[index]) * 16777619ul;
    }
    if (hash == 0ul) {
        hash = 1ul;
    }
    for (probe = 0u; probe < 16u; ++probe) {
        v9x_u32 *slot = &v9x_cif_seen[(hash + probe) % V9X_CIF_SEEN_SLOTS];

        if (*slot == hash) {
            return 1;
        }
        if (*slot == 0ul) {
            *slot = hash;
            return 0;
        }
    }
    if (!v9x_cif_seen_full) {
        v9x_cif_seen_full = 1ul;
        v9x_cif_log("distinct-key set full; only samples and frames log now");
    }
    return 1;
}

static void v9x_cif_summary(const char *why)
{
    char text[160];
    unsigned int ix;

    wsprintfA(text, "summary %s frames=%lu vertices=%lu", why,
              v9x_cif_frames, v9x_cif_vertices);
    v9x_cif_log(text);
    for (ix = 0u; ix < V9X_CIF_EXPORTS; ++ix) {
        if (v9x_cif_calls[ix] != 0ul) {
            wsprintfA(text, "  %s %lu", v9x_cif_names[ix], v9x_cif_calls[ix]);
            v9x_cif_log(text);
        }
    }
}

/* Whether a line about this call should be written: in a captured frame,
 * among the export's first calls, or a key not seen before. The caller
 * holds the lock. */
static int v9x_cif_should_log(unsigned int ix, v9x_u32 call,
                              const v9x_u32 *key, unsigned int count)
{
    if (v9x_cif_capturing && v9x_cif_capture_lines < V9X_CIF_CAPTURE_LINES) {
        ++v9x_cif_capture_lines;
        return 1;
    }
    if (call <= V9X_CIF_SAMPLE_FIRST) {
        return 1;
    }
    return !v9x_cif_seen_before(ix, key, count);
}

/*
 * Every export comes through here: count the call, and log the export's
 * name with its raw arguments if v9x_cif_should_log says so. key/keys
 * is what makes two calls the same for the distinct rule. Returns the
 * call number, and whether it logged through *logged.
 */
static v9x_u32 v9x_cif_call(unsigned int ix, const v9x_u32 *args,
                            unsigned int count, const v9x_u32 *key,
                            unsigned int keys, int *logged)
{
    char text[400];
    v9x_u32 call;
    int length;
    unsigned int index;
    int log_it;
    DWORD now;

    call = ++v9x_cif_calls[ix];
    log_it = v9x_cif_should_log(ix, call, key, keys);
    if (log_it) {
        length = wsprintfA(text, "%s #%lu", v9x_cif_names[ix], call);
        for (index = 0u; index < count && length < 360; ++index) {
            length += wsprintfA(text + length, " %08lX", args[index]);
        }
        v9x_cif_log(text);
    }
    now = GetTickCount();
    if (now - v9x_cif_summary_tick >= V9X_CIF_SUMMARY_MS) {
        v9x_cif_summary_tick = now;
        v9x_cif_summary("periodic");
    }
    if (logged != 0) {
        *logged = log_it;
    }
    return call;
}

/* Reads a 32-bit word the application handed over, or 0 if the pointer
 * is not readable: a census must not fault where the real DLL would
 * return BADPARAM. */
static v9x_u32 v9x_cif_peek(v9x_u32 address)
{
    if (address == 0ul || IsBadReadPtr((const void *)address, 4u)) {
        return 0ul;
    }
    return *(const v9x_u32 *)address;
}

/* ---- module ---------------------------------------------------------- */

v9x_u32 __stdcall ATI3DCIF_Init(void);
v9x_u32 __stdcall ATI3DCIF_Init(void)
{
    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_Init, 0, 0u, 0, 0u, 0);
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

v9x_u32 __stdcall ATI3DCIF_Term(void);
v9x_u32 __stdcall ATI3DCIF_Term(void)
{
    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_Term, 0, 0u, 0, 0u, 0);
    v9x_cif_summary("term");
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/*
 * GetInfo fills as much of C3D_3DCIFINFO as the client's u32Size covers:
 * the full 48 bytes, or the 28 of a build-17 client without caps words.
 * Anything smaller, or a size the census has not seen, is logged; a size
 * below 28 is BADPARAM, as the guide says for a wrong size.
 */
v9x_u32 __stdcall ATI3DCIF_GetInfo(v9x_u32 info);
v9x_u32 __stdcall ATI3DCIF_GetInfo(v9x_u32 info)
{
    v9x_u32 values[12];
    v9x_u32 *out = (v9x_u32 *)info;
    v9x_u32 size;
    v9x_u32 index;
    char text[200];

    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_GetInfo, &info, 1u, &info, 1u, 0);
    size = v9x_cif_peek(info);
    if (size < V9X_C3D_INFO_BUILD17_BYTES ||
        IsBadWritePtr(out, (UINT)size)) {
        wsprintfA(text, "GetInfo size=%lu refused BADPARAM", size);
        v9x_cif_log(text);
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_BADPARAM;
    }
    values[0] = size;
    values[1] = 0ul;
    values[2] = 0ul;
    values[3] = V9X_CIF_OFFSCREEN_SIZE;
    values[4] = V9X_CIF_TOTAL_RAM;
    values[5] = V9X_CIF_ASIC_ID;
    values[6] = V9X_CIF_ASIC_REVISION;
    values[7] = V9X_CIF_CAPS1;
    values[8] = V9X_CIF_CAPS2;
    values[9] = 0ul;
    values[10] = 0ul;
    values[11] = 0ul;
    for (index = 1ul; index < 12ul && (index + 1ul) * 4ul <= size; ++index) {
        out[index] = values[index];
    }
    wsprintfA(text, "GetInfo size=%lu asic=%lX rev=%lu ram=%lu caps1=%08lX "
              "caps2=%08lX", size, V9X_CIF_ASIC_ID, V9X_CIF_ASIC_REVISION,
              V9X_CIF_TOTAL_RAM, V9X_CIF_CAPS1, V9X_CIF_CAPS2);
    v9x_cif_log(text);
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/* LoadObject is gated on C3D_CAPS1_LOAD_OBJECT, which the census does not
 * advertise; a title that calls it anyway shows here. */
v9x_u32 __stdcall ATI3DCIF_LoadObject(v9x_u32 source, v9x_u32 dest,
                                             v9x_u32 size);
v9x_u32 __stdcall ATI3DCIF_LoadObject(v9x_u32 source, v9x_u32 dest,
                                             v9x_u32 size)
{
    v9x_u32 args[3];

    args[0] = source;
    args[1] = dest;
    args[2] = size;
    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_LoadObject, args, 3u, &size, 1u, 0);
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_NOTIMPYET;
}

/* ---- textures -------------------------------------------------------- */

/* Bits a texel in a C3D_ETEXFMT, 0 for a value the header does not name. */
static v9x_u32 v9x_cif_texel_bits(v9x_u32 format)
{
    switch (format) {
    case V9X_C3D_ETF_CI4:     return 4ul;
    case V9X_C3D_ETF_CI8:     return 8ul;
    case V9X_C3D_ETF_RGB332:  return 8ul;
    case V9X_C3D_ETF_Y8:      return 8ul;
    case V9X_C3D_ETF_RGB1555: return 16ul;
    case V9X_C3D_ETF_RGB565:  return 16ul;
    case V9X_C3D_ETF_YUV422:  return 16ul;
    case V9X_C3D_ETF_RGB4444: return 16ul;
    case V9X_C3D_ETF_RGB8888: return 32ul;
    case V9X_C3D_ETF_VQ:      return 2ul;
    default:                  return 0ul;
    }
}

/* FNV-1a over a texture level, 0 for an unreadable one. */
static v9x_u32 v9x_cif_checksum(v9x_u32 address, v9x_u32 bytes)
{
    const v9x_u8 *data = (const v9x_u8 *)address;
    v9x_u32 hash = 2166136261ul;
    v9x_u32 index;

    if (address == 0ul || bytes == 0ul || IsBadReadPtr(data, (UINT)bytes)) {
        return 0ul;
    }
    for (index = 0ul; index < bytes; ++index) {
        hash = (hash ^ data[index]) * 16777619ul;
    }
    return hash;
}

/*
 * TextureReg(ptmapToReg, phtmap): logs the C3D_TMAP as far as u32Size
 * covers it, keeps the top level's address, size and checksum, and hands
 * back a handle: the slot's address. Nothing is copied or uploaded.
 */
v9x_u32 __stdcall ATI3DCIF_TextureReg(v9x_u32 tmap, v9x_u32 handle);
v9x_u32 __stdcall ATI3DCIF_TextureReg(v9x_u32 tmap, v9x_u32 handle)
{
    v9x_u32 args[2];
    v9x_u32 key[6];
    v9x_u32 size;
    v9x_u32 levels = 0ul;
    v9x_u32 format;
    v9x_u32 xlg2;
    v9x_u32 ylg2;
    v9x_u32 bits;
    v9x_u32 index;
    struct v9x_cif_texture *slot = 0;
    char text[400];

    args[0] = tmap;
    args[1] = handle;
    EnterCriticalSection(&v9x_cif_lock);
    size = v9x_cif_peek(tmap);
    format = v9x_cif_peek(tmap + V9X_C3D_TMAP_FORMAT);
    xlg2 = v9x_cif_peek(tmap + V9X_C3D_TMAP_XLG2);
    ylg2 = v9x_cif_peek(tmap + V9X_C3D_TMAP_YLG2);
    key[0] = size;
    key[1] = format;
    key[2] = xlg2;
    key[3] = ylg2;
    key[4] = v9x_cif_peek(tmap + V9X_C3D_TMAP_MIPMAP);
    key[5] = v9x_cif_peek(tmap + V9X_C3D_TMAP_PALETTE) != 0ul ? 1ul : 0ul;
    v9x_cif_call(V9X_CIF_IX_TextureReg, args, 2u, key, 6u, 0);
    if (size < V9X_C3D_TMAP_BASE_BYTES || handle == 0ul ||
        IsBadWritePtr((void *)handle, 4u) || xlg2 > 10ul || ylg2 > 10ul) {
        wsprintfA(text, "TextureReg size=%lu fmt=%lu %lux%lu refused BADPARAM",
                  size, format, xlg2, ylg2);
        v9x_cif_log(text);
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_BADPARAM;
    }
    for (index = 0ul; index < V9X_CIF_TEXTURES; ++index) {
        if (!v9x_cif_textures[index].live) {
            slot = &v9x_cif_textures[index];
            break;
        }
    }
    if (slot == 0) {
        v9x_cif_log("TextureReg pool full MEMALLOCFAIL");
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_MEMALLOCFAIL;
    }
    for (index = 0ul; index < V9X_C3D_TMAP_MAX_LEVELS; ++index) {
        if (v9x_cif_peek(tmap + V9X_C3D_TMAP_LEVELS + index * 4ul) != 0ul) {
            ++levels;
        }
    }
    bits = v9x_cif_texel_bits(format);
    slot->live = 1ul;
    slot->level0 = v9x_cif_peek(tmap + V9X_C3D_TMAP_LEVELS);
    slot->bytes = ((1ul << xlg2) * (1ul << ylg2) * bits) / 8ul;
    if (slot->bytes > V9X_CIF_CHECKSUM_MAX) {
        slot->bytes = V9X_CIF_CHECKSUM_MAX;
    }
    slot->checksum = v9x_cif_checksum(slot->level0, slot->bytes);
    slot->selections = 0ul;
    slot->rewrites = 0ul;
    *(v9x_u32 *)handle = (v9x_u32)slot;
    wsprintfA(text, "TextureReg htx=%08lX size=%lu mip=%lu levels=%lu "
              "%lux%lu fmt=%lu chroma=%08lX pal=%08lX level0=%08lX "
              "level1=%08lX bytes=%lu sum=%08lX",
              (v9x_u32)slot, size, key[4], levels, 1ul << xlg2, 1ul << ylg2,
              format, v9x_cif_peek(tmap + V9X_C3D_TMAP_CHROMA),
              v9x_cif_peek(tmap + V9X_C3D_TMAP_PALETTE), slot->level0,
              v9x_cif_peek(tmap + V9X_C3D_TMAP_LEVELS + 4ul), slot->bytes,
              slot->checksum);
    v9x_cif_log(text);
    if (size >= V9X_C3D_TMAP_RAGE3_BYTES) {
        wsprintfA(text, "TextureReg htx=%08lX clampS=%lu clampT=%lu "
                  "alphaBlend=%lu tiling=%lu", (v9x_u32)slot,
                  v9x_cif_peek(tmap + V9X_C3D_TMAP_CLAMP_S),
                  v9x_cif_peek(tmap + V9X_C3D_TMAP_CLAMP_T),
                  v9x_cif_peek(tmap + V9X_C3D_TMAP_ALPHA_BLEND),
                  v9x_cif_peek(tmap + V9X_C3D_TMAP_TILING));
        v9x_cif_log(text);
    }
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

static struct v9x_cif_texture *v9x_cif_texture_from(v9x_u32 handle)
{
    v9x_u32 base = (v9x_u32)&v9x_cif_textures[0];
    v9x_u32 end = (v9x_u32)&v9x_cif_textures[V9X_CIF_TEXTURES];

    if (handle < base || handle >= end ||
        (handle - base) % sizeof(struct v9x_cif_texture) != 0ul) {
        return 0;
    }
    return (struct v9x_cif_texture *)handle;
}

v9x_u32 __stdcall ATI3DCIF_TextureUnreg(v9x_u32 handle);
v9x_u32 __stdcall ATI3DCIF_TextureUnreg(v9x_u32 handle)
{
    struct v9x_cif_texture *slot;
    v9x_u32 zero = 0ul;
    char text[160];

    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_TextureUnreg, &handle, 1u, &zero, 1u, 0);
    slot = v9x_cif_texture_from(handle);
    if (slot == 0 || !slot->live) {
        wsprintfA(text, "TextureUnreg htx=%08lX unknown BADPARAM", handle);
        v9x_cif_log(text);
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_BADPARAM;
    }
    if (slot->rewrites != 0ul) {
        wsprintfA(text, "TextureUnreg htx=%08lX selections=%lu rewrites=%lu",
                  handle, slot->selections, slot->rewrites);
        v9x_cif_log(text);
    }
    slot->live = 0ul;
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/* A selected texture's top level checksummed again on its first
 * selections and then every Nth: a change means the title rewrote a
 * registered texture in place. */
static void v9x_cif_texture_selected(v9x_u32 handle)
{
    struct v9x_cif_texture *slot = v9x_cif_texture_from(handle);
    v9x_u32 sum;
    char text[160];

    if (slot == 0 || !slot->live) {
        if (handle != 0ul) {
            wsprintfA(text, "TMAP_SELECT htx=%08lX not registered", handle);
            v9x_cif_log(text);
        }
        return;
    }
    ++slot->selections;
    if (slot->selections > V9X_CIF_RECHECK_FIRST &&
        slot->selections % V9X_CIF_RECHECK_EVERY != 0ul) {
        return;
    }
    sum = v9x_cif_checksum(slot->level0, slot->bytes);
    if (sum != slot->checksum) {
        ++slot->rewrites;
        wsprintfA(text, "texture htx=%08lX rewritten after registration "
                  "selection=%lu sum=%08lX was=%08lX", handle,
                  slot->selections, sum, slot->checksum);
        v9x_cif_log(text);
        slot->checksum = sum;
    }
}

/*
 * TexturePaletteCreate(epalette, pPalette, phtpalCreated): logs the kind
 * and the first entries; the handle is the slot's address.
 */
v9x_u32 __stdcall ATI3DCIF_TexturePaletteCreate(v9x_u32 kind,
                                                       v9x_u32 entries,
                                                       v9x_u32 handle);
v9x_u32 __stdcall ATI3DCIF_TexturePaletteCreate(v9x_u32 kind,
                                                       v9x_u32 entries,
                                                       v9x_u32 handle)
{
    v9x_u32 args[3];
    v9x_u32 index;
    char text[200];

    args[0] = kind;
    args[1] = entries;
    args[2] = handle;
    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_TexturePaletteCreate, args, 3u, &kind, 1u, 0);
    if (handle == 0ul || IsBadWritePtr((void *)handle, 4u)) {
        v9x_cif_log("TexturePaletteCreate no handle BADPARAM");
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_BADPARAM;
    }
    for (index = 0ul; index < V9X_CIF_PALETTES; ++index) {
        if (!v9x_cif_palettes[index]) {
            break;
        }
    }
    if (index == V9X_CIF_PALETTES) {
        v9x_cif_log("TexturePaletteCreate pool full MEMALLOCFAIL");
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_MEMALLOCFAIL;
    }
    v9x_cif_palettes[index] = 1ul;
    *(v9x_u32 *)handle = (v9x_u32)&v9x_cif_palettes[index];
    wsprintfA(text, "TexturePaletteCreate htxpal=%08lX kind=%lu entries=%lu "
              "first=%08lX %08lX %08lX %08lX",
              (v9x_u32)&v9x_cif_palettes[index], kind,
              (kind == V9X_C3D_ECI_4BIT_HI || kind == V9X_C3D_ECI_4BIT_LOW)
                  ? 16ul : 256ul,
              v9x_cif_peek(entries), v9x_cif_peek(entries + 4ul),
              v9x_cif_peek(entries + 8ul), v9x_cif_peek(entries + 12ul));
    v9x_cif_log(text);
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

v9x_u32 __stdcall ATI3DCIF_TexturePaletteDestroy(v9x_u32 handle);
v9x_u32 __stdcall ATI3DCIF_TexturePaletteDestroy(v9x_u32 handle)
{
    v9x_u32 base = (v9x_u32)&v9x_cif_palettes[0];
    v9x_u32 zero = 0ul;

    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_TexturePaletteDestroy, &handle, 1u, &zero, 1u, 0);
    if (handle >= base && handle < base + sizeof(v9x_cif_palettes) &&
        (handle - base) % 4ul == 0ul) {
        *(v9x_u32 *)handle = 0ul;
    }
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

v9x_u32 __stdcall ATI3DCIF_TexturePaletteAnimate(v9x_u32 handle,
                                                        v9x_u32 start,
                                                        v9x_u32 count,
                                                        v9x_u32 entries);
v9x_u32 __stdcall ATI3DCIF_TexturePaletteAnimate(v9x_u32 handle,
                                                        v9x_u32 start,
                                                        v9x_u32 count,
                                                        v9x_u32 entries)
{
    v9x_u32 args[4];
    v9x_u32 key[2];

    args[0] = handle;
    args[1] = start;
    args[2] = count;
    args[3] = entries;
    key[0] = start;
    key[1] = count;
    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_TexturePaletteAnimate, args, 4u, key, 2u, 0);
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/* ---- contexts -------------------------------------------------------- */

static struct v9x_cif_context *v9x_cif_context_from(v9x_u32 handle)
{
    v9x_u32 base = (v9x_u32)&v9x_cif_contexts[0];
    v9x_u32 end = (v9x_u32)&v9x_cif_contexts[V9X_CIF_CONTEXTS];
    struct v9x_cif_context *context;

    if (handle < base || handle >= end ||
        (handle - base) % sizeof(struct v9x_cif_context) != 0ul) {
        return 0;
    }
    context = (struct v9x_cif_context *)handle;
    return context->live ? context : 0;
}

v9x_u32 __stdcall ATI3DCIF_ContextCreate(void);
v9x_u32 __stdcall ATI3DCIF_ContextCreate(void)
{
    struct v9x_cif_context *context = 0;
    unsigned int index;
    char text[120];

    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_ContextCreate, 0, 0u, 0, 0u, 0);
    for (index = 0u; index < V9X_CIF_CONTEXTS; ++index) {
        if (!v9x_cif_contexts[index].live) {
            context = &v9x_cif_contexts[index];
            break;
        }
    }
    if (context == 0) {
        v9x_cif_log("ContextCreate pool full, returning NULL");
        LeaveCriticalSection(&v9x_cif_lock);
        return 0ul;
    }
    /* By hand: ZeroMemory is memset, and the DLL links no runtime. */
    for (index = 0u; index < V9X_C3D_ERS_NUM; ++index) {
        context->state[index] = v9x_cif_state_default[index];
    }
    for (index = 0u; index < 4u; ++index) {
        context->rect[0][index] = 0ul;
        context->rect[1][index] = 0ul;
    }
    context->set_once[0] = 0ul;
    context->set_once[1] = 0ul;
    context->live = 1ul;
    wsprintfA(text, "ContextCreate hrc=%08lX", (v9x_u32)context);
    v9x_cif_log(text);
    LeaveCriticalSection(&v9x_cif_lock);
    return (v9x_u32)context;
}

v9x_u32 __stdcall ATI3DCIF_ContextDestroy(v9x_u32 handle);
v9x_u32 __stdcall ATI3DCIF_ContextDestroy(v9x_u32 handle)
{
    struct v9x_cif_context *context;
    v9x_u32 zero = 0ul;

    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_ContextDestroy, &handle, 1u, &zero, 1u, 0);
    context = v9x_cif_context_from(handle);
    if (context == 0) {
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_BADPARAM;
    }
    if (context == v9x_cif_current) {
        v9x_cif_current = 0;
    }
    context->live = 0ul;
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/*
 * ContextSetState(hRC, eRStateID, pRStateData): pRStateData points at the
 * value, four C3D_INT32 for the two rectangles. A state is logged the
 * first time it is set in a context and whenever its value changes, which
 * keeps per-draw state traffic out of the log while recording every value
 * a title uses.
 */
v9x_u32 __stdcall ATI3DCIF_ContextSetState(v9x_u32 handle, v9x_u32 id,
                                                  v9x_u32 data);
v9x_u32 __stdcall ATI3DCIF_ContextSetState(v9x_u32 handle, v9x_u32 id,
                                                  v9x_u32 data)
{
    struct v9x_cif_context *context;
    v9x_u32 args[3];
    v9x_u32 key[2];
    v9x_u32 value[4];
    v9x_u32 *stored;
    unsigned int words = 1u;
    unsigned int index;
    int changed = 0;
    char text[200];

    args[0] = handle;
    args[1] = id;
    args[2] = data;
    EnterCriticalSection(&v9x_cif_lock);
    context = v9x_cif_context_from(handle);
    if (id < V9X_C3D_ERS_NUM && data != 0ul &&
        !IsBadReadPtr((const void *)data, 4u)) {
        value[0] = *(const v9x_u32 *)data;
    } else {
        value[0] = 0ul;
    }
    key[0] = id;
    key[1] = value[0];
    v9x_cif_call(V9X_CIF_IX_ContextSetState, args, 3u, key, 2u, 0);
    if (context == 0 || id >= V9X_C3D_ERS_NUM || data == 0ul) {
        wsprintfA(text, "ContextSetState hrc=%08lX id=%lu data=%08lX BADPARAM",
                  handle, id, data);
        v9x_cif_log(text);
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_BADPARAM;
    }
    stored = &context->state[id];
    if (id == V9X_C3D_ERS_SURF_VPORT || id == V9X_C3D_ERS_SURF_SCISSOR) {
        words = 4u;
        stored = context->rect[id == V9X_C3D_ERS_SURF_VPORT ? 0 : 1];
        if (IsBadReadPtr((const void *)data, 16u)) {
            v9x_cif_log("ContextSetState rectangle unreadable BADPARAM");
            LeaveCriticalSection(&v9x_cif_lock);
            return V9X_C3D_EC_BADPARAM;
        }
        for (index = 0u; index < 4u; ++index) {
            value[index] = ((const v9x_u32 *)data)[index];
        }
    }
    if ((context->set_once[id / 32u] & (1ul << (id % 32u))) == 0ul) {
        changed = 1;
        context->set_once[id / 32u] |= 1ul << (id % 32u);
    }
    for (index = 0u; index < words; ++index) {
        if (stored[index] != value[index]) {
            changed = 1;
            stored[index] = value[index];
        }
    }
    if (changed) {
        if (words == 4u) {
            wsprintfA(text, "state hrc=%08lX %s top=%ld left=%ld bottom=%ld "
                      "right=%ld", handle, v9x_cif_state_names[id],
                      (long)value[0], (long)value[1], (long)value[2],
                      (long)value[3]);
        } else {
            wsprintfA(text, "state hrc=%08lX %s=%08lX (%lu)", handle,
                      v9x_cif_state_names[id], value[0], value[0]);
        }
        v9x_cif_log(text);
    }
    if (id == V9X_C3D_ERS_TMAP_SELECT) {
        v9x_cif_texture_selected(value[0]);
    }
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/* ---- rendering ------------------------------------------------------- */

/* The surface state a draw goes to, logged at the start of a captured
 * frame and whenever the render context changes. */
static void v9x_cif_log_target(const struct v9x_cif_context *context)
{
    char text[300];

    wsprintfA(text, "target hrc=%08lX draw=%08lX pitch=%lu pf=%lu "
              "vport=%ld,%ld,%ld,%ld z=%08lX zpitch=%lu zmode=%lu",
              (v9x_u32)context, context->state[V9X_C3D_ERS_SURF_DRAW_PTR],
              context->state[V9X_C3D_ERS_SURF_DRAW_PITCH],
              context->state[V9X_C3D_ERS_SURF_DRAW_PF],
              (long)context->rect[0][0], (long)context->rect[0][1],
              (long)context->rect[0][2], (long)context->rect[0][3],
              context->state[V9X_C3D_ERS_SURF_Z_PTR],
              context->state[V9X_C3D_ERS_SURF_Z_PITCH],
              context->state[20]);
    v9x_cif_log(text);
}

v9x_u32 __stdcall ATI3DCIF_RenderBegin(v9x_u32 handle);
v9x_u32 __stdcall ATI3DCIF_RenderBegin(v9x_u32 handle)
{
    struct v9x_cif_context *context;
    v9x_u32 zero = 0ul;
    int logged;

    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_RenderBegin, &handle, 1u, &zero, 1u, &logged);
    context = v9x_cif_context_from(handle);
    if (context == 0) {
        v9x_cif_log("RenderBegin unknown context BADPARAM");
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_BADPARAM;
    }
    v9x_cif_current = context;
    v9x_cif_frame_draws = 0ul;
    if (logged) {
        v9x_cif_log_target(context);
    }
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/* RenderEnd closes a frame for the capture rules: the first frames and
 * every Nth are logged whole. */
v9x_u32 __stdcall ATI3DCIF_RenderEnd(void);
v9x_u32 __stdcall ATI3DCIF_RenderEnd(void)
{
    char text[80];

    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_RenderEnd, 0, 0u, 0, 0u, 0);
    v9x_cif_current = 0;
    ++v9x_cif_frames;
    v9x_cif_capturing = (v9x_cif_frames < V9X_CIF_CAPTURE_FIRST ||
                         v9x_cif_frames % V9X_CIF_CAPTURE_EVERY == 0ul)
                            ? 1ul : 0ul;
    if (v9x_cif_capturing) {
        v9x_cif_capture_lines = 0ul;
        wsprintfA(text, "---- frame %lu ----", v9x_cif_frames);
        v9x_cif_log(text);
    }
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

v9x_u32 __stdcall ATI3DCIF_RenderSwitch(v9x_u32 handle);
v9x_u32 __stdcall ATI3DCIF_RenderSwitch(v9x_u32 handle)
{
    struct v9x_cif_context *context;
    v9x_u32 zero = 0ul;
    int logged;

    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_RenderSwitch, &handle, 1u, &zero, 1u, &logged);
    context = v9x_cif_context_from(handle);
    if (context == 0) {
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_BADPARAM;
    }
    v9x_cif_current = context;
    if (logged) {
        v9x_cif_log_target(context);
    }
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/* Bytes in one vertex of the current context's type, 0 for an unknown
 * type. */
static unsigned int v9x_cif_vertex_bytes(void)
{
    v9x_u32 type;

    if (v9x_cif_current == 0) {
        return 0u;
    }
    type = v9x_cif_current->state[V9X_C3D_ERS_VERTEX_TYPE];
    if (type == V9X_C3D_EV_VTCF) {
        return V9X_C3D_VTCF_BYTES;
    }
    if (type == V9X_C3D_EV_TLVERTEX) {
        return V9X_C3D_TLVERTEX_BYTES;
    }
    return 0u;
}

/* One vertex as raw 32-bit words, decoded offline: the census keeps
 * floating point out of a runtime-free DLL. */
static void v9x_cif_dump_vertex(const char *who, v9x_u32 index,
                                v9x_u32 address)
{
    char text[400];
    unsigned int bytes = v9x_cif_vertex_bytes();
    unsigned int offset;
    int length;

    if (bytes == 0u || address == 0ul ||
        IsBadReadPtr((const void *)address, bytes)) {
        wsprintfA(text, "%s vertex %lu at %08lX unreadable", who, index,
                  address);
        v9x_cif_log(text);
        return;
    }
    length = wsprintfA(text, "%s vertex %lu", who, index);
    for (offset = 0u; offset < bytes; offset += 4u) {
        length += wsprintfA(text + length, " %08lX",
                            *(const v9x_u32 *)(address + offset));
    }
    v9x_cif_log(text);
}

/* The key every draw shares: what is drawn, not where the vertices are. */
static void v9x_cif_draw_key(v9x_u32 *key, v9x_u32 count)
{
    key[0] = v9x_cif_current != 0
                 ? v9x_cif_current->state[V9X_C3D_ERS_PRIM_TYPE] : 0xfffful;
    key[1] = v9x_cif_current != 0
                 ? v9x_cif_current->state[V9X_C3D_ERS_VERTEX_TYPE] : 0xfffful;
    key[2] = count;
}

/* Whether this draw's vertices are dumped: the first draws of a captured
 * frame, or a draw the rules logged. */
static int v9x_cif_dump_this(int logged)
{
    ++v9x_cif_frame_draws;
    return logged && v9x_cif_frame_draws <= V9X_CIF_DUMP_DRAWS;
}

static void v9x_cif_draw_outside(const char *who)
{
    char text[120];

    if (v9x_cif_current == 0) {
        wsprintfA(text, "%s outside RenderBegin", who);
        v9x_cif_log(text);
    }
}

v9x_u32 __stdcall ATI3DCIF_RenderPrimStrip(v9x_u32 strip,
                                                  v9x_u32 count);
v9x_u32 __stdcall ATI3DCIF_RenderPrimStrip(v9x_u32 strip,
                                                  v9x_u32 count)
{
    v9x_u32 args[2];
    v9x_u32 key[3];
    v9x_u32 index;
    int logged;

    args[0] = strip;
    args[1] = count;
    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_draw_key(key, count);
    v9x_cif_call(V9X_CIF_IX_RenderPrimStrip, args, 2u, key, 3u, &logged);
    v9x_cif_vertices += count;
    if (logged) {
        v9x_cif_draw_outside("RenderPrimStrip");
    }
    if (v9x_cif_dump_this(logged)) {
        for (index = 0ul; index < count && index < V9X_CIF_DUMP_VERTICES;
             ++index) {
            v9x_cif_dump_vertex("strip", index,
                                strip + index * v9x_cif_vertex_bytes());
        }
    }
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

v9x_u32 __stdcall ATI3DCIF_RenderPrimList(v9x_u32 list, v9x_u32 count);
v9x_u32 __stdcall ATI3DCIF_RenderPrimList(v9x_u32 list, v9x_u32 count)
{
    v9x_u32 args[2];
    v9x_u32 key[3];
    v9x_u32 index;
    int logged;

    args[0] = list;
    args[1] = count;
    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_draw_key(key, count);
    v9x_cif_call(V9X_CIF_IX_RenderPrimList, args, 2u, key, 3u, &logged);
    v9x_cif_vertices += count;
    if (logged) {
        v9x_cif_draw_outside("RenderPrimList");
    }
    if (v9x_cif_dump_this(logged)) {
        for (index = 0ul; index < count && index < V9X_CIF_DUMP_VERTICES;
             ++index) {
            v9x_cif_dump_vertex("list", index,
                                v9x_cif_peek(list + index * 4ul));
        }
    }
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/* RenderPrimMesh(vertex pointer array, indices, index count). */
v9x_u32 __stdcall ATI3DCIF_RenderPrimMesh(v9x_u32 vertices,
                                                 v9x_u32 indices,
                                                 v9x_u32 count);
v9x_u32 __stdcall ATI3DCIF_RenderPrimMesh(v9x_u32 vertices,
                                                 v9x_u32 indices,
                                                 v9x_u32 count)
{
    v9x_u32 args[3];
    v9x_u32 key[3];
    v9x_u32 index;
    v9x_u32 which;
    char text[120];
    int logged;

    args[0] = vertices;
    args[1] = indices;
    args[2] = count;
    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_draw_key(key, count);
    v9x_cif_call(V9X_CIF_IX_RenderPrimMesh, args, 3u, key, 3u, &logged);
    v9x_cif_vertices += count;
    if (logged) {
        v9x_cif_draw_outside("RenderPrimMesh");
    }
    if (v9x_cif_dump_this(logged)) {
        for (index = 0ul; index < count && index < V9X_CIF_DUMP_VERTICES;
             ++index) {
            which = v9x_cif_peek(indices + index * 4ul);
            wsprintfA(text, "mesh index %lu = %lu", index, which);
            v9x_cif_log(text);
            v9x_cif_dump_vertex("mesh", index,
                                v9x_cif_peek(vertices + which * 4ul));
        }
    }
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/* Not in the SDK 4.1 header: twelve bytes of arguments, logged raw. */
v9x_u32 __stdcall ATI3DCIF_RenderIndexedPrim(v9x_u32 a0, v9x_u32 a1,
                                                    v9x_u32 a2);
v9x_u32 __stdcall ATI3DCIF_RenderIndexedPrim(v9x_u32 a0, v9x_u32 a1,
                                                    v9x_u32 a2)
{
    v9x_u32 args[3];

    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_RenderIndexedPrim, args, 3u, &a2, 1u, 0);
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/* ---- the _lib pointers ----------------------------------------------- */

/*
 * ATI3DCIF.H declares "extern function_name##_t function_name##_lib":
 * each is a variable holding the function's address, exported by its
 * undecorated name. The pragmas give each variable exactly that symbol, so
 * scripts\build-cif.ps1 can export it as it stands. These 19 variables
 * and the 19 functions above are the DLL's exports, ATI's own list, and
 * the only external symbols here besides the entry point.
 */
typedef v9x_u32 (__stdcall *v9x_cif_fn0)(void);
typedef v9x_u32 (__stdcall *v9x_cif_fn1)(v9x_u32);
typedef v9x_u32 (__stdcall *v9x_cif_fn2)(v9x_u32, v9x_u32);
typedef v9x_u32 (__stdcall *v9x_cif_fn3)(v9x_u32, v9x_u32, v9x_u32);
typedef v9x_u32 (__stdcall *v9x_cif_fn4)(v9x_u32, v9x_u32, v9x_u32, v9x_u32);

#pragma aux ATI3DCIF_Init_lib                  "ATI3DCIF_Init_lib"
#pragma aux ATI3DCIF_Term_lib                  "ATI3DCIF_Term_lib"
#pragma aux ATI3DCIF_GetInfo_lib               "ATI3DCIF_GetInfo_lib"
#pragma aux ATI3DCIF_TextureReg_lib            "ATI3DCIF_TextureReg_lib"
#pragma aux ATI3DCIF_TextureUnreg_lib          "ATI3DCIF_TextureUnreg_lib"
#pragma aux ATI3DCIF_TexturePaletteCreate_lib  "ATI3DCIF_TexturePaletteCreate_lib"
#pragma aux ATI3DCIF_TexturePaletteDestroy_lib "ATI3DCIF_TexturePaletteDestroy_lib"
#pragma aux ATI3DCIF_TexturePaletteAnimate_lib "ATI3DCIF_TexturePaletteAnimate_lib"
#pragma aux ATI3DCIF_LoadObject_lib            "ATI3DCIF_LoadObject_lib"
#pragma aux ATI3DCIF_ContextCreate_lib         "ATI3DCIF_ContextCreate_lib"
#pragma aux ATI3DCIF_ContextDestroy_lib        "ATI3DCIF_ContextDestroy_lib"
#pragma aux ATI3DCIF_ContextSetState_lib       "ATI3DCIF_ContextSetState_lib"
#pragma aux ATI3DCIF_RenderBegin_lib           "ATI3DCIF_RenderBegin_lib"
#pragma aux ATI3DCIF_RenderEnd_lib             "ATI3DCIF_RenderEnd_lib"
#pragma aux ATI3DCIF_RenderSwitch_lib          "ATI3DCIF_RenderSwitch_lib"
#pragma aux ATI3DCIF_RenderPrimStrip_lib       "ATI3DCIF_RenderPrimStrip_lib"
#pragma aux ATI3DCIF_RenderPrimList_lib        "ATI3DCIF_RenderPrimList_lib"
#pragma aux ATI3DCIF_RenderPrimMesh_lib        "ATI3DCIF_RenderPrimMesh_lib"
#pragma aux ATI3DCIF_RenderIndexedPrim_lib     "ATI3DCIF_RenderIndexedPrim_lib"

v9x_cif_fn0 ATI3DCIF_Init_lib = ATI3DCIF_Init;
v9x_cif_fn0 ATI3DCIF_Term_lib = ATI3DCIF_Term;
v9x_cif_fn1 ATI3DCIF_GetInfo_lib = ATI3DCIF_GetInfo;
v9x_cif_fn2 ATI3DCIF_TextureReg_lib = ATI3DCIF_TextureReg;
v9x_cif_fn1 ATI3DCIF_TextureUnreg_lib = ATI3DCIF_TextureUnreg;
v9x_cif_fn3 ATI3DCIF_TexturePaletteCreate_lib = ATI3DCIF_TexturePaletteCreate;
v9x_cif_fn1 ATI3DCIF_TexturePaletteDestroy_lib = ATI3DCIF_TexturePaletteDestroy;
v9x_cif_fn4 ATI3DCIF_TexturePaletteAnimate_lib = ATI3DCIF_TexturePaletteAnimate;
v9x_cif_fn3 ATI3DCIF_LoadObject_lib = ATI3DCIF_LoadObject;
v9x_cif_fn0 ATI3DCIF_ContextCreate_lib = ATI3DCIF_ContextCreate;
v9x_cif_fn1 ATI3DCIF_ContextDestroy_lib = ATI3DCIF_ContextDestroy;
v9x_cif_fn3 ATI3DCIF_ContextSetState_lib = ATI3DCIF_ContextSetState;
v9x_cif_fn1 ATI3DCIF_RenderBegin_lib = ATI3DCIF_RenderBegin;
v9x_cif_fn0 ATI3DCIF_RenderEnd_lib = ATI3DCIF_RenderEnd;
v9x_cif_fn1 ATI3DCIF_RenderSwitch_lib = ATI3DCIF_RenderSwitch;
v9x_cif_fn2 ATI3DCIF_RenderPrimStrip_lib = ATI3DCIF_RenderPrimStrip;
v9x_cif_fn2 ATI3DCIF_RenderPrimList_lib = ATI3DCIF_RenderPrimList;
v9x_cif_fn3 ATI3DCIF_RenderPrimMesh_lib = ATI3DCIF_RenderPrimMesh;
v9x_cif_fn3 ATI3DCIF_RenderIndexedPrim_lib = ATI3DCIF_RenderIndexedPrim;

/* ---- the DLL --------------------------------------------------------- */

BOOL __stdcall V9xCifEntry(HINSTANCE instance, DWORD reason, LPVOID reserved);
BOOL __stdcall V9xCifEntry(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    char path[MAX_PATH];
    char text[MAX_PATH + 96];

    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        InitializeCriticalSection(&v9x_cif_lock);
        v9x_cif_summary_tick = GetTickCount();
        path[0] = '\0';
        GetModuleFileNameA(0, path, sizeof(path));
        wsprintfA(text, "attach census build=%s process=%s", V9X_BUILD_ID,
                  path);
        v9x_cif_log(text);
        path[0] = '\0';
        GetModuleFileNameA(instance, path, sizeof(path));
        wsprintfA(text, "module %s", path);
        v9x_cif_log(text);
    } else if (reason == DLL_PROCESS_DETACH) {
        v9x_cif_summary("detach");
        DeleteCriticalSection(&v9x_cif_lock);
    }
    return TRUE;
}
