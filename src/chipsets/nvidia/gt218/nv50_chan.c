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

/* PTE bits 46:40, the storage type, as bits 14:8 of the high word. */
#define V9X_NV50_PTE_KIND_SHIFT      8u

/* One 512 MiB window: its PDE, and a page table mapping video memory
 * 0-512 MiB with kind in every entry, written as the identity's are. */
static void v9x_nv50_build_window(v9x_u32 *region, v9x_u32 base,
                                  v9x_u32 pde, v9x_u32 table, v9x_u32 kind)
{
    v9x_u32 page;
    v9x_u32 pages = V9X_NV50_VM_BYTES / V9X_NV50_PAGE_BYTES;

    v9x_nv50_put(region, V9X_NV50_INST + V9X_NV50_INST_PD + pde * 8ul,
                 (base + table) | V9X_NV50_PDE_64K_VRAM);
    for (page = 0ul; page < pages; ++page) {
        v9x_nv50_put(region, table + page * 8ul,
                     (page & ~(V9X_NV50_PTE_BLOCK_PAGES - 1ul)) *
                     V9X_NV50_PAGE_BYTES |
                     (V9X_NV50_PTE_LOG2BLK_MAX << V9X_NV50_PTE_LOG2BLK_SHIFT) |
                     V9X_NV50_PTE_VALID);
        v9x_nv50_put(region, table + page * 8ul + 4ul,
                     kind << V9X_NV50_PTE_KIND_SHIFT);
    }
}

static void v9x_nv50_build_vm(v9x_u32 *region, v9x_u32 base)
{
    v9x_u32 page;
    v9x_u32 pages = V9X_NV50_VM_BYTES / V9X_NV50_PAGE_BYTES;

    /* PDE 0 covers 0-512 MiB; PDEs 1 to 3 are the alias windows; the rest
     * stay 0, invalid. */
    v9x_nv50_put(region, V9X_NV50_INST + V9X_NV50_INST_PD,
                 (base + V9X_NV50_PT) | V9X_NV50_PDE_64K_VRAM);
    v9x_nv50_build_window(region, base, 1ul, V9X_NV50_PT_TILED,
                          V9X_NV50_KIND_TILED);
    v9x_nv50_build_window(region, base, 2ul, V9X_NV50_PT_Z16,
                          V9X_NV50_KIND_Z16);
    v9x_nv50_build_window(region, base, 3ul, V9X_NV50_PT_Z24S8,
                          V9X_NV50_KIND_Z24S8);

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

    /* The VM DMA object over the whole 2 GiB VM, the alias windows
     * included, since the engines address every surface through it:
     * flags, limit low, start low, limit high << 24 | start high, 0,
     * part << 16 (part VM 0). */
    v9x_nv50_put(region, dma + 0x00ul, V9X_NV50_DMA_VM_FLAGS0);
    v9x_nv50_put(region, dma + 0x04ul, V9X_NV50_VM_SPACE_BYTES - 1ul);
    v9x_nv50_put(region, dma + 0x08ul, 0ul);
    v9x_nv50_put(region, dma + 0x0cul, 0ul);
    v9x_nv50_put(region, dma + 0x10ul, 0ul);
    v9x_nv50_put(region, dma + 0x14ul, 0ul);

    /* The 502D object: its class, then three zeros. */
    v9x_nv50_put(region, obj + 0x00ul, V9X_NV50_CLASS_2D);
    /* The 3D object the same way. Nothing binds it until a 3D push does,
     * so a channel that draws 2D only is unchanged in what it runs. */
    v9x_nv50_put(region, V9X_NV50_INST + V9X_NV50_INST_OBJ_3D,
                 V9X_NV50_CLASS_3D);
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
    status = v9x_nv50_ramht_insert(region, V9X_NV50_HANDLE_2D,
                                   V9X_NV50_ENGINE_GR, V9X_NV50_INST_OBJ_2D);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    return v9x_nv50_ramht_insert(region, V9X_NV50_HANDLE_3D,
                                 V9X_NV50_ENGINE_GR, V9X_NV50_INST_OBJ_3D);
}
