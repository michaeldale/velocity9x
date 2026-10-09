/*
 * GLIDE3X.DLL, census build (docs\plans\glide-3x-wrapper.md, Phase 0).
 *
 * Every Glide 3.x export the 3dfx DLL has, each logging its arguments to
 * C:\V9XDIAG\V9XGLD3.LOG and drawing nothing. A game run against it says
 * which calls, formats, layouts and modes it really uses, so the real
 * implementation is aimed at that list rather than at the whole API - the
 * method the Glide 2 work used for Need for Speed II SE.
 *
 * The functions here are the ones a game cannot get past with a zero:
 * queries (grGet, grGetString, texture memory), the window open, the frame
 * buffer lock, and the swap that paces the game. The rest are generated
 * stubs (scripts\lib\glide3-exports.ps1) that log and return zero.
 *
 * The board described is one Voodoo3-class unit with one TMU: the values
 * are named below and written into the log, so the census also records
 * which of them the game reads. Constants are from 3dfx's Glide 3 glide.h,
 * read for facts under the licence rule in docs\plans\glide-2x-wrapper.md.
 *
 * Runtime-free like the other Velocity9x DLLs: KERNEL32 and USER32 only.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"

#define V9X_GLIDE3_DEFINE_STUBS
#define V9X_GLIDE3_STUB_HOOK(ix, args, count) v9x_g3_call((ix), (args), (count))
static void v9x_g3_call(unsigned int ix, const v9x_u32 *args, unsigned int count);
#include "glide3_exports_gen.h"

/* ---- glide.h facts ------------------------------------------------- */

/* grGet pnames (glide.h, "grGet" section). */
#define V9X_GR_BITS_DEPTH                 0x01ul
#define V9X_GR_BITS_RGBA                  0x02ul
#define V9X_GR_FIFO_FULLNESS              0x03ul
#define V9X_GR_FOG_TABLE_ENTRIES          0x04ul
#define V9X_GR_GAMMA_TABLE_ENTRIES        0x05ul
#define V9X_GR_GLIDE_STATE_SIZE           0x06ul
#define V9X_GR_GLIDE_VERTEXLAYOUT_SIZE    0x07ul
#define V9X_GR_IS_BUSY                    0x08ul
#define V9X_GR_LFB_PIXEL_PIPE             0x09ul
#define V9X_GR_MAX_TEXTURE_SIZE           0x0aul
#define V9X_GR_MAX_TEXTURE_ASPECT_RATIO   0x0bul
#define V9X_GR_MEMORY_FB                  0x0cul
#define V9X_GR_MEMORY_TMU                 0x0dul
#define V9X_GR_MEMORY_UMA                 0x0eul
#define V9X_GR_NUM_BOARDS                 0x0ful
#define V9X_GR_NUM_FB                     0x11ul
#define V9X_GR_NUM_SWAP_HISTORY_BUFFER    0x12ul
#define V9X_GR_NUM_TMU                    0x13ul
#define V9X_GR_PENDING_BUFFERSWAPS        0x14ul
#define V9X_GR_REVISION_FB                0x15ul
#define V9X_GR_REVISION_TMU               0x16ul
#define V9X_GR_SUPPORTS_PASSTHRU          0x23ul
#define V9X_GR_TEXTURE_ALIGN              0x24ul
#define V9X_GR_VIDEO_POSITION             0x25ul
#define V9X_GR_VIEWPORT                   0x26ul
#define V9X_GR_WDEPTH_MIN_MAX             0x27ul
#define V9X_GR_ZDEPTH_MIN_MAX             0x28ul
#define V9X_GR_BITS_GAMMA                 0x2aul

/* grGetString names. */
#define V9X_GR_EXTENSION                  0xa0ul
#define V9X_GR_HARDWARE                   0xa1ul
#define V9X_GR_RENDERER                   0xa2ul
#define V9X_GR_VENDOR                     0xa3ul
#define V9X_GR_VERSION                    0xa4ul

/* LFB write modes 888 and 8888 are 32 bits a pixel; ANY means "yours". */
#define V9X_GR_LFBWRITEMODE_565           0x00ul
#define V9X_GR_LFBWRITEMODE_888           0x04ul
#define V9X_GR_LFBWRITEMODE_8888          0x05ul
#define V9X_GR_LFBWRITEMODE_ANY           0xfful

/* Texture formats below 8 are one byte a texel, 8 and up two. */
#define V9X_GR_TEXFMT_16BIT               0x08ul
/* grTexTextureMemRequired / grTexDownloadMipMap evenOdd: both. */
#define V9X_GR_MIPMAPLEVELMASK_BOTH       0x03ul

