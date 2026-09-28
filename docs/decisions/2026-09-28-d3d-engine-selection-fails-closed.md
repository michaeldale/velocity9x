# Direct3D engine selection fails closed; publish uses the stamped type

Date: 2026-09-28

## Decision

`v9x_d3d_engine()` (every call) and `v9x_d3d_publish_engine()`
(DriverInit) now make one decision, `v9x_d3d_select_engine` in
`src/display32/d3d/d3d_select.c`, host-tested in
`tests/host/test_d3d_select.c`:

1. `V9X_DD_ENGINE_CAP_D3D_SOFTWARE` selects the software rasterizer. This
   is checked before validity, because that engine serves chips whose
   descriptor names no engine.
2. Without `V9X_DD_ENGINE_VALID` the answer is none.
3. `S3_VIRGE_DX` selects the ViRGE and `INTEL_GEN3` selects Gen3. Every
   other type selects none, including `S3_TRIO64`, `ATI_MACH64` (no
   Direct3D engine yet) and unknown values.

At publish time, "none" now means no caps are described. The callbacks are
still wired, and each declines when `v9x_d3d_engine()` resolves nothing.
The previous publish-time default returned the ViRGE for any chip it did
not recognise. The 16-bit capability clamp hid those tables, but it was
still another chip's description. The ATI plan made removing that default
a prerequisite for a second shipping hardware engine.

`v9x_d3d_engine()` keeps its validity pre-check, so its runtime answer is
unchanged for every input.

## Why the default could go

The 2026-08-29 core/engine split record measured `D3DHalFound` 1 -> 0
when publish selected on `engine_type`, which was then still zero at
DriverInit. Since then, `dd16.c`'s `v9x_dd_stamp_engine_caps` stamps
`engine_type` and `V9X_DD_ENGINE_VALID` on the `DDGET32BITDRIVERNAME`
escape, which runs before DriverInit, for every chip with a
`fill_engine_descriptor` hook. The ViRGE and Trio3D/2X hooks set the type
unconditionally, with no dependency on a mapping. Gen3 already relied on
this stamp. The ViRGE did not, because the default covered it, so the
ordering needed a measurement on the ViRGE before the default could be
removed.

## Measurement

The guest was 86Box `Win86SE` (S3 ViRGE/DX 86C375, port 9869),
1024x768x16, in Direct3D hardware mode. The matched set `V9XDISP.DRV`,
`V9XMINI.VXD` and `V9XHAL.DLL` from package build `d3d-select-20260928-a`
was installed by a WININIT rename (plain `dest=src`, no `NUL=`). This
replaced the guest's 2026-09-26 set. Boot 639. The previous binaries are
kept host-side in the session scratchpad.

`V9XDDP.EXE` reports are in
[`../probe/virge-d3d-fail-closed-select-2026-09-28/`](../probe/virge-d3d-fail-closed-select-2026-09-28/):

| Key | Before | After (two runs) |
|---|---|---|
| `D3DHalFound` | 1 | 1, 1 |
| `TexFormatCount` | 2 | 2, 2 |
| `D3DDeviceCount` | 4 | 4, 4 |
| `D3DCreateDeviceHr` | `0x00000000` | `0x00000000` |

`D3DHalFound=1` with two texture formats means the ViRGE's `describe_caps`
ran at DriverInit. Under the new selector, that happens only if the
descriptor was valid and named `S3_VIRGE_DX` by then. The software
capability was absent (`Direct3DMode=hardware`). So the stamp does precede
DriverInit on this chip.

Outside the texture-mip matrix, the before and after reports are
identical once pointer values and texture handles are excluded. Inside the
matrix, the `TexM_*_Dmiss` and `AlphaCurve_Dmiss` flags differ. The two
after runs, same build and same boot, also differ by 11 lines, all in that
matrix. That noise was already there and is not attributable to this
change.

Not established here:

- `D3DTrianglePixelOk=0`, raw `31744`, is identical before and after. It
  was 0 on the pre-change build too, so it is outside this change and was
  not investigated.
- The Trio3D/2X and Gen3 publish paths were not re-measured. Both hooks
  stamp the type the same way, and Gen3 already selected on the stamp.
- The new 16-bit driver wrote no `EngineStamp` line to `V9XHW.INI`. The
  stamp was inferred from the published caps, not read directly.

Gates: check-tree, build-host (including a mutation in which Mach64
selected the ViRGE, caught by the new test) and run-checks passed.
