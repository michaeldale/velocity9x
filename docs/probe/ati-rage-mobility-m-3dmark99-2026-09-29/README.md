# Rage Mobility-M: first 3DMark 99 Max run on hardware Direct3D

Gateway Solo 2150, Velocity9x bound, HAL from `e0b04db` (textures 8 to 256,
mipmaps with level selection), boot 30. 3DMark 99 Max, default test
selection, 640x480x16, 16-bit Z, triple buffering (`SETTINGS-640X480.png`).
Started and driven through the remote agent.

## Result

The run completed with all 26 tests, and without a lock.

- Score: 457 3DMarks, 6542 CPU 3DMarks (`SCORE-457.png`). Recorded, not
  compared with anything.
- HAL counters across the run (`V9XSNA-BEFORE.INI`, `V9XSNA-AFTER.INI`):
  - 162,902 batches reached the engine, carrying 5,871,389 triangles
    (41,349 of them zero-area);
  - 147,124 batches were textured, 152,800 depth-tested and 31,633
    blended;
  - 11,479 draws were refused: about 7% of those submitted. The last
    refusal was a vertex refusal (engine reason 6), and the last policy
    refusal was fog with texture (policy reason 16);
  - 36,160 textures or chains were placed by the HAL;
  - zero FIFO timeouts, zero idle timeouts and zero resets.

## Screenshots

The agent captures through GDI. During page flipping that can be a buffer
other than the one on the panel, and three of the eleven captures were
black. What is black on the panel is not established.

- `ROCK-TEXTURE.png`: a scene, probably the bump-mapping test, textured and
  filtered correctly.
- `TEXTURE-TUNNEL-WRONG.png`: the texture rendering speed tunnel. Walls are
  flat averaged colours with radial streaks, not texture detail. This fits
  a very small mip level being sampled, or texture coordinates wrong under
  perspective. Not investigated.
- `LOADING-GAME2.png`: a between-test loading screen.

## Open

- The texture tunnel's wrong texturing. (Addressed below.)
- The two game tests finished within about 25 seconds each. Whether
  refused or wrongly drawn batches shortened them is not established.
- Which draws the 11,479 refusals were.

## The tunnel, and runs 2 and 3

The probe had drawn every triangle with rhw 1, so perspective had never
been tested. New scenes (`V9XDDT-PERSPECTIVE-FIXED.TXT` is after the fix)
found two defects.

- **W squared.** `TEX_CNTL` bit 19 clear is `TEX_ST_MULT_W`
  (xf86-video-mach64 `atiregs.h`): the engine multiplies S and T by W
  itself. The builder premultiplied as well, which is Mesa's convention,
  but Mesa uses it with `TEX_ST_DIRECT`. Symptoms:
  - a uniform rhw k scaled every coordinate by k and moved the mip level
    by log2 k;
  - on a perspective row, u never passed 0.25.

  The builder now sends plain S and T. Afterwards the uniform-W scenes
  are invariant, and the row's green/blue boundary falls between x 42
  and 46, against 44.8 computed.
- **Coordinate range.** A 64-texel texture sampled right at u 1000.1 and
  wrong at 10000.1. For WRAP, the HAL now moves each triangle's s and t
  down by the integer below their minimum.

Ruled out by measurement:
- W ratios to 10,000:1;
- a far-vertex u of about 860,000;
- all eight depth comparisons at equal depth.

Run 2 (431 3DMarks, 6575 CPU) and run 3 (404, 6517; wrap rebase added) completed
with no lock, zero timeouts and zero resets. Game 1, Game 2 and the texture
grids draw correctly (`RUN2-*.png`). The tunnel frames
(`RUN2-TUNNEL-POINT-MIP.png`, `RUN3-TUNNEL-TRILINEAR.png`) still show some
walls as checkerboards and some as radial streaks.

A bounded HAL dump of the tunnel's triangles (`M64TRI-TUNNEL.TXT`, since
removed) shows each wall drawn twice at the same positions:

