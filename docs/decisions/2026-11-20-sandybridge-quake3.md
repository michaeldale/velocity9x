# Quake 3 on the HD 3000's Gen6 path: 95 fps at 32-bit colour, 24-bit Z

Date: 2026-11-20
Status: **measured twice, M-NZHWBR boot 64.** Step 7 of
`docs\plans\intel-sandybridge-direct3d.md`. Build `dc48a9b4-dirty`
(branch `intel-snb`). Evidence:
`docs\probe\m-nzhwbr-snb-hd3000-quake3-2026-11-20\`.

## How

Quake 3 Arena demo 1.11 at `C:\games\Q3Ademo`, the Oland recipe
(`2026-11-16-oland-opengl-glide-quake.md`): `quake3.exe +set logfile 2
+set s_initsound 0 +set r_fullscreen 0 +set r_mode 3 +set nextdemo quit
+timedemo 1 +demo demo001`, windowed 640x480 on the 1024x768x32 desktop.
Everything else from the owner's `q3config.cfg`: 32-bit colour and
textures, a 24-bit depth buffer. The owner's config was copied off before
and put back after (same bytes). V9XTRACE snapshots before and after.

## Result

| Run | Result |
|---|---|
| 1 | 1346 frames, 14.1 s: **95.3 fps** |
| 2, screenshots during it | 1346 frames, 14.3 s: 94.4 fps |

`qconsole.log`: `GL_RENDERER: Velocity9x HD 3000`, GL 1.1,
`GL_ARB_multitexture GL_EXT_compiled_vertex_array GL_SGIS_multitexture`,
`PIXELFORMAT: color(32-bits) Z(24-bit) stencil(0-bits)`,
`GL_LINEAR_MIPMAP_NEAREST`, multitexture enabled. Its first pixel-format
request asks for 8 stencil bits and fails (`GLW_ChoosePFD failed`), as on
every engine: the ICD offers no stencil format; the 24-bit Z request
without stencil is taken.

The ICD's counters for run 1 (V9XGL.LOG tail): hw 153,328 batches /
4,152,529 triangles, of them 48,879 batches with both texture units;
cpu 0; refused 0; 355 textures (6,286 KB) created, none failed or lost.

HAL counters across both runs (`before-` to `after-run2-V9XSNAP.INI`): no
engine idle timeout or reset, `BatchesEngineRefused` unchanged at 24,
`DrawsIntoPresented` 0.

The screenshot during run 2 (`run2-shot1.jpg`) shows the level lightmapped,
the player model and the rocket launcher textured, smoke, coloured name
text and the HUD, drawn as expected by eye.

## What this adds to the V9XD32 record

- A 24-bit Z buffer (D24_UNORM_X8, Y-tiled) on Gen6, which V9XD32 had
  not asked for.
- Two texture units in one kernel on Gen6: the second sample from m1-m2
  and the stage-1 combine.
- The OpenGL ICD on the Gen6 builders, at 32-bit colour.

## Not run

Fullscreen Quake 3, Quake 2, a Glide game, the monitor.
