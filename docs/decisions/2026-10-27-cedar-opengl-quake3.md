# HD 5450 runs Quake 3 through the OpenGL ICD in hardware

Date: 2026-10-27

Status: **measured, one card.** M-BXMFA8's HD 5450 (`1002:68F9`, Cedar),
boots 32 and 33. Follows [3DMark 2000](2026-10-27-cedar-3dmark2000.md).
Captures in
[`../probe/m-bxmfa8-cedar-quake3-2026-10-27/`](../probe/m-bxmfa8-cedar-quake3-2026-10-27/).

## Why OpenGL could not have drawn in hardware

- `d3d_eg_map.c` refused every explicit draw. That is how the OpenGL ICD
  and Glide reach the engine, with a scissor and a colour write mask.
- The core published no surface textures for this engine to the render
  interface (`hw_texture_size_max` 0). So the ICD would have kept every
  texture in CPU memory, and the engine refuses those too. The Rage 128
  had the same gap until boot 534, when Half-Life ran at 0.75 fps in the
  software engine.

## Boot 32: Quake 3 found no pixel format

The Quake 3 demo (`C:\games\Q3Ademo`, 1.11, `timedemo 1`, `demo demo001`,
started with `+set nextdemo quit`) failed `GLW_ChoosePFD` and quit to its
console. The ICD log said `device: no render interface` and described no
pixel formats. The guest's `V9XGL.DLL` was the 0.30.1 package's build
(`66ab7fcb`), left by the vbe install. The deployments on this branch
replaced the display driver, the mini-VDD and the HAL, but not the ICD.
This boot measured nothing about the engine.

## The change

- `d3d_eg_map.c` takes an explicit draw's scissor (a non-empty rectangle
  inside the target, or the fallback) and its write mask.
- `eg_3d.c` gains `masked_channels`, which clears CB_TARGET_MASK bits.
  Its host test was written first and failed.
- CPU levels, a second texture unit and destination-alpha blends still
  go to the fallback.
- `d3d_core.c` publishes this engine's surface textures to the interface
  as for the Rage 128: powers of two, 1 to 2048, all three formats.

## Boot 33: 68.8 fps, every batch on the hardware

DRV, VXD, HAL and ICD from one build, each checked by hash after the
reboot. Quake 3 defaults: 640x480, 16-bit colour and Z,
`GL_LINEAR_MIPMAP_NEAREST`. It reports GL_RENDERER "Velocity9x Radeon HD
5450", GL 1.1, no extensions, GL_MAX_TEXTURE_SIZE 512.

| Run | Result | ICD paths |
|---|---|---|
| fullscreen | 1346 frames, 19.6 s: **68.8 fps** | hw 210,992 batches / 5,366,396 triangles; cpu 0; hw-refused 0 |
| windowed | 1346 frames, 20.0 s: **67.4 fps** | hw 210,982 / 5,365,814; cpu 0; hw-refused 0 |

The ICD created and uploaded 355 surface textures (6,286 KB) with no
failures or evictions. The HAL snapshot afterwards has `EngineFlags=9`,
no idle timeouts and no refusals. One windowed screenshot
(`b33-windowed-1.png`) shows the frame drawn as expected:
lightmapped walls and floor, torch flames, player models, the weapon,
HUD digits and console text.

Each run was made once, and the numbers are recorded, not compared.

## Not checked

- One frame was inspected, not the run.
- No software-engine run exists to compare against. Boot 32's failure
  happened before any frame.
- Glide uses the same explicit path, but no Glide game was run.
- No multitexture: the engine has one unit, so Quake 3 draws its
  lightmaps in a second pass.
