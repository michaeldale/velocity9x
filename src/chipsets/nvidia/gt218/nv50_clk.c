/*
 * The G96's core and shader reclock as an nv50_init.h op list: see
 * include\velocity9x\nv50_clk.h. Register fields, the script's steps and
 * their order are nouveau's (MIT): subdev\clk\nv50.c nv50_clk_read and
 * nv50_clk_calc, subdev\bus\hwsq.c's encoding, subdev\bus\g94.c
 * g94_bus_hwsq_exec, subdev\gpio\nv50.c's sense. The board facts (PLL
 * limits, perf level 3, the VID line) are this VBIOS's, 62.94.2C.00.91
 * (docs\decisions\2026-11-28-g96-clocks-measured.md).
 */
#include "velocity9x/nv50_clk.h"

#define V9X_NV50_CLK_CHIPSET_G96   0x96ul

/* PFIFO's block word: 1 stops it fetching while the clocks move. */
#define V9X_NV50_CLK_PFIFO_BLOCK   0x002504ul

/* g94_bus_hwsq_exec: the sequencer's program at 80000h, 1304h and 1318h
 * cleared, 1098h bit 3 off before the load and bits 4:3 on after, 130Ch
 * = 1 starts it, 1308h bit 8 is set while it runs. */
#define V9X_NV50_CLK_HWSQ_1304     0x001304ul
#define V9X_NV50_CLK_HWSQ_1318     0x001318ul
#define V9X_NV50_CLK_HWSQ_KICK     0x00130cul
#define V9X_NV50_CLK_HWSQ_CODE     0x080000ul
#define V9X_NV50_CLK_HWSQ_LOAD_OFF 0x00000008ul
#define V9X_NV50_CLK_HWSQ_RUN_ON   0x00000018ul
#define V9X_NV50_CLK_HWSQ_GO       0x00000001ul
#define V9X_NV50_CLK_HWSQ_BUSY     0x00000100ul

/* C040h: bits 1:0 core source, 5:4 shader source (3 = their PLL), bit 7
 * shader on hclk while unhooked, bit 20 the GT200 core bypass. Unhook
 * puts core on dom6 and shader on hclk; hook puts both back on their
 * PLLs (nv50_clk_calc, chipset 92h and later). */
#define V9X_NV50_CLK_UNHOOK_MASK   0x000000b3ul
#define V9X_NV50_CLK_UNHOOK        0x00000081ul
#define V9X_NV50_CLK_HOOK_MASK     0x00100033ul
#define V9X_NV50_CLK_HOOK          0x00000033ul

/* C050h (read_pll_src, 94h-98h): bits 12:11 NVPLL, 1:0 SPLL; 2 = the 100
 * MHz PCIe reference. */
#define V9X_NV50_CLK_REF_MASK      0x00001803ul
#define V9X_NV50_CLK_REF_HREF      0x00001002ul

/* PLL control: bit 31 enable, P in 18:16 and a copy in 21:19; nouveau
 * clears 31:30, 21:16 and 8 and sets the rest. Coefficient: N 15:8, M 7:0
 * (one stage: control bit 30 clear). */
#define V9X_NV50_CLK_PLL_ENABLE    0x80000000ul
#define V9X_NV50_CLK_PLL_CTRL_MASK 0xc03f0100ul
#define V9X_NV50_CLK_PLL_P_SHIFT   16u
#define V9X_NV50_CLK_PLL_P_COPY    19u
#define V9X_NV50_CLK_PLL_P_MASK    0x7ul
#define V9X_NV50_CLK_PLL_COEF_MASK 0x0000fffful
#define V9X_NV50_CLK_PLL_N_SHIFT   8u

/* The VID line: GPIO 5 (DCB function 04h), its sense bit 2 of the
 * line's nibble; sense 0 is VID 0, 1.00 V, level 3's voltage. */
#define V9X_NV50_CLK_VID_SHIFT     20u
#define V9X_NV50_CLK_VID_SENSE     0x4ul

