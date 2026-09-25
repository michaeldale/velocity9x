# 3DMark99 with mip trees: the game tests render, 246 at 1024x576 and 717 at 640x480

2026-09-25, MICHAEL-NETBOOK, boot 8, build `98b9bd6` (Gen3 mip trees).
3DMark 99 Max, all tests, 16-bit colour, 16-bit Z, triple buffering, one
pass at each resolution. The previous run on this machine (834 at
1024x576, `2026-09-24-netbook-3dmark99-completes-with-textures-missing.md`)
had game tests that largely did not render, so the scores do not compare.

## What the operator saw

At 1024x576: "massive improvement, first two tests work now and look a lot
better, still some missing things but it is greatly improved. Performance
is very slow though." The panel was not photographed.

## Scores

| | 1024x576 | 640x480 |
|---|---|---|
| 3DMarks | 246 | 717 |
| CPU 3DMarks | 14,904 | 14,572 |
| Rasterizer 3DRasterMarks | 95 | 347 |
| Game 1 Race | 2.8 FPS | 12.3 FPS |
| Game 2 First Person | 2.2 FPS | 5.1 FPS |
| Fill rate | 8.1 MTexels/s | 38.8 MTexels/s |
| Fill rate, multi-texturing | 8.1 MTexels/s | 39.1 MTexels/s |
| Texture rendering 2 / 4 / 8 / 16 / 32 MB | 18.3 / 15.4 / 10.7 / 7.0 / 4.2 FPS | 22.7 / 18.8 / 12.4 / 7.7 / 4.4 FPS |
| Point / bilinear / trilinear filtering speed | 99.7 / 100.0 / 99.8 % | 98.7 / 100.0 / 100.8 % |
| 6-pixel polygons, individual / strips | 407.9 / 371.7 K/s | 531.7 / 469.4 K/s |
| 1000-pixel polygons, individual / strips | 6.0 / 5.8 K/s | 7.6 / 7.4 K/s |
| Refresh rate reported | 25 Hz | 28 Hz |
| Bump mapping, anisotropic | Not Supported | Not Supported |

Screenshots: `...-1024-score.png`, `...-1024-details-{1,2,3}.png`,
`...-640-score.png`, `...-640-details-{1,2,3}.png`.

## What the driver counted

Deltas between V9XTRACE snapshots (`2026-09-25-netbook-mip-V9XTRACE-after-3dwb98.ini`
before, `...-1024-V9XTRACE.ini`, `...-640-V9XTRACE.ini`):

| | 1024x576 | 640x480 |
|---|---|---|
| Submissions | 207,883 | 273,165 |
| Triangles (indexed + single-primitive) | 9,151,682 | 10,539,374 |
| Flips handled | 1,795 | 3,275 |
| Textures created | 40,957 | 58,535 |
| Mip trees placed / declined (heap full) | 5,704 / 3,286 | 9,361 / 2,107 |
| Draws sampling levels | 27,796 | 63,697 |
| Texture binds refused on bounds | 4,706 | 16,770 |
| Draws with no texture handle | 5,247 | 18,069 |
| Batches refused (reason 6, vertex run) | 216 | 512 |
| Breadcrumb timeouts, drain stalls | 0, 0 | 0, 0 |

About 44 triangles per submission at 1024 and 39 at 640: 3DMark99 already
batches well, so the culling fragmentation seen in 3D WinBench (roughly
170 submissions a frame) is not what limits it.

## What this says, and what it does not

- **Memory pressure costs textures.** Every mip-tree decline is reason 4,
  the heap had no contiguous room, and the chains DirectDraw then placed
  itself land at addresses that are not page aligned
  (`D3dTextureRefusedVidMem=0xE6314E00`) and are refused at bind: those
  draws go untextured. A candidate for the missing objects, not shown to be
  them. DirectDraw's own placement gives plain textures a 4 KiB pitch here
  (`MipGapActual=0x1000` for a 256-texel texture needing 0x200), so each
  small texture holds many times its size.
- **The slowdown at 1024x576 is not proportional to pixels.** 1.9x the
  pixels cost 4.8x in fill rate and 3-4x in the game tests, and the
  per-triangle rates drop too. Not established why. Candidates, none
  measured: the synchronous wait per submission; the flip pacing (the
  active-video window was closed 100,769 times at 1024 and 196,484 at 640);
  texture uploads through an aperture that has no write-combining MTRR
  (Phase 1 record, 2026-09-12) under more memory pressure at the higher
  resolution.
- A fill rate of 39 MTexels/s at 640x480 is itself far below what this part
  should do; the pipeline, not the pixels, is the limit at both
  resolutions.

## Next

Measure before changing: time spent in stream build, decode, ring write,
the head wait and the flip wait, per boot, in counters. Then the changes
in order of expected reach: textures placed by the HAL at a tight pitch
(memory), asynchronous submission, merging runs across culled triangles
and across same-state draws, and texture-cache invalidation only after a
texture write.
