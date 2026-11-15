# GT218: pixel shaders through the HAL, and DDI 8

Date: 2026-11-15

Status: implemented and measured on W9QI0DAD (ASUS GeForce 210, GT218
`10DE:0A65`, FX-6300, Win98 SE), boots 25-30. Step 4 of
[nvidia-gt218-ps11.md](../plans/nvidia-gt218-ps11.md), after step 5.
V9XD32 T19 and T17 run, and 3DMark 2001 SE completes with every test
listed (boot 30, addendum at the end). Cube maps and the texm3x3 lookups
are step 6 and refused here.

Evidence: [`../probe/w9qi0dad-gt218-ddi8-2026-11-15/`](../probe/w9qi0dad-gt218-ddi8-2026-11-15/).

## The change

- `nv50_ps11.c`: fog after the shader (`desc.fog`): r0 clamped, then
  (r0 - fog) f + fog with f v1.w, Gen3's and Haswell's rule; the colour
  in the constant block's floats 61-63. Two fogged cases join the host
  model's corpus (17 programs, all matching the interpreter).
- `nv50_d3d.c`: the shader interface `/3d:ps11` drew - a 28-move vertex
  program at scratch VP 600h, 28 results, 24 interpolants
  (`FP_INTERPOLANT_CTRL` 08180418h), all four TIC/TSC entries bound -
  chosen by `state.shader`; the 64-float constant block and wide
  vertices (28 floats, the position last with the centre added) as push
  builders. `FP_CONTROL` is now written on every draw: `USES_KIL` for a
  shader that kills, 0 otherwise, as Mesa writes it per program.
- `nv50_3d.c`: the setup's TSC limit 1 -> 3.
- `d3d_nv50.c`: `draw_shader`. Programs are translated on first use and
  cached by shader, serial, projection, unbound stages and fog in 16
  slots of 4 KiB at reserve F0000h (`gt218_reserve.h`), the gap up to the
  push area; every batch waits for the engine to drain, so a slot is
  reused in place, its code written once per channel build with
  `CODE_CB_FLUSH`. Target, depth (through the shadow target), blend and
  alpha test are the fixed-function map's; stage textures go through the
  same rules (`v9x_d3d_nv50_map_unit`). Triangles pass the guard band
  check as fixed-function ones do. A sampled cube stage or a texm3x3
  lookup is refused and counted (the core then counts the draw dropped).
- `gt218_hw16.c`: `V9X_DD_ENGINE_CAP_D3D_DDI8`.

Host: the shader state, vertex program, constants and wide vertex held
in `test_nv50_d3d.c`; the reserve layout asserted.

Known side effect: with `draw_shader` set the core claims cube maps
(`v9x_d3d_cubes_claimed`), which this engine places and samples only
after step 6.

## DirectX 8.1b on W9QI0DAD (boot 30)

W9QI0DAD had DirectX 7a, so T17 and T19 skipped and 3DMark 2001 SE could
not run. Michael chose a download: `DirectX 8.1 Windows ME and 9x.zip`
from archive.org item `direct-x-8.1-windows-me-and-9x` (SHA-1
342f664a...37d8, as the item lists), holding `DX81eng.exe`, Authenticode
Valid (Microsoft Corporation), version 4.08.01.0881, SHA-256
d1762981...c5f7, the same on the guest after `put`. Extracted with
`/Q /T:C:\DX81 /C`, `DXSETUP /silent`; its `WININIT.INI` held 92
`NUL=`/rename pairs, all DirectX files in `C:\WINDOWS\SYSTEM`; `reset`
went through them by itself (boot 30), `WININIT.INI` consumed,
`D3D8.DLL` present, the driver's four files unchanged by hash.

## Measured

V9XD32 (the step-0 binary) at 32 bpp and `/bpp16`:

| | boot 26 (DDI 8 bit, DirectX 7a) | boot 30 (DirectX 8.1b) |
|---|---|---|
| every check but T19 | identical to step 5's | identical to step 5's |
| T17 | D3D8 not loadable | HAL device, `PixelShaderVersion` FFFF0101 |
| T19 | `SKIP-NO-D3D8` | runs: 15 programs pass, 3 mismatch, 4 skip |
| `Failures` | 1 (T20) | 2 (T20, T19) |

