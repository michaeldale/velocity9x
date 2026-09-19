# ViRGE on 86Box: the flip chain rebinds correctly, and the emulator cannot answer the timing

2026-09-19, `Win86SE` (86Box 6.0, S3 ViRGE/DX 86C375, 4 MiB, agent port
9869), `s3` family, build `f1b869a-dirty`, desktop 1024x768x16 RGB565,
Final Reality 1.01 Robots scene alone, one pass, rendering platform
**Direct3D On-board Accelerator**.

Attached: `2026-09-19-virge-86box-robots-V9XSNAP.txt`.

## Three things had to be fixed before the test could run at all

**The guest was unreachable.** `Win86SE`'s `86box.cfg` declared
`[SLiRP Port Forwarding #1] 0_external = 9869` with no `0_internal` line, so
the host port forwarded nowhere. Every sibling guest has both
(`Win98SE-Fast-D3D` 9878 -> 9869, `Win98SE-Trio64` 9871 -> 9869). Added
`0_internal = 9869`; the original is kept as `86box.cfg.pre-portfix-20260919`.

**The guest was three weeks stale**, on build `neproof2`. Deployed through
the WININIT.INI rename route, since `V9XCOPY.BAT` needs real DOS.

**Final Reality would not accept the hardware device at RGB555.** At a
32bpp desktop it refuses outright ("No Direct3D hardware rendering platforms
found!"). At 1024x768x16 it starts, but the rendering platform list holds
only `Direct3D Software`, and the trace shows DirectDraw creating and
destroying two 16x16 contexts on our HAL - an enumeration probe, accepted by
us (`D3dContextRejects=0`) and then rejected by FR. Forcing
`[Velocity9x] HighColor=16` in SYSTEM.INI, which resolves the layout to 565
(`ColourLayout=565-ini`, mask `0xF800`), makes `Direct3D On-board
Accelerator` appear and be selected. This is the same shape as Incoming
failing at CreateSurface for want of a 565 or P8 texture format, and it
means **every hardware-Direct3D result on this guest is conditional on 565**.
The physical Trio3D runs were at 565 desktops, which is why this never
surfaced there.

## What the run says

```
D3dRenderPrimitiveCalls=173725   FlipHandled=386   CountFlip=386
FlipStillDrawing=0   FlipWindowClosed=0   FlipDeclined=0
VirgeDrawsFlipPending=0
DrawsIntoPresented=0
PresentTraceCount=1157
```

The present trace, ten consecutive frames, reads the same every frame:

```
FLIP-ACCEPTED  seq 376  offset 0x00000000
FLIP-DONE      seq 376  offset 0x00000000
DRAW           seq 376  offset 0x00096000   context 0
FLIP-ACCEPTED  seq 377  offset 0x00096000
FLIP-DONE      seq 377  offset 0x00096000
DRAW           seq 377  offset 0x00000000   context 0
```

**The flip chain rebinds the engine correctly.** The buffers alternate
0 <-> 0x96000, and the first draw after each flip is always aimed at the
buffer that was NOT just presented. `DrawsIntoPresented=0` says the same
thing for all 173,725 batches, not just the ten traced: the engine was never
once aimed at the buffer the panel had been told to show. Stale binding is
dead as an explanation, and so is premature reuse of the presented buffer,
at least on this path in this environment.

## What it cannot say

`FLIP-DONE` follows `FLIP-ACCEPTED` immediately, in every frame, and
`FlipStillDrawing=0` across 386 flips. On S3, `v9x_scanout_hw_flip` and
`v9x_scanout_writes_in_blank` both answer from `v9x_i9xx_scanout_active` and
so are false; the flip goes to the blank-edge branch, and 86Box's vblank
resolves at once. **A flip is therefore never pending in this environment.**

That makes `VirgeDrawsFlipPending=0` a property of the emulator, not a
measurement of the driver. The missing ViRGE guard - `v9x_flip_wait_done` is
called from `d3d_i9xx.c` and nowhere else - is real, and this run does not
exercise it even once. Nothing here narrows the exposure on silicon, and
nothing here bears on the flicker itself: 86Box synthesises the vblank and
draws to a host window, so latch behaviour and what a panel shows are not
reproduced.

So the emulator answers the ordering questions and is structurally unable to
answer the timing one. The ViRGE exposure still has to be counted on the
physical Trio3D.

## The first instrument was wrong, and the run proved it

The present trace as first written recorded every draw batch. Robots
produces about 450 batches per frame, so a 32-record ring filled 355,422
times in one run and the kept records were 32 consecutive teardown draws at
offset 0 - the last frame's tail, spanning no flip at all. It could not have
answered the question it was built for in any run.

It now records only the first draw after each accepted flip, which is the
one the question is about, and the rest of the frame is counted rather than
traced. Three records a frame, so the same ring holds ten frames. The
aggregate `DrawsIntoPresented` carries the all-batches answer and does not
depend on the ring at all. ABI stamp 2026091904.

## Also recorded

`EngineType=1`, `EngineCaps=0x000000DF` - `CAP_D3D` set, `CAP_D3D_SOFTWARE`
clear. 386 flips for one Robots pass at roughly 450 batches each. The guest
is left at 565 with the current build installed.
