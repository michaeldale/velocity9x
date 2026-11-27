#include <stdio.h>
#include <string.h>

#include "velocity9x/si_gpu_init.h"
#include "velocity9x/si_gfx_microcode.h"
#include "velocity9x/si_3d.h"
#include "velocity9x/si_clear_state.h"
#include "velocity9x/vi_shader.h"
#include "velocity9x/cik_shader.h"
#include "velocity9x/si_shader.h"

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

/* FNV-1a over every entry's offset, clear and set, low byte first. */
static v9x_u32 ops_hash(v9x_u32 count)
{
    v9x_u32 hash = 0x811c9dc5ul;
    v9x_u32 index;
    v9x_u32 word;
    v9x_u32 value;
    v9x_u32 byte;

    for (index = 0ul; index < count; ++index) {
        for (word = 0ul; word < 3ul; ++word) {
            value = word == 0ul ? ops[index].offset :
                    word == 1ul ? ops[index].clear : ops[index].set;
            for (byte = 0ul; byte < 4ul; ++byte) {
                hash ^= (value >> (byte * 8ul)) & 0xfful;
                hash *= 0x01000193ul;
            }
        }
    }
    return hash;
}

/* A full die of chip: nothing fused off, the given row. */
static void full_config(struct v9x_si_gpu_config *config, v9x_u32 chip,
                        v9x_u32 noofcols)
{
    memset(config, 0, sizeof(*config));
    config->chip = chip;
    config->mc_arb_ramcfg = noofcols << 6;
}

/* The entries for offset in order, up to max; returns how many. */
static v9x_u32 find_all(v9x_u32 count, v9x_u32 offset,
                        const struct v9x_vi_reg_seq **found, v9x_u32 max)
{
    v9x_u32 index;
    v9x_u32 n = 0ul;

    for (index = 0ul; index < count; ++index) {
        if (ops[index].offset == offset) {
            if (n < max) {
                found[n] = &ops[index];
            }
            ++n;
        }
    }
    return n;
}

/* The index of the first entry at offset whose set is value, from start;
 * count if none. */
static v9x_u32 find_write(v9x_u32 count, v9x_u32 start, v9x_u32 offset,
                          v9x_u32 value)
{
    v9x_u32 index;

    for (index = start; index < count; ++index) {
        if (ops[index].offset == offset && ops[index].set == value) {
            return index;
        }
    }
    return count;
}

