# Intel Gen3: send a batch's state and texture flush only when they changed

Date: 2026-11-01. Status: step 0 done the same day
([record](../decisions/2026-11-01-gen3-batch-cost-step0-profile.md)): a
D3D submission is now CPU-bound (39 of 48.8 µs; 10.2 µs waiting on the
GPU) and only 56.7 % repeat their state (OpenGL 8.2 %). By the rule in
step 0, steps 1 and 2 wait on re-ranking. Step 1 run anyway at Michael's
request, the same day
([record](../decisions/2026-11-01-gen3-batch-cost-step1-variants.md)):
the texture flush costs nothing measurable, and cutting the repeated state
saves the GPU about 1 µs a submission while costing the CPU about 2;
Half-Life's fps did not move. **Stopped at step 1** by its own rule; the
omit bits 0x40 and 0x80 stay as measurement tools, off by default.

Machine: the netbook (945GSE / GMA 950, Atom N280 1.66 GHz).
Benchmarks: Half-Life `timedemo mwd5`, Direct3D and OpenGL, and 3DMark
2000 at 1024x576x16. Baselines are the write-combining boot 252
([record](../decisions/2026-10-25-intel-gen3-write-combining.md)):
40.1 fps D3D, 23.4 fps GL, 3DMark 2000 1189.
Engine: [d3d_i9xx.c](../../src/display32/d3d/d3d_i9xx.c),
`v9x_d3d_i9xx_draw_triangles_body`; builders in
[i9xx_3d.c](../../src/chipsets/intel/i9xx_3d.c), allowlist in
[i9xx_3d_decode.c](../../src/chipsets/intel/i9xx_3d_decode.c).

This is the Gen3 part of the review of the
[Voodoo4 throughput plan](3dfx-vsa100-d3d-throughput.md). Of that plan's
steps, Gen3 already has the equivalent of step 1 (asynchronous submission,
[record](../decisions/2026-10-01-intel-async-corruption-was-the-dword-head.md))
and step 5 (a ring in write-combined memory). Step 3 has little to take:
a vertex is already 7 dwords, with specular only under fog. What is left is
step 2: sending the same state on every batch.

## What a batch carries today

Read from the code on 2026-11-01:

1. `MI_FLUSH_READ`: a flush with the texture (map) cache invalidated, on
   every batch. It was added for Final Reality's stale texels (intel76),
   which come from the CPU refilling texture memory between frames. It is
   needed only after texels were written; it is paid whether or not they
   were. Marked "UNMEASURED as a fix" in the code.
2. Polygon stipple off (2 dwords), per batch, since GitHub issue 7.
3. The whole state block and fragment program: 61.3 dwords a submission on
   Half-Life D3D.
4. The primitives: 241 dwords a submission (about 11 triangles).
5. `MI_FLUSH`, so that completion means drawn and not just parsed.
6. The completion marker: an `XY_COLOR_BLT` to the status page and another
   `MI_FLUSH` (`v9x_i9xx_build_breadcrumb_stream`, `i9xx_flip.c`).

The [2026-10-01 profile](../decisions/2026-10-01-hal-submission-profile-on-the-netbook.md)
counted items 2 and 3 against the previous submission: **76.4 % of Half-Life
D3D submissions (80.0 % GL) repeated the previous state exactly**, and those
that differed differed in 0.5 dwords on average. Its time split (84.3 µs a
submission, 52.8 µs of it waiting for the GPU) predates both async
submission and write-combining, so it no longer says where the time goes.

## Steps

In order. Each step is measured on the netbook before the next starts.

### Step 0: profile again (no code)

The instrument exists: `I9xxStateDwords`/`PrimDwords`/`StateCompared`/
`StateRepeats`/`StateChangedDwords`, the HAL time buckets (`TimeEngineDraw`,
`TimeDecode`, `TimeRingWrite`, `TimeRenderDrain`) and the batch-shape cells.
Run Half-Life mwd5 D3D and GL with V9XTRACE on the current build, as the
10-01 record did (`hl-window.ps1`, `hlprof.py`), plus DB3D's StateChange,
BatchCost and TriBatch rows, which the HD 4600 work used to price a batch.

Acceptance: a decision record with the new split and repeat rate. It says
whether a submission is now limited by the CPU (build, decode, ring write)
or by the GPU (render drain). If the repeat rate has fallen well below the
10-01 figure, or the render drain is small and the build dominates, steps 1
and 2 are re-ranked before any code.

### Step 1: price each candidate before shipping it

As the HD 4600 did ([flush variants](../decisions/2026-10-28-haswell-flush-variants.md)):
a measurement-only omit mask, set by `V9XTRACE -omit:<mask>` through the
shared block and zero otherwise. Two variants:

- **TEXTURE**: no `MI_FLUSH_READ`. For DB3D only; it is wrong whenever
  texels changed, and is there to price the flush, not to ship.
