# 0.61.0 release checks: 1 machines, 1 packages

Date: 2026-11-26

Status: measured on 1 machines on `23e8926d-dirty`, built from a dirty tree: a measurement, not a release check. The probes (V9XD32, V9XGLP, V9XGLIDP, V9XR3DP, V9XWPRS, V9XFBCMP) were rebuilt with that build id first. Every probe report's `Build=` line is checked against it below.

Evidence: this folder.

## Builds

All eight files by WININIT rename (no `NUL=` lines), each machine restarted
by `reset`, then every installed file's SHA-256 compared with the package:

| Machine | Package | Card | Boot | Installed |
|---|---|---|---|---|
| M-8Z30ST, Lenovo C100, GMA 900 915GM (remote help) | intel-gma | 8086:2592, 8086:2792 | 4 to 5 | 8 of 8 |

## Results

| Check | M-8Z30ST, Lenovo C100, GMA 900 915GM (remote help) |
|---|---|
| V9XSNA | NOT-RUN (no report) (Build=V9XTRACEDUMP build=23e8926d-dirty) |
| V9XFBCMP | PASS |
| V9XGLIDP | PASS failures=0 |
| V9XGLP | PASS |
| V9XR3DP | COMPLETE |
| V9XD32 ddi5 | PASS |
| V9XD32 ddi6 | PASS |
| V9XD32 default | PASS |
| V9XWPRS | PASS |

## What changed since 0.60.0

Every probe verdict compared with `0.60.0-release-checks-2026-11-21`, by machine and check.

- **M-8Z30ST, Lenovo C100, GMA 900 915GM (remote help)**: not in the 0.60.0 checks; nothing to compare.

## Windowed presents

V9XWPRS: the average DirectDraw present and OpenGL swap into a 512x384
window, in ms (DirectDraw / OpenGL). Recorded, not judged; a present many
times slower than the previous release's needs a cause.

| Machine | This run | 0.60.0 |
|---|---|---|
| M-8Z30ST, Lenovo C100, GMA 900 915GM (remote help) | 0.47 / 0.39 | - |

## 3DMark 2000

Default benchmark, frames grabbed by V9XTRACE (`3dmark2000-grab/`), progress
watched through the HAL's FlipHandled (`3dmark-beat.ini`). Counters are this
run's, from V9XTRACE snapshots before and after it.

| Machine | Run | Score | Counters |
|---|---|---|---|
| M-8Z30ST, Lenovo C100, GMA 900 915GM (remote help) | finished in 6.0 min, last FlipHandled 7572 | 1586 (`3dmark-score.png`) | draws +505589, into presented +0, refused batches +0, engine stalls +0, FilterMagSeen 0x6 |

## Not covered

- The 3dfx, ati, matrox, nvidia, s3, sis, vbe packages: no machine booted their 0.61.0 files.
