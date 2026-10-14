/*
 * ATI3DCIF.DLL (docs\plans\ati-3dcif-wrapper.md, Phase 3): ATI's 3D Rage
 * Common Interface over Velocity9x's render interface. One of the DLL's
 * two platform files (scripts\check-tree.ps1 lists it); cif_surface.c is
 * the other, and the pure logic is cif_vertex.c, cif_state.c and
 * cif_texfmt.c.
 *
 * The application owns the screen: it creates its DirectDraw chain and
 * hands this DLL pointers into it (C3D_ERS_SURF_DRAW_PTR, _SURF_Z_PTR).
 * Draws name those surfaces to the render interface by address (ABI 5's
 * raw target and depth). Textures are copied at TextureReg - no demo in
 * the census rewrote one after registering it
 * (docs\decisions\2026-10-14-3dcif-demo-census.md) - converted when first
 * drawn with, into one-level video-memory surfaces of the DLL's own.
 *
 * Each entry point is exported twice, as ATI's are: the __stdcall function
 * and ATI3DCIF_<name>_lib, a variable holding a pointer to it. The demos
 * find only the second, through GetProcAddress, and call through it.
 *
 * The census's logging is kept (C:\V9XDIAG\V9XCIF.LOG): every call is
 * counted, and logged in captured frames, among each export's first calls,
 * or when its key is new; refusals and skipped draws are logged the first
 * few times each.
 *
 * Not yet: mipmaps (level 0 only, drawn without level selection), VQ and
 * tiled textures (registered, drawn untextured), 555 targets on engines
 * that draw 565 only (the Rage II and Mach64), TLVERTEX vertices, lines,
 * points and rectangles.
 *
 * Constants and layouts are ATI3DCIF.H (SDK 4.1) and the SDK-C02700 Rev
 * 1.30 guide, read for facts under the plan's licence rule.
 *
 * Runtime-free like the other Velocity9x DLLs: KERNEL32 and USER32 only.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"
#include "cif_api.h"
#include "cif_vertex.h"
#include "cif_state.h"
#include "cif_texfmt.h"
#include "cif_surface.h"

/* ---- ATI3DCIF.H facts not in cif_api.h ------------------------------ */

#define V9X_C3D_EC_GENFAIL         1ul
#define V9X_C3D_EC_MEMALLOCFAIL    2ul

/* C3D_3DCIFINFO is twelve 32-bit fields; build 17 and earlier had seven. */
#define V9X_C3D_INFO_BUILD17_BYTES 28ul
#define V9X_C3D_INFO_FIELDS        12ul

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

/* C3D_EPIXFMT: the draw surface's layout when SURF_DRAW_PF is set. */
#define V9X_C3D_EPF_RGB1555        3ul
#define V9X_C3D_EPF_RGB565         4ul

/* C3D_TMAP byte offsets. 72 bytes end at htxpalTexPalette; the Rage PRO
 * header adds bClampS, bClampT, bAlphaBlend and eTexTiling (88). */
#define V9X_C3D_TMAP_MIPMAP        4u
#define V9X_C3D_TMAP_LEVELS        8u
#define V9X_C3D_TMAP_XLG2          52u
#define V9X_C3D_TMAP_YLG2          56u
#define V9X_C3D_TMAP_FORMAT        60u
#define V9X_C3D_TMAP_CHROMA        64u
#define V9X_C3D_TMAP_PALETTE       68u
#define V9X_C3D_TMAP_BASE_BYTES    72u
#define V9X_C3D_TMAP_CLAMP_S       72u
#define V9X_C3D_TMAP_CLAMP_T       76u
#define V9X_C3D_TMAP_TILING        84u
#define V9X_C3D_TMAP_RAGE3_BYTES   88u

/* A VQ codebook: 256 entries of four 16-bit texels. */
#define V9X_CIF_PALETTE_ENTRIES    256u
#define V9X_CIF_CODEBOOK_WORDS     (256u * 4u)

/* ---- the device the DLL describes ----------------------------------- */

/*
 * The census's identity, kept until the plan's Identity decision: ASIC id
 * 0x401, the lowest the SDK 4.1 Knight demos accept as a Rage PRO
 * (AtiDemo.cpp), and the Rage PRO caps less LOAD_OBJECT and tiling.
 * Chosen, not measured; the guide gives no ASIC id values.
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

/* The census's rules (see the file comment). */
#define V9X_CIF_EXPORTS          19u
#define V9X_CIF_CAPTURE_FIRST    3ul
#define V9X_CIF_CAPTURE_EVERY    600ul
#define V9X_CIF_CAPTURE_LINES    400ul
#define V9X_CIF_SAMPLE_FIRST     8ul
#define V9X_CIF_SEEN_SLOTS       4096u
#define V9X_CIF_SUMMARY_MS       15000ul
/* Refusals, skips and approximations logged per kind. */
#define V9X_CIF_NOTES_MAX        16ul

/* Fixed pools: ATI's early DLLs allowed one context at a time. */
#define V9X_CIF_CONTEXTS         8u
#define V9X_CIF_TEXTURES         1024u
#define V9X_CIF_PALETTES         64u

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

/* Why a draw was not submitted, counted and logged the first few times. */
#define V9X_CIF_SKIP_NO_CONTEXT   0u
#define V9X_CIF_SKIP_NO_TARGET    1u
#define V9X_CIF_SKIP_VERTEX_TYPE  2u
#define V9X_CIF_SKIP_PRIMITIVE    3u
#define V9X_CIF_SKIP_TARGET_FORMAT 4u
#define V9X_CIF_SKIP_TEXTURE      5u
#define V9X_CIF_SKIP_NO_DEVICE    6u
#define V9X_CIF_SKIPS             7u

static const char *const v9x_cif_skip_names[V9X_CIF_SKIPS] = {
    "outside RenderBegin", "no draw surface", "vertex type",
    "primitive", "target format", "texture", "no device"
};

/* ---- objects -------------------------------------------------------- */

struct v9x_cif_context {
    v9x_u32 live;
    V9X_CIF_STATE state;
};

/*
 * One converted copy of a texture: opaque (RGB565, for draws that do not
 * read its alpha - the engines take MODULATE with the fragment's alpha
 * only then), with its own alpha, or with its chroma key.
 */
#define V9X_CIF_VARIANT_OPAQUE 0u
#define V9X_CIF_VARIANT_ALPHA  1u
#define V9X_CIF_VARIANT_KEYED  2u
#define V9X_CIF_VARIANTS       3u

struct v9x_cif_variant {
    v9x_u16 *texels;        /* width x height, packed, or null */
    void *surface;          /* its video-memory copy, or null */
    v9x_u32 abi_format;
    v9x_u32 failed;         /* conversion refused: do not retry */
    v9x_u32 last_frame;     /* the frame it was last drawn with */
};

