# The software engine draws Quake 2's sky darker and without blue

Found 2026-09-26 on 86Box `Win98SE-Fast-D3D` and on MICHAEL-NETBOOK with
every texture on the software fallback. Open.

## Symptom

In demo1's first room the sky shows through a hole in the ceiling. Region
averages over the same pixels:

| Renderer | R | G | B |
|---|---|---|---|
| Software engine (either machine) | 42 | 19 | 0 |
| Gen3 sampling the texture | 61 | 47 | 35 |
| Quake 2's software renderer | 54 | 47 | 31 |

Evidence in `docs/decisions/2026-09-26-phase5-quake2-gen3-hardware-textures.md`
and the files beside it.

## Known

- Quake 2 sends the same draws to both engines: textures 1383, 1384 and
  1386, 256x256 RGB565, one level, REPLACE (normalised to REPLACE/REPLACE),
  blend off, depth LEQUAL with writes, white vertices.
- The sky is drawn: with `gl_clear 1` the hole never shows the pink clear.
- The same state on uniform and gridded probe textures, at Quake 2's
  frustum and distances, draws the same on both engines.

## Next

Capture the sky batch itself: log one sky draw's vertices (sx, sy, sz,
rhw, tu, tv) from the ICD, replay them on the host through d3d_raster.c
with the real sky texels, and compare with the expected sample.