/* Ids to chips; shapes; GRBM_GFX_INDEX's select. */
static void test_si_chips(void)
{
    v9x_u32 engines = 0ul;
    v9x_u32 arrays = 0ul;

    CHECK(v9x_si_chip_for_device(0x6610ul) == V9X_SI_CHIP_OLAND);
    CHECK(v9x_si_chip_for_device(0x6611ul) == V9X_SI_CHIP_OLAND);
    CHECK(v9x_si_chip_for_device(0x682cul) == V9X_SI_CHIP_VERDE);
    CHECK(v9x_si_chip_for_device(0x6808ul) == V9X_SI_CHIP_PITCAIRN);
    CHECK(v9x_si_chip_for_device(0x6780ul) == V9X_SI_CHIP_TAHITI);
    CHECK(v9x_si_chip_for_device(0x6798ul) == V9X_SI_CHIP_NONE);
    CHECK(v9x_si_chip_for_device(0ul) == V9X_SI_CHIP_NONE);

    CHECK(v9x_si_chip_shape(V9X_SI_CHIP_OLAND, &engines, &arrays) ==
          V9X_STATUS_OK && engines == 1ul && arrays == 1ul);
    CHECK(v9x_si_chip_shape(V9X_SI_CHIP_VERDE, &engines, &arrays) ==
          V9X_STATUS_OK && engines == 1ul && arrays == 2ul);
    CHECK(v9x_si_chip_shape(V9X_SI_CHIP_PITCAIRN, &engines, &arrays) ==
          V9X_STATUS_OK && engines == 2ul && arrays == 2ul);
    CHECK(v9x_si_chip_shape(V9X_SI_CHIP_TAHITI, &engines, &arrays) ==
          V9X_STATUS_OK && engines == 2ul && arrays == 2ul);
    CHECK(v9x_si_chip_shape(V9X_SI_CHIP_COUNT, &engines, &arrays) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_si_chip_shape(V9X_SI_CHIP_OLAND, 0, &arrays) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* INSTANCE_BROADCAST (30), SH_INDEX 15:8, SE_INDEX 23:16. */
    CHECK(v9x_si_grbm_select(0ul, 0ul) == 0x40000000ul);
    CHECK(v9x_si_grbm_select(0ul, 1ul) == 0x40000100ul);
    CHECK(v9x_si_grbm_select(1ul, 0ul) == 0x40010000ul);
    CHECK(v9x_si_grbm_select(1ul, 1ul) == 0x40010100ul);
}

/* The Oland's list through the general path, from its measured inputs, is
 * the 144 entries that ran on W9QI0DAD, entry for entry (FNV-1a of the
 * list built from the 2026-11-28 tree before the other chips joined). */
static void test_oland_unchanged(void)
{
    struct v9x_si_gpu_config config;
    v9x_u32 count;

    count = v9x_si_gpu_init_ops(ops, SI_OPS_MAX);
    CHECK(count == 144ul);
    CHECK(ops_hash(count) == 0x105ccc15ul);

    full_config(&config, V9X_SI_CHIP_OLAND, 2ul);
    config.mc_arb_ramcfg = 0x000012a1ul;
    config.cu_disable[0][0] = 0xffc00000ul;
    count = v9x_si_gpu_init_ops_for(&config, ops, SI_OPS_MAX);
    CHECK(count == 144ul);
    CHECK(ops_hash(count) == 0x105ccc15ul);

    CHECK(v9x_si_gpu_init_ops_for(0, ops, SI_OPS_MAX) == 0ul);
    config.chip = V9X_SI_CHIP_COUNT;
    CHECK(v9x_si_gpu_init_ops_for(&config, ops, SI_OPS_MAX) == 0ul);
}

/* Tahiti, the whole die (W9000's 32 CUs, eight RBs), a 4 KiB row. */
static void test_tahiti_full(void)
{
    struct v9x_si_gpu_config config;
    const struct v9x_vi_reg_seq *entry;
    const struct v9x_vi_reg_seq *found[8];
    v9x_u32 count;
    v9x_u32 at;
    v9x_u32 se;
    v9x_u32 sh;

    full_config(&config, V9X_SI_CHIP_TAHITI, 2ul);
    count = v9x_si_gpu_init_ops_for(&config, ops, SI_OPS_MAX);
    CHECK(count != 0ul && count <= V9X_VI_GPU_INIT_OPS_MAX);

    /* TAHITI_GB_ADDR_CONFIG_GOLDEN 12011003h: ROW_SIZE 2 for 4 KiB,
     * NUM_SHADER_ENGINES (13:12) 1 for two. The same to HDP and both DMA
     * engines. */
    entry = find_last(count, 0x098f8ul);
    CHECK(entry != 0 && entry->set == 0x22011003ul &&
          entry->clear == 0xfffffffful);
    entry = find_last(count, 0x02f48ul);
    CHECK(entry != 0 && entry->set == 0x22011003ul);
    entry = find_last(count, 0x0d8b8ul);
    CHECK(entry != 0 && entry->set == 0x22011003ul);

    /* Tile mode 0: depth micro tiling (2), 2D thin (4) at 5:2,
     * P8_32x32_8x16 (10) at 10:6, 64 B split, bank height 4 (2) at 17:16,
     * macro aspect 2 (1) at 19:18, 16 banks (3) at 21:20. */
    entry = find_last(count, 0x09910ul);
    CHECK(entry != 0 && entry->set == 0x00360292ul);
    /* Modes 4, 8 and 13, the ones Direct3D uses: 1D depth, linear
     * aligned, 1D thin, at Oland's indices. */
    entry = find_last(count, 0x09910ul + 4ul * 4ul);
    CHECK(entry != 0 && entry->set == (2ul | (2ul << 2) | (10ul << 6)));
    entry = find_last(count, 0x09910ul + 8ul * 4ul);
    CHECK(entry != 0 && entry->set == (1ul << 2));
    entry = find_last(count, 0x09910ul + 13ul * 4ul);
    CHECK(entry != 0 && ((entry->set >> 2) & 0xful) == 2ul);
    /* Mode 3 takes the row: 4 KiB is 6 at 13:11. */
    entry = find_last(count, 0x09910ul + 3ul * 4ul);
    CHECK(entry != 0 && ((entry->set >> 11) & 7ul) == 6ul);

    /* setup_rb: all eight RBs on, so one broadcast write of
     * gfx_v6_0_raster_config's TAHITI value, after the golden 2A00126Ah. */
    entry = find_last(count, 0x28350ul);
    CHECK(entry != 0 && entry->set == 0x2a001260ul &&
          entry->clear == 0xfffffffful);
    CHECK(find_all(count, 0x28350ul, found, 8ul) == 2ul);

    /* setup_spi: each of the four arrays selected in turn, CU 0's bit
     * cleared in each, then broadcast. */
    at = find_write(count, 0ul, 0x28350ul, 0x2a001260ul);
    for (se = 0ul; se < 2ul; ++se) {
        for (sh = 0ul; sh < 2ul; ++sh) {
            at = find_write(count, at, 0x0802cul,
                            v9x_si_grbm_select(se, sh));
            CHECK(at + 1ul < count);
            if (at + 1ul < count) {
                CHECK(ops[at + 1ul].offset == 0x090e8ul &&
                      ops[at + 1ul].clear == 1ul && ops[at + 1ul].set == 0ul);
            }
        }
    }
    CHECK(find_all(count, 0x090e8ul, found, 8ul) == 4ul);
    entry = find_last(count, 0x0802cul);
    CHECK(entry != 0 && entry->set == 0xe0000000ul);

    /* Its own scan converter FIFO: backend 100h. */
    entry = find_last(count, 0x08bccul);
    CHECK(entry != 0 && entry->set ==
          (0x20ul | (0x100ul << 6) | (0x30ul << 15) | (0x130ul << 23)));
    /* No TCC off: setup_tcc writes nothing; the golden steer stands. */
    CHECK(find_all(count, 0x0ac14ul, found, 8ul) == 1ul);
    entry = find_last(count, 0x0ac0cul);
    CHECK(entry != 0 && entry->set == 0xa9210876ul);

    /* Pitcairn: Tahiti's address config and tile table, its own golden
     * steer and the same two-engine raster. */
    full_config(&config, V9X_SI_CHIP_PITCAIRN, 2ul);
    count = v9x_si_gpu_init_ops_for(&config, ops, SI_OPS_MAX);
    CHECK(count != 0ul && count <= V9X_VI_GPU_INIT_OPS_MAX);
    entry = find_last(count, 0x098f8ul);
    CHECK(entry != 0 && entry->set == 0x22011003ul);
    entry = find_last(count, 0x09910ul);
    CHECK(entry != 0 && entry->set == 0x00360292ul);
    entry = find_last(count, 0x28350ul);
    CHECK(entry != 0 && entry->set == 0x2a001260ul);
    entry = find_last(count, 0x0ac0cul);
    CHECK(entry != 0 && entry->set == 0x32761054ul);
    CHECK(find_all(count, 0x090e8ul, found, 8ul) == 4ul);
}

/* Tahiti with SE 1's second array's two RBs off: six of eight, so
 * write_harvested_raster_configs writes each engine its own. SE 0 keeps
 * the whole map; SE 1's packer 1 has no RB, so PKR_MAP goes to 0 (9:8)
 * and RB_MAP_PKR1 to 3 (3:2): 2A00106Ch. */
static void test_tahiti_harvested_rbs(void)
{
    struct v9x_si_gpu_config config;
    const struct v9x_vi_reg_seq *found[8];
    v9x_u32 count;
    v9x_u32 at;

    full_config(&config, V9X_SI_CHIP_TAHITI, 2ul);
    config.rb_disable[1][1] = 0x00030000ul;
    count = v9x_si_gpu_init_ops_for(&config, ops, SI_OPS_MAX);
    CHECK(count != 0ul);
    CHECK(find_all(count, 0x28350ul, found, 8ul) == 3ul);
    at = find_write(count, 0ul, 0x0802cul, 0x60000000ul);
    CHECK(at + 1ul < count && ops[at + 1ul].offset == 0x28350ul &&
          ops[at + 1ul].set == 0x2a001260ul &&
          ops[at + 1ul].clear == 0xfffffffful);
    at = find_write(count, at, 0x0802cul, 0x60010000ul);
    CHECK(at + 2ul < count && ops[at + 1ul].offset == 0x28350ul &&
          ops[at + 1ul].set == 0x2a00106cul);
    CHECK(at + 2ul < count && ops[at + 2ul].offset == 0x0802cul &&
          ops[at + 2ul].set == 0xe0000000ul);

    /* All of SE 1 off: SE_MAP to 0 in both engines' values (25:24). */
    full_config(&config, V9X_SI_CHIP_TAHITI, 2ul);
    config.rb_disable[1][0] = 0x00030000ul;
    config.rb_disable[1][1] = 0x00030000ul;
    count = v9x_si_gpu_init_ops_for(&config, ops, SI_OPS_MAX);
    at = find_write(count, 0ul, 0x0802cul, 0x60000000ul);
    CHECK(at + 1ul < count && ops[at + 1ul].set == 0x28001260ul);

    /* No RB readable at all: Linux writes the plain value, broadcast. */
    full_config(&config, V9X_SI_CHIP_TAHITI, 2ul);
    config.rb_disable[0][0] = 0x00ff0000ul;
    config.rb_disable[0][1] = 0x00ff0000ul;
    config.rb_disable[1][0] = 0x00ff0000ul;
    config.rb_disable[1][1] = 0x00ff0000ul;
    count = v9x_si_gpu_init_ops_for(&config, ops, SI_OPS_MAX);
    CHECK(find_all(count, 0x28350ul, found, 8ul) == 2ul);
    CHECK(found[1]->set == 0x2a001260ul);
}

/* Cape Verde, one engine of two arrays and four RBs; a 2 KiB row; the W4100
 * is a harvested die (eight of ten CUs), so setup_spi's choice of CU is
 * exercised as Linux makes it, its shift-by-k walk included. */
static void test_verde(void)
{
    struct v9x_si_gpu_config config;
    const struct v9x_vi_reg_seq *entry;
    const struct v9x_vi_reg_seq *found[8];
    v9x_u32 count;
    v9x_u32 at;

    full_config(&config, V9X_SI_CHIP_VERDE, 1ul);
    /* SH 0: CU 0 off, so CU 1's bit. SH 1: CUs 0 and 1 off; Linux's mask
     * goes 1, 2, 8, so CU 3's bit, not CU 2's. Bits past five CUs are
     * Linux's mask's to drop. */
    config.cu_disable[0][0] = 0xffe10000ul;
    config.cu_disable[0][1] = 0x00030000ul;
    count = v9x_si_gpu_init_ops_for(&config, ops, SI_OPS_MAX);
    CHECK(count != 0ul && count <= V9X_VI_GPU_INIT_OPS_MAX);

    /* VERDE_GB_ADDR_CONFIG_GOLDEN 12010002h with ROW_SIZE 1, one engine. */
    entry = find_last(count, 0x098f8ul);
    CHECK(entry != 0 && entry->set == 0x12010002ul);
    /* Mode 3 and 17 take the 2 KiB split (5). */
    entry = find_last(count, 0x09910ul + 3ul * 4ul);
    CHECK(entry != 0 && ((entry->set >> 11) & 7ul) == 5ul);
    entry = find_last(count, 0x09910ul + 17ul * 4ul);
    CHECK(entry != 0 && ((entry->set >> 11) & 7ul) == 5ul);
    /* Mode 2, a fixed 256 B split, is not the row's. */
    entry = find_last(count, 0x09910ul + 2ul * 4ul);
    CHECK(entry != 0 && ((entry->set >> 11) & 7ul) == 2ul);
    /* Mode 8 is plain linear aligned on Cape Verde, as on Tahiti. */
    entry = find_last(count, 0x09910ul + 8ul * 4ul);
    CHECK(entry != 0 && entry->set == (1ul << 2));

    /* Four RBs on: VERDE's raster config, broadcast. */
    entry = find_last(count, 0x28350ul);
    CHECK(entry != 0 && entry->set == 0x00001240ul);
    CHECK(find_all(count, 0x28350ul, found, 8ul) == 2ul);

    at = find_write(count, 0ul, 0x0802cul, 0x40000000ul);
    CHECK(at + 1ul < count && ops[at + 1ul].offset == 0x090e8ul &&
          ops[at + 1ul].clear == 2ul);
    at = find_write(count, at, 0x0802cul, 0x40000100ul);
    CHECK(at + 1ul < count && ops[at + 1ul].offset == 0x090e8ul &&
          ops[at + 1ul].clear == 8ul);

    entry = find_last(count, 0x08bccul);
    CHECK(entry != 0 && ((entry->set >> 6) & 0x1fful) == 0x40ul);

    /* An array with no CU found by the walk: selected, nothing cleared. */
    full_config(&config, V9X_SI_CHIP_VERDE, 2ul);
    config.cu_disable[0][1] = 0x001f0000ul;
    count = v9x_si_gpu_init_ops_for(&config, ops, SI_OPS_MAX);
    CHECK(find_all(count, 0x090e8ul, found, 8ul) == 1ul);
    at = find_write(count, 0ul, 0x0802cul, 0x40000100ul);
    CHECK(at + 1ul < count && ops[at + 1ul].offset == 0x0802cul);
}

/* The DRAM row from MC_ARB_RAMCFG's NOOFCOLS (7:6): 4 * 2^(8 + n) bytes,
 * at most 4 KiB (gfx_v6_0_constants_init). */
static void test_row_size(void)
{
    struct v9x_si_gpu_config config;
    const struct v9x_vi_reg_seq *entry;
    v9x_u32 count;

    full_config(&config, V9X_SI_CHIP_TAHITI, 0ul);
    count = v9x_si_gpu_init_ops_for(&config, ops, SI_OPS_MAX);
    entry = find_last(count, 0x098f8ul);
    CHECK(entry != 0 && entry->set == 0x02011003ul);
    entry = find_last(count, 0x09910ul + 3ul * 4ul);
    CHECK(entry != 0 && ((entry->set >> 11) & 7ul) == 4ul);

    full_config(&config, V9X_SI_CHIP_TAHITI, 3ul);
    count = v9x_si_gpu_init_ops_for(&config, ops, SI_OPS_MAX);
    entry = find_last(count, 0x098f8ul);
    CHECK(entry != 0 && entry->set == 0x22011003ul);
    entry = find_last(count, 0x09910ul + 3ul * 4ul);
    CHECK(entry != 0 && ((entry->set >> 11) & 7ul) == 6ul);

    /* Only NOOFCOLS counts. */
    full_config(&config, V9X_SI_CHIP_VERDE, 2ul);
    config.mc_arb_ramcfg |= 0xffffff3ful;
    count = v9x_si_gpu_init_ops_for(&config, ops, SI_OPS_MAX);
    entry = find_last(count, 0x098f8ul);
    CHECK(entry != 0 && entry->set == 0x22010002ul);
}

/* setup_tcc on Tahiti with TCC 0 off: the golden steer A9210876h,
 * 543Bh less TCC 0, eleven banks. */
static void test_tahiti_tcc(void)
{
    struct v9x_si_gpu_config config;
    const struct v9x_vi_reg_seq *entry;
    v9x_u32 count;

    full_config(&config, V9X_SI_CHIP_TAHITI, 2ul);
    config.tcc_disable = 0x00010000ul;
    count = v9x_si_gpu_init_ops_for(&config, ops, SI_OPS_MAX);
    entry = find_last(count, 0x0ac14ul);
    CHECK(entry != 0 && entry->clear == 0x0000000ful && entry->set == 10ul);
    entry = find_last(count, 0x0ac10ul);
    CHECK(entry != 0 && entry->set == 0x00000543ul &&
          entry->clear == 0xfffffffful);
    entry = find_last(count, 0x0ac0cul);
    CHECK(entry != 0 && entry->set == 0xba921876ul);

    /* Bits past Tahiti's twelve TCCs are not TCCs. */
    full_config(&config, V9X_SI_CHIP_TAHITI, 2ul);
    config.tcc_disable = 0xf0000000ul;
    count = v9x_si_gpu_init_ops_for(&config, ops, SI_OPS_MAX);
    entry = find_last(count, 0x0ac0cul);
    CHECK(entry != 0 && entry->set == 0xa9210876ul);
}

/* Every chip's list stays inside the probe's windows and the HAL's
 * capacity, never resets, never touches the RLC's microcode port, and
 * writes none of the registers the HAL reads before building it: the
 * reads stand for Linux's, which come after the golden lists. Every
 * GRBM_GFX_INDEX value is broadcast or a select. */
static void test_all_chips_safe(void)
{
    struct v9x_si_gpu_config config;
    v9x_u32 chip;
    v9x_u32 count;
    v9x_u32 index;
    v9x_u32 value;

    for (chip = 0ul; chip < V9X_SI_CHIP_COUNT; ++chip) {
        full_config(&config, chip, 2ul);
        config.rb_disable[1][1] = 0x00010000ul;
        config.tcc_disable = 0x00020000ul;
        count = v9x_si_gpu_init_ops_for(&config, ops, SI_OPS_MAX);
        CHECK(count != 0ul && count <= V9X_VI_GPU_INIT_OPS_MAX);
        CHECK(v9x_si_gpu_init_ops_for(&config, ops, count - 1ul) == 0ul);
        count = v9x_si_gpu_init_ops_for(&config, ops, SI_OPS_MAX);
        for (index = 0ul; index < count; ++index) {
            CHECK((ops[index].offset & 3ul) == 0ul);
            CHECK(in_windows(ops[index].offset));
            CHECK(ops[index].offset != 0x08020ul);
            CHECK(ops[index].offset != 0x0c32cul &&
                  ops[index].offset != 0x0c330ul);
            CHECK(ops[index].offset != V9X_SI_MC_ARB_RAMCFG &&
                  ops[index].offset != V9X_SI_CC_RB_BACKEND_DISABLE &&
                  ops[index].offset != V9X_SI_GC_USER_RB_BACKEND_DISABLE &&
                  ops[index].offset != V9X_SI_CC_GC_SHADER_ARRAY_CONFIG &&
                  ops[index].offset != V9X_SI_GC_USER_SHADER_ARRAY_CONFIG &&
                  ops[index].offset != V9X_SI_CGTS_TCC_DISABLE &&
                  ops[index].offset != V9X_SI_CGTS_USER_TCC_DISABLE);
            if (ops[index].offset == V9X_SI_GRBM_GFX_INDEX) {
                value = ops[index].set;
                CHECK(ops[index].clear == 0xfffffffful);
                CHECK(value == 0xe0000000ul ||
                      (value & 0xff00fefful) == 0x40000000ul ||
                      (value & 0xff00fffful) == 0x60000000ul);
            }
        }
        /* It ends broadcast. */
        CHECK(find_last(count, V9X_SI_GRBM_GFX_INDEX) != 0 &&
              find_last(count, V9X_SI_GRBM_GFX_INDEX)->set == 0xe0000000ul);
    }
}

/* The microcode images that differ per chip are their own; those that do
 * not are the Oland's (si_gfx_microcode.h). */
static void test_si_microcode(void)
{
    CHECK(v9x_si_verde_rlc_microcode[0] != 0ul ||
          v9x_si_verde_rlc_microcode[1] != 0ul);
    CHECK(memcmp(v9x_si_rlc_microcode, v9x_si_verde_rlc_microcode,
                 sizeof(v9x_si_rlc_microcode)) != 0);
    CHECK(memcmp(v9x_si_rlc_microcode, v9x_si_pitcairn_rlc_microcode,
                 sizeof(v9x_si_rlc_microcode)) != 0);
    CHECK(memcmp(v9x_si_rlc_microcode, v9x_si_tahiti_rlc_microcode,
                 sizeof(v9x_si_rlc_microcode)) != 0);
    CHECK(memcmp(v9x_si_pitcairn_rlc_microcode, v9x_si_tahiti_rlc_microcode,
                 sizeof(v9x_si_rlc_microcode)) != 0);
    CHECK(memcmp(v9x_si_pfp_microcode, v9x_si_verde_pfp_microcode,
                 sizeof(v9x_si_pfp_microcode)) != 0);
    CHECK(memcmp(v9x_si_pfp_microcode, v9x_si_pitcairn_pfp_microcode,
                 sizeof(v9x_si_pfp_microcode)) != 0);
    CHECK(memcmp(v9x_si_me_microcode, v9x_si_verde_me_microcode,
                 sizeof(v9x_si_me_microcode)) != 0);
}

static v9x_u32 stream_dwords[2048];

/* gfx_v6_0_cp_gfx_start's second half: PREAMBLE begin, the seven runs,
 * PREAMBLE end, CLEAR_STATE, the VGT pair. */
static void test_start(void)
{
    struct v9x_vi_pm4 stream;
    v9x_u32 n;
    v9x_u32 total = 0ul;
    v9x_u32 run;

    for (run = 0ul; run < V9X_SI_CLEAR_STATE_RUNS; ++run) {
        total += v9x_si_clear_state_runs[run].count;
    }
    CHECK(total == V9X_SI_CLEAR_STATE_DWORDS);
    CHECK(v9x_si_clear_state_runs[0].first == 0xa000ul &&
          v9x_si_clear_state_runs[0].count == 212ul);
    CHECK(v9x_si_clear_state_runs[6].first == 0xa2a5ul &&
          v9x_si_clear_state_runs[6].count == 233ul);

    v9x_vi_pm4_init(&stream, stream_dwords, 2048ul);
    v9x_si_3d_start(&stream);
    n = stream.count;
    CHECK(stream.overflow == 0ul);
    CHECK(n == V9X_SI_START_DWORDS);
    CHECK(stream_dwords[0] == V9X_VI_PACKET3(V9X_VI_P3_PREAMBLE_CNTL, 0ul));
    CHECK(stream_dwords[1] == 0x20000000ul);
    CHECK(stream_dwords[2] == V9X_VI_PACKET3(V9X_VI_P3_SET_CONTEXT_REG, 212ul));
    CHECK(stream_dwords[3] == 0ul);
    CHECK(stream_dwords[n - 8ul] ==
          V9X_VI_PACKET3(V9X_VI_P3_PREAMBLE_CNTL, 0ul));
    CHECK(stream_dwords[n - 7ul] == 0x30000000ul);
    CHECK(stream_dwords[n - 6ul] == V9X_VI_PACKET3(V9X_VI_P3_CLEAR_STATE, 0ul));
    CHECK(stream_dwords[n - 3ul] == 0x316ul);
    CHECK(stream_dwords[n - 2ul] == 0x0000000eul &&
          stream_dwords[n - 1ul] == 0x00000010ul);
}

/* The rectangle in GFX6 mode: no SET_UCONFIG_REG anywhere, the primitive
 * type through SET_CONFIG_REG at 256h, each stage's SH run at PGM_LO, and
 * SI's fence event. */
static void test_gfx6_rect(void)
{
    struct v9x_vi_pm4 stream;
    struct v9x_vi_solid_rect rect;
    v9x_u32 at = 0ul;
    v9x_u32 n;
    int uconfig = 0;
    int config_prim = 0;
    int sh_ps = 0;
    int sh_vs = 0;
    int sh_rsrc3 = 0;
    int eop_ok = 0;

    memset(&rect, 0, sizeof(rect));
    rect.target_low = 0x0f809000ul;
    rect.target_high = 0xf4ul;
    rect.pitch_pixels = 64ul;
    rect.height = 16ul;
    rect.format = V9X_VI_CB_FORMAT_8_8_8_8;
    rect.tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
    rect.vs_low = 0x0f804000ul;
    rect.ps_low = 0x0f804100ul;
    rect.shader_high = 0xf4ul;
    rect.left = 8ul;
    rect.top = 4ul;
    rect.right = 40ul;
    rect.bottom = 12ul;
    rect.color[0] = 0x3f800000ul;
    rect.fence_low = 0x0f808100ul;
    rect.fence_high = 0xf4ul;
    rect.fence_value = 0x12345678ul;

    v9x_vi_pm4_init(&stream, stream_dwords, 2048ul);
    CHECK(stream.gfx6 == 0ul);
    stream.gfx6 = 1ul;
    CHECK(v9x_vi_3d_solid_rect(&stream, &rect) == V9X_STATUS_OK);
    CHECK(stream.overflow == 0ul);
    n = stream.count;
    while (at < n) {
        v9x_u32 header = stream_dwords[at];
        v9x_u32 op = (header >> 8) & 0xfful;

        if (op == V9X_VI_P3_SET_UCONFIG_REG) {
            uconfig = 1;
        }
        if (op == V9X_VI_P3_SET_CONFIG_REG &&
            stream_dwords[at + 1ul] == 0x256ul) {
            config_prim = 1;
        }
        if (op == V9X_VI_P3_SET_SH_REG) {
            if (stream_dwords[at + 1ul] == 0x08ul) {
                sh_ps = 1;
            }
            if (stream_dwords[at + 1ul] == 0x48ul) {
                sh_vs = 1;
            }
            if (stream_dwords[at + 1ul] == 0x07ul ||
                stream_dwords[at + 1ul] == 0x46ul) {
                sh_rsrc3 = 1;
            }
        }
        if (op == V9X_VI_P3_EVENT_WRITE_EOP &&
            stream_dwords[at + 1ul] == 0x00000514ul &&
            stream_dwords[at + 2ul] == 0x0f808100ul &&
            stream_dwords[at + 4ul] == 0x12345678ul) {
            eop_ok = 1;
        }
        at += ((header >> 16) & 0x3ffful) + 2ul;
    }
    CHECK(at == n);
    CHECK(!uconfig);
    CHECK(config_prim);
    CHECK(sh_ps && sh_vs && !sh_rsrc3);
    CHECK(eop_ok);
}

/* The solid shaders translate for GFX6; the TL vertex shader, which loads
 * through FLAT, is refused at its first FLAT instruction. */
static void test_gfx6_shaders(void)
{
    v9x_u32 words[128];
    v9x_u32 failed = 0xfffffffful;
    v9x_u32 count;

    count = v9x_vi_shader_solid_vs(words);
    CHECK(v9x_si_shader_from_vi(words, count, &failed) == V9X_STATUS_OK);
    count = v9x_vi_shader_solid_ps(words);
    CHECK(v9x_si_shader_from_vi(words, count, &failed) == V9X_STATUS_OK);
    count = v9x_vi_shader_tl_vs(words);
    CHECK(v9x_si_shader_from_vi(words, count, &failed) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(failed < count && (words[failed] & 0xfc000000ul) == 0xdc000000ul);
    /* The same program still translates for CI. */
    count = v9x_vi_shader_tl_vs(words);
    CHECK(v9x_cik_shader_from_vi(words, count, &failed) == V9X_STATUS_OK);
}

/* SI's TL VS: the shift, two MUBUF loads in place of the FLAT fetch, then
 * the translated tail. LLVM's SI assembler test reads
 * "buffer_load_dwordx4 v[1:4], v0, s[4:7], 0 offen" as E0381000h,
 * 80010100h; these differ only in VADDR, VDATA and SRSRC. */
static void test_gfx6_tl_vs(void)
{
    v9x_u32 words[V9X_SI_TL_VS_MAX];
    v9x_u32 vi[64];
    v9x_u32 count = 0ul;
    v9x_u32 vi_count;
    v9x_u32 index;
    int flat = 0;

    CHECK(v9x_si_shader_tl_vs(words, &count) == V9X_STATUS_OK);
    CHECK(count == V9X_SI_TL_VS_DWORDS);
    CHECK(words[1] == 0xe0381000ul && words[2] == 0x80000301ul);
    CHECK(words[3] == 0xe0381010ul && words[4] == 0x80000901ul);
    /* The shift is the GCN 3 program's first word translated: VOP2
     * v_lshlrev_b32 is 12h on VI, 1Ah on SI. */
    vi_count = v9x_vi_shader_tl_vs(vi);
    CHECK(vi_count == V9X_VI_TL_VS_DWORDS);
    CHECK((words[0] & 0x01fffffful) == (vi[0] & 0x01fffffful) &&
          ((words[0] >> 25) & 0x3ful) == 0x1aul);
    /* The tail is the GCN 3 program's from its s_waitcnt, same length. */
    CHECK(count - 5ul == vi_count - 10ul);
    CHECK(words[5] == vi[10]);
    for (index = 0ul; index < count; ++index) {
        if ((words[index] & 0xfc000000ul) == 0xdc000000ul) {
            flat = 1;
        }
    }
    CHECK(!flat);
    CHECK(v9x_si_shader_tl_vs(0, &count) == V9X_STATUS_INVALID_ARGUMENT);
}

/* The TL draw in GFX6 mode hands the VS a four-dword V#. */
static void test_gfx6_tl_draw(void)
{
    struct v9x_vi_pm4 stream;
    struct v9x_vi_tl_draw draw;
    v9x_u32 at = 0ul;
    int resource = 0;

    memset(&draw, 0, sizeof(draw));
    draw.target_low = 0x0f809000ul;
    draw.target_high = 0xf4ul;
    draw.pitch_pixels = 64ul;
    draw.height = 16ul;
    draw.format = V9X_VI_CB_FORMAT_8_8_8_8;
    draw.tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
    draw.vs_low = 0x0f804000ul;
    draw.ps_low = 0x0f804100ul;
    draw.shader_high = 0xf4ul;
    draw.vertex_low = 0x0f80b000ul;
    draw.vertex_high = 0xf4ul;
    draw.vertex_count = 6ul;
    draw.fence_low = 0x0f808100ul;
    draw.fence_high = 0xf4ul;
    draw.fence_value = 1ul;

    /* Depth on GFX6: DB_DEPTH_INFO (A00Fh) ADDR5_SWIZZLE_MASK alone, then
     * DB_Z_INFO Z16 with Oland's 1D depth tile index 4; a linear Z buffer
     * is refused. */
    draw.depth_low = 0x0f80d000ul;
    draw.depth_high = 0xf4ul;
    draw.depth_func = V9X_VI_ZFUNC_LESS;
    draw.depth_write = 1ul;
    draw.depth_linear = 1ul;
    v9x_vi_pm4_init(&stream, stream_dwords, 2048ul);
    stream.gfx6 = 1ul;
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_INVALID_ARGUMENT);
    draw.depth_linear = 0ul;
    v9x_vi_pm4_init(&stream, stream_dwords, 2048ul);
    stream.gfx6 = 1ul;
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_OK);
    {
        int depth_ok = 0;

        at = 0ul;
        while (at < stream.count) {
            v9x_u32 header = stream_dwords[at];

            if (((header >> 8) & 0xfful) == V9X_VI_P3_SET_CONTEXT_REG &&
                stream_dwords[at + 1ul] == 0x00ful) {
                depth_ok = stream_dwords[at + 2ul] == 1ul &&
                           stream_dwords[at + 3ul] == 0x00400001ul;
            }
            at += ((header >> 16) & 0x3ffful) + 2ul;
        }
        CHECK(depth_ok);
    }
    at = 0ul;
    while (at < stream.count) {
        v9x_u32 header = stream_dwords[at];

        if (((header >> 8) & 0xfful) == V9X_VI_P3_SET_SH_REG &&
            ((header >> 16) & 0x3ffful) == 4ul &&
            stream_dwords[at + 1ul] == 0x4cul) {
            resource = stream_dwords[at + 2ul] == 0x0f80b000ul &&
                       stream_dwords[at + 3ul] == 0xf4ul &&
                       stream_dwords[at + 4ul] == 6ul * 32ul &&
                       stream_dwords[at + 5ul] == 0x00027facul;
        }
        at += ((header >> 16) & 0x3ffful) + 2ul;
    }
    CHECK(resource);
}

