# intel89: the plane base readback is the pending value, and the flip window leaks

2026-09-19, MICHAEL-NETBOOK (945GSE, 8086:27AE), build `514161b-dirty`,
640x480x16 game on a 1024x768x16 desktop, pipe B at 576 active lines in a
672-line frame. Attached: `2026-09-19-intel89-V9XSNAP.txt`.

The first boot to carry the three readings added on 2026-09-19 after the
i915 research.

## The base readback is the PENDING value. intel73 is dead.

```
FlipIssueLineMin=32   FlipIssueLineMax=660   FlipIssueVactive=576
FlipBaseImmediate=578   FlipBaseDeferred=0
```

578 flips, and at every one of them the plane base register read back the
new offset immediately - at scanlines scattered from 32 to 660, which is
right across active video and on into the blank.

A register holding the ACTIVE value cannot report a new offset at line 32
of a 576-line active area, because the latch has not happened yet. So the
readback is the pending copy, and this is now measured rather than
inferred.

Three consequences.

**intel73's "applies at once" is withdrawn.** It read the pending register
and concluded the hardware applies the base immediately. It does not; the
register simply reports what was written. Every reading taken from
`FlipBaseImmediate` and `FlipTakenAtDone` describes a register that never
reported the scanout, and the latch model in `i9xx_scanout.c` - which said
exactly this from the video record - is confirmed.

**i915's stall check does not transfer.** Upstream detects a stalled flip by
asking whether the plane address register has reached the expected offset.
On this part that is true the instant the write lands, so the check would
report every flip complete immediately. It is a safety net against a flip
that never executes, not a completion signal, and it cannot be borrowed as
one. The contradiction raised against our model is resolved in our favour.

**The flip-pending ISR bit is not what we assumed either.** `Ecoskpd`
reads `0x00000307`, so bit 0, `ECO_FLIP_DONE`, is SET. On Gen3 with that
bit set, i915 takes the flip-pending interrupt to mean the flip is DONE
rather than queued. This driver has spent intel71, intel72 and intel86
looking for the bit to indicate PENDING. It never appears at all -
`IsrAfterFlipOr` and `IsrBeforeFlipOr` are both zero across 578 flips, with
`FlipRingPendingSeen=0` - so the semantics do not by themselves explain the
silence, but the search was looking for the wrong event.

## The active-video window is what throttles presents

```
CountFlip=46183   FlipHandled=558
FlipStillDrawing=0   FlipWindowClosed=45625   FlipDeclined=0
```

This answers the question intel86 could not. There, 52,608 of 53,193 Flips
were refused and one counter carried both reasons, so nothing could say
which. Split on 2026-09-19: **45,625 of the 45,625 refusals are the beam
being outside the issue window**, and not one is the previous flip being
untaken. Presents are throttled by the window test alone.

## And the window leaks

`v9x_scanout_flip_window_open` requires `line < vactive - 8`, so 568 or
less at the moment of the test. The line at the moment the flip is actually
issued runs to **660** - eighty-four lines INTO the blank, past the latch
point the window exists to stay clear of.

So between the test passing and the write landing, up to a hundred-odd
lines elapse - two to three milliseconds - and a flip aimed at active video
arrives inside the blank instead. That is exactly the race the guard was
written to prevent, happening despite it, and it is measured rather than
supposed.

The gap is the work done between the two: the ring flip is built and
submitted, and the submit waits. Whatever the cause, testing the window and
then doing unbounded work before the write is not a guard.

## What to do

Re-read the window immediately before the write and abandon the flip if the
beam has moved out of it, rather than testing once and hoping. That is a
small change to `v9x_flip_body` and it is the first Intel-side lead in some
time that is both measured and actionable.

It is not obviously the flicker - intel86 closed the completion and
ownership models on this part, and the S3 card shows the same symptom with
a completely different present path - but it is a defect, it is the guard
failing at its one job, and the counters now exist to tell whether fixing
it changes anything.

## Unchanged

```
HwsSelfTest=1  HwsSelfTestPolls=0  BreadcrumbSubmits=550088  Timeouts=0
RenderDrainWaits=0  DrawsToFront=0  DrawsToBack=550088  DrawsFlipWaited=0
```

The completion channel still works and still reports that nothing was ever
outstanding; no batch landed in the buffer the base register named, with
the caveat now sharpened that the register names the PENDING buffer, so
`DrawsToFront` is blind for the frame between the write and the latch -
which is the whole window in question.
