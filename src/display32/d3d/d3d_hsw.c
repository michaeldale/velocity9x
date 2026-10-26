/*
 * Intel Gen7.5 (Haswell) Direct3D engine.
 *
 * Phase F2a of docs\plans\intel-haswell-direct3d.md. Every command and state
 * word comes from the host-tested builders in src\chipsets\intel\hsw_3d.c
 * and every pixel shader from the generator in hsw_eu.c; the batches are
 * the ones Phases C to F1a drew pixel-exact on the i5-4570
 * (docs\decisions\2026-10-17-haswell-phase-*). This file resolves surfaces,
 * maps the neutral draw onto that state, and submits.
 *
 * The render ring was opened by the mini-VDD at reserve + 2000h beside the
 * blitter's, under the forcewake it holds (HSW_ENGINE_OPEN). The first
 * process to find the status page without the render-initialised mark runs
 * the initialising batch Phase C measured.
 *
 * Pipelined (docs\plans\intel-haswell-pipelined-submission.md, Phase 3):
 * the reserve's per-draw state, batch and vertex areas are two slots, and
 * the next sequence's low bit picks one (hsw_pipe.c). A draw waits until
 * the batch that last used its slot is stored back - at most one batch is
 * left running - fills the slot, submits, and returns without waiting for
 * its own store, so the CPU builds the next draw while the GPU runs this
 * one. Before 2026-10-20 every draw waited for its store, and the 4570 spent
 * 75 us of each 158 us batch doing so
 * (docs\decisions\2026-10-20-haswell-pipelined-phase0-wait-split.md).
 *
 * What used to rely on the RCS being idle now waits for it instead: the
 * blitter's idle test covers both rings (eng_hsw.c), and Flip, Lock, Blt
 * and every drain go through it. Three things stay synchronous: a draw made
 * while GDI may be drawing on the screen (the 16-bit driver writes GDI and
 * the cursor with no hook this DLL sees; ddhal_core.c tracks exclusive mode
 * and FlipToGDISurface), the depth fill, and the initialising batch.
 *
 * No state lives in this DLL across draws: it is mapped into every Direct3D
 * process, and every batch carries all of its state. The sequence, and so
 * the slot, is the status page's, shared by every process; HAL callbacks
 * run under the Win16 lock, one at a time.
 *
 * Depth is F2b's: D16_UNORM Z buffers, which Gen7 always reads Y-tiled, so
 * this file places them in whole tiles and fills them by drawing - a
 * depth-only quad with colour writes masked, the shape the 4570 measured
 * (docs\decisions\2026-10-17-haswell-phase-f2b-depth.md). A Lock of a Z
 * buffer returns the tiled bytes; detiling is not done.
 */
#include "d3d_internal.h"
#include "d3d_hsw_map.h"
#include "d3d_state.h"
#include "velocity9x/intel_hsw.h"
#include "velocity9x/intel_hsw_3d.h"
#include "velocity9x/intel_hsw_eu.h"
#include "velocity9x/intel_hsw_pipe.h"
#include "velocity9x/intel_gma.h"
#include "r3d/r3d_cull.h"

/* Graphics addresses inside the reserve (velocity9x\intel_hsw.h), which the
 * 16-bit side places once a boot and publishes in the engine block. */
#define V9X_D3D_HSW_GFX(offset) (v9x_hal->engine.reserve_offset + (offset))
#define V9X_D3D_HSW_RCS_RING    V9X_D3D_HSW_GFX(V9X_INTEL_HSW_RCS_RING_OFF)
#define V9X_D3D_HSW_STATUS_GFX  V9X_D3D_HSW_GFX(V9X_INTEL_HSW_RING_BYTES)
/* The batch's own PIPE_CONTROL breadcrumb, a qword after the sequences. */
#define V9X_D3D_HSW_CRUMB_GFX   (V9X_D3D_HSW_STATUS_GFX + 0x30ul)

/* Each draw writes its surfaces, sampler, blend and CC state at E's
 * offsets (intel_hsw_3d.h); the border colour E's sampler names is the
 * zeroed run after the CC state. */
#define V9X_D3D_HSW_BORDER_BYTES 0x40ul
#define V9X_D3D_HSW_TEXTURE_OFF  V9X_HSW_SHADE_TEX_SURFACE(0)

/* Each draw's DEPTH_STENCIL_STATE, in the slot E's layout gave the first. */
#define V9X_D3D_HSW_DS_OFF V9X_HSW_SHADE_DS_ALWAYS

/* A Y tile is 128 bytes by 32 rows in 4 KB (Vol 1c), and the depth buffer
 * is always Y-tiled on Gen7 (Vol 2b 3DSTATE_DEPTH_BUFFER Tiled Surface):
 * a Z buffer's pitch, rows and base are whole tiles. */
#define V9X_D3D_HSW_TILE_PITCH 128ul
#define V9X_D3D_HSW_TILE_ROWS  32ul
#define V9X_D3D_HSW_TILE_BYTES 4096ul

/* A depth fill is two triangles. */
#define V9X_D3D_HSW_FILL_VERTICES 6ul

/* A slot's vertex area holds this many triangles of 96 bytes (298). */
#define V9X_D3D_HSW_MAX_TRIANGLES \
    (V9X_HSW_PIPE_VERTEX_BYTES / (3ul * V9X_HSW_TL_VERTEX_BYTES))
/* And this many of 120, with unit 1's coordinates after each vertex (238). */
#define V9X_D3D_HSW_MAX_TRIANGLES2 \
    (V9X_HSW_PIPE_VERTEX_BYTES / (3ul * V9X_HSW_TL2_VERTEX_BYTES))

/* Ring submission: MI_BATCH_BUFFER_START and the sequence store. */
#define V9X_D3D_HSW_SUBMIT_DWORDS 6ul

/* Status reads at about a microsecond, as the blitter's (eng_hsw.c). */
#define V9X_D3D_HSW_SPINS 0x00400000ul

/* The framebuffer is at aperture offset 0, as eng_hsw.c has it. */
#define V9X_D3D_HSW_APERTURE_MASK 0x0ffffffful

/* Fault-trace tag: "HSW3". */
#define V9X_D3D_HSW_FAULT_TAG 0x48535733ul

/* Refusal reasons, in the shared policy counters (V9XTRACE M64PolicyNN). */
#define V9X_D3D_HSW_REFUSE_NONE      0ul
#define V9X_D3D_HSW_REFUSE_TARGET    1ul
#define V9X_D3D_HSW_REFUSE_DEPTH     2ul
#define V9X_D3D_HSW_REFUSE_TEXTURE   3ul
#define V9X_D3D_HSW_REFUSE_TEXOP     4ul
#define V9X_D3D_HSW_REFUSE_BLEND     5ul
#define V9X_D3D_HSW_REFUSE_ADDRESS   6ul
#define V9X_D3D_HSW_REFUSE_KEY       7ul
#define V9X_D3D_HSW_REFUSE_ALPHA     8ul
/* After the mapping (9 and 10 are d3d_hsw_map.h's): the draw was expressible
 * but did not reach the engine. Counted in the same table so a capture names
 * the site; a tester's 3DMark 2001 SE run refused 4,863 batches with every
 * mapping reason at 0 (docs\issues\2026-10-20-haswell-3dmark2001se-
 * artifacts-field.md). */
#define V9X_D3D_HSW_REFUSE_NOT_READY 11ul  /* engine not ready or quarantined */
#define V9X_D3D_HSW_REFUSE_IDLE      12ul  /* blitter or render wait before
                                              the state write              */
#define V9X_D3D_HSW_REFUSE_STATE     13ul  /* a state block would not encode */
#define V9X_D3D_HSW_REFUSE_PIECE     14ul  /* render wait between pieces    */
#define V9X_D3D_HSW_REFUSE_WORKLOAD  15ul  /* the batch would not build     */
#define V9X_D3D_HSW_REFUSE_SUBMIT    16ul  /* the ring submission failed    */
/* Reason 13 split by the block that would not encode, after 3DMark 2001 SE
 * on the 4570 refused 5,203 batches as 13 (2026-10-20); 13 is now the
 * kernel alone. The last texture refused is recorded in the Matrox texture
 * fields, which this engine otherwise leaves at zero (V9XTRACE
 * MgaTexRefusedLast*): Bits the surface format, Size width << 16 | height,
 * Pitch, VidMem the graphics address, Flags unit << 12 | step << 8 |
 * levels with step 1 the surface, 2 the mips, 3 the sampler, and Alpha
 * mip << 16 | mag << 8 | min filter. */
#define V9X_D3D_HSW_REFUSE_TARGET_STATE  17ul  /* target RENDER_SURFACE_STATE */
#define V9X_D3D_HSW_REFUSE_TEXTURE_STATE 18ul  /* a texture's surface, mips or
                                                  sampler state          */
#define V9X_D3D_HSW_REFUSE_COLOUR_STATE  19ul  /* blend, CC or depth state  */

/* SAMPLER_STATE DW3 address modes for all three axes (Vol 2a/2d). */
#define V9X_D3D_HSW_TC_WRAP   0ul
#define V9X_D3D_HSW_TC_MIRROR ((1ul << 6) | (1ul << 3) | 1ul)
#define V9X_D3D_HSW_TC_CLAMP  ((2ul << 6) | (2ul << 3) | 2ul)

static const V9X_D3D_ENGINE_LIMITS v9x_d3d_hsw_limits = {
    16ul,           /* target_bits_per_pixel */
    16384ul,        /* target_pitch_max */
    4ul,            /* target_pitch_align */
    4096ul,         /* target_dimension_max */
    1ul,            /* texture_size_min */
    2048ul,         /* texture_size_max */
    8192.0f,        /* coordinate_limit */
    16ul,           /* depth_bits_per_pixel: D16_UNORM */
    16ul,           /* texture_align */
    /* The CLIP stage is off, and the core does not clip either: the
     * engine draws inside its guard band and clips only what Vol 7's 8K
     * span rule requires (v9x_d3d_hsw_guard). */
    0ul,            /* clip_in_core */
    /* 3DSTATE_DEPTH_BUFFER carries the Z buffer's own pitch, whole tiles
     * wider than its row. */
    1ul,            /* depth_pitch_own */
    0ul,            /* depth_fill_shift */
    2ul,            /* texture_units: step 4, render interface only */
    /* 3DSTATE_MULTISAMPLE Pixel Location is the pixel centre (Vol 2b), and
     * Phase D's coverage matched a reference sampling at x + 0.5. */
    1ul,            /* pixel_centre_half */
    1ul,            /* depth_tiled: Y tiles */
    /* XRGB8888 on a 32 bpp desktop as B8G8R8A8 with alpha writes off
     * (v9x_d3d_hsw_target); the pitch bound is the 16 bpp one. */
    16384ul,        /* target_pitch_max_xrgb8888 */
    /* A 32-bit Z buffer as D24_UNORM_X8_UINT, Y-tiled like D16
     * (v9x_d3d_hsw_depth_format). */
    24ul,           /* depth32_bits */
    /* The draw applies the half-pixel move as it copies each vertex
     * (draw->centre_offset), so the core does not stage every triangle to
     * make it; possible since the core stopped clipping for this engine. */
    1ul,            /* centre_in_engine */
    /* Two Direct3D texture stages in one pass, on the two samplers the
     * render interface already drew with: every pairing the core makes of
     * stages 0 and 1 (d3d_state.c) as a kernel (v9x_d3d_hsw_two_unit_stages),
     * a kernel that will not fit declined by accepts. */
    2ul,            /* texture_stages */
    /* DDI 6 programs are told of the guard band: V9XD32 T10, 3DMark 2000
     * and Half-Life ran on the 4570 with it (boots 81-82). */
    1ul             /* guard_band_ddi6 */
};

/* One texture unit as the sampler reads it. */
typedef struct v9x_d3d_hsw_texture {
    struct v9x_hsw_sampler sampler;
    v9x_u16 format;
    v9x_u32 address;
    v9x_u16 width;
    v9x_u16 height;
    v9x_u32 pitch;
    /* Mip levels the sampler may read: 1, or a chain create_surface laid
     * out (v9x_d3d_hsw_mip_levels). */
    v9x_u8 levels;
} V9X_D3D_HSW_TEXTURE;

/* What the mapping makes of a draw. */
typedef struct v9x_d3d_hsw_state {
    struct v9x_hsw_ps_desc desc;
    struct v9x_hsw_blend blend;
    v9x_u16 target_format;
    V9X_D3D_HSW_TEXTURE texture[2];
    v9x_u8 alpha_ref;
    /* Texture units sampled: 0, 1, or 2 for a render-interface draw with
     * texcoords1 (step 4). */
    int textured;
    int depth;
    v9x_u32 depth_address;
    v9x_u32 depth_pitch;
    v9x_u8 depth_function;
    v9x_u8 depth_write;
    /* D16_UNORM, or D24_UNORM_X8 for a 32-bit Z buffer
     * (v9x_d3d_hsw_depth_format). */
    v9x_u8 depth_format;
    /* Render interface step 2: the drawing rectangle, inclusive, and
     * whether any pixel of the target is inside it. */
    v9x_u32 scissor[4];
    int scissor_empty;
} V9X_D3D_HSW_STATE;

/* Once per process: the ring was found where the mini-VDD put it. */
static int v9x_d3d_hsw_ring_checked = 0;
/* Set on a timeout; nothing is submitted again in this process. */
static int v9x_d3d_hsw_quarantined = 0;

