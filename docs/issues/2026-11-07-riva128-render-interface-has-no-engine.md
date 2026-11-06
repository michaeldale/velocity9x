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
