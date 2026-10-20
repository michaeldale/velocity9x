/*
 * Haswell (Gen7.5) integrated graphics: the configuration-space decode.
 *
 * Phase 1 of docs\plans\intel-haswell-bringup.md. Before any Haswell code can
 * touch the GPU it has to know where the MMIO, the GTT, the aperture and
 * the stolen memory are, and every one of those facts comes out of PCI
 * config space at 00:00.0 and 00:02.0. This header and
 * src\chipsets\intel\hsw_config.c turn those raw words into a layout. They
 * do no I/O: the words come from a V9XSURV report today and from ring 0
 * later, and the decode is host-tested against both.
 *
 * Why this is not the Gen3 code with new constants: on Gen3 the GTT has its
 * own BAR (BAR3), the stolen size is a 3-bit index into an irregular table,
 * and the aperture size is read off the BAR. Haswell folds the GTT into the
 * upper half of a 4 MB BAR0, makes the stolen size a linear 5-bit multiple
 * of 32 MB, adds a separate GTT-stolen size, and puts the aperture size in
 * MSAC. Same questions, different registers.
 *
 * Sources, all in C:\everything\claude\personal\intel driver research:
 *   - Desktop 4th Gen Core datasheet Vol 2, 328898-003: GGC (3.1.13), BDSM
 *     (3.1.34), BGSM (3.1.35), GTTMMADR (3.5.10), GMADR (3.5.11).
 *   - HSW PRM Vol 12 (PCIe config): MGGC0 at 00:02.0 50h mirrors GGC; MSAC
 *     at 00:02.0 62h.
 *   - HSW PRM Vol 5 (Memory Views): the GTT page table entry.
 * None of it has been checked against a Haswell machine. The first V9XSURV
 * report from the i5-4570 is the test; see the plan.
 */
#ifndef VELOCITY9X_INTEL_HSW_H
#define VELOCITY9X_INTEL_HSW_H

#include "velocity9x/status.h"
#include "velocity9x/types.h"

/* Config offsets. GGC lives on the host bridge (00:00.0) and is mirrored,
 * read-only, as MGGC0 on the IGD (00:02.0); the decode takes either. */
#define V9X_INTEL_HSW_CFG_GGC      ((v9x_u16)0x50u)
#define V9X_INTEL_HSW_CFG_BDSM     ((v9x_u16)0x5cu) /* 00:02.0 */
#define V9X_INTEL_HSW_CFG_HOST_BDSM ((v9x_u16)0xb0u) /* 00:00.0 */
#define V9X_INTEL_HSW_CFG_HOST_BGSM ((v9x_u16)0xb4u) /* 00:00.0 */
#define V9X_INTEL_HSW_CFG_MSAC     ((v9x_u16)0x62u) /* 00:02.0, 8 bits */

/*
 * GGC, datasheet 3.1.13. GMS (7:3) is the data-stolen size in 32 MB steps,
 * 00h to 10h; anything above 10h is undefined there and is refused here.
 * GGMS (9:8) is the GTT-stolen size: 0, 1 or 2 MB, 3 reserved. IVD (1) set
 * means the IGD does not claim VGA cycles. GGCLCK (0) is the lock.
 */
#define V9X_INTEL_HSW_GGC_LOCK       ((v9x_u16)0x0001u)
#define V9X_INTEL_HSW_GGC_IVD        ((v9x_u16)0x0002u)
#define V9X_INTEL_HSW_GGC_GMS_SHIFT  3u
#define V9X_INTEL_HSW_GGC_GMS_MASK   ((v9x_u16)0x001fu)
#define V9X_INTEL_HSW_GGC_GMS_MAX    ((v9x_u16)0x0010u)
#define V9X_INTEL_HSW_GGC_GGMS_SHIFT 8u
#define V9X_INTEL_HSW_GGC_GGMS_MASK  ((v9x_u16)0x0003u)
#define V9X_INTEL_HSW_GGC_VAMEN      ((v9x_u16)0x4000u)
#define V9X_INTEL_HSW_GMS_UNIT_BYTES ((v9x_u32)0x02000000ul) /* 32 MB */

