# Five mechanisms narrowed, and the metric that got there

**REVISED after review, same night.** The title and the conclusions below
claimed more than the measurements carry. None of the tested changes
demonstrated a flicker reduction, and the probes narrow the search without
closing any of the five mechanisms. Four specific over-readings are
corrected in the section at the end; read that first.

2026-09-19 night. A8U4I5 with the Trio3D/2X, Final Reality Robots on
hardware Direct3D, the panel captured at 1080p60 for every run. Five builds
compared.

## The metric, after two wrong ones

The flicker is ONE display frame far darker than the frames either side of
it. Two earlier comparisons in this session counted frames below a fraction
of a median taken over a window or over the whole run, and neither is a
measure of that:

- over a twelve-second window, the window's own scene content sets the
  threshold, and each run's window held different scenery. That comparison
  gave 23, 22, 28 and 41 and supported nothing.
- over the full benchmark, the run medians ranged from 66 to 107, so the
  threshold moved between runs and the counts ran to ~900 frames - dark
  SCENERY, not flicker. That comparison made one build look 38% better than
  baseline. It is withdrawn.

The measure used here is scene-independent: for each frame, the median of
the five frames before and the five after, and a dip is a frame below 75%
of that. `scratchpad\dip_local.py`.

It discriminates. The edge write of e93027f - already known bad from the
operator's report and reverted - comes out at four times the baseline rate
and visibly deeper, which is the check that the metric can see an effect
when there is one.

## The five runs

| build | isolated dips | rate | median depth |
|---|---|---|---|
| unfixed baseline | 69 | 1.09/s | 61% of neighbours |
| **edge write (reverted)** | **267** | **4.24/s** | **43%** |
| unblank fix | 90 | 1.45/s | 54% |
| idle confirm, 32 reads | 80 | 1.27/s | 58% |
| strict flip settle, 8192 reads | 93 | 1.50/s | 59% |

Baseline 69 against 80, 90 and 93. None of the three changes reduced the
flicker; against a metric that reports a known regression at 267, an
improvement of any size would have shown. Single runs each, so 69 against
93 is not itself a finding - what is a finding is that none of them moved
toward zero.

Also new, and not noticed in any earlier run: **the flicker is bursty.**
1.09/s across the whole benchmark against roughly 7/s in the 78-90 s window
examined this afternoon. It concentrates in parts of the scene rather than
running steadily, which is the third reason the windowed comparisons were
untrustworthy.

## What is now measured out on this card

Every mechanism in the driver's presentation path, each with its own
instrument:

1. **Premature flip completion.** The reuse probe paints the retired buffer
   green at delays of 0, 17, 34 and 100 ms; 120 paints, and the panel shows
   green in exactly one 0.32 s setup fill across 158 s. The display never
   fetches a buffer the driver has released.
2. **Buffer ownership.** `DrawsIntoPresented=0` over 141,322 batches; the
   present trace alternates 0 and 0x96000 cleanly, first draw after every
   flip aimed at the buffer not just presented.
3. **Draws racing a pending flip.** `VirgeDrawsFlipPending=0`,
   `DrawsFlipWaited=0`, even after completion was corrected to the end of
   the retrace. The runtime holds the application behind GetFlipStatus -
   about 150 asks per flip - so nothing can race it.
4. **Blt and Lock racing a pending flip.** `BltFlipPending=0` over 520 Blts
   and 308 Locks. The clear does not get in either.
5. **Unfinished drawing at the flip.** The idle bit DOES lie -
   `VirgeIdleFalseSettle` reads 42 and 45 in two passes - but never at the
   flip: an 8,192-read confirmation at all 256 flips broke zero times.
   Completion is honest at the one moment that decides what is shown.

And the panel still holds a buffer containing only the clear and the sky,
in bursts, on a card where every one of those reads clean.

## What was removed again

The strict flip settle is gone. It answered its question with a zero and
cost two million register reads a pass; a check that never fires is not a
default worth carrying. The cheap 32-read confirmation stays: it catches
forty-odd genuinely wrong settles a run, which is a correctness improvement
on its own terms whatever it does for the flicker, and it costs a window
that was already being paid.

The unblank completion stays on the documented-VGA-discipline grounds given
in its own record, not because it helps.

## What this document got wrong

**The Lock reading was never taken.** `blt_flip_pending` is sampled in
`v9x_blt_drain`, and `V9xHalLock` does not go through it - it calls the
engine and render waits directly. So the zero covered 520 Blts and not one
of the 308 Locks, and mechanism 4 below is a statement about Blts only.
The sample is now taken in Lock as well, and the reading has to be redone.

**The metric does not require an isolated frame.** `dip_local.py` flags any
frame below three quarters of its neighbours' median, so two adjacent dark
frames both count, and a rapid scene change can trigger it. It is
scene-normalised, not scene-independent, and calling the counts "isolated
dips" overstates what they are.

**"An improvement of any size would have shown" does not follow.** The
metric caught a four-times regression on one run per build against a fault
that is bursty. That establishes some sensitivity, not enough to exclude a
modest improvement.

**"Completion is honest" is stronger than the result.** The 8,192-read
window establishes that the idle bit did not change during that
observation, using the very signal whose reliability is in question. It is
negative evidence against a short false-idle interval, not proof that
rendering had finished.

**The reuse probe speaks for its own workload.** It weakens routine
premature release under that probe's pattern. It does not establish that
the display never fetches a released buffer under the game's.

## Where to look next, and where not to

The claim that the remaining cause must lie between the latch and the panel
is also withdrawn. Correct destinations and completed commands do not prove
the submitted commands produced a complete frame: rejected or missing
geometry, wrong depth or blend state, and a clear issued at the wrong point
all remain open, and the fault concentrating in particular scene sections
makes them likelier rather than less.

**The strongest untested hypothesis is a display FIFO underrun.** The
scanout starving part way down a frame produces exactly the observed shape
- top band right, remainder not - depends on memory bandwidth rather than
on presentation, and would concentrate in heavy parts of a scene, which is
the burstiness nothing else here explains. GMCH parts have no underrun
interrupt at all, only a sticky status bit in PIPESTAT bit 31, which is why
two days in the flip path could not have found it. That bit is now read and
accumulated on the Intel side (`PipestatAOr`, `PipestatBOr`); it has never
been looked at.

The experiment that separates the two families cleanly is to keep the
rendered target itself: save the back buffer immediately before Flip, tagged
with a frame sequence, and correlate against the panel recording. If the
saved image is also missing the robot, the fault is in rendering, state,
clears or rejected submissions. If it is complete while the displayed frame
is not, presentation is implicated and the FIFO reading discriminates within
that.

The Intel side has one advantage the S3 side does not: i915 documents an
answer to compare against. `Ecoskpd` and `FlipIssueLine{Last,Min,Max}`
against `FlipIssueVactive` are in the build already and need one netbook
boot. The scanline reading settles whether the plane base readback is the
active or the pending value, which is the same latch question asked where
there is an external reference for the answer.
