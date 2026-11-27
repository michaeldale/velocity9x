/*
 * NV50-family (GT218) PFIFO channel and 2D engine structures, built for the
 * CPU to write into video memory.
 *
 * One G84-style channel in IB mode (class 826Fh), the PGRAPH context image,
 * a VM identity-mapping the first 512 MiB of video memory, and the
 * pushbuffer words for the 2D engine (class 502Dh). Pure policy, integer
 * only, no I/O: the caller writes the region through BAR1 (linear with no
 * BAR VM on the GT218, docs\decisions\2026-11-11-gt218-bar-windows.md) and
 * pokes the registers this header names. Step 2 of
 * docs\plans\nvidia-gt218-pgraph-2d.md.
 *
 * Every encoding is Linux nouveau's (nvkm/engine/fifo/g84.c and nv50.c,
 * nvkm/subdev/mmu/vmmnv50.c, nvkm/engine/dma/usernv50.c, nvkm/core/ramht.c,
 * nvkm/engine/gr/nv50.c, nvif/chan506f.c, nvhw/class/cl006c.h and
 * cl502d.h), unmeasured on the card unless a record says so. Where nouveau
 * allocates from its own heap this places things at fixed offsets; the
 * layout is the only part that is ours.
 */
#ifndef VELOCITY9X_NV50_CHAN_H
#define VELOCITY9X_NV50_CHAN_H

#include "velocity9x/types.h"
#include "velocity9x/status.h"
#include "velocity9x/nv50_grctx.h"

/*
 * The 16-bit driver compiles nv50_2d.c into a code segment of its own,
 * NV50CODE, because the nvidia family's _TEXT reached its budget when
 * gdi_accel.c gained the GT218 arm. Open Watcom's compact model calls near
 * by default and a near call cannot cross a segment, so every function
 * nv50_2d.c exports is declared and defined with this qualifier. Nothing in
 * nv50_2d.c calls out of its segment: no clibc helper either (wdis -a,
 * 2026-11-14), which is why nv3_2d.c, which calls __U4M and __U4D, stays in
 * _TEXT. The HAL, the probe's 32-bit hosts and the host tests have no
 * segments, so the qualifier is nothing there; intel16.h has the same
 * arrangement for the Intel family.
 */
#if defined(__WATCOMC__) && defined(_M_I86)
#define V9X_NV50_FAR __far
#else
#define V9X_NV50_FAR
#endif

/* ---- The region: offsets from its base, in bytes. ---- */

/* The 64 KiB instance block, 4 KiB aligned (fifo/nv50.c nv50_chan_inst). */
#define V9X_NV50_INST                0x00000ul
#define V9X_NV50_INST_BYTES          0x10000ul
/* Inside it: the engine context pointers (g84.c, `eng`, 200h). */
#define V9X_NV50_INST_ENG            0x00000ul
/* PGRAPH's pointer within them (g84.c g84_ectx_bind, ptr0 20h). */
#define V9X_NV50_ENG_GR              0x00020ul
/*
 * The rest follows the order nouveau's g84 channel allocates it in
 * (g84.c g84_chan_ramfc_write), as measured in an mmiotrace of nouveau
 * on a GT218 (docs/decisions/2026-11-13-gt218-nouveau-trace-pd-offset.md):
 * page directory + 200h, RAMFC + 4200h, cache + 4400h, RAMHT + 5400h.
 *
 * The page directory, at the offset the hardware fixes: 2048 eight-byte
 * entries. G84 and later read it at + 200h; only the original NV50 uses
 * 1400h (the saved mmu/nv50.c). This file had 1400h until 2026-11-13, with
 * a DMA object at 200h, which the chip then read as PDE 0.
 */