struct v9x_cif_texture {
    v9x_u32 live;
    v9x_u32 format;         /* V9X_C3D_ETF_* */
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 mipmapped;
    v9x_u32 clamp_s;
    v9x_u32 clamp_t;
    v9x_u32 key;            /* C3D_COLOR */
    v9x_u32 palette;        /* C3D_HTXPAL as registered */
    v9x_u32 drawable;       /* a format and layout this DLL converts */
    v9x_u32 shrink;         /* log2 decimation to fit the engine's size */
    v9x_u8 *source;         /* the level kept, as registered */
    struct v9x_cif_variant variant[V9X_CIF_VARIANTS];
};

struct v9x_cif_palette {
    v9x_u32 live;
    v9x_u32 kind;           /* C3D_ECI_TMAP_TYPE */
    v9x_u32 entries[V9X_CIF_PALETTE_ENTRIES];
};

static struct v9x_cif_context v9x_cif_contexts[V9X_CIF_CONTEXTS];
static struct v9x_cif_texture v9x_cif_textures[V9X_CIF_TEXTURES];
static struct v9x_cif_palette v9x_cif_palettes[V9X_CIF_PALETTES];
static struct v9x_cif_context *v9x_cif_current;

/* The display as it was at RenderBegin: the default draw surface. */
static v9x_u32 v9x_cif_display_width;
static v9x_u32 v9x_cif_display_height;
static v9x_u32 v9x_cif_display_pitch;
static v9x_u32 v9x_cif_display_format;

static V9X_R3D_ABI_VERTEX v9x_cif_batch[V9X_R3D_ABI_BATCH_MAX * 3ul];

static v9x_u32 v9x_cif_sequence;
static v9x_u32 v9x_cif_calls[V9X_CIF_EXPORTS];
static v9x_u32 v9x_cif_seen[V9X_CIF_SEEN_SLOTS];
static v9x_u32 v9x_cif_seen_full;
static v9x_u32 v9x_cif_capturing = 1ul;
static v9x_u32 v9x_cif_capture_lines;
static v9x_u32 v9x_cif_frames;
static v9x_u32 v9x_cif_summary_tick;
static v9x_u32 v9x_cif_triangles_drawn;
static v9x_u32 v9x_cif_triangles_refused;
static v9x_u32 v9x_cif_refusals_logged;
static v9x_u32 v9x_cif_approximations_logged;
static v9x_u32 v9x_cif_skips[V9X_CIF_SKIPS];
static int v9x_cif_device_ready;
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

void v9x_cif_log_text(const char *text)
{
    v9x_cif_log(text);
}

void v9x_cif_log3(const char *format, v9x_u32 a, v9x_u32 b, v9x_u32 c)
{
    char text[400];

    wsprintfA(text, format, a, b, c);
    v9x_cif_log(text);
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
    char text[200];
    unsigned int ix;

    wsprintfA(text, "summary %s frames=%lu drawn=%lu refused=%lu", why,
              v9x_cif_frames, v9x_cif_triangles_drawn,
              v9x_cif_triangles_refused);
    v9x_cif_log(text);
    for (ix = 0u; ix < V9X_CIF_EXPORTS; ++ix) {
        if (v9x_cif_calls[ix] != 0ul) {
            wsprintfA(text, "  %s %lu", v9x_cif_names[ix], v9x_cif_calls[ix]);
            v9x_cif_log(text);
        }
    }
    for (ix = 0u; ix < V9X_CIF_SKIPS; ++ix) {
        if (v9x_cif_skips[ix] != 0ul) {
            wsprintfA(text, "  skipped (%s) %lu", v9x_cif_skip_names[ix],
                      v9x_cif_skips[ix]);
            v9x_cif_log(text);
        }
    }
}

/*
 * Every export comes through here: count the call, and log its name and
 * raw arguments in a captured frame, among its first calls, or for a key
 * not seen before. The caller holds the lock.
 */
static void v9x_cif_call(unsigned int ix, const v9x_u32 *args,
                         unsigned int count, const v9x_u32 *key,
                         unsigned int keys)
{
    char text[400];
    v9x_u32 call;
    int length;
    unsigned int index;
    int log_it = 0;
    DWORD now;

    call = ++v9x_cif_calls[ix];
    if (v9x_cif_capturing && v9x_cif_capture_lines < V9X_CIF_CAPTURE_LINES) {
        ++v9x_cif_capture_lines;
        log_it = 1;
    } else if (call <= V9X_CIF_SAMPLE_FIRST) {
        log_it = 1;
    } else if (!v9x_cif_seen_before(ix, key, keys)) {
        log_it = 1;
    }
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
}

static void v9x_cif_skip(unsigned int why, v9x_u32 detail)
{
    char text[160];

    if (v9x_cif_skips[why]++ < V9X_CIF_NOTES_MAX) {
        wsprintfA(text, "draw skipped: %s detail=%08lX frame=%lu",
                  v9x_cif_skip_names[why], detail, v9x_cif_frames);
        v9x_cif_log(text);
    }
}

/* Reads a 32-bit word the application handed over, or 0 if the pointer
 * is not readable: the DLL returns BADPARAM rather than fault. */
static v9x_u32 v9x_cif_peek(v9x_u32 address)
{
    if (address == 0ul || IsBadReadPtr((const void *)address, 4u)) {
        return 0ul;
    }
    return *(const v9x_u32 *)address;
}

static void *v9x_cif_alloc(v9x_u32 bytes)
{
    return HeapAlloc(GetProcessHeap(), 0, bytes);
}

static void v9x_cif_free(void *block)
{
    if (block != 0) {
        HeapFree(GetProcessHeap(), 0, block);
    }
}

/* ---- textures -------------------------------------------------------- */

static void v9x_cif_variant_free(struct v9x_cif_variant *variant)
{
    v9x_cif_hwtex_release(variant->surface);
    v9x_cif_free(variant->texels);
    variant->surface = 0;
    variant->texels = 0;
    variant->failed = 0ul;
}

static void v9x_cif_texture_free(struct v9x_cif_texture *texture)
{
    unsigned int index;

    for (index = 0u; index < V9X_CIF_VARIANTS; ++index) {
        v9x_cif_variant_free(&texture->variant[index]);
    }
    v9x_cif_free(texture->source);
    texture->source = 0;
    texture->live = 0ul;
}

static struct v9x_cif_texture *v9x_cif_texture_from(v9x_u32 handle)
{
    v9x_u32 base = (v9x_u32)&v9x_cif_textures[0];
    v9x_u32 end = (v9x_u32)&v9x_cif_textures[V9X_CIF_TEXTURES];
    struct v9x_cif_texture *texture;

    if (handle < base || handle >= end ||
        (handle - base) % sizeof(struct v9x_cif_texture) != 0ul) {
        return 0;
    }
    texture = (struct v9x_cif_texture *)handle;
    return texture->live ? texture : 0;
}

static struct v9x_cif_palette *v9x_cif_palette_from(v9x_u32 handle)
{
    v9x_u32 base = (v9x_u32)&v9x_cif_palettes[0];
    v9x_u32 end = (v9x_u32)&v9x_cif_palettes[V9X_CIF_PALETTES];
    struct v9x_cif_palette *palette;

