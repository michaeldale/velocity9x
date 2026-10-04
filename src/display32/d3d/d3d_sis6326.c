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
    0ul             /* depth_pitch_own */
};

static void v9x_d3d_sis_write(v9x_u32 offset, v9x_u32 value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) =
        value;
}

static DWORD v9x_d3d_sis_read(v9x_u32 offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base +
                               offset);
}

/*
 * Set when the 3D engine fails to go idle; no 3D register is touched again
 * this boot. A hung 3D engine survived the probe's exit until a reboot
 * (textures record), and a V9XDDP run hard-locked A8U4I5 at boot 212 for a
 * reason not yet established, so a stuck engine is left alone rather than
 * fed more work.
 */
static int v9x_d3d_sis_quarantined = 0;

/* 89FCh D1: the 3D engine idle and its queue empty. */
static int v9x_d3d_sis_wait_idle(void)
{
    DWORD spins;

    for (spins = 0ul; spins < V9X_D3D_SIS_SPINS; ++spins) {
        if ((v9x_d3d_sis_read(V9X_SIS3D_STATUS) &
             V9X_SIS3D_STATUS_IDLE_EMPTY) != 0ul) {
            return 1;
        }
    }
    ++v9x_hal->engine.idle_timeouts;
    v9x_d3d_sis_quarantined = 1;
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
 * The measured boundary (textures and shading-and-depth records). Fog,
 * specular and colour keys were not measured and are refused per draw, as
 * the published caps say; so is everything d3d_sis6326_map.c refuses.
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
                        V9X_D3DPRASTERCAPS_SUBPIXEL;
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
        V9X_D3DPSHADECAPS_ALPHAGOURAUDBLEND;
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
    DWORD index;
    DWORD packets = 0ul;
    int textured;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul ||
        triangle_count > V9X_D3D_SIS_MAX_TRIANGLES || !v9x_d3d_sis_ready()) {
        return 0;
    }

    /* The mapping first: nothing below runs for a draw it refuses. */
    v9x_d3d_sis_resolve_texture(draw, &resolved);
    if (v9x_d3d_sis_map_draw(draw, &resolved, v9x_hal->fb.vram_bytes,
                             v9x_d3d_sis_specular_rgb(vertices,
                                                      triangle_count * 3ul),
                             &state, &texture, &textured) !=
        V9X_D3D_SIS_REFUSE_NONE) {
        return 0;
    }
    if (v9x_sis3d_build_state(&state, &v9x_d3d_sis_state_writes) !=
        V9X_STATUS_OK) {
        return 0;
    }
    /* The texture cache is cleared on every textured batch, D4 pulsed:
     * left set while drawing, it hung the engine (textures record). */
    if (textured) {
        texture.clear_cache = 1;
        if (v9x_sis3d_build_texture(&texture, &v9x_d3d_sis_clear_writes) !=
            V9X_STATUS_OK) {
            return 0;
        }
        texture.clear_cache = 0;
        if (v9x_sis3d_build_texture(&texture,
                                    &v9x_d3d_sis_texture_writes) !=
            V9X_STATUS_OK) {
            return 0;
        }
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

    /* Blits that wrote what this batch reads or overwrites finish first. */
    engine2d = v9x_engine32();
    if (engine2d != 0 && engine2d->wait_idle != 0 &&
        !engine2d->wait_idle(1)) {
        return 0;
    }
    if (!v9x_d3d_sis_wait_idle()) {
        return 0;
    }
    v9x_d3d_sis_emit(&v9x_d3d_sis_state_writes);
    if (textured) {
        v9x_d3d_sis_emit(&v9x_d3d_sis_clear_writes);
        v9x_d3d_sis_emit(&v9x_d3d_sis_texture_writes);
    }
    v9x_present_note_submission();
    for (index = 0ul; index < packets; ++index) {
        /* One triangle at a time: the Turbo Queue is off. */
        if (!v9x_d3d_sis_wait_idle()) {
            return 0;
        }
        v9x_d3d_sis_write(V9X_SIS3D_PRIMITIVE,
                          v9x_d3d_sis_primitives[index]);
        v9x_d3d_sis_emit(&v9x_d3d_sis_vertex_writes[index]);
    }
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