/* hwsq opcodes (bus\hwsq.c): E2h/42h data whole or low half, E0h/40h
 * address whole or low half, 80h + flag + 20h (0) or 40h (1) a flag,
 * 5Fh flag value a wait, 7Fh the end and the fill; flag 10h is the FB
 * enable, waited on as flag 0. 8 us after the PFIFO block. */
#define V9X_HWSQ_OP_DATA           0xe2u
#define V9X_HWSQ_OP_DATA_LOW       0x42u
#define V9X_HWSQ_OP_ADDR           0xe0u
#define V9X_HWSQ_OP_ADDR_LOW       0x40u
#define V9X_HWSQ_OP_FLAG           0x80u
#define V9X_HWSQ_OP_FLAG_SET       0x20u
#define V9X_HWSQ_OP_WAIT           0x5fu
#define V9X_HWSQ_OP_END            0x7fu
#define V9X_HWSQ_FLAG_FB           0x10u
#define V9X_HWSQ_WAIT_FB           0x00u
#define V9X_HWSQ_FIFO_SETTLE_US    8ul
#define V9X_HWSQ_HIGH_HALF         0xffff0000ul

struct v9x_nv50_clk_pll {
    v9x_u32 n;
    v9x_u32 m;
    v9x_u32 p;
};

/* POST's and level 3's coefficients on the 100 MHz reference, inside the
 * VBIOS's limits (NVPLL VCO 500-1000 MHz, input 14-75; SPLL VCO
 * 1000-2000, input up to 100): core 100 x 16 / 2 >> 1 = 400 and 100 x 11
 * / 2 = 550; shader 100 x 16 >> 1 = 800 and 100 x 14 = 1400. */
static const struct v9x_nv50_clk_pll v9x_nv50_clk_core_post = { 16ul, 2ul, 1ul };
static const struct v9x_nv50_clk_pll v9x_nv50_clk_core_l3 = { 11ul, 2ul, 0ul };
static const struct v9x_nv50_clk_pll v9x_nv50_clk_shader_post = { 16ul, 1ul, 1ul };
static const struct v9x_nv50_clk_pll v9x_nv50_clk_shader_l3 = { 14ul, 1ul, 0ul };

/* The script under construction: bytes, and the last address and data
 * the sequencer holds (all ones before the first). */
struct v9x_hwsq_code {
    v9x_u8 bytes[V9X_NV50_CLK_SCRIPT_WORDS * 4ul];
    v9x_u32 size;
    v9x_u32 overflow;
    v9x_u32 addr;
    v9x_u32 data;
};

static void v9x_hwsq_byte(struct v9x_hwsq_code *code, v9x_u32 value)
{
    if (code->size >= sizeof(code->bytes)) {
        code->overflow = 1ul;
        return;
    }
    code->bytes[code->size] = (v9x_u8)value;
    ++code->size;
}

static void v9x_hwsq_dword(struct v9x_hwsq_code *code, v9x_u32 op,
                           v9x_u32 value)
{
    v9x_hwsq_byte(code, op);
    v9x_hwsq_byte(code, value);
    v9x_hwsq_byte(code, value >> 8);
    v9x_hwsq_byte(code, value >> 16);
    v9x_hwsq_byte(code, value >> 24);
}

static void v9x_hwsq_half(struct v9x_hwsq_code *code, v9x_u32 op,
                          v9x_u32 value)
{
    v9x_hwsq_byte(code, op);
    v9x_hwsq_byte(code, value);
    v9x_hwsq_byte(code, value >> 8);
}

/* nvkm_hwsq_wr32. */
static void v9x_hwsq_wr32(struct v9x_hwsq_code *code, v9x_u32 addr,
                          v9x_u32 data)
{
    if (data != code->data) {
        if ((data & V9X_HWSQ_HIGH_HALF) != (code->data & V9X_HWSQ_HIGH_HALF)) {
            v9x_hwsq_dword(code, V9X_HWSQ_OP_DATA, data);
        } else {
            v9x_hwsq_half(code, V9X_HWSQ_OP_DATA_LOW, data);
        }
    }
    if ((addr & V9X_HWSQ_HIGH_HALF) != (code->addr & V9X_HWSQ_HIGH_HALF)) {
        v9x_hwsq_dword(code, V9X_HWSQ_OP_ADDR, addr);
    } else {
        v9x_hwsq_half(code, V9X_HWSQ_OP_ADDR_LOW, addr);
    }
    code->addr = addr;
    code->data = data;
}