    if (handle < base || handle >= end ||
        (handle - base) % sizeof(struct v9x_cif_palette) != 0ul) {
        return 0;
    }
    palette = (struct v9x_cif_palette *)handle;
    return palette->live ? palette : 0;
}

/*
 * Video memory holds the application's own textures as well as these
 * copies (the Knight demos' 4444 textures did not fit on the Rage IIC's
 * 4 MiB, boot 456). Releases the surface of the copy drawn with longest
 * ago, but none drawn with in this frame; its texels stay, and it is
 * uploaded again when next drawn with. Non-zero if one was released.
 */
static v9x_u32 v9x_cif_evictions;

static int v9x_cif_evict_one(void)
{
    struct v9x_cif_variant *oldest = 0;
    unsigned int index;
    unsigned int which;

    for (index = 0u; index < V9X_CIF_TEXTURES; ++index) {
        if (!v9x_cif_textures[index].live) {
            continue;
        }
        for (which = 0u; which < V9X_CIF_VARIANTS; ++which) {
            struct v9x_cif_variant *variant =
                &v9x_cif_textures[index].variant[which];

            if (variant->surface == 0 ||
                variant->last_frame == v9x_cif_frames) {
                continue;
            }
            if (oldest == 0 || variant->last_frame < oldest->last_frame) {
                oldest = variant;
            }
        }
    }
    if (oldest == 0) {
        return 0;
    }
    v9x_cif_hwtex_release(oldest->surface);
    oldest->surface = 0;
    if (v9x_cif_evictions++ < V9X_CIF_NOTES_MAX) {
        v9x_cif_log3("texture surface evicted: last drawn frame %lu, now %lu",
                     oldest->last_frame, v9x_cif_frames, 0ul);
    }
    return 1;
}

/* The size uploaded: the kept level's, decimated by `shrink`, never below
 * one texel. */
static v9x_u32 v9x_cif_texture_width(const struct v9x_cif_texture *texture)
{
    v9x_u32 width = texture->width >> texture->shrink;

    return width != 0ul ? width : 1ul;
}

static v9x_u32 v9x_cif_texture_height(const struct v9x_cif_texture *texture)
{
    v9x_u32 height = texture->height >> texture->shrink;

    return height != 0ul ? height : 1ul;
}

/*
 * A texture's converted copy for one variant, made on first use: the
 * registered level 0, row by row through cif_texfmt.c, into a surface of
 * the DLL's own. A palette texture takes the palette's entries as they
 * are now. Non-zero when the variant has a surface.
 */
static int v9x_cif_texture_ready(struct v9x_cif_texture *texture,
                                 unsigned int which)
{
    struct v9x_cif_variant *variant = &texture->variant[which];
    const struct v9x_cif_palette *palette = 0;
    v9x_u32 keyed = which == V9X_CIF_VARIANT_KEYED ? 1ul : 0ul;
    v9x_u32 row_bytes;
    v9x_u32 row;
    v9x_u32 nibble = V9X_C3D_ECI_TMAP_4BIT_LOW;

    if (variant->surface != 0) {
        variant->last_frame = v9x_cif_frames;
        return 1;
    }
    if (variant->failed || !texture->drawable) {
        return 0;
    }
    variant->failed = 1ul;
    if (texture->format == V9X_C3D_ETF_CI8 ||
        texture->format == V9X_C3D_ETF_CI4) {
        palette = v9x_cif_palette_from(texture->palette);
        if (palette == 0) {
            return 0;
        }
        nibble = palette->kind;
    }
    if (variant->texels == 0) {
        variant->texels = (v9x_u16 *)v9x_cif_alloc(texture->width *
                                                    texture->height * 2ul);
        if (variant->texels == 0) {
            return 0;
        }
        row_bytes = (texture->width * v9x_cif_texfmt_bits(texture->format)) /
                    8ul;
        for (row = 0ul; row < texture->height; ++row) {
            if (!v9x_cif_texfmt_convert_row(
                    texture->format, texture->source + row * row_bytes,
                    texture->width,
                    palette != 0 ? palette->entries : 0, nibble, keyed,
                    texture->key, variant->texels + row * texture->width)) {
                return 0;
            }
        }
        variant->abi_format = v9x_cif_texfmt_abi_format(texture->format,
                                                        keyed);
        if (texture->shrink != 0ul) {
            v9x_u32 width = texture->width;
            v9x_u32 height = texture->height;

            v9x_cif_texfmt_decimate(variant->texels, &width, &height,
                                    texture->shrink);
        }
        if (which == V9X_CIF_VARIANT_OPAQUE) {
            if (!v9x_cif_texfmt_to_565(variant->abi_format, variant->texels,
                                       v9x_cif_texture_width(texture) *
                                           v9x_cif_texture_height(texture))) {
                return 0;
            }
            variant->abi_format = V9X_R3D_ABI_FORMAT_RGB565;
        }
    }
    variant->surface = v9x_cif_hwtex_create(v9x_cif_texture_width(texture),
                                            v9x_cif_texture_height(texture),
                                            variant->abi_format);
    while (variant->surface == 0 && v9x_cif_evict_one()) {
        variant->surface = v9x_cif_hwtex_create(
            v9x_cif_texture_width(texture), v9x_cif_texture_height(texture),
            variant->abi_format);
    }
    if (variant->surface == 0) {
        /* Out of video memory with nothing to evict: try again next
         * frame, when this frame's textures can go. */
        variant->failed = 0ul;
        return 0;
    }
    variant->last_frame = v9x_cif_frames;
    if (!v9x_cif_hwtex_upload(variant->surface, variant->texels,
                              v9x_cif_texture_width(texture),
                              v9x_cif_texture_height(texture))) {
        v9x_cif_hwtex_release(variant->surface);
        variant->surface = 0;
        return 0;
    }
    variant->failed = 0ul;
    return 1;
}

/* ---- module ---------------------------------------------------------- */

v9x_u32 __stdcall ATI3DCIF_Init(void);
v9x_u32 __stdcall ATI3DCIF_Init(void)
{
    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_Init, 0, 0u, 0, 0u);
    v9x_cif_device_ready = v9x_cif_device_open();
    LeaveCriticalSection(&v9x_cif_lock);
    /* Without the render interface (ATI's own driver, or none) there is
     * nothing to draw with: say so now rather than at the first draw. */
    return v9x_cif_device_ready ? V9X_C3D_EC_OK : V9X_C3D_EC_GENFAIL;
}

v9x_u32 __stdcall ATI3DCIF_Term(void);
v9x_u32 __stdcall ATI3DCIF_Term(void)
{
    unsigned int index;

    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_Term, 0, 0u, 0, 0u);
    for (index = 0u; index < V9X_CIF_TEXTURES; ++index) {
        if (v9x_cif_textures[index].live) {
            v9x_cif_texture_free(&v9x_cif_textures[index]);
        }
    }
    v9x_cif_summary("term");
    v9x_cif_device_close();
    v9x_cif_device_ready = 0;
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/*
 * GetInfo fills as much of C3D_3DCIFINFO as the client's u32Size covers:
 * 48 bytes, the 32 of 4.02.0217 to 4.03.0039 (one caps word), or the 28
 * of build 17 and earlier. A size below 28 is BADPARAM, as the guide says
 * for a wrong size.
 */