static DWORD v9x_d3d_hsw_read32(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_d3d_hsw_write32(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

static v9x_u32 *v9x_d3d_hsw_reserve(DWORD offset)
{
    return (v9x_u32 *)(v9x_hal->engine.ring_linear_base + offset);
}

static volatile DWORD *v9x_d3d_hsw_status(void)
{
    return (volatile DWORD *)v9x_d3d_hsw_reserve(V9X_INTEL_HSW_RING_BYTES);
}

static DWORD v9x_d3d_hsw_vram(DWORD offset)
{
    return (v9x_hal->fb.physical_base & V9X_D3D_HSW_APERTURE_MASK) + offset;
}

static void v9x_d3d_hsw_quarantine(void)
{
    if (v9x_hal->engine.idle_timeouts++ == 0ul) {
        v9x_trace_flush_fault(V9X_D3D_HSW_FAULT_TAG,
                              v9x_d3d_hsw_read32(V9X_INTEL_HSW_RCS_HEAD));
    }
    v9x_d3d_hsw_quarantined = 1;
    v9x_hal->engine.flags &= ~V9X_DD_ENGINE_VALID;
}

/* Every submitted render sequence has been stored back. `polls`, when not
 * null, receives how many status reads found it not yet stored. */
static int v9x_d3d_hsw_wait_rcs_polled(DWORD *polls)
{
    volatile DWORD *status = v9x_d3d_hsw_status();
    DWORD spins;
    DWORD started = V9X_TIME_BEGIN();

    /* Timed as the render drain: the GPU's share of a synchronous draw. */
    for (spins = 0ul; spins < V9X_D3D_HSW_SPINS; ++spins) {
        if (status[V9X_INTEL_HSW_STATUS_RCS_DONE] ==
            status[V9X_INTEL_HSW_STATUS_RCS_SENT]) {
            V9X_TIME_END(V9X_TIME_RENDER_DRAIN, started);
            if (polls != 0) {
                *polls = spins;
            }
            return 1;
        }
    }
    V9X_TIME_END(V9X_TIME_RENDER_DRAIN, started);
    if (polls != 0) {
        *polls = spins;
    }
    v9x_d3d_hsw_quarantine();
    return 0;
}

static int v9x_d3d_hsw_wait_rcs(void)
{
    return v9x_d3d_hsw_wait_rcs_polled(0);
}

/*
 * The slot the next sequence submits from, once the batch that last used
 * it has been stored back (v9x_intel_hsw_pipe_may_write: at most one batch
 * outstanding). Phase 0's counters keep their meaning: RenderDrainWaits
 * these waits, RenderDrainStalls those that found the slot still in use,
 * BreadcrumbLagPollsTotal and -Max the status reads those stalls took. A
 * timeout is the render engine's quarantine, before anything is written.
 */
static int v9x_d3d_hsw_wait_slot(struct v9x_hsw_pipe_slot *slot)
{
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;
    volatile DWORD *status = v9x_d3d_hsw_status();
    DWORD spins;
    DWORD sent = 0ul;
    DWORD started = V9X_TIME_BEGIN();

    ++diagnostics->render_drain_waits;
    for (spins = 0ul; spins < V9X_D3D_HSW_SPINS; ++spins) {
        sent = status[V9X_INTEL_HSW_STATUS_RCS_SENT];
        if (v9x_intel_hsw_pipe_may_write(
                sent, status[V9X_INTEL_HSW_STATUS_RCS_DONE]) == V9X_TRUE) {
            break;
        }
    }
    V9X_TIME_END(V9X_TIME_RENDER_DRAIN, started);
    if (spins != 0ul) {
        ++diagnostics->render_drain_stalls;
        diagnostics->breadcrumb_lag_polls_total += spins;
        if (spins > diagnostics->breadcrumb_lag_polls_max) {
            diagnostics->breadcrumb_lag_polls_max = spins;
        }
    }
    if (spins == V9X_D3D_HSW_SPINS) {
        v9x_d3d_hsw_quarantine();
        return 0;
    }
    return v9x_intel_hsw_pipe_slot(sent + 1ul, slot) == V9X_STATUS_OK;
}

/*
 * Room in the render ring for one submission, planned against a fresh HEAD
 * by Gen3's planner (i9xx_ring.c), whose rules this ring shares: the tail
 * qword-aligned, an eight-byte guard before the head, a dword head while
 * the parser is busy. At most one six-dword submission is unread, so a 4 KB
 * ring never waits in practice; it is still planned, because Gen3's first
 * asynchronous build corrupted its output by planning against a stale head
 * (docs\decisions\2026-10-01-intel-async-corruption-was-the-dword-head.md).
 * Nothing has been published when this fails, so the draw is refused.
 */
static int v9x_d3d_hsw_ring_room(DWORD tail, struct v9x_i9xx_ring_plan *plan)
{
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;
    DWORD head;
    DWORD spins;
    v9x_status status;

    for (spins = 0ul; spins < V9X_D3D_HSW_SPINS; ++spins) {
        head = v9x_d3d_hsw_read32(V9X_INTEL_HSW_RCS_HEAD) &
               V9X_INTEL_HSW_HEAD_MASK;
        status = v9x_i9xx_ring_plan(head, tail, V9X_INTEL_HSW_RING_BYTES,
                                    V9X_D3D_HSW_SUBMIT_DWORDS, plan);
        if (status == V9X_STATUS_OK) {
            if (spins != 0ul) {
                ++diagnostics->ring_space_waits;
                diagnostics->ring_space_polls_total += spins;
                if (spins > diagnostics->ring_space_polls_max) {
                    diagnostics->ring_space_polls_max = spins;
                }
            }
            return 1;
        }
        if (status != V9X_STATUS_INSUFFICIENT_MEMORY) {
            return 0;
        }
    }
    ++diagnostics->ring_space_timeouts;
    return 0;
}

/*
 * The slot's batch, then the sequence store, at the ring's tail. `wait`
 * waits for the store before returning, as every submission did before
 * Phase 3; otherwise the batch is the GPU's once TAIL moves, and a later
 * wait finds out how it ended. A failure after TAIL moved is never a
 * refused draw (the plan's Gen3 invariant 8): the timeout is the
 * quarantine at whichever wait sees it.
 */
static int v9x_d3d_hsw_submit(const struct v9x_hsw_pipe_slot *slot, int wait)
{
    volatile DWORD *ring =
        (volatile DWORD *)v9x_d3d_hsw_reserve(V9X_INTEL_HSW_RCS_RING_OFF);
    volatile DWORD *status = v9x_d3d_hsw_status();
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;
    struct v9x_i9xx_ring_plan plan;
    v9x_u32 dw[V9X_D3D_HSW_SUBMIT_DWORDS];
    struct v9x_hsw_batch batch;
    DWORD sequence;
    DWORD tail;
    DWORD index;

    sequence = status[V9X_INTEL_HSW_STATUS_RCS_SENT] + 1ul;
    v9x_intel_hsw_batch_init(&batch, dw, V9X_D3D_HSW_SUBMIT_DWORDS);
    v9x_intel_hsw_mi_batch_buffer_start(
        &batch, V9X_D3D_HSW_GFX(slot->batch_off));
    v9x_intel_hsw_mi_store_data_imm(
        &batch, V9X_D3D_HSW_STATUS_GFX + V9X_INTEL_HSW_STATUS_RCS_DONE * 4ul,
        sequence);
    if (batch.overflow != 0u) {
        return 0;
    }

    tail = v9x_d3d_hsw_read32(V9X_INTEL_HSW_RCS_TAIL) &
           V9X_INTEL_HSW_TAIL_MASK;
    if (!v9x_d3d_hsw_ring_room(tail, &plan)) {
        return 0;
    }
    /* Ring memory first, every word of it, then the one TAIL write. */
    for (index = 0ul; index < plan.pad_dwords; ++index) {
        ring[tail / 4ul + index] = 0ul;
    }
    for (index = 0ul; index < V9X_D3D_HSW_SUBMIT_DWORDS; ++index) {
        ring[plan.command_tail / 4ul + index] = dw[index];
    }
    status[V9X_INTEL_HSW_STATUS_RCS_SENT] = sequence;
    diagnostics->breadcrumb_issued_last = sequence;
    /* Ring, batch, vertices and state out of any write-combining buffers
     * before TAIL tells the render engine to read them (d3d_i9xx.c). */
    v9x_wc_drain();
    v9x_d3d_hsw_write32(V9X_INTEL_HSW_RCS_TAIL, plan.next_tail);
    (void)v9x_d3d_hsw_read32(V9X_INTEL_HSW_RCS_TAIL);
    if (wait) {
        ++diagnostics->sync_submits;
        return v9x_d3d_hsw_wait_rcs();
    }
    ++diagnostics->async_submits;
    diagnostics->async_enabled = 1ul;
    return 1;
}

/*
 * The initialising batch (Phase C), once per ring open: the reserve is
 * zeroed at open, which removes the mark. Draw batches read the
 * initialising state too - the viewports, the scissor and the default
 * depth-stencil state at V9X_HSW_INIT_* from their state base - so it is
 * written into both slots' blocks; it is fixed, so both copies are the
 * same words.
 */
static int v9x_d3d_hsw_initialise(void)
{
    volatile DWORD *status = v9x_d3d_hsw_status();
    struct v9x_hsw_pipe_slot slot;
    struct v9x_hsw_batch batch;
    DWORD index;

    if (status[V9X_INTEL_HSW_STATUS_RCS_INIT] ==
        V9X_INTEL_HSW_RCS_INIT_MARK) {
        return 1;
    }
    for (index = 0ul; index < V9X_HSW_PIPE_SLOTS; ++index) {
        if (v9x_intel_hsw_pipe_slot(index, &slot) != V9X_STATUS_OK ||
            v9x_intel_hsw_init_state(v9x_d3d_hsw_reserve(slot.state_off),
                                     V9X_HSW_INIT_STATE_BYTES / 4ul) !=
                V9X_STATUS_OK) {
            return 0;
        }
    }
    if (!v9x_d3d_hsw_wait_slot(&slot)) {
        return 0;
    }
    v9x_intel_hsw_batch_init(&batch, v9x_d3d_hsw_reserve(slot.batch_off),
                             slot.batch_bytes / 4ul);
    if (v9x_intel_hsw_init_workload(&batch,
                                    V9X_D3D_HSW_GFX(slot.state_off),
                                    slot.state_span,
                                    V9X_D3D_HSW_CRUMB_GFX, 1ul) !=
            V9X_STATUS_OK ||
        !v9x_d3d_hsw_submit(&slot, 1)) {
        return 0;
    }
    status[V9X_INTEL_HSW_STATUS_RCS_INIT] = V9X_INTEL_HSW_RCS_INIT_MARK;
    return 1;
}

/* The blitter's own pair agrees: no blit is queued that could still be
 * writing a draw's texture or target. Read here rather than through the
 * engine's wait_idle, which since Phase 2 also waits for the render ring
 * and would bring back the wait this file has just removed. */
static int v9x_d3d_hsw_blitter_idle(void)
{
    volatile DWORD *status = v9x_d3d_hsw_status();

    return status[0] == status[1];
}

/*
 * A draw runs synchronously when it targets the GDI primary while GDI may
 * be drawing on the screen: the 16-bit driver writes GDI and the software
 * cursor into that buffer with no hook this DLL sees (plan decision 3,
 * narrowed 2026-10-20 and 2026-10-23). While this process holds exclusive
 * mode with its own buffers shown, GDI is not drawing, and every draw is
 * left running. Outside exclusive mode a target past the primary - an
 * OpenGL back buffer - is reached by the CPU only through paths that
 * drain, so it runs on too: before, every GL draw waited, and Quake 3 at
 * 1024x768 spent 70 % of its time there (HD 4600, 2026-10-23). The first
 * form of the rule, "offset 0 or the last flip's buffer", matched a stale
 * shared flip offset and kept Half-Life synchronous; the primary's extent
 * does not move with flips.
 */
static int v9x_d3d_hsw_draw_waits(const V9X_R3D_DRAW *draw)
{
    return v9x_intel_hsw_pipe_draw_waits(
               v9x_present_gdi_may_draw() ? V9X_TRUE : V9X_FALSE,
               draw->target.offset, draw->target.pitch, draw->target.height,
               v9x_hal->fb.visible_bytes) != V9X_FALSE;
}

/* The blitter's conditions, the render ring the mini-VDD opened (CAP_D3D),
 * still on where it was put, and initialised. */
static int v9x_d3d_hsw_ready(void)
{
    if (v9x_hal == 0 || v9x_d3d_hsw_quarantined ||
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) == 0ul ||
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) == 0ul ||
        v9x_hal->engine.engine_type != V9X_DD_ENGINE_TYPE_INTEL_GEN75 ||
        (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_D3D) == 0ul ||
        v9x_hal->engine.control_linear_base == 0ul ||
        v9x_hal->engine.ring_linear_base == 0ul) {
        return 0;
    }
    if (!v9x_d3d_hsw_ring_checked) {
        if ((v9x_d3d_hsw_read32(V9X_INTEL_HSW_RCS_CTL) &
             V9X_INTEL_HSW_CTL_ENABLE) == 0ul ||
            v9x_d3d_hsw_read32(V9X_INTEL_HSW_RCS_START) !=
                V9X_D3D_HSW_RCS_RING) {
            return 0;
        }
        v9x_d3d_hsw_ring_checked = 1;
    }
    /* The words the builders write follow the chip: Ivy Bridge's surface,
     * PS and PIPE_CONTROL rules, its workaround writes going to a status
     * dword nothing reads (intel_hsw_3d.h). Before the initialising batch,
     * which is the first one built. */
    if ((v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_GEN7_IVB) != 0ul) {
        if (v9x_intel_hsw_3d_set_chip(
                V9X_HSW_3D_CHIP_IVYBRIDGE,
                V9X_D3D_HSW_STATUS_GFX +
                    V9X_INTEL_HSW_STATUS_IVB_WA * 4ul) != V9X_STATUS_OK) {
            return 0;
        }
    } else {
        (void)v9x_intel_hsw_3d_set_chip(V9X_HSW_3D_CHIP_HASWELL, 0ul);
    }
    return v9x_d3d_hsw_initialise();
}

/* The 16-bit formats the sampler takes, by mask, as the 3dfx engine reads
 * them; the texture's alpha decides MODULATE's alpha. */
/* The surface's own layout, or the display's when it carries none, through
 * v9x_d3d_hsw_texel_format (d3d_hsw_map.c, host-tested). bytes may be
 * null. */
static int v9x_d3d_hsw_texture_format_of(const V9X_DD_SURFACE_LCL *surface,
                                         v9x_u16 *format, int *has_alpha,
                                         v9x_u32 *bytes)
{
    const V9X_DDPIXELFORMAT *pf;
    v9x_u32 texel_bytes;

    if (surface == 0 || surface->lpGbl == 0) {
        return 0;
    }
    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul) {
        pf = &surface->lpGbl->ddpfSurface;
    } else {
        pf = &v9x_hal->info.vmiData.ddpfDisplay;
    }
    if ((pf->dwFlags & V9X_DDPF_RGB) == 0ul ||
        !v9x_d3d_hsw_texel_format(pf->dwRGBBitCount,
                                  (pf->dwFlags & V9X_DDPF_ALPHAPIXELS) != 0ul,
                                  pf->dwRBitMask, pf->dwGBitMask,
                                  pf->dwBBitMask, pf->dwRGBAlphaBitMask,
                                  format, has_alpha, &texel_bytes)) {
        return 0;
    }
    if (bytes != 0) {
        *bytes = texel_bytes;
    }
    return 1;
}

static int v9x_d3d_hsw_texture_format(const V9X_DD_SURFACE_LCL *surface,
                                      DWORD *format_out)
{
    v9x_u16 format = 0u;
    int has_alpha = 0;

    if (format_out == 0) {
        return 0;
    }
    *format_out = 0ul;
    if (v9x_hal == 0 ||
        !v9x_d3d_hsw_texture_format_of(surface, &format, &has_alpha, 0)) {
        return 0;
    }
    *format_out = format;
    return 1;
}

/*
 * A Z buffer this file placed: video memory, a pitch of whole tiles, on a
 * tile boundary, and carrying the placement's signature (v9x_d3d_place_block
 * leaves lpVidMemHeap null and the block in dwReserved1). One DirectDraw's
 * heap placed has none of the padding rows the last row of tiles reaches
 * into, so the engine would write past it; it is refused.
 */
static int v9x_d3d_hsw_depth_surface(const V9X_DD_SURFACE_LCL *surface,
                                     DWORD *offset)
{
    const V9X_DD_SURFACE_GBL *global;

    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_ZBUFFER) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return 0;
    }
    global = surface->lpGbl;
    if (global->lPitch <= 0l ||
        ((DWORD)global->lPitch & (V9X_D3D_HSW_TILE_PITCH - 1ul)) != 0ul ||
        global->dwBlockSizeX != 0ul || global->dwReserved1 == 0ul ||
        global->wWidth == 0u || global->wHeight == 0u ||
        (DWORD)global->wWidth > v9x_d3d_hsw_limits.target_dimension_max ||
        (DWORD)global->wHeight > v9x_d3d_hsw_limits.target_dimension_max) {
        return 0;
    }
    *offset = v9x_surface_offset(surface);
    return *offset != 0xfffffffful &&
           (*offset & (V9X_D3D_HSW_TILE_BYTES - 1ul)) == 0ul;
}

/* Direct3D's blend factors (V9X_R3D_BLEND_*) as Gen's (Vol 2a
 * 3D_Color_Buffer_Blend_Factor); 0 for one this engine does not take. */
static v9x_u8 v9x_d3d_hsw_factor(v9x_u32 factor)
{
    switch (factor) {
    case V9X_R3D_BLEND_ZERO:
        return 0x11u;
    case V9X_R3D_BLEND_ONE:
        return 0x01u;
    case V9X_R3D_BLEND_SRCCOLOR:
        return 0x02u;
    case V9X_R3D_BLEND_INVSRCCOLOR:
        return 0x12u;
    case V9X_R3D_BLEND_SRCALPHA:
        return 0x03u;
    case V9X_R3D_BLEND_INVSRCALPHA:
        return 0x13u;
    case V9X_R3D_BLEND_DESTCOLOR:
        return 0x05u;
    case V9X_R3D_BLEND_INVDESTCOLOR:
        return 0x15u;
    case V9X_R3D_BLEND_SRCALPHASAT:
        return 0x06u;
    default:
        return 0u;
    }
}

/* Direct3D's comparisons 1-8 against Gen's (Vol 2a 3D_Compare_Function):
 * NEVER..GEQUAL keep their numbers, ALWAYS is 0. */
static v9x_u8 v9x_d3d_hsw_compare(v9x_u32 function)
{
    if (function == V9X_R3D_CMP_ALWAYS) {
        return 0u;
    }
    return (v9x_u8)(function & 7ul);
}

/* MAPFILTER_LINEAR for the filters whose texels blend within a level; the
 * part between levels is v9x_d3d_hsw_mip_filter's. The DirectX 5 names
 * read as d3d_i9xx.c's v9x_d3d_i9xx_filter settles them: LINEAR after
 * MIP is within a level, before it between levels. */
static v9x_u8 v9x_d3d_hsw_filter(v9x_u32 filter)
{
    return filter == V9X_R3D_FILTER_LINEAR ||
                   filter == V9X_R3D_FILTER_MIPLINEAR ||
                   filter == V9X_R3D_FILTER_LINEARMIPLINEAR
               ? 1u : 0u;
}

/* SAMPLER_STATE DW0 21:20, Mip Mode Filter (Vol 2d): MIPFILTER_NONE 0,
 * _NEAREST 1, _LINEAR 3. */
#define V9X_D3D_HSW_MIPFILTER_NONE    0u
#define V9X_D3D_HSW_MIPFILTER_NEAREST 1u
#define V9X_D3D_HSW_MIPFILTER_LINEAR  3u

static v9x_u8 v9x_d3d_hsw_mip_filter(v9x_u32 filter)
{
    if (filter == V9X_R3D_FILTER_MIPNEAREST ||
        filter == V9X_R3D_FILTER_MIPLINEAR) {
        return V9X_D3D_HSW_MIPFILTER_NEAREST;
    }
    if (filter == V9X_R3D_FILTER_LINEARMIPNEAREST ||
        filter == V9X_R3D_FILTER_LINEARMIPLINEAR) {
        return V9X_D3D_HSW_MIPFILTER_LINEAR;
    }
    return V9X_D3D_HSW_MIPFILTER_NONE;
}

/*
 * How many levels of a texture's chain the sampler may read: one when there
 * is no chain or its levels are not where v9x_intel_hsw_miptree puts them,
 * and each level that is, in order, otherwise.
 *
 * RENDER_SURFACE_STATE has one base and one pitch for every level, so only
 * a chain create_surface placed as one tree can be sampled past level 0.
 * One the heap placed - because create_surface declined it, or an
 * application attached the levels itself - is sampled at level 0 with no
 * mip filter, as every texture was before, and is counted in the mip_gap
 * counters so a capture tells the two apart. Gen3's v9x_d3d_i9xx_mip_levels
 * makes the same walk for its own layout.
 */
