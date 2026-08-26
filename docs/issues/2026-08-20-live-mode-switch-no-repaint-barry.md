# A live mode switch leaves the desktop unrepainted on the physical Trio64

Status: **open.** Found 2026-08-20 on driver 0.4.3. Not diagnosed.
**Still reproduces on 0.5.0** (build `edd7684`), reconfirmed 2026-08-26 on the
same card during `docs\plans\s3-physical-pipeline-validation.md`; see
"Reconfirmed on 0.5.0" below. No diagnosis attempted.

Target: BARRY, physical S3 Trio64 (86C764), `PCI\VEN_5333&DEV_8811`, 2 MiB VRAM,
Windows 98 SE, 32 MB RAM. Reached over the remote agent at `10.0.1.47:9869`.

## What happens

After a **live** mode switch - `ChangeDisplaySettingsA` with
`CDS_UPDATEREGISTRY`, which is what Display Properties does - the desktop never
finishes repainting. Large areas keep the *previous* mode's framebuffer
contents, which at a different stride reads as a doubled or tiled image.

A **reboot** into the same mode is completely clean.

Observed at 800x600x32 and, earlier the same day, on a live 8 -> 16 switch at
1024x768. Both of those depths predate the 24/32-bpp work.

## What it is not

Three explanations were checked and ruled out, which is most of what is known:

**Not a capture race.** That trap is real on this machine and produced a
withdrawn regression report the same day
(`2026-08-20-barry-tiling-was-a-screenshot-race.md`), so it was the first
suspect. It is not this: the state persisted through a forced `Ctrl+Esc` /
`Escape` and more than six capture round trips over tens of seconds, where the
race resolves within one or two.

**Not a stride or layout fault.** The regions that *do* repaint - the "My
Computer" icon and label, the Start button - are crisp, correctly scaled and
correctly coloured. New drawing lands in the right place. The driver's own
`Surface=` line agrees with the mode at every step:

```
Surface=pitch=3200 bpp=32 dwb=3200 dds=3200 w=800 h=600 debpp=32
```

pitch, `deWidthBytes` and `deDeltaScan` all 3200 for 800 pixels at 4 bytes.

**Not the no-clear flag alone.** The S3 families set modes with
`V9X_HW16_VBE_NO_CLEAR`, so stale VRAM after a mode set is expected - but
Windows should then repaint over it, and on a fresh boot into the same mode it
does.

So the fault is in *invalidation*: after the in-place PDEVICE rebuild, something
is not telling GDI the whole screen is dirty, or the repaint it does issue is
being dropped.

## Emulated targets reverified on the changed driver, 2026-08-26

The two fixes above land on the live-switch path that every family shares, so
they were regression-tested on the emulated guests where live switching works.
Nine live switches, three per target, each captured twice and read at the second
capture, all crisp with correct palettes and no stale content - and each target
had a clean "before" switch on its previously installed driver for comparison:

| Guest | Chip | Switches |
|---|---|---|
| `Win98SE-Trio64` :9871 | Trio64 4 MiB | 1024x768x32, 640x480x8, 1024x768x16 |
| `Win86SE` :9869 | ViRGE/DX 4 MiB | 1280x1024x8, 640x480x32, 1024x768x16 |
| `Win98SE-Mach64VT2` :9873 | Mach64 VT2 | 1024x768x16, 640x480x8, 800x600x16 |

So the reboot/live asymmetry is unchanged and confined to the physical card, and
the fixes do not regress the targets that work.

## Not reproducible under emulation

The same 0.4.3 binary live-switches cleanly on both 86Box S3 targets - ViRGE/DX
`:9869` and Trio64 `:9871`, both 4 MiB. Only the physical 2 MiB card shows it.
Whether the variable is the VRAM size, the real BIOS, or simply that BARRY is a
32 MB machine slow enough to expose an ordering bug the emulated ones hide, is
not established.

**The mode matrix cannot see this.** `run-vm-mode-matrix.ps1` reboots between
every mode, so every capture it takes is of a freshly painted desktop. That is
why 11/11 passes on both emulated chips and 0.4.3 shipped without noticing.

## Three cures ruled out, 2026-08-26

An attempt to fix this on 0.5.0 failed. All three candidates below were built,
deployed to BARRY and measured, and none of them changes the symptom. They are
recorded because each looked compelling and each is now excluded.

**1. USER's forced repaint, called from ReEnable. Ruled out.** Every Windows 98
DDK display sample resolves USER.EXE's unnamed export at ordinal 275 -
`REPAINT_EXPORT_INDEX` in `98DDK\src\display\mini\*\SSWITCH.ASM` - and calls it
to force a repaint of all windows whenever it has changed the screen behind
USER's back. This driver had no such call at all, which made it the obvious
gap. It is not the cause: instrumented into the boot trace, the export resolves
to a valid far pointer (`1807:0498` on that boot) and is called exactly once per
switch, and the desktop keeps the stale contents regardless. Issued from inside
`ReEnable` it is apparently too early to survive whatever USER does next. The
call is now in the tree for the paths the samples use it on, explicitly
documented as *not* fixing this.

**2. `UserRepaintDisable` at ordinal 500. Ruled out, but a real gap closed.**
USER calls this exported entry point to tell a driver whether repaint requests
may be issued yet, and the enabling call is the DDK's deferred-repaint trigger
(`bRepaintDisable` / `RepaintPending`). It looked like the missing "USER has
finished" hook, and this driver did not export ordinal 500 at all. Measurement
says it is not this path's hook: with the export in place and instrumented,
**USER never calls it across a `ChangeDisplaySettings`** - no call, in either
direction, before or after the switch. It is presumably only used for the
full-screen screen-switch path. The export and the deferral are kept, since a
display driver is supposed to have them, but they do not fire here.

