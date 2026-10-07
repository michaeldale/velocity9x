/*
 * ATI Rage Mobility-M (Mach64LM) Direct3D engine.
 *
 * Phase 5 of docs\plans\ati-rage-mobility-hardware-3d.md. Everything this
 * engine accepts or emits was decided elsewhere and measured there: the
 * boundary is v9x_m64_check_draw (mach64_policy.c), the state and setup
 * words are v9x_m64_build_draw_state and v9x_m64_build_reused_setup
 * (mach64_draw.c), the translation from the neutral draw is
 * d3d_mach64_map.c, and all four are host-tested. This file resolves
 * DirectDraw surfaces, converts floats, and emits.
 *
 * DORMANT. No ATI chip has an engine descriptor, so the early stamp never
 * names V9X_DD_ENGINE_TYPE_ATI_MACH64, the selector never reaches this
 * table, and the ATI manifest publishes EngineType NONE. The draw path has
 * not run on the Gateway; its first physical run is the Phase 5 D3D gate.
 *
 * Emission order is the refusal contract: the policy, the state stream and
 * every triangle's setup packet are built and validated first, and only
 * then is the first FIFO slot reserved. A refused batch writes nothing. A
 * hardware timeout during emission is the one way a batch can end part-way.
 */
#include "d3d_internal.h"
#include "d3d_mach64_map.h"
#include "velocity9x/ati_mach64_engine.h"

/* Bounded waits, matching eng_mach64.c's. */
#define V9X_D3D_MACH64_SPINS 0x00200000ul

/* The core hands no batch over V9X_D3D_INDEXED_BATCH triangles. */
#define V9X_D3D_MACH64_MAX_TRIANGLES 64ul

#define V9X_D3D_MACH64_REFUSE_NOT_READY  1ul
#define V9X_D3D_MACH64_REFUSE_ARGUMENTS  2ul
#define V9X_D3D_MACH64_REFUSE_POLICY     3ul
#define V9X_D3D_MACH64_REFUSE_TEXTURE    4ul
#define V9X_D3D_MACH64_REFUSE_STATE      5ul
#define V9X_D3D_MACH64_REFUSE_VERTEX     6ul
#define V9X_D3D_MACH64_REFUSE_EMIT       7ul

/* Pixel coordinates are 14.2; depth is Z16. */
#define V9X_D3D_MACH64_FIXED_SCALE 4.0f
#define V9X_D3D_MACH64_Z_SCALE     65535.0f
#define V9X_D3D_MACH64_Z_MAX       65535l

static v9x_u32 v9x_d3d_mach64_state_offsets[V9X_M64_DRAW_STATE_DWORDS];
static v9x_u32 v9x_d3d_mach64_state_values[V9X_M64_DRAW_STATE_DWORDS];
static v9x_u32 v9x_d3d_mach64_setup_offsets[V9X_D3D_MACH64_MAX_TRIANGLES]
                                           [V9X_M64_SETUP_DWORDS];
static v9x_u32 v9x_d3d_mach64_setup_values[V9X_D3D_MACH64_MAX_TRIANGLES]
                                          [V9X_M64_SETUP_DWORDS];
static v9x_u32 v9x_d3d_mach64_setup_counts[V9X_D3D_MACH64_MAX_TRIANGLES];

/*
 * What the hardware can take, from the builders' own limits and the
 * measured boundary: DST_OFF_PITCH's pitch is eight-pixel units up to 1023
 * of them, so pitches are 16-byte aligned; 1024 is the widest mode the
 * Gateway's panel was driven at; and textures have power-of-two edges
 * from 2 to 256, as the policy accepts, bound on V9X_M64_TEXTURE_BASE_ALIGN,
 * each mip level packed after the one before.
 * The core clips, so the setup engine sees only on-target coordinates.
 */
static const V9X_D3D_ENGINE_LIMITS v9x_d3d_mach64_limits = {
    16ul,           /* target_bits_per_pixel */
    16368ul,        /* target_pitch_max */
    16ul,           /* target_pitch_align */
    1024ul,         /* target_dimension_max */
    2ul,            /* texture_size_min */
    256ul,          /* texture_size_max */
    2048.0f,        /* coordinate_limit */
    16ul,           /* depth_bits_per_pixel */
    V9X_M64_TEXTURE_BASE_ALIGN, /* texture_align */
    1ul,            /* clip_in_core */
    0ul             /* depth_pitch_own */
};

