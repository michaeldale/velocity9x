# T19 FfSpecFog: the GCN and GT218 specular add did not saturate before fog; clamped, V9XD32 passes on the Oland

Date: 2026-11-25. Machine: W9QI0DAD, Radeon Oland XT `1002:6610`, `ati`
package, boots 136-137. Issue:
[2026-11-17-t19-ffspecfog-mismatch-oland-gt218](../issues/2026-11-17-t19-ffspecfog-mismatch-oland-gt218.md).

## Reading the failure

The issue recorded FfSpecFog's first bad pixel on the Oland at X8R8G8B8
as `5AD0E4h` got against `5ACDE4h` wanted. Red and blue are exact and
green is 3 over. An error in the fog factor would move every channel in
proportion to its distance from the fog colour (`40A0E0h`): red by about
26 units per unit of error and green by about 45. So green alone 3 over
rules the factor out. From the wanted green, f = 0.474 at that pixel.
The green that went into fog was then 0.627 + (0.816 - 0.627) / 0.474,
about 1.025. That is texel x diffuse + specular, not clamped to 1.

The row's vertex 2 has specular green `E6h` (0.90) and diffuse green
`4Dh`. Near it, texel x diffuse + specular passes 1 for any texel green
over about 0.33. The program V9XD32 judges against, and Direct3D,
saturate that sum before fog.

## Where

- **The core's fixed-stage program** (`v9x_d3d_ps_fixed`,
  `d3d_ps.c`) writes `add r0.rgb_sat`, and the ps.1.1 translators clamp
  r0 before fog. Not this.
- **GCN's built pixel shader** (`v9x_vi_shader_d3d_ps_built`,
  `vi_shader.c`; the Oland, Carrizo and Mullins draw it). On these
  chips the row is drawn by the engine's own specular add, not the
  stages' program. The add was a plain `v_add_f32` a channel, and fog
  followed. The colour export clamps, so this only showed under fog.
- **The GT218's fragment program tail** (`v9x_nv50_d3d_fp_tail`,
  `nv50_d3d.c`). The add was a short `fadd` without SAT, then fog: the
  same omission, which explains the same pixel failing there.
- Evergreen (`eg_shader.c`) and RV515 (`rv515_3d.c`) already saturate.

## The fix

- GCN: each specular `v_add_f32` is followed by `v_min_f32` with the
  inline 1.0, as the two-unit ADD combine already does. That is three
  words more; the longest built shader stays far under
  `V9X_VI_D3D_PS_DWORDS_MAX` (160).
- GT218: the specular `fadd` is long, with SAT, since the short form has
  no SAT. Every single-unit variant still builds within its 64 words;
  the host test builds them all.

Tests written first and seen failing (`build-host.ps1`):
- `test_gcn_sim.c`: MODULATE + specular + fog through the GCN model,
  with blue's sum at 1.3. 0.325 came out where 0.25 is right.
- `test_nv50_d3d.c`: the MODULATE+specular+fog program's expected words
  with the saturated long add. The two-unit model, now saturating after
  specular, found a fragment where the program gave 1.099 against 1.0.
- `test_vi_3d.c`: the specular shaders' words updated for the added
  `v_min_f32`s.

`run-checks.ps1` green.

## On the Oland

Installed by `run-release-checks.ps1 -Machine oland`: 8 of 8 files
hashed, boot 137, HAL SHA-256 `A63601EC...`, build `2c432932-dirty`.

| V9XD32 run | Before (boot 136, the B6 fix) | After (boot 137) |
|---|---|---|
| default | `Failures=1`, T19 FAIL | `Failures=0`, `Result=PASS` |
| `/bpp16` | `Failures=1`, T19 FAIL | `Failures=0`, `Result=PASS` |

Under FfSpecFog: `Mismatches=0`, `MaxChannelError=1`, 1,040 pixels
judged. Every other `T*Result` line is the same as before. V9XGLP,
V9XGLIDP, V9XR3DP, V9XWPRS and V9XFBCMP pass. All 7 probe runs exit 0,
the first clean V9XD32 on this card in the records.

Both runs drew T19 at X8R8G8B8 (`T19DisplayFormat=0x16`). The desktop
is 32 bpp, and V9XD32's `/bpp16` does not change T19's format. So the
R5G6B5 case (3 bad pixels in the issue) was not rerun.

## Not run

- The GT218: its card is no longer fitted in any machine. The fix is
  host-tested only.
- Carrizo and Mullins share `vi_shader.c`. They are not run.
- The R5G6B5 T19 run on the Oland, which needs the desktop at 16 bpp.

Evidence: [`../probe/t19-specfog-oland-2026-11-25/`](../probe/t19-specfog-oland-2026-11-25/)
(`release/oland/` the probe reports, `release-checks-summary.md` the
script's generated summary, moved out of `docs/decisions` because this
is not a release check).