static v9x_u8 v9x_d3d_hsw_mip_levels(const V9X_DD_SURFACE_LCL *top)
{
    const V9X_DD_SURFACE_LCL *chain[V9X_HSW_MIP_LEVELS_MAX];
    const V9X_DD_SURFACE_LCL *level = top;
    struct v9x_hsw_miptree tree;
    DWORD width = (DWORD)top->lpGbl->wWidth;
    DWORD height = (DWORD)top->lpGbl->wHeight;
    DWORD count = 1ul;
    DWORD verified;
    v9x_u16 format;
    int has_alpha;
    v9x_u32 bytes;

    if ((top->ddsCaps & V9X_DDSCAPS_MIPMAP) == 0ul) {
        return 1u;
    }
    ++v9x_hal->d3d_diagnostics.mip_chain_checks;
    chain[0] = top;

    /* Down the attachments: the next level is the attached surface that is
     * itself a mip level, each edge half the one before. */
    while (count < V9X_HSW_MIP_LEVELS_MAX) {
        const V9X_DD_ATTACH_NODE *node =
            (const V9X_DD_ATTACH_NODE *)level->lpAttachList;
        const V9X_DD_SURFACE_LCL *next = 0;
        DWORD next_width = width >> count;
        DWORD next_height = height >> count;

        while (node != 0) {
            if (node->object != 0 && node->object != level &&
                (node->object->ddsCaps & V9X_DDSCAPS_MIPMAP) != 0ul) {
                next = node->object;
                break;
            }
            node = node->next;
        }
        if (next == 0) {
            break;
        }
        if (next_width == 0ul) {
            next_width = 1ul;
        }
        if (next_height == 0ul) {
            next_height = 1ul;
        }
        if (next->lpGbl == 0 ||
            (DWORD)next->lpGbl->wWidth != next_width ||
            (DWORD)next->lpGbl->wHeight != next_height) {
            ++v9x_hal->d3d_diagnostics.mip_gap_shape;
            break;
        }
        chain[count++] = next;
        level = next;
    }
    if (count == 1ul) {
        return 1u;
    }
    /* The tree is laid out in the texel size create_miptree placed it at. */
    if (!v9x_d3d_hsw_texture_format_of(top, &format, &has_alpha, &bytes) ||
        v9x_intel_hsw_miptree((v9x_u16)width, (v9x_u16)height,
                              (v9x_u8)count, (v9x_u8)bytes, &tree) !=
            V9X_STATUS_OK) {
        ++v9x_hal->d3d_diagnostics.mip_gap_shape;
        ++v9x_hal->d3d_diagnostics.mip_chain_gaps;
        return 1u;
    }

    /* Every level at the tree's pitch and exactly at its place; the first
     * that is not ends the usable chain. A shorter chain's levels sit where
     * the longer tree put them, because each level's place depends only on
     * the levels above it. */
    verified = 1ul;
    if ((DWORD)top->lpGbl->lPitch == tree.pitch) {
        while (verified < count &&
               (DWORD)chain[verified]->lpGbl->lPitch == tree.pitch &&
               chain[verified]->lpGbl->fpVidMem ==
                   top->lpGbl->fpVidMem + tree.level_offset[verified]) {
            ++verified;
        }
    }
    if (verified < count) {
        ++v9x_hal->d3d_diagnostics.mip_chain_gaps;
        ++v9x_hal->d3d_diagnostics.mip_gap_offset;
        v9x_hal->d3d_diagnostics.mip_gap_expected =
            top->lpGbl->fpVidMem + tree.level_offset[verified];
        v9x_hal->d3d_diagnostics.mip_gap_actual =
            chain[verified]->lpGbl->fpVidMem;
    }
    v9x_hal->d3d_diagnostics.mip_chain_levels = verified - 1ul;
    if (verified - 1ul > v9x_hal->d3d_diagnostics.mip_levels_max) {
        v9x_hal->d3d_diagnostics.mip_levels_max = verified - 1ul;
    }
    return (v9x_u8)verified;
}

static v9x_u32 v9x_d3d_hsw_float(v9x_u32 byte)
{
    union {
        float value;
        v9x_u32 bits;
    } convert;

    convert.value = (float)byte / 255.0f;
    return convert.bits;
}

/* A texture surface as this engine samples it, or the reason it cannot be.
 * has_alpha: whether its format carries alpha. */
static DWORD v9x_d3d_hsw_texture_unit(const V9X_R3D_TEXTURE *texture,
                                      V9X_D3D_HSW_TEXTURE *unit,
                                      int *has_alpha)
{
    V9X_DD_SURFACE_LCL *surface = (V9X_DD_SURFACE_LCL *)texture->object;
    DWORD offset;

    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_TEXTURE) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
        surface->lpGbl->lPitch <= 0l ||
        !v9x_d3d_hsw_texture_format_of(surface, &unit->format, has_alpha,
                                       0)) {
        return V9X_D3D_HSW_REFUSE_TEXTURE;
    }
    offset = v9x_surface_offset(surface);
    if (offset == 0xfffffffful) {
        return V9X_D3D_HSW_REFUSE_TEXTURE;
    }
    if (texture->wrap_u != 0ul || texture->wrap_v != 0ul) {
        return V9X_D3D_HSW_REFUSE_ADDRESS;
    }
    /* MIN's choice between levels is the mip filter only when the chain
     * has levels; MAX LOD, U4.8, stops at the last (Vol 2d). MAG's has no
     * meaning, magnification being level 0. */
    unit->levels = v9x_d3d_hsw_mip_levels(surface);
    unit->sampler.mip_filter = V9X_D3D_HSW_MIPFILTER_NONE;
    unit->sampler.mag_filter = v9x_d3d_hsw_filter(texture->mag_filter);
    unit->sampler.min_filter = v9x_d3d_hsw_filter(texture->min_filter);
    unit->sampler.min_lod = 0u;
    unit->sampler.max_lod = 0u;
    if (unit->levels > 1u) {
        unit->sampler.mip_filter = v9x_d3d_hsw_mip_filter(texture->min_filter);
        unit->sampler.max_lod = (v9x_u16)((unit->levels - 1u) << 8);
    }
    unit->sampler.border_color_pointer = V9X_HSW_SHADE_BORDER_COLOR;
    switch (texture->address) {
    case V9X_R3D_ADDRESS_WRAP:
        unit->sampler.dw3 = V9X_D3D_HSW_TC_WRAP;
        break;
    case V9X_R3D_ADDRESS_MIRROR:
        unit->sampler.dw3 = V9X_D3D_HSW_TC_MIRROR;
        break;
    case V9X_R3D_ADDRESS_CLAMP:
        unit->sampler.dw3 = V9X_D3D_HSW_TC_CLAMP;
        break;
    default:
        return V9X_D3D_HSW_REFUSE_ADDRESS;
    }
    unit->address = v9x_d3d_hsw_vram(offset);
    unit->width = surface->lpGbl->wWidth;
    unit->height = surface->lpGbl->wHeight;
    unit->pitch = (v9x_u32)surface->lpGbl->lPitch;
    if (unit->width == 0u || unit->height == 0u ||
        unit->width > v9x_d3d_hsw_limits.texture_size_max ||
        unit->height > v9x_d3d_hsw_limits.texture_size_max) {
        return V9X_D3D_HSW_REFUSE_TEXTURE;
    }
    return V9X_D3D_HSW_REFUSE_NONE;
}

/* The draw as this engine's state, or the reason it cannot be. */
static DWORD v9x_d3d_hsw_map(const V9X_R3D_DRAW *draw,
                             V9X_D3D_HSW_STATE *state)
{
    V9X_DD_SURFACE_LCL *surface;
    DWORD offset;
    DWORD reason;
    int has_alpha = 0;
    int has_alpha1 = 0;
    v9x_u32 op;
    v9x_u8 target_disable = 0u;

    /* What the render interface states and this engine cannot draw exactly
     * (d3d_hsw_map.c): CPU levels. */
    reason = v9x_d3d_hsw_explicit_refusal(draw);
    if (reason != V9X_D3D_HSW_REFUSE_NONE) {
        return reason;
    }

    state->desc.stage0 = V9X_HSW_PS_DIFFUSE;
    state->desc.stage1 = V9X_HSW_PS_STAGE1_NONE;
    state->desc.fog = 0u;
    state->desc.fog_rgb[0] = 0ul;
    state->desc.fog_rgb[1] = 0ul;
    state->desc.fog_rgb[2] = 0ul;
    state->desc.specular = 0u;
    state->textured = 0;
    state->depth = 0;
    state->depth_format = 0u;
    state->depth_address = 0ul;
    state->depth_pitch = 0ul;
    state->depth_function = 0u;
    state->depth_write = 0u;

    /* 565, 1555, or XRGB8888 with its alpha writes off (target_disable,
     * ORed into the blend state below). */
    if (!v9x_d3d_hsw_target(draw->target.format, &state->target_format,
                            &target_disable)) {
        return V9X_D3D_HSW_REFUSE_TARGET;
    }
    if (draw->target.pitch == 0ul || draw->target.width == 0ul ||
        draw->target.height == 0ul ||
        draw->target.width > v9x_d3d_hsw_limits.target_dimension_max ||
        draw->target.height > v9x_d3d_hsw_limits.target_dimension_max) {
        return V9X_D3D_HSW_REFUSE_TARGET;
    }
    if (v9x_d3d_state_depth_active(draw->depth_enable,
                                   draw->depth.object != 0,
                                   draw->depth.pitch) != 0ul) {
        surface = (V9X_DD_SURFACE_LCL *)draw->depth.object;
        /* The depth buffer is addressed at the target's extent (DW3), so
         * it must cover the target. */
        if (!v9x_d3d_hsw_depth_surface(surface, &offset) ||
            (DWORD)surface->lpGbl->wWidth < draw->target.width ||
            (DWORD)surface->lpGbl->wHeight < draw->target.height ||
            draw->depth_func < V9X_R3D_CMP_NEVER ||
            draw->depth_func > V9X_R3D_CMP_ALWAYS ||
            !v9x_d3d_hsw_depth_format(
                draw->depth.format == V9X_R3D_DEPTH_Z24X8 ? 4ul : 2ul,
                &state->depth_format)) {
            return V9X_D3D_HSW_REFUSE_DEPTH;
        }
        state->depth = 1;
        state->depth_address = v9x_d3d_hsw_vram(offset);
        state->depth_pitch = (v9x_u32)surface->lpGbl->lPitch;
        state->depth_function = v9x_d3d_hsw_compare(draw->depth_func);
        state->depth_write = draw->depth_write != 0ul ? 1u : 0u;
    }

    if (draw->texcoords1 != 0) {
        /* Two units (render interface step 4): each unit's combine is in
         * its color_op and alpha_op, `op` is not set (r3d.h). */
        if (draw->color_key_enable != 0ul) {
            return V9X_D3D_HSW_REFUSE_KEY;
        }
        reason = v9x_d3d_hsw_texture_unit(&draw->texture, &state->texture[0],
                                          &has_alpha);
        if (reason != V9X_D3D_HSW_REFUSE_NONE) {
            return reason;
        }
        reason = v9x_d3d_hsw_texture_unit(&draw->texture1,
                                          &state->texture[1], &has_alpha1);
        if (reason != V9X_D3D_HSW_REFUSE_NONE) {
            return reason;
        }
        reason = v9x_d3d_hsw_two_unit_stages(
            draw->texture.color_op, draw->texture.alpha_op, has_alpha,
            draw->texture1.color_op, draw->texture1.alpha_op, has_alpha1,
            &state->desc.stage0, &state->desc.stage1);
        if (reason != V9X_D3D_HSW_REFUSE_NONE) {
            return reason;
        }
        state->textured = 2;
    } else if (draw->texture.object != 0) {
        reason = v9x_d3d_hsw_texture_unit(&draw->texture, &state->texture[0],
                                          &has_alpha);
        if (reason != V9X_D3D_HSW_REFUSE_NONE) {
            return reason;
        }
        if (draw->color_key_enable != 0ul) {
            return V9X_D3D_HSW_REFUSE_KEY;
        }
        op = draw->texture.op;
        if (op == V9X_R3D_TEXOP_DECAL || op == V9X_R3D_TEXOP_COPY) {
            state->desc.stage0 = V9X_HSW_PS_TEXTURE;
        } else if (op == V9X_R3D_TEXOP_MODULATE) {
            /* D3D's rule: the texture's alpha when it has one. */
            state->desc.stage0 = has_alpha ? V9X_HSW_PS_MODULATE_TEXALPHA
                                           : V9X_HSW_PS_MODULATE;
        } else if (op == V9X_R3D_TEXOP_MODULATEALPHA) {
            state->desc.stage0 = V9X_HSW_PS_MODULATE;
        } else if (op == V9X_R3D_TEXOP_ADD && !has_alpha) {
            state->desc.stage0 = V9X_HSW_PS_ADD;
        } else {
            return V9X_D3D_HSW_REFUSE_TEXOP;
        }
        state->textured = 1;
    }

    state->desc.specular = draw->specular_enable != 0ul ? 1u : 0u;
    if (draw->fog_enable != 0ul) {
        state->desc.fog = 1u;
        state->desc.fog_rgb[0] =
            v9x_d3d_hsw_float((draw->fog_color >> 16) & 0xfful);
        state->desc.fog_rgb[1] =
            v9x_d3d_hsw_float((draw->fog_color >> 8) & 0xfful);
        state->desc.fog_rgb[2] = v9x_d3d_hsw_float(draw->fog_color & 0xfful);
    }

    state->blend.enable = 0u;
    state->blend.color_function = 0u;
    state->blend.src_factor = 0u;
    state->blend.dst_factor = 0u;
    state->blend.independent_alpha = 0u;
    state->blend.alpha_function = 0u;
    state->blend.src_alpha_factor = 0u;
    state->blend.dst_alpha_factor = 0u;
    state->blend.write_disable = 0u;
    state->blend.alpha_test = 0u;
    state->blend.alpha_function_test = 0u;
    /* An explicit draw's mask, as BLEND_STATE's write disables (the depth
     * fill measured all four, F2b); Direct3D's draws state no mask. */
    if (draw->explicit_state != 0ul) {
        state->blend.write_disable =
            v9x_d3d_hsw_write_disable(draw->write_mask);
    }
    state->blend.write_disable |= target_disable;
    state->scissor_empty = !v9x_d3d_hsw_scissor(draw, state->scissor);
    if (draw->blend_enable != 0ul) {
        state->blend.src_factor = v9x_d3d_hsw_factor(draw->src_blend);
        state->blend.dst_factor = v9x_d3d_hsw_factor(draw->dst_blend);
        if (state->blend.src_factor == 0u || state->blend.dst_factor == 0u) {
            return V9X_D3D_HSW_REFUSE_BLEND;
        }
        state->blend.enable = 1u;
    }
    state->alpha_ref = 0u;
    if (draw->alpha_test_enable != 0ul) {
        if (draw->alpha_func < V9X_R3D_CMP_NEVER ||
            draw->alpha_func > V9X_R3D_CMP_ALWAYS) {
            return V9X_D3D_HSW_REFUSE_ALPHA;
        }
        state->blend.alpha_test = 1u;
        state->blend.alpha_function_test =
            v9x_d3d_hsw_compare(draw->alpha_func);
        state->alpha_ref = (v9x_u8)(draw->alpha_ref & 0xfful);
    }
    return V9X_D3D_HSW_REFUSE_NONE;
}

static void v9x_d3d_hsw_count_refusal(DWORD reason)
{
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;

    diagnostics->m64_policy_last = reason;
    if (reason < 20ul) {
        ++diagnostics->m64_policy_counts[reason];
    }
}

/* A kernel's slot is a fixed function of its description
 * (v9x_intel_hsw_pipe_kernel_slot), so every process writes the same words
 * to the same place; the last 128 bytes of a slot stay clear for the EU's
 * prefetch past a kernel's end (V9X_HSW_PIPE_KERNEL_DWORDS). */
#define V9X_D3D_HSW_KERNEL_DWORDS V9X_HSW_PIPE_KERNEL_DWORDS

/*
 * Where a kernel is assembled before it goes to its slot: ordinary cached
 * memory. The EU encoder ORs each instruction's fields into its output
 * (hsw_eu.c), and built in place that is an uncached read and write of the
 * aperture per field - the aperture is UC by the BIOS's MTRR over
 * E0000000-FFFFFFFF (V9XBOOT.INI, boot 20). Measured on the 4570 (boot 21):
 * 16.1 us a batch in place, about 210 ns per dword produced, against 48 ns
 * per dword for the plain vertex copy. Per process, as the DLL's data is;
 * HAL callbacks run under the Win16 lock, so one draw at a time.
 */
static v9x_u32 v9x_d3d_hsw_kernel_words[V9X_D3D_HSW_KERNEL_DWORDS];

/*
 * The kernel for a description, in its kernel slot; its offset from the
 * draw slot's state base.
 *
 * Each kernel slot's signature sits in the status page
 * (v9x_intel_hsw_pipe_kernel_sig). A slot that already holds this
 * description's words is used as it is, with no build and no copy. One that
 * holds something else - the slot index does not see the fog colour - may
 * be executing in the batch still running, so the render engine is drained
 * before the slot is rewritten.
 */
static int v9x_d3d_hsw_write_kernel(const struct v9x_hsw_ps_desc *desc,
                                    const struct v9x_hsw_pipe_slot *slot,
                                    DWORD *kernel)
{
    volatile DWORD *status = v9x_d3d_hsw_status();
    volatile DWORD *stored;
    v9x_u32 wanted[V9X_HSW_PIPE_SIG_DWORDS];
    v9x_u32 held[V9X_HSW_PIPE_SIG_DWORDS];
    v9x_u32 *slot_words;
    DWORD kernel_slot;
    DWORD index;
    v9x_u32 used = 0ul;
    DWORD started = V9X_TIME_BEGIN();
    int built;

    kernel_slot = v9x_intel_hsw_pipe_kernel_slot(desc);
    *kernel = slot->kernel_base + kernel_slot * V9X_INTEL_HSW_KERNEL_SLOT;
    stored = status + V9X_HSW_PIPE_SIG_STATUS_DWORD +
             kernel_slot * V9X_HSW_PIPE_SIG_DWORDS;
    v9x_intel_hsw_pipe_kernel_sig(desc, wanted);
    for (index = 0ul; index < V9X_HSW_PIPE_SIG_DWORDS; ++index) {
        held[index] = stored[index];
    }
    if (v9x_intel_hsw_pipe_kernel_sig_equal(wanted, held) == V9X_TRUE) {
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return 1;
    }
    if (v9x_intel_hsw_pipe_idle(status[V9X_INTEL_HSW_STATUS_RCS_SENT],
                                status[V9X_INTEL_HSW_STATUS_RCS_DONE]) !=
            V9X_TRUE &&
        !v9x_d3d_hsw_wait_rcs()) {
        V9X_TIME_END(V9X_TIME_DECODE, started);
        return 0;
    }
    built = v9x_intel_hsw_ps_kernel_build(v9x_d3d_hsw_kernel_words,
                                          V9X_D3D_HSW_KERNEL_DWORDS, desc,
                                          &used) == V9X_STATUS_OK;
    /* Only the words the builder produced, as building in place wrote; the
     * signature after them, so a slot is never marked for words it does not
     * yet hold. */
    if (built) {
        slot_words = v9x_d3d_hsw_reserve(
            V9X_INTEL_HSW_STATE_OFF + V9X_INTEL_HSW_KERNEL_OFF +
            kernel_slot * V9X_INTEL_HSW_KERNEL_SLOT);
        for (index = 0ul; index < used; ++index) {
            slot_words[index] = v9x_d3d_hsw_kernel_words[index];
        }
        for (index = 0ul; index < V9X_HSW_PIPE_SIG_DWORDS; ++index) {
            stored[index] = wanted[index];
        }
    }
    /* Haswell times the kernel build in the Gen3 decode bucket (TimeDecode
     * in V9XTRACE): the per-batch cost split of 2026-10-18. */
    V9X_TIME_END(V9X_TIME_DECODE, started);
    return built;
}