v9x_u32 __stdcall ATI3DCIF_GetInfo(v9x_u32 info);
v9x_u32 __stdcall ATI3DCIF_GetInfo(v9x_u32 info)
{
    v9x_u32 values[V9X_C3D_INFO_FIELDS];
    v9x_u32 *out = (v9x_u32 *)info;
    v9x_u32 size;
    v9x_u32 index;

    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_GetInfo, &info, 1u, &info, 1u);
    size = v9x_cif_peek(info);
    if (size < V9X_C3D_INFO_BUILD17_BYTES ||
        IsBadWritePtr(out, (UINT)size)) {
        v9x_cif_log3("GetInfo size=%lu refused BADPARAM", size, 0ul, 0ul);
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
    for (index = 1ul; index < V9X_C3D_INFO_FIELDS &&
                      (index + 1ul) * 4ul <= size; ++index) {
        out[index] = values[index];
    }
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/* Gated on C3D_CAPS1_LOAD_OBJECT, which is not advertised. */
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
    v9x_cif_call(V9X_CIF_IX_LoadObject, args, 3u, &size, 1u);
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_NOTIMPYET;
}

/* ---- texture registration ------------------------------------------- */

/*
 * TextureReg(ptmapToReg, phtmap): keeps the C3D_TMAP's facts and a copy of
 * level 0, read as rows of exactly the texture's width - the TMAP has no
 * pitch, so the chip reads texture levels packed. A format this DLL does
 * not convert (VQ, Y8, YUV, a tiled layout) is still registered, so the
 * application runs on; its draws go untextured and are logged.
 */
v9x_u32 __stdcall ATI3DCIF_TextureReg(v9x_u32 tmap, v9x_u32 handle);
v9x_u32 __stdcall ATI3DCIF_TextureReg(v9x_u32 tmap, v9x_u32 handle)
{
    v9x_u32 args[2];
    v9x_u32 key[4];
    v9x_u32 size;
    v9x_u32 bits;
    v9x_u32 bytes;
    v9x_u32 level0;
    v9x_u32 index;
    struct v9x_cif_texture *texture = 0;

    args[0] = tmap;
    args[1] = handle;
    EnterCriticalSection(&v9x_cif_lock);
    size = v9x_cif_peek(tmap);
    key[0] = size;
    key[1] = v9x_cif_peek(tmap + V9X_C3D_TMAP_FORMAT);
    key[2] = v9x_cif_peek(tmap + V9X_C3D_TMAP_XLG2);
    key[3] = v9x_cif_peek(tmap + V9X_C3D_TMAP_YLG2);
    v9x_cif_call(V9X_CIF_IX_TextureReg, args, 2u, key, 4u);
    if (size < V9X_C3D_TMAP_BASE_BYTES || handle == 0ul ||
        IsBadWritePtr((void *)handle, 4u) ||
        key[2] > V9X_C3D_TEXTURE_LG2_MAX || key[3] > V9X_C3D_TEXTURE_LG2_MAX) {
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_BADPARAM;
    }
    for (index = 0ul; index < V9X_CIF_TEXTURES; ++index) {
        if (!v9x_cif_textures[index].live) {
            texture = &v9x_cif_textures[index];
            break;
        }
    }
    if (texture == 0) {
        v9x_cif_log("TextureReg pool full");
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_MEMALLOCFAIL;
    }
    texture->format = key[1];
    texture->width = 1ul << key[2];
    texture->height = 1ul << key[3];
    texture->mipmapped = v9x_cif_peek(tmap + V9X_C3D_TMAP_MIPMAP) != 0ul;
    texture->key = v9x_cif_peek(tmap + V9X_C3D_TMAP_CHROMA);
    texture->palette = v9x_cif_peek(tmap + V9X_C3D_TMAP_PALETTE);
    texture->clamp_s = 0ul;
    texture->clamp_t = 0ul;
    texture->drawable = 0ul;
    texture->source = 0;
    for (index = 0ul; index < V9X_CIF_VARIANTS; ++index) {
        texture->variant[index].texels = 0;
        texture->variant[index].surface = 0;
        texture->variant[index].failed = 0ul;
        texture->variant[index].last_frame = 0ul;
    }
    if (size >= V9X_C3D_TMAP_RAGE3_BYTES) {
        texture->clamp_s = v9x_cif_peek(tmap + V9X_C3D_TMAP_CLAMP_S) != 0ul;
        texture->clamp_t = v9x_cif_peek(tmap + V9X_C3D_TMAP_CLAMP_T) != 0ul;
    }
    /*
     * Larger than the engine samples (the Rage II and Mach64 describe
     * 256; Pixie registers 512x512): a mipmapped texture keeps the first
     * level that fits, which the application supplied, and one without
     * mipmaps is decimated when converted.
     */
    texture->shrink = v9x_cif_texfmt_fit_level(
        key[2], key[3],
        v9x_cif_device_description() != 0
            ? v9x_cif_device_description()->hw_texture_size_max : 0ul);
    level0 = v9x_cif_peek(tmap + V9X_C3D_TMAP_LEVELS);
    if (texture->shrink != 0ul && texture->mipmapped &&
        texture->shrink < V9X_C3D_TEXTURE_LG2_MAX + 1ul &&
        v9x_cif_peek(tmap + V9X_C3D_TMAP_LEVELS +
                     texture->shrink * 4ul) != 0ul) {
        level0 = v9x_cif_peek(tmap + V9X_C3D_TMAP_LEVELS +
                              texture->shrink * 4ul);
        texture->width = texture->width >> texture->shrink;
        texture->height = texture->height >> texture->shrink;
        if (texture->width == 0ul) {
            texture->width = 1ul;
        }
        if (texture->height == 0ul) {
            texture->height = 1ul;
        }
        texture->shrink = 0ul;
    }
    bits = v9x_cif_texfmt_bits(texture->format);
    bytes = (texture->width * texture->height * bits) / 8ul;
    if (bits != 0ul && texture->format != V9X_C3D_ETF_VQ &&
        (size < V9X_C3D_TMAP_RAGE3_BYTES ||
         v9x_cif_peek(tmap + V9X_C3D_TMAP_TILING) != V9X_C3D_ETEXTILE_ON) &&
        level0 != 0ul && !IsBadReadPtr((const void *)level0, bytes)) {
        texture->source = (v9x_u8 *)v9x_cif_alloc(bytes);
        if (texture->source != 0) {
            for (index = 0ul; index < bytes; ++index) {
                texture->source[index] = ((const v9x_u8 *)level0)[index];
            }
            texture->drawable = 1ul;
        }
    }
    if (!texture->drawable) {
        v9x_cif_log3("TextureReg %lux%lu format=%lu not converted: drawn "
                     "untextured", texture->width, texture->height,
                     texture->format);
    }
    texture->live = 1ul;
    *(v9x_u32 *)handle = (v9x_u32)texture;
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

v9x_u32 __stdcall ATI3DCIF_TextureUnreg(v9x_u32 handle);
v9x_u32 __stdcall ATI3DCIF_TextureUnreg(v9x_u32 handle)
{
    struct v9x_cif_texture *texture;
    v9x_u32 zero = 0ul;

    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_TextureUnreg, &handle, 1u, &zero, 1u);
    texture = v9x_cif_texture_from(handle);
    if (texture == 0) {
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_BADPARAM;
    }
    v9x_cif_texture_free(texture);
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/* TexturePaletteCreate(epalette, pPalette, phtpalCreated): 16 or 256
 * C3D_PALETTENTRY, or a VQ codebook of 256 entries of four 16-bit texels
 * (kept, though VQ is not yet drawn). */
v9x_u32 __stdcall ATI3DCIF_TexturePaletteCreate(v9x_u32 kind,
                                                v9x_u32 entries,
                                                v9x_u32 handle);
v9x_u32 __stdcall ATI3DCIF_TexturePaletteCreate(v9x_u32 kind,
                                                v9x_u32 entries,
                                                v9x_u32 handle)
{
    v9x_u32 args[3];
    v9x_u32 count = V9X_CIF_PALETTE_ENTRIES;
    v9x_u32 index;
    struct v9x_cif_palette *palette = 0;

    args[0] = kind;
    args[1] = entries;
    args[2] = handle;
    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_TexturePaletteCreate, args, 3u, &kind, 1u);
    if (kind == V9X_C3D_ECI_TMAP_4BIT_HI || kind == V9X_C3D_ECI_TMAP_4BIT_LOW) {
        count = 16ul;
    } else if (kind == V9X_C3D_ECI_TMAP_VQ) {
        count = V9X_CIF_CODEBOOK_WORDS / 2ul;
    }
    if (handle == 0ul || IsBadWritePtr((void *)handle, 4u) ||
        entries == 0ul || IsBadReadPtr((const void *)entries, count * 4ul)) {
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_BADPARAM;
    }
    for (index = 0ul; index < V9X_CIF_PALETTES; ++index) {
        if (!v9x_cif_palettes[index].live) {
            palette = &v9x_cif_palettes[index];
            break;
        }
    }
    if (palette == 0) {
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_MEMALLOCFAIL;
    }
    for (index = 0ul; index < V9X_CIF_PALETTE_ENTRIES; ++index) {
        palette->entries[index] =
            index < count ? ((const v9x_u32 *)entries)[index] : 0ul;
    }
    palette->kind = kind;
    palette->live = 1ul;
    *(v9x_u32 *)handle = (v9x_u32)palette;
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

v9x_u32 __stdcall ATI3DCIF_TexturePaletteDestroy(v9x_u32 handle);
v9x_u32 __stdcall ATI3DCIF_TexturePaletteDestroy(v9x_u32 handle)
{
    struct v9x_cif_palette *palette;
    v9x_u32 zero = 0ul;

    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_TexturePaletteDestroy, &handle, 1u, &zero, 1u);
    palette = v9x_cif_palette_from(handle);
    if (palette != 0) {
        palette->live = 0ul;
    }
    LeaveCriticalSection(&v9x_cif_lock);
    return palette != 0 ? V9X_C3D_EC_OK : V9X_C3D_EC_BADPARAM;
}

