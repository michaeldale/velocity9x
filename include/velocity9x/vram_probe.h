/*
 * Installed video memory, measured: the decision half.
 *
 * A BIOS's VBE 4F00h figure is not always the card's. The MGA-2164W in
 * A8U4I5 reports 4 MiB and holds at least 8
 * (docs\issues\2026-10-10-mga2164w-vbe-reports-half-its-memory.md), and the
 * chip has no size register to ask instead. The memory itself is the only
 * witness: write a distinct signature every V9X_VRAM_PROBE_STEP bytes,
 * highest offset first, and read them all back. On a card whose memory
 * wraps at S bytes every offset at or above S aliases one below it and the
 * lower write lands last, so exactly the points below S keep their own
 * signature; memory that floats instead of wrapping ends the run the same
 * way. The leading run of held points is the size. This is the method of
 * tools\diag\vram_walk_win32.c (V9XVRAM.EXE), which measured that card.
 *
 * Split as src\common\mtrr.c is: this file decides, a family's hook does
 * the stores and loads through the framebuffer selector and nothing else,
 * and every rule here is host-tested.
 */
#ifndef VELOCITY9X_VRAM_PROBE_H
#define VELOCITY9X_VRAM_PROBE_H

#include "velocity9x/types.h"

/* 512 KiB between points, so a 16 MiB window is 32 points. Inside each step
 * rather than at it, so no point is the first byte an allocator hands out. */
#define V9X_VRAM_PROBE_STEP      0x00080000ul
#define V9X_VRAM_PROBE_INNER     0x00000200ul
#define V9X_VRAM_PROBE_MAX_POINTS 32u
#define V9X_VRAM_PROBE_SIGNATURE 0x5a3c0000ul

/* Points a window of mapped_bytes holds, at most V9X_VRAM_PROBE_MAX_POINTS. */
v9x_u32 v9x_vram_probe_points(v9x_u32 mapped_bytes);

/* The byte offset of point index, and the signature written there. */
v9x_u32 v9x_vram_probe_offset(v9x_u32 index);
v9x_u32 v9x_vram_probe_signature(v9x_u32 index);

/* The installed bytes the readback shows: the leading run of points that
 * kept their own signature, times the step. 0 when point 0 did not hold,
 * which means the walk itself failed, not that there is no memory. */
v9x_u32 v9x_vram_probe_size(const v9x_u32 *readback, v9x_u32 count);

/*
 * The memory figure to use. The measurement wins when it is at least what
 * the BIOS reported and no more than the window: a walk cannot show memory
 * that is not there, so a larger answer is the card correcting its BIOS,
 * while a smaller one is far more likely a walk that went wrong than a BIOS
 * that over-reports, and the BIOS figure is kept. With no BIOS figure the
 * measurement stands alone.
 */
v9x_u32 v9x_vram_probe_accept(v9x_u32 measured, v9x_u32 reported,
                              v9x_u32 mapped_bytes);

#endif
