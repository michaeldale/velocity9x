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

## Later: fullscreen, vid_restart, front buffer and lifetime

Quake 2 as Michael runs it (fullscreen 640x480 from his config, a mode
change from the 1024x576 desktop): `timerefresh` 11.21 fps, then
`vid_restart` rebuilt the context and the frame came back textured
(`...-fullscreen-vid-restart.{log,png}`).

The ICD now draws GL_FRONT: a front surface per drawable, made on first
use from the screen, kept equal to it by every swap and shown after each
front draw or clear; GL_NONE draws nothing; glReadBuffer(GL_FRONT) reads
the front. V9XGLP on both engines (`2026-09-26-phase5-front-and-
lifetime-{gen3,soft}-V9XGLP.ini`): a yellow quad drawn to the front after
a black swap is on the screen at once with the rest still black, reads
yellow from the front and black from the back, and a GL_NONE quad
changes neither.

Lifetime: 50 cycles of create context, make current, a 64x64 mipmapped
texture, a textured draw, swap, release and delete, with the window
resized at cycle 25. No failures on either engine, and free video
memory after cycle 26 equals free video memory after cycle 50 exactly
(Gen3 5,392,640 bytes; software 14,801,152).

## Later: non-square textures on Gen3, 11 to 22 fps

Counted per path in the ICD (`...-paths-before-nonsquare.txt`): with
square maps on Gen3, 180,748 of Quake 2's batches (478,294 triangles, a
quarter of the textured ones) went to the CPU for being non-square, each
after a drain of the GPU.

Gen3's shape rule is now one pure function, `v9x_d3d_i9xx_texture_shape`
(powers of two, the larger edge within 8..256, the smaller down to one
texel), host-tested and asked by the placement, the chain walk, the bind
and accepts; describe drops the square flag. MAP_STATE has always carried
width and height separately; the square rule was this driver's, not the
sampler's, and the Direct3D caps still say SQUAREONLY. The ICD's texture
chains take width and height (they were square by construction, so the
first non-square upload failed and fell back).

V9XGLP's non-square scene (`2026-09-26-phase5-gen3-nonsquare-{gen3,soft}-
V9XGLP.ini`): a 64x16 map of 4 x 2 coded cells on a 256x64 quad, its
16x64 transpose, and a 64x16 chain whose levels 0 and 2 are selected by
quad size. Every cell's centre reads the texel it should, identically on
Gen3 and the software engine, and the ICD's counters show every batch on
Gen3.

Quake 2, `timerefresh` at demo1's spawn point with `notarget` (so no
monster moves the player; the earlier 11.05 was also at the spawn point,
untouched): 21.69 and 21.82 fps (`...-timerefresh-nonsquare.log`). Every
textured batch is Gen3's (`...-paths-after-nonsquare.txt`: 1,231,233
batches, 440 refused and resent to the CPU, none non-square).

The frame (`...-frame-all-hardware.png`) agrees with the software
engine's: the grey sky, the pale ledge and the top-right piece were
products of the mixed frame, not of either engine. The issue is retitled
and keeps the mixed-engine depth hypothesis open.

## Later: held batches, 22 to 28 fps

After non-square textures, Quake 2 drew 1,231,233 batches of 2.7
triangles on average: every glEnd went through the render interface, and
each call costs the ICD's lock and window bind, the Win16 mutex, surface
validation, the state translation and a Gen3 submission whatever its
size.

The ICD now holds triangles across glEnd while they would be drawn the
same way - the same fragment state and texture description
(`v9x_gl_prim_same_draw`, host-tested first) into the same colour
buffers - up to the interface's 64 a batch. The held batch keeps its own
copy of the texture description and the texture's name. It is drawn
before every texture command, glClear, glReadPixels, glFlush, glFinish,
SwapBuffers, and a context release, switch or delete, so the images its
levels point at cannot change under it.

V9XGLP on both engines reads back identically to the run before the
change, scene for scene (`2026-09-26-phase5-batched-{gen3,soft}-V9XGLP.ini`).
Quake 2 at demo1's spawn under notarget: 28.35 and 28.42 fps
(`...-timerefresh-batched.log`), with 129,316 batches for 4,691,652
triangles, 36 a batch (`...-paths-batched.txt`). The frame
(`...-frame-batched.png`) differs from the unbatched one by 0.3, 0.2 and
0.1 levels on average, in the gun's bob, the falling sparks and Quake 2's
blinking help icon.

| Netbook, timerefresh at demo1's spawn | fps |
|---|---|
| Every texture on the CPU fallback | 1.24 |
| Square textures on Gen3 | 11.05 |
| Non-square textures on Gen3 | 21.7 |
| Held batches | 28.4 |

## Not established

- Why Gen3 and the software fallback disagree when they share a frame
  (the issue's depth hypothesis is unmeasured).
- Two GL processes at once, GL beside Direct3D, and a context used from
  a second thread.
- Which Quake 2 draws Gen3 refuses (683 batches in the batched run, sent
  again to the CPU).
