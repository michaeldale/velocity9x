/*
 * Matrox MGA-2164W Direct3D engine.
 *
 * Phase 4 of docs\plans\matrox-mga2164w-hardware-3d.md. Everything this
 * engine emits was measured by the MGA2D probe on A8U4I5
 * (docs\decisions\2026-10-10-mga2164w-trapezoids.md, -depth.md,
 * -textures.md): the register words come from mga_3d.c, each triangle's
 * trapezoids from mga_setup.c and the translation of the draw from
 * d3d_mga_map.c, all host-tested. This file resolves DirectDraw surfaces,
 * places Z buffers and textures, and emits.
 *
 * Only the 2164W gets here: its engine hook alone stamps CAP_D3D
 * (millennium_hw16.c), and v9x_d3d_mga_ready checks it again. The 2064W
 * has no texture engine and stays on the software rasterizer.
 *
 * A batch returns with its trapezoids still in the FIFO. Everything that
 * touches the engine or a surface after it waits for idle first: the 2D
 * engine before each operation (eng_mga.c), GDI before each of its own
 * (gdi_accel.c), and Lock, Flip, Blt and DestroySurface through
 * v9x_render_drain. Until 2026-10-10 each batch also waited for idle at its
 * end, which kept the CPU from setting up the next batch while the engine
 * drew this one.
 */
#include "d3d_internal.h"
#include "d3d_mga_map.h"
#include "velocity9x/mga_setup.h"

/* Bounded waits, as eng_mga.c's. */
#define V9X_D3D_MGA_SPINS 0x00200000ul

/* The 16 KiB control aperture the mini-VDD maps. */
#define V9X_D3D_MGA_MMIO_BYTES 0x00004000ul

/* The 2164W's FIFOSTATUS fifocount is <6:0>: 64 entries, an empty FIFO
 * reading 40h (measured, MGA2D on A8U4I5; the 2164W spec's field table
 * says <5:0>, its text 64). */
#define V9X_D3D_MGA_FIFO_COUNT_MASK 0x0000007ful

/* The core hands no batch over V9X_D3D_INDEXED_BATCH triangles. */
#define V9X_D3D_MGA_MAX_TRIANGLES 64ul

/* TEXORG ignores its low five bits; ZORG must be 512-aligned (textures and
 * depth records). Placement release takes the larger. */
#define V9X_D3D_MGA_TEXTURE_ALIGN 32ul
#define V9X_D3D_MGA_DEPTH_ALIGN   512ul
/* The narrowest texture row TEXCTL's tpitch can state: 8 texels. */
#define V9X_D3D_MGA_TEXTURE_PITCH_MIN 8ul
#define V9X_D3D_MGA_TEXTURE_LEVELS 11ul

/* Direct3D's 0..1 depth in the 16-bit Z buffer's units. */
#define V9X_D3D_MGA_Z_SCALE 65535.0f
#define V9X_D3D_MGA_SUBPIXEL 16.0f

/* "MG3D": the 3D idle timeout, in V9XTRACE.INI's FaultCode. */
#define V9X_D3D_MGA_FAULT_3D 0x4d473344ul

static struct v9x_mga3d_trap v9x_d3d_mga_traps[V9X_MGA_SETUP_TRAPS];
static struct v9x_mga3d_writes v9x_d3d_mga_writes;

/*
 * A 16 bpp target with a pitch the linearizer takes (64-byte steps of 32
 * pixels, at most 2048), inside the clip window's 11 bits. Textures are
 * powers of two; 256 is what is published, though 1024 fits the fields.
 * clip_in_core: the engine's guard band is not measured. depth_pitch_own:
 * the engine reads Z at the target's pitch, which create_surface gives the
 * Z buffer, not DirectDraw's packed row.
 */
static const V9X_D3D_ENGINE_LIMITS v9x_d3d_mga_limits = {
    16ul,           /* target_bits_per_pixel */
    4096ul,         /* target_pitch_max */
    64ul,           /* target_pitch_align */
    2047ul,         /* target_dimension_max */
    1ul,            /* texture_size_min */
    256ul,          /* texture_size_max */
    2047.0f,        /* coordinate_limit */
    16ul,           /* depth_bits_per_pixel */
    V9X_D3D_MGA_TEXTURE_ALIGN, /* texture_align */
    1ul,            /* clip_in_core */
    1ul,            /* depth_pitch_own */
    0ul,            /* depth_fill_shift */
    1ul             /* texture_units */
};

