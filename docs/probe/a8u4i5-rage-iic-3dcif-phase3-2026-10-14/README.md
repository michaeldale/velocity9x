# A8U4I5: ATI3DCIF.DLL draws Cubes on the Rage IIC, 2026-10-14

Plan: [ati-3dcif-wrapper.md](../../plans/ati-3dcif-wrapper.md), Phase 3.

- Machine: A8U4I5, Rage IIC AGP. Boot 456 runs the `ati` package built
  with render interface ABI 5 (build `a8-cif-abi5`). All six driver files
  were swapped in by WININIT rename, and the driver came up `enable-ok`.
- DLL: `build\cif\ATI3DCIF.DLL`, copied into
  `C:\3DCIF\PACK\ATi 3D C InterFace\CUBES\`. Three builds ran in turn:
  - `cif-p3-1`: every textured draw was refused UNSUPPORTED.
  - `cif-p3-2`: textures follow the D3D-op rules (`cif_state.c`'s
    `texel_alpha` and `white`).
  - `cif-p3-3`: the same, plus the centre-pixel readback.

Files:
- `V9XCIF-cubes-boot456.LOG`: the three builds' runs, from the first
  attach.
  - `cif-p3-1`: `draw refused result=4` on every textured draw.
  - `cif-p3-2`: `drawn=9206 refused=0`.
  - `cif-p3-3`: `pixel before draw` / `pixel after draw` lines. The
    target's centre changes from the clear colour `4210` to the cube's
    `A800` and `AD75` in both flip buffers (`D4455280`, `D43BF280`).
- `cubes-monitor-obs-boot456.png`: Michael's OBS capture of the monitor.
  Both cubes are textured and lit as under ATI's driver. The background is
  purple-blue rather than ATI's grey: Cubes clears with the 555 value
  `4210`, which a 565 screen shows as about RGB(66, 65, 132).

The agent's own screenshots of the same runs show only the flat clear
colour. They read the GDI primary, which does not follow Cubes' flips on
this driver; under ATI's driver they did.
