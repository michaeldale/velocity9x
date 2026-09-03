/*
 * ViRGE/DX S3D engine for V9XHAL.DLL's Direct3D path.
 *
 * The chip half of the D3D split: the fixed-point triangle setup that feeds
 * the 3D command registers, the sampler's view of which surfaces it can
 * texture from, the caps the device publishes, and the S3D unit's own limits
 * as data. This is the only file on the D3D path that writes a hardware
 * register.
 *
 * Everything chip-neutral is in d3d_core.c and reaches this file only through
 * V9X_D3D_ENGINE_OPS at the bottom. In particular the core owns the context
 * pool and the texture handle table; this file asks it which surface a
 * context has bound and then judges the surface itself.
 *
 * Split out of the combined d3d_virge.c on 2026-08-29. Every body here is the
 * one that passed the DirectDraw/Direct3D probe before the split, moved
 * unchanged.
 *
 * Every register offset and command bit it uses is in ddhal_internal.h with
 * the rest of the S3 vocabulary.
 */
#include "d3d_internal.h"
#include "d3d_zfixed.h"

/*
 * The S3D unit's constraints, previously literals spread through the routines
 * that are now in d3d_core.c.
 *
 * 16 bpp: the only render-target depth the engine has a pixel format for.
 * 0FF8h/8: the destination stride field is 8-byte aligned with a 12-bit byte
 * count. 2048: the coordinate registers' pixel range. 4..512 texels: the
 * sampler's square power-of-two range. 2048.0f: outside this the 12.20
 * fixed-point converter in this file overflows, so the core refuses the
 * vertex before clipping rather than wrapping it.
 */
static const V9X_D3D_ENGINE_LIMITS v9x_d3d_virge_limits = {
    16ul,           /* target_bits_per_pixel */
    0x00000ff8ul,   /* target_pitch_max */
    8ul,            /* target_pitch_align */
    2048ul,         /* target_dimension_max */
    4ul,            /* texture_size_min */
    512ul,          /* texture_size_max */
    2048.0f,        /* coordinate_limit */
    16ul            /* depth_bits_per_pixel */
};

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

/*
 * Is this texture's mip chain laid out where the engine will read it?
 *
 * The S3D unit takes one TEX_BASE and derives every level from it: the largest
 * level at the base, each smaller level immediately after the one before,
 * down to the level the command word's size field names. That is how the
 * emulator models it (build\reference-vid_s3_virge.c around 4479, the loop
 * that walks tex_base up through the levels) and it is the only reading of a
 * single base register. DirectDraw, meanwhile, creates each level as its own
 * surface through the ordinary allocator and attaches it to the level above;
 * nothing in that promises the second surface starts where the first ends.
 *
 * Until 2026-09-03 the engine assumed it did, on the strength of the emulated
 * ViRGE passing the probe's mip rung - which it passes because its allocator
 * happened to place the two levels back to back. A physical Trio3D/2X read
 * black for the same rung: the level-1 fetch went to whatever sat past level 0.
 *
 * So the chain is walked and checked, and a chain with a gap draws from level
 * 0 with mip selection off. Wrong - the texture will shimmer at distance - but
 * visibly so and from the right texels, where fetching past the top level is
 * an out-of-surface read that happens to land on zeroed VRAM today and on
 * somebody else's surface tomorrow. Both outcomes are counted so V9XTRACE can
 * say which one a machine is seeing.
 *
 * The walk is bounded: a 512-texel top level has at most eight levels below
 * it, and a malformed list is treated as a gap rather than followed.
 */
#define V9X_D3D_MIP_LEVELS_MAX 10ul