/* Set when the engine fails to drain; nothing 3D is emitted again until
 * the next mode set re-validates the descriptor. */
static int v9x_d3d_mga_quarantined = 0;

static void v9x_d3d_mga_write(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) =
        value;
}

static DWORD v9x_d3d_mga_read(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base +
                               offset);
}

/* On a timeout: the first of the boot is traced, and the engine is given
 * up for the mode, as eng_mga.c does, so Lock's drain completes. */
static int v9x_d3d_mga_timed_out(DWORD status)
{
    if (v9x_hal->engine.idle_timeouts++ == 0ul) {
        v9x_trace_flush_fault(V9X_D3D_MGA_FAULT_3D, status);
    }
    v9x_d3d_mga_quarantined = 1;
    v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
    return 0;
}

/*
 * FIFO entries known free: the last FIFOSTATUS read, less what has been
 * written since. The engine only drains, so the count is conservative, and
 * one read covers several trapezoids instead of one each - a PCI read
 * stalls the CPU. Valid within a batch; v9x_d3d_mga_batch_begin clears it,
 * since the 2D engine and GDI write between batches.
 */
static DWORD v9x_d3d_mga_fifo_credit = 0ul;

static int v9x_d3d_mga_wait_fifo(DWORD entries)
{
    DWORD spins;
    DWORD fifo = 0ul;
    DWORD free_entries;

    DWORD started;

    if (v9x_d3d_mga_fifo_credit >= entries) {
        v9x_d3d_mga_fifo_credit -= entries;
        return 1;
    }
    started = V9X_TIME_BEGIN();
    for (spins = 0ul; spins < V9X_D3D_MGA_SPINS; ++spins) {
        fifo = v9x_d3d_mga_read(V9X_MGA_FIFOSTATUS);
        free_entries = fifo & V9X_D3D_MGA_FIFO_COUNT_MASK;
        if (free_entries >= entries) {
            v9x_d3d_mga_fifo_credit = free_entries - entries;
            V9X_TIME_END(V9X_TIME_RING_SPACE_WAIT, started);
            return 1;
        }
    }
    V9X_TIME_END(V9X_TIME_RING_SPACE_WAIT, started);
    return v9x_d3d_mga_timed_out(fifo);
}

static void v9x_d3d_mga_batch_begin(void)
{
    v9x_d3d_mga_fifo_credit = 0ul;
}

/*
 * One trapezoid's words, behind a FIFO wait for all of them. Every word is
 * written. A cache that skipped state registers already holding their
 * value made mwd5 slower on A8U4I5 (7.953 and 7.773 fps with it, with and
 * without the idle wait, against 8.391 before it, boots 392-394); its
 * lookups, three scans of 11 offsets per write, are the suspected cost.
 */
static int v9x_d3d_mga_emit(const struct v9x_mga3d_writes *writes)
{
    DWORD index;
    DWORD started;

    if (!v9x_d3d_mga_wait_fifo(writes->count)) {
        return 0;
    }
    started = V9X_TIME_BEGIN();
    for (index = 0ul; index < writes->count; ++index) {
        v9x_d3d_mga_write(writes->offsets[index], writes->values[index]);
    }
    V9X_TIME_END(V9X_TIME_RING_WRITE, started);
    return 1;
}

/* The two 16-bit formats the probe measured: 565 and 1555. */
static int v9x_d3d_mga_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                      DWORD *format_out)
{
    const V9X_DDPIXELFORMAT *format;

    if (format_out != 0) {
        *format_out = 0ul;
    }
    if (surface == 0 || surface->lpGbl == 0 || format_out == 0) {
        return 0;
    }
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul) {
        format = &surface->lpGbl->ddpfSurface;
    } else {
        format = &v9x_hal->info.vmiData.ddpfDisplay;
    }
    if ((format->dwFlags & V9X_DDPF_RGB) == 0ul ||
        format->dwRGBBitCount != 16ul) {
        return 0;
    }
    if ((format->dwFlags & V9X_DDPF_ALPHAPIXELS) == 0ul &&
        format->dwRBitMask == 0x0000f800ul &&
        format->dwGBitMask == 0x000007e0ul &&
        format->dwBBitMask == 0x0000001ful) {
        *format_out = V9X_MGA3D_TEX_TW16;
        return 1;
    }
    if ((format->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul &&
        format->dwRBitMask == 0x00007c00ul &&
        format->dwGBitMask == 0x000003e0ul &&
        format->dwBBitMask == 0x0000001ful &&
        format->dwRGBAlphaBitMask == 0x00008000ul) {
        *format_out = V9X_MGA3D_TEX_TW15;
        return 1;
    }
    return 0;
}