/* ---- the board the census describes ------------------------------- */

/* One Voodoo3-class board, one TMU, 4 MiB of frame buffer and 4 MiB of
 * texture memory: 8 MiB in all, the Rage XL's own size on A8U4I5. */
#define V9X_G3_MEMORY_FB      (4ul * 1024ul * 1024ul)
#define V9X_G3_MEMORY_TMU     (4ul * 1024ul * 1024ul)
#define V9X_G3_MAX_TEXTURE    256ul
#define V9X_G3_MAX_ASPECT     3ul
#define V9X_G3_TEXTURE_ALIGN  8ul
#define V9X_G3_FOG_ENTRIES    64ul
#define V9X_G3_GAMMA_ENTRIES  256ul
#define V9X_G3_REVISION_FB    1ul
#define V9X_G3_REVISION_TMU   1ul

static const char v9x_g3_version[] = "3.01 Velocity9x census " V9X_BUILD_ID;
static const char v9x_g3_hardware[] = "Voodoo3";
static const char v9x_g3_renderer[] = "Glide";
static const char v9x_g3_vendor[] = "3Dfx Interactive";
static const char v9x_g3_extension[] = "";

/* ---- log volume ---------------------------------------------------- */

/*
 * The Glide 2 census of NFS II SE made 80 million calls in 40 seconds, so
 * the same three rules apply: whole frames (the first few and every Nth),
 * each export's first calls, and each distinct argument set once. Call
 * counts go out every V9X_G3_SUMMARY_MS and at shutdown.
 */
#define V9X_G3_CAPTURE_FIRST  3ul
#define V9X_G3_CAPTURE_EVERY  1800ul
#define V9X_G3_CAPTURE_LINES  3000ul
#define V9X_G3_SAMPLE_FIRST   24ul
#define V9X_G3_SEEN_SLOTS     8192u
#define V9X_G3_SUMMARY_MS     15000ul
/* A swap waits its interval in 60 Hz frames, at least one, so a game with
 * nothing drawn keeps its own timing instead of spinning. */
#define V9X_G3_FRAME_MS       16ul
/* Bytes of vertex data logged from the first draws. */
#define V9X_G3_VERTEX_DUMP    64u

/* The stand-in frame buffer a lock returns: 1024 rows of the widest
 * stride, the Voodoo's fixed 2048 bytes for 16-bit pixels and twice that
 * for 32-bit ones. */
#define V9X_G3_LFB_ROWS       1024ul
#define V9X_G3_LFB_STRIDE_16  2048ul
#define V9X_G3_LFB_STRIDE_32  4096ul

static const char *const v9x_g3_names[V9X_GLIDE3_EXPORT_COUNT] =
    V9X_GLIDE3_EXPORT_NAMES;

static v9x_u32 v9x_g3_sequence;
static v9x_u32 v9x_g3_calls[V9X_GLIDE3_EXPORT_COUNT];
static v9x_u32 v9x_g3_seen[V9X_G3_SEEN_SLOTS];
static v9x_u32 v9x_g3_seen_full;
static v9x_u32 v9x_g3_capturing = 1ul;
static v9x_u32 v9x_g3_capture_lines;
static v9x_u32 v9x_g3_swaps;
static v9x_u32 v9x_g3_summary_tick;
static v9x_u8 *v9x_g3_lfb;
static CRITICAL_SECTION v9x_g3_lock;

/* ---- the log ------------------------------------------------------- */

