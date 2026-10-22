# HD 4600: the render engine's cost per 3D batch

Date: 2026-10-23. Status: parked. Michael judged Tux Racer and Quake 3
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