/*
 * The measured boundary, as Matrox's own HAL publishes it on the same
 * card (docs\decisions\2026-10-10-mga2164w-matrox-hal-baseline.md), less
 * what was not measured: no specular, no fog, no alpha test of its own,
 * point sampling only. Source-alpha blending is drawn as the stipple.
 */
static void v9x_d3d_mga_describe_caps(V9X_DD_SHARED *shared)
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
    /* MASKZ: atype I compares and does not write (depth record). */
    tri->dwMiscCaps = V9X_D3DPMISCCAPS_MASKZ |
                      V9X_D3DPMISCCAPS_CULLNONE | V9X_D3DPMISCCAPS_CULLCW |
                      V9X_D3DPMISCCAPS_CULLCCW;
    /* Exact sixteenth-pixel coverage (mga_setup.c); 16 bpp shading is
     * dithered. */
    tri->dwRasterCaps = V9X_D3DPRASTERCAPS_DITHER |
                        V9X_D3DPRASTERCAPS_ZTEST |
                        V9X_D3DPRASTERCAPS_SUBPIXEL;
    tri->dwZCmpCaps =
        V9X_D3DPCMPCAPS_NEVER | V9X_D3DPCMPCAPS_LESS |
        V9X_D3DPCMPCAPS_EQUAL | V9X_D3DPCMPCAPS_LESSEQUAL |
        V9X_D3DPCMPCAPS_GREATER | V9X_D3DPCMPCAPS_NOTEQUAL |
        V9X_D3DPCMPCAPS_GREATEREQUAL | V9X_D3DPCMPCAPS_ALWAYS;
    tri->dwAlphaCmpCaps = 0ul;
    tri->dwSrcBlendCaps = V9X_D3DPBLENDCAPS_ONE |
                          V9X_D3DPBLENDCAPS_SRCALPHA;
    tri->dwDestBlendCaps = V9X_D3DPBLENDCAPS_ZERO |
                           V9X_D3DPBLENDCAPS_INVSRCALPHA;
    tri->dwShadeCaps =
        V9X_D3DPSHADECAPS_COLORFLATRGB | V9X_D3DPSHADECAPS_COLORGOURAUDRGB |
        V9X_D3DPSHADECAPS_ALPHAFLATSTIPPLED;
    tri->dwTextureCaps = V9X_D3DPTEXTURECAPS_PERSPECTIVE |
                         V9X_D3DPTEXTURECAPS_POW2 |
                         V9X_D3DPTEXTURECAPS_ALPHA |
                         V9X_D3DPTEXTURECAPS_TRANSPARENCY;
    tri->dwTextureFilterCaps = V9X_D3DPTFILTERCAPS_NEAREST;
    tri->dwTextureBlendCaps = V9X_D3DPTBLENDCAPS_DECAL |
                              V9X_D3DPTBLENDCAPS_MODULATE |
                              V9X_D3DPTBLENDCAPS_DECALALPHA |
                              V9X_D3DPTBLENDCAPS_MODULATEALPHA |
                              V9X_D3DPTBLENDCAPS_COPY;
    tri->dwTextureAddressCaps = V9X_D3DPTADDRESSCAPS_WRAP |
                                V9X_D3DPTADDRESSCAPS_CLAMP;

    shared->d3d_extended_caps.dwSize = sizeof(V9X_D3DHAL_D3DEXTENDEDCAPS);
    shared->d3d_extended_caps.dwMinTextureWidth =
        v9x_d3d_mga_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureWidth =
        v9x_d3d_mga_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinTextureHeight =
        v9x_d3d_mga_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureHeight =
        v9x_d3d_mga_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMaxStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMinStippleHeight = 0ul;
    shared->d3d_extended_caps.dwMaxStippleHeight = 0ul;
    shared->d3d_global.hwCaps.dwDeviceRenderBitDepth = V9X_DDBD_16;
    shared->d3d_global.hwCaps.dwDeviceZBufferBitDepth = V9X_DDBD_16;

    for (index = 0ul; index < 2ul; ++index) {
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
    shared->d3d_global.lpTextureFormats = &shared->texture_formats[0];
    shared->d3d_global.dwNumTextureFormats = 2ul;
    shared->d3d_global.dwNumVertices = 0ul;
    shared->d3d_global.dwNumClipVertices = 0ul;
}

