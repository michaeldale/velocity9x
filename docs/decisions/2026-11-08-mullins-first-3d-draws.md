# Mullins draws: Carrizo's streams with GCN 2 shaders, every probe mode as on Carrizo

Date: 2026-11-08

Status: **measured, one machine, one run per mode, one boot.** M-JJN1ZZ's
Radeon R3E (`1002:9854`), boot 14, the `ati` driver from `8e80796e`
running. Phase 6 of [the plan](../plans/ati-mullins-gfx7.md), after
[the CP ring test](2026-11-08-mullins-cp-ring-test.md). Reports in
[`../probe/m-jjn1zz-mullins-first-3d-draws-2026-11-08/`](../probe/m-jjn1zz-mullins-first-3d-draws-2026-11-08/).

## What was built

- **Shaders.** `cik_shader.c` rewrites the GCN 3 words `vi_shader.c`
  builds as GCN 2, in place: EXP and VINTRP prefixes, VOP2, VOPC and SOP1
  opcode numbers, VOP3's opcode and clamp fields, FLAT's dwordx4 load.
  Every mapping is from LLVM's AMDGPU instruction tables; anything not
  mapped refuses the whole program. Host-tested against LLVM's own SI/CI
  and VI test encodings (`92c585b9`).
- **gpu_init.** `cik_gpu_init.c` with tables generated from Linux cik.c
  and gfx_v7_0.c (`a697224f`): the CHIP_MULLINS golden tables, the
  Kabini/Kaveri/Mullins tile tables for a 1 KiB DRAM row, and
  `gfx_v7_0_constants_init`'s writes; 241 entries. Left out: a godavari
  entry at 260C0h (past the register BAR; most likely a typo for
  DB_DEBUG2, which constants_init sets anyway) and four clock-gating
  entries written through the PCIe and SMU index ports.
- **Start of stream.** `v9x_cik_3d_start`: `gfx_v7_0_cp_gfx_start`'s
  order with CI's clear state (`cik_clear_state.c`, generated from
  `clearstate_ci.h`) and raster configuration 0.
- **The probe.** `V9XCK3D`, a CIK copy of `V9XCZ3D` on the CIK CP probe's
  bring-up; its VxD's gpu_init allow-list is the 65 coalesced windows of
  the 241 entries' offsets. The draws are Carrizo's `vi_3d.c` streams
  unchanged: every register they write has the same index on CIK.

## Results

| Mode | Result |
|---|---|
| default | solid rectangle (8,4)-(40,12) in a 64x16 target: `Drew=PASS`, `InsidePixel=BF4080FF`, 0 pixels outside; the probe shaders' markers: `VsRan`, `VertexIds`, `Positions`, `PsRan` PASS |
| `/gouraud` | TL vertices from a buffer, a red ramp: `Gouraud=PASS`, 0 bad |
| `/depth` | three rectangles, LESS with writes, Z16 1D-tiled: colour and buffer PASS |
| `/texture` | a pattern mapped 1:1, point-sampled: 1024 of 1024 exact |
| `/modulate` | the texture times a diffuse colour: 1024 exact |
| `/blend` | SRC_ALPHA / ONE_MINUS_SRC_ALPHA: PASS, `BF807F00` |
| `/ztile` | the order of samples in an 8x8 Z16 tile: PASS, 0 bad |
| `/zlinear` | colour PASS, buffer `DepthBadDwords=256` of 512 |

Every draw's end-of-pipe fence landed; the CP and RLC were halted and put
back after each run. `VsRan` and `PsRan` read FAIL in every mode but the
default because only the probe shaders store the markers they check.

`/zlinear` is Carrizo's result exactly: 256 of 512 dwords not in
row-by-row order ([Carrizo's record](2026-10-29-carrizo-ddp-zero-rows.md)).
The depth block does not take that layout on either chip; Carrizo's
driver does not use it.

`GB_ADDR_CONFIG` reads 0 after the probe writes 02010001h, while
`PA_SC_FIFO_SIZE`, `SH_MEM_CONFIG` and `GB_TILE_MODE8` read back what was
written. Not explained; the draws do not show it.

## What it settles

On Mullins, Carrizo's 3D path runs with three CIK pieces: translated
shaders, CIK's gpu_init and clear state, and CIK's CP bring-up. Direct3D
on Mullins is Carrizo's `d3d_vi.c` and `engines\vi_cp.c` with those pieces
in place of the VI ones.

Not measured: the HAL, a game, 16 bpp targets, mip chains and the other
modes V9XCZ3D has, and more than one run of each.
