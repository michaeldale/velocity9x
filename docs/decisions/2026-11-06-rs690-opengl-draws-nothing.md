# RS690: OpenGL draws nothing because every render-interface draw is refused and dropped

Date: 2026-11-06

Status: **cause found by probe and code; mixed-engine probe run; no fix
applied.** M-GB8KRE (remote help, Radeon X1200 / RS690 `1002:791F`,
desktop 16 bpp), boot 86, ati package `3faeae06`. Evidence:
[`../probe/0.38.0-cva-checks-2026-11-06/rs690/`](../probe/0.38.0-cva-checks-2026-11-06/rs690/)
(V9XGLP, V9XR3DP) and
[`../probe/rs690-mixed-engine-2026-11-06/`](../probe/rs690-mixed-engine-2026-11-06/)
(`V9XDDP /mixed`; the run is `V9XDD2.INI`).

## What was seen

V9XGLP's `Cva` case read black on the RS690
([checks](2026-11-06-gl-ext-compiled-vertex-array-checks.md)), and so did
every other drawing check in that report. Its 0.37.0 report has the same
`OffsetNoneOk=0` and `FogLinearOk=0`, so this is not new.

## Where the draws go

V9XR3DP drives the HAL's render interface directly, as the ICD does.
Against the HD 4600 on the same build:

| | HD 4600 | RS690 |
|---|---|---|
| `DescribeEngine` | 9 (GEN75) | **0** |
| `DescribeRenderer` | Velocity9x HD 4600 | Velocity9x |
| `DrawAResult` / `DrawASubmitted` | 0 / 2 | **4 (UNSUPPORTED) / 0** |
| `DrawOk`, `MaskedOk` | 1, 1 | 0, 0 |

The code says why:

- The ICD, Glide and 3DCIF draw through the render interface, which marks
  every draw `explicit_state = 1` (`d3d_core.c`).
- `v9x_d3d_rs690_map_draw` refuses every such draw
  (`V9X_D3D_RS690_REFUSE_EXPLICIT`), with the comment that they "are the
  software engine's for now".
- `v9x_r3d_fallback` gives the software engine only to Gen3, the two MGA
  engines and the Rage 128. For the RS690 it returns nothing, so the draw
  returns UNSUPPORTED and nothing is drawn.
- `v9x_r3d_describe_body` has no case for the RS690 engine, so it reports
  engine 0 and the generic name.

Killed: that the ICD fails to reach the HAL. Describe, negotiate and the
state calls succeed on the RS690 exactly as on the HD 4600; only the
draws fail. The 0.37.0 RS690 record's "OpenGL is unaffected" was wrong
for the same reason: it read V9XGLP's `Result`, which does not count the
drawing checks.

The Evergreen map, by contrast, draws explicit draws on the engine with
their scissor and write mask, and refuses only CPU-resident levels and a
second unit.

## The mixed-engine probe

The step the 0.37.0 record named before the RS690 could join the
fallback. `V9XDDP /mixed`, at 16 bpp, Z16:

| Step | RS690 | Software engine, MGA-2164W, Rage 128 |
|---|---|---|
| Engine draws red at z 0.5 | `F800`, depth **`7FFF`** | `F800`, `8000` |
| CPU stores green, z 0.25 | `07E0`, `4000` | `07E0`, `4000` |
| Engine's second draw where the CPU wrote | rejected: `07E0`, `4000` | the same |
| Beside it, z 0.375 | `001F`, depth **`5FFF`** | `001F`, `6000` |

`MixedOrderingOk=1`, `MixedColorEncodingOk=1`, **`MixedDepthEncodingOk=0`**,
so `MixedOk=0`. The RS690's Z16 holds one less than the software engine's
at both depths. The software engine rounds `z x 65535` (32767.5 to
32768). Truncating it, or `z x 65536 - 1`, both give the RS690's readings;
these two depths do not tell them apart.

## What the depth difference would and would not affect

A fallback draw is drawn by the CPU into the same target and depth buffer
the engine uses. A one-step difference matters where the two engines draw
the same surface into one depth buffer: a second pass with LEQUAL drawn
by the CPU over a first pass drawn by the RS690 fails by one step.

On the RS690 that case does not arise through the render interface: the
map refuses every explicit draw, so with the fallback every OpenGL, Glide
and 3DCIF triangle would be the CPU's, and the engine none. Depth clears
are filled with a value computed by shared code
(`v9x_d3d_depth_fill_value`), not by either engine, so the far plane and
any cleared value are the same for both. It would arise only if the map
later drew some explicit draws on the engine and refused others.

## Options

1. Add the RS690 to `v9x_r3d_fallback` despite `MixedOk=0`, on the
   argument above, recorded here; and give `describe` an RS690 engine
   value and name. OpenGL, Glide and 3DCIF would then draw on the CPU,
   as they do under the vbe package. Not measured yet: speed on this
   machine.
2. As 1, and also make the software engine's Z16 conversion match the
   RS690's when it falls back for it, which would keep a future
   hardware/CPU split exact. Needs a third depth reading to choose between
   truncation and `z x 65536 - 1`.
3. Draw explicit draws on the RS690 engine, as the Evergreen does: the
   R300-class scissor and write-mask registers, probed first. Faster, and
   larger.
