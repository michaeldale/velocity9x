# Where the HD 4600's fixed cost a batch is not: seven flush variants, one kept

Date: 2026-10-28

Status: measured on the HD 4600 only (W9QI5A83, i5-4570, Win98 SE, boots
107-109, with the aperture write-combined since boot 106). One change
ships: draw batches no longer send the render-target flush before their
closing CC pointer and RT flush. The measurement switch stays.
Evidence: [`../probe/haswell-flush-variants-2026-10-28/`](../probe/haswell-flush-variants-2026-10-28/).
Plan: [intel-haswell-batch-cost.md](../plans/intel-haswell-batch-cost.md),
section 2026-10-28. Gates: `build-host`, `run-checks`.

## Why

With the aperture write-combined
([record](2026-10-28-haswell-pat-write-combining.md)), the CPU waits on
the render engine for 31 of a 3DMark 2000 batch's 58 us, and DB3D's
one-triangle state-change batches put the GPU's fixed cost a batch at
about 10 us. Every Haswell draw batch carries seven PIPE_CONTROLs, six with
CS stall, and re-emits the whole pipeline. The plan asked which of those
the 10 us is, measured before anything changes.

## The tool

- `V9XTRACE -omit:<mask>` sends `V9X_DDFLUSHVARIANT`; the 16-bit driver
  stores the mask in a new last field of the shared block
  (`hsw_flush_omit`, after `identity`, so the trace snapshot DB3D
  size-checks is unchanged), zero at every boot.
- `d3d_hsw.c` hands it to the batch builder before every batch
  (`v9x_intel_hsw_set_omit`); `hsw_3d.c` leaves out, per bit
  (`V9X_HSW_OMIT_*`, `intel_hsw_3d.h`): ICACHE and TEXTURE (two cache
  invalidates of the leading PIPE_CONTROL), DEPTH (the depth stall, depth
  cache flush, depth stall around the depth buffer), RT2 (the closing CC
  pointer and RT flush), ALLOC (the push-constant and URB allocations),
  LEAD (the first scoreboard stall). The init workload ignores it; no bit
  removes the scoreboard stall or the breadcrumb that slot reuse waits on.
  Host tests check each bit removes exactly its packets.
- Each run: arm, DB3D's StateChange, BatchCost, TriBatch, FillFlat and
  Clear families with the HAL's buckets (dispbench `db3d-hal-split`
  f2dd1cd), then V9XD32 /ddi6.

## Round 1, boot 107

HAL microseconds a batch (EngineDraw / RenderDrain) and DB3D's figure:

| omit | Texture.Change.1 | Blend.Change.1 | Depth.Change.1 | TriBatch.16 cyc/tri | TriBatch.100 cyc/tri | DB3D | V9XD32 |
|---|---|---|---|---|---|---|---|
| 0 | 12.0 / 9.1 | 11.0 / 8.2 | 12.3 / 9.4 | 854 | 637 | ok | PASS |
| 1 ICACHE | 12.0 / 9.1 | 11.0 / 8.2 | 12.2 / 9.3 | 853 | 637 | ok | PASS |
| 2 TEXTURE | 11.6 / 8.8 | 11.1 / 8.2 | 12.4 / 9.3 | 855 | 637 | ok | PASS |
| 4 DEPTH | 12.2 / 9.4 | 11.0 / 8.2 | 11.1 / 8.3 | 854 | 637 | **died** in TexUpload.Lock | PASS |
| 8 RT2 | 11.3 / 8.3 | 10.0 / 7.2 | 11.3 / 8.4 | 804 | 614 | ok | PASS |
| 16 ALLOC | 11.9 / 9.1 | 10.8 / 8.1 | 12.1 / 9.3 | 847 | 636 | ok | PASS |
| 32 LEAD | 11.9 / 8.9 | 10.6 / 7.7 | 12.3 / 9.1 | 834 | 625 | died after its last test | PASS |
| 63 all | 9.9 / 7.1 | 9.4 / 6.0 | 9.3 / 5.9 | 765 | 580 | **died** in TexUpload.Lock | PASS |

