# Direct3D on Mullins through the HAL: V9XD32 passes at 32 and 16 bpp on the GFX7 CP

Date: 2026-11-08

Status: **measured, one machine, one boot.** M-JJN1ZZ's Radeon R3E
(`1002:9854`), boot 15, the `ati` DRV, VXD and HAL from `32318ecc`
(installed by WININIT rename, each file's SHA-256 checked). Phase 6 of
[the plan](../plans/ati-mullins-gfx7.md), after
[the first draws](2026-11-08-mullins-first-3d-draws.md). Reports in
[`../probe/m-jjn1zz-mullins-direct3d-hal-2026-11-08/`](../probe/m-jjn1zz-mullins-direct3d-hal-2026-11-08/).

## The change

`32318ecc`: `engines\vi_cp.c` picks a bring-up table from
`V9X_DD_ENGINE_CAP_VI_MULLINS` when the CP starts (CIK's RLC and CP
microcode ports, masks and ring control, no gfx doorbell, Mullins'
graphics microcode, `v9x_cik_gpu_init_ops`, `v9x_cik_3d_start`), and on
Mullins every shader, the per-draw built pixel shader in `d3d_vi.c`
included, goes through `v9x_cik_shader_from_vi`. `mullins_hw16.c` stamps
`V9X_DD_ENGINE_CAP_D3D` beside it. Carrizo's table holds the values the
code had before.

## Boot 15

- `V9XBOOT.INI`: `Stage=enable-ok`. `V9XHW.INI`:
  `Direct3D=hardware-vi-cp`, `Direct3DMode=hardware`,
  `EngineCaps=10000017`.
- **V9XD32**, 1024x768x32: `Failures=0`, `Result=PASS`. T12 (two texture
  stages) skipped, one stage offered as on Carrizo; T18 no T&L device and
  T19 no pixel shaders, as on Carrizo. **`/bpp16`**: the same,
  `Result=PASS`.
- V9XTRACE after the 32 bpp run: 1,602 batches and 92,861 triangles to the
  engine (`R3dListSinkBatches`, `R3dListTrianglesIn`),
  `BatchesEngineRefused=1` (`M64Policy11`), `TrianglesDeclined=0`,
  `EngineIdleTimeouts=0`, `EngineResets=0`, 11 textures placed in video
  memory. HAL and driver builds agree.
- V9XDDP: fill, copy, the four overlap cases, colour-key fill, its
  Direct3D triangle and 4444 texture all pass; 20 flips in 326 ms.
  `V9XGDI /auto` PASS at 1024x768x32.

## Not measured

- A game or a benchmark: none is installed on M-JJN1ZZ.
- OpenGL and Glide on the engine (Carrizo's ICD paths); not run.
- What `M64Policy11` refused.
- The screen by eye.