/*
 * BDSM and BGSM: base in 31:20, lock in bit 0 (datasheet 3.1.34/3.1.35).
 * The GTT-stolen block sits immediately BELOW data-stolen: BIOS sets BGSM to
 * BDSM minus the GGMS size. (Both registers' prose cites "offset 52 bits
 * 7:4", the Gen3 location; their own field tables say 50h bits 7:3 and
 * 9:8, which is what GGC's definition says. The field tables are used.)
 */
#define V9X_INTEL_HSW_BASE_MASK ((v9x_u32)0xfff00000ul)

/*
 * GTTMMADR, datasheet 3.5.10: a 64-bit BAR claiming 4 MB, MMIO in the low
 * 2 MB and the GTT "at (GTTMMADR + 2 MB)". MEMTYP (2:1) is 10b for 64-bit.
 * GMADR, 3.5.11: 64-bit prefetchable, the CPU's linear view of graphics
 * memory through the GTT.
 */
#define V9X_INTEL_HSW_GTTMMADR_BYTES ((v9x_u32)0x00400000ul)
#define V9X_INTEL_HSW_GTT_OFFSET     ((v9x_u32)0x00200000ul)
#define V9X_INTEL_HSW_GTTMMADR_MASK  ((v9x_u32)0xffc00000ul)
#define V9X_INTEL_HSW_BAR_MEM64      ((v9x_u32)0x00000004ul)
#define V9X_INTEL_HSW_BAR_TYPE_MASK  ((v9x_u32)0x00000007ul)

/*
 * MSAC, Vol 12: LHSASH (2) and LHSASL (1) together set the aperture size.
 * 00b 128 MB, 01b 256 MB, 11b 512 MB, 10b "Illegal Programming".
 */
#define V9X_INTEL_HSW_MSAC_SIZE_SHIFT 1u
#define V9X_INTEL_HSW_MSAC_SIZE_MASK  ((v9x_u8)0x03u)

/*
 * The GTT PTE, Vol 5 "NEW Page Table Entry": one dword per 4 KB page.
 * 31:12 address 31:12; 11 cacheability control bit 3; 10:4 address 38:32,
 * "must be zero for 32 bit addresses"; 3:1 cacheability control 2:0;
 * 0 valid. Windows 98 has no physical memory above 4 GB, so a nonzero
 * extension is decoded but refused by the encoder.
 */
#define V9X_INTEL_HSW_PTE_VALID      ((v9x_u32)0x00000001ul)
#define V9X_INTEL_HSW_PTE_PAGE_MASK  ((v9x_u32)0xfffff000ul)
#define V9X_INTEL_HSW_PTE_HIGH_MASK  ((v9x_u32)0x000007f0ul)
#define V9X_INTEL_HSW_PTE_HIGH_SHIFT 4u
#define V9X_INTEL_HSW_PTE_CC3        ((v9x_u32)0x00000800ul)
#define V9X_INTEL_HSW_PTE_CC20_MASK  ((v9x_u32)0x0000000eul)
#define V9X_INTEL_HSW_PTE_BYTES      ((v9x_u32)4ul)
#define V9X_INTEL_HSW_PAGE_BYTES     ((v9x_u32)4096ul)

/*
 * Cacheability control [3:0], Vol 5's table. Only the two this driver has
 * a use for are named: UC, and WB in LLC/eLLC at the youngest age. The
 * other fourteen encodings are valid hardware values and decode as-is.
 */
#define V9X_INTEL_HSW_CACHE_UC    ((v9x_u8)0x0u)
#define V9X_INTEL_HSW_CACHE_WB_A3 ((v9x_u8)0x8u)
#define V9X_INTEL_HSW_CACHE_MASK  ((v9x_u8)0x0fu)

