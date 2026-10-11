# Voodoo4 2D engine probe, A8U4I5 boot 410, 2026-10-11

`TDFX2D.EXE` + `TDFX2D.VXD`, built by `scripts\build-tdfx-2d.ps1` from the
`vsa100-tier0` working tree on `1504b22`. Under Velocity9x `3dfx` tier-0,
640x480x16.

| File | Run |
|---|---|
| `TDFX2D-run1.TXT` | Default tests. `Result=FAIL` only because that build still counted the ROP `F0` comparison fill. |
| `TDFX2D-run2-small.TXT` | Rebuilt with `F0` reported but not counted, run with `/small`: `Result=PASS`. |

Record: [2026-10-11-voodoo4-2d-engine-fills-and-copies-pixel-exact.md](../../decisions/2026-10-11-voodoo4-2d-engine-fills-and-copies-pixel-exact.md).
