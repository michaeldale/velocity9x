/*
 * SiS 6326 Direct3D engine.
 *
 * Phase 4 of docs\plans\sis-6326-hardware-3d.md. Everything this engine
 * accepts or emits was measured by the SIS3D probe phases on A8U4I5
 * (docs\decisions\2026-10-05-sis6326-3d-*.md): the register words come from
 * sis6326_3d.c and the translation from the neutral draw from
 * d3d_sis6326_map.c, both host-tested. This file resolves DirectDraw
 * surfaces, places textures, and emits.
 *
 * Emission order is the refusal contract: the mapping, the state and
 * texture words and every triangle's vertex words are built first, and
 * only then is the first register written. A refused batch writes nothing.
 * A timeout during emission is the one way a batch can end part-way.
 *
 * Synchronous, as the probe ran: the Turbo Queue is off, the engine is
 * idle before each triangle, and a batch ends with the 3D engine idle, so
 * the 2D engine's own wait (eng_sis6326.c, 82A8h) never meets 3D work in
 * flight. Whether 82A8h covers the 3D engine is not measured.
 */
#include "d3d_internal.h"
#include "d3d_sis6326_map.h"

/* Bounded waits, matching eng_sis6326.c's. */
#define V9X_D3D_SIS_SPINS 0x00200000ul

/* The core hands no batch over V9X_D3D_INDEXED_BATCH triangles. */
#define V9X_D3D_SIS_MAX_TRIANGLES 64ul

/* The 64 KiB BAR1 window eng_sis6326.c requires. */
#define V9X_D3D_SIS_MMIO_BYTES 0x00010000ul

/*
 * Level bases are byte addresses (datasheet 8A44h); every base the texture
 * probe sampled sat on 256 bytes, so textures are placed there.
 */
#define V9X_D3D_SIS_TEXTURE_ALIGN 256ul
/* The 4-byte unit of the pitch field: a narrower row is padded to it. */
#define V9X_D3D_SIS_PITCH_UNIT 4ul

static struct v9x_sis3d_writes v9x_d3d_sis_state_writes;
static struct v9x_sis3d_writes v9x_d3d_sis_clear_writes;
static struct v9x_sis3d_writes v9x_d3d_sis_texture_writes;
static struct v9x_sis3d_writes v9x_d3d_sis_vertex_writes[V9X_D3D_SIS_MAX_TRIANGLES];
static v9x_u32 v9x_d3d_sis_primitives[V9X_D3D_SIS_MAX_TRIANGLES];

/*
 * The destination pitch field holds 14 bits and the clip fields 12 integer
 * bits; textures are powers of two from 1 to 512; the core clips, so the
 * setup engine sees only on-target coordinates. Z16.
 */
static const V9X_D3D_ENGINE_LIMITS v9x_d3d_sis_limits = {
    16ul,           /* target_bits_per_pixel */
    16380ul,        /* target_pitch_max */
    4ul,            /* target_pitch_align */
    2048ul,         /* target_dimension_max */
    1ul,            /* texture_size_min */
    512ul,          /* texture_size_max */
    2048.0f,        /* coordinate_limit */
    16ul,           /* depth_bits_per_pixel */
    V9X_D3D_SIS_TEXTURE_ALIGN, /* texture_align */
    1ul,            /* clip_in_core */
    0ul,            /* depth_pitch_own */
    /* Z16 holds z x 2^15 and the test compares the low 15 bits: fills of
     * 8000h and ABCDh failed LESS at z 0.5, FFFFh and 7FFFh passed, C000h
     * passed on the pixels written 3FFFh (SIS3D /phase4z, boot 228). */
    1ul             /* depth_fill_shift */
};

static void v9x_d3d_sis_write(v9x_u32 offset, v9x_u32 value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) =
        value;
}

/*
 * TEND after every triangle. Without it Final Reality's first Z-tested batch
 * hung the engine at one triangle or another however its state, texture,
 * Z contents, sequencer setup or Turbo Queue were changed; with it the
 * replayed batch drew all 64 triangles (SIS3D /phase6 /file, A8U4I5 boot
 * 287; issue 2026-10-05-a8u4i5-sis-3d-stalls-in-final-reality.md).
 */
static void v9x_d3d_sis_end_primitive(void)
{
    *(volatile unsigned char *)(v9x_hal->engine.control_linear_base +
                                V9X_SIS3D_TEND) = 0u;
}

static DWORD v9x_d3d_sis_read(v9x_u32 offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base +
                               offset);
}

/*
 * Set when the 3D engine fails to go idle; no 3D register is touched again
 * this boot. A stalled engine stays stalled until a reboot (textures
 * record), and on A8U4I5 boots 212 and 215 one froze the machine: the 2D
 * engine, behind it in the shared queue, never went idle, every Lock
 * answered WASSTILLDRAWING, and DDLOCK_WAIT retried for ever (engine
 * record, 2026-10-05).
 */
static int v9x_d3d_sis_quarantined = 0;

/* "S3ID": the 3D idle timeout, in V9XTRACE.INI's FaultCode. */
#define V9X_D3D_SIS_FAULT_3D_IDLE 0x53334944ul

