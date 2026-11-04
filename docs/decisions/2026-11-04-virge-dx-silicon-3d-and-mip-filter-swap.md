# The ViRGE/DX's 3D on silicon: 86Box agrees, and two mip filter modes were swapped

Date: 2026-11-04
Machine: A8U4I5 (Pentium III, Windows 98), PCI S3 ViRGE/DX `5333:8A01`,
4 MiB, refitted for this; its September devnode (`DISPLAY\0004`) still bound
Velocity9x. Evidence:
[`../probe/virge-dx-a8u4i5-silicon-2026-11-04/`](../probe/virge-dx-a8u4i5-silicon-2026-11-04/).

## Why

STATUS listed the ViRGE/DX's Direct3D and OpenGL as "measured on the
emulator, not on silicon": every 3D result for this card came from 86Box.
This is the first silicon run, on the released 0.35.0 `s3` package (boot 661,
every installed file matching the release zip; `Stage=enable-ok`,
`Direct3DMode=hardware`, a 5:5:5 desktop at 800x600).

## The probes on 0.35.0

| Probe | Result |
|---|---|
| V9XD32 `/bpp16` | 10 failures (below) |
| V9XDDP | 197 of 227 `Ok` checks (final values; the file repeats keys) |
| V9XGLP | PASS |
| V9XR3DP | COMPLETE |
| V9XGLIDP | 6 failures (below) |

No fault, no engine idle timeout, no draw into the presented page.

- **V9XDDP against 86Box** (the 0.18.5 run in
  [`../probe/86box-virge-0.18.5-regression-2026-10-16/`](../probe/86box-virge-0.18.5-regression-2026-10-16/)):
  of 113 checks both runs have, 85 pass on both and 26 fail on both. None
  passes on 86Box and fails on silicon. Two pass on silicon and fail on
  86Box: `D3DZCompareOk` and `D3DZWriteMaskOk`, which is
  [`2026-09-03-86box-virge-ignores-depth-write-disable.md`](../issues/2026-09-03-86box-virge-ignores-depth-write-disable.md)
  from the other side: the chip honours the depth-write disable. The 30
  silicon failures are the ViRGE's known gaps (fog, specular, alpha curves,
  mixed-engine ordering, some texture formats and blends).
- **V9XD32:** 7 failures are T9 and T11's culling cases. The engine
  advertises only `D3DPMISCCAPS_CULLNONE`, so the core does not cull for it;
  the RS690's software engine fails the same 7 by design. One is T14, the
  next program after one dies holding surfaces, the known issue in every
  release's notes. Two are not attributed. (Corrected below, "Later the
  same day": T14 is not that issue; T1, T4 and T14 are the chip's dither.)
- **V9XGLIDP:** the first Glide run on a ViRGE. Five texture checks read
  black (`read=0000`) where a texel colour was expected, and the table-fog
  check reads `FFFF`, the open fog issue
  ([issue](../issues/2026-10-15-virge-claims-vertex-fog-it-does-not-draw.md)).
  The texture failures are not looked into.

## 3DMark 2000

