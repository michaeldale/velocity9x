/*
 * ViRGE/DX Direct3D block for V9XHAL.DLL.
 *
 * The whole S3D path lives here: context and texture bookkeeping, render
 * state, the software clipper, the fixed-point triangle setup that feeds the
 * 3D command registers, and the caps this driver publishes for them.
 *
 * Nothing outside this file reaches into it. DriverInit calls v9x_d3d_publish
 * to fill the shared block's D3D fields and callback tables, and DDRAW then
 * calls the V9xD3d* entry points directly. The chipset gate is upstream of
 * both: the 16-bit side nulls lpD3D* and GetDriverInfo for any engine whose
 * engine_caps lack D3D, so a Trio64 never advertises what is implemented here.
 *
 * Every register offset and command bit it uses is in ddhal_internal.h with
 * the rest of the S3 vocabulary.
 */
#include "ddhal_internal.h"

#define V9X_D3D_CONTEXT_COUNT 16u
#define V9X_D3D_TEXTURE_COUNT 256u
#define V9X_D3D_MAX_BATCH_TRIANGLES 256u

typedef struct v9x_d3d_context {
    DWORD active;
    DWORD pid;
    V9X_DD_SURFACE_LCL *target;
    V9X_DD_SURFACE_LCL *zbuffer;
    DWORD target_offset;
    DWORD pitch;
    DWORD width;
    DWORD height;
    DWORD specular_enable;
    DWORD fog_enable;
    DWORD fog_color;
    DWORD alpha_blend_enable;
    DWORD src_blend;
    DWORD dest_blend;
    DWORD texture_handle;
    DWORD texture_min;
    DWORD texture_mag;
    DWORD texture_blend;
    DWORD texture_wrap;
    DWORD texture_border;
} V9X_D3D_CONTEXT;

typedef struct v9x_d3d_texture {
    DWORD active;
    DWORD context;
    void *surface;
} V9X_D3D_TEXTURE;

static V9X_D3D_CONTEXT v9x_d3d_contexts[V9X_D3D_CONTEXT_COUNT];
static V9X_D3D_TEXTURE v9x_d3d_textures[V9X_D3D_TEXTURE_COUNT];
static V9X_D3DHAL_CALLBACKS2 v9x_d3d_callbacks2;

#if V9X_C3_SERVE_D3D_CALLBACKS2
static const BYTE v9x_guid_d3d_callbacks2[16] = {
    0xe1u, 0x84u, 0xa5u, 0x0bu, 0xb6u, 0x70u, 0xd0u, 0x11u,
    0x88u, 0x9du, 0x00u, 0xaau, 0x00u, 0xbbu, 0xb7u, 0x6au
};
#endif

static V9X_D3D_CONTEXT *v9x_d3d_context_from_handle(DWORD handle)
{
    DWORD index;

    for (index = 0ul; index < V9X_D3D_CONTEXT_COUNT; ++index) {
        if ((DWORD)&v9x_d3d_contexts[index] == handle &&
            v9x_d3d_contexts[index].active != 0ul) {
            return &v9x_d3d_contexts[index];
        }
    }
    return 0;
}

static V9X_D3D_TEXTURE *v9x_d3d_texture_from_handle(DWORD handle,
                                                     DWORD context)
{
    DWORD index;

    for (index = 0ul; index < V9X_D3D_TEXTURE_COUNT; ++index) {
        if ((DWORD)&v9x_d3d_textures[index] == handle &&
            v9x_d3d_textures[index].active != 0ul &&
            v9x_d3d_textures[index].context == context) {
            return &v9x_d3d_textures[index];
        }
    }
    return 0;
}

static V9X_DD_SURFACE_LCL *v9x_d3d_surface_lcl(void *surface);

static void v9x_d3d_textures_destroy_context(DWORD context)
{
    DWORD index;

    for (index = 0ul; index < V9X_D3D_TEXTURE_COUNT; ++index) {
        if (v9x_d3d_textures[index].active != 0ul &&
            v9x_d3d_textures[index].context == context) {
            v9x_d3d_textures[index].active = 0ul;
            v9x_d3d_textures[index].context = 0ul;
            v9x_d3d_textures[index].surface = 0;
        }
    }
}

/* Which ViRGE texture format a surface is sampled as. */
#define V9X_TEX_FORMAT_ARGB1555 0
#define V9X_TEX_FORMAT_ARGB4444 1

/*
 * Classify the surface's pixel format.
 *
 * ddpfSurface exists only when the surface carries its own format, which
 * DDRAWISURF_HASPIXELFORMAT reports; without it the surface is in the
 * primary's format, which is RGB565 here and not a format this engine can
 * sample. Reading the field unconditionally would also read past the
 * allocation, since the DDK only allocates it in the differing case.
 */
static int v9x_d3d_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                  DWORD *format_out)
{
    const V9X_DDPIXELFORMAT *pixel;

    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) == 0ul) {
        return 0;
    }
    pixel = &surface->lpGbl->ddpfSurface;
    if ((pixel->dwFlags & V9X_DDPF_RGB) == 0ul ||
        pixel->dwRGBBitCount != 16ul) {
        return 0;
    }
    if (pixel->dwRBitMask == 0x00007c00ul &&
        pixel->dwGBitMask == 0x000003e0ul &&
        pixel->dwBBitMask == 0x0000001ful) {
        *format_out = V9X_TEX_FORMAT_ARGB1555;
        return 1;
    }
    if (pixel->dwRBitMask == 0x00000f00ul &&
        pixel->dwGBitMask == 0x000000f0ul &&
        pixel->dwBBitMask == 0x0000000ful) {
        *format_out = V9X_TEX_FORMAT_ARGB4444;
        return 1;
    }
    return 0;
}

