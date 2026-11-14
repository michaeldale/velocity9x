# DOT3 and environment bump mapping as texture-stage ops: drawn through the stages' own pixel shader, both claimed; 3DMark 2001 SE runs both tests

Date: 2026-11-14. The follow-up parked in
[`../plans/haswell-ddi8-pixel-shaders.md`](../plans/haswell-ddi8-pixel-shaders.md).
Machine: the Dell i5-4570 (HD 4600, W9QI5A83), boots 247 to 259, the
whole intel-gma set by `run-release-checks.ps1` each time, built from
`9c7445ec` plus this change, dirty. Evidence:
[`../probe/hsw-dot3-embm-2026-11-14/`](../probe/hsw-dot3-embm-2026-11-14/).

## What changed

- **The generator** (`v9x_d3d_ps_fixed`, `d3d_ps.c`, pure) that writes
  fixed-function stages as a ps.1.1 program, made for UT2004's cube maps,
  now takes three stages and three more things:
  - `DOTPRODUCT3` as a colour op: `dp3_sat r0.rgb` of both arguments
    read signed (`_bx2`). Colour channels only; the stage's alpha op
    runs beside it. As an alpha op it is refused (1.1's `dp3` cannot
    write alpha alone).
  - `BUMPENVMAP` on stage N: stage N+1 is sampled by `texbem` from stage
    N's texel, and stage N passes CURRENT. Two bump stages in a row are
    refused. `BUMPENVMAPLUMINANCE` is refused: no luminance bump format
    is published.
  - `D3DTA_TFACTOR`: constant c0, and c1 holds it signed for
    `DOTPRODUCT3`, so no constant carries a modifier.
- **The environment** (`v9x_d3d_ps_fixed_env`): the texture factor into
  c0 and c1, and each `texbem`'s matrix taken from the bump map's stage.
  Direct3D keeps a fixed-function bump matrix on the bump map's stage;
  `texbem` reads the sampled stage's.
- **The core** (`d3d_core.c`): `D3DRENDERSTATE_TEXTUREFACTOR` is kept. A
  DDI 8 draw with no shader bound takes the program when its stages hold
  `DOTPRODUCT3` or a bump op, not only when one binds a cube.
  `ValidateTextureStageState` answers one pass for stages the program
  draws whole.
- **The caps**: `D3DTEXOPCAPS_BUMPENVMAP` and `D3DTEXOPCAPS_DOTPRODUCT3`
  in the DDI 8 caps of an engine that runs shaders.
- **V9XD32 T19** has four rows drawn with no shader bound and judged
  pixel by pixel against a program written by hand in the probe, not the
  HAL's: `FfDot3`, `FfDot3F` (against the texture factor), `FfBem` (bump
  on stage 0) and `FfBem3` (base, bump, environment).

The route is the shared core's, so every engine with `draw_shader` takes
it (Haswell's and Gen3's). Only the HD 4600 has run it.

## Measured

| Boot | Build | V9XD32 default |
|---|---|---|
| 247 | two stages, both caps | PASS; `FfDot3`, `FfDot3F`, `FfBem` pass |
| 248 | three stages, both caps | PASS; those and `FfBem3` pass |
| 249 | three stages, `BUMPENVMAP` only | PASS; the same four pass |

Each boot: 8 of 8 files installed by hash, and V9XD32 at DDI 5, 6 and 7,
V9XGLP, V9XGLIDP, V9XR3DP, V9XWPRS and V9XFBCMP pass as in the 0.50.0
checks.

The desktop is R5G6B5, so the tolerance is T19's 565 one, 9. `FfDot3`
and `FfBem3` have a worst channel error of 9, at the limit, as `Push`,
`FourTex` and `PushFog` do; `FfDot3F` 4, `FfBem` 0. Not run at 32 bpp.

`ValidateDevice` answered one pass for all four rows.

3DMark 2001 SE, Bump Mapping tests only, 1024x768x32, compressed
textures:

- **Boot 247.** Environment Bump Mapping ran 2,976 draw calls, but every
  `ValidateDevice` was refused (`88760819`): 3DMark has the base map on
  stage 0, `BUMPENVMAP` on stage 1 and the environment on stage 2, and
  the first generator drew two stages. Then the DOT3 test faulted.