/* Updates the entries; textures already converted keep their colours
 * (no demo in the census animates a palette). */
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
    v9x_u32 index;
    struct v9x_cif_palette *palette;

    args[0] = handle;
    args[1] = start;
    args[2] = count;
    args[3] = entries;
    key[0] = start;
    key[1] = count;
    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_TexturePaletteAnimate, args, 4u, key, 2u);
    palette = v9x_cif_palette_from(handle);
    if (palette == 0 || start >= V9X_CIF_PALETTE_ENTRIES ||
        count > V9X_CIF_PALETTE_ENTRIES - start ||
        IsBadReadPtr((const void *)entries, count * 4ul)) {
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_BADPARAM;
    }
    for (index = 0ul; index < count; ++index) {
        palette->entries[start + index] = ((const v9x_u32 *)entries)[index];
    }
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/* ---- contexts --------------------------------------------------------- */

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

    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_ContextCreate, 0, 0u, 0, 0u);
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
    v9x_cif_state_init(&context->state);
    context->live = 1ul;
    LeaveCriticalSection(&v9x_cif_lock);
    return (v9x_u32)context;
}

v9x_u32 __stdcall ATI3DCIF_ContextDestroy(v9x_u32 handle);
v9x_u32 __stdcall ATI3DCIF_ContextDestroy(v9x_u32 handle)
{
    struct v9x_cif_context *context;
    v9x_u32 zero = 0ul;

    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_ContextDestroy, &handle, 1u, &zero, 1u);
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

/* ContextSetState stores the value (cif_state.c); the Knight demos call it
 * thousands of times a frame, so nothing is mapped here. */
v9x_u32 __stdcall ATI3DCIF_ContextSetState(v9x_u32 handle, v9x_u32 id,
                                           v9x_u32 data);
v9x_u32 __stdcall ATI3DCIF_ContextSetState(v9x_u32 handle, v9x_u32 id,
                                           v9x_u32 data)
{
    struct v9x_cif_context *context;
    v9x_u32 args[3];
    v9x_u32 key[2];
    v9x_u32 result;

    args[0] = handle;
    args[1] = id;
    args[2] = data;
    EnterCriticalSection(&v9x_cif_lock);
    key[0] = id;
    key[1] = id < V9X_C3D_ERS_NUM ? v9x_cif_peek(data) : 0ul;
    v9x_cif_call(V9X_CIF_IX_ContextSetState, args, 3u, key, 2u);
    context = v9x_cif_context_from(handle);
    if (context == 0 || data == 0ul ||
        IsBadReadPtr((const void *)data,
                     (id == V9X_C3D_ERS_SURF_VPORT ||
                      id == V9X_C3D_ERS_SURF_SCISSOR) ? 16u : 4u)) {
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_BADPARAM;
    }
    result = v9x_cif_state_set(&context->state, id, (const void *)data);
    LeaveCriticalSection(&v9x_cif_lock);
    return result;
}

/* ---- rendering --------------------------------------------------------- */

/* The display as the default draw surface, read once a 3D block. */
static void v9x_cif_read_display(void)
{
    if (!v9x_cif_device_display(&v9x_cif_display_width,
                                &v9x_cif_display_height,
                                &v9x_cif_display_pitch,
                                &v9x_cif_display_format)) {
        v9x_cif_display_width = 0ul;
        v9x_cif_display_height = 0ul;
        v9x_cif_display_pitch = 0ul;
        v9x_cif_display_format = 0ul;
    }
}