| Pass | Coordinates | Blend | Depth |
|---|---|---|---|
| 1 | u, v up to 1 | none | LESSEQUAL, write |
| 2 | u, v up to 21 | DESTCOLOR x ZERO | LESSEQUAL, no write |

The streaky walls are pass 1 without pass 2 on top. No draw was refused
during the test. Both passes of one of these triangles, replayed at full
size in the probe's 640x480 target (`V9XDDT-TUNNEL-REPLAY.TXT`), give the
exact perspective pattern at all 60 grid points (`TunP_*` against `Tun_*`).

The screenshots are GDI captures, which under triple buffering can read a
frame still being drawn. Whether the panel shows the streaks is not
established; it has not been looked at.

Two further findings:
- The engine chooses mip levels 1 to 1.5 coarser than Direct3D across a
  tunnel wall (`TunL_*`).
- `TEX_CNTL` LOD_BIAS values 0, 1, 2, 4, 8, C and F changed no level
  choice in the chain sweep or on the wall.

## Run 4: trilinear, specular, fog over textures

HAL from `4225265`, boot 43, same settings. The probe passed first,
including `D3DFogTex` and the specular scene. The run completed with all 26
tests and no lock.

- Score: 363 3DMarks, 6582 CPU 3DMarks (`RUN4-SCORE-363.png`). Recorded,
  not compared.
- Counters across the run (`RUN4-V9XSNA-BEFORE.INI`, `-AFTER.INI`):
  - 160,996 batches, 6,055,322 triangles (64,593 zero-area);
  - 147,378 textured, 150,915 depth-tested, 24,331 blended and 9,587
    fogged;
  - 193 draws refused, about 0.1% of those submitted, against 11,479 in
    run 1. The last was a vertex refusal (engine reason 6); the last
    policy refusal was a texture format (policy reason 7);
  - zero FIFO, idle or flip-wait timeouts, zero resets, zero mip chain
    gaps.
- `RUN4-GAME1-RACE.png`: Game 1 with a fogged, mipmapped road.

Which of the three changes removed most of the refusals was not isolated;
fog over a texture (policy reason 16, the last refusal in run 1) is the
likely one.

## Runs 5 and 6: the last refusals were whole batches lost to one triangle

Run 5 (boot 44, 366 3DMarks, 6582 CPU, `RUN5-SCORE-366.png`) carried a
temporary, uncommitted HAL log of the first 96 refusals (`RUN5-M64REF.TXT`).
Each line is `R` and hex words: the refusal (`62` a setup refusal, `3`
policy), the policy reason, then the request's texture, filter, blend, fog,
specular, alpha-test and target fields, then each corner's sx, sy, sz, rhw,
tu and tv as IEEE float bits.

- The first 38 are the probe's deliberate refusals: 36 forced alpha, one
  colour key, and one policy reason 7 whose texture did not resolve
  (format all-ones, 0x0). That last one is the "texture format" of run 4's
  counters; it is the probe's, not 3DMark's.
- The other 58 were 3DMark's, and every one was the setup builder refusing
  a vertex, on a mipmapped 128x128 LINEARMIPLINEAR texture. Two shapes:
  - zero-area triangles whose unused corners are all zero, rhw 0
    included; the rhw check ran before the area test;
  - a real triangle with NaN tu and tv on one corner.

  A refused batch fails the whole render primitive in the core
  (`d3d_core.c`, `PRIMREJECT`), so one such triangle cost every triangle
  after it.

Where the NaN coordinates and the zero corners come from, 3DMark or the
core's primitive expansion, was not established.

The fix: the setup builder tests area first, and returns
`V9X_STATUS_INVALID_STATE` for a textured triangle whose W, S or T is not
finite or whose W is not positive. The HAL skips that triangle alone and
counts it in `M64Unrenderable` (ABI `2026092902`).

