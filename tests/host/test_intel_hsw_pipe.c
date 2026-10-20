#include <stdio.h>
#include <string.h>

#include "velocity9x/intel_hsw_pipe.h"
#include "velocity9x/intel_hsw_3d.h"
#include "velocity9x/intel_gma.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* The kernel area: V9X_INTEL_HSW_KERNEL_OFF from the state area's start. */
#define KERNELS_AT (V9X_INTEL_HSW_STATE_OFF + V9X_INTEL_HSW_KERNEL_OFF)
#define KERNELS_END \
    (KERNELS_AT + V9X_INTEL_HSW_KERNEL_SLOTS * V9X_INTEL_HSW_KERNEL_SLOT)
#define STATE_END (V9X_INTEL_HSW_STATE_OFF + V9X_INTEL_HSW_STATE_BYTES)

static int disjoint(v9x_u32 a, v9x_u32 a_bytes, v9x_u32 b, v9x_u32 b_bytes)
{
    return a + a_bytes <= b || b + b_bytes <= a;
}

/* Each slot's share of the reserve: inside its area, clear of the other
 * slot and of the kernels, and big enough for what goes in it. */
static void test_slot_layout(void)
{
    struct v9x_hsw_pipe_slot s[2];
    struct v9x_hsw_pipe_slot probe;
    v9x_u32 index;
    /* Read through volatiles: a check of constants alone is folded, and
     * Open Watcom then warns that the failure branch is unreachable. */
    volatile v9x_u32 block = V9X_HSW_PIPE_STATE_BLOCK_BYTES;
    volatile v9x_u32 kernels_end = KERNELS_END;

    CHECK(block >= V9X_HSW_SHADE_STATE_BYTES);
    CHECK(kernels_end <= STATE_END);
    CHECK(v9x_intel_hsw_pipe_slot(0ul, &s[0]) == V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_pipe_slot(1ul, &s[1]) == V9X_STATUS_OK);
    CHECK(v9x_intel_hsw_pipe_slot(1ul, 0) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(s[0].index == 0ul && s[1].index == 1ul);

    /* Today's areas, the first half of each, are slot 0's: a synchronous
     * build that only ever used slot 0 would draw as before. */
    CHECK(s[0].state_off == V9X_INTEL_HSW_STATE_OFF);
    CHECK(s[0].batch_off == V9X_INTEL_HSW_BATCH_OFF);
    CHECK(s[0].vertex_off == V9X_INTEL_HSW_VERTEX_OFF);

    for (index = 0ul; index < V9X_HSW_PIPE_SLOTS; ++index) {
        /* The workload wants a page-aligned base and a whole-page size. */
        CHECK((s[index].state_off & 0xffful) == 0ul);
        CHECK((s[index].state_span & 0xffful) == 0ul);
        CHECK(s[index].state_off + s[index].state_span == STATE_END);
        /* The per-draw block holds E's layout and stops at the kernels. */
        CHECK(s[index].state_off + V9X_HSW_PIPE_STATE_BLOCK_BYTES <=
              KERNELS_AT);
        CHECK(s[index].state_off >= V9X_INTEL_HSW_STATE_OFF);
        /* The kernels are where they are today, seen from either base. */
        CHECK(s[index].state_off + s[index].kernel_base == KERNELS_AT);
        /* Batch and vertices inside today's areas. */
        CHECK(s[index].batch_bytes == V9X_HSW_PIPE_BATCH_BYTES);
        CHECK(s[index].batch_off >= V9X_INTEL_HSW_BATCH_OFF &&
              s[index].batch_off + s[index].batch_bytes <=
                  V9X_INTEL_HSW_BATCH_OFF + V9X_INTEL_HSW_BATCH_BYTES);
        CHECK(s[index].vertex_bytes == V9X_HSW_PIPE_VERTEX_BYTES);
        CHECK(s[index].vertex_off >= V9X_INTEL_HSW_VERTEX_OFF &&
              s[index].vertex_off + s[index].vertex_bytes <=
                  V9X_INTEL_HSW_VERTEX_OFF + V9X_INTEL_HSW_VERTEX_BYTES);
        CHECK(s[index].vertex_off + s[index].vertex_bytes <=
              V9X_INTEL_HSW_RESERVE_BYTES);
        /* Vertex fetch reads whole 32-byte vertices from a 4-byte base. */
        CHECK((s[index].vertex_off & 0x3ful) == 0ul);
        /* The plan's 298 TL triangles a slot, 238 with unit 1's pair. */
        CHECK(s[index].vertex_bytes / (3ul * V9X_HSW_TL_VERTEX_BYTES) ==
              298ul);
        CHECK(s[index].vertex_bytes / (3ul * V9X_HSW_TL2_VERTEX_BYTES) ==
              238ul);
    }
    CHECK(disjoint(s[0].state_off, V9X_HSW_PIPE_STATE_BLOCK_BYTES,
                   s[1].state_off, V9X_HSW_PIPE_STATE_BLOCK_BYTES));
    CHECK(disjoint(s[0].batch_off, s[0].batch_bytes,
                   s[1].batch_off, s[1].batch_bytes));
    CHECK(disjoint(s[0].vertex_off, s[0].vertex_bytes,
                   s[1].vertex_off, s[1].vertex_bytes));

    /* The low bit, through the wrap. */
    CHECK(v9x_intel_hsw_pipe_slot(2ul, &probe) == V9X_STATUS_OK &&
          probe.index == 0ul);
    CHECK(v9x_intel_hsw_pipe_slot(0xfffffffful, &probe) == V9X_STATUS_OK &&
          probe.index == 1ul);
    CHECK(v9x_intel_hsw_pipe_slot(0xfffffffeul, &probe) == V9X_STATUS_OK &&
          probe.index == 0ul);
}

/* The biggest draw d3d_hsw.c builds - depth test, two texture units,
 * scissor, a full slot of vertices - fits a slot's batch area. */
static void test_largest_batch_fits(void)
{
    static v9x_u32 dw[V9X_HSW_PIPE_BATCH_BYTES / 4ul];
    struct v9x_hsw_pipe_slot slot;
    struct v9x_hsw_batch batch;
    struct v9x_hsw_draw d;
    v9x_u32 base;

    CHECK(v9x_intel_hsw_pipe_slot(1ul, &slot) == V9X_STATUS_OK);
    base = V9X_INTEL_HSW_RESERVE_OFFSET + slot.state_off;
    memset(&d, 0, sizeof(d));
    d.binding_table = V9X_HSW_SHADE_BINDING_TABLE(0);
    d.kernel = slot.kernel_base +
               (V9X_INTEL_HSW_KERNEL_SLOTS - 1ul) * V9X_INTEL_HSW_KERNEL_SLOT;
    d.vertices = V9X_INTEL_HSW_RESERVE_OFFSET + slot.vertex_off;
    d.width = 1024u;
    d.height = 768u;
    d.attributes = V9X_HSW_SHADE_ATTRIBUTES;
    d.blend = V9X_HSW_SHADE_BLEND_ALPHA_TEST;
    d.cc = V9X_HSW_SHADE_CC;
    d.sampler = V9X_HSW_SHADE_SAMPLER;
    d.depth_buffer = 0x004a4000ul;
    d.depth_test = V9X_HSW_SHADE_DS_ALWAYS;
    d.tl_vertices = 1u;
    d.vertex_count = 238ul * 3ul;
    d.depth_format = V9X_HSW_DEPTH_D16_UNORM;
    d.depth_pitch = 2048ul;
    d.scissor = 1u;
    d.scissor_x1 = 1023u;
    d.scissor_y1 = 767u;
    d.tl_uv1 = 1u;
    v9x_intel_hsw_batch_init(&batch, dw, V9X_HSW_PIPE_BATCH_BYTES / 4ul);
    CHECK(v9x_intel_hsw_draw_workload(&batch, base, slot.state_span, &d,
                                      V9X_INTEL_HSW_STATUS_OFFSET + 0x30ul,
                                      1ul) == V9X_STATUS_OK);
    CHECK(batch.overflow == 0u);
    /* Room to spare, not a near miss: under half the area. */
    CHECK(batch.used < V9X_HSW_PIPE_BATCH_BYTES / 8ul);
}

static void test_may_write(void)
{
    /* Nothing outstanding, or one: the next slot is free. */
    CHECK(v9x_intel_hsw_pipe_may_write(0ul, 0ul) == V9X_TRUE);
    CHECK(v9x_intel_hsw_pipe_may_write(1ul, 0ul) == V9X_TRUE);
    CHECK(v9x_intel_hsw_pipe_may_write(9ul, 8ul) == V9X_TRUE);
    /* Two outstanding: sequence 2 is still in the slot sequence 3 needs. */
    CHECK(v9x_intel_hsw_pipe_may_write(2ul, 0ul) == V9X_FALSE);
    CHECK(v9x_intel_hsw_pipe_may_write(5ul, 3ul) == V9X_FALSE);
    CHECK(v9x_intel_hsw_pipe_may_write(5ul, 4ul) == V9X_TRUE);
    /* Through the wrap. */
    CHECK(v9x_intel_hsw_pipe_may_write(0ul, 0xfffffffful) == V9X_TRUE);
    CHECK(v9x_intel_hsw_pipe_may_write(1ul, 0xfffffffful) == V9X_FALSE);
    CHECK(v9x_intel_hsw_pipe_may_write(0xfffffffful, 0xfffffffeul) ==
          V9X_TRUE);
    /* A done ahead of sent is a corrupted page, never a free slot. */
    CHECK(v9x_intel_hsw_pipe_may_write(3ul, 4ul) == V9X_FALSE);
    CHECK(v9x_intel_hsw_pipe_may_write(0ul, 1ul) == V9X_FALSE);

    CHECK(v9x_intel_hsw_pipe_idle(7ul, 7ul) == V9X_TRUE);
    CHECK(v9x_intel_hsw_pipe_idle(8ul, 7ul) == V9X_FALSE);
    CHECK(v9x_intel_hsw_pipe_idle(0ul, 0xfffffffful) == V9X_FALSE);
    CHECK(v9x_intel_hsw_pipe_idle(0ul, 0ul) == V9X_TRUE);
}

/*
 * Two processes submitting in turn, from just below the wrap: each submit
 * waits until may_write says yes (a GPU retiring one sequence a step), and
 * no slot is ever written while the sequence last submitted from it is
 * outstanding. The sequence is the shared one, so which process submits
 * does not matter, which is the point.
 */
static void test_two_processes_interleave(void)
{
    struct v9x_hsw_pipe_slot slot;
    v9x_u32 sent = 0xfffffff0ul;
    v9x_u32 done = 0xfffffff0ul;
    v9x_u32 last_in_slot[2];
    int used[2] = { 0, 0 };
    unsigned int step;
    unsigned int waits = 0u;

    last_in_slot[0] = 0ul;
    last_in_slot[1] = 0ul;
    for (step = 0u; step < 64u; ++step) {
        /* Process A on even steps, B on odd; B is slower, so the GPU
         * catches up a sequence before B's submit half the time. */
        if ((step & 3u) == 3u && done != sent) {
            ++done;
        }
        /* Bounded: the GPU here retires at most what was sent, so a
         * may_write that never says yes is a failure, not a hang. */
        while (v9x_intel_hsw_pipe_may_write(sent, done) != V9X_TRUE &&
               done != sent) {
            ++done;
            ++waits;
        }
        CHECK(v9x_intel_hsw_pipe_may_write(sent, done) == V9X_TRUE);
        CHECK(v9x_intel_hsw_pipe_slot(sent + 1ul, &slot) == V9X_STATUS_OK);
        if (used[slot.index]) {
            /* Its previous user has been stored back. */
            CHECK((v9x_s32)(done - last_in_slot[slot.index]) >= 0);
        }
        used[slot.index] = 1;
        last_in_slot[slot.index] = sent + 1ul;
        ++sent;
        CHECK((v9x_u32)(sent - done) <= 2ul);
    }
    CHECK(waits != 0u);
    CHECK(used[0] && used[1]);
}

static void base_desc(struct v9x_hsw_ps_desc *desc)
{
    memset(desc, 0, sizeof(*desc));
    desc->stage0 = 2u;
    desc->stage1 = 0u;
    desc->specular = 1u;
}

static void test_kernel_signature(void)
{
    struct v9x_hsw_ps_desc a;
    struct v9x_hsw_ps_desc b;
    v9x_u32 sa[V9X_HSW_PIPE_SIG_DWORDS];
    v9x_u32 sb[V9X_HSW_PIPE_SIG_DWORDS];
    v9x_u32 zero[V9X_HSW_PIPE_SIG_DWORDS];
    volatile v9x_u32 table_start = V9X_HSW_PIPE_SIG_STATUS_DWORD;

    memset(zero, 0, sizeof(zero));
    base_desc(&a);
    base_desc(&b);
    v9x_intel_hsw_pipe_kernel_sig(&a, sa);
    v9x_intel_hsw_pipe_kernel_sig(&b, sb);
    CHECK(v9x_intel_hsw_pipe_kernel_sig_equal(sa, sb) == V9X_TRUE);
    /* The zeroed page a ring open leaves matches nothing. */
    CHECK((sa[0] & V9X_HSW_PIPE_SIG_VALID) != 0ul);
    CHECK(v9x_intel_hsw_pipe_kernel_sig_equal(sa, zero) == V9X_FALSE);
    memset(&a, 0, sizeof(a));
    v9x_intel_hsw_pipe_kernel_sig(&a, sa);
    CHECK(v9x_intel_hsw_pipe_kernel_sig_equal(sa, zero) == V9X_FALSE);

    /* Without fog, a stale fog colour is not a difference: the builder
     * does not read it. */
    base_desc(&a);
    base_desc(&b);
    b.fog_rgb[1] = 0x3f000000ul;
    v9x_intel_hsw_pipe_kernel_sig(&a, sa);
    v9x_intel_hsw_pipe_kernel_sig(&b, sb);
    CHECK(v9x_intel_hsw_pipe_kernel_sig_equal(sa, sb) == V9X_TRUE);
    CHECK(sb[1] == 0ul && sb[2] == 0ul && sb[3] == 0ul);

    /* With fog it is: the case the slot index cannot see. */
    a.fog = 1u;
    b.fog = 1u;
    v9x_intel_hsw_pipe_kernel_sig(&a, sa);
    v9x_intel_hsw_pipe_kernel_sig(&b, sb);
    CHECK(v9x_intel_hsw_pipe_kernel_sig_equal(sa, sb) == V9X_FALSE);
    b.fog_rgb[1] = 0ul;
    v9x_intel_hsw_pipe_kernel_sig(&b, sb);
    CHECK(v9x_intel_hsw_pipe_kernel_sig_equal(sa, sb) == V9X_TRUE);

    /* Every other field. */
    base_desc(&b);
    b.stage0 = 3u;
    v9x_intel_hsw_pipe_kernel_sig(&b, sb);
    base_desc(&a);
    v9x_intel_hsw_pipe_kernel_sig(&a, sa);
    CHECK(v9x_intel_hsw_pipe_kernel_sig_equal(sa, sb) == V9X_FALSE);
    base_desc(&b);
    b.stage1 = 1u;
    v9x_intel_hsw_pipe_kernel_sig(&b, sb);
    CHECK(v9x_intel_hsw_pipe_kernel_sig_equal(sa, sb) == V9X_FALSE);
    base_desc(&b);
    b.specular = 0u;
    v9x_intel_hsw_pipe_kernel_sig(&b, sb);
    CHECK(v9x_intel_hsw_pipe_kernel_sig_equal(sa, sb) == V9X_FALSE);
    base_desc(&b);
    b.fog = 1u;
    v9x_intel_hsw_pipe_kernel_sig(&b, sb);
    CHECK(v9x_intel_hsw_pipe_kernel_sig_equal(sa, sb) == V9X_FALSE);

    /* The table fits the status page beside the sequences and the crumb
     * (volatile for the same reason as in test_slot_layout). */
    CHECK(table_start * 4ul > 0x38ul);
    CHECK((table_start + V9X_INTEL_HSW_KERNEL_SLOTS *
           V9X_HSW_PIPE_SIG_DWORDS) * 4ul <= 0x1000ul);
}

/*
 * The render ring with one six-dword submission outstanding, through Gen3's
 * planner (the plan's "ring space"): a wrap pads only what the engine has
 * passed, an exact fit ends at 0, and a ring the engine has not drained is
 * refused rather than overwritten.
 */
static void test_ring_with_one_outstanding(void)
{
    struct v9x_i9xx_ring_plan plan;

    /* 24 bytes outstanding ending at 4080; 16 left before the end. */
    CHECK(v9x_i9xx_ring_plan(4056ul, 4080ul, V9X_INTEL_HSW_RING_BYTES, 6ul,
                             &plan) == V9X_STATUS_OK);
    CHECK(plan.pad_dwords == 4ul && plan.command_tail == 0ul &&
          plan.next_tail == 24ul);

    /* Exactly 24 bytes left: no padding, the tail wraps to 0. */
    CHECK(v9x_i9xx_ring_plan(4048ul, 4072ul, V9X_INTEL_HSW_RING_BYTES, 6ul,
                             &plan) == V9X_STATUS_OK);
    CHECK(plan.pad_dwords == 0ul && plan.command_tail == 4072ul &&
          plan.next_tail == 0ul);

    /* The padding would reach the head the engine is still behind. */
    CHECK(v9x_i9xx_ring_plan(8ul, 4080ul, V9X_INTEL_HSW_RING_BYTES, 6ul,
                             &plan) == V9X_STATUS_INSUFFICIENT_MEMORY);
    /* An idle ring, as today. */
    CHECK(v9x_i9xx_ring_plan(96ul, 96ul, V9X_INTEL_HSW_RING_BYTES, 6ul,
                             &plan) == V9X_STATUS_OK);
    CHECK(plan.command_tail == 96ul && plan.next_tail == 120ul);
}

/*
 * Every pixel-shader description the builder accepts builds within a kernel
 * slot, and each has a slot of its own. 3DMark 2001 SE on the 4570 refused
 * 4,979 batches because specular with fog needs 112 dwords and a 512-byte
 * slot held 96 (2026-10-20); a refused batch draws nothing.
 */
static void test_every_kernel_fits(void)
{
    static v9x_u32 words[V9X_HSW_PIPE_KERNEL_DWORDS];
    static int taken[V9X_INTEL_HSW_KERNEL_SLOTS];
    struct v9x_hsw_ps_desc desc;
    unsigned int s0;
    unsigned int s1;
    unsigned int spec;
    unsigned int fog;
    v9x_u32 used;
    v9x_u32 slot;
    v9x_u32 index;

    for (index = 0ul; index < V9X_INTEL_HSW_KERNEL_SLOTS; ++index) {
        taken[index] = 0;
    }
    for (s0 = 0u; s0 <= V9X_HSW_PS_MODULATE_TEXALPHA; ++s0) {
        for (s1 = 0u; s1 <= V9X_HSW_PS_MODULATE_TEX1; ++s1) {
            for (spec = 0u; spec <= 1u; ++spec) {
                for (fog = 0u; fog <= 1u; ++fog) {
                    memset(&desc, 0, sizeof(desc));
                    desc.stage0 = (v9x_u8)s0;
                    desc.stage1 = (v9x_u8)s1;
                    desc.specular = (v9x_u8)spec;
                    desc.fog = (v9x_u8)fog;
                    desc.fog_rgb[0] = 0x3f000000ul;
                    used = 0ul;
                    CHECK(v9x_intel_hsw_ps_kernel_build(
                              words, V9X_HSW_PIPE_KERNEL_DWORDS, &desc,
                              &used) == V9X_STATUS_OK);
                    slot = v9x_intel_hsw_pipe_kernel_slot(&desc);
                    CHECK(slot < V9X_INTEL_HSW_KERNEL_SLOTS);
                    if (slot < V9X_INTEL_HSW_KERNEL_SLOTS) {
                        CHECK(taken[slot] == 0);
                        taken[slot] = 1;
                    }
                }
            }
        }
    }
}

unsigned int v9x_run_intel_hsw_pipe_tests(void)
{
    failures = 0u;
    test_slot_layout();
    test_largest_batch_fits();
    test_may_write();
    test_two_processes_interleave();
    test_kernel_signature();
    test_ring_with_one_outstanding();
    test_every_kernel_fits();
    return failures;
}
