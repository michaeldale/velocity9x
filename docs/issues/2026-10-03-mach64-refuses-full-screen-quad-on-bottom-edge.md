# The Mach64 render interface rejects a full-screen quad whose vertex is on the bottom edge

Date: 2026-10-03. Reported by MarxVeix on an ATI 3D Rage XL AGP
(`1002:474D`, 0.10.0). Evidence:
`docs/probe/rage-xl-agp-marxveix-2026-10-03/V9XGL.LOG`, process
`FFFD2AE9` (engine 4). Status: open; whether anything is missing on
screen is unverified.

## Symptom

The session's first six draws returned 1 (INVALID) with nothing
submitted. Each was three vertices of one full-screen textured quad:
- `sx=441FFFFF sy=43F00000`, which is 639.99994, 480.0;
- `sx=0 sy=0`;
- `sx=44200000 sy=0`, which is 640.0, 0.

The textures were 512x256 and 128x128, and every later draw of the
session was accepted. The likely candidate is a console or menu
background in Quake 2.

## Suspect

`y = 480.0`, and `x = 640.0`, are exactly the target edge, which is a
half-open bound. The validation or the clip in the render interface may
treat the edge as outside rather than as a closed edge with no pixel
centre beyond it. Not checked in the source.

## Next

A host test in `test_r3d_validate.c` with this quad on a 640x480 target.
Write the test first; if it rejects, fix the edge rule.

## 2026-10-04: the suspect was wrong; fixed in the ICD, unverified on hardware

Nothing validates vertex positions there. The draws carried a 512x256
texture. `v9x_r3d_validate_levels` refuses a level past the engine's
`texture_size_max` (256 on the Mach64 and the Rage IIC) as INVALID, and
the ICD drops an INVALID draw, where on UNSUPPORTED it would have retried
with the CPU copy. The ICD now fits textures to the interface's limit
before the draw (`v9x_gl_tex_fit`, tested in `test_gl_texture.c`). That
is either the chain's own smaller levels, or a box-filtered copy of a
single level. Quake 2 on the Rage XL never binds a texture over 256 (0
INVALID draws), so the fix has not been seen on hardware. Quake 1, the
likely source, is not installed here.
`docs/decisions/2026-10-04-rage-xl-small-textures-sync-version-and-oversize.md`.
