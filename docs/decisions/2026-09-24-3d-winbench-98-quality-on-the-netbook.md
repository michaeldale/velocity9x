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

**NotCapable on the feature's own cap (19)**, each matching an OFF row in
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
test that rendered), 10,820 RenderPrimitive and 8,893 render-state calls,
3,154 textures with none refused, 4,411 flips, no engine timeouts or
resets. `D3dExecuteCalls=0` while `D3dRenderPrimitiveCalls=10820`: the
execute buffers reach the HAL as RenderPrimitive, one `D3DOP_TRIANGLE`
instruction at a time, and the runtime never calls the whole-buffer
`Execute`. (Corrected 2026-09-25: this paragraph first read the zero as
"the runtime converted them to DrawPrimitive and no execute-buffer path
ran", which was wrong - RenderPrimitive IS the execute-buffer path.)

## Pass 2: re-measuring the nine (incomplete)

Override set back to FORCE ON, suite re-run, test 21 cancelled instead
of answered so the override survived. Tests 1-20 reproduced pass 1
exactly. Of the nine:

| test | result |
|---|---|
| 23 Cull None | **Capable** - all nine squares (`...-cull-none-pass.png`) |
| 29 Alpha Transparency | **Capable** - background through leaves and pickets (`...-alpha-transparency-pass.png`) |
| 30 Source Alpha Pixel Blending | **Capable** - translucent sphere (`...-source-alpha-pass.png`) |
| 33, 37-41 | see pass 3 |

**Stopped at test 31 of 41** because the remote agent's screenshot had
degraded from about 2 s to 99 s per capture while input stayed at 0.2 s,
and the taskbar had filled with about 20 empty buttons that correspond to
no window (V9XWND inventory: 4 visible top-level windows). A leaked
screenshot helper per capture is the suspect; not established. Given
this evening's hard lock during a screenshot-plus-transfer overlap, the
run was not pushed further. Pass 2's table was not exported.

## Pass 3: the last six, after a reboot (2026-09-25, boot 4)

A reboot restored the screenshot to 4.7 s. The six were run as their own
suite (`C:\ZDBENCH\SUITES\REMAIN.ZDS`, written by hand in the `.ZDS`
format of `3d98all.zds`), CCW override still FORCE ON
(`OverrideCullCCW=1` in `C:\WINDOWS\3DWB98.INI` survived the restart).
Table: `2026-09-25-netbook-3dwb98-quality-pass3.txt`; V9XTRACE:
`...-pass3-V9XTRACE.ini` (6 contexts, 6 depth buffers accepted, 85,105
RenderPrimitive calls, 5,357 textures, none refused, no timeouts,
`D3dExecuteCalls=0`).

| test | result |
|---|---|
| 33 Alpha Vertices | **Capable** (`2026-09-25-...-alpha-vertices-pass.png`) |
| 37 Texture Swapping | **Capable** on the captured frame; the test also asks for frame-to-frame texture errors, which only the panel can show |
| 38 Narrow Z Accuracy | **Incorrect** - the left pair intersects in a sawtooth, the right pair straight (`2026-09-25-...-narrow-z-incorrect.png`) |
| 39 Wide Z Accuracy | **Incorrect** - the left pair's blue cube is missing entirely, as in the bad reference (`2026-09-25-...-wide-z-incorrect.png`) |
| 40 High Triangle Count | **Capable** |
| 41 Texture Fidelity | **Capable** - gradient banding not ruled out at screenshot scale |

Both Z-accuracy failures are at 16-bit Z (note 5: "Z buffer depth: 16
bits"). The benchmark's bad reference is the RGB emulator's own Z, so a
16-bit buffer is not automatically a fail; whether the netbook's depth
format, depth range, or the Z the HAL computes is short is not
established. `D3dDepthCaps=0x10024000`.

## Final tally, all 41

**Capable 17:** 1, 2, 4, 5, 6, 7, 12, 15, 16, 19, 23, 29, 30, 33, 37, 40,
41 - shading, Z-buffer, perspective, nearest and linear filtering,
modulate and modulatealpha, flat wrap and clamp, cull none, alpha
transparency, source alpha blend, alpha vertices, texture swapping, high
triangle count, texture fidelity.

**Incorrect 5:** 17 and 18 cylindrical wrap (claimed, not honoured),
21 cull CCW (the hardware culls nothing), 38 and 39 Z accuracy.

**NotCapable 19:** the OFF-cap list above.

17 + 5 + 19 = 41.

## What the suite says to fix, in order of reach

1. **Cull modes** (`D3DRENDERSTATE_CULLMODE` CW/CCW, `D3DPMISCCAPS_CULLCW`
   / `CULLCCW`). Without them this benchmark runs nothing unmodified, and
   neither will any title that culls and checks the cap. **Done
   2026-09-25**: `2026-09-25-back-face-culling-in-the-d3d-core.md`.
2. **Cylindrical wrap** (`D3DRENDERSTATE_WRAPU`/`WRAPV`): implement it or
   stop claiming it. **Done 2026-09-25, in hardware (S3)**:
   `2026-09-25-cylindrical-wrap-through-s3.md`.
3. **Z accuracy** at 16 bits: find which of format, range or the HAL's
   computed Z loses the wide-range cube. **Diagnosed 2026-09-25, parked**:
   both results are what a 16-bit screen-space Z buffer gives; the remedy is
   Gen3's 24-bit depth format, planned in `docs\plans\intel-24-bit-depth.md`.
4. **2026-09-25: mirror addressing, Add and Modulate pixel blending and
   DECAL done** (`2026-09-25-mirror-colour-blend-factors-and-decal.md`).
   **Mipmapping done the same day** (`2026-09-25-mip-trees-on-gen3.md`),
   with the LOD differences recorded there. The rest of the OFF list is the
   backlog: fog, DecalAlpha, colour key, specular, dithering,
   anti-aliasing. Mipmapping and colour key are the likeliest to matter for
   3DMark99's missing textures; that link is not tested.

## Not established

- Anything about the panel: every verdict is from the captured frame.
- Why Z accuracy fails at 16 bits (format, range or computed Z).
- Whether the Incorrect wrap results change with a DrawPrimitive API run.