#define V9X_NV50_INST_PD             0x00200ul
#define V9X_NV50_PD_BYTES            0x04000ul
/* RAMFC, 100h bytes, 100h aligned (g84.c). */
#define V9X_NV50_INST_RAMFC          0x04200ul
/* PFIFO's cache area, 1000h bytes, 400h aligned (g84.c). */
#define V9X_NV50_INST_CACHE          0x04400ul
/* RAMHT, 8000h bytes: 4096 entries, a 12-bit hash (g84.c, ramht.c). */
#define V9X_NV50_INST_RAMHT          0x05400ul
#define V9X_NV50_RAMHT_BYTES         0x08000ul
#define V9X_NV50_RAMHT_BITS          12u
/* The VM DMA object the pushbuffer and the 2D engine use, 16 aligned. */
#define V9X_NV50_INST_DMA_VM         0x0e000ul
/* The 502D object, 16 aligned (gr/nv50.c nv50_gr_object_bind). */
#define V9X_NV50_INST_OBJ_2D         0x0e020ul
/* The Tesla 3D object, laid out as 502D's
 * (docs\plans\nvidia-gt218-3d.md). */
#define V9X_NV50_INST_OBJ_3D         0x0e040ul

/* The page table for the first PDE: 64 KiB pages, 8192 eight-byte entries,
 * 512 MiB (vmmnv50.c nv50_vmm_desc_16). */
#define V9X_NV50_PT                  0x10000ul
#define V9X_NV50_PT_BYTES            0x10000ul
#define V9X_NV50_PAGE_BYTES          0x10000ul
#define V9X_NV50_VM_BYTES            0x20000000ul

/* The runlist: one 4-byte entry, the channel id (fifo/nv50.c). */
#define V9X_NV50_RUNLIST             0x20000ul
/* The IB ring: 512 entries of 8 bytes (class 826F, chan506f.c). */
#define V9X_NV50_IB                  0x21000ul
#define V9X_NV50_IB_ENTRIES          512ul
#define V9X_NV50_IB_ENTRIES_LOG2     9u
/* The pushbuffer. */
#define V9X_NV50_PUSH                0x22000ul
#define V9X_NV50_PUSH_BYTES          0x04000ul
/* The PGRAPH context image, 4 KiB aligned. */
#define V9X_NV50_GRCTX               0x30000ul
#define V9X_NV50_GRCTX_MAX_BYTES     0x80000ul

/*
 * The alias windows' page tables (docs\plans\nvidia-gt218-3d-z.md): PDEs
 * 1 to 3 map the same 512 MiB of video memory as PDE 0, each with one
 * storage type ("kind", PTE bits 46:40, vmmnv50.c nv50_vmm_valid), so a
 * surface's layout is chosen by the VM address the engine is given. The
 * kinds are Mesa's, compression off (nv50_mt_choose_storage_type): 70h
 * tiled colour, 6Ch Z16, 28h Z24S8. Fixed when the channel is built, so
 * no PTE changes and no TLB flush at run time.
 */
#define V9X_NV50_PT_TILED            0xb0000ul
#define V9X_NV50_PT_Z16              0xc0000ul
#define V9X_NV50_PT_Z24S8            0xd0000ul
#define V9X_NV50_VM_TILED            0x20000000ul
#define V9X_NV50_VM_Z16              0x40000000ul
#define V9X_NV50_VM_Z24S8            0x60000000ul
#define V9X_NV50_KIND_TILED          0x70ul
#define V9X_NV50_KIND_Z16            0x6cul
#define V9X_NV50_KIND_Z24S8          0x28ul
/* The VM space the engines address, all four windows. */
#define V9X_NV50_VM_SPACE_BYTES      0x80000000ul

#define V9X_NV50_REGION_BYTES        (V9X_NV50_PT_Z24S8 + V9X_NV50_PT_BYTES)
#define V9X_NV50_REGION_WORDS        (V9X_NV50_REGION_BYTES / 4ul)

/* ---- Handles, subchannel, engine ids. ---- */

/* Our object handles; any value works, these hash apart. */
#define V9X_NV50_HANDLE_DMA_VM       0xbeef0201ul
#define V9X_NV50_HANDLE_2D           0xbeef502dul
/* nouveau binds 2D on subchannel 3 (its NvSub2D). */
#define V9X_NV50_SUBC_2D             3u
/* The Tesla 3D object, GT214_TESLA (8597h): the class nouveau's gt215_gr
 * lists for every GT21x chip, the GT218 included, and the one
 * xf86-video-nouveau picks for chipset A8h (NVA3_3D_CLASS). Subchannel 1
 * is free here; nouveau's own choice was not checked. */