The default benchmark refuses to start ("The default settings require 16MB
of video memory"): 1024x768x32 does not fit 4 MiB, and the ViRGE draws 3D at
16 bpp only. A custom benchmark at 800x600x16, 16-bit Z, triple buffered,
completes: **134** on 0.35.0.

Michael, watching, saw Game 1's sky drawn in blocks. The grab confirms it
(`3dmark-before-fix/grab0-sky.png`): magnified sky in flat squares, while
the mipmapped terrain looks right. The snapshot shows 3DMark asking for
minifying filter 4, `MIPLINEAR`.

## The cause: two filter constants on each other's values

`r3d.h` numbers filters as Direct3D 5 does: 4 is `MIPLINEAR`, bilinear
within one mip level; 5 is `LINEARMIPNEAREST`, the nearest texel of two
levels blended. The ViRGE data book (DB019-B, 15.4.8.1 "Texture Filtering",
p. 15-27) gives Command Set bits 14-12 as 001 = M2TPP (LINEAR_MIP_NEAREST),
"interpolated between the nearest texels from 2 mipmap levels", and 010 =
M4TPP (MIP_LINEAR), four texels of one level. S3's names match Direct3D's.
`ddhal_internal.h` had `V9X_VIRGE_3D_CMD_MIP_LINEAR` at `0x1000` (001) and
`LINEAR_MIP_NEAREST` at `0x2000` (010), so every `MIPLINEAR` draw went to the
chip as M2TPP: point-sampled within its level, blocky when magnified.

The fix swaps the two values to the data book's. The trilinear code in
`d3d_virge.c`, which wants "bilinear on one level" and used the name whose
wrong value happened to be 010, now uses `MIP_LINEAR`, so it sends the same
010 as before. 86Box's ViRGE model drew the swapped modes without anyone
seeing it.

Measured: the fixed build (`69bb160c-dirty`, the s3 set by WININIT rename,
boot 662), the same custom 3DMark run: the sky is filtered
(`3dmark-after-fix/grab0-sky.png`; an OBS recording of Game 1 agrees), the
fill-rate tunnel textures smooth, **150**, no fault, no timeout. Scores are
recorded, not compared.

## Smaller things seen

- The CPU Speed test draws a 640x480 view inside the 800x600 frame, and its
  margins carry horizontal streaks of stale video memory
  (`3dmark-before-fix/obs-cpu-test-margins.jpg`, still there after the fix).
  Michael: other cards show black there. Minor, not looked into.
- White shapes over the helicopter in some grabs are missile smoke, present
  in both runs; not a texture fault.

## Not done

Trio 3D and Trio3D/2X share this path and the constants. Their data books
are not at hand; the fix applies to them on the same-design rule and has not
run on either. V9XD32's two unattributed failures, the Glide texture
failures and the CPU-test margins are open.

## Later the same day: Glide textures, V9XD32's flat triangles

### Glide: two refusals, one behind the other

The five black texture checks were all textures the engine never drew. The
Glide log (`glide-repack-only/V9XGLIDE-probe-run.LOG`) shows why, in two
layers:

1. **Format.** Glide's RGB_565 and palettised textures convert to RGB565,
   which the ViRGE's describe does not list (ARGB1555 and ARGB4444 only), so
   no surface was made and the draw went with CPU texels, which this engine
   has no fallback for. Fix: `v9x_glide_texture_convert` repacks RGB565 to
   ARGB1555 (alpha one, green's low bit dropped) when the device lists 1555
   and not 565; `v9x_glide_texels_565_to_argb1555`, host-tested. Run alone
   (boot 663): surfaces made and uploaded (`uploads=5`), draws still refused,
   now as `storage/format=0103`. Six failures as before.
2. **Texture op.** The checks' combine (texture times iterated colour, both
   colour and alpha) reaches the core as `MODULATEALPHA`, which
   `v9x_d3d_virge_accepts` refused: the S3D unit has DECAL, MODULATE and
   COPY. With every vertex alpha 255 the two ops give the same colour and
   alpha, so the ViRGE engine now takes `MODULATEALPHA` as `MODULATE` when
   the render interface's `vertex_alpha_opaque` is set. Direct3D's own
   paths leave that flag clear and are unchanged.

Both together (boot 664, `glide-repack-and-modulatealpha/`): V9XGLIDP **1
failure**, `FogTable` (the open fog issue). `Texture565`, `TexturePalette`,
`TexturePaletteChange`, `ChromaKeyed` and `ChromaOff` pass; one draw refused
in the whole run, the fogged one. No fault, no engine timeout.

### V9XD32: T1, T4 and T14 are the chip's ordered dither

V9XD32 now records the first wrong pixel inside and outside each flat
triangle. Boot 664: T1 374 wrong inside, 0 outside; first at local (2,1),
`0D74` where `0953` was expected. T4 506 inside, first at (2,1) `15B6`
for `11B5`. Each wrong pixel is one 5-bit step high in some channels; the
centre pixel, judged separately, is exact.

86Box's ViRGE model (`build\reference-vid_s3_virge.c`, `RGB15`) dithers
every 16-bit write with a 4x4 table, on by default, under no command bit.
Its table predicts both first misses exactly: T1's (2,1) is screen (10,9),
table value 7; 0x13+7, 0x57+7 and 0x9B+7 truncate to 3, 0B and 14, which is
`0D74`; T4's (2,1) at the surface origin, also 7, gives `15B6`. The data
book's 3D Command Set (DB019-B, chapter 19, MMB500) has no dither bit, and
the probe already sets `D3DRENDERSTATE_DITHERENABLE` off. So the ViRGE
dithers 16-bit 3D output whatever the driver asks, and the probe's exact
flat-colour check cannot pass on it. Not a drawing fault; no driver change.

T14 failed with T1's exact count (374) in its own process, so it is this
dither, not the killed-program issue claimed above.

T4 also has 8 pixels outside the triangle, the first `7C00` (pure red) at
local (52,0) of its 64x64 offscreen target, cleared by a colour fill just
before. One run; not looked into.

V9XD32's ten failures on this card are therefore: 7 culling (no cull on
this engine, by design), 3 dither (T1, T4, T14).

### V9XD32 now judges both (2026-11-05)

The probe was changed so these stop reading as failures, without
loosening it for chips that pass exactly:

- **Dither.** T1, T4 and T14 report `<T>Flat`: `EXACT`; `DITHERED` when
  nothing outside the triangle was written, some inside pixels are exact
  and every inside miss is the colour truncated to its channel width or
  one step above it (16 bpp only); else `FAIL`. Dithered passes. A
  colour the engine rounds wrong misses at every pixel, so it has no exact
  pixel and still fails. A blanket tolerance would have hidden that.
- **Culling.** The probe records the HAL's triangle `dwMiscCaps`
  (`HalTriMiscCaps`). A direction the device does not advertise is
  expected drawn in T9 and T11, since Direct3D leaves culling to the
  driver. A device advertising CW and CCW is judged as before.

Measured with the changed probe (`64dc2e2f-dirty`;
[`../probe/virge-dx-a8u4i5-silicon-2026-11-04/d32-dither-and-cull-classified/`](../probe/virge-dx-a8u4i5-silicon-2026-11-04/d32-dither-and-cull-classified/)):

- **ViRGE/DX, A8U4I5 `/bpp16`** (driver unchanged since boot 664):
  `HalTriMiscCaps=0x10` (CULLNONE only). T9 and all six T11 forms pass.
  T1 and T14 `DITHERED` (374 misses, all one step). T4 `FAIL`: its 506
  inside misses are all one step, but the 8 pixels outside (first `7C00`
  at local (52,0)) remain. **1 failure**, down from 10.
- **Control, Haswell 4570** (boot 173 build): `HalTriMiscCaps=0x70`, so
  culling is judged strictly. 32 bpp and `/bpp16` both PASS, every flat
  triangle `EXACT`.

T4's stray pixels are now the ViRGE's only V9XD32 failure, and still not
explained.