/* The counters live in the shared diagnostics block (m64_*), where
 * V9XTRACE.EXE and the fault trace read them. */
static int v9x_d3d_mach64_refuse(DWORD reason)
{
    ++v9x_hal->d3d_diagnostics.m64_refused;
    v9x_hal->d3d_diagnostics.m64_refuse_last = reason;
    return 0;
}

static int v9x_d3d_mach64_texture_format(const V9X_DD_SURFACE_LCL *surface,
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
        *format_out = V9X_M64_TEXTURE_FORMAT_RGB565;
        return 1;
    }
    if ((format->dwFlags & V9X_DDPF_ALPHAPIXELS) == 0ul) {
        return 0;
    }
    if (format->dwRBitMask == 0x00007c00ul &&
        format->dwGBitMask == 0x000003e0ul &&
        format->dwBBitMask == 0x0000001ful &&
        format->dwRGBAlphaBitMask == 0x00008000ul) {
        *format_out = V9X_M64_TEXTURE_FORMAT_ARGB1555;
        return 1;
    }
    if (format->dwRBitMask == 0x00000f00ul &&
        format->dwGBitMask == 0x000000f0ul &&
        format->dwBBitMask == 0x0000000ful &&
        format->dwRGBAlphaBitMask == 0x0000f000ul) {
        *format_out = V9X_M64_TEXTURE_FORMAT_ARGB4444;
        return 1;
    }
    return 0;
}

/*
 * Exactly the measured boundary (mach64_policy.c). Direct3D cannot say
 * "alpha test only with texel alpha" or "fog only untextured", so those two
 * are published and the rest refused per draw, which is Direct3D's
 * established skip-and-count. SUBPIXEL is not claimed: every physical scene
 * used whole-pixel vertices.
 */
static void v9x_d3d_mach64_describe_caps(V9X_DD_SHARED *shared)
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
    tri->dwRasterCaps = V9X_D3DPRASTERCAPS_ZTEST |
                        V9X_D3DPRASTERCAPS_FOGVERTEX;
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
        V9X_D3DPBLENDCAPS_SRCCOLOR | V9X_D3DPBLENDCAPS_INVSRCCOLOR |
        V9X_D3DPBLENDCAPS_SRCALPHA | V9X_D3DPBLENDCAPS_INVSRCALPHA;
    tri->dwShadeCaps =
        V9X_D3DPSHADECAPS_COLORFLATRGB | V9X_D3DPSHADECAPS_COLORGOURAUDRGB |
        V9X_D3DPSHADECAPS_ALPHAFLATBLEND |
        V9X_D3DPSHADECAPS_ALPHAGOURAUDBLEND |
        V9X_D3DPSHADECAPS_FOGFLAT | V9X_D3DPSHADECAPS_FOGGOURAUD |
        /* ALPHA_TST_CNTL SPECULAR_LIGHT_EN: D3DSpecularGouraud and, over
         * a texture, D3DSpecularTex (2026-09-29). */
        V9X_D3DPSHADECAPS_SPECULARFLATRGB |
        V9X_D3DPSHADECAPS_SPECULARGOURAUDRGB;
    /* Not SQUAREONLY since 2026-10-07: rectangles have been placed and
     * sampled since 2026-10-01 (v9x_d3d_mach64_create_chain), and the
     * leftover bit made Direct3D 8 refuse every non-square CreateTexture
     * with INVALIDCALL, which stopped 3DMark 2001 SE. */
    tri->dwTextureCaps = V9X_D3DPTEXTURECAPS_PERSPECTIVE |
                         V9X_D3DPTEXTURECAPS_POW2 |
                         V9X_D3DPTEXTURECAPS_ALPHA;
    /* Level selection, nearest (MIPNEAREST) or bilinear (MIPLINEAR) within
     * the level, and trilinear (LINEARMIPLINEAR, measured by the probe's
     * MipTri and TexM trilinear scenes, 2026-09-29). LINEARMIPNEAREST has
     * no engine function and refuses (mach64_policy.c). */
    tri->dwTextureFilterCaps = V9X_D3DPTFILTERCAPS_NEAREST |
                               V9X_D3DPTFILTERCAPS_LINEAR |
                               V9X_D3DPTFILTERCAPS_MIPNEAREST |
                               V9X_D3DPTFILTERCAPS_MIPLINEAR |
                               V9X_D3DPTFILTERCAPS_LINEARMIPLINEAR;
    tri->dwTextureBlendCaps = V9X_D3DPTBLENDCAPS_DECAL |
                              V9X_D3DPTBLENDCAPS_MODULATE |
                              V9X_D3DPTBLENDCAPS_DECALALPHA |
                              V9X_D3DPTBLENDCAPS_COPY;
    tri->dwTextureAddressCaps = V9X_D3DPTADDRESSCAPS_WRAP |
                                V9X_D3DPTADDRESSCAPS_CLAMP;

    shared->d3d_extended_caps.dwSize = sizeof(V9X_D3DHAL_D3DEXTENDEDCAPS);
    shared->d3d_extended_caps.dwMinTextureWidth =
        v9x_d3d_mach64_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureWidth =
        v9x_d3d_mach64_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinTextureHeight =
        v9x_d3d_mach64_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureHeight =
        v9x_d3d_mach64_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMaxStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMinStippleHeight = 0ul;
    shared->d3d_extended_caps.dwMaxStippleHeight = 0ul;
    shared->d3d_global.hwCaps.dwDeviceRenderBitDepth = V9X_DDBD_16;
    shared->d3d_global.hwCaps.dwDeviceZBufferBitDepth = V9X_DDBD_16;

    /* RGB565, ARGB1555 and ARGB4444: the three item 4 and 7 sampled. */
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

