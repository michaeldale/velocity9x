/*
 * Tests for the write-combining decision.
 *
 * The mini-VDD reads the MSRs and reports them; every rule about what those
 * numbers permit lives in src\common\mtrr.c and is decided here, against
 * states no machine in this project can produce as well as the three it can.
 * That split is the whole point: a wrong rule discovered on real hardware
 * shows up as memory corruption somewhere else entirely, so the rules are
 * held to a table instead.
 *
 * The fixtures named below are the measured machines. The netbook aperture is
 * from docs\issues\2026-08-27-netbook-gma950-findings.md (LFB 0xD0000000);
 * the RAM range shapes are the ordinary BIOS arrangement this policy is built
 * around - RAM described by write-back ranges, the PCI hole left at the
 * uncached default type.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/mtrr.h"

static unsigned int mtrr_failures = 0u;

#define MTCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++mtrr_failures; \
    } \
} while (0)

/* Memory types as a BIOS writes them into PHYSBASE bits 7:0. */
#define TYPE_WB ((v9x_u32)6ul)

/* A CPU that can do all of this, and a cap word offering eight pairs and WC. */
#define CPU_ALL ((v9x_u16)(V9X_MTRR_CPU_CPUID | V9X_MTRR_CPU_MSR | \
                           V9X_MTRR_CPU_MTRR | V9X_MTRR_CPU_PGE))
#define CAP_8_WC ((v9x_u32)(8ul | V9X_MTRR_CAP_WC))
#define DEF_UC_ON ((v9x_u32)(V9X_MTRR_TYPE_UC | V9X_MTRR_DEF_ENABLE))

/* The ordinary arrangement: 256 MiB of RAM as one write-back range, every
 * other pair unused, the PCI hole left at the uncached default. */
static void fixture_typical(struct v9x_mtrr_state *state)
{
    memset(state, 0, sizeof(*state));
    state->cpu_flags = CPU_ALL;
    state->cap = CAP_8_WC;
    state->def_type = DEF_UC_ON;
    state->range_count = V9X_MTRR_RANGE_MAX;
    state->base[0] = 0x00000000ul | TYPE_WB;
    state->mask[0] = 0xF0000000ul | V9X_MTRR_PHYSMASK_VALID;
}

static void test_window_arithmetic(void)
{
    /* An 8 MiB aperture on an 8 MiB-aligned base is expressible whole. */
    MTCHECK(v9x_mtrr_window(0xD0000000ul, 0x00800000ul) == 0x00800000ul);
    /* 12 MiB is not a power of two: the largest block that fits is 8. */
    MTCHECK(v9x_mtrr_window(0xD0000000ul, 0x00C00000ul) == 0x00800000ul);
    /* Alignment binds before size does. */
    MTCHECK(v9x_mtrr_window(0xD0100000ul, 0x00800000ul) == 0x00100000ul);
    /* A base aligned to less than a page yields nothing usable. */
    MTCHECK(v9x_mtrr_window(0xD0000800ul, 0x00800000ul) == 0x00000800ul);
    MTCHECK(v9x_mtrr_window(0ul, 0x00800000ul) == 0ul);
    MTCHECK(v9x_mtrr_window(0xD0000000ul, 0ul) == 0ul);
    /* The whole low half of the space, from zero-adjacent bases. */
    MTCHECK(v9x_mtrr_window(0x80000000ul, 0xFFFFFFFFul) == 0x80000000ul);

    /* The mask a size becomes, and the sizes that have none. */
    MTCHECK(v9x_mtrr_physmask(0x00800000ul) ==
            (0xFF800000ul | V9X_MTRR_PHYSMASK_VALID));
    MTCHECK(v9x_mtrr_physmask(0x00100000ul) ==
            (0xFFF00000ul | V9X_MTRR_PHYSMASK_VALID));
    MTCHECK(v9x_mtrr_physmask(0x00001000ul) ==
            (0xFFFFF000ul | V9X_MTRR_PHYSMASK_VALID));
    MTCHECK(v9x_mtrr_physmask(0x00000800ul) == 0ul); /* below a page */
    MTCHECK(v9x_mtrr_physmask(0x00300000ul) == 0ul); /* not a power of two */
    MTCHECK(v9x_mtrr_physmask(0ul) == 0ul);
}