/* hwsq_mask: a write only when the value changes; *cache is the value the
 * register holds at that point in the script. */
static void v9x_hwsq_mask(struct v9x_hwsq_code *code, v9x_u32 addr,
                          v9x_u32 *cache, v9x_u32 mask, v9x_u32 data)
{
    v9x_u32 value;

    value = (*cache & ~mask) | data;
    if (value != *cache) {
        v9x_hwsq_wr32(code, addr, value);
        *cache = value;
    }
}

/* nvkm_hwsq_nsec, for a whole number of microseconds. */
static void v9x_hwsq_usec(struct v9x_hwsq_code *code, v9x_u32 usec)
{
    v9x_u32 shift = 0ul;

    while ((usec & ~3ul) != 0ul) {
        usec >>= 2;
        ++shift;
    }
    v9x_hwsq_byte(code, (shift << 2) | usec);
}

static v9x_u32 v9x_nv50_clk_pll_is(v9x_u32 ctrl, v9x_u32 coef,
                                   const struct v9x_nv50_clk_pll *pll)
{
    return (ctrl & V9X_NV50_CLK_PLL_ENABLE) != 0ul &&
           ((ctrl >> V9X_NV50_CLK_PLL_P_SHIFT) & V9X_NV50_CLK_PLL_P_MASK) ==
               pll->p &&
           (coef & V9X_NV50_CLK_PLL_COEF_MASK) ==
               ((pll->n << V9X_NV50_CLK_PLL_N_SHIFT) | pll->m);
}

/* nv50.c read_pll for a one-stage PLL: ref x N1 / M1, then the post-divider
 * P from the control word (nv50_clk_read). */
#define V9X_NV50_CLK_HREF_KHZ      100000ul
#define V9X_NV50_CLK_PLL_M_MASK    0xfful
#define V9X_NV50_CLK_PLL_N_MASK    0xfful

v9x_u32 v9x_nv50_clk_pll_khz(v9x_u32 ctrl, v9x_u32 coef)
{
    v9x_u32 n = (coef >> V9X_NV50_CLK_PLL_N_SHIFT) & V9X_NV50_CLK_PLL_N_MASK;
    v9x_u32 m = coef & V9X_NV50_CLK_PLL_M_MASK;
    v9x_u32 p = (ctrl >> V9X_NV50_CLK_PLL_P_SHIFT) & V9X_NV50_CLK_PLL_P_MASK;

    if ((ctrl & V9X_NV50_CLK_PLL_ENABLE) == 0ul || m == 0ul) {
        return 0ul;
    }
    return (V9X_NV50_CLK_HREF_KHZ * n / m) >> p;
}

v9x_u32 v9x_nv50_clk_at_level3(v9x_u32 nvpll_ctrl, v9x_u32 nvpll_coef,
                               v9x_u32 spll_ctrl, v9x_u32 spll_coef)
{
    return v9x_nv50_clk_pll_is(nvpll_ctrl, nvpll_coef,
                               &v9x_nv50_clk_core_l3) &&
           v9x_nv50_clk_pll_is(spll_ctrl, spll_coef,
                               &v9x_nv50_clk_shader_l3);
}

static v9x_u32 v9x_nv50_clk_pll_ctrl(const struct v9x_nv50_clk_pll *pll)
{
    return V9X_NV50_CLK_PLL_ENABLE | (pll->p << V9X_NV50_CLK_PLL_P_COPY) |
           (pll->p << V9X_NV50_CLK_PLL_P_SHIFT);
}

static v9x_u32 v9x_nv50_clk_pll_coef(const struct v9x_nv50_clk_pll *pll)
{
    return (pll->n << V9X_NV50_CLK_PLL_N_SHIFT) | pll->m;
}

