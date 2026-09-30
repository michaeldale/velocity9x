# Gateway fixed-driver validation, 2026-09-30

Gateway 10.0.1.22 reconnected on boot 70 after one driver-install reboot.
DesktopReady was verified before testing. The installed HAL was read back
and its SHA256 matched the final fixed candidate:
622DD5424FFC923450D06A84B36D2F7044758EA5BE8E63A18C4E93DDB9C89EA8.

V9XTRACE -dd completed successfully. The retained boot70-pre-probe.ini is
its snapshot. The existing dp-record-merge-ati/V9XDDP.EXE (95,744 bytes)
was then launched detached. After 20 seconds the first report read failed
because the remote agent no longer responded. A further 45-second wait
and repeat check failed; ICMP still responded. The user confirmed Windows
was frozen and began a hard reset. No broad-probe completion, post-probe
snapshot, or clipped-fan result was obtained on boot 70. The targeted probe
had not been launched. This is a failed broad run, with cause unresolved.

After the user hard reset, boot 71 retained the same verified fixed HAL.
The first targeted invocation exited with FAIL-SHARED-OR-ENGINE: its argument
parser scanned the executable path and mistook dp-record-merge-ati for the
-refusal switch. No geometry was submitted by that invocation. The parser
now skips the executable token and matches the complete -refusal argument.
The rebuilt probe is gateway-clip-args-fix, 39,424 bytes, CRC32 C9F2C02F.

The corrected targeted test passes all three cases with zero mismatches.
List-call / sink-batch counts are 1/1, 1/1 and 2/3, with three clipped
triangles per case. Reversing overlap order changes 1,657 pixels. No new
refusals, FIFO/idle timeouts or resets occur. All six PPMs match each other
and the earlier candidate/control images exactly (CRC32 FB32474C).
The post-test snapshot and final ping confirm the host remains responsive.
The fixed HAL remains installed on boot 71.

The broad report files were recovered after reboot. V9XDD.INI ends after
ExclusiveVBlankHr, before completion. V9XDD2.INI includes binary damage and
older rolled data; it cannot be treated as results for this run. Neither
file establishes a completed broad probe or a specific lock cause.

## Continued intermittent-lock investigation

The earlier stop after targeted validation was premature. Investigation
resumed with durable checkpoints before/after SetDisplayMode and an isolated
/modeonly option in the broad probe (gateway-mode-boundaries build).
On boot 71 the isolated 640x480x16 switch and desktop restore both succeeded.
The subsequent full checkpointed probe completed with exactly 202 passing
and 14 existing failing boolean checks, all keys equal to the preceding
successful candidate baseline. It added the same 38 Mach64 reason-3 policy
refusals, with zero FIFO timeouts, idle timeouts or resets.

A fresh boot 72 repeated the full checkpointed probe without a preceding
mode-only run. It also completed with identical 202/14 checks, 38 reason-3
refusals and zero timeouts/resets. The two successful instrumented runs do
not establish the original lock's cause: profile flushes can change timing.
An unmodified-probe fresh-boot repeat follows to check that qualification.

The unmodified packaged broad probe (b182f94-dirty, CRC32 ADA8449E)
completed on fresh boot 73. Every final boolean check again matches the
successful historical baseline: 202 passing, 14 existing failures, zero
changed or new check keys. The run adds 38 known reason-3 policy refusals,
zero FIFO/idle timeouts and zero resets. This excludes added profile flushes
as a requirement for successful completion, but does not exclude their
influence on the probability of a timing-sensitive failure.

There are now three completed fixed-HAL broad reruns (boots 71, 72, 73),
two from fresh boots, plus the targeted clipped-fan pass and isolated mode
switch/restore pass. The boot-70 confirmed hard lock is still an open
intermittent incident; there is no demonstrated root cause or driver fix
for it. The original packaged probe is restored on the host. The repository
probe retains durable mode boundaries and /modeonly for a recurrence.
The fixed HAL remains installed and the host is responsive on boot 73.

## Matched stress comparison

The user reported that the confirmed lock displayed a black screen.
The same-tree no-record-merge control HAL was installed and readback-verified
on boot 74 (SHA256 D45BBB26A343542FE2600168EC23F7687D4B84912AEE6769AF32E7F564D0D4D3).
Five original-probe runs completed. Each matches all baseline checks (202/14).
The third report was initially INCOMPLETE at the 15-second read, then reached
COMPLETE without intervention. The desktop and agent remained responsive.
This delayed completion was not a lock; later waits were lengthened to
30 seconds with no overlapping probe processes. Initial and late reports,
window enumeration, screen capture and counters are retained in stress-control/.
Five runs added 190 known reason-3 policy refusals and zero timeouts/resets.

The final fixed HAL was restored and readback-verified on boot 75. Five
matched original-probe runs also completed, each with all baseline checks
unchanged (202/14), 190 total known reason-3 refusals and zero timeouts/resets.
See stress-fixed/. These outcomes do not discriminate the intermittent lock
between merging enabled and disabled, because neither series reproduced it.

The diagnostic probe now provides /modestress: 32 round trips between
640x480x16 and the desktop, with no geometry submissions. ModeCycle and the
before/after switch/restore boundaries are flushed. On boot 75 it completed
all 32 cycles, adding no D3D context creates, refusals, timeouts or resets.
Its intermediate cycle-17 report is preserved as an active-run checkpoint,
not an incomplete failure. A further fresh-boot transition test follows.

On fresh boot 76, before any rendering probe, /modestress completed all 32
round trips. The final trace confirms D3dContextCreates=0, DpRecords=0,
DpRecordTriangles=0, M64Refused=0, and zero FIFO/idle timeouts or resets.
Thus two 32-cycle transition series completed (one after the fixed broad
series and one fresh-boot with no submitted record geometry). The host
remains responsive with the verified final fixed HAL installed on boot 76.
This provides 10 matched broad completions plus 64 isolated transition
cycles in this continuation; it does not reproduce or fix the black-screen
lock. The open incident is tracked in
[the issue record](../../../issues/2026-09-30-gateway-intermittent-black-screen-probe-lock.md).

The original unmodified V9XDDP.EXE remains on the host; the diagnostic
V9XDDM.EXE supports /modeonly and /modestress. It is gateway-mode-stress,
96,256 bytes, CRC32 5FA832C1. No driver code changes were made to address the
lock because no implicated failing path was established. Build, tree and
whitespace checks pass for the added diagnostic tooling.
