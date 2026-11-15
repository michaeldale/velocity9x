# GT218: OpenGL drew black because the render interface did not name the engine

Date: 2026-11-16

Status: fixed and measured on W9QI0DAD (V9XGLP, boot 31) and on M-BXMFA8,
where Daniel reported it (Quake 3 demo, boot 136: every textured draw on
the card; no picture yet).

Evidence: [`../probe/gt218-opengl-describe-2026-11-16/`](../probe/gt218-opengl-describe-2026-11-16/).

## The report

M-BXMFA8 (Daniel's GeForce 210, release 0.52.0's driver), 2026-11-15, by
remote-help chat: Quake 3 shows a static corrupted or black picture,
windowed and fullscreen, while its sound runs; Quake II the same, and on
exit "GLimp_EndFrame() - SwapBuffers() failed".

## What the logs and a rerun showed

`V9XGL.LOG` on M-BXMFA8 holds his runs (the ICD logs every process with
its exe). Quake 3 demo started at 11:57-12:00 and the Quake II demo at
12:11-12:12. A Quake 3 rerun by the agent (14:03, boot 135) gave a black
window (`m-bxmfa8-q3-0.52.0-black.png`) and its log
(`m-bxmfa8-q3-0.52.0-V9XGL-excerpt.txt`):

- `describe result=0 engine=0 formats=00000002`: the render interface
  named no engine for this HAL;
- every textured draw `result=4 submitted=0`, the hardware-texture
  counters all zero (`hwtex creates=0`), and the "cpu" path counting
  about 150,000 triangles every 10 seconds in about 46 ms of draw time;
- about 90 swaps a second, every present `DD_OK`: frames were presented,
  but nothing textured was ever drawn into them.

## Cause

`v9x_r3d_describe` (d3d_core.c) has a branch per engine that sets the
engine id, the renderer name and, through `hw_texture_size_max`, whether
the ICD may put textures in DirectDraw surfaces. The GT218's engine had
none: the ICD kept every texture as CPU levels, the GT218 map refuses CPU
levels (as Cedar, the R6xx, RS690 and RIVA 128 maps do), and this engine
has no software fallback, so each textured draw was dropped. The RS690
and the RIVA 128 hit the same omission and their branches say so.

The SwapBuffers failure at Quake II's exit was not traced; no
`present hr=` line was logged in any of the runs.

## The change

- `r3d_abi.h`: `V9X_R3D_ABI_ENGINE_NV50` 17 (a new value of an existing
  field; front ends only log it). 18 since the merge into main, where the
  Radeon 9250's R200 had taken 17; the logs below were taken at 17.
- `d3d_core.c`: the GT218 described as "Velocity9x GeForce 210", with
  video-memory textures 1 to 2048, power-of-two edges, all three
  interface formats: what `v9x_d3d_nv50_create_surface` places and the
  Direct3D texture checks have drawn.

## Measured: V9XGLP on W9QI0DAD

Same probe binary, boot 30 (DDI 8 build, no fix) against boot 31 (fix):

| | before | after |
|---|---|---|
| `Renderer` | Velocity9x | Velocity9x GeForce 210 |
| `ReplaceRgbBlendOk`, `ReplaceRgbFogBlendOk` | 0, 0 | 1, 1 |
| textured pixel reads (`Texture*Pixel`, `MipQuad*`, `Sky*`, `SubImage*`) | all 0 | drawn (e.g. `MipQuad64` 00FF0000, `SkyModulateWhite` 009C8E63) |
| every other `*Ok` / `*Result` | | identical |

So W9QI0DAD reproduced Daniel's failure in the probe: every textured
read-back was black before the fix.

## Measured: Quake 3 demo on M-BXMFA8

The fix build (`gl-describe-dirty`, V9XGL 0.53.0) was installed by the
WININIT route on boot 136. All five files were hash-checked and
`WININIT.INI` was gone. The boot needed the owner's power-cycle: a
bootable USB stick had been left in the machine. The agent started the
Quake 3 demo, which ran for about 190 s
(`m-bxmfa8-q3-fixed-V9XGL-excerpt.txt`):

- `describe result=0 engine=17`;
- every textured batch on the card: `hw=777330/36842034`, with
  `cpu=0/0` and `hw-refused=0/0`;
- `hwtex creates=10 create-failed=0`, 731 uploads (721 partial: the
  intro cinematic);
- about 91 swaps a second, as before the fix.

The agent's screenshots of the game window timed out through the relay,
so this run has no picture. The visual check is the owner's: Quake 3
and Quake II, asked by chat, answer pending.

The relay needed one change for this install: the 1.2 MB HAL upload
timed out twice at the agent tool's pipeline window of 32 chunks (1 MB
in one relay POST), and went through with a window of 4. That is a
remote-agent matter, recorded here only because it decided the route.

## Not yet seen

The owner's view of Quake 3 and Quake II; GL two-unit draws (the
interface still reports one unit, `texture_units` 1).

## Gates

`run-checks.ps1` green; the four driver files hash-checked on W9QI0DAD
after `reset`.