/* The wide-vertex VS: seven IDXEN loads, then the GCN 3 tail rewritten.
 * IDXEN is MUBUF bit 13 beside OFFEN's 12 (BUFInstructions.td's
 * MUBUF_Real_gfx6_gfx7; the TL VS's "offen" load is E0381000h). */
static void test_gfx6_wide_vs(void)
{
    v9x_u32 words[64];
    v9x_u32 count = 0ul;
    v9x_u32 index;
    int flat = 0;
    int exports = 0;

    CHECK(v9x_si_shader_wide_vs(words, &count) == V9X_STATUS_OK);
    CHECK(count == V9X_SI_WIDE_VS_DWORDS);
    CHECK(words[0] == 0xe0382000ul && words[1] == 0x80000100ul);
    CHECK(words[2] == 0xe0382010ul && words[3] == 0x80000500ul);
    CHECK(words[12] == 0xe0382060ul && words[13] == 0x80001900ul);
    /* SI's s_waitcnt, then six parameter exports and POS0 with done, in
     * SI's EXP encoding (F8000000h). */
    CHECK(words[14] == v9x_vi_s_waitcnt(V9X_VI_WAITCNT_ALL));
    for (index = 15ul; index + 1ul < count; index += 2ul) {
        if ((words[index] & 0xfc000000ul) == 0xf8000000ul) {
            ++exports;
        }
    }
    CHECK(exports == 7);
    CHECK(((words[15] >> 4) & 0x3ful) == 32ul &&
          words[16] == 0x08070605ul);                 /* PARAM0, v5-v8 */
    CHECK(((words[27] >> 4) & 0x3ful) == 12ul &&
          (words[27] & 0x800ul) != 0ul &&
          words[28] == 0x04030201ul);                 /* POS0 done */
    CHECK(words[29] == V9X_VI_S_ENDPGM);
    for (index = 0ul; index < count; ++index) {
        if ((words[index] & 0xfc000000ul) == 0xdc000000ul) {
            flat = 1;
        }
    }
    CHECK(!flat);
    CHECK(v9x_si_shader_wide_vs(0, &count) == V9X_STATUS_INVALID_ARGUMENT);
}

