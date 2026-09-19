# The watermark was computed from a depth the hardware was not in

2026-09-20. Five fixes, four of them from the review filed in `05d47b5` and
one from intel95's own evidence. No hardware trial has been run against any
of them.

## What intel95 caught

The watermark programming committed in `36d0d7f` worked, and then undid
itself. From `FinalReality\V9XSNA1.INI`:

```
Wm0Blc=0x03060106   Wm0Want=0x0314011A   ← the BIOS value, then ours
Wm1Blc=0x0314011A   Wm1Want=0x0314011A   ← read back: the write took
Wm3Src=0x027F01DF   Wm3Want=0x030C011A   ← the same mode, a different answer
WmWrites=2          WmWritten=0x030C011A
```

`0x0314011A` is plane B 20, plane A 26, both burst bits, bit 25 preserved -
exactly as designed, confirmed on hardware. `0x030C011A` is plane B **12**,
and it was written last.

The arithmetic says where 12 came from:

| cpp | entries | plane B watermark |
|---|---|---|
| 2 | 9 | 20 |
| 4 | 17 | **12** |

So `cpp` was 4 at the fourth transition. The hardware disagrees:
`FlipDspCntrLast=0x95000000` is format 5, BGRX565, and
`FlipStrideLast=0x00000500` is 1280 bytes for a 640-wide line - two bytes a
pixel, both readings. The driver took the depth from
`v9x_hal->fb.bits_per_pixel`, which read 32, and **lowered the live plane's
watermark below what it had installed one mode change earlier**, to twice
what the BIOS left.

A watermark computed from a depth the hardware is not in is worse than no
watermark at all, and this one was written to the register.

It also confounds the only underrun reading taken against the change. The
Final Reality boot underran (`PipestatBFirst=0x00000202` clean,
`PipestatBOr=0x80000202`) and the 3DMark99 boot did not - and the Final
Reality boot is the one where the watermark was dropped to 12. Neither
result says anything about the change as intended.

## The five fixes

**1. The depth comes from DSPCNTR.** `v9x_i9xx_wm_cpp_from_dspcntr` maps the
format field to bytes per pixel and returns zero for a format it cannot
name; the caller then counts `WmDeclined` and programs nothing. A number
that cannot be derived is refused rather than guessed, which is what made
intel93's DSPARB error cost one capture instead of a conclusion.

**2. Programming no longer stops when the diagnostic log fills.**
`wm_log_count` reached its cap of four in both intel95 captures, and the
function returned before the write. The log was gating the driver. Capacity
now gates only the logging.

**3. The trigger is the calculation's inputs, not the source size.** intel95
alternated two modes at the same 54,180 kHz and the same partition, so a
depth change at an unchanged source would have gone unnoticed. The pipe
source, pixel rate, depth, DSPARB and FW_BLC's own value are all in the
comparison now - so a watermark something else overwrites is put back.

**4. The pipe is resolved separately from the plane.**
`v9x_i9xx_scanout_pipe` resolves both and kept only the plane, so the timing
reads assumed a plane drives its namesake pipe. It does on the netbook.
That is not a licence to assume it, and a plane carries pipe-select bits
precisely because it need not.

**5. The burst floor.** `intel_calculate_wm` applies a final minimum of
eight after the maximum and after the default; this module let an active
plane fall to 1, and the host test agreed because it was written from the
same misreading. The order matters as much as the value: the no-room case
returned the default *early* and so escaped the floor entirely. It changes
nothing at the netbook's 20 and changes what the helper hands a tighter
mode. Reference: Linux v4.4 `drivers/gpu/drm/i915/intel_pm.c`.

## And the accelerated clear is now counted

`blt_flip_pending` is sampled in `v9x_blt_drain`, which a successful engine
fill, depth fill or copy returns before reaching. So every accelerated clear
went uncounted, and the ViRGE's zero covered CPU blits alone - the opposite
of reassuring, because the accelerated path is the one a game's clear takes.

`BltEngineFlipPending` samples before the engine is dispatched and records
the destination with it, because a pending flip during a clear only matters
if the clear lands where the scanout is reading.

## What this does not do

It does not fix the flicker, and it does not show that the watermark has
anything to do with it. It makes the change do what it was written to do,
which intel95 showed it did not. The comparison that would settle the
question - dips counted against the 23-per-12 s and 1.09-per-second
baselines, on a build where the watermark stays at 20 for the whole run -
has not been run.

One stale comment went with it: the pipestat boundary's note said the
watermarks were "never written", which stopped being true on 2026-09-20.
That is also why `FwBlc` and `WmWritten` disagree in the Final Reality
capture - `FwBlc` is sampled once at the boundary and there were five
DriverInits. Ordering, not a lost write.