/* What the layout decode could not accept. Each is a reason to stop. */
#define V9X_INTEL_HSW_LAYOUT_GMS_RESERVED   ((v9x_u16)0x0001u)
#define V9X_INTEL_HSW_LAYOUT_GGMS_RESERVED  ((v9x_u16)0x0002u)
#define V9X_INTEL_HSW_LAYOUT_NO_STOLEN      ((v9x_u16)0x0004u)
#define V9X_INTEL_HSW_LAYOUT_BAR0_TYPE      ((v9x_u16)0x0008u)
#define V9X_INTEL_HSW_LAYOUT_BAR2_TYPE      ((v9x_u16)0x0010u)
#define V9X_INTEL_HSW_LAYOUT_ABOVE_4G       ((v9x_u16)0x0020u)
#define V9X_INTEL_HSW_LAYOUT_MSAC_ILLEGAL   ((v9x_u16)0x0040u)
#define V9X_INTEL_HSW_LAYOUT_GTT_TOO_SMALL  ((v9x_u16)0x0080u)
#define V9X_INTEL_HSW_LAYOUT_BGSM_MISMATCH  ((v9x_u16)0x0100u)
#define V9X_INTEL_HSW_LAYOUT_UNASSIGNED     ((v9x_u16)0x0200u)

/*
 * The BCS reserve Phase B1 measured (docs\decisions\2026-10-17-haswell-
 * phase-b1-bcs-ring.md): a 4 KB ring at aperture offset 1800000h and a 4 KB
 * status page after it, above the 16 MB DirectDraw heap and below the top
 * megabyte of stolen memory, which refused CPU writes. The mini-VDD maps
 * both contiguously; the HAL addresses the status page as ring + ring
 * bytes. Status dword 0 is the last sequence the blitter stored, dword 1
 * the last the CPU submitted.
 */
#define V9X_INTEL_HSW_RESERVE_OFFSET ((v9x_u32)0x01800000ul)
#define V9X_INTEL_HSW_RING_BYTES     ((v9x_u32)0x00001000ul)
#define V9X_INTEL_HSW_STATUS_OFFSET  ((v9x_u32)0x01801000ul)
/*
 * Phase F2 (docs\plans\intel-haswell-direct3d.md) grew the reserve to
 * 128 KB: the BCS ring and status page as before, the RCS ring after them,
 * then the render engine's state, batch and vertices. The probe region at
 * 1820000h starts where this ends.
 */
#define V9X_INTEL_HSW_RESERVE_BYTES  ((v9x_u32)0x00020000ul)
#define V9X_INTEL_HSW_RESERVE_PAGES  ((v9x_u32)32ul)
#define V9X_INTEL_HSW_RCS_RING_OFF   ((v9x_u32)0x00002000ul)
#define V9X_INTEL_HSW_STATE_OFF      ((v9x_u32)0x00004000ul)
/*
 * Kernel slots are 768 bytes, 48 of them, one per pixel-shader description
 * (v9x_intel_hsw_pipe_kernel_slot). Until 2026-10-20 they were 512 bytes,
 * 64 of them: 96 dwords after the prefetch margin, which every Direct3D
 * stage with specular and fog (112) and the largest two-unit kernel (140)
 * overflowed, so those draws were refused. The state area grew by 4 KB to
 * hold them, taken from the batch area (a draw's batch is under 2 KB).
 */
#define V9X_INTEL_HSW_STATE_BYTES    ((v9x_u32)0x0000d000ul)
#define V9X_INTEL_HSW_KERNEL_OFF     ((v9x_u32)0x00004000ul) /* in state */
#define V9X_INTEL_HSW_KERNEL_SLOT    ((v9x_u32)0x00000300ul)
#define V9X_INTEL_HSW_KERNEL_SLOTS   ((v9x_u32)48ul)
#define V9X_INTEL_HSW_BATCH_OFF      ((v9x_u32)0x00011000ul)
#define V9X_INTEL_HSW_BATCH_BYTES    ((v9x_u32)0x00001000ul)
#define V9X_INTEL_HSW_VERTEX_OFF     ((v9x_u32)0x00012000ul)
#define V9X_INTEL_HSW_VERTEX_BYTES   ((v9x_u32)0x0000e000ul)
/* Status dwords: BCS stored/submitted 0-1; RCS stored/submitted 8-9; the
 * mark the init batch ran, 10. */
#define V9X_INTEL_HSW_STATUS_RCS_DONE   ((v9x_u32)8ul)
#define V9X_INTEL_HSW_STATUS_RCS_SENT   ((v9x_u32)9ul)
#define V9X_INTEL_HSW_STATUS_RCS_INIT   ((v9x_u32)10ul)
#define V9X_INTEL_HSW_RCS_INIT_MARK     ((v9x_u32)0x48535749ul) /* "HSWI" */