static void test_plans_the_ordinary_machine(void)
{
    struct v9x_mtrr_state state;
    struct v9x_mtrr_plan plan;

    fixture_typical(&state);
    MTCHECK(v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_OK);
    MTCHECK(plan.base == 0xD0000000ul);
    MTCHECK(plan.size == 0x00800000ul);
    /* Pair 0 describes RAM, so the first free pair is 1. */
    MTCHECK(plan.slot == 1u);

    /* A 2 MiB Trio64 aperture: same decision, smaller window. */
    MTCHECK(v9x_mtrr_plan_wc(&state, 0xE8000000ul, 0x00200000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_OK);
    MTCHECK(plan.size == 0x00200000ul);

    /* An aperture whose size is not a power of two is covered in part, never
     * rounded up past its own end. */
    MTCHECK(v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00C00000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_OK);
    MTCHECK(plan.size == 0x00800000ul);
    MTCHECK(plan.base + plan.size <= 0xD0000000ul + 0x00C00000ul);
}

static void test_capability_ladder(void)
{
    struct v9x_mtrr_state state;
    struct v9x_mtrr_plan plan;

    /* Each rung refuses with its own reason, and each is the licence for the
     * next: a 486 never reaches CPUID, and nothing without the MTRR bit may
     * read MTRRCAP at all. */
    fixture_typical(&state);
    state.cpu_flags = 0u;
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_NO_CPUID);
    MTCHECK(plan.base == 0ul && plan.size == 0ul && plan.slot == 0u);

    state.cpu_flags = V9X_MTRR_CPU_CPUID;
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_NO_MSR);

    state.cpu_flags = (v9x_u16)(V9X_MTRR_CPU_CPUID | V9X_MTRR_CPU_MSR);
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_NO_MTRR);

    fixture_typical(&state);
    state.cap = 8ul; /* pairs, but the cap denies write-combining */
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_NO_WC);

    fixture_typical(&state);
    state.cap = V9X_MTRR_CAP_WC; /* WC, but no variable pairs at all */
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_NO_VCNT);

    MTCHECK(!v9x_mtrr_plan_wc(0, 0xD0000000ul, 0x00800000ul, &plan));
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00800000ul, 0));
}

static void test_default_type_is_the_safety_rule(void)
{
    struct v9x_mtrr_state state;
    struct v9x_mtrr_plan plan;

    /*
     * The reasoning this policy rests on: with MTRRs enabled and the default
     * type uncached, an address no valid range covers is both uncached (worth
     * improving) and not described as RAM (safe to improve). Neither half of
     * that holds otherwise, so neither case may proceed.
     */
    fixture_typical(&state);
    state.def_type = V9X_MTRR_TYPE_UC; /* enable bit clear */
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_DISABLED);

    fixture_typical(&state);
    state.def_type = TYPE_WB | V9X_MTRR_DEF_ENABLE;
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_DEFAULT_NOT_UC);
}

static void test_aperture_gates(void)
{
    struct v9x_mtrr_state state;
    struct v9x_mtrr_plan plan;

    fixture_typical(&state);
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_NO_APERTURE);
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_NO_APERTURE);

    /* Below 16 MiB is RAM, ROM and the ISA hole - never a linear framebuffer,
     * and the one place a bogus BIOS answer could put WC over real memory. */
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0x00100000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_APERTURE_LOW);
    MTCHECK(!v9x_mtrr_plan_wc(&state,
                              V9X_MTRR_APERTURE_FLOOR - 0x1000ul,
                              0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_APERTURE_LOW);

    /*
     * Exactly at the floor is admissible - but only reachable on a machine
     * whose RAM ends there, because on the ordinary fixture 16 MiB is inside
     * the write-back range and the overlap rule refuses first. Both halves are
     * worth asserting: the floor is a belt-and-braces gate sitting behind the
     * rule that actually does the work.
     */
    MTCHECK(!v9x_mtrr_plan_wc(&state, V9X_MTRR_APERTURE_FLOOR,
                              0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_ALREADY_COVERED);

    fixture_typical(&state);
    state.mask[0] = 0xFF000000ul | V9X_MTRR_PHYSMASK_VALID; /* 16 MiB of RAM */
    MTCHECK(v9x_mtrr_plan_wc(&state, V9X_MTRR_APERTURE_FLOOR,
                             0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_OK);
    MTCHECK(plan.base == V9X_MTRR_APERTURE_FLOOR);

    /* A window under 1 MiB is not worth the write sequence that installs it. */
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00080000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_WINDOW_SMALL);
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0xD0080000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_WINDOW_SMALL);
}