#define V9X_NV50_HANDLE_3D           0xbeef8597ul
#define V9X_NV50_SUBC_3D             1u
/* RAMHT engine ids on the GT218's runlist (fifo/g98.c): DMA objects 0,
 * PGRAPH 1. */
#define V9X_NV50_ENGINE_DMA          0ul
#define V9X_NV50_ENGINE_GR           1ul
#define V9X_NV50_CLASS_2D            0x502dul
#define V9X_NV50_CLASS_3D            0x8597ul
/* The other Tesla 3D classes, by chipset (nv50_chan.c): NV84_3D for the
 * 80h and 90h families, the G96 (GeForce 9500 GT) among them; NVA0_3D
 * for GT200 (A0h); NVAF_3D for MCP89 (AFh). */
#define V9X_NV50_CLASS_3D_NV84       0x8297ul
#define V9X_NV50_CLASS_3D_NVA0       0x8397ul
#define V9X_NV50_CLASS_3D_NVAF       0x8697ul
#define V9X_NV50_CHIPSET_A0          0xa0ul
#define V9X_NV50_CHIPSET_AF          0xaful

/* ---- Registers (BAR0) the caller writes or polls. ---- */

#define V9X_NV50_PMC_ENABLE          0x000200ul
#define V9X_NV50_PMC_ENABLE_PFIFO    0x00000100ul
#define V9X_NV50_PMC_ENABLE_PGRAPH   0x00201000ul
#define V9X_NV50_PFIFO_INTR          0x002100ul
#define V9X_NV50_PFIFO_INTR_EN       0x002140ul
#define V9X_NV50_PFIFO_CACHES        0x002500ul
#define V9X_NV50_PFIFO_CHAN(chid)    (0x002600ul + (v9x_u32)(chid) * 4ul)
#define V9X_NV50_PFIFO_CHAN_COUNT    128u
#define V9X_NV50_PFIFO_CHAN_ENABLE   0x80000000ul
#define V9X_NV50_PFIFO_RUNLIST_ADDR  0x0032f4ul
#define V9X_NV50_PFIFO_RUNLIST_COUNT 0x0032ecul
#define V9X_NV50_PFIFO_RUNLIST_BUSY  0x00000100ul
/* USERD in BAR0 (fifo/nv50.c nv50_chan_userd), and in it the IB GET and
 * PUT (chan506f.c) and the reference counter (nouveau's nv10_fence,
 * unverified here). */
#define V9X_NV50_USERD(chid)         (0xc00000ul + (v9x_u32)(chid) * 0x2000ul)
#define V9X_NV50_USERD_REF           0x48ul
#define V9X_NV50_USERD_IB_GET        0x88ul
#define V9X_NV50_USERD_IB_PUT        0x8cul
/* VM TLB flush (vmmnv50.c nv50_vmm_flush): unit id << 16 | trigger. */
#define V9X_NV50_VM_FLUSH            0x100c80ul
#define V9X_NV50_VM_FLUSH_GR         0x00000000ul
#define V9X_NV50_VM_FLUSH_TRIGGER    0x00000001ul

/* ---- The pushbuffer. ---- */

/* NV06C method header (cl006c.h): count 28:18, subchannel 15:13, method
 * 12:2, opcode 31:29 = 0 (incrementing). */
#define V9X_NV50_PUSH_HEADER(subc, method, count) \
    ((((v9x_u32)(count)) << 18) | (((v9x_u32)(subc)) << 13) | \
     ((v9x_u32)(method) & 0x1ffcul))
/* PFIFO's own methods: bind an object to a subchannel, and the reference
 * counter USERD + 48h reads back (nv10_fence's REF_CNT, unverified). */
#define V9X_NV50_MTHD_SET_OBJECT     0x0000ul
#define V9X_NV50_MTHD_REF_CNT        0x0050ul

