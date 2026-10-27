/*
 * Tests for the PAT write-combining decision.
 *
 * The fixture is the HD 4600 machine (W9QI5A83, i5-4570): its MTRR plan
 * refuses with ALREADY_COVERED (r=11, docs\decisions\2026-10-25-intel-gen3-
 * write-combining.md), and IA32_PAT is assumed at the power-on value the SDM
 * gives, 0007040600070406h, since nothing on Windows 98 changes it.
 */
#include <stdio.h>

#include "velocity9x/pat.h"

static unsigned int pat_failures = 0u;

#define PTCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++pat_failures; \
    } \
} while (0)

/* The 4570's MTRR refusal: pair 3 is UC over E0000000-FFFFFFFF. */
#define ROUTE V9X_MTRR_ALREADY_COVERED
#define CPU_ALL ((v9x_u16)(V9X_PAT_CPU_CPUID | V9X_PAT_CPU_MSR | \
                           V9X_PAT_CPU_PAT))
/* IA32_PAT's power-on high dword: entries 4-7 WB, WT, UC-, UC. */
#define PAT_HIGH_RESET ((v9x_u32)0x00070406ul)
/* The same with entry 4 already WC. */
#define PAT_HIGH_OURS  ((v9x_u32)0x00070401ul)
/* A DPMI 0800h mapping in the shared arena, 64 MiB as the HD 4600 maps. */
#define LINEAR_FB ((v9x_u32)0x83400000ul)
#define BYTES_FB  ((v9x_u32)0x04000000ul)

static void test_route(void)
{
    struct v9x_pat_plan plan;
    static const v9x_u16 refused[] = {
        V9X_MTRR_OK, V9X_MTRR_DISABLED, V9X_MTRR_DEFAULT_NOT_UC,
        V9X_MTRR_NO_APERTURE, V9X_MTRR_APERTURE_LOW, V9X_MTRR_NO_MTRR,
        V9X_MTRR_NO_WC, V9X_MTRR_WINDOW_SMALL
    };
    unsigned int index;

    /* The HD 4600's refusal, and the other BIOS-made one. */
    PTCHECK(v9x_pat_plan_wc(V9X_MTRR_ALREADY_COVERED, CPU_ALL,
                            PAT_HIGH_RESET, LINEAR_FB, BYTES_FB,
                            &plan) == V9X_TRUE);
    PTCHECK(v9x_pat_plan_wc(V9X_MTRR_NO_SLOT, CPU_ALL, PAT_HIGH_RESET,
                            LINEAR_FB, BYTES_FB, &plan) == V9X_TRUE);
    /* The MTRR route worked, or failed for a reason that is not the BIOS's
     * ranges: PAT is not taken, and nothing is planned. */
    for (index = 0u; index < sizeof(refused) / sizeof(refused[0]); ++index) {
        PTCHECK(v9x_pat_plan_wc(refused[index], CPU_ALL, PAT_HIGH_RESET,
                                LINEAR_FB, BYTES_FB, &plan) == V9X_FALSE);
        PTCHECK(plan.reason == V9X_PAT_MTRR_ROUTE);
        PTCHECK(plan.new_high == 0ul && plan.msr_write == 0u);
    }
}

static void test_capability_ladder(void)
{
    struct v9x_pat_plan plan;

    PTCHECK(v9x_pat_plan_wc(ROUTE,0u, PAT_HIGH_RESET, LINEAR_FB, BYTES_FB,
                            &plan) == V9X_FALSE);
    PTCHECK(plan.reason == V9X_PAT_NO_CPUID);
    PTCHECK(v9x_pat_plan_wc(ROUTE,V9X_PAT_CPU_CPUID | V9X_PAT_CPU_PAT,
                            PAT_HIGH_RESET, LINEAR_FB, BYTES_FB,
                            &plan) == V9X_FALSE);
    PTCHECK(plan.reason == V9X_PAT_NO_MSR);
    PTCHECK(v9x_pat_plan_wc(ROUTE,V9X_PAT_CPU_CPUID | V9X_PAT_CPU_MSR,
                            PAT_HIGH_RESET, LINEAR_FB, BYTES_FB,
                            &plan) == V9X_FALSE);
    PTCHECK(plan.reason == V9X_PAT_NO_PAT);
    /* A refusal leaves nothing to write. */
    PTCHECK(plan.old_high == 0ul && plan.new_high == 0ul &&
            plan.msr_write == 0u);
}

static void test_haswell_plan(void)
{
    struct v9x_pat_plan plan;

    PTCHECK(v9x_pat_plan_wc(ROUTE,CPU_ALL, PAT_HIGH_RESET, LINEAR_FB, BYTES_FB,
                            &plan) == V9X_TRUE);
    PTCHECK(plan.reason == V9X_PAT_OK);
    PTCHECK(plan.msr_write == V9X_TRUE);
    PTCHECK(plan.old_high == PAT_HIGH_RESET);
    /* Entry 4 alone changes, WB to WC; 5-7 are kept. */
    PTCHECK(plan.new_high == PAT_HIGH_OURS);

    /* Windows restarted without a CPU reset: entry 4 is already ours, so
     * there is nothing to write, only PTEs to type. */
    PTCHECK(v9x_pat_plan_wc(ROUTE,CPU_ALL, PAT_HIGH_OURS, LINEAR_FB, BYTES_FB,
                            &plan) == V9X_TRUE);
    PTCHECK(plan.msr_write == V9X_FALSE);
    PTCHECK(plan.new_high == PAT_HIGH_OURS);
}

