# Intel Gen3: submit without waiting for the ring head

Date: 2026-09-29. Status: phases 0-3 implemented and all local gates pass;
phase 4 failed on physical hardware. The synchronous control was correct, but
the first sustained asynchronous 3DMark 99 run produced severe stale-band,
displaced-geometry and blank-rectangle corruption. It was aborted and boot 52
was restored to `IntelAsyncSubmit=0`. See
`../decisions/2026-09-29-intel-gen3-async-submission-physical.md` and
`../issues/2026-09-29-intel-gen3-async-corrupts-3dmark99.md`. Phase 5 is not
reached; asynchronous submission is rejected for normal use.

## Goal and measured ceiling

Let the CPU build later Direct3D batches while the 945GSE executes earlier
ones. Keep the existing in-order ring, allowlist, trailing flush and
breadcrumb completion contract. This changes submission latency, not GPU fill
rate.

Half-Life at 640x480x16 on boot 31
(`docs\decisions\2026-09-25-netbook-halflife-alpha-test-V9XTRACE.ini`) spent
51.8 of 79 seconds in Direct3D calls. Of the whole run, 33.6 seconds (43%) was
polling `RING_HEAD` for 933,256 batches, about 14.5 seconds (18%) was building
and writing them, and 0.7 seconds (1%) was the breadcrumb wait after the head.
Perfect overlap would replace the sum of CPU and GPU time with the larger of
the two, so this run's upper bound is roughly one third less elapsed time. It
is not a frame-rate promise: 3DMark 99 at 640x480 spent only 2.7% waiting on
the head, while its old 1024x576 run spent 43% before later stride fixes.

## Current contract

`v9x_d3d_i9xx_ring_submit` in `src/display32/d3d/d3d_i9xx.c` currently:

1. reads `RING_HEAD` and `RING_TAIL` and asks `v9x_i9xx_ring_plan` for room;
2. writes the stream and advances `RING_TAIL`;
3. waits for the head to reach that tail;
4. when the stream carries a breadcrumb, waits for that value to land; and
5. leaves a timed-out breadcrumb outstanding for `Flip`, `Lock`, `Blt` or
   `DestroySurface` to drain.

The last breadcrumb is sufficient because the engine executes the ring in
order. Reaching the tail is not completion: only the store after the trailing
`MI_FLUSH` says the pixels and reads ahead of it are finished.

The audited synchronization points already present are:

| Boundary | Current action | Async requirement |
|---|---|---|
| Flip/present | `v9x_flip_body` drains rendering | keep |
| Surface Lock | `V9xHalLock` drains engine and rendering; honours `DONOTWAIT` | keep |
| CPU blit fallback | `v9x_blt_drain` drains before CPU access | keep |
| CPU render-interface clear | `v9x_r3d_drain` before writing target/depth | keep |
| Placed surface destroy | drains to DONE before `v9x_d3d_destroy_surface`; leaks on ABANDONED | keep |
| Engine blit after engine work | same ring, therefore ordered | stop rejecting it merely because a breadcrumb is outstanding |
| Context destroy | removes CPU-side handles only; it does not release surface storage | no new drain |
| DriverInit/mode change | currently resets completion state immediately | drain the old session before reset, or abandon without reusing its memory |

Texture uploads visible to this backend go through `Lock`, and `Unlock` only
marks colour-key metadata dirty. The Gen3 path does not perform the ViRGE
draw-time colour-key texel rewrite. This audit must be repeated if either fact
changes.

## Required invariants

1. **One newest completion is owed.** After every asynchronous draw or engine
   blit carrying sequence *N*, `breadcrumb_outstanding` is *N*. A later submit
   may replace it with *N+1*: observing *N+1* proves every earlier command
   completed. It is cleared only after observing that exact newest value or
   entering the existing abandoned state.
2. **Completion values are global to the hardware ring.** Static DLL state is
   not enough if two processes have separate data instances. Before coding,
   establish where the driver has one cross-process sequence allocator under
   the Win16 serialization already measured by the callback instrumentation.
   The preferred design reserves a CPU-owned DWORD in the mapped status page,
   initializes it while the ring is idle, and allocates non-zero sequences
   there. Do not ship asynchronous submission with a per-process counter.
3. **No overwrite of unread commands.** The producer writes only after a fresh
   head read makes `v9x_i9xx_ring_plan` succeed. The existing eight-byte guard,
   qword-aligned tail and wrap NOOPs remain unchanged.
4. **Publication stays ordered.** Ring memory, including wrap padding and the
   complete command stream, is written before the single tail MMIO write.
5. **CPU access and storage reuse drain first.** A CPU read/write of video
   memory, a display of a rendered buffer, or release/reuse of storage named
   by queued work must observe the newest breadcrumb first.
