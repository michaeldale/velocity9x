/*
 * 3dfx VSA-100 (Napalm): decodes of the memory, aperture and clock registers.
 *
 * Pure policy, no I/O. Callers read the registers and pass the raw values;
 * these functions interpret them and nothing else. Register meanings, with
 * Napalm spec citations: docs\specifications\vsa100-registers.md. The measured
 * vectors the host tests use are recorded in
 * docs\plans\3dfx-vsa100-family.md, "Measured on a sibling board".
 */
#ifndef VELOCITY9X_TDFX_VSA100_H
#define VELOCITY9X_TDFX_VSA100_H

#include "velocity9x/types.h"

/* PCI identity: every shipped VSA-100 board is 121A:0009. */
#define V9X_TDFX_VENDOR_ID        0x121au
#define V9X_TDFX_VSA100_DEVICE_ID 0x0009u

/* BAR0 register offsets, I/O-space block (Napalm init and video registers). */
#define V9X_TDFX_STATUS           0x000ul
#define V9X_TDFX_PCI_INIT0        0x004ul
#define V9X_TDFX_LFB_MEMORY_CONFIG 0x00cul
#define V9X_TDFX_MISC_INIT0       0x010ul
#define V9X_TDFX_MISC_INIT1       0x014ul
#define V9X_TDFX_DRAM_INIT0       0x018ul
#define V9X_TDFX_DRAM_INIT1       0x01cul
#define V9X_TDFX_AGP_INIT0        0x020ul
#define V9X_TDFX_VGA_INIT0        0x028ul
#define V9X_TDFX_VGA_INIT1        0x02cul
#define V9X_TDFX_STRAP_INFO       0x038ul
#define V9X_TDFX_PLL_CTRL0        0x040ul
#define V9X_TDFX_DAC_MODE         0x04cul
#define V9X_TDFX_VID_PROC_CFG     0x05cul
#define V9X_TDFX_VID_SCREEN_SIZE  0x098ul
#define V9X_TDFX_VID_DESKTOP_START 0x0e4ul
#define V9X_TDFX_VID_DESKTOP_STRIDE 0x0e8ul

/* status bit 9: the aggregate busy bit. The FIFO-free field's prose and bit
 * table disagree, so it is not used. */
#define V9X_TDFX_STATUS_BUSY      0x00000200ul

/*
 * lfbMemoryConfig is two registers behind a selector. A write with bit 30
 * set changes no data; it selects what a read returns: bit 29 clear for tile
 * control, set for tile compare. The spec's prose says bit 30 writes data;
 * its bit table, and the Mini-Me, say it does not.
 */
#define V9X_TDFX_LFB_SELECT_CONTROL 0x40000000ul
#define V9X_TDFX_LFB_SELECT_COMPARE 0x60000000ul

/* BAR1 is cut into 4 KiB pages for the linear/tiled boundary. */
#define V9X_TDFX_TILE_PAGE_BYTES  4096ul

/*
 * Bytes of memory the dramInit0 straps describe: bit 26 (two sets of parts
 * or one; counted here as 8 or 4 parts) times part density (bits 29:27,
 * 8 Mbit << n for n = 0..4) times 128 KiB per Mbit. 0 for the undefined
 * density codes 5-7. The databook gives the fields, not the part count;
 * the arithmetic is the one measured on two boards (Mini-Me 64 MiB, retail
 * Voodoo4 4500 32 MiB).
 */
v9x_u32 v9x_tdfx_vsa100_decode_memory(v9x_u32 dram_init0);

/*
 * The first BAR1 page (4 KiB units) served through the tiled view, from the
 * tile control and tile compare values the selector protocol reads. Compare
 * bit 15 set: compare bits 14:0. Otherwise control bits 12:0, with control
 * bits 24:23 as page bits 14:13.
 */
v9x_u32 v9x_tdfx_vsa100_tile_first_page(v9x_u32 tile_control,
                                         v9x_u32 tile_compare);

/*
 * Memory the CPU can reach through BAR1's linear view: the smaller of the
 * decoded size and the tile boundary. 0 when the straps do not decode.
 */
v9x_u32 v9x_tdfx_vsa100_usable_memory(v9x_u32 dram_init0,
                                       v9x_u32 tile_control,
                                       v9x_u32 tile_compare);

/*
 * pllCtrl0-2 output in kHz: 14318.18 x (N + 2) / ((M + 2) x 2^K), with N in
 * bits 15:8, M in bits 7:2, K in bits 1:0. 0 for M = 0, which the Mini-Me's
 * unused PLL0 holds and which has an attributed silicon exception.
 */
v9x_u32 v9x_tdfx_vsa100_pll_khz(v9x_u32 pll_ctrl);

#endif
