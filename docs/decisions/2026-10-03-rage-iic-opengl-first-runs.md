# Rage IIC OpenGL: first runs of V9XGL.DLL on the trapezoid engine

Date: 2026-10-03. Machine: A8U4I5 (ATI Rage IIC AGP, `1002:4757` rev 7A,
4 MiB, Win98SE), 640x480 at 16 bpp, 565 desktop. Boots 145-148.
Evidence: `docs/probe/a8u4i5-rage-iic-registers-2026-10-02/phase6-*`.

## What was done

`V9XGL.DLL` already routes draws through the HAL's render interface
(`V9xRenderInterface`, `d3d_core.c`). For the Rage IIC the interface
now describes the engine (`V9X_R3D_ABI_ENGINE_RAGE2`, name "Velocity9x
Rage IIC", the texture sizes from the chip's limits, power-of-two only,
a target format only on a 565 desktop), and the draw goes to the same
`v9x_d3d_rage2` ops Direct3D uses. No GL-specific path exists in the
chip code.

## Measurements

| Boot | Change under test | Texture-fit skips | Colour skips | Picture |
|---|---|---|---|---|
| 145 | first route | 238,332 pieces unrenderable, not yet broken down | - | dark, world textures missing (`phase6-b145-q2/q2-running.png`) |
| 146 | per-stage skip counters | 85,727 | 24,152 | - |
| 147 | slivers drawn flat | 68,270 | 0 | - |
| 148 | FPU saved around the draw | 2,589 | 0 | textured, lit (`q2-running.png`) |

Quake 2, attract demo, `+set vid_ref gl +set timedemo 1`; counters from
`V9XSNA7-DURING.INI`. `GL_RENDERER` reads "Velocity9x Rage IIC".

- `V9XGLP` (boot 145, `phase6-b145-glp/V9XGLP.INI`): `Result=PASS`,
  with format 1 the ICD's accelerated double-buffered 16/16 format.
- Boot 148 ICD totals (`V9XGL.LOG`, pid FFFD4975): 32,071 batches /
  1,161,798 triangles drawn by the engine, 132 untextured, 2,807 batches
  / 142,572 triangles refused (`r4`, UNSUPPORTED). Degenerate pieces
  30,779.
- Timedemo: 468 frames in 147.6 s, 3.2 fps (recorded, not a target).
  The ICD's timing lines put 8.2 s of each 10 s inside the HAL draw call
  and 0.17 s in present, so the frame is CPU triangle setup, not fill.

## Hypotheses the evidence killed

- **The texture-fit failures were range or precision limits of the
  quadratic fit.** No: the boot 147 failing inputs, replayed in a host
  test, fit at every texture size from 8 to 256. The difference was the
  FPU: the render-interface draw entry, unlike the Direct3D entries, did
  not `fnsave`/`frstor`, so the double-based setup ran under Quake 2's
  24-bit precision control word. With the save, skips fell from 68,270
  to 2,589 (commit 1f7ceb1).
- **Colour slivers need a wider interpolator.** They need none: a
  sliver whose gradient exceeds S.8.12 covers a few pixels and is drawn
  at its centroid's colour (commit 7631592).

## What remains, and why

- **Refused by policy, not defects.** Every refused state in the boot
  148 run is ARGB4444:
  - alpha test with replace (textures 1152, 256x256; 1155, 128x128).
    The chip's only alpha test is the texel alpha LSB mask, which
    expresses a threshold for 1555 and not for 4444.
  - GL_MODULATE of the 8x8 particle texture (1153) under vertex alpha
    0xD4 with SRCALPHA/INVSRCALPHA. The blend needs texel alpha times
    vertex alpha; the chip takes one or the other.
- **2,589 texture-fit skips**, cause not analysed.
- Michael has not yet looked at Quake 2 on the monitor; the only picture
  is the agent's screenshot.
