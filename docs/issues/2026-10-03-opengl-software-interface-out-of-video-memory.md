# An OpenGL session on the Rage XL ran on the CPU after "out of video memory"

Date: 2026-10-03. Reported by MarxVeix on an ATI 3D Rage XL AGP
(`1002:474D`, 8 MB, 0.10.0). Evidence:
`docs/probe/rage-xl-agp-marxveix-2026-10-03/V9XGL.LOG`, process
`FFFD3753`. Status: open; which application this was is inferred.

## Symptom

In one ICD session the render interface answered with engine 1
(software), not 4 (Mach64): `describe result=0 engine=1`. Creating the
640x480 surface then failed 82 times:
`surface caps=00006040 640x480 failed`, `hr=8876017C`
(DDERR_OUTOFVIDEOMEMORY), `front: surface refused`. Draws returned 6
(NO_MEMORY), and every batch took the CPU path (`cpu=867/37315` in the
last interval, `hw=0/0`). The reporter's Quake 1 screenshot renders
correctly, so this was probably that run, drawn by the CPU.

Another session (`FFFD2AE9`, engine 4) drew 877,507 triangles on the
hardware path. The card works; this session did not use it.

## Questions

- Why engine 1: the Direct3D mode changed in the Velocity9x tab, or the
  HAL fell back? The reporter mentions trying the tab's options.
- Why a 640x480 offscreen surface does not fit in 8 MB. Find the desktop
  mode at the time; DxDiag showed 800x600x16.

## Next

Ask the reporter for the desktop mode and the tab settings during the
Quake 1 run, and a V9XTRACE snapshot right after it. If it reproduces,
reproduce it on a Rage Pro-class card here.
