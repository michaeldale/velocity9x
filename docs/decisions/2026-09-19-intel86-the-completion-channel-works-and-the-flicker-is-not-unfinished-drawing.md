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

**`DrawsToFront=0` again**, `DrawsToBack=575868`, with `FlipDeclined=0`,
`FlipForcedIdle=0`, `ScanoutUnresolved=0`.

This does NOT say the buffers are right, and an earlier revision of this
document claimed it did. The comparison reads the plane base register, and
the latch model in `i9xx_scanout.c` says that register's readback returns
the PENDING value on a double-buffered register - which is how intel73 came
to say "applies at once". So for the frame between the write and the latch,
`draws_displayed_last` names the buffer about to be shown, not the one on
screen, and a batch landing in the buffer the panel is still fetching is
counted as a batch to the back. `DrawsToFront` is blind in exactly the
window the flicker occupies. What it rules out is a target that disagrees
with the pending base for a whole frame, which is not the same thing.

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

## The S3 path flickers the same way

Reported the same day from A8U4I5 (`10.0.1.172`, physical Windows 98 SE)
with the S3 Trio3D/2X `5333:8A13` fitted, running the `s3` family with
`Direct3DMode=hardware`: Final Reality flickers there too, and it looks the
same. `8A13` binds through the ViRGE backend
(`src/common/backend_registry_table.inc`, "virge-dx alias"), so this is the
ViRGE code path on Trio3D silicon.

"Looks the same" is an eye against an eye, on two panels, weeks apart. It
is a lead, not a measurement, and two causes can share an appearance.

It is also the first time anyone has WATCHED Final Reality on this card.
`docs\decisions\2026-09-02-final-reality-on-a-real-trio3d.md` scored the
same benchmark on the same machine and is titled "rendered blind" - nobody
saw the image. So the symptom cannot be dated on the S3 path, and nothing
says it is new there.

The two present paths are not the same code. `v9x_scanout_hw_flip` and
`v9x_scanout_writes_in_blank` both answer from `v9x_i9xx_scanout_active`,
so on S3 they are false: the base is written whenever Flip is called, with
no beam timing at all and no window test, and `v9x_flip_arm` waits for the
next blank edge. Intel takes the hardware-flip branch with the active-video
window.

**Presentation timing is NOT cleared by that.** An earlier revision argued
that one symptom across two present timings meant the timing was innocent.
It does not follow, because the two backends are not in the same state with
respect to a second hazard: `v9x_flip_wait_done` - the intel78 guard that
holds a batch until a pending flip is taken - is called from `d3d_i9xx.c`
and nowhere else. The ViRGE path has no such guard. So Intel ran with the
guard in place and measured the exposure at zero, while S3 ran with the
hazard open and unmeasured. Two backends can reach the same look by two
routes, and until the ViRGE exposure is counted, they might have.

What the two paths DO share is everything above the scanout: the Flip
sequence in `ddhal_core.c`, the flip state machine, `v9x_render_drain`, and
the whole D3D core including which buffer the engine is bound to.

## What the next build measures, and why the counters were wrong

A first cut counted changes in the render target's offset
(`target_offset_changes`, ABI 2026091902, never deployed). It could not
work: the comparison was against one global diagnostic value, taken on
every context lookup, so switching between contexts or looking one up
without drawing moved it, and nothing in it said which buffer a given
frame used. Withdrawn.

What distinguishes a stale binding from premature reuse is the ORDER of
three events, so ABI 2026091903 records them in one ring
(`V9X_D3D_PRESENT_TRACE`, 32 records): the accepted flip with the offset
handed to `set_display_start`, the completion of that flip, and every draw
batch with the destination the engine was ACTUALLY given plus its context
index - taken at the shared dispatch, not inferred from a later lookup.
Each record carries the accepted-flip sequence, so a draw sits between the
flip it follows and the completion it precedes. A draw aimed at the buffer
the flip released is premature reuse; the same draw aimed at the buffer the
flip presented is a stale binding.

Alongside it, `virge_draws_flip_pending` counts batches the ViRGE path
began while a flip was still pending - the exposure Intel closed in intel78
and S3 never had. This build counts it and does not change it, so the build
that waits can be compared against it.

## What this leaves

Every mechanism the issue doc lists is now measured out, and the flicker is
unchanged. What has never been read is the latch itself - whether the plane
base takes effect where it is written or at the blank - and no register on
this part has been found that reports it. The next measurement has to be one
that can see what the panel shows rather than what the driver believes; the
video record has been that instrument twice and is still the only one.

The order to take it in:

1. **Count the ViRGE exposure**, then build one that waits and compare. If
   the count is large and the waiting build is clean, the S3 flicker is the
   missing guard and was never the same fault as the Intel one.
2. **Read the present trace** from either family. It answers stale binding
   against premature reuse directly, which no aggregate counter can.
3. **`/reuse` on both machines, with the screen recorded.** Green after
   reported completion implicates scanout ownership.

A caution on the third, because an earlier revision of this document had it
wrong: `/reuse` does NOT take Direct3D out of the picture. The probe waits
until `GetFlipStatus` says the flip is done, waits the delay, and only then
paints the retired buffer - so it tests whether REPORTED COMPLETION is
premature. It does not reproduce a path that draws without waiting, which
is exactly what the ViRGE path does. A clean `/reuse` narrows the
investigation; it does not clear D3D synchronisation.
