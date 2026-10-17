# HD 4600 render interface, step 1: CPU textures refused, textures sampled, the tiled clear through the engine

Date: 2026-10-18. Machine: the i5-4570 (`8086:0412`), boot 18 (before)
and boot 19 (after), the HAL replaced by WININIT rename. Step 1 of
[intel-haswell-glide-3dcif-opengl.md](../plans/intel-haswell-glide-3dcif-opengl.md).
Evidence:
[`../probe/hsw-4570-render-interface-step1-2026-10-18/`](../probe/hsw-4570-render-interface-step1-2026-10-18/).

## What changed

- **`d3d_hsw_map.c`** (new, host-tested by `test_d3d_hsw_map.c`, which
  failed eight checks before the function was written). It refuses:
  - a draw with CPU levels on either unit (`REFUSE_CPU_TEXTURE`, reason
    9);
  - on an explicit draw, a write mask other than RGB (10);
  - a scissor that does not cover the target (11).

  `v9x_d3d_hsw_map` asks it first.
- **`describe`** has a Haswell case: engine 9 (`V9X_R3D_ABI_ENGINE_GEN75`,
  appended; the ABI version stays 5, as only the ID is new and front ends
  only log it), renderer "Velocity9x HD 4600", surface textures 1 to
  2048, powers of two.
- **The render interface's clear** sends depth through `v9x_hal_depth_fill`
  when the engine's depth is tiled, rectangle by rectangle, after a CPU
  colour clear. A refused fill fails the clear.

## Measured

**V9XR3DP**, before and after:

| | boot 18 | boot 19 |
|---|---|---|
| `DescribeEngine` / renderer | 0 / "Velocity9x" | 9 / "Velocity9x HD 4600" |
| `ClearOk` (red, depth `FFFF`) | 1 | 1 |
| `DrawOk` (three quads, LESSEQUAL) | 1 | 1 |
| textured draw under a half scissor | drew both halves (`FFE0`, `FFE0`): `TexturedScissorOk` 0 | refused (result 4), target untouched |
| draw with a partial mask | wrote all channels (`FFFF`): `MaskedOk` 0 | refused (result 4), target untouched |

The two Ok keys are 0 both times. Before, they were 0 because the picture
was wrong; now the draw is refused, until step 2 programs a scissor and
a mask.

The clear's depth readback reads `FFFF` either way, so it cannot show the
tiling. A uniform value is the same in any layout, and V9XR3DP reads
through Lock, which returns tiled bytes.

**V9XGLP** (the ICD probe): `Result=PASS` and no Ok key 0, both boots.
The texture readings are the difference:

| | boot 18 | boot 19 |
|---|---|---|
| texel phase rows (`TexPhase0Cols`) | `FFFFFFFFFFFFFFFF` (white: untextured) | `0123456789ABCDEF` |
| sub-image quads A/B/C | the vertex colour `FF8200` three times | `FF0000`, `0000FF00`, `000000FF` |
| sky, lightmap and non-square readings | white or the vertex colour | texel colours |
| mip quads 64/16/4 | `0000FFFF` | `00FF0000` each: level 0, the known limit |

**So before this change every ICD texture was drawn untextured on this
engine, and now they are sampled from video memory.**

Readings not yet explained against a reference: `TexPhaseM5Cols`
(`123456789ABCDEF.`) and `M5Rows`. V9XGLP records them as readings, not
checks; they look like the half-pixel offset, unconfirmed.

The driver counted 121 render-interface draws and no refusal. Refusals
made in `accepts` are not counted: the ops table defines `accepts` as
passive.

## Not done

- Step 2 (scissor and mask), then OpenGL titles.
- Glide and 3DCIF were not run. Glide's textures take the same surface
  path; 3DCIF's raw depth is still refused.

Gates: check-tree, build-host (the new tests failed before the function
existed), run-checks.