/* The draw's state in its slot: the target and texture surfaces, the
 * binding table, the sampler, blend, CC and depth-stencil state, the zeroed
 * border colour, and the kernel. */
/* The texture a state write refused, and at which step (see the reason
 * definitions above). */
static void v9x_d3d_hsw_note_texture(const V9X_D3D_HSW_TEXTURE *texture,
                                     DWORD unit, DWORD step)
{
    V9X_D3D_DIAGNOSTICS *diagnostics = &v9x_hal->d3d_diagnostics;

    diagnostics->mga_texture_refused_last_bits = (DWORD)texture->format;
    diagnostics->mga_texture_refused_last_size =
        ((DWORD)texture->width << 16) | (DWORD)texture->height;
    diagnostics->mga_texture_refused_last_pitch = texture->pitch;
    diagnostics->mga_texture_refused_last_vidmem = texture->address;
    diagnostics->mga_texture_refused_last_flags =
        (unit << 12) | (step << 8) | (DWORD)texture->levels;
    diagnostics->mga_texture_refused_last_alpha =
        ((DWORD)texture->sampler.mip_filter << 16) |
        ((DWORD)texture->sampler.mag_filter << 8) |
        (DWORD)texture->sampler.min_filter;
}

/* Zero, or the V9X_D3D_HSW_REFUSE_* reason for the block that would not
 * encode. */
/*
 * Where a draw's fragment state goes, as offsets from the slot's state
 * base. Every draw but a T&L batch's later segments uses the fixed layout
 * E's offsets give (v9x_d3d_hsw_layout_shade); those segments get their
 * own (v9x_d3d_hsw_segment_layout, step 3b).
 */
typedef struct v9x_d3d_hsw_layout {
    DWORD binding_table;
    DWORD tex_surface[2];
    DWORD sampler;
    DWORD blend;
    DWORD cc;
    DWORD ds;
} V9X_D3D_HSW_LAYOUT;

static const V9X_D3D_HSW_LAYOUT v9x_d3d_hsw_layout_shade = {
    V9X_HSW_SHADE_BINDING_TABLE(0),
    { V9X_HSW_SHADE_TEX_SURFACE(0), V9X_HSW_SHADE_TEX_SURFACE(1) },
    V9X_HSW_SHADE_SAMPLER,
    V9X_HSW_SHADE_BLEND_ALPHA_TEST,
    V9X_HSW_SHADE_CC,
    V9X_D3D_HSW_DS_OFF
};

static DWORD v9x_d3d_hsw_write_state_at(const V9X_R3D_DRAW *draw,
                                        const V9X_D3D_HSW_STATE *state,
                                        const struct v9x_hsw_pipe_slot *slot,
                                        const V9X_D3D_HSW_LAYOUT *layout,
                                        DWORD *kernel);

static DWORD v9x_d3d_hsw_write_state(const V9X_R3D_DRAW *draw,
                                     const V9X_D3D_HSW_STATE *state,
                                     const struct v9x_hsw_pipe_slot *slot,
                                     DWORD *kernel)
{
    return v9x_d3d_hsw_write_state_at(draw, state, slot,
                                      &v9x_d3d_hsw_layout_shade, kernel);
}

static DWORD v9x_d3d_hsw_write_state_at(const V9X_R3D_DRAW *draw,
                                        const V9X_D3D_HSW_STATE *state,
                                        const struct v9x_hsw_pipe_slot *slot,
                                        const V9X_D3D_HSW_LAYOUT *layout,
                                        DWORD *kernel)
{
    v9x_u32 *image = v9x_d3d_hsw_reserve(slot->state_off);
    v9x_u32 *table;
    const V9X_D3D_HSW_TEXTURE *texture;
    DWORD index;

    if (v9x_intel_hsw_surface_2d(image + V9X_HSW_DRAW_RT_SURFACE(0) / 4ul,
                                 state->target_format,
                                 v9x_d3d_hsw_vram(draw->target.offset),
                                 (v9x_u16)draw->target.width,
                                 (v9x_u16)draw->target.height,
                                 draw->target.pitch) != V9X_STATUS_OK) {
        return V9X_D3D_HSW_REFUSE_TARGET_STATE;
    }
    table = image + layout->binding_table / 4ul;
    table[0] = V9X_HSW_DRAW_RT_SURFACE(0);
    table[1] = layout->tex_surface[0];
    table[2] = layout->tex_surface[0];
    /* Unit n's surface at E's texture n, its sampler n after the first
     * (the kernel's TEX1 reads binding 2, sampler 1: hsw_eu.c). */
    for (index = 0ul; index < (DWORD)state->textured; ++index) {
        texture = &state->texture[index];
        if (v9x_intel_hsw_surface_2d(
                image + layout->tex_surface[index] / 4ul,
                texture->format, texture->address, texture->width,
                texture->height, texture->pitch) != V9X_STATUS_OK) {
            v9x_d3d_hsw_note_texture(texture, index, 1ul);
            return V9X_D3D_HSW_REFUSE_TEXTURE_STATE;
        }
        if (texture->levels > 1u &&
            v9x_intel_hsw_surface_mips(
                image + layout->tex_surface[index] / 4ul,
                texture->levels) != V9X_STATUS_OK) {
            v9x_d3d_hsw_note_texture(texture, index, 2ul);
            return V9X_D3D_HSW_REFUSE_TEXTURE_STATE;
        }
        if (v9x_intel_hsw_sampler(
                image + (layout->sampler +
                         index * V9X_HSW_SAMPLER_BYTES) / 4ul,
                &texture->sampler) != V9X_STATUS_OK) {
            v9x_d3d_hsw_note_texture(texture, index, 3ul);
            return V9X_D3D_HSW_REFUSE_TEXTURE_STATE;
        }
    }
    if (state->textured == 2) {
        table[2] = layout->tex_surface[1];
    }
    if (!state->textured) {
        /* An untextured kernel samples nothing, but the binding table still
         * names a surface: the target's. */
        table[1] = V9X_HSW_DRAW_RT_SURFACE(0);
        table[2] = V9X_HSW_DRAW_RT_SURFACE(0);
    }
    for (index = 0ul; index < V9X_D3D_HSW_BORDER_BYTES / 4ul; ++index) {
        image[V9X_HSW_SHADE_BORDER_COLOR / 4ul + index] = 0ul;
    }
    if (v9x_intel_hsw_blend(image + layout->blend / 4ul,
                            &state->blend) != V9X_STATUS_OK ||
        v9x_intel_hsw_cc_state(image + layout->cc / 4ul,
                               state->alpha_ref) != V9X_STATUS_OK) {
        return V9X_D3D_HSW_REFUSE_COLOUR_STATE;
    }
    if (state->depth &&
        v9x_intel_hsw_depth_stencil(image + layout->ds / 4ul, 1u,
                                    state->depth_function,
                                    state->depth_write) != V9X_STATUS_OK) {
        return V9X_D3D_HSW_REFUSE_COLOUR_STATE;
    }
    if (!v9x_d3d_hsw_write_kernel(&state->desc, slot, kernel)) {
        return V9X_D3D_HSW_REFUSE_STATE;
    }
    return V9X_D3D_HSW_REFUSE_NONE;
}

/*
 * Whether a description's kernel fits a kernel slot. A two-stage
 * DECALALPHA beside fog or specular does not (test_intel_hsw_pipe.c), and
 * is better declined by accepts - the core then draws stage 0 alone and
 * counts it - than refused by the draw it was accepted for. Built once per
 * signature change into the same cached scratch write_state assembles in;
 * the Win16 lock serialises both.
 */
static int v9x_d3d_hsw_kernel_fits(const struct v9x_hsw_ps_desc *desc)
{
    static v9x_u32 checked[V9X_HSW_PIPE_SIG_DWORDS];
    static int checked_valid;
    static int checked_fits;
    v9x_u32 wanted[V9X_HSW_PIPE_SIG_DWORDS];
    v9x_u32 used = 0ul;
    DWORD index;

    v9x_intel_hsw_pipe_kernel_sig(desc, wanted);
    if (checked_valid &&
        v9x_intel_hsw_pipe_kernel_sig_equal(wanted, checked) == V9X_TRUE) {
        return checked_fits;
    }
    checked_fits = v9x_intel_hsw_ps_kernel_build(v9x_d3d_hsw_kernel_words,
                                                 V9X_D3D_HSW_KERNEL_DWORDS,
                                                 desc, &used) ==
                   V9X_STATUS_OK;
    for (index = 0ul; index < V9X_HSW_PIPE_SIG_DWORDS; ++index) {
        checked[index] = wanted[index];
    }
    checked_valid = 1;
    return checked_fits;
}

static int v9x_d3d_hsw_accepts(const V9X_R3D_DRAW *draw)
{
    V9X_D3D_HSW_STATE state;

    if (draw == 0 || v9x_hal == 0 ||
        v9x_d3d_hsw_map(draw, &state) != V9X_D3D_HSW_REFUSE_NONE) {
        return 0;
    }
    return state.textured != 2 || v9x_d3d_hsw_kernel_fits(&state.desc);
}

/* The batch's description of a mapped Direct3D draw of TL triangles:
 * sequential, unculled; the indexed path fills in its own fields. */
static void v9x_d3d_hsw_draw_desc(const V9X_R3D_DRAW *draw,
                                  const V9X_D3D_HSW_STATE *state,
                                  struct v9x_hsw_draw *d)
{
    d->binding_table = V9X_HSW_SHADE_BINDING_TABLE(0);
    d->width = (v9x_u16)draw->target.width;
    d->height = (v9x_u16)draw->target.height;
    d->attributes = V9X_HSW_SHADE_ATTRIBUTES;
    d->blend = V9X_HSW_SHADE_BLEND_ALPHA_TEST;
    d->cc = V9X_HSW_SHADE_CC;
    d->sampler = V9X_HSW_SHADE_SAMPLER;
    /* With a depth buffer, the batch emits it after Vol 2b's stall, flush
     * and stall, and the whole draw runs under the depth-stencil state;
     * without one the buffer is NULL. */
    d->depth_buffer = state->depth ? state->depth_address : 0ul;
    d->depth_clear = 0ul;
    d->depth_test = state->depth ? V9X_D3D_HSW_DS_OFF : 0ul;
    d->tl_vertices = 1u;
    d->flat = 0u;   /* the core copies the provoking vertex */
    d->depth_format = state->depth ? state->depth_format : (v9x_u8)0u;
    d->depth_pitch = state->depth_pitch;
    d->depth_only = 0u;
    d->scissor = 1u;
    d->scissor_x0 = (v9x_u16)state->scissor[0];
    d->scissor_y0 = (v9x_u16)state->scissor[1];
    d->scissor_x1 = (v9x_u16)state->scissor[2];
    d->scissor_y1 = (v9x_u16)state->scissor[3];
    d->tl_uv1 = state->textured == 2 ? 1u : 0u;
    d->indices = 0ul;
    d->index_count = 0ul;
    d->strip = 0u;
    d->cull = V9X_HSW_DRAW_CULL_NONE;
}

/*
 * The guard band (HSW PRM Vol 7, "Vertex XY Clamping and Quantization"):
 * screen-space vertices are clamped to [-32K, 32K-1], and an object whose
 * bounding box exceeds 8K in x or y is not rendered as intended, so
 * software must clip it. Pixels outside the drawing rectangle, which is
 * the target or the explicit scissor, are not drawn ("Drawing Rectangle
 * Clipping"). So a triangle inside +/- coordinate_limit (8192, well inside
 * the clamp) whose span is under 8K is drawn as it is; one whose span
 * reaches 8K is clipped; one past the band is refused, as the core's
 * clipper refuses it.
 */
#define V9X_D3D_HSW_SPAN_LIMIT 8192.0f
#define V9X_D3D_HSW_GUARD_DRAW 0
#define V9X_D3D_HSW_GUARD_CLIP 1
#define V9X_D3D_HSW_GUARD_DROP 2

/* Finite, from the bits: Open Watcom's x87 compares do not see NaN
 * (docs\decisions\2026-09-25-nan-detection-in-the-z-conversion.md). */
#define V9X_D3D_HSW_EXPONENT_MASK 0x7f800000ul

static int v9x_d3d_hsw_finite(float value)
{
    union {
        float value;
        v9x_u32 bits;
    } convert;

    convert.value = value;
    return (convert.bits & V9X_D3D_HSW_EXPONENT_MASK) !=
           V9X_D3D_HSW_EXPONENT_MASK;
}

static int v9x_d3d_hsw_guard(const V9X_R3D_VERTEX *triangle)
{
    float limit = v9x_d3d_hsw_limits.coordinate_limit;
    float left = triangle[0].sx;
    float right = triangle[0].sx;
    float top = triangle[0].sy;
    float bottom = triangle[0].sy;
    DWORD index;

    for (index = 0ul; index < 3ul; ++index) {
        float x = triangle[index].sx;
        float y = triangle[index].sy;

        if (!v9x_d3d_hsw_finite(x) || !v9x_d3d_hsw_finite(y) ||
            !(x >= -limit && x < limit && y >= -limit && y < limit)) {
            return V9X_D3D_HSW_GUARD_DROP;
        }
        if (x < left) {
            left = x;
        }
        if (x > right) {
            right = x;
        }
        if (y < top) {
            top = y;
        }
        if (y > bottom) {
            bottom = y;
        }
    }
    if (right - left < V9X_D3D_HSW_SPAN_LIMIT &&
        bottom - top < V9X_D3D_HSW_SPAN_LIMIT) {
        return V9X_D3D_HSW_GUARD_DRAW;
    }
    return V9X_D3D_HSW_GUARD_CLIP;
}

/* One 32-byte TL vertex into the vertex area. */
static v9x_u32 *v9x_d3d_hsw_put_vertex(v9x_u32 *target,
                                       const V9X_R3D_VERTEX *vertex)
{
    const v9x_u32 *words = (const v9x_u32 *)vertex;
    DWORD index;

    for (index = 0ul; index < V9X_HSW_TL_VERTEX_BYTES / 4ul; ++index) {
        *target++ = words[index];
    }
    return target;
}

