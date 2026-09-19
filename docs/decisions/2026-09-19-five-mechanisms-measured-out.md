# Five mechanisms measured out, and the metric that got there

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

## Where to look next, and where not to

Not at more driver-side counters. Five mechanisms, five instruments, five
clean reads, and the fault is untouched. The remaining gap is between the
CRTC start address being latched and what the panel actually fetches, and
no register on this part reports it - which is the same gap the Intel side
has been stuck in since intel73, on a different chip with a different
register file.

The Intel side has one advantage the S3 side does not: i915 documents an
answer to compare against. `Ecoskpd` and `FlipIssueLine{Last,Min,Max}`
against `FlipIssueVactive` are in the build already and need one netbook
boot. The scanline reading settles whether the plane base readback is the
active or the pending value, which is the same latch question asked where
there is an external reference for the answer.
