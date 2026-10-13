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

/* BAR1's size on every measured board (Voodoo4 4500 AGP and PCI: 128 MiB
 * Config Manager ranges, A8U4I5), so its base is 128 MiB aligned and an
 * address inside it, less the base, is its offset in video memory. */
#define V9X_TDFX_LFB_BAR_BYTES    0x08000000ul

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
 * The tile control value both measured BIOSes leave after their mode set:
 * begin page 1FFFh, so BAR1's linear view ends one page short of 32 MiB
 * (Voodoo4 4500 BIOS 1.15, boots 409-426; the Mini-Me, VelocityNT). Glide
 * moves the boundary down to its own buffers and does not move it back
 * (A8U4I5 boot 423: A7D3Ch after a clean session), and the VBE mode set does
 * not restore it, so the driver writes this back after every mode set.
 */
#define V9X_TDFX_LFB_BIOS_TILE_CONTROL 0x00003ffful

/*
 * The desktop's place in memory.
 *
 * Glide's command FIFO starts at 18000h and its buffers end at the fbRam
 * the driver reports through EXT_HWC; a CPU write into an enabled FIFO's
 * range is taken as a FIFO write. A desktop at offset 0 therefore sits in
 * Glide's FIFO, and GDI's drawing during a session jams the 3D engine
 * (docs\issues\2026-10-12-glide-cmdfifo-overlaps-the-desktop.md). 3dfx's
 * own driver puts the desktop at the top of memory; this driver splits the
 * linear memory in two: Glide below, the desktop and DirectDraw's heap
 * above.
 *
 * Returns the desktop's byte offset: half the linear memory, rounded down to
 * a MiB. 0 - the desktop stays at the bottom, and Glide gets everything as
 * before - for a card with less than V9X_TDFX_SPLIT_MIN_BYTES.
 */
#define V9X_TDFX_SPLIT_MIN_BYTES  0x00800000ul
v9x_u32 v9x_tdfx_vsa100_desktop_offset(v9x_u32 linear_bytes);

/*
 * The fbRam Glide is told: memory below the desktop less a guard of
 * V9X_TDFX_GLIDE_GUARD_BYTES. Glide moves the tile boundary to its own
 * buffers, which end at fbRam, and the CPU then reaches everything above
 * the boundary through the tiled view; a tiled access lands within one row
 * of tiles of its linear address (128 bytes x 32 lines x the tile count),
 * so the guard keeps a desktop write near its bottom from landing in
 * Glide's last buffer. With no split, all of the linear memory.
 */
#define V9X_TDFX_GLIDE_GUARD_BYTES 0x00100000ul
v9x_u32 v9x_tdfx_vsa100_glide_bytes(v9x_u32 linear_bytes);

/*
 * vidProcCfg with the desktop shown and the overlay off, linear: what the
 * BIOS mode set leaves (30040C81h at 640x480x16) and what a Glide session
 * that never closed does not (32E50501h, A8U4I5 boot 424). Every other bit,
 * the formats included, is left as the mode set made it.
 */
v9x_u32 v9x_tdfx_vsa100_desktop_vidproc(v9x_u32 vid_proc_cfg);

/*
 * vidDesktopOverlayStride with the desktop stride (bits 14:0, bytes when
 * linear) set to the mode's pitch; the overlay half is kept.
 */
v9x_u32 v9x_tdfx_vsa100_desktop_stride(v9x_u32 stride_register,
                                        v9x_u32 pitch_bytes);

/*
 * pllCtrl0-2 output in kHz: 14318.18 x (N + 2) / ((M + 2) x 2^K), with N in
 * bits 15:8, M in bits 7:2, K in bits 1:0. 0 for M = 0, which the Mini-Me's
 * unused PLL0 holds and which has an attributed silicon exception.
 */
v9x_u32 v9x_tdfx_vsa100_pll_khz(v9x_u32 pll_ctrl);

#endif
