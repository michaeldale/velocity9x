# dispbench as the instrument mode 3 needs, and the four other products proposed alongside it

Status: planning, 2026-08-30. Nothing here is built. This is a Velocity9x-side
scoping document for work that would happen in `C:\everything\dispbench`, a
separate repository under a different licence; if it is taken up, the plan of
record moves there and this file becomes the pointer.

## Why this exists now

`DDBLT_DEPTHFILL` was mode 3's first deliverable. It works, it is
pixel-verified, and it produced a result nobody can explain: the same change
gains 22.6% on Robots and 36.2% on City scene, loses 37.8% on Fill rate, and
leaves Final Reality's composite flat
([decision](../decisions/2026-08-30-ddblt-depthfill.md)).

Getting even that far cost three full 14-minute benchmark runs, a hand-built
control HAL from a reverted worktree, and a callback-counting trick across two
builds to establish which code path was serving the call. Final Reality returns
four composite numbers. It cannot separate "the clear is slow" from "the clear
interferes with queued 3D work", and it never will, because that is not what it
is for.

**Every remaining question in mode 3 has that shape.** Is a per-span blit
cheaper than a CPU loop, and above what span width? Does hardware colour
expansion pay? Does a screen-to-screen present beat the CPU? The plan already
warns that guessing here is "the same class of mistake as assuming a FIFO could
supply 18 slots when it reports 16" - and the depth fill has now demonstrated
that warning was correct in a way nobody predicted.

So mode 3 is blocked on an instrument. That is the argument for doing dispbench
work first. It is not an argument for doing all of the work below.

## What dispbench already is

A generic Windows 9x display-driver benchmark and auditor, BSD-2-Clause,
deliberately vendor-neutral: it measures what any Win9x driver from any vendor
actually does, using only public APIs. Stage 0 (skeleton, host libraries,
gates, fixtures) and stage 1 (`DBCAPS.EXE`, the capability inventory) are
built. Stages 2 to 7 are not.

Its `PLAN.md` §9 stages the rest. Two of them are exactly what mode 3 needs:

- **Stage 2, `DBTIME.EXE`** - the timer core and CPU/memory references alone.
  Its own plan calls this "the highest-risk unknown, isolated into one small
  binary", and says not to proceed until calibration agrees to <0.1% across
  runs. **If 86Box's TSC is unusable, that is discovered here** - which matters
  enormously, because every number in the depth-fill decision came from 86Box.
- **Stage 3, `DBGDI.EXE`** - six operations with a size sweep, plus alignment
  and overlap probes.

A per-operation size sweep is precisely the missing instrument. "Above what
span width does a blit beat a CPU loop" *is* a size sweep.

## The four proposals, separated

The suggestion bundles five things. They have different costs, different risks,
and three of them are separable. Taking them as one project is how a
measurement tool becomes a platform and never ships.

### 1. Per-operation timing for mode 3 — the one that pays for itself

dispbench stages 2 and 3, scoped to the operations mode 3 turns on. This is the
only item that unblocks current Velocity9x work, and the depth-fill episode is
the receipt.

It also needs one thing dispbench does not currently have and Velocity9x does:
**a way to count what the driver did, not just how long it took.** The
`CountBlt` / `CountBltEngine` pair settled in one run what the pixel test could
not settle at all - whether the driver served a call, and whether the engine or
the CPU fallback did it. That is a Velocity9x escape (`DDGETTRACE`), so it
cannot go in a vendor-neutral tool. The clean shape is that dispbench measures
time and Velocity9x's own probe reports counters, correlated by the host-side
scripts. Worth stating explicitly, because the temptation to put a driver
escape into dispbench is exactly what its §10.5 forbids.

### 2. Rolling the survey in — cheap, but it is a merge of two execution models

`V9XSURV.EXE` is a **real-mode DOS** program. dispbench is **Win32**. They
cannot become one binary; at best the project ships both, and the DOS half
keeps its own toolchain, its own safety gate and its own report schema.

Three things to settle before moving any code:

- **Licence.** The survey is Velocity9x code under GPL-3.0. dispbench is
  BSD-2-Clause, and its licence decision rests on a stated property: "It shares
  no code with Velocity9x - it only follows the same conventions - so there is
  no derivative-work question." Moving the survey in makes that sentence false.
  The copyright holder can relicense his own code, but it has to be a deliberate
  act recorded in a decision doc, and the survey's shared headers
  (`diagpaths.h` and friends) come with it or have to be severed.