/* eng_mga.c's conditions, the 2164W's D3D stamp, and no timeout. */
static int v9x_d3d_mga_ready(void)
{
    return v9x_hal != 0 && !v9x_d3d_mga_quarantined &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_MGA &&
        (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_D3D) != 0ul &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_D3D_MGA_MMIO_BYTES;
}

static DWORD v9x_d3d_mga_log2(DWORD value)
{
    DWORD log2 = 0ul;

    while ((1ul << log2) < value && log2 < 31ul) {
        ++log2;
    }
    return log2;
}

static int v9x_d3d_mga_pow2(DWORD value)
{
    return value != 0ul && (value & (value - 1ul)) == 0ul;
}

/* The row a texture level is placed at: its width, at least 8 texels,
 * which is what tpitch can state. */
static DWORD v9x_d3d_mga_texture_pitch(DWORD width)
{
    return (width < V9X_D3D_MGA_TEXTURE_PITCH_MIN
                ? V9X_D3D_MGA_TEXTURE_PITCH_MIN : width) * 2ul;
}

/* The bound texture's top level, as the sampler will read it. A mip chain
 * is drawn from its top level: the chip has no mipmapping. */
static void v9x_d3d_mga_resolve_texture(const V9X_R3D_DRAW *draw,
                                        V9X_D3D_MGA_TEXTURE *texture)
{
    V9X_DD_SURFACE_LCL *surface;
    DWORD format;
    DWORD offset;
    DWORD width;
    DWORD height;
    DWORD pitch;

    texture->valid = 0ul;
    texture->has_color_key = 0ul;
    texture->color_key = 0ul;
    surface = (V9X_DD_SURFACE_LCL *)draw->texture.object;
    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
        !v9x_d3d_mga_texture_format(surface, &format)) {
        return;
    }
    offset = v9x_surface_offset(surface);
    width = (DWORD)surface->lpGbl->wWidth;
    height = (DWORD)surface->lpGbl->wHeight;
    if (offset == 0xfffffffful || surface->lpGbl->lPitch <= 0l ||
        (offset % V9X_D3D_MGA_TEXTURE_ALIGN) != 0ul ||
        !v9x_d3d_mga_pow2(width) || !v9x_d3d_mga_pow2(height) ||
        width > v9x_d3d_mga_limits.texture_size_max ||
        height > v9x_d3d_mga_limits.texture_size_max) {
        return;
    }
    /* The row must be a power of two of at least 8 texels and the width. */
    pitch = (DWORD)surface->lpGbl->lPitch / 2ul;
    if (!v9x_d3d_mga_pow2(pitch) || pitch < V9X_D3D_MGA_TEXTURE_PITCH_MIN ||
        pitch < width || (DWORD)surface->lpGbl->lPitch != pitch * 2ul) {
        return;
    }
    texture->valid = 1ul;
    texture->format = format;
    texture->offset = offset;
    texture->log2_width = v9x_d3d_mga_log2(width);
    texture->log2_height = v9x_d3d_mga_log2(height);
    texture->log2_pitch = v9x_d3d_mga_log2(pitch);
    if ((surface->dwFlags & V9X_DDRAWISURF_HASCKEYSRCBLT) != 0ul) {
        texture->has_color_key = 1ul;
        texture->color_key = surface->ddckCKSrcBltLow;
    }
}

/* Colour a specular-enabled draw would actually add. */
static DWORD v9x_d3d_mga_specular_rgb(const V9X_R3D_VERTEX *vertices,
                                      DWORD count)
{
    DWORD rgb = 0ul;
    DWORD index;

    for (index = 0ul; index < count; ++index) {
        rgb |= vertices[index].specular & 0x00fffffful;
    }
    return rgb;
}

