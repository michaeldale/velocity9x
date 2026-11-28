/*
 * The G96's (GeForce 9500 GT, chipset 96h) core and shader clocks raised
 * from where POST leaves them to the VBIOS's perf level 3, as a list of
 * nv50_init.h ops the HAL engine replays before its channel comes up.
 *
 * Measured on M-Z2T02D (docs\decisions\2026-11-28-g96-clocks-measured.md,
 * docs\plans\nvidia-g96-reclock.md): POST leaves core 400 and shader 800
 * MHz with the voltage already at level 3's 1.00 V; the VBIOS's level 3 is
 * 550 and 1400. The script is nouveau's nv50_clk_calc for those two
 * domains, the shader on SPLL, encoded by bus\hwsq.c's rules, run as
 * g94_bus_hwsq_exec runs it; V9XN5MM /reclock ran the same 19 words there
 * and read 550/1400 back.
 *
 * The list is built only from the exact state measured: chipset 96h, both
 * PLLs on the 100 MHz PCIe reference, core and shader on their PLLs at
 * POST's coefficients, and the VID line at VID 0. Any other card or state
 * gets an empty list and keeps its clocks. Pure arithmetic: nothing here
 * touches hardware.
 */
#ifndef VELOCITY9X_NV50_CLK_H
#define VELOCITY9X_NV50_CLK_H

#include "velocity9x/types.h"
#include "velocity9x/status.h"
#include "velocity9x/nv50_init.h"

/* The registers read for the input and written by the list. */
#define V9X_NV50_CLK_BUS_HWSQ_CTRL   0x001098ul
#define V9X_NV50_CLK_BUS_HWSQ_STATUS 0x001308ul
#define V9X_NV50_CLK_SOURCE          0x00c040ul
#define V9X_NV50_CLK_REF_SELECT      0x00c050ul
#define V9X_NV50_CLK_SPLL_CTRL       0x004020ul
#define V9X_NV50_CLK_SPLL_COEF       0x004024ul
#define V9X_NV50_CLK_NVPLL_CTRL      0x004028ul
#define V9X_NV50_CLK_NVPLL_COEF      0x00402cul
#define V9X_NV50_CLK_GPIO_0_7        0x00e104ul

/* The script, in words, and the list around it: three writes before the
 * script, two after, one wait. */
#define V9X_NV50_CLK_SCRIPT_WORDS    19ul
#define V9X_NV50_CLK_LIST_OPS        (V9X_NV50_CLK_SCRIPT_WORDS + 6ul)

struct v9x_nv50_clk_input {
    /* PMC_BOOT_0 bits 28:20. */
    v9x_u32 chipset;
    v9x_u32 hwsq_ctrl;      /* 1098h */
    v9x_u32 source;         /* C040h */
    v9x_u32 ref_select;     /* C050h */
    v9x_u32 spll_ctrl;      /* 4020h */
    v9x_u32 spll_coef;      /* 4024h */
    v9x_u32 nvpll_ctrl;     /* 4028h */
    v9x_u32 nvpll_coef;     /* 402Ch */
    v9x_u32 gpio_0_7;       /* E104h */
};

/*
 * Build the list into ops (room for max). *count is 0 when the clocks are
 * to stay as they are; otherwise the list ends with a wait on 1308h bit 8,
 * which the caller bounds. V9X_STATUS_INVALID_ARGUMENT for a null pointer,
 * V9X_STATUS_INSUFFICIENT_MEMORY when max is too small.
 */
v9x_status v9x_nv50_clk_build(const struct v9x_nv50_clk_input *input,
                              struct v9x_nv50_init_op *ops, v9x_u32 max,
                              v9x_u32 *count);

/* A one-stage PLL's output in kHz from its control and coefficient words,
 * on the 100 MHz PCIe reference (the only one the list is built for):
 * 100000 x N / M >> P; 0 when the PLL is off or M is 0. */
v9x_u32 v9x_nv50_clk_pll_khz(v9x_u32 ctrl, v9x_u32 coef);

/* Non-zero when the four PLL words decode to level 3's 550/1400. */
v9x_u32 v9x_nv50_clk_at_level3(v9x_u32 nvpll_ctrl, v9x_u32 nvpll_coef,
                               v9x_u32 spll_ctrl, v9x_u32 spll_coef);

#endif
