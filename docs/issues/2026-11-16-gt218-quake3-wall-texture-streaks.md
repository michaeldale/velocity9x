# GT218: Quake 3 wall texture drawn as horizontal streaks at some distances

Date: 2026-11-16

Status: open. Seen in one OBS recording; cause not established.

Machine: W9QI0DAD (10.0.1.172), ASUS GeForce 210, GT218, `nvidia`
family, OpenGL ICD; build as in
[the Car Chase record](2026-11-16-gt218-3dmark2001se-car-chase-cleared-frames-on-screen.md).
Resolution and depth of the run were not recorded.

## What the recording shows

`Y:\MWD\videos\obs\2026-11-16 18-51-01.mkv`, 6.5 s, 1080p60, Quake 3
demo, a stone room with torches.

- The far wall, and parts of the left wall, are sometimes drawn as
  horizontal streaks: each streak one colour across its width, in the
  wall's browns, instead of the stone blocks.
- From nearly the same position one frame shows clean stone and the next
  streaks; the change follows distance, not time.
- Within one frame the streaked region ends at a sharp vertical edge
  beside clean stone of the same wall (`c3_crop.png`).
- The top rows of the screen are clean; the row-block defect fixed in
  4439b81a does not show.

## Lead (unmeasured)

Distance-dependent and confined to a face's region suggests one of the
texture's smaller mip levels is stored or addressed wrongly (a row pitch
or tiling mismatch would give rows of one colour). A host-side probe
that uploads a mipmapped texture and reads every level back, or a V9XGLP
run with forced levels, would test it. Not checked: whether the streaks
are on one texture only, and the ICD's texture log for that run.

### 2026-11-16: the mip lead does not fit the code

The GT218 Direct3D path samples level 0 only: `d3d_nv50_map.c` reads a
mip filter as its filter within level 0 and refuses a draw whose levels
are in CPU memory to the fallback. A bad smaller level cannot be sampled,
so the lead above is wrong for GPU-drawn batches. What remains, unchecked:

- batches refused to the fallback (levels in CPU memory) drawn wrongly
  by it, with the sharp edge being the boundary between a GPU-drawn and a
  fallback-drawn triangle;
- texture coordinates lost on triangles clipped in software (near plane
  or guard band), which also changes with distance.

A run with the ICD's and the HAL's refusal counters, and F12 screenshots
of a streaked view, is the first measurement.

Evidence: `build/driver-results/w9qi0dad-obs-2026-11-16/sheet3.png`,
`c3_streak.png`, `c3_crop.png`.

## 1.0 re-read, 2026-11-26

Stays open for 1.0 and is named in the README's known issues.
Unmeasured; the README's GeForce 210 entry names it.
