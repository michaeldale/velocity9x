# HD 4600: submit a draw and return, with the next draw built while the render engine runs this one

Date: 2026-10-20. Status: approved 2026-10-20 as written, with the four
proposed decisions below. Phase 0 done the same day: 0 of 1,534,574
pre-write waits stalled, and the post-submit wait is 75.1 µs
(`../decisions/2026-10-20-haswell-pipelined-phase0-wait-split.md`).
Phase 1 next.

## Why

`docs/decisions/2026-10-20-haswell-3dmark2000-baseline.md` measured
157.7 µs per Direct3D batch on the i5-4570. A tester's HD 4600 showed the
same (report V9X-D9VZS6,
`docs/issues/2026-10-20-haswell-3dmark2001se-artifacts-field.md`). Of
that time:

| µs a batch | what | where |
|---|---|---|
| 75.5 | two render-engine waits | `v9x_d3d_hsw_wait_rcs`, timed as `TimeRenderDrain` |
| 60.6 | vertex copy into the uncached reserve | `TimeRingWrite` |
| 6.8 | state write | `TimeRingWait` |
| 4.5 | kernel build | `TimeDecode` |
| about 10 | mapping, batch build, submit | the rest |

`d3d_hsw.c` is synchronous by design (its header says so): each draw
waits for the blitter and the render engine, writes its state, submits,
and waits for the sequence store. The wait before the state write finds
the engine idle, because the draw before it already waited, so nearly
all of the 75.5 µs is the wait after the submit: the CPU spinning while
the GPU runs the batch. Nothing overlaps.

The ceiling: if the CPU builds batch N+1 while the GPU runs batch N,
the batch costs the larger of the two halves rather than their sum,
about 83 µs of CPU against about 75 of GPU. That is roughly 158 down to
85-95 µs a batch. It is an estimate from one table of averages, not a
frame-rate promise. Phase 0 measures how the two waits split before
anything is built on it.

Not in scope: the GPU's own cost per batch (why 43 triangles take about
75 µs, presumably the full state and flushes every batch carries), the
uncached vertex copy (the aperture is UC by MTRR; PAT is untried), and
the 16 MiB heap. Each is a separate lever.

## What the synchronous design is relied on for

"The RCS is idle whenever anything else runs" is a contract other code
leans on without saying so. Each of these must hold again once draws
are left running:

| Boundary | Today | With draws in flight |
|---|---|---|
| Flip (`ddhal_core.c`, `v9x_wait_idle` before the flip) | waits for the blitter; the RCS is idle anyway | must also wait for the RCS, or the panel shows a buffer still being drawn |
| Lock (`V9xHalLock`) | waits for the blitter; honours `DDLOCK_DONOTWAIT` | must also wait for the RCS before the CPU reads or writes |
| Blt and fill on BCS (`eng_hsw.c`) | each blit waits for the previous one | a blit must not start while the RCS still writes or reads its surfaces: the two rings are not ordered against each other |
| CPU blit fallback, render-interface CPU clear | drain the blitter | drain both |
| `GetBltStatus` / `GetFlipStatus` | the blitter's state | busy while either ring is busy |
| A draw into the displayed buffer | finishes before the call returns | the 16-bit driver draws GDI and the software cursor straight into that buffer, with no drain hook; see decision 3 |
| `FlipToGDISurface`, `SetExclusiveMode`, `DestroyDriver`, D3D context destroy | nothing owed | drain: the next owner of the screen is GDI |
| Disable / mode set / the mini-VDD's `HSW_ENGINE_CLOSE` | ring idle by construction | the close must not turn a busy ring off |
| Reserve contents | one draw owns all of it | the GPU reads the previous draw's state, batch and vertices while the CPU writes the next |
| Kernel slots | rewritten every batch with the same words | the slot index omits the fog colour (`v9x_d3d_hsw_kernel_slot`), so a slot can be rewritten with different words while a batch in flight executes it |
| The RCS ring wrap in `v9x_d3d_hsw_submit` | "Called idle, so HEAD equals TAIL" | the wrap's NOOP fill and the new commands must not overwrite commands the engine has not read |

The first five reduce to one change. All of them already go through
`v9x_hsw_wait_idle` in `eng_hsw.c`, directly or through
`v9x_render_drain`. If "idle" there means both rings caught up (BCS
status dwords 0 and 1, RCS dwords 8 and 9), every existing CPU and BCS
synchronization point covers draws in flight without a new call site.
The status page is one page shared by both, so this reads no register
it does not already read.

