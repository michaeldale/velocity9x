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
