# Haswell, 3DMark 2000 Game 1: flicker from unfinished frames, and a blocky sky

Date: 2026-11-01. Seen by Michael on the i5-4570 (W9QI5A83) during the
0.34.0 release checks, on screen through OBS: "lots of flicker" and
"sky looks pixelate". The GMA 950 netbook, same benchmark, "looks good".
Status: fixed. Both causes found and fixed in source, measured on the
4570 (boots 169-171, below), and seen on the monitor at boot 171 by
Michael: "looks good". Not a
0.34.0 regression (0.33.0 shows both), but a release blocker at
Michael's word. Evidence:
[`../probe/0.34.0-release-checks-2026-11-01/hd4600-3dmark2000-visual/`](../probe/0.34.0-release-checks-2026-11-01/hd4600-3dmark2000-visual/),
[`../probe/hsw-3dmark2000-flicker-2026-11-01/`](../probe/hsw-3dmark2000-flicker-2026-11-01/).

## What the recordings show

`Y:\MWD\videos\obs\2026-11-01 10-18-41.mkv`, build `522c79d3` (0.34.0
candidate), boot 167. Frames taken three a second (`frames-0.34.0.jpg`)
include frames with the helicopter missing and the HUD panels half drawn
beside complete ones. A frame the engine had not finished is being put on
screen, or a stale one is.

The sky (`sky-crop-0.34.0.png`, top of a full-resolution frame) is hard
edged blocks about 30 capture pixels across with no blend between them:
a small image, or a small mip level, magnified with point sampling.

## A/B against 0.33.0

The released 0.33.0 intel-gma package's eight files installed by WININIT
rename, boot 168, every hash matching the package. Same run,
`Y:\MWD\videos\obs\2026-11-01 10-26-11.mkv` (about 5 s of Game 1).
Michael: "same issue in 0.33". `sky-crop-0.33.0.png` shows the same
blocks. Neither is new in 0.34.0, so the HD 4600 commits since 0.33.0
(DDI 8, pixel shaders, cube and DXT textures) are not the cause.

## What was ruled out by reading, not measured

- The DXT lock hook (`v9x_d3d_hsw_lock_surface`) acts only on DXT texture
  levels; it cannot touch a render target.
- `v9x_d3d_hsw_draw_shader` serves DX8 pixel shaders; 3DMark 2000 is DX7.
- The filter mapping (`v9x_d3d_hsw_filter`, `v9x_d3d_hsw_mip_filter`,
  `v9x_d3d_min_filter_fold`) reads correctly for the DX5 and DX7 names.

## 2026-11-01, boot 169: what the driver counted

0.34.0 candidate (`522c79d3`) back on the 4570. V9XTRACE before Game 1,
then `-grab:40x10`, then after (`grab-b169/`, V9XSNA7.INI is the after):

| | |
|---|---|
| `FlipHandled` / `FlipDeclined` | 444 / 0 |
| `DrawsIntoPresented` | **21,358** |
| `FlipStillDrawing` | 13,437 |
| `R3dListSinkBatches` | 9,679 |
| `Dp2FlushesExternal` | 0 |
| `FilterMagSeen` / `FilterMinSeen` | **0x2 / 0x2** |

The ten grabbed frames are complete: the grab drains the engine after the
flip, so it shows the page once drawn, not what the scanout read while it
was being drawn.

**Flicker: draws into the page on screen.** The present trace (flip
accepted, flip done, first draw after it) for a triple-buffered chain at
0, 18C000h and 318000h:

| flip | to | first draw after it |
|---|---|---|
| 434 | 318000 | 318000 |
| 435 | 000000 | 18C000 |
| 436 | 18C000 | 000000 |
| 437 | 318000 | 318000 |

The draws rotate round the chain the other way from the flips. At DDI 7
the runtime names the target by CreateSurfaceEx handle in a
SETRENDERTARGET every frame, and the handle table holds the local object
that had the handle at CreateSurfaceEx. A flip rotates the chain's memory
among the local objects and moves the handles with it, so one handle
keeps naming one page while the recorded object points at another; read
through it, the two rotations compose. DDI 6 names the target by the
application's surface, which is why the GMA 950 netbook (DDI 6 for every
program) does not flicker, and why 3DMark 99's flips (Phase F3, DDI 6)
were fixed for good. `Dp2FlushesExternal` 0 kills the other candidate, a
batch held across the Flip.

**Sky: every texture point-sampled.** `FilterMagSeen`/`FilterMinSeen`
set one bit per value asked for; 0x2 is `D3DFILTER_NEAREST` and nothing
else, the whole boot. At DDI 6 and 7 every engine publishes only the DX5
filter words in `dwTextureFilterCaps`; a DX6 or DX7 program reads the
`MINF`/`MAGF`/`MIPF` bits, found none, and chose point. Only DDI 8
derived them (`v9x_d3d_caps8_filter`). The sky is a small texture
magnified about 16x, so it is where point sampling shows; the grabs show
the same blocks (`grab-b169/V9XGRAB4.BMP`).

## Fixes

- `v9x_d3d_ddi7_chain_holder` (`d3d_core.c`): SETRENDERTARGET's handle
  resolves to the flip-chain surface that holds it now, walking the
  chain's attachments from the recorded one; logged as `setrt-moved` in
  V9XD7.LOG the first eight times.
- `v9x_d3d_apply_ddi_level`: at DDI 6 and 7 the triangle (and so line)
  filter caps carry the DX6 bits `v9x_d3d_caps8_filter` derives from the
  engine's DX5 words, so no engine claims a filter it lacks. Every engine
  takes it (same design, same fix).

| Game 1, same run | boot 169 (none) | boot 170 (target) | boot 171 (both) |
|---|---|---|---|
| `DrawsIntoPresented` | 21,358 | 0 | 0 |
| `FlipStillDrawing` | 13,437 | 664 | 493 |
| `FlipHandled` | 444 | 447 | 442 |
| `FilterMagSeen` / `FilterMinSeen` | 0x2 / 0x2 | - | 0x6 / 0x16 |

Boot 170's V9XD7.LOG: `setrt-moved` for handles 1 and 3 from the first
frames, so the handles do move on a flip. Boot 171's grabs show the sky
filtered (`grab-fix2/`); 3DMark now asks for LINEAR and MIPLINEAR.
Neither fix is host-testable: both read DirectDraw's live objects.