/* 502D methods (cl502d.h). */
#define V9X_502D_SET_DST_CONTEXT_DMA      0x0184ul
#define V9X_502D_SET_DST_FORMAT           0x0200ul
#define V9X_502D_SET_DST_MEMORY_LAYOUT    0x0204ul
#define V9X_502D_SET_DST_BLOCK_DIMENSIONS 0x0208ul
#define V9X_502D_SET_DST_DEPTH            0x020cul
#define V9X_502D_SET_DST_LAYER            0x0210ul
#define V9X_502D_SET_DST_PITCH            0x0214ul
#define V9X_502D_SET_DST_WIDTH            0x0218ul
#define V9X_502D_SET_DST_HEIGHT           0x021cul
#define V9X_502D_SET_DST_OFFSET_UPPER     0x0220ul
#define V9X_502D_SET_DST_OFFSET_LOWER     0x0224ul
/* The source surface and the blit (cl502d.h; BLIT_CONTROL's fields from
 * Mesa's nv50_2d.xml.h: bit 0 origin corner, bit 4 bilinear). */
#define V9X_502D_SET_SRC_CONTEXT_DMA      0x0188ul
#define V9X_502D_SET_SRC_FORMAT           0x0230ul
#define V9X_502D_SET_SRC_PITCH            0x0244ul
#define V9X_502D_SET_SAFE_OVERLAP         0x0888ul
#define V9X_502D_BLIT_CONTROL             0x088cul
#define V9X_502D_BLIT_DST_X0              0x08b0ul
#define V9X_502D_BLIT_POINT_CENTER        0ul
#define V9X_502D_SET_CLIP_ENABLE          0x0290ul
#define V9X_502D_SET_OPERATION            0x02acul
#define V9X_502D_RENDER_SOLID_PRIM_MODE   0x0580ul
#define V9X_502D_SET_SOLID_COLOR_FORMAT   0x0584ul
#define V9X_502D_SET_SOLID_COLOR          0x0588ul
#define V9X_502D_SOLID_POINT_X0           0x0600ul
#define V9X_502D_LAYOUT_PITCH             1ul
#define V9X_502D_OPERATION_SRCCOPY        3ul
#define V9X_502D_PRIM_RECTS               4ul
#define V9X_502D_FORMAT_R5G6B5            0xe8ul
#define V9X_502D_FORMAT_X8R8G8B8          0xe6ul
#define V9X_502D_FORMAT_A8R8G8B8          0xcful
/* One 8-bit channel, written as given: the 8 bpp surface format run 34
 * measured on M-BXMFA8. */
#define V9X_502D_FORMAT_Y8                0xf3ul

/* What the caller needs after the build: absolute video memory addresses
 * (base + offset) and the register values they imply. */
struct v9x_nv50_chan_layout {
    v9x_u32 base;
    v9x_u32 chid;
    v9x_u32 inst_addr;
    v9x_u32 ramfc_addr;
    v9x_u32 runlist_addr;
    v9x_u32 ib_addr;
    v9x_u32 push_addr;
    v9x_u32 grctx_addr;
    v9x_u32 grctx_bytes;
    /* 2600h + chid * 4: RAMFC >> 8 with the enable bit (g84.c bind,
     * nv50.c start). */
    v9x_u32 chan_entry;
    /* 32F4h: the runlist >> 12; 32ECh: its entry count. */
    v9x_u32 runlist_reg;
    v9x_u32 runlist_count;
    /* The context program to upload (400324h = 0, then 400328h). */
    v9x_u32 program[V9X_NV50_GRCTX_PROGRAM_MAX];
    v9x_u32 program_words;
};

/*
 * Build every structure of one channel into region, which stands for
 * V9X_NV50_REGION_BYTES of video memory starting at base: zeroed, then the
 * instance block, page directory and table, objects, RAMHT, RAMFC, runlist
 * and context image. The IB ring and pushbuffer are left zero for
 * v9x_nv50_push_*. base must be 64 KiB aligned and the region must end
 * inside the mapped 512 MiB; chid 1 to 126 (fifo/nv50.c: 0 and 127 are not
 * usable).
 */
v9x_status v9x_nv50_chan_build(v9x_u32 base, v9x_u32 chid,
                               const struct v9x_nv50_grctx_input *grctx,
                               v9x_u32 *region, v9x_u32 region_words,
                               struct v9x_nv50_chan_layout *layout);

/* RAMHT slot for a handle (ramht.c nvkm_ramht_hash, with nouveau's chid 0
 * for every object on NV50, fifo/nv50.c nv50_eobj_ramht_add). */
v9x_u32 v9x_nv50_ramht_hash(v9x_u32 handle);

/* ---- Pushbuffer and IB. ---- */

