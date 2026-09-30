# Merge surviving triangles within one draw call

Date: 2026-09-30. Status: implemented; repository and physical correctness
gates pass. Half-Life's batch-size hypothesis was rejected below.

The later record-merging plan's [gameplay comparison](../decisions/2026-09-30-halflife-gameplay-record-merging.md)
now records and replays `v9xbench` on both builds. It measures a 17.8% gain
for record merging, with the vertex-builder refusal difference still open.
The later [refusal fix](../decisions/2026-09-30-record-refusal-neighbours.md)
preserves original records after an atomic Gen3 vertex rejection and passes
netbook validation. The updated Mach64 clipped-fan retest passes on boot 71; the broad probe hard-locked on boot 70 and remains unresolved.

Implementation review on 2026-09-30 corrected two assumptions in the draft:
the shared block is now capped at 8,192 bytes, not 4,096, and the 6,144-byte
staging frame is nested below an existing 6,144-byte indexed/fan gather frame.
Open Watcom emits `sub esp,1848h` for the list wrapper and `188ch`/`1894h` for
the large callers, so peak nested use is about 12.5 KiB. The storage remains
per-call and bounded; physical testing is still the gate for accepting it.

Implemented in the working tree:

- diagnostics ABI `2026093001` appends all five list counters and the trace
  snapshot writer publishes them;
- the neutral builder merges only after its first break, retains the no-copy
  window for an unbroken list, and preserves zero-capacity behaviour;
- all DX5 list callers use a 64-triangle caller-owned staging array; the
  render interface remains at zero capacity;
- the six required host behaviours are covered, including continued drawing
  after a refused merged batch;
- `check-tree.ps1`, `build-host.ps1`, and the complete `run-checks.ps1` pass.

The first physical attempt on 2026-09-30 could not start, because both
machines timed out. Netbook boot 54 then ran the probe, 3DMark 99 at both
resolutions and a Quake 2 timedemo with no regression
(`../decisions/2026-09-30-r3d-batch-merge-netbook-boot54.md`). 3DMark culls
and clips nothing in the list builder, so merging never ran there. Quake 2
goes through the zero-capacity render interface; a control build of
`010b2d6` on boots 55-56 put both builds at 17.1-17.3 fps.

**Phase 0 answered on boot 56: the hypothesis is dead.** At the Half-Life
`c1a1` spawn point, 0.83% of list triangles were culled and list calls made
0.995 sink batches each. The small batches are small `DrawPrimitives`
records, about two triangles each and one list call per record. The Evidence
section below misread boot 31: 199,457 `DrawPrimitives` calls sat beside the
indexed calls, so the engine made 2.85 batches a call, not 7.3 an indexed
call (`../decisions/2026-09-30-halflife-batches-are-small-records-not-culling.md`).
The built change stays, because it regresses nothing, but Phase 4 will not
find a Half-Life gain in it. The in-call lever that remains is merging
consecutive state-free records of one `DrawPrimitives` buffer. Boot 57
counted it (`../decisions/2026-09-30-halflife-record-runs.md`): 269,953
records would become 196,675 batches, 1.37 records a run, and 72% of runs
are started by a record carrying state pairs. Boot 58 counted those pairs
(`../decisions/2026-09-30-halflife-redundant-state.md`): 90% of state-carrying
records leave the context unchanged, and every record that does change it
changes the texture handle. Letting unchanged records join a run cuts record
batches by 76% (4.17 records, 8.07 triangles a batch), 73% of all sink
batches. That argues for lifting this plan's "no state comparison" exclusion
for record merging within one `DrawPrimitives` call; it needs its own plan
revision. Phase 3's clip-in-core ordering check now passes on the Gateway:
the [targeted clipped-fan probe](../decisions/2026-09-30-mach64-clipped-fan-ordering.md)
clips three input triangles per case, matches all pixels against separate
original-order calls, and crosses the 64-triangle staging boundary without
a new refusal, timeout or reset. Reversing the records changes 1657 pixels.

## Goal

Submit fewer, larger batches without changing when anything reaches the
hardware. Every merged batch is built and submitted inside the Direct3D call
that supplied its triangles, so no draw outlives its call, no state or
resource boundary is crossed, and the synchronous submission contract stays
as it is. This is the lower-risk alternative to holding batches across calls
and to asynchronous submission, which failed on hardware
(`../issues/2026-09-29-intel-gen3-async-corrupts-3dmark99.md`).