static int v9x_d3d_mip_chain_contiguous(const V9X_DD_SURFACE_LCL *top,
                                        DWORD top_offset, DWORD top_size)
{
    const V9X_DD_SURFACE_LCL *level = top;
    DWORD expected = top_offset + top_size * top_size * 2ul;
    DWORD size = top_size;
    DWORD depth;

    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.mip_chain_checks;
        v9x_hal->d3d_diagnostics.mip_chain_levels = 0ul;
        v9x_hal->d3d_diagnostics.mip_chain_delta = 0xfffffffful;
    }
    for (depth = 0ul; depth < V9X_D3D_MIP_LEVELS_MAX; ++depth) {
        const V9X_DD_ATTACH_NODE *node =
            (const V9X_DD_ATTACH_NODE *)level->lpAttachList;
        const V9X_DD_SURFACE_LCL *next = 0;
        DWORD next_offset;

        /* The next level is the attached surface that is itself a mip
         * level. A texture may carry other attachments - a Z buffer never,
         * but the walk does not assume - so the caps are tested. */
        while (node != 0) {
            if (node->object != 0 && node->object != level &&
                (node->object->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul) {
                next = node->object;
                break;
            }
            node = node->next;
        }
        if (next == 0) {
            return 1;   /* the chain ends here, and everything so far fit */
        }
        if (next->lpGbl == 0 || size < 2ul ||
            (DWORD)next->lpGbl->wWidth != size / 2ul ||
            (DWORD)next->lpGbl->wHeight != size / 2ul) {
            break;
        }
        next_offset = v9x_surface_offset(next);
        if (v9x_hal != 0 && depth == 0ul && next_offset != 0xfffffffful) {
            v9x_hal->d3d_diagnostics.mip_chain_delta =
                next_offset - top_offset;
        }
        if (next_offset != expected) {
            break;
        }
        if (v9x_hal != 0) {
            ++v9x_hal->d3d_diagnostics.mip_chain_levels;
        }
        size /= 2ul;
        expected = next_offset + size * size * 2ul;
        level = next;
    }
    if (v9x_hal != 0) {
        ++v9x_hal->d3d_diagnostics.mip_chain_gaps;
    }
    return 0;
}