v9x_u32 __stdcall ATI3DCIF_RenderBegin(v9x_u32 handle);
v9x_u32 __stdcall ATI3DCIF_RenderBegin(v9x_u32 handle)
{
    struct v9x_cif_context *context;
    v9x_u32 zero = 0ul;

    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_RenderBegin, &handle, 1u, &zero, 1u);
    context = v9x_cif_context_from(handle);
    if (context == 0) {
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_BADPARAM;
    }
    v9x_cif_current = context;
    v9x_cif_read_display();
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/*
 * RenderEnd waits for the engine. The guide's RenderEnd hands the chip
 * back to 2D - the application blits, locks and flips only after it - so
 * everything drawn must be in its surfaces by then. Waiting here is what
 * makes the application's own Lock and Flip correct without this DLL
 * seeing them.
 */
v9x_u32 __stdcall ATI3DCIF_RenderEnd(void);
v9x_u32 __stdcall ATI3DCIF_RenderEnd(void)
{
    const V9X_R3D_INTERFACE *iface;
    const V9X_R3D_ABI_DESCRIBE *description;
    char text[64];

    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_RenderEnd, 0, 0u, 0, 0u);
    iface = v9x_cif_device_interface();
    description = v9x_cif_device_description();
    if (iface != 0 && description != 0) {
        (void)iface->finish(description->generation);
    }
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

    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_RenderSwitch, &handle, 1u, &zero, 1u);
    context = v9x_cif_context_from(handle);
    if (context == 0) {
        LeaveCriticalSection(&v9x_cif_lock);
        return V9X_C3D_EC_BADPARAM;
    }
    v9x_cif_current = context;
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/* What one call's triangles are drawn into and with. */
typedef struct v9x_cif_frame_setup {
    V9X_CIF_DRAW_SETUP setup;
    V9X_CIF_VERTEX_SETUP vertex;
    V9X_R3D_ABI_RAW target;
    V9X_R3D_ABI_RAW depth;
    V9X_R3D_ABI_TEXTURE texture;
} V9X_CIF_FRAME_SETUP;

/*
 * The draw target from the context: SURF_DRAW_PTR, with the pitch (in
 * pixels, as the Knight demos set 640 for a 640-wide surface) and format
 * from the context where set and from the display otherwise ("Desktop
 * Surface"). Its size is the display's, narrowed to the pitch: CIF names no
 * size, and every census demo drew a full-screen chain. The Z buffer is
 * named only while Z is on; its pitch is in pixels too.
 */
static unsigned int v9x_cif_target(const struct v9x_cif_context *context,
                                   V9X_CIF_FRAME_SETUP *frame)
{
    const v9x_u32 *value = context->state.value;
    v9x_u32 pitch = value[V9X_C3D_ERS_SURF_DRAW_PITCH] * 2ul;
    v9x_u32 format = v9x_cif_display_format;
    v9x_u32 width = v9x_cif_display_width;

    if (value[V9X_C3D_ERS_SURF_DRAW_PTR] == 0ul) {
        return V9X_CIF_SKIP_NO_TARGET;
    }
    if (pitch == 0ul) {
        pitch = v9x_cif_display_pitch;
    }
    if (value[V9X_C3D_ERS_SURF_DRAW_PF] == V9X_C3D_EPF_RGB565) {
        format = V9X_R3D_ABI_FORMAT_RGB565;
    } else if (value[V9X_C3D_ERS_SURF_DRAW_PF] == V9X_C3D_EPF_RGB1555) {
        format = V9X_R3D_ABI_FORMAT_XRGB1555;
    } else if (value[V9X_C3D_ERS_SURF_DRAW_PF] != 0ul) {
        format = 0ul;
    }
    if (format == 0ul || pitch == 0ul) {
        return V9X_CIF_SKIP_TARGET_FORMAT;
    }
    if (width > pitch / 2ul) {
        width = pitch / 2ul;
    }
    frame->target.address = (void *)value[V9X_C3D_ERS_SURF_DRAW_PTR];
    frame->target.pitch = pitch;
    frame->target.width = width;
    frame->target.height = v9x_cif_display_height;
    frame->target.format = format;
    frame->depth.address = 0;
    frame->depth.pitch = 0ul;
    frame->depth.width = 0ul;
    frame->depth.height = 0ul;
    frame->depth.format = 0ul;
    if (value[V9X_C3D_ERS_Z_MODE] != V9X_C3D_EZMODE_OFF &&
        value[V9X_C3D_ERS_SURF_Z_PTR] != 0ul) {
        frame->depth.address = (void *)value[V9X_C3D_ERS_SURF_Z_PTR];
        frame->depth.pitch = value[V9X_C3D_ERS_SURF_Z_PITCH] != 0ul
            ? value[V9X_C3D_ERS_SURF_Z_PITCH] * 2ul : pitch;
        frame->depth.width = width;
        frame->depth.height = frame->target.height;
    }
    return V9X_CIF_SKIPS;
}

/*
 * The whole setup for a draw: target, state mapped against it, the
 * selected texture converted, and the vertex setup. V9X_CIF_SKIPS when it
 * can be drawn, else the reason it cannot.
 */