/* nv50_clk_calc's core and shader part with the shader on SPLL, from the
 * registers' current values. */
static void v9x_nv50_clk_script(const struct v9x_nv50_clk_input *input,
                                struct v9x_hwsq_code *code)
{
    v9x_u32 source = input->source;
    v9x_u32 nvpll_ctrl = input->nvpll_ctrl;
    v9x_u32 nvpll_coef = input->nvpll_coef;
    v9x_u32 spll_ctrl = input->spll_ctrl;
    v9x_u32 spll_coef = input->spll_coef;
    v9x_u32 at;

    for (at = 0ul; at < sizeof(code->bytes); ++at) {
        code->bytes[at] = V9X_HWSQ_OP_END;
    }
    code->size = 0ul;
    code->overflow = 0ul;
    code->addr = 0xfffffffful;
    code->data = 0xfffffffful;

    v9x_hwsq_wr32(code, V9X_NV50_CLK_PFIFO_BLOCK, 1ul);
    v9x_hwsq_usec(code, V9X_HWSQ_FIFO_SETTLE_US);
    v9x_hwsq_byte(code, V9X_HWSQ_OP_FLAG + V9X_HWSQ_FLAG_FB +
                        V9X_HWSQ_OP_FLAG_SET);
    v9x_hwsq_byte(code, V9X_HWSQ_OP_WAIT);
    v9x_hwsq_byte(code, V9X_HWSQ_WAIT_FB);
    v9x_hwsq_byte(code, 1ul);

    v9x_hwsq_mask(code, V9X_NV50_CLK_SOURCE, &source,
                  V9X_NV50_CLK_UNHOOK_MASK, V9X_NV50_CLK_UNHOOK);
    v9x_hwsq_mask(code, V9X_NV50_CLK_NVPLL_CTRL, &nvpll_ctrl,
                  V9X_NV50_CLK_PLL_CTRL_MASK,
                  v9x_nv50_clk_pll_ctrl(&v9x_nv50_clk_core_l3));
    v9x_hwsq_mask(code, V9X_NV50_CLK_NVPLL_COEF, &nvpll_coef,
                  V9X_NV50_CLK_PLL_COEF_MASK,
                  v9x_nv50_clk_pll_coef(&v9x_nv50_clk_core_l3));
    v9x_hwsq_mask(code, V9X_NV50_CLK_SPLL_CTRL, &spll_ctrl,
                  V9X_NV50_CLK_PLL_CTRL_MASK,
                  v9x_nv50_clk_pll_ctrl(&v9x_nv50_clk_shader_l3));
    v9x_hwsq_mask(code, V9X_NV50_CLK_SPLL_COEF, &spll_coef,
                  V9X_NV50_CLK_PLL_COEF_MASK,
                  v9x_nv50_clk_pll_coef(&v9x_nv50_clk_shader_l3));
    v9x_hwsq_mask(code, V9X_NV50_CLK_SOURCE, &source,
                  V9X_NV50_CLK_HOOK_MASK, V9X_NV50_CLK_HOOK);

    v9x_hwsq_byte(code, V9X_HWSQ_OP_FLAG + V9X_HWSQ_FLAG_FB +
                        V9X_HWSQ_OP_FLAG_SET + V9X_HWSQ_OP_FLAG_SET);
    v9x_hwsq_byte(code, V9X_HWSQ_OP_WAIT);
    v9x_hwsq_byte(code, V9X_HWSQ_WAIT_FB);
    v9x_hwsq_byte(code, 0ul);
    v9x_hwsq_wr32(code, V9X_NV50_CLK_PFIFO_BLOCK, 0ul);
}

