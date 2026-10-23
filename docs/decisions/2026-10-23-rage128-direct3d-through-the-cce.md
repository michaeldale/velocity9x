# Rage 128 Direct3D through the CCE: V9XD32 passes, 3DMark 2000 and Half-Life run

Date: 2026-10-23

Status: **measured, one card.** A8U4I5's Rage Fury Pro (`1002:5046`), boots
526 and 527, the ati package from the working tree on `c1f4710c` (HAL,
driver and trace builds agree). Phase 5 of
[ati-rage128.md](../plans/ati-rage128.md), on the builder measured in the
[builder record](2026-10-23-rage128-3d-builder-gouraud-z-texture.md).
Evidence: [`../probe/a8u4i5-rage128-d3d-2026-10-23/`](../probe/a8u4i5-rage128-d3d-2026-10-23/).

## What was built

- `src\display32\d3d\d3d_r128.c`: the HAL engine. It resolves surfaces,
  places textures and Z buffers, and submits through the CCE in PIO mode.
  For each batch it:
  - waits for the 2D engine to go idle;
  - loads the microcode once, checked by reading back the first pair;
  - selects 192PIO and starts the microengine free-running;
  - writes the assembled stream in EVEN/ODD pairs as `PM4_STAT` reports
    room;
  - waits for the CCE to go idle and for the GUI pixel cache to go idle;
  - stops the CCE back to NONPM4;
  - flushes the non-GUI pixel cache.

  A timeout stops the CCE and quarantines the engine. The microengine is
  never left running, because a free-running microengine hung A8U4I5
  while idle.
- `src\display32\d3d\d3d_r128_map.c`: the neutral draw mapped to the
  builder's state. It is host-tested in `tests\host\test_d3d_r128_map.c`.
- `src\chipsets\ati\rage128\r128_cce_microcode.c`: linux-firmware's
  `r128_cce.bin` (MIT, AMD 2000) as a C array, with the notice kept in the
  file.
- `rage128_hw16.c` sets `CAP_D3D`, and the manifest declares `Direct3D =
  'hardware-rage128-cce'`. `d3d_select.c` maps the type to the engine.
- Z buffers are placed by the engine, not by DirectDraw's heap.
  `Z_OFFSET_C` counts 32-byte units and `Z_PITCH_C` counts 8-pixel units,
  while the heap's `dwZBufferAlign` is 8. V9XD32's Z16 landed at
  `0x12C000` with a 1280-byte pitch.

## V9XD32

**16 bpp, boot 526: PASS on the first run.** T1 to T8 all pass: flat and
Gouraud colour, depth, offscreen target, depth precision, an ARGB1555 decal
texture, a half-alpha blend, and sloped depth. The Z read-back is linear
and matches the builder record (front `0x4000`, kept `0x8000`). The
snapshot shows the draws went to the engine: 17 sink batches, every refusal
counter at zero, no idle or FIFO timeouts.

**32 bpp, boot 526: FAIL, on the X byte only.** Every RGB value was right,
but each drawn pixel read back as `0xFF......` (T1 `0xFF13579B`); T7, the
blend, read `0xBF807E00`. The engine's only 32-bit destination datatype
is ARGB8888, so it writes alpha into what DirectDraw calls the X byte of
XRGB8888. The fix sets `PLANE_3D_MASK_C` to `0x00FFFFFF` for a 4-byte
target. The test was written first and failed
(`test_r128_3d.c:225`), and passed after the fix.

**Boot 527, with the fix: PASS at 32 bpp and at 16 bpp**, with
`T1Pixel=0x0013579B`.

## 3DMark 2000

The default benchmark (1024x768, 16-bit colour and Z, triple buffering)
ran to the end and scored **993**. ATI's driver scored 2404 on the same
card. The snapshot after the run shows 3DMARK2000.EXE with five D3D
contexts, 42,135,739 triangles in 963,518 batches, 2,642 placed
textures, no refusals, no timeouts, and the engine still valid.

The display panel named the card "Velocity9x: ATI Rage XL PCI". That is the
devnode's `DriverDesc` left in the registry from an earlier card swap
(`Display\0007`, installed for the Rage XL). It is not code.

## Half-Life Direct3D, mwd5

| Run | fps |
|---|---|
| cold | 23.1 |
| warm | 31.2 |
| warm | 31.2 |

ATI's driver: 34.7 cold, 59.2 to 59.5 warm. The snapshot shows HL.EXE
drawing 53 million triangles in total, with alpha test, three filter
modes and wrap and clamp addressing seen, and no refusals. The frames were
not seen: the agent's screenshot is blind while the game flips, and it
showed only the console afterwards. Image correctness in Half-Life is
therefore **unverified**. V9XD32's pixel checks are the evidence for
correctness.

## What this does not show

- Each batch starts and stops the CCE, about 44 triangles a batch in 3DMark.
  This is the safe first form; the score gap is not investigated here (no
  benchmark chasing).
- Mip chains, trilinear filtering, specular and fog are encoded from Mesa
  and exercised by 3DMark and Half-Life, but no probe has checked their
  pixels.
- Z24/Z32 and the second texture unit are not built.
- The "one draw late" fault after a power-on (builder record) has not been
  retried. Boots 526 and 527 were warm restarts.