static int v9x_d3d_texture_info(V9X_D3D_CONTEXT *context,
                                DWORD *offset_out, DWORD *size_log_out,
                                int *mipmapped_out, DWORD *format_out)
{
    V9X_DD_SURFACE_LCL *surface = v9x_d3d_context_texture_surface(context);
    DWORD size;
    DWORD size_log = 0ul;
    DWORD offset;
    DWORD last_byte;

    if (surface == 0 || surface->lpGbl == 0) {
        return 0;
    }
    if ((surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return 0;
    }
    if (surface->lpGbl->wWidth != surface->lpGbl->wHeight ||
        (DWORD)surface->lpGbl->wWidth < v9x_d3d_virge_limits.texture_size_min ||
        (DWORD)surface->lpGbl->wWidth > v9x_d3d_virge_limits.texture_size_max) {
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
    *mipmapped_out = (surface->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul &&
                     v9x_d3d_mip_chain_contiguous(surface, offset, size);
    return 1;
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

/*
 * D3D's comparison function as the S3D unit encodes it.
 *
 * A table, not arithmetic: the chip's order is NEVER, GREATER, EQUAL,
 * GREATEREQUAL, LESS, NOTEQUAL, LESSEQUAL, ALWAYS, so six of the eight differ
 * from (D3DCMP value - 1). Taken from the DDK's own switch, D3DRENDR.C:290-314,
 * and cross-checked against 86Box's Z_CLIP.
 *
 * The default arm is ALWAYS rather than a fallthrough to zero, and that
 * matters: zero is the encoding for NEVER, so a function this driver does not
 * recognise would otherwise discard every pixel and render black with nothing
 * anywhere to say why.
 */
static DWORD v9x_d3d_z_compare(DWORD func)
{
    switch (func) {
    case V9X_D3DCMP_NEVER:        return V9X_VIRGE_3D_CMD_Z_CMP_NEVER;
    case V9X_D3DCMP_LESS:         return V9X_VIRGE_3D_CMD_Z_CMP_LESS;
    case V9X_D3DCMP_EQUAL:        return V9X_VIRGE_3D_CMD_Z_CMP_EQUAL;
    case V9X_D3DCMP_LESSEQUAL:    return V9X_VIRGE_3D_CMD_Z_CMP_LESSEQUAL;
    case V9X_D3DCMP_GREATER:      return V9X_VIRGE_3D_CMD_Z_CMP_GREATER;
    case V9X_D3DCMP_NOTEQUAL:     return V9X_VIRGE_3D_CMD_Z_CMP_NOTEQUAL;
    case V9X_D3DCMP_GREATEREQUAL: return V9X_VIRGE_3D_CMD_Z_CMP_GREATEREQUAL;
    default:                      return V9X_VIRGE_3D_CMD_Z_CMP_ALWAYS;
    }
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
    float dzdx = 0.0f, dzdy = 0.0f;
    int z_active;
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
    /*
     * Depth is active only when the application asked for it AND a validated
     * depth surface is actually attached. The conjunction is the DDK's
     * (D3DRENDR.C:266, `if (ctxt->lpLclZ && ctxt->bZEnabled)`) and it is load
     * bearing: an application may legally set ZENABLE with no Z buffer bound,
     * and the runtime often replays a whole default state block. Enabling the
     * hardware on that would point the depth unit at depth_offset 0, which is
     * the visible framebuffer.
     */
    z_active = context->z_enable != 0ul && context->zbuffer != 0 &&
               context->depth_pitch != 0ul;
    if (z_active) {
        /* Same shape as the colour gradients below, and the same fdxr. That
         * value is 1/|dx|; the triangle's orientation is carried once, in bit
         * 31 of Y01_Y12. Using a signed 1/dx for depth alone would make depth
         * run backwards against the shading on half of all triangles. */
        dzdy = (p2->sz - p0->sz) * fdy02r;
        dzdx = (p1->sz - (dzdy * fdy01 + p0->sz)) * fdxr;
    }
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
    /* Depth base and stride, or zero when depth is off. Both come from the
     * same z_active expression as the command word's Z-mode field, and must
     * keep doing so: a base of 0 with the mode field enabled writes depth
     * values over the visible framebuffer. */
    v9x_mmio_write(V9X_VIRGE_3D_Z_BASE,
                   z_active ? context->depth_offset : 0ul);
    v9x_mmio_write(V9X_VIRGE_3D_DEST_BASE, context->target_offset);
    v9x_mmio_write(V9X_VIRGE_3D_CLIP_L_R, context->width - 1ul);
    v9x_mmio_write(V9X_VIRGE_3D_CLIP_T_B, context->height - 1ul);
    /*
     * Both halves. The register is destination stride in the high word and
     * source stride in the low, and this wrote only the high one - which the
     * emulator forgave, because its 3D blend reads the destination back
     * through the destination stride (build\reference-vid_s3_virge.c around
     * 4379). Real silicon does not have to: an alpha blend that fetches the
     * pixel it is blending over through a source stride of zero reads the
     * wrong row every time, and the Trio3D/2X's vertex-alpha result on
     * 2026-09-03 was exactly that shape - a colour no weighting of red over
     * blue produces, and different on every boot
     * (docs\issues\2026-09-03-trio3d-alpha-and-mip-differ-from-virge-dx.md).
     * The 3D engine has one surface to read back from, and it is the one it
     * writes to, so the two strides are the same number.
     */
    v9x_mmio_write(V9X_VIRGE_3D_DEST_SRC_STRIDE,
                   (context->pitch << 16) | (context->pitch & 0xfffful));
    v9x_mmio_write(V9X_VIRGE_3D_Z_STRIDE,
                   z_active ? context->depth_pitch : 0ul);
    v9x_mmio_write(V9X_VIRGE_3D_TEX_BASE,
                   textured ? texture_offset : 0ul);
    v9x_mmio_write(V9X_VIRGE_3D_TEX_BORDER, context->texture_border);
    v9x_mmio_write(V9X_VIRGE_3D_FADE_COLOR, 0ul);

    if (textured) {
        /* Eleven writes, not nine: the two mip-level gradients are part of
         * the texture setup and were never written before, which left the
         * level index to drift across the triangle - see V9X_VIRGE_3D_DDDX.
         * Eleven is inside the FIFO's sixteen, which is what the depth
         * triple's separate reservation below was about. */
        if (!v9x_wait_fifo(11ul, 1)) {
            return 0;
        }
        v9x_mmio_write(V9X_VIRGE_3D_TBV, 0ul);
        v9x_mmio_write(V9X_VIRGE_3D_TBU, 0ul);
        v9x_mmio_write(V9X_VIRGE_3D_DDDX, 0ul);
        v9x_mmio_write(V9X_VIRGE_3D_DDDY, 0ul);
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
    /*
     * Fifteen writes from COMMAND through Y01_Y12. The depth triple needs
     * three more and reserves them separately, immediately before it writes
     * them.
     *
     * Not one reservation of eighteen, which is what this was and what made
     * the whole depth feature inert. SUBSYS_STAT carries the free-slot count
     * in five bits at 12:8, and 86Box's model sets bit 12 on both arms of
     * that read (build\reference-vid_s3_virge.c:1457-1462), so the count it
     * reports is always exactly 16. A wait for 18 can therefore never be
     * satisfied: it spun out V9X_VIRGE_FIFO_SPIN_LIMIT, counted a FIFO
     * timeout, reset the engine and abandoned the triangle - on every
     * depth-enabled draw, with every HRESULT still reporting success and
     * every depth pixel reading zero. Measured on Win86SE 2026-08-30: seven
     * FIFO timeouts and seven engine resets for the seven rungs of the depth
     * ladders, against zero on the same run's depth-off draws. See
     * docs/decisions/2026-08-30-virge-depth-fifo-reservation.md.
     *
     * Sixteen is also the S3D FIFO's own depth in that model - it throttles
     * its queue at FIFO_ENTRIES >= 16 - so reserving more slots than the FIFO
     * holds was never going to work on the chip either.
     */
    if (!v9x_wait_fifo(15ul, 1)) {
        return 0;
    }
    /* With AE set, CMD_SET establishes persistent state; the final
     * Y01_Y12 write launches the triangle. */
    /*
     * A ternary rather than the DDK's mask-and-OR, deliberately. The
     * depth-disabled arm is the literal pre-Z constant, so "a triangle drawn
     * without depth emits exactly the word it always did" is true by
     * construction rather than by reasoning about mask exactness - and the
     * header's compile-time assertion pins that constant's value.
     */
    command = z_active
        ? (V9X_VIRGE_3D_CMD_GOURAUD_16 |
           v9x_d3d_z_compare(context->z_func) |
           (context->z_write != 0ul ? V9X_VIRGE_3D_CMD_Z_UPDATE : 0ul))
        : V9X_VIRGE_3D_CMD_GOURAUD_16_AE;
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
    if (z_active) {
        /* Reserved apart from the fifteen above; see that reservation for
         * why three and fifteen cannot be asked for as one eighteen. */
        if (!v9x_wait_fifo(3ul, 1)) {
            return 0;
        }
        /* Where GENTRI.C:975-984 puts them: after the colour registers and
         * before the edges. ZS02 is p0's depth advanced to the start scanline
         * by the same fdycc the X edges use - without that correction the
         * whole triangle's depth is biased by a fraction of a scan line. */
        v9x_mmio_write(V9X_VIRGE_3D_DZDX,
                       (DWORD)v9x_d3d_z_to_1_31_signed(dzdx));
        v9x_mmio_write(V9X_VIRGE_3D_DZDY,
                       (DWORD)v9x_d3d_z_to_1_31_signed(dzdy));
        v9x_mmio_write(V9X_VIRGE_3D_ZS02,
                       (DWORD)v9x_d3d_z_to_1_31_depth(
                           p0->sz + fdycc * dzdy));
    }
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
        /* Same split as the first pass, for the same reason. */
        if (!v9x_wait_fifo(15ul, 1)) {
            return 0;
        }
        second_command &= ~V9X_VIRGE_3D_CMD_TEXTURE_UNLIT;
        second_command |= V9X_VIRGE_3D_CMD_TEXTURE_LIT |
                          V9X_VIRGE_3D_CMD_TEX_MODULATE |
                          V9X_VIRGE_3D_CMD_ALPHA_SOURCE |
                          V9X_VIRGE_3D_CMD_ALPHA_ENABLE;
        /*
         * The depth rule for the second pass, which is not obvious.
         *
         * This pass re-draws the same triangle at the same depth to blend mip
         * level N+1 over level N. Inheriting the first pass's comparison would
         * break it: with the usual LESS, the first pass has already written
         * this depth, so the second pass finds Zfb == Zs, fails, and trilinear
         * silently degrades to bilinear.
         *
         * Turning depth off for the pass would be worse - it would paint over
         * pixels the first pass correctly rejected as occluded, bleeding
         * hidden geometry through at the blend alpha.
         *
         * So: never update depth twice, and select exactly the pixels the
         * first pass wrote. Both passes emit bit-identical depth registers, so
         * EQUAL is exact for that set, whatever the application's own function
         * was. When the first pass did not update depth, the buffer is
         * unchanged and repeating the application's comparison selects the
         * same set anyway.
         */
        if (z_active) {
            second_command &= ~V9X_VIRGE_3D_CMD_Z_UPDATE;
            if (context->z_write != 0ul) {
                second_command &= ~V9X_VIRGE_3D_CMD_Z_CMP_ALWAYS;
                second_command |= V9X_VIRGE_3D_CMD_Z_CMP_EQUAL;
            }
        }
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
        if (z_active) {
            /* Reserved apart from the fifteen above, as in the first pass. */
            if (!v9x_wait_fifo(3ul, 1)) {
                return 0;
            }
            /* Re-emitted with the identical values rather than relied upon to
             * persist: the second COMMAND write launches a separate triangle,
             * and whether the setup registers latch across an autoexecute
             * launch is not something this project can verify. Three writes
             * are cheaper than that assumption. */
            v9x_mmio_write(V9X_VIRGE_3D_DZDX,
                           (DWORD)v9x_d3d_z_to_1_31_signed(dzdx));
            v9x_mmio_write(V9X_VIRGE_3D_DZDY,
                           (DWORD)v9x_d3d_z_to_1_31_signed(dzdy));
            v9x_mmio_write(V9X_VIRGE_3D_ZS02,
                           (DWORD)v9x_d3d_z_to_1_31_depth(
                               p0->sz + fdycc * dzdy));
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

/*
 * Draw a run of triangles.
 *
 * The batch entry point the ops table fixes. The ViRGE is immediate-mode, so
 * this is a loop over the per-triangle emitter above and nothing more; a
 * command-stream engine would build one packet here instead, which is the
 * whole reason the seam is at this granularity and not at the triangle.
 *
 * Stops at the first triangle the emitter refuses, which is what the three
 * call sites did when they held the loop themselves.
 */
static int v9x_d3d_virge_draw_triangles(V9X_D3D_CONTEXT *context,
                                        const V9X_D3DTLVERTEX *vertices,
                                        DWORD triangle_count)
{
    DWORD index;

    for (index = 0ul; index < triangle_count; ++index) {
        if (!v9x_d3d_triangle(context, &vertices[index * 3ul])) {
            return 0;
        }
    }
    return 1;
}

static void v9x_d3d_virge_describe_caps(V9X_DD_SHARED *shared)
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
}

/*
 * The ViRGE is ready to draw when its 2D engine has been validated.
 *
 * This is the test that used to sit in d3d_core.c on all three draw entry
 * points, moved to the engine it was always about: v9x_engine_ready() tests
 * engine_type == S3_VIRGE_DX literally, plus a control window and a mapped
 * MMIO aperture. The validate call comes with it, because it is what makes
 * the answer current rather than stale - the core used to issue it
 * unconditionally right before asking, and that pairing is a ViRGE detail.
 */
static int v9x_d3d_virge_ready(void)
{
    (void)v9x_engine_validate_status();
    return v9x_engine_status_validated();
}

const V9X_D3D_ENGINE_OPS v9x_d3d_engine_virge = {
    &v9x_d3d_virge_limits,
    v9x_d3d_texture_format,
    v9x_d3d_virge_describe_caps,
    v9x_d3d_virge_draw_triangles,
    v9x_d3d_virge_ready
};