static void test_existing_ranges(void)
{
    struct v9x_mtrr_state state;
    struct v9x_mtrr_plan plan;

    /* A BIOS that already marked the aperture: leave its work alone. */
    fixture_typical(&state);
    state.base[1] = 0xD0000000ul | V9X_MTRR_TYPE_WC;
    state.mask[1] = 0xFF800000ul | V9X_MTRR_PHYSMASK_VALID;
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_ALREADY_COVERED);

    /* Partial overlap counts: this code never splits or resizes a range. */
    fixture_typical(&state);
    state.base[1] = 0xD0400000ul | TYPE_WB;
    state.mask[1] = 0xFFC00000ul | V9X_MTRR_PHYSMASK_VALID;
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_ALREADY_COVERED);

    /* Adjacent but not overlapping is fine: the RAM range ends where the
     * aperture begins. */
    fixture_typical(&state);
    state.base[1] = 0xCF800000ul | TYPE_WB;
    state.mask[1] = 0xFF800000ul | V9X_MTRR_PHYSMASK_VALID;
    MTCHECK(v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_OK);
    MTCHECK(plan.slot == 2u);

    /* An invalid pair carrying stale contents is free space, not a range. */
    fixture_typical(&state);
    state.base[1] = 0xD0000000ul | V9X_MTRR_TYPE_WC;
    state.mask[1] = 0xFF800000ul; /* valid bit clear */
    MTCHECK(v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_OK);
    MTCHECK(plan.slot == 1u);

    /* A range whose low mask bits are all zero spans at least 4 GiB. */
    fixture_typical(&state);
    state.mask[0] = V9X_MTRR_PHYSMASK_VALID;
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_ALREADY_COVERED);

    /* A range based above 4 GiB cannot reach anything this driver maps, so it
     * is skipped rather than treated as an obstacle - but its pair is in use
     * and must not be handed out as free. */
    fixture_typical(&state);
    state.base[1] = 0xD0000000ul | TYPE_WB; /* low half looks like a clash */
    state.mask[1] = 0xFF800000ul | V9X_MTRR_PHYSMASK_VALID;
    state.high_bits = (v9x_u16)0x0002u;     /* ...but it lives above 4 GiB */
    MTCHECK(v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_OK);
    MTCHECK(plan.slot == 2u);
}

static void test_slot_exhaustion(void)
{
    struct v9x_mtrr_state state;
    struct v9x_mtrr_plan plan;
    v9x_u16 index;

    /* Every pair in use, none of them over the aperture. */
    fixture_typical(&state);
    for (index = 0u; index < V9X_MTRR_RANGE_MAX; ++index) {
        state.base[index] = (v9x_u32)(0x01000000ul * (index + 1u)) | TYPE_WB;
        state.mask[index] = 0xFF000000ul | V9X_MTRR_PHYSMASK_VALID;
    }
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_NO_SLOT);

    /*
     * The cap claims more pairs than ring 0 actually read. Only what was read
     * may be reasoned about, and both of those are in use - so the answer is
     * to decline. Trusting the claim instead would index past the array.
     */
    fixture_typical(&state);
    state.range_count = 2u;
    state.base[0] = 0x00000000ul | TYPE_WB;
    state.mask[0] = 0xF0000000ul | V9X_MTRR_PHYSMASK_VALID;
    state.base[1] = 0x10000000ul | TYPE_WB;
    state.mask[1] = 0xF0000000ul | V9X_MTRR_PHYSMASK_VALID;
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_NO_SLOT);

    /* Nothing read at all is the same refusal as no pairs at all. */
    fixture_typical(&state);
    state.range_count = 0u;
    MTCHECK(!v9x_mtrr_plan_wc(&state, 0xD0000000ul, 0x00800000ul, &plan));
    MTCHECK(plan.reason == V9X_MTRR_NO_VCNT);
}

static v9x_u32 mtrr_prng_state = 0x2468ACE0ul;

static v9x_u32 mtrr_prng_next(void)
{
    mtrr_prng_state = mtrr_prng_state * 1664525ul + 1013904223ul;
    return mtrr_prng_state;
}

/*
 * The invariants that must hold for any answer this policy gives, over
 * arbitrary states. The one that matters is the last: an accepted plan never
 * touches a pair that is in use, and never covers an address some other valid
 * range already describes.
 */
