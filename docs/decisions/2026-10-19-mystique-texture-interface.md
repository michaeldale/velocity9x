# The Mystique's 3D interface is the 2164W's, with 16-bit Z, and format 4 is not 4444 here either

Date: 2026-10-19

Status: measured on A8U4I5 (Michael's Mystique, 051A revision 02), boot
507, 800x600x32, MGA2D build `d458b19a-dirty`. The measuring half of
Phase 3 of [matrox-mystique.md](../plans/matrox-mystique.md). Nothing in
the driver changed: D3D is still not advertised on `051A`.

## Why the 2164W's registers were tried

The MGA-1064SG specification cuts the texture registers, as the 2164W's
does. Its Table 3-3 (with the extraction's one-row shift undone; see the
2164W's extraction for the same fault) names 2C00h-2DFFh **DWGREG1,
"Second drawing registers"**. Table 3-4 part 7 decodes 2C00h-2C48h,
reserves 2C4Ch-2CFFh, and maps 2D00h-2DFFh onto 2C00h-2CFCh with go.
The 2164W's texture registers that `mga_3d.c` writes (TMR0-8 at
2C00h-2C20h, TEXORG to TEXTRANS at 2C24h-2C34h) all lie below 2C4Ch. The
2164W's 32-bit Z registers (2C50h on) do not, and the specification gives
the Mystique a 16-bit Z buffer only (1-4).

MGA2D's VxD now lets a Mystique write the second block only where the
low byte of the offset is at most 48h, in either half. Its EXE runs
`/tri`, `/depth` and `/tex` on 051A, and skips the Z32 cases as off the
2064W.

## Measured

Reports in
[`docs/probe/a8u4i5-mystique-3d-2026-10-19/`](../probe/a8u4i5-mystique-3d-2026-10-19/).

| Run | Cases | Result |
|---|---|---|
| `/tri` | 5 flat trapezoids, 3 Gouraud | all PASS; Gouraud matches `V9X_MGA3D_FOLD_EDGE`, as on the 2164W |
| `/depth` | 12 Z16 (write, all six compares, test-only, ramp, top, bottom, fraction) | all PASS, 0 colour and 0 Z mismatches; 4 Z32 SKIPPED-CHIP |
| `/tex` | 27: coordinates, magnify, wrap, clamp, negative, skew, perspective, colour key, decal with tamask/takey, 1555 decal and modulate, sizes, pitch, TEXORG alignment, perspective ramps | 26 PASS; TexPerspectiveSteep KNOWN-DIVERGENCE with 4 mismatches over 2 rows, the same 2 a row at exact texel boundaries as on the 2164W ([record](2026-10-10-mga2164w-textures.md), finding 5) |
| `/ri` | dither, no dither, flat and gradient clip, two plane masks | all PASS |

The written pixel carries FCOL's alpha byte (5Ah), as on the 2164W.

So the model and builder in `mga_3d.c`, which were fitted to the 2164W,
describe this chip as well, including the 1/8-texel perspective bias and
the 32-byte TEXORG alignment (TexOrg32 passes, and the 16-byte case is not
in the suite).

## ARGB4444: format 4 is not 4444 on this chip

Michael expected 4444 might work on the Mystique. The 1064SG specification
lists texture formats CLUT4, CLUT8, 3:3:2, 5:6:5 and 5:5:5 (1-4), and no
4444. A new MGA2D switch, `/texfmt`, tries formats the builder refuses on
the card alone: the texels are uploaded in that format, TEXCTL's
texformat is patched, and the pixels are reported raw.

- **Tex4444Identity** (white modulation, 16x4): a working 4444 would show
  red = s x 11h, green = t x 11h. The card drew mostly black with a
  four-pixel period: `41 00 00` in every fourth column, a few other
  values elsewhere. 63 of 64 pixels differ from the TW16 model, and the
  pattern is not a 4444 decode of these texels.
- **Decal4444Key1** (tamask and takey set; every texel's alpha nibble is
  F): the Gouraud colour (80FF40) shows in every fourth column, and the
  texel side reads near black.

That is the same kind of four-pixel-period garbage the 2164W drew with
format 4 ([record](2026-10-10-mga2164w-textures.md), finding 3). So
on this chip, too, 4444 textures stay off the engine; the 2164W's
565/1555-only describe applies unchanged. What format 4 is was not
pursued.

## What this does not establish

- Direct3D on the Mystique. The driver still does not stamp D3D for
  `051A`. That needs the HAL to select the MGA 3D engine on this chip with
  16-bit Z only (no 32-bit depth record), and a V9XDDP/D3D run.
- The palettised formats (TW8, TW4) and the LUT load; 3:3:2.
- Texturing with depth, and onto a 16 bpp target.
- The 1164SG.

Gates: `build-mga2d.ps1` builds; `run-checks.ps1` passes.
