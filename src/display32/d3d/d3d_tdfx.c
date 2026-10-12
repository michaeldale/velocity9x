/*
 * 3dfx VSA-100 Direct3D engine.
 *
 * Step 3 of docs\plans\3dfx-vsa100-d3d.md. The register words come from
 * src\chipsets\tdfx\tdfx_3d.c and the translation from the neutral draw
 * from d3d_tdfx_map.c, both host-tested; the TDFX3D probe drew the same
 * words pixel-exact on the Voodoo4 4500 (docs\decisions\2026-10-12-voodoo4-
 * 3d-engine-draws-pixel-exact.md). This file resolves DirectDraw surfaces,
 * places textures, and writes.
 *
 * Direct writes to the triangle setup unit, no command FIFO: the 3D
 * registers at BAR0 + 200000h, reached through the engine window the
 * 16-bit side maps from the 2D block (+100000h) through TMU0's registers.
 *
 * Emission order is the refusal contract: the mapping and the state words
 * are built first, and only then is the first register written, so a
 * refused batch writes nothing. Each triangle's words are built just before
 * they are written; past the mapping that cannot fail. A timeout during
 * emission is the one way a batch can end part-way.
 *
 * Every batch carries its whole state. 3dfx's Glide reprograms the 3D
 * engine while it holds the card, and the HAL loses V9X_DD_ENGINE_VALID for
 * that time (dd16.c), so nothing here assumes a register kept its value
 * between batches.
 */
#include "d3d_internal.h"
#include "d3d_tdfx_map.h"
#include "velocity9x/tdfx_2d.h"
#include "velocity9x/tdfx_vsa100.h"

/* The core hands no batch over V9X_D3D_INDEXED_BATCH triangles. */
#define V9X_D3D_TDFX_MAX_TRIANGLES 64ul

/* The engine window: the 2D block, then the 3D block 1 MiB on, through
 * TMU0's registers (vsa100_hw16.c, V9X_TDFX_MMIO_BYTES there). */
#define V9X_D3D_TDFX_WINDOW_BYTES 0x00101000ul
#define V9X_D3D_TDFX_3D_WINDOW (V9X_TDFX_3D_BASE - V9X_TDFX_2D_BASE)

/* Bounded waits, matching eng_tdfx.c's. */
#define V9X_D3D_TDFX_SPINS 0x00200000ul
/* Idle is three consecutive clear reads (vsa100-registers.md, status). */
#define V9X_D3D_TDFX_IDLE_READS 3ul
/* status bits 4:0: free PCI FIFO entries, 1Fh empty. Writes go in runs of
 * at most this many, each after waiting for that much room, as the probe
 * did (section 8.1: back-pressure by PCI retry is not established). */
#define V9X_D3D_TDFX_FIFO_MASK 0x0000001ful
#define V9X_D3D_TDFX_FIFO_CHUNK 16ul

/* Textures are placed 16-byte aligned: texBaseAddr is bits 24:4. */
#define V9X_D3D_TDFX_TEXTURE_ALIGN 16ul
#define V9X_D3D_TDFX_TEXEL_BYTES 2ul

/* Fault-trace tag: "TX3D". */
#define V9X_D3D_TDFX_FAULT_3D_IDLE 0x54583344ul

static struct v9x_tdfx_3d_words v9x_d3d_tdfx_state_words;
static struct v9x_tdfx_3d_words v9x_d3d_tdfx_triangle_words;

/*
 * The colour and clip fields hold 12 bits and a linear stride 14 bytes in
 * multiples of 16; textures are square powers of two to 256; the core
 * clips, because the setup unit has not been measured drawing a vertex off
 * the target. Z16 in depth counts, whose full scale is a DDBLT_DEPTHFILL's.
 */
static const V9X_D3D_ENGINE_LIMITS v9x_d3d_tdfx_limits = {
    16ul,           /* target_bits_per_pixel */
    16368ul,        /* target_pitch_max */
    16ul,           /* target_pitch_align */
    2048ul,         /* target_dimension_max */
    1ul,            /* texture_size_min */
    256ul,          /* texture_size_max */
    2048.0f,        /* coordinate_limit */
    16ul,           /* depth_bits_per_pixel */
    V9X_D3D_TDFX_TEXTURE_ALIGN, /* texture_align */
    1ul,            /* clip_in_core */
    1ul,            /* depth_pitch_own: auxBufferStride takes the pitch */
    0ul,            /* depth_fill_shift */
    1ul             /* texture_units */
};