/* The same conditions eng_mach64.c's 2D engine requires. */
static int v9x_d3d_mach64_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_ATI_MACH64 &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= 0x1000ul;
}

/*
 * The bound texture's format, shape and placement, from its surface. A
 * surface that cannot be sampled comes back with an unknown format, which
 * the policy refuses; this reads memory the runtime owns and writes none.
 */
/*
 * How many levels of a mip chain the sampler may read, their offsets into
 * texture->level_offsets from level 1 on.
 *
 * Down the attachments, as the Gen3 and ViRGE walks take them: the next
 * level is the attached surface that is itself a mip level. A level counts
 * only where the layout the builder assumes holds - edge halved, edge*2
 * bytes a row, on the base alignment - which a chain create_surface placed meets by
 * construction and one the heap placed does not. The first level that
 * fails ends the chain; a chain of one is sampled as a plain texture.
 */
static DWORD v9x_d3d_mach64_chain(const V9X_DD_SURFACE_LCL *top,
                                  V9X_D3D_MACH64_TEXTURE *texture)
{
    const V9X_DD_SURFACE_LCL *level = top;
    DWORD width = (DWORD)top->lpGbl->wWidth;
    DWORD height = (DWORD)top->lpGbl->wHeight;
    DWORD count = 1ul;

    /* Counted in the shared mip-chain fields, which V9XTRACE reports: a
     * check per walk, a shape gap for a level whose size or pitch is not
     * the layout's, a chain gap for one off its alignment, and the pitch and
     * width of the last level that ended a walk in mip_chain_delta. */
    ++v9x_hal->d3d_diagnostics.mip_chain_checks;
    if ((DWORD)top->lpGbl->lPitch != width * 2ul) {
        ++v9x_hal->d3d_diagnostics.mip_gap_shape;
        v9x_hal->d3d_diagnostics.mip_chain_delta =
            ((DWORD)top->lpGbl->lPitch << 16) | width;
        return 1ul;
    }
    while (count < V9X_M64_TEXTURE_LEVELS_MAX) {
        const V9X_DD_ATTACH_NODE *node =
            (const V9X_DD_ATTACH_NODE *)level->lpAttachList;
        const V9X_DD_SURFACE_LCL *next = 0;
        DWORD edge = width >> count;
        DWORD rows = height >> count;
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
        /* A rectangle's edges halve to one texel each on their own. */
        if (edge == 0ul) {
            edge = 1ul;
        }
        if (rows == 0ul) {
            rows = 1ul;
        }
        if ((DWORD)next->lpGbl->wWidth != edge ||
            (DWORD)next->lpGbl->wHeight != rows ||
            (DWORD)next->lpGbl->lPitch != edge * 2ul) {
            ++v9x_hal->d3d_diagnostics.mip_gap_shape;
            v9x_hal->d3d_diagnostics.mip_chain_delta =
                ((DWORD)next->lpGbl->lPitch << 16) |
                (DWORD)next->lpGbl->wWidth;
            break;
        }
        offset = v9x_surface_offset(next);
        if (offset == 0xfffffffful ||
            (offset & (v9x_d3d_mach64_limits.texture_align - 1ul)) != 0ul) {
            ++v9x_hal->d3d_diagnostics.mip_chain_gaps;
            v9x_hal->d3d_diagnostics.mip_chain_delta = offset;
            break;
        }
        texture->level_offsets[count++] = offset;
        level = next;
    }
    v9x_hal->d3d_diagnostics.mip_chain_levels = count;
    return count;
}

