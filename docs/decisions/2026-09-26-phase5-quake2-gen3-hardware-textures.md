# Phase 5: Quake 2 on Gen3 - hardware textures, 1.2 to 11 fps

Date: 2026-09-26. Machines: MICHAEL-NETBOOK (GMA 950, Win98 SE,
1024x576 565, 10.0.1.248) and 86Box `Win98SE-Fast-D3D` (software engine,
127.0.0.1:9878). Quake 2 3.14 demo, `quake2.exe +set vid_ref gl
+set vid_fullscreen 0 +set gl_mode 3 +map demo1`, 640x480 windowed.

## Measured

`timerefresh` at the start of demo1 (128 frames through a full turn):

| Netbook | fps |
|---|---|
| Every textured draw through the software fallback (aac2834) | 1.24 |
| Gen3 samples the textures (this change) | 11.05 |

Logs: `2026-09-26-phase5-quake2-gen3-timerefresh-fallback.log`,
`...-timerefresh-hwtex.log`. The demo pak has no recorded demo, so there
is no timedemo; `timerefresh` is the repeatable number.

Frames of the same view: `...-frame-cpu-fallback.png`,
`...-frame-hwtex.png`, and Quake 2's own software renderer
`...-frame-ref-soft.png`.

## What changed

- The render interface is ABI version 2. Describe states the hardware
  sampler's texture limits (`hw_texture_size_max`, `hw_texture_shape`):
  256, square, power of two on Gen3; none on the other engines.
- The ICD keeps a DirectDraw texture chain per texture object that fits,
  filled from its 16-bit copy through Lock and refilled when the object's
  images change, dropped with the object, the context or a mode change.
  A batch the interface refuses with UNSUPPORTED is sent again with the
  CPU copy.
- Gen3's accepts refuses, for interface draws, a surface texture its bind
  would draw untextured (Direct3D keeps its counted behaviour). The
  software fallback does not take a surface texture: Gen3 lays a chain out
  as one tree the software sampler cannot read.
- REPLACE and DECAL on a texture without alpha send the texel's alpha
  when nothing reads the fragment's, so Quake 2's world pass is the decal
  Gen3 draws.
- The ICD's window mapping clamps each clipped vertex to the viewport and
  the depth range. Gen3's stream builder refuses a sign bit (including
  -0.0) or a coordinate past the surface, and float error at a clipped
  edge produced both: the probe's perspective quad came back INDETERMINATE
  (Gen3 refusal code 6, vertices) until the clamp. Host test first.

## Probe scenes (V9XGLP), Gen3 and software engine

`2026-09-26-phase5-quake2-gen3-textures-{gen3,soft}-V9XGLP.ini`. Agree on
both, and where the software guest briefly ran Microsoft's generic GL (its
HAL was still ABI 1) the mip scene agreed with that too:

- mip levels 0, 2, 4 selected by quad size;
- one texture rewritten by glTexSubImage2D between three draws: A, B, C;
- a 256x256 single-level RGB texture, LINEAR/CLAMP and NEAREST/REPEAT,
  REPLACE under an orange colour and MODULATE under white;
- translucent MODULATE with alpha 0.33 over grey, and the ZERO/SRC_COLOR
  lightmap multiply;
- a far quad under Quake 2's near 4 / far 4096 frustum, RGB and RGBA,
  REPLACE and MODULATE with orange.

The software engine truncates in the lightmap multiply where Gen3 rounds:
grey x (206, 101, 41) reads (99, 48, 16) on the CPU and (99, 52, 24) on
Gen3.

## What the evidence disputes

The texture problems Michael saw on the netbook before today were the
textured draws Gen3 refused and the application never heard about
(2912111); with the fallback (aac2834) the frame matched the software
guest.

With hardware textures the frames differ in one place that matters: the
sky seen through the ceiling. Gen3's sky matches Quake 2's software
renderer; the software engine's is darker and has no blue at all
(`...-sky-cpu-gen3-refsoft.png`). Quake 2 sends the same sky draws to both
(`...-draw-states-{gen3,soft}.txt`: 256x256 RGB565, REPLACE, no blend,
LEQUAL, white vertices), and none of the isolated scenes above reproduces
the difference. Filed as `docs/issues/2026-09-26-software-engine-quake2-sky.md`.
The hypotheses killed on the way: stale Gen3 mip levels, a stale texture
cache, lost sky draws (a pink `gl_clear 1` never showed), the environment
or filter or address mode, far depth and tiny rhw, RGBA storage.

## Not established

- What the software engine does to Quake 2's sky.
- Non-square textures on Gen3, which still take the CPU path (the bind is
  square-only; its layout for non-square exists since d7a07ce).
- Fullscreen, `vid_restart`, and a mode change through the texture copies.
- The per-batch cost left: every GL_POLYGON is its own batch, because
  glEnd flushes.
