/*
 * NV50-family channel, VM, objects and 2D pushbuffer words; see
 * include\velocity9x\nv50_chan.h. Pure policy, no I/O.
 */
#include "velocity9x/nv50_chan.h"

/* RAMFC words (fifo/g84.c g84_chan_ramfc_write), by byte offset. */
#define V9X_NV50_RAMFC_3C            0x3cul
#define V9X_NV50_RAMFC_3C_VALUE      0x403f6078ul
#define V9X_NV50_RAMFC_44            0x44ul
#define V9X_NV50_RAMFC_44_VALUE      0x01003ffful
#define V9X_NV50_RAMFC_PUSH_DMA      0x48ul
#define V9X_NV50_RAMFC_IB_LOW        0x50ul
#define V9X_NV50_RAMFC_IB_HIGH       0x54ul
#define V9X_NV50_RAMFC_IB_SIZE_SHIFT 16u
#define V9X_NV50_RAMFC_60            0x60ul
#define V9X_NV50_RAMFC_60_VALUE      0x7ffffffful
#define V9X_NV50_RAMFC_78            0x78ul
#define V9X_NV50_RAMFC_DEVM          0x7cul
/* 30000000h | the class default devm, FFFh (g84.c g84_chan_ramfc). */
#define V9X_NV50_RAMFC_DEVM_VALUE    0x30000ffful
#define V9X_NV50_RAMFC_RAMHT         0x80ul
#define V9X_NV50_RAMHT_SIZE_SHIFT    27u
#define V9X_NV50_RAMHT_SIZE_BASE     9u
/* 4 << 24: nouveau's SEARCH_FULL. */
#define V9X_NV50_RAMHT_SEARCH_FULL   0x04000000ul
#define V9X_NV50_RAMFC_CACHE         0x88ul
#define V9X_NV50_RAMFC_INST          0x98ul

/* The PGRAPH context pointer (g84.c g84_ectx_bind): flags, limit low,
 * start low, a fourth word, two zeros. */
#define V9X_NV50_ENG_FLAGS           0x00190000ul

/* A VM-target DMA object (dma/usernv50.c with nouveau's VM defaults:
 * COMP_VM 3 << 29, KIND_VM 7Fh << 22, PRIV_VM 0, target VM, access VM,
 * class NV_DMA_IN_MEMORY 3Dh). */
#define V9X_NV50_DMA_VM_FLAGS0       0x7fc0003dul

/* PDE for a 64 KiB-page table in video memory (vmmnv50.c nv50_vmm_pde). */
#define V9X_NV50_PDE_64K_VRAM        0x00000001ul
/* PTE: valid, video memory (aperture 0), kind 0 (pitch), read-write,
 * contiguity hint log2blk 7 (128 pages, vmmnv50.c nv50_vmm_pgt_pte). */
#define V9X_NV50_PTE_VALID           0x00000001ul
#define V9X_NV50_PTE_LOG2BLK_SHIFT   7u
#define V9X_NV50_PTE_LOG2BLK_MAX     7ul
#define V9X_NV50_PTE_BLOCK_PAGES     128ul

#define V9X_NV50_CHID_FIRST          1ul
#define V9X_NV50_CHID_LAST           126ul
#define V9X_NV50_BASE_ALIGN          0xfffful

static void v9x_nv50_put(v9x_u32 *region, v9x_u32 byte_offset,
                         v9x_u32 value)
{
    region[byte_offset / 4ul] = value;
}

v9x_u32 v9x_nv50_ramht_hash(v9x_u32 handle)
{
    v9x_u32 hash = 0ul;
    v9x_u32 mask = (1ul << V9X_NV50_RAMHT_BITS) - 1ul;

    /* XOR-fold the handle in 12-bit chunks; chid 0 adds nothing. */
    while (handle != 0ul) {
        hash ^= handle & mask;
        handle >>= V9X_NV50_RAMHT_BITS;
    }
    return hash;
}

