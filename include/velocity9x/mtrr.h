/*
 * Write-combining the framebuffer aperture: the decision half.
 *
 * Tier-0 draws with the CPU straight into the linear framebuffer, and on a PC
 * whose BIOS leaves the PCI hole at the MTRR default type that framebuffer is
 * uncached - every store goes to the bus on its own. Marking the aperture
 * write-combining with one variable-range MTRR is the standard fix and costs
 * nothing per frame.
 *
 * It is also the most dangerous single thing this driver could do: MTRRs are
 * global CPU state, a wrong range puts WC on somebody else's memory, and the
 * symptom is corruption a long way from here. So the work is split the way
 * every other risky decision in this project is split, and staged.
 *
 *   This header and src\common\mtrr.c are pure policy: no I/O, no MSR, no OS.
 *   They take the facts ring 0 read and return a plan, or a reason there is
 *   none. Every rule is therefore host-tested against a table of states,
 *   including the ones no machine here can produce.
 *
 *   The mini-VDD reads the MSRs and reports them. It decides nothing, which is
 *   the same rule that keeps significant_depth out of ring 0 in
 *   include\velocity9x\vbe_cache.h.
 *
 * Stage A - what ships today - stops there: the plan is computed, published to
 * the boot diagnostics, and NOT executed. Nothing writes an MTRR. The point is
 * to collect what the rules actually decide on every machine this project can
 * reach before any of them acts, because a rule that is wrong on a real BIOS
 * should be found in an INI file rather than in a corrupted desktop.
 *
 * Stage B, once that evidence exists, adds the write in the mini-VDD behind a
 * SYSTEM.INI kill switch. The reasons below are what its go/no-go will be.
 *
 * The numeric contract is shared with include\asm\V9XMAPI.INC, which is what
 * the two assembly users assemble from; scripts\check-tree.ps1 asserts the two
 * files agree.
 */
#ifndef VELOCITY9X_MTRR_H
#define VELOCITY9X_MTRR_H

#include "velocity9x/types.h"

/*
 * What the CPU admits to, established before any MSR is touched.
 *
 * The order matters and is a safety property, not a style: CPUID itself is an
 * invalid opcode on the 386 and early 486 this project still targets on the
 * VLB machine, RDMSR is one without MSR support, and the MTRR MSRs exist only
 * when the MTRR bit is set. Each flag is the licence to look for the next.
 */
#define V9X_MTRR_CPU_CPUID ((v9x_u16)0x0001u) /* EFLAGS.ID could be toggled */
#define V9X_MTRR_CPU_MSR   ((v9x_u16)0x0002u) /* CPUID.01h EDX[5], RDMSR/WRMSR */
#define V9X_MTRR_CPU_MTRR  ((v9x_u16)0x0004u) /* CPUID.01h EDX[12] */
#define V9X_MTRR_CPU_PGE   ((v9x_u16)0x0008u) /* CPUID.01h EDX[13], CR4.PGE */

/* IA32_MTRRCAP: VCNT in bits 7:0, WC availability in bit 10. */
#define V9X_MTRR_CAP_VCNT_MASK ((v9x_u32)0x000000FFul)
#define V9X_MTRR_CAP_WC        ((v9x_u32)0x00000400ul)

/* IA32_MTRR_DEF_TYPE: default memory type in bits 7:0, enable in bit 11. */
#define V9X_MTRR_DEF_TYPE_MASK ((v9x_u32)0x000000FFul)
#define V9X_MTRR_DEF_ENABLE    ((v9x_u32)0x00000800ul)

/* The memory types this code names. UC is the only default it will act under
 * and WC is the only type it would ever write. */
#define V9X_MTRR_TYPE_UC ((v9x_u32)0x00ul)
#define V9X_MTRR_TYPE_WC ((v9x_u32)0x01ul)

/* IA32_MTRR_PHYSMASKn bit 11 marks the pair valid. */
#define V9X_MTRR_PHYSMASK_VALID ((v9x_u32)0x00000800ul)

/*
 * How many variable-range pairs this code will read and reason about.
 *
 * Eight is what every x86 that implements MTRRs has exposed; VCNT is read
 * from the hardware and clamped to this, so a CPU reporting more is reasoned
 * about only as far as it is reported - and, because an unexaminable range is
 * treated as a reason to do nothing, that direction is safe.
 */
#define V9X_MTRR_RANGE_MAX ((v9x_u16)8u)

/*
 * Floor and granularity.
 *
 * No linear framebuffer this project has met or can meet sits below 16 MiB -
 * that space is RAM, the ROM area and the ISA hole - so a reported aperture
 * base below it is a BIOS answer that must not become a WC range over
 * somebody's RAM. A window smaller than 1 MiB is not worth the risk of the
 * write sequence that would install it.
 */
#define V9X_MTRR_APERTURE_FLOOR ((v9x_u32)0x01000000ul)
#define V9X_MTRR_MIN_WINDOW     ((v9x_u32)0x00100000ul)