T19 at both depths, the same verdicts:

- pass: Push, FourTex, Kill, Bem, PushFog, KillFog (pixel shaders);
  FfDot3, FfDot3F, FfBem, FfBem3, FfSpec, FfSpecU, FfSpec2, FfTwo (fixed
  stages drawn as shaders), every one with 0 mismatches and nothing drawn
  outside;
- mismatch: Cube and Normal (1,053 and 1,040 pixels): both sample a cube
  map, which `draw_shader` refuses until step 6; FfSpecFog: 3 pixels, the
  first at (12, 52), got 5ADBE7h for 5ACFDEh, 12 steps against T19's
  tolerance of 9. Not traced.
- skip: Dxt1, Dxt3 (no DXT, step 7), FfBemL8, FfBemL6 (no luminance bump
  op claimed).

3DMark 2000 default on the DDI 8 build (boot 26, DirectX 7a): completed,
score window 4739 (recorded, not compared), no fault record, no guard
drops or cache refusals, no engine timeouts.

## What Michael saw in 3DMark 2000 (boot 26)

His recording (`Y:\MWD\videos\obs\2026-11-15 22-55-23.mkv`, 5 s of the
helicopter test) shows the sky flipping between its cloud layer and flat
white, trees appearing late in the distance, and about 1.5 s of repeated
frames. Two checks:

- The step-0 build (024c14cc: one stage, core clipping, no guard band)
  ran 3DMark 2000 on boot 27; Michael saw the same sky problem there. The
  sky predates steps 4 and 5. Tree pop-in and the stutter were not
  compared; whether they are new is open.
- The guard band in perspective: `/3d:guard` gained cases with each
  vertex its own colour and weight, a sky's shape (far corners at rhw
  0.05, a far point at 0.02), judged against exact perspective-correct
  interpolation. Every pixel within one step at every distance to 32,768
  off the target, no leaks (boot 27). The band does not explain the sky.

## Addendum: 3DMark 2001 SE (boot 30)

Installed from `C:\data\3DMARK01.EXE` (version 330) by driving its
InstallShield wizard over the agent; Michael accepted the free licence
in chat. Default folder, "Purchase Later" on the registration page, no
restart asked for. The default benchmark (all 7 game, 6 theoretical and
4 DX8 feature tests; its note "not run using default settings" is the
D3D Software T&L this driver implies) completed in 7.7 minutes: Overall
Score window 2855 (recorded, not compared), no `V9XN50F.TXT`. Every test
has a result in Details, the pixel shader ones among them: Pixel Shader
153.9 fps, Advanced Pixel Shader 30.3, Environment Bump Mapping 74.3,
DOT3 Bump Mapping 22.8, Nature 11.0 (screenshot
`3dmark2001se-details-tests.png`).

Those numbers are of partly drawn frames. Between the snapshots, shader
and fixed-function draws were refused 318,626 times for a texture format
(policy counter 9) and 128,506 for a texture's shape (counter 10, which
also counts the cube and texm3x3 refusals in `draw_shader`); 160
triangles were dropped at the guard band (counter 19). The core's
`V9XPS.LOG` (which also holds V9XD32's T19 contexts from the same boot,
a few dozen triangles) sums to 7,083,898 triangles drawn under shaders
and 447,134 shader draws dropped, one 3DMark context drawing nothing. Its refused-draw records name 256x256 32-bit textures and the
cube faces the log's `cube-*` lines create; which test each belongs to
was not traced. No frame was inspected (no flips to grab).

Engine timeouts stayed 0.

## Open

- What 3DMark 2001 SE's refused draws are: cube maps (step 6), DXT
  (step 7), and whatever counter 9's formats are.
- FfSpecFog's 3 pixels; tree pop-in and stutter in 3DMark 2000 against
  the step-0 build; the 3DMark 2000 sky (pre-existing).
- Cube maps (step 6), DXT (step 7).

## Gates

`build-host.ps1` and `run-checks.ps1` green; the four installed files
hash-checked after each `reset`.
