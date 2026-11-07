#include <stdio.h>

#include "velocity9x/cik_gpu_init.h"
#include "velocity9x/cik_clear_state.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define CIK_OPS_MAX 512u

static struct v9x_vi_reg_seq ops[CIK_OPS_MAX];

/* The last entry for offset, or 0. */
static const struct v9x_vi_reg_seq *find_last(v9x_u32 count, v9x_u32 offset)
{
    const struct v9x_vi_reg_seq *found = 0;
    v9x_u32 index;

    for (index = 0ul; index < count; ++index) {
        if (ops[index].offset == offset) {
            found = &ops[index];
        }
    }
    return found;
}

static void test_gpu_init(void)
{
    const struct v9x_vi_reg_seq *entry;
    v9x_u32 count;
    v9x_u32 index;

    count = v9x_cik_gpu_init_ops(ops, CIK_OPS_MAX);
    /* 91 golden entries, then constants_init: 3, 32 tile, 15 macrotile,
     * 3, 16 VMIDs of 5, 1, and 20 more; godavari's 260C0h entry is
     * left out (gen-mullins-gpu-init.ps1). */
    CHECK(count == 245ul);
    CHECK(v9x_cik_gpu_init_ops(ops, 10ul) == 0ul);
    CHECK(v9x_cik_gpu_init_ops(0, CIK_OPS_MAX) == 0ul);
    count = v9x_cik_gpu_init_ops(ops, CIK_OPS_MAX);

    /* cik.c: kalindi_mgcg_cgcg_init opens with 3108h, godavari's SDMA0
     * CHICKEN_BITS entry is the one the SDMA probe used. */
    CHECK(ops[0].offset == 0x3108ul * 4ul && ops[0].set == 0xfffffffcul);
    entry = find_last(count, 0xd014ul);
    CHECK(entry != 0 && entry->clear == 0x00010000ul &&
          entry->set == 0x00810001ul);

    /* GB_ADDR_CONFIG for a 1 KiB row, and HDP's copy. */
    entry = find_last(count, 0x098f8ul);
    CHECK(entry != 0 && entry->set == 0x02010001ul);
    entry = find_last(count, 0x02f48ul);
    CHECK(entry != 0 && entry->set == 0x02010001ul);

    /* The tile entries the 3D stream uses: 5 is 1D-tiled depth (array
     * mode 2, micro mode DEPTH), 8 linear-aligned (array mode 1). */
    entry = find_last(count, 0x09910ul + 5ul * 4ul);
    CHECK(entry != 0 && ((entry->set >> 2) & 0xful) == 2ul &&
          ((entry->set >> 22) & 0x7ul) == 2ul);
    entry = find_last(count, 0x09910ul + 8ul * 4ul);
    CHECK(entry != 0 && entry->set == 0x00000004ul);
    /* Macrotile 7 is not written. */
    CHECK(find_last(count, 0x09990ul + 7ul * 4ul) == 0);

    /* Every offset lies in the 256 KiB register BAR. */
    for (index = 0ul; index < count; ++index) {
        CHECK(ops[index].offset < 0x00040000ul && (ops[index].offset & 3ul) == 0ul);
    }

    /* The last SRBM_GFX_CNTL selects VMID 0 again. */
    entry = find_last(count, 0x00e44ul);
    CHECK(entry != 0 && entry->set == 0ul);
}

static void test_clear_state(void)
{
    v9x_u32 index;
    v9x_u32 total = 0ul;
    v9x_u32 at;

    for (index = 0ul; index < V9X_CIK_CLEAR_STATE_RUNS; ++index) {
        total += v9x_cik_clear_state_runs[index].count;
    }
    CHECK(total == V9X_CIK_CLEAR_STATE_DWORDS);
    CHECK(v9x_cik_clear_state_runs[0].first == 0xa000ul);
    CHECK(v9x_cik_clear_state_runs[6].first == 0xa2a5ul);

    /* VGT_VERTEX_REUSE_BLOCK_CNTL (A316h) and VGT_OUT_DEALLOC_CNTL: CI's
     * Eh and 10h, the values cp_gfx_start also writes, where VI has 1Eh
     * and 20h. A316h is 71h into the seventh run, after 652 values. */
    at = 652ul + (0xa316ul - 0xa2a5ul);
    CHECK(v9x_cik_clear_state_values[at] == 0x0000000eul);
    CHECK(v9x_cik_clear_state_values[at + 1ul] == 0x00000010ul);
}

unsigned int v9x_run_cik_gpu_tests(void)
{
    failures = 0u;
    test_gpu_init();
    test_clear_state();
    return failures;
}
