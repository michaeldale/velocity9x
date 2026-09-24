# Back-face culling in the Direct3D core: the netbook passes all three cull tests

2026-09-25, MICHAEL-NETBOOK (945GSE / GMA 950), Win98 SE, boot 5. Driver
0.8.0 `intel-gma` with only `V9XHAL.DLL` replaced by a build of this
change (100,352 bytes, byte-identical to `build\win98se-intel-gma`),
swapped in by a WININIT.INI rename because DDHELP holds the loaded HAL.

## The problem

`d3d_i9xx.c` advertised `D3DPMISCCAPS_CULLNONE` only, honestly: its state
block programs `S4_CULLMODE_NONE` and the hardware culls nothing. 3D
WinBench 98 renders every scene with CCW culling, so it refused all 41
quality tests before creating a device
(`2026-09-24-3d-winbench-98-quality-on-the-netbook.md`). And because
Direct3D's default CULLMODE is CCW, every application that never set it
was getting back faces drawn on every engine.

## What changed

- `src\display32\d3d\d3d_cull.c` / `.h` (new, leaf, host-tested in
  `tests\host\test_d3d_cull.c`): the decision. Twice the signed area of the
  screen-space triangle; with y growing down, positive is clockwise on the
  monitor. `D3DCULL_CW` removes positive, `CCW` negative; zero area,
  non-finite area and unknown modes draw. `v9x_d3d_cull_honoured` applies a
  mode only if the engine advertises its `D3DPMISCCAPS` bit.
- `d3d_core.c`: `D3DRENDERSTATE_CULLMODE` (22) recorded per context,
  default CCW; `v9x_d3d_triangle_culled` applied in both triangle funnels
  - `v9x_d3d_draw_list` (the DX5 DrawPrimitive entry points; a culled
  triangle ends a run the way a clipped one does) and the RenderPrimitive
  loop (execute-buffer triangles, before any colour or clip work).
- `d3d_i9xx.c`: advertises `CULLNONE | CULLCW | CULLCCW`. S4 stays NONE and
  the decoder is unchanged.
- The ViRGE and software engines still claim `CULLNONE` alone, so the gate
  keeps their output exactly as it was.

Why not the hardware's S4 cull field: which Direct3D winding each S4
encoding removes is unmeasured on this part (Mesa's i915 flips the value
with the framebuffer's y orientation), and the allowlist decoder pins S4 to
NONE. The signed area is exact on vertices the core already holds, and a
triangle dropped there costs the GPU nothing.

## Two Open Watcom findings the test caught

The first implementation relied on `area > 0.0f` being false for NaN.
Under Open Watcom it is TRUE - a NaN triangle would have been culled
silently instead of reaching the clipper, which refuses it with a count.
The explicit fix, `area != area`, also failed: Watcom answers `nan != nan`
FALSE. Its x87 compares do not honour the unordered result, so no
comparison-based NaN test is reliable in this toolchain. The shipped test
reads the exponent bits (all ones = NaN or infinity). Not investigated:
whether other NaN guards in the tree that rely on comparisons
(`d3d_zfixed.c` uses `value == value`) behave as their comments say under
Watcom.

## Measured

Host: `build-host.ps1` - the new test failed 7 cases against a stub, then
passed; the NaN case failed twice as above before the bit test. Full gate:
`run-checks.ps1` green.

Netbook, 3D WinBench 98, **all overrides reset to DEFAULT** (the CCW FORCE
ON used for the earlier runs removed from `C:\WINDOWS\3DWB98.INI`):

- D3D HAL Problems tab: Cull Clockwise **ON**, Cull Counterclockwise **ON**,
  Cull None ON (`2026-09-25-netbook-3dwb98-hal-caps-cull.png`).
- Suite of four (`2026-09-25-netbook-3dwb98-cull-check.txt`), note 1
  "Direct3D HAL was used" on every row:

| test | result | captured frame |
|---|---|---|
| 1 Flat Shading | Capable | runs with no override - refused on CCW before |
| 21 Cull Counterclockwise | Capable | the five CW squares only (`...-cull-ccw-pass.png`) |
| 22 Cull Clockwise | Capable | the four CCW squares only (`...-cull-cw-pass.png`) |
| 23 Cull None | Capable | all nine (`...-cull-none-default.png`) |

Verdicts are from the benchmark's captured last frame, as before; the panel
was not watched.

## Not measured

- The rest of the 41 without the override. Every test the override let
  through should now run unmodified; not re-run.
- 3DMark99 and Final Reality with culling on. Both set CCW by default, so
  both now draw fewer triangles; any change in score or in the missing
  textures is unmeasured.
- The DX5 DrawPrimitive entry points' culling. The benchmark ran execute
  buffers, which reach the HAL through RenderPrimitive; the `draw_list`
  change is covered by the same decision function but no application has
  exercised it yet.