/* Whether this is the state measured, the one the script is built for. */
static v9x_u32 v9x_nv50_clk_from_post(const struct v9x_nv50_clk_input *input)
{
    return input->chipset == V9X_NV50_CLK_CHIPSET_G96 &&
           (input->ref_select & V9X_NV50_CLK_REF_MASK) ==
               V9X_NV50_CLK_REF_HREF &&
           (input->source & V9X_NV50_CLK_HOOK_MASK) == V9X_NV50_CLK_HOOK &&
           ((input->gpio_0_7 >> V9X_NV50_CLK_VID_SHIFT) &
            V9X_NV50_CLK_VID_SENSE) == 0ul &&
           v9x_nv50_clk_pll_is(input->nvpll_ctrl, input->nvpll_coef,
                               &v9x_nv50_clk_core_post) &&
           v9x_nv50_clk_pll_is(input->spll_ctrl, input->spll_coef,
                               &v9x_nv50_clk_shader_post);
}

static void v9x_nv50_clk_put(struct v9x_nv50_init_op *ops, v9x_u32 *count,
                             v9x_u32 kind, v9x_u32 reg, v9x_u32 value,
                             v9x_u32 mask)
{
    ops[*count].kind = kind;
    ops[*count].reg = reg;
    ops[*count].value = value;
    ops[*count].mask = mask;
    ++*count;
}

v9x_status v9x_nv50_clk_build(const struct v9x_nv50_clk_input *input,
                              struct v9x_nv50_init_op *ops, v9x_u32 max,
                              v9x_u32 *count)
{
    struct v9x_hwsq_code code;
    v9x_u32 hwsq_ctrl;
    v9x_u32 word;
    v9x_u32 at;

    if (input == 0 || ops == 0 || count == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *count = 0ul;
    if (!v9x_nv50_clk_from_post(input)) {
        return V9X_STATUS_OK;
    }
    if (max < V9X_NV50_CLK_LIST_OPS) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    v9x_nv50_clk_script(input, &code);
    /* nvkm_hwsq_fini: the words through the last byte and one more, so
     * the program always ends on an end opcode. */
    if (code.overflow != 0ul ||
        (code.size + 4ul) / 4ul != V9X_NV50_CLK_SCRIPT_WORDS) {
        return V9X_STATUS_INVALID_STATE;
    }

    hwsq_ctrl = input->hwsq_ctrl & ~V9X_NV50_CLK_HWSQ_LOAD_OFF;
    v9x_nv50_clk_put(ops, count, V9X_NV50_INIT_WRITE,
                     V9X_NV50_CLK_BUS_HWSQ_CTRL, hwsq_ctrl, 0xfffffffful);
    v9x_nv50_clk_put(ops, count, V9X_NV50_INIT_WRITE,
                     V9X_NV50_CLK_HWSQ_1304, 0ul, 0xfffffffful);
    v9x_nv50_clk_put(ops, count, V9X_NV50_INIT_WRITE,
                     V9X_NV50_CLK_HWSQ_1318, 0ul, 0xfffffffful);
    for (at = 0ul; at < V9X_NV50_CLK_SCRIPT_WORDS; ++at) {
        word = (v9x_u32)code.bytes[at * 4ul] |
               ((v9x_u32)code.bytes[at * 4ul + 1ul] << 8) |
               ((v9x_u32)code.bytes[at * 4ul + 2ul] << 16) |
               ((v9x_u32)code.bytes[at * 4ul + 3ul] << 24);
        v9x_nv50_clk_put(ops, count, V9X_NV50_INIT_WRITE,
                         V9X_NV50_CLK_HWSQ_CODE + at * 4ul, word,
                         0xfffffffful);
    }
    v9x_nv50_clk_put(ops, count, V9X_NV50_INIT_WRITE,
                     V9X_NV50_CLK_BUS_HWSQ_CTRL,
                     hwsq_ctrl | V9X_NV50_CLK_HWSQ_RUN_ON, 0xfffffffful);
    v9x_nv50_clk_put(ops, count, V9X_NV50_INIT_WRITE,
                     V9X_NV50_CLK_HWSQ_KICK, V9X_NV50_CLK_HWSQ_GO,
                     0xfffffffful);
    v9x_nv50_clk_put(ops, count, V9X_NV50_INIT_WAIT,
                     V9X_NV50_CLK_BUS_HWSQ_STATUS, 0ul,
                     V9X_NV50_CLK_HWSQ_BUSY);
    return V9X_STATUS_OK;
}
