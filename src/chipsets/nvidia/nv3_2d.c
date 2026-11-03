/*
 * NVIDIA RIVA 128 (NV3) 2D engine: the bring-up, solid fill and screen
 * copy, as register and method writes.
 *
 * Pure policy, no I/O; see nv3_2d.h. The tables are Linux rivafb's
 * (drivers\video\fbdev\riva\riva_tbl.h: RivaTablePMC, RivaTablePTIMER,
 * nv3TablePFIFO, nv3TablePRAMIN and its per-depth tables, nv3TablePGRAPH
 * and its per-depth SURF_CONFIG, RivaTableFIFO) with every word index
 * turned into a byte offset, the form the Phase 3 write probe ran on the
 * card (tools\diag\nv3_2d_win32.c,
 * docs\decisions\2026-11-02-riva128-2d-engine-writes.md). Left out, as the
 * probe left them out: the DMA object and its page table at RAMIN 1400h,
 * and the D3D and surface objects, which only Direct3D needs.
 */
#include "velocity9x/nv3_2d.h"

#define V9X_NV3_BIND_COUNT 7u

/* nv3TablePFIFO, in order: everything off, the tables placed (RAMHT at
 * RAMIN 0, 4 KiB; RAMRO 2000h; RAMFC 2200h), the pointers and contexts
 * cleared, interrupts off and acknowledged, then the FIFO back on. CACHE1's
 * eight contexts are 10h apart (word indexes 4A0h-4BCh). */
static const struct v9x_nv3_write v9x_nv3_pfifo[] = {
    { 0x002500ul, 0x00000000ul }, { 0x003200ul, 0x00000000ul },
    { 0x003240ul, 0x00000000ul }, { 0x003250ul, 0x00000000ul },
    { 0x003204ul, 0x00000000ul }, { 0x002210ul, 0x00000000ul },
    { 0x002218ul, 0x00002000ul }, { 0x002214ul, 0x00002200ul },
    { 0x003210ul, 0x00000000ul }, { 0x003270ul, 0x00000000ul },
    { 0x002410ul, 0x00000000ul }, { 0x002420ul, 0x00000000ul },
    { 0x002400ul, 0x00000000ul }, { 0x003280ul, 0x00000000ul },
    { 0x003290ul, 0x00000000ul }, { 0x0032a0ul, 0x00000000ul },
    { 0x0032b0ul, 0x00000000ul }, { 0x0032c0ul, 0x00000000ul },
    { 0x0032d0ul, 0x00000000ul }, { 0x0032e0ul, 0x00000000ul },
    { 0x0032f0ul, 0x00000000ul }, { 0x002140ul, 0x00000000ul },
    { 0x002100ul, 0xfffffffful }, { 0x003200ul, 0x00000001ul },
    { 0x003240ul, 0x00000001ul }, { 0x002500ul, 0x00000001ul }
};

/* nv3TablePGRAPH, in order. DEBUG_0-3 first; the interrupts off and
 * acknowledged; the FIFO enable last. */
static const struct v9x_nv3_write v9x_nv3_pgraph[] = {
    { 0x400080ul, 0x1230001ful }, { 0x400084ul, 0x10113000ul },
    { 0x400088ul, 0x1131f101ul }, { 0x40008cul, 0x0100f531ul },
    { 0x400180ul, 0x00000000ul }, { 0x400194ul, 0x00000000ul },
    { 0x4001a0ul, 0x00000000ul }, { 0x4001a4ul, 0x00000000ul },
    { 0x4001a8ul, 0x00000000ul }, { 0x4001acul, 0x00000000ul },
    { 0x4001b0ul, 0x00000000ul }, { 0x4001b4ul, 0x00000000ul },
    { 0x4001b8ul, 0x00000000ul }, { 0x4001bcul, 0x00000000ul },
    { 0x4006a0ul, 0x00000000ul }, { 0x401100ul, 0xfffffffful },
    { 0x401200ul, 0x00000001ul }, { 0x400680ul, 0x00000000ul },
    { 0x400688ul, 0x00000000ul }, { 0x400628ul, 0xfffffffful },
    { 0x400640ul, 0x00000000ul }, { 0x400508ul, 0x00000000ul },
    { 0x400550ul, 0x00000000ul }, { 0x400554ul, 0xfffffffful },
    { 0x400558ul, 0x00000000ul }, { 0x40055cul, 0xfffffffful },
    { 0x400190ul, 0x10010002ul }, { 0x400140ul, 0x00000000ul },
    { 0x400144ul, 0x00000000ul }, { 0x400100ul, 0xfffffffful },
    { 0x400104ul, 0xfffffffful }, { 0x401100ul, 0xfffffffful },
    { 0x4006a4ul, 0x00000001ul }
};