- **The out-of-scope rule.** dispbench §10.5 forbids "no driver hooking, no
  `DCICOMMAND` escapes, no MMIO peeking", on the grounds that public APIs only
  is what makes it run against any driver. A DOS survey reading PCI config
  space, the video BIOS and the VGA register file is *more* vendor-neutral than
  the Win32 half, not less - it touches no driver at all. So the rule needs
  restating rather than breaking: the thing that must stay out is **privileged
  access to a running driver**, not privileged access to hardware in an
  environment where no driver is running. That distinction is worth writing
  down properly, because it is the whole boundary.
- **What is actually gained.** The survey works today and ships in every
  release. The gain is one place to send a stranger, not new capability. That
  is a real benefit for proposal 3 and close to zero benefit on its own.

### 3. Crowdsourced reports — a different product with a different threat model

"People run it and report back" changes what the tool is. Four things that are
not engineering:

- **Safety.** A tool strangers run unattended on hardware nobody here owns. The
  survey already has the right instinct - a source safety gate with twelve
  mutation tests, and a null-pointer defect that a third party's NAV50 found the
  hard way in 0.6.1. Any crowdsourced binary inherits that bar, and the Win32
  benchmark half has no equivalent gate today.
- **Privacy.** Reports carry machine names, paths, adapter strings and
  potentially EDID serial numbers. A tool that asks strangers to send files back
  needs to say what it collects, and ideally to collect less than it can.
- **A submission channel and a schema that survives drift.** dispbench §10.4
  says "no results database". Crowdsourcing is a results database. That is a
  reversal of a stated decision, not an extension of it, and deserves its own
  record.
- **Support load.** Every report is a conversation. 0.6.1 shows what one
  third-party machine produced: four defects and a fortnight.

None of that argues against it. It argues that it is its own project, gated on
1 and 2 existing and being trustworthy.

### 4. Remote-agent integration — nearly free, and already licence-compatible

The agent (`v9x-remote-agent`, BSD-2) already drives Velocity9x guests: push,
exec, screenshot, reboot, wait-desktop. dispbench under BSD-2 is compatible,
and this session drove three guests and Final Reality entirely through it. This
is the lowest-cost item and mostly a matter of scripting conventions rather than
new code. Do it as part of 1; it is how 1 gets run repeatedly without a human.

### 5. Debugging other people's drivers — a consequence, not a goal

dispbench is already designed to do this: vendor-neutral, public APIs, and its
headline output is "distinguishing genuine hardware acceleration from a
well-optimised CPU fallback, and flagging every capability bit a driver claims
but does not deliver". Nothing needs planning. It falls out of 1 and 3.

## Recommendation

**Do 1, with 4 folded into it. Defer 2 and 3. Do nothing for 5.**

That is dispbench stages 2 and 3, scoped to the operations mode 3 depends on,
driven through the remote agent. It is the only part that unblocks work in
progress, and it is the part whose absence has already cost measurable time
twice - once on the depth fill, once on the Final Reality re-runs that could
not answer the question they were run for.

Deliberately not "a full update to dispbench". Stages 5 to 7 are completeness,
and stage 7 (`DB3D.EXE`) is described in its own plan as "least relevant to the
current decision, most likely to hang a machine".

Two things that would change this recommendation, both cheap to discover:

- **If dispbench stage 2 finds 86Box's TSC unusable**, then no amount of
  instrument-building helps under emulation, and mode 3 needs BARRY or physical
  silicon for every performance claim. That would be a large finding and it is
  the first thing stage 2 tests.
- **If the fill-rate anomaly turns out to be an 86Box artefact** - which
  published per-cycle rates already suggest, since both measured figures exceed
  the real part's non-textured ceiling - then mode 3's design questions may not
  be answerable under emulation at all, and the instrument has to run on BARRY.

## What this document does not decide

The licence question in proposal 2, the database and privacy questions in
proposal 3, and whether the survey's DOS half stays in Velocity9x. Those are
decisions for `dispbench`'s own `docs/decisions/`, and this file should not be
read as having pre-empted them.