## Evidence

Half-Life at 640x480x16 on MICHAEL-NETBOOK, boot 31
(`../decisions/2026-09-25-netbook-halflife-alpha-test-V9XTRACE.ini`):

- `IndexedCalls=128580`, `IndexedTriangles=3480923`: 27 triangles a call,
  every one through `DrawOneIndexedPrimitive`. `IndexedPrimTypeSeen=0x10`:
  triangle lists only.
- `TimeEngineDrawCalls=933256`: 7.3 engine batches a call, 3.7 triangles a
  batch.
- Head wait split by the batch-shape cells: 689,351 batches under 1K pixels
  took 6.3 s at 8-19 us each; the other 244K took about 25 s and grow with
  area. `2026-09-25-the-gen3-head-wait-is-pixels.md` puts the fixed GPU cost
  at about 5 us a batch.
- Building and writing cost 14.5 s over the run, 15.5 us a batch.

The call is not what makes the batches small. `V9xD3dDrawOneIndexedPrimitive`
gathers up to 64 triangles (`V9X_D3D_INDEXED_BATCH`) and hands them to
`v9x_r3d_draw_list` (`src/display32/r3d/r3d_clip.c`), which sends runs as
windows on the array. Gen3 has `clip_in_core = 0`, so the only thing that
ends a run there is a culled triangle: each back face submits the run before
it. The ViRGE, Mach64 and software engines clip in the core, and a clipped
triangle also ends a run and sends its fan as its own batch.

**Unmeasured:** that culling is what splits Half-Life's lists. The inference
rests on Gen3's list builder having no other break condition. No counter
records culled triangles. Phase 0 measures it before anything is changed.

## Expected effect

This is arithmetic, not a measurement. If about 650K of the small batches
disappear, the saving is their fixed GPU cost (about 3-4 s of head wait) plus
the per-batch share of build and write, perhaps 5-7 s: roughly 10-15% of the
79 s run. Pixel time, the larger part of the head wait, does not change. On
the same chip, the ICD's held batches took Quake 2 from 22 to 28 fps
(`../decisions/2026-09-26-phase5-quake2-gen3-hardware-textures.md`), but that
merged across calls and is not the same mechanism.

## Contract

1. **Order is preserved.** Triangles reach the sink in input order, fans in
   the position of the triangle they replace. Blending and equal-depth
   results depend on it.
2. **Nothing outlives the call.** Staging is empty whenever
   `v9x_r3d_draw_list` returns. No static pending batch. No flush hook at
   Flip, Lock, Blt, SetRenderState, texture update or destroy, because none
   is needed.
3. **One state per list.** A list is drawn under one context state. The DX5
   `DrawPrimitives` path applies inline state between
   `V9X_D3DHAL_DRAWPRIMCOUNTS` records and calls the list builder once per
   record, so staging never spans a state change. Phase 1 confirms each
   caller keeps this.
4. **Capacity is the engine's.** A merged batch never exceeds 64 triangles,
   the smallest current bound (`V9X_I9XX_RUNTIME_MAX_TRIANGLES`, the 192
   vertex arrays, and `V9X_D3D_INDEXED_BATCH`). The list carries the bound
   explicitly rather than assuming it.
5. **The unbroken case costs no copy.** A list with nothing culled or clipped
   is still one window on the caller's array, as today. Staging starts at the
   first break: the pending window is copied in, then later survivors and
   fans are appended.
6. **Refusal semantics are unchanged.** A sink refusal still makes the list
   return zero while the rest is drawn. It now covers a merged batch, so it
   can lose more triangles than before. Half-Life has `TrianglesDeclined=0`,
   and the counters must show refusals do not rise.

## Design

`V9X_R3D_LIST` gains two fields, `staging` (a caller-owned
`V9X_R3D_VERTEX` array) and `staging_triangles` (its capacity). A zero
capacity keeps today's behavior, so every caller can move over one at a
time. The loop in `v9x_r3d_draw_list` becomes:

- while unbroken, extend the window;
- on the first culled or clipped triangle, copy the window into staging, in
  capacity-sized pieces if it is longer;
- append survivors and fan triangles, flushing staging to the sink when the
  next append would not fit;
- at the end, send the window if staging was never used, otherwise flush
  staging.

The staging array is 64 x 3 x 32 = 6,144 bytes, on the caller's stack beside
the indexed path's existing gather array of the same size. It is not static:
the render interface also calls the list builder, and a shared buffer would
need a serialization argument nobody has made.

