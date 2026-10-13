# The Voodoo4 engine samples mip chains, and draws on Direct3D's pixel centres

Date: 2026-10-13. Machine: A8U4I5, boots 433-441. Card: Voodoo4 4500 PCI.
Michael ran Half-Life and watched the monitor. Evidence:
[a8u4i5-voodoo4-mip-2026-10-13](../probe/a8u4i5-voodoo4-mip-2026-10-13/).
Plan: [3dfx-vsa100-d3d.md](../plans/3dfx-vsa100-d3d.md). Follows
[2026-10-12-voodoo4-half-life-direct3d-on-velocity9x.md](2026-10-12-voodoo4-half-life-direct3d-on-velocity9x.md),
which found mipmapping missing.

## What was built

1. **Mip chains** (`fc841f0`).
   - The builder takes a level count and writes tLOD's maximum from it.
     The chip finds every level from one texBaseAddr, the levels packed
     after level 0 (Napalm section 20 p.228).
   - The map hands the chain to the sampler when the minification filter
     is a mip filter, and level 0 alone otherwise.
   - `create_surface` packs a chain with no alignment between levels; the
     engine walks the attached levels and counts one only where it starts
     where the last ended.
   - MIPNEAREST and MIPLINEAR are advertised. The two filters that blend
     levels are not built and take the nearest level.
2. **A +0.5 LOD bias** (`565be29`, narrowed in this commit). The chip
   truncates its LOD; Direct3D's nearest level is the rounded one. The bias
   applies only where the minification and magnification filters agree:
   the chip picks between them on the biased LOD (section 8.77 p.147).
3. **Rectangular textures to 8:1** (this commit). tLOD's aspect (22:21)
   and S-wider (20) fields. The single base is where the same aspect's
   256-long level 0 would start. S and T count texels of that level, so
   the short side's unit is 256 >> aspect. SQUAREONLY is no longer
   advertised; 3dfx's driver does not advertise it (0xCF, boot 430).
4. **Direct3D pixel centres** (this commit). Every vertex moves half a
   pixel down and right before it reaches the setup unit.

## The mip levels (V9XDDP)

- **Boot 433, chains, no bias:** every mip scene that failed before
  passes: chains A-E, level select, mip ladder, the degraded mip
  triangle. No pass turned to a fail.
- **Against 3dfx's driver (boot 430):** our level boundaries sat at 2, 4
  and 8 texels a pixel, 3dfx's near 1.4, 2.8 and 5.7.
- **Boot 434, +0.5 bias:** every affine sweep (ChainSweep, LodE,
  ChainRows, PerspWChain) reads the level 3dfx's driver reads, and
  ChainRows passes.
- **Still different:**
  - trilinear and the trilinear MipTri: not built;
  - the perspective rows PLodA/PLodB. Ours changes level along the row as
    1/W runs from 1 to 0.25; 3dfx's stays nearly flat and depends on the
    direction of u. Which is right is not established.

## Half-Life's console text, and three dead hypotheses

After mipmaps, Half-Life looked better but its console font lost strokes:
"SIEPRA", "Coudnt", "fite". Under 3dfx's driver (boot 436) the same
console is crisp. Michael recalled it fine before mipmaps; boot 438's
snapshot argues against that recollection (below).

- **Dead: the bias switching the font to the minification filter.** Boot
  435 applied no bias to mixed filters; the text did not change.
- **Dead: SQUAREONLY making Half-Life stretch the font.** Boot 437
  dropped the cap and drew rectangles natively; the text did not change.
- **Dead: mipmapping at all.** Boot 438's HAL snapshot after a console
  session counts 2 placed textures, no mip-chain checks, no alpha-tested
  draw sampling a chain (MipDraws 0), and minification filters NEAREST
  and LINEAR only. The text was broken with no mip code in play.

**What it was: the pixel centre.** V9XDDP's new TexPhase scene draws a
16x16 texture 1:1, point-sampled, as a quad at a whole-pixel offset and
half a pixel either way, and reads back which texel each pixel took:

| Offset | Ours, boot 438 | 3dfx's, boot 439 | Ours, boot 440 (-0.5) | Ours, boot 441 (+0.5) |
|---|---|---|---|---|
| 0 | `0123456789ABCDEF` | `0123456789ABCDEF` | `123456789ABCDEF.` | `0123456789ABCDEF` |
| -0.5 | `123456789ABCDEF.` | `0123456789ABCDEF` | `123456789ABCDEF.` | `0123456789ABCDEF` |
| +0.5 | `0123456789ABCDEF` | `.0123456789ABCDE` | `0123456789ABCDEF` | `.0123456789ABCDE` |

Rows read the same as columns throughout.

- The chip samples a pixel at +0.5; Direct3D on the whole coordinate.
- Text drawn at whole coordinates samples on texel edges, and one-pixel
  strokes fall to the neighbour.
- Boot 440 moved vertices up and left, the wrong sign, and doubled the
  error. Boot 441 moves them down and right and matches 3dfx's driver in
  every row.

Boot 441, Half-Life Direct3D 640x480x16, `timedemo mwd5`: the console is
crisp ("looks good now"), 20.118, 25.755 and 25.722 fps, as before.

## V9XDDP beside boot 437

No pass key changed. Raw values that moved with the half-pixel shift:

- **now 3dfx's:** PLodA_K, WHuge100;
- **mixed:** the Gouraud ramp moved by one colour step. Two of its
  samples now read 3dfx's value and three that did no longer do;
- **no longer 3dfx's:** PerspRow46.

## Not established

- Whether Michael's recollection that the text was fine before mipmaps
  holds. The pixel-centre error predates mipmaps, and boot 438 drew the
  broken text with no chain sampled.
- The S and T unit for a rectangle's short side is Glide's convention.
  Crisp text from the font is the only measurement.
- The perspective level rows (PLodA/PLodB).
- The diagnostics in `d3d_tdfx.c` (MipDraws and friends, alpha-tested
  chained draws) stay in; they cost nothing off that path.