/* Set when the engine fails to go idle; no 3D register is touched again
 * until the next boot, and V9X_DD_ENGINE_VALID goes with it, so the 2D
 * engine stops too and DirectDraw falls back to the CPU. */
static int v9x_d3d_tdfx_quarantined = 0;

static DWORD v9x_d3d_tdfx_read_status(void)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base +
                               V9X_TDFX_2D_STATUS);
}

static void v9x_d3d_tdfx_write(v9x_u32 offset, v9x_u32 value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base +
                        V9X_D3D_TDFX_3D_WINDOW + offset) = value;
}

/*
 * Where surfaces start in video memory: the framebuffer the driver maps
 * begins at the desktop, which the 16-bit side places above the memory
 * Glide is given, and BAR1 is 128 MiB aligned (eng_tdfx.c does the same).
 */
static DWORD v9x_d3d_tdfx_vram_base(void)
{
    return v9x_hal->fb.physical_base & (V9X_TDFX_LFB_BAR_BYTES - 1ul);
}

static void v9x_d3d_tdfx_quarantine(DWORD status)
{
    if (v9x_hal->engine.idle_timeouts++ == 0ul) {
        v9x_trace_flush_fault(V9X_D3D_TDFX_FAULT_3D_IDLE, status);
    }
    v9x_d3d_tdfx_quarantined = 1;
    v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
}

/* nopCMD, then three consecutive reads with status bit 9 clear (section
 * 8.5). */
static int v9x_d3d_tdfx_wait_idle(void)
{
    DWORD spins;
    DWORD clear = 0ul;
    DWORD status = 0ul;

    v9x_d3d_tdfx_write(V9X_TDFX_3D_NOP_CMD, 0ul);
    for (spins = 0ul; spins < V9X_D3D_TDFX_SPINS; ++spins) {
        status = v9x_d3d_tdfx_read_status();
        if (status == 0xfffffffful) {
            break;
        }
        if (v9x_tdfx_status_busy(status) == 0ul) {
            if (++clear >= V9X_D3D_TDFX_IDLE_READS) {
                return 1;
            }
        } else {
            clear = 0ul;
        }
    }
    v9x_d3d_tdfx_quarantine(status);
    return 0;
}

static int v9x_d3d_tdfx_wait_fifo(DWORD entries)
{
    DWORD spins;
    DWORD status = 0ul;

    for (spins = 0ul; spins < V9X_D3D_TDFX_SPINS; ++spins) {
        status = v9x_d3d_tdfx_read_status();
        if (status == 0xfffffffful) {
            break;
        }
        if ((status & V9X_D3D_TDFX_FIFO_MASK) >= entries) {
            return 1;
        }
    }
    v9x_d3d_tdfx_quarantine(status);
    return 0;
}

static int v9x_d3d_tdfx_emit(const struct v9x_tdfx_3d_words *words)
{
    v9x_u32 index;

    for (index = 0u; index < words->count; ++index) {
        if ((index % V9X_D3D_TDFX_FIFO_CHUNK) == 0u &&
            !v9x_d3d_tdfx_wait_fifo(V9X_D3D_TDFX_FIFO_CHUNK)) {
            return 0;
        }
        v9x_d3d_tdfx_write(words->offsets[index], words->values[index]);
    }
    return 1;
}

