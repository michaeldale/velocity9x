# Oland: cube maps and the signed bump formats in pixel shaders

Date: 2026-11-17

Status: implemented and measured on W9QI0DAD (Oland XT `1002:6610`,
FX-6300, Win98 SE) at boot 60. The ati package was built from this
commit's tree (`402c2d92-dirty`). Its DRV, VXD, HAL and SETP were put in
place by WININIT rename and hash-checked. This is step 6 of
[ati-gcn-ps11.md](../plans/ati-gcn-ps11.md), on the Oland alone.

Evidence: [`../probe/oland-cubes-signed-w9qi0dad-2026-11-17/`](../probe/oland-cubes-signed-w9qi0dad-2026-11-17/).

## Why

Since step 5 the core claims cube maps and publishes Q8W8V8U8 and V8U8
on the Oland, because it has `draw_shader` (`v9x_d3d_cubes_claimed`, the
DDI 8 format list). The HAL refused both at draw time: reason 10
(texture shape) for a cube stage or a texm3x3 lookup, and reason 9
(texture format) for a signed texture. V9XD32 T19 Cube and Normal
mismatched, and in 3DMark 2001 SE those draws were dropped.

## The change

This follows the HD 4600's steps 5c and 5d
([cubes](2026-10-30-haswell-ps11-step5c-cubes.md),
[bump formats](2026-10-30-haswell-ps11-step5d-bump-formats.md)), on GCN's
terms.

- **Cube layout** (`v9x_vi_3d_cube_layout`, `vi_3d.c`). A cube is an
  array of six slices. Each level has the pitch the measured mip layout
  gives it, and its slice is already whole 256-byte blocks. The levels
  go one after another, all six faces of a level before the next level:
  face f of level n is at `offsets[n] + f * slices[n]`. That is the order
  radeon's linear surface code gives an array; it was not measured here
  beyond level 0 (below).
- **Cube placement** (`v9x_d3d_vi_create_cube`, `d3d_vi.c`).
  - The runtime's CreateSurface list for a video-memory cube (six faces,
    each face's top and then its levels, each naming its face in
    DDSCAPS2, as the HD 4600 recorded at its boot 131) is placed as one
    block. Each surface's place comes from its face bit and its edge.
  - A list it cannot account for is left to DirectDraw.
  - The cube is remembered by its +X top's offset, in a 64-slot table.
    `destroy_surface` forgets it with the block.
- **Cube T#** (`vi_3d.c`): TYPE CUBE (11), LAST_ARRAY 5 in word 5, DEPTH
  0, as radeonsi's `si_make_texture_descriptor` writes a single cube.
  The S# clamps, since the face coordinates step 2's translator computes
  (`v_cube*`) are on [0, 1].
- **Signed formats.**
  - `v9x_d3d_vi_signed_format` (`d3d_vi_map.c`) knows Q8W8V8U8 and V8U8
    by D3DFORMAT under DDPF_D3DFORMAT or DDPF_FOURCC, and V8U8 also by
    DirectX 7's DDPF_BUMPDUDV.
  - They are 8_8_8_8 and 8_8 with NUM_FORMAT SNORM, read in memory
    order: U V W Q as X Y Z W, and U V as X Y with 0 and 1 for Z and W.
    The 0 and 1 copy the HD 4600's R8G8_SNORM; Direct3D's own choice is
    not checked, and no measured test reads V8U8's blue or alpha.
  - `create_surface` already placed every texture format the engine
    samples, so a signed texture now gets its place from the engine.
    DirectDraw cannot size Q8W8V8U8, which the runtime describes as a
    FourCC with no bit count.
- **`draw_shader`.**
  - A stage whose texture is a placed cube is sampled as one. The core
    marks no stage a cube, so the HAL builds the mask itself and keys
    its program cache on it.
  - The step 5 refusals of cube stages and of texm3x3tex, texm3x3spec
    and texm3x3vspec are gone. The translator still refuses a texm3x3
    lookup on a 2D stage, as reason 15.
