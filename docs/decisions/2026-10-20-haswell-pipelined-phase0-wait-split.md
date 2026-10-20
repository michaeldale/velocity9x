# HD 4600: the wait before a draw's state write never stalls; the 75 µs is all the wait after the submit

Date: 2026-10-20. Machine: the i5-4570, boot 36, build `a94cb112-dirty`
(driver and HAL agree), installed by WININIT rename. Phase 0 of
`docs/plans/intel-haswell-pipelined-submission.md`. Evidence:
[`../probe/hsw-4570-pipelined-phase0-2026-10-20/`](../probe/hsw-4570-pipelined-phase0-2026-10-20/)
(`V9XSNA7`, taken after the run with 3DMark still open).

## The instrument

`d3d_hsw.c` counts the wait a draw makes before it writes the reserve,
both at the start of a draw and between its pieces, apart from the wait
after its submit. Gen3's counters carry it, unused on Haswell until now:

- `RenderDrainWaits`: these waits.
- `RenderDrainStalls`: those that found the render engine busy.
- `BreadcrumbLagPollsTotal` and `BreadcrumbLagPollsMax`: the status
  reads those stalls took.

Nothing else changed.

## The run

3DMark2000 v1.1 default benchmark, as in
`2026-10-20-haswell-3dmark2000-baseline.md`: 1024x768x16, triple
buffered, Z16. Score 1994, recorded and not compared.

| | |
|---|---|
| batches (`R3dListSinkBatches`) | 1,534,574 |
| waits before the state write (`RenderDrainWaits`) | 1,534,574 |
| of them found the engine busy (`RenderDrainStalls`) | **0** |
| polls in those stalls | 0 |
| all render waits (`TimeRenderDrain`) | 3,083,827 calls, 116.4 s |
| so the waits after a submit (including depth fills) | 1,549,253, about **75.1 µs** each |
| engine draw per batch | 157.0 µs |
| vertex copy per batch | 60.3 µs |
| state write / kernel build | 6.7 / 4.3 µs |
| engine timeouts, resets, refused batches | 0, 0, 0 |

The plan predicted "under 1% of pre-write waits stall, and the
post-submit wait is about 75 µs". Both hold. The pre-write wait's own
cost is one uncached status read, so it is inside the 75.1 µs figure
and does not change it.

So nearly half of every batch is the CPU waiting for the GPU to run the
batch it just submitted, and nothing else is waiting on it. The
ceiling in the plan stands: roughly 158 to 85-95 µs a batch if the next
draw is built while this one runs. Phase 1 (the pure module and its
tests) proceeds.
