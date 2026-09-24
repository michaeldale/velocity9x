# 3D WinBench 98's quality suite on the netbook: culling, wrap, and a caps list that is mostly honest

2026-09-24/25, MICHAEL-NETBOOK (945GSE / GMA 950), Win98 SE, driver 0.8.0
`intel-gma` build `890c828`, boot 3. 3D WinBench 98 Version 1.0, 3D
Quality suite (41 tests), defaults: Direct3D HAL, execute buffers,
640x480x16 full screen, front buffer. How the suite works and how it was
installed: `docs\specifications\3d-winbench-98.md`.

Verdicts were given remotely by Claude from the benchmark's own verdict
page, which shows the test's captured last frame beside a good and a bad
reference image. The panel itself was not watched; a verdict here says
the captured frame matched a reference, not that the panel did.

## Pass 0: every test refused on one cap

With default settings, tests 1 and 2 (Flat and Gouraud shading) returned
"This capability is not supported", no Direct3D context was created
(`D3dContextCreates=0`, mode never left 1024x576), and the result note
read **"The HAL on Primary Display Driver cannot run this test. The
unsupported settings are: CCW Culling."**
(`...-3dwb98-pass0-ccw-refusal.png`).

Cause, in `src\display32\d3d\d3d_i9xx.c` (the `dpcTriCaps.dwMiscCaps`
assignment): the Intel HAL advertises `D3DPMISCCAPS_CULLNONE` only,
deliberately, because its state block programs `S4_CULLMODE_NONE` and the
hardware culls nothing. The benchmark's scenes all render with CCW culling,
so it refuses every scene before creating a device. The claim is honest;
the gap is real: **D3DRENDERSTATE_CULLMODE is not implemented.**

## Pass 1: CCW culling forced on in the benchmark

The D3D HAL Problems tab overrides a reported cap; Cull Counterclockwise
was set FORCE ON (a benchmark setting, nothing in the driver changed).
Full table: `...-3dwb98-quality-pass1.txt`; V9XTRACE after it:
`...-quality-pass1-V9XTRACE.ini`.

**Capable (10):** 1 Flat Shading, 2 Gouraud Shading, 4 Z-buffer,
5 Perspective Correction, 6 Nearest, 7 Linear, 12 Modulate Texture
Blending, 15 ModulateAlpha Texture Blending, 16 Flat Wrap Texture
Addressing, 19 Clamp Texture Addressing. Each captured frame matched its
good image (`...-modulate-pass.png` as an example).

**Incorrect (3):**

- **17 Cylindrical Wrap u, 18 Cylindrical Wrap v** - the HAL reports both
  ON and the frame matches the *bad* reference: the seam is interpolated
  across the whole cylinder instead of wrapping
  (`...-wrap-u-incorrect.png`, `...-wrap-v-incorrect.png`). A cap claimed
  and not honoured - the advertise-then-ignore pattern.
- **21 Cull Counterclockwise** - all nine squares drawn, CCW included,
  matching "culling turned off" (`...-cull-ccw-draws-all.png`). Expected
  under the override; it confirms the hardware culls nothing.

**NotCapable on the feature's own cap (18)**, each matching an OFF row in
the caps readout: 3 Dithering; 8-11 all four mipmap filters; 13 Decal,
14 DecalAlpha; 20 Mirror addressing; 22 Cull Clockwise; 24-26 fog
(vertex linear, table linear, table exponential); 27 Specular; 28 Color
Key; 31 Add and 32 Modulate pixel blending; 34-35 fog combinations;
36 Anti-aliasing.

**Refused on "CCW Culling" alone (9):** 23, 29, 30, 33, 37-41. Not a
driver result. Answering No on test 21 made the benchmark record CCW
culling as broken, which reset the FORCE ON override to DEFAULT (it also
set FORCE OFF on the two wrap rows answered No). Every later test needing
CCW culling was then refused.

Driver counters for pass 1: 13 contexts created and destroyed (one per
test that rendered), 10,820 DrawPrimitive and 8,893 render-state calls,
3,154 textures with none refused, 4,411 flips, no engine timeouts or
resets. **`D3dExecuteCalls=0`**: the benchmark reports "Execute buffers",
but the Direct3D runtime converted them to DrawPrimitive before the HAL -
this run did not exercise any execute-buffer path in our driver.

## Pass 2: re-measuring the nine (incomplete)

Override set back to FORCE ON, suite re-run, test 21 cancelled instead
of answered so the override survived. Tests 1-20 reproduced pass 1
exactly. Of the nine:

| test | result |
|---|---|
| 23 Cull None | **Capable** - all nine squares (`...-cull-none-pass.png`) |
| 29 Alpha Transparency | **Capable** - background through leaves and pickets (`...-alpha-transparency-pass.png`) |
| 30 Source Alpha Pixel Blending | **Capable** - translucent sphere (`...-source-alpha-pass.png`) |
| 33, 37-41 | not reached |

**Stopped at test 31 of 41** because the remote agent's screenshot had
degraded from about 2 s to 99 s per capture while input stayed at 0.2 s,
and the taskbar had filled with about 20 empty buttons that correspond to
no window (V9XWND inventory: 4 visible top-level windows). A leaked
screenshot helper per capture is the suspect; not established. Given
this evening's hard lock during a screenshot-plus-transfer overlap, the
run was not pushed further. Pass 2's table was not exported.

## What the suite says to fix, in order of reach

1. **Cull modes** (`D3DRENDERSTATE_CULLMODE` CW/CCW, `D3DPMISCCAPS_CULLCW`
   / `CULLCCW`). Without them this benchmark runs nothing unmodified, and
   neither will any title that culls and checks the cap.
2. **Cylindrical wrap** (`D3DRENDERSTATE_WRAPU`/`WRAPV`): implement it or
   stop claiming it.
3. The OFF list is the backlog: mipmapping, fog, decal modes, mirror
   addressing, colour key, add/modulate framebuffer blends, specular,
   dithering. Mipmapping and colour key are the likeliest to matter for
   3DMark99's missing textures; that link is not tested.

## Not established

- Anything about the panel: every verdict is from the captured frame.
- Tests 33 and 37-41 under a working CCW cap.
- Whether the Incorrect wrap results change with a DrawPrimitive API run.