static void test_entry_in_use(void)
{
    struct v9x_pat_plan plan;

    /* Somebody set entry 4 to UC, WT, WP or UC-: not ours to take. */
    PTCHECK(v9x_pat_plan_wc(ROUTE,CPU_ALL, 0x00070400ul, LINEAR_FB, BYTES_FB,
                            &plan) == V9X_FALSE);
    PTCHECK(plan.reason == V9X_PAT_ENTRY_IN_USE);
    PTCHECK(v9x_pat_plan_wc(ROUTE,CPU_ALL, 0x00070404ul, LINEAR_FB, BYTES_FB,
                            &plan) == V9X_FALSE);
    PTCHECK(v9x_pat_plan_wc(ROUTE,CPU_ALL, 0x00070405ul, LINEAR_FB, BYTES_FB,
                            &plan) == V9X_FALSE);
    PTCHECK(v9x_pat_plan_wc(ROUTE,CPU_ALL, 0x00070407ul, LINEAR_FB, BYTES_FB,
                            &plan) == V9X_FALSE);
    /* Entries 5-7 changed by someone else do not matter: only 4 is used. */
    PTCHECK(v9x_pat_plan_wc(ROUTE,CPU_ALL, 0x01010106ul, LINEAR_FB, BYTES_FB,
                            &plan) == V9X_TRUE);
    PTCHECK(plan.new_high == 0x01010101ul);
}

static void test_range(void)
{
    struct v9x_pat_plan plan;

    PTCHECK(v9x_pat_plan_wc(ROUTE,CPU_ALL, PAT_HIGH_RESET, LINEAR_FB, 0ul,
                            &plan) == V9X_FALSE);
    PTCHECK(plan.reason == V9X_PAT_BAD_RANGE);
    /* Not page-aligned at either end. */
    PTCHECK(v9x_pat_plan_wc(ROUTE,CPU_ALL, PAT_HIGH_RESET, LINEAR_FB + 4ul,
                            BYTES_FB, &plan) == V9X_FALSE);
    PTCHECK(v9x_pat_plan_wc(ROUTE,CPU_ALL, PAT_HIGH_RESET, LINEAR_FB,
                            BYTES_FB - 4ul, &plan) == V9X_FALSE);
    /* The private arena, whose PDEs change with the process. */
    PTCHECK(v9x_pat_plan_wc(ROUTE,CPU_ALL, PAT_HIGH_RESET, 0x7FFFF000ul,
                            0x1000ul, &plan) == V9X_FALSE);
    PTCHECK(plan.reason == V9X_PAT_BAD_RANGE);
    /* The floor itself is allowed. */
    PTCHECK(v9x_pat_plan_wc(ROUTE,CPU_ALL, PAT_HIGH_RESET, V9X_PAT_LINEAR_FLOOR,
                            0x1000ul, &plan) == V9X_TRUE);
    /* Ending exactly at 4 GiB wraps the sum to zero: refused, as is any
     * range past the top. */
    PTCHECK(v9x_pat_plan_wc(ROUTE,CPU_ALL, PAT_HIGH_RESET, 0xFFFFF000ul,
                            0x1000ul, &plan) == V9X_FALSE);
    PTCHECK(v9x_pat_plan_wc(ROUTE,CPU_ALL, PAT_HIGH_RESET, 0xFFFFF000ul,
                            0x2000ul, &plan) == V9X_FALSE);
    /* The 256 KiB engine reserve as _MapPhysToLinear hands it out. */
    PTCHECK(v9x_pat_plan_wc(ROUTE,CPU_ALL, PAT_HIGH_RESET, 0xC1A00000ul,
                            0x00040000ul, &plan) == V9X_TRUE);
}

static void test_pte(void)
{
    /* A present, writable, uncached (PCD|PWT) system page: entry 3 before,
     * entry 4 after, frame and every other bit kept. */
    PTCHECK(v9x_pat_pte_wc(0xE0000063ul | 0x18ul) == 0xE00000E3ul);
    /* Already selecting entry 4: unchanged. */
    PTCHECK(v9x_pat_pte_wc(0xE00000E3ul) == 0xE00000E3ul);
    /* PCD alone or PWT alone also go. */
    PTCHECK(v9x_pat_pte_wc(0xE0001073ul) == 0xE00010E3ul);
    PTCHECK(v9x_pat_pte_wc(0xE000106Bul) == 0xE00010E3ul);
    /* Bits above 7 (global, the OS's available bits) untouched. */
    PTCHECK(v9x_pat_pte_wc(0xE0002F03ul) == 0xE0002F83ul);
}

unsigned int v9x_run_pat_tests(void)
{
    pat_failures = 0u;
    test_route();
    test_capability_ladder();
    test_haswell_plan();
    test_entry_in_use();
    test_range();
    test_pte();
    return pat_failures;
}
