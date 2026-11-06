# RIVA 128ZX: the render interface has no engine, so Glide and 3DCIF draw nothing

Date: 2026-11-07. Machine: M-7V636C, RIVA 128ZX (12D2:0018), nvidia
package. Found while explaining
`2026-11-06-riva128-glide-draws-nothing-on-0.37.0-candidate.md`.

## What is seen

On 0.37.0 (`55a661a2`, boot 40), V9XR3DP's describe returns
`DescribeEngine=0` and renderer `Velocity9x`. Both of its draws return 4
(UNSUPPORTED). Velocity9x's GLIDE2X.DLL logs `engine=0` and refuses all
29 of V9XGLIDP's draws, untextured and textured alike
(`docs/probe/m7v636c-riva128-glide-2026-11-07/V9XGLIDE.LOG`). Clears and
flips pass. The development build before 0.37.0 reads the same in
V9XR3DP.

## Why, from the source

- `v9x_r3d_describe_body` (`src/display32/d3d/d3d_core.c`) names no engine
  for `v9x_d3d_engine_nv3` and sets no hardware texture shape, so clients
  keep textures on the CPU.
- `v9x_r3d_fallback` gives the NV3 no software engine, so a draw the NV3
  map does not accept is refused, not drawn on the CPU.

Which of the NV3 map's refusals the untextured draws hit is not known.
The Direct3D HAL draws the same kinds of triangle (V9XDDP 216/227,
Half-Life, Thief, UT), so the likely difference is the target or the
state the render interface hands it, not the chip.

## Next

The RS690 went through the same steps in 0.38.0 (`a3440f06`, `99c78398`,
`docs/decisions/2026-11-06-rs690-opengl-draws-nothing.md`):

1. Name the engine in describe. Find which NV3 map refusal the
   render interface's untextured draw hits, and fix that.
2. Offer video-memory textures through the NV3's create-surface path.
3. Judge it by V9XR3DP and V9XGLIDP on M-7V636C. The owner's 3dfx
   GLIDE2X.DLL is no longer in `C:\WINDOWS\SYSTEM`, so no Voodoo 1 Glide
   can confuse the reading.

Not measured: anything above. This record is from the logs and the
source.

## 2026-11-07: render-interface draws on the NV3 engine (boot 42)

The refusal was the NV3 map's first test. `v9x_d3d_nv3_map` refused
every draw with `explicit_state` set, and the render interface sets it on
every draw. The HAL change, built on `7805bd1d` with the tree dirty:

- The map accepts an explicit draw that changes nothing a Direct3D draw
  would: a scissor covering the whole target, all three channels written,
  and a surface texture or none. A draw with CPU texture levels is
  refused. Without that refusal, the map would read such a draw as
  untextured and draw it white. These are the ViRGE's terms. Nothing
  here programs a scissor or a channel mask on the NV3, and none has been
  measured.
- describe names the engine: `V9X_R3D_ABI_ENGINE_NV3` (15), "Velocity9x
  RIVA 128". It also offers video-memory textures, powers of two from 1 to
  256, as `v9x_d3d_nv3_create_surface` already places them for Direct3D.

Install: only V9XHAL.DLL changed. It went in by a WININIT rename and a
`reset` (boot 41 to 42), and the installed SHA-256 matches the build
(`A3F056FE...`). The Glide, OpenGL and 3DCIF DLLs are 0.37.0's. A first
attempt at boot 41 changed nothing: `C:\V9XSTAGE` did not exist, the
upload failed, and WININIT skipped a rename whose source was missing. The
old HAL stayed in place.

Results, evidence in
`docs/probe/m7v636c-riva128-glide-2026-11-07/boot42-engine/`, compared
with 0.37.0's `riva128` folder:

| Probe | 0.37.0, boot 40 | This HAL, boot 42 |
|---|---|---|
| V9XGLIDP | FAIL, 14 | FAIL, 1 (BlendHalf) |
| V9XR3DP | engine 0, DrawOk 0 | engine 15, DrawOk 1 |
| V9XGLP | PASS, ten checks reading 0 | PASS, those ten pass |
| V9XD32 `/bpp16` | FAIL, T21 | FAIL, T21 (unchanged) |

- **Glide.** All 29 draws are drawn and none refused; there were 5
  texture uploads. The line, depth, clip, snap, texture, palette,
  chroma-key and fog checks pass. The one failure is BlendHalf, which
  reads `7BCF` where `8410`+-1 is expected. That is half-white over black
  on the 5:5:5 desktop: 15 in each 5-bit channel where the probe wants 16,
  and green two steps low once widened to 565. The blend truncates where
  the probe expects rounding. This is the same kind of miss as the Rage
  IIC's (`2026-10-14-rage-iic-blend-one-step-low-at-555.md`). Not chased.
- **OpenGL.** It now draws on the engine, renderer "Velocity9x RIVA 128".
  OffsetNone/Nearer/Farther, FogLinear/Exp/ExpAtEye, RectColor3us,
  Vertex2sColor4b and ReplaceRgbBlend/FogBlend read 0 on the ICD's CPU
  path in 0.37.0. They now read 1. LifetimeFailures is 0.
- **Still refused, as the change intends.** V9XR3DP's
  DrawTexturedScissor and DrawMasked (result 4) are refused: a partial
  scissor and a partial write mask, which this map does not take. An
  OpenGL program that sets a smaller glScissor or a colour mask will still
  have those draws dropped on this chip. Glide's ClipInside passes:
  V9XGLIDP's clip window at that check covers the target, or Glide clips
  it itself (not checked which).
- Direct3D: V9XD32 reads as it did, so the map change did not move
  Direct3D's draws.

Not run: any game through OpenGL or Glide, 3DCIF, 32 bpp (the NV3 map
draws only 16-bit targets), the screen by eye.

Status: drawn. Open for the scissor and channel mask.