/* The TL2 VS: the TL VS with a third load (s4-s7, IDXEN on v0) after its
 * two and parameter 3's export ahead of POS0's. */
static void test_gfx6_tl2_vs(void)
{
    v9x_u32 tl[V9X_SI_TL_VS_MAX];
    v9x_u32 words[V9X_SI_TL_VS_MAX];
    v9x_u32 tl_count = 0ul;
    v9x_u32 count = 0ul;
    v9x_u32 index;

    CHECK(v9x_si_shader_tl_vs(tl, &tl_count) == V9X_STATUS_OK);
    CHECK(v9x_si_shader_tl2_vs(words, &count) == V9X_STATUS_OK);
    CHECK(count == V9X_SI_TL2_VS_DWORDS && count <= 64ul);
    for (index = 0ul; index < 5ul; ++index) {
        CHECK(words[index] == tl[index]);
    }
    CHECK(words[5] == 0xe0382000ul && words[6] == 0x80011100ul);
    for (index = 5ul; index < tl_count - 3ul; ++index) {
        CHECK(words[index + 2ul] == tl[index]);
    }
    /* PARAM3 (target 35) of v17-v20, then POS0 done and s_endpgm. */
    CHECK((words[count - 5ul] & 0xfc000000ul) == 0xf8000000ul &&
          ((words[count - 5ul] >> 4) & 0x3ful) == 35ul &&
          words[count - 4ul] == 0x14131211ul);
    CHECK(words[count - 3ul] == tl[tl_count - 3ul] &&
          words[count - 1ul] == tl[tl_count - 1ul]);
}

