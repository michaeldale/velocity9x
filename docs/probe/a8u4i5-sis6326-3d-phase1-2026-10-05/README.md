# A8U4I5: SiS 6326 3D engine, phase 1 (first triangle), 2026-10-05

- Machine: A8U4I5, boot 210, SiS 6326 card 2 (rev 0Bh), Velocity9x `sis`
  (2D engine window enabled by the driver), desktop 800x600x16.
- Tool: `SIS3D.EXE` on `SIS2D.VXD` (build `sis2d-20261005-b`, which adds the
  wait-until-set op). Register values from `src/chipsets/sis/sis6326_3d.c`.
- Targets: 32x32 RGB565 at pitch 64, VRAM 2 MiB + 4 KiB x region, guarded
  with A5A5h. Flat green (ARGB FF00FF00h, 07E0h).

| File | Build | Reference the probe compared against | Shots |
|---|---|---|---|
| `SIS3D-A-CENTRE-REFERENCE.TXT` | `sis3d-20261005-a` | pixel centre (x+0.5, y+0.5) | Right/Left x direction 0/1 |
| `SIS3D-B-TIES.TXT` | `sis3d-20261005-b` | corner (x, y), strict | the four above, TieTopLeft, TieBottomRight |
| `SIS3D-C-SHIFTED.TXT` | `sis3d-20261005-c` | corner (x, y), strict | the six above, ShiftTopLeft, ShiftBottomRight |

Each `*Row<n>` line is `expected L-R/count actual L-R/count`, with `DIFF`
where the row differs. The decision record fits the tie rules offline from
these spans.