static unsigned int v9x_cif_frame_setup(const struct v9x_cif_context *context,
                                        V9X_CIF_FRAME_SETUP *frame)
{
    struct v9x_cif_texture *texture;
    V9X_CIF_TEXTURE_INFO info;
    unsigned int why;
    unsigned int which;

    if (!v9x_cif_device_ready) {
        return V9X_CIF_SKIP_NO_DEVICE;
    }
    if (context->state.value[V9X_C3D_ERS_VERTEX_TYPE] != V9X_C3D_EV_VTCF) {
        return V9X_CIF_SKIP_VERTEX_TYPE;
    }
    why = v9x_cif_target(context, frame);
    if (why != V9X_CIF_SKIPS) {
        return why;
    }
    texture = v9x_cif_texture_from(
        context->state.value[V9X_C3D_ERS_TMAP_SELECT]);
    /* One level is uploaded, so level selection is off whatever the
     * mipmap flag says (the file comment's "Not yet"). */
    info.mipmapped = 0ul;
    info.clamp_s = texture != 0 ? texture->clamp_s : 0ul;
    info.clamp_t = texture != 0 ? texture->clamp_t : 0ul;
    v9x_cif_state_map(&context->state, frame->target.width,
                      frame->target.height, texture != 0 ? &info : 0,
                      &frame->setup);
    if (!frame->setup.recognized &&
        v9x_cif_approximations_logged < V9X_CIF_NOTES_MAX) {
        ++v9x_cif_approximations_logged;
        v9x_cif_log3("state drawn approximately: texop=%lu composite=%lu "
                     "fog/blend=%lu",
                     context->state.value[V9X_C3D_ERS_TMAP_TEXOP],
                     context->state.value[V9X_C3D_ERS_COMPOSITE_EN],
                     (context->state.value[V9X_C3D_ERS_FOG_EN] << 8) |
                         context->state.value[V9X_C3D_ERS_ALPHA_SRC]);
    }

    frame->texture.storage = V9X_R3D_ABI_TEXTURE_NONE;
    if (frame->setup.textured) {
        which = frame->setup.key_texture ? V9X_CIF_VARIANT_KEYED
              : frame->setup.texel_alpha ? V9X_CIF_VARIANT_ALPHA
                                         : V9X_CIF_VARIANT_OPAQUE;
        if (texture == 0 || !v9x_cif_texture_ready(texture, which)) {
            /* Drawn untextured rather than not at all: the shading and
             * depth still land, and the log says which texture. */
            v9x_cif_skip(V9X_CIF_SKIP_TEXTURE,
                         texture != 0 ? (texture->format << 16) |
                                            texture->width
                                      : 0ul);
            frame->setup.textured = 0ul;
            frame->setup.state.alpha_test_enable = 0ul;
        } else {
            frame->texture.storage = V9X_R3D_ABI_TEXTURE_HW;
            frame->texture.format = texture->variant[which].abi_format;
            frame->texture.surface.surface = texture->variant[which].surface;
            frame->texture.levels = 0;
            frame->texture.level_count = 0ul;
            frame->texture.min_filter = frame->setup.min_filter;
            frame->texture.mag_filter = frame->setup.mag_filter;
            frame->texture.mip = V9X_R3D_ABI_MIP_NONE;
            frame->texture.address = frame->setup.address;
            frame->texture.color_op = frame->setup.color_op;
            frame->texture.alpha_op = frame->setup.alpha_op;
            frame->texture.env_color = 0ul;
        }
    }
    frame->vertex.origin_x = frame->setup.origin_x;
    frame->vertex.origin_y = frame->setup.origin_y;
    frame->vertex.shade = frame->setup.shade;
    frame->vertex.solid_argb = frame->setup.solid_argb;
    frame->vertex.fog = frame->setup.fog;
    frame->vertex.white = frame->setup.textured ? frame->setup.white : 0ul;
    return V9X_CIF_SKIPS;
}

static void v9x_cif_zero(void *block, DWORD bytes)
{
    DWORD i;

    for (i = 0ul; i < bytes; ++i) {
        ((BYTE *)block)[i] = 0u;
    }
}

/* One batch to the render interface: retried after a mode change (STALE)
 * and without fog when an engine refuses fog, as Glide's is. */
static void v9x_cif_submit(const V9X_CIF_FRAME_SETUP *frame,
                           v9x_u32 triangles)
{
    const V9X_R3D_INTERFACE *iface = v9x_cif_device_interface();
    const V9X_R3D_ABI_DESCRIBE *description;
    V9X_R3D_ABI_DRAW draw;
    V9X_R3D_ABI_OUTCOME outcome;
    v9x_u32 result = V9X_R3D_RESULT_NOT_READY;
    unsigned int attempt;

    if (iface == 0 || triangles == 0ul) {
        return;
    }
    v9x_cif_zero(&draw, sizeof(draw));
    draw.struct_bytes = sizeof(draw);
    draw.raw_target = frame->target;
    draw.raw_depth = frame->depth;
    draw.texture = frame->texture;
    draw.texture1.storage = V9X_R3D_ABI_TEXTURE_NONE;
    draw.state = frame->setup.state;
    draw.vertices = v9x_cif_batch;
    draw.triangle_count = triangles;
    /* Texel alpha times an alpha of 255 is the texel's: D3D MODULATE on
     * a texture with alpha, which the Rage IIC blends by, where the
     * product it cannot (Pixie's sprites, A8U4I5 boot 456). */
    if (draw.texture.storage == V9X_R3D_ABI_TEXTURE_HW &&
        draw.texture.alpha_op == V9X_R3D_ABI_ALPHAOP_MODULATE &&
        draw.texture.format != V9X_R3D_ABI_FORMAT_RGB565 &&
        v9x_cif_vertices_opaque(v9x_cif_batch, triangles)) {
        draw.texture.alpha_op = V9X_R3D_ABI_ALPHAOP_REPLACE;
    }
    for (attempt = 0u; attempt < 3u; ++attempt) {
        description = v9x_cif_device_description();
        draw.generation = description != 0 ? description->generation : 0ul;
        v9x_cif_zero(&outcome, sizeof(outcome));
        result = iface->draw(&draw, &outcome);
        if (result == V9X_R3D_RESULT_STALE && v9x_cif_device_redescribe()) {
            continue;
        }
        if (result == V9X_R3D_RESULT_UNSUPPORTED && draw.state.fog_enable) {
            draw.state.fog_enable = 0ul;
            continue;
        }
        break;
    }
    if (result == V9X_R3D_RESULT_OK || result == V9X_R3D_RESULT_PARTIAL) {
        v9x_cif_triangles_drawn += outcome.submitted;
        return;
    }
    v9x_cif_triangles_refused += triangles;
    if (v9x_cif_refusals_logged < V9X_CIF_NOTES_MAX) {
        ++v9x_cif_refusals_logged;
        v9x_cif_log3("draw refused result=%lu target=%08lX pitch=%lu",
                     result, (v9x_u32)frame->target.address,
                     frame->target.pitch);
        v9x_cif_log3("  format=%lu depth=%08lX texture=%lu",
                     frame->target.format, (v9x_u32)frame->depth.address,
                     frame->texture.storage);
        v9x_cif_log3("  blend=%lu src=%lu dst=%lu", draw.state.blend_enable,
                     draw.state.src_blend, draw.state.dst_blend);
        v9x_cif_log3("  alphatest=%lu fog=%lu texformat=%lu",
                     draw.state.alpha_test_enable, draw.state.fog_enable,
                     frame->texture.format);
        v9x_cif_log3("  ops color=%lu alpha=%lu filters=%08lX",
                     frame->texture.color_op, frame->texture.alpha_op,
                     (frame->texture.min_filter << 16) |
                         (frame->texture.mag_filter << 8) |
                         frame->texture.address);
    }
}

/*
 * In a captured frame's first draws, the target's centre pixel read back
 * through the application's own pointer, after the engine has finished:
 * whether the triangles reach the memory the application flips.
 */
#define V9X_CIF_PIXEL_PROBES 24ul
static v9x_u32 v9x_cif_pixel_probes;

static void v9x_cif_probe_pixel(const char *when,
                                const V9X_CIF_FRAME_SETUP *frame)
{
    const V9X_R3D_INTERFACE *iface = v9x_cif_device_interface();
    const V9X_R3D_ABI_DESCRIBE *description = v9x_cif_device_description();
    v9x_u32 address;
    v9x_u32 finish = 0xfffffffful;

    if (!v9x_cif_capturing || v9x_cif_pixel_probes >= V9X_CIF_PIXEL_PROBES ||
        iface == 0 || description == 0) {
        return;
    }
    ++v9x_cif_pixel_probes;
    finish = iface->finish(description->generation);
    address = (v9x_u32)frame->target.address +
              (frame->target.height / 2ul) * frame->target.pitch +
              (frame->target.width / 2ul) * 2ul;
    v9x_cif_log3(when[0] == 'b' ? "pixel before draw: %04lX finish=%lu at %08lX"
                                : "pixel after draw: %04lX finish=%lu at %08lX",
                 IsBadReadPtr((const void *)address, 2u)
                     ? 0xfffful : (v9x_u32)*(const v9x_u16 *)address,
                 finish, address);
}