static int v9x_d3d_hsw_draw_batch(const V9X_R3D_DRAW *draw,
                                  const V9X_R3D_VERTEX *vertices,
                                  DWORD triangle_count)
{
    V9X_D3D_HSW_STATE state;
    struct v9x_hsw_pipe_slot slot;
    struct v9x_hsw_batch batch;
    struct v9x_hsw_draw d;
    DWORD reason;
    DWORD kernel = 0ul;
    DWORD done = 0ul;
    DWORD count;
    DWORD limit;
    DWORD index;
    DWORD vertex;
    DWORD timed;
    float width;
    float height;
    float centre;
    int wait_after;
    const v9x_u32 *source;
    const v9x_u32 *uv1;
    v9x_u32 *target;
    V9X_R3D_VERTEX clipped[V9X_R3D_CLIP_MAX_VERTICES];
    V9X_R3D_VERTEX moved[3];

    if (draw == 0 || vertices == 0 || triangle_count == 0ul) {
        return 0;
    }
    if (!v9x_d3d_hsw_ready()) {
        if (v9x_hal != 0) {
            v9x_d3d_hsw_count_refusal(V9X_D3D_HSW_REFUSE_NOT_READY);
        }
        return 0;
    }
    reason = v9x_d3d_hsw_map(draw, &state);
    if (reason != V9X_D3D_HSW_REFUSE_NONE) {
        v9x_d3d_hsw_count_refusal(reason);
        return 0;
    }
    /* A scissor with no pixel of the target in it: every triangle is drawn,
     * as nothing. */
    if (state.scissor_empty) {
        return 1;
    }
    if ((state.textured >= 1 && state.texture[0].levels > 1u) ||
        (state.textured == 2 && state.texture[1].levels > 1u)) {
        ++v9x_hal->d3d_diagnostics.mip_draws;
    }

    /* A blit still queued may be writing this draw's texture or target: wait
     * for it, through the engine's own wait, only when one is. */
    if (!v9x_d3d_hsw_blitter_idle() && !v9x_engine32_hsw.wait_idle(1)) {
        v9x_d3d_hsw_count_refusal(V9X_D3D_HSW_REFUSE_IDLE);
        return 0;
    }
    /*
     * A flip still pending means the panel is still fetching the buffer
     * Flip just released, which is where Direct3D draws next: Flip returns
     * once DSPASURF is written, the plane takes it at the next blank, and
     * Direct3D does not ask GetFlipStatus before drawing. Without this wait
     * 3DMark 2001 SE's Lobby showed half-built frames, without its overlay,
     * every few frames on the i5-4570 (OBS capture, 2026-10-20), with
     * DrawsFlipWaited at 0. Gen3 has waited here since intel78, and the
     * rule is Gen3's: a wait that runs out is a dead scanout, and the batch
     * is drawn anyway.
     */
    {
        int waited = v9x_flip_wait_done();

        if (waited != V9X_FLIP_WAIT_NONE) {
            ++v9x_hal->d3d_diagnostics.draws_flip_waited;
            if (waited == V9X_FLIP_WAIT_TIMEOUT) {
                ++v9x_hal->d3d_diagnostics.draws_flip_wait_timeouts;
            }
        }
    }
    wait_after = v9x_d3d_hsw_draw_waits(draw);

    v9x_d3d_hsw_draw_desc(draw, &state, &d);
    limit = d.tl_uv1 ? V9X_D3D_HSW_MAX_TRIANGLES2 : V9X_D3D_HSW_MAX_TRIANGLES;
    width = (float)draw->target.width;
    height = (float)draw->target.height;
    centre = draw->centre_offset;

    while (done < triangle_count) {
        /* Each piece is one sequence, so it has a slot of its own and
         * writes its state there; the batch before it may still be reading
         * the other slot. */
        if (!v9x_d3d_hsw_wait_slot(&slot)) {
            v9x_d3d_hsw_count_refusal(done == 0ul ?
                                      V9X_D3D_HSW_REFUSE_IDLE :
                                      V9X_D3D_HSW_REFUSE_PIECE);
            return 0;
        }
        /* The per-batch cost split (2026-10-18), in Gen3 buckets V9XTRACE
         * names: the whole state write in RingSpaceWait (the kernel inside
         * it also in Decode), the vertex copy in RingWrite. */
        timed = V9X_TIME_BEGIN();
        reason = v9x_d3d_hsw_write_state(draw, &state, &slot, &kernel);
        if (reason != V9X_D3D_HSW_REFUSE_NONE) {
            v9x_d3d_hsw_count_refusal(reason);
            return 0;
        }
        V9X_TIME_END(V9X_TIME_RING_SPACE_WAIT, timed);
        d.kernel = kernel;
        d.vertices = V9X_D3D_HSW_GFX(slot.vertex_off);

        /*
         * The triangles, as the core hands them over unclipped: each one
         * inside the guard band and under the 8K span goes as it is; one
         * whose span reaches 8K is cut to the target by the core's own
         * clipper and goes as its fan; one with a vertex past the band or
         * not finite is dropped, as the core's clipper refuses it
         * (v9x_d3d_hsw_guard). A piece stops while a whole fan still fits.
         */
        target = v9x_d3d_hsw_reserve(slot.vertex_off);
        count = 0ul;
        timed = V9X_TIME_BEGIN();
        while (done < triangle_count &&
               count + V9X_R3D_MAX_FAN_TRIANGLES <= limit) {
            const V9X_R3D_VERTEX *triangle = vertices + done * 3ul;
            int kind;

            /* The pixel-centre move the core leaves to this engine
             * (centre_in_engine): half a pixel on x and y, before the
             * guard band and any cut, as the core made it. */
            if (centre != 0.0f) {
                for (vertex = 0ul; vertex < 3ul; ++vertex) {
                    moved[vertex] = triangle[vertex];
                    moved[vertex].sx += centre;
                    moved[vertex].sy += centre;
                }
                triangle = moved;
            }
            kind = v9x_d3d_hsw_guard(triangle);

            if (kind == V9X_D3D_HSW_GUARD_DRAW) {
                source = (const v9x_u32 *)triangle;
                if (d.tl_uv1) {
                    /* Each 32-byte vertex, then its tu1 tv1: the TL2
                     * vertex. */
                    uv1 = (const v9x_u32 *)(draw->texcoords1 +
                                            done * 3ul * 2ul);
                    for (vertex = 0ul; vertex < 3ul; ++vertex) {
                        for (index = 0ul;
                             index < V9X_HSW_TL_VERTEX_BYTES / 4ul; ++index) {
                            *target++ = *source++;
                        }
                        *target++ = *uv1++;
                        *target++ = *uv1++;
                    }
                } else {
                    for (index = 0ul;
                         index < 3ul * V9X_HSW_TL_VERTEX_BYTES / 4ul;
                         ++index) {
                        *target++ = source[index];
                    }
                }
                ++count;
            } else if (kind == V9X_D3D_HSW_GUARD_CLIP && !d.tl_uv1) {
                /* A second unit's coordinates cannot follow a cut, and the
                 * render interface clips its two-unit draws before they
                 * come here, so only the one-unit fan is built. */
                int corners = v9x_r3d_clip_triangle(
                    triangle, v9x_d3d_hsw_limits.coordinate_limit, width,
                    height, clipped);
                int fan;

                for (fan = 1; fan + 1 < corners; ++fan) {
                    target = v9x_d3d_hsw_put_vertex(target, &clipped[0]);
                    target = v9x_d3d_hsw_put_vertex(target, &clipped[fan]);
                    target = v9x_d3d_hsw_put_vertex(target,
                                                    &clipped[fan + 1]);
                    ++count;
                }
            }
            ++done;
        }
        V9X_TIME_END(V9X_TIME_RING_WRITE, timed);
        if (count == 0ul) {
            continue;
        }
        d.vertex_count = count * 3ul;
        v9x_intel_hsw_batch_init(&batch, v9x_d3d_hsw_reserve(slot.batch_off),
                                 slot.batch_bytes / 4ul);
        if (v9x_intel_hsw_draw_workload(&batch,
                                        V9X_D3D_HSW_GFX(slot.state_off),
                                        slot.state_span, &d,
                                        V9X_D3D_HSW_CRUMB_GFX,
                                        done) != V9X_STATUS_OK) {
            v9x_d3d_hsw_count_refusal(V9X_D3D_HSW_REFUSE_WORKLOAD);
            return 0;
        }
        if (!v9x_d3d_hsw_submit(&slot, wait_after)) {
            v9x_d3d_hsw_count_refusal(V9X_D3D_HSW_REFUSE_SUBMIT);
            return 0;
        }
    }
    v9x_present_note_submission();
    return 1;
}

/* The whole draw timed, as d3d_mga.c's is; its RCS waits are in the render
 * drain bucket as well. */
static int v9x_d3d_hsw_draw(const V9X_R3D_DRAW *draw,
                            const V9X_R3D_VERTEX *vertices,
                            DWORD triangle_count)
{
    DWORD started = V9X_TIME_BEGIN();
    int result = v9x_d3d_hsw_draw_batch(draw, vertices, triangle_count);

    V9X_TIME_END(V9X_TIME_ENGINE_DRAW, started);
    return result;
}

/*
 * Hardware T&L, step 3 (docs\plans\hardware-tnl.md): the vertex stage on.
 *
 * The constant block must fit between its offset and the VS kernel's
 * (intel_hsw_3d.h), and the kernel area must end inside the slot's block.
 */
typedef char v9x_d3d_hsw_assert_tnl_constants[
    V9X_HSW_SHADE_VS_CONSTANTS + sizeof(V9X_R3D_TNL_CONSTANTS) <=
            V9X_HSW_SHADE_VS_KERNEL ? 1 : -1];
typedef char v9x_d3d_hsw_assert_tnl_end[
    V9X_HSW_SHADE_VS_END <= V9X_HSW_PIPE_STATE_BLOCK_BYTES ? 1 : -1];

/* The VS kernel's words are fixed for step 3, so a slot that holds them is
 * not rewritten: this marks it, and changes whenever the kernel does. */
#define V9X_D3D_HSW_VS_KERNEL_MARK ((v9x_u32)0x56530301ul)

/* Half the 8K span SF allows an object (Vol 7 p578, the rule
 * v9x_d3d_hsw_guard applies on the CPU): the guard band reaches this many
 * pixels either side of the viewport's centre, so nothing the clipper
 * passes untouched spans more. A viewport wider than the span is declined. */
#define V9X_D3D_HSW_GUARD_HALF 4096.0f
/* SF_CLIP_VIEWPORT's guard-band fields stop at +/- 16K (Vol 2d p292). */
#define V9X_D3D_HSW_GUARD_NDC_MAX 16000.0f

static v9x_u8 v9x_d3d_hsw_cull_of(DWORD cull_mode);

static int v9x_d3d_hsw_tnl_available(void)
{
    return v9x_hal != 0 &&
           v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_INTEL_GEN75 &&
           (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_GEN7_IVB) == 0ul;
}

/* What step 3's position-only kernel runs: no lighting, fog or texture
 * matrix, both colours carried in the vertex and passed through. */
static int v9x_d3d_hsw_tnl_key_runs(const V9X_R3D_TNL_KEY *key)
{
    v9x_u32 colours = V9X_R3D_TNL_IN_COLOR0 | V9X_R3D_TNL_IN_COLOR1;

    return key->flags == 0ul && key->light_count == 0ul &&
           key->texture_matrix == 0ul &&
           key->fog_mode == V9X_R3D_TNL_FOG_NONE &&
           (key->inputs & colours) == colours;
}

static v9x_u32 v9x_d3d_hsw_bits(float value)
{
    union {
        float value;
        v9x_u32 bits;
    } convert;

    convert.value = value;
    return convert.bits;
}

/*
 * Float to long through fistp, as d3d_fog.c converts: the HAL links no
 * runtime, and a cast would call Watcom's __CHP helper.
 */
#ifdef __WATCOMC__
static long v9x_d3d_hsw_to_long(float value);
#pragma aux v9x_d3d_hsw_to_long = \
    "sub esp,4" \
    "fistp dword ptr [esp]" \
    "pop eax" \
    parm [8087] value [eax] modify exact [eax];
#else
static long v9x_d3d_hsw_to_long(float value)
{
    return (long)value;
}
#endif

/* The largest whole number not above value, whatever the conversion's
 * rounding: corrected by one when it rounded up. |value| is under 16K. */
static long v9x_d3d_hsw_floor(float value)
{
    long whole = v9x_d3d_hsw_to_long(value);

    return (float)whole > value ? whole - 1l : whole;
}

static float v9x_d3d_hsw_guard_ndc(float half_extent)
{
    float guard = V9X_D3D_HSW_GUARD_HALF / half_extent;

    if (guard < 1.0f) {
        return 1.0f;
    }
    return guard > V9X_D3D_HSW_GUARD_NDC_MAX ? V9X_D3D_HSW_GUARD_NDC_MAX
                                              : guard;
}

/* The VS state in the slot's block: both viewports, the pushed constants,
 * and the kernel unless the slot already holds it. */
static int v9x_d3d_hsw_write_vs_state(const struct v9x_hsw_pipe_slot *slot,
                                      const V9X_R3D_TNL *tnl,
                                      const V9X_R3D_TNL_CONSTANTS *block)
{
    v9x_u32 *image = v9x_d3d_hsw_reserve(slot->state_off);
    const v9x_u32 *constants = (const v9x_u32 *)block;
    float half_width = tnl->viewport_width * 0.5f;
    float half_height = tnl->viewport_height * 0.5f;
    float low = tnl->depth_near;
    float high = tnl->depth_far;
    v9x_u32 used = 0ul;
    DWORD index;

    if (v9x_intel_hsw_sf_clip_viewport(
            image + V9X_HSW_SHADE_VS_SF_CLIP_VIEWPORT / 4ul,
            v9x_d3d_hsw_bits(half_width), v9x_d3d_hsw_bits(-half_height),
            v9x_d3d_hsw_bits(tnl->depth_far - tnl->depth_near),
            v9x_d3d_hsw_bits(tnl->viewport_x + half_width),
            v9x_d3d_hsw_bits(tnl->viewport_y + half_height),
            v9x_d3d_hsw_bits(tnl->depth_near),
            v9x_d3d_hsw_bits(v9x_d3d_hsw_guard_ndc(half_width)),
            v9x_d3d_hsw_bits(v9x_d3d_hsw_guard_ndc(half_height))) !=
        V9X_STATUS_OK) {
        return 0;
    }
    /* CC_VIEWPORT clamps depth to the range, either order (Vol 2d p64). */
    if (low > high) {
        low = tnl->depth_far;
        high = tnl->depth_near;
    }
    image[V9X_HSW_SHADE_VS_CC_VIEWPORT / 4ul] = v9x_d3d_hsw_bits(low);
    image[V9X_HSW_SHADE_VS_CC_VIEWPORT / 4ul + 1ul] = v9x_d3d_hsw_bits(high);

    /* Only what the kernel reads: its constant GRFs, eight dwords each. */
    for (index = 0ul; index < (DWORD)V9X_HSW_VS_CONSTANT_GRFS * 8ul;
         ++index) {
        image[V9X_HSW_SHADE_VS_CONSTANTS / 4ul + index] = constants[index];
    }

    if (image[V9X_HSW_SHADE_VS_KERNEL_MARK / 4ul] ==
        V9X_D3D_HSW_VS_KERNEL_MARK) {
        return 1;
    }
    /* Built in cached memory and copied, as the PS kernels are
     * (v9x_d3d_hsw_kernel_words says why). */
    if (v9x_intel_hsw_vs_kernel_position(v9x_d3d_hsw_kernel_words,
                                         V9X_D3D_HSW_KERNEL_DWORDS,
                                         &used) != V9X_STATUS_OK ||
        used * 4ul + 128ul > V9X_HSW_SHADE_VS_KERNEL_BYTES) {
        return 0;
    }
    for (index = 0ul; index < used; ++index) {
        image[V9X_HSW_SHADE_VS_KERNEL / 4ul + index] =
            v9x_d3d_hsw_kernel_words[index];
    }
    image[V9X_HSW_SHADE_VS_KERNEL_MARK / 4ul] = V9X_D3D_HSW_VS_KERNEL_MARK;
    return 1;
}

/* The index buffer's cache line (Vol 2b's Haswell index-buffer note),
 * defined again below for the TL indexed path. */
#define V9X_D3D_HSW_TNL_INDEX_LINE 64ul

/*
 * Step 3b: where a T&L batch's later segments keep their state.
 *
 * - Push constants: each segment's at V9X_HSW_SHADE_VS_CONSTANTS + 40h n,
 *   in the space the full constant block was given; step 3's kernels
 *   read two GRFs (64 bytes).
 * - Binding tables: 20h each from 17A0h, after the constants, inside the
 *   slot's state block - BINDING_TABLE_POINTERS_PS takes a 16-bit offset
 *   from the surface state base, and the vertex area of slot 1 is past
 *   that from its base.
 * - The rest - two texture surfaces, samplers, blend, CC and depth-stencil
 *   state, 200h a segment - in the last 4 KB of the slot's vertex area,
 *   which indexed T&L draws leave free. Those are 32-bit offsets, and the
 *   batch's state bounds are widened to reach them.
 */
#define V9X_D3D_HSW_SEGMENT_TAIL   0x1000ul
#define V9X_D3D_HSW_SEGMENT_STRIDE 0x200ul
#define V9X_D3D_HSW_SEG_CONSTANTS(n) \
    (V9X_HSW_SHADE_VS_CONSTANTS + 0x40ul * (n))
#define V9X_D3D_HSW_SEG_BT(n)      (0x17a0ul + 0x20ul * (n))

typedef char v9x_d3d_hsw_assert_seg_constants[
    V9X_HSW_VS_CONSTANT_GRFS * 32u <= 0x40u &&
    V9X_D3D_HSW_SEG_CONSTANTS(V9X_HSW_VS_SEGMENTS_MAX + 1u) <=
        V9X_D3D_HSW_SEG_BT(0) ? 1 : -1];
typedef char v9x_d3d_hsw_assert_seg_bt[
    V9X_D3D_HSW_SEG_BT(V9X_HSW_VS_SEGMENTS_MAX + 1u) <=
        V9X_HSW_SHADE_VS_KERNEL ? 1 : -1];
typedef char v9x_d3d_hsw_assert_seg_tail[
    V9X_D3D_HSW_SEGMENT_STRIDE * (V9X_HSW_VS_SEGMENTS_MAX + 1u) <=
        V9X_D3D_HSW_SEGMENT_TAIL ? 1 : -1];

/* The segments' mapped state and batch records; static, as the kernel
 * scratch is, since HAL calls run one at a time under the Win16 lock. */
static V9X_D3D_HSW_STATE v9x_d3d_hsw_seg_state[V9X_HSW_VS_SEGMENTS_MAX + 1u];
static struct v9x_hsw_vs_segment v9x_d3d_hsw_segs[V9X_HSW_VS_SEGMENTS_MAX];

static void v9x_d3d_hsw_segment_layout(const struct v9x_hsw_pipe_slot *slot,
                                       DWORD n, V9X_D3D_HSW_LAYOUT *layout)
{
    DWORD base = slot->vertex_off + slot->vertex_bytes -
                 V9X_D3D_HSW_SEGMENT_TAIL - slot->state_off +
                 n * V9X_D3D_HSW_SEGMENT_STRIDE;

    layout->binding_table = V9X_D3D_HSW_SEG_BT(n);
    layout->tex_surface[0] = base;
    layout->tex_surface[1] = base + 0x20ul;
    layout->sampler = base + 0x40ul;
    layout->blend = base + 0x80ul;
    layout->cc = base + 0xc0ul;
    layout->ds = base + 0x100ul;
}

/* A segment's push constants: the GRFs the kernel reads. */
static void v9x_d3d_hsw_write_constants(const struct v9x_hsw_pipe_slot *slot,
                                        const V9X_R3D_TNL_CONSTANTS *block,
                                        DWORD offset)
{
    v9x_u32 *image = v9x_d3d_hsw_reserve(slot->state_off + offset);
    const v9x_u32 *source = (const v9x_u32 *)block;
    DWORD index;

    for (index = 0ul; index < (DWORD)V9X_HSW_VS_CONSTANT_GRFS * 8ul;
         ++index) {
        image[index] = source[index];
    }
}

/*
 * An indexed T&L draw, one piece: the vertices once, in their order (with
 * unit 1's coordinates after each, as the TL2 vertex has them), then the
 * indices from the next cache line, zero to the end of the line the last
 * one is in. An index past the vertices, or a draw too large for a slot's
 * vertex area, is declined before anything is written to the engine.
 */
