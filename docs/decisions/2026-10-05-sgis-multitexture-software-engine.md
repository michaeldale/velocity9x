# GL_SGIS_multitexture on the software engine: the probe exact, Quake 2 uses it

Date: 2026-10-05. Plan: `docs/plans/gen3-sgis-multitexture.md`, step 3
(the CPU rasterizer and software engine, the reference Gen3 will be held
to). Guest: 86Box `Win98SE-Fast-D3D` (127.0.0.1:9878, vbe package,
software Direct3D), boot 600, 1024x768x16. Driver, mini-VDD, HAL,
settings page and ICD deployed together from this tree by WININIT rename;
the previous set (2026-09-26) is kept in the session scratchpad.
Evidence: `docs/probe/sgis-multitexture-soft-2026-10-05/`.

## What changed

- The CPU rasterizer has `v9x_d3d_raster_triangle2`: a second texture at
  each vertex's `u1`/`v1`, on unit 0's q, with its own filter, address
  mode, mip chain and level of detail. Unit 1 combines with unit 0's
  result, and its alpha op follows unit 0's (contract section "A second
  texture unit"). `v9x_d3d_raster_triangle` is it with no second texture,
  and its output does not change by a bit.
- The software engine takes a render-interface draw with a second unit on
  CPU levels and says two units in its limits, so the ICD offers
  `GL_SGIS_multitexture` on this engine.

## Measured

**V9XGLP** (`V9XGLP-boot600.ini`): `SgisAdvertised=1` (the name with its
trailing space), both entry points, and every case within its tolerance:

| case | read | GL 1.1 (before 565) |
|---|---|---|
| MODULATE | 0x636508 | 100, 100, 13 |
| REPLACE | 0x84FF42 | 128, 255, 64 |
| BLEND toward blue | 0x630063 | 100, 0, 101 |
| DECAL, alpha 128 | 0xA5B639 | 164, 178, 57 |
| unit 1 s = 0.25 (black half) | 0x000000 | 0, 0, 0 |
| unit 1 s = 0.75 (white half) | 0xC66531 | 200, 100, 50 |
| unit 1 disabled | 0xC66531 | 200, 100, 50 |

Every other key of the report equals the last software-engine run
(`2026-09-26-phase5-serious-sam-soft-V9XGLP.ini`), except `Extensions`.

**Quake 2 3.14 demo**, demo1 spawn, 640x480 windowed, `notarget`:

- With the extension Quake 2 prints `...using GL_SGIS_multitexture`;
  with `gl_ext_multitexture 0`, `...ignoring GL_SGIS_multitexture`.
- `timerefresh`: **0.762 fps** with it, **0.511 fps** without
  (`timerefresh-mtex{1,0}-qconsole.log`). Recorded, not a target: an
  emulated Celeron drawing every pixel on the CPU.
- The ICD's census (`mtex1-V9XGL-excerpt.log`): world batches carry unit
  0 as RGB565 REPLACE and unit 1 as the 128x128 lightmap page, RGB565
  MODULATE (`tex1=10xx/01010200`); 19,455 two-unit batches by the 223 s
  line. No draw failure and no refusal.
- Frames (`spawn-mtex1.png`, `spawn-mtex0.png`, console closed): over the
  left wall (79,200 pixels) 82% are identical, 16% differ by one or two
  565 steps (4, 8 or 9 levels), and 2% by more. Over the panel on the
  right (11,200 pixels) 95% are identical and the rest are within two
  steps. The one-to-two-step differences are what the two-pass path's
  intermediate 565 store predicts. The 2% tail was not separated from
  the scene's own changes between runs: the second frame has an
  explosion mid-air with its dynamic light, and Quake 2's light styles
  animate.
- With `gl_lightmap 1` (unit 1 REPLACE, the lightmaps alone,
  `spawn-lightmap-only-unit1.png`) the lightmaps sit where the scene's
  shadows and lights are, so unit 1's coordinates are right.

**Withdrawn on the way:** a first pair of frames, taken with the console
open, showed the left wall much brighter with multitexture. The frames
with the console closed do not reproduce it. That pair's second frame was
taken at a different moment of the same scripted scene; it is not
evidence of a defect, and it is not kept.

## Not measured

Half-Life (not installed on this guest), GLQuake (no media), and a run
with a texture copy the HAL refuses (no hardware engine here). Gen3 is
step 4.

Gates: build-host (tests written first and seen failing; one perspective
mutation of unit 1 caught by `test_second_unit_samples_as_unit_zero`),
run-checks.
