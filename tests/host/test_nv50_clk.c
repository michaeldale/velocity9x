#include <stdio.h>

#include "velocity9x/nv50_clk.h"

/*
 * The G96 reclock list (src\chipsets\nvidia\gt218\nv50_clk.c) against
 * what V9XN5MM /reclock ran on M-Z2T02D, boot 18: the registers it read
 * and the 19 script words it loaded, after which the card read back core
 * 550 and shader 1400 MHz (docs\decisions\2026-11-28-g96-clocks-
 * measured.md, which lists the run's V9XN5CK.INI words).
 */

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* V9XN5CK.INI [Hwsq] of the /reclock run, as the card ran it. */
static const v9x_u32 measured_script[V9X_NV50_CLK_SCRIPT_WORDS] = {
    0x000001e2ul, 0x2504e000ul, 0xb0060000ul, 0xe201005ful, 0x2e801b81ul,
    0xe2c04040ul, 0xa0000000ul, 0xe2402840ul, 0x00000b02ul, 0xe2402c40ul,
    0x80000000ul, 0xe2402040ul, 0x00000e01ul, 0xe2402440ul, 0x2e801bb3ul,
    0xd0c04040ul, 0xe200005ful, 0x00000000ul, 0x7f250440ul
};

static struct v9x_nv50_init_op ops[64];

/* The registers as boot 18 read them before the reclock; 1098h from
 * after it (21CA001Ch), with bits 4:3 as nouveau leaves them. */
static void post_state(struct v9x_nv50_clk_input *input)
{
    input->chipset = 0x96ul;
    input->hwsq_ctrl = 0x21ca001cul;
    input->source = 0x2e801b33ul;
    input->ref_select = 0x0000500eul;
    input->spll_ctrl = 0x80010000ul;
    input->spll_coef = 0x00001001ul;
    input->nvpll_ctrl = 0xa0010000ul;
    input->nvpll_coef = 0x00001002ul;
    input->gpio_0_7 = 0x74244440ul;
}

static void test_measured_script(void)
{
    struct v9x_nv50_clk_input input;
    v9x_u32 count = 0ul;
    v9x_u32 at;

    post_state(&input);
    CHECK(v9x_nv50_clk_build(&input, ops, 64ul, &count) == V9X_STATUS_OK);
    CHECK(count == V9X_NV50_CLK_LIST_OPS);
    if (count != V9X_NV50_CLK_LIST_OPS) {
        return;
    }
    /* g94_bus_hwsq_exec's order: 1098h bit 3 off, 1304h, 1318h, the
     * program, 1098h bits 4:3 on, 130Ch = 1, wait for 1308h bit 8. */
    CHECK(ops[0].kind == V9X_NV50_INIT_WRITE && ops[0].reg == 0x1098ul &&
          ops[0].value == 0x21ca0014ul);
    CHECK(ops[1].reg == 0x1304ul && ops[1].value == 0ul);
    CHECK(ops[2].reg == 0x1318ul && ops[2].value == 0ul);
    for (at = 0ul; at < V9X_NV50_CLK_SCRIPT_WORDS; ++at) {
        CHECK(ops[3ul + at].kind == V9X_NV50_INIT_WRITE);
        CHECK(ops[3ul + at].reg == 0x080000ul + at * 4ul);
        CHECK(ops[3ul + at].value == measured_script[at]);
    }
    CHECK(ops[22].reg == 0x1098ul && ops[22].value == 0x21ca001cul);
    CHECK(ops[23].reg == 0x130cul && ops[23].value == 1ul);
    CHECK(ops[24].kind == V9X_NV50_INIT_WAIT && ops[24].reg == 0x1308ul &&
          ops[24].mask == 0x100ul && ops[24].value == 0ul);
}

/* Any state but the measured one keeps its clocks. */
static void test_other_states_untouched(void)
{
    struct v9x_nv50_clk_input input;
    v9x_u32 count;

    post_state(&input);
    input.chipset = 0xa8ul;             /* the GT218 */
    count = 99ul;
    CHECK(v9x_nv50_clk_build(&input, ops, 64ul, &count) == V9X_STATUS_OK);
    CHECK(count == 0ul);

    post_state(&input);
    input.gpio_0_7 = 0x74644440ul;      /* VID 1: 0.95 V */
    count = 99ul;
    CHECK(v9x_nv50_clk_build(&input, ops, 64ul, &count) == V9X_STATUS_OK);
    CHECK(count == 0ul);

    post_state(&input);
    input.ref_select = 0x0000400eul;    /* NVPLL off the PCIe reference */
    count = 99ul;
    CHECK(v9x_nv50_clk_build(&input, ops, 64ul, &count) == V9X_STATUS_OK);
    CHECK(count == 0ul);

    post_state(&input);
    input.source = 0x2e801b23ul;        /* shader tied to the core */
    count = 99ul;
    CHECK(v9x_nv50_clk_build(&input, ops, 64ul, &count) == V9X_STATUS_OK);
    CHECK(count == 0ul);

    /* Already at level 3, as the card read back after the script. */
    post_state(&input);
    input.source = 0x2e801bb3ul;
    input.spll_ctrl = 0x80000000ul;
    input.spll_coef = 0x00000e01ul;
    input.nvpll_ctrl = 0xa0000000ul;
    input.nvpll_coef = 0x00000b02ul;
    count = 99ul;
    CHECK(v9x_nv50_clk_build(&input, ops, 64ul, &count) == V9X_STATUS_OK);
    CHECK(count == 0ul);
    CHECK(v9x_nv50_clk_at_level3(0xa0000000ul, 0x00000b02ul, 0x80000000ul,
                                 0x00000e01ul) != 0ul);
    CHECK(v9x_nv50_clk_at_level3(0xa0010000ul, 0x00001002ul, 0x80010000ul,
                                 0x00001001ul) == 0ul);
}

static void test_arguments(void)
{
    struct v9x_nv50_clk_input input;
    v9x_u32 count;

    post_state(&input);
    CHECK(v9x_nv50_clk_build(0, ops, 64ul, &count) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_nv50_clk_build(&input, ops, V9X_NV50_CLK_LIST_OPS - 1ul,
                             &count) == V9X_STATUS_INSUFFICIENT_MEMORY);
}

unsigned int v9x_run_nv50_clk_tests(void)
{
    failures = 0u;
    test_measured_script();
    test_other_states_untouched();
    test_arguments();
    return failures;
}
