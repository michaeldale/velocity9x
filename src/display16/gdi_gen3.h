/*
 * GDI screen-to-screen copies on the Gen3 blitter, the intel-gma family's
 * arm of the BitBlt dispatcher (gdi_accel.c): the interface it calls. The
 * implementation is src\display16\gdi_gen3.c; see that file's header and
 * docs\plans\gen3-gdi-blitter.md.
 */
#ifndef VELOCITY9X_GDI_GEN3_H
#define VELOCITY9X_GDI_GEN3_H

/* The display ABI header first: it brings in <windows.h>, which
 * win9x_ddraw_abi.h's types need. */
#include "win9x_display_abi.h"
#include "velocity9x/leaf16.h"
#include "velocity9x/win9x_ddraw_abi.h"

/* What v9x_gdi_gen3_run did. FAILED and DONE_FAILED are an engine that
 * must not be fed again: the dispatcher latches the poison. */
#define V9X_GDI_GEN3_FAILED       0u   /* nothing drawn; the DIB Engine draws */
#define V9X_GDI_GEN3_DONE         1u
#define V9X_GDI_GEN3_DONE_FAILED  2u   /* drawn as far as it got; see .c */

/*
 * At Enable, from the engine descriptor gdi_accel.c reads: describe the
 * register and ring windows. reserve is where the reserve_video_memory hook
 * put the reserve (the ring's graphics address and the end of what a copy
 * may touch), which gdi_accel.c reads because gdi_gen3.c, in LEAFCODE,
 * cannot call the hook. 1 when copies may run.
 */
WORD V9X_LEAF_FAR v9x_gdi_gen3_configure(unsigned long control_base,
                                         unsigned long ring_base,
                                         unsigned long ring_size,
                                         unsigned long reserve,
                                         unsigned long vram_bytes);

/*
 * Gate 8: build the copy's first stream, from the screen PDEVICE's depth,
 * base and pitch. 0 when its band plan or the decoder refuses it, so the
 * DIB Engine copies it untouched; nothing has been submitted.
 */
WORD V9X_LEAF_FAR v9x_gdi_gen3_prepare(V9X_DIB_ENGINE FAR *device,
                                       WORD destination_x,
                                       WORD destination_y,
                                       WORD source_x, WORD source_y,
                                       WORD width, WORD height);

/* Submit the prepared copy, every stream of it, and wait for each.
 * V9X_GDI_GEN3_*; the counters a timeout or a submission touches are
 * stats'. */
WORD V9X_LEAF_FAR v9x_gdi_gen3_run(V9X_GDI_STATS FAR *stats);

#endif
