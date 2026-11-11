# S3 shutdown garble on 0.39.0 (2026-11-07)

Physical evidence for
[`docs/issues/2026-11-07-s3-shutdown-garble-before-safe-screen.md`](../../issues/2026-11-07-s3-shutdown-garble-before-safe-screen.md).

| File | What |
|---|---|
| `shutdown-garble-eizo.jpg` | Eizo LCD during the 1-2 s flash on shutdown (cyan/black columns), Trio64V+ card in frame |
| `diagnostics-trio64vplus.txt` | Settings report, 0.39.0 / `186b656c`, Adapter Trio64V+ 86C765 |
| `diagnostics-trio64v2dx.txt` | Settings report, same build, Adapter Trio64V2/DX 86C775 |

Also seen every time on a second 765 card (no separate capture). Stock Win98
S3 Trio64 driver: black in that gap on the same machines.
