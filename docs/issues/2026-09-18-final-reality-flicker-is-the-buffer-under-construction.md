# Final Reality on the 945GSE flickers: the panel shows the buffer being drawn

**Status:** OPEN. Not fixed after nine boots (intel71 to intel79) of flip
work on 2026-09-18. **Machine:** MICHAEL-NETBOOK, Intel 945GSE
(8086:27AE), LVDS on pipe B, plane B, 640x480x16 at 60 Hz, 576 active
lines in a 672-line frame. **Software:** Windows 98SE, Final Reality,
Velocity9x 0.8.0 with Intel runtime 3D and the Intel flip on by default.
**Running record:** `2026-09-16-final-reality-renders-black-and-the-hal-faults.md`
(1,300 lines; this file is the summary that record has grown too long to
give). **Decision record for the latch claim:**
`..\decisions\2026-09-18-intel-plane-base-latches-at-vblank-start.md`.

## The symptom, as measured

The operator's phone video (60 fps, 663 frames, an intel7x flip build)
measured frame by frame, mean grey of the middle and lower thirds of the
panel:

```
f408 mid 116.8 bot 175.5   dark: cleared buffer, sky drawn, ground not
f409 mid 179.0 bot 188.7   filling in
f410 mid 191.8 bot 219.4
f411 mid 194.0 bot 224.5   finished picture, held
f412 mid 194.2 bot 224.7
f413 mid 194.3 bot 224.5
f414 mid 193.9 bot 223.7
f415 mid 119.7 bot 176.8   dark again: period 7 video frames, 8.6 Hz
```

Once per game frame the panel shows the buffer the game is drawing into -
the clear, the sky, then the ground filling in - for about one display
frame, then the finished picture. Another frame shows the finished upper
half over a flat grey lower half with a hard horizontal edge: the ground
pass caught part-way. The operator's words for it were "a transparent
overlay that doesn't block the image behind, starts at the bottom and
covers the screen by the end of the scene"; the ground is where the
unfinished part of a frame is, and the scenes fill with ground as they
play.

It is NOT a torn frame, an older frame, wrong texels or missing geometry.
It is an unfinished frame on screen.

## What the counters say, every build

```
DrawsToFront=0          every batch went to the buffer the plane base
                        register did NOT name at that moment (intel74-78)
FlipTakenAtDone=all     the base register held the flipped offset when the
FlipNotTakenAtDone=0    state machine declared each flip done
IsrAfterFlipOr=0        the i915 flip-pending ISR bits never read set
FlipFramesInSubmit=1    the command streamer does not stall on MI_DISPLAY_FLIP
ScanBTickLine=671       the frame counter ticks with DSL at 671; blank is 576-671
```

So: the game alternates buffers 0 and 0x96000 exactly as DirectDraw's
pointer swap says; every draw is aimed at the buffer the register does not
name; and the panel nonetheless shows draws in progress. The register and
the panel disagree for a period after each write. That is the whole
problem in one sentence.

## Hypotheses tried on hardware and the boot that killed each

| Boot | Build | Change | Result |
|---|---|---|---|
| intel65 | | flip written anywhere, waited on afterwards | "tearing, lower half" |
| intel66 | | written inside the blank, released at blank end | worse |
| intel71 | | MI_DISPLAY_FLIP through the ring | tears the same |
| intel72 | audit | i915 v4.4 pending bits 11/10 | never set; base reads new at once |
| intel73 | `86da02c` | complete when the frame counter moves | ISR zero, CS does not stall |
| intel74 | `57f6ccc` | written in the first 24 lines of the blank | FASTER flicker |
| intel75 | `72bea7b` | count draws to front/back | DrawsToFront=0 |
| intel76 | `07ffc98` | MI_FLUSH before each batch (stale render cache) | unchanged |
| intel77 | `00faf7d` | MI_READ_FLUSH before each batch (stale texture cache) | unchanged |
| intel78 | `3d9a103` | base written in ACTIVE video, before the blank-start latch | "a little better" |
| intel79 | `e83a65e` (built as `3d9a103-dirty`) | engine waits for the pending flip before each batch | package copied 16:27, NOT YET BOOTED |

Three of those (intel76, intel77, and the skipped-depth reading that
intel77's counters refuted) were guesses from a verbal description and
could never have put a cleared buffer on screen. They cost three boots and
are the reason this file exists. The two flushes stay in the driver: the
game rebuilds about 117 textures a frame into the same memory, so the
texture-cache invalidate is a correctness requirement on its own.

## The current model, and what would prove or kill it

The plane base register is double-buffered: a write goes to a pending copy
and reads back at once (intel73's "applies immediately"); the live copy
the panel fetches from takes the pending value at a latch point once per
frame. The one latch point consistent with every boot above is the START
of the vertical blank (line 576): a write in the blank (intel66, 74-77)
then waits a whole frame, and a release at that blank's tick (671) hands
the game a buffer still on screen for 16.7 ms. i915 v4.4 writes plane
registers in the 100 us BEFORE `crtc_vblank_start` for this reason
(`intel_pipe_update_start`, `VBLANK_EVASION_TIME_US`).

intel78 moved the write into active video and the flicker got "a little
better", not gone. The gap that leaves: Flip returns as soon as the base
is written. DirectDraw holds Lock and Blt on GetFlipStatus (55 asks a
frame in intel78) so the CPU clears wait for the latch; Direct3D does
not ask, RenderPrimitive goes straight to the engine, and the frame's
first batches land in the buffer the panel is still fetching until the
latch - one display frame of sky on a cleared buffer, whichever side of
the blank the base was written. `e83a65e` makes the engine wait for the
pending flip before a batch and counts the batches that waited
(`DrawsFlipWaited`, shared ABI 2026091710).

What the intel79 boot will say:

- Construction gone, `DrawsFlipWaited` about one per frame: the model is
  right and the gap was the Direct3D path. Close this issue.
- Construction still there, `DrawsFlipWaited` about one per frame: the
  draws did wait for what the state machine calls done, and done is still
  before the panel switches. The completion rule is wrong, not the gate.
  Next: complete a flip only at the SECOND frame tick after the write,
  which costs one frame of latency and cannot be early under any latch
  model.
- Construction still there, `DrawsFlipWaited` near zero: the draws are not
  racing the pending flip at all, and the model is wrong. Next: the
  readback instrument below, before any further code change.

## The instrument that ends the guessing

A probe rung, not a driver change: render two known solid frames
alternately into a flipping chain, and after each Flip read the FRONT
buffer's memory back at a point in the lower half several hundred times
per frame, recording the first moment its content matches the frame just
presented, in scanlines from the flip write. That gives the latch delay
as a number, from the hardware, without a camera. Everything above is
reasoned from counters and a video; this measures the one thing they
cannot: when the panel's buffer actually changes.

## Not in scope here

- 3DMark99 shows one or two textures and complains about 800x600: open,
  deferred until the flicker is closed.
- The 117 texture creates a frame (76,006 in 650 frames, one Lock each):
  a cost, not a fault; noted for later.
