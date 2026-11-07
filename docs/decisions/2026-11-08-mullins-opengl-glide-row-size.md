# Mullins: OpenGL and Glide on the engine, and the DRAM row size read

Date: 2026-11-08

Status: **measured, one machine, one boot.** M-JJN1ZZ's Radeon R3E
(`1002:9854`), boot 16, the whole `ati` set from `0b82e30c` (DRV, VXD,
HAL, settings page, OpenGL ICD, both Glide DLLs and 3DCIF, by WININIT
rename, each SHA-256 checked; until this boot the machine had the `vbe`
package's ICD and Glide DLLs). Follows
[the Direct3D record](2026-11-08-mullins-direct3d-hal.md). Reports in
[`../probe/m-jjn1zz-mullins-gl-glide-row-size-2026-11-08/`](../probe/m-jjn1zz-mullins-gl-glide-row-size-2026-11-08/).

## OpenGL and Glide

- **V9XGLP `Result=PASS`**, `LifetimeFailures=0`: an accelerated
  32-bit format with a 16-bit depth buffer chosen, context made current,
  clear, scissored clear, geometry and the probe's scenes as on Carrizo.
  The ICD's own log at the end of the run: `hw=116/196`
  batches/triangles, `cpu=0/0`, `hw-refused=0/0`, 73 textures created and
  none failed or lost. Everything went to the engine.
- **V9XGLIDP `Result=PASS failures=0`**. The Glide DLL bound the R3D
  engine 13 (`V9X_R3D_ABI_ENGINE_VI`); its census: `drawn=29 refused=0`,
  no surfaces lost.
- V9XTRACE after both: 84 textures placed in video memory, no idle
  timeouts, no resets, driver and HAL builds agree.
- The ICD named itself "Velocity9x Radeon R7E": `d3d_core.c` named the VI
  engine R7E whatever the chip. It now reads the Mullins identity bit and
  says "Velocity9x Radeon R3E", as the Evergreen engine's names already
  do. Not yet run on the machine.

## The DRAM row size

The read probe (`V9XCKMM`) gained `MC_FUS_DRAM0/1_BANK_ADDR_MAPPING`
(2844h, 2848h, `gmc_7_0_d.h`) and decodes them as
`gfx_v7_0_gpu_early_init` does for an APU: 2844h = 307h, 2848h = 11h, so
the four DIMM maps are 7, 0, 1 and 1. None is 11, so Linux picks a
**1 KiB row**, which is what `gen-mullins-gpu-init.ps1` and
`CIK_GB_ADDR_CONFIG_MULLINS` assumed. Nothing changes.

## GB_ADDR_CONFIG

Read on the same boot with the driver's CP running: `GB_ADDR_CONFIG`
(98F8h, the same index in `gfx_7_0_d.h` and `gfx_7_2_d.h`) still reads 0,
while `GB_TILE_MODE0` reads the driver's `00800010` and `GB_TILE_MODE8`
read back in V9XCK3D. So gpu_init's writes land and this one register
reads 0. Not explained. Nothing drawn so far depends on it: the
targets are linear and the depth buffer 1D-tiled, neither of which uses
the pipe and bank interleave it describes.

## Not measured

A game, OpenGL beyond the probe, 3DCIF, and the renamed ICD string.