static void v9x_d3d_mach64_resolve_texture(const V9X_R3D_DRAW *draw,
                                           V9X_D3D_MACH64_TEXTURE *texture)
{
    V9X_DD_SURFACE_LCL *surface;
    DWORD format = 0ul;
    DWORD offset;

    texture->format = V9X_D3D_MACH64_TEXTURE_UNKNOWN;
    texture->width = 0ul;
    texture->height = 0ul;
    texture->levels = 1ul;
    texture->offset = 0ul;
    texture->pitch_bytes = 0ul;

    surface = (V9X_DD_SURFACE_LCL *)draw->texture.object;
    if (surface == 0 || surface->lpGbl == 0) {
        return;
    }
    if ((surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return;
    }
    offset = v9x_surface_offset(surface);
    if (offset == 0xfffffffful || surface->lpGbl->lPitch <= 0l ||
        !v9x_d3d_mach64_texture_format(surface, &format)) {
        return;
    }
    texture->format = format;
    texture->width = (DWORD)surface->lpGbl->wWidth;
    texture->height = (DWORD)surface->lpGbl->wHeight;
    texture->offset = offset;
    texture->pitch_bytes = (DWORD)surface->lpGbl->lPitch;
    texture->level_offsets[0] = offset;
    if ((surface->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul) {
        texture->levels = v9x_d3d_mach64_chain(surface, texture);
    }
}

/* Passive, as the contract requires: no counter, no hardware access. The
 * batch's specular colour is unknown here, so draw() checks again. */
static int v9x_d3d_mach64_accepts(const V9X_R3D_DRAW *draw)
{
    V9X_D3D_MACH64_TEXTURE texture;
    struct v9x_m64_draw_request request;
    struct v9x_m64_draw_decision decision;
    v9x_u32 reason;

    if (draw == 0) {
        return 0;
    }
    v9x_d3d_mach64_resolve_texture(draw, &texture);
    v9x_d3d_mach64_map_request(draw, &texture, 0ul, &request);
    request.vertex_alpha_opaque = draw->vertex_alpha_opaque;
    /* Only opacity is known before the vertices: 255 or unknown. */
    request.vertex_alpha_min = draw->vertex_alpha_opaque != 0ul ? 255ul : 0ul;
    reason = v9x_m64_check_draw(&request, &decision);
    /* A count only: the answer is the same either way. */
    if (reason != V9X_M64_REFUSE_NONE && reason < 20ul) {
        ++v9x_hal->d3d_diagnostics.m64_accept_policy[reason];
    }
    return reason == V9X_M64_REFUSE_NONE;
}

/*
 * One vertex to the setup engine's integers. Flat shading takes the first
 * vertex's colour and fog factor, as Direct3D defines it, and the engine is
 * always in the measured Gouraud setup. The core clipped to the target, so
 * a coordinate outside it is refused rather than wrapped.
 */
static int v9x_d3d_mach64_vertex(const V9X_R3D_VERTEX *vertex,
                                 const V9X_R3D_VERTEX *provoking,
                                 int flat,
                                 struct v9x_m64_setup_vertex *out)
{
    LONG x;
    LONG y;
    LONG z;

    x = v9x_float_to_long(vertex->sx * V9X_D3D_MACH64_FIXED_SCALE);
    y = v9x_float_to_long(vertex->sy * V9X_D3D_MACH64_FIXED_SCALE);
    if (x < 0l || y < 0l ||
        (DWORD)x > V9X_M64_SETUP_COORD_MAX_FIXED ||
        (DWORD)y > V9X_M64_SETUP_COORD_MAX_FIXED) {
        return 0;
    }
    z = v9x_float_to_long(vertex->sz * V9X_D3D_MACH64_Z_SCALE);
    if (z < 0l) {
        z = 0l;
    }
    if (z > V9X_D3D_MACH64_Z_MAX) {
        z = V9X_D3D_MACH64_Z_MAX;
    }
    out->x_fixed = (v9x_u32)x;
    out->y_fixed = (v9x_u32)y;
    out->z16 = (v9x_u32)z;
    out->rhw = vertex->rhw;
    out->s = vertex->tu;
    out->t = vertex->tv;
    out->argb = flat ? provoking->color : vertex->color;
    out->specular = flat ? provoking->specular : vertex->specular;
    return 1;
}

/*
 * A wrapped texture looks the same with a whole number taken off every
 * coordinate, and the engine's coordinate range is finite: on the Gateway
 * a 64-texel texture sampled right at u of 1000.1 and wrong at 10000.1
 * (2026-09-29), between 64,000 and 640,000 texels. So each triangle's
 * coordinates are moved by a whole number. It keeps a long wall of a
 * tunnel, which tiles its texture far along its length, inside the range.
 *
 * Which whole number is not free: the engine's mip level reads the
 * gradient of S*W over the pixel's W, whose error under perspective is the
 * pixel's s itself. Moving s and t to start at their minimum made every
 * error one-signed, and the tunnel's walls 1 to 1.5 levels coarser than
 * Direct3D's; the integer nearest the perspective-correct centroid
 * (v9x_d3d_mach64_wrap_reference) centres them on zero. Clamped
 * coordinates mean what they say and are left alone, error and all.
 */
static float v9x_d3d_mach64_floor(float value)
{
    float whole = (float)v9x_float_to_long(value);

    if (whole > value) {
        whole -= 1.0f;
    }
    return whole;
}

static void v9x_d3d_mach64_wrap_origin(struct v9x_m64_setup_vertex *setup)
{
    float centre_s;
    float centre_t;
    float base_s;
    float base_t;
    DWORD corner;

    /* No positive W draws nothing; setup skips the triangle. */
    if (!v9x_d3d_mach64_wrap_reference(setup, &centre_s, &centre_t)) {
        return;
    }
    /* Past the long range the whole part is not representable anyway. */
    if (centre_s < -1.0e9f || centre_s > 1.0e9f ||
        centre_t < -1.0e9f || centre_t > 1.0e9f) {
        return;
    }
    base_s = v9x_d3d_mach64_floor(centre_s + 0.5f);
    base_t = v9x_d3d_mach64_floor(centre_t + 0.5f);
    for (corner = 0ul; corner < 3ul; ++corner) {
        setup[corner].s -= base_s;
        setup[corner].t -= base_t;
    }
}

static int v9x_d3d_mach64_draw(const V9X_R3D_DRAW *draw,
                               const V9X_R3D_VERTEX *vertices,
                               DWORD triangle_count)
{
    V9X_D3D_MACH64_TEXTURE texture;
    struct v9x_m64_draw_request request;
    struct v9x_m64_draw_decision decision;
    struct v9x_m64_draw_state state;
    struct v9x_m64_setup_vertex setup[3];
    struct v9x_m64_setup_slot setup_slot[3];
    struct v9x_m64_engine *core;
    const V9X_R3D_VERTEX *triangle;
    v9x_u32 state_written = 0ul;
    v9x_u32 reason;
    v9x_status status;
    DWORD index;
    DWORD corner;
    DWORD packets = 0ul;
    int flat;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul ||
        triangle_count > V9X_D3D_MACH64_MAX_TRIANGLES) {
        return v9x_d3d_mach64_refuse(V9X_D3D_MACH64_REFUSE_ARGUMENTS);
    }
    if (!v9x_d3d_mach64_ready()) {
        return v9x_d3d_mach64_refuse(V9X_D3D_MACH64_REFUSE_NOT_READY);
    }

    /* The policy first: nothing below runs for a draw it refuses. */
    v9x_d3d_mach64_resolve_texture(draw, &texture);
    v9x_d3d_mach64_map_request(draw, &texture,
        v9x_d3d_mach64_specular_rgb(vertices, triangle_count * 3ul),
        &request);
    request.vertex_alpha_opaque =
        v9x_d3d_mach64_vertices_opaque(vertices, triangle_count * 3ul);
    request.vertex_alpha_min =
        v9x_d3d_mach64_vertices_alpha_min(vertices, triangle_count * 3ul);
    reason = v9x_m64_check_draw(&request, &decision);
    if (reason == V9X_M64_REFUSE_NONE && decision.alpha_test_dropped != 0ul) {
        /* A test that can discard nothing is not programmed. */
        request.alpha_test_enable = 0ul;
    }
    if (reason != V9X_M64_REFUSE_NONE) {
        v9x_hal->d3d_diagnostics.m64_policy_last = reason;
        if (reason < 20ul) {
            ++v9x_hal->d3d_diagnostics.m64_policy_counts[reason];
        }
        if (reason == V9X_M64_REFUSE_TEXTURE_OP &&
            request.texture_op < 32ul) {
            v9x_hal->d3d_diagnostics.m64_texop_refused_mask |=
                1ul << request.texture_op;
        }
        if (reason == V9X_M64_REFUSE_TEXTURE_ADDRESS) {
            v9x_hal->d3d_diagnostics.m64_address_refused_mask |=
                (draw->texture.address < 8ul
                     ? 1ul << draw->texture.address : 0ul) |
                (draw->texture.wrap_u != 0ul ? 0x100ul : 0ul) |
                (draw->texture.wrap_v != 0ul ? 0x200ul : 0ul) |
                (draw->texture.wrap_either != 0ul ? 0x400ul : 0ul);
            v9x_hal->d3d_diagnostics.m64_address_refused_last_size =
                (request.texture_width & 0xfffful) |
                (request.texture_height << 16);
        }
        if (reason == V9X_M64_REFUSE_TEXTURE_SHAPE) {
            v9x_hal->d3d_diagnostics.m64_shape_refused_last_size =
                (request.texture_width & 0xfffful) |
                (request.texture_height << 16);
        }
        return v9x_d3d_mach64_refuse(V9X_D3D_MACH64_REFUSE_POLICY);
    }

    v9x_d3d_mach64_map_state(draw, &request, &texture,
                             v9x_hal->fb.vram_bytes, &state);
    status = v9x_m64_build_draw_state(&state, &decision,
                                      v9x_d3d_mach64_state_offsets,
                                      v9x_d3d_mach64_state_values,
                                      V9X_M64_DRAW_STATE_DWORDS,
                                      &state_written);
    if (status != V9X_STATUS_OK) {
        return v9x_d3d_mach64_refuse(request.textured != 0ul
            ? V9X_D3D_MACH64_REFUSE_TEXTURE : V9X_D3D_MACH64_REFUSE_STATE);
    }

    /* Every packet before any write, so a bad vertex refuses the batch. */
    flat = request.shade_mode == V9X_R3D_SHADE_FLAT;
    for (corner = 0ul; corner < 3ul; ++corner) {
        setup_slot[corner].known = 0ul;
    }
    for (index = 0ul; index < triangle_count; ++index) {
        triangle = vertices + index * 3ul;
        for (corner = 0ul; corner < 3ul; ++corner) {
            if (!v9x_d3d_mach64_vertex(&triangle[corner], &triangle[0],
                                       flat, &setup[corner])) {
                return v9x_d3d_mach64_refuse(V9X_D3D_MACH64_REFUSE_VERTEX);
            }
        }
        if (request.textured != 0ul &&
            request.texture_address == V9X_R3D_ADDRESS_WRAP) {
            v9x_d3d_mach64_wrap_origin(setup);
        }
        status = v9x_m64_build_reused_setup(setup, request.textured,
                                  (request.fog_enable != 0ul
                                     ? V9X_M64_SETUP_FOG : 0ul) |
                                  (request.specular_enable != 0ul
                                     ? V9X_M64_SETUP_SPECULAR : 0ul),
                                  setup_slot,
                                  v9x_d3d_mach64_setup_offsets[packets],
                                  v9x_d3d_mach64_setup_values[packets],
                                  V9X_M64_SETUP_DWORDS,
                                  &v9x_d3d_mach64_setup_counts[packets]);
        if (status == V9X_STATUS_UNSUPPORTED) {
            ++v9x_hal->d3d_diagnostics.m64_degenerate; /* zero area */
            continue;
        }
        if (status == V9X_STATUS_INVALID_STATE) {
            ++v9x_hal->d3d_diagnostics.m64_unrenderable; /* NaN, W <= 0 */
            continue;
        }
        if (status != V9X_STATUS_OK) {
            return v9x_d3d_mach64_refuse(V9X_D3D_MACH64_REFUSE_VERTEX);
        }
        ++packets;
    }

    core = v9x_m64_shared_core();
    if (core == 0 || core->quarantined) {
        return v9x_d3d_mach64_refuse(V9X_D3D_MACH64_REFUSE_NOT_READY);
    }
    if (packets == 0ul) {
        ++v9x_hal->d3d_diagnostics.m64_draws;
        return 1;
    }

    /*
     * The full state every batch: no redundant-state skipping until this
     * path has run, and TEX_CACHE_FLUSH in every textured state is what the
     * texture-mutation gate proved a CPU upload needs.
     */
    if (v9x_m64_emit_batch(core, v9x_d3d_mach64_state_offsets,
                           v9x_d3d_mach64_state_values, state_written,
                           V9X_D3D_MACH64_SPINS) != V9X_STATUS_OK) {
        return v9x_d3d_mach64_refuse(V9X_D3D_MACH64_REFUSE_EMIT);
    }
    v9x_present_note_submission();
    for (index = 0ul; index < packets; ++index) {
        if (v9x_m64_emit_batch(core, v9x_d3d_mach64_setup_offsets[index],
                               v9x_d3d_mach64_setup_values[index],
                               v9x_d3d_mach64_setup_counts[index],
                               V9X_D3D_MACH64_SPINS) != V9X_STATUS_OK) {
            return v9x_d3d_mach64_refuse(V9X_D3D_MACH64_REFUSE_EMIT);
        }
    }
    ++v9x_hal->d3d_diagnostics.m64_draws;
    v9x_hal->d3d_diagnostics.m64_triangles += packets;
    if (request.textured != 0ul) {
        ++v9x_hal->d3d_diagnostics.m64_texture_draws;
    }
    if (request.depth_enable != 0ul) {
        ++v9x_hal->d3d_diagnostics.m64_depth_draws;
    }
    if (request.blend_enable != 0ul) {
        ++v9x_hal->d3d_diagnostics.m64_blend_draws;
    }
    if (request.fog_enable != 0ul) {
        ++v9x_hal->d3d_diagnostics.m64_fog_draws;
    }
    return 1;
}

/*
 * A texture the engine can sample, placed at the pitch the sampler reads.
 *
 * DirectDraw rounds a texture's pitch to vmiData.dwTextureAlign, which was
 * this engine's 4 KiB base alignment, so its heap gave the Gateway's 8x8
 * texture a 4096-byte pitch (Tex8Pitch=0x1000, 2026-09-29) and the builder,
 * which takes only max(w,h)*2, refused every textured draw. So a lone
 * video-memory texture of a format and size the policy accepts is placed in
 * a block of its own, on V9X_M64_TEXTURE_BASE_ALIGN (4 KiB until
 * 2026-09-29, the only base the Phase 4 scenes sampled), at 2 bytes a texel. Anything else is left to DirectDraw, and is
 * refused at draw time exactly as before. Placements count in the generic
 * texture_placed and texture_placed_bytes.
 *
 * Rectangles as well as squares since 2026-10-01: a row is the width, and a
 * chain's levels halve each edge to one texel on its own.
 *
 * A mip chain created in one call - DirectDraw's list, top first - is
 * placed in one block: level n at edge >> n, edge*2 bytes a row, each level
 * packed after the one before on the same alignment: at 4 KiB a level each,
 * Quake 2's textures did not fit the Gateway's 4 MB. Each level has its own TEX_n_OFF, so the
 * levels need not touch; the per-level pitch is the hypothesis the probe's
 * mip scenes measure (struct v9x_m64_texture_state).
 */
static DWORD v9x_d3d_mach64_create_chain(V9X_DDHAL_CREATESURFACEDATA *data,
                                         DWORD width, DWORD height)
{
    V9X_DD_SURFACE_LCL **list = (V9X_DD_SURFACE_LCL **)data->lplpSList;
    v9x_u32 offsets[V9X_M64_TEXTURE_LEVELS_MAX];
    v9x_u32 pitches[V9X_M64_TEXTURE_LEVELS_MAX];
    DWORD align = v9x_d3d_mach64_limits.texture_align;
    DWORD bytes = 0ul;
    DWORD level;
    DWORD base;

    if (data->dwSCnt > V9X_M64_TEXTURE_LEVELS_MAX) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    for (level = 0ul; level < data->dwSCnt; ++level) {
        const V9X_DD_SURFACE_LCL *surface = list[level];
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
        if (surface == 0 || surface->lpGbl == 0 ||
            (surface->ddsCaps & V9X_DDSCAPS_MIPMAP) == 0ul ||
            (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
            (DWORD)surface->lpGbl->wWidth != edge ||
            (DWORD)surface->lpGbl->wHeight != rows) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        offsets[level] = (bytes + align - 1ul) & ~(align - 1ul);
        pitches[level] = edge * 2ul;
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

/* One edge of a texture the policy accepts: a power of two in range. */
static int v9x_d3d_mach64_texture_edge(DWORD edge)
{
    return (edge & (edge - 1ul)) == 0ul &&
           edge >= v9x_d3d_mach64_limits.texture_size_min &&
           edge <= v9x_d3d_mach64_limits.texture_size_max;
}

static DWORD v9x_d3d_mach64_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list;
    V9X_DD_SURFACE_LCL *surface;
    v9x_u32 offsets[1];
    DWORD format;
    DWORD width;
    DWORD height;
    DWORD pitch;
    DWORD base;

    if (v9x_hal == 0 || data == 0 || data->dwSCnt == 0ul ||
        data->lplpSList == 0) {
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
    if (!v9x_d3d_mach64_texture_edge(width) ||
        !v9x_d3d_mach64_texture_edge(height) ||
        !v9x_d3d_mach64_texture_format(surface, &format)) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    if (data->dwSCnt > 1ul) {
        return v9x_d3d_mach64_create_chain(data, width, height);
    }

    pitch = width * 2ul;
    offsets[0] = 0ul;
    if (v9x_d3d_place_block(data, v9x_d3d_mach64_limits.texture_align,
                            pitch, height, offsets, &base) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    ++v9x_hal->d3d_diagnostics.texture_placed;
    v9x_hal->d3d_diagnostics.texture_placed_bytes += pitch * height;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

static void v9x_d3d_mach64_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    (void)v9x_d3d_place_release(data, v9x_d3d_mach64_limits.texture_align);
}

/* Positional: V9X_D3D_ENGINE_OPS is append-only (d3d_internal.h). */
const V9X_D3D_ENGINE_OPS v9x_d3d_engine_mach64 = {
    &v9x_d3d_mach64_limits,
    v9x_d3d_mach64_texture_format,
    v9x_d3d_mach64_describe_caps,
    0,                                  /* draw_triangles: draw serves */
    v9x_d3d_mach64_ready,
    v9x_d3d_mach64_create_surface,
    v9x_d3d_mach64_destroy_surface,
    v9x_d3d_mach64_draw,
    v9x_d3d_mach64_accepts
};
