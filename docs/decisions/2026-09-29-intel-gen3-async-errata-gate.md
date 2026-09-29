# Intel Gen3 asynchronous submission is authorised for the controlled A/B run

Date: 2026-09-29
Status: accepted for the experiment. Decided by Michael Dale with "do the
plan", after the plan named this as the phase-0 authorization required before
an asynchronous hardware boot.
Plan: `docs/plans/intel-gen3-async-submission.md`

## Decision

Runtime Direct3D may overlap CPU writes to the ring and its CPU-owned status
page words with execution of earlier GPU work on exact device `8086:27AE`
revision 03, for the synchronous/asynchronous A/B run in the plan.

The conditions are the existing scratch installation, AC power, a recoverable
machine, and the DOS recovery switch. `V9X3D ASYNC` is the explicit opt-in;
absence, `V9X3D FLIP`, and `V9X3D ON` stay synchronous. `V9X3D OFF` disables
runtime 3D, the Intel flip and asynchronous submission.

## Why this needs its own decision

Intel erratum 12 in 309220-0132 describes an incorrect internal-buffer flush
for an unpublished sequence of processor and integrated-graphics memory
accesses, with no fix listed for A3. Synchronous runtime 3D lets the GPU finish
each submitted batch before the CPU builds the next one. Asynchronous
submission deliberately introduces CPU ring/status-page writes while the GPU
executes older commands. Neither the sustained-3D decision nor the later 2D
blit decision authorizes that new interleave automatically.

## Bounds

- The stream allowlist, trailing flush and breadcrumb remain unchanged.
- The status-page self-test remains synchronous.
- Every wait is bounded; no GPU reset path is added.
- Flip, Lock, CPU fallback, surface release and session reset drain the newest
  breadcrumb before display, CPU access or storage reuse.
- A ring-space timeout happens before the tail moves and may refuse safely.
- Any hang, abandonment, ring-space timeout, corruption or missing geometry
  ends the asynchronous run and returns the next boot to synchronous mode.

## What acceptance does not establish

It does not establish that erratum 12 cannot trigger, that asynchronous
submission is correct, or that it is faster. Those are the physical A/B run's
questions. Until that evidence is recorded, asynchronous submission remains
an exact-key opt-in and is not a release default.
