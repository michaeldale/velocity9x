# Phase 5's sequencer, its capture, and the submit path that is not there

**Date:** 2026-09-15
**Status:** Step 6 of `docs/plans/intel-gma950-phase5.md` Part 2, **partially
complete**. The unarmed path is finished and is what B1 needs. **The armed path
cannot submit**, and refuses rather than half-running. **Nothing has been
booted.**

## What is finished

`src/display16/intel_3d16.c`, behind its own positive guard
`V9X_I9XX_PHASE5_EXECUTOR`, separate from Phase 4's so Phase 5 can be built
dark. It is wired into the boot path immediately after Phase 4, in the same
boot, reading the result Phase 4 just recorded.

**The unarmed path is complete.** It writes nothing and produces a full
`INTEL3D0.TXT`:

- the whole 59-dword stream, one dword per key, plus a packet offset/length
  index derived from the builders' own extents so a reader can find the drawing
  rectangle without counting;
- the vertices **twice**, as raw bits and as decoded integers - deliberately
  redundant, so a float-transport fault shows in the artefact rather than as a
  wrong picture;
- two read-only hash passes over the untouched target, both published and not
  compared, plus two `V9xGmadrRead` samples to prove the address path;
- the reserve-backing operands re-derived this boot, and the in-reserve guards.

That is exactly what B1 is for: a decoder error, an unstable mapping or a dead
address path is caught there and costs no armed boot.

The armed path's fill, hashing, 480 row CRCs, fourteen named pixel probes and
`HeapProbe` are written and compile, but are unreachable - see below.

## What is not finished, and why it refuses instead

**The mini-VDD cannot stage or submit a Phase 5 stream.** Staging arms 20-24
belong inside `V9xMini_I9xx_Ring_Stage`, with a second table and a second
counter so a Phase 4 dword can never land in a Phase 5 slot. The plan assigned
that to step 5 and step 5 did not deliver it; this record is where that is
stated rather than left to be discovered.

So the armed path **refuses before it writes anything**, with
`V9X_P5_PRE_SUBMIT_MISSING`. The alternative - filling the target and stopping -
would consume the one-shot token, write 600 KiB and prove nothing: an armed
boot spent producing a uniform rectangle. Refusing costs nothing and says
exactly why.

Steps 24-27 are numbered and deliberately unused, so the step namespace does
not shift when submit lands and captures from before and after stay comparable.

`check-tree.ps1` asserts `V9X_I9XX_PHASE5_SUBMIT` is **not** defined anywhere.
That is what stops it being added alongside the executor guard it resembles,
without the staging work that has to come first.

## Two traps this step walked into

**`GetTickCount` is a USER import.** The plan asks for the fill's duration. The
only Win16 tick source is `GetTickCount`, which lives in USER - and GDI loads a
display driver before USER exists, so one import makes the module unloadable
with no diagnostic anywhere. That cost three netbook boots to find once
already, and `audit-family-binary.ps1` refuses it for exactly that reason. It
refused this. No duration is recorded; the fill's correctness is established by
the hash either side of it, which is what actually matters.

**`__U4M`, for the fourth time.** The row loop and the pixel-probe offsets both
multiplied at run time. The row loop now accumulates a base; the probes use a
local shift-add helper. Four times now `wlink` E2052 has caught a runtime-helper
reference that no grep would have found, which is worth recording as a pattern
rather than as four incidents.

## The capture's deliberate asymmetries

**The two hash passes are published and never compared by the driver.** An
unstable read must reach the artefact as two different numbers. The validator
compares them; the driver does not, because collapsing it in the driver would
discard the evidence before it was written down.

**`HeapProbe` is read-only and is an observation, not an assertion.** The dword
below the reserve is still published heap. It is recorded before and after, and
the validator *reports* a change rather than failing on it, because nothing
proves the heap was quiescent or ours for the interval. Writable guard patterns
go only inside the reserve, where every byte is ours - and a change to one of
those **is** a kill, which the validator does enforce.

**The pixel probes avoid the triangle's edges.** The plan licenses a one-pixel
band along an edge to disagree with the software reference, because the fill
rule is not something the packet audit established. Sampling an edge would
manufacture a failure that was already excused.

## The validator and the armer

`scripts/check-intel-3d-capture.ps1` checks the capture against the generated
stream table: every stream dword, the CRC, the decoded vertices against the
geometry, the layout, and the reserve-backing arithmetic. `-SelfTest` builds a
clean fixture and rejects eight targeted mutations.

`scripts/arm-intel-phase5.ps1` is a **separate script** from Phase 4's, because
one armer that could arm either phase is a flag away from arming the wrong one.
It takes the combined CRC from the generated table rather than a keyboard, and
it **refuses to arm at all** until a Phase 5 errata-gate decision exists in
`docs/decisions`. Its self-test currently reports "errata gate refuses (no
decision on record)", which is correct: the 2026-09-13 decision covers Phase 4
only, on an argument its own record says does not transfer to a heavier
workload. That decision has to be taken, not inherited.

Both are wired into `run-checks.ps1`.

## Gates run

- `check-tree.ps1` — pass, with `intel_3d16.c` declared as an OS boundary and
  the executor-pairing rule extended to Phase 5.
- `build-win16-ddi-skeleton.ps1 -Family intel-gma` and its audit — pass.
  `_TEXT` 41,061 and `I9XXCODE` 31,121 against a 57,344 budget.
- `build-host.ps1`, `build-host-msvc.ps1`, `run-checks.ps1` — pass.
- The capture validator and the Phase 5 armer self-tests both pass; the
  `V9X_I9XX_PHASE5_SUBMIT` assertion was verified to refuse.

## What remains before B1

Nothing, for the unarmed boot: B1 can run against this. What it will exercise
is the layout move, the heap shrink, the 1 MiB mapping, two read-only v7 hash
passes, and both stream plans.

Before an **armed** boot, three things that are not code:

1. the mini-VDD staging arms, so Phase 5 can submit at all;
2. the Phase 5 errata-gate decision, which the armer refuses without;
3. re-arming the stick, which step 2's CRC change invalidated.

And the netbook regression boot for Part 1 has still never run.