/*
 * The last two batches' register streams, for the first timeout's record.
 * V9XDDP on A8U4I5 boot 217 stalled the engine (89FCh 00200074h) right after
 * its first textured batch, while the SIS3D probe replaying those words did
 * not; this records what the driver actually wrote.
 */
#define V9X_D3D_SIS_LOG_PATH "C:\\V9XDIAG\\V9XSIS3D.TXT"
/* The current batch's texture level 0 and Z buffer, as VRAM held them at
 * the timeout: the probe's replays of Final Reality's stall filled both
 * with one value, and a buffer every pixel fails went idle (boot 280). */
#define V9X_D3D_SIS_TEXTURE_PATH "C:\\V9XDIAG\\V9XSIS3T.BIN"
#define V9X_D3D_SIS_DEPTH_PATH   "C:\\V9XDIAG\\V9XSIS3Z.BIN"
#define V9X_D3D_SIS_WAIT_BEFORE_STATE    1ul
#define V9X_D3D_SIS_WAIT_BEFORE_TRIANGLE 2ul
#define V9X_D3D_SIS_WAIT_AFTER_BATCH     3ul

typedef struct v9x_d3d_sis_batch_log {
    DWORD number;
    DWORD textured;
    DWORD triangles;
    v9x_u32 primitive;
    DWORD texture_offset;
    DWORD texture_bytes;
    DWORD depth_offset;
    DWORD depth_bytes;
    struct v9x_sis3d_writes state;
    struct v9x_sis3d_writes clear;
    struct v9x_sis3d_writes texture;
    struct v9x_sis3d_writes vertex;
} V9X_D3D_SIS_BATCH_LOG;

static V9X_D3D_SIS_BATCH_LOG v9x_d3d_sis_log[2];
static DWORD v9x_d3d_sis_batches = 0ul;
static DWORD v9x_d3d_sis_wait_site = 0ul;
static DWORD v9x_d3d_sis_wait_triangle = 0ul;

static void v9x_d3d_sis_log_text(HANDLE file, const char *text)
{
    DWORD length = 0ul;
    DWORD written;

    while (text[length] != '\0') {
        ++length;
    }
    WriteFile(file, text, length, &written, 0);
}

static void v9x_d3d_sis_log_hex(HANDLE file, const char *key, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char line[64];
    int at = 0;
    int shift;

    while (*key != '\0' && at < 40) {
        line[at++] = *key++;
    }
    line[at++] = '=';
    for (shift = 28; shift >= 0; shift -= 4) {
        line[at++] = digits[(value >> shift) & 0xful];
    }
    line[at++] = '\r';
    line[at++] = '\n';
    line[at] = '\0';
    v9x_d3d_sis_log_text(file, line);
}

static void v9x_d3d_sis_log_writes(HANDLE file,
                                   const struct v9x_sis3d_writes *writes)
{
    v9x_u32 index;

    for (index = 0u; index < writes->count; ++index) {
        v9x_d3d_sis_log_hex(file, "  Offset", writes->offsets[index]);
        v9x_d3d_sis_log_hex(file, "  Value", writes->values[index]);
    }
}

static void v9x_d3d_sis_log_batch(HANDLE file, const char *name,
                                  const V9X_D3D_SIS_BATCH_LOG *batch)
{
    v9x_d3d_sis_log_text(file, name);
    v9x_d3d_sis_log_text(file, "\r\n");
    v9x_d3d_sis_log_hex(file, "Number", batch->number);
    v9x_d3d_sis_log_hex(file, "Textured", batch->textured);
    v9x_d3d_sis_log_hex(file, "Triangles", batch->triangles);
    v9x_d3d_sis_log_hex(file, "Primitive0", batch->primitive);
    v9x_d3d_sis_log_hex(file, "TextureOffset", batch->texture_offset);
    v9x_d3d_sis_log_hex(file, "TextureBytes", batch->texture_bytes);
    v9x_d3d_sis_log_hex(file, "DepthOffset", batch->depth_offset);
    v9x_d3d_sis_log_hex(file, "DepthBytes", batch->depth_bytes);
    v9x_d3d_sis_log_text(file, "State\r\n");
    v9x_d3d_sis_log_writes(file, &batch->state);
    v9x_d3d_sis_log_text(file, "TextureClear\r\n");
    v9x_d3d_sis_log_writes(file, &batch->clear);
    v9x_d3d_sis_log_text(file, "Texture\r\n");
    v9x_d3d_sis_log_writes(file, &batch->texture);
    v9x_d3d_sis_log_text(file, "Vertices0\r\n");
    v9x_d3d_sis_log_writes(file, &batch->vertex);
}

/* `bytes` of VRAM from `offset` to `path`, when both lie in the aperture. */
static void v9x_d3d_sis_dump_vram(const char *path, DWORD offset,
                                  DWORD bytes)
{
    HANDLE file;
    DWORD written;

    if (bytes == 0ul || offset >= v9x_hal->fb.vram_bytes ||
        bytes > v9x_hal->fb.vram_bytes - offset) {
        return;
    }
    file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    WriteFile(file, (const void *)(v9x_hal->fb.linear_base + offset), bytes,
              &written, 0);
    CloseHandle(file);
}

