# RV630: a DMA copy stalls at 3DMark 2000's start; the engine is given up and Direct3D refused

Date: 2026-11-13
Status: **open.** Blocks 3DMark 2000 (and any program that makes the
same copy) on the HD 2600 XT row.

Machine: M-NZHWBR, Radeon HD 2600 XT (`1002:9588`), the `ati` driver of
`4080c5b2` (Direct3D through the CP, DirectDraw fill and copy on the DMA
engine). Captures: [`../probe/rv630-d3d-2026-11-13/3dmark2000/`](../probe/rv630-d3d-2026-11-13/3dmark2000/).

## Symptom

3DMark 2000's default run (1024x768x16, triple buffer, its saved
settings) stops at "3DMark could not initialize your 3D Accelerator ...
CreateDevice : DDERR_GENERIC". The HAL's counters say why:
`EngineIdleTimeouts=1`, `EngineFlags` 1 to 8 (the engine invalidated),
`D3dContextRejects=1`. With no valid engine `v9x_d3d_engine()` answers
none and the context is refused.

The timeout is the DMA engine's (`V9XR6DMA.TXT`; no CP record, and the CP
had not run in boot 11). It is the first operation after the mode's
validation fence (`Fence=1`, `Sequence=2`): a copy from the screen (pitch
2048) into an offscreen surface of pitch 840 bytes, one COPY a row of
`D2h` dwords (420 pixels), 176 rows, `B40h` bytes of ring. The first
two rows completed; `DMA_RB_RPTR` stopped at `68h`, inside the third row's
packet (dwords 24-27: `300000D2 E01A3460 E0096A5C 0`); `DMA_STATUS_REG`
`44809042` (DMA_IDLE clear; the probe read `44C83D57` idle);
`GRBM_STATUS B030`, `SRBM_STATUS 200000C0`.

## Reproduced

Boot 10 and, after a `reset`, boot 11: the same packets, addresses and
read pointer, so it is deterministic.

## What the evidence says and does not

- The rows' source addresses are dword aligned but not 8-byte aligned
  (`...5A5C`, `...625C`, `...6A5C`); the destinations are 8-byte aligned
  (`...2DD0`, `...3118`, `...3460`). The DMA write probe never measured
  unaligned addresses or odd dword counts above one
  ([record](../decisions/2026-11-13-rv630-dma-write-probe.md), "Not
  measured"), so its rules do not cover this copy. Why the third row and
  not the first two is not explained by alignment: all three share it.
- Not the Direct3D work: no CP submission in boot 11 before the stall.
- Not tested: whether the 0.48.0-era claim (`2c6eeccb`) stalls the same
  way, which would need that driver reinstalled.

## Leads

- A V9XRVDMA run of exactly this plan (screen to a pitch-840 surface,
  these offsets), then varying source alignment, destination pitch and
  row count, to find the rule.
- Linux `r600_copy_dma` copies only page-aligned blocks; whether the
  engine has a limit Linux never meets.