struct v9x_nv50_push {
    v9x_u32 *words;
    v9x_u32 max;
    v9x_u32 count;
    /* Set by a write past max; the stream is then unusable. */
    v9x_u32 overflow;
};

void V9X_NV50_FAR v9x_nv50_push_init(struct v9x_nv50_push *push, v9x_u32 *words,
                                     v9x_u32 max);
/* A header and count data words, incrementing from method. */
void V9X_NV50_FAR v9x_nv50_push_method(struct v9x_nv50_push *push, v9x_u32 subc,
                                       v9x_u32 method, v9x_u32 count,
                                       const v9x_u32 *data);
/* Bind the 502D object to its subchannel and point it at the VM DMA
 * object. */
void V9X_NV50_FAR v9x_nv50_push_bind_2d(struct v9x_nv50_push *push);
/* A solid rectangle into a pitch-linear surface at VM address dst (the
 * VM is the identity, so a video memory address). */
void V9X_NV50_FAR v9x_nv50_push_fill(struct v9x_nv50_push *push, v9x_u32 dst,
                                     v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
                                     v9x_u32 format, v9x_u32 colour,
                                     v9x_u32 x, v9x_u32 y, v9x_u32 w, v9x_u32 h);
/* A 1:1 copy of w x h pixels from (sx, sy) to (dx, dy) inside one
 * pitch-linear surface at VM address surface. Safe overlap is asked for,
 * so the rectangles may overlap. */
void V9X_NV50_FAR v9x_nv50_push_copy(struct v9x_nv50_push *push, v9x_u32 surface,
                                     v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
                                     v9x_u32 format,
                                     v9x_u32 sx, v9x_u32 sy, v9x_u32 dx, v9x_u32 dy,
                                     v9x_u32 w, v9x_u32 h);
/* The same copy between two surfaces of one format, each with its own
 * address, pitch and size. Measured on a card only as the one-surface
 * case above (run 33 on M-BXMFA8); two surfaces put different values in
 * the same words. */
void V9X_NV50_FAR v9x_nv50_push_blit(struct v9x_nv50_push *push,
                                     v9x_u32 src_surface, v9x_u32 src_pitch,
                                     v9x_u32 src_width, v9x_u32 src_height,
                                     v9x_u32 dst_surface, v9x_u32 dst_pitch,
                                     v9x_u32 dst_width, v9x_u32 dst_height,
                                     v9x_u32 format,
                                     v9x_u32 sx, v9x_u32 sy, v9x_u32 dx, v9x_u32 dy,
                                     v9x_u32 w, v9x_u32 h);
/*
 * What a live channel built at base looks like from the registers,
 * without building it: the value of its channel table entry
 * (2600h + chid * 4), PGRAPH's current-context register (40032Ch) while
 * it holds this channel, and where its pushbuffer and IB ring are. The
 * HAL and the 16-bit driver both test the first two before every
 * operation, and neither needs the 704 KiB region to compute them. They
 * are the values v9x_nv50_chan_build's layout reports (host-tested).
 */
v9x_u32 V9X_NV50_FAR v9x_nv50_live_chan_entry(v9x_u32 base);
v9x_u32 V9X_NV50_FAR v9x_nv50_live_context(v9x_u32 base);
v9x_u32 V9X_NV50_FAR v9x_nv50_live_push(v9x_u32 base);
v9x_u32 V9X_NV50_FAR v9x_nv50_live_ib(v9x_u32 base);
/* The IB ring index after index: the ring holds V9X_NV50_IB_ENTRIES and
 * wraps (run 35 carried it past the end). */
v9x_u32 V9X_NV50_FAR v9x_nv50_ib_next(v9x_u32 index);
/* The 502D surface format for a desktop depth in bytes per pixel: 1, 2
 * (5:6:5) and 4, the three measured in run 34. 0 for any other, which the
 * engine must not draw at. */
v9x_u32 V9X_NV50_FAR v9x_nv50_2d_format(v9x_u32 bytes_per_pixel);
/* The two words of an IB entry for words dwords at VM address addr, a
 * main (not subroutine) entry (chan506f.c gpfifo_push). */
void V9X_NV50_FAR v9x_nv50_ib_entry(v9x_u32 addr, v9x_u32 words, v9x_u32 *entry);

#endif
