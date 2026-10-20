/*
 * Intel Gen7.5 (Haswell): the arithmetic of pipelined render submission.
 *
 * Phase 1 of docs\plans\intel-haswell-pipelined-submission.md. Pure: no
 * DDHAL header, so scripts\build-host.ps1 compiles it and
 * tests\host\test_intel_hsw_pipe.c holds it to the plan. d3d_hsw.c reads
 * the status page and the reserve and applies the answers, as enable16.c
 * applies src\common\mtrr.c's.
 *
 * The reserve (velocity9x\intel_hsw.h) keeps its 128 KB and its place; its
 * per-draw state, batch and vertex areas are cut in two, so the CPU can
 * fill one slot while the render engine reads the other. The slot is the
 * sequence's low bit. The sequence lives in the shared status page, so two
 * Direct3D processes alternate slots with no state of their own.
 *
 * The ring itself needs nothing new: Gen3's v9x_i9xx_ring_plan (i9xx_ring.c)
 * plans a submission against a fresh HEAD with the same rules this ring has,
 * and the host tests hold it to the Haswell case.
 */
#ifndef VELOCITY9X_INTEL_HSW_PIPE_H
#define VELOCITY9X_INTEL_HSW_PIPE_H

#include "velocity9x/types.h"
#include "velocity9x/intel_hsw.h"
#include "velocity9x/intel_hsw_eu.h"

#define V9X_HSW_PIPE_SLOTS 2ul

/*
 * Each slot's share of the reserve, as reserve offsets. The per-draw state
 * blocks sit below the kernel slots (state + 4000h, V9X_INTEL_HSW_KERNEL_OFF),
 * each 8 KB against the 1540h V9X_HSW_SHADE_STATE_BYTES uses; the batch area
 * and the vertex area are halved.
 */
#define V9X_HSW_PIPE_STATE_BLOCK_BYTES  ((v9x_u32)0x00002000ul)
#define V9X_HSW_PIPE_BATCH_BYTES        ((v9x_u32)0x00001000ul)
#define V9X_HSW_PIPE_VERTEX_BYTES       ((v9x_u32)0x00007000ul)

struct v9x_hsw_pipe_slot {
    v9x_u32 index;
    /* The block the batch names as its state base: its per-draw state at
     * the V9X_HSW_SHADE_* offsets from here. */
    v9x_u32 state_off;
    /* Bytes from state_off to the end of the state area, kernels included:
     * the state size the workload is given. */
    v9x_u32 state_span;
    /* The kernel area, as an offset from state_off: a kernel in kernel
     * slot k is at kernel_base + k * V9X_INTEL_HSW_KERNEL_SLOT. */
    v9x_u32 kernel_base;
    v9x_u32 batch_off;
    v9x_u32 batch_bytes;
    v9x_u32 vertex_off;
    v9x_u32 vertex_bytes;
};

/* The slot sequence `sequence` is submitted from: its low bit. */
v9x_status v9x_intel_hsw_pipe_slot(v9x_u32 sequence,
                                   struct v9x_hsw_pipe_slot *slot);

/*
 * Whether the next sequence, sent + 1, may write its slot: the sequence
 * that last used that slot, sent - 1, has been stored back. Equivalently,
 * at most one submission is outstanding. Wrapping 32-bit arithmetic; a
 * `done` ahead of `sent`, which only a corrupted status page can produce,
 * is never free.
 */
v9x_u16 v9x_intel_hsw_pipe_may_write(v9x_u32 sent, v9x_u32 done);

/* Every submitted sequence stored back: the render engine is idle. */
v9x_u16 v9x_intel_hsw_pipe_idle(v9x_u32 sent, v9x_u32 done);

/*
 * Kernel-slot signatures, one per V9X_INTEL_HSW_KERNEL_SLOTS slot, in the
 * status page from dword V9X_HSW_PIPE_SIG_STATUS_DWORD. The slot index is a
 * function of the stages, specular and fog but not of the fog colour
 * (d3d_hsw.c, v9x_d3d_hsw_kernel_slot), so the same slot can hold different
 * words; a slot whose signature differs from the draw's must not be
 * rewritten while a batch that may execute it is outstanding.
 *
 * Word 0 packs the description with a valid bit, so the zeroed page a
 * ring open leaves never matches; words 1 to 3 are the fog colour, and zero
 * without fog, because the builder reads it only with fog (hsw_eu.c).
 */
#define V9X_HSW_PIPE_SIG_DWORDS        4ul
#define V9X_HSW_PIPE_SIG_VALID         ((v9x_u32)0x80000000ul)
#define V9X_HSW_PIPE_SIG_STATUS_DWORD  ((v9x_u32)0x100ul)

void v9x_intel_hsw_pipe_kernel_sig(const struct v9x_hsw_ps_desc *desc,
                                   v9x_u32 *sig);
v9x_u16 v9x_intel_hsw_pipe_kernel_sig_equal(const v9x_u32 *a,
                                            const v9x_u32 *b);

#endif