static int v9x_d3d_texture_info(V9X_D3D_CONTEXT *context,
                                DWORD *offset_out, DWORD *size_log_out,
                                int *mipmapped_out, DWORD *format_out)
{
    V9X_D3D_TEXTURE *texture;
    V9X_DD_SURFACE_LCL *surface;
    DWORD size;
    DWORD size_log = 0ul;
    DWORD offset;
    DWORD last_byte;

    if (context->texture_handle == 0ul) {
        return 0;
    }
    texture = v9x_d3d_texture_from_handle(context->texture_handle,
                                           (DWORD)context);
    surface = texture != 0 ? v9x_d3d_surface_lcl(texture->surface) : 0;
    if (surface == 0 || surface->lpGbl == 0) {
        return 0;
    }
    if ((surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return 0;
    }
    if (surface->lpGbl->wWidth != surface->lpGbl->wHeight ||
        surface->lpGbl->wWidth < 4u || surface->lpGbl->wWidth > 512u) {
        return 0;
    }
    if (surface->lpGbl->lPitch != (LONG)surface->lpGbl->wWidth * 2l) {
        return 0;
    }
    if (!v9x_d3d_texture_format(surface, format_out)) {
        return 0;
    }
    size = surface->lpGbl->wWidth;
    while ((1ul << size_log) < size && size_log < 9ul) {
        ++size_log;
    }
    if ((1ul << size_log) != size) {
        return 0;
    }
    offset = v9x_surface_offset(surface);
    last_byte = size * size * 2ul;
    if ((surface->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul) {
        last_byte += last_byte / 3ul;
    }
    if (offset == 0xfffffffful || last_byte > v9x_hal->fb.vram_bytes ||
        offset > v9x_hal->fb.vram_bytes - last_byte) {
        return 0;
    }
    *offset_out = offset;
    *size_log_out = size_log;
    *mipmapped_out = (surface->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul;
    return 1;
}

static int v9x_d3d_triangle(V9X_D3D_CONTEXT *context,
                            const V9X_D3DTLVERTEX *first);
DWORD __stdcall V9xD3dRenderPrimitive(
    V9X_D3DHAL_RENDERPRIMITIVEDATA *data);

static BYTE v9x_d3d_lerp_byte(BYTE first, BYTE second, float amount)
{
    return (BYTE)v9x_float_to_long((float)first +
        ((float)second - (float)first) * amount);
}

static DWORD v9x_d3d_lerp_color(DWORD first, DWORD second, float amount)
{
    return ((DWORD)v9x_d3d_lerp_byte((BYTE)(first >> 24),
                                     (BYTE)(second >> 24), amount) << 24) |
           ((DWORD)v9x_d3d_lerp_byte((BYTE)(first >> 16),
                                     (BYTE)(second >> 16), amount) << 16) |
           ((DWORD)v9x_d3d_lerp_byte((BYTE)(first >> 8),
                                     (BYTE)(second >> 8), amount) << 8) |
           (DWORD)v9x_d3d_lerp_byte((BYTE)first, (BYTE)second, amount);
}

static void v9x_d3d_lerp_vertex(V9X_D3DTLVERTEX *result,
                                const V9X_D3DTLVERTEX *first,
                                const V9X_D3DTLVERTEX *second,
                                float amount)
{
    result->sx = first->sx + (second->sx - first->sx) * amount;
    result->sy = first->sy + (second->sy - first->sy) * amount;
    result->sz = first->sz + (second->sz - first->sz) * amount;
    result->rhw = first->rhw + (second->rhw - first->rhw) * amount;
    result->color = v9x_d3d_lerp_color(first->color, second->color, amount);
    result->specular = v9x_d3d_lerp_color(first->specular,
                                          second->specular, amount);
    result->tu = first->tu + (second->tu - first->tu) * amount;
    result->tv = first->tv + (second->tv - first->tv) * amount;
}

static int v9x_d3d_clip_triangle(const V9X_D3D_CONTEXT *context,
                                 const V9X_D3DTLVERTEX *triangle,
                                 V9X_D3DTLVERTEX *result)
{
    V9X_D3DTLVERTEX buffers[2][8];
    V9X_D3DTLVERTEX *input = buffers[0];
    V9X_D3DTLVERTEX *output = buffers[1];
    DWORD count = 3ul;
    DWORD edge;
    DWORD index;

    for (index = 0ul; index < 3ul; ++index) {
        if (!(triangle[index].sx >= -2048.0f &&
              triangle[index].sx < 2048.0f &&
              triangle[index].sy >= -2048.0f &&
              triangle[index].sy < 2048.0f)) {
            return -1;
        }
        input[index] = triangle[index];
    }
    for (edge = 0ul; edge < 4ul && count != 0ul; ++edge) {
        V9X_D3DTLVERTEX previous = input[count - 1ul];
        int previous_inside;
        DWORD output_count = 0ul;
        float boundary = (edge == 0ul || edge == 2ul) ? 0.0f :
            (edge == 1ul ? (float)(context->width - 1ul) :
                           (float)(context->height - 1ul));

        if (edge < 2ul) {
            previous_inside = edge == 0ul ? previous.sx >= boundary
                                          : previous.sx <= boundary;
        } else {
            previous_inside = edge == 2ul ? previous.sy >= boundary
                                          : previous.sy <= boundary;
        }
        for (index = 0ul; index < count; ++index) {
            V9X_D3DTLVERTEX current = input[index];
            int current_inside;

            if (edge < 2ul) {
                current_inside = edge == 0ul ? current.sx >= boundary
                                             : current.sx <= boundary;
            } else {
                current_inside = edge == 2ul ? current.sy >= boundary
                                             : current.sy <= boundary;
            }
            if (current_inside != previous_inside) {
                float denominator = edge < 2ul
                    ? current.sx - previous.sx : current.sy - previous.sy;
                float numerator = edge < 2ul
                    ? boundary - previous.sx : boundary - previous.sy;

                if (denominator != 0.0f && output_count < 8ul) {
                    v9x_d3d_lerp_vertex(&output[output_count], &previous,
                                        &current, numerator / denominator);
                    if (edge < 2ul) {
                        output[output_count].sx = boundary;
                    } else {
                        output[output_count].sy = boundary;
                    }
                    ++output_count;
                }
            }
            if (current_inside && output_count < 8ul) {
                output[output_count++] = current;
            }
            previous = current;
            previous_inside = current_inside;
        }
        count = output_count;
        {
            V9X_D3DTLVERTEX *swap = input;
            input = output;
            output = swap;
        }
    }
    for (index = 0ul; index < count; ++index) {
        result[index] = input[index];
    }
    return (int)count;
}

static V9X_DD_SURFACE_LCL *v9x_d3d_surface_lcl(void *surface)
{
    V9X_DD_SURFACE_INT *wrapper = (V9X_DD_SURFACE_INT *)surface;

    return wrapper != 0 ? wrapper->lpLcl : 0;
}

static int v9x_d3d_set_target(V9X_D3D_CONTEXT *context, void *surface,
                              void *zbuffer)
{
    V9X_DD_SURFACE_LCL *target = v9x_d3d_surface_lcl(surface);
    V9X_DD_SURFACE_LCL *depth = zbuffer != 0
        ? v9x_d3d_surface_lcl(zbuffer) : 0;
    V9X_DD_SURFACE_GBL *global;
    DWORD offset;
    DWORD last_byte;
    DWORD pitch;
    DWORD width;
    DWORD height;
    int primary;
    int display_layout;

    if (context == 0 || target == 0 || target->lpGbl == 0 ||
        (target->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return 0;
    }
    global = target->lpGbl;
    offset = v9x_surface_offset(target);
    primary = (target->ddsCaps & V9X_DDSCAPS_PRIMARYSURFACE) != 0ul;
    display_layout = (target->ddsCaps &
        (V9X_DDSCAPS_PRIMARYSURFACE | V9X_DDSCAPS_BACKBUFFER)) != 0ul;
    pitch = (DWORD)global->lPitch;
    width = global->wWidth;
    height = global->wHeight;
    /* Low byte: 0x80 marks raw DDRAW metadata; bits 1:0 identify
     * offscreen/primary/backbuffer. The following event records the pitch
     * actually selected after display-layout normalization. */
    v9x_trace_push(V9X_TRACE_D3D_TARGET_LAYOUT,
                   ((pitch & 0xfffful) << 16) |
                   ((v9x_hal->fb.bits_per_pixel & 0xfful) << 8) | 0x80ul |
                   (primary ? 1ul : (display_layout ? 2ul : 0ul)));
    if (display_layout) {
        V9X_DDPIXELFORMAT *format = &v9x_hal->info.vmiData.ddpfDisplay;

        /* DDRAW's primary/flip-chain metadata has varied across the legacy
         * runtime paths. The scanout descriptor is authoritative for these
         * display-sized surfaces: using a stale surface pitch here creates
         * diagonal/striped S3D output and can walk beyond the page. */
        if ((primary && offset != 0ul) ||
            v9x_hal->fb.bits_per_pixel != 16ul ||
            v9x_hal->info.vmiData.lDisplayPitch !=
                (LONG)v9x_hal->fb.pitch ||
            format->dwSize != sizeof(V9X_DDPIXELFORMAT) ||
            (format->dwFlags & V9X_DDPF_RGB) == 0ul ||
            format->dwRGBBitCount != 16ul ||
            format->dwRBitMask != 0x0000f800ul ||
            format->dwGBitMask != 0x000007e0ul ||
            format->dwBBitMask != 0x0000001ful) {
            return 0;
        }
        pitch = v9x_hal->fb.pitch;
        width = v9x_hal->fb.width;
        height = v9x_hal->fb.height;
    }
    v9x_trace_push(V9X_TRACE_D3D_TARGET_LAYOUT,
                   ((pitch & 0xfffful) << 16) |
                   ((v9x_hal->fb.bits_per_pixel & 0xfful) << 8) |
                   (primary ? 1ul : (display_layout ? 2ul : 0ul)));
    if (offset == 0xfffffffful || (!display_layout && global->lPitch <= 0l) ||
        (pitch & 7ul) != 0ul || pitch > 0x00000ff8ul ||
        width == 0ul || width > pitch / 2ul || height == 0ul ||
        width > 2048ul || height > 2048ul) {
        return 0;
    }
    last_byte = (height - 1ul) * pitch + width * 2ul;
    if (last_byte > v9x_hal->fb.vram_bytes ||
        offset > v9x_hal->fb.vram_bytes - last_byte) {
        return 0;
    }
    if (depth != 0) {
        DWORD depth_offset;
        DWORD depth_pitch;
        DWORD depth_last_byte;

        if (depth->lpGbl == 0 ||
            (depth->ddsCaps & V9X_DDSCAPS_ZBUFFER) == 0ul ||
            (depth->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
            depth->lpGbl->lPitch <= 0l || depth->lpGbl->wWidth < width ||
            depth->lpGbl->wHeight < height) {
            return 0;
        }
        depth_offset = v9x_surface_offset(depth);
        depth_pitch = (DWORD)depth->lpGbl->lPitch;
        depth_last_byte = (height - 1ul) * depth_pitch + width * 2ul;
        if ((depth_pitch & 7ul) != 0ul || depth_pitch > 0x00000ff8ul ||
            depth_offset == 0xfffffffful ||
            depth_last_byte > v9x_hal->fb.vram_bytes ||
            depth_offset > v9x_hal->fb.vram_bytes - depth_last_byte) {
            return 0;
        }
    }
    context->target = target;
    context->zbuffer = depth;
    context->target_offset = offset;
    context->pitch = pitch;
    context->width = width;
    context->height = height;
    return 1;
}

DWORD __stdcall V9xD3dContextCreate(V9X_D3DHAL_CONTEXTCREATEDATA *data)
{
    DWORD index;
    V9X_D3D_CONTEXT *context;

    v9x_trace_enter(V9X_TRACE_D3D_CTXCREATE,
                    data != 0 ? data->dwPID : 0ul);
    if (data == 0 || v9x_hal == 0 || data->lpDDS == 0 ||
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) == 0ul ||
        v9x_hal->fb.bits_per_pixel != 16ul) {
        if (data != 0) {
            data->ddrval = 0x80070057ul;
        }
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.context_rejects;
        }
        v9x_trace_exit(V9X_TRACE_D3D_CTXCREATE, 0x80070057ul);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    for (index = 0ul; index < V9X_D3D_CONTEXT_COUNT; ++index) {
        context = &v9x_d3d_contexts[index];
        if (context->active == 0ul) {
            if (!v9x_d3d_set_target(context, data->lpDDS, data->lpDDSZ)) {
                data->ddrval = 0x80070057ul;
                ++v9x_hal->d3d_diagnostics.context_rejects;
                v9x_trace_exit(V9X_TRACE_D3D_CTXCREATE, data->ddrval);
                return V9X_DDHAL_DRIVER_HANDLED;
            }
            context->pid = data->dwPID;
            context->specular_enable = 0ul;
            context->fog_enable = 0ul;
            context->fog_color = 0ul;
            context->alpha_blend_enable = 0ul;
            context->src_blend = V9X_D3DBLEND_SRCALPHA;
            context->dest_blend = V9X_D3DBLEND_INVSRCALPHA;
            context->texture_handle = 0ul;
            context->texture_min = V9X_D3DFILTER_NEAREST;
            context->texture_mag = V9X_D3DFILTER_NEAREST;
            context->texture_blend = V9X_D3DTBLEND_MODULATE;
            context->texture_wrap = 1ul;
            context->texture_border = 0ul;
            context->active = 1ul;
            data->dwhContext = (DWORD)context;
            data->ddrval = V9X_DD_OK;
            ++v9x_hal->d3d_diagnostics.context_creates;
            v9x_trace_exit(V9X_TRACE_D3D_CTXCREATE, data->ddrval);
            return V9X_DDHAL_DRIVER_HANDLED;
        }
    }
    data->ddrval = 0x8007000eul;
    ++v9x_hal->d3d_diagnostics.context_rejects;
    v9x_trace_exit(V9X_TRACE_D3D_CTXCREATE, data->ddrval);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dContextDestroy(V9X_D3DHAL_CONTEXTDESTROYDATA *data)
{
    V9X_D3D_CONTEXT *context;

    context = data != 0 ? v9x_d3d_context_from_handle(data->dwhContext) : 0;
    v9x_trace_enter(V9X_TRACE_D3D_CTXDESTROY,
                    data != 0 ? data->dwhContext : 0ul);
    if (context == 0) {
        if (data != 0) {
            data->ddrval = 0x80070057ul;
        }
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.context_rejects;
        }
        v9x_trace_exit(V9X_TRACE_D3D_CTXDESTROY, 0x80070057ul);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    v9x_d3d_textures_destroy_context(data->dwhContext);
    context->active = 0ul;
    context->pid = 0ul;
    context->target = 0;
    context->zbuffer = 0;
    context->target_offset = 0ul;
    context->pitch = 0ul;
    context->width = 0ul;
    context->height = 0ul;
    context->specular_enable = 0ul;
    context->fog_enable = 0ul;
    context->fog_color = 0ul;
    context->alpha_blend_enable = 0ul;
    context->src_blend = 0ul;
    context->dest_blend = 0ul;
    context->texture_handle = 0ul;
    context->texture_min = 0ul;
    context->texture_mag = 0ul;
    context->texture_blend = 0ul;
    context->texture_wrap = 0ul;
    context->texture_border = 0ul;
    data->ddrval = V9X_DD_OK;
    ++v9x_hal->d3d_diagnostics.context_destroys;
    v9x_trace_exit(V9X_TRACE_D3D_CTXDESTROY, data->ddrval);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dContextDestroyAll(
    V9X_D3DHAL_CONTEXTDESTROYALLDATA *data)
{
    DWORD index;

    v9x_trace_enter(V9X_TRACE_D3D_CTXDESTROYALL,
                    data != 0 ? data->dwPID : 0ul);
    if (data == 0) {
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    for (index = 0ul; index < V9X_D3D_CONTEXT_COUNT; ++index) {
        if (v9x_d3d_contexts[index].active != 0ul &&
            v9x_d3d_contexts[index].pid == data->dwPID) {
            v9x_d3d_textures_destroy_context(
                (DWORD)&v9x_d3d_contexts[index]);
            v9x_d3d_contexts[index].active = 0ul;
            v9x_d3d_contexts[index].pid = 0ul;
            v9x_d3d_contexts[index].target = 0;
            v9x_d3d_contexts[index].zbuffer = 0;
            v9x_d3d_contexts[index].target_offset = 0ul;
            v9x_d3d_contexts[index].pitch = 0ul;
            v9x_d3d_contexts[index].width = 0ul;
            v9x_d3d_contexts[index].height = 0ul;
            v9x_d3d_contexts[index].specular_enable = 0ul;
            v9x_d3d_contexts[index].fog_enable = 0ul;
            v9x_d3d_contexts[index].fog_color = 0ul;
            v9x_d3d_contexts[index].alpha_blend_enable = 0ul;
            v9x_d3d_contexts[index].src_blend = 0ul;
            v9x_d3d_contexts[index].dest_blend = 0ul;
            v9x_d3d_contexts[index].texture_handle = 0ul;
            v9x_d3d_contexts[index].texture_min = 0ul;
            v9x_d3d_contexts[index].texture_mag = 0ul;
            v9x_d3d_contexts[index].texture_blend = 0ul;
            v9x_d3d_contexts[index].texture_wrap = 0ul;
            v9x_d3d_contexts[index].texture_border = 0ul;
        }
    }
    data->ddrval = V9X_DD_OK;
    ++v9x_hal->d3d_diagnostics.context_destroy_alls;
    v9x_trace_exit(V9X_TRACE_D3D_CTXDESTROYALL, data->ddrval);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dTextureCreate(V9X_D3DHAL_TEXTURECREATEDATA *data)
{
    DWORD index;

    v9x_trace_enter(V9X_TRACE_D3D_TEXTURECREATE,
                    data != 0 ? data->dwhContext : 0ul);
    if (data == 0 || data->lpDDS == 0 ||
        v9x_d3d_context_from_handle(data->dwhContext) == 0) {
        if (data != 0) {
            data->ddrval = 0x80070057ul;
        }
        v9x_trace_exit(V9X_TRACE_D3D_TEXTURECREATE, 0x80070057ul);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    for (index = 0ul; index < V9X_D3D_TEXTURE_COUNT; ++index) {
        if (v9x_d3d_textures[index].active == 0ul) {
            v9x_d3d_textures[index].active = 1ul;
            v9x_d3d_textures[index].context = data->dwhContext;
            v9x_d3d_textures[index].surface = data->lpDDS;
            data->dwHandle = (DWORD)&v9x_d3d_textures[index];
            data->ddrval = V9X_DD_OK;
            ++v9x_hal->d3d_diagnostics.texture_creates;
            v9x_trace_exit(V9X_TRACE_D3D_TEXTURECREATE, data->ddrval);
            return V9X_DDHAL_DRIVER_HANDLED;
        }
    }
    data->ddrval = 0x8007000eul;
    v9x_trace_exit(V9X_TRACE_D3D_TEXTURECREATE, data->ddrval);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dTextureDestroy(V9X_D3DHAL_TEXTUREDESTROYDATA *data)
{
    V9X_D3D_TEXTURE *texture;

    v9x_trace_enter(V9X_TRACE_D3D_TEXTUREDESTROY,
                    data != 0 ? data->dwHandle : 0ul);
    texture = data != 0
        ? v9x_d3d_texture_from_handle(data->dwHandle, data->dwhContext) : 0;
    if (texture == 0) {
        if (data != 0) {
            data->ddrval = 0x80070057ul;
        }
        v9x_trace_exit(V9X_TRACE_D3D_TEXTUREDESTROY, 0x80070057ul);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    texture->active = 0ul;
    texture->context = 0ul;
    texture->surface = 0;
    data->ddrval = V9X_DD_OK;
    ++v9x_hal->d3d_diagnostics.texture_destroys;
    v9x_trace_exit(V9X_TRACE_D3D_TEXTUREDESTROY, data->ddrval);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dTextureSwap(V9X_D3DHAL_TEXTURESWAPDATA *data)
{
    V9X_D3D_TEXTURE *first;
    V9X_D3D_TEXTURE *second;
    void *surface;

    v9x_trace_enter(V9X_TRACE_D3D_TEXTURESWAP,
                    data != 0 ? data->dwHandle1 : 0ul);
    first = data != 0
        ? v9x_d3d_texture_from_handle(data->dwHandle1, data->dwhContext) : 0;
    second = data != 0
        ? v9x_d3d_texture_from_handle(data->dwHandle2, data->dwhContext) : 0;
    if (first == 0 || second == 0) {
        if (data != 0) {
            data->ddrval = 0x80070057ul;
        }
        v9x_trace_exit(V9X_TRACE_D3D_TEXTURESWAP, 0x80070057ul);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    surface = first->surface;
    first->surface = second->surface;
    second->surface = surface;
    data->ddrval = V9X_DD_OK;
    ++v9x_hal->d3d_diagnostics.texture_swaps;
    v9x_trace_exit(V9X_TRACE_D3D_TEXTURESWAP, data->ddrval);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dTextureGetSurf(V9X_D3DHAL_TEXTUREGETSURFDATA *data)
{
    V9X_D3D_TEXTURE *texture;

    v9x_trace_enter(V9X_TRACE_D3D_TEXTUREGETSURF,
                    data != 0 ? data->dwHandle : 0ul);
    texture = data != 0
        ? v9x_d3d_texture_from_handle(data->dwHandle, data->dwhContext) : 0;
    if (texture == 0) {
        if (data != 0) {
            data->ddrval = 0x80070057ul;
        }
        v9x_trace_exit(V9X_TRACE_D3D_TEXTUREGETSURF, 0x80070057ul);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    data->lpDDS = (DWORD)texture->surface;
    data->ddrval = V9X_DD_OK;
    ++v9x_hal->d3d_diagnostics.texture_get_surfs;
    v9x_trace_exit(V9X_TRACE_D3D_TEXTUREGETSURF, data->ddrval);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dRenderState(V9X_D3DHAL_RENDERSTATEDATA *data)
{
    V9X_D3D_CONTEXT *context;
    V9X_DD_SURFACE_LCL *exe;
    V9X_D3DSTATE *states;
    DWORD index;

    v9x_trace_enter(V9X_TRACE_D3D_RENDERSTATE,
                    data != 0 ? data->dwCount : 0ul);
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.render_state_calls;
    }
    context = data != 0
        ? v9x_d3d_context_from_handle(data->dwhContext) : 0;
    exe = data != 0 ? v9x_d3d_surface_lcl(data->lpExeBuf) : 0;
    if (context != 0 && exe != 0 && exe->lpGbl != 0 &&
        exe->lpGbl->fpVidMem != 0ul && data->dwCount <= 64ul) {
        states = (V9X_D3DSTATE *)(exe->lpGbl->fpVidMem + data->dwOffset);
        for (index = 0ul; index < data->dwCount; ++index) {
            switch (states[index].type) {
            case V9X_D3DRENDERSTATE_TEXTUREHANDLE:
                context->texture_handle = states[index].argument;
                break;
            case V9X_D3DRENDERSTATE_TEXTUREPERSPECTIVE:
                /* Perspective setup is added after the affine texture gate. */
                break;
            case V9X_D3DRENDERSTATE_WRAPU:
            case V9X_D3DRENDERSTATE_WRAPV:
                context->texture_wrap = states[index].argument != 0ul;
                break;
            case V9X_D3DRENDERSTATE_TEXTUREMAG:
                context->texture_mag = states[index].argument;
                break;
            case V9X_D3DRENDERSTATE_TEXTUREMIN:
                context->texture_min = states[index].argument;
                break;
            case V9X_D3DRENDERSTATE_TEXTUREMAPBLEND:
                context->texture_blend = states[index].argument;
                break;
            case V9X_D3DRENDERSTATE_BORDERCOLOR:
                context->texture_border = states[index].argument;
                break;
            case V9X_D3DRENDERSTATE_SRCBLEND:
                context->src_blend = states[index].argument;
                break;
            case V9X_D3DRENDERSTATE_DESTBLEND:
                context->dest_blend = states[index].argument;
                break;
            case V9X_D3DRENDERSTATE_ALPHABLENDENABLE:
                context->alpha_blend_enable = states[index].argument != 0ul;
                break;
            case V9X_D3DRENDERSTATE_FOGENABLE:
                context->fog_enable = states[index].argument != 0ul;
                break;
            case V9X_D3DRENDERSTATE_SPECULARENABLE:
                context->specular_enable = states[index].argument != 0ul;
                break;
            case V9X_D3DRENDERSTATE_FOGCOLOR:
                context->fog_color = states[index].argument;
                break;
            default:
                break;
            }
        }
    }
    if (data != 0) {
        data->ddrval = V9X_DD_OK;
    }
    v9x_trace_exit(V9X_TRACE_D3D_RENDERSTATE, V9X_DD_OK);
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

static BYTE v9x_d3d_saturating_add_byte(BYTE first, BYTE second)
{
    WORD sum = (WORD)first + (WORD)second;

    return sum > 255u ? 255u : (BYTE)sum;
}

static BYTE v9x_d3d_fog_byte(BYTE color, BYTE fog, BYTE factor)
{
    return (BYTE)(((DWORD)color * factor +
                   (DWORD)fog * (255u - factor) + 127ul) / 255ul);
}

static void v9x_d3d_apply_vertex_color(const V9X_D3D_CONTEXT *context,
                                       V9X_D3DTLVERTEX *vertex)
{
    DWORD color = vertex->color;
    BYTE alpha = (BYTE)(color >> 24);
    BYTE red = (BYTE)(color >> 16);
    BYTE green = (BYTE)(color >> 8);
    BYTE blue = (BYTE)color;

    if (context->specular_enable != 0ul) {
        red = v9x_d3d_saturating_add_byte(
            red, (BYTE)(vertex->specular >> 16));
        green = v9x_d3d_saturating_add_byte(
            green, (BYTE)(vertex->specular >> 8));
        blue = v9x_d3d_saturating_add_byte(blue, (BYTE)vertex->specular);
    }
    if (context->fog_enable != 0ul) {
        BYTE factor = (BYTE)(vertex->specular >> 24);

        red = v9x_d3d_fog_byte(red, (BYTE)(context->fog_color >> 16),
                               factor);
        green = v9x_d3d_fog_byte(green, (BYTE)(context->fog_color >> 8),
                                 factor);
        blue = v9x_d3d_fog_byte(blue, (BYTE)context->fog_color, factor);
    }
    vertex->color = ((DWORD)alpha << 24) | ((DWORD)red << 16) |
                    ((DWORD)green << 8) | (DWORD)blue;
}

#define V9X_D3DOP_TRIANGLE             3u
#define V9X_D3DOP_EXIT                11u
#define V9X_D3DHAL_EXECUTE_OVERRIDE    1ul
#define V9X_D3DHAL_EXECUTE_UNHANDLED   0x00000211ul

DWORD __stdcall V9xD3dExecute(V9X_D3DHAL_EXECUTEDATA *data)
{
    V9X_DD_SURFACE_LCL *exe;
    V9X_D3DINSTRUCTION *instruction;
    BYTE *base;
    DWORD offset;
    DWORD end;
    int one_instruction;

    v9x_trace_enter(V9X_TRACE_D3D_EXECUTE,
                    data != 0 ? data->dwFlags : 0ul);
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.execute_calls;
    }
    exe = data != 0 ? v9x_d3d_surface_lcl(data->lpExeBuf) : 0;
    if (data == 0 || v9x_d3d_context_from_handle(data->dwhContext) == 0 ||
        exe == 0 || exe->lpGbl == 0 || exe->lpGbl->fpVidMem == 0ul ||
        data->deExData.dwInstructionLength > 0x00100000ul) {
        if (data != 0) {
            data->ddrval = 0x80070057ul;
        }
        v9x_trace_exit(V9X_TRACE_D3D_EXECUTE, 0x80070057ul);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    base = (BYTE *)exe->lpGbl->fpVidMem;
    one_instruction = (data->dwFlags & V9X_D3DHAL_EXECUTE_OVERRIDE) != 0ul;
    offset = one_instruction ? data->dwOffset
                             : data->deExData.dwInstructionOffset +
                               data->dwOffset;
    end = data->deExData.dwInstructionOffset +
          data->deExData.dwInstructionLength;
    for (;;) {
        DWORD bytes;

        if (!one_instruction && (offset > end ||
            end - offset < sizeof(V9X_D3DINSTRUCTION))) {
            data->ddrval = 0x80070057ul;
            v9x_trace_exit(V9X_TRACE_D3D_EXECUTE, data->ddrval);
            return V9X_DDHAL_DRIVER_HANDLED;
        }
        instruction = one_instruction ? &data->diInstruction
                                      : (V9X_D3DINSTRUCTION *)(base + offset);
        bytes = (DWORD)instruction->bSize * (DWORD)instruction->wCount;
        if (!one_instruction && bytes > end - offset -
            sizeof(V9X_D3DINSTRUCTION)) {
            data->ddrval = 0x80070057ul;
            v9x_trace_exit(V9X_TRACE_D3D_EXECUTE, data->ddrval);
            return V9X_DDHAL_DRIVER_HANDLED;
        }
        if (instruction->bOpcode == V9X_D3DOP_EXIT) {
            data->ddrval = V9X_DD_OK;
            v9x_trace_exit(V9X_TRACE_D3D_EXECUTE, data->ddrval);
            return V9X_DDHAL_DRIVER_HANDLED;
        }
        if (instruction->bOpcode == V9X_D3DOP_TRIANGLE) {
            V9X_D3DHAL_RENDERPRIMITIVEDATA primitive;

            primitive.dwhContext = data->dwhContext;
            primitive.dwOffset = one_instruction ? data->dwOffset
                : offset + sizeof(V9X_D3DINSTRUCTION);
            primitive.dwStatus = data->dwStatus;
            primitive.lpExeBuf = data->lpExeBuf;
            primitive.dwTLOffset = data->lpTLBuf != 0
                ? 0ul : data->deExData.dwVertexOffset;
            primitive.lpTLBuf = data->lpTLBuf != 0
                ? data->lpTLBuf : data->lpExeBuf;
            primitive.diInstruction = *instruction;
            primitive.ddrval = V9X_DD_OK;
            (void)V9xD3dRenderPrimitive(&primitive);
            if (primitive.ddrval != V9X_DD_OK) {
                data->ddrval = primitive.ddrval;
                v9x_trace_exit(V9X_TRACE_D3D_EXECUTE, data->ddrval);
                return V9X_DDHAL_DRIVER_HANDLED;
            }
        } else {
            data->dwOffset = one_instruction ? data->dwOffset
                : offset - data->deExData.dwInstructionOffset;
            data->ddrval = V9X_D3DHAL_EXECUTE_UNHANDLED;
            v9x_trace_exit(V9X_TRACE_D3D_EXECUTE, data->ddrval);
            return one_instruction ? V9X_DDHAL_DRIVER_NOTHANDLED
                                   : V9X_DDHAL_DRIVER_HANDLED;
        }
        if (one_instruction) {
            data->ddrval = V9X_DD_OK;
            v9x_trace_exit(V9X_TRACE_D3D_EXECUTE, data->ddrval);
            return V9X_DDHAL_DRIVER_HANDLED;
        }
        offset += sizeof(V9X_D3DINSTRUCTION) + bytes;
    }
}

DWORD __stdcall V9xD3dExecuteClipped(
    V9X_D3DHAL_EXECUTECLIPPEDDATA *data)
{
    V9X_D3DHAL_EXECUTEDATA execute;
    DWORD handled;

    if (data == 0) {
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    execute.dwhContext = data->dwhContext;
    execute.dwOffset = data->dwOffset;
    execute.dwFlags = data->dwFlags;
    execute.dwStatus = data->dwStatus;
    execute.deExData = data->deExData;
    execute.lpExeBuf = data->lpExeBuf;
    execute.lpTLBuf = data->lpTLBuf;
    execute.diInstruction = data->diInstruction;
    execute.ddrval = data->ddrval;
    handled = V9xD3dExecute(&execute);
    data->dwOffset = execute.dwOffset;
    data->dwStatus = execute.dwStatus;
    data->ddrval = execute.ddrval;
    return handled;
}

DWORD __stdcall V9xD3dRenderPrimitive(
    V9X_D3DHAL_RENDERPRIMITIVEDATA *data)
{
    V9X_FPU_AREA fpu;
    V9X_D3D_CONTEXT *context;
    V9X_DD_SURFACE_LCL *exe;
    V9X_DD_SURFACE_LCL *tl;
    const V9X_D3DTRIANGLE *triangles;
    const V9X_D3DTLVERTEX *vertices;
    DWORD index;
    int ok = 0;

    v9x_trace_enter(V9X_TRACE_D3D_RENDERPRIM,
                    data != 0
                        ? (((DWORD)data->diInstruction.bOpcode << 24) |
                           ((DWORD)data->diInstruction.bSize << 16) |
                           (DWORD)data->diInstruction.wCount)
                        : 0ul);
    v9x_fpu_save(&fpu);
    context = data != 0 ? v9x_d3d_context_from_handle(data->dwhContext) : 0;
    exe = data != 0 ? v9x_d3d_surface_lcl(data->lpExeBuf) : 0;
    tl = data != 0 ? v9x_d3d_surface_lcl(data->lpTLBuf) : 0;
    (void)v9x_engine_validate_status();
    if (context != 0 && exe != 0 && exe->lpGbl != 0 && tl != 0 &&
        tl->lpGbl != 0 && v9x_engine_status_validated() &&
        data->diInstruction.bOpcode == 3u &&
        data->diInstruction.bSize >= sizeof(V9X_D3DTRIANGLE) &&
        data->diInstruction.wCount <= V9X_D3D_MAX_BATCH_TRIANGLES) {
        triangles = (const V9X_D3DTRIANGLE *)
            (exe->lpGbl->fpVidMem + data->dwOffset);
        vertices = (const V9X_D3DTLVERTEX *)
            (tl->lpGbl->fpVidMem + data->dwTLOffset);
        ok = 1;
        for (index = 0ul; index < data->diInstruction.wCount; ++index) {
            const V9X_D3DTRIANGLE *triangle =
                (const V9X_D3DTRIANGLE *)
                ((const BYTE *)triangles +
                 index * data->diInstruction.bSize);
            V9X_D3DTLVERTEX source[3];
            V9X_D3DTLVERTEX clipped[8];
            int clipped_count;
            int fan;

            source[0] = vertices[triangle->v1];
            source[1] = vertices[triangle->v2];
            source[2] = vertices[triangle->v3];
            v9x_d3d_apply_vertex_color(context, &source[0]);
            v9x_d3d_apply_vertex_color(context, &source[1]);
            v9x_d3d_apply_vertex_color(context, &source[2]);
            clipped_count = v9x_d3d_clip_triangle(context, source, clipped);
            if (clipped_count < 0) {
                v9x_trace_push(V9X_TRACE_D3D_PRIMREJECT,
                               0x20000000ul | index);
                ok = 0;
                break;
            }
            for (fan = 1; fan + 1 < clipped_count; ++fan) {
                V9X_D3DTLVERTEX clipped_triangle[3];

                clipped_triangle[0] = clipped[0];
                clipped_triangle[1] = clipped[fan];
                clipped_triangle[2] = clipped[fan + 1];
                if (!v9x_d3d_triangle(context, clipped_triangle)) {
                    v9x_trace_push(V9X_TRACE_D3D_PRIMREJECT,
                                   0x30000000ul | index);
                    ok = 0;
                    break;
                }
            }
            if (!ok) {
                break;
            }
        }
    }
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.render_primitive_calls;
    }
    if (data != 0) {
        data->ddrval = ok ? V9X_DD_OK : 0x80070057ul;
    }
    v9x_fpu_restore(&fpu);
    v9x_trace_exit(V9X_TRACE_D3D_RENDERPRIM,
                   ok ? V9X_DD_OK : 0x80070057ul);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dSetRenderTarget(
    V9X_D3DHAL_SETRENDERTARGETDATA *data)
{
    V9X_D3D_CONTEXT *context = data != 0
        ? v9x_d3d_context_from_handle(data->dwhContext) : 0;

    v9x_trace_enter(V9X_TRACE_D3D_SETRENDERTARGET,
                    data != 0 ? data->dwhContext : 0ul);
    if (context == 0 ||
        !v9x_d3d_set_target(context, data->lpDDS, data->lpDDSZ)) {
        if (data != 0) {
            data->ddrval = 0x80070057ul;
        }
        v9x_trace_exit(V9X_TRACE_D3D_SETRENDERTARGET, 0x80070057ul);
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    data->ddrval = V9X_DD_OK;
    v9x_trace_exit(V9X_TRACE_D3D_SETRENDERTARGET, data->ddrval);
    return V9X_DDHAL_DRIVER_HANDLED;
}

static LONG v9x_d3d_fixed_12_20(float value)
{
    return v9x_float_to_long(value * 1048576.0f);
}

static WORD v9x_d3d_fixed_8_7(float value)
{
    LONG fixed = v9x_float_to_long(value * 128.0f);

    if (fixed < -32768l) {
        fixed = -32768l;
    } else if (fixed > 32767l) {
        fixed = 32767l;
    }
    return (WORD)fixed;
}

static int v9x_d3d_triangle(V9X_D3D_CONTEXT *context,
                            const V9X_D3DTLVERTEX *first)
{
    const V9X_D3DTLVERTEX *p0 = &first[0];
    const V9X_D3DTLVERTEX *p1 = &first[1];
    const V9X_D3DTLVERTEX *p2 = &first[2];
    const V9X_D3DTLVERTEX *temp;
    LONG i0y, i1y, i2y;
    LONG dy01, dy12, dy02;
    float fdy01, fdy12, fdy02r, fdycc;
    float dxdy01, dxdy12, dxdy02, dx;
    float fdxr;
    float dgdx, dbdx, drdx;
    float dgdy, dbdy, drdy;
    float dadx, dady;
    float dudx = 0.0f, dvdx = 0.0f;
    float dudy = 0.0f, dvdy = 0.0f;
    DWORD color;
    DWORD gs_bs;
    DWORD as_rs;
    DWORD command;
    DWORD texture_offset = 0ul;
    DWORD texture_size_log = 0ul;
    DWORD texture_d = 0ul;
    DWORD texture_level = 0ul;
    BYTE trilinear_alpha = 0u;
    int trilinear_blend = 0;
    int texture_mipmapped = 0;
    DWORD texture_format = V9X_TEX_FORMAT_ARGB1555;
    int textured;

    if (!(p0->sx >= 0.0f && p0->sx <= (float)(context->width - 1ul) &&
          p0->sy >= 0.0f && p0->sy <= (float)(context->height - 1ul) &&
          p1->sx >= 0.0f && p1->sx <= (float)(context->width - 1ul) &&
          p1->sy >= 0.0f && p1->sy <= (float)(context->height - 1ul) &&
          p2->sx >= 0.0f && p2->sx <= (float)(context->width - 1ul) &&
          p2->sy >= 0.0f && p2->sy <= (float)(context->height - 1ul))) {
        return 0;
    }
    if (p2->sy > p1->sy) { temp = p2; p2 = p1; p1 = temp; }
    if (p2->sy > p0->sy) { temp = p2; p2 = p0; p0 = temp; }
    if (p1->sy > p0->sy) { temp = p1; p1 = p0; p0 = temp; }

    i2y = v9x_float_to_long(p2->sy);
    i0y = v9x_float_to_long(p0->sy);
    dy02 = i0y - i2y;
    if (dy02 == 0l || p0->sy == p2->sy) {
        return 1;
    }
    i1y = v9x_float_to_long(p1->sy);
    dy12 = i1y - i2y;
    dy01 = i0y - i1y;
    fdy02r = 1.0f / (p0->sy - p2->sy);
    fdy01 = p0->sy - p1->sy;
    fdy12 = p1->sy - p2->sy;
    fdycc = p0->sy - (float)i0y;
    if (fdycc == 0.0f && dy01 == 0l) {
        if (dy02 <= 1l) {
            return 1;
        }
        --i0y;
        --i1y;
        fdycc = 1.0f;
    }
    dxdy12 = fdy12 != 0.0f ? (p2->sx - p1->sx) / fdy12 : 0.0f;
    dxdy01 = fdy01 != 0.0f ? (p1->sx - p0->sx) / fdy01 : 0.0f;
    dxdy02 = (p2->sx - p0->sx) * fdy02r;
    dx = p1->sx - (fdy01 * dxdy02 + p0->sx);
    if (dx > -0.000002f && dx < 0.000002f) {
        return 1;
    }
    fdxr = dx < 0.0f ? -1.0f / dx : 1.0f / dx;
    dgdy = ((float)((LONG)((p2->color >> 8) & 0xfful) -
                          (LONG)((p0->color >> 8) & 0xfful))) * fdy02r;
    dbdy = ((float)((LONG)(p2->color & 0xfful) -
                          (LONG)(p0->color & 0xfful))) * fdy02r;
    drdy = ((float)((LONG)((p2->color >> 16) & 0xfful) -
                          (LONG)((p0->color >> 16) & 0xfful))) * fdy02r;
    dgdx = ((float)((LONG)((p1->color >> 8) & 0xfful) -
                          (LONG)((p0->color >> 8) & 0xfful)) -
            dgdy * fdy01) * fdxr;
    dbdx = ((float)((LONG)(p1->color & 0xfful) -
                          (LONG)(p0->color & 0xfful)) -
            dbdy * fdy01) * fdxr;
    drdx = ((float)((LONG)((p1->color >> 16) & 0xfful) -
                          (LONG)((p0->color >> 16) & 0xfful)) -
            drdy * fdy01) * fdxr;
    dady = ((float)((LONG)((p2->color >> 24) & 0xfful) -
                          (LONG)((p0->color >> 24) & 0xfful))) * fdy02r;
    dadx = ((float)((LONG)((p1->color >> 24) & 0xfful) -
                          (LONG)((p0->color >> 24) & 0xfful)) -
            dady * fdy01) * fdxr;
    textured = v9x_d3d_texture_info(context, &texture_offset,
                                    &texture_size_log, &texture_mipmapped,
                                    &texture_format);
    if (textured) {
        dudy = (p2->tu - p0->tu) * 134217728.0f * fdy02r;
        dvdy = (p2->tv - p0->tv) * 134217728.0f * fdy02r;
        dudx = ((p1->tu - p0->tu) * 134217728.0f - dudy * fdy01) *
                fdxr;
        dvdx = ((p1->tv - p0->tv) * 134217728.0f - dvdy * fdy01) *
                fdxr;
        if (texture_mipmapped &&
            context->texture_min >= V9X_D3DFILTER_MIPNEAREST) {
            float rho = dudx < 0.0f ? -dudx : dudx;
            float derivative;
            float level_base = 134217728.0f /
                               (float)(1ul << texture_size_log);
            DWORD level = 0ul;

            derivative = dvdx < 0.0f ? -dvdx : dvdx;
            if (derivative > rho) rho = derivative;
            derivative = dudy < 0.0f ? -dudy : dudy;
            if (derivative > rho) rho = derivative;
            derivative = dvdy < 0.0f ? -dvdy : dvdy;
            if (derivative > rho) rho = derivative;
            while (level < texture_size_log && rho >= level_base * 2.0f) {
                level_base *= 2.0f;
                ++level;
            }
            texture_d = level << 27;
            texture_level = level;
            if ((context->texture_min == V9X_D3DFILTER_MIPLINEAR ||
                 context->texture_min == V9X_D3DFILTER_LINEARMIPLINEAR) &&
                level < texture_size_log && rho > level_base) {
                texture_d += (DWORD)v9x_float_to_long(
                    ((rho - level_base) / level_base) * 134217727.0f);
            }
            if (context->texture_min ==
                    V9X_D3DFILTER_LINEARMIPLINEAR &&
                context->alpha_blend_enable == 0ul &&
                level < texture_size_log &&
                (texture_d & 0x07fffffful) != 0ul) {
                trilinear_alpha = (BYTE)v9x_float_to_long(
                    ((float)(texture_d & 0x07fffffful) /
                     134217727.0f) * 255.0f);
                trilinear_blend = 1;
                texture_d = level << 27;
            }
        }
    }
    color = p0->color;

    if (!v9x_wait_idle(1) || !v9x_wait_fifo(9ul, 1)) {
        return 0;
    }
    v9x_mmio_write(V9X_VIRGE_3D_Z_BASE, 0ul);
    v9x_mmio_write(V9X_VIRGE_3D_DEST_BASE, context->target_offset);
    v9x_mmio_write(V9X_VIRGE_3D_CLIP_L_R, context->width - 1ul);
    v9x_mmio_write(V9X_VIRGE_3D_CLIP_T_B, context->height - 1ul);
    v9x_mmio_write(V9X_VIRGE_3D_DEST_SRC_STRIDE, context->pitch << 16);
    v9x_mmio_write(V9X_VIRGE_3D_Z_STRIDE, context->width * 2ul);
    v9x_mmio_write(V9X_VIRGE_3D_TEX_BASE,
                   textured ? texture_offset : 0ul);
    v9x_mmio_write(V9X_VIRGE_3D_TEX_BORDER, context->texture_border);
    v9x_mmio_write(V9X_VIRGE_3D_FADE_COLOR, 0ul);

    if (textured) {
        if (!v9x_wait_fifo(9ul, 1)) {
            return 0;
        }
        v9x_mmio_write(V9X_VIRGE_3D_TBV, 0ul);
        v9x_mmio_write(V9X_VIRGE_3D_TBU, 0ul);
        v9x_mmio_write(V9X_VIRGE_3D_DVDX, (DWORD)v9x_float_to_long(dvdx));
        v9x_mmio_write(V9X_VIRGE_3D_DUDX, (DWORD)v9x_float_to_long(dudx));
        v9x_mmio_write(V9X_VIRGE_3D_DVDY, (DWORD)v9x_float_to_long(dvdy));
        v9x_mmio_write(V9X_VIRGE_3D_DUDY, (DWORD)v9x_float_to_long(dudy));
        v9x_mmio_write(V9X_VIRGE_3D_DS, texture_d);
        v9x_mmio_write(V9X_VIRGE_3D_VS,
            (DWORD)v9x_float_to_long(p0->tv * 134217728.0f));
        v9x_mmio_write(V9X_VIRGE_3D_US,
            (DWORD)v9x_float_to_long(p0->tu * 134217728.0f));
    }
    if (!v9x_wait_fifo(15ul, 1)) {
        return 0;
    }
    /* With AE set, CMD_SET establishes persistent state; the final
     * Y01_Y12 write launches the triangle. */
    command = V9X_VIRGE_3D_CMD_GOURAUD_16_AE;
    if (textured) {
        command |= (texture_format == V9X_TEX_FORMAT_ARGB4444
                        ? V9X_VIRGE_3D_CMD_TEX_ARGB4444
                        : V9X_VIRGE_3D_CMD_TEX_ARGB1555) |
                   (texture_size_log << 8);
        if (context->texture_blend == V9X_D3DTBLEND_MODULATE) {
            command |= V9X_VIRGE_3D_CMD_TEXTURE_LIT |
                       V9X_VIRGE_3D_CMD_TEX_MODULATE;
        } else {
            command |= V9X_VIRGE_3D_CMD_TEXTURE_UNLIT;
        }
        if (texture_mipmapped &&
            context->texture_min == V9X_D3DFILTER_MIPNEAREST) {
            command |= V9X_VIRGE_3D_CMD_MIP_NEAREST;
        } else if (texture_mipmapped &&
                   context->texture_min == V9X_D3DFILTER_MIPLINEAR) {
            command |= V9X_VIRGE_3D_CMD_MIP_LINEAR;
        } else if (texture_mipmapped &&
                   context->texture_min ==
                       V9X_D3DFILTER_LINEARMIPNEAREST) {
            command |= V9X_VIRGE_3D_CMD_LINEAR_MIP_NEAREST;
        } else if (texture_mipmapped &&
                   context->texture_min ==
                       V9X_D3DFILTER_LINEARMIPLINEAR) {
            command |= trilinear_blend
                ? V9X_VIRGE_3D_CMD_LINEAR_MIP_NEAREST
                : V9X_VIRGE_3D_CMD_LINEAR_MIP_LINEAR;
        } else if (context->texture_min == V9X_D3DFILTER_LINEAR ||
                   context->texture_mag == V9X_D3DFILTER_LINEAR) {
            command |= V9X_VIRGE_3D_CMD_FILTER_LINEAR;
        } else {
            command |= V9X_VIRGE_3D_CMD_FILTER_NEAREST;
        }
        if (context->texture_wrap != 0ul) {
            command |= V9X_VIRGE_3D_CMD_TEXTURE_WRAP;
        }
    }
    if (context->alpha_blend_enable != 0ul &&
        context->src_blend == V9X_D3DBLEND_SRCALPHA &&
        context->dest_blend == V9X_D3DBLEND_INVSRCALPHA) {
        command |= V9X_VIRGE_3D_CMD_ALPHA_SOURCE |
                   V9X_VIRGE_3D_CMD_ALPHA_ENABLE;
    }
    v9x_mmio_write(V9X_VIRGE_3D_COMMAND, command);
    gs_bs = (((color >> 8) & 0xfful) << 23) |
            ((color & 0xfful) << 7);
    as_rs = (((color >> 24) & 0xfful) << 23) |
            (((color >> 16) & 0xfful) << 7);
    v9x_mmio_write(V9X_VIRGE_3D_DGDX_DBDX,
                   ((DWORD)v9x_d3d_fixed_8_7(dgdx) << 16) |
                   (DWORD)v9x_d3d_fixed_8_7(dbdx));
    v9x_mmio_write(V9X_VIRGE_3D_DADX_DRDX,
                   ((DWORD)v9x_d3d_fixed_8_7(dadx) << 16) |
                   (DWORD)v9x_d3d_fixed_8_7(drdx));
    v9x_mmio_write(V9X_VIRGE_3D_DGDY_DBDY,
                   ((DWORD)v9x_d3d_fixed_8_7(dgdy) << 16) |
                   (DWORD)v9x_d3d_fixed_8_7(dbdy));
    v9x_mmio_write(V9X_VIRGE_3D_DADY_DRDY,
                   ((DWORD)v9x_d3d_fixed_8_7(dady) << 16) |
                   (DWORD)v9x_d3d_fixed_8_7(drdy));
    v9x_mmio_write(V9X_VIRGE_3D_GS_BS, gs_bs);
    v9x_mmio_write(V9X_VIRGE_3D_AS_RS, as_rs);
    v9x_mmio_write(V9X_VIRGE_3D_DXDY12,
                   (DWORD)v9x_d3d_fixed_12_20(dxdy12));
    v9x_mmio_write(V9X_VIRGE_3D_XEND12,
                   (DWORD)v9x_d3d_fixed_12_20(
                       p1->sx + dxdy12 * (p1->sy - (float)i1y)));
    v9x_mmio_write(V9X_VIRGE_3D_DXDY01,
                   (DWORD)v9x_d3d_fixed_12_20(dxdy01));
    v9x_mmio_write(V9X_VIRGE_3D_XEND01,
                   (DWORD)v9x_d3d_fixed_12_20(p0->sx + dxdy01 * fdycc));
    v9x_mmio_write(V9X_VIRGE_3D_DXDY02,
                   (DWORD)v9x_d3d_fixed_12_20(dxdy02));
    v9x_mmio_write(V9X_VIRGE_3D_XSTART02,
                   (DWORD)v9x_d3d_fixed_12_20(p0->sx + dxdy02 * fdycc));
    v9x_mmio_write(V9X_VIRGE_3D_YSTART, (DWORD)i0y);
    v9x_mmio_write(V9X_VIRGE_3D_Y01_Y12,
                   ((DWORD)dy01 << 16) |
                   (DWORD)(dy12 + (p2->sy == (float)i2y ? 1l : 0l)) |
                   (dx > 0.0f ? 0x80000000ul : 0ul));
    if (trilinear_blend) {
        DWORD second_command = command;

        if (!v9x_wait_idle(1) || !v9x_wait_fifo(9ul, 1)) {
            return 0;
        }
        v9x_mmio_write(V9X_VIRGE_3D_TBV, 0ul);
        v9x_mmio_write(V9X_VIRGE_3D_TBU, 0ul);
        v9x_mmio_write(V9X_VIRGE_3D_DVDX,
                       (DWORD)v9x_float_to_long(dvdx));
        v9x_mmio_write(V9X_VIRGE_3D_DUDX,
                       (DWORD)v9x_float_to_long(dudx));
        v9x_mmio_write(V9X_VIRGE_3D_DVDY,
                       (DWORD)v9x_float_to_long(dvdy));
        v9x_mmio_write(V9X_VIRGE_3D_DUDY,
                       (DWORD)v9x_float_to_long(dudy));
        v9x_mmio_write(V9X_VIRGE_3D_DS, (texture_level + 1ul) << 27);
        v9x_mmio_write(V9X_VIRGE_3D_VS,
            (DWORD)v9x_float_to_long(p0->tv * 134217728.0f));
        v9x_mmio_write(V9X_VIRGE_3D_US,
            (DWORD)v9x_float_to_long(p0->tu * 134217728.0f));
        if (!v9x_wait_fifo(15ul, 1)) {
            return 0;
        }
        second_command &= ~V9X_VIRGE_3D_CMD_TEXTURE_UNLIT;
        second_command |= V9X_VIRGE_3D_CMD_TEXTURE_LIT |
                          V9X_VIRGE_3D_CMD_TEX_MODULATE |
                          V9X_VIRGE_3D_CMD_ALPHA_SOURCE |
                          V9X_VIRGE_3D_CMD_ALPHA_ENABLE;
        v9x_mmio_write(V9X_VIRGE_3D_COMMAND, second_command);
        if (context->texture_blend == V9X_D3DTBLEND_MODULATE) {
            v9x_mmio_write(V9X_VIRGE_3D_DGDX_DBDX,
                           ((DWORD)v9x_d3d_fixed_8_7(dgdx) << 16) |
                           (DWORD)v9x_d3d_fixed_8_7(dbdx));
            v9x_mmio_write(V9X_VIRGE_3D_DGDY_DBDY,
                           ((DWORD)v9x_d3d_fixed_8_7(dgdy) << 16) |
                           (DWORD)v9x_d3d_fixed_8_7(dbdy));
            v9x_mmio_write(V9X_VIRGE_3D_GS_BS, gs_bs);
            v9x_mmio_write(V9X_VIRGE_3D_AS_RS,
                ((DWORD)trilinear_alpha << 23) |
                (((color >> 16) & 0xfful) << 7));
            v9x_mmio_write(V9X_VIRGE_3D_DADX_DRDX,
                           (DWORD)v9x_d3d_fixed_8_7(drdx));
            v9x_mmio_write(V9X_VIRGE_3D_DADY_DRDY,
                           (DWORD)v9x_d3d_fixed_8_7(drdy));
        } else {
            v9x_mmio_write(V9X_VIRGE_3D_DGDX_DBDX, 0ul);
            v9x_mmio_write(V9X_VIRGE_3D_DGDY_DBDY, 0ul);
            v9x_mmio_write(V9X_VIRGE_3D_GS_BS,
                           (255ul << 23) | (255ul << 7));
            v9x_mmio_write(V9X_VIRGE_3D_AS_RS,
                           ((DWORD)trilinear_alpha << 23) |
                           (255ul << 7));
            v9x_mmio_write(V9X_VIRGE_3D_DADX_DRDX, 0ul);
            v9x_mmio_write(V9X_VIRGE_3D_DADY_DRDY, 0ul);
        }
        v9x_mmio_write(V9X_VIRGE_3D_DXDY12,
                       (DWORD)v9x_d3d_fixed_12_20(dxdy12));
        v9x_mmio_write(V9X_VIRGE_3D_XEND12,
                       (DWORD)v9x_d3d_fixed_12_20(
                           p1->sx + dxdy12 * (p1->sy - (float)i1y)));
        v9x_mmio_write(V9X_VIRGE_3D_DXDY01,
                       (DWORD)v9x_d3d_fixed_12_20(dxdy01));
        v9x_mmio_write(V9X_VIRGE_3D_XEND01,
                       (DWORD)v9x_d3d_fixed_12_20(
                           p0->sx + dxdy01 * fdycc));
        v9x_mmio_write(V9X_VIRGE_3D_DXDY02,
                       (DWORD)v9x_d3d_fixed_12_20(dxdy02));
        v9x_mmio_write(V9X_VIRGE_3D_XSTART02,
                       (DWORD)v9x_d3d_fixed_12_20(
                           p0->sx + dxdy02 * fdycc));
        v9x_mmio_write(V9X_VIRGE_3D_YSTART, (DWORD)i0y);
        v9x_mmio_write(V9X_VIRGE_3D_Y01_Y12,
                       ((DWORD)dy01 << 16) |
                       (DWORD)(dy12 +
                           (p2->sy == (float)i2y ? 1l : 0l)) |
                       (dx > 0.0f ? 0x80000000ul : 0ul));
    }
    return 1;
}

DWORD __stdcall V9xD3dDrawOnePrimitive(
    V9X_D3DHAL_DRAWONEPRIMITIVEDATA *data)
{
    V9X_FPU_AREA fpu;
    V9X_D3D_CONTEXT *context;
    int ok = 0;

    v9x_trace_enter(V9X_TRACE_D3D_DRAWONEPRIM,
                    data != 0
                        ? ((data->PrimitiveType << 16) |
                           (data->dwNumVertices & 0xfffful))
                        : 0ul);
    v9x_fpu_save(&fpu);
    context = data != 0 ? v9x_d3d_context_from_handle(data->dwhContext) : 0;
    (void)v9x_engine_validate_status();
    if (context != 0 && v9x_engine_status_validated() &&
        data->PrimitiveType == V9X_D3DPT_TRIANGLELIST &&
        data->VertexType == V9X_D3DVT_TLVERTEX &&
        data->lpvVertices != 0 && data->dwNumVertices == 3ul) {
        ok = v9x_d3d_triangle(context,
                              (const V9X_D3DTLVERTEX *)data->lpvVertices);
    }
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.render_primitive_calls;
    }
    if (data != 0) {
        data->ddrval = ok ? V9X_DD_OK : 0x80070057ul;
    }
    v9x_fpu_restore(&fpu);
    v9x_trace_exit(V9X_TRACE_D3D_DRAWONEPRIM,
                   ok ? V9X_DD_OK : 0x80070057ul);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dDrawPrimitives(V9X_D3DHAL_DRAWPRIMITIVESDATA *data)
{
    V9X_FPU_AREA fpu;
    V9X_D3D_CONTEXT *context;
    V9X_D3DHAL_DRAWPRIMCOUNTS *counts;
    BYTE *cursor;
    DWORD record;
    DWORD vertex;
    int ok = 0;

    v9x_trace_enter(V9X_TRACE_D3D_DRAWPRIMS,
                    data != 0 ? (DWORD)data->lpvData : 0ul);
    v9x_fpu_save(&fpu);
    context = data != 0 ? v9x_d3d_context_from_handle(data->dwhContext) : 0;
    (void)v9x_engine_validate_status();
    if (context != 0 && v9x_engine_status_validated() &&
        data->lpvData != 0) {
        cursor = (BYTE *)data->lpvData;
        ok = 1;
        for (record = 0ul; record < 64ul; ++record) {
            counts = (V9X_D3DHAL_DRAWPRIMCOUNTS *)cursor;
            cursor += sizeof(*counts);
            if (counts->wNumStateChanges > 64u) {
                ok = 0;
                break;
            }
            cursor += (DWORD)counts->wNumStateChanges * 2ul * sizeof(DWORD);
            if (counts->wNumVertices == 0u) {
                break;
            }
            cursor = (BYTE *)(((DWORD)cursor + 31ul) & ~31ul);
            if (counts->wPrimitiveType != V9X_D3DPT_TRIANGLELIST ||
                counts->wVertexType != V9X_D3DVT_TLVERTEX ||
                counts->wNumVertices > 192u ||
                (counts->wNumVertices % 3u) != 0u) {
                ok = 0;
                break;
            }
            for (vertex = 0ul; vertex < counts->wNumVertices; vertex += 3ul) {
                if (!v9x_d3d_triangle(
                        context,
                        &((const V9X_D3DTLVERTEX *)cursor)[vertex])) {
                    ok = 0;
                    break;
                }
            }
            if (!ok) {
                break;
            }
            cursor += (DWORD)counts->wNumVertices *
                      sizeof(V9X_D3DTLVERTEX);
        }
        if (record == 64ul) {
            ok = 0;
        }
    }
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.render_primitive_calls;
    }
    if (data != 0) {
        data->ddrval = ok ? V9X_DD_OK : 0x80070057ul;
    }
    v9x_fpu_restore(&fpu);
    v9x_trace_exit(V9X_TRACE_D3D_DRAWPRIMS,
                   ok ? V9X_DD_OK : 0x80070057ul);
    return V9X_DDHAL_DRIVER_HANDLED;
}

DWORD __stdcall V9xD3dDrawOneIndexedPrimitive(void *data)
{
    (void)data;
    v9x_trace_enter(V9X_TRACE_D3D_DRAWONEINDEXED, 0ul);
    v9x_trace_exit(V9X_TRACE_D3D_DRAWONEINDEXED, 0ul);
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

DWORD __stdcall V9xHalGetDriverInfo(V9X_DDHAL_GETDRIVERINFODATA *data)
{
#if V9X_C3_SERVE_D3D_CALLBACKS2
    DWORD index;
    DWORD bytes;
    BYTE *destination;
    const BYTE *source;
#endif

    if (data == 0) {
        return V9X_DDHAL_DRIVER_HANDLED;
    }
    v9x_trace_enter(V9X_TRACE_GETDRIVERINFO,
                    ((DWORD)data->guidInfo[3] << 24) |
                    ((DWORD)data->guidInfo[2] << 16) |
                    ((DWORD)data->guidInfo[1] << 8) |
                    (DWORD)data->guidInfo[0]);
    data->dwActualSize = 0ul;
    data->ddRVal = 0x88760028ul;
#if V9X_C3_SERVE_D3D_CALLBACKS2
    for (index = 0ul; index < 16ul; ++index) {
        if (data->guidInfo[index] != v9x_guid_d3d_callbacks2[index]) {
            v9x_trace_exit(V9X_TRACE_GETDRIVERINFO, data->ddRVal);
            return V9X_DDHAL_DRIVER_HANDLED;
        }
    }
    bytes = data->dwExpectedSize < sizeof(v9x_d3d_callbacks2)
        ? data->dwExpectedSize : sizeof(v9x_d3d_callbacks2);
    data->dwActualSize = sizeof(v9x_d3d_callbacks2);
    if (data->lpvData != 0) {
        v9x_d3d_callbacks2.dwSize = bytes;
        destination = (BYTE *)data->lpvData;
        source = (const BYTE *)&v9x_d3d_callbacks2;
        for (index = 0ul; index < bytes; ++index) {
            destination[index] = source[index];
        }
        data->ddRVal = V9X_DD_OK;
    }
#endif
    v9x_trace_exit(V9X_TRACE_GETDRIVERINFO, data->ddRVal);
    return V9X_DDHAL_DRIVER_HANDLED;
}

/*
 * Publish the Direct3D global data, texture formats and callback tables into
 * the shared block.
 *
 * DriverInit calls this at the point the single-file build wrote these fields
 * inline, so the field set and its ordering are unchanged. Keeping them here
 * rather than in the core is what makes the D3D block self-contained: a family
 * whose engine has no S3D path links none of this, and until then the 16-bit
 * caps clamp is what stops a Trio64 advertising it.
 */
void v9x_d3d_publish(V9X_DD_SHARED *shared)
{
    shared->d3d_global.dwSize = sizeof(V9X_D3DHAL_GLOBALDRIVERDATA);
    shared->d3d_global.hwCaps.dwSize = sizeof(V9X_D3DDEVICEDESC_V1);
    shared->d3d_global.hwCaps.dwFlags =
        V9X_D3DDD_COLORMODEL | V9X_D3DDD_DEVCAPS |
        V9X_D3DDD_TRICAPS |
        V9X_D3DDD_DEVICERENDERBITDEPTH |
        V9X_D3DDD_DEVICEZBUFFERBITDEPTH;
    shared->d3d_global.hwCaps.dcmColorModel = V9X_D3DCOLOR_RGB;
    shared->d3d_global.hwCaps.dwDevCaps =
        V9X_D3DDEVCAPS_FLOATTLVERTEX |
        /*
         * "Device can use execute buffers from system memory."
         *
         * A DirectX 2/3-era title renders only through execute buffers and
         * selects its device by capability, so omitting this bit makes it
         * discard the HAL and fall back to a software device - which is what
         * Hellbender was doing: it read the caps, warned about fog, then
         * created no context on the driver at all.
         *
         * The driver does not parse execute buffers and does not need to.
         * The runtime decomposes them into RenderState and RenderPrimitive
         * calls, which is why the Windows 98 DDK's own ViRGE sample sets this
         * bit while leaving the Execute and ExecuteClipped callbacks null,
         * exactly as this driver does.
         */
        V9X_D3DDEVCAPS_EXECUTESYSTEMMEMORY |
        /*
         * "Device can texture from device memory", and only from there:
         * v9x_d3d_texture_setup rejects any surface carrying
         * DDSCAPS_SYSTEMMEMORY. Declaring it is what tells an application
         * where its textures have to live.
         */
        V9X_D3DDEVCAPS_TEXTUREVIDEOMEMORY |
        V9X_D3DDEVCAPS_TLVERTEXSYSTEMMEMORY |
        V9X_D3DDEVCAPS_DRAWPRIMTLVERTEX;
    shared->d3d_global.hwCaps.dtcTransformCaps.dwSize =
        sizeof(V9X_D3DTRANSFORMCAPS);
    shared->d3d_global.hwCaps.dlcLightingCaps.dwSize =
        sizeof(V9X_D3DLIGHTINGCAPS);
    /*
     * dpcLineCaps stays empty and D3DDD_LINECAPS unset: this driver does not
     * rasterise lines. Populating them, and claiming
     * D3DDEVCAPS_TEXTUREVIDEOMEMORY, were both measured against Hellbender
     * and neither made it create a context on the HAL, so neither claim is
     * kept. See docs/issues/2026-08-15-hellbender-software-fallback.md.
     */
    shared->d3d_global.hwCaps.dpcLineCaps.dwSize =
        sizeof(V9X_D3DPRIMCAPS);
    shared->d3d_global.hwCaps.dpcTriCaps.dwSize =
        sizeof(V9X_D3DPRIMCAPS);
    shared->d3d_global.hwCaps.dpcTriCaps.dwMiscCaps =
        V9X_D3DPMISCCAPS_CULLNONE;
    shared->d3d_global.hwCaps.dpcTriCaps.dwRasterCaps =
        V9X_D3DPRASTERCAPS_ZTEST |
        V9X_D3DPRASTERCAPS_SUBPIXEL |
        V9X_D3DPRASTERCAPS_FOGVERTEX;
    shared->d3d_global.hwCaps.dpcTriCaps.dwZCmpCaps =
        V9X_D3DPCMPCAPS_NEVER | V9X_D3DPCMPCAPS_LESS |
        V9X_D3DPCMPCAPS_EQUAL | V9X_D3DPCMPCAPS_LESSEQUAL |
        V9X_D3DPCMPCAPS_GREATER | V9X_D3DPCMPCAPS_NOTEQUAL |
        V9X_D3DPCMPCAPS_GREATEREQUAL | V9X_D3DPCMPCAPS_ALWAYS;
    shared->d3d_global.hwCaps.dpcTriCaps.dwSrcBlendCaps =
        V9X_D3DPBLENDCAPS_SRCALPHA;
    shared->d3d_global.hwCaps.dpcTriCaps.dwDestBlendCaps =
        V9X_D3DPBLENDCAPS_INVSRCALPHA;
    shared->d3d_global.hwCaps.dpcTriCaps.dwShadeCaps =
        V9X_D3DPSHADECAPS_COLORFLATRGB |
        V9X_D3DPSHADECAPS_COLORGOURAUDRGB |
        V9X_D3DPSHADECAPS_SPECULARGOURAUDRGB |
        V9X_D3DPSHADECAPS_ALPHAFLATBLEND |
        V9X_D3DPSHADECAPS_ALPHAGOURAUDBLEND |
        /*
         * Flat fog. The driver blends fog into the vertex colour
         * (v9x_d3d_apply_vertex_color), and under flat shading that colour is
         * used across the whole triangle, so this costs nothing beyond the
         * Gouraud case already implemented.
         *
         * It was the one capability difference from the retail S3 ViRGE
         * driver with a visible symptom: without it Hellbender warns that the
         * adapter cannot show fog, and the retail driver - which sets it -
         * produces no warning at all.
         */
        V9X_D3DPSHADECAPS_FOGFLAT |
        V9X_D3DPSHADECAPS_FOGGOURAUD;
    /*
     * Describe the sampler's real constraints. v9x_d3d_texture_setup accepts
     * only square, power-of-two, 16-bit surfaces between 4 and 512 texels and
     * silently declines anything else, so a driver that does not declare
     * POW2 and SQUAREONLY leaves an application no way to comply - its
     * textures are simply dropped. ALPHA matches the ARGB1555 format
     * published below, whose alpha bit the sampler reads.
     */
    shared->d3d_global.hwCaps.dpcTriCaps.dwTextureCaps =
        V9X_D3DPTEXTURECAPS_PERSPECTIVE |
        V9X_D3DPTEXTURECAPS_POW2 |
        V9X_D3DPTEXTURECAPS_SQUAREONLY |
        V9X_D3DPTEXTURECAPS_ALPHA;
    shared->d3d_global.hwCaps.dpcTriCaps.dwTextureFilterCaps =
        V9X_D3DPTFILTERCAPS_NEAREST | V9X_D3DPTFILTERCAPS_LINEAR |
        V9X_D3DPTFILTERCAPS_MIPNEAREST |
        V9X_D3DPTFILTERCAPS_MIPLINEAR |
        V9X_D3DPTFILTERCAPS_LINEARMIPNEAREST |
        V9X_D3DPTFILTERCAPS_LINEARMIPLINEAR;
    shared->d3d_global.hwCaps.dpcTriCaps.dwTextureBlendCaps =
        V9X_D3DPTBLENDCAPS_DECAL | V9X_D3DPTBLENDCAPS_MODULATE |
        V9X_D3DPTBLENDCAPS_COPY;
    shared->d3d_global.hwCaps.dpcTriCaps.dwTextureAddressCaps =
        V9X_D3DPTADDRESSCAPS_WRAP | V9X_D3DPTADDRESSCAPS_CLAMP;
    shared->d3d_global.hwCaps.dwDeviceRenderBitDepth = V9X_DDBD_16;
    shared->d3d_global.hwCaps.dwDeviceZBufferBitDepth = V9X_DDBD_16;
    shared->d3d_global.dwNumVertices = 0ul;
    shared->d3d_global.dwNumClipVertices = 0ul;
    shared->texture_formats[0].dwSize = sizeof(V9X_DDSURFACEDESC);
    shared->texture_formats[0].dwFlags =
        V9X_DDSD_CAPS | V9X_DDSD_PIXELFORMAT;
    shared->texture_formats[0].ddpfPixelFormat.dwSize =
        sizeof(V9X_DDPIXELFORMAT);
    shared->texture_formats[0].ddpfPixelFormat.dwFlags =
        V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
    shared->texture_formats[0].ddpfPixelFormat.dwRGBBitCount = 16ul;
    shared->texture_formats[0].ddpfPixelFormat.dwRBitMask = 0x00007c00ul;
    shared->texture_formats[0].ddpfPixelFormat.dwGBitMask = 0x000003e0ul;
    shared->texture_formats[0].ddpfPixelFormat.dwBBitMask = 0x0000001ful;
    shared->texture_formats[0].ddpfPixelFormat.dwRGBAlphaBitMask =
        0x00008000ul;
    shared->texture_formats[0].ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;

    /*
     * ARGB4444. The texture unit selects its format from bits 7:5 of the
     * command register - 1 is ARGB4444, 2 is ARGB1555 - so both are native
     * and the sampler needs no conversion. Publishing only one format left
     * an application with a single choice that carries one alpha bit; 4444
     * trades colour precision for four bits of it.
     */
    shared->texture_formats[1].dwSize = sizeof(V9X_DDSURFACEDESC);
    shared->texture_formats[1].dwFlags =
        V9X_DDSD_CAPS | V9X_DDSD_PIXELFORMAT;
    shared->texture_formats[1].ddpfPixelFormat.dwSize =
        sizeof(V9X_DDPIXELFORMAT);
    shared->texture_formats[1].ddpfPixelFormat.dwFlags =
        V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
    shared->texture_formats[1].ddpfPixelFormat.dwRGBBitCount = 16ul;
    shared->texture_formats[1].ddpfPixelFormat.dwRBitMask = 0x00000f00ul;
    shared->texture_formats[1].ddpfPixelFormat.dwGBitMask = 0x000000f0ul;
    shared->texture_formats[1].ddpfPixelFormat.dwBBitMask = 0x0000000ful;
    shared->texture_formats[1].ddpfPixelFormat.dwRGBAlphaBitMask =
        0x0000f000ul;
    shared->texture_formats[1].ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;

    shared->d3d_global.dwNumTextureFormats = 2ul;
    shared->d3d_global.lpTextureFormats = &shared->texture_formats[0];

    shared->d3d_callbacks.dwSize = sizeof(V9X_D3DHAL_CALLBACKS);
    shared->d3d_callbacks.ContextCreate =
        (V9X_DD_CODE_PTR)V9xD3dContextCreate;
    shared->d3d_callbacks.ContextDestroy =
        (V9X_DD_CODE_PTR)V9xD3dContextDestroy;
    shared->d3d_callbacks.ContextDestroyAll =
        (V9X_DD_CODE_PTR)V9xD3dContextDestroyAll;
    shared->d3d_callbacks.Execute = 0;
    shared->d3d_callbacks.ExecuteClipped = 0;
    shared->d3d_callbacks.RenderState =
        (V9X_DD_CODE_PTR)V9xD3dRenderState;
    shared->d3d_callbacks.RenderPrimitive =
        (V9X_DD_CODE_PTR)V9xD3dRenderPrimitive;
    shared->d3d_callbacks.TextureCreate =
        (V9X_DD_CODE_PTR)V9xD3dTextureCreate;
    shared->d3d_callbacks.TextureDestroy =
        (V9X_DD_CODE_PTR)V9xD3dTextureDestroy;
    shared->d3d_callbacks.TextureSwap =
        (V9X_DD_CODE_PTR)V9xD3dTextureSwap;
    shared->d3d_callbacks.TextureGetSurf =
        (V9X_DD_CODE_PTR)V9xD3dTextureGetSurf;

    v9x_d3d_callbacks2.dwSize = sizeof(V9X_D3DHAL_CALLBACKS2);
    v9x_d3d_callbacks2.dwFlags =
        V9X_D3DHAL2_CB32_SETRENDERTARGET |
        V9X_D3DHAL2_CB32_DRAWONEPRIMITIVE |
        V9X_D3DHAL2_CB32_DRAWONEINDEXEDPRIMITIVE |
        V9X_D3DHAL2_CB32_DRAWPRIMITIVES;
    v9x_d3d_callbacks2.SetRenderTarget =
        (V9X_DD_CODE_PTR)V9xD3dSetRenderTarget;
    v9x_d3d_callbacks2.DrawOnePrimitive =
        (V9X_DD_CODE_PTR)V9xD3dDrawOnePrimitive;
    v9x_d3d_callbacks2.DrawOneIndexedPrimitive =
        (V9X_DD_CODE_PTR)V9xD3dDrawOneIndexedPrimitive;
    v9x_d3d_callbacks2.DrawPrimitives =
        (V9X_DD_CODE_PTR)V9xD3dDrawPrimitives;
}
