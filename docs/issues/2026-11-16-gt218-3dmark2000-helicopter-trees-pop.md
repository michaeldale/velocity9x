# GT218: trees pop in and out in 3DMark 2000's helicopter test

Date: 2026-11-16

Status: open. Seen in one OBS recording (Michael); cause not established,
not compared against another driver.

Machine: W9QI0DAD (10.0.1.172), ASUS GeForce 210, GT218, `nvidia`
family; build as in
[the Car Chase record](2026-11-16-gt218-3dmark2001se-car-chase-cleared-frames-on-screen.md).

## What the recording shows

`Y:\MWD\videos\obs\2026-11-16 18-49-39.mkv`, 7.0 s, 1080p60, Game 1
(Helicopter).

- At about 4.6 s a foreground tree at the left is absent for three
  capture frames, then present at full opacity in the next. No fade; the
  camera barely moves across those frames.
- At about 5.5 s (mission time 10.3-10.5) a tree at the lower right
  changes strength between nearby frames: solid in some, faint or partly
  missing in others.
- Distant trees along the ridge come and go over the clip.
- The sky does not flip between cloud and white in this clip (sky luma
  changes smoothly 192 to 117 and back with the camera), so the
  2026-11-15 sky observation did not recur here.

A first review of this clip at 2 fps called it clean; the tree changes
fall between those samples. Review such clips frame by frame.

## Open questions

3DMark 2000 may cull trees at a fixed distance on any card, so a pop-in
alone is not proof of a driver defect; the frame-to-frame fading of the
right tree is less likely to be the application. Candidates, none tested:

- alpha test or the alpha reference on the tree billboards;
- a tree texture not resident when drawn;
- depth or fog state that changes from one frame to the next;
- the cleared-buffer scan-out of the Car Chase record, if a buffer is
  shown before the trees (drawn late, blended) reach it. Every Flip on
  this row is declined (no EVO flips yet), so the application may be
  drawing into the scanned-out memory; trees, drawn after the terrain,
  would then be missing from whatever part of a frame the beam passes
  first. The 2026-11-15 cloud/white sky flicker fits the same reading.

The comparison that separates application from driver is the same scene
on another card under Velocity9x, or on the GT218 under a native driver.

Evidence: `build/driver-results/w9qi0dad-obs-2026-11-16/c2_lefttree.png`,
`c2_righttree.png` (16 consecutive full-resolution crops each),
`c2_trees.png`, `sheet2.png`.

## 2026-11-17: a likely cause, fixed; this test not re-run

On the Oland, 3DMark 2001 SE Nature's alpha-tested leaves vanished
because DX5 MODULATE multiplied in the vertex's alpha, which Direct3D
ignores on a texture with alpha. The GT218's map did the same.
448e2f2d keeps the texel's alpha. On M-BXMFA8's GeForce 210, Nature's
later scenes went from bare trees to trees in leaf
([record](../decisions/2026-11-17-gt218-dx5-modulate-keeps-texel-alpha.md)).

Daniel reported the same helicopter popping on his HD 5450 in October,
only under 3DMark's "Pentium III" CPU optimisation. That fits vertex
alpha as the variable. This helicopter test has not been re-run on the
fix, so the issue stays open until it is.

## 1.0 re-read, 2026-11-26

Re-run owed on the release candidate (the 1.0 plan's re-run list); if
the run does not settle it, it becomes a README known issue. The likely
cure (448e2f2d) is in; the Helicopter test has not been re-run on the
GeForce 210. The README's GeForce 210 entry names it until it is.

## 2026-11-26: re-run, trees steady

3DMark 2000 on the GeForce 210 (W9QI0DAD, boot 158, the 0.61.0 tree,
`VSync=1` for the run), recorded through OBS. In 48 consecutive capture
frames of Game 1 low detail, mission time 9.7 to 11.2, where the
recording above saw the pop, every foreground and ridge tree is present
at full strength in every frame; no engine timeouts. Closed. Only that
span was reviewed frame by frame, and the old recording's unsynced flips
are not excluded as a contributor
([record](../decisions/2026-11-26-1.0-reruns-gt218-netbook-gm965.md)).
