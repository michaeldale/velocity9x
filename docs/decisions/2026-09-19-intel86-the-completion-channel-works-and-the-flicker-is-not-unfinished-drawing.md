# intel86: the completion channel works, and the flicker is not unfinished drawing

2026-09-19, MICHAEL-NETBOOK (945GSE, 8086:27AE), Windows 98 SE, driver
package `build\win98se-intel-gma` built 2026-09-18 23:36. Final Reality
robot benchmark, one run, 1024x576x16 desktop with the game at 640x480.

The capture reports `CaptureBuildId=43c00be-dirty`. That id predates commit
9c86f7d by ninety seconds: the package was built from the working tree with
the eight-dword breadcrumb applied and not yet committed. The code under
test is 9c86f7d.

Attached: `2026-09-19-intel86-V9XSNAP.txt` (the runtime snapshot),
`2026-09-19-intel86-INTELEVT.txt` (the ownership journal).

## The completion channel came up on hardware

```
HwsSelfTest=1            HwsSelfTestPolls=0
BreadcrumbSubmits=575868 BreadcrumbTimeouts=0    BreadcrumbLate=0
BreadcrumbOutstanding=0  BreadcrumbAbandoned=0
BreadcrumbLagPollsMax=0  BreadcrumbLagPollsTotal=0
HwsPgaBefore=0x1FFFF000  HwsPgaAfter=0x1FFFF000
```

This is the first run in which a GPU write reaches the CPU inside the wait.
intel82, intel83 and intel84 each failed here with a different store
mechanism; the fill form with its measured MI_FLUSH suffix passes the
self-test at zero polls and then carries 575,868 batches without one
timeout, late arrival or abandonment. HWS_PGA is read and never written,
and reads the BIOS value at both ends.

`BreadcrumbLagPollsMax=0` is not evidence that the GPU is instantly done.
The breadcrumb is read at the next Flip, which on this run is roughly 984
draw submissions later; the channel is never asked early enough to observe
an incomplete one. What it does establish is that the value always arrives.

## Three models closed by the same run

**The write window is not it.** `i9xx_scanout.c` issues the plane-base
write in active video, short of a latch modelled at the first blank line,
and that block carried "UNMEASURED as a fix until the next boot". This is
that boot: 585 presents through the active-video window, and the operator
reports the same flicker. The latch model itself is still unread from any
register; what is measured is that moving the write inside it changes
nothing on screen.

**intel78's exposure did not occur.** `DrawsFlipWaited=0`,
`DrawsFlipWaitTimeouts=0`. The engine waits for a pending flip before the
first batch of a frame; across 575,868 batches that wait was never needed,
so no batch ever landed in a buffer the panel was still fetching by that
route.

**The flip never presented unfinished drawing.** `RenderDrainWaits=0`,
`RenderDrainStalls=0`, against a completion channel that is now known to
work. Flip gates on `v9x_render_drain`; the drain never once found
rendering outstanding. The model that the breadcrumb existed to test -
"head == tail says the parser consumed the batch, not that the pixels
landed" - is answered: by the time a flip is taken, the pixels have landed.

**And the buffers remain right.** `DrawsToFront=0`, `DrawsToBack=575868`,
against the plane base register read at each batch, not a cached value.
`FlipDeclined=0`, `FlipForcedIdle=0`, `ScanoutUnresolved=0`.

## Confirmed, not new

`IsrAfterFlipOr=0x00000000`, `IsrBeforeFlipOr=0x00000000`,
`FlipRingPendingSeen=0` across 605 flips. intel71 saw this across 795 and
intel72 across its own; the search for the pending bit by ORing ISR before
and after each flip has now run on this build and found nothing set, not
even a vblank bit. `v9x_flip_done` runs on the frame counter alone. The
counter is sound: PIPEB_FRAMEHIGH at 0x71040 reads 0x31, which
reconstructs to the recorded `ScanSampleFrame=12575`.

## The instrument that failed

`FlipHandled=585`, `FlipStillDrawing=52608`, of `CountFlip=53193`. Ninety-
nine per cent of Flips were refused, and the counter cannot say by which
test: `flip_still_drawing` was incremented both where the previous flip was
not yet taken and where the issue window was shut. The thing throttling
presents to 585 is therefore unnamed. Split in this commit:
`FlipWindowClosed` is the window test from ABI stamp 2026091901, and
`FlipStillDrawing` is the previous flip alone.

## What the panel was fed

From the in-game layout sample, plane B: `DSPBCNTR=0x95000000`,
`DSPBLINOFF=0x00096000`, stride `0x500`, `PIPEBSRC=0x027F01DF` (640x480)
inside a `0x053F03FF`/`0x029F023F` timing (1024x576), panel fitter on
(`PFIT_CONTROL=0xA2C4008E`). 640x480x16 is 0x96000 bytes, so the front
buffer is offset 0 and the back 0x96000, and the sample is a flip to the
back - which is what the sampler is for, since it only records a flip whose
target is not zero.

## The event journal is refused

`check-intel-event-capture.ps1` rejects this capture: every post-boot record
carries `Flags=0000002F`, missing `V9X_I9XX_FP_SOURCE_MATCH` (0x10), where
the 2026-09-12 matrix has `3F` throughout. The pipe source did not equal the
mode the driver expected at the instant each snapshot was taken, though
`INTELMM.TXT` later in the same boot reads 1024x576 with the bit set - which
points at when the snapshot is taken rather than at the pipe. The arming
precondition is unaffected: it tests the latest event of a kind, and event 00
still matches it exactly (flags `3F`, four ring fields zero, PGTBL
`7FFC0001`, HWS_PGA `1FFFF000`, both GTT hashes `4D8707C5`). Not chased
here; the validator is stricter than the driver's own rule and one of the
two has to move.

## What this leaves

Every mechanism the issue doc lists is now measured out, and the flicker is
unchanged. What has never been read is the latch itself - whether the plane
base takes effect where it is written or at the blank - and no register on
this part has been found that reports it. The next measurement has to be one
that can see what the panel shows rather than what the driver believes; the
video record has been that instrument twice and is still the only one.