static int v9x_d3d_hsw_draw_tnl_indexed(const V9X_R3D_DRAW *draw,
                                        const V9X_R3D_VERTEX *vertices,
                                        const V9X_R3D_TNL *tnl,
                                        const V9X_D3D_HSW_STATE *state,
                                        struct v9x_hsw_draw *d,
                                        struct v9x_hsw_vs_draw *vs,
                                        int wait_after)
{
    struct v9x_hsw_pipe_slot slot;
    struct v9x_hsw_batch batch;
    DWORD reason;
    DWORD kernel = 0ul;
    DWORD stride = d->tl_uv1 ? V9X_HSW_TL2_VERTEX_BYTES
                             : V9X_HSW_TL_VERTEX_BYTES;
    DWORD index_off;
    DWORD index_end;
    DWORD index;
    DWORD word;
    DWORD seg;
    const v9x_u32 *source;
    const v9x_u32 *uv1;
    v9x_u32 *target;
    DWORD timed;

    for (index = 0ul; index < tnl->index_count; ++index) {
        if ((DWORD)tnl->indices[index] >= tnl->vertex_count) {
            return -1;
        }
    }
    index_off = (tnl->vertex_count * stride + V9X_D3D_HSW_TNL_INDEX_LINE -
                 1ul) & ~(V9X_D3D_HSW_TNL_INDEX_LINE - 1ul);
    index_end = (index_off + tnl->index_count * 2ul +
                 V9X_D3D_HSW_TNL_INDEX_LINE - 1ul) &
                ~(V9X_D3D_HSW_TNL_INDEX_LINE - 1ul);
    if (index_end > V9X_HSW_PIPE_VERTEX_BYTES - V9X_D3D_HSW_SEGMENT_TAIL) {
        return -1;
    }
    /* Each later segment's fragment state, mapped as the first's was, and
     * drawable in the same batch: the same depth buffer and the same
     * scissor, since the batch has one of each. */
    for (seg = 1ul; seg < tnl->segment_count; ++seg) {
        V9X_D3D_HSW_STATE *other = &v9x_d3d_hsw_seg_state[seg];

        reason = v9x_d3d_hsw_map(&tnl->segments[seg].draw, other);
        if (reason != V9X_D3D_HSW_REFUSE_NONE) {
            v9x_d3d_hsw_count_refusal(reason);
            return -1;
        }
        if (other->depth != state->depth ||
            (state->depth &&
             (other->depth_address != state->depth_address ||
              other->depth_format != state->depth_format)) ||
            other->scissor_empty != state->scissor_empty ||
            other->scissor[0] != state->scissor[0] ||
            other->scissor[1] != state->scissor[1] ||
            other->scissor[2] != state->scissor[2] ||
            other->scissor[3] != state->scissor[3] ||
            other->textured == 2 || state->textured == 2) {
            return -1;
        }
    }
    if (!v9x_d3d_hsw_wait_slot(&slot)) {
        v9x_d3d_hsw_count_refusal(V9X_D3D_HSW_REFUSE_IDLE);
        return -1;
    }
    /* The state write in RingWait, as the TL path times it. */
    timed = V9X_TIME_BEGIN();
    reason = v9x_d3d_hsw_write_state(draw, state, &slot, &kernel);
    if (reason != V9X_D3D_HSW_REFUSE_NONE) {
        v9x_d3d_hsw_count_refusal(reason);
        return -1;
    }
    if (!v9x_d3d_hsw_write_vs_state(&slot, tnl,
                                    tnl->segment_count != 0ul ?
                                        tnl->segments[0].constants :
                                        tnl->constants)) {
        v9x_d3d_hsw_count_refusal(V9X_D3D_HSW_REFUSE_STATE);
        return -1;
    }
    for (seg = 1ul; seg < tnl->segment_count; ++seg) {
        V9X_D3D_HSW_LAYOUT layout;
        struct v9x_hsw_vs_segment *out = &v9x_d3d_hsw_segs[seg - 1ul];
        DWORD seg_kernel = 0ul;

        v9x_d3d_hsw_segment_layout(&slot, seg, &layout);
        reason = v9x_d3d_hsw_write_state_at(&tnl->segments[seg].draw,
                                            &v9x_d3d_hsw_seg_state[seg],
                                            &slot, &layout, &seg_kernel);
        if (reason != V9X_D3D_HSW_REFUSE_NONE) {
            v9x_d3d_hsw_count_refusal(reason);
            return -1;
        }
        v9x_d3d_hsw_write_constants(&slot, tnl->segments[seg].constants,
                                    V9X_D3D_HSW_SEG_CONSTANTS(seg));
        out->kernel = seg_kernel;
        out->binding_table = layout.binding_table;
        out->sampler = layout.sampler;
        out->blend = layout.blend;
        out->cc = layout.cc;
        out->depth_test = layout.ds;
        out->constants = V9X_D3D_HSW_GFX(slot.state_off +
                                         V9X_D3D_HSW_SEG_CONSTANTS(seg));
        out->first_index = tnl->segments[seg].first_index;
        out->index_count = tnl->segments[seg].index_count;
    }
    V9X_TIME_END(V9X_TIME_RING_SPACE_WAIT, timed);

    /* The copy into the uncached aperture is most of a T&L draw's CPU
     * time; timed as the TL path's vertex copy is (RingWrite). */
    timed = V9X_TIME_BEGIN();
    target = v9x_d3d_hsw_reserve(slot.vertex_off);
    source = (const v9x_u32 *)vertices;
    if (d->tl_uv1) {
        uv1 = (const v9x_u32 *)draw->texcoords1;
        for (index = 0ul; index < tnl->vertex_count; ++index) {
            for (word = 0ul; word < V9X_HSW_TL_VERTEX_BYTES / 4ul; ++word) {
                *target++ = *source++;
            }
            *target++ = *uv1++;
            *target++ = *uv1++;
        }
    } else {
        for (word = 0ul;
             word < tnl->vertex_count * V9X_HSW_TL_VERTEX_BYTES / 4ul;
             ++word) {
            *target++ = source[word];
        }
    }
    /* Two indices a dword, as Haswell reads 16-bit indices little-endian;
     * the last dword and the rest of its line zero. */
    target = v9x_d3d_hsw_reserve(slot.vertex_off + index_off);
    for (index = 0ul; index < (index_end - index_off) / 4ul; ++index) {
        DWORD low = index * 2ul;
        v9x_u32 value = 0ul;

        if (low < tnl->index_count) {
            value = (v9x_u32)tnl->indices[low];
        }
        if (low + 1ul < tnl->index_count) {
            value |= (v9x_u32)tnl->indices[low + 1ul] << 16;
        }
        target[index] = value;
    }
    V9X_TIME_END(V9X_TIME_RING_WRITE, timed);

    d->kernel = kernel;
    d->vertices = V9X_D3D_HSW_GFX(slot.vertex_off);
    d->vertex_count = tnl->vertex_count;
    d->indices = V9X_D3D_HSW_GFX(slot.vertex_off + index_off);
    d->index_count = tnl->segment_count != 0ul ?
                         tnl->segments[0].index_count : tnl->index_count;
    d->strip = 0u;
    vs->constants = V9X_D3D_HSW_GFX(slot.state_off +
                                    V9X_HSW_SHADE_VS_CONSTANTS);
    vs->segments = v9x_d3d_hsw_segs;
    vs->segment_count = tnl->segment_count > 1ul ?
                            (v9x_u8)(tnl->segment_count - 1ul) : (v9x_u8)0u;
    v9x_intel_hsw_batch_init(&batch, v9x_d3d_hsw_reserve(slot.batch_off),
                             slot.batch_bytes / 4ul);
    /* The state span reaches the end of the slot's vertex area, where the
     * later segments' state is (v9x_d3d_hsw_segment_layout). */
    if (v9x_intel_hsw_draw_workload_vs(&batch,
                                       V9X_D3D_HSW_GFX(slot.state_off),
                                       slot.vertex_off + slot.vertex_bytes -
                                           slot.state_off,
                                       d, vs, V9X_D3D_HSW_CRUMB_GFX,
                                       tnl->index_count / 3ul) !=
        V9X_STATUS_OK) {
        v9x_d3d_hsw_count_refusal(V9X_D3D_HSW_REFUSE_WORKLOAD);
        return -1;
    }
    if (!v9x_d3d_hsw_submit(&slot, wait_after)) {
        v9x_d3d_hsw_count_refusal(V9X_D3D_HSW_REFUSE_SUBMIT);
        return 0;
    }
    v9x_present_note_submission();
    return 1;
}

static int v9x_d3d_hsw_draw_tnl_body(const V9X_R3D_DRAW *draw,
                                     const V9X_R3D_VERTEX *vertices,
                                     DWORD triangle_count,
                                     const V9X_R3D_TNL *tnl);

/* The whole T&L draw timed as the TL draw is (EngineDraw), so V9XTRACE's
 * per-batch split covers both paths. */
static int v9x_d3d_hsw_draw_tnl(const V9X_R3D_DRAW *draw,
                                const V9X_R3D_VERTEX *vertices,
                                DWORD triangle_count,
                                const V9X_R3D_TNL *tnl)
{
    DWORD started = V9X_TIME_BEGIN();
    int result = v9x_d3d_hsw_draw_tnl_body(draw, vertices, triangle_count,
                                           tnl);

    V9X_TIME_END(V9X_TIME_ENGINE_DRAW, started);
    return result;
}

static int v9x_d3d_hsw_draw_tnl_body(const V9X_R3D_DRAW *draw,
                                     const V9X_R3D_VERTEX *vertices,
                                     DWORD triangle_count,
                                     const V9X_R3D_TNL *tnl)
{
    V9X_D3D_HSW_STATE state;
    struct v9x_hsw_pipe_slot slot;
    struct v9x_hsw_batch batch;
    struct v9x_hsw_draw d;
    struct v9x_hsw_vs_draw vs;
    DWORD reason;
    DWORD kernel = 0ul;
    DWORD done = 0ul;
    DWORD count;
    DWORD limit;
    DWORD index;
    DWORD vertex;
    long x0;
    long y0;
    long x1;
    long y1;
    int wait_after;
    const v9x_u32 *source;
    const v9x_u32 *uv1;
    v9x_u32 *target;

    if (draw == 0 || vertices == 0 || tnl == 0 || triangle_count == 0ul ||
        !v9x_d3d_hsw_tnl_available() ||
        !v9x_d3d_hsw_tnl_key_runs(tnl->key) ||
        tnl->viewport_width > 2.0f * V9X_D3D_HSW_GUARD_HALF ||
        tnl->viewport_height > 2.0f * V9X_D3D_HSW_GUARD_HALF ||
        tnl->segment_count > V9X_HSW_VS_SEGMENTS_MAX + 1ul ||
        (tnl->segment_count != 0ul &&
         (tnl->indices == 0 || tnl->segments[0].first_index != 0ul))) {
        return -1;
    }
    if (!v9x_d3d_hsw_ready()) {
        v9x_d3d_hsw_count_refusal(V9X_D3D_HSW_REFUSE_NOT_READY);
        return -1;
    }
    /* Segmented (step 3b): the first segment is the batch's own draw, and
     * the request's draw supplied only what every segment shares. */
    if (tnl->segment_count != 0ul) {
        draw = &tnl->segments[0].draw;
    }
    reason = v9x_d3d_hsw_map(draw, &state);
    if (reason != V9X_D3D_HSW_REFUSE_NONE) {
        v9x_d3d_hsw_count_refusal(reason);
        return -1;
    }

    /* The pixels the triangles may reach: the state's scissor and the
     * viewport, both inclusive here. Triangles the guard band passes
     * untouched reach past the viewport, and OpenGL and Direct3D both clip
     * to it. */
    x0 = v9x_d3d_hsw_floor(tnl->viewport_x);
    y0 = v9x_d3d_hsw_floor(tnl->viewport_y);
    x1 = v9x_d3d_hsw_floor(tnl->viewport_x + tnl->viewport_width) - 1l;
    y1 = v9x_d3d_hsw_floor(tnl->viewport_y + tnl->viewport_height) - 1l;
    if (state.scissor_empty || x1 < (long)state.scissor[0] ||
        y1 < (long)state.scissor[1] || x0 > (long)state.scissor[2] ||
        y0 > (long)state.scissor[3]) {
        return 1;
    }
    if (x0 < (long)state.scissor[0]) {
        x0 = (long)state.scissor[0];
    }
    if (y0 < (long)state.scissor[1]) {
        y0 = (long)state.scissor[1];
    }
    if (x1 > (long)state.scissor[2]) {
        x1 = (long)state.scissor[2];
    }
    if (y1 > (long)state.scissor[3]) {
        y1 = (long)state.scissor[3];
    }

    if (!v9x_d3d_hsw_blitter_idle() && !v9x_engine32_hsw.wait_idle(1)) {
        v9x_d3d_hsw_count_refusal(V9X_D3D_HSW_REFUSE_IDLE);
        return -1;
    }
    (void)v9x_flip_wait_done();
    wait_after = v9x_d3d_hsw_draw_waits(draw);

    v9x_d3d_hsw_draw_desc(draw, &state, &d);
    d.scissor = 1u;
    d.scissor_x0 = (v9x_u16)x0;
    d.scissor_y0 = (v9x_u16)y0;
    d.scissor_x1 = (v9x_u16)x1;
    d.scissor_y1 = (v9x_u16)y1;
    d.cull = v9x_d3d_hsw_cull_of(tnl->cull);
    vs.kernel = V9X_HSW_SHADE_VS_KERNEL;
    vs.constant_grfs = V9X_HSW_VS_CONSTANT_GRFS;
    vs.sf_clip_viewport = V9X_HSW_SHADE_VS_SF_CLIP_VIEWPORT;
    vs.cc_viewport = V9X_HSW_SHADE_VS_CC_VIEWPORT;
    vs.provoking = (v9x_u8)tnl->provoking;
    vs.segments = 0;
    vs.segment_count = 0u;
    limit = d.tl_uv1 ? V9X_D3D_HSW_MAX_TRIANGLES2 : V9X_D3D_HSW_MAX_TRIANGLES;
    if (tnl->indices != 0) {
        return v9x_d3d_hsw_draw_tnl_indexed(draw, vertices, tnl, &state, &d,
                                            &vs, wait_after);
    }

    while (done < triangle_count) {
        if (!v9x_d3d_hsw_wait_slot(&slot)) {
            v9x_d3d_hsw_count_refusal(done == 0ul ? V9X_D3D_HSW_REFUSE_IDLE :
                                                    V9X_D3D_HSW_REFUSE_PIECE);
            return done == 0ul ? -1 : 0;
        }
        reason = v9x_d3d_hsw_write_state(draw, &state, &slot, &kernel);
        if (reason != V9X_D3D_HSW_REFUSE_NONE) {
            v9x_d3d_hsw_count_refusal(reason);
            return done == 0ul ? -1 : 0;
        }
        if (!v9x_d3d_hsw_write_vs_state(&slot, tnl, tnl->constants)) {
            v9x_d3d_hsw_count_refusal(V9X_D3D_HSW_REFUSE_STATE);
            return done == 0ul ? -1 : 0;
        }
        d.kernel = kernel;
        d.vertices = V9X_D3D_HSW_GFX(slot.vertex_off);
        vs.constants = V9X_D3D_HSW_GFX(slot.state_off +
                                       V9X_HSW_SHADE_VS_CONSTANTS);

        /* The vertices as they came: object space, nothing to guard or
         * cut on the CPU - the clipper does both. */
        target = v9x_d3d_hsw_reserve(slot.vertex_off);
        count = triangle_count - done;
        if (count > limit) {
            count = limit;
        }
        source = (const v9x_u32 *)(vertices + done * 3ul);
        if (d.tl_uv1) {
            uv1 = (const v9x_u32 *)(draw->texcoords1 + done * 3ul * 2ul);
            for (vertex = 0ul; vertex < count * 3ul; ++vertex) {
                for (index = 0ul; index < V9X_HSW_TL_VERTEX_BYTES / 4ul;
                     ++index) {
                    *target++ = *source++;
                }
                *target++ = *uv1++;
                *target++ = *uv1++;
            }
        } else {
            for (index = 0ul;
                 index < count * 3ul * V9X_HSW_TL_VERTEX_BYTES / 4ul;
                 ++index) {
                *target++ = source[index];
            }
        }
        done += count;
        d.vertex_count = count * 3ul;
        v9x_intel_hsw_batch_init(&batch, v9x_d3d_hsw_reserve(slot.batch_off),
                                 slot.batch_bytes / 4ul);
        if (v9x_intel_hsw_draw_workload_vs(&batch,
                                           V9X_D3D_HSW_GFX(slot.state_off),
                                           slot.state_span, &d, &vs,
                                           V9X_D3D_HSW_CRUMB_GFX,
                                           done) != V9X_STATUS_OK) {
            v9x_d3d_hsw_count_refusal(V9X_D3D_HSW_REFUSE_WORKLOAD);
            return done == count ? -1 : 0;
        }
        if (!v9x_d3d_hsw_submit(&slot, wait_after)) {
            v9x_d3d_hsw_count_refusal(V9X_D3D_HSW_REFUSE_SUBMIT);
            return 0;
        }
    }
    v9x_present_note_submission();
    return 1;
}

/* A declined indexed call, counted by why (win9x_ddraw_abi.h). */
static int v9x_d3d_hsw_decline(DWORD reason, DWORD started)
{
    if (reason < V9X_D3D_INDEXED_DECLINE_REASONS) {
        ++v9x_hal->d3d_diagnostics.indexed_decline[reason];
    }
    V9X_TIME_END(V9X_TIME_ENGINE_DRAW, started);
    return -1;
}

/* 3DSTATE_SF's cull for the core's honoured cull mode (r3d_cull.h). */
static v9x_u8 v9x_d3d_hsw_cull_of(DWORD cull_mode)
{
    if (cull_mode == V9X_R3D_CULL_CW) {
        return V9X_HSW_DRAW_CULL_CW;
    }
    if (cull_mode == V9X_R3D_CULL_CCW) {
        return V9X_HSW_DRAW_CULL_CCW;
    }
    return V9X_HSW_DRAW_CULL_NONE;
}

/* The index buffer's cache line (Vol 2b's Haswell index-buffer note). */
#define V9X_D3D_HSW_INDEX_LINE 64ul

/*
 * A DrawOneIndexedPrimitive call, or a DrawPrimitives2 indexed list, whole
 * (d3d_internal.h, draw_indexed): the vertices it names copied once into
 * the slot's vertex area with the pixel centre the list path applies, the
 * indices rebased after them, one RANDOM 3DPRIMITIVE, and the back face
 * culled by 3DSTATE_SF rather than by the core.
 *
 * Declined (-1, nothing submitted) for anything the list path would treat
 * differently: a second unit's coordinates (which follow list order), flat
 * shading (the list path copies the provoking vertex's colours into the
 * other two, which a shared vertex cannot take), an index past the pool,
 * a vertex of the range that is not finite, and one past half the guard
 * band after the move: inside +/- 4096 no triangle of the call can span
 * 8K, so none needs the clip the list path would give it
 * (v9x_d3d_hsw_guard). A range or index count larger than one slot's
 * vertex area is declined too, and the list path splits it.
 */
#define V9X_D3D_HSW_INDEXED_LIMIT (V9X_D3D_HSW_SPAN_LIMIT / 2.0f)

