/*
 * ATI 3D Rage IIC (264GT2C) Direct3D engine.
 *
 * Phase 5 of docs\plans\ati-rage-iic-hardware-3d.md. The chip has no setup
 * engine: every triangle is set up on the CPU into trapezoids. All of that
 * - the boundary (v9x_r2_check_draw), the state and the per-triangle
 * packets, the perspective subdivision - is rage2_draw.c, host-tested
 * against the engine models measured on A8U4I5. The translation from the
 * neutral draw is the Mobility-M's (d3d_mach64_map.c), which is pure and
 * names nothing chip-specific beyond the family's texture format numbers.
 * This file resolves DirectDraw surfaces, converts floats, and emits.
 *
 * Emission order is the refusal contract: the policy, the state stream and
 * every vertex are checked first, and only then is the first FIFO slot
 * reserved, so a refused batch writes nothing. Packets are then built and
 * emitted one triangle at a time - a subdivided triangle's packets do not
 * fit a whole batch's worth of static memory - so a triangle the
 * interpolators cannot express is skipped and counted, not refused.
 *
 * Counters are the Mobility-M's m64_* fields: the two engines never share a
 * machine, the refusal reasons are the same numbers, and V9XTRACE reads them.
 */
#include "d3d_internal.h"
#include "d3d_mach64_map.h"
#include "velocity9x/ati_rage2_draw.h"
#include "velocity9x/ati_mach64_engine.h"

/* Bounded waits, matching eng_mach64.c's. */
#define V9X_D3D_RAGE2_SPINS 0x00200000ul

/* The core hands no batch over V9X_D3D_INDEXED_BATCH triangles. */
#define V9X_D3D_RAGE2_MAX_TRIANGLES 64ul

#define V9X_D3D_RAGE2_REFUSE_NOT_READY  1ul
#define V9X_D3D_RAGE2_REFUSE_ARGUMENTS  2ul
#define V9X_D3D_RAGE2_REFUSE_POLICY     3ul
#define V9X_D3D_RAGE2_REFUSE_TEXTURE    4ul
#define V9X_D3D_RAGE2_REFUSE_STATE      5ul
#define V9X_D3D_RAGE2_REFUSE_VERTEX     6ul
#define V9X_D3D_RAGE2_REFUSE_EMIT       7ul

/* Positions in the setup's sixteenths; depth Z16. */
#define V9X_D3D_RAGE2_FIXED_SCALE 16.0f
#define V9X_D3D_RAGE2_Z_SCALE     65535.0f
#define V9X_D3D_RAGE2_Z_MAX       65535l

#define V9X_D3D_RAGE2_PIECE_DWORDS \
    (V9X_R2_DRAW_TRAPS_MAX * V9X_R2_DRAW_TRAP_DWORDS)

static v9x_u32 v9x_d3d_rage2_state_offsets[V9X_R2_DRAW_STATE_DWORDS];
static v9x_u32 v9x_d3d_rage2_state_values[V9X_R2_DRAW_STATE_DWORDS];
static v9x_u32 v9x_d3d_rage2_offsets[V9X_D3D_RAGE2_PIECE_DWORDS];
static v9x_u32 v9x_d3d_rage2_values[V9X_D3D_RAGE2_PIECE_DWORDS];
static struct v9x_r2_draw_vertex
    v9x_d3d_rage2_vertices[V9X_D3D_RAGE2_MAX_TRIANGLES * 3ul];
static struct v9x_r2_draw_vertex
    v9x_d3d_rage2_pieces[V9X_R2_DRAW_SPLIT_MAX * 3u];

/*
 * DST_OFF_PITCH's pitch is eight-pixel units up to 1023 of them, so
 * pitches are 16-byte aligned; 1024 is as wide as a back and Z buffer fit
 * the 4 MiB beside a front buffer; textures are the policy's powers of two
 * from 8 to 256, at a pitch of their width (TEX_SIZE_PITCH has no separate
 * width: 2026-10-02-rage-iic-texture-addressing.md). The setup takes
 * coordinates up to 2047 pixels; the core clips to the target.
 */