/* Every vertex at alpha 255. */
static v9x_u32 v9x_d3d_mga_vertices_opaque(const V9X_R3D_VERTEX *vertices,
                                           DWORD count)
{
    DWORD index;

    for (index = 0ul; index < count; ++index) {
        if ((vertices[index].color & 0xff000000ul) != 0xff000000ul) {
            return 0ul;
        }
    }
    return 1ul;
}

/* A refused blend pair in the diagnostics' table: its own slot while one
 * is free, the overflow count after. */
static void v9x_d3d_mga_count_blend(V9X_D3D_DIAGNOSTICS *diagnostics,
                                    const V9X_R3D_DRAW *draw)
{
    DWORD key;
    unsigned int slot;

    key = ((draw->src_blend & 0xfful) << 16) |
          ((draw->dst_blend & 0xfful) << 8);
    if (draw->texture.object != 0) {
        key |= V9X_D3D_BLEND_KEY_TEXTURED;
    }
    for (slot = 0u; slot < V9X_D3D_BLEND_REFUSED_SLOTS; ++slot) {
        if (diagnostics->blend_refused_count[slot] == 0ul) {
            diagnostics->blend_refused_key[slot] = key;
        }
        if (diagnostics->blend_refused_key[slot] == key) {
            ++diagnostics->blend_refused_count[slot];
            return;
        }
    }
    ++diagnostics->blend_refused_overflow;
}

/* A refusal by reason, in the counters the Rage IIC and the SiS use
 * (V9XTRACE's M64PolicyNN). */
static void v9x_d3d_mga_count_refusal(const V9X_R3D_DRAW *draw,
                                      v9x_u32 reason)
{
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;

    diagnostics->m64_policy_last = reason;
    if (reason < 20ul) {
        ++diagnostics->m64_policy_counts[reason];
    }
    if (reason == V9X_D3D_MGA_REFUSE_TEXTURE_OP && draw != 0 &&
        draw->texture.op < 32ul) {
        diagnostics->m64_texop_refused_mask |= 1ul << draw->texture.op;
    }
    if (reason == V9X_D3D_MGA_REFUSE_BLEND && draw != 0) {
        diagnostics->blend_last_pair =
            (draw->src_blend << 16) | (draw->dst_blend & 0xfffful);
        v9x_d3d_mga_count_blend(diagnostics, draw);
    }
}

/* A triangle declined after mapping, by stage, as a SETUP refusal too. */
static void v9x_d3d_mga_count_setup(const V9X_R3D_DRAW *draw,
                                    unsigned int stage)
{
    ++v9x_hal->d3d_diagnostics.mga_setup_refused[stage];
    v9x_d3d_mga_count_refusal(draw, V9X_D3D_MGA_REFUSE_SETUP);
}

static int v9x_d3d_mga_accepts(const V9X_R3D_DRAW *draw)
{
    V9X_D3D_MGA_TEXTURE texture;
    V9X_D3D_MGA_MAPPED mapped;
    v9x_u32 reason;

    if (draw == 0 || v9x_hal == 0) {
        return 0;
    }
    v9x_d3d_mga_resolve_texture(draw, &texture);
    reason = v9x_d3d_mga_map_draw(draw, &texture, v9x_hal->fb.vram_bytes,
                                  0ul, &mapped);
    if (reason != V9X_D3D_MGA_REFUSE_NONE && reason < 20ul) {
        ++v9x_hal->d3d_diagnostics.m64_accept_policy[reason];
    }
    return reason == V9X_D3D_MGA_REFUSE_NONE;
}

/* A packed colour's channel as a setup colour. */
static double v9x_d3d_mga_channel(DWORD argb, unsigned int shift)
{
    return (double)((argb >> shift) & 0xfful);
}

/*
 * One vertex to the setup's units: position in sixteenths, Z in the 16-bit
 * buffer's units, the colour (the first vertex's under flat shading), the
 * texture coordinates and 1/w. Zero for a vertex out of the setup's range,
 * which the core's clipping should never produce.
 */