static int v9x_d3d_hsw_draw_indexed(const V9X_R3D_DRAW *draw,
                                    const V9X_R3D_VERTEX *pool,
                                    DWORD vertex_count,
                                    const v9x_u16 *indices,
                                    DWORD index_count,
                                    int strip,
                                    DWORD cull_mode)
{
    V9X_D3D_HSW_STATE state;
    struct v9x_hsw_pipe_slot slot;
    struct v9x_hsw_batch batch;
    struct v9x_hsw_draw d;
    DWORD started = V9X_TIME_BEGIN();
    DWORD first = 0xfffffffful;
    DWORD last = 0ul;
    DWORD count;
    DWORD vertex_bytes;
    DWORD index_bytes;
    DWORD reason;
    DWORD kernel = 0ul;
    DWORD index;
    DWORD word;
    int wait_after;
    v9x_u32 *target;
    volatile v9x_u16 *index_at;
    union {
        float value;
        v9x_u32 bits;
    } moved;

    if (draw == 0 || pool == 0 || indices == 0 || index_count < 3ul ||
        draw->texcoords1 != 0 ||
        (!strip && index_count % 3ul != 0ul) || !v9x_d3d_hsw_ready()) {
        return v9x_d3d_hsw_decline(V9X_D3D_INDEXED_DECLINE_SHAPE, started);
    }
    if (draw->shade_mode == V9X_R3D_SHADE_FLAT) {
        return v9x_d3d_hsw_decline(V9X_D3D_INDEXED_DECLINE_STATE, started);
    }
    for (index = 0ul; index < index_count; ++index) {
        DWORD vertex = (DWORD)indices[index];

        if (vertex >= vertex_count) {
            return v9x_d3d_hsw_decline(V9X_D3D_INDEXED_DECLINE_INDEX,
                                       started);
        }
        if (vertex < first) {
            first = vertex;
        }
        if (vertex > last) {
            last = vertex;
        }
    }
    count = last - first + 1ul;
    vertex_bytes = (count * V9X_HSW_TL_VERTEX_BYTES + V9X_D3D_HSW_INDEX_LINE -
                    1ul) & ~(V9X_D3D_HSW_INDEX_LINE - 1ul);
    index_bytes = (index_count * 2ul + V9X_D3D_HSW_INDEX_LINE - 1ul) &
                  ~(V9X_D3D_HSW_INDEX_LINE - 1ul);
    if (vertex_bytes + index_bytes > V9X_HSW_PIPE_VERTEX_BYTES) {
        if (count > v9x_hal->d3d_diagnostics.indexed_decline_range_max) {
            v9x_hal->d3d_diagnostics.indexed_decline_range_max = count;
        }
        return v9x_d3d_hsw_decline(V9X_D3D_INDEXED_DECLINE_RANGE, started);
    }
    /* Every vertex of the range finite and inside +/- 4096 after the
     * move. */
    for (index = first; index <= last; ++index) {
        float x = pool[index].sx + draw->centre_offset;
        float y = pool[index].sy + draw->centre_offset;

        if (!v9x_d3d_hsw_finite(x) || !v9x_d3d_hsw_finite(y)) {
            return v9x_d3d_hsw_decline(V9X_D3D_INDEXED_DECLINE_VERTEX,
                                       started);
        }
        if (!(x >= -V9X_D3D_HSW_INDEXED_LIMIT &&
              x < V9X_D3D_HSW_INDEXED_LIMIT &&
              y >= -V9X_D3D_HSW_INDEXED_LIMIT &&
              y < V9X_D3D_HSW_INDEXED_LIMIT)) {
            return v9x_d3d_hsw_decline(V9X_D3D_INDEXED_DECLINE_GUARD,
                                       started);
        }
    }
    reason = v9x_d3d_hsw_map(draw, &state);
    if (reason != V9X_D3D_HSW_REFUSE_NONE || state.textured == 2) {
        return v9x_d3d_hsw_decline(V9X_D3D_INDEXED_DECLINE_STATE, started);
    }
    if (state.scissor_empty) {
        V9X_TIME_END(V9X_TIME_ENGINE_DRAW, started);
        return 1;
    }
    if (state.textured >= 1 && state.texture[0].levels > 1u) {
        ++v9x_hal->d3d_diagnostics.mip_draws;
    }
    /* The waits draw_batch makes, for the same reasons. */
    if (!v9x_d3d_hsw_blitter_idle() && !v9x_engine32_hsw.wait_idle(1)) {
        v9x_d3d_hsw_count_refusal(V9X_D3D_HSW_REFUSE_IDLE);
        V9X_TIME_END(V9X_TIME_ENGINE_DRAW, started);
        return 0;
    }
    {
        int waited = v9x_flip_wait_done();

        if (waited != V9X_FLIP_WAIT_NONE) {
            ++v9x_hal->d3d_diagnostics.draws_flip_waited;
            if (waited == V9X_FLIP_WAIT_TIMEOUT) {
                ++v9x_hal->d3d_diagnostics.draws_flip_wait_timeouts;
            }
        }
    }
    wait_after = v9x_d3d_hsw_draw_waits(draw);
    if (!v9x_d3d_hsw_wait_slot(&slot)) {
        v9x_d3d_hsw_count_refusal(V9X_D3D_HSW_REFUSE_IDLE);
        V9X_TIME_END(V9X_TIME_ENGINE_DRAW, started);
        return 0;
    }
    reason = v9x_d3d_hsw_write_state(draw, &state, &slot, &kernel);
    if (reason != V9X_D3D_HSW_REFUSE_NONE) {
        v9x_d3d_hsw_count_refusal(reason);
        V9X_TIME_END(V9X_TIME_ENGINE_DRAW, started);
        return 0;
    }
    if (count > v9x_hal->d3d_diagnostics.indexed_drawn_range_max) {
        v9x_hal->d3d_diagnostics.indexed_drawn_range_max = count;
    }

    /* The range's vertices, the pixel centre on x and y and the rest as
     * given; then the indices rebased to the range and the rest of their
     * cache line zeroed. TAIL's drain publishes both. */
    target = v9x_d3d_hsw_reserve(slot.vertex_off);
    for (index = first; index <= last; ++index) {
        const v9x_u32 *words = (const v9x_u32 *)&pool[index];

        moved.value = pool[index].sx + draw->centre_offset;
        *target++ = moved.bits;
        moved.value = pool[index].sy + draw->centre_offset;
        *target++ = moved.bits;
        for (word = 2ul; word < V9X_HSW_TL_VERTEX_BYTES / 4ul; ++word) {
            *target++ = words[word];
        }
    }
    index_at = (volatile v9x_u16 *)v9x_d3d_hsw_reserve(slot.vertex_off +
                                                       vertex_bytes);
    for (index = 0ul; index < index_count; ++index) {
        index_at[index] = (v9x_u16)((DWORD)indices[index] - first);
    }
    for (index = index_count; index < index_bytes / 2ul; ++index) {
        index_at[index] = 0u;
    }

    v9x_d3d_hsw_draw_desc(draw, &state, &d);
    d.kernel = kernel;
    d.vertices = V9X_D3D_HSW_GFX(slot.vertex_off);
    d.vertex_count = count;
    d.indices = V9X_D3D_HSW_GFX(slot.vertex_off + vertex_bytes);
    d.index_count = index_count;
    d.strip = strip ? 1u : 0u;
    d.cull = v9x_d3d_hsw_cull_of(cull_mode);
    v9x_intel_hsw_batch_init(&batch, v9x_d3d_hsw_reserve(slot.batch_off),
                             slot.batch_bytes / 4ul);
    if (v9x_intel_hsw_draw_workload(&batch, V9X_D3D_HSW_GFX(slot.state_off),
                                    slot.state_span, &d,
                                    V9X_D3D_HSW_CRUMB_GFX,
                                    index_count) != V9X_STATUS_OK) {
        v9x_d3d_hsw_count_refusal(V9X_D3D_HSW_REFUSE_WORKLOAD);
        V9X_TIME_END(V9X_TIME_ENGINE_DRAW, started);
        return 0;
    }
    if (!v9x_d3d_hsw_submit(&slot, wait_after)) {
        v9x_d3d_hsw_count_refusal(V9X_D3D_HSW_REFUSE_SUBMIT);
        V9X_TIME_END(V9X_TIME_ENGINE_DRAW, started);
        return 0;
    }
    v9x_present_note_submission();
    V9X_TIME_END(V9X_TIME_ENGINE_DRAW, started);
    return 1;
}

/* One TL vertex (V9X_HSW_TL_VERTEX_BYTES): x, y, z, rhw 1.0, then a black
 * diffuse and specular and zero texture coordinates. */
static v9x_u32 *v9x_d3d_hsw_fill_vertex(v9x_u32 *at, float x, float y,
                                        float z)
{
    union {
        float value;
        v9x_u32 bits;
    } convert;

    convert.value = x;
    at[0] = convert.bits;
    convert.value = y;
    at[1] = convert.bits;
    convert.value = z;
    at[2] = convert.bits;
    convert.value = 1.0f;
    at[3] = convert.bits;
    at[4] = 0ul;
    at[5] = 0ul;
    at[6] = 0ul;
    at[7] = 0ul;
    return at + V9X_HSW_TL_VERTEX_BYTES / 4u;
}

/*
 * A rectangle of a Z buffer at value / 65535: the depth-only draw the
 * /depth16 probe measured on the 4570 (Phase F2b, run 3). Two triangles
 * whose edges lie on the rectangle's, so with sampling at pixel centres
 * they cover exactly its pixels, under DEPTH_STENCIL ALWAYS with write; the
 * shader runs, and the blend state masks every colour channel. Its colour
 * target is the Z buffer's own memory, named as B5G6R5 at the same extent,
 * which nothing writes. Synchronous, after the blitter is idle.
 */
int v9x_d3d_hsw_depth_fill(V9X_DD_SURFACE_LCL *surface, const LONG *rect,
                           DWORD value, int wait)
{
    struct v9x_hsw_ps_desc desc;
    struct v9x_hsw_blend blend;
    struct v9x_hsw_pipe_slot slot;
    struct v9x_hsw_batch batch;
    struct v9x_hsw_draw d;
    v9x_u32 *image;
    v9x_u32 *table;
    v9x_u32 *at;
    DWORD offset = 0ul;
    DWORD kernel = 0ul;
    DWORD width;
    DWORD height;
    DWORD pitch;
    float left;
    float top;
    float right;
    float bottom;
    float z;
    /* The surface's own cell (d3d_core.c): D16, or D24_UNORM_X8 for a
     * 32-bit Z buffer. */
    DWORD cell;
    v9x_u8 format = 0u;

    if (surface == 0 || rect == 0 || !v9x_d3d_hsw_ready() ||
        !v9x_d3d_hsw_depth_surface(surface, &offset)) {
        return V9X_BLT_DECLINED;
    }
    cell = v9x_d3d_depth_bytes_per_pixel(surface);
    if (!v9x_d3d_hsw_depth_format(cell, &format)) {
        return V9X_BLT_DECLINED;
    }
    width = (DWORD)surface->lpGbl->wWidth;
    height = (DWORD)surface->lpGbl->wHeight;
    pitch = (DWORD)surface->lpGbl->lPitch;
    if (rect[0] < 0l || rect[1] < 0l || rect[2] <= rect[0] ||
        rect[3] <= rect[1] || (DWORD)rect[2] > width ||
        (DWORD)rect[3] > height) {
        return V9X_BLT_DECLINED;
    }
    /* Both rings idle (the engine's wait covers the render ring since
     * Phase 2), then the slot of this fill's own sequence: a draw after it
     * takes the other one while the fill may still be reading this. */
    if (!v9x_engine32_hsw.wait_idle(wait)) {
        return V9X_BLT_BUSY;
    }
    if (!v9x_d3d_hsw_wait_rcs() || !v9x_d3d_hsw_wait_slot(&slot)) {
        return V9X_BLT_DECLINED;
    }

    image = v9x_d3d_hsw_reserve(slot.state_off);
    if (v9x_intel_hsw_surface_2d(image + V9X_HSW_DRAW_RT_SURFACE(0) / 4ul,
                                 V9X_HSW_FMT_B5G6R5_UNORM,
                                 v9x_d3d_hsw_vram(offset), (v9x_u16)width,
                                 (v9x_u16)height, pitch) != V9X_STATUS_OK) {
        return V9X_BLT_DECLINED;
    }
    table = image + V9X_HSW_SHADE_BINDING_TABLE(0) / 4ul;
    table[0] = V9X_HSW_DRAW_RT_SURFACE(0);
    table[1] = V9X_HSW_DRAW_RT_SURFACE(0);
    table[2] = V9X_HSW_DRAW_RT_SURFACE(0);

    blend.enable = 0u;
    blend.color_function = 0u;
    blend.src_factor = 0u;
    blend.dst_factor = 0u;
    blend.independent_alpha = 0u;
    blend.alpha_function = 0u;
    blend.src_alpha_factor = 0u;
    blend.dst_alpha_factor = 0u;
    blend.write_disable = 0xfu;
    blend.alpha_test = 0u;
    blend.alpha_function_test = 0u;
    desc.stage0 = V9X_HSW_PS_DIFFUSE;
    desc.stage1 = V9X_HSW_PS_STAGE1_NONE;
    desc.fog = 0u;
    desc.fog_rgb[0] = 0ul;
    desc.fog_rgb[1] = 0ul;
    desc.fog_rgb[2] = 0ul;
    desc.specular = 0u;
    /* COMPAREFUNCTION_ALWAYS is 0 (Vol 2a). */
    if (v9x_intel_hsw_blend(image + V9X_HSW_SHADE_BLEND_NO_COLOUR / 4ul,
                            &blend) != V9X_STATUS_OK ||
        v9x_intel_hsw_cc_state(image + V9X_HSW_SHADE_CC / 4ul, 0u) !=
            V9X_STATUS_OK ||
        v9x_intel_hsw_depth_stencil(image + V9X_D3D_HSW_DS_OFF / 4ul, 1u,
                                    0u, 1u) != V9X_STATUS_OK ||
        !v9x_d3d_hsw_write_kernel(&desc, &slot, &kernel)) {
        return V9X_BLT_DECLINED;
    }

    /* D16_UNORM stores round(z x 65535), D24_UNORM_X8 round(z x
     * 16777215). */
    z = v9x_d3d_hsw_fill_z(value, cell);
    left = (float)rect[0];
    top = (float)rect[1];
    right = (float)rect[2];
    bottom = (float)rect[3];
    at = v9x_d3d_hsw_reserve(slot.vertex_off);
    at = v9x_d3d_hsw_fill_vertex(at, left, top, z);
    at = v9x_d3d_hsw_fill_vertex(at, right, top, z);
    at = v9x_d3d_hsw_fill_vertex(at, left, bottom, z);
    at = v9x_d3d_hsw_fill_vertex(at, right, top, z);
    at = v9x_d3d_hsw_fill_vertex(at, right, bottom, z);
    (void)v9x_d3d_hsw_fill_vertex(at, left, bottom, z);

    d.binding_table = V9X_HSW_SHADE_BINDING_TABLE(0);
    d.kernel = kernel;
    d.vertices = V9X_D3D_HSW_GFX(slot.vertex_off);
    d.width = (v9x_u16)width;
    d.height = (v9x_u16)height;
    d.attributes = V9X_HSW_SHADE_ATTRIBUTES;
    d.blend = V9X_HSW_SHADE_BLEND_NO_COLOUR;
    d.cc = V9X_HSW_SHADE_CC;
    d.sampler = V9X_HSW_SHADE_SAMPLER;
    d.depth_buffer = v9x_d3d_hsw_vram(offset);
    d.depth_clear = 0ul;
    d.depth_test = V9X_D3D_HSW_DS_OFF;
    d.tl_vertices = 1u;
    d.vertex_count = V9X_D3D_HSW_FILL_VERTICES;
    d.flat = 0u;
    d.depth_format = format;
    d.depth_pitch = pitch;
    d.depth_only = 1u;
    d.scissor = 0u;
    d.tl_uv1 = 0u;
    d.indices = 0ul;
    d.index_count = 0ul;
    d.strip = 0u;
    d.cull = V9X_HSW_DRAW_CULL_NONE;
    v9x_intel_hsw_batch_init(&batch, v9x_d3d_hsw_reserve(slot.batch_off),
                             slot.batch_bytes / 4ul);
    if (v9x_intel_hsw_draw_workload(&batch, V9X_D3D_HSW_GFX(slot.state_off),
                                    slot.state_span, &d,
                                    V9X_D3D_HSW_CRUMB_GFX, 1ul) !=
            V9X_STATUS_OK ||
        !v9x_d3d_hsw_submit(&slot, 1)) {
        return V9X_BLT_DECLINED;
    }
    return V9X_BLT_DONE;
}

/*
 * A Z buffer in whole Y tiles: pitch width x 2 (x 4 for a 32-bit one)
 * rounded up to 128 bytes, rows rounded up to 32, on a 4 KB boundary, so
 * the last row of tiles the engine reaches is the surface's own.
 */