/* The three 16-bit formats the builder samples, by their masks. */
static int v9x_d3d_tdfx_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                       DWORD *format_out)
{
    const V9X_DDPIXELFORMAT *format;

    if (format_out != 0) {
        *format_out = V9X_D3D_TDFX_TEXTURE_UNKNOWN;
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
    if ((format->dwFlags & V9X_DDPF_ALPHAPIXELS) == 0ul) {
        if (format->dwRBitMask == 0x0000f800ul &&
            format->dwGBitMask == 0x000007e0ul &&
            format->dwBBitMask == 0x0000001ful) {
            *format_out = V9X_TDFX_TEX_RGB565;
            return 1;
        }
        return 0;
    }
    if (format->dwRBitMask == 0x00007c00ul &&
        format->dwGBitMask == 0x000003e0ul &&
        format->dwBBitMask == 0x0000001ful &&
        format->dwRGBAlphaBitMask == 0x00008000ul) {
        *format_out = V9X_TDFX_TEX_ARGB1555;
        return 1;
    }
    if (format->dwRBitMask == 0x00000f00ul &&
        format->dwGBitMask == 0x000000f0ul &&
        format->dwBBitMask == 0x0000000ful &&
        format->dwRGBAlphaBitMask == 0x0000f000ul) {
        *format_out = V9X_TDFX_TEX_ARGB4444;
        return 1;
    }
    return 0;
}

/*
 * What the builder expresses and d3d_tdfx_map.c accepts: Z and alpha tests
 * with every function, the blend factors of the 16-bit target, Gouraud,
 * square textures and their mip chains with replace and modulate. No fog,
 * no specular, no colour key; those are refused per draw. No blend between
 * levels: LINEARMIPNEAREST and LINEARMIPLINEAR are not advertised.
 */
static void v9x_d3d_tdfx_describe_caps(V9X_DD_SHARED *shared)
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
    tri->dwRasterCaps = V9X_D3DPRASTERCAPS_ZTEST;
    tri->dwZCmpCaps =
        V9X_D3DPCMPCAPS_NEVER | V9X_D3DPCMPCAPS_LESS |
        V9X_D3DPCMPCAPS_EQUAL | V9X_D3DPCMPCAPS_LESSEQUAL |
        V9X_D3DPCMPCAPS_GREATER | V9X_D3DPCMPCAPS_NOTEQUAL |
        V9X_D3DPCMPCAPS_GREATEREQUAL | V9X_D3DPCMPCAPS_ALWAYS;
    tri->dwAlphaCmpCaps = tri->dwZCmpCaps;
    tri->dwSrcBlendCaps =
        V9X_D3DPBLENDCAPS_ZERO | V9X_D3DPBLENDCAPS_ONE |
        V9X_D3DPBLENDCAPS_SRCALPHA | V9X_D3DPBLENDCAPS_INVSRCALPHA |
        V9X_D3DPBLENDCAPS_DESTCOLOR | V9X_D3DPBLENDCAPS_INVDESTCOLOR;
    tri->dwDestBlendCaps =
        V9X_D3DPBLENDCAPS_ZERO | V9X_D3DPBLENDCAPS_ONE |
        V9X_D3DPBLENDCAPS_SRCALPHA | V9X_D3DPBLENDCAPS_INVSRCALPHA |
        V9X_D3DPBLENDCAPS_SRCCOLOR | V9X_D3DPBLENDCAPS_INVSRCCOLOR;
    tri->dwShadeCaps =
        V9X_D3DPSHADECAPS_COLORFLATRGB | V9X_D3DPSHADECAPS_COLORGOURAUDRGB |
        V9X_D3DPSHADECAPS_ALPHAFLATBLEND |
        V9X_D3DPSHADECAPS_ALPHAGOURAUDBLEND;
    tri->dwTextureCaps = V9X_D3DPTEXTURECAPS_PERSPECTIVE |
                         V9X_D3DPTEXTURECAPS_POW2 |
                         V9X_D3DPTEXTURECAPS_ALPHA |
                         V9X_D3DPTEXTURECAPS_SQUAREONLY;
    tri->dwTextureFilterCaps = V9X_D3DPTFILTERCAPS_NEAREST |
                               V9X_D3DPTFILTERCAPS_LINEAR |
                               V9X_D3DPTFILTERCAPS_MIPNEAREST |
                               V9X_D3DPTFILTERCAPS_MIPLINEAR;
    tri->dwTextureBlendCaps = V9X_D3DPTBLENDCAPS_DECAL |
                              V9X_D3DPTBLENDCAPS_MODULATE |
                              V9X_D3DPTBLENDCAPS_MODULATEALPHA |
                              V9X_D3DPTBLENDCAPS_COPY;
    tri->dwTextureAddressCaps = V9X_D3DPTADDRESSCAPS_WRAP |
                                V9X_D3DPTADDRESSCAPS_CLAMP;

    shared->d3d_extended_caps.dwSize = sizeof(V9X_D3DHAL_D3DEXTENDEDCAPS);
    shared->d3d_extended_caps.dwMinTextureWidth =
        v9x_d3d_tdfx_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureWidth =
        v9x_d3d_tdfx_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinTextureHeight =
        v9x_d3d_tdfx_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureHeight =
        v9x_d3d_tdfx_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMaxStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMinStippleHeight = 0ul;
    shared->d3d_extended_caps.dwMaxStippleHeight = 0ul;
    shared->d3d_global.hwCaps.dwDeviceRenderBitDepth = V9X_DDBD_16;
    shared->d3d_global.hwCaps.dwDeviceZBufferBitDepth = V9X_DDBD_16;

    /* RGB565, ARGB1555 and ARGB4444: the shared block has three slots. */
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

/* The 2D engine's conditions, the 3D window mapped, and no timeout. While
 * 3dfx's Glide holds the card VALID is clear and this says no. */
static int v9x_d3d_tdfx_ready(void)
{
    return v9x_hal != 0 && !v9x_d3d_tdfx_quarantined &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_TDFX &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_D3D_TDFX_WINDOW_BYTES;
}

/*
 * How many levels of a mip chain the sampler may read from top: level 0
 * and each attached mip level after it whose edge halves, whose rows are
 * packed, and which starts where the level before it ends - the layout
 * create_surface gives a chain and the single texBaseAddr assumes (section
 * 20 p.228). The first level that fails ends the chain, counted in the
 * shared mip-chain fields as the Mach64 counts its own.
 */
static DWORD v9x_d3d_tdfx_chain(const V9X_DD_SURFACE_LCL *top, DWORD offset)
{
    const V9X_DD_SURFACE_LCL *level = top;
    DWORD edge = (DWORD)top->lpGbl->wWidth;
    DWORD count = 1ul;

    ++v9x_hal->d3d_diagnostics.mip_chain_checks;
    while (count <= V9X_TDFX_TEX_LOG2_MAX && edge > 1ul) {
        const V9X_DD_ATTACH_NODE *node =
            (const V9X_DD_ATTACH_NODE *)level->lpAttachList;
        const V9X_DD_SURFACE_LCL *next = 0;
        DWORD next_offset;

        while (node != 0) {
            if (node->object != 0 && node->object != level &&
                (node->object->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul) {
                next = node->object;
                break;
            }
            node = node->next;
        }
        if (next == 0 || next->lpGbl == 0) {
            break;
        }
        offset += edge * edge * V9X_D3D_TDFX_TEXEL_BYTES;
        edge >>= 1;
        if ((DWORD)next->lpGbl->wWidth != edge ||
            (DWORD)next->lpGbl->wHeight != edge ||
            (DWORD)next->lpGbl->lPitch != edge * V9X_D3D_TDFX_TEXEL_BYTES) {
            ++v9x_hal->d3d_diagnostics.mip_gap_shape;
            v9x_hal->d3d_diagnostics.mip_chain_delta =
                ((DWORD)next->lpGbl->lPitch << 16) |
                (DWORD)next->lpGbl->wWidth;
            break;
        }
        next_offset = v9x_surface_offset(next);
        if (next_offset != offset) {
            ++v9x_hal->d3d_diagnostics.mip_chain_gaps;
            v9x_hal->d3d_diagnostics.mip_chain_delta = next_offset;
            break;
        }
        ++count;
        level = next;
    }
    v9x_hal->d3d_diagnostics.mip_chain_levels = count;
    return count;
}

/* The bound texture's format, shape and placement, from its surface; an
 * unknown format when it cannot be sampled, which the mapping refuses. */
static void v9x_d3d_tdfx_resolve_texture(const V9X_R3D_DRAW *draw,
                                         V9X_D3D_TDFX_TEXTURE *texture)
{
    V9X_DD_SURFACE_LCL *surface;
    DWORD format = V9X_D3D_TDFX_TEXTURE_UNKNOWN;
    DWORD offset;

    texture->format = V9X_D3D_TDFX_TEXTURE_UNKNOWN;
    texture->has_alpha = 0;
    texture->width = 0ul;
    texture->height = 0ul;
    texture->offset = 0ul;
    texture->pitch_bytes = 0ul;
    texture->levels = 1ul;

    surface = (V9X_DD_SURFACE_LCL *)draw->texture.object;
    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return;
    }
    offset = v9x_surface_offset(surface);
    if (offset == 0xfffffffful || surface->lpGbl->lPitch <= 0l ||
        !v9x_d3d_tdfx_texture_format(surface, &format)) {
        return;
    }
    texture->format = format;
    texture->has_alpha = format != V9X_TDFX_TEX_RGB565;
    texture->width = (DWORD)surface->lpGbl->wWidth;
    texture->height = (DWORD)surface->lpGbl->wHeight;
    texture->offset = offset;
    texture->pitch_bytes = (DWORD)surface->lpGbl->lPitch;
    if ((surface->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul &&
        texture->pitch_bytes == texture->width * V9X_D3D_TDFX_TEXEL_BYTES) {
        texture->levels = v9x_d3d_tdfx_chain(surface, offset);
    }
}

static DWORD v9x_d3d_tdfx_specular_rgb(const V9X_R3D_VERTEX *vertices,
                                       DWORD count)
{
    DWORD rgb = 0ul;
    DWORD index;

    for (index = 0ul; index < count; ++index) {
        rgb |= vertices[index].specular & 0x00fffffful;
    }
    return rgb;
}

/* A refused draw by reason, in the shared policy counters (V9XTRACE's
 * M64PolicyNN), as the SiS engine counts its own. */
static void v9x_d3d_tdfx_count_refusal(const V9X_R3D_DRAW *draw,
                                       v9x_u32 reason)
{
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;

    diagnostics->m64_policy_last = reason;
    if (reason < 20ul) {
        ++diagnostics->m64_policy_counts[reason];
    }
    if (reason == V9X_D3D_TDFX_REFUSE_TEXTURE_OP && draw->texture.op < 32ul) {
        diagnostics->m64_texop_refused_mask |= 1ul << draw->texture.op;
    }
    if (reason == V9X_D3D_TDFX_REFUSE_BLEND) {
        diagnostics->blend_last_pair =
            (draw->src_blend << 16) | (draw->dst_blend & 0xfffful);
    }
}

/* Passive, as the contract requires: the vertices' specular colour is not
 * known here, so draw() checks it again. */
static int v9x_d3d_tdfx_accepts(const V9X_R3D_DRAW *draw)
{
    V9X_D3D_TDFX_TEXTURE resolved;
    struct v9x_tdfx_3d_state state;
    v9x_u32 reason;

    if (draw == 0 || v9x_hal == 0) {
        return 0;
    }
    v9x_d3d_tdfx_resolve_texture(draw, &resolved);
    reason = v9x_d3d_tdfx_map_draw(draw, &resolved, v9x_d3d_tdfx_vram_base(),
                                   v9x_hal->fb.vram_bytes, 0ul, &state);
    if (reason != V9X_D3D_TDFX_REFUSE_NONE && reason < 20ul) {
        ++v9x_hal->d3d_diagnostics.m64_accept_policy[reason];
    }
    return reason == V9X_D3D_TDFX_REFUSE_NONE;
}

static int v9x_d3d_tdfx_draw(const V9X_R3D_DRAW *draw,
                             const V9X_R3D_VERTEX *vertices,
                             DWORD triangle_count)
{
    V9X_D3D_TDFX_TEXTURE resolved;
    struct v9x_tdfx_3d_state state;
    struct v9x_tdfx_3d_vertex corners[3];
    DWORD index;
    DWORD packets = 0ul;
    v9x_u32 reason;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul ||
        triangle_count > V9X_D3D_TDFX_MAX_TRIANGLES ||
        !v9x_d3d_tdfx_ready()) {
        return 0;
    }

    /* The mapping first: nothing below runs for a draw it refuses. */
    v9x_d3d_tdfx_resolve_texture(draw, &resolved);
    reason = v9x_d3d_tdfx_map_draw(draw, &resolved, v9x_d3d_tdfx_vram_base(),
                                   v9x_hal->fb.vram_bytes,
                                   v9x_d3d_tdfx_specular_rgb(vertices,
                                       triangle_count * 3ul),
                                   &state);
    if (reason != V9X_D3D_TDFX_REFUSE_NONE) {
        v9x_d3d_tdfx_count_refusal(draw, reason);
        return 0;
    }
    if (v9x_tdfx_3d_build_state(&state, &v9x_d3d_tdfx_state_words) !=
        V9X_STATUS_OK) {
        return 0;
    }

    /*
     * A triangle's words are built and written one at a time. Once the
     * mapping and the state have been accepted, building them cannot fail
     * - a fresh word list always has room for one triangle - so this keeps
     * the refusal contract without holding a whole batch, which was 33 KB
     * of the DLL image.
     */
    for (index = 0ul; index < triangle_count; ++index) {
        if (!v9x_d3d_tdfx_triangle(vertices + index * 3ul, draw->shade_mode,
                                   state.texture_enable != 0ul, corners)) {
            continue;
        }
        v9x_d3d_tdfx_triangle_words.count = 0u;
        if (v9x_tdfx_3d_build_triangle(&state, corners,
                                       &v9x_d3d_tdfx_triangle_words) !=
            V9X_STATUS_OK) {
            return 0;
        }
        if (packets == 0ul) {
            /*
             * Idle first: a blit that wrote this batch's texture or target,
             * or a previous batch, finishes before the state changes under
             * it. The busy bit covers every unit (section 8.5).
             */
            if (!v9x_d3d_tdfx_wait_idle() ||
                !v9x_d3d_tdfx_emit(&v9x_d3d_tdfx_state_words)) {
                return 0;
            }
        }
        if (!v9x_d3d_tdfx_emit(&v9x_d3d_tdfx_triangle_words)) {
            return 0;
        }
        ++packets;
    }
    if (packets != 0ul) {
        v9x_present_note_submission();
    }
    return 1;
}

/* One edge the sampler takes: a power of two in range. */
static int v9x_d3d_tdfx_texture_edge(DWORD edge)
{
    return (edge & (edge - 1ul)) == 0ul &&
           edge >= v9x_d3d_tdfx_limits.texture_size_min &&
           edge <= v9x_d3d_tdfx_limits.texture_size_max;
}

/*
 * A texture placed where the sampler reads it: rows packed at two bytes a
 * texel, level 0 16-byte aligned. DirectDraw would round the pitch to
 * dwTextureAlign and the chip assumes a packed level, so a video-memory
 * texture of a format and size this engine samples is placed here, and a
 * chain created in one call in one block, each level straight after the
 * one before with no alignment between: the sampler finds every level
 * from level 0's address (section 20 p.228), and the 2x2 and 1x1 levels
 * are 8 and 2 bytes.
 */
static DWORD v9x_d3d_tdfx_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list;
    V9X_DD_SURFACE_LCL *surface;
    v9x_u32 offsets[V9X_TDFX_TEX_LOG2_MAX + 1ul];
    v9x_u32 pitches[V9X_TDFX_TEX_LOG2_MAX + 1ul];
    DWORD align = V9X_D3D_TDFX_TEXTURE_ALIGN;
    DWORD format;
    DWORD width;
    DWORD bytes = 0ul;
    DWORD level;
    DWORD base;

    if (v9x_hal == 0 || data == 0 || data->dwSCnt == 0ul ||
        data->dwSCnt > V9X_TDFX_TEX_LOG2_MAX + 1ul || data->lplpSList == 0) {
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
    if (!v9x_d3d_tdfx_texture_edge(width) ||
        (DWORD)surface->lpGbl->wHeight != width ||
        !v9x_d3d_tdfx_texture_format(surface, &format)) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }

    for (level = 0ul; level < data->dwSCnt; ++level) {
        const V9X_DD_SURFACE_LCL *member = list[level];
        DWORD edge = width >> level;

        if (edge == 0ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        if (member == 0 || member->lpGbl == 0 ||
            (data->dwSCnt > 1ul &&
             (member->ddsCaps & V9X_DDSCAPS_MIPMAP) == 0ul) ||
            (member->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
            (DWORD)member->lpGbl->wWidth != edge ||
            (DWORD)member->lpGbl->wHeight != edge) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        offsets[level] = bytes;
        pitches[level] = edge * V9X_D3D_TDFX_TEXEL_BYTES;
        bytes = offsets[level] + pitches[level] * edge;
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

static void v9x_d3d_tdfx_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    (void)v9x_d3d_place_release(data, V9X_D3D_TDFX_TEXTURE_ALIGN);
}

/* Positional: V9X_D3D_ENGINE_OPS is append-only (d3d_internal.h). */
const V9X_D3D_ENGINE_OPS v9x_d3d_engine_tdfx = {
    &v9x_d3d_tdfx_limits,
    v9x_d3d_tdfx_texture_format,
    v9x_d3d_tdfx_describe_caps,
    0,                                  /* draw_triangles: draw serves */
    v9x_d3d_tdfx_ready,
    v9x_d3d_tdfx_create_surface,
    v9x_d3d_tdfx_destroy_surface,
    v9x_d3d_tdfx_draw,
    v9x_d3d_tdfx_accepts
};
