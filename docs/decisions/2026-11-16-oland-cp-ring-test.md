# Oland: the graphics CP runs and passes Linux's ring test, first try

Date: 2026-11-16

Status: **measured, one run.** W9QI0DAD (Oland XT `1002:6610`, 2 GiB),
boot 49, on the ati Oland row (`546fd516-dirty`, DirectDraw on SI's DMA
engine) with the desktop at 1024x768x16. Step 1 of
[`../plans/ati-oland-gfx6-3d.md`](../plans/ati-oland-gfx6-3d.md).
Evidence:
[`../probe/oland-w9qi0dad-2026-11-16/sicp-run1/`](../probe/oland-w9qi0dad-2026-11-16/sicp-run1/).

## The probe

`V9XSICP.EXE` + `SICP.VXD` (`tools\diag\si_cp_win32.c`, `si_cp.asm`,
`scripts\build-si-cp-probe.ps1`), built `318dac2c-dirty`. A copy of
Mullins' `V9XCKCP` for SI, following amdgpu `gfx_v6_0.c`. What differs
from CIK is listed at the top of both files. The main points:

- RLC microcode through an indexed port: `RLC_UCODE_ADDR` (C32Ch) is
  written with each dword's index before `RLC_UCODE_DATA`. That needed a
  new path in the VxD's ucode op.
- The ME is started by an `ME_INITIALIZE` packet (contexts 8, device id
  1) plus the CE partitions at C000h/E000h, committed before the unhalt.
  SI has no `CP_RB0_BASE_HI`, `CP_DEVICE_ID`, `CP_MAX_CONTEXT` or
  `CP_ENDIAN_SWAP`.
- `SCRATCH_REG0` is a config register (8500h): the ring test is
  `SET_CONFIG_REG`.
- Submissions pad to 256 dwords with type-2 NOPs (`80000000h`), SI's
  gfx ring alignment.
- Load balancing (LBPW) is left off, the GUI-idle interrupts are turned
  off and stay off, and no gpu_init is run.

Microcode: linux-firmware `amdgpu/oland_*.bin`. PFP version 54h, CE
3Dh, ME 91h (2,144 dwords each), RLC 1 (2,048 dwords).

## Before (call 1, read-only)

As the survey found: `CP_ME_CNTL` 15000000 (three engines halted),
`RLC_CNTL` 0, `RLC_STAT` 6 (its bits are named two ways; see the correction in 2026-11-16-oland-gpu-init.md),
`GRBM_SOFT_RESET` 0, `GB_ADDR_CONFIG` 0. New here: **the BIOS leaves
coarse-grain clock gating on**, `RLC_CGCG_CGLS_CTRL` 3. gfx_v6_0 does not
touch it, so neither did this run. `CP_RB0_BASE` reads FEDCBAEF, a
power-on pattern, so the BIOS never set up a ring.

Scratch window: BAR0 + 248 MiB, GPU address `F4_0F800000`. The canary
pattern read back through BAR0.

## The run

| | |
|---|---|
| RLC | stopped, SERDES idle at once, reset pulsed, 2,048 dwords loaded, started: `RLC_CNTL` 1, `RLC_STAT` 7 |
| CP | halted, PFP/CE/ME loaded (8,480 microcode dwords written in all), `CP_RB0_CNTL` 90Bh (16 KiB ring, 4 KiB blocks) |
| Unhalt | `CP_ME_CNTL` 0; ME_INITIALIZE submission consumed (`CP_RB0_RPTR` 100h) |
| Ring test | **PASS**: `SCRATCH_REG0` DEADBEEF after 1 poll |
| Preamble | **PASS**: PREAMBLE_CNTL, CLEAR_STATE and the VGT context writes ran, then `SCRATCH_REG0` 600DCAFE after 2 polls |
| Writeback | read pointer 300h in memory, all 768 ring dwords consumed |
| While busy | `GRBM_STATUS` A0003028 (GUI active, CP busy), `CP_STAT` 800000E3 |
| After | `CP_ME_CNTL` 15000000, `RLC_CNTL` 0, `GRBM_STATUS` 3028, `GRBM_SOFT_RESET` 0 |

Two readings left over afterwards, neither explained:

- `RLC_STAT` stays at 7 after `RLC_CNTL` is cleared (bit 0). Later measured: it clears only on the RLC's soft reset (2026-11-16-oland-gpu-init.md).
- `GRBM_STATUS2` reads 100h after the run, 0 before.

The CP and RLC were put back halted and every register written was
restored, except `CP_RB0_RPTR`. The microcode stays loaded.

## Afterwards, same boot

- V9XBLTX `/random:200` at 16 bpp: 253 cases PASS, byte-exact, 136 on
  the DMA engine, no timeouts or resets (`../probe/.../bltx-after-sicp-16bpp/`).
  The DMA engine is unaffected by the CP's bring-up.
- The desktop is intact (`sicp-run1/desktop-after.png`).

## What this settles

- SI's RLC and CP run from the BIOS state with Linux's order and no
  save/restore or clear-state buffer, as amdgpu gives Oland (no power or
  clock gating flags).
- The RLC's indexed microcode port, ME_INITIALIZE, SET_CONFIG_REG and
  the 256-dword type-2 NOP padding are right for this chip.

Not settled: anything that draws. gpu_init has not run, so
`GB_ADDR_CONFIG` is still 0 and the tile modes are the BIOS's.
