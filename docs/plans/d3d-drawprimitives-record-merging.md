# Merge DrawPrimitives records that share one state

Date: 2026-09-30. Status: implemented and retained after physical comparison; qualification limits remain. It revises
`r3d-in-call-batch-merging.md`, lifting that plan's "no comparing state
between batches" exclusion for one case only: records inside one
`DrawPrimitives` call.

## Decisions

Michael Dale made both on 2026-09-30:

- **The no-state-comparison rule is lifted** for records inside one
  `DrawPrimitives` call. Merging across calls, and merging indexed calls,
  stay excluded.
- **Run accumulation lives in a new leaf module,
  `src/display32/r3d/r3d_records.c`, with its header**, and is host-tested.
  The external symbols it adds are agreed. See "Where the logic lives".

Implementation completed in the follow-up session on 2026-09-30. The host
tests first failed to link against the missing module, then passed with the
implementation. `check-tree.ps1`, `build-host.ps1`, and `run-checks.ps1`
passed, including all five family packages. The module reuses the fan buffer;
the core restores the prior context for pending-run flushes on state changes
and flushes on every record-loop exit. An oversized fan retains the previous
behaviour of ending its record on a refused chunk; subsequent records still
draw. Physical probes, benchmarks and fresh-boot timing are now recorded
below; the decision retains the implementation with explicit validation limits.

Hardware continuation, 2026-09-30: broad candidate probes matched controls
on the GMA950 netbook and Mach64 Gateway. The netbook completed 3DMark at
1024x576 and 640x480; the Gateway completed 640x480. Fresh-boot Half-Life
repeat series on Wi-Fi averaged 99.174 fps on candidate boot 61 and 87.103
fps on control boot 62. The implementation is retained, with the initial
Gateway lock unexplained. The subsequent
[gameplay demo comparison](../decisions/2026-09-30-halflife-gameplay-record-merging.md)
averages 48.823 fps versus 41.436 fps (17.8% faster), with matching sampled
geometry but an unresolved reason-6 refusal-count difference. It completes
the owed comparison without establishing a clean correctness pass. The
[targeted physical clipped-fan check](../decisions/2026-09-30-mach64-clipped-fan-ordering.md)
now passes on the Mach64, including redundant state and capacity splits. See the
[qualified decision](../decisions/2026-09-30-drawprimitives-record-merging-physical.md)
and [physical evidence](../probe/d3d-record-merge-2026-09-30/README.md).
Local deployment artifacts are in build/driver-results/dp-record-merge/.

## Evidence

Netbook boots 56-58, Half-Life 1.1.1.0 at the `c1a1` spawn point, 640x480
Direct3D, `timerefresh` spins:

- Half-Life's world arrives as `DrawPrimitives` records of about two
  triangles, 4.7 to a call, and each record is its own list call and sink
  batch. Culling splits almost nothing (0.8-1.0% culled, 0.995 batches a
  list call).
  `../decisions/2026-09-30-halflife-batches-are-small-records-not-culling.md`.
- 54% of drawn records carry state pairs, and 90.3% of those leave the
  context unchanged. Every record whose pairs do change the context changes
  the texture handle.
  `../decisions/2026-09-30-halflife-redundant-state.md`.
- Counted on boot 58 with `5da6648`: merging consecutive records that leave
  the context unchanged would turn 279,109 record batches into 66,861, at
  4.17 records and 8.07 triangles a batch. That is 73% of all sink batches
  in the window. Merging only records with no pairs at all would save 27%.

**Unmeasured:** the time saved. At the ~5 us fixed GPU cost a batch in
`2026-09-25-the-gen3-head-wait-is-pixels.md`, 212K fewer batches is about
1 s of an 18.6 s window, before build and write. Pixel cost is unchanged.
Phase 4 measures it. Other applications are uncounted: 3DMark 99 sends up to
101 state pairs a record, and its record sizes are unknown.

## Contract

1. **One state per batch.** A merged batch is drawn under exactly the
   context its records were drawn under one by one. A record joins the
   pending run only if applying its pairs leaves the context byte-identical
   (`v9x_d3d_context_same`, as counted).
2. **Nothing outlives the call.** The pending run is drawn before
   `V9xD3dDrawPrimitives` returns, on every path out of the record loop:
   the terminator, the 64-record bound, a clamped state count, a refused
   record shape.
3. **Order is preserved.** Triangles reach `v9x_d3d_draw_list` in record
   order, fans expanded in place, as today.
4. **Capacity is `V9X_D3D_INDEXED_BATCH`.** The same 64-triangle bound
   every list path uses. A record that alone exceeds it (a list of up to
   192 vertices is exactly 64; a fan can reach 190 triangles) flushes the
   run and is drawn as today.
5. **Refusal semantics widen, as before.** A refused merged batch counts
   once in `batches_engine_refused` and can lose more triangles than one
   record did. Refusals must not rise on any gate run.
6. **No INI key.** It becomes the behaviour of the next build, per the
   project's no-gated-defaults rule. The comparison run uses the previous
   build.

## Design

The flush has to happen *before* a real change takes effect, because
`v9x_d3d_draw_list` hands the live context to the engine sink when it draws.
Predicting the change from the pairs would duplicate
`v9x_d3d_apply_state`'s switch, so the loop applies and compares instead:

1. Copy the context (`state_before`, already there for the counters) and
   apply the record's pairs.
2. If the context changed and a run is pending: copy the new context aside,
   restore `state_before`, draw the pending run, and put the new context
   back. The context is about 40 DWORDs, so this is two small struct
   copies per real change (~15K per 18 s window here).
3. Append the record's triangles to the run: a list is copied, a fan is
   expanded. If they would not fit, draw the run first.
4. On every exit from the loop, draw the pending run.

The run buffer is the existing `fan_batch` array (64 x 3 TL vertices,
6,144 bytes), which the fan path already uses as a gather. So the stack
frame does not grow, and the nesting under `v9x_d3d_draw_list`'s own
staging stays at the ~12.5 KiB the in-call plan recorded.

### Where the logic lives

Run accumulation (append, split at capacity, flush in order, flush on exit)
is pure, and is where ordering bugs would hide. It belongs in host-tested
code, not in `d3d_core.c`, which the host build does not compile. It is
therefore a leaf module, the same shape as `r3d_cull.c` and `r3d_clip.c`:

- `src/display32/r3d/r3d_records.c` and `r3d_records.h`, including only
  `velocity9x/types.h` and `r3d.h`, never the DDHAL side;
- a run struct over a caller-owned `V9X_R3D_VERTEX` array and its triangle
  capacity, plus a sink callback and user pointer, as `V9X_R3D_LIST` does;
- `v9x_r3d_records_append_list`, `v9x_r3d_records_append_fan` and
  `v9x_r3d_records_flush`, following the project's
  `v9x_<area>_<verb>` naming, each returning whether the sink accepted
  everything it was given;
- added to `build-ddraw-hal-dll.ps1`'s source list and to
  `scripts/lib/host-sources.ps1` with `tests/host/test_r3d_records.c`,
  registered in `test_main.c`.

The state comparison, the restore-and-flush on a real change, and the
counters stay in `d3d_core.c`, which owns the context.
`v9x_d3d_context_same` is already there from `5da6648`. The rejected
alternative was header-only statics in the style of `r3d_runs.h`, which
avoids external symbols but puts about 100 lines of logic in a header.

The four record-run counters stay. After the change, list calls from the
`DrawPrimitives` path should fall to about `DpRecordRunsNoopJoined`, which
is the direct check that the merge does what was counted.

## Phases

### Phase 1: host module, test first

In `tests/host/test_r3d_records.c`, before the module exists:

- consecutive list records form one batch, triangles in order;
- a fan record expands in place between list records;
- a run splits exactly at 64 triangles, order preserved;
- a record over capacity flushes the run and is drawn alone;
- a flush with nothing pending draws nothing;
- a sink refusal is reported and later runs still draw.

Watch them fail, then implement. Gates: `check-tree.ps1`, `build-host.ps1`.

### Phase 2: wire into `V9xD3dDrawPrimitives`

Replace the per-record `v9x_d3d_draw_list` calls with the run, add the
restore-and-flush on a real change, and flush on every loop exit. Leave the
indexed path, `DrawOnePrimitive` and the DX3 `RenderPrimitive` loop alone.
Gates: `check-tree.ps1`, `build-host.ps1`, `run-checks.ps1`.

### Phase 3: hardware correctness

`d3d_core.c` is shared by every engine, so the change is checked on two:

- **Netbook, Gen3:** the broad DirectDraw/Direct3D probe; 3DMark 99 at
  640x480 and 1024x576, which sends all its state through `DrawPrimitives`
  and is the hardest state test available, with screenshots; Half-Life at
  the `c1a1` spawn with a frame capture. Compare `DpRecordRunsNoopJoined`
  against the new list-call count.
- **A clip-in-core engine:** the Mach64 on the Gateway, or the ViRGE/DX on
  A8U4I5. The probe and a 3DMark 99 run. This also closes the in-call
  plan's outstanding clipped-fan ordering check.

Stop on missing or reordered geometry, wrong textures on merged polygons
(the signature of a flush that missed a texture change), a new refusal or
any failure counter.

### Phase 4: performance and decision

Half-Life at the `c1a1` spawn, four `timerefresh` spins, on the merge build
and on `5da6648` (counters, no merge), each on a fresh boot. Report fps,
`TimeD3dCalls`, engine-draw, ring-write and head-wait time, and batches per
frame. Keep the change if it is correct and not slower. Claim a gain only
outside the spin-to-spin spread (boot 58: 104.0-104.1 fps). A gameplay
demo comparison is now recorded in the
[gameplay decision](../decisions/2026-09-30-halflife-gameplay-record-merging.md).
Chained movement scripts recorded `v9xbench` without operator input; both
swap reboots reconnected within the 180-second allowance. The performance
comparison is complete, but the reason-6 refusal difference still requires
isolating the rejected draw before a clean correctness sign-off.

## Not in scope

- Merging across `DrawPrimitives` calls, or holding anything past a call.
- Merging indexed calls with each other or with records.
- Asynchronous submission, which stays rejected.
- Filtering redundant pairs before they are applied. The context compare
  already makes them free for merging, and skipping them would change what
  the diagnostics see.