Every timed row that ran verified. ALLOC was run alone after the others
(Michael's call), as the one most likely to wedge the engine; nothing
wedged.

## Round 2, boot 108

RT2 removes the end Vol 2b's MI_BATCH_BUFFER_START note asks of a 3D
batch (CC pointers, then an RT-flush CS-stall PIPE_CONTROL), which is why
it is not shipped. The batch also flushed the render target once before
that pair; a seventh bit, RT1, left that one out instead:

| omit | Texture.Change.1 | Blend.Change.1 | Depth.Change.1 | TriBatch.16 cyc/tri | TriBatch.100 cyc/tri | DB3D | V9XD32 |
|---|---|---|---|---|---|---|---|
| 0 | 12.3 / 9.4 | 11.0 / 8.2 | 12.2 / 9.3 | 859 | 642 | ok | PASS |
| 64 RT1 | 11.7 / 8.7 | 10.4 / 7.6 | 11.2 / 8.3 | 817 | 614 | ok | PASS |
| 8 RT2 | 11.0 / 8.1 | 10.0 / 7.2 | 11.5 / 8.6 | 810 | 606 | ok | PASS |

With RT1 armed: V9XD32 /ddi6 and /ddi5, V9XGLP, V9XGLIDP pass, V9XR3DP
COMPLETE; 3DMark 2000 3711 (3719 on boot 106 without it), 57.6 us a
batch (58.1), 30.5 waiting for a slot (31.3).

## Boot 109: the shipped batch

The draw batch without its first RT flush, mask 0, the whole intel-gma
set deployed by WININIT rename and checked by SHA-256:

| | Boot 109 |
|---|---|
| Texture / Blend / Depth .Change.1 | 11.5 / 8.6, 10.5 / 7.5, 11.2 / 8.3 |
| TriBatch.16, .100 cyc/tri | 816, 613 |
| V9XD32 /ddi6, /ddi5 | PASS, PASS |
| V9XGLP, V9XGLIDP, V9XR3DP | PASS, PASS, COMPLETE |
| 3DMark 2000 | 3731; 57.4 us a batch, 30.5 waiting for a slot; 14,237 flips, 0 declined |

## Readings

- **The flushes are not the 10 us.** Every bit together takes about 2 us
  off a one-triangle batch (12.0 to 9.9) and 9% off TriBatch.100. ICACHE,
  TEXTURE and ALLOC are within noise: re-emitting the allocations and
  invalidating those caches costs the 4570 nothing measurable. The rest of
  a batch's fixed cost is its closing scoreboard stall and breadcrumb,
  which every slot's reuse rests on, and the GPU running a batch it has
  to drain before the next can start.
- **In 3DMark it is a percent.** RT1 moved a 58 us batch by half a
  microsecond; the score did not move outside one run's noise. 3DMark's
  batches spend their GPU time drawing, so the next lever there is fewer,
  larger batches (drawing more per batch), not a cheaper batch.
- **RT1 ships, for draw batches** (`hsw_3d.c`): about 5% off DB3D's small
  batches on every shape, every check passing, and the batch still ends
  as Vol 2b asks. The init batch keeps the flush Phase C ran
  ([record](2026-10-17-haswell-phase-c-render-ring-init.md)), which named
  only the end pair as the PRM's requirement.
- **DEPTH is not safe**: DB3D died in TexUpload.Lock under it and under
  all bits together, both times after every Fill, State and Batch row had
  verified. Each time run-bench's capture showed the desktop intact and
  DB3D gone, and V9XD32 then passed (the captures are not kept: they show
  the wireless adapter's settings window). Vol 2b's depth stall, flush, stall before a depth buffer
  change stays.
- **LEAD's death was after DB3D's last test**, with no `[Result]`: the
  teardown death seen on the netbook and the Rage 128 without any variant,
  and on boot 106 in DBGDI. Not counted against it; not shipped either, at
  0.4 us.

## Not checked

- Ivy Bridge shares `hsw_3d.c` and takes RT1's change; W9QI4E14 has not
  run it (its board's IGD read FFFFFFFF on 2026-10-22).
- A game with the change: Half-Life and Quake 3 were not run.
- Each variant is one DB3D run; differences under about 0.3 us are noise
  (mask 0's two runs differ by that much).