/* One RAMHT entry, probing linearly from the hash as nouveau does. */
static v9x_status v9x_nv50_ramht_insert(v9x_u32 *region, v9x_u32 handle,
                                        v9x_u32 engine,
                                        v9x_u32 object_offset)
{
    v9x_u32 entries = V9X_NV50_RAMHT_BYTES / 8ul;
    v9x_u32 slot = v9x_nv50_ramht_hash(handle);
    v9x_u32 tries;
    v9x_u32 at;

    for (tries = 0ul; tries < entries; ++tries) {
        at = V9X_NV50_INST + V9X_NV50_INST_RAMHT + slot * 8ul;
        if (region[at / 4ul] == 0ul) {
            v9x_nv50_put(region, at, handle);
            /* Engine << 20 | the object's offset in the instance block
             * >> 4 (ramht.c nvkm_ramht_update, card_type >= NV50). */
            v9x_nv50_put(region, at + 4ul,
                         (engine << 20) | (object_offset >> 4));
            return V9X_STATUS_OK;
        }
        slot = (slot + 1ul) % entries;
    }
    return V9X_STATUS_INSUFFICIENT_MEMORY;
}

static void v9x_nv50_build_vm(v9x_u32 *region, v9x_u32 base)
{
    v9x_u32 page;
    v9x_u32 pages = V9X_NV50_VM_BYTES / V9X_NV50_PAGE_BYTES;

    /* PDE 0 covers 0-512 MiB; the rest stay 0, invalid. */
    v9x_nv50_put(region, V9X_NV50_INST + V9X_NV50_INST_PD,
                 (base + V9X_NV50_PT) | V9X_NV50_PDE_64K_VRAM);

    /* The identity: VM address = video memory address. Every 128-page
     * block is aligned, so every entry carries the largest hint, and with
     * it the block's first address, the same value in all 128 entries, as
     * nv50_vmm_pgt_pte writes it. The chip adds the page's place in the
     * block itself. Until 2026-11-14 each entry had its own address, so
     * every page but the first of a block resolved too high (run 28 on
     * M-BXMFA8: the IB ring read as empty). */
    for (page = 0ul; page < pages; ++page) {
        v9x_nv50_put(region, V9X_NV50_PT + page * 8ul,
                     (page & ~(V9X_NV50_PTE_BLOCK_PAGES - 1ul)) *
                     V9X_NV50_PAGE_BYTES |
                     (V9X_NV50_PTE_LOG2BLK_MAX << V9X_NV50_PTE_LOG2BLK_SHIFT) |
                     V9X_NV50_PTE_VALID);
    }
}

static void v9x_nv50_build_objects(v9x_u32 *region)
{
    v9x_u32 dma = V9X_NV50_INST + V9X_NV50_INST_DMA_VM;
    v9x_u32 obj = V9X_NV50_INST + V9X_NV50_INST_OBJ_2D;

    /* The VM DMA object over the mapped 512 MiB: flags, limit low, start
     * low, limit high << 24 | start high, 0, part << 16 (part VM 0). */
    v9x_nv50_put(region, dma + 0x00ul, V9X_NV50_DMA_VM_FLAGS0);
    v9x_nv50_put(region, dma + 0x04ul, V9X_NV50_VM_BYTES - 1ul);
    v9x_nv50_put(region, dma + 0x08ul, 0ul);
    v9x_nv50_put(region, dma + 0x0cul, 0ul);
    v9x_nv50_put(region, dma + 0x10ul, 0ul);
    v9x_nv50_put(region, dma + 0x14ul, 0ul);

    /* The 502D object: its class, then three zeros. */
    v9x_nv50_put(region, obj + 0x00ul, V9X_NV50_CLASS_2D);
}

static void v9x_nv50_build_engine(v9x_u32 *region, v9x_u32 grctx_addr,
                                  v9x_u32 grctx_bytes)
{
    v9x_u32 at = V9X_NV50_INST + V9X_NV50_INST_ENG + V9X_NV50_ENG_GR;
    v9x_u32 limit = grctx_addr + grctx_bytes - 1ul;

    v9x_nv50_put(region, at + 0x00ul, V9X_NV50_ENG_FLAGS);
    v9x_nv50_put(region, at + 0x04ul, limit);
    v9x_nv50_put(region, at + 0x08ul, grctx_addr);
    /* nouveau writes upper(limit) << 24 | lower(start) here, where its
     * DMA objects put upper(start) (dma/usernv50.c). Kept as nouveau has
     * it, the form known to run: below 4 GiB it is the start address. */
    v9x_nv50_put(region, at + 0x0cul, grctx_addr);
}