static int v9x_d3d_mga_vertex(const V9X_R3D_VERTEX *vertex,
                              const V9X_R3D_VERTEX *provoking, int flat,
                              struct v9x_mga_setup_vertex *out)
{
    LONG x = v9x_float_to_long(vertex->sx * V9X_D3D_MGA_SUBPIXEL);
    LONG y = v9x_float_to_long(vertex->sy * V9X_D3D_MGA_SUBPIXEL);
    DWORD argb = flat ? provoking->color : vertex->color;

    if (x < 0l || y < 0l || x > V9X_MGA_SETUP_COORD_MAX ||
        y > V9X_MGA_SETUP_COORD_MAX) {
        return 0;
    }
    out->x = (v9x_s32)x;
    out->y = (v9x_s32)y;
    out->z = (double)(vertex->sz * V9X_D3D_MGA_Z_SCALE);
    out->red = v9x_d3d_mga_channel(argb, 16u);
    out->green = v9x_d3d_mga_channel(argb, 8u);
    out->blue = v9x_d3d_mga_channel(argb, 0u);
    out->u = (double)vertex->tu;
    out->v = (double)vertex->tv;
    out->q = (double)vertex->rhw;
    return 1;
}

/* A triangle's alpha for the stipple: the first vertex's when flat, the
 * mean otherwise. */
static DWORD v9x_d3d_mga_triangle_alpha(const V9X_R3D_VERTEX *triangle,
                                        int flat)
{
    if (flat) {
        return triangle[0].color >> 24;
    }
    return ((triangle[0].color >> 24) + (triangle[1].color >> 24) +
            (triangle[2].color >> 24)) / 3ul;
}

static int v9x_d3d_mga_draw_batch(const V9X_R3D_DRAW *draw,
                                  const V9X_R3D_VERTEX *vertices,
                                  DWORD triangle_count)
{
    V9X_R3D_DRAW described;
    V9X_D3D_MGA_TEXTURE texture;
    V9X_D3D_MGA_MAPPED mapped;
    struct v9x_mga3d_trap base;
    struct v9x_mga_setup_vertex corners[3];
    const V9X_ENGINE32_OPS *engine2d;
    const V9X_R3D_VERTEX *triangle;
    v9x_u32 reason;
    v9x_u32 count;
    v9x_u32 trap;
    v9x_status status;
    DWORD index;
    DWORD corner;
    DWORD alpha;
    DWORD started;
    int flat;
    int emitted = 0;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul ||
        triangle_count > V9X_D3D_MGA_MAX_TRIANGLES || !v9x_d3d_mga_ready()) {
        return 0;
    }
    /* Direct3D's draw leaves vertex_alpha_opaque zero (d3d_core.c); the
     * batch's own vertices decide it here, as the Mach64's and the Rage
     * II's draws do. Without it every alpha test was refused: Half-Life's
     * ladders and grates, A8U4I5 boot 388. */
    described = *draw;
    if (described.explicit_state == 0ul) {
        described.vertex_alpha_opaque =
            v9x_d3d_mga_vertices_opaque(vertices, triangle_count * 3ul);
    }
    v9x_d3d_mga_resolve_texture(&described, &texture);
    reason = v9x_d3d_mga_map_draw(&described, &texture,
                                  v9x_hal->fb.vram_bytes,
                                  v9x_d3d_mga_specular_rgb(
                                      vertices, triangle_count * 3ul),
                                  &mapped);
    if (reason != V9X_D3D_MGA_REFUSE_NONE) {
        v9x_d3d_mga_count_refusal(&described, reason);
        return 0;
    }
    if (mapped.skip != 0ul) {
        return 1;
    }

    /* The 2D side's per-mode state (pixel width, plane mask, clip) before
     * the first trapezoid of the mode. */
    engine2d = v9x_engine32();
    if (engine2d == 0 || engine2d->validate_status == 0 ||
        !engine2d->validate_status()) {
        return 0;
    }

    v9x_d3d_mga_batch_begin();
    flat = mapped.flat != 0ul;
    for (index = 0ul; index < triangle_count; ++index) {
        triangle = vertices + index * 3ul;
        base = mapped.base;
        if (mapped.stipple != 0ul) {
            alpha = v9x_d3d_mga_triangle_alpha(triangle, flat);
            base.trans = v9x_d3d_mga_stipple(alpha);
            if (base.trans == V9X_D3D_MGA_STIPPLE_NONE) {
                continue;
            }
        }
        for (corner = 0ul; corner < 3ul; ++corner) {
            if (!v9x_d3d_mga_vertex(&triangle[corner], &triangle[0], flat,
                                    &corners[corner])) {
                break;
            }
        }
        if (corner != 3ul) {
            v9x_d3d_mga_count_setup(draw, V9X_D3D_MGA_SETUP_VERTEX);
            continue;
        }
        if (base.texture.enabled != 0ul) {
            /* Perspective only where w varies: the linear path's floor is
             * exact, the perspective path's carries the measured bias. */
            if (!(triangle[0].rhw > 0.0f && triangle[1].rhw > 0.0f &&
                  triangle[2].rhw > 0.0f)) {
                v9x_d3d_mga_count_setup(draw, V9X_D3D_MGA_SETUP_RHW);
                continue;
            }
            base.texture.perspective =
                (triangle[0].rhw != triangle[1].rhw ||
                 triangle[0].rhw != triangle[2].rhw) ? 1ul : 0ul;
        }
        started = V9X_TIME_BEGIN();
        status = v9x_mga_setup_triangle(&base, corners, v9x_d3d_mga_traps,
                                        &count);
        V9X_TIME_END(V9X_TIME_DECODE, started);
        if (status != V9X_STATUS_OK) {
            v9x_d3d_mga_count_setup(draw, V9X_D3D_MGA_SETUP_SPLIT);
            continue;
        }
        for (trap = 0ul; trap < count; ++trap) {
            started = V9X_TIME_BEGIN();
            status = v9x_mga3d_build_trap(&v9x_d3d_mga_traps[trap],
                                          &v9x_d3d_mga_writes);
            V9X_TIME_END(V9X_TIME_DECODE, started);
            if (status != V9X_STATUS_OK) {
                v9x_d3d_mga_count_setup(draw, V9X_D3D_MGA_SETUP_BUILD);
                continue;
            }
            if (!v9x_d3d_mga_emit(&v9x_d3d_mga_writes)) {
                return 0;
            }
            emitted = 1;
        }
    }
    if (emitted) {
        v9x_present_note_submission();
    }
    return 1;
}

