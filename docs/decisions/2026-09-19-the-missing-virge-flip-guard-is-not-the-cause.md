# The missing ViRGE flip guard is not the cause

2026-09-19, **A8U4I5** (`10.0.1.172`, physical Windows 98 SE) with the S3
Trio3D/2X `5333:8A13` fitted, `s3` family through the ViRGE backend, build
`f1b869a-dirty`, desktop 1024x768x16 RGB555, Final Reality 1.01 Robots
alone, one pass, rendering platform **Direct3D On-board Accelerator**.

Attached: `2026-09-19-trio3d-robots-V9XSNAP.txt`.

## The question

`v9x_flip_wait_done` - the intel78 guard that holds a batch until a pending
flip is taken - is called from `d3d_i9xx.c` and nowhere else. The ViRGE path
has none. That asymmetry was the reason to stop claiming that one symptom
across two present timings cleared presentation timing: Intel ran with the
guard and measured the exposure at zero, S3 ran without it and had never
been measured. `virge_draws_flip_pending` counts the exposure.

## The answer

```
D3dRenderPrimitiveCalls=133771   FlipHandled=268   CountFlip=268
FlipStillDrawing=0   FlipWindowClosed=0   FlipDeclined=0
VirgeDrawsFlipPending=0
DrawsIntoPresented=0
PresentTraceCount=803
```

**Zero.** Not one draw batch in 133,771 began while a flip was pending, on
silicon. The guard Intel has and ViRGE lacks would never have fired, so its
absence cannot be what puts the flicker on this card, and the build that
waits has nothing to wait for. The comparison that was planned against it is
not worth running.

The present trace, ten consecutive frames, is the same shape the 86Box guest
produced:

```
FLIP-ACCEPTED  seq 258  offset 0x00000000
FLIP-DONE      seq 258  offset 0x00000000
DRAW           seq 258  offset 0x00096000   context 0
FLIP-ACCEPTED  seq 259  offset 0x00096000
FLIP-DONE      seq 259  offset 0x00096000
DRAW           seq 259  offset 0x00000000   context 0
```

Buffers alternate 0 <-> 0x96000, and the first draw after every flip is
aimed at the buffer that was not just presented. `DrawsIntoPresented=0` says
the same for all 133,771 batches. Stale binding and premature reuse are dead
on this path on silicon, as they were in the emulator.

## Why the exposure is zero, and what that is conditional on

268 flips across roughly sixty seconds is about 4.5 frames a second, so
there are some 220 ms between presents - orders of magnitude longer than a
flip takes to be taken. `FlipStillDrawing=0` across every one of those 268
Flips says the state machine had always finished by the time the next call
arrived. The window in which the guard could matter exists, but nothing ever
lands in it at this frame rate.

That is a measurement and not an emulator artefact - unlike the identical
`VirgeDrawsFlipPending=0` from the 86Box guest, where flips complete
instantly because 86Box's vblank resolves at once and the window does not
exist at all. But it is conditional on the frame rate: a faster scene, or a
card that presents at 60, could put batches inside the window. Robots on a
Trio3D does not.

## Not reproduced here

This run was driven headless and nobody watched the panel; OBS was not
connected. Nothing in it confirms or denies that this particular pass
flickered. The operator's report of flicker on this machine stands as the
reason for the investigation and is not re-established by this capture.

## Also measured, and it corrects a guest result

**The physical Trio3D offers the hardware device at RGB555.** `Direct3D
On-board Accelerator` appears and is preselected with `ColourLayout=555-auto`
and no `HighColor` key in SYSTEM.INI. The 86Box ViRGE/DX does not - there the
list holds only `Direct3D Software` until `HighColor=16` forces 565
(`2026-09-19-virge-86box-...`). So that blocker is the emulated ViRGE's, not
the S3 Direct3D path's, and the operator's flicker runs on this machine were
on the hardware rasteriser, not a silent software fallback. That was worth
settling: had FR been falling back, the flicker would have been a different
code path from the Intel one and the shared-code reading would have been
wrong.

## Where this leaves the flicker

Every mechanism instrumented so far is now zero on both backends:
completion, flip bookkeeping, buffer binding, premature reuse, and now the
pending-flip exposure. The Intel side adds the same, with a working
completion channel behind it (intel86). Two different present paths, two
chips, every counter clean, and the operator sees flicker on both.

What has never been measured on either is the latch - when the scanout
begins fetching from a base that has been written - and no register on
either part reports it. The remaining instrument is the panel. OBS is
connected to this session for that purpose; a recording of this machine
through several flips, read against `PresentTraceCount` and the flip
sequence, is the next measurement.