/* A pixel shader 1.x draw on GFX6: the strided V#, six parameters, the
 * table as two PS user SGPRs, the PS's SGPRs in RSRC1, and COMP_SWAP. */
static void test_gfx6_wide_draw(void)
{
    static v9x_u32 stream_dwords[2048];
    struct v9x_vi_pm4 stream;
    struct v9x_vi_tl_draw draw;
    v9x_u32 at = 0ul;
    int resource = 0;
    int table = 0;
    int rsrc = 0;
    int swap = 0;
    int inputs = 0;

    memset(&draw, 0, sizeof(draw));
    draw.target_low = 0x0f80b000ul;
    draw.target_high = 0xf4ul;
    draw.pitch_pixels = 64ul;
    draw.height = 48ul;
    draw.format = V9X_VI_CB_FORMAT_8_8_8_8;
    draw.tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
    draw.vs_low = 0x0f804000ul;
    draw.ps_low = 0x0f804100ul;
    draw.shader_high = 0xf4ul;
    draw.vertex_low = 0x0f808400ul;
    draw.vertex_high = 0xf4ul;
    draw.vertex_count = 6ul;
    draw.fence_low = 0x0f808100ul;
    draw.fence_high = 0xf4ul;
    draw.fence_value = 1ul;
    draw.params = V9X_SI_WIDE_PARAMS;
    draw.vertex_stride = V9X_SI_WIDE_VERTEX_BYTES;
    draw.vs_vgprs = V9X_SI_WIDE_VS_VGPRS;
    draw.ps_vgprs = 72ul;
    draw.ps_sgprs = 96ul;
    draw.ps_table_low = 0x0f808200ul;
    draw.ps_table_high = 0xf4ul;
    draw.target_swap = V9X_VI_CB_SWAP_ALT;
    draw.ps_kill = 1ul;

    /* A strided V# is GFX6's, and a table cannot go with a texture. */
    v9x_vi_pm4_init(&stream, stream_dwords, 2048ul);
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_INVALID_ARGUMENT);
    draw.texture_low = 0x0f80e000ul;
    v9x_vi_pm4_init(&stream, stream_dwords, 2048ul);
    stream.gfx6 = 1ul;
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_INVALID_ARGUMENT);
    draw.texture_low = 0ul;
    v9x_vi_pm4_init(&stream, stream_dwords, 2048ul);
    stream.gfx6 = 1ul;
    CHECK(v9x_vi_3d_tl_draw(&stream, &draw) == V9X_STATUS_OK);

    while (at < stream.count) {
        v9x_u32 header = stream_dwords[at];
        v9x_u32 op = (header >> 8) & 0xfful;
        v9x_u32 n = (header >> 16) & 0x3ffful;
        v9x_u32 reg = stream_dwords[at + 1ul];

        if (op == V9X_VI_P3_SET_SH_REG && reg == 0x4cul && n == 4ul) {
            resource = stream_dwords[at + 2ul] == 0x0f808400ul &&
                       stream_dwords[at + 3ul] == (0xf4ul | (112ul << 16)) &&
                       stream_dwords[at + 4ul] == 6ul;
        }
        if (op == V9X_VI_P3_SET_SH_REG && reg == 0x0cul && n == 2ul) {
            table = stream_dwords[at + 2ul] == 0x0f808200ul &&
                    stream_dwords[at + 3ul] == 0xf4ul;
        }
        /* SPI_SHADER_PGM_LO_PS (2C08h): VGPRS 17 (72), SGPRS 12 (98
         * with VCC), FLOAT_MODE C0h; two user SGPRs. */
        if (op == V9X_VI_P3_SET_SH_REG && reg == 0x08ul && n == 4ul) {
            rsrc = stream_dwords[at + 4ul] == 0x000c0311ul &&
                   stream_dwords[at + 5ul] == 4ul;
        }
        /* CB_COLOR0_INFO: COLOR_8_8_8_8, SWAP_ALT at 12:11. */
        if (op == V9X_VI_P3_SET_CONTEXT_REG && reg == 0x31cul && n == 1ul &&
            stream_dwords[at + 2ul] == 0x00000828ul) {
            swap = 1;
        }
        /* SPI_PS_IN_CONTROL: six interpolants. */
        if (op == V9X_VI_P3_SET_CONTEXT_REG && reg == 0x1b6ul && n == 1ul) {
            inputs = stream_dwords[at + 2ul] == 6ul;
        }
        at += n + 2ul;
    }
    CHECK(resource);
    CHECK(table);
    CHECK(rsrc);
    CHECK(swap);
    CHECK(inputs);
}

unsigned int v9x_run_si_gpu_tests(void)
{
    failures = 0u;
    test_gpu_init();
    test_si_chips();
    test_oland_unchanged();
    test_tahiti_full();
    test_tahiti_harvested_rbs();
    test_verde();
    test_row_size();
    test_tahiti_tcc();
    test_all_chips_safe();
    test_si_microcode();
    test_start();
    test_gfx6_rect();
    test_gfx6_shaders();
    test_gfx6_tl_vs();
    test_gfx6_tl_draw();
    test_gfx6_wide_vs();
    test_gfx6_tl2_vs();
    test_gfx6_wide_draw();
    return failures;
}
