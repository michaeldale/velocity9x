# HD 4600: the render engine's cost per 3D batch

Date: 2026-10-23. Status: lever 2 (write-combining) done 2026-10-28;
lever 1 measured, not started (section 2026-10-28). Michael judged Tux Racer and Quake 3
good enough on the HD 4600 after the 2026-10-23 changes; this is written
down for later, not started. Nothing below is measured beyond what the
linked record says.

## Where it stands

[2026-10-23-haswell-gl-draws-run-on.md](../decisions/2026-10-23-haswell-gl-draws-run-on.md)
left draws outside the GDI primary running. Quake 3's `demo001` at
1024x768x32 on the i5-4570 went from 35.7 to 42.6 fps. V9XTRACE then
split a draw (3.2 GHz TSC, 860,492 draws):

| µs a draw | what | counter |
|---|---|---|
| 178.8 | the whole engine draw | `TimeEngineDraw` |
| 99.3 | waiting for a free slot: the batch before last still running | `TimeRenderDrain` |
| 54.4 | vertex copy into the uncached reserve | `TimeRingWrite` |
| 5.9 | state write and kernel | `TimeRingWait`, `TimeDecode` |

The CPU no longer waits for its own draw; it waits for the render engine,
about 100 µs a batch. Quake 3 sends about 100 batches a frame of about 20
triangles each, so batch cost, not triangle count, sets its rate. The
pipelining plan listed this cost as out of scope
([intel-haswell-pipelined-submission.md](intel-haswell-pipelined-submission.md),
"Not in scope").

## 2026-10-28: after write-combining

Lever 2 is done: the aperture and the reserve are write-combined through
PAT ([record](../decisions/2026-10-28-haswell-pat-write-combining.md)).
3DMark 2000 went from 2344 to 3719 and its batch from 162 to 58 us, of
which the vertex copy is now 3.5 us and the wait for a free slot 31 us.
The CPU now waits on the render engine for over half of every batch, so
lever 1 is the limit.

What the WC boot's DB3D rows (with the HAL's buckets) say about it:

| Row | us a batch | RenderDrain | What it isolates |
|---|---|---|---|
| BatchCost.Tiny (64 one-pixel triangles a batch) | 7.5 | 0.0 | the GPU finishes a small batch inside the CPU's 7.5 us |
| StateChange.*.Change.1 (one triangle a batch) | 11.9 | 9.1 | a batch's fixed GPU cost, about 10 us |
| TriBatch.100.V32 | 18.2 | 8.3 | |

So the fixed cost of a batch on the GPU is about 10 us, and a 3DMark
batch takes about 58: up to a sixth of 3DMark's GPU time is per-batch
overhead, the rest its pixels and vertices. That bounds what lever 1 can
give 3DMark 2000; Quake 3, at about 20 triangles a batch, has more to
gain. Still not measured: which packet or flush the 10 us is.

The batch as `v9x_hsw_workload` (`hsw_3d.c`) builds it carries seven
PIPE_CONTROLs, six with CS stall: a scoreboard stall and a stall with
state, constant, VF, texture and instruction cache invalidation at the
start; a depth stall, a depth cache flush and a depth stall around a depth
buffer; two render-target flushes with CS stall around a CC pointer at the
end; a scoreboard stall; the breadcrumb write. Every batch also re-emits
the whole pipeline: STATE_BASE_ADDRESS, the push-constant and URB
allocations, every stage, depth, drawing rectangle and pointers.

### Next step, measure-first

1. A HAL variant mask in the shared block, armed only by a V9XTRACE switch
   as `-grab` is, which leaves out one candidate at a time: the leading
   instruction-cache invalidate; the texture-cache invalidate when no
   texture was written since the last batch; the depth stall/flush/stall
   when the depth buffer did not change; the second RT flush; the
   push-constant and URB re-allocation when unchanged. Each variant run
   is DB3D's StateChange, BatchCost and TriBatch rows with the HAL's
   buckets, then V9XD32 for correctness, on the 4570.
2. Only the variants that are both cheaper and pass go into the shipped
   path, each citing the Haswell PRM, i915 or crocus for why it is safe.
   Variants that drop a CS stall the breadcrumb or a slot's reuse rests
   on are not tried.

A wedged render engine makes `reset` hard-lock the 4570: every variant
boot needs Michael within reach for a power cycle.

## The two levers

**1. The GPU's time per batch.** Each batch
(`v9x_intel_hsw_draw_workload`, `src/chipsets/intel/hsw_3d.c`) re-emits
the whole pipeline state and carries several PIPE_CONTROLs with CS stall
and render-target cache flush, plus depth stalls around depth state. The
presumed cost is the stalls and the full re-emission; it has not been
measured. i915/crocus emit state once per context and only what changed
per draw, and flush the render cache only when a surface's use changes.

**2. The 54 µs vertex copy.** The aperture is uncached by MTRR, and the
copy writes the reserve a dword at a time. Write-combining has the same
open questions as Gen3's Stage B
([netbook-performance-parked.md](netbook-performance-parked.md)); PAT is
untried.

## How to start

1. **Measure first, no shipped change.** A probe that submits the same
   draw in variants (full workload; state once then draws only; without
   each PIPE_CONTROL in turn) and times each by the status-page sequence.
   The result, with the hypotheses it kills, goes in a
   `docs/decisions/` record.
2. **Then a plan section per change**, each flush or state packet
   dropped or hoisted citing the Haswell PRM, i915 or crocus for why it
   is safe, before the shipped path changes. Kernel slots, the two
   reserve slots and the GDI rule constrain what may be left out
   between batches from different processes (the sequence is shared).
3. Gates: `build-host`, `run-checks`; on the 4570 V9XDDP, 3DMark 2000,
   Half-Life `mwd5`, and Quake 3 `demo001` frame times (`com_speeds 1`,
   `logfile 2`) against the record above. Ivy Bridge shares `d3d_hsw.c`
   and needs the same gates on W9QI4E14.

A wedged render engine makes `reset` hard-lock the 4570: after an RCS
hang, ask Michael for a power cycle.
