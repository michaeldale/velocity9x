# RIVA 128ZX: the Direct3D HAL's first light, and the bug that hid it

Date: 2026-11-03

Machine: M-7V636C (remote help), RIVA 128ZX 8 MiB, Windows 98 first
edition, boots 19-23, desktop 800x600x16 (5:6:5). Plan:
`docs/plans/nvidia-riva128-d3d.md`, step 5. Files:
`docs/probe/m7v636c-riva128-d3d-2026-11-03/hal` (V9XDDP reports and V9XTRACE
snapshots) and `state-run3-x1`, `state-run4-surfaces`, `state-run5-align`
(probe cases 15-27).

## What was built

`d3d_nv3.c` on the core's neutral draw, words from `nv3_3d.c`:
- **Textures.** A placed texture block holds the linear surface DirectDraw
  and the application see, plus a swizzled square copy that the CPU
  rebuilds before a draw when the texels may have changed. The core's new
  `surface_written` op, called at Unlock and as a Blt starts, marks the copy
  stale.
- **Untextured draws** sample an 8x8 white texture in RAMIN's unused bottom
  128 bytes.
- **Bring-up.** `eng_nv3.c`'s bring-up writes the D3D object, the
  whole-memory DMA object and the white texture, and binds subchannel 7. A
  D3D entry, `v9x_nv3_3d_begin`, brings the engine up at 16 bpp, where the
  2D side issues nothing.
- **Synchronous batches.** Each batch ends idle.
- **Caps.** `riva128_hw16.c` stamps CAP_D3D, and the manifest says
  `hardware-nv3`.

## Boots 19-23

| Boot | Build | Result |
|---|---|---|
| 19 | first HAL | `D3DHalFound=0`: the guest's SYSTEM.INI had `[Velocity9x] Direct3D=1` (user-disabled), and the manifest still said `not-advertised`. SYSTEM.INI backed up to `C:\V9XSYSIN.BAK` and set to 0 |
| 20 | manifest and device word fixed | HAL found, hardware device. Every pixel check read 0 |
| 21 | counters | 425 batches emitted, none stopped, engine idle after each; 174 refused (167 at the state build) |
| 22 | first batch recorded | Words as intended: surface 2 at the target, the clip, the state, the vertices. No PGRAPH interrupt, no trap |
| 23 | full-memory bounds | **Draws.** Every triangle, shape and edge check reads 7C00h, X1R5G5B5 red, with the outside pixel 0 |

The bug was in boot 20's HAL. `fb.vram_bytes` ends below RAMIN, because
`nvidia_hw16.c` keeps RAMIN off the heap. The white texture sits in RAMIN,
so the builder's bounds check refused every untextured draw. The DMA object
built from the same number did not reach RAMIN either. Both now use RAMIN's
start plus 32 KiB (`v9x_nv3_3d_vram_bytes`). Boot 22's recorded batch was a
later textured test, which is why its words looked right.

## Hypotheses the probe killed on the way (cases 15-27)

- **X1R5G5B5 reads alpha 1.** It does. With the source-alpha blend, an
  alpha-0 A1R5G5B5 texel read as X1 draws (X1AlphaZero). The opaque path
  stands. Vertex alpha 0 draws nothing (VertexAlphaZero).
- **SOURCE_COLOR 4 and 5** draw inverted colours where texel alpha is 0.
  **SOURCE_COLOR 8** draws only alpha-0 texels. None of these is needed.
- **The D3D object draws into surface 2**, not 0 or 1. Moving surface 2 by 8
  lines moved the drawing; moving 0 or 1 did not.
- **Zeta is not surface 1.** Moving surface 1 under the Z pair changed
  nothing, so by the object's options word zeta is surface 3. Moving
  surface 3 directly would leave the probe's 64 KiB window, so it was not
  tried.
- **Texture offsets need no 4 KiB alignment.** F100h, F800h and F010h all
  sample exactly.

## Boot 23, against V9XDDP

- **Coverage is right; the colour layout is not.** The target is 5:6:5 and
  the chip writes X1R5G5B5, so most of V9XDDP's colour comparisons fail.
  Step 3 (5:5:5 desktops) is what fixes them.
- **Textures sample through the swizzled copies.** Tex8 halves read 3E0h
  and 1Fh, green and blue in 5:5:5. Modulate reads 200h and 10h.
- **Passed:** D3DTriangleShapeOk, D3DDepthFogOk, D3DSlotPermutationsOk and
  D3DSlotPartialOk.
- **Z buffer creation fails** in V9XDDP's Z tests (`D3DZSurfaceHr=88760231h`),
  as it did on boot 19 with Direct3D off. Not investigated.
- **96 draws refused at the state build.** Opaque A4R4G4B4 is the expected
  part of that; the rest are not identified.
- No hang, no engine timeout, desktop intact through every run.

## Not done

- Step 3: 5:5:5 desktops, which needs the owner at the monitor.
- The Z buffer failure.
- The remaining state refusals.
- Games (step 6).
