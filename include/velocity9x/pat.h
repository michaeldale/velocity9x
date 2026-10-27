/*
 * Write-combining an aperture through the page attribute table: the decision
 * half.
 *
 * The MTRR route (include\velocity9x\mtrr.h) needs a free variable-range pair
 * and an aperture no other range covers. The HD 4600's BIOS uses all eight
 * pairs and covers its aperture with an explicit UC range
 * (docs\decisions\2026-10-25-intel-gen3-write-combining.md), so that route
 * refuses there. The PAT route types pages, not physical ranges: one IA32_PAT
 * entry is set to WC and the aperture mapping's PTEs select it. PAT WC over
 * MTRR UC is WC (SDM vol. 3, table 11-7), so the BIOS's ranges stay as they
 * are.
 *
 * Entry 4 is the one used. A 4 KiB PTE selects entry PAT*4 + PCD*2 + PWT, so
 * entry 4 is PAT=1 with PCD and PWT clear. Windows 98 never sets PTE bit 7
 * (it does not know the PAT exists) and its power-on value for entry 4 is WB,
 * the same as entry 0, so no page outside this driver's mappings selects it.
 * That is checked rather than assumed: entry 4 must read WB (nobody changed
 * it) or WC (this driver set it on an earlier Windows start without a CPU
 * reset). Anything else means some other software owns the entry, and the
 * answer is to do nothing.
 *
 * Same split as the MTRR policy: this header and src\common\pat.c take what
 * ring 0 read and return a plan or a reason. The mini-VDD re-checks only what
 * it can without interpreting (V9XMINI_FN_PAT_COVER in V9XMAPI.INC) and does
 * the write and the PTE edits.
 */
#ifndef VELOCITY9X_PAT_H
#define VELOCITY9X_PAT_H

#include "velocity9x/types.h"
#include "velocity9x/mtrr.h"

/*
 * What the CPU admits to for the PAT, in licence order as for the MTRRs:
 * CPUID, then RDMSR/WRMSR, then the PAT bit (CPUID.01h EDX[16]). Shared with
 * include\asm\V9XMAPI.INC; check-tree.ps1 asserts the two agree.
 */
#define V9X_PAT_CPU_CPUID ((v9x_u16)0x0001u)
#define V9X_PAT_CPU_MSR   ((v9x_u16)0x0002u)
#define V9X_PAT_CPU_PAT   ((v9x_u16)0x0004u)

/* PAT memory-type encodings (SDM vol. 3, table 11-10). */
#define V9X_PAT_TYPE_WC ((v9x_u32)0x01ul)
#define V9X_PAT_TYPE_WB ((v9x_u32)0x06ul)

/* Entry 4 is bits 7:0 of IA32_PAT's high dword. */
#define V9X_PAT_ENTRY4_MASK ((v9x_u32)0x000000FFul)

/* The 4 KiB PTE bits that select the entry: PAT (7), PCD (4), PWT (3). */
#define V9X_PAT_PTE_PAT ((v9x_u32)0x00000080ul)
#define V9X_PAT_PTE_PCD ((v9x_u32)0x00000010ul)
#define V9X_PAT_PTE_PWT ((v9x_u32)0x00000008ul)

/*
 * The lowest linear address a covered range may start at. Below 2 GiB is the
 * private arena, whose page-directory entries Windows 98 swaps per process;
 * an edit there would land in whichever process was current. Every mapping
 * this driver types (DPMI 0800h, _MapPhysToLinear) is in the shared or system
 * arena above it.
 */
#define V9X_PAT_LINEAR_FLOOR ((v9x_u32)0x80000000ul)

#define V9X_PAT_PAGE_BYTES ((v9x_u32)0x00001000ul)

/* Why no cover will be made, or V9X_PAT_OK. */
#define V9X_PAT_OK            ((v9x_u16)0u)
#define V9X_PAT_NO_CPUID      ((v9x_u16)1u)  /* pre-CPUID CPU */
#define V9X_PAT_NO_MSR        ((v9x_u16)2u)  /* no RDMSR/WRMSR */
#define V9X_PAT_NO_PAT        ((v9x_u16)3u)  /* CPU has no PAT */
#define V9X_PAT_ENTRY_IN_USE  ((v9x_u16)4u)  /* entry 4 neither WB nor WC */
#define V9X_PAT_BAD_RANGE     ((v9x_u16)5u)  /* zero, unaligned, low, wraps */
#define V9X_PAT_MTRR_ROUTE    ((v9x_u16)6u)  /* the MTRR plan did not refuse
                                              * for a reason PAT answers */

/*
 * What the mini-VDD is asked to do when reason is V9X_PAT_OK: check that
 * IA32_PAT's high dword still reads old_high (or already reads new_high), set
 * it to new_high, and select entry 4 in every PTE of the range.
 * msr_write is zero when entry 4 is already WC.
 */
struct v9x_pat_plan {
    v9x_u16 reason;
    v9x_u16 msr_write;
    v9x_u32 old_high;
    v9x_u32 new_high;
};

/*
 * Decide whether [linear, linear + bytes) may be write-combined through PAT
 * entry 4, given why the MTRR plan over the same aperture refused and the
 * CPU flags and IA32_PAT's high dword as ring 0 read them. plan is always
 * filled in; returns non-zero when reason is V9X_PAT_OK.
 *
 * The route comes first: only an MTRR refusal for ALREADY_COVERED (a valid
 * range covers the aperture) or NO_SLOT (every pair is in use) is answered.
 * Every other refusal (no MTRRs, MTRRs off, a default type that is not UC, a
 * bad aperture) is left alone, so PAT is taken only where the MTRR route was
 * possible in principle and the BIOS's ranges are what stop it.
 */
v9x_u16 v9x_pat_plan_wc(v9x_u16 mtrr_reason, v9x_u16 cpu_flags,
                        v9x_u32 pat_high, v9x_u32 linear, v9x_u32 bytes,
                        struct v9x_pat_plan *plan);

/* The PTE that selects entry 4 for the page pte maps: PAT set, PCD and PWT
 * clear, every other bit kept. The mini-VDD's edit (V9xMini_Pat_Cover),
 * stated here so it is tested. Host only: the display driver never calls it,
 * and its _TEXT has no bytes to spare. */
#if !defined(_M_I86)
v9x_u32 v9x_pat_pte_wc(v9x_u32 pte);
#endif

#endif /* VELOCITY9X_PAT_H */