- **STATE**: no stipple, state block or program when they equal the last
  ones submitted (the conditions in step 2).

Each variant: DB3D rows with the buckets, then V9XD32 `/ddi5` and `/ddi6`.
The completion marker and the `MI_FLUSH` before it are not varied: slot
reuse, Flip and Lock rest on them.

If neither variant moves a small batch by more than DB3D's run-to-run noise
(about 0.3 µs on the 4570; to be established on the netbook from two runs
of mask 0), the plan stops here with a record saying so.

### Step 2: ship what step 1 priced

**State.** The full stream is still built and still decoded: the allowlist
runs on exactly what it runs on today, so the memory-safety argument does
not change. Only then, if the state region equals the last submitted one
and nothing has invalidated it, the region is cut from the stream before
`v9x_d3d_i9xx_ring_submit` (the primitives move down; about 240 dwords).
The skipped dwords are the ones already in the engine, and they passed the
decoder when they were sent.

The copy describes the engine, which is one per machine, so the HAL's
shared statics ([issue](../issues/2026-10-06-hal-statics-shared-across-processes.md))
are the right home for it this time. That holds only if this DLL is the only
thing writing the ring after boot. The 16-bit side's `intel_3d16.c` scenes
appear to run at boot only; confirm that, and that Gen3 Glide and the ICD
reach the ring only through this draw path, before relying on it.

The copy is discarded, and the next batch sends everything, on:

- any submission not made by the draw path: `v9x_d3d_i9xx_submit_blt`, the
  ring flip (`i9xx_scanout.c`), the status-page proof. Whether a blit
  disturbs 3D state on Gen3 is not documented (no public Gen3 3D PRM), so
  this is conservative; Half-Life has a few of these per frame, not per
  batch.
- `v9x_d3d_i9xx_reset` (a new session), Enable, a mode set, and the DOS-box
  return path ([record](../decisions/2026-10-23-a-dos-vm-reprograms-the-gma-through-its-io-bar.md)).
- a submission that was refused after the compare, failed, or whose
  completion timed out or was abandoned.

The stipple pair moves into the compared region: it is per-batch only
because "nothing else this driver does says the state survives", and the
discard rules above are what now say so.

**Texture flush.** `MI_FLUSH_READ` only when a texture generation number
has moved since the last batch. It moves on Unlock of any surface that is
not the primary or a render target, on any Blt (engine or CPU) whose
destination is not a render target, on texture create, and with every
discard above. This over-counts on purpose: the cost of a wrong skip is
intel76's stale texels.

**Where the logic lives.** The comparison, the discard rules and the
texture generation are pure logic and go in a host-tested module beside
`i9xx_3d.c`, with a test written to fail first: equal state skipped, each
discard forcing a full send, a texture write forcing the flush, a refused
batch not updating the copy. This adds external symbols, which is a design
change: **for Michael's agreement before code.**

Gates: `check-tree`, `build-host`, `run-checks`; on the netbook V9XD32
`/ddi5` `/ddi6`, V9XDDP's texture-update checks, V9XGLP, a Glide game
returning to the desktop, Final Reality (the intel76 stale-texel case),
and Half-Life D3D and GL plus 3DMark 2000 against the baselines above.

The GMA 900 and the Pineview GMA 3150 take the change through the shared
engine (same design, same fix). Neither is a gate; the record says they
did not run it.

### Step 3: one submission for consecutive same-state draws (decision first)

With the state elided, two draws in a row with the same state differ only
in their primitives, and could share one flush and one completion marker.
Within one `DrawPrimitives` call, record merging already does this. Across
calls it needs the core to hold a batch after the call returns, which the
[record-merging decision](d3d-drawprimitives-record-merging.md) of
2026-09-30 excluded. Michael would have to lift that exclusion first, and
it would apply to every engine, not only Gen3. Step 0's batch counts say
whether it is worth asking.

## Not in this plan

- **The completion marker as a blit.** Each batch ends by switching from
  the 3D pipe to the blitter for the status-page store. That switch may be
  part of the per-batch GPU cost; it is not measured. The store-based
  marker failed on this machine (intel83: 1,460 timeouts, none landed;
  [review](../issues/2026-09-18-final-reality-flicker-independent-review.md)),
  and the blit is the measured replacement. Revisiting it needs its own
  probe.
- **Strips and fans** as hardware primitives. Gen3 has them and Mesa's i915
  uses them, but the core expands them before the engine sees them. It is
  the same engine-interface change as the Voodoo4 plan's step 4 and should
  be decided once, for both.
- **The HD 4600.** Its flush variants already showed invalidation and state
  re-allocation cost nothing measurable there; its remaining lever is
  fewer, larger batches ([batch cost](intel-haswell-batch-cost.md)), which
  is step 3's question.
