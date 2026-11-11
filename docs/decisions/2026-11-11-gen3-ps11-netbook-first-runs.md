# Gen3 pixel shaders on the GMA 950: T19 passes, 3DMark 2001 SE stops in Advanced Pixel Shader

Date: 2026-11-11. Machine: the netbook (W9QI2BE2, GMA 950, 945GSE, 7.7 MB
of stolen memory), boots 302-305. Step 5f of
[`../plans/gen3-ps11-pixel-shaders.md`](../plans/gen3-ps11-pixel-shaders.md).
Each build deployed by WININIT rename (DRV, VXD, HAL, ICD), hashes
matching.

## Results

| Boot | Build | What it showed |
|---|---|---|
| 302 | db3d7573 (5e) | V9XD32 T19 `SKIP-NO-PS`: the HAL device reported `PixelShaderVersion` FFFF0000 |
| 303 | a0c9c3c1 (Gen3 offered DDI 8) | T19 runs; all eight programs fail at (8, 8), each drawing 37 pixels outside its triangle |
| 304 | a0c9cntr (= 7999cfac: the half-pixel move) | T19 **PASS** at R5G6B5 and X8R8G8B8. 3DMark 2001 SE runs Nature and the shader tests, then stops in Advanced Pixel Shader |
| 305 | 7999oom1 (signed textures and cubes refused as out of memory) | No draws refused. 3DMark still stops in Advanced Pixel Shader, same error. T19 PASS |

## 1. DDI 8 never reached Gen3 (boot 302)

The plan listed it as unknown. Only the Haswell 16-bit open stamped
`V9X_DD_ENGINE_CAP_D3D_DDI8`. The core also gave DDI 8 only on top of
DDI 7, which needs an engine with T&L, so DirectX 8 programs on Gen3 had
DDI 6. The runtime built D3DCAPS8 itself, with no pixel shaders.

DDI 8's vertex processing is the runtime's (ddi8-on-win9x.md: no
HWTRANSFORMANDLIGHT), as it already is on Haswell. So a0c9c3c1 gives DDI
8 over DDI 6 too, to an engine with `draw_shader`, and Gen3 stamps the
bit. Its GM965/G4x and Ironlake wrappers take it off. Every DirectX 8
title on Gen3 now gets DDI 8 instead of DDI 6.

## 2. Half a pixel (boot 303)

Gen3 samples at x + 0.5 (`pixel_centre_half`). The core moves a
fixed-function draw's vertices for it, but hands `draw_shader` wide
vertices unmoved: `centre_offset` is zero because `centre_in_engine` is
clear. Every program drew 37 pixels outside its triangle. 7999cfac moves
them in `draw_shader`, before the on-target test and the clip.

## 3. T19 (boots 304, 305)

| Desktop | Programs | Worst channel error | Tolerance |
|---|---|---|---|
| R5G6B5 | Push, FourTex, Kill, Bem, PushFog, KillFog, Cube, Normal: all pass | 9 | 9 |
| X8R8G8B8 | the same, all pass | 1 | 2 |

Dxt1 and Dxt3 skipped: Gen3 publishes no DXT. This settles three of the
plan's open points on this part:
- **Cube sampling** works with the direction unscaled (Cube).
- **Q8W8V8U8** is sampled signed, in the byte order its name gives
  (Normal).
- **TEXKILL** kills, under both readings of the w question (Kill,
  KillFog).

V8U8 is not in T19. 3DMark's Environment Bump Mapping uses it and ran,
but nothing judged its pixels.

## 4. 3DMark 2001 SE (boots 304, 305)

640x480, 16-bit colour, 32-bit textures, all 17 tests, D3D software T&L.

| Test | Boot 304 | Boot 305 |
|---|---|---|
| Game 1-3 | 5.7/2.1, 7.1/2.5, 12.6/6.5 fps | 5.7/2.1, 7.0/2.4, 12.5/6.4 fps |
| Game 4 - Nature | 2.9 fps | 2.8 fps |
| Environment Bump Mapping | 19.7 fps | 19.5 fps |
| Vertex Shader | 4.6 fps | 4.6 fps |
| Pixel Shader | 10.7 fps | 10.8 fps |
| Advanced Pixel Shader | stopped: "Could not create texture - D3DERR_INVALIDCALL" | the same |
| Point Sprites, 3DMark score | not reached | not reached |

Before 5e (boot 301, a decision of the same day) the same settings
completed with 489: Nature and the shader tests were skipped as
unsupported. **So with draw_shader, 3DMark 2001 SE no longer completes
on the GMA 950.**

### Video memory (boot 304)

The 7.7 MB ran out. 29,187 mip chains were declined for want of room
(reason 4). For an RGB format that is harmless: DirectDraw places it
itself. The signed formats come as a FourCC with no bit count, so
DirectDraw cannot size them, and the surfaces it made failed the
footprint check at bind. That refused 7,605 shader draws
(`D3dTextureRefusedBounds`). From boot 305 a signed texture, or a cube
in any format, that the heap cannot hold is answered
`DDERR_OUTOFVIDEOMEMORY`, as the RIVA 128's placement already does. The
runtime then evicts and retries. V9XPS.LOG shows three 256-pixel cubes
refused so, each followed by a retry that was placed. Refused draws:
0.

### What stops Advanced Pixel Shader: not found

The failing texture never reached CreateSurface. In V9XPS.LOG's last
entries every creation was placed:
- a 512x512 V8U8 map (BUMPDUDV);
- a 512x512 Q8W8V8U8 map of three levels;
- the 256 cube.

Then the test's own program (ps.1.0, `texm3x3vspec`) and its draws. So
the Direct3D 8 runtime refused the call itself, which points at
something the caps or the format list do not allow: a format, a usage
or a size.

Not checked:
- **DXT.** Gen3 publishes none; the HD 4600 did when APS completed there.
- **Render targets.** Is a render-target texture allowed in the format
  the test asks for? The test renders to a texture (Haswell's step 6
  record).
- **Volume textures and texture size.** Neither is claimed on Gen3.

## Evidence

V9XD32 reports, V9XTRACE snapshots, 3DMark's error logs and V9XPS.LOG
are in session 7b86a0ab's scratchpad (`nb30*-*`), not in the repo.
