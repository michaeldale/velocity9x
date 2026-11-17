# Oland: `draw_shader` in the HAL, and DDI 8

Date: 2026-11-17

Status: implemented and measured on W9QI0DAD (Oland XT `1002:6610`,
FX-6300, Win98 SE) at boots 58 and 59. The ati package was built from
this commit's tree (`1c631c63-dirty`). Its DRV, VXD, HAL and SETP were
put in place by WININIT rename and hash-checked. This is step 5 of
[ati-gcn-ps11.md](../plans/ati-gcn-ps11.md), on the Oland alone. Carrizo
and Mullins stay on one stage and DDI 6.

Evidence: [`../probe/oland-ddi8-w9qi0dad-2026-11-17/`](../probe/oland-ddi8-w9qi0dad-2026-11-17/).

## The change

- **Engine caps.** `oland_engine16.c` stamps
  `V9X_DD_ENGINE_CAP_D3D_DDI8` beside `D3D`: EngineCaps 40800017 at boot
  58.
- **`v9x_d3d_vi_draw_shader`** (`d3d_vi.c`), in the Oland's engine
  table only.
  - Programs are cached on the CPU: 8 entries, keyed by program,
    serial, projected and unbound stages, fog, alpha test and its
    function. Each is translated by step 2's `v9x_vi_ps11_build` and
    rewritten for SI, up to 512 dwords.
  - The fixed-function map is run on the draw's plain form for the
    target, depth, blend, fog colour and alpha reference.
  - Each batch puts the PS, its 108-dword table (stage T#s and S#s,
    constants, bump matrices, luminance, fog colour, alpha reference)
    and its wide vertices into the CP's arena. Nothing on the GPU is
    rewritten while it may be read.
  - At most 256 triangles a batch, through the wide VS: 28 floats, a
    stride-112 V#, 6 parameters.
  - Cube stages and the texm3x3 lookups are refused as TEXTURE_SHAPE
    until step 6.
- **Channels.** The convention step 3 measured: a shader stage's T#
  reads red into x (`texture_rgba`, swizzle Z Y X W, or Z Y X 1 when
  opaque). The CB swaps it back with ALT for XRGB8888 and STD_REV for
  RGB565. The fixed-function path keeps memory order.
- **Alpha test in the translator.** GCN has no fixed-function alpha
  test. After fog, `vi_ps11.c` clamps r0.a, compares it with the table's
  reference using the D3D function (`v_cmpx`), and branches to the null
  export when no lane is left. `test_vi_ps11.c` adds a GE case with a
  texkill and fog, and an LT case. 21 programs match the interpreter in
  the GCN model.
- **CP.** A ninth shader slot, `V9X_VI_CP_SLOT_VS_WIDE`, holds the wide
  VS. It is uploaded on GFX6 only.
- **Map.** `v9x_d3d_vi_map_shader_unit` maps a shader stage's texture
  and sampler with the one-unit rules, and sets `texture_rgba`.
  `test_d3d_vi_map.c` checks the swizzle words.

## Measured

V9XD32 at boot 58, run with and without `/bpp16`:

Correction (step 6, same day): both runs had a 16 bpp desktop
(`T19DisplayFormat=0x17`, R5G6B5, tolerance 9). T19 draws in the
desktop's format, so at this step it was measured at R5G6B5 only, not at
32 bpp as first written here. Step 6's runs include a 32 bpp desktop
([2026-11-17-oland-cubes-signed-formats.md](2026-11-17-oland-cubes-signed-formats.md)).

- T12, T13, T14, T15, T16: PASS, as at boot 54. T18: no T&L device, as
  before.
- T17: the HAL reports pixel shader 1.1 (`FFFF0101`).
- T19 used to skip with no pixel shader. It now runs, with the same
  verdicts in both runs (both at R5G6B5, above):
  - pass: Push, FourTex, Kill, Bem, PushFog, KillFog, FfDot3, FfDot3F,
    FfBem, FfBem3, FfSpec, FfSpecU, FfSpec2, FfTwo.
  - mismatch: Cube and Normal. Both sample a cube map, refused until
    step 6.
  - mismatch: FfSpecFog, 3 pixels, the first at (12, 52), got 5ADBDEh
    for 5ACFDEh. That is the pixel and expected value the GT218's step 5
    recorded
    ([2026-11-15-gt218-draw-shader-ddi8.md](2026-11-15-gt218-draw-shader-ddi8.md)),
    on another vendor's hardware, so the cause is more likely in what
    the two share (the core's fixed-stage program, or V9XD32's expected
    value) than in either back end. Not traced.
  - skip: Dxt1 and Dxt3 (step 7); FfBemL8 and FfBemL6 (no luminance bump
    op claimed).

So T19 reports FAIL, from Cube, Normal and FfSpecFog: the GT218's
verdicts at the same step.

V9XDDP at boot 58: 80 keys. All are 1 except `MipTriDegradedOk=0`,
which is the reading for a part that blends between mip levels.

3DMark 2001 SE, default benchmark, 17 tests:

- **Run 1 (boot 58):** hung the machine in Car Chase High Detail, with
  the CP stalled and the fences 38 behind. Recorded as an open issue
  ([2026-11-17-oland-3dmark2001se-cp-stall-hang.md](../issues/2026-11-17-oland-3dmark2001se-cp-stall-hang.md)).
  That test draws no pixel shaders.
- **Run 2 (boot 59, same build):** completed with 3DMark score 1915,
  in 7.4 minutes. The 2026-11-16 DDI 6 one-stage build scored 1486 (not
  compared, recorded).
  - Its pixel shader log has 65 shaders created.
  - Some shader draws were refused for texture shape (reason 10: cube
    maps and texm3x3) or texture format (reason 9). Those are step 6
    and 7.
  - The log caps its refusal lines, so no total is given here.
  - The frame grabs cover the Car Chase tests only. Whether Nature drew
    anything was not looked at (step 8).

## Not done or not known

- The hang's cause, and how often it happens.
- Nature's frames, the Advanced Pixel Shader test's frames, and how
  many shader triangles were dropped in 3DMark 2001 SE.
- Carrizo and Mullins: `draw_shader` is in the Oland's table only. The
  SI wide VS is the Oland's; GCN 3 needs its own.