static DWORD v9x_d3d_hsw_create_depth(V9X_DDHAL_CREATESURFACEDATA *data,
                                      V9X_DD_SURFACE_LCL *surface)
{
    v9x_u32 offsets[1];
    DWORD pitch;
    DWORD rows;
    DWORD base;
    /* A 16-bit Z buffer, or a 32-bit one (D24_UNORM_X8). */
    DWORD bytes = 2ul;

    if ((surface->dwFlags & V9X_DDRAWISURF_HASPIXELFORMAT) != 0ul) {
        if (surface->lpGbl->ddpfSurface.dwRGBBitCount == 32ul) {
            bytes = 4ul;
        } else if (surface->lpGbl->ddpfSurface.dwRGBBitCount != 16ul) {
            return V9X_DDHAL_DRIVER_NOTHANDLED;
        }
    }
    if (surface->lpGbl->wWidth == 0u || surface->lpGbl->wHeight == 0u ||
        (DWORD)surface->lpGbl->wWidth >
            v9x_d3d_hsw_limits.target_dimension_max ||
        (DWORD)surface->lpGbl->wHeight >
            v9x_d3d_hsw_limits.target_dimension_max) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    pitch = ((DWORD)surface->lpGbl->wWidth * bytes + V9X_D3D_HSW_TILE_PITCH -
             1ul) & ~(V9X_D3D_HSW_TILE_PITCH - 1ul);
    rows = ((DWORD)surface->lpGbl->wHeight + V9X_D3D_HSW_TILE_ROWS - 1ul) &
           ~(V9X_D3D_HSW_TILE_ROWS - 1ul);
    offsets[0] = 0ul;
    if (v9x_d3d_place_block(data, V9X_D3D_HSW_TILE_BYTES, pitch, rows,
                            offsets, &base) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    ++v9x_hal->d3d_diagnostics.z_placed;
    v9x_hal->d3d_diagnostics.z_placed_pitch = pitch;
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

/*
 * Mip trees. RENDER_SURFACE_STATE has one base and one pitch for a whole
 * chain, and the sampler finds each level by the layout v9x_intel_hsw_miptree
 * computes. DirectDraw's heap places each level as a surface of its own,
 * which is never that layout, so a chain is placed here instead: one block
 * for the tree (d3d_place.c), on the 4 KB boundary the Z buffers use so
 * one release rule frees both, and each level's fpVidMem at its place in
 * it with the tree's pitch. Gen3's create_surface does the same for its
 * own layout. Declines are counted with Gen3's reasons.
 */
#define V9X_D3D_HSW_MIPTREE_SHAPE  1ul  /* not a halving chain */
#define V9X_D3D_HSW_MIPTREE_FORMAT 2ul  /* not a layout the sampler takes */

static DWORD v9x_d3d_hsw_miptree_decline(DWORD reason)
{
    ++v9x_hal->d3d_diagnostics.mip_tree_declined;
    v9x_hal->d3d_diagnostics.mip_tree_declined_last = reason;
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

static DWORD v9x_d3d_hsw_create_miptree(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list = (V9X_DD_SURFACE_LCL **)data->lplpSList;
    V9X_DD_SURFACE_LCL *top = list[0];
    struct v9x_hsw_miptree tree;
    v9x_u16 format;
    int has_alpha;
    v9x_u32 bytes;
    DWORD width = (DWORD)top->lpGbl->wWidth;
    DWORD height = (DWORD)top->lpGbl->wHeight;
    DWORD index;
    DWORD edge_w;
    DWORD edge_h;
    DWORD base;
    DWORD reason;

    /* The list is DirectDraw's, top first, each edge half the one before
     * down to 1; one in any other order is declined rather than sorted. */
    if (width == 0ul || height == 0ul ||
        width > v9x_d3d_hsw_limits.texture_size_max ||
        height > v9x_d3d_hsw_limits.texture_size_max ||
        data->dwSCnt > V9X_HSW_MIP_LEVELS_MAX) {
        return v9x_d3d_hsw_miptree_decline(V9X_D3D_HSW_MIPTREE_SHAPE);
    }
    for (index = 1ul; index < data->dwSCnt; ++index) {
        const V9X_DD_SURFACE_LCL *level = list[index];

        edge_w = width >> index;
        edge_h = height >> index;
        if (edge_w == 0ul) {
            edge_w = 1ul;
        }
        if (edge_h == 0ul) {
            edge_h = 1ul;
        }
        if (level == 0 || level->lpGbl == 0 ||
            (level->ddsCaps & V9X_DDSCAPS_MIPMAP) == 0ul ||
            (DWORD)level->lpGbl->wWidth != edge_w ||
            (DWORD)level->lpGbl->wHeight != edge_h) {
            return v9x_d3d_hsw_miptree_decline(V9X_D3D_HSW_MIPTREE_SHAPE);
        }
    }
    /* A layout the sampler takes (565, 1555, 4444, ARGB8888, XRGB8888),
     * and its texel size, which the layout is computed in. No format of its
     * own means the display's. */
    if (!v9x_d3d_hsw_texture_format_of(top, &format, &has_alpha, &bytes)) {
        return v9x_d3d_hsw_miptree_decline(V9X_D3D_HSW_MIPTREE_FORMAT);
    }
    if (v9x_intel_hsw_miptree((v9x_u16)width, (v9x_u16)height,
                              (v9x_u8)data->dwSCnt, (v9x_u8)bytes, &tree) !=
            V9X_STATUS_OK) {
        return v9x_d3d_hsw_miptree_decline(V9X_D3D_HSW_MIPTREE_SHAPE);
    }
    reason = v9x_d3d_place_block(data, V9X_D3D_HSW_TILE_BYTES, tree.pitch,
                                 tree.rows, tree.level_offset, &base);
    if (reason != 0ul) {
        return v9x_d3d_hsw_miptree_decline(reason);
    }
    ++v9x_hal->d3d_diagnostics.mip_tree_allocs;
    v9x_hal->d3d_diagnostics.mip_tree_last_offset = base;
    v9x_hal->d3d_diagnostics.mip_tree_last_shape =
        (tree.pitch << 16) | (tree.rows & 0xfffful);
    data->ddRVal = V9X_DD_OK;
    return V9X_DDHAL_DRIVER_HANDLED;
}

/* A lone video-memory Z buffer, or a video-memory mip chain; every other
 * surface is DirectDraw's. */
static DWORD v9x_d3d_hsw_create_surface(V9X_DDHAL_CREATESURFACEDATA *data)
{
    V9X_DD_SURFACE_LCL **list;
    V9X_DD_SURFACE_LCL *surface;

    if (v9x_hal == 0 || data == 0 || data->dwSCnt == 0ul ||
        data->lplpSList == 0) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    list = (V9X_DD_SURFACE_LCL **)data->lplpSList;
    surface = list[0];
    if (surface == 0 || surface->lpGbl == 0 ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul) {
        return V9X_DDHAL_DRIVER_NOTHANDLED;
    }
    if (data->dwSCnt == 1ul &&
        (surface->ddsCaps & V9X_DDSCAPS_ZBUFFER) != 0ul) {
        return v9x_d3d_hsw_create_depth(data, surface);
    }
    if (data->dwSCnt >= 2ul &&
        (surface->ddsCaps & (V9X_DDSCAPS_TEXTURE | V9X_DDSCAPS_MIPMAP)) ==
            (V9X_DDSCAPS_TEXTURE | V9X_DDSCAPS_MIPMAP)) {
        return v9x_d3d_hsw_create_miptree(data);
    }
    return V9X_DDHAL_DRIVER_NOTHANDLED;
}

/* The top of a tree or a Z buffer placed above is going: free the block
 * (d3d_place.c ignores every other surface). */
static void v9x_d3d_hsw_destroy_surface(V9X_DDHAL_DESTROYSURFACEDATA *data)
{
    if (v9x_d3d_place_release(data, V9X_D3D_HSW_TILE_BYTES)) {
        ++v9x_hal->d3d_diagnostics.mip_tree_frees;
    }
}

/*
 * What the mapping takes: Gouraud and the core's flat; texture DECAL, COPY,
 * MODULATE (with Direct3D's alpha rule) and MODULATEALPHA, published, and
 * ADD and SRCALPHASAT, mapped but not published (the DDK headers this tree
 * carries define neither caps bit); specular;
 * vertex fog; every blend factor but the destination alpha ones (a 16-bit
 * target has no useful alpha); the alpha test with every function; WRAP,
 * MIRROR, CLAMP; a 16-bit Z buffer with every comparison (F2b). Mip
 * filters are published; a chain create_surface placed samples its levels,
 * any other texture level 0.
 */
static void v9x_d3d_hsw_describe_caps(V9X_DD_SHARED *shared)
{
    V9X_D3DPRIMCAPS *tri;
    DWORD index;

    if (shared == 0) {
        return;
    }
    shared->d3d_global.dwSize = sizeof(V9X_D3DHAL_GLOBALDRIVERDATA);
    shared->d3d_global.hwCaps.dwSize = sizeof(V9X_D3DDEVICEDESC_V1);
    shared->d3d_global.hwCaps.dwFlags =
        V9X_D3DDD_COLORMODEL | V9X_D3DDD_DEVCAPS |
        V9X_D3DDD_TRICAPS | V9X_D3DDD_DEVICERENDERBITDEPTH |
        V9X_D3DDD_DEVICEZBUFFERBITDEPTH;
    shared->d3d_global.hwCaps.dcmColorModel = V9X_D3DCOLOR_RGB;
    shared->d3d_global.hwCaps.dwDevCaps =
        V9X_D3DDEVCAPS_FLOATTLVERTEX |
        V9X_D3DDEVCAPS_EXECUTESYSTEMMEMORY |
        V9X_D3DDEVCAPS_TLVERTEXSYSTEMMEMORY |
        V9X_D3DDEVCAPS_TEXTUREVIDEOMEMORY |
        V9X_D3DDEVCAPS_DRAWPRIMTLVERTEX;
    shared->d3d_global.hwCaps.dtcTransformCaps.dwSize =
        sizeof(V9X_D3DTRANSFORMCAPS);
    shared->d3d_global.hwCaps.dlcLightingCaps.dwSize =
        sizeof(V9X_D3DLIGHTINGCAPS);
    shared->d3d_global.hwCaps.dpcLineCaps.dwSize = sizeof(V9X_D3DPRIMCAPS);

    tri = &shared->d3d_global.hwCaps.dpcTriCaps;
    tri->dwSize = sizeof(V9X_D3DPRIMCAPS);
    tri->dwMiscCaps = V9X_D3DPMISCCAPS_CULLNONE | V9X_D3DPMISCCAPS_CULLCW |
                      V9X_D3DPMISCCAPS_CULLCCW;
    tri->dwRasterCaps = V9X_D3DPRASTERCAPS_SUBPIXEL |
                        V9X_D3DPRASTERCAPS_FOGVERTEX |
                        V9X_D3DPRASTERCAPS_FOGTABLE |
                        V9X_D3DPRASTERCAPS_ZTEST;
    tri->dwZCmpCaps =
        V9X_D3DPCMPCAPS_NEVER | V9X_D3DPCMPCAPS_LESS |
        V9X_D3DPCMPCAPS_EQUAL | V9X_D3DPCMPCAPS_LESSEQUAL |
        V9X_D3DPCMPCAPS_GREATER | V9X_D3DPCMPCAPS_NOTEQUAL |
        V9X_D3DPCMPCAPS_GREATEREQUAL | V9X_D3DPCMPCAPS_ALWAYS;
    tri->dwAlphaCmpCaps = tri->dwZCmpCaps;
    tri->dwSrcBlendCaps =
        V9X_D3DPBLENDCAPS_ZERO | V9X_D3DPBLENDCAPS_ONE |
        V9X_D3DPBLENDCAPS_SRCCOLOR | V9X_D3DPBLENDCAPS_INVSRCCOLOR |
        V9X_D3DPBLENDCAPS_SRCALPHA | V9X_D3DPBLENDCAPS_INVSRCALPHA |
        V9X_D3DPBLENDCAPS_DESTCOLOR | V9X_D3DPBLENDCAPS_INVDESTCOLOR;
    tri->dwDestBlendCaps = tri->dwSrcBlendCaps;
    tri->dwShadeCaps =
        V9X_D3DPSHADECAPS_COLORFLATRGB | V9X_D3DPSHADECAPS_COLORGOURAUDRGB |
        V9X_D3DPSHADECAPS_SPECULARFLATRGB |
        V9X_D3DPSHADECAPS_SPECULARGOURAUDRGB |
        V9X_D3DPSHADECAPS_ALPHAFLATBLEND |
        V9X_D3DPSHADECAPS_ALPHAGOURAUDBLEND |
        V9X_D3DPSHADECAPS_FOGFLAT | V9X_D3DPSHADECAPS_FOGGOURAUD;
    tri->dwTextureCaps = V9X_D3DPTEXTURECAPS_PERSPECTIVE |
                         V9X_D3DPTEXTURECAPS_ALPHA;
    tri->dwTextureFilterCaps =
        V9X_D3DPTFILTERCAPS_NEAREST | V9X_D3DPTFILTERCAPS_LINEAR |
        V9X_D3DPTFILTERCAPS_MIPNEAREST | V9X_D3DPTFILTERCAPS_MIPLINEAR |
        V9X_D3DPTFILTERCAPS_LINEARMIPNEAREST |
        V9X_D3DPTFILTERCAPS_LINEARMIPLINEAR;
    tri->dwTextureBlendCaps =
        V9X_D3DPTBLENDCAPS_DECAL | V9X_D3DPTBLENDCAPS_MODULATE |
        V9X_D3DPTBLENDCAPS_MODULATEALPHA | V9X_D3DPTBLENDCAPS_COPY;
    tri->dwTextureAddressCaps = V9X_D3DPTADDRESSCAPS_WRAP |
                                V9X_D3DPTADDRESSCAPS_MIRROR |
                                V9X_D3DPTADDRESSCAPS_CLAMP;

    shared->d3d_extended_caps.dwSize = sizeof(V9X_D3DHAL_D3DEXTENDEDCAPS);
    shared->d3d_extended_caps.dwMinTextureWidth =
        v9x_d3d_hsw_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureWidth =
        v9x_d3d_hsw_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinTextureHeight =
        v9x_d3d_hsw_limits.texture_size_min;
    shared->d3d_extended_caps.dwMaxTextureHeight =
        v9x_d3d_hsw_limits.texture_size_max;
    shared->d3d_extended_caps.dwMinStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMaxStippleWidth = 0ul;
    shared->d3d_extended_caps.dwMinStippleHeight = 0ul;
    shared->d3d_extended_caps.dwMaxStippleHeight = 0ul;
    /* 16 and 32 bpp targets (v9x_d3d_hsw_target); depth D16 or a 32-bit
     * D24_UNORM_X8 at both. */
    shared->d3d_global.hwCaps.dwDeviceRenderBitDepth =
        V9X_DDBD_16 | V9X_DDBD_32;
    shared->d3d_global.hwCaps.dwDeviceZBufferBitDepth =
        V9X_DDBD_16 | V9X_DDBD_32;

    /* RGB565, ARGB1555 and ARGB4444, then ARGB8888 and XRGB8888
     * (v9x_d3d_hsw_texel_format): the shared block's five slots. */
    for (index = 0ul; index < 5ul; ++index) {
        shared->texture_formats[index].dwSize = sizeof(V9X_DDSURFACEDESC);
        shared->texture_formats[index].dwFlags =
            V9X_DDSD_CAPS | V9X_DDSD_PIXELFORMAT;
        shared->texture_formats[index].ddpfPixelFormat.dwSize =
            sizeof(V9X_DDPIXELFORMAT);
        shared->texture_formats[index].ddpfPixelFormat.dwRGBBitCount = 16ul;
        shared->texture_formats[index].ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;
    }
    shared->texture_formats[0].ddpfPixelFormat.dwFlags = V9X_DDPF_RGB;
    shared->texture_formats[0].ddpfPixelFormat.dwRBitMask = 0x0000f800ul;
    shared->texture_formats[0].ddpfPixelFormat.dwGBitMask = 0x000007e0ul;
    shared->texture_formats[0].ddpfPixelFormat.dwBBitMask = 0x0000001ful;
    shared->texture_formats[0].ddpfPixelFormat.dwRGBAlphaBitMask = 0ul;
    shared->texture_formats[1].ddpfPixelFormat.dwFlags =
        V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
    shared->texture_formats[1].ddpfPixelFormat.dwRBitMask = 0x00007c00ul;
    shared->texture_formats[1].ddpfPixelFormat.dwGBitMask = 0x000003e0ul;
    shared->texture_formats[1].ddpfPixelFormat.dwBBitMask = 0x0000001ful;
    shared->texture_formats[1].ddpfPixelFormat.dwRGBAlphaBitMask =
        0x00008000ul;
    shared->texture_formats[2].ddpfPixelFormat.dwFlags =
        V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
    shared->texture_formats[2].ddpfPixelFormat.dwRBitMask = 0x00000f00ul;
    shared->texture_formats[2].ddpfPixelFormat.dwGBitMask = 0x000000f0ul;
    shared->texture_formats[2].ddpfPixelFormat.dwBBitMask = 0x0000000ful;
    shared->texture_formats[2].ddpfPixelFormat.dwRGBAlphaBitMask =
        0x0000f000ul;
    shared->texture_formats[3].ddpfPixelFormat.dwFlags =
        V9X_DDPF_RGB | V9X_DDPF_ALPHAPIXELS;
    shared->texture_formats[3].ddpfPixelFormat.dwRGBBitCount = 32ul;
    shared->texture_formats[3].ddpfPixelFormat.dwRBitMask = 0x00ff0000ul;
    shared->texture_formats[3].ddpfPixelFormat.dwGBitMask = 0x0000ff00ul;
    shared->texture_formats[3].ddpfPixelFormat.dwBBitMask = 0x000000fful;
    shared->texture_formats[3].ddpfPixelFormat.dwRGBAlphaBitMask =
        0xff000000ul;
    shared->texture_formats[4].ddpfPixelFormat.dwFlags = V9X_DDPF_RGB;
    shared->texture_formats[4].ddpfPixelFormat.dwRGBBitCount = 32ul;
    shared->texture_formats[4].ddpfPixelFormat.dwRBitMask = 0x00ff0000ul;
    shared->texture_formats[4].ddpfPixelFormat.dwGBitMask = 0x0000ff00ul;
    shared->texture_formats[4].ddpfPixelFormat.dwBBitMask = 0x000000fful;
    shared->texture_formats[4].ddpfPixelFormat.dwRGBAlphaBitMask = 0ul;
    shared->d3d_global.lpTextureFormats = &shared->texture_formats[0];
    shared->d3d_global.dwNumTextureFormats = 5ul;
    shared->d3d_global.dwNumVertices = 0ul;
    shared->d3d_global.dwNumClipVertices = 0ul;
}

/* Positional: V9X_D3D_ENGINE_OPS is append-only (d3d_internal.h). */
const V9X_D3D_ENGINE_OPS v9x_d3d_engine_hsw = {
    &v9x_d3d_hsw_limits,
    v9x_d3d_hsw_texture_format,
    v9x_d3d_hsw_describe_caps,
    0,                                  /* draw_triangles: draw serves */
    v9x_d3d_hsw_ready,
    v9x_d3d_hsw_create_surface,         /* Z buffers in Y tiles */
    v9x_d3d_hsw_destroy_surface,
    v9x_d3d_hsw_draw,
    v9x_d3d_hsw_accepts,
    v9x_d3d_hsw_draw_indexed,
    v9x_d3d_hsw_draw_tnl,
    v9x_d3d_hsw_tnl_available
};