Run 6 (boot 45, 359 3DMarks, 6575 CPU, `RUN6-SCORE-359.png`, counters
`RUN6-V9XSNA-*.INI`): 3DMark caused no refusals; the 38 are the probe's.
1,944 triangles were skipped as unrenderable, and 72,782 as zero-area
against 65,161 in run 5. Zero timeouts and resets. The probe's failing keys
are a subset of the earlier committed log's.

## How the engine picks a mip level

New probe scenes, all on MIPNEAREST chains, level 0 red, 1 green, 2 blue,
3 and below magenta. `V9XDDT-LOD-FLOOR-REBASE.TXT` was taken with the old
rebase, `V9XDDT-LOD-CENTRED-REBASE.TXT` with the new one (boot 47).

Affine (uniform rhw):

| Rate (texels a pixel) | 1.4 | 2.0 | 2.8 | 4.0 | 5.6 |
|---|---|---|---|---|---|
| Direct3D nearest | 0 | 1 | 1 | 2 | 2 |
| `LodX`, `LodY`: one axis stretched | 1 | 1 | 2 | 2 | 3 |
| `LodR`: turned 45 degrees in texture space | 0 | 1 | 1 | 2 | 2 |

- The engine takes the largest single rate, u or v in x or y, not the
  gradient's length or a sum: at 45 degrees each rate is ratio/sqrt 2.
- Its boundaries are near 1.1, 2.2 and 4.4 texels a pixel, against
  Direct3D's 1.41, 2.83 and 5.66. `LodE*` swept 32, 128 and 256 chains
  of full and 6-level length: all read `001112223` at rates 0.8 to 4.8.
  About a third of a level coarser, whatever the size; `TEX_CNTL`
  LOD_BIAS had no effect, so this part stays.

Perspective:

- `PLodA` (u 0 to 2 towards the rhw 0.25 corner) read `11111111222`,
  `PLodB` (u 2 to 0) `22222233333`: the same rate at every pixel, a level
  apart. The engine's estimate depends on u.
- Fitted: the rate is the per-triangle gradient of S*W (and T*W) divided
  by the pixel's W. The true derivative is (d(sW) - s dW) / W; the engine
  drops the s dW term, so its error at a pixel is that pixel's s times
  dW/W. The model reproduces both rows and the 60-point tunnel grid
  (`TunL_*`) to within a cell or two.
- The HAL chooses s's origin for wrapped textures, one whole number a
  triangle. It used the integer below the minimum, which makes every
  error the same sign. It now uses the integer nearest the
  perspective-correct centroid, sum(s rhw) / sum(rhw).

After the change (boot 47): `PLodB` reads `11111111222`, like `PLodA`. The
tunnel's wall, against Direct3D:

| Row | Direct3D | Before | After |
|---|---|---|---|
| y 50 | `000` | `011` | `000` |
| y 150 | `00000000` | `01111112` | `00000001` |
| y 240 | `000000001123` | `011111222333` | `000000111223` |
| y 330 | `00000000` | `11111122` | `00000011` |
| y 430 | `000` | `111` | `000` |

The tunnel replay's texture pattern (`Tun_*`, `TunP_*`) is identical at
all 60 points to the earlier run, so the shift moved the level choice
and nothing sampled; every wrap cell of the texture matrix passes with
the centred, now partly negative, coordinates. Clamped textures cannot
be shifted and keep the error.

Run 7 (boot 48, HAL built from the tree of `54a78b7` before it was
committed, so its build id is the earlier one): 363 3DMarks, 6576 CPU
(`RUN7-SCORE-363.png`, recorded and not compared). No refusals from
3DMark, 1,950 triangles skipped as unrenderable, zero timeouts and
resets (`RUN7-V9XSNA-*.INI`). The bilinear mipmapped tunnel
(`RUN7-TUNNEL-CENTRED.png`) shows a checkerboard on every wall to the
vanishing point; in runs 2 and 3 some walls were radial streaks. It is
still a GDI capture; the panel was not looked at.