static void v9x_nv50_build_ramfc(v9x_u32 *region, v9x_u32 base)
{
    v9x_u32 at = V9X_NV50_INST + V9X_NV50_INST_RAMFC;
    v9x_u32 ib = base + V9X_NV50_IB;

    v9x_nv50_put(region, at + V9X_NV50_RAMFC_3C, V9X_NV50_RAMFC_3C_VALUE);
    v9x_nv50_put(region, at + V9X_NV50_RAMFC_44, V9X_NV50_RAMFC_44_VALUE);
    /* The pushbuffer DMA object: its offset in the instance block >> 4. */
    v9x_nv50_put(region, at + V9X_NV50_RAMFC_PUSH_DMA,
                 V9X_NV50_INST_DMA_VM >> 4);
    /* The IB ring's VM address and log2 of its entry count. */
    v9x_nv50_put(region, at + V9X_NV50_RAMFC_IB_LOW, ib);
    v9x_nv50_put(region, at + V9X_NV50_RAMFC_IB_HIGH,
                 (v9x_u32)V9X_NV50_IB_ENTRIES_LOG2 <<
                 V9X_NV50_RAMFC_IB_SIZE_SHIFT);
    v9x_nv50_put(region, at + V9X_NV50_RAMFC_60, V9X_NV50_RAMFC_60_VALUE);
    v9x_nv50_put(region, at + V9X_NV50_RAMFC_78, 0ul);
    v9x_nv50_put(region, at + V9X_NV50_RAMFC_DEVM,
                 V9X_NV50_RAMFC_DEVM_VALUE);
    /* RAMHT: size as bits - 9, full search, its offset in the instance
     * block >> 4. */
    v9x_nv50_put(region, at + V9X_NV50_RAMFC_RAMHT,
                 ((v9x_u32)(V9X_NV50_RAMHT_BITS - V9X_NV50_RAMHT_SIZE_BASE)
                  << V9X_NV50_RAMHT_SIZE_SHIFT) |
                 V9X_NV50_RAMHT_SEARCH_FULL |
                 (V9X_NV50_INST_RAMHT >> 4));
    /* The cache and the instance block, as absolute addresses. */
    v9x_nv50_put(region, at + V9X_NV50_RAMFC_CACHE,
                 (base + V9X_NV50_INST + V9X_NV50_INST_CACHE) >> 10);
    v9x_nv50_put(region, at + V9X_NV50_RAMFC_INST,
                 (base + V9X_NV50_INST) >> 12);
}

v9x_status v9x_nv50_chan_build(v9x_u32 base, v9x_u32 chid,
                               const struct v9x_nv50_grctx_input *grctx,
                               v9x_u32 *region, v9x_u32 region_words,
                               struct v9x_nv50_chan_layout *layout)
{
    v9x_u32 index;
    v9x_u32 image_words = 0ul;
    v9x_status status;

    if (grctx == 0 || region == 0 || layout == 0 ||
        region_words < V9X_NV50_REGION_WORDS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if ((base & V9X_NV50_BASE_ALIGN) != 0ul ||
        base > V9X_NV50_VM_BYTES - V9X_NV50_REGION_BYTES) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (chid < V9X_NV50_CHID_FIRST || chid > V9X_NV50_CHID_LAST) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0ul; index < V9X_NV50_REGION_WORDS; ++index) {
        region[index] = 0ul;
    }

    status = v9x_nv50_grctx_build(grctx, layout->program,
                                  &layout->program_words,
                                  region + V9X_NV50_GRCTX / 4ul,
                                  V9X_NV50_GRCTX_MAX_BYTES / 4ul,
                                  &image_words);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    layout->base = base;
    layout->chid = chid;
    layout->inst_addr = base + V9X_NV50_INST;
    layout->ramfc_addr = base + V9X_NV50_INST + V9X_NV50_INST_RAMFC;
    layout->runlist_addr = base + V9X_NV50_RUNLIST;
    layout->ib_addr = base + V9X_NV50_IB;
    layout->push_addr = base + V9X_NV50_PUSH;
    layout->grctx_addr = base + V9X_NV50_GRCTX;
    layout->grctx_bytes = image_words * 4ul;
    layout->chan_entry = (layout->ramfc_addr >> 8) |
                         V9X_NV50_PFIFO_CHAN_ENABLE;
    layout->runlist_reg = layout->runlist_addr >> 12;
    layout->runlist_count = 1ul;

