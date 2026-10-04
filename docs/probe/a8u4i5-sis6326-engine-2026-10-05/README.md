# A8U4I5: SiS 6326 2D engine under DirectDraw, 2026-10-05

- Machine: A8U4I5, boot 209, SiS 6326 card 2 (rev 0Bh).
- Driver: `sis` package build `afd2154`; `V9XDISP.DRV`, `V9XMINI.VXD` and
  `V9XHAL.DLL` deployed by WININIT rename (plain `dest=src` pairs) over the
  `c58dfe5` install, sizes confirmed 46584 / 12332 / 300544.
- Tools from `C:\V9XSIS` (package of `c58dfe5`, build id `7202853-dirty`).

| File | What |
|---|---|
| `B209-V9XBOOT.INI`, `B209-V9XHW.INI` | `enable-ok` at 640x480x16, `Acceleration=directdraw-fill-copy` |
| `B209-V9XDD.INI` | `V9XDDP` default run on the engine |
| `B209-SNAP.INI` | `V9XTRACE` after it: EngineType 6, caps 3, validated, CountBlt 6 = CountBltEngine 6 |
| `B209-gdi.INI` | `V9XGDI /auto` |
| `B209-msw-cycle.INI`, `B209-msw-depth.INI` | `V9XMSW /cycle:10`, `/depth:10` |
| `B209-dd-modestress.INI` | `V9XDDP /modestress` |
| `B209-msw-800.INI`, `B209-dd-after-switch.INI` | `/set:800x600x16`, then `V9XDDP` again |
| `B209-SNAP2.INI` | `V9XTRACE` at the end: EnableCount 24, CountBlt 12 = CountBltEngine 12, no timeouts |

The desktop was left at 800x600x16.
