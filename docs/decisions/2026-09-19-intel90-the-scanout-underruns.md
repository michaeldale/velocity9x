# intel90: the scanout underruns on the live pipe

2026-09-19, MICHAEL-NETBOOK (945GSE), build `7d11b59-dirty`, Final Reality
Robots on hardware Direct3D. The first boot carrying the FIFO underrun
reading. Attached: `2026-09-19-intel90-V9XSNAP.txt`.

## The instrument validated, then answered

```
PipestatCleared=1
PipestatAFirst=0x00000203   PipestatAOr=0x00000203
PipestatBFirst=0x00000202   PipestatBOr=0x80000202
LivePipe=1  LivePlane=1
```

`PipestatCleared=1`, so the boundary was established and the accumulators
mean what they claim. Pipe B is the live pipe, feeding the LVDS panel.

**Pipe B's underrun status was CLEAR at the baseline and SET afterwards.**
Bit 31 of PIPESTAT is the display FIFO underrun: the scanout ran out of
data part way down a frame. It is a fresh event, after the measurement
boundary, on the pipe driving the panel.

The confound is ruled out by the same numbers. The clear writes
`(first & 0xffff0000) | bit31`, and the read immediately after it is ORed
in - so a clear that failed would have put bit 31 in the accumulator at
once. Pipe B's baseline had bit 31 already clear, so there was nothing for
the write to clear and nothing for a failed write to leave behind; bit 31
in the accumulator can only have come from a later sample. Pipe A, which
drives nothing, stayed clear throughout.

## Why this is the first plausible mechanism in two days

A display FIFO underrun starves the scanout part way down a frame, so the
top of the picture is right and the rest is not. That is the shape of the
recorded flicker frames - the sky band present, everything below it black -
which no presentation fault explains as naturally.

It depends on memory bandwidth rather than on presentation, which explains
the burstiness measured on the Trio3D: the fault concentrates in the heavy
parts of a scene, and nothing in the flip path predicts that.

And it is invisible from where this driver has been looking. GMCH parts
have no underrun interrupt at all, only this sticky status, so two days of
flip-path instruments could not have found it however carefully they were
built. Every one of those instruments reading clean is consistent with an
underrun rather than evidence against one.

Not proven to BE the flicker. What is established is that the scanout
starved at least once during the run, on the pipe that feeds the panel,
which is a defect in its own right and the first mechanism found that
predicts the symptom's shape and its timing.

## Two corrections this capture forces

**The Lock reading was hiding a signal, not just missing one.** With Lock
sampled, `BltFlipPending=1104` against 1,124 Blts and 62,138 Locks - where
the Trio3D read zero with Blt alone. So application access DOES arrive with
a flip pending, at least on this part, and the earlier "nothing the
application does races the flip" rested on a counter that was not being
called from the path in question. One counter cannot say which of Blt or
Lock it was; they are counted apart from here.

**intel89's leaking window was a misreading, and the guard change is
reverted.** `FlipIssueDeltaMax=1`: the write path costs at most one
scanline, so a window-tested flip cannot be issued more than a line past
where it was tested and there is no gap to guard. The high issue lines came
from flips that never took the test - `FlipToGDISurface` writes the display
start directly on the way back to the desktop, 22 times here, and
`FlipRingIssued` 578 against `FlipHandled` 554 is the same twenty-odd.
Mixing them put line 610 into a statistic the guard was then sized from.

The widening cost what widening costs and bought nothing:
`FlipWindowClosed` went 45,625 to 169,972 with presents unchanged at 554
against 558. Back to eight lines, and the line statistics now exclude flips
the window does not govern.

## What to do next

Read the underrun on the Trio3D. The S3 path has its own pixel FIFO,
streams processor and memory arbitration settings, and if that card
underruns too then one mechanism explains the symptom on both, which
nothing else this investigation has found comes close to doing.

Then compare the scanout configuration against the stock driver and the
BIOS after the game's mode change - watermarks and FIFO thresholds on
Intel, FIFO and arbitration on S3. An underrun is usually a configuration
that does not leave the scanout enough margin, not a fault in the flip
path.

And the experiment that separates rendering from presentation outright is
still worth doing: keep a known image displayed while rendering heavily
into a disjoint offscreen buffer, without flipping. Flicker there would
implicate bandwidth and arbitration directly, with the flip path removed
from the question entirely.