    v9x_nv50_build_vm(region, base);
    v9x_nv50_build_objects(region);
    v9x_nv50_build_engine(region, layout->grctx_addr, layout->grctx_bytes);
    v9x_nv50_build_ramfc(region, base);
    v9x_nv50_put(region, V9X_NV50_RUNLIST, chid);

    status = v9x_nv50_ramht_insert(region, V9X_NV50_HANDLE_DMA_VM,
                                   V9X_NV50_ENGINE_DMA,
                                   V9X_NV50_INST_DMA_VM);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    return v9x_nv50_ramht_insert(region, V9X_NV50_HANDLE_2D,
                                 V9X_NV50_ENGINE_GR, V9X_NV50_INST_OBJ_2D);
}

void v9x_nv50_push_init(struct v9x_nv50_push *push, v9x_u32 *words,
                        v9x_u32 max)
{
    push->words = words;
    push->max = max;
    push->count = 0ul;
    push->overflow = 0ul;
}

static void v9x_nv50_push_word(struct v9x_nv50_push *push, v9x_u32 word)
{
    if (push->count >= push->max) {
        push->overflow = 1ul;
        return;
    }
    push->words[push->count++] = word;
}

void v9x_nv50_push_method(struct v9x_nv50_push *push, v9x_u32 subc,
                          v9x_u32 method, v9x_u32 count,
                          const v9x_u32 *data)
{
    v9x_u32 index;

    v9x_nv50_push_word(push, V9X_NV50_PUSH_HEADER(subc, method, count));
    for (index = 0ul; index < count; ++index) {
        v9x_nv50_push_word(push, data[index]);
    }
}

static void v9x_nv50_push_one(struct v9x_nv50_push *push, v9x_u32 method,
                              v9x_u32 value)
{
    v9x_nv50_push_method(push, V9X_NV50_SUBC_2D, method, 1ul, &value);
}

void v9x_nv50_push_bind_2d(struct v9x_nv50_push *push)
{
    v9x_nv50_push_one(push, V9X_NV50_MTHD_SET_OBJECT, V9X_NV50_HANDLE_2D);
    v9x_nv50_push_one(push, V9X_502D_SET_DST_CONTEXT_DMA,
                      V9X_NV50_HANDLE_DMA_VM);
}

void v9x_nv50_push_fill(struct v9x_nv50_push *push, v9x_u32 dst,
                        v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
                        v9x_u32 format, v9x_u32 colour,
                        v9x_u32 x, v9x_u32 y, v9x_u32 w, v9x_u32 h)
{
    v9x_u32 surface[10];
    v9x_u32 points[4];

    /* DST_FORMAT through DST_OFFSET_LOWER, 200h-224h, in one run: a
     * pitch-linear surface, one layer deep, block dimensions unused. */
    surface[0] = format;
    surface[1] = V9X_502D_LAYOUT_PITCH;
    surface[2] = 0ul;
    surface[3] = 1ul;
    surface[4] = 0ul;
    surface[5] = pitch;
    surface[6] = width;
    surface[7] = height;
    surface[8] = 0ul;
    surface[9] = dst;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_2D, V9X_502D_SET_DST_FORMAT,
                         10ul, surface);
    v9x_nv50_push_one(push, V9X_502D_SET_CLIP_ENABLE, 0ul);
    v9x_nv50_push_one(push, V9X_502D_SET_OPERATION,
                      V9X_502D_OPERATION_SRCCOPY);
    v9x_nv50_push_one(push, V9X_502D_RENDER_SOLID_PRIM_MODE,
                      V9X_502D_PRIM_RECTS);
    v9x_nv50_push_one(push, V9X_502D_SET_SOLID_COLOR_FORMAT, format);
    v9x_nv50_push_one(push, V9X_502D_SET_SOLID_COLOR, colour);

    /* One rectangle: its two corners, the second exclusive, as the old
     * nouveau fbcon fill passed them (recalled, not in the saved sources;
     * the probe's readback of the edges decides it). */
    points[0] = x;
    points[1] = y;
    points[2] = x + w;
    points[3] = y + h;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_2D, V9X_502D_SOLID_POINT_X0,
                         4ul, points);
}

