# HD 4600: the vertex stage draws OpenGL correctly; it is at par with the CPU path, not ahead, and why

Date: 2026-10-26. Machine: the i5-4570 (W9QI5A83, HD 4600), boots 84 and 85,
800x600x16 desktop (565). Step 3 of
[`../plans/hardware-tnl.md`](../plans/hardware-tnl.md). Evidence:
[`../probe/hsw-4570-tnl-step3-2026-10-26/`](../probe/hsw-4570-tnl-step3-2026-10-26/).

## What ran

- **The OpenGL path:**
  - The ICD captures filled, unfogged, smooth, single-unit primitives as
    object-space vertices and sends them through render interface ABI 7's
    `draw_tnl`.
  - The HAL draws them with the vertex stage on: a position-only VS kernel
    (`v9x_intel_hsw_vs_kernel_position`), push constants through
    CONSTANT_VS behind the PRM's dummy primitive, CLIP in NORMAL/D3D mode,
    and the SF viewport transform.
  - The CPU still transforms the same triangles. Its batch is the fallback
    when the engine refuses, and was never needed: 0 refused, 0 failed in
    every run below.
- **Boot 84:** first run. The full set (DRV, VxD, HAL, SETP, GL, Glide 2,
  Glide 3, 3DCIF) by WININIT rename.
- **Boot 85:** the indexed HAL and ICD. Later ICDs swapped alone with no
  GL program running.

## Correctness

The V9XGLP A/B scene draws the same frame with T&L and with `V9XGL_TNL=0`
(the CPU path) and compares them. The scene has an offset viewport, a
narrowed depth range, a culled perspective cube, a triangle cut by the near
plane, a quad 120 units wide (the guard band) and a textured quad.

| Measure | Result |
|---|---|
| Pixels drawn | 26,578 of 100,800 |
| Pixels differing by more than 3/255 | 1,800 |
| Differing by more than 8/255 | 112 |
| Differing by more than 16/255 | 0 |
| Worst channel difference | 9/255 |
| Pixels drawn outside the viewport | 0 |
| Same numbers with the indexed form (boot 85) | yes |

The difference image (`V9XTNLD.png`, x16) shows where the frames disagree:

- 565 colour-step contours one pixel apart on the Gouraud ramps of the
  clipped triangle and the wide quad;
- a seam on the wide quad where the CPU's clipper re-triangulates it.

Nothing differs on the cube, its culled faces or the texture. One 565 step
is 8/255, so these are interpolation-rounding differences. None is a
geometry difference.

Half-Life GL ran windowed through the vertex stage, with 3.03 M triangles
in 792,803 batches on the first build. World, view model, HUD and sky were
right in every frame captured (`hl-gl-tnl-frames.png`).

## Speed: where the 40% went, and what came back

Half-Life `timedemo mwd5`, windowed, 640x480. "Off" is `V9XGL_TNL=0`, the
same build's CPU path.

| Build | T&L on | Off |
|---|---|---|
| One draw per captured batch, unmerged | 24.1 | 84.7 |
| Batches merged as screen batches are | 50.8 | 84.6 |
| Indexed: each vertex sent once | 52.3 | 85.0 |
| Same, with merge-break counters | 58.6 | |
| Batches with nothing surviving the CPU's cull not sent | 79.1, 78.9 | 85.3, 84.8 |

Quake 3 demo `timedemo demo001` (fullscreen, its own config) on the last
build: 56.6 fps with T&L, 57.0 without.

### Hypotheses the measurements killed

- **"The uncached vertex copy dominates."** The aperture is UC: every MTRR
  pair is the BIOS's. Indexing cut the copied bytes to about a third and
  moved the result 1.5 fps.
- **"T&L batches are dearer on the GPU."** Inside the timedemo the cost per
  flush was 60 µs with T&L and 58 µs without.
- **"Batches break on matrix changes."** The ICD's counters for the
  indexed run: 40,655 breaks on texture or fragment state, 0 on constants,
  0 on the frame, 1,281 behind a held screen batch, 139 for room.

### What was left

The number of draws:

- 126 a frame with T&L against 75 without.
- The CPU path never makes a batch for a primitive it culls or clips away.
  The T&L path sent every one: 1.49 M object triangles where the CPU kept
  0.62 M, 58% of them back-facing or off-screen.

### The fix

While the CPU twin still exists, its count is exact: a captured batch whose
twin is empty draws nothing and is not sent. That took T&L batches from
42,086 to 24,129 and the timedemo to within 7% of the CPU path. Quake 3 was
then at par.

## What this means for the plan

- **Correctness:** step 3's gate is met for OpenGL. The A/B scene is within
  rounding, Half-Life and Quake 3 run correctly, and nothing was refused.
- **Speed:** step 3 cannot be faster than the CPU path, because the CPU
  path still runs underneath it. The gain has to come from removing the
  CPU's transform. What that needs:
  - A visibility answer for skipping empty batches that does not need the
    full transform. For Half-Life's 3-6-vertex primitives any per-vertex
    test costs nearly what the transform does. A test per batch or per
    vertex array is where it pays.
  - Capturing `glDrawElements` and `glDrawArrays` from the arrays
    themselves, vertices once and indices as given. Today they reach the
    capture one `glVertex` at a time, so Quake 3's shared vertices are
    sent again per index.
  - A CPU fallback that transforms the held object vertices, host-tested
    against the CPU path's window positions as `test_tnl_window_matches_cpu`
    already does.
- **Where T&L should win clearly:** Direct3D 7 vertex buffers (step 6),
  which the CPU never touches after the application fills them. Half-Life
  1's immediate mode is close to the worst case.
- **Direct3D unchanged:** the shared Haswell changes did not disturb the
  Direct3D path. 3DMark 2000 Pro's default run scored 2346; 3DMark 2001 SE
  ran to completion and scored 1112, saying it was not at default settings
  (this machine's saved configuration). Neither offers a T&L device to the
  benchmark yet.

## Not checked

- Ivy Bridge: `tnl_available` refuses it.
- GL with a 32-bit desktop.
- Two-unit captures and fog: not captured in step 3.
- The x2 polygon-offset question from the specification note: polygon
  offset is not captured.
