# Mullins (Radeon R3E, GCN 2) as the BIOS leaves it: Carrizo's state at CIK's offsets

Date: 2026-11-08

Status: **measured, one machine, one run.** M-JJN1ZZ (`W9QI0DA2`), an AMD
GX-412HC with the Radeon R3E, PCI `1002:9854` rev 01, subsystem
`1002:9854`, boot 13, desktop at 1024x768x16 under the `vbe` package with
write-combining on (`MtrrWrite=ok` in `V9XBOOT.INI`; neither INI records
the build id, and the 2026-11-07 notes put it at a dev build of
aadde512). Phase 2 of
[the plan](../plans/ati-mullins-gfx7.md). Reports in
[`../probe/m-jjn1zz-mullins-register-probe-2026-11-08/`](../probe/m-jjn1zz-mullins-register-probe-2026-11-08/).

## The probe

`CKMM.VXD` + `V9XCKMM.EXE` (`scripts/build-cik-mmio-probe.ps1`), a copy of
the Carrizo read probe (`tools/diag/carrizo_mmio_probe*`). Before copying,
every offset the Carrizo probe reads was looked up in Linux's CIK headers
(`gfx_7_2_d.h`, `oss_2_0_d.h`, `gmc_7_1_d.h`, `dce_8_0_d.h`, `bif_4_1_d.h`)
against the VI ones it was written from. The names match at the same
offsets for SRBM, the VM and MC registers, `CONFIG_MEMSIZE`, GRBM, the CP
status and ring registers, `GB_*`, both SDMA blocks, `SCRATCH_REG0`,
`GRBM_GFX_INDEX` and the first display controller. These differ:

| | VI (Carrizo) | CIK (Mullins) |
|---|---|---|
| RLC | `RLC_CNTL` 3B000h | C300h, `RLC_GPM_STAT` C400h, `RLC_PG_CNTL` C40Ch |
| CP microcode ports | 3E050h- | C150h-C17Fh |
| 600h | `MP0PUB_IND_INDEX`, the SMU index | `GPIOPAD_SW_INT_STAT` |
| SMU index | MP0PUB at 600h | `SMC_IND_INDEX_0` at 200h |
| `SDMA0_ID`/`_VERSION`, `GFX_DOORBELL` | D070h, D074h, D248h | not present |
| Second display controller | +800h | +C00h |

So the copy drops the SMU header read (it would have written a GPIO
register), moves the RLC windows, drops the VI-only SDMA registers and
reads two controllers at 0 and +C00h. It writes nothing.

## What it read

- BAR0 `E000000C` (64-bit prefetchable), BAR2 `F000000C`, BAR4 I/O 3000h,
  BAR5 `F0C00000`. Windows' Config Manager allocation for the devnode
  gives BAR0 as `E0000000-EFFFFFFF`: **256 MiB, the whole carve-out**.
- `CONFIG_MEMSIZE` 100h: **256 MiB**. `MC_VM_FB_LOCATION` `F40FF400`: the
  carve-out at GPU `F4_00000000-F4_0FFFFFFF`, as on Carrizo.
  `MC_VM_FB_OFFSET` 43Ch: system address `1_0F000000`, above 4 GiB.
- D1 scans `F4_00000000` at 1024x768x16, pitch 1024, linear: **the VBE
  scanout is the FB start, LFB offset 0.** D2 is disabled.
- Every engine halted, none in reset, no microcode: CP CE/PFP/ME halted,
  MEC ME1 halted, RLC off, SDMA0 and SDMA1 halted, idle, rings off.
- `RLC_GPM_STAT` 6: GFX powered and clocked; power gating off.
- `GB_ADDR_CONFIG` reads 0 (Linux's Mullins value is set by gfx init,
  not the BIOS). `CC_RB_BACKEND_DISABLE` 1, `GB_TILE_MODE0` `06C03788`.

## What it settles

Mullins starts where Carrizo did: a 256 MiB carve-out behind BAR0, the
VBE desktop at its start, and every engine waiting for microcode. The
Carrizo SDMA path's assumptions hold: BAR0 256 MiB aligned and reaching
240 MiB, the carve-out's GPU address a fixed high half (F4h), a
`CONFIG_MEMSIZE` at the mini-VDD's 256 MiB floor.

Not settled: the SMU's state (not read, and CIK's microcode does not
need it), and anything about the GFX7 CP.