/*
 * Why no range will be programmed, or V9X_MTRR_OK.
 *
 * Reported rather than reduced to a boolean for the same reason the VBE
 * collection reports counts and flags separately: "no write-combining" has a
 * dozen causes with completely different follow-ups, and a field report of a
 * slow desktop needs to name which one.
 */
#define V9X_MTRR_OK              ((v9x_u16)0u)
#define V9X_MTRR_NO_CPUID        ((v9x_u16)1u)  /* pre-CPUID CPU */
#define V9X_MTRR_NO_MSR          ((v9x_u16)2u)  /* no RDMSR/WRMSR */
#define V9X_MTRR_NO_MTRR         ((v9x_u16)3u)  /* CPU has no MTRRs */
#define V9X_MTRR_NO_WC           ((v9x_u16)4u)  /* MTRRCAP denies WC */
#define V9X_MTRR_NO_VCNT         ((v9x_u16)5u)  /* VCNT is zero */
#define V9X_MTRR_DISABLED        ((v9x_u16)6u)  /* DEF_TYPE.E clear */
#define V9X_MTRR_DEFAULT_NOT_UC  ((v9x_u16)7u)  /* default type is not UC */
#define V9X_MTRR_NO_APERTURE     ((v9x_u16)8u)  /* base or size unknown */
#define V9X_MTRR_APERTURE_LOW    ((v9x_u16)9u)  /* below the 16 MiB floor */
#define V9X_MTRR_WINDOW_SMALL    ((v9x_u16)10u) /* aligned window under 1 MiB */
#define V9X_MTRR_ALREADY_COVERED ((v9x_u16)11u) /* a valid range overlaps it */
#define V9X_MTRR_NO_SLOT         ((v9x_u16)12u) /* every pair is in use */

/*
 * The MSR facts, exactly as ring 0 read them.
 *
 * base[] and mask[] are the low halves of IA32_MTRR_PHYSBASEn/PHYSMASKn.
 *
 * high_bits carries one bit per range, set when that range's PHYSBASE had a
 * non-zero high dword - the range starts above 4 GiB and therefore cannot
 * overlap anything this driver maps. PHYSMASK's high dword is deliberately
 * not recorded: on any CPU with more than 32 physical address bits it is
 * routinely non-zero (it carries the upper address bits of the mask), so
 * treating it as remarkable would refuse on nearly every real machine. For a
 * range that begins below 4 GiB, the low half of the mask alone determines
 * the size, and a low half of zero means the range is at least 4 GiB wide.
 */
struct v9x_mtrr_state {
    v9x_u16 cpu_flags;
    v9x_u32 cap;
    v9x_u32 def_type;
    v9x_u16 range_count;
    v9x_u16 high_bits;
    v9x_u32 base[V9X_MTRR_RANGE_MAX];
    v9x_u32 mask[V9X_MTRR_RANGE_MAX];
};

/*
 * What would be written, when reason is V9X_MTRR_OK.
 *
 * size is always a power of two and base is always a multiple of it, because
 * that is what a variable-range MTRR can express; when the aperture is not
 * itself such a block the window is the largest one that fits inside it, and
 * the remainder simply stays uncached. Covering the remainder would mean
 * rounding up, and rounding up means putting WC on physical space that is not
 * the framebuffer - which is the one outcome this whole file exists to avoid.
 */
struct v9x_mtrr_plan {
    v9x_u16 reason;
    v9x_u16 slot;
    v9x_u32 base;
    v9x_u32 size;
};

/*
 * Decide whether one WC range may be installed over [aperture_base,
 * aperture_base + aperture_bytes), and where.
 *
 * plan is always filled in: on refusal its reason says why and its other
 * fields are zero. Returns non-zero when reason is V9X_MTRR_OK.
 *
 * The rule that makes this safe is the default type. When MTRRs are enabled
 * and the default is UC, every address no valid range covers is uncached -
 * so an aperture no range covers is both uncached (worth fixing) and not
 * described as RAM by the BIOS (safe to fix). Any other default type means
 * that reasoning does not hold, and the answer is to do nothing.
 */
v9x_u16 v9x_mtrr_plan_wc(const struct v9x_mtrr_state *state,
                         v9x_u32 aperture_base,
                         v9x_u32 aperture_bytes,
                         struct v9x_mtrr_plan *plan);

/* The largest power of two that both divides base and fits in bytes, or 0.
 * Exposed because it is the arithmetic most worth testing on its own. */
v9x_u32 v9x_mtrr_window(v9x_u32 base, v9x_u32 bytes);

/* The IA32_MTRR_PHYSMASKn value that expresses a size-byte range, valid bit
 * included. Zero when size is not a usable power of two. */
v9x_u32 v9x_mtrr_physmask(v9x_u32 size);

#endif /* VELOCITY9X_MTRR_H */