- **Boot 248**, three stages. Environment Bump Mapping draws
  (`3dmark2001se-embm-boot248-frame318.png`): the bumped, reflecting
  figure and seat. Nobody has compared it with a reference card's frame.
  Then the DOT3 test faulted again, at the same address.
- **Boot 249**, `DOTPRODUCT3` unclaimed. The run completes: Environment
  Bump Mapping 33.0 fps, DOT3 Bump Mapping "Not supported by hardware".
  No fault (`WatchFaultTick=0`), no engine timeout, no shader triangle
  dropped. Recorded, not compared.

- **Boots 250 to 258** were measurement and control builds for the DOT3
  fault, none kept (the issue has them).
- **Boot 259**, this tree, **no d3d8 proxy in 3DMark's folders**:
  - the probe set passes, T19 with all four rows; `T19TextureOpCaps`
    00A0104F;
  - Bump Mapping tests alone: Environment Bump Mapping 47.5 fps, DOT3
    Bump Mapping 28.8 fps;
  - all 21 tests: **2524 3D marks**, Environment Bump Mapping 48.0 fps,
    DOT3 Bump Mapping 29.0 fps. No fault (`WatchFaultTick=0`), no engine
    timeout or reset, no draw refused, no shader triangle dropped.
    Recorded, not compared.
  The 4570 is left on it.

Every 3DMark figure above boot 259 was taken with our d3d8 logging proxy
in 3DMark's folders, which costs about a third of the frame rate (48
against 29 to 33 fps in the bump test).

Nobody has looked at a DOT3 frame: the run was judged by its completing
and its counters. The grabbed bump-test frame is from boot 248.

## The DOT3 fault was our own proxy

Through boots 247 to 256, 3DMark's DOT3 test ended in an access
violation in 3DMark's renderer whenever `DOTPRODUCT3` was claimed, and
for a while the cap was withheld for it. The cause is in the
[issue](../issues/2026-11-14-haswell-3dmark2001se-dot3-renderer-fault.md):
3DMark leaves one entry of its DOT3 material's settings unwritten, and
the d3d8 logging proxy that earlier sessions left in its folders moved
the stack so that the entry read 2, the pool number of a vertex buffer
the runtime had just created. With the proxy renamed away the test runs
on a build that claims the cap and changes nothing else (boot 258).

Two DevCaps bits (`TLVERTEXVIDEOMEMORY`, `HWVERTEXBUFFER`) made the test
run with the proxy in place (boot 257). They are not in the tree: they
would have changed what every DirectX 8 program is told about vertex
buffers to work around a tool of ours.

## Recalled, not measured

- That a fixed-function colour `DOTPRODUCT3` leaves alpha alone: the
  DirectX 8 SDK's D3DTEXTUREOP page, from memory.
- That a `BUMPENVMAP` stage passes CURRENT: the reference rasterizer,
  from memory. 3DMark's stage 1 has CURRENT as its second argument, so
  its frames do not test the other reading.
- That the signed read is `2x - 1` on both arguments, the sum unscaled.

## Not done

- `BUMPENVMAPLUMINANCE`, and the L6V5U5 and X8L8V8U8 formats it needs.
- DirectX 7 programs: only DDI 8 draws take the shader path, and the
  DDI 7 caps are unchanged. 3DMark 2000's bump tests were not run.
- Gen3 (`d3d_i9xx.c`) has a `draw_shader` and so the same route, and no
  run; nor has Ivy Bridge, which shares `d3d_hsw.c`. Gen4 and Gen5 have
  no shader path and are unchanged.
- The three-stage host test was written with its code, not seen to fail
  first; the two-stage tests failed against stubs first (ten checks).
- UT2004 was not rerun. Its 814 draws a botmatch with a cube on stage 1
  and the texture factor in a stage now translate; nobody has looked.
- A look at the DOT3 test's picture, and a 32 bpp desktop.

Gates: `build-host.ps1` and `run-checks.ps1` passed on the final tree.
