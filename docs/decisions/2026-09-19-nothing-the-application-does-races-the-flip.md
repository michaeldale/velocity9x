# Nothing the application does races the flip

2026-09-19 evening, A8U4I5 with the Trio3D/2X, build `595afe4-dirty` then
one with the Blt counter added, Final Reality Robots, hardware Direct3D.
Two passes. Attached: `2026-09-19-trio3d-unblank-fix-V9XSNAP.txt` and
`2026-09-19-trio3d-blt-exposure-V9XSNAP.txt`.

## The S3 fix did not engage

The afternoon's change made the flip complete when the retrace ENDS rather
than when it starts, and gave the ViRGE draw path the intel78 wait. The
point was that `VirgeDrawsFlipPending=0` had been an artefact of completing
at blank onset, so with the definition corrected the guard should have
something to guard.

It does not:

```
FlipDoneFirstPoll=0      (was 1 - the unblank change did take effect)
DrawsFlipWaited=0
VirgeDrawsFlipPending=0
DrawsFlipWaitTimeouts=0
279 flips, 141,322 primitives
```

`FlipDoneFirstPoll` moving from 1 to 0 says the completion really did move
later. The guard still never fires.

## Why, and the third counter to read zero for it

`CountGetFlipStatus=41922` across 279 flips - about 150 asks per flip. The
runtime holds the application behind GetFlipStatus, so by the time any
batch arrives the flip has long completed. The D3D guard cannot fire at
this frame rate whatever the completion means, and neither could intel79's
on the netbook.

The application also issues about two Blts and one Lock a frame, its clear
among them, and those paths test the ENGINE for idleness and never ask
about the flip. A cleared buffer on the panel is what the recording shows,
and a clear is a Blt, so that was the obvious remaining route. It is
counted now:

```
BltFlipPending=0        CountBlt=520   CountLock=308   252 flips
```

Also zero. So on the driver's own instruments, **nothing the application
does - no draw, no Blt, no Lock - ever arrives while a flip is pending.**

That is the whole ownership family of explanations measured out, and it
leaves one reading standing: the driver's notion of a flip being pending is
itself wrong, in which case every counter built on it reads zero by
construction and always would have. `VirgeDrawsFlipPending`,
`DrawsFlipWaited` and now `BltFlipPending` have each read zero, and the
same suspicion explains all three.

`v9x_flip_done` decides completion from `v9x_in_vblank` transitions alone.
`VblankInBlank/VblankSamples` is 997 of 286,038, about 0.35%, which is the
right order for a vertical retrace pulse and says the source is not stuck -
but nothing has ever established that the transition it reports coincides
with the CRTC actually latching the new start address. That is the same
unmeasured latch the Intel side has been stuck on since intel73, in a
different register file.

## One thing did change

```
EngineIdleTimeouts=0    EngineResets=0
```

Both read 1 in every earlier Robots pass on this card, reproducibly, and
the runbook says to expect zero. Completing the flip at the end of the
retrace rather than its start removed them. That is a real improvement and
it is not the flicker; it is filed here so the next capture is not read as
a regression when they stay at zero.

## Not judged

The panel was not recorded for either pass - OBS was not connected. So
whether the picture changed is unknown, and the counters say the mechanism
the fix targets never occurred, which makes an unchanged picture the
expectation rather than a surprise. The fix is kept because completing at
the end of the retrace is the documented VGA discipline and because it
removed the engine resets, not because it has been shown to help the
flicker.