/* nv3TablePRAMIN's RAMHT entries, handle then context (40h | type in bits
 * 22:16, the instance in 15:0), for the objects bound. */
static const struct v9x_nv3_write v9x_nv3_ramht[] = {
    { 0x0800ul, 0x80000000ul }, { 0x0804ul, 0x00c20341ul },  /* ROP */
    { 0x0810ul, 0x80000001ul }, { 0x0814ul, 0x00c50342ul },  /* clip */
    { 0x0820ul, 0x80000002ul }, { 0x0824ul, 0x00c60343ul },  /* pattern */
    { 0x0900ul, 0x80000010ul }, { 0x0904ul, 0x00d10344ul },  /* pixmap */
    { 0x0910ul, 0x80000011ul }, { 0x0914ul, 0x00d00345ul },  /* blit */
    { 0x0920ul, 0x80000012ul }, { 0x0924ul, 0x00cc0346ul },  /* bitmap */
    { 0x0960ul, 0x80000016ul }, { 0x0964ul, 0x00ca034cul }   /* line */
};

/* The objects' first instance words, at RAMIN instance x 10h, per depth:
 * nv3TablePRAMIN_8BPP, _15BPP (rivafb's 16 bpp too) and _32BPP. Bits 2:0
 * are the colour format (nv3_pgraph.xml: 3 Y8, 0 X1R5G5B5, 1 X8R8G8B8). */
static const v9x_u32 v9x_nv3_instance_offsets[V9X_NV3_BIND_COUNT] = {
    0x3410ul, 0x3420ul, 0x3430ul, 0x3440ul, 0x3450ul, 0x3460ul, 0x34c0ul
};
static const v9x_u32 v9x_nv3_instance_8[V9X_NV3_BIND_COUNT] = {
    0x10110203ul, 0x10110203ul, 0x1011020bul, 0x10118203ul, 0x10110203ul,
    0x10110203ul, 0x10118203ul
};
static const v9x_u32 v9x_nv3_instance_16[V9X_NV3_BIND_COUNT] = {
    0x10110200ul, 0x10110200ul, 0x10110208ul, 0x10118200ul, 0x10110200ul,
    0x10110200ul, 0x10118200ul
};
static const v9x_u32 v9x_nv3_instance_32[V9X_NV3_BIND_COUNT] = {
    0x10110201ul, 0x10110201ul, 0x10110209ul, 0x10118201ul, 0x10110201ul,
    0x10110201ul, 0x10118201ul
};

/* RivaTableFIFO: subchannel and handle, the D3D binding left out. */
static const struct v9x_nv3_write v9x_nv3_bindings[V9X_NV3_BIND_COUNT] = {
    { V9X_NV3_SUB_ROP, 0x80000000ul },
    { V9X_NV3_SUB_CLIP, 0x80000001ul },
    { V9X_NV3_SUB_PATTERN, 0x80000002ul },
    { V9X_NV3_SUB_PIXMAP, 0x80000010ul },
    { V9X_NV3_SUB_BLIT, 0x80000011ul },
    { V9X_NV3_SUB_BITMAP, 0x80000012ul },
    { V9X_NV3_SUB_LINE, 0x80000016ul }
};

#define V9X_NV3_COUNT(a) ((v9x_u32)(sizeof(a) / sizeof((a)[0])))

static void v9x_nv3_put(struct v9x_nv3_write *list, v9x_u32 *count,
                        v9x_u32 offset, v9x_u32 value)
{
    list[*count].offset = offset;
    list[*count].value = value;
    ++*count;
}

static v9x_u32 v9x_nv3_method(v9x_u32 subchannel, v9x_u32 method)
{
    return V9X_NV3_USER + subchannel * V9X_NV3_SUBCHANNEL_STEP + method;
}