void v9x_nv50_push_copy(struct v9x_nv50_push *push, v9x_u32 surface,
                        v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
                        v9x_u32 format,
                        v9x_u32 sx, v9x_u32 sy, v9x_u32 dx, v9x_u32 dy,
                        v9x_u32 w, v9x_u32 h)
{
    v9x_nv50_push_blit(push, surface, pitch, width, height,
                       surface, pitch, width, height, format,
                       sx, sy, dx, dy, w, h);
}

void v9x_nv50_push_blit(struct v9x_nv50_push *push,
                        v9x_u32 src_surface, v9x_u32 src_pitch,
                        v9x_u32 src_width, v9x_u32 src_height,
                        v9x_u32 dst_surface, v9x_u32 dst_pitch,
                        v9x_u32 dst_width, v9x_u32 dst_height,
                        v9x_u32 format,
                        v9x_u32 sx, v9x_u32 sy, v9x_u32 dx, v9x_u32 dy,
                        v9x_u32 w, v9x_u32 h)
{
    v9x_u32 dst[10];
    v9x_u32 src_format[2];
    v9x_u32 src[5];
    v9x_u32 blit[12];

    v9x_nv50_push_one(push, V9X_502D_SET_SRC_CONTEXT_DMA,
                      V9X_NV50_HANDLE_DMA_VM);

    /* The destination, as the fill sets it. */
    dst[0] = format;
    dst[1] = V9X_502D_LAYOUT_PITCH;
    dst[2] = 0ul;
    dst[3] = 1ul;
    dst[4] = 0ul;
    dst[5] = dst_pitch;
    dst[6] = dst_width;
    dst[7] = dst_height;
    dst[8] = 0ul;
    dst[9] = dst_surface;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_2D, V9X_502D_SET_DST_FORMAT,
                         10ul, dst);

    /* The source: format and layout at 230h, then pitch, width, height
     * and address from 244h. The words between belong to block-linear
     * surfaces and are left alone. */
    src_format[0] = format;
    src_format[1] = V9X_502D_LAYOUT_PITCH;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_2D, V9X_502D_SET_SRC_FORMAT,
                         2ul, src_format);
    src[0] = src_pitch;
    src[1] = src_width;
    src[2] = src_height;
    src[3] = 0ul;
    src[4] = src_surface;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_2D, V9X_502D_SET_SRC_PITCH,
                         5ul, src);

    v9x_nv50_push_one(push, V9X_502D_SET_CLIP_ENABLE, 0ul);
    v9x_nv50_push_one(push, V9X_502D_SET_OPERATION,
                      V9X_502D_OPERATION_SRCCOPY);
    v9x_nv50_push_one(push, V9X_502D_SET_SAFE_OVERLAP, 1ul);
    v9x_nv50_push_one(push, V9X_502D_BLIT_CONTROL,
                      V9X_502D_BLIT_POINT_CENTER);

    /* The blit, 8B0h-8DCh in one run: destination rectangle, a scale of
     * exactly 1 each way (fraction, integer), then the source corner
     * (fraction, integer). Writing the last word starts it. The order and
     * the control value are how nouveau's X driver copied (recalled, not
     * in the saved sources); the probe's readback decides them. */
    blit[0] = dx;
    blit[1] = dy;
    blit[2] = w;
    blit[3] = h;
    blit[4] = 0ul;
    blit[5] = 1ul;
    blit[6] = 0ul;
    blit[7] = 1ul;
    blit[8] = 0ul;
    blit[9] = sx;
    blit[10] = 0ul;
    blit[11] = sy;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_2D, V9X_502D_BLIT_DST_X0,
                         12ul, blit);
}

v9x_u32 v9x_nv50_2d_format(v9x_u32 bytes_per_pixel)
{
    if (bytes_per_pixel == 1ul) {
        return V9X_502D_FORMAT_Y8;
    }
    if (bytes_per_pixel == 2ul) {
        return V9X_502D_FORMAT_R5G6B5;
    }
    if (bytes_per_pixel == 4ul) {
        return V9X_502D_FORMAT_X8R8G8B8;
    }
    return 0ul;
}

void v9x_nv50_ib_entry(v9x_u32 addr, v9x_u32 words, v9x_u32 *entry)
{
    /* Address low; address high (0 here) | length in dwords << 10. */
    entry[0] = addr;
    entry[1] = words << 10;
}
