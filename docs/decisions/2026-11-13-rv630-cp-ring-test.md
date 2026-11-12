# The HD 2600 XT's CP runs AMD's RV630 microcode: ME_INITIALIZE and Linux's ring test pass

Date: 2026-11-13

Status: **measured, one card, one run.** M-NZHWBR's HD 2600 XT
(`1002:9588`, RV630), boot 5, the `ati` driver of `2c6eeccb` running
DirectDraw on the DMA engine. The 3D step of
[the plan](../plans/ati-rv630-hd2600xt.md), first part. Tool:
`V9XEGCP.EXE` + `EGCP.VXD`, Cedar's CP probe, given an RV630 branch.
Captures: [`../probe/rv630-cp-2026-11-13/`](../probe/rv630-cp-2026-11-13/).

## Microcode

linux-firmware's `radeon/RV630_pfp.bin` (2,304 bytes, SHA-256
`c0f58b75...9246`) and `radeon/RV630_me.bin` (21,504 bytes,
`2a7e310a...e9e0`), fetched 2026-11-13 into `build\external\rv630\
firmware\` with `LICENSE.radeon`. The sizes are radeon_ucode.h's: PFP
576 dwords, ME three times `R600_PM4_UCODE_SIZE` (1,792). The probe read
them as `PFP.BIN` and `ME.BIN`; first dwords `00CA0400` and `00000000`.

## The RV630 branch

What differs from Evergreen's bring-up, from Linux `r600.c`:

- `r600_cp_stop` halts the ME alone. The BIOS leaves `CP_ME_CNTL` at
  `10000000h` (ME halted, PFP not), so the probe's "CP halted" gate takes
  the ME bit alone on this chip. Its first run stopped there under
  Evergreen's two-bit test (`CP-NOT-HALTED`).
- `r600_cp_load_microcode`: CP_RB_CNTL as Evergreen's, a GRBM reset of the
  CP alone, then the ME (5,376 dwords) before the PFP (576).
- `r600_cp_resume`: the CP alone in the reset pulse; no
  `CP_SEM_INCOMPLETE_TIMER_CNTL`, which R6xx does not have (left out of
  the saved list too).
- `r600_cp_start`: `ME_INITIALIZE` with second word 3 and
  `max_hw_contexts - 1` = 7 (`r600_gpu_init` gives the RV630 8), then
  `CP_ME_CNTL` FFh.

The VxD finds `1002:9588`, takes `CONFIG_MEMSIZE` in bytes beside MiB, and
reads R6xx's `MC_VM_FB_LOCATION` at 2180h. Its window is BAR0 + 241 MiB,
GPU `EF100000`, clear of the driver's DMA ring at 240 MiB.

## Result

| | |
|---|---|
| GRBM_SOFT_RESET during the pulse | `00000001` (the CP) |
| ME_INITIALIZE | consumed: `CP_RB_RPTR` reached 10h at the first poll |
| r600_ring_test | `SCRATCH_REG0` = `DEADBEEF` at the first poll, RPTR 20h |
| CP_ME_CNTL while running | `000000FF` |
| GRBM_STATUS | `00003030` before, during and after |
| After | CP_ME_CNTL back to `10000000`, every saved register put back |

The DirectDraw engine was idle and untouched by the run; the display was
not looked at.

## Not measured

- Clear state, `r600_gpu_init`'s configuration, any 3D state or draw.
- A second run, or a run while DirectDraw is drawing.