v9x_u32 v9x_nv3_pfb_config(v9x_u32 bytes_per_pixel, v9x_u32 width)
{
    v9x_u32 depth;

    depth = bytes_per_pixel > 2ul ? 3ul : bytes_per_pixel;
    return ((width + V9X_NV3_PFB_WIDTH_UNIT - 1ul) /
            V9X_NV3_PFB_WIDTH_UNIT) |
           (depth << V9X_NV3_PFB_DEPTH_SHIFT) | V9X_NV3_PFB_TILING_OFF;
}

/*
 * Whether the engine draws DirectDraw fills and copies at this depth: 8 and
 * 32 bpp. The builders accept 16 bpp, where the probe measured the engine
 * setting bit 15 of every pixel; this BIOS's 16 bpp modes are 5:6:5, where
 * that bit is red's top bit, so 16 bpp stays on the CPU until the desktop
 * runs 5:5:5 (decision 1 of docs\plans\nvidia-riva128.md).
 */
v9x_u16 v9x_nv3_depth_drawn(v9x_u32 bytes_per_pixel)
{
    return bytes_per_pixel == 1ul || bytes_per_pixel == 4ul ?
           V9X_TRUE : V9X_FALSE;
}

/* Start bits the CRTC holds: 21 bits of dwords (CR19 bits 4-0 the top
 * five), 8 MiB. */
#define V9X_NV3_CRTC_START_LIMIT  0x00200000ul
#define V9X_NV3_CR19_START_MASK   0x1fu

/*
 * A start the registers cannot express is refused, not rounded: a byte
 * offset that is not a whole dword would need the attribute controller's
 * pan register, which rivafb writes and this does not, and rounding shifts
 * every pixel of the frame.
 */
v9x_status v9x_nv3_build_crtc_start(v9x_u32 byte_offset, v9x_u32 vram_bytes,
                                    v9x_u8 cr19,
                                    struct v9x_nv3_crtc_start *start)
{
    v9x_u32 dwords = byte_offset >> 2;

    if (start == 0 || (byte_offset & 3ul) != 0ul ||
        byte_offset >= vram_bytes || dwords >= V9X_NV3_CRTC_START_LIMIT) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    start->cr0d = (v9x_u8)(dwords & 0xfful);
    start->cr0c = (v9x_u8)((dwords >> 8) & 0xfful);
    start->cr19 = (v9x_u8)((cr19 & (v9x_u8)~V9X_NV3_CR19_START_MASK) |
                           (v9x_u8)((dwords >> 16) &
                                    V9X_NV3_CR19_START_MASK));
    return V9X_STATUS_OK;
}

v9x_u32 v9x_nv3_ramin_vram_offset(v9x_u32 vram_top, v9x_u32 ramin_offset)
{
    return vram_top - V9X_NV3_RAMIN_UNIT -
           (ramin_offset & ~(V9X_NV3_RAMIN_UNIT - 1ul)) +
           (ramin_offset & (V9X_NV3_RAMIN_UNIT - 1ul));
}

v9x_status v9x_nv3_build_setup(v9x_u32 bytes_per_pixel, v9x_u32 width,
                               v9x_u32 height, v9x_u32 pitch_bytes,
                               struct v9x_nv3_setup *setup)
{
    const v9x_u32 *instance;
    v9x_u32 surf_config;
    v9x_u32 index;

