# The physical Trio64's "high-colour tiling" was a screenshot race

Status: **not a defect. Withdrawn 2026-08-20**, same day it was filed. The
driver is correct; the measurement was wrong. Kept because the way it was got
wrong is worth not repeating.

Target: BARRY, physical S3 Trio64 (86C764), 2 MiB, Windows 98 SE.

## What was claimed

That branch `high-depth-dynamic-modes` broke every mode above 8 bpp on BARRY -
the desktop rendering tiled at half width - and that it was a regression,
because a capture taken on the same card in the same mode with the 0.4.2-era
driver was clean and re-deploying a `main` build restored a clean desktop.

## What was actually happening

The agent's `screenshot` was catching the desktop **mid-repaint after a live
mode switch**. BARRY is a 32 MB machine; redrawing the desktop takes longer
there than the round trip that captures it, so the capture contained the
previous mode's framebuffer content being progressively overwritten - which,
read at the new mode's stride, looks exactly like a stride error.

Taking a second capture a few seconds later, with no other change, returns a
clean desktop. That single extra call is what dissolves the whole thing.

Verified afterwards on BARRY with the branch driver, each capture allowed to
settle:

- fresh boot at 1024x768x16 - clean
- live 8 -> 16 depth switch - clean once settled
- 800x600x32 - clean, `Surface=pitch=3200 bpp=32 dwb=3200 dds=3200 w=800 h=600
  debpp=32`
- 1024x768x32 and 1280x1024x16 - correctly refused, `DISP_CHANGE_BADMODE`,
  because they need 3 MiB and 2.5 MiB on a 2 MiB card

So the new 32-bpp rows work on real hardware, and the `ValidateMode` VRAM check
does what it was added for.

## Why the "evidence" looked so convincing

Every step of the false conclusion was individually reasonable, which is the
part worth remembering:

- **The control comparison was not like-for-like.** The clean 0.4.2 capture
  (`build\driver-results\settings-tab-042`) was taken after a *boot*, on a
  settled desktop. Every tiled capture came moments after a *live mode switch*.
  The variable that actually differed was time-since-repaint, not the driver.
- **Re-deploying `main` did "fix" it** - because a deploy ends in a reboot and a
  screenshot taken after `wait-desktop`, by which point that desktop had
  finished painting. It would have "fixed" a driver with no bug in it.
- **The reboot that appeared to rule out a repaint artifact did not.** That
  capture was also taken immediately after `DesktopReady`, which reports the
  shell being up, not the desktop being drawn.
- **The driver's own numbers were right the whole time.** The `Surface=` line
  added while chasing this reported `pitch`, `deWidthBytes` and `deDeltaScan` in
  agreement at every depth. That should have been read as "the driver is fine,
  suspect the observation" rather than "the corruption must be below GDI".

The `d32-barry-1` deploy record and the tiled captures are left in place as the
worked example.

## The actual lesson

`docs\issues\2026-08-16-tier0-defects-deferred.md` D5 says a green GDI-side
matrix is not evidence the display works, and the answer to that is to look at
the screen. This is the other edge of the same knife: **a single screenshot is
not evidence the display is broken, either.** On a slow guest a capture is a
sample of an animation, and one frame of a repaint is indistinguishable from a
stride bug.

Anything that screenshots after a mode change should either settle first or
capture twice and compare. Worth building into the mode matrix, which currently
gets away with it only because it reboots between modes and captures late.

## What did come out of it

Two real fixes, both kept:

- `tools\diag\win16_driver_loader.c` asserted that `ValidateMode` **rejects**
  1280x1024x8, as its "unsupported mode incorrectly accepted" check. That mode
  is now supported, so the probe exited 5 - and `update-associated-driver.ps1`
  runs it as preflight, which blocked every deploy once the branch driver was
  installed. It now asks about 2048x1536, outside anything these cards scan out.
- `src\display16\ddi.c` publishes `Surface=` to the boot trace: the table's
  pitch and depth beside the layout the DIB Engine actually settled on. Nothing
  published these before, and their agreement is what ruled the driver out here.