**3. The unpaired `VDD_PRE_MODE_CHANGE`. Ruled out, but a real bug fixed.**
`v9x_build_pdevice` opens every mode set with `V9xVddPreMode`, and the live
switch branch of `ReEnable` never sent the matching `VDD_POST_MODE_CHANGE` -
while the unchanged-mode branch beside it always has. So the master VDD was
left believing a mode change was still in flight. That is a genuine protocol
violation and is now fixed, and it does not change this symptom either.

## What is now known about the mechanism

Two measurements narrow it a long way.

**The framebuffer really does hold the stale image, and everything the driver
reports is correct.** The agent's screenshot is a GDI `BitBlt` of the screen DC,
so it reads through the driver's own mapping - and it shows the doubled image.
So this is not a scanout-only artefact. Meanwhile `C:\V9XBOOT.INI` reports
`Surface=pitch=2048 bpp=16 dwb=2048 dds=2048 w=1024 h=768 debpp=16` after the
switch, the guest-side GDI probe reports `Width=1024 Height=768
BitsPerPixel=16` with `Result=PASS`, and its drawing checks - `BlackPixel`,
`WhitePixel`, `RedPixel`, `BltPixel`, `SetPixel` - all read back correct. GDI
draws correctly into a correctly described surface.

**An Explorer desktop refresh repairs it completely.** Clicking an empty desktop
area and pressing F5 after the failed switch repaints the desktop crisply and
correctly at the new geometry, leaving only a couple of stale text fragments
where the taskbar had been - F5 refreshes the desktop, not the taskbar. So
nothing is wrong with the surface, the geometry or the driver's painting: the
desktop simply is never invalidated, and it repaints perfectly the moment
something asks it to.

Taken with the ruled-out cures, the remaining question is narrow and is
**not about the driver's own state**: what invalidates the desktop after a
resolution change on the emulated S3 targets and fails to on this machine, given
that the driver reports the change identically in both. Whoever picks this up
should probably start by establishing whether the **stock S3 driver** live
switches cleanly on BARRY. If it does not, this is a platform or hardware
property rather than a Velocity9x defect, and that reframes the whole issue.
That test was not run here because it means unbinding Velocity9x from the only
physical S3 target.

## Where to look

`ReEnable` in `src\display16\ddi.c` is the live-switch path. It rebuilds the
PDEVICE in place between `DIB_BeginAccess` / `DIB_EndAccess` with
`CURSOREXCLUDE`, re-registers the visible byte count with the master VDD and
refills GDIINFO - the design is recorded in
`docs\decisions\2026-08-10-dynamic-mode-switching.md`, which verified live
switching at 8 and 16 bpp on the **86Box ViRGE**, never on physical hardware.

Worth checking in roughly this order:

1. ~~Whether anything invalidates the full desktop after the rebuild, and
   whether Windows' own post-`ChangeDisplaySettings` repaint is arriving at
   all. A deliberate full-screen invalidate at the end of `ReEnable` would be
   the cheap test, even if it turns out to be papering over the real cause.~~
   **Done 2026-08-26 and it is not the answer at the end of `ReEnable`.** The
   invalidate half of this is confirmed - nothing invalidates the desktop, and
   an Explorer F5 repairs it - but a forced repaint issued from `ReEnable` does
   not work. See "Three cures ruled out" above. What is still unanswered is who
   is supposed to issue that invalidate and why the emulated targets get it.
2. Whether `V9xVddReregister`'s visible-byte count matters here: it is the one
   figure that differs between a 2 MiB and a 4 MiB card, and the master VDD uses
   it for save/restore sizing.
3. Whether the 15-second revert applet is involved - the switch reports success,
   so it should not be, but it has a hand in screen state around a mode change.

## How to reproduce

```
v9xctl exec -Application C:\V9XREMOTE\JOBS\<job>\V9XMSW.EXE `
    -Arguments "/set:800x600x32" -Host 10.0.1.47
```

then screenshot **several times over 30 seconds** and confirm the stale content
persists rather than resolving. Reboot and screenshot again for the clean
comparison. `V9XMSW.INI` reports `Result=PASS` and `ChangeResult=0` throughout:
the switch itself succeeds, only the repaint does not.

## Reconfirmed on 0.5.0, 2026-08-26

Driver 0.5.0, build `edd7684`, same card. A live switch from a booted
800x600x32 desktop to **1024x768x16** reports `Result=PASS` and
`ChangeResult=0`, and the desktop never finishes repainting. The 800-wide
desktop's stale framebuffer, read at the new 2048-byte stride, appears twice
side by side with a band of garbage between - the same signature as 0.4.3.

**One new observation, and it narrows the search.** Six captures at 5-second
intervals over 30 seconds are pixel-identical *except the taskbar clock, which
advances from 5:34 PM to 5:35 PM*. So GDI is still drawing, the driver is still
presenting those updates, and they land in the right place - the only thing
missing is invalidation of everything else. That kills the capture-race
explanation for good, and it makes suspect 1 in "Where to look" (nothing
invalidates the full desktop after the `ReEnable` rebuild) the strongly
favoured one over suspects 2 and 3.

Also worth recording for whoever picks this up: the nine reboot-entered modes
tested the same day were all clean, so the reboot/live asymmetry is unchanged.
