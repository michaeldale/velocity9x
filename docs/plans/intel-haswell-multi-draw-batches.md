# HD 4600: several draws to a render batch

Date: 2026-11-18. Status: planned; Phase 0 next. Follows
[the batch-size and indexed-pieces record](../decisions/2026-11-18-haswell-batch-size-and-indexed-pieces.md)
and [intel-haswell-batch-cost.md](intel-haswell-batch-cost.md), which this
continues as "lever 1".

## Why

On the i5-4570 at boot 275, 3DMark 2001 SE (3888) sent 3.8 M render
batches, and the CPU waited 93 s of the run for the GPU to free a slot.
Every batch is a whole pipeline (`v9x_hsw_workload`, `hsw_3d.c`):
PIPELINE_SELECT, STATE_BASE_ADDRESS, the cache invalidates, the
push-constant and URB allocations, every stage, the depth buffer with Vol
2b's stalls, then CC pointers, an RT flush with CS stall, a scoreboard
stall and the breadcrumb. So the GPU starts and drains its pipeline for
every draw.

What is measured about that cost:

- DB3D's one-triangle batches cost the GPU about 10 us each
  ([flush variants](../decisions/2026-10-28-haswell-flush-variants.md)).
- All seven PIPE_CONTROL variants together took 2 us of it. Leaving out
  the allocations, the texture and instruction cache invalidates took
  nothing measurable.
- What the other 8 us is has not been split: pipeline setup, the closing
  drain, or the front end's latency.

i915 and crocus put every draw between two flushes in one batch, emit state
once per batch and then only what changed, and flush the render cache only
when a surface's use changes. That is the design here.

Upper bound, not a forecast: closing a batch once per DrawPrimitives2
call would cut 3.8 M batches to about 0.55 M (the run's DP2 calls), and at
8-10 us a batch saves the GPU 26-32 s of a run in which the CPU waited
93 s for it. How much of that reaches the score depends on how much of
the run is GPU-bound rather than held at the refresh.

## Phase 0: measure the payoff (probe only)

A V9XHSWMM switch, `/multidraw`, after Phase D's `/draw`: N draws of K
triangles each submitted as N batches, then as one batch with N
primitives (the state set up once; between primitives only a vertex
buffer address), each timed by the status page's sequence over many
repetitions, for N in 1, 4, 16 and K in 1, 16, 100. Read the frame buffer
after each to check both forms drew the same pixels.

The result goes in a `docs/decisions/` record with the hypotheses it
kills. If one batch of N primitives does not cost much less than N
batches, the plan stops here.

Risk: the probe owns the render engine for the run (the driver's engine
closed, as Phase D's was). A wedged RCS makes `reset` hard-lock the 4570,
so every run with a new batch shape needs Michael within reach for a
power cycle.

## Phase 1: room for a batch of draws

The reserve's per-slot areas hold one draw: an 8 KB state block (a draw
uses 1540h), a 2 KB batch and 28 KB of vertices (`intel_hsw_pipe.h`).
Each slot needs, for up to D draws (D chosen from Phase 0):

- D state sub-blocks, each a draw's surfaces, sampler, blend, CC and
  depth-stencil state, named by pointers relative to one
  STATE_BASE_ADDRESS over the whole state area, so the base is set once
  per batch;
- a batch area for the setup, D draws' deltas and the end;
- a vertex area shared by the batch's draws.

The reserve size is a contract: `V9X_INTEL_HSW_RESERVE_BYTES`, the
mini-VDD's `V9X_HSW_RESERVE_BYTES`/`_PAGES` (`loader.asm`), V9XHSWMM's
`HSW_RCS_RING_BELOW`, all held by `check-tree.ps1`. And the reserve sits at
the top of stolen memory, so the heap's end moves down by the growth
(`v9x_intel_hsw_plan_heap`). Ivy Bridge shares all of it.

Phase 1 alone changes no batch: each still carries one draw. Larger
vertex areas also let `batch_triangles` and the indexed pieces grow, and
that is measured on its own.

## Phase 2: the builder in three parts

`v9x_hsw_workload` split into:

- **begin:** PIPELINE_SELECT, STATE_BASE_ADDRESS, the leading stall and
  invalidates, the allocations, the disabled stages;
- **draw:** what differs from the batch's previous draw:
  - SF (cull, depth format), SBE, WM and PS when the attributes or kernel
    change;
  - the depth buffer, with Vol 2b's depth stall, flush and stall, when it
    changes;
  - the drawing rectangle, the state pointers, the vertex buffer and
    elements, and the primitive;
  - a render-target change takes an RT flush with CS stall first;
- **end:** CC pointers, RT flush with CS stall, scoreboard stall,
  breadcrumb, as today.

Host tests:

- begin, one draw, end reproduces today's batch word for word;
- a second draw with nothing changed adds only pointers, vertex state and
  a primitive;
- every change that needs a flush carries one.

## Phase 3: the HAL keeps a batch open

A draw appends to the open batch of the current slot. The batch is closed
(end, submit) before anything that needs its results or the engine's
quiet:

- a full slot (state, batch or vertex area);
- every drain and wait: Lock, Blt and the blitter's fills, Flip, the
  flip and blit status calls, `wait_idle`, a kernel slot about to be
  rewritten;
- the GDI rule (`v9x_intel_hsw_pipe_draw_waits`);
- a draw that samples a surface the open batch draws into: close first,
  as GCN's `v9x_vi_cp_before_read` does.

First version: the batch also closes at the end of every engine entry,
so nothing is open between two driver calls. The core has no such hook.
An `end` op appended to `V9X_D3D_ENGINE_OPS`, called where a
DrawPrimitives2 or DX5 draw call returns, is the smallest one. Keeping a
batch open across calls is a later step, with the drain points above as
its only closes.

The list path's pieces, the indexed pieces and the T&L and PS 1.1 draws
all become draws in the open batch; the depth fill too. The render
interface (GL, Glide, 3DCIF) closes any open batch before it submits.

## Phase 4: measure

On the 4570: the release probes, V9XD32 at every DDI, then 3DMark 2001
SE with grabs against 3888 (boot 275), and the counters: batches, draws
per batch, `TimeRenderDrain`. Quake 3 `demo001`, whose batches are about
20 triangles, against the 2026-10-23 figures.

## Not in scope

- Flip queued behind fences, and colour clears drawn on the render engine
  instead of the blitter after a drain: separate, smaller changes.
- More than two slots: the slot wait is the GPU's time, not the CPU's
  (boot 274: half the batches, the same 72 s).
