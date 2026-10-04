# A8U4I5: SiS 6326 3D engine, phase 2 (state), 2026-10-05

- Machine: A8U4I5, boot 210, SiS 6326 card 2 (rev 0Bh), Velocity9x `sis`
  (2D engine window enabled by the driver), desktop 800x600x16.
- Tool: `SIS3D.EXE /phase2` on `SIS2D.VXD` (build `sis2d-20261005-b`).
  State from `v9x_sis3d_build_state`, vertices from `v9x_sis3d_float_fixed`
  in `src/chipsets/sis/sis6326_3d.c`.
- Targets: 32x32 RGB565 at pitch 64, VRAM 2 MiB + 4 KiB x region; Z16
  buffers the same shape in their own region, cleared to FFFFh.

| File | Build | Scenes added |
|---|---|---|
| `SIS3D-A-FIRST.TXT` | `sis3d-20261005-p2a` | Shift{4,8,10,12,16}{TopLeft,BottomRight}, Gouraud, DepthLess, AlphaGreater, BlendSrcAlpha, BlendAdditive |
| `SIS3D-B-GOURAUD-DUMPS.TXT` | `sis3d-20261005-p2b` | GouraudX, GouraudY; every pixel of the Gouraud scenes as `*Pixels<row>` |
| `SIS3D-C-ZSCALE.TXT` | `sis3d-20261005-p2c` | DepthScale at z 0, 1.0, 1.5, 1.99999988 |
| `SIS3D-D-ZBELOW1.TXT` | `sis3d-20261005-p2d` | DepthScale with 0.99999994 in place of 0 |

`*Row<n>` lines appear only where coverage differs from the reference.
`*Colours` and `*ZValues` are value:count histograms of the read-back target
and Z buffer. `*Off` counts pixels more than one 5/6-bit step from the
reference; `*MaxError` is the largest step.

`gouraud_fit_x.py` and `gouraud_fit_prestep.py` fit the Gouraud dumps in
file B offline:

    python gouraud_fit_x.py SIS3D-B-GOURAUD-DUMPS.TXT
    python gouraud_fit_prestep.py SIS3D-B-GOURAUD-DUMPS.TXT