6. **No breadcrumb means no async.** If status-page bring-up or its round trip
   fails, use the existing synchronous head-wait path for that session. Head
   equality must never be promoted to render completion.
7. **Every wait is bounded.** Ring-space waits use a per-submit bound. Drains
   retain the accumulated abandon bound. Neither path adds a reset.
8. **Failure after tail publication is not a failed draw.** Once `TAIL` moves,
   the batch is owned by the GPU. A later timeout records an outstanding or
   abandoned channel; it cannot ask the core to replay the same draw.

## Design

### Submission modes

Add a retained `IntelAsyncSubmit` boot switch beside `IntelRuntime3D` and
`IntelFlip`. During development, only exact `1` selects the new path; `0` or
an absent/malformed key selects the old synchronous path. This lets one
package perform an A/B run and keeps a DOS recovery from requiring replacement
files. Publish the selected mode in the diagnostic snapshot. A later decision
may make absence mean on, but only after phase 5 accepts the feature.

Do not overload `IntelEnableThisBoot`; it belongs to the one-shot Phase 4/5
arm transaction and is normally clear during runtime 3D.

### Asynchronous submit

Split the current function into policy and mechanics:

- validate arguments and the maximum possible batch size before polling;
- read the current tail once for this producer attempt;
- read the head, call `v9x_i9xx_ring_plan`, and, on transient insufficient
  room, poll a fresh head and re-plan until room exists or the bound expires;
- write padding and commands, then advance the tail once;
- if asynchronous mode is active and this stream has a breadcrumb, publish
  that sequence as the newest outstanding completion and return success;
- otherwise run the existing head/breadcrumb wait unchanged.

Invalid geometry, odd dword counts and a batch too large for an empty ring are
permanent failures and must not enter the space-wait loop. A full ring is
transient, not a draw refusal. On space-wait timeout, no tail has moved, so the
submit may safely return failure.

`v9x_d3d_i9xx_hws_open` is special: its store-only self-test must stay
synchronous because it is what proves breadcrumbs may be trusted. The helper
must request synchronous submission explicitly instead of depending on a
temporary global flag.

Raw ring-flip streams carry no breadcrumb. They retain their scanout pending
bit contract. The flip path already drains rendering before enqueuing them,
and the draw path already gates new drawing while a flip is pending; neither
contract is replaced by render breadcrumbs.

### Blitter semantics

`eng_i9xx.c` currently makes `can_blt` require no outstanding render work.
That would turn the Half-Life present following an asynchronous draw into a
CPU fallback and give back most of the intended win. Change it to mean "the
Gen3 ring and completion channel can accept work." A GPU blit queued behind a
draw is ordered and may return `V9X_BLT_DONE` once accepted; later Flip, Lock,
CPU fallback or DestroySurface still drains the newest blit breadcrumb.

If the ring has no room, the bounded space wait happens in submit. If it times
out before moving the tail, return `V9X_BLT_DECLINED`; the core may then drain
and perform the CPU fallback without duplicating an accepted blit.

### Drains and teardown

Keep `v9x_d3d_i9xx_render_drain(wait)` as the sole completion query. Extend
its accounting with a drain-site identifier supplied by the caller, or thin
wrappers for Flip, Lock, CPU blit, surface destroy, render clear and session
teardown. This is diagnostic attribution only; all wrappers reach the same
newest-sequence state.

Before `DriverInit` calls `v9x_d3d_i9xx_reset`, drain the previous Gen3
session while its old MMIO mapping and status page are still valid. If
completion is abandoned, do not claim that the old work finished; record the
site and continue only through the existing new-session recovery contract.
The implementation review must establish that the old `v9x_hal` mapping is
available at this point. If it is not, move the drain to the last callback
that still owns it rather than dereferencing the new shared block.

Context destroy needs no drain unless future code makes it free or overwrite
surface storage. Surface destruction already has the required drain and must
remain the storage-lifetime boundary.

### Diagnostics without losing the baseline

The shared block is already constrained to 4096 bytes, so design the counter
layout before appending fields. Prefer repurposing the two timings whose old
meaning disappears:

- `TimeHeadWait` becomes `TimeRingSpaceWait`;
- `TimeCrumbWait` becomes `TimeRenderDrain`.

Update the ABI comments and report labels together. Preserve counts for:

- submissions accepted asynchronously;
- ring-space wait calls, polls, timeouts and maximum occupancy/high-water;
- drains and stalls by site;
- latest issued and latest observed sequence;
- breadcrumb abandonments and HWS failure; and
- synchronous fallbacks because async was disabled or HWS was unavailable.

The old build remains the baseline capture; comparisons must not interpret
the renamed buckets as the old meanings.

## Implementation phases

### Phase 0: decision and reversible gate