/* Fixed storage and KERNEL32 file I/O only, as the fault trace. */
static void v9x_d3d_sis_log_timeout(DWORD status)
{
    HANDLE file;
    DWORD current = (v9x_d3d_sis_batches - 1ul) & 1ul;
    DWORD index;

    file = CreateFileA(V9X_D3D_SIS_LOG_PATH, GENERIC_WRITE,
                       FILE_SHARE_READ, 0, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    v9x_d3d_sis_log_text(file, "[Sis3dTimeout]\r\n");
    v9x_d3d_sis_log_hex(file, "Status", status);
    v9x_d3d_sis_log_hex(file, "WaitSite", v9x_d3d_sis_wait_site);
    v9x_d3d_sis_log_hex(file, "WaitTriangle", v9x_d3d_sis_wait_triangle);
    v9x_d3d_sis_log_hex(file, "Batches", v9x_d3d_sis_batches);
    v9x_d3d_sis_log_batch(file, "[Current]", &v9x_d3d_sis_log[current]);
    if (v9x_d3d_sis_batches > 1ul) {
        v9x_d3d_sis_log_batch(file, "[Previous]",
                              &v9x_d3d_sis_log[current ^ 1ul]);
    }
    /* Every triangle of the current batch, which is still in the build
     * buffers: Final Reality's second stall (boot 256) came at triangle 9
     * of 64, where Vertices0 says nothing. */
    v9x_d3d_sis_log_text(file, "[CurrentTriangles]\r\n");
    for (index = 0ul; index < v9x_d3d_sis_log[current].triangles; ++index) {
        v9x_d3d_sis_log_hex(file, "Triangle", index);
        v9x_d3d_sis_log_hex(file, "Primitive", v9x_d3d_sis_primitives[index]);
        v9x_d3d_sis_log_writes(file, &v9x_d3d_sis_vertex_writes[index]);
    }
    CloseHandle(file);
    v9x_d3d_sis_dump_vram(V9X_D3D_SIS_TEXTURE_PATH,
                          v9x_d3d_sis_log[current].texture_offset,
                          v9x_d3d_sis_log[current].texture_bytes);
    v9x_d3d_sis_dump_vram(V9X_D3D_SIS_DEPTH_PATH,
                          v9x_d3d_sis_log[current].depth_offset,
                          v9x_d3d_sis_log[current].depth_bytes);
}

/*
 * 89FCh D1: the 3D engine idle and its queue empty.
 *
 * On a timeout the record is written through to disk first: the last two
 * batches to V9XSIS3D.TXT, then the fault trace with the last status in
 * FaultAddress. Then the 3D engine is quarantined and the engine descriptor
 * invalidated, which stops the 2D engine too (eng_sis6326.c requires it
 * valid), so the 2D engine is never fed behind a stalled 3D one and Lock's
 * drain completes. Until the next mode set DirectDraw falls back to the
 * CPU.
 */
static int v9x_d3d_sis_wait_idle(void)
{
    DWORD spins;
    DWORD status = 0ul;

    for (spins = 0ul; spins < V9X_D3D_SIS_SPINS; ++spins) {
        status = v9x_d3d_sis_read(V9X_SIS3D_STATUS);
        if ((status & V9X_SIS3D_STATUS_IDLE_EMPTY) != 0ul) {
            return 1;
        }
    }
    /* The first timeout of the boot only, as eng_sis6326.c does. */
    if (v9x_hal->engine.idle_timeouts++ == 0ul) {
        v9x_d3d_sis_log_timeout(status);
        v9x_trace_flush_fault(V9X_D3D_SIS_FAULT_3D_IDLE, status);
    }
    v9x_d3d_sis_quarantined = 1;
    v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
    return 0;
}

static void v9x_d3d_sis_emit(const struct v9x_sis3d_writes *writes)
{
    v9x_u32 index;

    for (index = 0u; index < writes->count; ++index) {
        v9x_d3d_sis_write(writes->offsets[index], writes->values[index]);
    }
}

/* The five formats the texture probe measured exact, by their masks. */
static int v9x_d3d_sis_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                      DWORD *format_out)
{
    const V9X_DDPIXELFORMAT *format;

    if (format_out != 0) {
        *format_out = V9X_D3D_SIS_TEXTURE_UNKNOWN;
    }
    if (surface == 0 || surface->lpGbl == 0 || format_out == 0) {
        return 0;
    }
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul) {
        format = &surface->lpGbl->ddpfSurface;
    } else {
        format = &v9x_hal->info.vmiData.ddpfDisplay;
    }
    if ((format->dwFlags & V9X_DDPF_RGB) == 0ul) {
        return 0;
    }
    if (format->dwRGBBitCount == 32ul &&
        (format->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul &&
        format->dwRBitMask == 0x00ff0000ul &&
        format->dwGBitMask == 0x0000ff00ul &&
        format->dwBBitMask == 0x000000fful &&
        format->dwRGBAlphaBitMask == 0xff000000ul) {
        *format_out = V9X_SIS3D_TEXEL_ARGB8888;
        return 1;
    }
    if (format->dwRGBBitCount != 16ul) {
        return 0;
    }
    if ((format->dwFlags & V9X_DDPF_ALPHAPIXELS) == 0ul) {
        if (format->dwRBitMask == 0x0000f800ul &&
            format->dwGBitMask == 0x000007e0ul &&
            format->dwBBitMask == 0x0000001ful) {
            *format_out = V9X_SIS3D_TEXEL_RGB565;
            return 1;
        }
        if (format->dwRBitMask == 0x00007c00ul &&
            format->dwGBitMask == 0x000003e0ul &&
            format->dwBBitMask == 0x0000001ful) {
            *format_out = V9X_SIS3D_TEXEL_RGB555;
            return 1;
        }
        return 0;
    }
    if (format->dwRBitMask == 0x00007c00ul &&
        format->dwGBitMask == 0x000003e0ul &&
        format->dwBBitMask == 0x0000001ful &&
        format->dwRGBAlphaBitMask == 0x00008000ul) {
        *format_out = V9X_SIS3D_TEXEL_ARGB1555;
        return 1;
    }
    if (format->dwRBitMask == 0x00000f00ul &&
        format->dwGBitMask == 0x000000f0ul &&
        format->dwBBitMask == 0x0000000ful &&
        format->dwRGBAlphaBitMask == 0x0000f000ul) {
        *format_out = V9X_SIS3D_TEXEL_ARGB4444;
        return 1;
    }
    return 0;
}