/* RCS ring registers (Vol 2c). */
#define V9X_INTEL_HSW_RCS_TAIL   ((v9x_u32)0x00002030ul)
#define V9X_INTEL_HSW_RCS_HEAD   ((v9x_u32)0x00002034ul)
#define V9X_INTEL_HSW_RCS_START  ((v9x_u32)0x00002038ul)
#define V9X_INTEL_HSW_RCS_CTL    ((v9x_u32)0x0000203cul)

/* BCS ring registers (Vol 2c) and the head/tail offset fields. */
#define V9X_INTEL_HSW_BCS_TAIL   ((v9x_u32)0x00022030ul)
#define V9X_INTEL_HSW_BCS_HEAD   ((v9x_u32)0x00022034ul)
#define V9X_INTEL_HSW_BCS_START  ((v9x_u32)0x00022038ul)
#define V9X_INTEL_HSW_BCS_CTL    ((v9x_u32)0x0002203cul)
#define V9X_INTEL_HSW_HEAD_MASK  ((v9x_u32)0x001ffffcul)
#define V9X_INTEL_HSW_TAIL_MASK  ((v9x_u32)0x001ffff8ul)
#define V9X_INTEL_HSW_CTL_ENABLE ((v9x_u32)0x00000001ul)

struct v9x_intel_hsw_ggc {
    v9x_u32 stolen_bytes;     /* GMS * 32 MB */
    v9x_u32 gtt_stolen_bytes; /* GGMS: 0, 1 or 2 MB */
    v9x_u16 gms;
    v9x_u16 ggms;
    v9x_u16 vga_disabled;     /* IVD */
    v9x_u16 locked;           /* GGCLCK */
    v9x_u16 versatile_mode;   /* VAMEN: class 048000h, not a display */
};

/* The raw words a layout is built from, as V9XSURV reports them. */
struct v9x_intel_hsw_config {
    v9x_u16 ggc;           /* 00:00.0 50h, or the 00:02.0 mirror */
    v9x_u32 bdsm;          /* 00:02.0 5Ch */
    v9x_u32 host_bgsm;     /* 00:00.0 B4h, 0 if not read */
    v9x_u32 bar0_low;      /* 00:02.0 10h */
    v9x_u32 bar0_high;     /* 00:02.0 14h */
    v9x_u32 bar2_low;      /* 00:02.0 18h */
    v9x_u32 bar2_high;     /* 00:02.0 1Ch */
    v9x_u8 msac;           /* 00:02.0 62h */
};

struct v9x_intel_hsw_layout {
    struct v9x_intel_hsw_ggc ggc;
    v9x_u32 mmio_base;
    v9x_u32 gtt_base;          /* mmio_base + 2 MB */
    v9x_u32 gtt_entries;       /* gtt_stolen_bytes / 4 */
    v9x_u32 aperture_base;
    v9x_u32 aperture_bytes;
    v9x_u32 stolen_base;       /* BDSM 31:20 */
    v9x_u32 gtt_stolen_base;   /* stolen_base - gtt_stolen_bytes */
    v9x_u16 refused;           /* V9X_INTEL_HSW_LAYOUT_* */
};

struct v9x_intel_hsw_pte {
    v9x_u32 physical_low;  /* address 31:12 */
    v9x_u8 physical_high;  /* address 38:32 */
    v9x_u8 cache;          /* cacheability control 3:0 */
    v9x_u8 valid;
};

v9x_status v9x_intel_hsw_decode_ggc(v9x_u16 ggc,
                                    struct v9x_intel_hsw_ggc *out);

v9x_status v9x_intel_hsw_aperture_bytes(v9x_u8 msac, v9x_u32 *bytes);

v9x_status v9x_intel_hsw_decode_layout(
    const struct v9x_intel_hsw_config *config,
    struct v9x_intel_hsw_layout *out);

v9x_status v9x_intel_hsw_decode_pte(v9x_u32 raw,
                                    struct v9x_intel_hsw_pte *out);

v9x_status v9x_intel_hsw_encode_pte(v9x_u32 physical, v9x_u8 cache,
                                    v9x_u32 *raw);

#endif