static void test_plan_properties(void)
{
    struct v9x_mtrr_state state;
    struct v9x_mtrr_plan plan;
    v9x_u32 iteration;

    for (iteration = 0ul; iteration < 20000ul; ++iteration) {
        v9x_u32 aperture_base;
        v9x_u32 aperture_bytes;
        v9x_u16 index;
        v9x_u16 accepted;

        memset(&state, 0, sizeof(state));
        state.cpu_flags = (v9x_u16)(mtrr_prng_next() & 0x0Ful);
        state.cap = mtrr_prng_next() & 0x7FFul;
        state.def_type = mtrr_prng_next() & 0xFFFul;
        state.range_count = (v9x_u16)(mtrr_prng_next() % 10ul);
        for (index = 0u; index < V9X_MTRR_RANGE_MAX; ++index) {
            state.base[index] = mtrr_prng_next() & 0xFFFFF00Ful;
            state.mask[index] = mtrr_prng_next() & 0xFFFFF800ul;
        }
        state.high_bits = (v9x_u16)(mtrr_prng_next() & 0xFFul);
        aperture_base = mtrr_prng_next() & 0xFFFFF000ul;
        aperture_bytes = mtrr_prng_next() & 0x0FFFFFFFul;

        accepted = v9x_mtrr_plan_wc(&state, aperture_base, aperture_bytes,
                                    &plan);

        if (!accepted) {
            /* A refusal always names a reason and promises nothing. */
            MTCHECK(plan.reason != V9X_MTRR_OK);
            MTCHECK(plan.base == 0ul);
            MTCHECK(plan.size == 0ul);
            MTCHECK(plan.slot == 0u);
            continue;
        }

        MTCHECK(plan.reason == V9X_MTRR_OK);
        /* Expressible as a variable range: a power of two, at least a page,
         * on a base that is a multiple of it. */
        MTCHECK(plan.size >= V9X_MTRR_MIN_WINDOW);
        MTCHECK((plan.size & (plan.size - 1ul)) == 0ul);
        MTCHECK((plan.base % plan.size) == 0ul);
        MTCHECK(v9x_mtrr_physmask(plan.size) != 0ul);
        /* Inside the aperture it was asked about, never past its end. */
        MTCHECK(plan.base == aperture_base);
        MTCHECK(plan.size <= aperture_bytes);
        MTCHECK(plan.base >= V9X_MTRR_APERTURE_FLOOR);
        /* The chosen pair was free, and was one ring 0 actually read. */
        MTCHECK(plan.slot < V9X_MTRR_RANGE_MAX);
        MTCHECK(plan.slot < state.range_count);
        MTCHECK((state.mask[plan.slot] & V9X_MTRR_PHYSMASK_VALID) == 0ul);
        /* And no valid range below 4 GiB already describes the window. The
         * ends are saturated, not wrapped: a window or a range that runs to
         * the top of the address space still has to be compared as covering
         * everything up to it. */
        for (index = 0u; index < state.range_count &&
                         index < V9X_MTRR_RANGE_MAX; ++index) {
            v9x_u32 range_base;
            v9x_u32 range_mask;
            v9x_u32 range_end;
            v9x_u32 window_end;

            if ((state.mask[index] & V9X_MTRR_PHYSMASK_VALID) == 0ul) {
                continue;
            }
            if ((state.high_bits & (v9x_u16)(1u << index)) != 0u) {
                continue;
            }
            range_base = state.base[index] & 0xFFFFF000ul;
            range_mask = state.mask[index] & 0xFFFFF000ul;
            window_end = plan.base + plan.size;
            if (window_end < plan.base) {
                window_end = 0xFFFFFFFFul;
            }
            if (range_mask == 0ul) {
                range_end = 0xFFFFFFFFul;
            } else {
                range_end = range_base + ((v9x_u32)0ul - range_mask);
                if (range_end < range_base) {
                    range_end = 0xFFFFFFFFul;
                }
            }
            MTCHECK(range_base >= window_end || range_end <= plan.base);
        }
    }
}

unsigned int v9x_run_mtrr_tests(void)
{
    test_window_arithmetic();
    test_plans_the_ordinary_machine();
    test_capability_ladder();
    test_default_type_is_the_safety_rule();
    test_aperture_gates();
    test_existing_ranges();
    test_slot_exhaustion();
    test_plan_properties();
    return mtrr_failures;
}
