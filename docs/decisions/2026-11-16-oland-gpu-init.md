# Oland: Linux's gpu_init runs and reads back, except two registers that read back partly

Date: 2026-11-16

Status: **measured, two runs.** W9QI0DAD (Oland XT `1002:6610`), boot 49,
ati Oland row with DirectDraw on SI's DMA engine, desktop 1024x768x16.
Step 2, first half, of
[`../plans/ati-oland-gfx6-3d.md`](../plans/ati-oland-gfx6-3d.md).
Evidence:
[`../probe/oland-w9qi0dad-2026-11-16/sicp-gpuinit/`](../probe/oland-w9qi0dad-2026-11-16/sicp-gpuinit/)
and `sicp-gpuinit2/`.

## The list

`v9x_si_gpu_init_ops` (`src\chipsets\ati\si\si_gpu_init.c`), 144 entries
in Linux's order:

- **Golden registers.** amdgpu `si.c`'s `oland_golden_registers`,
  `oland_golden_rlc_registers` and `oland_mgcg_cgcg_init`, generated into
  `si_gpu_init_tables.c` by `scripts\gen-oland-gpu-init.ps1`. Kept: the
  graphics entries (16, 6 and 60). Left out and named in the generated
  file: 32 entries for display, audio, the memory controller, VM, XDMA,
  the low indexed ports, and `CGTT_CP_CLK_CTRL`, which sits at C164h next
  to the CP's microcode ports.
- **Tile modes.** `gfx_v6_0_tiling_mode_table_init`'s CHIP_OLAND table,
  resolved through `gfx_v6_0.c`'s own field macros and `gfx_7_2_enum.h`.
  It is generated for a 4 KiB DRAM row: `MC_ARB_RAMCFG` 000012A1h has
  NOOFCOLS 2.
- **`gfx_v6_0_constants_init` for Oland.**
  - `GB_ADDR_CONFIG` 22010002h (Verde's golden value with ROW_SIZE 2),
    copied to HDP and both DMA engines.
  - `setup_rb`'s raster config (RB_YSEL, 80h, both RBs on), and
    `setup_spi`'s bit-0 clear.
  - The queue and FIFO thresholds and the remaining constants.
  - Left out: interrupts, BIF_FB_EN (already 3), DMIF (display), and
    `setup_tcc`, which writes only when a TCC is fused off. Both TCC fuse
    registers read 0 here, so it would have written nothing.

`tests\host\test_si_gpu.c` pins the count, the address-config values, tile
modes 0, 5 and 31, the raster config, setup_spi's mask and the FIFO
sizes. It also checks that every entry lies inside SICP.VXD's table
windows: the graphics config space less `GRBM_SOFT_RESET`, the RLC less
its microcode port, the context registers, and the three
address-configuration copies.

## The runs

`V9XSICP /gpuinit` runs the list as one table op before `rlc_resume`, as
Linux's hw_init does, then the step-1 bring-up. Both runs:
`Result=COMPLETE`, ring test and preamble **PASS**.

Read back after the preamble:

| | Read | Wanted |
|---|---|---|
| Tile modes 0-31 | all 32 as written | |
| `PA_SC_RASTER_CONFIG` | 80h | 80h |
| `PA_SC_FIFO_SIZE` | 98181020h | the same |
| `GRBM_CNTL` | FFh | FFh |
| `HDP_ADDR_CONFIG` | 22010002h | 22010002h |
| `SPI_STATIC_THREAD_MGMT_3` | FFFEh | bit 0 cleared |
| `CGTS_TCC_DISABLE`, `_USER_` | 0, 0 | (read only) |
| **`GB_ADDR_CONFIG`** | **20000000h** | 22010002h |
| **`DMA0_TILING_CONFIG`** | **0** | 22010002h |

The second run read the two odd registers straight after the table op,
before the RLC ran. They gave the same values (20000000h, 0), as did
`GB_ADDR_CONFIG` read again once the RLC had started. So nothing changes
them afterwards: on this chip they read back only partly, `GB_ADDR_CONFIG`
showing only ROW_SIZE (29:28). Mullins' `GB_ADDR_CONFIG` read 0 after its
BIOS (2026-11-08), which may be the same effect. Whether the hardware
holds the written value can only be shown by a tiled draw or a tiled DMA
copy. Nothing yet uses tiling, so neither is a blocker.

## RLC_STAT, corrected

The step-1 record read `RLC_STAT` 6 as "clock and power on", from
`sid.h`'s names (`RLC_BUSY_STATUS`, `GFX_POWER_STATUS`,
`GFX_CLOCK_STATUS`, `GFX_LS_STATUS`, bits 0-3), which gfx_v6_0 waits on.
amdgpu's `gfx_6_0_sh_mask.h` names the same bits `RLC_BUSY`,
`RLC_GPM_BUSY` and `RLC_SPM_BUSY`. The two headers disagree, and which
reading is right is not settled.

What was measured: 6 after the RLC's soft reset, 7 once it runs, and
still 7 after `RLC_CNTL` is cleared, until the next reset. That is why the
idle wait before the reset (Linux's, mask Fh for 6) timed out on every run
after the first. Linux carries on after the timeout and so did the probe;
the ring test was unaffected.

Also seen: `SRBM_STATUS` 20000AC0h after the first `/gpuinit` run
(MCB_BUSY and MCC_BUSY, the memory controller), 200000C0h on other
reads. Read once, not followed up.

## Next

The clear state (`si_cs_data`, `clearstate_si.h`) in the preamble, and
the first draws. They need GCN 1 shaders, so step 3 of the plan (the
encoder against the SI ISA guide) comes in with them.