    if (setup == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    setup->before_count = 0ul;
    setup->ramin_count = 0ul;
    setup->after_count = 0ul;
    setup->bind_count = 0ul;
    setup->defaults_count = 0ul;

    if (bytes_per_pixel == 1ul) {
        instance = v9x_nv3_instance_8;
        surf_config = 0x00001111ul;
    } else if (bytes_per_pixel == 2ul) {
        instance = v9x_nv3_instance_16;
        surf_config = 0x00002222ul;
    } else if (bytes_per_pixel == 4ul) {
        instance = v9x_nv3_instance_32;
        surf_config = 0x00003333ul;
    } else {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (width == 0ul || height == 0ul ||
        width > V9X_NV3_EXTENT_MAX || height > V9X_NV3_EXTENT_MAX ||
        pitch_bytes == 0ul || pitch_bytes > V9X_NV3_PITCH_MAX ||
        (pitch_bytes & (V9X_NV3_SURFACE_ALIGN - 1ul)) != 0ul ||
        pitch_bytes / bytes_per_pixel < width) {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* RivaTablePMC: interrupts off, then PFIFO (bit 8) and PGRAPH (bit 12)
     * cycled off and on with every other unit on. */
    v9x_nv3_put(setup->before, &setup->before_count, V9X_NV3_PMC_INTR_EN,
                0x00000000ul);
    v9x_nv3_put(setup->before, &setup->before_count, V9X_NV3_PMC_ENABLE,
                0xffff00fful);
    v9x_nv3_put(setup->before, &setup->before_count, V9X_NV3_PMC_ENABLE,
                0xfffffffful);
    /* RivaTablePTIMER. */
    v9x_nv3_put(setup->before, &setup->before_count, 0x009200ul,
                0x00000008ul);
    v9x_nv3_put(setup->before, &setup->before_count, 0x009210ul,
                0x00000003ul);
    v9x_nv3_put(setup->before, &setup->before_count, 0x009140ul,
                0x00000000ul);
    v9x_nv3_put(setup->before, &setup->before_count, 0x009100ul,
                0xfffffffful);
    /* PFB before RAMIN, which LoadStateExt says it must be. */
    v9x_nv3_put(setup->before, &setup->before_count, V9X_NV3_PFB_CONFIG_0,
                v9x_nv3_pfb_config(bytes_per_pixel, width));
    for (index = 0ul; index < V9X_NV3_COUNT(v9x_nv3_pfifo); ++index) {
        v9x_nv3_put(setup->before, &setup->before_count,
                    v9x_nv3_pfifo[index].offset, v9x_nv3_pfifo[index].value);
    }

    for (index = 0ul; index < V9X_NV3_COUNT(v9x_nv3_ramht); ++index) {
        v9x_nv3_put(setup->ramin, &setup->ramin_count,
                    v9x_nv3_ramht[index].offset, v9x_nv3_ramht[index].value);
    }
    for (index = 0ul; index < V9X_NV3_BIND_COUNT; ++index) {
        v9x_nv3_put(setup->ramin, &setup->ramin_count,
                    v9x_nv3_instance_offsets[index], instance[index]);
    }

    for (index = 0ul; index < V9X_NV3_COUNT(v9x_nv3_pgraph); ++index) {
        v9x_nv3_put(setup->after, &setup->after_count,
                    v9x_nv3_pgraph[index].offset,
                    v9x_nv3_pgraph[index].value);
    }
    v9x_nv3_put(setup->after, &setup->after_count,
                V9X_NV3_PGRAPH_SURF_CONFIG, surf_config);
    for (index = 0ul; index < 4ul; ++index) {
        v9x_nv3_put(setup->after, &setup->after_count,
                    V9X_NV3_PGRAPH_SURF_OFFSET + index * 4ul, 0ul);
        v9x_nv3_put(setup->after, &setup->after_count,
                    V9X_NV3_PGRAPH_SURF_PITCH + index * 4ul, pitch_bytes);
    }

    for (index = 0ul; index < V9X_NV3_BIND_COUNT; ++index) {
        v9x_nv3_put(setup->bind, &setup->bind_count,
                    v9x_nv3_method(v9x_nv3_bindings[index].offset, 0ul),
                    v9x_nv3_bindings[index].value);
    }

    /* rivafb riva_setup_accel and riva_set_rop_solid: clip to the screen
     * (width low, height high), an all-ones pattern, SRCCOPY. */
    v9x_nv3_put(setup->defaults, &setup->defaults_count,
                v9x_nv3_method(V9X_NV3_SUB_CLIP, V9X_NV3_M_CLIP_POINT), 0ul);
    v9x_nv3_put(setup->defaults, &setup->defaults_count,
                v9x_nv3_method(V9X_NV3_SUB_CLIP, V9X_NV3_M_CLIP_SIZE),
                width | (height << 16));
    v9x_nv3_put(setup->defaults, &setup->defaults_count,
                v9x_nv3_method(V9X_NV3_SUB_PATTERN,
                               V9X_NV3_M_PATTERN_COLOR0), 0xfffffffful);
    v9x_nv3_put(setup->defaults, &setup->defaults_count,
                v9x_nv3_method(V9X_NV3_SUB_PATTERN,
                               V9X_NV3_M_PATTERN_COLOR1), 0xfffffffful);
    v9x_nv3_put(setup->defaults, &setup->defaults_count,
                v9x_nv3_method(V9X_NV3_SUB_PATTERN,
                               V9X_NV3_M_PATTERN_MONO0), 0xfffffffful);
    v9x_nv3_put(setup->defaults, &setup->defaults_count,
                v9x_nv3_method(V9X_NV3_SUB_PATTERN,
                               V9X_NV3_M_PATTERN_MONO1), 0xfffffffful);
    v9x_nv3_put(setup->defaults, &setup->defaults_count,
                v9x_nv3_method(V9X_NV3_SUB_ROP, V9X_NV3_M_ROP3),
                V9X_NV3_ROP_SRCCOPY);
    return V9X_STATUS_OK;
}

/*
 * A rectangle inside one surface as the engine addresses it: the surface
 * offset rounded down to the 16-byte unit PGRAPH holds, the remainder
 * folded into the left edge when it is whole pixels. Returns OK with the
 * engine's offset and left edge, INVALID_ARGUMENT for a rectangle the
 * surface does not hold, or UNSUPPORTED for one the engine cannot express.
 */
static v9x_status v9x_nv3_place(v9x_u32 vram_bytes, v9x_u32 offset,
                                v9x_u32 pitch_bytes, v9x_u32 bytes_per_pixel,
                                v9x_u32 left, v9x_u32 top, v9x_u32 width,
                                v9x_u32 height, v9x_u32 *engine_offset,
                                v9x_u32 *engine_left)
{
    v9x_u32 base;
    v9x_u32 shift;
    v9x_u32 row_bytes;
    v9x_u32 end;

    if (width == 0ul || height == 0ul || pitch_bytes == 0ul ||
        bytes_per_pixel == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (pitch_bytes > V9X_NV3_PITCH_MAX ||
        (pitch_bytes & (V9X_NV3_SURFACE_ALIGN - 1ul)) != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (left > pitch_bytes / bytes_per_pixel ||
        width > pitch_bytes / bytes_per_pixel - left ||
        top > V9X_NV3_EXTENT_MAX || height > V9X_NV3_EXTENT_MAX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    base = offset & ~(V9X_NV3_SURFACE_ALIGN - 1ul);
    shift = offset - base;
    if (shift % bytes_per_pixel != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* The last byte touched; top and height are 16-bit and pitch below
     * 8 KiB, so nothing here overflows. */
    row_bytes = width * bytes_per_pixel;
    end = (top + height - 1ul) * pitch_bytes + left * bytes_per_pixel +
          row_bytes;
    if (offset >= vram_bytes || end > vram_bytes - offset) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (offset + end > V9X_NV3_SURFACE_LIMIT) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (left + shift / bytes_per_pixel + width > V9X_NV3_EXTENT_MAX) {
        return V9X_STATUS_UNSUPPORTED;
    }
    *engine_offset = base;
    *engine_left = left + shift / bytes_per_pixel;
    return V9X_STATUS_OK;
}

/* The clip, covering the destination exactly: PGRAPH clips in destination
 * surface coordinates, and DirectDraw surfaces are not the screen's size. */
static void v9x_nv3_clip(struct v9x_nv3_op *op, v9x_u32 right,
                         v9x_u32 bottom)
{
    v9x_nv3_put(op->methods, &op->method_count,
                v9x_nv3_method(V9X_NV3_SUB_CLIP, V9X_NV3_M_CLIP_POINT), 0ul);
    v9x_nv3_put(op->methods, &op->method_count,
                v9x_nv3_method(V9X_NV3_SUB_CLIP, V9X_NV3_M_CLIP_SIZE),
                right | (bottom << 16));
}

v9x_status v9x_nv3_build_fill(const struct v9x_nv3_fill *fill,
                              struct v9x_nv3_op *op)
{
    v9x_u32 offset;
    v9x_u32 left;
    v9x_u32 color;
    v9x_status status;

    if (fill == 0 || op == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    op->surface_count = 0ul;
    op->method_count = 0ul;
    if (fill->bytes_per_pixel != 1ul && fill->bytes_per_pixel != 2ul &&
        fill->bytes_per_pixel != 4ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    status = v9x_nv3_place(fill->vram_bytes, fill->target_offset,
                           fill->pitch_bytes, fill->bytes_per_pixel,
                           fill->left, fill->top, fill->width, fill->height,
                           &offset, &left);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    /* The pixel's bits as the surface holds them; the probe drew raw
     * values at every depth. */
    color = fill->color;
    if (fill->bytes_per_pixel == 1ul) {
        color &= 0xfful;
    } else if (fill->bytes_per_pixel == 2ul) {
        color &= 0xfffful;
    }

    v9x_nv3_put(op->surface, &op->surface_count,
                V9X_NV3_PGRAPH_SURF_OFFSET, offset);
    v9x_nv3_put(op->surface, &op->surface_count,
                V9X_NV3_PGRAPH_SURF_PITCH, fill->pitch_bytes);
    v9x_nv3_clip(op, left + fill->width, fill->top + fill->height);
    /* rivafb_fillrect: the colour, then x in the high half of the point
     * and of the size; the size starts the fill. */
    v9x_nv3_put(op->methods, &op->method_count,
                v9x_nv3_method(V9X_NV3_SUB_BITMAP, V9X_NV3_M_RECT_COLOR),
                color);
    v9x_nv3_put(op->methods, &op->method_count,
                v9x_nv3_method(V9X_NV3_SUB_BITMAP, V9X_NV3_M_RECT_POINT),
                (left << 16) | fill->top);
    v9x_nv3_put(op->methods, &op->method_count,
                v9x_nv3_method(V9X_NV3_SUB_BITMAP, V9X_NV3_M_RECT_SIZE),
                (fill->width << 16) | fill->height);
    return V9X_STATUS_OK;
}

v9x_status v9x_nv3_build_copy(const struct v9x_nv3_copy *copy,
                              struct v9x_nv3_op *op)
{
    v9x_u32 source_offset;
    v9x_u32 source_left;
    v9x_u32 destination_offset;
    v9x_u32 destination_left;
    v9x_status status;

    if (copy == 0 || op == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    op->surface_count = 0ul;
    op->method_count = 0ul;
    if (copy->bytes_per_pixel != 1ul && copy->bytes_per_pixel != 2ul &&
        copy->bytes_per_pixel != 4ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    status = v9x_nv3_place(copy->vram_bytes, copy->source_offset,
                           copy->source_pitch_bytes, copy->bytes_per_pixel,
                           copy->source_left, copy->source_top, copy->width,
                           copy->height, &source_offset, &source_left);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    status = v9x_nv3_place(copy->vram_bytes, copy->destination_offset,
                           copy->destination_pitch_bytes,
                           copy->bytes_per_pixel, copy->destination_left,
                           copy->destination_top, copy->width, copy->height,
                           &destination_offset, &destination_left);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    /* Surface 0 the destination, surface 1 the source (measured: the
     * record's "Which surface is which"). */
    v9x_nv3_put(op->surface, &op->surface_count,
                V9X_NV3_PGRAPH_SURF_OFFSET, destination_offset);
    v9x_nv3_put(op->surface, &op->surface_count,
                V9X_NV3_PGRAPH_SURF_PITCH, copy->destination_pitch_bytes);
    v9x_nv3_put(op->surface, &op->surface_count,
                V9X_NV3_PGRAPH_SURF_OFFSET + 4ul, source_offset);
    v9x_nv3_put(op->surface, &op->surface_count,
                V9X_NV3_PGRAPH_SURF_PITCH + 4ul, copy->source_pitch_bytes);
    v9x_nv3_clip(op, destination_left + copy->width,
                 copy->destination_top + copy->height);
    /* rivafb_copyarea: y in the high half; the size starts the blit. The
     * engine picks the direction itself: overlapping copies in every
     * direction measured right. */
    v9x_nv3_put(op->methods, &op->method_count,
                v9x_nv3_method(V9X_NV3_SUB_BLIT, V9X_NV3_M_BLIT_SRC),
                (copy->source_top << 16) | source_left);
    v9x_nv3_put(op->methods, &op->method_count,
                v9x_nv3_method(V9X_NV3_SUB_BLIT, V9X_NV3_M_BLIT_DST),
                (copy->destination_top << 16) | destination_left);
    v9x_nv3_put(op->methods, &op->method_count,
                v9x_nv3_method(V9X_NV3_SUB_BLIT, V9X_NV3_M_BLIT_SIZE),
                (copy->height << 16) | copy->width);
    return V9X_STATUS_OK;
}
