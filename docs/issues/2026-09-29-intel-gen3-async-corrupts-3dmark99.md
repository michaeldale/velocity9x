# Gen3 asynchronous submission corrupts sustained 3DMark 99 frames

Date: 2026-09-29. Machine: MICHAEL-NETBOOK, 945GSE / GMA 950
`8086:27AE` revision 03, Windows 98 SE. Candidate build `8b58304-dirty`.
Evidence: `../probe/intel-gma950-async-2026-09-29/`.

## Reproduction

Boot 50 used the candidate binaries with `IntelAsyncSubmit=0`. The broad
DirectDraw/Direct3D probe completed in 4.966 seconds. 3DMark 99 Max then ran
the default project at 1024x576x16, 16-bit Z and triple buffering. It was
visually correct and scored 759 3DMarks / 16490 CPU 3DMarks.

Boot 51 changed only `IntelAsyncSubmit` to `1`. The same probe completed in
5.313 seconds. Its nonvolatile results match the control; part one differs
only in the instantaneously sampled `VBlankStatus`. The snapshot reports 580
asynchronous submits, 26 synchronous submits, no ring-space wait or timeout,
no engine timeout or reset, and no breadcrumb abandonment.

The following identical 3DMark run visibly corrupted before completion:

- broad horizontal strips contain displaced or stale parts of the frame;
- geometry is repeated and shifted between strips; and
- large white, black or stale rectangles replace rendered regions.

The three operator photographs preserve each form of corruption. The run was
aborted immediately and no score is accepted.

The switch was rewritten to `IntelAsyncSubmit=0` and the machine rebooted.
Boot 52 reached the desktop and the broad probe completed in 5.394 seconds.
The final snapshot reports `AsyncEnabled=0`, `AsyncSubmits=0`, 606 synchronous
submits and zero timeout, reset or abandonment counters.

## What this establishes

The small probe is not a sufficient correctness gate for queued Gen3 work.
Neither the breadcrumb failure counters nor the ring-space counters detect
this corruption. In-order ring execution and a drain at the audited public
Flip/Lock/Blt/destroy boundaries are not, by themselves, a complete lifetime
or visibility contract for this workload.

The photographs resemble command, resource or target reuse before the GPU is
finished with it, but they do not distinguish those causes. In particular,
this evidence does not prove that the ring-space arithmetic is wrong, that a
texture upload lacks a drain, or that the render drain missed a Flip. Those
remain hypotheses for an isolated queue-depth experiment.

## Standing

Asynchronous submission is rejected for normal use and remains absent-key
off. `V9X3D FLIP` is the recovery setting. Do not use the async path for a
performance comparison until the first corrupt frame is reduced to an
isolated reproducer with an instrumented in-flight depth.

The planned Half-Life timedemo comparison was not performed: the installed
GOTY copy reports `ERROR: couldn't open ...\valve\hldemo1.dem`. Inventing an
interactive FPS comparison would not satisfy the paired-timedemo gate.

## Resolved 2026-10-01

The corruption was refused draws, not memory reuse. A busy ring's head
stops on any dword; `v9x_i9xx_ring_free_space` refused one that was not
qword aligned, so the submit failed and the core dropped the batch. With
the new counters, Half-Life on the unfixed code refused 39,080 ring plans
and dropped 38,897 draws (last head 0xF424); with the fix, none, and the
operator saw Half-Life and 3DMark 99 at 1024x576 correct. Asynchronous
submission is now the default. See
[the record](../decisions/2026-10-01-intel-async-corruption-was-the-dword-head.md).