static void v9x_g3_log(const char *text)
{
    HANDLE file;
    char line[480];
    DWORD written;
    int length;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    file = CreateFileA(V9X_DIAG_GLIDE3_LOG, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    SetFilePointer(file, 0, 0, FILE_END);
    length = wsprintfA(line, "%lu t=%lu %s\r\n", ++v9x_g3_sequence,
                       GetTickCount(), text);
    WriteFile(file, line, (DWORD)length, &written, 0);
    CloseHandle(file);
}

/* Whether this argument set of this export has been logged before. A
 * fixed open-addressed set of hashes; once full, nothing new is added. */
static int v9x_g3_seen_before(unsigned int ix, const v9x_u32 *args,
                              unsigned int count)
{
    v9x_u32 hash = 2166136261ul ^ (v9x_u32)ix;
    unsigned int index;
    unsigned int probe;

    for (index = 0u; index < count; ++index) {
        hash = (hash ^ args[index]) * 16777619ul;
    }
    if (hash == 0ul) {
        hash = 1ul;
    }
    for (probe = 0u; probe < 16u; ++probe) {
        v9x_u32 *slot = &v9x_g3_seen[(hash + probe) % V9X_G3_SEEN_SLOTS];

        if (*slot == hash) {
            return 1;
        }
        if (*slot == 0ul) {
            *slot = hash;
            return 0;
        }
    }
    if (!v9x_g3_seen_full) {
        v9x_g3_seen_full = 1ul;
        v9x_g3_log("distinct-argument set full; only samples and frames log now");
    }
    return 1;
}

static void v9x_g3_summary(const char *why)
{
    char text[160];
    unsigned int ix;

    wsprintfA(text, "summary %s swaps=%lu", why, v9x_g3_swaps);
    v9x_g3_log(text);
    for (ix = 0u; ix < V9X_GLIDE3_EXPORT_COUNT; ++ix) {
        if (v9x_g3_calls[ix] != 0ul) {
            wsprintfA(text, "  %s %lu", v9x_g3_names[ix], v9x_g3_calls[ix]);
            v9x_g3_log(text);
        }
    }
}

/*
 * Every export comes through here first: count the call, and log it if it
 * is in a captured frame, among the export's first calls, or an argument
 * set not seen before.
 */
static void v9x_g3_call(unsigned int ix, const v9x_u32 *args,
                        unsigned int count)
{
    char text[400];
    v9x_u32 call;
    int length;
    unsigned int index;
    int log_it;
    DWORD now;

    EnterCriticalSection(&v9x_g3_lock);
    call = ++v9x_g3_calls[ix];
    log_it = 0;
    if (v9x_g3_capturing && v9x_g3_capture_lines < V9X_G3_CAPTURE_LINES) {
        ++v9x_g3_capture_lines;
        log_it = 1;
    } else if (call <= V9X_G3_SAMPLE_FIRST) {
        log_it = 1;
    } else if (!v9x_g3_seen_before(ix, args, count)) {
        log_it = 1;
    }
    if (log_it) {
        length = wsprintfA(text, "%s #%lu", v9x_g3_names[ix], call);
        for (index = 0u; index < count && length < 360; ++index) {
            length += wsprintfA(text + length, " %08lX", args[index]);
        }
        v9x_g3_log(text);
    }
    now = GetTickCount();
    if (now - v9x_g3_summary_tick >= V9X_G3_SUMMARY_MS) {
        v9x_g3_summary_tick = now;
        v9x_g3_summary("periodic");
    }
    LeaveCriticalSection(&v9x_g3_lock);
}

static void v9x_g3_note(const char *text)
{
    EnterCriticalSection(&v9x_g3_lock);
    v9x_g3_log(text);
    LeaveCriticalSection(&v9x_g3_lock);
}

/* ---- queries -------------------------------------------------------- */

/* grGet: the answer is written to params (FxI32 each) and its size in
 * bytes returned; an unknown pname or too small a buffer returns 0. */
v9x_u32 __stdcall grGet(v9x_u32 pname, v9x_u32 plength, v9x_u32 params);
v9x_u32 __stdcall grGet(v9x_u32 pname, v9x_u32 plength, v9x_u32 params)
{
    v9x_u32 args[3];
    v9x_u32 values[4];
    v9x_u32 count = 1ul;
    v9x_u32 *out = (v9x_u32 *)params;
    v9x_u32 index;
    char text[160];

    args[0] = pname;
    args[1] = plength;
    args[2] = params;
    v9x_g3_call(V9X_GLIDE3_IX_grGet, args, 3u);

    values[0] = 0ul;
    switch (pname) {
    case V9X_GR_BITS_DEPTH:              values[0] = 16ul; break;
    case V9X_GR_BITS_RGBA:
        values[0] = 5ul; values[1] = 6ul; values[2] = 5ul; values[3] = 0ul;
        count = 4ul;
        break;
    case V9X_GR_FIFO_FULLNESS:           values[0] = 0ul; break;
    case V9X_GR_FOG_TABLE_ENTRIES:       values[0] = V9X_G3_FOG_ENTRIES; break;
    case V9X_GR_GAMMA_TABLE_ENTRIES:     values[0] = V9X_G3_GAMMA_ENTRIES; break;
    case V9X_GR_BITS_GAMMA:              values[0] = 8ul; break;
    case V9X_GR_IS_BUSY:                 values[0] = 0ul; break;
    case V9X_GR_LFB_PIXEL_PIPE:          values[0] = 0ul; break;
    case V9X_GR_MAX_TEXTURE_SIZE:        values[0] = V9X_G3_MAX_TEXTURE; break;
    case V9X_GR_MAX_TEXTURE_ASPECT_RATIO: values[0] = V9X_G3_MAX_ASPECT; break;
    case V9X_GR_MEMORY_FB:               values[0] = V9X_G3_MEMORY_FB; break;
    case V9X_GR_MEMORY_TMU:              values[0] = V9X_G3_MEMORY_TMU; break;
    case V9X_GR_MEMORY_UMA:              values[0] = 0ul; break;
    case V9X_GR_NUM_BOARDS:              values[0] = 1ul; break;
    case V9X_GR_NUM_FB:                  values[0] = 1ul; break;
    case V9X_GR_NUM_SWAP_HISTORY_BUFFER: values[0] = 0ul; break;
    case V9X_GR_NUM_TMU:                 values[0] = 1ul; break;
    case V9X_GR_PENDING_BUFFERSWAPS:     values[0] = 0ul; break;
    case V9X_GR_REVISION_FB:             values[0] = V9X_G3_REVISION_FB; break;
    case V9X_GR_REVISION_TMU:            values[0] = V9X_G3_REVISION_TMU; break;
    case V9X_GR_SUPPORTS_PASSTHRU:       values[0] = 0ul; break;
    case V9X_GR_TEXTURE_ALIGN:           values[0] = V9X_G3_TEXTURE_ALIGN; break;
    case V9X_GR_VIDEO_POSITION:          values[0] = 0ul; break;
    case V9X_GR_ZDEPTH_MIN_MAX:
        values[0] = 0xfffful; values[1] = 0ul; count = 2ul;
        break;
    case V9X_GR_WDEPTH_MIN_MAX:
        values[0] = 1ul; values[1] = 0xfffful; count = 2ul;
        break;
    default:
        count = 0ul;
        break;
    }
    if (count == 0ul || out == 0 || plength < count * 4ul) {
        wsprintfA(text, "grGet pname=%02lX plength=%lu unanswered", pname,
                  plength);
        v9x_g3_note(text);
        return 0ul;
    }
    for (index = 0ul; index < count; ++index) {
        out[index] = values[index];
    }
    wsprintfA(text, "grGet pname=%02lX -> %lu (%lu bytes)", pname, values[0],
              count * 4ul);
    v9x_g3_note(text);
    return count * 4ul;
}

v9x_u32 __stdcall grGetString(v9x_u32 pname);
v9x_u32 __stdcall grGetString(v9x_u32 pname)
{
    const char *answer = v9x_g3_extension;

    v9x_g3_call(V9X_GLIDE3_IX_grGetString, &pname, 1u);
    switch (pname) {
    case V9X_GR_HARDWARE: answer = v9x_g3_hardware; break;
    case V9X_GR_RENDERER: answer = v9x_g3_renderer; break;
    case V9X_GR_VENDOR:   answer = v9x_g3_vendor; break;
    case V9X_GR_VERSION:  answer = v9x_g3_version; break;
    default:              break;
    }
    return (v9x_u32)answer;
}

/*
 * grQueryResolutions(template, output): the GrResolution entries
 * (resolution, refresh, numColorBuffers, numAuxBuffers; 16 bytes) that
 * match the template, GR_QUERY_ANY matching any value. Returns their size
 * in bytes and writes them to output when it is not null, so a caller asks
 * twice: once for the size, once for the list. Rollcage fills its mode
 * list from it and offered none while it was a stub (A8U4I5, 2026-10-10).
 *
 * The census offers the modes the flip chain has opened (640x480, 800x600,
 * 1024x768) at 60 Hz, one to three colour buffers and up to one aux
 * buffer, keeping only those whose 16-bit buffers fit the board's frame
 * buffer less the 64 KiB 3dfx's own check holds back for the FIFO.
 */
#define V9X_GR_QUERY_ANY          0xfffffffful
#define V9X_GR_REFRESH_60Hz       0x00ul
#define V9X_GR_RESOLUTION_ENTRY   16ul
#define V9X_G3_COLOR_BUFFERS_MAX  3ul
#define V9X_G3_AUX_BUFFERS_MAX    1ul
#define V9X_G3_FIFO_RESERVE       0x10000ul

static const v9x_u32 v9x_g3_modes[3][3] = {
    { 0x07ul, 640ul, 480ul },
    { 0x08ul, 800ul, 600ul },
    { 0x0cul, 1024ul, 768ul }
};

static int v9x_g3_matches(v9x_u32 wanted, v9x_u32 value)
{
    return wanted == V9X_GR_QUERY_ANY || wanted == value;
}

v9x_u32 __stdcall grQueryResolutions(v9x_u32 res_template, v9x_u32 output);
v9x_u32 __stdcall grQueryResolutions(v9x_u32 res_template, v9x_u32 output)
{
    const v9x_u32 *want = (const v9x_u32 *)res_template;
    v9x_u32 *out = (v9x_u32 *)output;
    v9x_u32 args[2];
    v9x_u32 size = 0ul;
    v9x_u32 mode;
    v9x_u32 color;
    v9x_u32 aux;
    char text[160];

    args[0] = res_template;
    args[1] = output;
    v9x_g3_call(V9X_GLIDE3_IX_grQueryResolutions, args, 2u);
    if (want == 0) {
        v9x_g3_note("grQueryResolutions template=null -> 0");
        return 0ul;
    }
    for (mode = 0ul; mode < 3ul; ++mode) {
        if (!v9x_g3_matches(want[0], v9x_g3_modes[mode][0]) ||
            !v9x_g3_matches(want[1], V9X_GR_REFRESH_60Hz)) {
            continue;
        }
        for (color = 1ul; color <= V9X_G3_COLOR_BUFFERS_MAX; ++color) {
            for (aux = 0ul; aux <= V9X_G3_AUX_BUFFERS_MAX; ++aux) {
                if (!v9x_g3_matches(want[2], color) ||
                    !v9x_g3_matches(want[3], aux) ||
                    v9x_g3_modes[mode][1] * v9x_g3_modes[mode][2] * 2ul *
                        (color + aux) >=
                        V9X_G3_MEMORY_FB - V9X_G3_FIFO_RESERVE) {
                    continue;
                }
                size += V9X_GR_RESOLUTION_ENTRY;
                if (out != 0) {
                    out[0] = v9x_g3_modes[mode][0];
                    out[1] = V9X_GR_REFRESH_60Hz;
                    out[2] = color;
                    out[3] = aux;
                    out += 4;
                }
            }
        }
    }
    wsprintfA(text, "grQueryResolutions template=%08lX,%08lX,%08lX,%08lX -> %lu bytes",
              want[0], want[1], want[2], want[3], size);
    v9x_g3_note(text);
    return size;
}

/* ---- texture memory -------------------------------------------------- */

/*
 * Bytes a mip chain needs: from largeLodLog2 down to smallLodLog2, each
 * level halving both sides to a minimum of one, at one or two bytes a
 * texel, rounded up to the texture alignment. aspectRatioLog2 is positive
 * for a wide texture (glide.h GR_ASPECT_LOG2_*).
 */
static v9x_u32 v9x_g3_chain_bytes(v9x_u32 small_lod, v9x_u32 large_lod,
                                  v9x_u32 aspect, v9x_u32 format)
{
    v9x_u32 bytes = 0ul;
    v9x_u32 texel = format >= V9X_GR_TEXFMT_16BIT ? 2ul : 1ul;
    v9x_s32 ratio = (v9x_s32)aspect;
    v9x_u32 lod;

    if (large_lod > 8ul || small_lod > large_lod) {
        return 0ul;
    }
    for (lod = small_lod; lod <= large_lod; ++lod) {
        v9x_u32 width = 1ul << lod;
        v9x_u32 height = 1ul << lod;

        if (ratio > 0) {
            height = (lod >= (v9x_u32)ratio) ? 1ul << (lod - (v9x_u32)ratio) : 1ul;
        } else if (ratio < 0) {
            width = (lod >= (v9x_u32)-ratio) ? 1ul << (lod - (v9x_u32)-ratio) : 1ul;
        }
        bytes += width * height * texel;
    }
    return (bytes + V9X_G3_TEXTURE_ALIGN - 1ul) & ~(V9X_G3_TEXTURE_ALIGN - 1ul);
}

v9x_u32 __stdcall grTexMinAddress(v9x_u32 tmu);
v9x_u32 __stdcall grTexMinAddress(v9x_u32 tmu)
{
    v9x_g3_call(V9X_GLIDE3_IX_grTexMinAddress, &tmu, 1u);
    return 0ul;
}

/* The highest start address that still fits the largest texture. */
v9x_u32 __stdcall grTexMaxAddress(v9x_u32 tmu);
v9x_u32 __stdcall grTexMaxAddress(v9x_u32 tmu)
{
    v9x_g3_call(V9X_GLIDE3_IX_grTexMaxAddress, &tmu, 1u);
    return V9X_G3_MEMORY_TMU - V9X_G3_MAX_TEXTURE * V9X_G3_MAX_TEXTURE * 2ul;
}

/* GrTexInfo is five 32-bit fields: smallLodLog2, largeLodLog2,
 * aspectRatioLog2, format, data. */
v9x_u32 __stdcall grTexTextureMemRequired(v9x_u32 even_odd, v9x_u32 info);
v9x_u32 __stdcall grTexTextureMemRequired(v9x_u32 even_odd, v9x_u32 info)
{
    v9x_u32 args[2];
    const v9x_u32 *texture = (const v9x_u32 *)info;

    args[0] = even_odd;
    args[1] = info;
    v9x_g3_call(V9X_GLIDE3_IX_grTexTextureMemRequired, args, 2u);
    if (texture == 0) {
        return 0ul;
    }
    return v9x_g3_chain_bytes(texture[0], texture[1], texture[2], texture[3]);
}

v9x_u32 __stdcall grTexCalcMemRequired(v9x_u32 small_lod, v9x_u32 large_lod,
                                       v9x_u32 aspect, v9x_u32 format);
v9x_u32 __stdcall grTexCalcMemRequired(v9x_u32 small_lod, v9x_u32 large_lod,
                                       v9x_u32 aspect, v9x_u32 format)
{
    v9x_u32 args[4];

    args[0] = small_lod;
    args[1] = large_lod;
    args[2] = aspect;
    args[3] = format;
    v9x_g3_call(V9X_GLIDE3_IX_grTexCalcMemRequired, args, 4u);
    return v9x_g3_chain_bytes(small_lod, large_lod, aspect, format);
}

/* The texture calls log the GrTexInfo they are handed, which carries the
 * formats and sizes the census is after. */
static void v9x_g3_texinfo(const char *who, v9x_u32 info)
{
    const v9x_u32 *texture = (const v9x_u32 *)info;
    char text[160];

    if (texture == 0) {
        return;
    }
    wsprintfA(text, "%s info small=%ld large=%ld aspect=%ld format=%02lX",
              who, (long)texture[0], (long)texture[1], (long)texture[2],
              texture[3]);
    EnterCriticalSection(&v9x_g3_lock);
    if (!v9x_g3_seen_before(V9X_GLIDE3_EXPORT_COUNT, texture, 4u)) {
        v9x_g3_log(text);
    }
    LeaveCriticalSection(&v9x_g3_lock);
}

void __stdcall grTexSource(v9x_u32 tmu, v9x_u32 address, v9x_u32 even_odd,
                           v9x_u32 info);
void __stdcall grTexSource(v9x_u32 tmu, v9x_u32 address, v9x_u32 even_odd,
                           v9x_u32 info)
{
    v9x_u32 args[4];

    args[0] = tmu;
    args[1] = address;
    args[2] = even_odd;
    args[3] = info;
    v9x_g3_call(V9X_GLIDE3_IX_grTexSource, args, 4u);
    v9x_g3_texinfo("grTexSource", info);
}

void __stdcall grTexDownloadMipMap(v9x_u32 tmu, v9x_u32 address,
                                   v9x_u32 even_odd, v9x_u32 info);
void __stdcall grTexDownloadMipMap(v9x_u32 tmu, v9x_u32 address,
                                   v9x_u32 even_odd, v9x_u32 info)
{
    v9x_u32 args[4];

    args[0] = tmu;
    args[1] = address;
    args[2] = even_odd;
    args[3] = info;
    v9x_g3_call(V9X_GLIDE3_IX_grTexDownloadMipMap, args, 4u);
    v9x_g3_texinfo("grTexDownloadMipMap", info);
}

/* ---- drawing ---------------------------------------------------------- */

/* The first bytes of a vertex, as the layout the game declared reads
 * them; logged for the first draws only. */
static void v9x_g3_vertex(const char *who, const v9x_u8 *vertex)
{
    char text[400];
    int length;
    unsigned int index;

    if (vertex == 0) {
        return;
    }
    length = wsprintfA(text, "%s vertex", who);
    for (index = 0u; index < V9X_G3_VERTEX_DUMP; index += 4u) {
        length += wsprintfA(text + length, " %08lX",
                            *(const v9x_u32 *)(vertex + index));
    }
    v9x_g3_note(text);
}

void __stdcall grDrawVertexArray(v9x_u32 mode, v9x_u32 count,
                                 v9x_u32 pointers);
void __stdcall grDrawVertexArray(v9x_u32 mode, v9x_u32 count,
                                 v9x_u32 pointers)
{
    v9x_u32 args[3];

    args[0] = mode;
    args[1] = count;
    args[2] = pointers;
    v9x_g3_call(V9X_GLIDE3_IX_grDrawVertexArray, args, 3u);
    if (v9x_g3_calls[V9X_GLIDE3_IX_grDrawVertexArray] <= 8ul &&
        pointers != 0ul && count != 0ul) {
        v9x_g3_vertex("grDrawVertexArray", *(const v9x_u8 *const *)pointers);
    }
}

void __stdcall grDrawVertexArrayContiguous(v9x_u32 mode, v9x_u32 count,
                                           v9x_u32 vertices, v9x_u32 stride);
void __stdcall grDrawVertexArrayContiguous(v9x_u32 mode, v9x_u32 count,
                                           v9x_u32 vertices, v9x_u32 stride)
{
    v9x_u32 args[4];

    args[0] = mode;
    args[1] = count;
    args[2] = vertices;
    args[3] = stride;
    v9x_g3_call(V9X_GLIDE3_IX_grDrawVertexArrayContiguous, args, 4u);
    if (v9x_g3_calls[V9X_GLIDE3_IX_grDrawVertexArrayContiguous] <= 8ul &&
        vertices != 0ul && count != 0ul) {
        v9x_g3_vertex("grDrawVertexArrayContiguous",
                      (const v9x_u8 *)vertices);
    }
}

/* ---- window, swap and frame buffer ---------------------------------- */

/* GrScreenResolution_t values (sst1vid.h) the window is sized to. */
#define V9X_GR_RESOLUTION_640x480   0x07ul
#define V9X_GR_RESOLUTION_800x600   0x08ul
#define V9X_GR_RESOLUTION_1024x768  0x0cul

/*
 * grSstWinOpen(hWnd, resolution, refresh, colorFormat, origin, nColBuf,
 * nAuxBuf): a context handle, any non-zero value. The census takes no
 * display, but it does size the game's window to the resolution at the
 * screen's top-left corner: a Voodoo owns the screen, so Diablo II leaves
 * its window a few pixels wide, and injected clicks then miss it
 * (A8U4I5, 2026-10-10). Nothing is drawn into it.
 */
v9x_u32 __stdcall grSstWinOpen(v9x_u32 window, v9x_u32 resolution,
                               v9x_u32 refresh, v9x_u32 color_format,
                               v9x_u32 origin, v9x_u32 color_buffers,
                               v9x_u32 aux_buffers);
v9x_u32 __stdcall grSstWinOpen(v9x_u32 window, v9x_u32 resolution,
                               v9x_u32 refresh, v9x_u32 color_format,
                               v9x_u32 origin, v9x_u32 color_buffers,
                               v9x_u32 aux_buffers)
{
    v9x_u32 args[7];

    args[0] = window;
    args[1] = resolution;
    args[2] = refresh;
    args[3] = color_format;
    args[4] = origin;
    args[5] = color_buffers;
    args[6] = aux_buffers;
    v9x_g3_call(V9X_GLIDE3_IX_grSstWinOpen, args, 7u);
    if (window != 0ul) {
        int width = 640;
        int height = 480;

        if (resolution == V9X_GR_RESOLUTION_800x600) {
            width = 800;
            height = 600;
        } else if (resolution == V9X_GR_RESOLUTION_1024x768) {
            width = 1024;
            height = 768;
        }
        SetWindowPos((HWND)window, HWND_TOP, 0, 0, width, height,
                     SWP_SHOWWINDOW);
    }
    return 1ul;
}

v9x_u32 __stdcall grSstWinClose(v9x_u32 context);
v9x_u32 __stdcall grSstWinClose(v9x_u32 context)
{
    v9x_g3_call(V9X_GLIDE3_IX_grSstWinClose, &context, 1u);
    return 1ul;
}

v9x_u32 __stdcall grSelectContext(v9x_u32 context);
v9x_u32 __stdcall grSelectContext(v9x_u32 context)
{
    v9x_g3_call(V9X_GLIDE3_IX_grSelectContext, &context, 1u);
    return 1ul;
}

void __stdcall grBufferSwap(v9x_u32 interval);
void __stdcall grBufferSwap(v9x_u32 interval)
{
    v9x_g3_call(V9X_GLIDE3_IX_grBufferSwap, &interval, 1u);
    EnterCriticalSection(&v9x_g3_lock);
    ++v9x_g3_swaps;
    v9x_g3_capturing = (v9x_g3_swaps < V9X_G3_CAPTURE_FIRST ||
                        v9x_g3_swaps % V9X_G3_CAPTURE_EVERY == 0ul) ? 1ul : 0ul;
    if (v9x_g3_capturing) {
        v9x_g3_capture_lines = 0ul;
        v9x_g3_log("---- frame ----");
    }
    LeaveCriticalSection(&v9x_g3_lock);
    Sleep(V9X_G3_FRAME_MS * (interval != 0ul ? interval : 1ul));
}

/*
 * grLfbLock(type, buffer, writeMode, origin, pixelPipeline, info): the
 * census hands back memory of its own, so a game that writes the frame
 * buffer runs on and the log shows how. GrLfbInfo_t is size, lfbPtr,
 * strideInBytes, writeMode, origin.
 */
v9x_u32 __stdcall grLfbLock(v9x_u32 type, v9x_u32 buffer, v9x_u32 write_mode,
                            v9x_u32 origin, v9x_u32 pixel_pipeline,
                            v9x_u32 info);
v9x_u32 __stdcall grLfbLock(v9x_u32 type, v9x_u32 buffer, v9x_u32 write_mode,
                            v9x_u32 origin, v9x_u32 pixel_pipeline,
                            v9x_u32 info)
{
    v9x_u32 args[6];
    v9x_u32 *out = (v9x_u32 *)info;
    v9x_u32 mode = write_mode;

    args[0] = type;
    args[1] = buffer;
    args[2] = write_mode;
    args[3] = origin;
    args[4] = pixel_pipeline;
    args[5] = info;
    v9x_g3_call(V9X_GLIDE3_IX_grLfbLock, args, 6u);
    if (out == 0 || v9x_g3_lfb == 0) {
        return 0ul;
    }
    if (mode == V9X_GR_LFBWRITEMODE_ANY) {
        mode = V9X_GR_LFBWRITEMODE_565;
    }
    out[1] = (v9x_u32)v9x_g3_lfb;
    out[2] = (mode == V9X_GR_LFBWRITEMODE_888 ||
              mode == V9X_GR_LFBWRITEMODE_8888) ? V9X_G3_LFB_STRIDE_32
                                                : V9X_G3_LFB_STRIDE_16;
    out[3] = mode;
    out[4] = origin;
    return 1ul;
}

v9x_u32 __stdcall grLfbUnlock(v9x_u32 type, v9x_u32 buffer);
v9x_u32 __stdcall grLfbUnlock(v9x_u32 type, v9x_u32 buffer)
{
    v9x_u32 args[2];

    args[0] = type;
    args[1] = buffer;
    v9x_g3_call(V9X_GLIDE3_IX_grLfbUnlock, args, 2u);
    return 1ul;
}

void __stdcall grGlideShutdown(void);
void __stdcall grGlideShutdown(void)
{
    v9x_g3_call(V9X_GLIDE3_IX_grGlideShutdown, 0, 0u);
    EnterCriticalSection(&v9x_g3_lock);
    v9x_g3_summary("shutdown");
    LeaveCriticalSection(&v9x_g3_lock);
}

/* ---- the DLL ----------------------------------------------------------- */

BOOL __stdcall V9xGlide3Entry(HINSTANCE instance, DWORD reason,
                              LPVOID reserved);
BOOL __stdcall V9xGlide3Entry(HINSTANCE instance, DWORD reason,
                              LPVOID reserved)
{
    char path[MAX_PATH];
    char text[MAX_PATH + 96];

    (void)instance;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        InitializeCriticalSection(&v9x_g3_lock);
        v9x_g3_lfb = (v9x_u8 *)VirtualAlloc(0,
            V9X_G3_LFB_ROWS * V9X_G3_LFB_STRIDE_32, MEM_COMMIT,
            PAGE_READWRITE);
        v9x_g3_summary_tick = GetTickCount();
        path[0] = '\0';
        GetModuleFileNameA(0, path, sizeof(path));
        wsprintfA(text, "attach census build=%s process=%s",
                  V9X_BUILD_ID, path);
        v9x_g3_log(text);
        wsprintfA(text, "board fb=%lu tmu=%lu tmus=1 maxtex=%lu hardware=%s",
                  V9X_G3_MEMORY_FB, V9X_G3_MEMORY_TMU, V9X_G3_MAX_TEXTURE,
                  v9x_g3_hardware);
        v9x_g3_log(text);
    } else if (reason == DLL_PROCESS_DETACH) {
        v9x_g3_summary("detach");
        DeleteCriticalSection(&v9x_g3_lock);
    }
    return TRUE;
}