Write `docs/decisions/2026-09-29-intel-gen3-async-errata-gate.md`. The required
one-line decision is whether runtime 3D may overlap CPU ring/status-page writes
with earlier GPU work on exact device `8086:27AE` revision 03, under the same
scratch-install, AC-power and DOS `V9X3D OFF` recovery conditions as sustained
3D. Erratum 12 (Intel 309220-0132, no published trigger and no A3 fix) makes
this a new interleave, not an inherited authorization.

Add the retained switch in the 16-bit boot policy and publish it across the
existing shared ABI. Default it off until phases 1-3 are green and the
decision is accepted.

### Phase 1: pure ring-space policy

In `include/velocity9x/intel_gma.h` and `src/chipsets/intel/i9xx_ring.c`, add a
pure decision helper that distinguishes invalid/oversize, retryable-full,
ready and timed-out. Cover it in `tests/host/test_i9xx_ring.c` with:

- room immediately;
- full, then room as head advances;
- wrap padding counted against space;
- head wrap-count and low status bits masked;
- no head progress through the exact poll bound;
- odd, zero and permanently oversized streams never retried; and
- tail unchanged on every refusal.

This phase changes no hardware behavior.

### Phase 2: completion state and submit split

In `d3d_i9xx.c`:

- introduce explicit synchronous-self-test and normal-runtime submit modes;
- allocate globally unique non-zero breadcrumb sequences;
- publish the newest sequence immediately after its tail write;
- implement the bounded re-plan loop;
- preserve synchronous behavior when the switch is off or HWS is failed; and
- remove the unconditional per-submit head and crumb waits only on the armed
  asynchronous branch.

Extract the sequence transition rules into host-testable logic. Tests must
cover issue A, issue B before A lands, observe A only (still busy), observe B
(done), wrap past `0xffffffff`, submit failure before tail publication, and
abandonment. Add a two-producer test proving values cannot collide.

### Phase 3: consumers, teardown and instrumentation

Audit every `v9x_d3d_i9xx_ring_submit` caller and every CPU framebuffer write.
Change `eng_i9xx.c` so ordered GPU blits queue behind rendering. Add the
session drain before reset. Attribute every drain site and update the snapshot
writer and its checks for the renamed timings and new counters.

Run `scripts/check-tree.ps1`, `scripts/build-host.ps1` and
`scripts/run-checks.ps1`. Review the final diff specifically for:

- a tail write followed by a path that returns failure;
- an outstanding sequence cleared without observing it;
- a CPU video-memory access without a drain;
- a storage free/reuse without a drain; and
- a raw ring caller accidentally treated as breadcrumb-complete.

### Phase 4: physical correctness, synchronous control first

Install one build with `IntelAsyncSubmit=0`. Run the DirectDraw/D3D probe,
Half-Life and 3DMark 99, preserve the complete `V9XTRACE` snapshot, and verify
it matches the pre-change synchronous behavior. This proves the refactor and
gate did not change the control branch.

Then enable async for one boot and run, in order:

1. the DirectDraw/D3D probe, including texture Lock/update, render, Flip,
   surface destroy/recreate and a windowed render case;
2. 3DMark 99 at 640x480x16 and 1024x576x16; and
3. Half-Life at 640x480x16 with a shipped timedemo and identical command line.

For every run retain the build ID, boot number, mode, elapsed time/FPS, full
snapshot, visual result and whether the desktop remained usable after exit.
Record the result in a dated decision document; screenshots or photographs
are required for corruption because stale texels often leave no counter.

Stop and revert to `IntelAsyncSubmit=0` on any hang, breadcrumb abandonment,
ring-space timeout, non-zero reset/error counter, texture corruption, missing
geometry, or surface-lifetime mismatch. Do not continue benchmarking a build
that failed correctness.

### Phase 5: performance decision

Compare paired synchronous/asynchronous timedemos, not interactive play. For
Half-Life report at least:

- timedemo FPS and elapsed time;
- `TimeD3dCalls`, stream build/decode/write, ring-space wait and drain time;
- submissions, triangles per submission and async queue high-water;
- blits handled by hardware versus CPU fallback; and
- all drain-site stalls and completion failures.

Accept the feature as default only if three repeated async runs are visually
correct, have no new failure counters, and improve the median Half-Life result
outside run-to-run noise. If GPU time still dominates, keep the mechanism only
if it is neutral and needed by the next batch-merging work; otherwise leave
the synchronous default.

## Not in scope

- Batch merging. Half-Life averages 3.7 triangles per batch (3,480,923
  triangles in 933,256 submissions); merging is the next independent lever.
- Faster GPU fill or the unresolved 1024x576 per-pixel cost.
- Interrupt-driven completion, multiple hardware rings or a GPU reset path.
- Weakening the stream allowlist, flush placement or surface-lifetime rules.
