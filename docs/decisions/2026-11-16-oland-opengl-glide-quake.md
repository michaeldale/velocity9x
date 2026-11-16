# Oland: OpenGL and Glide probes pass, Quake 2 and 3 draw on the chip

Date: 2026-11-16

Status: **measured, one boot.** W9QI0DAD (Oland XT `1002:6610`, 2 GiB),
boot 51, the HAL of
[`2026-11-16-oland-direct3d-hal.md`](2026-11-16-oland-direct3d-hal.md)
(`636a2fcd-dirty`). The box's `V9XGL.DLL`, `GLIDE2X.DLL` and
`GLIDE3X.DLL` predated this branch. They were replaced from the ati
package built from the same source after that commit, and hash-checked.
The old files are kept in `build\driver-results\w9qi0dad-gl-before-oland`
(not in git). No code changed. Step 5 of
[`../plans/ati-oland-gfx6-3d.md`](../plans/ati-oland-gfx6-3d.md), second
part. Desktop 1024x768 at 16 bpp throughout. Evidence:
[`../probe/oland-w9qi0dad-2026-11-16/`](../probe/oland-w9qi0dad-2026-11-16/)
`glprobes-1`, `quake3-windowed-1`, `quake3-windowed-2-shots`,
`quake2-windowed-1`. `V9XGL-summary.txt` in each holds the last lines of
the ICD's log, which is cumulative on the box.

## Probes

- **V9XGLP: PASS.** "Velocity9x Radeon Oland", GL 1.1, pixel format 1
  (16-bit colour, Z16, double-buffered).
  - Every batch went to the engine: hw 1,174 batches, cpu 0, hw-refused 0.
  - Compared with the Mullins release-check run
    (`m-jjn1zz-r3e-3dmark2000-2026-11-08/jjn1zz/glp-V9XGLP.INI`, 32 bpp),
    every differing row is a 565 rounding of the same colour (`C5` against
    `C6`, `FF` against `F8`). The `Lit5ColourMaterial` rows read "off" on
    both.
  - The four rows that are unexplained on Carrizo read as expected here:
    the geometry scene's left half, `AlphaSolid`, `Small2x2`.
- **V9XGLIDP: PASS**, 0 failures.

## Quake 3 demo (`C:\Q3Ademo`)

`quake3.exe +set logfile 2 +set s_initsound 0 +set r_fullscreen 0 +set
r_mode 3 +set nextdemo quit +timedemo 1 +demo demo001`: windowed,
640x480, 16-bit colour and Z, `GL_LINEAR_MIPMAP_NEAREST`, no
multitexture (`GL_ARB_multitexture` not offered).

| Run | Result | ICD |
|---|---|---|
| 1 | 1346 frames, 18.5 s: **72.9 fps** | hw 206,198 batches / 4,880,563 triangles; cpu 0; refused 0; 354 textures (6,284 KB), none failed |
| 2, five agent screenshots taken during it | 1346 frames, 19.6 s: 68.6 fps | hw 206,146 / 4,877,849; cpu 0; refused 0 |

Run 2's screenshots (`screens-sheet.jpg`) show the level lit, with
models, muzzle flashes, the HUD and the scoreboard, all drawn correctly.
Run 1's V9XTRACE grabs were 3DMark 2001's frames, left on the box from
that run: windowed Quake presents by blit, not flip, so the grabber
caught none of its frames. They were thrown away.

## Quake 2 demo (`C:\Q2Demo`)

`quake2.exe +set vid_ref gl +set vid_fullscreen 0 +set gl_mode 3 +set
s_initsound 0 +set logfile 2 +bind F10 timerefresh +map demo1`, as on the
Radeon 9250 (`2026-11-15-rv280-3dmark2000-quake2.md`). It reported
"Velocity9x Radeon Oland", GL 1.1, `GL_EXT_compiled_vertex_array`, and
no `GL_SGIS_multitexture`.

- `timerefresh`: 0.520 s and 0.511 s, **246.2 and 250.5 fps**.
- ICD: hw 171,732 batches / 6,273,861 triangles; cpu 0; refused 0.
- The screenshots show demo1 lightmapped, with the blaster and the HUD.
  The scene is as dark as Quake 2's GL renderer is without a gamma
  setting.

## Counters

V9XTRACE snapshots around every run: no engine idle timeouts, no
resets. `BatchesEngineRefused` stayed at 2 and `DrawsIntoPresented` at
11, both from before these runs
([`2026-11-16-oland-3dmark.md`](2026-11-16-oland-3dmark.md)).

## Not checked

- Fullscreen Quake, and 32 bpp desktops for the ICD.
- A Glide game. Only the probe has run.
- Quake 2 with gamma raised, which would show its lightmaps more clearly.