static const V9X_D3D_ENGINE_LIMITS v9x_d3d_rage2_limits = {
    16ul,           /* target_bits_per_pixel */
    16368ul,        /* target_pitch_max */
    16ul,           /* target_pitch_align */
    1024ul,         /* target_dimension_max */
    V9X_R2_DRAW_TEXTURE_MIN, /* texture_size_min */
    V9X_R2_DRAW_TEXTURE_MAX, /* texture_size_max */
    2048.0f,        /* coordinate_limit */
    16ul,           /* depth_bits_per_pixel */
    64ul,           /* texture_align */
    1ul,            /* clip_in_core */
    0ul             /* depth_pitch_own */
};

static int v9x_d3d_rage2_refuse(DWORD reason)
{
    ++v9x_hal->d3d_diagnostics.m64_refused;
    v9x_hal->d3d_diagnostics.m64_refuse_last = reason;
    return 0;
}

/* RGB565, ARGB1555, ARGB4444: the three /texmix sampled. */
static int v9x_d3d_rage2_texture_format(const V9X_DD_SURFACE_LCL *surface,
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
 * Exactly the measured boundary (rage2_draw.c). Not claimed:
 * - mip filters: only level 0 is sampled, so a mip-mapped texture draws
 *   from its largest level;
 * - CLAMP: the GT2C wraps, and a batch inside [0, 1] is all that is drawn
 *   for a clamped texture;
 * - MODULATEALPHA: drawn only where texel or vertex alpha is 1;
 * - SUBPIXEL: vertices are snapped to a quarter pixel;
 * - specular, colour keys, dithering.
 * The alpha test is the 1555 alpha mask, which Direct3D cannot express as
 * a cap: the comparisons it serves are published and the rest refused.
 */
static void v9x_d3d_rage2_describe_caps(V9X_DD_SHARED *shared)
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
    tri->dwAlphaCmpCaps =
        V9X_D3DPCMPCAPS_GREATER | V9X_D3DPCMPCAPS_NOTEQUAL |
        V9X_D3DPCMPCAPS_GREATEREQUAL | V9X_D3DPCMPCAPS_ALWAYS;
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
        V9X_D3DPSHADECAPS_FOGFLAT | V9X_D3DPSHADECAPS_FOGGOURAUD;
    /* Non-square maps were measured (T10, T14), so no SQUAREONLY. */
    tri->dwTextureCaps = V9X_D3DPTEXTURECAPS_PERSPECTIVE |
                         V9X_D3DPTEXTURECAPS_POW2 |
                         V9X_D3DPTEXTURECAPS_ALPHA;
    tri->dwTextureFilterCaps = V9X_D3DPTFILTERCAPS_NEAREST |
                               V9X_D3DPTFILTERCAPS_LINEAR;
    tri->dwTextureBlendCaps = V9X_D3DPTBLENDCAPS_DECAL |
                              V9X_D3DPTBLENDCAPS_MODULATE |
                              V9X_D3DPTBLENDCAPS_DECALALPHA |
                              V9X_D3DPTBLENDCAPS_COPY;
    tri->dwTextureAddressCaps = V9X_D3DPTADDRESSCAPS_WRAP;

    shared->d3d_extended_caps.dwSize = sizeof(V9X_D3DHAL_D3DEXTENDEDCAPS);
    shared->d3d_extended_caps.dwMinTextureWidth =
        v9x_d3d_rage2_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureWidth =
        v9x_d3d_rage2_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinTextureHeight =
        v9x_d3d_rage2_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureHeight =
        v9x_d3d_rage2_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMaxStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMinStippleHeight = 0ul;
    shared->d3d_extended_caps.dwMaxStippleHeight = 0ul;
    shared->d3d_global.hwCaps.dwDeviceRenderBitDepth = V9X_DDBD_16;
    shared->d3d_global.hwCaps.dwDeviceZBufferBitDepth = V9X_DDBD_16;

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

/* The same conditions eng_mach64.c's 2D engine requires, for this type. */
static int v9x_d3d_rage2_ready(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_ATI_RAGE2 &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= 0x1000ul;
}

/*
 * The bound texture's format, shape and placement, level 0 only. A
 * surface the sampler cannot read - including one whose pitch is not its
 * width, which TEX_SIZE_PITCH cannot express - comes back with an unknown
 * format, which the policy refuses.
 */
static void v9x_d3d_rage2_resolve_texture(const V9X_R3D_DRAW *draw,
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
    if (offset == 0xfffffffful || (offset & 7ul) != 0ul ||
        surface->lpGbl->lPitch != (LONG)((DWORD)surface->lpGbl->wWidth * 2ul) ||
        !v9x_d3d_rage2_texture_format(surface, &format)) {
        return;
    }
    texture->format = format;
    texture->width = (DWORD)surface->lpGbl->wWidth;
    texture->height = (DWORD)surface->lpGbl->wHeight;
    texture->offset = offset;
    texture->pitch_bytes = (DWORD)surface->lpGbl->lPitch;
    texture->level_offsets[0] = offset;
}

static v9x_u32 v9x_d3d_rage2_log2(v9x_u32 edge)
{
    v9x_u32 log2 = 0ul;

    while (edge > 1ul) {
        edge >>= 1;
        ++log2;
    }
    return log2;
}

/* Passive, as the contract requires. The batch's specular colour and
 * coordinates are unknown here, so draw() checks again. */
static int v9x_d3d_rage2_accepts(const V9X_R3D_DRAW *draw)
{
    V9X_D3D_MACH64_TEXTURE texture;
    struct v9x_m64_draw_request request;
    struct v9x_r2_draw_decision decision;

    if (draw == 0) {
        return 0;
    }
    v9x_d3d_rage2_resolve_texture(draw, &texture);
    v9x_d3d_mach64_map_request(draw, &texture, 0ul, &request);
    request.vertex_alpha_opaque = draw->vertex_alpha_opaque;
    return v9x_r2_check_draw(&request, 0ul, &decision) ==
           V9X_M64_REFUSE_NONE;
}

/* Every tu and tv of the batch inside [0, 1]: there CLAMP is WRAP. */
static v9x_u32 v9x_d3d_rage2_coords_in_unit(const V9X_R3D_VERTEX *vertices,
                                            DWORD count)
{
    DWORD index;

    for (index = 0ul; index < count; ++index) {
        if (!(vertices[index].tu >= 0.0f && vertices[index].tu <= 1.0f &&
              vertices[index].tv >= 0.0f && vertices[index].tv <= 1.0f)) {
            return 0ul;
        }
    }
    return 1ul;
}

/*
 * One vertex to the setup's integers: the position in sixteenths, snapped
 * to a quarter pixel (v9x_r2_snap: split triangles meet their neighbours
 * without a crack), Z16, the colours, and the texture coordinates and 1/w
 * as the perspective fit takes them. The core clipped to the target, so a
 * coordinate outside the setup's range is refused rather than wrapped.
 */
static int v9x_d3d_rage2_vertex(const V9X_R3D_VERTEX *vertex,
                                const V9X_R3D_VERTEX *provoking, int flat,
                                struct v9x_r2_draw_vertex *out)
{
    LONG x;
    LONG y;
    LONG z;

    x = v9x_float_to_long(vertex->sx * V9X_D3D_RAGE2_FIXED_SCALE);
    y = v9x_float_to_long(vertex->sy * V9X_D3D_RAGE2_FIXED_SCALE);
    if (x < 0l || y < 0l || x > V9X_R2_SETUP_COORD_MAX ||
        y > V9X_R2_SETUP_COORD_MAX) {
        return 0;
    }
    z = v9x_float_to_long(vertex->sz * V9X_D3D_RAGE2_Z_SCALE);
    if (z < 0l) {
        z = 0l;
    }
    if (z > V9X_D3D_RAGE2_Z_MAX) {
        z = V9X_D3D_RAGE2_Z_MAX;
    }
    out->x = v9x_r2_snap((v9x_s32)x);
    out->y = v9x_r2_snap((v9x_s32)y);
    if (out->x > V9X_R2_SETUP_COORD_MAX) {
        out->x = V9X_R2_SETUP_COORD_MAX - (V9X_R2_SETUP_COORD_MAX %
                                           V9X_R2_DRAW_SNAP);
    }
    if (out->y > V9X_R2_SETUP_COORD_MAX) {
        out->y = V9X_R2_SETUP_COORD_MAX - (V9X_R2_SETUP_COORD_MAX %
                                           V9X_R2_DRAW_SNAP);
    }
    out->z = (v9x_u32)z;
    out->argb = flat ? provoking->color : vertex->color;
    /* Direct3D's vertex fog factor is the specular alpha. */
    out->fog = (flat ? provoking->specular : vertex->specular) >> 24;
    out->tu = (double)vertex->tu;
    out->tv = (double)vertex->tv;
    out->q = (double)vertex->rhw;
    return 1;
}

static int v9x_d3d_rage2_draw(const V9X_R3D_DRAW *draw,
                              const V9X_R3D_VERTEX *vertices,
                              DWORD triangle_count)
{
    V9X_D3D_MACH64_TEXTURE texture;
    struct v9x_m64_draw_request request;
    struct v9x_r2_draw_decision decision;
    struct v9x_r2_draw_state state;
    struct v9x_m64_engine *core;
    v9x_u32 state_written = 0ul;
    v9x_u32 reason;
    v9x_status status;
    DWORD index;
    DWORD corner;
    DWORD triangles = 0ul;
    int flat;

    if (draw == 0 || vertices == 0 || triangle_count == 0ul ||
        triangle_count > V9X_D3D_RAGE2_MAX_TRIANGLES) {
        return v9x_d3d_rage2_refuse(V9X_D3D_RAGE2_REFUSE_ARGUMENTS);
    }
    if (!v9x_d3d_rage2_ready()) {
        return v9x_d3d_rage2_refuse(V9X_D3D_RAGE2_REFUSE_NOT_READY);
    }

    /* The policy first: nothing below runs for a draw it refuses. */
    v9x_d3d_rage2_resolve_texture(draw, &texture);
    v9x_d3d_mach64_map_request(draw, &texture,
        v9x_d3d_mach64_specular_rgb(vertices, triangle_count * 3ul),
        &request);
    request.vertex_alpha_opaque =
        v9x_d3d_mach64_vertices_opaque(vertices, triangle_count * 3ul);
    reason = v9x_r2_check_draw(&request,
        request.textured != 0ul
            ? v9x_d3d_rage2_coords_in_unit(vertices, triangle_count * 3ul)
            : 0ul,
        &decision);
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
        if (reason == V9X_M64_REFUSE_TEXTURE_SHAPE) {
            v9x_hal->d3d_diagnostics.m64_shape_refused_last_size =
                (request.texture_width & 0xfffful) |
                (request.texture_height << 16);
        }
        return v9x_d3d_rage2_refuse(V9X_D3D_RAGE2_REFUSE_POLICY);
    }

    /* The surfaces. The request's scissor is half-open; the chip's is
     * inclusive. */
    state.target.offset = draw->target.offset;
    state.target.pitch_bytes = draw->target.pitch;
    state.target.width = draw->target.width;
    state.target.height = draw->target.height;
    state.target.vram_bytes = v9x_hal->fb.vram_bytes;
    state.target.scissor_left = request.scissor_left;
    state.target.scissor_top = request.scissor_top;
    state.target.scissor_right = request.scissor_right - 1ul;
    state.target.scissor_bottom = request.scissor_bottom - 1ul;
    state.depth_enable = request.depth_enable;
    state.depth_offset = draw->depth.offset;
    state.depth_pitch_bytes = draw->depth.pitch;
    state.depth_func = request.depth_func;
    state.depth_write = request.depth_write;
    state.textured = request.textured;
    state.texture.offset = texture.offset;
    state.texture.log2_width = v9x_d3d_rage2_log2(texture.width);
    state.texture.log2_height = v9x_d3d_rage2_log2(texture.height);
    state.texture.log2_pitch = state.texture.log2_width;
    state.texture.format = decision.texture_format;
    state.texture.scale_3d_extra = 0ul;
    state.fog_color = draw->fog_color;
    status = v9x_r2_build_draw_state(&state, &decision,
                                     v9x_d3d_rage2_state_offsets,
                                     v9x_d3d_rage2_state_values,
                                     V9X_R2_DRAW_STATE_DWORDS,
                                     &state_written);
    if (status != V9X_STATUS_OK) {
        return v9x_d3d_rage2_refuse(request.textured != 0ul
            ? V9X_D3D_RAGE2_REFUSE_TEXTURE : V9X_D3D_RAGE2_REFUSE_STATE);
    }

    /* Every vertex before any write, so a bad one refuses the batch. */
    flat = request.shade_mode == V9X_R3D_SHADE_FLAT;
    for (index = 0ul; index < triangle_count; ++index) {
        const V9X_R3D_VERTEX *triangle = vertices + index * 3ul;

        for (corner = 0ul; corner < 3ul; ++corner) {
            if (!v9x_d3d_rage2_vertex(&triangle[corner], &triangle[0], flat,
                    &v9x_d3d_rage2_vertices[index * 3ul + corner])) {
                return v9x_d3d_rage2_refuse(V9X_D3D_RAGE2_REFUSE_VERTEX);
            }
        }
    }

    core = v9x_m64_shared_core();
    if (core == 0 || core->quarantined) {
        return v9x_d3d_rage2_refuse(V9X_D3D_RAGE2_REFUSE_NOT_READY);
    }
    /* The full state every batch: no redundant-state skipping until this
     * path has run on the card. */
    if (v9x_m64_emit_batch(core, v9x_d3d_rage2_state_offsets,
                           v9x_d3d_rage2_state_values, state_written,
                           V9X_D3D_RAGE2_SPINS) != V9X_STATUS_OK) {
        return v9x_d3d_rage2_refuse(V9X_D3D_RAGE2_REFUSE_EMIT);
    }
    v9x_present_note_submission();

    for (index = 0ul; index < triangle_count; ++index) {
        v9x_u32 pieces = 0ul;
        v9x_u32 piece;

        status = v9x_r2_split_triangle(&state, &decision,
                                       &v9x_d3d_rage2_vertices[index * 3ul],
                                       v9x_d3d_rage2_pieces, &pieces);
        if (status != V9X_STATUS_OK) {
            ++v9x_hal->d3d_diagnostics.m64_unrenderable;
            continue;
        }
        for (piece = 0ul; piece < pieces; ++piece) {
            v9x_u32 written = 0ul;

            status = v9x_r2_build_piece(&state, &decision,
                                        &v9x_d3d_rage2_pieces[piece * 3ul],
                                        v9x_d3d_rage2_offsets,
                                        v9x_d3d_rage2_values,
                                        V9X_D3D_RAGE2_PIECE_DWORDS, &written,
                                        0, 0);
            if (status == V9X_STATUS_UNSUPPORTED) {
                /* A sliver the interpolators cannot express, or a 1/w
                 * that is not positive. */
                ++v9x_hal->d3d_diagnostics.m64_unrenderable;
                continue;
            }
            if (status != V9X_STATUS_OK) {
                ++v9x_hal->d3d_diagnostics.m64_degenerate;
                continue;
            }
            if (written == 0ul) {
                ++v9x_hal->d3d_diagnostics.m64_degenerate; /* no centre */
                continue;
            }
            if (v9x_m64_emit_batch(core, v9x_d3d_rage2_offsets,
                                   v9x_d3d_rage2_values, written,
                                   V9X_D3D_RAGE2_SPINS) != V9X_STATUS_OK) {
                return v9x_d3d_rage2_refuse(V9X_D3D_RAGE2_REFUSE_EMIT);
            }
        }
        ++triangles;
    }
    ++v9x_hal->d3d_diagnostics.m64_draws;
    v9x_hal->d3d_diagnostics.m64_triangles += triangles;
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

/* One edge of a texture the policy accepts: a power of two in range. */
static int v9x_d3d_rage2_texture_edge(DWORD edge)
{
    return (edge & (edge - 1ul)) == 0ul &&
           edge >= v9x_d3d_rage2_limits.texture_size_min &&
           edge <= v9x_d3d_rage2_limits.texture_size_max;
}

/*
 * A texture the engine can sample, placed at the pitch the sampler reads:
 * its width, 2 bytes a texel. DirectDraw's heap rounds a pitch to
 * dwTextureAlign, which TEX_SIZE_PITCH cannot follow (the Mobility-M met
 * the same, d3d_mach64.c). A mip chain is placed level after level at
 * each level's own width, so the levels stay valid surfaces; only level 0
 * is sampled. Anything else is left to DirectDraw and refused at draw.
 */
static DWORD v9x_d3d_rage2_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list;
    V9X_DD_SURFACE_LCL *surface;
    v9x_u32 offsets[V9X_M64_TEXTURE_LEVELS_MAX];
    v9x_u32 pitches[V9X_M64_TEXTURE_LEVELS_MAX];
    DWORD align = v9x_d3d_rage2_limits.texture_align;
    DWORD format;
    DWORD width;
    DWORD height;
    DWORD bytes = 0ul;
    DWORD level;
    DWORD base;

    if (v9x_hal == 0 || data == 0 || data->dwSCnt == 0ul ||
        data->lplpSList == 0 || data->dwSCnt > V9X_M64_TEXTURE_LEVELS_MAX) {
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
    if (!v9x_d3d_rage2_texture_edge(width) ||
        !v9x_d3d_rage2_texture_edge(height) ||
        !v9x_d3d_rage2_texture_format(surface, &format)) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    for (level = 0ul; level < data->dwSCnt; ++level) {
        const V9X_DD_SURFACE_LCL *next = list[level];
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
        if (next == 0 || next->lpGbl == 0 ||
            (data->dwSCnt > 1ul &&
             (next->ddsCaps & V9X_DDSCAPS_MIPMAP) == 0ul) ||
            (next->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
            (DWORD)next->lpGbl->wWidth != edge ||
            (DWORD)next->lpGbl->wHeight != rows) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
        offsets[level] = (bytes + align - 1ul) & ~(align - 1ul);
        pitches[level] = edge * 2ul;
        bytes = offsets[level] + pitches[level] * rows;
    }
    if (data->dwSCnt == 1ul) {
        if (v9x_d3d_place_block(data, align, pitches[0], height, offsets,
                                &base) != 0ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
    } else if (v9x_d3d_place_chain(data, align, bytes, offsets, pitches,
                                   &base) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    ++v9x_hal->d3d_diagnostics.texture_placed;
    v9x_hal->d3d_diagnostics.texture_placed_bytes += bytes;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

static void v9x_d3d_rage2_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    (void)v9x_d3d_place_release(data, v9x_d3d_rage2_limits.texture_align);
}

/* Positional: V9X_D3D_ENGINE_OPS is append-only (d3d_internal.h). */
const V9X_D3D_ENGINE_OPS v9x_d3d_engine_rage2 = {
    &v9x_d3d_rage2_limits,
    v9x_d3d_rage2_texture_format,
    v9x_d3d_rage2_describe_caps,
    0,                                  /* draw_triangles: draw serves */
    v9x_d3d_rage2_ready,
    v9x_d3d_rage2_create_surface,
    v9x_d3d_rage2_destroy_surface,
    v9x_d3d_rage2_draw,
    v9x_d3d_rage2_accepts
};