Cross-chunk merging, where a 64-triangle input chunk culled to 32 survivors
fills the rest of its batch from the next chunk, is not in the first change.
It would move culling into the indexed gather so that `batch` fills with
survivors. Phase 4 decides whether that is worth doing.

## Phases

For future HL1 runs, use the supplied [mwd5 benchmark](../../tests/benchmarks/hl1/README.md):
Half-Life 1.1.1.0, `timedemo mwd5`, best FPS of three runs. The `v9xbench`
instructions and results below describe the earlier investigation.

### Phase 0: measure the split and record the demo

- Add counters to `d3d_diagnostics`: list calls, triangles in, culled,
  clipped, and sink batches. The shared block is capped at 8192 bytes, so
  check room first and bump the shared ABI. `check-tree.ps1` and the snapshot
  writer change with them.
- On the netbook, record a Half-Life demo (`record v9xbench`, a fixed route,
  `stop`). This install has no `hldemo1.dem`. Keep the `.dem` with the
  evidence so later runs replay the same frames.
- Run `timedemo v9xbench` three times on the current synchronous build and
  keep the full `V9XTRACE` snapshot.

**Stop rule:** if culled triangles are a small share and sink batches per
list call are near one, the hypothesis is dead. Record that in a decision doc
and do not build Phase 1.

### Phase 1: pure list-builder change, test first

In `tests/host/test_r3d_clip.c`, add tests that fail against the current
builder:

- alternating culled and visible triangles give one batch, survivors in
  order;
- a clipped triangle in the middle keeps its fan between its neighbours'
  triangles, in one batch;
- more than capacity splits exactly at capacity, preserving order;
- a sink refusal mid-list still draws the rest and returns zero;
- an unbroken list is still one window (pointer identity with the input);
- zero capacity reproduces today's batches exactly.

Several existing tests assert today's split boundaries and window pointers
(`sink.batches[1].vertices != v`, and similar). Change them where they encode
the old splitting, and say so in the commit. Watch the new tests fail, then
implement in `r3d_clip.c` and `r3d.h`.

### Phase 2: callers

Give staging to the Direct3D list paths in `d3d_core.c`: `v9x_d3d_draw_list`
serves the indexed, DrawPrimitives and DrawOnePrimitive paths. Leave the
render interface (`v9x_r3d_draw_body`) at zero capacity: the ICD culls before
it sends and already holds its own batches. Leave the DX3 `RenderPrimitive`
loop, which submits per source triangle and does not use the list builder;
Half-Life does not use it for triangles. It can come later, with the same
staging.

Gates: `check-tree.ps1`, `build-host.ps1`, `run-checks.ps1`.

### Phase 3: hardware correctness

The list builder is shared, so correctness is checked on two engines:

- **Netbook, Gen3:** the broad DirectDraw/Direct3D probe; 3DMark 99 at
  640x480x16 and 1024x576x16 with screenshots or photographs against the
  boot 50 control; Half-Life `timedemo v9xbench` with a frame capture.
- **One clip-in-core engine,** ViRGE/DX on A8U4I5 or the Mach64 on the
  Gateway: the probe and a 3DMark 99 run, checking the clipped-fan ordering.

3DMark scores are recorded, not compared. Stop on missing geometry, reordered
blending, a new refusal, or any failure counter.

### Phase 4: performance and decision

Run the paired Half-Life timedemo three times on each build. Report FPS,
`TimeD3dCalls`, engine-draw, ring-write and head-wait time, sink batches and
triangles per batch, and refusals. Keep the change if it is visually correct
and not slower; claim a gain only for a median outside run-to-run noise.
Record the result in `docs/decisions/`. Include cross-chunk merging only if
triangles per batch are still well under 64 and the remaining batches are
mostly the small, fixed-cost kind.

## Not in scope

- Holding batches across calls, and any flush at Flip, Lock, Blt or state
  changes.
- Asynchronous submission, which stays opt-in and off.
- Comparing state between batches.
- Gen3 stream changes: TRIFAN/TRISTRIP emission, trimming flushes, or
  per-pixel cost.

Gateway follow-up: three broad reruns completed on boots 71-73, including
two fresh boots and the unmodified probe. All checks match baseline (202/14),
with zero timeouts/resets. The boot-70 intermittent lock remains open; see
[retained investigation](../probe/d3d-record-merge-2026-09-30/gateway-refusal-fix/README.md).