static DWORD v9x_d3d_sis_texel_bytes(DWORD format)
{
    return format == V9X_SIS3D_TEXEL_ARGB8888 ? 4ul : 2ul;
}

/*
 * The measured boundary (the textures, shading-and-depth and phase 5
 * records). Colour keys were not measured and are refused per draw, as the
 * published caps say; so is everything d3d_sis6326_map.c refuses.
 */
static void v9x_d3d_sis_describe_caps(V9X_DD_SHARED *shared)
{
    V9X_D3DPRIMCAPS *tri;
    DWORD index;

    if (shared == 0) {
        return;
    }
    shared->d3d_global.dwSize = sizeof(V9X_D3DHAL_GLOBALDRIVERDATA);
    shared->d3d_global.hwCaps.dwSize = sizeof(V9X_D3DDEVICEDESC_V1);
    shared->d3d_global.hwCaps.dwFlags =
        V9X_D3DDD_COLORMODEL | V9X_D3DDD_DEVCAPS |
        V9X_D3DDD_TRICAPS | V9X_D3DDD_DEVICERENDERBITDEPTH |
        V9X_D3DDD_DEVICEZBUFFERBITDEPTH;
    shared->d3d_global.hwCaps.dcmColorModel = V9X_D3DCOLOR_RGB;
    shared->d3d_global.hwCaps.dwDevCaps =
        V9X_D3DDEVCAPS_FLOATTLVERTEX |
        V9X_D3DDEVCAPS_EXECUTESYSTEMMEMORY |
        V9X_D3DDEVCAPS_TLVERTEXSYSTEMMEMORY |
        V9X_D3DDEVCAPS_TEXTUREVIDEOMEMORY |
        V9X_D3DDEVCAPS_DRAWPRIMTLVERTEX;
    shared->d3d_global.hwCaps.dtcTransformCaps.dwSize =
        sizeof(V9X_D3DTRANSFORMCAPS);
    shared->d3d_global.hwCaps.dlcLightingCaps.dwSize =
        sizeof(V9X_D3DLIGHTINGCAPS);
    shared->d3d_global.hwCaps.dpcLineCaps.dwSize = sizeof(V9X_D3DPRIMCAPS);

    tri = &shared->d3d_global.hwCaps.dpcTriCaps;
    tri->dwSize = sizeof(V9X_D3DPRIMCAPS);
    tri->dwMiscCaps = V9X_D3DPMISCCAPS_CULLNONE | V9X_D3DPMISCCAPS_CULLCW |
                      V9X_D3DPMISCCAPS_CULLCCW;
    /* Ties to 1/65536 pixel and the integer sample point: phases 1-2. */
    tri->dwRasterCaps = V9X_D3DPRASTERCAPS_ZTEST |
                        V9X_D3DPRASTERCAPS_SUBPIXEL |
                        V9X_D3DPRASTERCAPS_FOGVERTEX;
    tri->dwZCmpCaps =
        V9X_D3DPCMPCAPS_NEVER | V9X_D3DPCMPCAPS_LESS |
        V9X_D3DPCMPCAPS_EQUAL | V9X_D3DPCMPCAPS_LESSEQUAL |
        V9X_D3DPCMPCAPS_GREATER | V9X_D3DPCMPCAPS_NOTEQUAL |
        V9X_D3DPCMPCAPS_GREATEREQUAL | V9X_D3DPCMPCAPS_ALWAYS;
    tri->dwAlphaCmpCaps = tri->dwZCmpCaps;
    /* The four factors phase 2 measured (d3d_sis6326_map.c). */
    tri->dwSrcBlendCaps =
        V9X_D3DPBLENDCAPS_ZERO | V9X_D3DPBLENDCAPS_ONE |
        V9X_D3DPBLENDCAPS_SRCALPHA | V9X_D3DPBLENDCAPS_INVSRCALPHA;
    tri->dwDestBlendCaps = tri->dwSrcBlendCaps;
    tri->dwShadeCaps =
        V9X_D3DPSHADECAPS_COLORFLATRGB | V9X_D3DPSHADECAPS_COLORGOURAUDRGB |
        V9X_D3DPSHADECAPS_ALPHAFLATBLEND |
        V9X_D3DPSHADECAPS_ALPHAGOURAUDBLEND |
        /* Vertex fog and specular, against Direct3D's formulas, textured
         * and not, specular before fog (SIS3D /phase5, boot 230). */
        V9X_D3DPSHADECAPS_FOGFLAT | V9X_D3DPSHADECAPS_FOGGOURAUD |
        V9X_D3DPSHADECAPS_SPECULARFLATRGB |
        V9X_D3DPSHADECAPS_SPECULARGOURAUDRGB;
    /* W is RHW; any power-of-two rectangle; texel alpha. */
    tri->dwTextureCaps = V9X_D3DPTEXTURECAPS_PERSPECTIVE |
                         V9X_D3DPTEXTURECAPS_POW2 |
                         V9X_D3DPTEXTURECAPS_ALPHA;
    tri->dwTextureFilterCaps = V9X_D3DPTFILTERCAPS_NEAREST |
                               V9X_D3DPTFILTERCAPS_LINEAR |
                               V9X_D3DPTFILTERCAPS_MIPNEAREST |
                               V9X_D3DPTFILTERCAPS_MIPLINEAR |
                               V9X_D3DPTFILTERCAPS_LINEARMIPNEAREST |
                               V9X_D3DPTFILTERCAPS_LINEARMIPLINEAR;
    tri->dwTextureBlendCaps = V9X_D3DPTBLENDCAPS_DECAL |
                              V9X_D3DPTBLENDCAPS_MODULATE |
                              V9X_D3DPTBLENDCAPS_DECALALPHA |
                              V9X_D3DPTBLENDCAPS_MODULATEALPHA |
                              V9X_D3DPTBLENDCAPS_DECALMASK |
                              V9X_D3DPTBLENDCAPS_MODULATEMASK |
                              V9X_D3DPTBLENDCAPS_COPY;
    tri->dwTextureAddressCaps = V9X_D3DPTADDRESSCAPS_WRAP |
                                V9X_D3DPTADDRESSCAPS_MIRROR |
                                V9X_D3DPTADDRESSCAPS_CLAMP;

    shared->d3d_extended_caps.dwSize = sizeof(V9X_D3DHAL_D3DEXTENDEDCAPS);
    shared->d3d_extended_caps.dwMinTextureWidth =
        v9x_d3d_sis_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureWidth =
        v9x_d3d_sis_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinTextureHeight =
        v9x_d3d_sis_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureHeight =
        v9x_d3d_sis_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMaxStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMinStippleHeight = 0ul;
    shared->d3d_extended_caps.dwMaxStippleHeight = 0ul;
    shared->d3d_global.hwCaps.dwDeviceRenderBitDepth = V9X_DDBD_16;
    shared->d3d_global.hwCaps.dwDeviceZBufferBitDepth = V9X_DDBD_16;

    /* RGB565, ARGB1555 and ARGB4444: the shared block has three slots.
     * RGB555 and ARGB8888 also sample exactly, unpublished. */
    for (index = 0ul; index < 3ul; ++index) {
        shared->texture_formats[index].dwSize = sizeof(V9X_DDSURFACEDESC);
        shared->texture_formats[index].dwFlags =
            V9X_DDSD_CAPS | V9X_DDSD_PIXELFORMAT;
        shared->texture_formats[index].ddpfPixelFormat.dwSize =
            sizeof(V9X_DDPIXELFORMAT);
        shared->texture_formats[index].ddpfPixelFormat.dwRGBBitCount = 16ul;
        shared->texture_formats[index].ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;
    }
    shared->texture_formats[0].ddpfPixelFormat.dwFlags = V9X_DDPF_RGB;
    shared->texture_formats[0].ddpfPixelFormat.dwRBitMask = 0x0000f800ul;
    shared->texture_formats[0].ddpfPixelFormat.dwGBitMask = 0x000007e0ul;
    shared->texture_formats[0].ddpfPixelFormat.dwBBitMask = 0x0000001ful;
    shared->texture_formats[0].ddpfPixelFormat.dwRGBAlphaBitMask = 0ul;
    shared->texture_formats[1].ddpfPixelFormat.dwFlags =
        V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
    shared->texture_formats[1].ddpfPixelFormat.dwRBitMask = 0x00007c00ul;
    shared->texture_formats[1].ddpfPixelFormat.dwGBitMask = 0x000003e0ul;
    shared->texture_formats[1].ddpfPixelFormat.dwBBitMask = 0x0000001ful;
    shared->texture_formats[1].ddpfPixelFormat.dwRGBAlphaBitMask =
        0x00008000ul;
    shared->texture_formats[2].ddpfPixelFormat.dwFlags =
        V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
    shared->texture_formats[2].ddpfPixelFormat.dwRBitMask = 0x00000f00ul;
    shared->texture_formats[2].ddpfPixelFormat.dwGBitMask = 0x000000f0ul;
    shared->texture_formats[2].ddpfPixelFormat.dwBBitMask = 0x0000000ful;
    shared->texture_formats[2].ddpfPixelFormat.dwRGBAlphaBitMask =
        0x0000f000ul;
    shared->d3d_global.lpTextureFormats = &shared->texture_formats[0];
    shared->d3d_global.dwNumTextureFormats = 3ul;
    shared->d3d_global.dwNumVertices = 0ul;
    shared->d3d_global.dwNumClipVertices = 0ul;
}