/* An offscreen surface Direct3D will render into. The primary and its flip
 * chain are DirectDraw's, at the display's pitch, which is a linearizer
 * pitch in every mode the family sets. */
static int v9x_d3d_mga_is_target(const V9X_DD_SURFACE_LCL *surface)
{
    return (surface->ddsCaps & V9X_DDSCAPS_3DDEVICE) != 0ul &&
        (surface->ddsCaps & (V9X_DDSCAPS_PRIMARYSURFACE |
                             V9X_DDSCAPS_FLIP | V9X_DDSCAPS_BACKBUFFER |
                             V9X_DDSCAPS_TEXTURE |
                             V9X_DDSCAPS_ZBUFFER)) == 0ul;
}

/*
 * A batch, timed where the CPU has a TSC (ddhal_internal.h). The buckets
 * are named for the Gen3 ring and mean, here: EngineDraw the whole batch,
 * Decode the triangle setup and the register builder, RingWait the FIFO
 * polls and RingWrite the register writes.
 */
static int v9x_d3d_mga_draw(const V9X_R3D_DRAW *draw,
                            const V9X_R3D_VERTEX *vertices,
                            DWORD triangle_count)
{
    DWORD started = V9X_TIME_BEGIN();
    int result = v9x_d3d_mga_draw_batch(draw, vertices, triangle_count);

    V9X_TIME_END(V9X_TIME_ENGINE_DRAW, started);
    return result;
}

/*
 * Surfaces at the layouts the engine reads.
 *
 * A render target, or a Z buffer, at the narrowest linearizer pitch its
 * width fits: the engine draws only at those pitches without ylin, which
 * is not measured, and DirectDraw would give a 64-pixel target a 128-byte
 * row (V9XDDP, boot 386: 551 draws refused TARGET_SHAPE). The engine
 * addresses Z at the target's pitch (depth record), so a Z buffer takes
 * the pitch its target of the same width gets; at the display's widths
 * that is the display's pitch. 512-aligned, ZORG's alignment, which also
 * puts a target's origin on the 32-pixel grid YDSTORG needs.
 *
 * A texture, or a mip chain level by level, at a power-of-two row of at
 * least 8 texels on 32 bytes, what TEXCTL and TEXORG can state.
 */
