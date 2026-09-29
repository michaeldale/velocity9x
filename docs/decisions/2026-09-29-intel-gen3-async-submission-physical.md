# Gen3 asynchronous submission fails the physical correctness gate

Date: 2026-09-29. Machine: MICHAEL-NETBOOK, 945GSE / GMA 950
`8086:27AE` revision 03, Windows 98 SE. Build `8b58304-dirty`.

Decision: keep synchronous submission as the only normal setting.
`IntelAsyncSubmit` remains exact-opt-in and absent-key off for diagnosis, but
it is not accepted for gameplay, benchmarking or a future default.

The synchronous control on boot 50 completed the broad probe in 4.966 seconds
and a visually correct 3DMark 99 Max default project at 1024x576x16. The score
was 759 3DMarks / 16490 CPU 3DMarks. Its post-probe snapshot reports
`AsyncEnabled=0`, 606 synchronous submits and no engine, ring-space or
breadcrumb failure.

Boot 51 changed only the retained key to `IntelAsyncSubmit=1`. The same probe
completed in 5.313 seconds. All nonvolatile probe results matched the control
apart from a sampled vblank bit. The snapshot proves the branch ran: 580
asynchronous submits and only 26 synchronous submits, with zero ring-space
waits/timeouts, engine timeouts/resets and breadcrumb abandonments.

The following identical 3DMark run produced severe visible corruption. The
operator's three photographs show stale horizontal bands, displaced geometry
and large blank or stale rectangles. This is a correctness failure even
though the preceding diagnostic counters were clean. The benchmark was
aborted under the plan's stop rule; no async score or speed claim is accepted.

Boot 52 was explicitly returned to `IntelAsyncSubmit=0`. The desktop came
back and the broad probe completed in 5.394 seconds. Its snapshot confirms
`AsyncEnabled=0`, zero async submits, 606 synchronous submits and no failure
counters. The machine was left in that safe state.

Evidence and photographs are in
`../probe/intel-gma950-async-2026-09-29/`. The reproducible defect and the
remaining hypotheses are filed in
`../issues/2026-09-29-intel-gen3-async-corrupts-3dmark99.md`.

The Phase 5 Half-Life performance gate cannot be claimed. This Half-Life GOTY
installation has no shipped `hldemo1.dem`; the developer console capture
records the failed open. No interactive result is substituted for a paired
timedemo.