/*
 * Draws one call's primitives: each triangle's vertices converted, flat
 * shading applied per triangle, in batches of the interface's maximum.
 * `vertex_of` gives a vertex's address from its index in the call.
 */
typedef v9x_u32 (*V9X_CIF_VERTEX_AT)(v9x_u32 base, v9x_u32 index);

static v9x_u32 v9x_cif_strip_vertex(v9x_u32 base, v9x_u32 index)
{
    return base + index * V9X_C3D_VTCF_FLOATS * sizeof(float);
}

static v9x_u32 v9x_cif_list_vertex(v9x_u32 base, v9x_u32 index)
{
    return v9x_cif_peek(base + index * 4ul);
}

static void v9x_cif_draw(v9x_u32 form, v9x_u32 base, v9x_u32 count,
                         V9X_CIF_VERTEX_AT vertex_at)
{
    V9X_CIF_FRAME_SETUP frame;
    const struct v9x_cif_context *context = v9x_cif_current;
    v9x_u32 prim;
    v9x_u32 triangles;
    v9x_u32 triangle;
    v9x_u32 corner;
    v9x_u32 pending = 0ul;
    v9x_u32 address;
    unsigned int why;

    if (context == 0) {
        v9x_cif_skip(V9X_CIF_SKIP_NO_CONTEXT, count);
        return;
    }
    prim = context->state.value[V9X_C3D_ERS_PRIM_TYPE];
    triangles = v9x_cif_prim_triangles(prim, form, count);
    if (triangles == 0ul) {
        if (count != 0ul) {
            v9x_cif_skip(V9X_CIF_SKIP_PRIMITIVE, (prim << 16) | form);
        }
        return;
    }
    why = v9x_cif_frame_setup(context, &frame);
    if (why != V9X_CIF_SKIPS) {
        v9x_cif_skip(why, context->state.value[V9X_C3D_ERS_SURF_DRAW_PTR]);
        return;
    }
    v9x_cif_probe_pixel("before", &frame);
    for (triangle = 0ul; triangle < triangles; ++triangle) {
        V9X_R3D_ABI_VERTEX *out = &v9x_cif_batch[pending * 3ul];

        for (corner = 0ul; corner < 3ul; ++corner) {
            address = vertex_at(base, v9x_cif_prim_vertex(prim, form,
                                                          triangle, corner));
            if (address == 0ul ||
                IsBadReadPtr((const void *)address,
                             V9X_C3D_VTCF_FLOATS * sizeof(float))) {
                v9x_cif_skip(V9X_CIF_SKIP_VERTEX_TYPE, address);
                return;
            }
            v9x_cif_vtcf_convert(&frame.vertex, (const float *)address,
                                 &out[corner]);
        }
        if (frame.vertex.shade == V9X_C3D_ESH_FLAT) {
            v9x_cif_triangle_flat(out);
        }
        if (++pending == V9X_R3D_ABI_BATCH_MAX) {
            v9x_cif_submit(&frame, pending);
            pending = 0ul;
        }
    }
    v9x_cif_submit(&frame, pending);
    v9x_cif_probe_pixel("after", &frame);
}

v9x_u32 __stdcall ATI3DCIF_RenderPrimStrip(v9x_u32 strip, v9x_u32 count);
v9x_u32 __stdcall ATI3DCIF_RenderPrimStrip(v9x_u32 strip, v9x_u32 count)
{
    v9x_u32 args[2];
    v9x_u32 key[2];

    args[0] = strip;
    args[1] = count;
    EnterCriticalSection(&v9x_cif_lock);
    key[0] = count;
    key[1] = v9x_cif_current != 0
        ? v9x_cif_current->state.value[V9X_C3D_ERS_PRIM_TYPE] : 0xfffful;
    v9x_cif_call(V9X_CIF_IX_RenderPrimStrip, args, 2u, key, 2u);
    v9x_cif_draw(V9X_CIF_FORM_STRIP, strip, count, v9x_cif_strip_vertex);
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

v9x_u32 __stdcall ATI3DCIF_RenderPrimList(v9x_u32 list, v9x_u32 count);
v9x_u32 __stdcall ATI3DCIF_RenderPrimList(v9x_u32 list, v9x_u32 count)
{
    v9x_u32 args[2];
    v9x_u32 key[2];

    args[0] = list;
    args[1] = count;
    EnterCriticalSection(&v9x_cif_lock);
    key[0] = count;
    key[1] = v9x_cif_current != 0
        ? v9x_cif_current->state.value[V9X_C3D_ERS_PRIM_TYPE] : 0xfffful;
    v9x_cif_call(V9X_CIF_IX_RenderPrimList, args, 2u, key, 2u);
    if (list != 0ul && !IsBadReadPtr((const void *)list, count * 4ul)) {
        v9x_cif_draw(V9X_CIF_FORM_LIST, list, count, v9x_cif_list_vertex);
    }
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/* No census demo draws meshes; counted and logged, nothing drawn yet. */
v9x_u32 __stdcall ATI3DCIF_RenderPrimMesh(v9x_u32 vertices, v9x_u32 indices,
                                          v9x_u32 count);
v9x_u32 __stdcall ATI3DCIF_RenderPrimMesh(v9x_u32 vertices, v9x_u32 indices,
                                          v9x_u32 count)
{
    v9x_u32 args[3];

    args[0] = vertices;
    args[1] = indices;
    args[2] = count;
    EnterCriticalSection(&v9x_cif_lock);
    v9x_cif_call(V9X_CIF_IX_RenderPrimMesh, args, 3u, &count, 1u);
    v9x_cif_skip(V9X_CIF_SKIP_PRIMITIVE, 0x4d455348ul);
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
    v9x_cif_call(V9X_CIF_IX_RenderIndexedPrim, args, 3u, &a2, 1u);
    LeaveCriticalSection(&v9x_cif_lock);
    return V9X_C3D_EC_OK;
}

/* ---- the _lib pointers ------------------------------------------------ */

/*
 * ATI3DCIF.H declares "extern function_name##_t function_name##_lib":
 * each is a variable holding the function's address, exported by its
 * undecorated name. The pragmas give each variable exactly that symbol, so
 * scripts\build-cif.ps1 can export it as it stands. These 19 variables
 * and the 19 functions above are the DLL's exports, ATI's own list; the
 * log functions cif_surface.c calls and the entry point are the only
 * other external symbols.
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

/* ---- the DLL -------------------------------------------------------------- */

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
        wsprintfA(text, "attach build=%s process=%s", V9X_BUILD_ID, path);
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
