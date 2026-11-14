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
it (Haswell's and Gen3's). The HD 4600 and the GMA 950 have run it.

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

## Gen3: the netbook (GMA 950), boot 334

The committed code (`d7c1962d`, built as `9c7445ec-dirty` before the
commit), the whole intel-gma set by `run-release-checks.ps1`, 8 of 8
installed.

- The release probe set passes with no verdict changed from the 0.50.0
  checks: V9XD32 at DDI 6 and 5, V9XGLP, V9XGLIDP, V9XR3DP, V9XWPRS,
  V9XFBCMP.
- V9XD32 at its default, DDI 8: PASS, and all four new rows pass
  through Gen3's shader path (`V9XD32-netbook-boot334.TXT`): `FfDot3`
  worst channel error 4, `FfDot3F` 0, `FfBem` 0, `FfBem3` 8, tolerance 9
  (R5G6B5). `T19TextureOpCaps` 00A0104F. The two DXT rows skip: Gen3
  publishes no DXT.
- 3DMark 2001 SE, Bump Mapping tests only, 640x480 16-bit colour, no
  d3d8 proxy: **Environment Bump Mapping 10.1 fps**, where it read "Not
  supported by hardware"; **DOT3 Bump Mapping still "Not supported by
  hardware"**.

Why DOT3 is not offered there: 3DMark never asks. No `DOTPRODUCT3`
stage reaches `ValidateDevice` (proxy log, one run). Gen3's ShadeCaps
are 00084008 against Haswell's 00084208: no
`D3DPSHADECAPS_SPECULARGOURAUDRGB`, and the DOT3 test's second pass
takes SPECULAR as its argument. With that one bit added to what 3DMark
is told (proxy patch, one run), it validates both DOT3 passes and starts
the test (`d3d8-proxy-netbook-boot334-dot3-not-offered.txt`); that run
then ended in the proxy's own fault, as on the 4570, so it says nothing
of the picture. Gen3's AlphaCmpCaps are 0 as well, which the same run
shows is not what holds the test back. Whether Gen3 can claim specular
Gouraud shading is its own question and was not opened here.

Not done on the netbook: a full 3DMark 2001 SE run without the proxy
(one ran through the proxy by accident, all 17 selected tests to the
score dialog, and 3DMark faulted on closing with the proxy loaded; no
figure of it is kept), UT2004, and a look at a frame.

The netbook's older d3d8 proxies (in both 3DMark folders since at least
2026-11-12) are replaced by copies of the real runtime, its own
`D3DR.DLL`; its agent has no `shell` verb to rename them.

## Gen3 specular through the shader path: tried, measured, not kept

The netbook, boots 335 to 339, the whole set each time, built dirty from
`1f044d33`. Evidence in `netbook-specular-route/` of the probe folder.

Gen3's one- and two-unit programs add no specular colour
(`d3d_i9xx.c`), which is why it cannot claim the cap. The generator
already adds specular last, and Gen3's shader path draws its programs.
So: a DDI 8 draw with `SPECULARENABLE` taken through the stages' own
program (two stages, the cascade ended at a stage reading a texture
with none bound, as the fixed-function path draws it), and
`SPECULARGOURAUDRGB` told to DDI 8 programs only. A pure helper with a
host test seen to fail first (six checks against a stub), and a fifth
probe row, `FfSpec`.

| Boot | Build | Result |
|---|---|---|
| 335 | route and cap | probe set passes; V9XD32 DDI 8 PASS, `FfSpec` pass, ShadeCaps 00084208 |
| 335 | the same | 3DMark 2001 SE bump tests: Environment Bump Mapping 10.1 fps, **DOT3 Bump Mapping 5.8 fps** |
| 335 | the same | all 17 tests: 951; High Polygon Count 0.6 MTriangles/s and **N/A** (8 lights) |
| 336 | neither (control) | High Polygon Count 0.7 / 0.6 |
| 337 | route and cap | High Polygon Count 0.6 / **N/A** again |
| 338 | cap, no route | High Polygon Count 0.7 / 0.6 |

So the cap alone changes nothing in that test and the route breaks it.
On boot 337 the driver's own account is clean: 3,078,828 triangles drawn
under the program in the 8-light test's context (the same count in both
runs), none dropped, no kernel refused, no malformed record, no fault,
and the frame looks right
(`3dmark2001se-boot337-route-8lights-frame.png`). The run of both tests
took 46 s against 40 s. Why 3DMark writes N/A instead of a lower figure
was not found.

Not kept. The shader path gathers triangles one at a time into wide
vertices and has no indexed run; it is the wrong place for a scene of
plain lit triangles, as it was for UT2004's `MODULATE2X` stages (37 % of
the frame rate, boots 329-330). The code, the limits field and the host
test are out of the tree. `FfSpec` stays in V9XD32: it skips on Gen3
(no cap) and judges the engine's own specular add elsewhere.

What Gen3 specular needs instead is the specular colour in its own
programs: a specular form of each one-unit program beside its fog form
(`i9xx_fog.c`; the fog stream already carries the specular dword per
vertex), the same in the two-unit builder (`i9xx_multitex.c`), and the
decoder taught both. That is register-level work with its own
measurements, done the same day:
[`2026-11-14-gen3-specular-and-bump-luminance.md`](2026-11-14-gen3-specular-and-bump-luminance.md),
which also has `BUMPENVMAPLUMINANCE`.

`FfSpec` on the HD 4600 (the 4570, the driver it was left on, the probe
alone replaced; the boot number was not read): pass, worst channel
error 9 of 9 at R5G6B5, V9XD32 PASS (`V9XD32-hsw-ffspec.TXT`). That is
Haswell's fixed-function kernel adding the specular colour.

The netbook did not come back from the restart that was to put the
committed driver code on again (from boot 338, the cap-only build): the
eight files were staged by WININIT rename and `reset` sent, and the
agent did not answer again, nor the address. Michael found it on a blue
screen: "A fatal exception 0E has occurred at 018F:BFF86EBD. The
current application will be terminated." The agent's log for boot 338
ends `reset-notified 33 1072ms`, `reset-flush C`, as a reset's does,
so the log does not say whether the fault came in what was left of boot
338 or in a start of Windows after it that never reached the agent.
Which program faulted is not known, nor whether the cap-only build of
boot 338 had a part in it; `C:\WINDOWS\FAULTLOG.TXT` does not exist.
One occurrence in five resets this session.

Boot 339, after Michael power-cycled it from the blue screen (no key
was tried, so whether Windows would have gone on is not known): all eight installed files
match `1f044d33`'s package by SHA-256, WININIT.INI consumed, the probe
set passes with no verdict changed, V9XD32 at DDI 8 PASS with ShadeCaps
00084008 and `FfSpec` `skip-no-specular`. The netbook is left on it.

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
- Ivy Bridge, which shares `d3d_hsw.c`, has no run. Gen4 and Gen5 have
  no shader path and are unchanged. Gen3 is above.
- The three-stage host test was written with its code, not seen to fail
  first; the two-stage tests failed against stubs first (ten checks).
- UT2004 was not rerun. Its 814 draws a botmatch with a cube on stage 1
  and the texture factor in a stage now translate; nobody has looked.
- A look at the DOT3 test's picture, and a 32 bpp desktop.

Gates: `build-host.ps1` and `run-checks.ps1` passed on the final tree.
