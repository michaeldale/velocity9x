# GT218: 3DMark 2001 SE Car Chase shows cleared, half-drawn frames

Date: 2026-11-16

Status: fixed in source on branch `gt218-flips` and measured on
W9QI0DAD boot 44: real flips through the VGA CRTC emulation; 16 of 16
sampled Car Chase frames complete (see the last section).

Machine: W9QI0DAD (10.0.1.172), ASUS GeForce 210, GT218 10DE:0A65,
`nvidia` family. The build was not recorded with the capture; the box
was on the row-block build from origin/main 4439b81a (boot 41 on) when
the clip was made.

## What the recording shows

`Y:\MWD\videos\obs\2026-11-16 18-49-11.mkv`, 4.9 s, 1080p60, Car Chase
(game test 1), 3DMark reporting 25-36 fps.

Each 60 Hz capture frame was reduced to the mean luma of 120 bands down
the 4:3 picture. A band at 0 is black, not dark scene:

- 295 frames: 75 entirely black, 171 part black, 49 complete.
- In a part-black frame the black is a horizontal region whose edge moves
  from frame to frame; the sky alone, the scene without the HUD, or the
  HUD alone over black all occur.
- Complete frames render correctly (lighting, sky, HUD).

The picture being scanned out is a buffer that has been cleared and is
being drawn into. Either Direct3D draws land in the visible buffer, or a
flip shows a buffer whose drawing has not finished.

## Lead (by reading, unmeasured)

The Haswell had the same picture in 2026-10-20's Lobby capture: draws and
engine clears landed in the buffer the last flip had released while it
was still being scanned. The fix was a wait for the pending flip in the
draw path (`v9x_flip_wait_done`), see
[2026-10-20-haswell-3dmark2001se-flicker.md](2026-10-20-haswell-3dmark2001se-flicker.md).
On origin/main d42d9fb7 only `d3d_g4.c`, `d3d_hsw.c`, `d3d_i9xx.c` and
`d3d_virge.c` call it; `d3d_nv50.c` does not. `eng_nv50.c`'s
`v9x_nv50_wait_idle` says "Direct3D's own batches drain without it."

Not checked: whether 3DMark ran double or triple buffered, and the HAL's
flip counters for this run (`V9XTRACE` snapshot: flips armed, completed,
`FlipStillDrawing`). Those are the first measurement.

## 2026-11-16: the flip-wait lead is wrong

The GT218 row has no display-start control, so `v9x_ddhal_flip` declines
every Flip to DirectDraw (`FlipDeclined` 68,206 in one 3DMark 2000 run,
[2026-11-14-gt218-3d-d3d-builders.md](../decisions/2026-11-14-gt218-3d-d3d-builders.md)).
No flip is ever pending, so `v9x_flip_wait_done` would return at once; a
draw-side wait cannot change this picture.

What DirectDraw does with a declined primary Flip has not been measured.
The 2026-11-14 record says it "presents every flip by its own copy". The
black frames say the application's clears reach the scanned-out memory at
least some of the time, which a copy-present would not do; one reading
that fits is DirectDraw swapping the chain's `fpVidMem` while the scanout
stays on the original offset, so the application draws into the visible
buffer every second or third frame. Unmeasured. A HAL counter of draws
and clears whose target is the mode-set primary offset would settle it.

The fix in either case is real flips: on NV50 the scanout base is a
method on an EVO channel (head 0 surface offset, method 0860h), which is
Phase 5 of [nvidia-gt218.md](../plans/nvidia-gt218.md), not started.

## 2026-11-16: flips through the VGA CRTC emulation; fixed

The EVO premise was wrong for the modes the VBE sets: the head scans out
through the VGA CRTC emulation, and the VBIOS's own VBE 4F07 writes the
start as CR0C/CR0D/CR35/CR34 behind CR3F. Probed on the card with the
picture read through OBS
([record](../decisions/2026-11-16-gt218-flips-through-vga-crtc.md)).
The GT218 row now declares FLIP and the HAL writes that start address
(`nv50_crtc.c`, host-tested; `v9x_nv50_set_display_start`).

Boot 44 (the branch's nvidia set, DRV/VXD/HAL/SETP hash-checked), 3DMark
2001 SE default run (1024x768x32, double-buffered): 16 OBS samples of the
HDMI output, 8 in Car Chase Low Detail and 8 in High Detail, all 16
complete frames with the HUD; before, 49 of 295 captured frames were
complete. Samples are single frames, not a recording, so a rare torn or
partial frame is not excluded. Evidence:
`build/driver-results/w9qi0dad-gt218-flips-boot44/` (`cc-sheet1.png`,
`cc-sheet2.png`).

Evidence: `build/driver-results/w9qi0dad-obs-2026-11-16/sheet1.png`
(2 fps contact sheet) and `c1_seq.png` (12 consecutive frames).
