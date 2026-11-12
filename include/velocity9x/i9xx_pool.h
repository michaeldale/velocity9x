/*
 * The Gen3 texture pool, as arithmetic: system RAM pages mapped into the GTT
 * above the stolen run, for textures the 8 MiB the BIOS stole cannot hold.
 *
 * Plan: docs\plans\intel-gen3-system-memory-textures.md. Decisions and the
 * errata gate: docs\decisions\2026-11-12-intel-gen3-texture-pool-decisions.md.
 *
 * Nothing here allocates a page or writes a PTE. The mini-VDD allocates the
 * pages and reports their physical addresses; this unit decides how big the
 * pool is, where in the aperture it goes, and whether a page list may be
 * mapped there at all; the caller writes what it is handed. It is the
 * mtrr.c split: hardware mechanics behind a backend, the decision host-tested.
 *
 * Every refusal is a distinct reason, so a boot that declines the pool says
 * which rule declined it.
 */
#ifndef VELOCITY9X_I9XX_POOL_H
#define VELOCITY9X_I9XX_POOL_H

#include "velocity9x/types.h"

/* Decision 1: 32 MiB on a machine with 512 MiB of RAM or more. */
#define V9X_I9XX_POOL_BYTES          ((v9x_u32)0x02000000ul)
#define V9X_I9XX_POOL_MIN_RAM_BYTES  ((v9x_u32)0x20000000ul)
/* Below this a pool is not worth a heap: one 256x256 32 bpp mip tree is
 * about 350 KB, and the field cases decline dozens of them. */
#define V9X_I9XX_POOL_MIN_BYTES      ((v9x_u32)0x00400000ul)
/*
 * The pool's aperture offset is the stolen run's end rounded up to this.
 * The VBIOS points the entries between the stolen run and the next MiB at
 * its scratch page (2026-09-12-intel-phase2-gtt-inventory.md); starting on
 * a MiB boundary leaves them as the VBIOS made them and costs aperture
 * addresses only, not memory.
 */
#define V9X_I9XX_POOL_ALIGN_BYTES    ((v9x_u32)0x00100000ul)
#define V9X_I9XX_POOL_PAGE_BYTES     ((v9x_u32)4096ul)
/* The pool is sized in four-page grains: the mini-VDD hands its pages to
 * the display driver four at a time (V9XMINI_FN_I9XX_POOL_CHUNK). */
#define V9X_I9XX_POOL_GRAIN_BYTES    ((v9x_u32)0x00004000ul)
#define V9X_I9XX_POOL_MAX_EXCLUDED   4u

/* The 945 PTE for a system-memory page: physical address, valid, cache
 * type 0 (uncached), as every entry the VBIOS writes. */
#define V9X_I9XX_POOL_PTE_VALID      ((v9x_u32)0x00000001ul)

/* Refusal reasons. Zero is accepted. */
#define V9X_I9XX_POOL_OK             ((v9x_u16)0u)
#define V9X_I9XX_POOL_NO_RAM         ((v9x_u16)1u)  /* under 512 MiB */
#define V9X_I9XX_POOL_NO_ROOM        ((v9x_u16)2u)  /* mapping too small */
#define V9X_I9XX_POOL_BAD_LAYOUT     ((v9x_u16)3u)  /* inputs inconsistent */
#define V9X_I9XX_POOL_COUNT          ((v9x_u16)4u)  /* pages != pool pages */
#define V9X_I9XX_POOL_ALIGNMENT      ((v9x_u16)5u)  /* page not 4 KiB aligned */
#define V9X_I9XX_POOL_ZERO_PAGE      ((v9x_u16)6u)  /* physical page 0 */
#define V9X_I9XX_POOL_ORDER          ((v9x_u16)7u)  /* not ascending: twice */
#define V9X_I9XX_POOL_STOLEN         ((v9x_u16)8u)  /* page in stolen memory */
#define V9X_I9XX_POOL_EXCLUDED       ((v9x_u16)9u)  /* GTT, ring, HWS, ... */
#define V9X_I9XX_POOL_APERTURE       ((v9x_u16)10u) /* past aperture or GTT */

/* A physical range no pool page may lie in: [base, base + bytes). */
struct v9x_i9xx_pool_range {
    v9x_u32 base;
    v9x_u32 bytes;
};

struct v9x_i9xx_pool_layout {
    v9x_u32 bsm;              /* stolen memory's physical base */
    v9x_u32 stolen_bytes;     /* GGC's GMS, in bytes */
    v9x_u32 aperture_bytes;   /* GMADR's size (MSAC) */
    v9x_u32 gtt_entries;      /* entries the GTT holds */
    v9x_u32 mapped_bytes;     /* GMADR bytes the CPU has mapped from 0 */
    v9x_u32 ram_bytes;        /* system RAM the OS reports */
    v9x_u16 excluded_count;
    struct v9x_i9xx_pool_range excluded[V9X_I9XX_POOL_MAX_EXCLUDED];
};

struct v9x_i9xx_pool_plan {
    v9x_u32 aperture_offset;  /* GMADR offset of the pool's first byte */
    v9x_u32 bytes;            /* pool size; zero when refused */
    v9x_u32 first_entry;      /* GTT index of the pool's first page */
    v9x_u32 entry_count;      /* pages, one entry each */
    v9x_u16 reason;           /* V9X_I9XX_POOL_*; OK when accepted */
};

/*
 * Size and place the pool from the layout alone, before any page exists.
 * The mini-VDD is asked for plan->entry_count pages; a refusal leaves
 * bytes and entry_count zero and names the rule.
 */
v9x_u16 v9x_i9xx_pool_size(const struct v9x_i9xx_pool_layout *layout,
                           struct v9x_i9xx_pool_plan *plan);

/* Sort a page list ascending, in place (heapsort: no recursion, no
 * allocation). The GTT order of the pool's pages does not matter; sorting
 * is what lets the check below find a page listed twice in one pass. */
void v9x_i9xx_pool_sort(v9x_u32 *pages, v9x_u32 count);

/*
 * Check a sorted page list against the plan and the layout. Accepted only
 * if every page is 4 KiB aligned, non-zero, strictly ascending, outside
 * stolen memory and every excluded range, and the plan's entries fit the
 * GTT, the aperture and the CPU's mapping.
 */
v9x_u16 v9x_i9xx_pool_check(const struct v9x_i9xx_pool_layout *layout,
                            const struct v9x_i9xx_pool_plan *plan,
                            const v9x_u32 *pages, v9x_u32 count);

/*
 * The same checks in streaming form, for the DRV, which receives the pages
 * from the mini-VDD four at a time and has no room to sort 32 KiB of them:
 * the plan once, then each page. No order or repeat check: the pages come
 * from one _PageAllocate, which cannot hand out a page twice. Each returns
 * a V9X_I9XX_POOL_* reason, OK when accepted.
 */
v9x_u16 v9x_i9xx_pool_check_plan(const struct v9x_i9xx_pool_layout *layout,
                                 const struct v9x_i9xx_pool_plan *plan);
v9x_u16 v9x_i9xx_pool_check_page(const struct v9x_i9xx_pool_layout *layout,
                                 v9x_u32 page);

/* The PTE that maps one accepted page. */
v9x_u32 v9x_i9xx_pool_pte(v9x_u32 physical_page);

#endif