static DWORD v9x_d3d_mga_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list;
    V9X_DD_SURFACE_LCL *surface;
    v9x_u32 offsets[1];
    v9x_u32 pitches[V9X_D3D_MGA_TEXTURE_LEVELS];
    v9x_u32 rows[V9X_D3D_MGA_TEXTURE_LEVELS];
    DWORD format;
    DWORD width;
    DWORD height;
    DWORD level;
    DWORD base;
    DWORD pitch;

    if (v9x_hal == 0 || data == 0 || data->dwSCnt == 0ul ||
        data->lplpSList == 0) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    list = (V9X_DD_SURFACE_LCL **)data->lplpSList;
    surface = list[0];
    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }

    if (data->dwSCnt == 1ul &&
        ((surface->ddsCaps & V9X_DDSCAPS_ZBUFFER) != 0ul ||
         v9x_d3d_mga_is_target(surface))) {
        pitch = v9x_mga_pitch_for_width((DWORD)surface->lpGbl->wWidth) *
                2ul;
        if (pitch == 0ul ||
            ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul &&
             surface->lpGbl->ddpfSurface.dwRGBBitCount != 16ul) ||
            v9x_hal->fb.bits_per_pixel != 16ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        offsets[0] = 0ul;
        if (v9x_d3d_place_block(data, V9X_D3D_MGA_DEPTH_ALIGN, pitch,
                                (DWORD)surface->lpGbl->wHeight, offsets,
                                &base) != 0ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        if ((surface->ddsCaps & V9X_DDSCAPS_ZBUFFER) != 0ul) {
            ++v9x_hal->d3d_diagnostics.z_placed;
            v9x_hal->d3d_diagnostics.z_placed_pitch = pitch;
        }
        data->ddRVal = V9X_DD_OK;
        return V9X_DDHAL_DRIVER_HANDLED;
    }

    if ((surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_ZBUFFER) != 0ul ||
        data->dwSCnt > V9X_D3D_MGA_TEXTURE_LEVELS ||
        !v9x_d3d_mga_texture_format(surface, &format)) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    width = (DWORD)surface->lpGbl->wWidth;
    height = (DWORD)surface->lpGbl->wHeight;
    if (!v9x_d3d_mga_pow2(width) || !v9x_d3d_mga_pow2(height) ||
        width > v9x_d3d_mga_limits.texture_size_max ||
        height > v9x_d3d_mga_limits.texture_size_max) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    for (level = 0ul; level < data->dwSCnt; ++level) {
        const V9X_DD_SURFACE_LCL *member = list[level];
        DWORD edge = width >> level;
        DWORD lines = height >> level;

        if (edge == 0ul) {
            edge = 1ul;
        }
        if (lines == 0ul) {
            lines = 1ul;
        }
        if (member == 0 || member->lpGbl == 0 ||
            (member->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
            (DWORD)member->lpGbl->wWidth != edge ||
            (DWORD)member->lpGbl->wHeight != lines) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        pitches[level] = v9x_d3d_mga_texture_pitch(edge);
        rows[level] = lines;
    }
    if (v9x_d3d_place_each(data, V9X_D3D_MGA_TEXTURE_ALIGN, pitches,
                           rows) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    ++v9x_hal->d3d_diagnostics.texture_placed;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

static void v9x_d3d_mga_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    (void)v9x_d3d_place_release(data, V9X_D3D_MGA_DEPTH_ALIGN);
}

/* Positional: V9X_D3D_ENGINE_OPS is append-only (d3d_internal.h). */
const V9X_D3D_ENGINE_OPS v9x_d3d_engine_mga = {
    &v9x_d3d_mga_limits,
    v9x_d3d_mga_texture_format,
    v9x_d3d_mga_describe_caps,
    0,                                  /* draw_triangles: draw serves */
    v9x_d3d_mga_ready,
    v9x_d3d_mga_create_surface,
    v9x_d3d_mga_destroy_surface,
    v9x_d3d_mga_draw,
    v9x_d3d_mga_accepts
};