## Design

### Two slots, chosen by the sequence

The reserve stays 128 KB at `1800000h`, so the mini-VDD's mapping and
the probe region at `1820000h` do not move. The per-draw areas split in
two:

| Area | Today | Slot 0 | Slot 1 |
|---|---|---|---|
| per-draw state (`V9X_HSW_SHADE_STATE_BYTES` is `1540h`) | state + `0000h` | state + `0000h` | state + `2000h` |
| kernel slots | state + `4000h`-`C000h` | shared, unchanged | shared |
| batch | `10000h`, 8 KB | `10000h`, 4 KB | `11000h`, 4 KB |
| vertices | `12000h`, 56 KB | `12000h`, 28 KB | `19000h`, 28 KB |

A slot holds 298 TL triangles (248 with unit 1's coordinates), against
597 today. The average batch measured is 43, and the pieces loop already
splits longer ones. Slot 1's per-draw state is addressed by giving the
workload a state base `2000h` higher, so every `V9X_HSW_SHADE_*` offset
stays as it is. The kernel offset handed to the batch is then `2000h`
lower. Phase 1 checks that the init state at state + `0000h`-`0220h`
is only read by the init batch and not referenced by draws.

The slot is the next sequence's low bit: sequence N uses slot N & 1.
The sequence lives in the shared status page (dword 9, `RCS_SENT`), so
two Direct3D processes alternate slots correctly with no per-process
state, under the Win16 serialization that every HAL entry already runs
in (`Win16*Held` equals `Win16*Calls` in both snapshots).

Before a draw writes slot N & 1, the batch that last used that slot,
sequence N-2, must be done: `RCS_DONE >= RCS_SENT - 1`, in wrapping
32-bit arithmetic. At most one batch is in flight. That replaces the
wait after the submit. The wait before the state write stays, with this
condition instead of "all done".

### Kernel slots

Each of the 64 kernel slots gets a signature in the status page: its
description (stage 0, stage 1, specular, fog, and the three fog colour
words) as last written. A draw whose kernel matches the signature skips
both the build and the copy (`TimeDecode`, about 4.5 µs). One that
differs waits for the RCS to be fully idle, then writes the kernel and
the signature. A fog-colour change costs a drain; nothing else does.

### Ring space

`v9x_d3d_hsw_submit` reads HEAD and plans the six-dword submission
against it, as `v9x_i9xx_ring_plan` does for Gen3. The NOOP fill at a
wrap is written only over bytes the engine has passed. With at most one
submission (24 bytes) unread in a 4 KB ring this never waits in
practice. It is still checked, because the Gen3 corruption of
2026-09-29 was exactly a plan made against a stale head
(`docs/decisions/2026-10-01-intel-async-corruption-was-the-dword-head.md`).

### The pure part

The slot choice, the "may write slot s" test, the kernel-signature
compare and the ring-space plan are arithmetic on sequence numbers,
offsets and words. They go in `src/chipsets/intel/hsw_pipe.c` with a
header in `include/velocity9x/`, beside `hsw_3d.c`. Host tests in
`tests/host/` cover:
- sequence wrap at 2^32;
- two processes interleaving;
- a ring wrap with one submission outstanding;
- a stale head;
- the slot geometry against `V9X_HSW_SHADE_STATE_BYTES` and the vertex
  sizes.

`d3d_hsw.c` applies the answers. This is the `mtrr.c` layering.

### Draws that stay synchronous

- **The depth fill** (`v9x_d3d_hsw_depth_fill`) drains, then runs as
  today. It is rare: 3,941 Clear2s in a 3DMark 2001 SE run against 2.77M
  batches.
- **A draw whose target is the buffer on screen** waits for its own
  sequence before returning (decision 3).
- **The initialising batch** stays synchronous: it proves the status
  page round trip that every later wait trusts.

### Failure

A timeout in the wait before a slot is written happens before anything
is published, so the draw is refused and counted as today (reason 12,
`REFUSE_IDLE`, added 2026-10-20). Once TAIL has moved the batch is the
GPU's. A later timeout is the existing quarantine at whichever drain
site sees it, and never a refused or replayed draw: Gen3's invariant 8.

The mini-VDD's `HSW_ENGINE_CLOSE` gets a bounded wait for RCS dwords 8
and 9 to agree before it turns the ring off, and records whether it
timed out. That close has not yet run on hardware (`docs/STATUS.md`);
Phase 3 runs it.

### Diagnostics

No new shared fields. The block is near its 4096-byte limit, and the
Haswell path leaves Gen3's pipeline counters at zero. They take the
Haswell meanings below, written in `d3d_hsw.c`'s header and in the
V9XTRACE labels together:

- `AsyncEnabled`: 1 when the pipelined path is active.
- `AsyncSubmits`: batches returned without waiting.
- `RenderDrainWaits` / `RenderDrainStalls`: the pre-write waits, and
  those that found their slot still busy.
- `BreadcrumbIssuedLast` / `BreadcrumbObservedLast`: `RCS_SENT` and
  `RCS_DONE` at the last wait.
- `SyncSubmits`: draws kept synchronous (depth fill, displayed target,
  kernel change).

`TimeRenderDrain` keeps its meaning: time spent waiting for the RCS,
wherever the wait now is.

## Phases

0. **Measure the split.** The pre-write wait counts its stalls into
   `RenderDrainStalls` and times itself apart from the post-submit
   wait. No behaviour change. Prediction: under 1% of pre-write waits
   stall, and the post-submit wait is about 75 µs. One run of the 3DMark
   2000 default benchmark on the 4570, snapshot after. If the prediction
   fails, the ceiling above is wrong and this plan is rewritten before
   Phase 1.
1. **Pure module and tests**: `hsw_pipe.c`, its host tests, and
   `build-host.ps1` green. Failing tests written first.
2. **Both rings in `v9x_hsw_wait_idle`**, still synchronous submission.
   It changes nothing observable, because the RCS is always idle here,
   and it proves every drain site reaches the RCS check: V9XDDP's
   DirectDraw and D3D checks, then 3DMark 2000, with the counters
   unchanged from the baseline.
3. **Slots, kernel signatures, ring plan and the deferred wait**, as the
   build default (no INI key or verb: there is no gated default on the
   dev driver). Gates on the 4570, in order:
   1. V9XDDP's full D3D set, pixel-exact as before.
   2. V9XD32 at 32 bpp.
   3. Half-Life `-d3d` `timedemo mwd5` three times. Frames are watched
      for corruption by screenshot, which works during Direct3D on this
      machine.
   4. 3DMark 2000 default, every game and feature test screenshotted.
   5. Alt-tab out of and back into a running benchmark
      (`FlipToGDISurface`, `SetExclusiveMode`).
   6. Two Direct3D processes at once.
   7. A restart through the agent's `reset` with Direct3D idle, which
      runs Disable and the engine close.

   Each gate records `EngineIdleTimeouts`, `EngineResets`,
   `BatchesEngineRefused` and the counters above. Scores are recorded,
   not compared or chased.
4. **The tester's case**: 3DMark 2001 SE on the 4570, when an installer
   is to hand, at 1024x768x32 as the tester ran it. This is also where
   the refusal-site reasons added 2026-10-20 get their first reading.

If corruption appears in Phase 3 it is filed in `docs/issues/` with
captures, and the build goes back to Phase 2's. Gen3's first attempt
corrupted 3DMark 99, and its cause was found by measurement, not by
reading the code.

## Decisions owed

1. **Two slots, not more.** One batch in flight hides the GPU's 75 µs
   behind the CPU's 83. A third slot would buy only the cases where the
   GPU is the slower half, and would halve the vertex area again.
   Proposed: two.
2. **Draining in `v9x_hsw_wait_idle`** (both rings), rather than adding
   a render drain at each site. It covers every existing site at once.
   The cost is that a DirectDraw blit always waits for the last draw,
   which is required for correctness in any case. Proposed: yes.
3. **A draw into the displayed buffer stays synchronous.** The 16-bit
   driver's GDI and software cursor write that buffer with no hook this
   DLL sees. The alternative is a drain hook on the 16-bit side, a
   cross-module change. Proposed: synchronous, counted in `SyncSubmits`.
   (`DrawsToFront` is counted only on Gen3, `d3d_i9xx.c`, so it says
   nothing here.)
4. **The kernel-signature skip** (no rebuild, no copy when unchanged)
   rides along because the signature table is needed for correctness
   anyway. Proposed: yes, in Phase 3.