/* The same conditions eng_sis6326.c's 2D engine requires, and an engine
 * that has not timed out. */
static int v9x_d3d_sis_ready(void)
{
    return v9x_hal != 0 && !v9x_d3d_sis_quarantined &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_SIS_6326 &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_D3D_SIS_MMIO_BYTES;
}

/* A level's row in the layout the builder assumes: the texels, padded to
 * the 4-byte pitch unit. */
static DWORD v9x_d3d_sis_level_pitch(DWORD edge, DWORD texel_bytes)
{
    DWORD row = edge * texel_bytes;

    return row < V9X_D3D_SIS_PITCH_UNIT ? V9X_D3D_SIS_PITCH_UNIT : row;
}

/*
 * The mip chain down the attachments, as the Mach64 walks it: a level
 * counts only where the layout the builder assumes holds - edges halved,
 * tight rows, on the base alignment - which a chain create_surface placed
 * meets by construction. The first level that fails ends the chain.
 */
static DWORD v9x_d3d_sis_chain(const V9X_DD_SURFACE_LCL *top,
                               DWORD texel_bytes,
                               V9X_D3D_SIS_TEXTURE *texture)
{
    const V9X_DD_SURFACE_LCL *level = top;
    DWORD count = 1ul;

    while (count < V9X_D3D_SIS_TEXTURE_LEVELS) {
        const V9X_DD_ATTACH_NODE *node =
            (const V9X_DD_ATTACH_NODE *)level->lpAttachList;
        const V9X_DD_SURFACE_LCL *next = 0;
        DWORD edge = texture->width >> count;
        DWORD rows = texture->height >> count;
        DWORD offset;

        while (node != 0) {
            if (node->object != 0 && node->object != level &&
                (node->object->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul) {
                next = node->object;
                break;
            }
            node = node->next;
        }
        if (next == 0 || next->lpGbl == 0 || (edge == 0ul && rows == 0ul)) {
            break;
        }
        if (edge == 0ul) {
            edge = 1ul;
        }
        if (rows == 0ul) {
            rows = 1ul;
        }
        if ((DWORD)next->lpGbl->wWidth != edge ||
            (DWORD)next->lpGbl->wHeight != rows ||
            (DWORD)next->lpGbl->lPitch !=
                v9x_d3d_sis_level_pitch(edge, texel_bytes)) {
            break;
        }
        offset = v9x_surface_offset(next);
        if (offset == 0xfffffffful ||
            (offset & (V9X_D3D_SIS_TEXTURE_ALIGN - 1ul)) != 0ul) {
            break;
        }
        texture->level_offsets[count++] = offset;
        level = next;
    }
    return count;
}

/* The bound texture's format, shape and placement, from its surface; an
 * unknown format when it cannot be sampled, which the mapping refuses. */
static void v9x_d3d_sis_resolve_texture(const V9X_R3D_DRAW *draw,
                                        V9X_D3D_SIS_TEXTURE *texture)
{
    V9X_DD_SURFACE_LCL *surface;
    DWORD format = V9X_D3D_SIS_TEXTURE_UNKNOWN;
    DWORD offset;

    texture->format = V9X_D3D_SIS_TEXTURE_UNKNOWN;
    texture->has_alpha = 0;
    texture->width = 0ul;
    texture->height = 0ul;
    texture->levels = 1ul;
    texture->offset = 0ul;
    texture->pitch_bytes = 0ul;

    surface = (V9X_DD_SURFACE_LCL *)draw->texture.object;
    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return;
    }
    offset = v9x_surface_offset(surface);
    if (offset == 0xfffffffful || surface->lpGbl->lPitch <= 0l ||
        !v9x_d3d_sis_texture_format(surface, &format)) {
        return;
    }
    texture->format = format;
    texture->has_alpha = format != V9X_SIS3D_TEXEL_RGB565 &&
                         format != V9X_SIS3D_TEXEL_RGB555;
    texture->width = (DWORD)surface->lpGbl->wWidth;
    texture->height = (DWORD)surface->lpGbl->wHeight;
    texture->offset = offset;
    texture->pitch_bytes = (DWORD)surface->lpGbl->lPitch;
    texture->level_offsets[0] = offset;
    if ((surface->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul) {
        texture->levels = v9x_d3d_sis_chain(surface,
                                            v9x_d3d_sis_texel_bytes(format),
                                            texture);
    }
}

/* Colour a specular-enabled draw would actually add. */
static DWORD v9x_d3d_sis_specular_rgb(const V9X_R3D_VERTEX *vertices,
                                      DWORD count)
{
    DWORD rgb = 0ul;
    DWORD index;

    for (index = 0ul; index < count; ++index) {
        rgb |= vertices[index].specular & 0x00fffffful;
    }
    return rgb;
}

/*
 * A refused draw by V9X_D3D_SIS_REFUSE_* reason, in the Mach64's policy
 * counters as the Rage IIC does (V9XTRACE's M64PolicyNN): Final Reality's
 * full run had 17,013 refused batches and nothing to say why (boot 289).
 * The texture op and blend pair of the refused draw are kept as well.
 */
static void v9x_d3d_sis_count_refusal(const V9X_R3D_DRAW *draw,
                                      v9x_u32 reason)
{
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;

    diagnostics->m64_policy_last = reason;
    if (reason < 20ul) {
        ++diagnostics->m64_policy_counts[reason];
    }
    if (reason == V9X_D3D_SIS_REFUSE_TEXTURE_OP && draw->texture.op < 32ul) {
        diagnostics->m64_texop_refused_mask |= 1ul << draw->texture.op;
    }
    if (reason == V9X_D3D_SIS_REFUSE_BLEND) {
        diagnostics->blend_last_pair =
            (draw->src_blend << 16) | (draw->dst_blend & 0xfffful);
    }
}

/* Passive, as the contract requires: the vertices' specular colour is not
 * known here, so draw() checks it again. */
static int v9x_d3d_sis_accepts(const V9X_R3D_DRAW *draw)
{
    V9X_D3D_SIS_TEXTURE resolved;
    struct v9x_sis3d_state state;
    struct v9x_sis3d_texture texture;
    int textured;

    if (draw == 0 || v9x_hal == 0) {
        return 0;
    }
    v9x_d3d_sis_resolve_texture(draw, &resolved);
    return v9x_d3d_sis_map_draw(draw, &resolved, v9x_hal->fb.vram_bytes,
                                0ul, &state, &texture, &textured) ==
           V9X_D3D_SIS_REFUSE_NONE;
}

static int v9x_d3d_sis_draw(const V9X_R3D_DRAW *draw,
                            const V9X_R3D_VERTEX *vertices,
                            DWORD triangle_count)
{
    V9X_D3D_SIS_TEXTURE resolved;
    struct v9x_sis3d_state state;
    struct v9x_sis3d_texture texture;
    struct v9x_sis3d_vertex corners[3];
    const V9X_ENGINE32_OPS *engine2d;
    V9X_D3D_SIS_BATCH_LOG *log;
    DWORD index;
    DWORD packets = 0ul;
    v9x_u32 reason;
    int textured;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul ||
        triangle_count > V9X_D3D_SIS_MAX_TRIANGLES || !v9x_d3d_sis_ready()) {
        return 0;
    }

    /* The mapping first: nothing below runs for a draw it refuses. */
    v9x_d3d_sis_resolve_texture(draw, &resolved);
    reason = v9x_d3d_sis_map_draw(draw, &resolved, v9x_hal->fb.vram_bytes,
                                  v9x_d3d_sis_specular_rgb(vertices,
                                      triangle_count * 3ul),
                                  &state, &texture, &textured);
    if (reason != V9X_D3D_SIS_REFUSE_NONE) {
        v9x_d3d_sis_count_refusal(draw, reason);
        return 0;
    }
    if (v9x_sis3d_build_state(&state, &v9x_d3d_sis_state_writes) !=
        V9X_STATUS_OK) {
        return 0;
    }
    /* Every batch carries texture words (d3d_sis6326_map.c), and the cache
     * is cleared with D4 pulsed, as the probe phases ran it. */
    texture.clear_cache = 1;
    if (v9x_sis3d_build_texture(&texture, &v9x_d3d_sis_clear_writes) !=
        V9X_STATUS_OK) {
        return 0;
    }
    texture.clear_cache = 0;
    if (v9x_sis3d_build_texture(&texture, &v9x_d3d_sis_texture_writes) !=
        V9X_STATUS_OK) {
        return 0;
    }

    /* Every triangle's words before any write. */
    for (index = 0ul; index < triangle_count; ++index) {
        if (!v9x_d3d_sis_triangle(vertices + index * 3ul, draw->shade_mode,
                                  textured, corners,
                                  &v9x_d3d_sis_primitives[packets])) {
            continue;
        }
        v9x_sis3d_build_vertices(corners,
                                 &v9x_d3d_sis_vertex_writes[packets]);
        ++packets;
    }
    if (packets == 0ul) {
        return 1;
    }

    /* Kept for the first timeout's record (v9x_d3d_sis_log_timeout). */
    log = &v9x_d3d_sis_log[v9x_d3d_sis_batches & 1ul];
    log->number = v9x_d3d_sis_batches++;
    log->textured = (DWORD)textured;
    log->triangles = packets;
    log->primitive = v9x_d3d_sis_primitives[0];
    log->state = v9x_d3d_sis_state_writes;
    log->clear = v9x_d3d_sis_clear_writes;
    log->texture = v9x_d3d_sis_texture_writes;
    log->vertex = v9x_d3d_sis_vertex_writes[0];
    log->texture_offset = textured ? texture.offset : 0ul;
    log->texture_bytes = textured
                             ? texture.pitch_bytes << texture.log2_height
                             : 0ul;
    log->depth_offset = 0ul;
    log->depth_bytes = 0ul;
    if ((state.enable & V9X_SIS3D_ENABLE_Z_TEST) != 0ul) {
        log->depth_offset = state.z_offset;
        log->depth_bytes = state.z_pitch_bytes * state.target.height;
    }

    /* Blits that wrote what this batch reads or overwrites finish first. */
    engine2d = v9x_engine32();
    if (engine2d != 0 && engine2d->wait_idle != 0 &&
        !engine2d->wait_idle(1)) {
        return 0;
    }
    v9x_d3d_sis_wait_site = V9X_D3D_SIS_WAIT_BEFORE_STATE;
    if (!v9x_d3d_sis_wait_idle()) {
        return 0;
    }
    v9x_d3d_sis_emit(&v9x_d3d_sis_state_writes);
    v9x_d3d_sis_emit(&v9x_d3d_sis_clear_writes);
    v9x_d3d_sis_emit(&v9x_d3d_sis_texture_writes);
    v9x_present_note_submission();
    v9x_d3d_sis_wait_site = V9X_D3D_SIS_WAIT_BEFORE_TRIANGLE;
    for (index = 0ul; index < packets; ++index) {
        /* One triangle at a time: the Turbo Queue is off. */
        v9x_d3d_sis_wait_triangle = index;
        if (!v9x_d3d_sis_wait_idle()) {
            return 0;
        }
        v9x_d3d_sis_write(V9X_SIS3D_PRIMITIVE,
                          v9x_d3d_sis_primitives[index]);
        v9x_d3d_sis_emit(&v9x_d3d_sis_vertex_writes[index]);
        v9x_d3d_sis_end_primitive();
    }
    v9x_d3d_sis_wait_site = V9X_D3D_SIS_WAIT_AFTER_BATCH;
    return v9x_d3d_sis_wait_idle();
}

