#include <stdio.h>

#include "velocity9x/si_gpu_init.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define SI_OPS_MAX 512u

static struct v9x_vi_reg_seq ops[SI_OPS_MAX];

/* SICP.VXD's SCInitWindows, byte offsets [start, limit): the probe refuses
 * a table with any entry outside them, so every entry must be inside. */
static const v9x_u32 si_windows[][2] = {
    { 0x02f48ul, 0x02f4cul }, { 0x08000ul, 0x08020ul },
    { 0x08024ul, 0x0c000ul }, { 0x0c300ul, 0x0c32cul },
    { 0x0c334ul, 0x0c500ul }, { 0x0d0b8ul, 0x0d0bcul },
    { 0x0d8b8ul, 0x0d8bcul }, { 0x28000ul, 0x29000ul }
};

static int in_windows(v9x_u32 offset)
{
    v9x_u32 index;

    for (index = 0ul; index < sizeof(si_windows) / sizeof(si_windows[0]);
         ++index) {
        if (offset >= si_windows[index][0] && offset < si_windows[index][1]) {
            return 1;
        }
    }
    return 0;
}

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

    count = v9x_si_gpu_init_ops(ops, SI_OPS_MAX);
    /* 16 + 6 + 60 golden, 5 address configs, 32 tile modes, 2 for
     * setup_rb, 3 for setup_spi, 12 constants, 8 CB perfcounter selects. */
    CHECK(count == 82ul + 5ul + 32ul + 2ul + 3ul + 12ul + 8ul);
    CHECK(v9x_si_gpu_init_ops(ops, 10ul) == 0ul);
    CHECK(v9x_si_gpu_init_ops(0, SI_OPS_MAX) == 0ul);
    count = v9x_si_gpu_init_ops(ops, SI_OPS_MAX);

    /* The golden list starts with CB_HW_CONTROL's read-modify-write. */
    CHECK(ops[0].offset == 0x09a10ul && ops[0].clear == 0x00010000ul &&
          ops[0].set == 0x00018208ul);
    /* golden_rlc writes 02010002h; constants_init's 4 KiB row wins. */
    entry = find_last(count, 0x098f8ul);
    CHECK(entry != 0 && entry->set == 0x22010002ul &&
          entry->clear == 0xfffffffful);
    entry = find_last(count, 0x02f48ul);
    CHECK(entry != 0 && entry->set == 0x22010002ul);
    entry = find_last(count, 0x0d0b8ul);
    CHECK(entry != 0 && entry->set == 0x22010002ul);
    /* Tile mode 0: depth micro tiling, 2D thin, P4_8x16, 64 B split, 16
     * banks, bank height 4, macro aspect 4 (gfx_v6_0.c's CHIP_OLAND). */
    entry = find_last(count, 0x09910ul);
    CHECK(entry != 0 && entry->set == 0x003a0112ul);
    CHECK((entry->set & 3ul) == 2ul && ((entry->set >> 2) & 0xful) == 4ul &&
          ((entry->set >> 6) & 0x1ful) == 4ul);
    /* Tile mode 5 takes the row's split: 4 KiB is 6 at 13:11. */
    entry = find_last(count, 0x09910ul + 5ul * 4ul);
    CHECK(entry != 0 && ((entry->set >> 11) & 7ul) == 6ul);
    /* Modes 26-31 are cleared, as Linux's memset leaves them. */
    entry = find_last(count, 0x09910ul + 31ul * 4ul);
    CHECK(entry != 0 && entry->set == 0ul);
    /* Raster config: golden's 82h, then setup_rb's RB_YSEL alone. */
    entry = find_last(count, 0x28350ul);
    CHECK(entry != 0 && entry->set == 0x00000080ul &&
          entry->clear == 0xfffffffful);
    /* setup_spi clears CU 0's bit and nothing else. */
    entry = find_last(count, 0x090e8ul);
    CHECK(entry != 0 && entry->clear == 1ul && entry->set == 0ul);
    /* GRBM_GFX_INDEX ends broadcast. */
    entry = find_last(count, 0x0802cul);
    CHECK(entry != 0 && entry->set == 0xe0000000ul);
    entry = find_last(count, 0x08bccul);
    CHECK(entry != 0 && entry->set ==
          (0x20ul | (0x40ul << 6) | (0x30ul << 15) | (0x130ul << 23)));

    for (index = 0ul; index < count; ++index) {
        CHECK((ops[index].offset & 3ul) == 0ul);
        CHECK(in_windows(ops[index].offset));
        /* Never a reset, never the RLC's microcode port. */
        CHECK(ops[index].offset != 0x08020ul);
        CHECK(ops[index].offset != 0x0c32cul && ops[index].offset != 0x0c330ul);
    }
}

unsigned int v9x_run_si_gpu_tests(void)
{
    failures = 0u;
    test_gpu_init();
    return failures;
}
