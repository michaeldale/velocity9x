# M-7V636C (RIVA 128ZX): the Glide 2 probe draws no triangles on the 0.37.0 candidate

Date: 2026-11-06. Machine: M-7V636C, RIVA 128ZX (12D2:0018), nvidia
package, desktop 800x600. Found in the 0.37.0 release checks. Michael:
review in a later release.

## What was seen

| Probe | Installed driver, boot 39 (build not identified) | 0.37.0 candidate `55a661a2`, boot 40 |
|---|---|---|
| V9XD32 `/bpp16` | 1 failure (T21) | 1 failure (T21) |
| V9XGLP | PASS | PASS |
| V9XGLIDP | FAIL, 1 (Line) | **FAIL, 14** |
| V9XR3DP | COMPLETE | COMPLETE |

On the candidate, Glide's open, clears, flip and front-buffer checks
pass. Every triangle, line, textured, chroma-keyed, blended and fogged
check reads the clear colour or 0000. That is the same pattern as the
RS690, whose engine refuses render-interface draws and has no software
fallback (`docs/decisions/2026-11-06-rs690-0.37.0-checks.md`).

T21's one failure is the RIVA's known mip floor: levels below 8x8 are
not drawn (T21Ok 7, the 4x4 quad). It is the same on both drivers.

## Not known

- Which build was installed at boot 39. It was a RIVA development
  build, so whether the released 0.36.0 nvidia package already draws
  nothing in Glide is not known.
- Why the candidate refuses or drops Glide's draws on this engine.

## Next

Run the 0.36.0 nvidia package's files on this card. If Glide draws
there, bisect `0.36.0..55a661a2` for the nvidia engine's
render-interface path (`v9x_r3d_fallback`, the NV3 map's refusals).

## 2026-11-07: explained, not a regression

The boot 39 run did not use Velocity9x's Glide. It used 3dfx's, on the
machine's second card, a Voodoo 1 (Canopus Pure3D, `121A:0001`).
Velocity9x's Glide has never drawn a triangle on the RIVA.

Evidence, all from M-7V636C, still on boot 40, read on 2026-11-07:

- `C:\V9XDIAG\V9XGLIDE.LOG`
  (copy: `docs/probe/m7v636c-riva128-glide-2026-11-07/V9XGLIDE.LOG`).
  Our GLIDE2X.DLL appends a line to it on every attach. It holds two
  sessions, both build `55a661a2` on boot 40 (uptime 7 and 64 minutes).
  There is no line from boot 39, so the run there loaded some other
  GLIDE2X.DLL. In both sessions the device reads `describe result=0
  engine=0`, and all 29 draws are refused with result 4 (UNSUPPORTED),
  untextured and textured alike.
- `C:\WINDOWS\SYSTEM\FXMEMMAP.VXD` exists. That is 3dfx's memory-mapping
  VxD for Voodoo Glide. `GLIDE2X.DLL` there is now ours (84,992 bytes),
  put in place by the 0.37.0 WININIT rename.
- V9XR3DP, which calls the HAL directly with no Glide DLL involved, gives
  the same answer on both drivers. `DescribeEngine=0`, `DrawAResult=4` and
  `DrawBResult=4` appear in both `riva128-before` and `riva128` under
  `docs/probe/0.37.0-release-checks-2026-11-06/`. Only
  `DescribeGeneration` differs.
- No NV3 or Glide code changed in a way that could matter between
  `f03d3513`, the last RIVA build the card ran, and `55a661a2`. The
  one change to `glide_core.c` (`64dc2e2f`) converts 565 textures only for
  an engine that offers no 565. The RIVA's describe offers it (texture
  formats `1Ah`).

The "before" pattern also fits a real Voodoo 1: everything passes except
Line.

Why the HAL refuses: `v9x_r3d_describe_body` has no branch for
`v9x_d3d_engine_nv3`, so the RIVA describes engine 0 with no hardware
texture shape. `v9x_r3d_fallback` gives the NV3 no software engine either,
so every render-interface draw is refused. OpenGL's probe passes because
the ICD has its own CPU path; Glide and 3DCIF have none. This is the
RS690's 0.37.0 state, which `a3440f06` and `99c78398` fixed for that chip.
It is a missing feature, filed as
`2026-11-07-riva128-render-interface-has-no-engine.md`.

A side effect of the release checks: the WININIT rename replaced the
owner's 3dfx GLIDE2X.DLL (and GLIDE3X.DLL, if one was there) in
`C:\WINDOWS\SYSTEM`, and no copy was kept. Glide games on the owner's
Voodoo 1 now load Velocity9x's Glide, which draws nothing on this
machine. Restoring it needs the owner's 3dfx driver files.
`scripts/run-release-checks.ps1` keeps no copy of the files it replaces.

Status: explained. Closed as a regression; the gap stays open in the new
issue.