/* One edge the sampler takes: a power of two in range. */
static int v9x_d3d_sis_texture_edge(DWORD edge)
{
    return (edge & (edge - 1ul)) == 0ul &&
           edge >= v9x_d3d_sis_limits.texture_size_min &&
           edge <= v9x_d3d_sis_limits.texture_size_max;
}

/*
 * A texture placed at the pitch the sampler reads. DirectDraw would round a
 * texture's pitch to vmiData.dwTextureAlign; the engine ORs a row's term
 * into the column offset, so a pitch whose lowest set bit is below the
 * row's bytes misaddresses (textures record). So a video-memory texture of
 * a format and size this engine samples is placed in a block of its own at
 * its tight pitch, and a chain created in one call - DirectDraw's list, top
 * first - in one block, each level after the one before on the alignment.
 */
static DWORD v9x_d3d_sis_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list;
    V9X_DD_SURFACE_LCL *surface;
    v9x_u32 offsets[V9X_D3D_SIS_TEXTURE_LEVELS];
    v9x_u32 pitches[V9X_D3D_SIS_TEXTURE_LEVELS];
    DWORD align = V9X_D3D_SIS_TEXTURE_ALIGN;
    DWORD format;
    DWORD texel_bytes;
    DWORD width;
    DWORD height;
    DWORD bytes = 0ul;
    DWORD level;
    DWORD base;

    if (v9x_hal == 0 || data == 0 || data->dwSCnt == 0ul ||
        data->dwSCnt > V9X_D3D_SIS_TEXTURE_LEVELS || data->lplpSList == 0) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    list = (V9X_DD_SURFACE_LCL **)data->lplpSList;
    surface = list[0];
    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & (V9X_DDSCAPS_SYSTEMMEMORY |
                             V9X_DDSCAPS_ZBUFFER)) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    width = (DWORD)surface->lpGbl->wWidth;
    height = (DWORD)surface->lpGbl->wHeight;
    if (!v9x_d3d_sis_texture_edge(width) ||
        !v9x_d3d_sis_texture_edge(height) ||
        !v9x_d3d_sis_texture_format(surface, &format)) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    texel_bytes = v9x_d3d_sis_texel_bytes(format);

    for (level = 0ul; level < data->dwSCnt; ++level) {
        const V9X_DD_SURFACE_LCL *member = list[level];
        DWORD edge = width >> level;
        DWORD rows = height >> level;

        if (edge == 0ul && rows == 0ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        if (edge == 0ul) {
            edge = 1ul;
        }
        if (rows == 0ul) {
            rows = 1ul;
        }
        if (member == 0 || member->lpGbl == 0 ||
            (data->dwSCnt > 1ul &&
             (member->ddsCaps & V9X_DDSCAPS_MIPMAP) == 0ul) ||
            (member->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
            (DWORD)member->lpGbl->wWidth != edge ||
            (DWORD)member->lpGbl->wHeight != rows) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        offsets[level] = (bytes + align - 1ul) & ~(align - 1ul);
        pitches[level] = v9x_d3d_sis_level_pitch(edge, texel_bytes);
        bytes = offsets[level] + pitches[level] * rows;
    }
    if (v9x_d3d_place_chain(data, align, bytes, offsets, pitches,
                            &base) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    ++v9x_hal->d3d_diagnostics.texture_placed;
    v9x_hal->d3d_diagnostics.texture_placed_bytes += bytes;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

static void v9x_d3d_sis_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    (void)v9x_d3d_place_release(data, V9X_D3D_SIS_TEXTURE_ALIGN);
}

/* Positional: V9X_D3D_ENGINE_OPS is append-only (d3d_internal.h). */
const V9X_D3D_ENGINE_OPS v9x_d3d_engine_sis6326 = {
    &v9x_d3d_sis_limits,
    v9x_d3d_sis_texture_format,
    v9x_d3d_sis_describe_caps,
    0,                                  /* draw_triangles: draw serves */
    v9x_d3d_sis_ready,
    v9x_d3d_sis_create_surface,
    v9x_d3d_sis_destroy_surface,
    v9x_d3d_sis_draw,
    v9x_d3d_sis_accepts
};
