# MGA-2164W: nodither, the clip window and PLNWT on trapezoids, and modulate against the CPU

2026-10-18, A8U4I5 (Matrox Millennium II, MGA-2164W, 8 MiB, 10.0.1.172),
boot 493. Step 1 of
[`../plans/matrox-mga2164w-render-interface.md`](../plans/matrox-mga2164w-render-interface.md):
the engine functions an explicit render-interface draw needs, measured
before any product code uses them. Evidence:
[`../probe/a8u4i5-mga2164w-render-interface-2026-10-18/`](../probe/a8u4i5-mga2164w-render-interface-2026-10-18/)
(`MGA2D-ri-*`).

## Instrument

`MGA2D.EXE /ri /vram:8` (`tools/diag/mga2d_win32.c`):
- Each case draws one Gouraud trapezoid, 56 x 12 pixels, built by
  `mga_3d.c`, with one thing changed around it.
- The result is read back and compared with the engine model at 32 bpp,
  truncated to 565 at 16 bpp.
- Pixels outside a clip or plane mask must keep the prepared pattern.
- The setup is written again after each case, so the clip and mask are
  open for the next.

The register meanings come from the 2064W table in
[`../specifications/mga2064w-2d-engine.md`](../specifications/mga2064w-2d-engine.md):
- MACCESS nodither <30> (4-64);
- CXBNDRY inclusive (4-28);
- YTOP and YBOT as linear `line * PITCH + YDSTORG`, inclusive (4-83,
  4-79);
- PLNWT (4-69; 2164W 3-75).

The question was whether the 2164W's 3D trapezoids obey them.

Two probe fixes on the way:
- MGA2D found no device: `CR_NO_SUCH_DEVNODE`. Its devnode search tried
  only the first instance under each `VEN_102B&DEV_051B` key, and after
  the card swaps the present card is not that one. It now tries every
  instance, as its comment already said. The card is
  `...REV_00\BUS_00&DEV_08&FUNC_00`.
- The first 16 bpp run drew the clip and mask cases dithered. Dithering
  moved even the exact levels: F8 red drawn as F0 (`F3E7` for `FBE7`).
  That failed all four cases (174, 113, 168 and 588 mismatching pixels)
  for a reason that has nothing to do with the clip or the mask. Those
  cases now draw with nodither, as explicit draws would.

## Measured

| Case | 16 bpp (800x600x16) | 32 bpp (800x600x32) |
|---|---|---|
| RiDither: as Direct3D draws it | 437 of 672 pixels differ from truncation (first `11F7` for `1218`) | 0, PASS |
| RiNoDither: MACCESS bit 30 | 0, PASS | 0, PASS |
| RiClipFlat: columns 12-40, rows 3-8 | 0, PASS | 0, PASS |
| RiClipGradient: the same, shaded | 0, PASS | 0, PASS |
| RiMaskGreen: PLNWT `07E007E0` / `0000FF00` | 0, PASS | 0, PASS |
| RiMaskRedBlue: PLNWT `F81FF81F` / `00FF00FF` | 0, PASS | 0, PASS |

The clip covered exactly columns 12 to 40 and rows 3 to 8, 29 columns by
6 rows (`RiClipFlatRow*`). The desktop was put back to 800x600x8
afterwards.

## Modulate, on the host

The textures record measured the 2164W's TEXCTL tmodulate as
`texel * colour >> 8` per channel. The software engine's MODULATE is
`(texel * colour + 127) / 255` (`d3d_raster.c`). Over all 65,536 pairs of
8-bit inputs:

| | equal | MGA 1 lower | MGA 2 lower | MGA higher |
|---|---|---|---|---|
| 8-bit result | 18,480 | 45,664 | 1,392 | 0 |
| after 565, 5-bit channel | 59,758 | 5,778 | - | 0 |
| after 565, 6-bit channel | 53,724 | 11,812 | - | 0 |

- The MGA is never lighter and at most two levels darker.
- A white texel on white Gouraud is 254, not 255. At 8888 that shows; at
  565 it truncates to the same 31 and 63.

## Findings

- **MACCESS nodither turns 16 bpp shading into plain truncation** on the
  2164W's trapezoids, pixel for pixel. Explicit draws can be undithered,
  which removes the one 16 bpp difference V9XD32 saw (`1AB3` against
  `12B3`).
- **The clip window bounds 3D trapezoids** as the 2064W spec says for 2D:
  all four bounds inclusive, YTOP and YBOT linear, at 16 and 32 bpp. An
  explicit draw's half-open scissor maps to it by subtracting one from the
  right and bottom.
- **PLNWT masks 3D colour writes** per bit at both depths, replicated per
  16-bit pixel at 16 bpp. Partial RGB write masks can be served.
- **Modulate is not exact against the software engine.** It differs by
  one 565 step on 9 % (red, blue) and 18 % (green) of input pairs, always
  darker. Whether that fits V9XGLP's and V9XGLIDP's tolerances is decided
  by running them in step 5, not here. Until then the plan's table keeps
  MODULATE as "measure".

## Not measured

- A clip with 32-pixel-unaligned YTOP or YBOT values. The probe's rows
  start on 1024-pixel rows, so every value was a multiple of 32, which the
  2064W spec requires.
- PLNWT with Z writes (the 2164W spec says it is not used for Z cycles).
- Nodither with a 1555 target (dit555).