- **The fixed-function map** refuses a cube (reason 10) and a signed
  texture (reason 9). The core draws those stages through `draw_shader`.
- **Host tests.** They were written first and failed against stubs: 23
  checks, then 4 of them again from a test bug, a V8U8 pitch narrower
  than its width. All pass now.
  - `test_vi_3d.c`: the cube layout for T19's 8x8 two-level cube, a 1x1
    V8U8 face, and the refusals.
  - `test_d3d_vi_map.c`: the cube and signed T# words, the
    fixed-function refusals, and the format recognition.

## Measured, boot 60

V9XD32 three times: with a 16 bpp desktop (R5G6B5, T19 tolerance 9)
without and with `/bpp16`, then with a 32 bpp desktop (X8R8G8B8,
tolerance 2, after `V9XMSW /set:1024x768x32`). The first run's folder was
deleted when the 16 bpp desktop came to light; its verdicts, read before
that, were the second run's. The evidence is the second and third runs
(`d32-desktop16-boot60`, `d32-desktop32-boot60`).

- T12 to T16 pass in all three runs. T18: no T&L device, as before.
- T19 Cube: 1,053 pixels inside (41Dh), 0 mismatches, in all three. It
  reads all six faces at level 0.
- T19 Normal (Q8W8V8U8 through `mad r0, t0, c0, c0`): 1,040 inside
  (410h), 0 mismatches, in all three.
- Every other T19 row is as at step 5, Dxt1 and Dxt3 skipping (step 7)
  and FfBemL8 and FfBemL6 skipping (no luminance bump op).
- FfSpecFog still mismatches: 3 pixels at R5G6B5, as at step 5 and on
  the GT218. At X8R8G8B8 it is 17 pixels, the first at (11, 49), got
  5AD0E4h for 5ACDE4h: green 3 steps over a tolerance of 2. Not traced.

T19 reports FAIL, from FfSpecFog alone.

3DMark 2001 SE, default benchmark (17 tests), with the 32 bpp desktop:

- It completed in 7.1 minutes with 3DMark score 3101. Step 5's
  completed run scored 1915 (recorded, not compared).
- Correction (step 7, same day): the CP stalled during this run.
  `EngineIdleTimeouts` was 0 in the snapshot before it and 1 after. The
  CP record it left (`3dmark2001se-boot60/V9XCZCP.TXT`) has the hang's
  signature: CP_STAT 84038243h, GRBM_STATUS A0003028h, a full ring,
  fences 20 behind. The HAL gave the CP up and the run went on to its
  score window with no 3D engine for some part of it.
  - So the score is not a measure of this build. 
  - "No hang", as first written here, was wrong: the machine stayed up,
    but the stall happened. See the
    [issue](../issues/2026-11-17-oland-3dmark2001se-cp-stall-hang.md).
  - When in the run it happened is not known.
- `V9XPS.LOG` keeps its lines across boots. Step 5's file is a prefix of
  this one, so the 6,000 lines after it are boot 60's (V9XD32's three
  runs, then 3DMark):
  - no `ps-refused-draw` line. At step 5 there were refusals for
    texture shape (reason 10) and format (reason 9).
  - six cubes made. V9XD32's three (8x8, two levels) and three of
    3DMark's (256x256, one level) were placed by the engine. One 256x256
    cube was declined once (`cube-created 0`) and placed when the
    runtime asked again at once. Why it was declined is not known; no
    reason was logged.
- The frame grabs cover the Car Chase tests only. No Nature frame was
  seen (step 8).

## Not done or not known

- **A cube's smaller levels.** T19 Cube magnifies and reads level 0. The
  level-major slice order is radeon's linear rule. On the Oland no level
  past 0 of a cube has been read.
- **V8U8 in a draw that is judged.** T19 Bem and FfBem passed before
  and after this change. The signed format a draw reads here was
  Q8W8V8U8 (Normal).
- **The luminance bump formats** (X8L8V8U8, L6V5U5) are not published.
- **Carrizo and Mullins** still have no `draw_shader`.
