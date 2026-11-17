# Oland: texture shapes the code places but no test has judged

Date: 2026-11-17

Machine: W9QI0DAD (Oland XT `1002:6610`), ati package as merged to main
(d3dd1b7d). Status: open. This is a coverage gap, not a seen defect.

The GCN code places and samples the shapes below, and host tests pin
their layout arithmetic. No check on the card has yet compared their
pixels with expected values:

- **Non-square DXT mip chains.** BC levels are rounded up to whole
  1D-thin tiles (8x8 blocks). T19's Dxt1 and Dxt3 rows and the BC
  probes use square textures. A chain such as 256x64 (3DMark's DXT3
  sets include one) changes tile rows per level, and that is untested.
  ([2026-11-17-oland-bc-1d-tiled.md](../decisions/2026-11-17-oland-bc-1d-tiled.md))
- **DXT cube maps.** The cube path places six array slices, level-major,
  and the BC path places tiles. Their combination is coded and never
  drawn in a judged test.
- **A cube's smaller mip levels.** T19 Cube reads all six faces at
  level 0 only
  ([2026-11-17-oland-cubes-signed-formats.md](../decisions/2026-11-17-oland-cubes-signed-formats.md)).
- **V8U8 (8_8 SNORM, X Y 0 1).** T19's Normal row covers Q8W8V8U8. No
  judged draw reads V8U8; the FfBem rows' bump map is not confirmed to
  be V8U8.

Nature and the full 3DMark 2001 SE runs draw with none of these refused
([2026-11-17-oland-nature.md](../decisions/2026-11-17-oland-nature.md)),
but frames judged by eye are not a pixel check.

## Next

Extend V9XD32 T19, or V9XSICP's BC probe, with:
- a 256x64 DXT1 chain sampled at each level;
- a DXT1 cube;
- a two-level cube sampled at level 1;
- a V8U8 bump draw.

Run each at both desktop depths, then on the GT218 and the HD 4600 for
comparison.
