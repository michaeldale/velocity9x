# RV280 pre-merge checks: branch ati-rv280 with main merged in

Date: 2026-11-15 (checked 2026-11-16 local)

Machine: M-HNBJNV, Radeon 9250 (`1002:5960`), Windows 98 SE. Build:
merge commit `20740abb` (main at `1551795e` merged into `ati-rv280`,
whose tip `e738aa90` was Michael's plan commit on the branch).
`run-checks.ps1` passed on it. Evidence:
[`../probe/field-radeon-9250-hnbjnv-2026-11-15/premerge-boot27/`](../probe/field-radeon-9250-hnbjnv-2026-11-15/premerge-boot27/).

Main's changes since the branch point that reach a driver are
GeForce-210-only:
- the GT218 GDI fill gate on write-combining;
- the logon engine start, which only the nvidia family's INF installs.

The ATI driver code is the branch's own.

## Install

The first attempt's restart (2026-11-15, 12:24 UTC) was followed by ten
hours offline. The owner reported a power outage or shutdown, not a boot
fault.

The second attempt staged the eight files and restarted. The restart came
back after the script's wait ran out, so the boot counter was not read.
All eight installed files then matched the build by SHA-256.

## Results

| Check | Result |
|---|---|
| V9XD32 default (32 bpp) | `Failures=0`, PASS |
| V9XD32 `/bpp16` | `Failures=0`, PASS |
| V9XGLP | PASS, `Renderer=Velocity9x Radeon 9250` |
| V9XGLIDP | PASS, 0 failures |
| V9XR3DP | COMPLETE |
| 3DMark 2000 default, 1024x768x16 | 871, completed to the score window |

HAL counters across the 3DMark run (V9XTRACE snapshots):
- 893,000 draws and 38.8 million triangles;
- `M64Refused` 6 before and after, the 6 being V9XD32's;
- `EngineIdleTimeouts` and `EngineFifoTimeouts` 0.

The after-snapshot was taken a minute after the score window, with
nothing drawn in between.

All checks met the bar set before the run, so the branch was
fast-forwarded into GitLab `main`. Open with it:
- [GeomScan's band](../issues/2026-11-15-rv280-gl-geomscan-band.md);
- [placed render targets never freed](../issues/2026-11-15-placed-render-targets-never-freed.md);
- no colour key, no mipmaps, whole-surface depth fills only
  ([HAL record](2026-11-15-rv280-d3d-hal.md)).
